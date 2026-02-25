#include <arch/x86_64/apic.h>
#include <arch/x86_64/io.h>

#include <mm/mmu.h>

#include <video/printk.h>

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

static uint64_t lapic_base_virt = 0;
static uint64_t lapic_base_phys = 0;
static bool x2apic_mode = false;
static uint64_t apic_timer_frequency = 0;  /* APIC timer frequency in Hz */
static bool tsc_deadline_available = false;

static inline void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx,
                         uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile("cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf), "c"(0));
}

bool apic_check_support(void) {
    uint32_t eax, ebx, ecx, edx;
    cpuid(1, &eax, &ebx, &ecx, &edx);
    /* APIC feature is bit 9 of EDX */
    return (edx & (1 << 9)) != 0;
}

bool apic_check_x2apic_support(void) {
    uint32_t eax, ebx, ecx, edx;
    cpuid(1, &eax, &ebx, &ecx, &edx);
    /* x2APIC feature is bit 21 of ECX */
    return (ecx & (1 << 21)) != 0;
}

bool apic_is_x2apic_enabled(void) {
    return x2apic_mode;
}

uint64_t apic_get_base(void) {
    uint64_t msr_value = rdmsr(IA32_APIC_BASE_MSR);
    return msr_value & IA32_APIC_BASE_ADDR_MASK;
}

void apic_set_base(uint64_t base) {
    uint64_t msr_value = rdmsr(IA32_APIC_BASE_MSR);
    /* Preserve BSP and enable flags, update address */
    msr_value &= ~IA32_APIC_BASE_ADDR_MASK;
    msr_value |= (base & IA32_APIC_BASE_ADDR_MASK) | IA32_APIC_BASE_ENABLE;
    wrmsr(IA32_APIC_BASE_MSR, msr_value);
}

uint32_t apic_read(uint32_t reg) {
    if (x2apic_mode) {
        /* In x2APIC mode, read from MSRs */
        uint32_t msr = X2APIC_MSR_BASE + (reg >> 4);
        return (uint32_t)rdmsr(msr);
    }
    
    if (lapic_base_virt == 0)
        return 0;
    return *(volatile uint32_t *)(lapic_base_virt + reg);
}

void apic_write(uint32_t reg, uint32_t value) {
    if (x2apic_mode) {
        /* In x2APIC mode, write to MSRs */
        uint32_t msr = X2APIC_MSR_BASE + (reg >> 4);
        wrmsr(msr, value);
        return;
    }
    
    if (lapic_base_virt == 0)
        return;
    *(volatile uint32_t *)(lapic_base_virt + reg) = value;
}

void apic_eoi(void) {
    apic_write(LAPIC_EOI, 0);
}

uint32_t apic_get_id(void) {
    if (x2apic_mode) {
        /* In x2APIC mode, the ID is the full 32-bit value */
        return apic_read(LAPIC_ID);
    }
    return (apic_read(LAPIC_ID) >> 24) & 0xFF;
}

void apic_lvt_set_mask(uint32_t lvt_reg, bool mask) {
    uint32_t value = apic_read(lvt_reg);
    if (mask)
        value |= LAPIC_LVT_MASKED;
    else
        value &= ~LAPIC_LVT_MASKED;
    apic_write(lvt_reg, value);
}

void apic_lvt_set(uint32_t lvt_reg, uint32_t vector, uint32_t delivery_mode, bool masked) {
    uint32_t value = (vector & LAPIC_LVT_VECTOR_MASK) | delivery_mode;
    if (masked)
        value |= LAPIC_LVT_MASKED;
    apic_write(lvt_reg, value);
}

void apic_send_ipi(uint32_t apic_id, uint32_t vector, uint32_t delivery_mode,
                   uint32_t dest_mode, uint32_t dest_shorthand) {
    if (x2apic_mode) {
        /* In x2APIC mode, ICR is a single 64-bit MSR */
        uint64_t icr = (vector & LAPIC_ICR_VECTOR_MASK) | delivery_mode | 
                       dest_mode | LAPIC_ICR_ASSERT | dest_shorthand;
        icr |= ((uint64_t)apic_id << 32);
        wrmsr(X2APIC_MSR_ICR, icr);
    } else {
        uint32_t icr_low, icr_high;
        
        icr_high = (apic_id & 0xFF) << 24;
        apic_write(LAPIC_ICR_HIGH, icr_high);
        
        icr_low = (vector & LAPIC_ICR_VECTOR_MASK) | delivery_mode | dest_mode |
                  LAPIC_ICR_ASSERT | dest_shorthand;
        
        apic_write(LAPIC_ICR_LOW, icr_low);
        
        while (apic_read(LAPIC_ICR_LOW) & LAPIC_ICR_PENDING)
            __asm__ volatile("pause");
    }
}

uint64_t apic_timer_calibrate_pit(void) {
    /* Use PIT (Programmable Interval Timer) to calibrate APIC timer
     * PIT channel 0 runs at 1193182 Hz (1.193182 MHz) */
    
    const uint32_t PIT_FREQUENCY = 1193182;
    const uint32_t CALIBRATION_MS = 10;  /* Calibrate for 10ms */
    const uint32_t PIT_TICKS = (PIT_FREQUENCY * CALIBRATION_MS) / 1000;
    
    __asm__ volatile("cli");
    
    outb(0x43, 0x30);  /* Channel 0, lobyte/hibyte, mode 0 */
    
    outb(0x40, PIT_TICKS & 0xFF);
    outb(0x40, (PIT_TICKS >> 8) & 0xFF);
    
    apic_write(LAPIC_TIMER_DIV, LAPIC_TIMER_DIV_1);
    apic_write(LAPIC_TIMER, LAPIC_LVT_MASKED);  /* mask timer interrupts */
    apic_write(LAPIC_TIMER_INITCNT, 0xFFFFFFFF);
    
    uint8_t status;
    do {
        outb(0x43, 0xE2);  /* Read-back command for channel 0 */
        status = inb(0x40);
    } while (!(status & 0x80));  /* Wait until OUT pin goes high */
    
    uint32_t apic_count = apic_read(LAPIC_TIMER_CURRCNT);
    
    apic_write(LAPIC_TIMER_INITCNT, 0);
    
    __asm__ volatile("sti");
    
    uint32_t apic_ticks = 0xFFFFFFFF - apic_count;
    
    apic_timer_frequency = ((uint64_t)apic_ticks * PIT_FREQUENCY) / PIT_TICKS;
    
    return apic_timer_frequency;
}

uint64_t apic_timer_get_frequency(void) {
    return apic_timer_frequency;
}

void apic_timer_set_frequency(uint64_t freq_hz) {
    apic_timer_frequency = freq_hz;
}

bool apic_timer_tsc_deadline_supported(void) {
    return tsc_deadline_available;
}

void apic_timer_set_tsc_deadline(uint64_t deadline) {
    if (!tsc_deadline_available)
        return;
    
    wrmsr(0x6E0, deadline);
}

void apic_timer_init(uint32_t vector, uint32_t divisor, uint32_t initial_count, bool periodic) {
    /* Set timer divisor */
    apic_write(LAPIC_TIMER_DIV, divisor);
    
    uint32_t lvt_value = (vector & LAPIC_LVT_VECTOR_MASK);
    if (periodic)
        lvt_value |= LAPIC_LVT_TIMER_PERIODIC;
    
    apic_write(LAPIC_TIMER, lvt_value);
    apic_write(LAPIC_TIMER_INITCNT, initial_count);
}

void apic_timer_stop(void) {
    apic_write(LAPIC_TIMER_INITCNT, 0);
    apic_lvt_set_mask(LAPIC_TIMER, true);
}

uint32_t apic_timer_get_current(void) {
    return apic_read(LAPIC_TIMER_CURRCNT);
}

uint32_t apic_get_errors(void) {
    /* Reading ESR requires writing to it first to latch errors */
    apic_write(LAPIC_ESR, 0);
    return apic_read(LAPIC_ESR);
}

void apic_clear_errors(void) {
    /* Clear errors by writing 0 twice */
    apic_write(LAPIC_ESR, 0);
    apic_write(LAPIC_ESR, 0);
}

bool apic_enable_x2apic(void) {
    if (!apic_check_x2apic_support()) {
        printk("apic: x2APIC not supported by CPU\n");
        return false;
    }
    
    uint64_t apic_base = rdmsr(IA32_APIC_BASE_MSR);
    
    if (!(apic_base & IA32_APIC_BASE_ENABLE)) {
        apic_base |= IA32_APIC_BASE_ENABLE;
        wrmsr(IA32_APIC_BASE_MSR, apic_base);
    }
    
    apic_base |= IA32_APIC_BASE_X2APIC;
    wrmsr(IA32_APIC_BASE_MSR, apic_base);
    
    x2apic_mode = true;
    
    return true;
}

void apic_init(void) {
    /* Check if APIC is supported */
    if (!apic_check_support()) {
        printk("apic: not supported by CPU\n");
        return;
    }
    
    uint32_t eax, ebx, ecx, edx;
    cpuid(1, &eax, &ebx, &ecx, &edx);
    tsc_deadline_available = (ecx & (1 << 24)) != 0;
    
    if (apic_check_x2apic_support()) {
         apic_enable_x2apic();
    }
    
    if (!x2apic_mode) {
        lapic_base_phys = apic_get_base();
        if (lapic_base_phys == 0)
            lapic_base_phys = LAPIC_DEFAULT_BASE;
        
        mmu_context_t *kernel_ctx = mmu_get_kernel_context();
        lapic_base_virt = lapic_base_phys;  /* Identity map for simplicity */
        
        int result = mmu_map_page(kernel_ctx, lapic_base_virt, lapic_base_phys,
                                  MMU_MAP_PRESENT | MMU_MAP_WRITE | MMU_MAP_NOCACHE);
        if (result != 0) {
            printk("apic: failed to map APIC registers\n");
            lapic_base_virt = 0;
            return;
        }
        
        apic_set_base(lapic_base_phys);
    }
    
    uint32_t svr = apic_read(LAPIC_SVR);
    svr |= LAPIC_SVR_ENABLE | APIC_SPURIOUS_VECTOR;
    apic_write(LAPIC_SVR, svr);
    
    apic_lvt_set_mask(LAPIC_TIMER, true);
    apic_lvt_set_mask(LAPIC_THERMAL, true);
    apic_lvt_set_mask(LAPIC_PERF, true);
    apic_lvt_set_mask(LAPIC_LINT0, true);
    apic_lvt_set_mask(LAPIC_LINT1, true);
    apic_lvt_set_mask(LAPIC_ERROR, true);
    
    apic_clear_errors();
    
    apic_write(LAPIC_EOI, 0);
    
    uint64_t freq = apic_timer_calibrate_pit();
    
    if (tsc_deadline_available) {
    }
    
   printk_ts("apic: initialized\n");
}
