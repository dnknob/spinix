#ifndef _ATOMIC_H
#define _ATOMIC_H

#include <klibc/types.h>

static inline void mfence(void)
{
    __asm__ volatile("mfence" ::: "memory");
}

static inline void sfence(void)
{
    __asm__ volatile("sfence" ::: "memory");
}

static inline void lfence(void)
{
    __asm__ volatile("lfence" ::: "memory");
}

static inline void barrier(void)
{
    __asm__ volatile("" ::: "memory");
}

static inline uint8_t atomic_load_8(volatile uint8_t *ptr)
{
    uint8_t val = *ptr;
    barrier();
    return val;
}

static inline uint16_t atomic_load_16(volatile uint16_t *ptr)
{
    uint16_t val = *ptr;
    barrier();
    return val;
}

static inline uint32_t atomic_load_32(volatile uint32_t *ptr)
{
    uint32_t val = *ptr;
    barrier();
    return val;
}

static inline uint64_t atomic_load_64(volatile uint64_t *ptr)
{
    uint64_t val = *ptr;
    barrier();
    return val;
}

static inline void atomic_store_8(volatile uint8_t *ptr, uint8_t val)
{
    barrier();
    *ptr = val;
    barrier();
}

static inline void atomic_store_16(volatile uint16_t *ptr, uint16_t val)
{
    barrier();
    *ptr = val;
    barrier();
}

static inline void atomic_store_32(volatile uint32_t *ptr, uint32_t val)
{
    barrier();
    *ptr = val;
    barrier();
}

static inline void atomic_store_64(volatile uint64_t *ptr, uint64_t val)
{
    barrier();
    *ptr = val;
    barrier();
}

static inline uint32_t atomic_load_acquire_32(volatile uint32_t *ptr)
{
    uint32_t val = *ptr;
    barrier();
    return val;
}

static inline uint64_t atomic_load_acquire_64(volatile uint64_t *ptr)
{
    uint64_t val = *ptr;
    barrier();
    return val;
}

static inline void atomic_store_release_32(volatile uint32_t *ptr, uint32_t val)
{
    barrier();
    *ptr = val;
}

static inline void atomic_store_release_64(volatile uint64_t *ptr, uint64_t val)
{
    barrier();
    *ptr = val;
}

static inline uint8_t atomic_exchange_8(volatile uint8_t *ptr, uint8_t val)
{
    __asm__ volatile("xchgb %b0, %1" : "+q"(val), "+m"(*ptr) :: "memory");
    return val;
}

static inline uint16_t atomic_exchange_16(volatile uint16_t *ptr, uint16_t val)
{
    __asm__ volatile("xchgw %w0, %1" : "+r"(val), "+m"(*ptr) :: "memory");
    return val;
}

static inline uint32_t atomic_exchange_32(volatile uint32_t *ptr, uint32_t val)
{
    __asm__ volatile("xchgl %0, %1" : "+r"(val), "+m"(*ptr) :: "memory");
    return val;
}

static inline uint64_t atomic_exchange_64(volatile uint64_t *ptr, uint64_t val)
{
    __asm__ volatile("xchgq %0, %1" : "+r"(val), "+m"(*ptr) :: "memory");
    return val;
}

static inline bool atomic_compare_exchange_8(volatile uint8_t *ptr,
                                              uint8_t *expected, uint8_t desired)
{
    uint8_t old = *expected, prev;
    __asm__ volatile("lock cmpxchgb %2, %1"
                     : "=a"(prev), "+m"(*ptr) : "q"(desired), "0"(old) : "memory");
    *expected = prev;
    return prev == old;
}

static inline bool atomic_compare_exchange_16(volatile uint16_t *ptr,
                                               uint16_t *expected, uint16_t desired)
{
    uint16_t old = *expected, prev;
    __asm__ volatile("lock cmpxchgw %w2, %1"
                     : "=a"(prev), "+m"(*ptr) : "r"(desired), "0"(old) : "memory");
    *expected = prev;
    return prev == old;
}

static inline bool atomic_compare_exchange_32(volatile uint32_t *ptr,
                                               uint32_t *expected, uint32_t desired)
{
    uint32_t old = *expected, prev;
    __asm__ volatile("lock cmpxchgl %2, %1"
                     : "=a"(prev), "+m"(*ptr) : "r"(desired), "0"(old) : "memory");
    *expected = prev;
    return prev == old;
}

static inline bool atomic_compare_exchange_64(volatile uint64_t *ptr,
                                               uint64_t *expected, uint64_t desired)
{
    uint64_t old = *expected, prev;
    __asm__ volatile("lock cmpxchgq %2, %1"
                     : "=a"(prev), "+m"(*ptr) : "r"(desired), "0"(old) : "memory");
    *expected = prev;
    return prev == old;
}

static inline uint8_t atomic_fetch_add_8(volatile uint8_t *ptr, uint8_t val)
{
    __asm__ volatile("lock xaddb %b0, %1" : "+q"(val), "+m"(*ptr) :: "memory", "cc");
    return val;
}

static inline uint16_t atomic_fetch_add_16(volatile uint16_t *ptr, uint16_t val)
{
    __asm__ volatile("lock xaddw %w0, %1" : "+r"(val), "+m"(*ptr) :: "memory", "cc");
    return val;
}

static inline uint32_t atomic_fetch_add_32(volatile uint32_t *ptr, uint32_t val)
{
    __asm__ volatile("lock xaddl %0, %1" : "+r"(val), "+m"(*ptr) :: "memory", "cc");
    return val;
}

static inline uint64_t atomic_fetch_add_64(volatile uint64_t *ptr, uint64_t val)
{
    __asm__ volatile("lock xaddq %0, %1" : "+r"(val), "+m"(*ptr) :: "memory", "cc");
    return val;
}

static inline uint8_t  atomic_fetch_sub_8(volatile uint8_t *ptr, uint8_t val)   { return atomic_fetch_add_8(ptr, -val); }
static inline uint16_t atomic_fetch_sub_16(volatile uint16_t *ptr, uint16_t val) { return atomic_fetch_add_16(ptr, -val); }
static inline uint32_t atomic_fetch_sub_32(volatile uint32_t *ptr, uint32_t val) { return atomic_fetch_add_32(ptr, -val); }
static inline uint64_t atomic_fetch_sub_64(volatile uint64_t *ptr, uint64_t val) { return atomic_fetch_add_64(ptr, -val); }

static inline void atomic_add_8(volatile uint8_t *ptr, uint8_t val)
{ __asm__ volatile("lock addb %b1, %0" : "+m"(*ptr) : "qi"(val) : "memory", "cc"); }

static inline void atomic_add_16(volatile uint16_t *ptr, uint16_t val)
{ __asm__ volatile("lock addw %w1, %0" : "+m"(*ptr) : "ri"(val) : "memory", "cc"); }

static inline void atomic_add_32(volatile uint32_t *ptr, uint32_t val)
{ __asm__ volatile("lock addl %1, %0" : "+m"(*ptr) : "ri"(val) : "memory", "cc"); }

static inline void atomic_add_64(volatile uint64_t *ptr, uint64_t val)
{ __asm__ volatile("lock addq %1, %0" : "+m"(*ptr) : "ri"(val) : "memory", "cc"); }

static inline void atomic_sub_8(volatile uint8_t *ptr, uint8_t val)
{ __asm__ volatile("lock subb %b1, %0" : "+m"(*ptr) : "qi"(val) : "memory", "cc"); }

static inline void atomic_sub_16(volatile uint16_t *ptr, uint16_t val)
{ __asm__ volatile("lock subw %w1, %0" : "+m"(*ptr) : "ri"(val) : "memory", "cc"); }

static inline void atomic_sub_32(volatile uint32_t *ptr, uint32_t val)
{ __asm__ volatile("lock subl %1, %0" : "+m"(*ptr) : "ri"(val) : "memory", "cc"); }

static inline void atomic_sub_64(volatile uint64_t *ptr, uint64_t val)
{ __asm__ volatile("lock subq %1, %0" : "+m"(*ptr) : "ri"(val) : "memory", "cc"); }

static inline void atomic_inc_8(volatile uint8_t *ptr)
{ __asm__ volatile("lock incb %0" : "+m"(*ptr) :: "memory", "cc"); }

static inline void atomic_inc_16(volatile uint16_t *ptr)
{ __asm__ volatile("lock incw %0" : "+m"(*ptr) :: "memory", "cc"); }

static inline void atomic_inc_32(volatile uint32_t *ptr)
{ __asm__ volatile("lock incl %0" : "+m"(*ptr) :: "memory", "cc"); }

static inline void atomic_inc_64(volatile uint64_t *ptr)
{ __asm__ volatile("lock incq %0" : "+m"(*ptr) :: "memory", "cc"); }

static inline void atomic_dec_8(volatile uint8_t *ptr)
{ __asm__ volatile("lock decb %0" : "+m"(*ptr) :: "memory", "cc"); }

static inline void atomic_dec_16(volatile uint16_t *ptr)
{ __asm__ volatile("lock decw %0" : "+m"(*ptr) :: "memory", "cc"); }

static inline void atomic_dec_32(volatile uint32_t *ptr)
{ __asm__ volatile("lock decl %0" : "+m"(*ptr) :: "memory", "cc"); }

static inline void atomic_dec_64(volatile uint64_t *ptr)
{ __asm__ volatile("lock decq %0" : "+m"(*ptr) :: "memory", "cc"); }

static inline void atomic_or_8(volatile uint8_t *ptr, uint8_t val)
{ __asm__ volatile("lock orb %b1, %0" : "+m"(*ptr) : "qi"(val) : "memory", "cc"); }

static inline void atomic_or_16(volatile uint16_t *ptr, uint16_t val)
{ __asm__ volatile("lock orw %w1, %0" : "+m"(*ptr) : "ri"(val) : "memory", "cc"); }

static inline void atomic_or_32(volatile uint32_t *ptr, uint32_t val)
{ __asm__ volatile("lock orl %1, %0" : "+m"(*ptr) : "ri"(val) : "memory", "cc"); }

static inline void atomic_or_64(volatile uint64_t *ptr, uint64_t val)
{ __asm__ volatile("lock orq %1, %0" : "+m"(*ptr) : "ri"(val) : "memory", "cc"); }

static inline void atomic_and_8(volatile uint8_t *ptr, uint8_t val)
{ __asm__ volatile("lock andb %b1, %0" : "+m"(*ptr) : "qi"(val) : "memory", "cc"); }

static inline void atomic_and_16(volatile uint16_t *ptr, uint16_t val)
{ __asm__ volatile("lock andw %w1, %0" : "+m"(*ptr) : "ri"(val) : "memory", "cc"); }

static inline void atomic_and_32(volatile uint32_t *ptr, uint32_t val)
{ __asm__ volatile("lock andl %1, %0" : "+m"(*ptr) : "ri"(val) : "memory", "cc"); }

static inline void atomic_and_64(volatile uint64_t *ptr, uint64_t val)
{ __asm__ volatile("lock andq %1, %0" : "+m"(*ptr) : "ri"(val) : "memory", "cc"); }

static inline void atomic_xor_8(volatile uint8_t *ptr, uint8_t val)
{ __asm__ volatile("lock xorb %b1, %0" : "+m"(*ptr) : "qi"(val) : "memory", "cc"); }

static inline void atomic_xor_16(volatile uint16_t *ptr, uint16_t val)
{ __asm__ volatile("lock xorw %w1, %0" : "+m"(*ptr) : "ri"(val) : "memory", "cc"); }

static inline void atomic_xor_32(volatile uint32_t *ptr, uint32_t val)
{ __asm__ volatile("lock xorl %1, %0" : "+m"(*ptr) : "ri"(val) : "memory", "cc"); }

static inline void atomic_xor_64(volatile uint64_t *ptr, uint64_t val)
{ __asm__ volatile("lock xorq %1, %0" : "+m"(*ptr) : "ri"(val) : "memory", "cc"); }

static inline bool atomic_bit_test_and_set_32(volatile uint32_t *ptr, uint32_t bit)
{
    bool old;
    __asm__ volatile("lock btsl %2, %0; setc %1"
                     : "+m"(*ptr), "=qm"(old) : "Ir"(bit) : "memory", "cc");
    return old;
}

static inline bool atomic_bit_test_and_set_64(volatile uint64_t *ptr, uint64_t bit)
{
    bool old;
    __asm__ volatile("lock btsq %2, %0; setc %1"
                     : "+m"(*ptr), "=qm"(old) : "Ir"(bit) : "memory", "cc");
    return old;
}

static inline bool atomic_bit_test_and_clear_32(volatile uint32_t *ptr, uint32_t bit)
{
    bool old;
    __asm__ volatile("lock btrl %2, %0; setc %1"
                     : "+m"(*ptr), "=qm"(old) : "Ir"(bit) : "memory", "cc");
    return old;
}

static inline bool atomic_bit_test_and_clear_64(volatile uint64_t *ptr, uint64_t bit)
{
    bool old;
    __asm__ volatile("lock btrq %2, %0; setc %1"
                     : "+m"(*ptr), "=qm"(old) : "Ir"(bit) : "memory", "cc");
    return old;
}

#endif