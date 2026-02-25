#include <arch/x86_64/intr.h>

#include <core/assertk.h>

#include <video/printk.h>

#include <stdint.h>

void _kassert(const char *expr, const char *file, const char *line) {
    printk("\n\n");
    printk("   kernel PANIC - ASSERTION FAILED  \n");
    printk("====================================\n\n");
    printk("assertion: %s\n", expr);
    printk("file: %s\n", file);
    printk("line: %s\n\n", line);
    
    kernel_panic("Assertion failure - halting system");
}

#if __STDC_VERSION__ >= 199901L
void _kassert_99(const char *expr, const char *func, const char *file, const char *line) {
    printk("\n\n");
    printk("   kernel PANIC - ASSERTION FAILED  \n");
    printk("====================================\n\n");
    printk("assertion: %s\n", expr);
    printk("function: %s\n", func);
    printk("file: %s\n", file);
    printk("line: %s\n\n", line);
    
    kernel_panic("Assertion failure - halting system");
}
#endif
