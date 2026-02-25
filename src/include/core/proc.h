#ifndef PROC_H
#define PROC_H

#include <klibc/types.h>

#include <core/scheduler.h>

#define PROC_STATE_NEW          0
#define PROC_STATE_RUNNING      1
#define PROC_STATE_READY        2
#define PROC_STATE_WAITING      3
#define PROC_STATE_TERMINATED   4
#define PROC_STATE_ZOMBIE       5   /* Terminated but not yet reaped */

#define PROC_NAME_LEN           32
#define PROC_MAX_THREADS        16
#define PROC_MAX_FDS            256
#define PROC_CWD_LEN            256
#define PROC_NSIG               32

#define PROC_USER_STACK_SIZE    (64 * 1024)     /* 64 KiB */

#define PROC_SIG_NONE           0
#define PROC_SIG_HUP            1
#define PROC_SIG_INT            2
#define PROC_SIG_QUIT           3
#define PROC_SIG_ILL            4
#define PROC_SIG_TRAP           5
#define PROC_SIG_ABRT           6
#define PROC_SIG_BUS            7
#define PROC_SIG_FPE            8
#define PROC_SIG_KILL           9
#define PROC_SIG_USR1           10
#define PROC_SIG_SEGV           11
#define PROC_SIG_USR2           12
#define PROC_SIG_PIPE           13
#define PROC_SIG_ALRM           14
#define PROC_SIG_TERM           15
#define PROC_SIG_CHLD           17
#define PROC_SIG_CONT           18
#define PROC_SIG_STOP           19

#define PROC_SIG_DFL            ((void (*)(int))0)
#define PROC_SIG_IGN            ((void (*)(int))1)

typedef struct wait_queue_entry {
    tcb_t *thread;
    uint64_t wake_reason;
    void *wake_data;
    struct wait_queue_entry *next;
} wait_queue_entry_t;

typedef struct wait_queue {
    wait_queue_entry_t *head;
    wait_queue_entry_t *tail;
    uint32_t count;
    uint64_t wakeup_seq;
} wait_queue_t;

typedef struct file_descriptor {
    void *file;
    uint32_t flags;
    uint64_t offset;
    uint32_t refcount;
} file_descriptor_t;

typedef struct signal_handler {
    void (*handler)(int);
    uint64_t mask;
    uint32_t flags;
} signal_handler_t;

typedef struct proc_cred {
    uid_t uid;  uid_t euid;  uid_t suid;
    gid_t gid;  gid_t egid;  gid_t sgid;
} proc_cred_t;

typedef struct proc_rlimits {
    uint64_t cpu_time_ns;
    uint64_t max_memory;
    uint64_t max_files;
    uint64_t max_threads;
    uint64_t max_children;
} proc_rlimits_t;

typedef struct proc_stats {
    uint64_t minor_faults;
    uint64_t major_faults;
    uint64_t context_switches;
    uint64_t signals_received;
    uint64_t signals_sent;
    uint64_t syscalls;
} proc_stats_t;

typedef struct pcb {
    pid_t    pid;
    char     name[PROC_NAME_LEN];

    uint8_t  state;
    uint8_t  priority;

    struct pcb *parent;
    struct pcb *first_child;
    struct pcb *next_sibling;

    tcb_t   *threads[PROC_MAX_THREADS];
    uint32_t thread_count;
    tcb_t   *main_thread;

    uint64_t cr3;           /* page table root (kernel CR3 until VM is per-process) */
    void    *vm_space;      /* future: per-process vm_space_t */

    uint64_t user_entry;        /* ring-3 entry point (RIP)              */
    uint64_t user_rsp;          /* ring-3 stack pointer at first run      */
    uint64_t user_rflags;       /* RFLAGS for ring-3 entry (IF set)       */
    uint64_t user_stack_virt;   /* virtual base of user stack allocation  */
    uint64_t user_stack_size;   /* size of user stack (bytes)             */

    time_t   time_created_ns;
    clock_t  cpu_time_ns;

    int      exit_code;
    struct pcb *next;

    pid_t    pgid;
    pid_t    sid;
    struct pcb *pgrp_leader;
    struct pcb *session_leader;

    proc_cred_t cred;

    file_descriptor_t *fd_table[PROC_MAX_FDS];
    uint32_t next_fd;
    char     cwd[PROC_CWD_LEN];

    signal_handler_t sig_handlers[PROC_NSIG];
    uint64_t sig_pending;
    uint64_t sig_blocked;
    uint64_t sig_caught;

    wait_queue_t  exit_waiters;
    wait_queue_t *custom_waitq;

    proc_rlimits_t limits;
    proc_stats_t   stats;

    bool     waited_on;
    uint64_t exit_time_ns;
    uint64_t vm_size;
    uint64_t rss;

    uint32_t flags;
#define PROC_FLAG_KERNEL    (1 << 0)  /* runs entirely in ring 0           */
#define PROC_FLAG_TRACED    (1 << 1)  /* being traced (ptrace)             */
#define PROC_FLAG_EXITING   (1 << 2)  /* in process of exiting             */
#define PROC_FLAG_ORPHANED  (1 << 3)  /* orphaned process group            */
#define PROC_FLAG_USER      (1 << 4)  /* has ring-3 execution context      */

} pcb_t;

void proc_init(void);

pcb_t *proc_create(const char *name, void (*entry)(void), uint8_t priority);
pcb_t *proc_create_child(pcb_t *parent, const char *name,
                         void (*entry)(void), uint8_t priority);

pcb_t *proc_create_user(const char *name, uint64_t entry,
                        uint64_t user_stack_top, uint8_t priority);

void proc_exit(int exit_code);
void proc_terminate(pcb_t *proc, int exit_code);

tcb_t *proc_add_thread(pcb_t *proc, void (*entry)(void), const char *name);
void   proc_remove_thread(pcb_t *proc, tcb_t *thread);

pcb_t *proc_get_current(void);
pcb_t *proc_find_by_pid(pid_t pid);
pid_t  proc_get_current_pid(void);
pcb_t *proc_get_init(void);

void   proc_reparent_children(pcb_t *proc, pcb_t *new_parent);
int    proc_setpgid(pcb_t *proc, pid_t pgid);
pid_t  proc_getpgid(pcb_t *proc);
pid_t  proc_setsid(pcb_t *proc);
pid_t  proc_getsid(pcb_t *proc);

void waitq_init(wait_queue_t *wq);
void waitq_add(wait_queue_t *wq, tcb_t *thread);
void waitq_remove(wait_queue_t *wq, tcb_t *thread);
void waitq_wake_one(wait_queue_t *wq, uint64_t reason, void *data);
void waitq_wake_all(wait_queue_t *wq, uint64_t reason, void *data);
int  waitq_wait(wait_queue_t *wq, uint64_t timeout_ns);
int  waitq_wait_interruptible(wait_queue_t *wq, uint64_t timeout_ns);

int  proc_signal_send(pcb_t *proc, int signum);
int  proc_signal_send_by_pid(pid_t pid, int signum);
int  proc_signal_set_handler(pcb_t *proc, int signum, void (*handler)(int));
int  proc_signal_block(pcb_t *proc, int signum);
int  proc_signal_unblock(pcb_t *proc, int signum);
void proc_signal_deliver_pending(pcb_t *proc);
int  proc_kill(pid_t pid, int signal);

int proc_wait(int *status);
int proc_waitpid(pid_t pid, int *status, int options);
#define PROC_WNOHANG    1
#define PROC_WUNTRACED  2

int               proc_fd_alloc(pcb_t *proc);
int               proc_fd_install(pcb_t *proc, int fd, file_descriptor_t *f);
file_descriptor_t *proc_fd_get(pcb_t *proc, int fd);
void              proc_fd_close(pcb_t *proc, int fd);
void              proc_fd_close_all(pcb_t *proc);

int         proc_set_cwd(pcb_t *proc, const char *path);
const char *proc_get_cwd(pcb_t *proc);

int  proc_setuid(pcb_t *proc, uid_t uid);
int  proc_setgid(pcb_t *proc, gid_t gid);
uid_t proc_getuid(pcb_t *proc);
uid_t proc_geteuid(pcb_t *proc);
gid_t proc_getgid(pcb_t *proc);
gid_t proc_getegid(pcb_t *proc);

int      proc_set_rlimit(pcb_t *proc, int resource, uint64_t limit);
uint64_t proc_get_rlimit(pcb_t *proc, int resource);
#define PROC_RLIMIT_CPU         0
#define PROC_RLIMIT_MEMORY      1
#define PROC_RLIMIT_FILES       2
#define PROC_RLIMIT_THREADS     3
#define PROC_RLIMIT_CHILDREN    4

void proc_get_stats(pcb_t *proc, proc_stats_t *stats);
void proc_update_cpu_time(pcb_t *proc);

typedef void (*proc_iter_fn)(pcb_t *proc, void *data);
void proc_for_each(proc_iter_fn fn, void *data);
void proc_for_each_child(pcb_t *parent, proc_iter_fn fn, void *data);

void proc_reap_zombies(void);
int  proc_count_zombies(void);

void proc_dump_tree(void);
void proc_dump_all(void);
void proc_dump_info(pcb_t *proc);
void proc_dump_detailed(pcb_t *proc);
void proc_dump_signals(pcb_t *proc);
void proc_dump_fds(pcb_t *proc);
void proc_dump_credentials(pcb_t *proc);

#endif