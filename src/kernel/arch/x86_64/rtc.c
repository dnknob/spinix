#include <arch/x86_64/intr.h>
#include <arch/x86_64/idt.h>
#include <arch/x86_64/pic.h>
#include <arch/x86_64/rtc.h>
#include <arch/x86_64/io.h>

#include <video/printk.h>

#include <stdint.h>
#include <stddef.h>

static volatile uint64_t rtc_ticks  = 0;   /* seconds since rtc_init()    */
static volatile rtc_time_t rtc_cache;       /* last snapshot from IRQ      */

uint8_t rtc_cmos_read(uint8_t reg)
{
    outb(CMOS_ADDR, (reg & 0x7F) | 0x80);  /* select reg, NMI disabled    */
    io_wait();
    uint8_t val = inb(CMOS_DATA);
    outb(CMOS_ADDR, 0x00);                  /* re-enable NMI               */
    return val;
}

void rtc_cmos_write(uint8_t reg, uint8_t val)
{
    outb(CMOS_ADDR, (reg & 0x7F) | 0x80);  /* select reg, NMI disabled    */
    io_wait();
    outb(CMOS_DATA, val);
    outb(CMOS_ADDR, 0x00);                  /* re-enable NMI               */
}

static inline uint8_t bcd_to_bin(uint8_t bcd)
{
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

static inline int rtc_update_in_progress(void)
{
    return (rtc_cmos_read(RTC_REG_A) & RTC_A_UIP) != 0;
}

static void rtc_read_registers(rtc_time_t *t)
{
    rtc_time_t last;
    uint8_t regb;

    while (rtc_update_in_progress())
        ;

    do {
        last = *t;

        t->second  = rtc_cmos_read(RTC_REG_SECONDS);
        t->minute  = rtc_cmos_read(RTC_REG_MINUTES);
        t->hour    = rtc_cmos_read(RTC_REG_HOURS);
        t->weekday = rtc_cmos_read(RTC_REG_WEEKDAY);
        t->day     = rtc_cmos_read(RTC_REG_DAY);
        t->month   = rtc_cmos_read(RTC_REG_MONTH);
        t->year    = rtc_cmos_read(RTC_REG_YEAR);

    } while (last.second != t->second ||
             last.minute != t->minute ||
             last.hour   != t->hour   ||
             last.day    != t->day    ||
             last.month  != t->month  ||
             last.year   != t->year);

    regb = rtc_cmos_read(RTC_REG_B);

    if (!(regb & RTC_B_BINARY)) {
        t->second  = bcd_to_bin(t->second);
        t->minute  = bcd_to_bin(t->minute);
        t->hour    = bcd_to_bin(t->hour & 0x7F) | (t->hour & 0x80);
        t->weekday = bcd_to_bin(t->weekday);
        t->day     = bcd_to_bin(t->day);
        t->month   = bcd_to_bin(t->month);
        t->year    = bcd_to_bin((uint8_t)t->year);
    }

    if (!(regb & RTC_B_24HR) && (t->hour & 0x80)) {
        t->hour = ((t->hour & 0x7F) + 12) % 24;
    }

    if (t->year < 100)
        t->year += 2000;
}

void rtc_read(rtc_time_t *t)
{
    if (!t)
        return;
    rtc_read_registers(t);
}

uint64_t rtc_get_ticks(void)
{
    return rtc_ticks;
}

static void rtc_irq_handler(struct interrupt_frame *frame)
{
    (void)frame;

    uint8_t regc = rtc_cmos_read(RTC_REG_C);

    if (regc & RTC_C_UF) {
        /* Update-Ended interrupt: RTC just finished a 1-second update */
        rtc_ticks++;
        rtc_read_registers((rtc_time_t *)&rtc_cache);
    }

    if (regc & RTC_C_PF) {
        /* Periodic interrupt — not enabled by default, placeholder */
    }

    if (regc & RTC_C_AF) {
        /* Alarm interrupt — not enabled by default, placeholder */
    }
}

void rtc_init(void)
{
    printk("rtc: initialising RTC driver\n");

    uint8_t regb = rtc_cmos_read(RTC_REG_B);
    regb &= ~(RTC_B_PIE | RTC_B_AIE | RTC_B_UIE);
    rtc_cmos_write(RTC_REG_B, regb);

    regb = rtc_cmos_read(RTC_REG_B);
    regb |= (RTC_B_BINARY | RTC_B_24HR);
    rtc_cmos_write(RTC_REG_B, regb);

    (void)rtc_cmos_read(RTC_REG_C);

    irq_install_handler(8, rtc_irq_handler);

    irq_enable(8);

    regb = rtc_cmos_read(RTC_REG_B);
    regb |= RTC_B_UIE;
    rtc_cmos_write(RTC_REG_B, regb);

    rtc_read_registers((rtc_time_t *)&rtc_cache);

    printk("rtc: current time %04u-%02u-%02u %02u:%02u:%02u\n",
           rtc_cache.year,  rtc_cache.month,  rtc_cache.day,
           rtc_cache.hour,  rtc_cache.minute, rtc_cache.second);
    printk("rtc: IRQ 8 enabled (Update-Ended, 1 Hz)\n");
}
