#include <stddef.h>
#include <stdint.h>

#define SYS_WRITE 1
#define SYS_EXIT  60

static size_t kstrlen(const char *s)
{
    size_t len = 0;
    while (s[len])
        len++;
    return len;
}

static long syscall3(long n, long a1, const void *a2, size_t a3)
{
    long r;
    __asm__ volatile(
        "syscall"
        : "=a"(r)
        : "0"(n), "D"(a1), "S"(a2), "d"(a3)
        : "rcx", "r11", "memory"
    );
    return r;
}

static long syscall1(long n, long a1)
{
    long r;
    __asm__ volatile(
        "syscall"
        : "=a"(r)
        : "0"(n), "D"(a1)
        : "rcx", "r11", "memory"
    );
    return r;
}

static long write(int fd, const void *buf, size_t count)
{
    return syscall3(SYS_WRITE, fd, buf, count);
}

__attribute__((noreturn))
static void exit(int status)
{
    syscall1(SYS_EXIT, status);
    __builtin_unreachable();
}

static void print(const char *s)
{
    size_t len = kstrlen(s);
    write(1, s, len);
}

int main(void)
{
    print("Hello, World!\n");
    return 0;
}