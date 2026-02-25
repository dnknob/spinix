#ifndef GDT_H
#define GDT_H

#include <klibc/types.h>

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct tss_entry {
    uint32_t reserved0;
    uint64_t rsp0;          /* kernel RSP for ring-0 entry via interrupt */
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;          /* IST stacks (see IST_* below) */
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

#define GDT_ACCESS_PRESENT      (1 << 7)
#define GDT_ACCESS_RING0        (0 << 5)
#define GDT_ACCESS_RING3        (3 << 5)
#define GDT_ACCESS_SYSTEM       (0 << 4)
#define GDT_ACCESS_CODE_DATA    (1 << 4)
#define GDT_ACCESS_EXECUTABLE   (1 << 3)
#define GDT_ACCESS_DC           (1 << 2)
#define GDT_ACCESS_RW           (1 << 1)
#define GDT_ACCESS_ACCESSED     (1 << 0)

#define GDT_GRANULARITY_4K      (1 << 7)
#define GDT_GRANULARITY_BYTE    (0 << 7)
#define GDT_FLAG_32BIT          (1 << 6)
#define GDT_FLAG_64BIT          (1 << 5)

#define GDT_TSS_AVAILABLE       0x9
#define GDT_TSS_BUSY            0xB

#define GDT_KERNEL_CODE         0x08
#define GDT_KERNEL_DATA         0x10
#define GDT_USER_DATA           0x18    /* ← index 3: MUST be before USER_CODE */
#define GDT_USER_CODE           0x20    /* ← index 4: SYSRET loads CS from here */
#define GDT_TSS                 0x28

#define GDT_USER_CODE_SELECTOR  (GDT_USER_CODE | 3)    /* 0x23 */
#define GDT_USER_DATA_SELECTOR  (GDT_USER_DATA | 3)    /* 0x1B */

#define IST_DOUBLE_FAULT        1
#define IST_NMI                 2
#define IST_MACHINE_CHECK       3
#define IST_DEBUG               4

#define IST_STACK_SIZE          4096

void gdt_init(void);
void gdt_set_kernel_stack(uint64_t stack);   /* update TSS.rsp0 */
void gdt_set_ist(uint8_t ist_num, uint64_t stack);

extern void gdt_flush(uint64_t gdt_ptr);
extern void tss_flush(void);

#endif