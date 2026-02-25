#ifndef IDT_H
#define IDT_H

#include <klibc/types.h>

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

#define IDT_TYPE_INTERRUPT  0xE
#define IDT_TYPE_TRAP       0xF

#define IDT_ATTR_PRESENT    (1 << 7)
#define IDT_ATTR_DPL0       (0 << 5)
#define IDT_ATTR_DPL3       (3 << 5)

#define IDT_INTERRUPT_GATE  (IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_INTERRUPT)
#define IDT_TRAP_GATE       (IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_TRAP)
#define IDT_USER_INTERRUPT  (IDT_ATTR_PRESENT | IDT_ATTR_DPL3 | IDT_TYPE_INTERRUPT)

typedef void (*isr_handler_t)(void);

#define ISR_DIVIDE_ERROR            0
#define ISR_DEBUG                   1
#define ISR_NMI                     2
#define ISR_BREAKPOINT              3
#define ISR_OVERFLOW                4
#define ISR_BOUND_RANGE             5
#define ISR_INVALID_OPCODE          6
#define ISR_DEVICE_NOT_AVAILABLE    7
#define ISR_DOUBLE_FAULT            8
#define ISR_COPROCESSOR_OVERRUN     9
#define ISR_INVALID_TSS             10
#define ISR_SEGMENT_NOT_PRESENT     11
#define ISR_STACK_FAULT             12
#define ISR_GENERAL_PROTECTION      13
#define ISR_PAGE_FAULT              14
#define ISR_RESERVED                15
#define ISR_FPU_ERROR               16
#define ISR_ALIGNMENT_CHECK         17
#define ISR_MACHINE_CHECK           18
#define ISR_SIMD_FP_EXCEPTION       19
#define ISR_VIRTUALIZATION          20
#define ISR_CONTROL_PROTECTION      21

#define IRQ0    32
#define IRQ1    33
#define IRQ2    34
#define IRQ3    35
#define IRQ4    36
#define IRQ5    37
#define IRQ6    38
#define IRQ7    39
#define IRQ8    40
#define IRQ9    41
#define IRQ10   42
#define IRQ11   43
#define IRQ12   44
#define IRQ13   45
#define IRQ14   46
#define IRQ15   47

struct interrupt_frame {
    uint64_t gs, fs, es, ds;
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed));

typedef void (*irq_handler_fn)(struct interrupt_frame *frame);

void idt_init(void);

const struct idt_ptr *idt_get_ptr(void);

void idt_set_gate(uint8_t num, uint64_t handler, uint16_t selector,
                  uint8_t type_attr, uint8_t ist);

void irq_install_handler(int irq, irq_handler_fn handler);
void irq_uninstall_handler(int irq);

extern void isr0(void);  extern void isr1(void);  extern void isr2(void);
extern void isr3(void);  extern void isr4(void);  extern void isr5(void);
extern void isr6(void);  extern void isr7(void);  extern void isr8(void);
extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void);
extern void isr15(void); extern void isr16(void); extern void isr17(void);
extern void isr18(void); extern void isr19(void); extern void isr20(void);
extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void);
extern void isr27(void); extern void isr28(void); extern void isr29(void);
extern void isr30(void); extern void isr31(void);

extern void irq0(void);  extern void irq1(void);  extern void irq2(void);
extern void irq3(void);  extern void irq4(void);  extern void irq5(void);
extern void irq6(void);  extern void irq7(void);  extern void irq8(void);
extern void irq9(void);  extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void);
extern void irq15(void);

extern void idt_flush(uint64_t idt_ptr);

#endif