#ifndef _IO_H
#define _IO_H

#include <klibc/types.h>

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %b0, %w1" : : "a"(val), "Nd"(port) : "memory");
}
static inline void outw(uint16_t port, uint16_t val)
{
    __asm__ volatile ("outw %w0, %w1" : : "a"(val), "Nd"(port) : "memory");
}
static inline void outl(uint16_t port, uint32_t val)
{
    __asm__ volatile ("outl %0, %w1" : : "a"(val), "Nd"(port) : "memory");
}
static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    __asm__ volatile ("inb %w1, %b0" : "=a"(ret) : "Nd"(port) : "memory");
    return ret;
}
static inline uint16_t inw(uint16_t port)
{
    uint16_t ret;
    __asm__ volatile ("inw %w1, %w0" : "=a"(ret) : "Nd"(port) : "memory");
    return ret;
}
static inline uint32_t inl(uint16_t port)
{
    uint32_t ret;
    __asm__ volatile ("inl %w1, %0" : "=a"(ret) : "Nd"(port) : "memory");
    return ret;
}
static inline void io_wait(void)
{
    outb(0x80, 0);
}

static inline uint64_t rdmsr(uint32_t msr)
{
    uint32_t lo, hi;
    __asm__ volatile (
        "rdmsr"
        : "=a"(lo), "=d"(hi)
        : "c"(msr)
        : "memory"
    );
    return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr(uint32_t msr, uint64_t value)
{
    uint32_t lo = (uint32_t)(value & 0xFFFFFFFFULL);
    uint32_t hi = (uint32_t)(value >> 32);
    __asm__ volatile (
        "wrmsr"
        :
        : "c"(msr), "a"(lo), "d"(hi)
        : "memory"
    );
}

#endif