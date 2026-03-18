#ifndef SYSCALL_UTIL_H
#define SYSCALL_UTIL_H

#include <klibc/types.h>

#include <fs/vfs.h>

typedef struct {
    uint64_t st_dev;
    uint64_t st_ino;
    uint64_t st_nlink;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t __pad0;
    uint64_t st_rdev;
    int64_t  st_size;
    int64_t  st_blksize;
    int64_t  st_blocks;
    uint64_t st_atime;
    uint64_t st_atime_nsec;
    uint64_t st_mtime;
    uint64_t st_mtime_nsec;
    uint64_t st_ctime;
    uint64_t st_ctime_nsec;
    int64_t  __unused[3];
} __attribute__((packed)) linux_stat_t;

typedef struct {
    uint64_t sa_handler;
    uint64_t sa_flags;
    uint64_t sa_restorer;
    uint64_t sa_mask;
} __attribute__((packed)) linux_sigaction_t;

typedef struct {
    int64_t tv_sec;
    int64_t tv_nsec;
} __attribute__((packed)) linux_timespec_t;

typedef struct {
    int64_t tv_sec;
    int64_t tv_usec;
} __attribute__((packed)) linux_timeval_t;

int validate_user_buf(const void *ptr, size_t len);
int validate_user_str(const char *ptr, size_t *out_len);

int stat_fill_user(uint64_t statbuf_addr, const vfs_stat_t *vs);

uint32_t mmap_prot_to_vmm(uint64_t prot);

void clock_subsystem_init(void);
uint64_t clock_get_realtime_base(void);

#endif