#include <arch/x86_64/switch.h>
#include <arch/x86_64/tsc.h>
#include <arch/x86_64/io.h>
#include <arch/x86_64/apic.h>
#include <arch/x86_64/idt.h>

#include <core/scheduler.h>
#include <core/proc.h>
#include <core/spinlock.h>

#include <mm/heap.h>
#include <mm/paging.h>

#include <video/printk.h>

#include <klibc/string.h>

tcb_t *current_task = NULL;

static tcb_t *ready_queue_heads[NUM_PRIORITY_QUEUES] = {NULL};
static tcb_t *ready_queue_tails[NUM_PRIORITY_QUEUES] = {NULL};
static uint8_t ready_queue_bitmap = 0;  /* Bitmap: bit set = queue has tasks */

static tcb_t *sleeping_tasks = NULL;

static tcb_t *terminated_tasks = NULL;

static tcb_t *idle_task = NULL;

static uint64_t next_tid = 1;

static int scheduler_lock_count = 0;

static int postpone_task_switches = 0;
static int task_switch_postponed = 0;

static uint64_t time_since_boot_ns = 0;
static uint64_t last_tsc = 0;

static uint64_t time_slice_remaining_ns = 0;

static int scheduler_initialized = 0;

static sched_stats_t sched_stats = {0};

static spinlock_irq_t scheduler_data_lock = SPINLOCK_IRQ_INIT;

bool scheduler_is_initialized(void) {
    return scheduler_initialized;
}

static inline void cli(void) {
    __asm__ volatile("cli" ::: "memory");
}

static inline void sti(void) {
    __asm__ volatile("sti" ::: "memory");
}

static inline int priority_to_queue(uint8_t priority) {
    return priority >> 5;  /* Divide by 32 */
}

static inline int find_highest_priority_queue(void) {
    if (ready_queue_bitmap == 0)
        return -1;

    for (int i = NUM_PRIORITY_QUEUES - 1; i >= 0; i--) {
        if (ready_queue_bitmap & (1 << i))
            return i;
    }
    return -1;
}

static inline uint64_t calculate_time_slice(uint8_t priority) {
    /* Higher priority = longer time slices
     * Priority 0-31:   5ms
     * Priority 32-63:  7ms
     * Priority 64-95:  10ms
     * Priority 96-127: 12ms
     * Priority 128-159: 15ms
     * Priority 160-191: 20ms
     * Priority 192-223: 25ms
     * Priority 224-255: 30ms
     */
    static const uint64_t time_slices_ms[] = {
        5, 7, 10, 12, 15, 20, 25, 30
    };

    int queue = priority_to_queue(priority);
    return time_slices_ms[queue] * 1000000ULL;  /* Convert to nanoseconds */
}

static void task_exit_wrapper(void) {
    terminate_task();
    for (;;) __asm__("hlt");
}

static void update_time_accounting(void) {
    uint64_t current_tsc = rdtsc();
    uint64_t tsc_delta = current_tsc - last_tsc;
    uint64_t ns_elapsed = tsc_to_ns(tsc_delta);

    last_tsc = current_tsc;
    time_since_boot_ns += ns_elapsed;

    if (current_task != NULL) {
        current_task->time_used_ns += ns_elapsed;
        current_task->last_run_time_ns = time_since_boot_ns;

        if (current_task == idle_task) {
            sched_stats.idle_time_ns += ns_elapsed;
        }
    }
}

void lock_scheduler(void) {
    scheduler_lock_count++;
    postpone_task_switches++;
}

void unlock_scheduler(void) {
    postpone_task_switches--;

    int should_schedule = (postpone_task_switches == 0 && task_switch_postponed);

    scheduler_lock_count--;

    if (should_schedule) {
        task_switch_postponed = 0;
        schedule();
    }
}

static void add_to_ready_queue(tcb_t *task) {
    /* NOTE: Caller must hold scheduler_data_lock */
    task->next = NULL;
    task->state = TASK_STATE_READY;
    task->wait_ticks = 0;  /* Reset aging counter */

    int queue_idx = priority_to_queue(task->priority);

    if (ready_queue_heads[queue_idx] == NULL) {
        ready_queue_heads[queue_idx] = task;
        ready_queue_tails[queue_idx] = task;
        ready_queue_bitmap |= (1 << queue_idx);  /* Mark queue as non-empty */
    } else {
        ready_queue_tails[queue_idx]->next = task;
        ready_queue_tails[queue_idx] = task;
    }
}

static tcb_t *remove_from_ready_queue(void) {
    /* NOTE: Caller must hold scheduler_data_lock */
    int queue_idx = find_highest_priority_queue();
    if (queue_idx < 0)
        return NULL;

    tcb_t *task = ready_queue_heads[queue_idx];
    ready_queue_heads[queue_idx] = task->next;

    if (ready_queue_heads[queue_idx] == NULL) {
        ready_queue_tails[queue_idx] = NULL;
        ready_queue_bitmap &= ~(1 << queue_idx);  /* Mark queue as empty */
    }

    task->next = NULL;
    return task;
}

static void age_waiting_tasks(void) {
    /* NOTE: Caller must hold scheduler_data_lock */
    for (int i = 0; i < NUM_PRIORITY_QUEUES; i++) {
        tcb_t *task = ready_queue_heads[i];
        while (task != NULL) {
            task->wait_ticks++;

            if (task->wait_ticks >= AGING_THRESHOLD_TICKS &&
                task->priority < 255 - AGING_BOOST_AMOUNT) {

                int old_queue = i;
                task->priority += AGING_BOOST_AMOUNT;
                int new_queue = priority_to_queue(task->priority);

                if (new_queue != old_queue) {
                    /* Remove from current queue */
                    tcb_t **prev = &ready_queue_heads[old_queue];
                    tcb_t *curr = ready_queue_heads[old_queue];

                    while (curr != NULL) {
                        if (curr == task) {
                            *prev = curr->next;
                            if (ready_queue_tails[old_queue] == task)
                                ready_queue_tails[old_queue] = (prev == &ready_queue_heads[old_queue]) ? NULL : (tcb_t*)prev;
                            if (ready_queue_heads[old_queue] == NULL)
                                ready_queue_bitmap &= ~(1 << old_queue);
                            break;
                        }
                        prev = &curr->next;
                        curr = curr->next;
                    }

                    task->wait_ticks = 0;
                    add_to_ready_queue(task);
                    sched_stats.aging_boosts++;

                    break;  /* Only age one task per queue per tick */
                }
            }

            task = task->next;
        }
    }
}

extern void task_startup_wrapper(void);

tcb_t *create_kernel_task(void (*entry_point)(void), const char *name, uint8_t priority) {
    /* Allocate TCB */
    tcb_t *task = (tcb_t *)kmalloc(sizeof(tcb_t));
    if (task == NULL) {
        printk("sched: failed to allocate TCB\n");
        return NULL;
    }

    memset(task, 0, sizeof(tcb_t));

    void *stack = kmalloc(KERNEL_STACK_SIZE);
    if (stack == NULL) {
        printk("sched: failed to allocate kernel stack\n");
        kfree(task);
        return NULL;
    }

    task->tid = next_tid++;
    strncpy(task->name, name, sizeof(task->name) - 1);
    task->name[sizeof(task->name) - 1] = '\0';

    task->kernel_stack_base = (uint64_t)stack;
    task->kernel_stack_size = KERNEL_STACK_SIZE;
    task->cr3 = paging_get_pml4();  /* Use kernel's page table for now */

    task->state = TASK_STATE_READY;
    task->priority = priority;
    task->base_priority = priority;
    task->next = NULL;

    task->time_used_ns = 0;
    task->sleep_expiry_ns = 0;
    task->wait_ticks = 0;
    task->switch_count = 0;
    task->last_run_time_ns = 0;

    task->owner_proc = NULL;        /* Will be set by proc manager */
    task->wait_queue = NULL;        /* not waiting on anything yet */
    task->wait_reason = 0;
    task->wait_data = NULL;
    task->pending_signals = 0;      /* no pending signals */
    task->signal_pending = false;
    task->thread_errno = 0;         /* no error */
    task->preempt_count = 0;        /* Never preempted yet */
    task->yield_count = 0;          /* Never yielded yet */
    task->wakeup_count = 0;         /* Never woken yet */

    uint64_t *stack_top = (uint64_t *)((uint8_t *)stack + KERNEL_STACK_SIZE);

    stack_top--;
    *stack_top = (uint64_t)task_exit_wrapper;

    stack_top--;
    *stack_top = (uint64_t)entry_point;

    stack_top--;
    *stack_top = (uint64_t)task_startup_wrapper;

    stack_top--; *stack_top = 0;  /* R15 */
    stack_top--; *stack_top = 0;  /* R14 */
    stack_top--; *stack_top = 0;  /* R13 */
    stack_top--; *stack_top = 0;  /* R12 */
    stack_top--; *stack_top = 0;  /* RBX */
    stack_top--; *stack_top = 0;  /* RBP */

    task->rsp = (uint64_t)stack_top;

    lock_scheduler();
    spinlock_irq_acquire(&scheduler_data_lock);
    add_to_ready_queue(task);
    spinlock_irq_release(&scheduler_data_lock);
    unlock_scheduler();

    return task;
}

void schedule(void) {
    /* Check if task switches are postponed */
    if (postpone_task_switches != 0) {
        task_switch_postponed = 1;
        return;
    }

    update_time_accounting();

    spinlock_irq_acquire(&scheduler_data_lock);

    tcb_t *next_task = remove_from_ready_queue();

    if (next_task == NULL) {
        if (current_task == idle_task && current_task->state == TASK_STATE_RUNNING) {
            /* Already running idle task, just continue */
            time_slice_remaining_ns = 0;  /* no time slice for idle */
            spinlock_irq_release(&scheduler_data_lock);
            return;
        }
        next_task = idle_task;
        time_slice_remaining_ns = 0;  /* no time slice for idle */
    } else {
        /* Set dynamic time slice based on priority */
        time_slice_remaining_ns = calculate_time_slice(next_task->priority);
        next_task->switch_count++;
        sched_stats.total_switches++;
    }

    if (current_task != NULL && current_task->state == TASK_STATE_RUNNING) {
        /* Don't re-queue idle task */
        if (current_task != idle_task) {
            /* NEW: Track preemption */
            current_task->preempt_count++;

            /* Reset priority if it was boosted by aging */
            if (current_task->priority > current_task->base_priority) {
                current_task->priority = current_task->base_priority;
            }
            add_to_ready_queue(current_task);
        }
    }

    next_task->state = TASK_STATE_RUNNING;

    spinlock_irq_release(&scheduler_data_lock);

    switch_to_task(next_task);
}

void yield(void) {
    lock_scheduler();

    if (current_task != NULL) {
        current_task->yield_count++;
    }

    schedule();
    unlock_scheduler();
}

void block_task(uint8_t reason) {
    lock_scheduler();

    if (current_task != NULL) {
        current_task->state = reason;
    }

    schedule();
    unlock_scheduler();
}

void unblock_task(tcb_t *task) {
    if (task == NULL) {
        return;
    }

    lock_scheduler();
    spinlock_irq_acquire(&scheduler_data_lock);

    task->wakeup_count++;

    if (ready_queue_bitmap == 0 && current_task == idle_task) {
        /* Preempt idle task immediately */
        task->state = TASK_STATE_RUNNING;
        spinlock_irq_release(&scheduler_data_lock);
        switch_to_task(task);
    } else {
        /* Add to ready queue - will be picked up by next schedule() */
        add_to_ready_queue(task);
        spinlock_irq_release(&scheduler_data_lock);
    }

    unlock_scheduler();
}

void nano_sleep_until(uint64_t wake_time_ns) {
    if (wake_time_ns <= time_since_boot_ns) {
        return;  // Already past wake time
    }

    lock_scheduler();
    spinlock_irq_acquire(&scheduler_data_lock);

    current_task->sleep_expiry_ns = wake_time_ns;

    tcb_t **insert_point = &sleeping_tasks;
    while (*insert_point != NULL && (*insert_point)->sleep_expiry_ns <= wake_time_ns) {
        insert_point = &(*insert_point)->next;
    }

    current_task->next = *insert_point;
    *insert_point = current_task;

    spinlock_irq_release(&scheduler_data_lock);
    unlock_scheduler();

    block_task(TASK_STATE_SLEEPING);
}

void nano_sleep(uint64_t nanoseconds) {
    nano_sleep_until(time_since_boot_ns + nanoseconds);
}

void sleep_ms(uint64_t milliseconds) {
    nano_sleep(milliseconds * 1000000ULL);
}

int nano_sleep_interruptible(uint64_t nanoseconds) {
    if (nanoseconds <= 0) {
        return 0;
    }

    tcb_t *task = get_current_task();
    if (task == NULL) {
        return -1;
    }

    if (task->pending_signals != 0) {
        return -1;  /* Interrupted by signal */
    }

    uint64_t wake_time = get_time_since_boot_ns() + nanoseconds;

    lock_scheduler();
    spinlock_irq_acquire(&scheduler_data_lock);

    task->sleep_expiry_ns = wake_time;
    task->state = TASK_STATE_INTERRUPTIBLE;

    tcb_t **insert_point = &sleeping_tasks;
    while (*insert_point != NULL && (*insert_point)->sleep_expiry_ns <= wake_time) {
        insert_point = &(*insert_point)->next;
    }

    task->next = *insert_point;
    *insert_point = task;

    spinlock_irq_release(&scheduler_data_lock);
    unlock_scheduler();

    block_task(TASK_STATE_INTERRUPTIBLE);

    if (task->pending_signals != 0) {
        return -1;  /* Interrupted */
    }

    return 0;  /* Completed normally */
}

void task_wake_interruptible(tcb_t *task) {
    if (task == NULL) return;

    lock_scheduler();
    spinlock_irq_acquire(&scheduler_data_lock);

    if (task->state == TASK_STATE_INTERRUPTIBLE) {
        /* Remove from sleeping tasks queue */
        tcb_t **prev = &sleeping_tasks;
        tcb_t *curr = sleeping_tasks;

        while (curr != NULL) {
            if (curr == task) {
                *prev = curr->next;
                curr->next = NULL;
                break;
            }
            prev = &curr->next;
            curr = curr->next;
        }

        task->wakeup_count++;
        spinlock_irq_release(&scheduler_data_lock);
        unblock_task(task);
    } else {
        spinlock_irq_release(&scheduler_data_lock);
    }

    unlock_scheduler();
}

uint64_t get_time_since_boot_ns(void) {
    update_time_accounting();
    return time_since_boot_ns;
}

void scheduler_timer_tick(void) {
    if (!scheduler_initialized) return;
    update_time_accounting();

    spinlock_irq_acquire(&scheduler_data_lock);
    sched_stats.total_ticks++;

    tcb_t **prev_ptr = &sleeping_tasks;
    tcb_t *task = sleeping_tasks;

    while (task != NULL && task->sleep_expiry_ns <= time_since_boot_ns) {
        tcb_t *next = task->next;
        *prev_ptr = next;
        task->next = NULL;

        if (task->state == TASK_STATE_SLEEPING || task->state == TASK_STATE_INTERRUPTIBLE) {
            task->state = TASK_STATE_READY;
            add_to_ready_queue(task);
        }

        task = next;
    }

    if (sched_stats.total_ticks % 10 == 0) {  /* Every ~100ms at 100Hz */
        age_waiting_tasks();
    }

    spinlock_irq_release(&scheduler_data_lock);

    if (time_slice_remaining_ns > 0) {
        /* timer: ticks every 10ms at 100Hz */
        uint64_t tick_ns = 10000000ULL;

        if (time_slice_remaining_ns <= tick_ns) {
            time_slice_remaining_ns = 0;
            // schedule();  /* DISABLED: Cannot task switch from interrupt */
        } else {
            time_slice_remaining_ns -= tick_ns;
        }
    }
}

void terminate_task(void) {
    lock_scheduler();
    spinlock_irq_acquire(&scheduler_data_lock);

    if (current_task != NULL) {
        /* Add to terminated list */
        current_task->next = terminated_tasks;
        terminated_tasks = current_task;
    }

    spinlock_irq_release(&scheduler_data_lock);

    block_task(TASK_STATE_TERMINATED);

    unlock_scheduler();

    for (;;) __asm__("hlt");
}

void terminate_other_task(tcb_t *task) {
    if (task == NULL || task == current_task) {
        return;
    }

    lock_scheduler();
    spinlock_irq_acquire(&scheduler_data_lock);

    task->state = TASK_STATE_TERMINATED;
    task->next = terminated_tasks;
    terminated_tasks = task;

    spinlock_irq_release(&scheduler_data_lock);
    unlock_scheduler();
}

void task_set_priority(tcb_t *task, uint8_t new_priority) {
    if (task == NULL)
        return;

    lock_scheduler();

    task->priority = new_priority;
    task->base_priority = new_priority;

    unlock_scheduler();
}

tcb_t *get_current_task(void) {
    return current_task;
}

uint64_t get_current_tid(void) {
    return current_task ? current_task->tid : 0;
}

void task_set_owner_proc(tcb_t *task, struct pcb *proc) {
    if (task == NULL) return;

    lock_scheduler();
    task->owner_proc = proc;
    unlock_scheduler();
}

struct pcb *task_get_owner_proc(tcb_t *task) {
    if (task == NULL) return NULL;
    return (struct pcb *)task->owner_proc;
}

void task_set_errno(int err) {
    tcb_t *task = get_current_task();
    if (task != NULL) {
        task->thread_errno = err;
    }
}

int task_get_errno(void) {
    tcb_t *task = get_current_task();
    if (task != NULL) {
        return task->thread_errno;
    }
    return 0;
}

void task_get_info(tcb_t *task, task_info_t *info) {
    if (task == NULL || info == NULL) return;

    lock_scheduler();

    info->tid = task->tid;
    strncpy(info->name, task->name, sizeof(info->name));
    info->state = task->state;
    info->priority = task->priority;
    info->time_used_ns = task->time_used_ns;
    info->switch_count = task->switch_count;
    info->preempt_count = task->preempt_count;
    info->yield_count = task->yield_count;
    info->wakeup_count = task->wakeup_count;

    if (task->owner_proc != NULL) {
        struct pcb *proc = (struct pcb *)task->owner_proc;
        info->owner_pid = proc->pid;
    } else {
        info->owner_pid = 0;
    }

    unlock_scheduler();
}

void scheduler_get_stats(sched_stats_t *stats) {
    if (stats == NULL)
        return;

    lock_scheduler();
    spinlock_irq_acquire(&scheduler_data_lock);
    *stats = sched_stats;
    spinlock_irq_release(&scheduler_data_lock);
    unlock_scheduler();
}

void scheduler_dump_stats(void) {
    spinlock_irq_acquire(&scheduler_data_lock);

    printk("scheduler statistics:\n");
    printk("Total context switches: %lu\n", sched_stats.total_switches);
    printk("Total timer ticks: %lu\n", sched_stats.total_ticks);
    printk("idle time: %lu ms (%lu%%)\n",
           sched_stats.idle_time_ns / 1000000,
           time_since_boot_ns > 0 ? (sched_stats.idle_time_ns * 100) / time_since_boot_ns : 0);
    printk("aging boosts: %lu\n", sched_stats.aging_boosts);
    printk("uptime: %lu ms\n", time_since_boot_ns / 1000000);
    printk("===========================\n\n");

    spinlock_irq_release(&scheduler_data_lock);
}

void scheduler_dump_tasks(void) {
    spinlock_irq_acquire(&scheduler_data_lock);

    printk("task dump:\n");

    if (current_task) {
        const char *state_names[] = {
            "RUNNING", "READY", "SLEEPING", "PAUSED",
            "WAITING_LOCK", "TERMINATED", "WAITING_EVENT", "INTERRUPTIBLE"
        };

        printk("current: [%lu] %s (state=%s, prio=%u/%u, time=%lu ms, switches=%lu)\n",
               current_task->tid, current_task->name,
               current_task->state < 8 ? state_names[current_task->state] : "UNKNOWN",
               current_task->priority, current_task->base_priority,
               current_task->time_used_ns / 1000000,
               current_task->switch_count);

        printk("         preempts=%lu, yields=%lu, wakeups=%lu",
               current_task->preempt_count, current_task->yield_count,
               current_task->wakeup_count);

        if (current_task->owner_proc != NULL) {
            struct pcb *proc = (struct pcb *)current_task->owner_proc;
            printk(", PID=%lu", proc->pid);
        }
        printk("\n");
    }

    printk("\nready queues (by priority):\n");
    for (int i = NUM_PRIORITY_QUEUES - 1; i >= 0; i--) {
        if (ready_queue_heads[i] != NULL) {
            printk("  queue %d (priority %u-%u):\n", i, i * 32, (i * 32) + 31);
            tcb_t *task = ready_queue_heads[i];
            int count = 0;
            while (task != NULL && count < 20) {
                printk("    [%lu] %s (prio=%u/%u, time=%lu ms, waits=%lu)\n",
                       task->tid, task->name,
                       task->priority, task->base_priority,
                       task->time_used_ns / 1000000,
                       task->wait_ticks);

                if (task->owner_proc != NULL) {
                    struct pcb *proc = (struct pcb *)task->owner_proc;
                    printk("           PID=%lu", proc->pid);
                }

                task = task->next;
                count++;
            }
        }
    }

    printk("\nsleeping tasks (sorted by wake time):\n");
    tcb_t *task = sleeping_tasks;
    int count = 0;
    while (task != NULL && count < 20) {
        const char *sleep_type = (task->state == TASK_STATE_INTERRUPTIBLE) ?
                                 "interruptible" : "sleeping";

        printk("  [%lu] %s (%s, wake=%lu ms, in %ld ms)\n",
               task->tid, task->name, sleep_type,
               task->sleep_expiry_ns / 1000000,
               (int64_t)(task->sleep_expiry_ns - time_since_boot_ns) / 1000000);
        task = task->next;
        count++;
    }

    printk("================\n\n");

    spinlock_irq_release(&scheduler_data_lock);
}

void idle_task_entry(void) {
    for (;;) {
        __asm__ volatile("hlt");
    }
}

void scheduler_init(void) {
    if (scheduler_initialized) {
        printk("sched: already initialized\n");
        return;
    }


    last_tsc = rdtsc();
    time_since_boot_ns = 0;

    memset(&sched_stats, 0, sizeof(sched_stats_t));

    tcb_t *boot_task = (tcb_t *)kmalloc(sizeof(tcb_t));
    if (boot_task == NULL) {
        printk("sched: failed to allocate boot task TCB!\n");
        for (;;) __asm__("hlt");
    }

    memset(boot_task, 0, sizeof(tcb_t));

    boot_task->tid = 0;
    strncpy(boot_task->name, "boot", sizeof(boot_task->name) - 1);
    boot_task->cr3 = paging_get_pml4();
    boot_task->state = TASK_STATE_RUNNING;
    boot_task->priority = PRIORITY_NORMAL;
    boot_task->base_priority = PRIORITY_NORMAL;
    boot_task->next = NULL;

    boot_task->owner_proc = NULL;  /* Will be set by proc_init() */
    boot_task->wait_queue = NULL;
    boot_task->wait_reason = 0;
    boot_task->wait_data = NULL;
    boot_task->pending_signals = 0;
    boot_task->signal_pending = false;
    boot_task->thread_errno = 0;
    boot_task->preempt_count = 0;
    boot_task->yield_count = 0;
    boot_task->wakeup_count = 0;

    uint64_t current_rsp;
    __asm__ volatile("mov %%rsp, %0" : "=r"(current_rsp));

    boot_task->kernel_stack_base = current_rsp & ~(KERNEL_STACK_SIZE - 1);
    boot_task->kernel_stack_size = KERNEL_STACK_SIZE;
    boot_task->rsp = 0;  /* Will be set on first task switch */



    current_task = boot_task;

    idle_task = create_kernel_task(idle_task_entry, "idle", PRIORITY_IDLE);
    if (idle_task == NULL) {
        printk("sched: failed to create idle task!\n");
        for (;;) __asm__("hlt");
    }

    lock_scheduler();
    spinlock_irq_acquire(&scheduler_data_lock);

    int idle_queue = priority_to_queue(PRIORITY_IDLE);
    if (ready_queue_heads[idle_queue] == idle_task) {
        ready_queue_heads[idle_queue] = idle_task->next;
        if (ready_queue_heads[idle_queue] == NULL) {
            ready_queue_tails[idle_queue] = NULL;
            ready_queue_bitmap &= ~(1 << idle_queue);
        }
    }
    idle_task->next = NULL;

    spinlock_irq_release(&scheduler_data_lock);
    unlock_scheduler();

    scheduler_initialized = 1;

    printk_ts("sched: initialized\n");
}