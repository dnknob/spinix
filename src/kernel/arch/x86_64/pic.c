#include <arch/x86_64/pic.h>

#include <stdint.h>

void pic_init() {
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0x11), "Nd"((uint16_t)0x20));
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0x11), "Nd"((uint16_t)0xA0));
    
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0x20), "Nd"((uint16_t)0x21));
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0x28), "Nd"((uint16_t)0xA1));
    
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0x04), "Nd"((uint16_t)0x21));
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0x02), "Nd"((uint16_t)0xA1));
    
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0x01), "Nd"((uint16_t)0x21));
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0x01), "Nd"((uint16_t)0xA1));

    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0xFB), "Nd"((uint16_t)0x21));
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0xFF), "Nd"((uint16_t)0xA1));
}

void irq_enable(uint8_t irq) {
    uint16_t port = irq < 8 ? 0x21 : 0xA1;
    uint8_t value;
    
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    value &= ~(1 << (irq % 8));
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

void pic_disable(void) {
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0xFF), "Nd"((uint16_t)0x21));
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0xFF), "Nd"((uint16_t)0xA1));
}
