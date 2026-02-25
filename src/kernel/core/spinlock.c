#include <arch/x86_64/atomic.h>

#include <core/spinlock.h>

static inline void cpu_pause(void)
{
    __asm__ volatile("pause" ::: "memory");
}

static inline uint64_t save_irq_disable(void)
{
    uint64_t flags;
    __asm__ volatile(
        "pushfq\n\t"           /* Push RFLAGS onto stack */
        "pop %0\n\t"           /* Pop into flags variable */
        "cli"                  /* Disable interrupts */
        : "=r"(flags)
        :
        : "memory"
    );
    return flags;
}

static inline void restore_irq(uint64_t flags)
{
    __asm__ volatile(
        "push %0\n\t"          /* Push flags onto stack */
        "popfq"                /* Pop into RFLAGS */
        :
        : "r"(flags)
        : "memory", "cc"
    );
}

void spinlock_init(spinlock_t *lock)
{
    atomic_store_32(&lock->lock, 0);
}

void spinlock_acquire(spinlock_t *lock)
{
    /*
     * Fast path: try to acquire immediately
     * atomic_bit_test_and_set returns the OLD value of the bit
     * Returns 0 (false) if lock was free, 1 (true) if already locked
     */
    if (!atomic_bit_test_and_set_32(&lock->lock, 0)) {
        return;  /* Lock acquired successfully */
    }

    while (1) {
        cpu_pause();  /* Hint to CPU we're in a spin loop (reduces power, improves performance) */

        if ((atomic_load_acquire_32(&lock->lock) & 1) == 0) {
            /* Lock appears free, try to acquire it */
            if (!atomic_bit_test_and_set_32(&lock->lock, 0)) {
                return;  /* Lock acquired successfully */
            }
            /* Someone else grabbed it, continue spinning */
        }
    }
}

bool spinlock_try_acquire(spinlock_t *lock)
{
    /*
     * Non-blocking acquire attempt
     * Returns true if lock was acquired, false otherwise
     */
    return !atomic_bit_test_and_set_32(&lock->lock, 0);
}

void spinlock_release(spinlock_t *lock)
{
    atomic_store_release_32(&lock->lock, 0);
}

bool spinlock_is_locked(spinlock_t *lock)
{
    return (atomic_load_acquire_32(&lock->lock) & 1) != 0;
}

void spinlock_irq_init(spinlock_irq_t *lock)
{
    atomic_store_32(&lock->lock, 0);
    lock->flags = 0;
}

void spinlock_irq_acquire(spinlock_irq_t *lock)
{
    uint64_t flags = save_irq_disable();

    if (!atomic_bit_test_and_set_32(&lock->lock, 0)) {
        /* Lock acquired on first try */
        lock->flags = flags;
        return;
    }

    while (1) {
        cpu_pause();

        if ((atomic_load_acquire_32(&lock->lock) & 1) == 0) {
            if (!atomic_bit_test_and_set_32(&lock->lock, 0)) {
                /* Lock acquired */
                lock->flags = flags;
                return;
            }
        }
    }
}

void spinlock_irq_release(spinlock_irq_t *lock)
{
    uint64_t flags = lock->flags;

    atomic_store_release_32(&lock->lock, 0);

    restore_irq(flags);
}
