#include <arch/x86_64/syscall.h>
#include <arch/x86_64/syscall_util.h>

#include <arch/x86_64/tsc.h>

#include <drivers/input/kb.h>

#include <fs/vfs.h>

#include <core/scheduler.h>
#include <core/proc.h>

#include <mm/heap.h>
#include <mm/paging.h>
#include <mm/mmu.h>
#include <mm/pmm.h>
#include <mm/vmm.h>

#include <video/printk.h>
#include <video/log.h>

#include <stdint.h>
#include <stdbool.h>

extern uint64_t syscall_user_rip;
extern uint64_t syscall_user_rsp;
extern uint64_t syscall_user_rflags;

static inline uint64_t rdmsr(uint32_t msr)
{
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr(uint32_t msr, uint64_t val)
{
    __asm__ volatile("wrmsr" :: "c"(msr),
                               "a"((uint32_t)val),
                               "d"((uint32_t)(val >> 32)));
}

void syscall_init(void)
{
    ebegin("Starting syscall interface");
    wrmsr(IA32_EFER,   rdmsr(IA32_EFER) | 1);
    wrmsr(IA32_STAR,   ((uint64_t)0x10 << 48) | ((uint64_t)0x08 << 32));
    wrmsr(IA32_LSTAR,  (uint64_t)syscall_entry);
    wrmsr(IA32_SFMASK, (1 << 9));
    clock_subsystem_init();
    eend(0, NULL);
}

#define STDIN_BUF_SIZE  512
#define STDIN_BUF_MASK  (STDIN_BUF_SIZE - 1)

static volatile char     stdin_ring[STDIN_BUF_SIZE];
static volatile uint32_t stdin_head   = 0;
static volatile uint32_t stdin_tail   = 0;
static volatile uint32_t stdin_lines  = 0;
static volatile tcb_t   *stdin_waiter = NULL;

static void stdin_enqueue(char c)
{
    uint32_t next = (stdin_head + 1) & STDIN_BUF_MASK;
    if (next == stdin_tail) return;     /* full — drop */
    stdin_ring[stdin_head] = c;
    stdin_head = next;
}

static void stdin_kb_callback(kb_event_t *ev)
{
    if (!ev->pressed) return;

    uint8_t kc = ev->keycode;

    if (kc == KEY_ENTER) {
        printk("\n");
        stdin_enqueue('\n');
        stdin_lines++;

        tcb_t *waiter = (tcb_t *)stdin_waiter;
        if (waiter != NULL) {
            stdin_waiter = NULL;
            unblock_task(waiter);
        }
        return;
    }

    if (kc == KEY_BACKSPACE) {
        if (stdin_head != stdin_tail) {
            uint32_t prev = (stdin_head - 1) & STDIN_BUF_MASK;
            if (stdin_ring[prev] != '\n') {
                stdin_head = prev;
                printk("\b \b");
            }
        }
        return;
    }

    if (kc < 0x20 || kc > 0x7E) return;

    printk("%c", (char)kc);
    stdin_enqueue((char)kc);
}

void stdin_init(void)
{
    stdin_head   = 0;
    stdin_tail   = 0;
    stdin_lines  = 0;
    stdin_waiter = NULL;
    kb_set_callback(stdin_kb_callback);

    pcb_t *proc = proc_get_current();
    if (proc != NULL) {
        for (int i = 0; i < 3; i++) {
            file_descriptor_t *fde =
                (file_descriptor_t *)kmalloc(sizeof(file_descriptor_t));
            if (fde == NULL) continue;

            fde->file     = NULL;   /* sentinel: handled by syscall layer */
            fde->flags    = (i == 0) ? VFS_O_RDONLY : VFS_O_WRONLY;
            fde->offset   = 0;
            fde->refcount = 1;

            proc_fd_install(proc, i, fde);
        }
    }

    einfo("stdin: canonical keyboard input ready");
}

static int64_t sys_read(uint64_t fd, uint64_t buf_addr, uint64_t count)
{
    if (count == 0) return 0;

    if (validate_user_buf((void *)buf_addr, count) != 0)
        return -14LL;   /* -EFAULT */

    if (fd == 0) {
        char *buf = (char *)buf_addr;

        while (stdin_lines == 0) {
            tcb_t *self = get_current_task();
            if (stdin_waiter == NULL)
                stdin_waiter = self;

            if (stdin_lines == 0)
                block_task(TASK_STATE_WAITING_EVENT);

            if ((tcb_t *)stdin_waiter == self)
                stdin_waiter = NULL;
        }

        int64_t n = 0;
        while ((uint64_t)n < count && stdin_tail != stdin_head) {
            char c = stdin_ring[stdin_tail];
            stdin_tail = (stdin_tail + 1) & STDIN_BUF_MASK;
            buf[n++] = c;
            if (c == '\n') {
                stdin_lines--;
                break;
            }
        }
        return n;
    }

    if (fd <= 2) return -9LL;   /* -EBADF */

    pcb_t *proc = proc_get_current();
    if (proc == NULL || fd >= PROC_MAX_FDS) return -9LL;

    file_descriptor_t *fde = proc_fd_get(proc, (int)fd);
    if (fde == NULL || fde->file == NULL) return -9LL;

    vfs_file_t *vfile = (vfs_file_t *)fde->file;
    int ret = vfs_read(vfile, (void *)buf_addr, (size_t)count);
    if (ret < 0) return (int64_t)ret;

    fde->offset = vfile->f_offset;
    return (int64_t)ret;
}

static int64_t write_to_terminal(const char *buf, uint64_t count)
{
    for (uint64_t i = 0; i < count; i++)
        printk("%c", buf[i]);
    return (int64_t)count;
}

static int64_t sys_write(uint64_t fd, uint64_t buf_addr, uint64_t count)
{
    if (count == 0) return 0;

    if (validate_user_buf((const void *)buf_addr, count) != 0) {
        ewarn("syscall: write: bad user ptr 0x%lx", buf_addr);
        return -14LL;   /* -EFAULT */
    }

    const char *buf = (const char *)buf_addr;

    if (fd <= 2)
        return write_to_terminal(buf, count);

    pcb_t *proc = proc_get_current();
    if (proc == NULL || fd >= PROC_MAX_FDS) return -9LL;

    file_descriptor_t *fde = proc_fd_get(proc, (int)fd);
    if (fde == NULL || fde->file == NULL) return -9LL;

    vfs_file_t *vfile = (vfs_file_t *)fde->file;
    int ret = vfs_write(vfile, buf, (size_t)count);
    if (ret < 0) return (int64_t)ret;

    fde->offset = vfile->f_offset;
    return (int64_t)ret;
}

static int64_t sys_open(uint64_t path_addr, uint64_t flags, uint64_t mode)
{
    size_t path_len = 0;
    int vret = validate_user_str((const char *)path_addr, &path_len);
    if (vret != 0) return (int64_t)vret;
    if (path_len == 0) return -2LL;     /* -ENOENT */

    uint32_t acc = (uint32_t)(flags & VFS_O_ACCMODE);
    if (acc == (VFS_O_RDONLY | VFS_O_WRONLY)) return -22LL; /* -EINVAL */
    if ((flags & VFS_O_TRUNC) && acc == VFS_O_RDONLY) return -22LL;

    pcb_t *proc = proc_get_current();
    if (proc == NULL) return -9LL;

    if (proc->limits.max_files != UINT64_MAX) {
        int open_count = 0;
        for (int i = 0; i < PROC_MAX_FDS; i++)
            if (proc->fd_table[i] != NULL) open_count++;
        if ((uint64_t)open_count >= proc->limits.max_files)
            return -24LL;   /* -EMFILE */
    }

    uint32_t vfs_flags = (uint32_t)(flags & ~VFS_O_CLOEXEC);

    vfs_file_t *vfile = NULL;
    int ret = vfs_open((const char *)path_addr, vfs_flags, (uint32_t)mode, &vfile);
    if (ret != 0) return (int64_t)ret;

    int fd = proc_fd_alloc(proc);
    if (fd < 0 || fd < 3) {
        vfs_close(vfile);
        return -24LL;   /* -EMFILE */
    }

    file_descriptor_t *fde =
        (file_descriptor_t *)kmalloc(sizeof(file_descriptor_t));
    if (fde == NULL) {
        vfs_close(vfile);
        return -12LL;   /* -ENOMEM */
    }

    fde->file     = vfile;
    fde->flags    = (uint32_t)flags;
    fde->offset   = (flags & VFS_O_APPEND) ? (uint64_t)vfile->f_vnode->v_size : 0;
    fde->refcount = 1;

    if (proc_fd_install(proc, fd, fde) != 0) {
        kfree(fde);
        vfs_close(vfile);
        return -9LL;
    }

    proc->stats.syscalls++;
    return (int64_t)fd;
}

static int64_t sys_close(uint64_t fd)
{
    if (fd >= PROC_MAX_FDS) return -9LL;
    if (fd < 3)             return 0;   /* silently succeed for stdin/out/err */

    pcb_t *proc = proc_get_current();
    if (proc == NULL) return -9LL;

    file_descriptor_t *fde = proc_fd_get(proc, (int)fd);
    if (fde == NULL) return -9LL;

    vfs_file_t *vfile = (fde->refcount <= 1) ? (vfs_file_t *)fde->file : NULL;

    proc_fd_close(proc, (int)fd);

    int64_t ret = 0;
    if (vfile != NULL) {
        int vret = vfs_close(vfile);
        if (vret != 0) ret = (int64_t)vret;
    }

    proc->stats.syscalls++;
    return ret;
}

static int64_t sys_stat(uint64_t path_addr, uint64_t statbuf_addr)
{
    size_t path_len = 0;
    int vret = validate_user_str((const char *)path_addr, &path_len);
    if (vret != 0) return (int64_t)vret;
    if (path_len == 0) return -2LL;

    vfs_stat_t vs;
    int ret = vfs_stat((const char *)path_addr, &vs);
    if (ret != 0) return (int64_t)ret;

    return (int64_t)stat_fill_user(statbuf_addr, &vs);
}

static int64_t sys_fstat(uint64_t fd, uint64_t statbuf_addr)
{
    if (fd >= PROC_MAX_FDS) return -9LL;

    if (fd <= 2) {
        if (validate_user_buf((void *)statbuf_addr, sizeof(linux_stat_t)) != 0)
            return -14LL;   /* -EFAULT */

        linux_stat_t *ls = (linux_stat_t *)statbuf_addr;
        for (size_t i = 0; i < sizeof(linux_stat_t); i++)
            ((uint8_t *)ls)[i] = 0;
        ls->st_mode    = 0x2000 | 0666;    /* S_IFCHR | rw-rw-rw- */
        ls->st_nlink   = 1;
        ls->st_blksize = 4096;
        return 0;
    }

    pcb_t *proc = proc_get_current();
    if (proc == NULL) return -9LL;

    file_descriptor_t *fde = proc_fd_get(proc, (int)fd);
    if (fde == NULL || fde->file == NULL) return -9LL;

    vfs_stat_t vs;
    int ret = vfs_fstat((vfs_file_t *)fde->file, &vs);
    if (ret != 0) return (int64_t)ret;

    return (int64_t)stat_fill_user(statbuf_addr, &vs);
}

static int64_t sys_lseek(uint64_t fd, int64_t offset, uint64_t whence)
{
    if (whence > 2)         return -22LL;   /* -EINVAL */
    if (fd <= 2)            return -29LL;   /* -ESPIPE */
    if (fd >= PROC_MAX_FDS) return -9LL;

    pcb_t *proc = proc_get_current();
    if (proc == NULL) return -9LL;

    file_descriptor_t *fde = proc_fd_get(proc, (int)fd);
    if (fde == NULL || fde->file == NULL) return -9LL;

    int64_t result = vfs_lseek((vfs_file_t *)fde->file, offset, (int)whence);
    if (result >= 0)
        fde->offset = (uint64_t)result;
    return result;
}

static uint64_t sys_mmap(uint64_t addr, uint64_t len,
                         uint64_t prot, uint64_t flags,
                         uint64_t fd,   uint64_t offset)
{
    if (len == 0) return MMAP_FAILED;

    uint64_t aligned_len = (len + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);

    pcb_t *proc = proc_get_current();
    if (proc == NULL || proc->vm_space == NULL) return MMAP_FAILED;

    vm_space_t *space     = (vm_space_t *)proc->vm_space;
    uint32_t    vmm_flags = mmap_prot_to_vmm(prot);
    uint64_t    map_addr  = 0;

    if (flags & MMAP_MAP_FIXED) {
        if (addr == 0 || (addr & (PAGE_SIZE - 1)) != 0)
            return MMAP_FAILED;

        uint64_t cur = addr;
        uint64_t end = addr + aligned_len;
        while (cur < end) {
            vm_area_t *a = vmm_find_area(space, cur);
            if (a == NULL) { cur += PAGE_SIZE; continue; }

            uint64_t us = (a->virt_start > addr) ? a->virt_start : addr;
            uint64_t ue = (a->virt_end   < end)  ? a->virt_end   : end;

            if (us > a->virt_start) vmm_split_region(space, us);
            if (ue < a->virt_end) {
                a = vmm_find_area(space, us);
                if (a) vmm_split_region(space, ue);
            }
            vmm_unmap_region(space, us, ue - us);
            cur = ue;
        }

        if (vmm_map_region(space, addr, aligned_len,
                           vmm_flags, VMM_TYPE_ANON, VMM_ALLOC_ZERO, 0) != 0)
            return MMAP_FAILED;

        map_addr = addr;

    } else if (flags & MMAP_MAP_ANONYMOUS) {
        map_addr = vmm_alloc_region(space, aligned_len, vmm_flags, VMM_ALLOC_ZERO);
        if (map_addr == 0) return MMAP_FAILED;

    } else {
        if (fd >= PROC_MAX_FDS) return MMAP_FAILED;

        file_descriptor_t *fde = proc_fd_get(proc, (int)fd);
        if (fde == NULL || fde->file == NULL) return MMAP_FAILED;

        vfs_file_t *vfile = (vfs_file_t *)fde->file;
        vnode_t    *vnode = vfile->f_vnode;

        if (vnode == NULL || vnode->v_ops == NULL || vnode->v_ops->read == NULL)
            return MMAP_FAILED;

        uint64_t file_size = (uint64_t)vnode->v_size;
        if (offset >= file_size) return MMAP_FAILED;

        uint64_t readable = file_size - offset;
        if (readable < aligned_len)
            aligned_len = (readable + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);

        map_addr = vmm_alloc_region(space, aligned_len, vmm_flags, VMM_ALLOC_ZERO);
        if (map_addr == 0) return MMAP_FAILED;

        uint64_t file_off  = offset;
        uint64_t virt      = map_addr;
        uint64_t remaining = (readable < len) ? readable : len;

        while (remaining > 0 && virt < map_addr + aligned_len) {
            uint64_t phys = mmu_virt_to_phys((mmu_context_t *)space->mmu_ctx, virt);
            if (phys == 0) break;

            uint8_t  *kpage = (uint8_t *)PHYS_TO_VIRT(phys);
            uint64_t  chunk = (remaining < PAGE_SIZE) ? remaining : PAGE_SIZE;
            int n = vnode->v_ops->read(vnode, kpage, (size_t)chunk, file_off);
            if (n <= 0) break;

            file_off  += (uint64_t)n;
            virt      += PAGE_SIZE;
            remaining -= (uint64_t)n;
        }
    }

    return map_addr;
}

static int64_t sys_munmap(uint64_t addr, uint64_t len)
{
    if (len == 0 || (addr & (PAGE_SIZE - 1)) != 0)
        return -22LL;   /* -EINVAL */

    pcb_t *proc = proc_get_current();
    if (proc == NULL || proc->vm_space == NULL) return -22LL;

    vm_space_t *space = (vm_space_t *)proc->vm_space;
    uint64_t    start = addr;
    uint64_t    end   = (addr + len + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);
    uint64_t    cur   = start;

    while (cur < end) {
        vm_area_t *area = vmm_find_area(space, cur);
        if (area == NULL) { cur += PAGE_SIZE; continue; }

        uint64_t us = (area->virt_start > start) ? area->virt_start : start;
        uint64_t ue = (area->virt_end   < end)   ? area->virt_end   : end;

        if (us > area->virt_start) {
            if (vmm_split_region(space, us) != 0) return -22LL;
            area = vmm_find_area(space, us);
            if (area == NULL) return -22LL;
        }
        if (ue < area->virt_end) {
            if (vmm_split_region(space, ue) != 0) return -22LL;
        }

        if (vmm_unmap_region(space, us, ue - us) != 0) return -22LL;
        cur = ue;
    }

    return 0;
}

static uint64_t sys_brk(uint64_t addr)
{
    pcb_t *proc = proc_get_current();
    if (proc == NULL) return 0;

    uint64_t old_brk = proc->heap_brk;

    if (addr == 0 || addr < proc->heap_base)
        return old_brk;

    uint64_t    new_brk = PAGE_ALIGN_UP(addr);
    vm_space_t *space   = (vm_space_t *)proc->vm_space;
    if (space == NULL) return old_brk;

    if (new_brk == old_brk) return old_brk;

    if (new_brk > old_brk) {
        if (vmm_map_region(space, old_brk, new_brk - old_brk,
                           VMM_USER | VMM_READ | VMM_WRITE,
                           VMM_TYPE_ANON, VMM_ALLOC_ZERO, 0) != 0)
            return old_brk;
        proc->heap_brk = new_brk;
        return new_brk;
    }

    uint64_t cur = old_brk;
    while (cur > new_brk) {
        vm_area_t *area = vmm_find_area(space, cur - 1);
        if (area == NULL) break;

        uint64_t us = area->virt_start;
        uint64_t ue = area->virt_end;

        if (us < new_brk) us = new_brk;
        if (us > area->virt_start)
            if (vmm_split_region(space, us) != 0) break;

        if (vmm_unmap_region(space, us, ue - us) != 0) break;
        cur = us;
    }

    proc->heap_brk = cur;
    return cur;
}

static uint64_t sys_getpid(void)
{
    pcb_t *proc = proc_get_current();
    return (proc != NULL) ? (uint64_t)proc->pid : 0;
}

static uint64_t sys_getppid(void)
{
    pcb_t *proc = proc_get_current();
    if (proc == NULL || proc->parent == NULL) return 0;
    return (uint64_t)proc->parent->pid;
}

static int64_t sys_unlink(uint64_t path_addr)
{
    size_t path_len = 0;
    int vret = validate_user_str((const char *)path_addr, &path_len);
    if (vret != 0) return (int64_t)vret;
    if (path_len == 0) return -2LL;

    return (int64_t)vfs_unlink((const char *)path_addr);
}

static int64_t sys_mkdir(uint64_t path_addr, uint64_t mode)
{
    size_t path_len = 0;
    int vret = validate_user_str((const char *)path_addr, &path_len);
    if (vret != 0) return (int64_t)vret;
    if (path_len == 0) return -2LL;

    return (int64_t)vfs_mkdir((const char *)path_addr, (uint32_t)mode);
}

static int64_t sys_rmdir(uint64_t path_addr)
{
    size_t path_len = 0;
    int vret = validate_user_str((const char *)path_addr, &path_len);
    if (vret != 0) return (int64_t)vret;
    if (path_len == 0) return -2LL;

    return (int64_t)vfs_rmdir((const char *)path_addr);
}

static int64_t sys_dup(uint64_t oldfd)
{
    if (oldfd >= PROC_MAX_FDS) return -9LL;     /* -EBADF */

    pcb_t *proc = proc_get_current();
    if (proc == NULL) return -9LL;

    file_descriptor_t *old_fde = proc_fd_get(proc, (int)oldfd);
    if (old_fde == NULL) return -9LL;           /* -EBADF: not open */

    int newfd = proc_fd_alloc(proc);
    if (newfd < 0)  return -24LL;               /* -EMFILE */
    if (newfd < 3)  return -24LL;               /* guard: never steal stdio */

    file_descriptor_t *new_fde =
        (file_descriptor_t *)kmalloc(sizeof(file_descriptor_t));
    if (new_fde == NULL) return -12LL;          /* -ENOMEM */

    new_fde->file     = old_fde->file;
    new_fde->flags    = old_fde->flags & ~(uint32_t)VFS_O_CLOEXEC;
    new_fde->offset   = 0;                      /* offset lives in vfs_file_t */
    new_fde->refcount = 1;

    if (new_fde->file != NULL)
        __sync_fetch_and_add(&((vfs_file_t *)new_fde->file)->f_refcount, 1);

    if (proc_fd_install(proc, newfd, new_fde) != 0) {
        /* Undo the refcount bump before bailing */
        if (new_fde->file != NULL)
            __sync_fetch_and_sub(&((vfs_file_t *)new_fde->file)->f_refcount, 1);
        kfree(new_fde);
        return -9LL;
    }

    proc->stats.syscalls++;
    return (int64_t)newfd;
}

static int64_t sys_dup2(uint64_t oldfd, uint64_t newfd)
{
    if (oldfd >= PROC_MAX_FDS) return -9LL;     /* -EBADF */
    if (newfd >= PROC_MAX_FDS) return -9LL;     /* -EBADF */
    if (newfd < 3)             return -9LL;     /* never clobber stdio */

    pcb_t *proc = proc_get_current();
    if (proc == NULL) return -9LL;

    file_descriptor_t *old_fde = proc_fd_get(proc, (int)oldfd);
    if (old_fde == NULL) return -9LL;           /* -EBADF */

    if (oldfd == newfd) return (int64_t)newfd;

    file_descriptor_t *existing = proc_fd_get(proc, (int)newfd);
    if (existing != NULL) {
        vfs_file_t *vfile = NULL;
        if (existing->refcount <= 1)
            vfile = (vfs_file_t *)existing->file;
        proc_fd_close(proc, (int)newfd);
        if (vfile != NULL)
            vfs_close(vfile);
    }

    file_descriptor_t *new_fde =
        (file_descriptor_t *)kmalloc(sizeof(file_descriptor_t));
    if (new_fde == NULL) return -12LL;          /* -ENOMEM */

    new_fde->file     = old_fde->file;
    new_fde->flags    = old_fde->flags & ~(uint32_t)VFS_O_CLOEXEC;
    new_fde->offset   = 0;
    new_fde->refcount = 1;

    if (new_fde->file != NULL)
        __sync_fetch_and_add(&((vfs_file_t *)new_fde->file)->f_refcount, 1);

    if (proc_fd_install(proc, (int)newfd, new_fde) != 0) {
        if (new_fde->file != NULL)
            __sync_fetch_and_sub(&((vfs_file_t *)new_fde->file)->f_refcount, 1);
        kfree(new_fde);
        return -9LL;
    }

    proc->stats.syscalls++;
    return (int64_t)newfd;
}

static int64_t sys_clock_gettime(uint64_t clk_id, uint64_t ts_addr)
{
    if (validate_user_buf((void *)ts_addr, sizeof(linux_timespec_t)) != 0)
        return -14LL;   /* -EFAULT */

    linux_timespec_t *ts = (linux_timespec_t *)ts_addr;
    uint64_t uptime_ns   = tsc_get_uptime_ns();

    switch (clk_id) {
    case CLOCK_REALTIME:
    case CLOCK_REALTIME_COARSE:
        ts->tv_sec  = (int64_t)(clock_get_realtime_base() + uptime_ns / 1000000000ULL);
        ts->tv_nsec = (int64_t)(uptime_ns % 1000000000ULL);
        return 0;

    case CLOCK_MONOTONIC:
    case CLOCK_MONOTONIC_RAW:
    case CLOCK_MONOTONIC_COARSE:
    case CLOCK_BOOTTIME:
        ts->tv_sec  = (int64_t)(uptime_ns / 1000000000ULL);
        ts->tv_nsec = (int64_t)(uptime_ns % 1000000000ULL);
        return 0;

    default:
        return -22LL;   /* -EINVAL */
    }
}

static int64_t sys_gettimeofday(uint64_t tv_addr, uint64_t tz_addr)
{
    (void)tz_addr;  /* timezone intentionally ignored */

    if (tv_addr == 0) return 0;

    if (validate_user_buf((void *)tv_addr, sizeof(linux_timeval_t)) != 0)
        return -14LL;   /* -EFAULT */

    linux_timeval_t *tv = (linux_timeval_t *)tv_addr;
    uint64_t uptime_us  = tsc_get_uptime_ns() / 1000ULL;

    tv->tv_sec  = (int64_t)(clock_get_realtime_base() + uptime_us / 1000000ULL);
    tv->tv_usec = (int64_t)(uptime_us % 1000000ULL);

    return 0;
}

static int64_t sys_rt_sigaction(uint64_t signum, uint64_t act_addr,
                                uint64_t oldact_addr, uint64_t sigsetsize)
{
    if (sigsetsize != 8)                              return -22LL;
    if (signum < 1 || signum >= PROC_NSIG)            return -22LL;
    if (signum == PROC_SIG_KILL || signum == PROC_SIG_STOP) return -22LL;

    pcb_t *proc = proc_get_current();
    if (proc == NULL) return -14LL;

    if (oldact_addr != 0) {
        if (validate_user_buf((void *)oldact_addr, sizeof(linux_sigaction_t)) != 0)
            return -14LL;
        signal_handler_t  *kh  = &proc->sig_handlers[signum];
        linux_sigaction_t *ols = (linux_sigaction_t *)oldact_addr;
        ols->sa_handler  = (uint64_t)(uintptr_t)kh->handler;
        ols->sa_flags    = (uint64_t)kh->flags;
        ols->sa_restorer = kh->restorer;
        ols->sa_mask     = kh->mask;
    }

    if (act_addr != 0) {
        if (validate_user_buf((void *)act_addr, sizeof(linux_sigaction_t)) != 0)
            return -14LL;
        linux_sigaction_t *ns = (linux_sigaction_t *)act_addr;
        signal_handler_t  *kh = &proc->sig_handlers[signum];

        lock_scheduler();
        kh->handler  = (void (*)(int))(uintptr_t)ns->sa_handler;
        kh->flags    = (uint32_t)ns->sa_flags;
        kh->restorer = (ns->sa_flags & SA_RESTORER) ? ns->sa_restorer : 0;
        kh->mask     = ns->sa_mask & ~SIG_UNCATCHABLE;

        if (kh->handler != PROC_SIG_DFL && kh->handler != PROC_SIG_IGN)
            proc->sig_caught |=  (1ULL << signum);
        else
            proc->sig_caught &= ~(1ULL << signum);

        if (kh->handler == PROC_SIG_IGN)
            proc->sig_pending &= ~(1ULL << signum);
        unlock_scheduler();
    }

    proc->stats.syscalls++;
    return 0;
}

static int64_t sys_rt_sigprocmask(uint64_t how, uint64_t set_addr,
                                  uint64_t oldset_addr, uint64_t sigsetsize)
{
    if (sigsetsize != 8) return -22LL;

    pcb_t *proc = proc_get_current();
    if (proc == NULL) return -14LL;

    if (oldset_addr != 0) {
        if (validate_user_buf((void *)oldset_addr, 8) != 0) return -14LL;
        *(uint64_t *)oldset_addr = proc->sig_blocked;
    }

    if (set_addr != 0) {
        if (validate_user_buf((void *)set_addr, 8) != 0) return -14LL;
        uint64_t requested = *(uint64_t *)set_addr & ~SIG_UNCATCHABLE;

        lock_scheduler();
        switch (how) {
        case SIG_BLOCK:   proc->sig_blocked |=  requested; break;
        case SIG_UNBLOCK: proc->sig_blocked &= ~requested; break;
        case SIG_SETMASK: proc->sig_blocked  =  requested; break;
        default: unlock_scheduler(); return -22LL;
        }
        unlock_scheduler();

        if ((how == SIG_UNBLOCK || how == SIG_SETMASK) &&
            (proc->sig_pending & ~proc->sig_blocked)) {
            tcb_t *t = proc->main_thread;
            if (t != NULL && t->state == TASK_STATE_SLEEPING)
                unblock_task(t);
        }
    }

    proc->stats.syscalls++;
    return 0;
}

[[noreturn]] static void do_exit(int status, bool group)
{
    pcb_t *proc = proc_get_current();
    if (proc != NULL) {
        proc->stats.syscalls++;
        if (group)
            proc_terminate(proc, status & 0xFF);
        else
            proc_exit(status & 0xFF);
    }
    for (;;) __asm__ volatile("hlt");
}

[[noreturn]] static void sys_exit_impl(uint64_t status)
{
    do_exit((int)status, false);
}

[[noreturn]] static void sys_exit_group_impl(uint64_t status)
{
    do_exit((int)status, true);
}

uint64_t syscall_dispatch(uint64_t num,
                          uint64_t a1, uint64_t a2, uint64_t a3,
                          uint64_t a4, uint64_t a5, uint64_t a6)
{
    switch (num) {
    case SYS_READ:          return (uint64_t)sys_read(a1, a2, a3);
    case SYS_WRITE:         return (uint64_t)sys_write(a1, a2, a3);
    case SYS_OPEN:          return (uint64_t)sys_open(a1, a2, a3);
    case SYS_CLOSE:         return (uint64_t)sys_close(a1);
    case SYS_STAT:          return (uint64_t)sys_stat(a1, a2);
    case SYS_FSTAT:         return (uint64_t)sys_fstat(a1, a2);
    case SYS_LSEEK:         return (uint64_t)sys_lseek(a1, (int64_t)a2, a3);
    case SYS_DUP:  return (uint64_t)sys_dup(a1);
    case SYS_DUP2: return (uint64_t)sys_dup2(a1, a2);
    case SYS_MMAP:          return sys_mmap(a1, a2, a3, a4, a5, a6);
    case SYS_MUNMAP:        return (uint64_t)sys_munmap(a1, a2);
    case SYS_BRK:           return sys_brk(a1);
    case SYS_GETPID:        return sys_getpid();
    case SYS_GETPPID:       return sys_getppid();
    case SYS_UNLINK:        return (uint64_t)sys_unlink(a1);
    case SYS_MKDIR:         return (uint64_t)sys_mkdir(a1, a2);
    case SYS_RMDIR:         return (uint64_t)sys_rmdir(a1);
    case SYS_CLOCK_GETTIME: return (uint64_t)sys_clock_gettime(a1, a2);
    case SYS_GETTIMEOFDAY:  return (uint64_t)sys_gettimeofday(a1, a2);
    case SYS_RT_SIGACTION:   return (uint64_t)sys_rt_sigaction(a1, a2, a3, a4);
    case SYS_RT_SIGPROCMASK: return (uint64_t)sys_rt_sigprocmask(a1, a2, a3, a4);
    case SYS_EXIT:          sys_exit_impl(a1);
    case SYS_EXIT_GROUP:    sys_exit_group_impl(a1);
    default:
        return (uint64_t)-38LL;     /* -ENOSYS */
    }
}