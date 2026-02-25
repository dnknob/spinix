#include <arch/x86_64/switch.h>
#include <arch/x86_64/gdt.h>

#include <core/scheduler.h>

#include <video/printk.h>

#include <stdint.h>
#include <stddef.h>

extern tcb_t *current_task;

void switch_to_task(tcb_t *next_task) {
    if (next_task == NULL) {
        return;
    }

    uint64_t new_stack_top = next_task->kernel_stack_base + next_task->kernel_stack_size;
    
    if (new_stack_top == 0) {
        printk("sched: warning: task '%s' has invalid stack metadata (base=0x%lx, size=0x%lx)\n",
               next_task->name, next_task->kernel_stack_base, next_task->kernel_stack_size);
    }
    
    gdt_set_kernel_stack(new_stack_top);
    switch_to_task_asm(next_task);
}

void task_startup_wrapper(void) {
    /* Unlock scheduler - the task was created with scheduler locked */
    unlock_scheduler();
}
