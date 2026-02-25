#ifndef RTC_H
#define RTC_H

#include <klibc/types.h>

#define CMOS_ADDR           0x70
#define CMOS_DATA           0x71

#define RTC_REG_SECONDS     0x00
#define RTC_REG_MINUTES     0x02
#define RTC_REG_HOURS       0x04
#define RTC_REG_WEEKDAY     0x06
#define RTC_REG_DAY         0x07
#define RTC_REG_MONTH       0x08
#define RTC_REG_YEAR        0x09
#define RTC_REG_CENTURY     0x32
#define RTC_REG_A           0x0A
#define RTC_REG_B           0x0B
#define RTC_REG_C           0x0C
#define RTC_REG_D           0x0D

#define RTC_A_UIP           (1 << 7)
#define RTC_A_RATE_MASK     0x0F

#define RTC_B_DST           (1 << 0)
#define RTC_B_24HR          (1 << 1)
#define RTC_B_BINARY        (1 << 2)
#define RTC_B_SQWE          (1 << 3)
#define RTC_B_UIE           (1 << 4)
#define RTC_B_AIE           (1 << 5)
#define RTC_B_PIE           (1 << 6)
#define RTC_B_SET           (1 << 7)

#define RTC_C_UF            (1 << 4)
#define RTC_C_AF            (1 << 5)
#define RTC_C_PF            (1 << 6)
#define RTC_C_IRQF          (1 << 7)

#define RTC_RATE_1024HZ     0x06
#define RTC_RATE_512HZ      0x07
#define RTC_RATE_256HZ      0x08
#define RTC_RATE_2HZ        0x0F

typedef struct {
    uint8_t  second;
    uint8_t  minute;
    uint8_t  hour;
    uint8_t  weekday;
    uint8_t  day;
    uint8_t  month;
    uint16_t year;
} rtc_time_t;

void    rtc_init(void);
void    rtc_read(rtc_time_t *t);
uint64_t rtc_get_ticks(void);
uint8_t rtc_cmos_read(uint8_t reg);
void    rtc_cmos_write(uint8_t reg, uint8_t val);

#endif