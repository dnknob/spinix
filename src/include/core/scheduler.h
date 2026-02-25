#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <klibc/types.h>

struct pcb;

#define TASK_STATE_RUNNING          0
#define TASK_STATE_READY            1
#define TASK_STATE_SLEEPING         2
#define TASK_STATE_PAUSED           3
#define TASK_STATE_WAITING_LOCK     4
#define TASK_STATE_TERMINATED       5
#define TASK_STATE_WAITING_EVENT    6  /* Waiting on a wait queue */
#define TASK_STATE_INTERRUPTIBLE    7  /* Sleeping, can be interrupted by signals */

#define KERNEL_STACK_SIZE           (16 * 1024)

#define TIME_SLICE_LENGTH_NS        10000000ULL

#define NUM_PRIORITY_QUEUES         8
#define PRIORITY_IDLE               0
#define PRIORITY_LOW                32
#define PRIORITY_NORMAL             128
#define PRIORITY_HIGH               192
#define PRIORITY_REALTIME           224

#define AGING_THRESHOLD_TICKS       50   /* ~500ms at 100Hz */
#define AGING_BOOST_AMOUNT          16

typedef struct tcb {
    /* Saved context (must be first for assembly access) */
    uint64_t rsp;                   /* Saved stack pointer */

    tid_t    tid;                   /* Task ID */
    char name[32];                  /* Task name for debugging */

    uint64_t kernel_stack_base;     /* Base of kernel stack */
    uint64_t kernel_stack_size;     /* Size of kernel stack */
    uint64_t cr3;                   /* Page table base (for future user-space) */

    uint8_t state;                  /* Current task state */
    uint8_t priority;               /* Task priority (0-255) */
    uint8_t base_priority;          /* Original priority (for aging reset) */
    uint8_t _padding;

    struct tcb *next;               /* Next task in queue */

    uint64_t time_used_ns;          /* Total CPU time consumed (nanoseconds) */
    uint64_t sleep_expiry_ns;       /* Wake up time for sleeping tasks */

    uint64_t wait_ticks;            /* Ticks spent waiting (for aging) */
    uint64_t switch_count;          /* Number of times scheduled */
    uint64_t last_run_time_ns;      /* When task last ran */

    struct pcb *owner_proc;         /* Back-pointer to owning process (O(1) lookup!) */
    
    void *wait_queue;               /* Wait queue this thread is on */
    uint64_t wait_reason;           /* Reason for waiting */
    void *wait_data;                /* Data associated with wait */
    
    uint64_t pending_signals;       /* Signals pending for this thread */
    bool signal_pending;            /* Fast check: any signal pending? */
    
    int thread_errno;               /* Thread-local error number */
    
    uint64_t preempt_count;         /* Times preempted */
    uint64_t yield_count;           /* Times voluntarily yielded */
    uint64_t wakeup_count;          /* Times woken from sleep/wait */

} tcb_t;

typedef struct {
    uint64_t total_switches;        /* Total context switches */
    uint64_t total_ticks;           /* Total timer ticks */
    uint64_t idle_time_ns;          /* Time spent in idle */
    uint64_t aging_boosts;          /* Number of priority boosts */
} sched_stats_t;

void scheduler_init(void);

tcb_t *create_kernel_task(void (*entry_point)(void), const char *name, uint8_t priority);

void schedule(void);
void yield(void);

void block_task(uint8_t reason);
void unblock_task(tcb_t *task);

void nano_sleep(uint64_t nanoseconds);
void nano_sleep_until(uint64_t wake_time_ns);
void sleep_ms(uint64_t milliseconds);

void terminate_task(void);
void terminate_other_task(tcb_t *task);

void lock_scheduler(void);
void unlock_scheduler(void);

uint64_t get_time_since_boot_ns(void);
void scheduler_timer_tick(void);

tcb_t *get_current_task(void);
uint64_t get_current_tid(void);
void scheduler_dump_tasks(void);
void scheduler_dump_stats(void);

void scheduler_get_stats(sched_stats_t *stats);

void task_set_priority(tcb_t *task, uint8_t new_priority);

extern void idle_task_entry(void);

void task_set_owner_proc(tcb_t *task, struct pcb *proc);

struct pcb *task_get_owner_proc(tcb_t *task);

int nano_sleep_interruptible(uint64_t nanoseconds);

void task_wake_interruptible(tcb_t *task);

void task_set_errno(int err);
int task_get_errno(void);

typedef struct {
    tid_t    tid;
    char name[32];
    uint8_t state;
    uint8_t priority;
    uint64_t time_used_ns;
    uint64_t switch_count;
    uint64_t preempt_count;
    uint64_t yield_count;
    uint64_t wakeup_count;
    pid_t    owner_pid;  /* PID of owning process */
} task_info_t;

void task_get_info(tcb_t *task, task_info_t *info);

#endif
