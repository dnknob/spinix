#ifndef PIT_H
#define PIT_H

#include <klibc/types.h>

#define PIT_CHANNEL0_DATA   0x40
#define PIT_CHANNEL1_DATA   0x41
#define PIT_CHANNEL2_DATA   0x42
#define PIT_COMMAND         0x43

#define PIT_FREQUENCY       1193182

#define PIT_SELECT_CHANNEL0 0x00
#define PIT_SELECT_CHANNEL1 0x40
#define PIT_SELECT_CHANNEL2 0x80

#define PIT_ACCESS_LATCH    0x00
#define PIT_ACCESS_LOBYTE   0x10
#define PIT_ACCESS_HIBYTE   0x20
#define PIT_ACCESS_LOHI     0x30

#define PIT_MODE_0          0x00
#define PIT_MODE_1          0x02
#define PIT_MODE_2          0x04
#define PIT_MODE_3          0x06
#define PIT_MODE_4          0x08
#define PIT_MODE_5          0x0A

#define PIT_BINARY_MODE     0x00
#define PIT_BCD_MODE        0x01

#define PIT_FREQ_1000HZ     1000
#define PIT_FREQ_100HZ      100
#define PIT_FREQ_50HZ       50
#define PIT_FREQ_18_2HZ     18

void     pit_init(uint32_t frequency);
void     pit_set_frequency(uint32_t frequency);
uint16_t pit_calculate_divisor(uint32_t frequency);
void     pit_set_divisor(uint16_t divisor);
uint16_t pit_read_count(void);
uint64_t pit_get_ticks(void);
uint32_t pit_get_frequency(void);
void     pit_sleep_ms(uint32_t ms);
uint64_t pit_get_uptime_ms(void);
uint64_t pit_get_uptime_seconds(void);

#endif