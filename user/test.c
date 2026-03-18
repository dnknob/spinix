static long sys_write(int fd, const char *buf, unsigned long len)
{
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(1UL), "D"((unsigned long)fd), "S"(buf), "d"(len)
        : "rcx", "r11", "memory"
    );
    return ret;
}

int main(void)
{
    sys_write(1, "Hello, World!\n", 14);
    return 0;
}