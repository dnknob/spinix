#include <arch/x86_64/apic.h>
#include <arch/x86_64/idt.h>
#include <arch/x86_64/io.h>

#include <core/scheduler.h>

#include <video/printk.h>

#include <stdint.h>
#include <stddef.h>
#include <stdnoreturn.h>

extern bool scheduler_is_initialized(void);

static irq_handler_fn irq_handlers[16] = {0};

static const char *exception_messages[] = {
    "Divide by Zero",
    "Debug",
    "Non-Maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 Floating-Point Exception",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point Exception",
    "Virtualization Exception",
    "Control Protection Exception",
};

void dump_registers(struct interrupt_frame *frame) {
    printk("\n=== Register Dump ===\n");
    printk("RAX=%016llx RBX=%016llx RCX=%016llx RDX=%016llx\n",
           frame->rax, frame->rbx, frame->rcx, frame->rdx);
    printk("RSI=%016llx RDI=%016llx RBP=%016llx RSP=%016llx\n",
           frame->rsi, frame->rdi, frame->rbp, frame->rsp);
    printk("R8 =%016llx R9 =%016llx R10=%016llx R11=%016llx\n",
           frame->r8, frame->r9, frame->r10, frame->r11);
    printk("R12=%016llx R13=%016llx R14=%016llx R15=%016llx\n",
           frame->r12, frame->r13, frame->r14, frame->r15);
    printk("RIP=%016llx RFL=%016llx CS=%04llx SS=%04llx\n",
           frame->rip, frame->rflags, frame->cs, frame->ss);
    printk("INT=%llu ERR=%016llx CPL=%llu\n",
           frame->int_no, frame->err_code, frame->cs & 3);

    if (frame->int_no == 14) {
        uint64_t cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        printk("\nPage Fault: addr=%016llx %s %s %s\n", cr2,
               (frame->err_code & 0x1) ? "present" : "not-present",
               (frame->err_code & 0x2) ? "write" : "read",
               (frame->err_code & 0x4) ? "user" : "kernel");
    }
}

noreturn void kernel_panic(const char *message) {
    printk("\n*** KERNEL PANIC: %s ***\n", message);
    printk("System halted.\n");
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
    __builtin_unreachable();
}

void interrupt_handler(struct interrupt_frame *frame) {
    uint64_t int_no = frame->int_no;

    if (int_no < 32) {
        printk("\n*** CPU EXCEPTION: %s (#%llu) ***\n",
               int_no < 22 ? exception_messages[int_no] : "Unknown",
               int_no);
        
        if (int_no == 8) {
            printk("Double fault detected - possible stack corruption\n");
        }
        
        dump_registers(frame);
        kernel_panic("Unrecoverable exception");
    }

    if (int_no >= 32 && int_no < 48) {
        int irq = int_no - 32;
        
        if (irq_handlers[irq]) {
            irq_handlers[irq](frame);
        }
        
        if (irq == 0) {
            if (scheduler_is_initialized())
                scheduler_timer_tick();
        }
        
        apic_eoi();
        return;
    }

    printk("Unexpected interrupt: %llu\n", int_no);
}

void irq_install_handler(int irq, irq_handler_fn handler) {
    if (irq >= 0 && irq < 16) {
        irq_handlers[irq] = handler;
        printk("irq: Installed handler for IRQ%d\n", irq);
    }
}

void irq_uninstall_handler(int irq) {
    if (irq >= 0 && irq < 16) {
        irq_handlers[irq] = NULL;
        printk("irq: Removed handler for IRQ%d\n", irq);
    }
}
