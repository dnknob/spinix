#include <arch/x86_64/pit.h>
#include <arch/x86_64/io.h>
#include <arch/x86_64/intr.h>

#include <video/printk.h>

#include <stdint.h>
#include <stdbool.h>

static volatile uint64_t pit_ticks = 0;
static uint32_t pit_frequency = 0;
static uint16_t pit_divisor = 0;

static void pit_irq_handler(struct interrupt_frame *frame) {
    (void)frame;
    pit_ticks++;
}

uint16_t pit_calculate_divisor(uint32_t frequency) {
    if (frequency == 0 || frequency < 19) {
        /* Minimum frequency to avoid divisor overflow */
        return 0;  /* 0 represents 65536 */
    }
    
    uint32_t divisor = PIT_FREQUENCY / frequency;
    
    if (divisor > 65535) {
        return 0;  /* 0 represents 65536 */
    }
    
    return (uint16_t)divisor;
}

void pit_set_divisor(uint16_t divisor) {
    /* Disable interrupts during PIT programming */
    __asm__ volatile("cli");
    
    uint8_t command = PIT_SELECT_CHANNEL0 | PIT_ACCESS_LOHI | 
                      PIT_MODE_3 | PIT_BINARY_MODE;
    outb(PIT_COMMAND, command);
    
    outb(PIT_CHANNEL0_DATA, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0_DATA, (uint8_t)((divisor >> 8) & 0xFF));
    
    pit_divisor = divisor;
    
    if (divisor == 0) {
        pit_frequency = PIT_FREQUENCY / 65536;
    } else {
        pit_frequency = PIT_FREQUENCY / divisor;
    }
    
    __asm__ volatile("sti");
}

void pit_set_frequency(uint32_t frequency) {
    uint16_t divisor = pit_calculate_divisor(frequency);
    pit_set_divisor(divisor);
}

uint16_t pit_read_count(void) {
    /* Latch counter value */
    outb(PIT_COMMAND, PIT_SELECT_CHANNEL0 | PIT_ACCESS_LATCH);
    
    uint8_t low = inb(PIT_CHANNEL0_DATA);
    uint8_t high = inb(PIT_CHANNEL0_DATA);
    
    return (uint16_t)((high << 8) | low);
}

uint64_t pit_get_ticks(void) {
    return pit_ticks;
}

uint32_t pit_get_frequency(void) {
    return pit_frequency;
}

void pit_sleep_ms(uint32_t ms) {
    if (pit_frequency == 0) {
        return;  /* Not initialized */
    }
    
    uint64_t start_ticks = pit_ticks;
    uint64_t target_ticks = (uint64_t)ms * pit_frequency / 1000;
    
    while ((pit_ticks - start_ticks) < target_ticks) {
        __asm__ volatile("pause");
    }
}

void pit_init(uint32_t frequency) {
    printk("pit: initializing PIT at %u Hz\n", frequency);
    
    pit_ticks = 0;
    
    pit_set_frequency(frequency);
    
    irq_install_handler(0, pit_irq_handler);
    
    printk("pit: initialized successfully\n");
}

uint64_t pit_get_uptime_ms(void) {
    if (pit_frequency == 0) return 0;

    return (pit_ticks * 1000ULL) / pit_frequency;
}

uint64_t pit_get_uptime_seconds(void) {
    if (pit_frequency == 0) return 0;

    return pit_ticks / pit_frequency;
}
