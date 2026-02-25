#ifndef SWITCH_H
#define SWITCH_H

#include <core/scheduler.h>

extern void switch_to_task_asm(tcb_t *next_task);
void switch_to_task(tcb_t *next_task);

void task_startup_wrapper(void);

#endif
