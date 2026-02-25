#ifndef APIC_H
#define APIC_H

#include <klibc/types.h>

#define LAPIC_ID                    0x020
#define LAPIC_VERSION               0x030
#define LAPIC_TPR                   0x080
#define LAPIC_APR                   0x090
#define LAPIC_PPR                   0x0A0
#define LAPIC_EOI                   0x0B0
#define LAPIC_RRD                   0x0C0
#define LAPIC_LDR                   0x0D0
#define LAPIC_DFR                   0x0E0
#define LAPIC_SVR                   0x0F0
#define LAPIC_ISR_BASE              0x100
#define LAPIC_TMR_BASE              0x180
#define LAPIC_IRR_BASE              0x200
#define LAPIC_ESR                   0x280
#define LAPIC_CMCI                  0x2F0
#define LAPIC_ICR_LOW               0x300
#define LAPIC_ICR_HIGH              0x310
#define LAPIC_TIMER                 0x320
#define LAPIC_THERMAL               0x330
#define LAPIC_PERF                  0x340
#define LAPIC_LINT0                 0x350
#define LAPIC_LINT1                 0x360
#define LAPIC_ERROR                 0x370
#define LAPIC_TIMER_INITCNT         0x380
#define LAPIC_TIMER_CURRCNT         0x390
#define LAPIC_TIMER_DIV             0x3E0

#define LAPIC_SVR_ENABLE            (1 << 8)
#define LAPIC_SVR_FOCUS             (1 << 9)
#define LAPIC_SVR_EOI_SUPPRESS      (1 << 12)

#define LAPIC_LVT_VECTOR_MASK       0xFF
#define LAPIC_LVT_DELIVERY_FIXED    (0 << 8)
#define LAPIC_LVT_DELIVERY_SMI      (2 << 8)
#define LAPIC_LVT_DELIVERY_NMI      (4 << 8)
#define LAPIC_LVT_DELIVERY_INIT     (5 << 8)
#define LAPIC_LVT_DELIVERY_EXTINT   (7 << 8)
#define LAPIC_LVT_PENDING           (1 << 12)
#define LAPIC_LVT_ACTIVE_LOW        (1 << 13)
#define LAPIC_LVT_REMOTE_IRR        (1 << 14)
#define LAPIC_LVT_LEVEL             (1 << 15)
#define LAPIC_LVT_MASKED            (1 << 16)
#define LAPIC_LVT_TIMER_PERIODIC    (1 << 17)
#define LAPIC_LVT_TIMER_TSCDEADLINE (2 << 17)

#define LAPIC_ICR_VECTOR_MASK       0xFF
#define LAPIC_ICR_DELIVERY_FIXED    (0 << 8)
#define LAPIC_ICR_DELIVERY_LOWPRI   (1 << 8)
#define LAPIC_ICR_DELIVERY_SMI      (2 << 8)
#define LAPIC_ICR_DELIVERY_NMI      (4 << 8)
#define LAPIC_ICR_DELIVERY_INIT     (5 << 8)
#define LAPIC_ICR_DELIVERY_STARTUP  (6 << 8)
#define LAPIC_ICR_DEST_PHYSICAL     (0 << 11)
#define LAPIC_ICR_DEST_LOGICAL      (1 << 11)
#define LAPIC_ICR_PENDING           (1 << 12)
#define LAPIC_ICR_ASSERT            (1 << 14)
#define LAPIC_ICR_LEVEL             (1 << 15)
#define LAPIC_ICR_DEST_SELF         (1 << 18)
#define LAPIC_ICR_DEST_ALL          (2 << 18)
#define LAPIC_ICR_DEST_ALL_OTHER    (3 << 18)

#define LAPIC_TIMER_DIV_1           0x0B
#define LAPIC_TIMER_DIV_2           0x00
#define LAPIC_TIMER_DIV_4           0x01
#define LAPIC_TIMER_DIV_8           0x02
#define LAPIC_TIMER_DIV_16          0x03
#define LAPIC_TIMER_DIV_32          0x08
#define LAPIC_TIMER_DIV_64          0x09
#define LAPIC_TIMER_DIV_128         0x0A

#define LAPIC_ESR_SEND_CHECKSUM     (1 << 0)
#define LAPIC_ESR_RECV_CHECKSUM     (1 << 1)
#define LAPIC_ESR_SEND_ACCEPT       (1 << 2)
#define LAPIC_ESR_RECV_ACCEPT       (1 << 3)
#define LAPIC_ESR_REDIRECTABLE_IPI  (1 << 4)
#define LAPIC_ESR_SEND_ILLEGAL      (1 << 5)
#define LAPIC_ESR_RECV_ILLEGAL      (1 << 6)
#define LAPIC_ESR_ILLEGAL_REG       (1 << 7)

#define IA32_APIC_BASE_MSR          0x1B
#define IA32_APIC_BASE_BSP          (1 << 8)
#define IA32_APIC_BASE_ENABLE       (1 << 11)
#define IA32_APIC_BASE_X2APIC       (1 << 10)
#define IA32_APIC_BASE_ADDR_MASK    0xFFFFF000UL

#define LAPIC_DEFAULT_BASE          0xFEE00000

#define APIC_SPURIOUS_VECTOR        0xFF

#define APIC_TIMER_MODE_ONESHOT     0
#define APIC_TIMER_MODE_PERIODIC    1
#define APIC_TIMER_MODE_TSCDEADLINE 2

#define X2APIC_MSR_BASE             0x800
#define X2APIC_MSR_ID               (X2APIC_MSR_BASE + 0x02)
#define X2APIC_MSR_VERSION          (X2APIC_MSR_BASE + 0x03)
#define X2APIC_MSR_TPR              (X2APIC_MSR_BASE + 0x08)
#define X2APIC_MSR_EOI              (X2APIC_MSR_BASE + 0x0B)
#define X2APIC_MSR_SVR              (X2APIC_MSR_BASE + 0x0F)
#define X2APIC_MSR_ESR              (X2APIC_MSR_BASE + 0x28)
#define X2APIC_MSR_ICR              (X2APIC_MSR_BASE + 0x30)
#define X2APIC_MSR_TIMER            (X2APIC_MSR_BASE + 0x32)
#define X2APIC_MSR_SELF_IPI         (X2APIC_MSR_BASE + 0x3F)

bool apic_check_support(void);
bool apic_check_x2apic_support(void);
bool apic_is_x2apic_enabled(void);

void apic_init(void);
bool apic_enable_x2apic(void);

uint32_t apic_get_id(void);
void     apic_eoi(void);

uint32_t apic_read(uint32_t reg);
void     apic_write(uint32_t reg, uint32_t value);

phys_addr_t apic_get_base(void);
void        apic_set_base(phys_addr_t base);

void apic_send_ipi(uint32_t apic_id, uint32_t vector, uint32_t delivery_mode,
                   uint32_t dest_mode, uint32_t dest_shorthand);

void     apic_timer_init(uint32_t vector, uint32_t divisor, uint32_t initial_count, bool periodic);
void     apic_timer_stop(void);
uint32_t apic_timer_get_current(void);

uint64_t apic_timer_calibrate_pit(void);
uint64_t apic_timer_get_frequency(void);
void     apic_timer_set_frequency(uint64_t freq_hz);

bool apic_timer_tsc_deadline_supported(void);
void apic_timer_set_tsc_deadline(uint64_t deadline);

void     apic_lvt_set_mask(uint32_t lvt_reg, bool mask);
void     apic_lvt_set(uint32_t lvt_reg, uint32_t vector, uint32_t delivery_mode, bool masked);

uint32_t apic_get_errors(void);
void     apic_clear_errors(void);

#endif