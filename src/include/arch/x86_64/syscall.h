#ifndef SYSCALL_H
#define SYSCALL_H

#include <klibc/types.h>

#define SYS_READ            0
#define SYS_WRITE           1
#define SYS_OPEN            2
#define SYS_CLOSE           3
#define SYS_STAT            4
#define SYS_FSTAT           5
#define SYS_LSEEK           8
#define SYS_MMAP            9
#define SYS_MUNMAP          11
#define SYS_BRK             12
#define SYS_RT_SIGACTION    13
#define SYS_RT_SIGPROCMASK  14
#define SYS_DUP             32
#define SYS_DUP2            33
#define SYS_GETPID          39
#define SYS_EXIT            60
#define SYS_GETCWD          79
#define SYS_CHDIR           80
#define SYS_MKDIR           83
#define SYS_RMDIR           84
#define SYS_UNLINK          87
#define SYS_GETTIMEOFDAY    96
#define SYS_GETPPID         110
#define SYS_GETDENTS64      217
#define SYS_CLOCK_GETTIME   228
#define SYS_EXIT_GROUP      231

#define MMAP_PROT_NONE      0
#define MMAP_PROT_READ      (1 << 0)
#define MMAP_PROT_WRITE     (1 << 1)
#define MMAP_PROT_EXEC      (1 << 2)

#define MMAP_MAP_SHARED     (1 << 0)
#define MMAP_MAP_PRIVATE    (1 << 1)
#define MMAP_MAP_FIXED      (1 << 4)
#define MMAP_MAP_ANONYMOUS  (1 << 5)

#define MMAP_FAILED         ((uint64_t)-1ULL)   /* (void *)-1 */
#define SIG_UNCATCHABLE     ((1ULL << 9) | (1ULL << 19))  /* SIGKILL | SIGSTOP */

#define SIG_BLOCK    0
#define SIG_UNBLOCK  1
#define SIG_SETMASK  2

#define CLOCK_REALTIME          0
#define CLOCK_MONOTONIC         1
#define CLOCK_MONOTONIC_RAW     4
#define CLOCK_REALTIME_COARSE   5
#define CLOCK_MONOTONIC_COARSE  6
#define CLOCK_BOOTTIME          7

#define IA32_EFER       0xC0000080
#define IA32_STAR       0xC0000081
#define IA32_LSTAR      0xC0000082
#define IA32_SFMASK     0xC0000084

#define SA_RESTORER     0x04000000UL

extern uint64_t syscall_kernel_rsp;

void     syscall_init(void);
void     stdin_init(void);

uint64_t syscall_dispatch(uint64_t num,
                          uint64_t a1, uint64_t a2, uint64_t a3,
                          uint64_t a4, uint64_t a5, uint64_t a6);

extern void syscall_entry(void);

#endif