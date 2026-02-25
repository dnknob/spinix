#include <arch/x86_64/atomic.h>

#include <core/mutex.h>
#include <core/scheduler.h>

#include <mm/heap.h>

static inline void cpu_pause(void)
{
    __asm__ volatile("pause" ::: "memory");
}

static inline void queue_lock_acquire(volatile uint32_t *lock)
{
    while (atomic_bit_test_and_set_32(lock, 0)) {
        cpu_pause();
    }
}

static inline void queue_lock_release(volatile uint32_t *lock)
{
    atomic_store_32(lock, 0);
}

void mutex_init(mutex_t *mutex)
{
    atomic_store_64(&mutex->entries, 0);
    atomic_store_64(&mutex->exits, 0);
    atomic_store_32((uint32_t*)&mutex->queue_lock, 0);
    mutex->wait_queue = NULL;
    mutex->owner = NULL;
}

void mutex_lock(mutex_t *mutex)
{
    uint64_t current_exits = atomic_load_acquire_64(&mutex->exits);
    uint64_t current_entries = atomic_load_acquire_64(&mutex->entries);
    
    if (current_exits == current_entries) {
        /* Lock might be free, try to grab it */
        uint64_t expected = current_entries;
        if (atomic_compare_exchange_64(&mutex->entries, &expected, current_entries + 1)) {
            /* Successfully acquired! */
            mutex->owner = get_current_task();
            barrier();  /* Prevent critical section from moving up */
            return;
        }
    }

    uint64_t my_ticket = atomic_fetch_add_64(&mutex->entries, 1);

    if (atomic_load_acquire_64(&mutex->exits) == my_ticket) {
        mutex->owner = get_current_task();
        barrier();  /* Prevent critical section from moving up */
        return;  /* Lock acquired! */
    }

    tcb_t *task = get_current_task();
    task->wait_reason = my_ticket;
    task->wait_data = NULL;

    queue_lock_acquire(&mutex->queue_lock);

    if (mutex->wait_queue == NULL) {
        mutex->wait_queue = task;
    } else {
        /* Find tail and append */
        tcb_t *tail = mutex->wait_queue;
        while (tail->wait_data != NULL) {
            tail = (tcb_t *)tail->wait_data;
        }
        tail->wait_data = task;
    }
    task->wait_queue = (void *)mutex;  /* Remember which mutex we're waiting on */

    queue_lock_release(&mutex->queue_lock);

    while (atomic_load_acquire_64(&mutex->exits) != my_ticket) {
        /*
         * Block ourselves with TASK_STATE_WAITING_LOCK
         * The scheduler will put us to sleep and run other tasks
         */
        block_task(TASK_STATE_WAITING_LOCK);
        
    }

    queue_lock_acquire(&mutex->queue_lock);

    tcb_t **prev = &mutex->wait_queue;
    while (*prev != NULL) {
        if (*prev == task) {
            *prev = (tcb_t *)task->wait_data;
            break;
        }
        prev = (tcb_t **)&((*prev)->wait_data);
    }

    task->wait_queue = NULL;
    task->wait_data = NULL;

    queue_lock_release(&mutex->queue_lock);

    mutex->owner = task;
    barrier();  /* Prevent critical section from moving up */
}

bool mutex_try_lock(mutex_t *mutex)
{
    uint64_t current_exits = atomic_load_acquire_64(&mutex->exits);
    uint64_t current_entries = atomic_load_acquire_64(&mutex->entries);

    if (current_exits != current_entries) {
        return false;  /* Lock is held */
    }

    uint64_t expected = current_entries;
    if (atomic_compare_exchange_64(&mutex->entries, &expected, current_entries + 1)) {
        mutex->owner = get_current_task();
        barrier();  /* Prevent critical section from moving up */
        return true;
    }

    return false;  /* Someone else got it */
}

void mutex_unlock(mutex_t *mutex)
{
    /* Clear owner before releasing */
    barrier();  /* Prevent critical section from moving down */
    mutex->owner = NULL;

    uint64_t current_exits = atomic_load_64(&mutex->exits);
    uint64_t next_ticket = current_exits + 1;

    atomic_store_release_64(&mutex->exits, next_ticket);

    queue_lock_acquire(&mutex->queue_lock);

    tcb_t *waiter = mutex->wait_queue;
    while (waiter != NULL) {
        if (waiter->wait_reason == next_ticket) {
            unblock_task(waiter);
            break;
        }
        waiter = (tcb_t *)waiter->wait_data;
    }

    queue_lock_release(&mutex->queue_lock);
}

bool mutex_is_locked(mutex_t *mutex)
{
    uint64_t entries = atomic_load_acquire_64(&mutex->entries);
    uint64_t exits = atomic_load_acquire_64(&mutex->exits);
    return entries != exits;
}

struct tcb *mutex_get_owner(mutex_t *mutex)
{
    return mutex->owner;
}
