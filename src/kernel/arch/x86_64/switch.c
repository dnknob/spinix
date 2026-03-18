#include <arch/x86_64/switch.h>
#include <arch/x86_64/gdt.h>
#include <arch/x86_64/mmu.h>

#include <core/scheduler.h>

#include <video/printk.h>

#include <stdint.h>
#include <stddef.h>

extern uint64_t syscall_kernel_rsp;
extern tcb_t *current_task;

void switch_to_task(tcb_t *next_task) {
    if (next_task == NULL)
        return;

    uint64_t new_stack_top = next_task->kernel_stack_base
                           + next_task->kernel_stack_size;
    gdt_set_kernel_stack(new_stack_top);
    syscall_kernel_rsp = new_stack_top;

    if (next_task->cr3 != 0) {
        uint64_t cur_cr3;
        __asm__ volatile("mov %%cr3, %0" : "=r"(cur_cr3));
        if (next_task->cr3 != cur_cr3)
            mmu_load_cr3(next_task->cr3);
    }

    switch_to_task_asm(next_task);
}

void task_startup_wrapper(void) {
    unlock_scheduler();
}