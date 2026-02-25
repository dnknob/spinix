#ifndef PRINTK_H
#define PRINTK_H

#include <klibc/types.h>

#include <stdarg.h>

int printk(const char *fmt, ...);
int vprintk(const char *fmt, va_list args);

int printk_ts(const char *fmt, ...);
int vprintk_ts(const char *fmt, va_list args);

int snprintk(char *buf, size_t size, const char *fmt, ...);
int vsnprintk(char *buf, size_t size, const char *fmt, va_list args);

#endif