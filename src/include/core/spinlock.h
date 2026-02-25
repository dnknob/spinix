#ifndef _SPINLOCK_H
#define _SPINLOCK_H

#include <klibc/types.h>

typedef struct {
    volatile uint32_t lock;
} __attribute__((aligned(4))) spinlock_t;

typedef struct {
    volatile uint32_t lock;
    volatile uint64_t flags;  /* Saved RFLAGS */
} __attribute__((aligned(8))) spinlock_irq_t;

#define SPINLOCK_INIT { .lock = 0 }
#define SPINLOCK_IRQ_INIT { .lock = 0, .flags = 0 }

void spinlock_init(spinlock_t *lock);
void spinlock_acquire(spinlock_t *lock);
bool spinlock_try_acquire(spinlock_t *lock);
void spinlock_release(spinlock_t *lock);
bool spinlock_is_locked(spinlock_t *lock);

void spinlock_irq_init(spinlock_irq_t *lock);
void spinlock_irq_acquire(spinlock_irq_t *lock);
void spinlock_irq_release(spinlock_irq_t *lock);

#endif
