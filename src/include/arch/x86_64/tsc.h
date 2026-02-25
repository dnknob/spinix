#ifndef TSC_H
#define TSC_H

#include <klibc/types.h>




#define TSC_FEATURE_PRESENT         (1 << 4)   /* CPUID.1:EDX[4] - TSC present */
#define TSC_FEATURE_INVARIANT       (1 << 8)   /* CPUID.80000007H:EDX[8] - Invariant TSC */
#define TSC_FEATURE_RDTSCP          (1 << 27)  /* CPUID.80000001H:EDX[27] - RDTSCP available */
#define TSC_FEATURE_DEADLINE        (1 << 24)  /* CPUID.1:ECX[24] - TSC-Deadline mode */

#define IA32_TSC_MSR                0x10       /* TSC value */
#define IA32_TSC_DEADLINE_MSR       0x6E0      /* TSC-Deadline */
#define IA32_TSC_ADJUST_MSR         0x3B       /* TSC adjustment */

#define TSC_CALIB_CPUID             0          /* CPUID leaf 0x15/0x16 */
#define TSC_CALIB_PIT               1          /* PIT-based calibration */
#define TSC_CALIB_APIC              2          /* APIC-based calibration */
#define TSC_CALIB_ACPI_PM           3          /* ACPI PM timer */
#define TSC_CALIB_FALLBACK          4          /* Fallback/default */

static inline uint64_t rdtsc(void) {
    uint32_t low, high;
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32) | low;
}

static inline uint64_t rdtscp(uint32_t *aux) {
    uint32_t low, high, processor_id;
    __asm__ volatile("rdtscp" : "=a"(low), "=d"(high), "=c"(processor_id));
    if (aux)
        *aux = processor_id;
    return ((uint64_t)high << 32) | low;
}

static inline uint64_t rdtsc_serialized(void) {
    __asm__ volatile("lfence" ::: "memory");
    return rdtsc();
}

static inline uint64_t rdtsc_full_fence(void) {
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile(
        "cpuid\n\t"
        "rdtsc\n\t"
        : "=a"(eax), "=d"(edx)
        : "a"(0)
        : "ebx", "ecx", "memory"
    );
    return ((uint64_t)edx << 32) | eax;
}

bool tsc_is_supported(void);
bool tsc_has_rdtscp(void);
bool tsc_is_invariant(void);
bool tsc_is_constant(void);
bool tsc_deadline_supported(void);

void tsc_init(void);

bool tsc_calibrate_cpuid(void);
bool tsc_calibrate_pit(void);
bool tsc_calibrate_apic(uint64_t apic_frequency_hz);
uint8_t tsc_get_calibration_method(void);
const char *tsc_get_calibration_method_name(void);

uint64_t tsc_get_frequency(void);
void tsc_set_frequency(uint64_t freq_hz);

uint64_t tsc_to_ns(uint64_t tsc_value);
uint64_t ns_to_tsc(uint64_t ns);
uint64_t tsc_to_us(uint64_t tsc_value);
uint64_t us_to_tsc(uint64_t us);
uint64_t tsc_to_ms(uint64_t tsc_value);
uint64_t ms_to_tsc(uint64_t ms);

void tsc_delay_ns(uint64_t ns);
void tsc_delay_us(uint64_t us);
void tsc_delay_ms(uint64_t ms);

uint64_t tsc_get_current_ns(void);
uint64_t tsc_get_current_us(void);
uint64_t tsc_get_current_ms(void);

uint64_t tsc_get_uptime_ms(void);
uint64_t tsc_get_uptime_us(void);
uint64_t tsc_get_uptime_ns(void);

void tsc_print_info(void);

#endif
