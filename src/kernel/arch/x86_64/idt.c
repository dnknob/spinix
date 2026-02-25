#include <arch/x86_64/idt.h>
#include <arch/x86_64/gdt.h>

#include <video/printk.h>

#include <stddef.h>
#include <stdint.h>

static struct idt_entry idt_entries[256];
static struct idt_ptr idt_pointer;

static const uint64_t isr_table[32] = {
    (uint64_t)isr0,  (uint64_t)isr1,  (uint64_t)isr2,  (uint64_t)isr3,
    (uint64_t)isr4,  (uint64_t)isr5,  (uint64_t)isr6,  (uint64_t)isr7,
    (uint64_t)isr8,  (uint64_t)isr9,  (uint64_t)isr10, (uint64_t)isr11,
    (uint64_t)isr12, (uint64_t)isr13, (uint64_t)isr14, (uint64_t)isr15,
    (uint64_t)isr16, (uint64_t)isr17, (uint64_t)isr18, (uint64_t)isr19,
    (uint64_t)isr20, (uint64_t)isr21, (uint64_t)isr22, (uint64_t)isr23,
    (uint64_t)isr24, (uint64_t)isr25, (uint64_t)isr26, (uint64_t)isr27,
    (uint64_t)isr28, (uint64_t)isr29, (uint64_t)isr30, (uint64_t)isr31,
};

static const uint64_t irq_table[16] = {
    (uint64_t)irq0,  (uint64_t)irq1,  (uint64_t)irq2,  (uint64_t)irq3,
    (uint64_t)irq4,  (uint64_t)irq5,  (uint64_t)irq6,  (uint64_t)irq7,
    (uint64_t)irq8,  (uint64_t)irq9,  (uint64_t)irq10, (uint64_t)irq11,
    (uint64_t)irq12, (uint64_t)irq13, (uint64_t)irq14, (uint64_t)irq15,
};

void idt_set_gate(uint8_t num, uint64_t handler, uint16_t selector, uint8_t type_attr, uint8_t ist) {
    idt_entries[num].offset_low = handler & 0xFFFF;
    idt_entries[num].offset_mid = (handler >> 16) & 0xFFFF;
    idt_entries[num].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt_entries[num].selector = selector;
    idt_entries[num].ist = ist & 0x07;
    idt_entries[num].type_attr = type_attr;
    idt_entries[num].zero = 0;
}

void idt_init(void) {
    idt_pointer.limit = (sizeof(struct idt_entry) * 256) - 1;
    idt_pointer.base = (uint64_t)&idt_entries;

    for (int i = 0; i < 256; i++) {
        idt_entries[i] = (struct idt_entry){0};
    }

    for (int i = 0; i < 32; i++) {
        uint8_t ist = 0;
        
        if (i == 1 || i == 3) ist = IST_DEBUG;          /* Debug/Breakpoint */
        else if (i == 2) ist = IST_NMI;                 /* NMI */
        else if (i == 8) ist = IST_DOUBLE_FAULT;        /* Double Fault */
        else if (i == 18) ist = IST_MACHINE_CHECK;      /* Machine Check */
        
        idt_set_gate(i, isr_table[i], GDT_KERNEL_CODE, IDT_INTERRUPT_GATE, ist);
    }

    for (int i = 0; i < 16; i++) {
        idt_set_gate(32 + i, irq_table[i], GDT_KERNEL_CODE, IDT_INTERRUPT_GATE, 0);
    }

    /* Uncomment when syscalls exist
       idt_set_gate(0x80, (uint64_t)isr128, GDT_KERNEL_CODE, IDT_USER_INTERRUPT, 0);
    */
    idt_flush((uint64_t)&idt_pointer);
}

const struct idt_ptr *idt_get_ptr(void) {
    return &idt_pointer;
}