#ifndef _MUTEX_H
#define _MUTEX_H

#include <klibc/types.h>

struct tcb;

typedef struct {
    volatile uint64_t entries;      /* Ticket counter for entering threads */
    volatile uint64_t exits;        /* Counter for threads that have exited */
    volatile uint32_t queue_lock;   /* Spinlock protecting wait_queue */
    struct tcb *wait_queue;         /* Queue of sleeping waiters (TCBs linked via wait_data) */
    struct tcb *owner;              /* Current owner (for debugging/deadlock detection) */
} __attribute__((aligned(64))) mutex_t;

#define MUTEX_INIT { .entries = 0, .exits = 0, .queue_lock = 0, .wait_queue = NULL, .owner = NULL }

void mutex_init(mutex_t *mutex);

void mutex_lock(mutex_t *mutex);
bool mutex_try_lock(mutex_t *mutex);
void mutex_unlock(mutex_t *mutex);
bool mutex_is_locked(mutex_t *mutex);

/* Get current mutex owner (for debugging) */
struct tcb *mutex_get_owner(mutex_t *mutex);

#endif
