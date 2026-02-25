#ifndef PIC_H
#define PIC_H

#include <klibc/types.h>

void pic_init();
void irq_enable(uint8_t irq);
void pic_disable(void);

#endif
