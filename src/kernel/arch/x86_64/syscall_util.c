#include <arch/x86_64/syscall_util.h>
#include <arch/x86_64/syscall.h>
#include <arch/x86_64/rtc.h>
#include <arch/x86_64/tsc.h>

#include <mm/vmm.h>
#include <mm/paging.h>

#include <core/proc.h>

int validate_user_buf(const void *ptr, size_t len)
{
    if (ptr == NULL || len == 0)
        return -1;

    uint64_t start = (uint64_t)ptr;
    uint64_t end   = start + len;

    if (start >= 0x00007FFFFFFFFFFFULL || end > 0x00007FFFFFFFFFFFULL)
        return -1;

    pcb_t *proc = proc_get_current();
    if (proc == NULL || proc->vm_space == NULL)
        return -1;

    vm_space_t *space = (vm_space_t *)proc->vm_space;
    uint64_t    page  = start & ~(uint64_t)(PAGE_SIZE - 1);

    while (page < end) {
        if (!vmm_is_mapped(space, page))
            return -1;
        page += PAGE_SIZE;
    }
    return 0;
}

int validate_user_str(const char *ptr, size_t *out_len)
{
    if (ptr == NULL)
        return -14;     /* -EFAULT */

    pcb_t *proc = proc_get_current();
    if (proc == NULL || proc->vm_space == NULL)
        return -14;

    vm_space_t *space = (vm_space_t *)proc->vm_space;

    for (size_t i = 0; i < VFS_PATH_MAX; i++) {
        uint64_t addr = (uint64_t)(ptr + i);

        if (i == 0 || (addr & (PAGE_SIZE - 1)) == 0) {
            uint64_t page = addr & ~(uint64_t)(PAGE_SIZE - 1);
            if (!vmm_is_mapped(space, page))
                return -14;     /* -EFAULT */
        }

        if (ptr[i] == '\0') {
            if (out_len)
                *out_len = i;
            return 0;
        }
    }

    return -36;     /* -ENAMETOOLONG */
}

int stat_fill_user(uint64_t statbuf_addr, const vfs_stat_t *vs)
{
    if (validate_user_buf((void *)statbuf_addr, sizeof(linux_stat_t)) != 0)
        return -14;     /* -EFAULT */

    linux_stat_t *ls = (linux_stat_t *)statbuf_addr;

    ls->st_dev        = (uint64_t)vs->st_dev;
    ls->st_ino        = (uint64_t)vs->st_ino;
    ls->st_nlink      = (uint64_t)vs->st_nlink;
    ls->st_mode       = (uint32_t)vs->st_mode;
    ls->st_uid        = (uint32_t)vs->st_uid;
    ls->st_gid        = (uint32_t)vs->st_gid;
    ls->__pad0        = 0;
    ls->st_rdev       = (uint64_t)vs->st_rdev;
    ls->st_size       = (int64_t) vs->st_size;
    ls->st_blksize    = vs->st_blksize ? (int64_t)vs->st_blksize : 4096LL;
    ls->st_blocks     = (int64_t) vs->st_blocks;
    ls->st_atime      = (uint64_t)vs->st_atime;
    ls->st_atime_nsec = 0;
    ls->st_mtime      = (uint64_t)vs->st_mtime;
    ls->st_mtime_nsec = 0;
    ls->st_ctime      = (uint64_t)vs->st_ctime;
    ls->st_ctime_nsec = 0;
    ls->__unused[0]   = 0;
    ls->__unused[1]   = 0;
    ls->__unused[2]   = 0;

    return 0;
}

uint32_t mmap_prot_to_vmm(uint64_t prot)
{
    uint32_t f = VMM_USER;
    if (prot & MMAP_PROT_READ)  f |= VMM_READ;
    if (prot & MMAP_PROT_WRITE) f |= VMM_WRITE;
    if (prot & MMAP_PROT_EXEC)  f |= VMM_EXEC;
    return f;
}

static uint64_t s_realtime_base = 0;

static const uint8_t s_days_in_month[12] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

static inline int s_is_leap(uint32_t y)
{
    return (y % 4 == 0) && ((y % 100 != 0) || (y % 400 == 0));
}

static uint64_t rtc_to_unix(const rtc_time_t *t)
{
    uint32_t y    = (uint32_t)t->year;
    uint64_t days = 0;

    for (uint32_t yr = 1970; yr < y; yr++)
        days += s_is_leap(yr) ? 366u : 365u;

    for (uint32_t m = 1; m < (uint32_t)t->month; m++) {
        days += s_days_in_month[m - 1];
        if (m == 2 && s_is_leap(y))
            days++;
    }

    days += (uint32_t)t->day - 1u;

    return days * 86400ULL
         + (uint64_t)t->hour   * 3600ULL
         + (uint64_t)t->minute *   60ULL
         + (uint64_t)t->second;
}

void clock_subsystem_init(void)
{
    rtc_time_t t;
    rtc_read(&t);

    uint64_t rtc_unix = rtc_to_unix(&t);
    uint64_t uptime_s = tsc_get_uptime_ns() / 1000000000ULL;

    s_realtime_base = (rtc_unix > uptime_s) ? (rtc_unix - uptime_s) : rtc_unix;
}

uint64_t clock_get_realtime_base(void)
{
    return s_realtime_base;
}