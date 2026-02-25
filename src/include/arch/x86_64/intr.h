#ifndef INTR_H
#define INTR_H

#include <arch/x86_64/idt.h>

#include <klibc/types.h>

void irq_install_handler(int irq, irq_handler_fn handler);
void irq_uninstall_handler(int irq);

void interrupt_handler(struct interrupt_frame *frame);

NORETURN void kernel_panic(const char *message);

void dump_registers(struct interrupt_frame *frame);

#endif