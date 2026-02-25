#include <arch/x86_64/apic.h>
#include <arch/x86_64/tsc.h>
#include <arch/x86_64/io.h>

#include <video/printk.h>

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define PIT_FREQUENCY       1193182     /* PIT runs at 1.193182 MHz */
#define PIT_CH0_DATA        0x40        /* Channel 0 data port */
#define PIT_CH2_DATA        0x42        /* Channel 2 data port */
#define PIT_MODE_CMD        0x43        /* Mode/Command register */
#define PIT_CH2_GATE        0x61        /* PC Speaker control */

static uint64_t tsc_boot = 0;
static uint64_t tsc_frequency = 0;      /* TSC frequency in Hz */
static bool tsc_available = false;
static bool tsc_invariant = false;
static bool tsc_constant = false;
static bool tsc_rdtscp_available = false;
static bool tsc_deadline_available = false;
static uint8_t calibration_method = TSC_CALIB_FALLBACK;

static inline void cpuid(uint32_t leaf, uint32_t subleaf, uint32_t *eax,
                         uint32_t *ebx, uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile("cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf), "c"(subleaf));
}

static uint64_t mul_div64(uint64_t a, uint64_t b, uint64_t c) {
    if (c == 0)
        return 0;
    
    uint64_t result;
    __asm__ volatile(
        "mulq %2\n\t"      /* rdx:rax = rax * b */
        "divq %3\n\t"      /* rax = rdx:rax / c */
        : "=a"(result)
        : "a"(a), "rm"(b), "rm"(c)
        : "rdx"
    );
    return result;
}

bool tsc_is_supported(void) {
    uint32_t eax, ebx, ecx, edx;
    cpuid(1, 0, &eax, &ebx, &ecx, &edx);
    /* TSC feature is bit 4 of EDX */
    return (edx & TSC_FEATURE_PRESENT) != 0;
}

bool tsc_has_rdtscp(void) {
    uint32_t eax, ebx, ecx, edx;

    cpuid(0x80000000, 0, &eax, &ebx, &ecx, &edx);
    if (eax < 0x80000001)
        return false;

    cpuid(0x80000001, 0, &eax, &ebx, &ecx, &edx);
    /* RDTSCP is bit 27 of EDX */
    return (edx & TSC_FEATURE_RDTSCP) != 0;
}

bool tsc_is_invariant(void) {
    uint32_t eax, ebx, ecx, edx;

    cpuid(0x80000000, 0, &eax, &ebx, &ecx, &edx);
    if (eax < 0x80000007)
        return false;

    cpuid(0x80000007, 0, &eax, &ebx, &ecx, &edx);
    /* Invariant TSC is bit 8 of EDX */
    return (edx & TSC_FEATURE_INVARIANT) != 0;
}

bool tsc_is_constant(void) {
    /* Constant TSC means it runs at a constant rate regardless of:
     * - CPU frequency scaling (SpeedStep, Turbo, etc.)
     * - Deep C-states
     * This is indicated by the Invariant TSC flag on modern CPUs */
    return tsc_invariant;
}

bool tsc_deadline_supported(void) {
    uint32_t eax, ebx, ecx, edx;
    cpuid(1, 0, &eax, &ebx, &ecx, &edx);
    /* TSC-Deadline is bit 24 of ECX */
    return (ecx & TSC_FEATURE_DEADLINE) != 0;
}

bool tsc_calibrate_cpuid(void) {
    uint32_t eax, ebx, ecx, edx;
    
    cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    if (eax < 0x15)
        return false;
    
    cpuid(0x15, 0, &eax, &ebx, &ecx, &edx);
    
    uint32_t denominator = eax;  /* TSC increment denominator */
    uint32_t numerator = ebx;    /* TSC increment numerator */
    uint32_t crystal_hz = ecx;   /* Crystal clock frequency in Hz */
    
    if (denominator == 0 || numerator == 0)
        return false;
    
    if (crystal_hz == 0) {
        cpuid(0, 0, &eax, &ebx, &ecx, &edx);
        if (eax < 0x16)
            return false;
        
        cpuid(0x16, 0, &eax, &ebx, &ecx, &edx);
        uint32_t base_mhz = eax;  /* Processor base frequency in MHz */
        
        if (base_mhz == 0)
            return false;
        
        uint64_t base_hz = (uint64_t)base_mhz * 1000000ULL;
        crystal_hz = (base_hz * denominator) / numerator;
    }
    
    tsc_frequency = ((uint64_t)crystal_hz * numerator) / denominator;
    
    if (tsc_frequency == 0)
        return false;
    
    calibration_method = TSC_CALIB_CPUID;
    
    return true;
}

bool tsc_calibrate_pit(void) {
    if (!tsc_available)
        return false;
    
    const uint32_t SAMPLES = 3;           /* Number of samples to take */
    const uint32_t PIT_TICKS = 11932;     /* ~10ms at 1.193182 MHz */
    
    uint64_t best_freq = 0;
    uint64_t min_error = UINT64_MAX;
    
    __asm__ volatile("cli");
    
    for (uint32_t sample = 0; sample < SAMPLES; sample++) {
        /* Set PIT channel 2 to one-shot mode */
        outb(PIT_MODE_CMD, 0xB0);  /* Channel 2, lobyte/hibyte, mode 0 */
        
        outb(PIT_CH2_DATA, PIT_TICKS & 0xFF);
        outb(PIT_CH2_DATA, (PIT_TICKS >> 8) & 0xFF);
        
        uint8_t gate = inb(PIT_CH2_GATE);
        outb(PIT_CH2_GATE, gate | 0x01);  /* Enable gate */
        
        uint64_t tsc_start = rdtsc_serialized();
        
        uint8_t pit_status;
        do {
            pit_status = inb(PIT_CH2_GATE);
        } while ((pit_status & 0x20) == 0);  /* Wait for output to go high */
        
        uint64_t tsc_end = rdtsc_serialized();
        
        gate = inb(PIT_CH2_GATE);
        outb(PIT_CH2_GATE, gate & ~0x01);  /* Disable gate */
        
        uint64_t tsc_delta = tsc_end - tsc_start;
        uint64_t freq = mul_div64(tsc_delta, PIT_FREQUENCY, PIT_TICKS);
        
        uint64_t error = (freq > best_freq) ? (freq - best_freq) : (best_freq - freq);
        
        if (sample == 0 || error < min_error) {
            best_freq = freq;
            min_error = error;
        }
    }
    
    __asm__ volatile("sti");
    
    if (best_freq == 0)
        return false;
    
    tsc_frequency = best_freq;
    calibration_method = TSC_CALIB_PIT;
    
    
    return true;
}

bool tsc_calibrate_apic(uint64_t apic_frequency_hz) {
    if (!tsc_available || apic_frequency_hz == 0)
        return false;

    uint32_t saved_lvt = apic_read(LAPIC_TIMER);
    uint32_t saved_div = apic_read(LAPIC_TIMER_DIV);

    apic_write(LAPIC_TIMER_DIV, LAPIC_TIMER_DIV_1);
    apic_write(LAPIC_TIMER, LAPIC_LVT_MASKED);

    uint32_t apic_ticks = apic_frequency_hz / 100;
    if (apic_ticks == 0)
        apic_ticks = 1000000;  /* Fallback */

    uint64_t tsc_start = rdtsc_serialized();

    apic_write(LAPIC_TIMER_INITCNT, apic_ticks);

    while (apic_read(LAPIC_TIMER_CURRCNT) != 0)
        __asm__ volatile("pause");

    uint64_t tsc_end = rdtsc_serialized();

    apic_write(LAPIC_TIMER_DIV, saved_div);
    apic_write(LAPIC_TIMER, saved_lvt);

    uint64_t tsc_delta = tsc_end - tsc_start;
    tsc_frequency = mul_div64(tsc_delta, apic_frequency_hz, apic_ticks);

    if (tsc_frequency == 0)
        return false;

    calibration_method = TSC_CALIB_APIC;
    printk("tsc: APIC calibration successful\n");

    return true;
}

uint8_t tsc_get_calibration_method(void) {
    return calibration_method;
}

const char *tsc_get_calibration_method_name(void) {
    switch (calibration_method) {
        case TSC_CALIB_CPUID:    return "CPUID";
        case TSC_CALIB_PIT:      return "PIT";
        case TSC_CALIB_APIC:     return "APIC";
        case TSC_CALIB_ACPI_PM:  return "ACPI PM";
        case TSC_CALIB_FALLBACK: return "Fallback";
        default:                 return "Unknown";
    }
}

void tsc_init(void) {
    tsc_available = tsc_is_supported();
    if (!tsc_available) {
        printk("tsc: not supported by CPU\n");
        return;
    }

    tsc_rdtscp_available = tsc_has_rdtscp();
    tsc_invariant = tsc_is_invariant();
    tsc_constant = tsc_is_constant();
    tsc_deadline_available = tsc_deadline_supported();

    if (tsc_calibrate_cpuid()) {
        goto calibration_done;
    }
    
    if (tsc_calibrate_pit()) {
        goto calibration_done;
    }
    
    uint64_t apic_freq = apic_timer_get_frequency();
    if (apic_freq > 0 && tsc_calibrate_apic(apic_freq)) {
        goto calibration_done;
    }
    
    printk("tsc: All calibration methods failed, using fallback\n");
    tsc_frequency = 2400000000ULL;  /* 2.4 GHz fallback */
    calibration_method = TSC_CALIB_FALLBACK;

calibration_done:
    printk("tsc: initialized (%lu MHz)\n", tsc_frequency / 1000000);
    tsc_boot = rdtsc();
}

uint64_t tsc_get_frequency(void) {
    return tsc_frequency;
}

void tsc_set_frequency(uint64_t freq_hz) {
    tsc_frequency = freq_hz;
    calibration_method = TSC_CALIB_FALLBACK;
}

uint64_t tsc_to_ns(uint64_t tsc_value) {
    if (tsc_frequency == 0)
        return 0;
    return mul_div64(tsc_value, 1000000000ULL, tsc_frequency);
}

uint64_t ns_to_tsc(uint64_t ns) {
    if (tsc_frequency == 0)
        return 0;
    return mul_div64(ns, tsc_frequency, 1000000000ULL);
}

uint64_t tsc_to_us(uint64_t tsc_value) {
    if (tsc_frequency == 0)
        return 0;
    return mul_div64(tsc_value, 1000000ULL, tsc_frequency);
}

uint64_t us_to_tsc(uint64_t us) {
    if (tsc_frequency == 0)
        return 0;
    return mul_div64(us, tsc_frequency, 1000000ULL);
}

uint64_t tsc_to_ms(uint64_t tsc_value) {
    if (tsc_frequency == 0)
        return 0;
    return mul_div64(tsc_value, 1000ULL, tsc_frequency);
}

uint64_t ms_to_tsc(uint64_t ms) {
    if (tsc_frequency == 0)
        return 0;
    return mul_div64(ms, tsc_frequency, 1000ULL);
}

void tsc_delay_ns(uint64_t ns) {
    if (tsc_frequency == 0)
        return;
    
    uint64_t ticks = ns_to_tsc(ns);
    uint64_t start = rdtsc();
    uint64_t end = start + ticks;
    
    while (rdtsc() < end)
        __asm__ volatile("pause");
}

void tsc_delay_us(uint64_t us) {
    if (tsc_frequency == 0)
        return;
    
    uint64_t ticks = us_to_tsc(us);
    uint64_t start = rdtsc();
    uint64_t end = start + ticks;
    
    while (rdtsc() < end)
        __asm__ volatile("pause");
}

void tsc_delay_ms(uint64_t ms) {
    if (tsc_frequency == 0)
        return;
    
    uint64_t ticks = ms_to_tsc(ms);
    uint64_t start = rdtsc();
    uint64_t end = start + ticks;
    
    while (rdtsc() < end)
        __asm__ volatile("pause");
}

uint64_t tsc_get_current_ns(void) {
    return tsc_to_ns(rdtsc());
}

uint64_t tsc_get_current_us(void) {
    return tsc_to_us(rdtsc());
}

uint64_t tsc_get_current_ms(void) {
    return tsc_to_ms(rdtsc());
}

uint64_t tsc_get_uptime_ms(void) {
    return tsc_to_ms(rdtsc() - tsc_boot);
}

uint64_t tsc_get_uptime_us(void) {
    return tsc_to_us(rdtsc() - tsc_boot);
}

uint64_t tsc_get_uptime_ns(void) {
    return tsc_to_ns(rdtsc() - tsc_boot);
}

void tsc_print_info(void) {
    printk("\n==============================\n");
    printk("TSC (Time Stamp Counter) Info\n");
    
    if (!tsc_available) {
        printk("status: not supported\n");
        printk("==============================\n\n");
        return;
    }
    
    printk("status:         supported\n");
    printk("frequency:      %lu Hz (%lu.%03lu MHz)\n",
           tsc_frequency,
           tsc_frequency / 1000000,
           (tsc_frequency / 1000) % 1000);
    printk("calibration:    %s\n", tsc_get_calibration_method_name());
    printk("Invariant TSC:  %s\n", tsc_invariant ? "yes" : "no");
    printk("constant rate:  %s\n", tsc_constant ? "yes" : "no");
    printk("RDTSCP:         %s\n", tsc_rdtscp_available ? "yes" : "no");
    printk("TSC-Deadline:   %s\n", tsc_deadline_available ? "yes" : "no");
    
    uint64_t current_tsc = rdtsc();
    uint64_t current_us = tsc_to_us(current_tsc);
    
    printk("\nCurrent TSC:    %lu\n", current_tsc);
    printk("uptime:         %lu.%06lu seconds\n",
           current_us / 1000000,
           current_us % 1000000);
    
    printk("==============================\n\n");
}
