#include <arch/x86_64/switch.h>
#include <arch/x86_64/mmu.h>

#include <core/scheduler.h>
#include <core/proc.h>

#include <fs/vfs.h>

#include <mm/paging.h>
#include <mm/heap.h>
#include <mm/vmm.h>
#include <mm/mmu.h>

#include <video/printk.h>
#include <video/log.h>

#include <klibc/string.h>

static pcb_t *process_list = NULL;
static pcb_t *init_process = NULL;
static uint64_t next_pid = 1;

static pcb_t *current_process = NULL;

static pcb_t *zombie_list = NULL;

extern void lock_scheduler(void);
extern void unlock_scheduler(void);
extern void task_set_owner_proc(tcb_t *task, struct pcb *proc);

static void user_task_trampoline(void) {
    pcb_t *proc = proc_get_current();
    if (proc == NULL || !(proc->flags & PROC_FLAG_USER)) {
        terminate_task();
        return;
    }
    enter_userspace(proc->user_entry, proc->user_rsp, proc->user_rflags);
    for (;;) __asm__("hlt");
}

void waitq_init(wait_queue_t *wq) {
    if (wq == NULL) return;

    wq->head = NULL;
    wq->tail = NULL;
    wq->count = 0;
    wq->wakeup_seq = 0;
}

void waitq_add(wait_queue_t *wq, tcb_t *thread) {
    if (wq == NULL || thread == NULL) return;

    lock_scheduler();

    wait_queue_entry_t *entry = (wait_queue_entry_t *)kmalloc(sizeof(wait_queue_entry_t));
    if (entry == NULL) {
        unlock_scheduler();
        return;
    }

    entry->thread = thread;
    entry->wake_reason = 0;
    entry->wake_data = NULL;
    entry->next = NULL;

    if (wq->tail == NULL) {
        wq->head = entry;
        wq->tail = entry;
    } else {
        wq->tail->next = entry;
        wq->tail = entry;
    }

    wq->count++;

    unlock_scheduler();
}

void waitq_remove(wait_queue_t *wq, tcb_t *thread) {
    if (wq == NULL || thread == NULL) return;

    lock_scheduler();

    wait_queue_entry_t **prev = &wq->head;
    wait_queue_entry_t *curr = wq->head;

    while (curr != NULL) {
        if (curr->thread == thread) {
            *prev = curr->next;
            if (wq->tail == curr) {
                wq->tail = (prev == &wq->head) ? NULL : (wait_queue_entry_t *)prev;
            }
            kfree(curr);
            wq->count--;
            break;
        }
        prev = &curr->next;
        curr = curr->next;
    }

    unlock_scheduler();
}

void waitq_wake_one(wait_queue_t *wq, uint64_t reason, void *data) {
    if (wq == NULL || wq->head == NULL) return;

    lock_scheduler();

    wait_queue_entry_t *entry = wq->head;
    if (entry != NULL) {
        wq->head = entry->next;
        if (wq->head == NULL) {
            wq->tail = NULL;
        }
        wq->count--;
        wq->wakeup_seq++;

        entry->wake_reason = reason;
        entry->wake_data = data;

        tcb_t *thread = entry->thread;
        kfree(entry);

        if (thread != NULL) {
            unblock_task(thread);
        }
    }

    unlock_scheduler();
}

void waitq_wake_all(wait_queue_t *wq, uint64_t reason, void *data) {
    if (wq == NULL) return;

    lock_scheduler();

    while (wq->head != NULL) {
        wait_queue_entry_t *entry = wq->head;
        wq->head = entry->next;

        entry->wake_reason = reason;
        entry->wake_data = data;

        tcb_t *thread = entry->thread;
        kfree(entry);

        if (thread != NULL) {
            unblock_task(thread);
        }
    }

    wq->tail = NULL;
    wq->count = 0;
    wq->wakeup_seq++;

    unlock_scheduler();
}

int waitq_wait(wait_queue_t *wq, uint64_t timeout_ns) {
    if (wq == NULL) return -1;

    tcb_t *current = get_current_task();
    if (current == NULL) return -1;

    waitq_add(wq, current);

    if (timeout_ns > 0) {
        nano_sleep(timeout_ns);
    } else {
        block_task(TASK_STATE_WAITING_EVENT);
    }

    waitq_remove(wq, current);
    return 0;
}

int waitq_wait_interruptible(wait_queue_t *wq, uint64_t timeout_ns) {
    /* TODO: Check for pending signals and return -EINTR if present */
    return waitq_wait(wq, timeout_ns);
}

static const char *signal_names[PROC_NSIG] = {
    "NONE", "HUP", "INT", "QUIT", "ILL", "TRAP", "ABRT", "BUS",
    "FPE", "KILL", "USR1", "SEGV", "USR2", "PIPE", "ALRM", "TERM",
    NULL, "CHLD", "CONT", "STOP"
};

int proc_signal_send(pcb_t *proc, int signum) {
    if (proc == NULL || signum <= 0 || signum >= PROC_NSIG) {
        return -1;
    }

    lock_scheduler();

    proc->sig_pending |= (1ULL << signum);
    proc->stats.signals_received++;

    if (!(proc->sig_blocked & (1ULL << signum))) {
        /* Wake up main thread if it's sleeping */
        if (proc->main_thread != NULL &&
            proc->main_thread->state == TASK_STATE_SLEEPING) {
            unblock_task(proc->main_thread);
        }
    }

    unlock_scheduler();

    return 0;
}

int proc_signal_send_by_pid(pid_t pid, int signum) {
    pcb_t *proc = proc_find_by_pid(pid);
    if (proc == NULL) {
        return -1;
    }

    return proc_signal_send(proc, signum);
}

int proc_signal_set_handler(pcb_t *proc, int signum, void (*handler)(int)) {
    if (proc == NULL || signum <= 0 || signum >= PROC_NSIG) {
        return -1;
    }

    if (signum == PROC_SIG_KILL || signum == PROC_SIG_STOP) {
        return -1;
    }

    lock_scheduler();

    proc->sig_handlers[signum].handler = handler;

    if (handler != PROC_SIG_IGN && handler != PROC_SIG_DFL) {
        proc->sig_caught |= (1ULL << signum);
    } else {
        proc->sig_caught &= ~(1ULL << signum);
    }

    unlock_scheduler();

    return 0;
}

int proc_signal_block(pcb_t *proc, int signum) {
    if (proc == NULL || signum <= 0 || signum >= PROC_NSIG) {
        return -1;
    }

    lock_scheduler();
    proc->sig_blocked |= (1ULL << signum);
    unlock_scheduler();

    return 0;
}

int proc_signal_unblock(pcb_t *proc, int signum) {
    if (proc == NULL || signum <= 0 || signum >= PROC_NSIG) {
        return -1;
    }

    lock_scheduler();
    proc->sig_blocked &= ~(1ULL << signum);
    unlock_scheduler();

    return 0;
}

void proc_signal_deliver_pending(pcb_t *proc) {
    if (proc == NULL) return;

    lock_scheduler();

    uint64_t pending = proc->sig_pending & ~proc->sig_blocked;

    for (int sig = 1; sig < PROC_NSIG; sig++) {
        if (pending & (1ULL << sig)) {
            signal_handler_t *handler = &proc->sig_handlers[sig];

            proc->sig_pending &= ~(1ULL << sig);

            if (handler->handler == PROC_SIG_DFL) {
                /* Default action - typically terminate */
                if (sig == PROC_SIG_CHLD) {
                    /* Ignore SIGCHLD by default */
                    continue;
                }
                unlock_scheduler();
                proc_terminate(proc, sig);
                return;
            } else if (handler->handler == PROC_SIG_IGN) {
                /* Ignore signal */
                continue;
            } else {
                /* Call user handler */
                unlock_scheduler();
                handler->handler(sig);
                lock_scheduler();
            }
        }
    }

    unlock_scheduler();
}

int proc_kill(pid_t pid, int signal) {
    return proc_signal_send_by_pid(pid, signal);
}

static pcb_t *alloc_pcb(const char *name, uint8_t priority) {
    pcb_t *proc = (pcb_t *)kmalloc(sizeof(pcb_t));
    if (proc == NULL) {
        epanic("proc", "failed to allocate PCB");
        return NULL;
    }
    memset(proc, 0, sizeof(pcb_t));

    proc->fd_table = (file_descriptor_t **)kmalloc(
        PROC_MAX_FDS * sizeof(file_descriptor_t *));
    if (proc->fd_table == NULL) {
        epanic("proc", "failed to allocate fd table");
        kfree(proc);
        return NULL;
    }
    memset(proc->fd_table, 0, PROC_MAX_FDS * sizeof(file_descriptor_t *));

    proc->pid = next_pid++;
    strncpy(proc->name, name, PROC_NAME_LEN - 1);
    proc->name[PROC_NAME_LEN - 1] = '\0';

    proc->state = PROC_STATE_NEW;
    proc->priority = priority;

    proc->cr3 = paging_get_pml4();  /* Use kernel page table for now */
    proc->vm_space = NULL;          /* TODO: Create separate VM space for user processes */

    proc->time_created_ns = get_time_since_boot_ns();
    proc->cpu_time_ns = 0;
    proc->exit_code = 0;

    proc->parent = NULL;
    proc->first_child = NULL;
    proc->next_sibling = NULL;

    proc->thread_count = 0;
    proc->main_thread = NULL;

    proc->pgid = proc->pid;  /* Process group = own pid by default */
    proc->sid = 0;
    proc->pgrp_leader = proc;
    proc->session_leader = NULL;

    proc->cred.uid = 0;
    proc->cred.euid = 0;
    proc->cred.suid = 0;
    proc->cred.gid = 0;
    proc->cred.egid = 0;
    proc->cred.sgid = 0;

    for (int i = 0; i < PROC_MAX_FDS; i++) {
        proc->fd_table[i] = NULL;
    }
    proc->next_fd = 0;
    strncpy(proc->cwd, "/", PROC_CWD_LEN);

    for (int i = 0; i < PROC_NSIG; i++) {
        proc->sig_handlers[i].handler = PROC_SIG_DFL;
        proc->sig_handlers[i].mask = 0;
        proc->sig_handlers[i].flags = 0;
    }
    proc->sig_pending = 0;
    proc->sig_blocked = 0;
    proc->sig_caught = 0;

    waitq_init(&proc->exit_waiters);
    proc->custom_waitq = NULL;

    proc->limits.cpu_time_ns = UINT64_MAX;
    proc->limits.max_memory = UINT64_MAX;
    proc->limits.max_files = PROC_MAX_FDS;
    proc->limits.max_threads = PROC_MAX_THREADS;
    proc->limits.max_children = UINT64_MAX;

    memset(&proc->stats, 0, sizeof(proc_stats_t));

    proc->waited_on = false;
    proc->exit_time_ns = 0;

    proc->vm_size = 0;
    proc->rss = 0;

    proc->flags = PROC_FLAG_KERNEL;  /* All processes are kernel for now */

    return proc;
}

static void add_to_process_list(pcb_t *proc) {
    lock_scheduler();

    proc->next = process_list;
    process_list = proc;

    unlock_scheduler();
}

static void add_child_to_parent(pcb_t *parent, pcb_t *child) {
    lock_scheduler();

    child->parent = parent;
    child->next_sibling = parent->first_child;
    parent->first_child = child;

    unlock_scheduler();
}

void proc_init(void) {

    init_process = alloc_pcb("init", 128);
    if (init_process == NULL) {
        epanic("proc", "failed to create init process");
        for (;;) __asm__("hlt");
    }

    init_process->pid = 1;
    init_process->state = PROC_STATE_RUNNING;
    init_process->sid = 1;  /* Init is session leader */
    init_process->session_leader = init_process;

    tcb_t *boot_task = get_current_task();
    if (boot_task != NULL) {
        init_process->threads[0] = boot_task;
        init_process->main_thread = boot_task;
        init_process->thread_count = 1;

        task_set_owner_proc(boot_task, init_process);
    }

    add_to_process_list(init_process);
    current_process = init_process;

}

pcb_t *proc_create(const char *name, void (*entry_point)(void), uint8_t priority) {
    return proc_create_child(init_process, name, entry_point, priority);
}

pcb_t *proc_create_child(pcb_t *parent, const char *name, void (*entry_point)(void), uint8_t priority) {
    if (parent == NULL) {
        parent = init_process;
    }

    pcb_t *proc = alloc_pcb(name, priority);
    if (proc == NULL) {
        return NULL;
    }

    proc->cred = parent->cred;

    proc->pgid = parent->pgid;
    proc->sid = parent->sid;
    proc->session_leader = parent->session_leader;

    strncpy(proc->cwd, parent->cwd, PROC_CWD_LEN);

    char thread_name[32];
    snprintk(thread_name, sizeof(thread_name), "%s-main", name);

    tcb_t *main_thread = create_kernel_task(entry_point, thread_name, priority);
    if (main_thread == NULL) {
        epanic("proc", "failed to create main thread");
        kfree(proc);
        return NULL;
    }

    proc->threads[0] = main_thread;
    proc->main_thread = main_thread;
    proc->thread_count = 1;

    task_set_owner_proc(main_thread, proc);

    add_child_to_parent(parent, proc);

    add_to_process_list(proc);

    proc->state = PROC_STATE_READY;

    return proc;

}

pcb_t *proc_create_user(const char *name, uint64_t entry,
                        uint64_t user_stack_top, uint8_t priority)
{
    pcb_t *proc = alloc_pcb(name, priority);
    if (proc == NULL) return NULL;

    vm_space_t *space = vmm_create_space();
    if (space == NULL) { kfree(proc); return NULL; }
    proc->vm_space = space;

    uint64_t pml4_phys = mmu_get_pml4_phys((mmu_context_t *)space->mmu_ctx);
    proc->cr3 = pml4_phys;

    proc->user_entry      = entry;
    proc->user_rsp        = user_stack_top;
    proc->user_rflags     = 0x202;       /* IF set, reserved bit 1 */
    proc->user_stack_virt = user_stack_top;
    proc->user_stack_size = PROC_USER_STACK_SIZE;
    proc->flags          &= ~PROC_FLAG_KERNEL;
    proc->flags          |=  PROC_FLAG_USER;

    tcb_t *t = create_kernel_task(user_task_trampoline, name, priority);
    if (t == NULL) {
        vmm_destroy_space(space);
        kfree(proc);
        return NULL;
    }
    t->cr3 = pml4_phys;   /* so switch_to_task loads the right PML4 */

    proc->threads[0]   = t;
    proc->main_thread  = t;
    proc->thread_count = 1;
    task_set_owner_proc(t, proc);

    add_child_to_parent(init_process, proc);
    add_to_process_list(proc);
    proc->state = PROC_STATE_READY;

    return proc;
}

pcb_t *proc_create_user_with_space(const char *name, uint64_t entry,
                                   uint64_t user_stack_top,
                                   uint8_t priority,
                                   vm_space_t *space)
{
    if (space == NULL) return NULL;

    pcb_t *proc = alloc_pcb(name, priority);
    if (proc == NULL) return NULL;

    uint64_t pml4_phys = mmu_get_pml4_phys((mmu_context_t *)space->mmu_ctx);

    proc->vm_space        = space;
    proc->cr3             = pml4_phys;
    proc->user_entry      = entry;
    proc->user_rsp        = user_stack_top;
    proc->user_rflags     = 0x202;
    proc->user_stack_virt = user_stack_top;
    proc->user_stack_size = PROC_USER_STACK_SIZE;
    proc->flags          &= ~PROC_FLAG_KERNEL;
    proc->flags          |=  PROC_FLAG_USER;

    tcb_t *t = create_kernel_task(user_task_trampoline, name, priority);
    if (t == NULL) { kfree(proc); return NULL; }
    t->cr3 = pml4_phys;

    proc->threads[0]   = t;
    proc->main_thread  = t;
    proc->thread_count = 1;
    task_set_owner_proc(t, proc);

    add_child_to_parent(init_process, proc);
    add_to_process_list(proc);
    proc->state = PROC_STATE_READY;

    return proc;
}

pcb_t *proc_fork(uint64_t user_rip, uint64_t user_rsp, uint64_t user_rflags)
{
    pcb_t *parent = proc_get_current();
    if (parent == NULL || !(parent->flags & PROC_FLAG_USER))
        return NULL;

    pcb_t *child = alloc_pcb(parent->name, parent->priority);
    if (child == NULL)
        return NULL;

    vm_space_t *child_space = vmm_fork_space((vm_space_t *)parent->vm_space);
    if (child_space == NULL) {
        kfree(child->fd_table);
        kfree(child);
        return NULL;
    }
    child->vm_space = child_space;
    child->cr3      = mmu_get_pml4_phys((mmu_context_t *)child_space->mmu_ctx);

    for (int i = 0; i < PROC_MAX_FDS; i++) {
        file_descriptor_t *pfde = parent->fd_table[i];
        if (pfde == NULL)
            continue;

        file_descriptor_t *cfde =
            (file_descriptor_t *)kmalloc(sizeof(file_descriptor_t));
        if (cfde == NULL)
            continue;   /* best-effort; real kernels would unwind, but this is fine */

        *cfde          = *pfde;
        cfde->refcount = 1;

        if (cfde->file != NULL)
            __sync_fetch_and_add(&((vfs_file_t *)cfde->file)->f_refcount, 1);

        child->fd_table[i] = cfde;
    }

    child->heap_base       = parent->heap_base;
    child->heap_brk        = parent->heap_brk;
    child->cred            = parent->cred;
    child->pgid            = parent->pgid;
    child->sid             = parent->sid;
    child->session_leader  = parent->session_leader;
    memcpy(child->cwd,          parent->cwd,          PROC_CWD_LEN);
    memcpy(child->sig_handlers, parent->sig_handlers, sizeof(parent->sig_handlers));
    child->sig_blocked     = parent->sig_blocked;
    child->flags           = parent->flags;

    child->user_entry      = user_rip;
    child->user_rsp        = user_rsp;
    child->user_rflags     = user_rflags;
    child->user_stack_virt = parent->user_stack_virt;
    child->user_stack_size = parent->user_stack_size;

    tcb_t *t = create_kernel_task(user_task_trampoline, child->name, child->priority);
    if (t == NULL) {
        vmm_destroy_space(child_space);
        proc_fd_close_all(child);
        kfree(child->fd_table);
        kfree(child);
        return NULL;
    }
    t->cr3 = child->cr3;

    child->threads[0]   = t;
    child->main_thread  = t;
    child->thread_count = 1;
    task_set_owner_proc(t, child);

    add_child_to_parent(parent, child);
    add_to_process_list(child);
    child->state = PROC_STATE_READY;

    veinfo("fork: pid %lu -> child pid %lu (rip=0x%lx)",
           parent->pid, child->pid, user_rip);

    return child;
}

tcb_t *proc_add_thread(pcb_t *proc, void (*entry_point)(void), const char *thread_name) {
    if (proc == NULL || proc->thread_count >= PROC_MAX_THREADS) {
        return NULL;
    }

    tcb_t *thread = create_kernel_task(entry_point, thread_name, proc->priority);
    if (thread == NULL) {
        return NULL;
    }

    lock_scheduler();

    proc->threads[proc->thread_count] = thread;
    proc->thread_count++;

    task_set_owner_proc(thread, proc);

    unlock_scheduler();


    return thread;
}

void proc_remove_thread(pcb_t *proc, tcb_t *thread) {
    if (proc == NULL || thread == NULL) {
        return;
    }

    lock_scheduler();

    for (uint32_t i = 0; i < proc->thread_count; i++) {
        if (proc->threads[i] == thread) {
            /* Shift remaining threads down */
            for (uint32_t j = i; j < proc->thread_count - 1; j++) {
                proc->threads[j] = proc->threads[j + 1];
            }
            proc->threads[proc->thread_count - 1] = NULL;
            proc->thread_count--;
            break;
        }
    }

    if (proc->thread_count == 0) {
        proc->state = PROC_STATE_TERMINATED;
    }

    unlock_scheduler();
}

void proc_terminate(pcb_t *proc, int exit_code) {
    if (proc == NULL || proc->state == PROC_STATE_TERMINATED) {
        return;
    }

    lock_scheduler();

    proc->exit_code = exit_code;
    proc->exit_time_ns = get_time_since_boot_ns();
    proc->flags |= PROC_FLAG_EXITING;

    if (proc->parent != NULL && !proc->waited_on) {
        proc->state = PROC_STATE_ZOMBIE;

        proc_signal_send(proc->parent, PROC_SIG_CHLD);

        waitq_wake_all(&proc->exit_waiters, exit_code, proc);
    } else {
        proc->state = PROC_STATE_TERMINATED;
    }

    for (uint32_t i = 0; i < proc->thread_count; i++) {
        if (proc->threads[i] != NULL) {
            terminate_other_task(proc->threads[i]);
        }
    }

    proc_fd_close_all(proc);

    if (proc != init_process) {
        proc_reparent_children(proc, init_process);
    }

    unlock_scheduler();
}

void proc_exit(int exit_code) {
    pcb_t *proc = proc_get_current();
    if (proc != NULL) {
        proc_terminate(proc, exit_code);
    }

    terminate_task();
}

void proc_reparent_children(pcb_t *proc, pcb_t *new_parent) {
    if (proc == NULL || new_parent == NULL) {
        return;
    }

    lock_scheduler();

    pcb_t *child = proc->first_child;
    while (child != NULL) {
        pcb_t *next = child->next_sibling;

        child->parent = new_parent;

        child->next_sibling = new_parent->first_child;
        new_parent->first_child = child;

        child = next;
    }

    proc->first_child = NULL;

    unlock_scheduler();
}

pcb_t *proc_get_current(void) {
    tcb_t *current_thread = get_current_task();
    if (current_thread == NULL) {
        return NULL;
    }

    if (current_thread->owner_proc != NULL) {
        return (pcb_t *)current_thread->owner_proc;
    }

    lock_scheduler();

    pcb_t *proc = process_list;
    while (proc != NULL) {
        for (uint32_t i = 0; i < proc->thread_count; i++) {
            if (proc->threads[i] == current_thread) {
                unlock_scheduler();
                return proc;
            }
        }
        proc = proc->next;
    }

    unlock_scheduler();
    return NULL;
}

pcb_t *proc_find_by_pid(pid_t pid) {
    lock_scheduler();

    pcb_t *proc = process_list;
    while (proc != NULL) {
        if (proc->pid == pid) {
            unlock_scheduler();
            return proc;
        }
        proc = proc->next;
    }

    unlock_scheduler();
    return NULL;
}

pid_t proc_get_current_pid(void) {
    pcb_t *proc = proc_get_current();
    return proc ? proc->pid : 0;
}

pcb_t *proc_get_init(void) {
    return init_process;
}

int proc_wait(int *status) {
    return proc_waitpid(0, status, 0);  /* Wait for any child */
}

int proc_waitpid(pid_t pid, int *status, int options) {
    pcb_t *current = proc_get_current();
    if (current == NULL) {
        return -1;
    }

    lock_scheduler();

    pcb_t *child = current->first_child;
    pcb_t *zombie_child = NULL;

    while (child != NULL) {
        if ((pid == 0 || child->pid == pid) && child->state == PROC_STATE_ZOMBIE) {
            zombie_child = child;
            break;
        }
        child = child->next_sibling;
    }

    if (zombie_child != NULL) {
        /* found zombie child, reap it */
        if (status != NULL) {
            *status = zombie_child->exit_code;
        }

        pid_t child_pid = zombie_child->pid;

        zombie_child->waited_on = true;
        zombie_child->state = PROC_STATE_TERMINATED;

        zombie_child->next = zombie_list;
        zombie_list = zombie_child;

        unlock_scheduler();
        return child_pid;
    }

    unlock_scheduler();

    if (options & PROC_WNOHANG) {
        return 0;  /* no child available, don't block */
    }

    waitq_wait(&current->exit_waiters, 0);

    return proc_waitpid(pid, status, options);
}

void proc_reap_zombies(void) {
    lock_scheduler();

    pcb_t **prev = &zombie_list;
    pcb_t *proc = zombie_list;

    while (proc != NULL) {
        pcb_t *next = proc->next;

        if (proc->state == PROC_STATE_TERMINATED && proc->waited_on) {
            /* Really dead now, free it */
            *prev = next;

            for (int i = 0; i < PROC_MAX_FDS; i++) {
                if (proc->fd_table[i] != NULL) {
                    kfree(proc->fd_table[i]);
                }
            }

            kfree(proc->fd_table);
            kfree(proc);
        } else {
            prev = &proc->next;
        }

        proc = next;
    }

    unlock_scheduler();
}

int proc_count_zombies(void) {
    lock_scheduler();

    int count = 0;
    pcb_t *proc = zombie_list;

    while (proc != NULL) {
        count++;
        proc = proc->next;
    }

    unlock_scheduler();
    return count;
}

int proc_setpgid(pcb_t *proc, pid_t pgid) {
    if (proc == NULL) return -1;

    lock_scheduler();
    proc->pgid = pgid;
    unlock_scheduler();

    return 0;
}

pid_t proc_getpgid(pcb_t *proc) {
    if (proc == NULL) return 0;
    return proc->pgid;
}

pid_t proc_setsid(pcb_t *proc) {
    if (proc == NULL) return -1;

    lock_scheduler();

    proc->sid = proc->pid;
    proc->pgid = proc->pid;
    proc->session_leader = proc;
    proc->pgrp_leader = proc;

    unlock_scheduler();

    return proc->pid;
}

pid_t proc_getsid(pcb_t *proc) {
    if (proc == NULL) return 0;
    return proc->sid;
}

int proc_fd_alloc(pcb_t *proc) {
    if (proc == NULL) return -1;

    lock_scheduler();

    for (int i = 0; i < PROC_MAX_FDS; i++) {
        if (proc->fd_table[i] == NULL) {
            unlock_scheduler();
            return i;
        }
    }

    unlock_scheduler();
    return -1;  /* no free FDs */
}

int proc_fd_install(pcb_t *proc, int fd, file_descriptor_t *file) {
    if (proc == NULL || fd < 0 || fd >= PROC_MAX_FDS || file == NULL) {
        return -1;
    }

    lock_scheduler();

    if (proc->fd_table[fd] != NULL) {
        unlock_scheduler();
        return -1;  /* FD already in use */
    }

    proc->fd_table[fd] = file;

    unlock_scheduler();
    return 0;
}

file_descriptor_t *proc_fd_get(pcb_t *proc, int fd) {
    if (proc == NULL || fd < 0 || fd >= PROC_MAX_FDS) {
        return NULL;
    }

    return proc->fd_table[fd];
}

void proc_fd_close(pcb_t *proc, int fd) {
    if (proc == NULL || fd < 0 || fd >= PROC_MAX_FDS) {
        return;
    }

    lock_scheduler();

    file_descriptor_t *file = proc->fd_table[fd];
    if (file != NULL) {
        file->refcount--;
        if (file->refcount == 0) {
            kfree(file);
        }
        proc->fd_table[fd] = NULL;
    }

    unlock_scheduler();
}

void proc_fd_close_all(pcb_t *proc) {
    if (proc == NULL) return;

    for (int i = 0; i < PROC_MAX_FDS; i++) {
        proc_fd_close(proc, i);
    }
}

int proc_set_cwd(pcb_t *proc, const char *path) {
    if (proc == NULL || path == NULL) {
        return -1;
    }

    lock_scheduler();
    strncpy(proc->cwd, path, PROC_CWD_LEN - 1);
    proc->cwd[PROC_CWD_LEN - 1] = '\0';
    unlock_scheduler();

    return 0;
}

const char *proc_get_cwd(pcb_t *proc) {
    if (proc == NULL) {
        return NULL;
    }

    return proc->cwd;
}

int proc_setuid(pcb_t *proc, uid_t uid) {
    if (proc == NULL) return -1;

    lock_scheduler();

    if (proc->cred.euid != 0) {
        if (uid != proc->cred.uid && uid != proc->cred.suid) {
            unlock_scheduler();
            return -1;  /* Permission denied */
        }
    }

    proc->cred.uid = uid;
    proc->cred.euid = uid;
    proc->cred.suid = uid;

    unlock_scheduler();
    return 0;
}

int proc_setgid(pcb_t *proc, gid_t gid) {
    if (proc == NULL) return -1;

    lock_scheduler();

    if (proc->cred.egid != 0) {
        if (gid != proc->cred.gid && gid != proc->cred.sgid) {
            unlock_scheduler();
            return -1;
        }
    }

    proc->cred.gid = gid;
    proc->cred.egid = gid;
    proc->cred.sgid = gid;

    unlock_scheduler();
    return 0;
}

uid_t proc_getuid(pcb_t *proc) {
    if (proc == NULL) return 0;
    return proc->cred.uid;
}

uid_t proc_geteuid(pcb_t *proc) {
    if (proc == NULL) return 0;
    return proc->cred.euid;
}

gid_t proc_getgid(pcb_t *proc) {
    if (proc == NULL) return 0;
    return proc->cred.gid;
}

gid_t proc_getegid(pcb_t *proc) {
    if (proc == NULL) return 0;
    return proc->cred.egid;
}

int proc_set_rlimit(pcb_t *proc, int resource, uint64_t limit) {
    if (proc == NULL) return -1;

    lock_scheduler();

    switch (resource) {
        case PROC_RLIMIT_CPU:
            proc->limits.cpu_time_ns = limit;
            break;
        case PROC_RLIMIT_MEMORY:
            proc->limits.max_memory = limit;
            break;
        case PROC_RLIMIT_FILES:
            proc->limits.max_files = limit;
            break;
        case PROC_RLIMIT_THREADS:
            proc->limits.max_threads = limit;
            break;
        case PROC_RLIMIT_CHILDREN:
            proc->limits.max_children = limit;
            break;
        default:
            unlock_scheduler();
            return -1;
    }

    unlock_scheduler();
    return 0;
}

uint64_t proc_get_rlimit(pcb_t *proc, int resource) {
    if (proc == NULL) return 0;

    switch (resource) {
        case PROC_RLIMIT_CPU:
            return proc->limits.cpu_time_ns;
        case PROC_RLIMIT_MEMORY:
            return proc->limits.max_memory;
        case PROC_RLIMIT_FILES:
            return proc->limits.max_files;
        case PROC_RLIMIT_THREADS:
            return proc->limits.max_threads;
        case PROC_RLIMIT_CHILDREN:
            return proc->limits.max_children;
        default:
            return 0;
    }
}

void proc_get_stats(pcb_t *proc, proc_stats_t *stats) {
    if (proc == NULL || stats == NULL) return;

    lock_scheduler();
    *stats = proc->stats;
    unlock_scheduler();
}

void proc_update_cpu_time(pcb_t *proc) {
    if (proc == NULL) return;

    lock_scheduler();

    uint64_t total_time = 0;
    for (uint32_t i = 0; i < proc->thread_count; i++) {
        if (proc->threads[i] != NULL) {
            total_time += proc->threads[i]->time_used_ns;
        }
    }

    proc->cpu_time_ns = total_time;

    unlock_scheduler();
}

void proc_for_each(proc_iter_fn fn, void *data) {
    if (fn == NULL) return;

    lock_scheduler();

    pcb_t *proc = process_list;
    while (proc != NULL) {
        fn(proc, data);
        proc = proc->next;
    }

    unlock_scheduler();
}

void proc_for_each_child(pcb_t *parent, proc_iter_fn fn, void *data) {
    if (parent == NULL || fn == NULL) return;

    lock_scheduler();

    pcb_t *child = parent->first_child;
    while (child != NULL) {
        fn(child, data);
        child = child->next_sibling;
    }

    unlock_scheduler();
}

void proc_dump_info(pcb_t *proc) {
    if (proc == NULL) {
        return;
    }

    const char *state_str[] = {"NEW", "RUNNING", "READY", "WAITING", "TERMINATED", "ZOMBIE"};

    printk("  [%lu] %s - state: %s, priority: %u, threads: %u\n",
           proc->pid, proc->name,
           state_str[proc->state],
           proc->priority,
           proc->thread_count);

    if (proc->parent) {
        printk("    parent: [%lu] %s\n", proc->parent->pid, proc->parent->name);
    }

    for (uint32_t i = 0; i < proc->thread_count; i++) {
        if (proc->threads[i] != NULL) {
            printk("    thread %u: [%lu] %s\n",
                   i, proc->threads[i]->tid, proc->threads[i]->name);
        }
    }
}

void proc_dump_detailed(pcb_t *proc) {
    if (proc == NULL) return;

    const char *state_str[] = {"NEW", "RUNNING", "READY", "WAITING", "TERMINATED", "ZOMBIE"};

    printk("process details: [pid %lu] %s:\n", proc->pid, proc->name);
    printk("state: %s, priority: %u\n", state_str[proc->state], proc->priority);
    printk("Created: %lu ms ago\n", (get_time_since_boot_ns() - proc->time_created_ns) / 1000000);
    printk("cpu time: %lu ms\n", proc->cpu_time_ns / 1000000);
    printk("exit code: %d\n", proc->exit_code);

    printk("\nprocess group: %lu, session: %lu\n", proc->pgid, proc->sid);
    printk("uid: %u, Euid: %u, gid: %u, Egid: %u\n",
           proc->cred.uid, proc->cred.euid, proc->cred.gid, proc->cred.egid);

    printk("cwd: %s\n", proc->cwd);

    printk("\nThreads (%u):\n", proc->thread_count);
    for (uint32_t i = 0; i < proc->thread_count; i++) {
        if (proc->threads[i] != NULL) {
            tcb_t *t = proc->threads[i];
            printk("  [tid %lu] %s - CPU: %lu ms, Switches: %lu\n",
                   t->tid, t->name, t->time_used_ns / 1000000, t->switch_count);
        }
    }

    printk("\nStatistics:\n");
    printk("  page faults: %lu minor, %lu major\n",
           proc->stats.minor_faults, proc->stats.major_faults);
    printk("  context switches: %lu\n", proc->stats.context_switches);
    printk("  signals: %lu received, %lu sent\n",
           proc->stats.signals_received, proc->stats.signals_sent);
    printk("  syscalls: %lu\n", proc->stats.syscalls);

    printk("=================================\n\n");
}

void proc_dump_signals(pcb_t *proc) {
    if (proc == NULL) return;

    printk("signals for [pid %lu] %s:\n", proc->pid, proc->name);
    printk("pending: 0x%016lx\n", proc->sig_pending);
    printk("blocked: 0x%016lx\n", proc->sig_blocked);
    printk("caught:  0x%016lx\n", proc->sig_caught);

    printk("\nhandlers:\n");
    for (int i = 1; i < PROC_NSIG; i++) {
        if (signal_names[i] != NULL) {
            void (*handler)(int) = proc->sig_handlers[i].handler;
            const char *handler_str;

            if (handler == PROC_SIG_DFL) {
                handler_str = "DEFAULT";
            } else if (handler == PROC_SIG_IGN) {
                handler_str = "IGNORE";
            } else {
                handler_str = "CUSTOM";
            }

            if (proc->sig_pending & (1ULL << i)) {
                printk("  sig%-6s: %s [pending]\n", signal_names[i], handler_str);
            }
        }
    }
    printk("===============================\n\n");
}

void proc_dump_fds(pcb_t *proc) {
    if (proc == NULL) return;

    printk("file descriptors for [pid %lu] %s:\n", proc->pid, proc->name);

    int count = 0;
    for (int i = 0; i < PROC_MAX_FDS; i++) {
        if (proc->fd_table[i] != NULL) {
            file_descriptor_t *fd = proc->fd_table[i];
            printk("  fd %d: flags=0x%x, offset=%lu, refcount=%u\n",
                   i, fd->flags, fd->offset, fd->refcount);
            count++;
        }
    }

    if (count == 0) {
        printk("  (no open file descriptors)\n");
    }

    printk("total: %d open files\n", count);
    printk("=======================================\n\n");
}

void proc_dump_credentials(pcb_t *proc) {
    if (proc == NULL) return;

    printk("credentials for [pid %lu] %s:\n", proc->pid, proc->name);
    printk("Real uid:      %u\n", proc->cred.uid);
    printk("Effective uid: %u\n", proc->cred.euid);
    printk("Saved uid:     %u\n", proc->cred.suid);
    printk("Real gid:      %u\n", proc->cred.gid);
    printk("Effective gid: %u\n", proc->cred.egid);
    printk("Saved gid:     %u\n", proc->cred.sgid);
    printk("====================================\n\n");
}

void proc_dump_all(void) {
    printk("process table:\n");

    lock_scheduler();

    int count = 0;
    pcb_t *proc = process_list;
    while (proc != NULL) {
        proc_dump_info(proc);
        proc = proc->next;
        count++;
    }

    unlock_scheduler();

    printk("total processes: %d\n", count);

    int zombie_count = proc_count_zombies();
    if (zombie_count > 0) {
        printk("zombie processes: %d\n", zombie_count);
    }

    printk("====================\n\n");
}

static void dump_tree_recursive(pcb_t *proc, int depth) {
    if (proc == NULL) {
        return;
    }

    for (int i = 0; i < depth; i++) {
        printk("  ");
    }

    const char *state_str[] = {"NEW", "RUN", "RDY", "WAIT", "TERM", "ZOMB"};
    printk("[pid %lu] %s (%s, %u threads)\n",
           proc->pid, proc->name,
           state_str[proc->state],
           proc->thread_count);

    pcb_t *child = proc->first_child;
    while (child != NULL) {
        dump_tree_recursive(child, depth + 1);
        child = child->next_sibling;
    }
}

void proc_dump_tree(void) {
    printk("process tree:\n");

    lock_scheduler();
    dump_tree_recursive(init_process, 0);
    unlock_scheduler();

    printk("====================\n\n");
}