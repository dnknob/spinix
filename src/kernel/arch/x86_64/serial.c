#include <arch/x86_64/serial.h>

#include <core/spinlock.h>

#include <stddef.h>

static spinlock_irq_t serial_lock = SPINLOCK_IRQ_INIT;

int serial_init(uint16_t port) {
    // Disable all interrupts
    outb(port + SERIAL_INT_ENABLE_REG, 0x00);

    outb(port + SERIAL_LINE_CTRL_REG, SERIAL_LCR_DLAB);

    outb(port + SERIAL_DIVISOR_LOW, SERIAL_BAUD_38400);
    outb(port + SERIAL_DIVISOR_HIGH, 0x00);

    outb(port + SERIAL_LINE_CTRL_REG, SERIAL_LCR_8N1);

    outb(port + SERIAL_FIFO_CTRL_REG,
         SERIAL_FCR_ENABLE | SERIAL_FCR_CLEAR_RX |
         SERIAL_FCR_CLEAR_TX | SERIAL_FCR_TRIGGER_14);

    outb(port + SERIAL_MODEM_CTRL_REG,
         SERIAL_MCR_DTR | SERIAL_MCR_RTS | SERIAL_MCR_OUT2);

    outb(port + SERIAL_MODEM_CTRL_REG,
         SERIAL_MCR_DTR | SERIAL_MCR_RTS | SERIAL_MCR_OUT1 |
         SERIAL_MCR_OUT2 | SERIAL_MCR_LOOP);

    outb(port + SERIAL_DATA_REG, 0xAE);

    if (inb(port + SERIAL_DATA_REG) != 0xAE) {
        return 1;
    }

    outb(port + SERIAL_MODEM_CTRL_REG,
         SERIAL_MCR_DTR | SERIAL_MCR_RTS | SERIAL_MCR_OUT1 | SERIAL_MCR_OUT2);

    return 0;
}

int serial_received(uint16_t port) {
    return inb(port + SERIAL_LINE_STATUS_REG) & SERIAL_LSR_DATA_READY;
}

char serial_read(uint16_t port) {
    spinlock_irq_acquire(&serial_lock);
    
    while (serial_received(port) == 0)
        ;
    
    char c = inb(port + SERIAL_DATA_REG);
    
    spinlock_irq_release(&serial_lock);
    return c;
}

int serial_is_transmit_empty(uint16_t port) {
    return inb(port + SERIAL_LINE_STATUS_REG) & SERIAL_LSR_THRE;
}

void serial_write(uint16_t port, char c) {
    spinlock_irq_acquire(&serial_lock);
    
    while (serial_is_transmit_empty(port) == 0)
        ;

    outb(port + SERIAL_DATA_REG, c);
    
    spinlock_irq_release(&serial_lock);
}

void serial_write_string(uint16_t port, const char *str) {
    if (str == NULL) {
        return;
    }

    spinlock_irq_acquire(&serial_lock);
    
    while (*str) {
        while (serial_is_transmit_empty(port) == 0)
            ;
        outb(port + SERIAL_DATA_REG, *str++);
    }
    
    spinlock_irq_release(&serial_lock);
}
