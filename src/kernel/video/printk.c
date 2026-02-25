#include <arch/x86_64/tsc.h>

#include <video/flanterm.h>
#include <video/printk.h>

#include <core/spinlock.h>

#include <klibc/string.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>

static int column_position = 0;

static spinlock_irq_t printk_lock = SPINLOCK_IRQ_INIT;

extern struct flanterm_context *g_ft_ctx;

static int uitoa(unsigned long long num, char *buf, int base, bool uppercase) {
    const char *digits_lower = "0123456789abcdef";
    const char *digits_upper = "0123456789ABCDEF";
    const char *digits = uppercase ? digits_upper : digits_lower;
    char tmp[32];
    int i = 0, j = 0;

    if (num == 0) {
        buf[j++] = '0';
        return j;
    }

    while (num != 0) {
        tmp[i++] = digits[num % base];
        num /= base;
    }

    while (i > 0) {
        buf[j++] = tmp[--i];
    }

    return j;
}

static int itoa(long long num, char *buf) {
    int j = 0;

    if (num < 0) {
        buf[j++] = '-';
        num = -num;
    }

    j += uitoa((unsigned long long)num, buf + j, 10, false);
    return j;
}

int vsnprintk(char *buf, size_t size, const char *fmt, va_list args) {
    size_t pos = 0;
    int total = 0;

    if (!buf || size == 0) {
        return 0;
    }

    size_t max_pos = size - 1;

    while (*fmt) {
        if (*fmt != '%') {
            if (pos < max_pos) {
                buf[pos++] = *fmt;
            }
            total++;
            fmt++;
            continue;
        }

        fmt++; // skip '%'

        bool zero_pad = false;
        bool left_align = false;
        int width = 0;
        bool long_mode = false;
        bool longlong_mode = false;

        while (*fmt == '0' || *fmt == '-') {
            if (*fmt == '0') zero_pad = true;
            if (*fmt == '-') left_align = true;
            fmt++;
        }

        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        if (*fmt == 'l') {
            fmt++;
            long_mode = true;
            if (*fmt == 'l') {
                fmt++;
                longlong_mode = true;
            }
        } else if (*fmt == 'z') {
            fmt++;
            long_mode = true;
        }

        char tmp[64];
        int len = 0;

        switch (*fmt) {
            case 's': {
                const char *s = va_arg(args, const char *);
                if (!s) s = "(null)";
                len = strlen(s);

                if (!left_align && width > len) {
                    int padding = width - len;
                    for (int i = 0; i < padding; i++) {
                        if (pos < max_pos) buf[pos] = ' ';
                        pos++;
                        total++;
                    }
                }

                for (int i = 0; i < len; i++) {
                    if (pos < max_pos) buf[pos] = s[i];
                    pos++;
                    total++;
                }

                if (left_align && width > len) {
                    int padding = width - len;
                    for (int i = 0; i < padding; i++) {
                        if (pos < max_pos) buf[pos] = ' ';
                        pos++;
                        total++;
                    }
                }
                break;
            }

            case 'd':
            case 'i': {
                long long val;
                if (longlong_mode) {
                    val = va_arg(args, long long);
                } else if (long_mode) {
                    val = va_arg(args, long);
                } else {
                    val = va_arg(args, int);
                }
                len = itoa(val, tmp);
                
                if (!left_align && width > len) {
                    char pad_char = zero_pad ? '0' : ' ';
                    for (int i = 0; i < width - len; i++) {
                        if (pos < max_pos) buf[pos] = pad_char;
                        pos++;
                        total++;
                    }
                }
                
                for (int i = 0; i < len; i++) {
                    if (pos < max_pos) buf[pos] = tmp[i];
                    pos++;
                    total++;
                }
                
                if (left_align && width > len) {
                    for (int i = 0; i < width - len; i++) {
                        if (pos < max_pos) buf[pos] = ' ';
                        pos++;
                        total++;
                    }
                }
                break;
            }

            case 'u': {
                unsigned long long val;
                if (longlong_mode) {
                    val = va_arg(args, unsigned long long);
                } else if (long_mode) {
                    val = va_arg(args, unsigned long);
                } else {
                    val = va_arg(args, unsigned int);
                }
                len = uitoa(val, tmp, 10, false);
                
                if (!left_align && width > len) {
                    char pad_char = zero_pad ? '0' : ' ';
                    for (int i = 0; i < width - len; i++) {
                        if (pos < max_pos) buf[pos] = pad_char;
                        pos++;
                        total++;
                    }
                }
                
                for (int i = 0; i < len; i++) {
                    if (pos < max_pos) buf[pos] = tmp[i];
                    pos++;
                    total++;
                }
                
                if (left_align && width > len) {
                    for (int i = 0; i < width - len; i++) {
                        if (pos < max_pos) buf[pos] = ' ';
                        pos++;
                        total++;
                    }
                }
                break;
            }

            case 'x':
            case 'X': {
                unsigned long long val;
                if (longlong_mode) {
                    val = va_arg(args, unsigned long long);
                } else if (long_mode) {
                    val = va_arg(args, unsigned long);
                } else {
                    val = va_arg(args, unsigned int);
                }
                len = uitoa(val, tmp, 16, *fmt == 'X');
                
                if (!left_align && width > len) {
                    char pad_char = zero_pad ? '0' : ' ';
                    for (int i = 0; i < width - len; i++) {
                        if (pos < max_pos) buf[pos] = pad_char;
                        pos++;
                        total++;
                    }
                }
                
                for (int i = 0; i < len; i++) {
                    if (pos < max_pos) buf[pos] = tmp[i];
                    pos++;
                    total++;
                }
                
                if (left_align && width > len) {
                    for (int i = 0; i < width - len; i++) {
                        if (pos < max_pos) buf[pos] = ' ';
                        pos++;
                        total++;
                    }
                }
                break;
            }

            case 'p': {
                void *ptr = va_arg(args, void *);
                if (pos < max_pos) buf[pos] = '0';
                pos++;
                total++;
                if (pos < max_pos) buf[pos] = 'x';
                pos++;
                total++;

                len = uitoa((unsigned long long)(uintptr_t)ptr, tmp, 16, false);
                int ptr_width = sizeof(void *) * 2;
                for (int i = len; i < ptr_width; i++) {
                    if (pos < max_pos) buf[pos] = '0';
                    pos++;
                    total++;
                }
                for (int i = 0; i < len; i++) {
                    if (pos < max_pos) buf[pos] = tmp[i];
                    pos++;
                    total++;
                }
                break;
            }

            case 'c': {
                char c = (char)va_arg(args, int);
                if (pos < max_pos) buf[pos] = c;
                pos++;
                total++;
                break;
            }

            case '%': {
                if (pos < max_pos) buf[pos] = '%';
                pos++;
                total++;
                break;
            }

            default: {
                if (pos < max_pos) buf[pos] = '%';
                pos++;
                total++;
                if (pos < max_pos) buf[pos] = *fmt;
                pos++;
                total++;
                break;
            }
        }

        fmt++;
    }

    if (pos < size) {
        buf[pos] = '\0';
    } else {
        buf[max_pos] = '\0';
    }

    return total;
}

static int vprintk_internal(const char *fmt, va_list args, bool with_timestamp) {
    spinlock_irq_acquire(&printk_lock);

    if (!g_ft_ctx) {
        spinlock_irq_release(&printk_lock);
        return 0;
    }

    char fmtbuf[1024];
    int len = vsnprintk(fmtbuf, sizeof(fmtbuf), fmt, args);
    if (len < 0) len = 0;
    if (len > (int)sizeof(fmtbuf)) len = sizeof(fmtbuf);

    char outbuf[256];
    int outpos = 0;
    int total = 0;

    for (int i = 0; i < len; i++) {
        char c = fmtbuf[i];

        if (with_timestamp && column_position == 0) {
            uint64_t ns = tsc_get_current_ns();
            uint64_t sec = ns / 1000000000ULL;
            uint64_t usec = (ns % 1000000000ULL) / 1000ULL;

            char ts[32];
            int tslen = snprintk(
                ts, sizeof(ts),
                "[%5llu.%06llu] ",
                (unsigned long long)sec,
                (unsigned long long)usec
            );

            flanterm_write(g_ft_ctx, ts, tslen);
            total += tslen;
            column_position += tslen;
        }

        if (c == '\t') {
            int next_stop = ((column_position / 8) + 1) * 8;
            while (column_position < next_stop) {
                outbuf[outpos++] = ' ';
                column_position++;

                if (outpos == sizeof(outbuf)) {
                    flanterm_write(g_ft_ctx, outbuf, outpos);
                    total += outpos;
                    outpos = 0;
                }
            }
            continue;
        }

        outbuf[outpos++] = c;
        column_position++;

        if (c == '\n')
            column_position = 0;

        if (outpos == sizeof(outbuf)) {
            flanterm_write(g_ft_ctx, outbuf, outpos);
            total += outpos;
            outpos = 0;
        }
    }

    if (outpos > 0) {
        flanterm_write(g_ft_ctx, outbuf, outpos);
        total += outpos;
    }

    flanterm_flush(g_ft_ctx);

    spinlock_irq_release(&printk_lock);
    return total;
}

int vprintk(const char *fmt, va_list args) {
    return vprintk_internal(fmt, args, false);
}

int printk(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = vprintk(fmt, args);
    va_end(args);
    return ret;
}

int vprintk_ts(const char *fmt, va_list args) {
    return vprintk_internal(fmt, args, true);
}

int printk_ts(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = vprintk_ts(fmt, args);
    va_end(args);
    return ret;
}

int snprintk(char *buf, size_t size, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = vsnprintk(buf, size, fmt, args);
    va_end(args);
    return ret;
}
