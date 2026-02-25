#ifndef _X86_64_CPUID_H
#define _X86_64_CPUID_H

#include <klibc/types.h>

typedef struct {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
} cpuid_regs_t;

/* Feature flags - ECX (leaf 1) */
#define CPUID_FEAT_ECX_SSE3         (1 << 0)
#define CPUID_FEAT_ECX_PCLMUL       (1 << 1)
#define CPUID_FEAT_ECX_DTES64       (1 << 2)
#define CPUID_FEAT_ECX_MONITOR      (1 << 3)
#define CPUID_FEAT_ECX_DS_CPL       (1 << 4)
#define CPUID_FEAT_ECX_VMX          (1 << 5)
#define CPUID_FEAT_ECX_SMX          (1 << 6)
#define CPUID_FEAT_ECX_EIST         (1 << 7)
#define CPUID_FEAT_ECX_TM2          (1 << 8)
#define CPUID_FEAT_ECX_SSSE3        (1 << 9)
#define CPUID_FEAT_ECX_CNXT_ID      (1 << 10)
#define CPUID_FEAT_ECX_SDBG         (1 << 11)
#define CPUID_FEAT_ECX_FMA          (1 << 12)
#define CPUID_FEAT_ECX_CX16         (1 << 13)
#define CPUID_FEAT_ECX_XTPR         (1 << 14)
#define CPUID_FEAT_ECX_PDCM         (1 << 15)
#define CPUID_FEAT_ECX_PCID         (1 << 17)
#define CPUID_FEAT_ECX_DCA          (1 << 18)
#define CPUID_FEAT_ECX_SSE4_1       (1 << 19)
#define CPUID_FEAT_ECX_SSE4_2       (1 << 20)
#define CPUID_FEAT_ECX_X2APIC       (1 << 21)
#define CPUID_FEAT_ECX_MOVBE        (1 << 22)
#define CPUID_FEAT_ECX_POPCNT       (1 << 23)
#define CPUID_FEAT_ECX_TSC_DEADLINE (1 << 24)
#define CPUID_FEAT_ECX_AES          (1 << 25)
#define CPUID_FEAT_ECX_XSAVE        (1 << 26)
#define CPUID_FEAT_ECX_OSXSAVE      (1 << 27)
#define CPUID_FEAT_ECX_AVX          (1 << 28)
#define CPUID_FEAT_ECX_F16C         (1 << 29)
#define CPUID_FEAT_ECX_RDRAND       (1 << 30)
#define CPUID_FEAT_ECX_HYPERVISOR   (1U << 31)

/* Feature flags - EDX (leaf 1) */
#define CPUID_FEAT_EDX_FPU          (1 << 0)
#define CPUID_FEAT_EDX_VME          (1 << 1)
#define CPUID_FEAT_EDX_DE           (1 << 2)
#define CPUID_FEAT_EDX_PSE          (1 << 3)
#define CPUID_FEAT_EDX_TSC          (1 << 4)
#define CPUID_FEAT_EDX_MSR          (1 << 5)
#define CPUID_FEAT_EDX_PAE          (1 << 6)
#define CPUID_FEAT_EDX_MCE          (1 << 7)
#define CPUID_FEAT_EDX_CX8          (1 << 8)
#define CPUID_FEAT_EDX_APIC         (1 << 9)
#define CPUID_FEAT_EDX_SEP          (1 << 11)
#define CPUID_FEAT_EDX_MTRR         (1 << 12)
#define CPUID_FEAT_EDX_PGE          (1 << 13)
#define CPUID_FEAT_EDX_MCA          (1 << 14)
#define CPUID_FEAT_EDX_CMOV         (1 << 15)
#define CPUID_FEAT_EDX_PAT          (1 << 16)
#define CPUID_FEAT_EDX_PSE36        (1 << 17)
#define CPUID_FEAT_EDX_PSN          (1 << 18)
#define CPUID_FEAT_EDX_CLFSH        (1 << 19)
#define CPUID_FEAT_EDX_DS           (1 << 21)
#define CPUID_FEAT_EDX_ACPI         (1 << 22)
#define CPUID_FEAT_EDX_MMX          (1 << 23)
#define CPUID_FEAT_EDX_FXSR         (1 << 24)
#define CPUID_FEAT_EDX_SSE          (1 << 25)
#define CPUID_FEAT_EDX_SSE2         (1 << 26)
#define CPUID_FEAT_EDX_SS           (1 << 27)
#define CPUID_FEAT_EDX_HTT          (1 << 28)
#define CPUID_FEAT_EDX_TM           (1 << 29)
#define CPUID_FEAT_EDX_IA64         (1 << 30)
#define CPUID_FEAT_EDX_PBE          (1U << 31)

/* Feature flags - EBX (leaf 7, subleaf 0) */
#define CPUID_FEAT_EBX_FSGSBASE     (1 << 0)
#define CPUID_FEAT_EBX_TSC_ADJUST   (1 << 1)
#define CPUID_FEAT_EBX_SGX          (1 << 2)
#define CPUID_FEAT_EBX_BMI1         (1 << 3)
#define CPUID_FEAT_EBX_HLE          (1 << 4)
#define CPUID_FEAT_EBX_AVX2         (1 << 5)
#define CPUID_FEAT_EBX_FDP_EXCPTN   (1 << 6)
#define CPUID_FEAT_EBX_SMEP         (1 << 7)
#define CPUID_FEAT_EBX_BMI2         (1 << 8)
#define CPUID_FEAT_EBX_ERMS         (1 << 9)
#define CPUID_FEAT_EBX_INVPCID      (1 << 10)
#define CPUID_FEAT_EBX_RTM          (1 << 11)
#define CPUID_FEAT_EBX_PQM          (1 << 12)
#define CPUID_FEAT_EBX_FPU_CS_DS    (1 << 13)
#define CPUID_FEAT_EBX_MPX          (1 << 14)
#define CPUID_FEAT_EBX_PQE          (1 << 15)
#define CPUID_FEAT_EBX_AVX512F      (1 << 16)
#define CPUID_FEAT_EBX_AVX512DQ     (1 << 17)
#define CPUID_FEAT_EBX_RDSEED       (1 << 18)
#define CPUID_FEAT_EBX_ADX          (1 << 19)
#define CPUID_FEAT_EBX_SMAP         (1 << 20)
#define CPUID_FEAT_EBX_AVX512IFMA   (1 << 21)
#define CPUID_FEAT_EBX_PCOMMIT      (1 << 22)
#define CPUID_FEAT_EBX_CLFLUSHOPT   (1 << 23)
#define CPUID_FEAT_EBX_CLWB         (1 << 24)
#define CPUID_FEAT_EBX_INTEL_PT     (1 << 25)
#define CPUID_FEAT_EBX_AVX512PF     (1 << 26)
#define CPUID_FEAT_EBX_AVX512ER     (1 << 27)
#define CPUID_FEAT_EBX_AVX512CD     (1 << 28)
#define CPUID_FEAT_EBX_SHA          (1 << 29)
#define CPUID_FEAT_EBX_AVX512BW     (1 << 30)
#define CPUID_FEAT_EBX_AVX512VL     (1U << 31)

/* Feature flags - ECX (leaf 7, subleaf 0) */
#define CPUID_FEAT_ECX7_PREFETCHWT1 (1 << 0)
#define CPUID_FEAT_ECX7_AVX512VBMI  (1 << 1)
#define CPUID_FEAT_ECX7_UMIP        (1 << 2)
#define CPUID_FEAT_ECX7_PKU         (1 << 3)
#define CPUID_FEAT_ECX7_OSPKE       (1 << 4)
#define CPUID_FEAT_ECX7_WAITPKG     (1 << 5)
#define CPUID_FEAT_ECX7_AVX512VBMI2 (1 << 6)
#define CPUID_FEAT_ECX7_CET_SS      (1 << 7)
#define CPUID_FEAT_ECX7_GFNI        (1 << 8)
#define CPUID_FEAT_ECX7_VAES        (1 << 9)
#define CPUID_FEAT_ECX7_VPCLMULQDQ  (1 << 10)
#define CPUID_FEAT_ECX7_AVX512VNNI  (1 << 11)
#define CPUID_FEAT_ECX7_AVX512BITALG (1 << 12)
#define CPUID_FEAT_ECX7_TME_EN      (1 << 13)
#define CPUID_FEAT_ECX7_AVX512VPOPCNTDQ (1 << 14)
#define CPUID_FEAT_ECX7_LA57        (1 << 16)
#define CPUID_FEAT_ECX7_RDPID       (1 << 22)
#define CPUID_FEAT_ECX7_KL          (1 << 23)
#define CPUID_FEAT_ECX7_CLDEMOTE    (1 << 25)
#define CPUID_FEAT_ECX7_MOVDIRI     (1 << 27)
#define CPUID_FEAT_ECX7_MOVDIR64B   (1 << 28)
#define CPUID_FEAT_ECX7_ENQCMD      (1 << 29)
#define CPUID_FEAT_ECX7_SGX_LC      (1 << 30)
#define CPUID_FEAT_ECX7_PKS         (1U << 31)

/* Feature flags - EDX (leaf 7, subleaf 0) */
#define CPUID_FEAT_EDX7_SGX_KEYS    (1 << 1)
#define CPUID_FEAT_EDX7_AVX512_4VNNIW (1 << 2)
#define CPUID_FEAT_EDX7_AVX512_4FMAPS (1 << 3)
#define CPUID_FEAT_EDX7_FSRM        (1 << 4)
#define CPUID_FEAT_EDX7_AVX512_VP2INTERSECT (1 << 8)
#define CPUID_FEAT_EDX7_SRBDS_CTRL  (1 << 9)
#define CPUID_FEAT_EDX7_MD_CLEAR    (1 << 10)
#define CPUID_FEAT_EDX7_TSX_FORCE_ABORT (1 << 13)
#define CPUID_FEAT_EDX7_SERIALIZE   (1 << 14)
#define CPUID_FEAT_EDX7_HYBRID      (1 << 15)
#define CPUID_FEAT_EDX7_TSXLDTRK    (1 << 16)
#define CPUID_FEAT_EDX7_PCONFIG     (1 << 18)
#define CPUID_FEAT_EDX7_LBR         (1 << 19)
#define CPUID_FEAT_EDX7_CET_IBT     (1 << 20)
#define CPUID_FEAT_EDX7_AMX_BF16    (1 << 22)
#define CPUID_FEAT_EDX7_AVX512_FP16 (1 << 23)
#define CPUID_FEAT_EDX7_AMX_TILE    (1 << 24)
#define CPUID_FEAT_EDX7_AMX_INT8    (1 << 25)
#define CPUID_FEAT_EDX7_SPEC_CTRL   (1 << 26)
#define CPUID_FEAT_EDX7_STIBP       (1 << 27)
#define CPUID_FEAT_EDX7_L1D_FLUSH   (1 << 28)
#define CPUID_FEAT_EDX7_IA32_ARCH_CAPABILITIES (1 << 29)
#define CPUID_FEAT_EDX7_IA32_CORE_CAPABILITIES (1 << 30)
#define CPUID_FEAT_EDX7_SSBD        (1U << 31)

/* Thermal/power flags */
#define CPUID_THERM_EAX_DTS         (1 << 0)
#define CPUID_THERM_EAX_TURBO       (1 << 1)
#define CPUID_THERM_EAX_ARAT        (1 << 2)
#define CPUID_THERM_EAX_PLN         (1 << 4)
#define CPUID_THERM_EAX_ECMD        (1 << 5)
#define CPUID_THERM_EAX_PTM         (1 << 6)
#define CPUID_THERM_EAX_HWP         (1 << 7)
#define CPUID_THERM_EAX_HWP_NOTIFY  (1 << 8)
#define CPUID_THERM_EAX_HWP_ACT_WIN (1 << 9)
#define CPUID_THERM_EAX_HWP_EPP     (1 << 10)
#define CPUID_THERM_EAX_HWP_PKG     (1 << 11)
#define CPUID_THERM_EAX_HDC         (1 << 13)
#define CPUID_THERM_EAX_TURBO3      (1 << 14)
#define CPUID_THERM_EAX_HWP_CAP     (1 << 15)
#define CPUID_THERM_EAX_HWP_PECI    (1 << 16)
#define CPUID_THERM_EAX_HWP_FLEX    (1 << 17)
#define CPUID_THERM_EAX_HWP_FAST    (1 << 18)
#define CPUID_THERM_EAX_HW_FEEDBACK (1 << 19)
#define CPUID_THERM_EAX_HWP_IDLE    (1 << 20)
#define CPUID_THERM_ECX_HCFC        (1 << 0)
#define CPUID_THERM_ECX_ENERGY_BIAS (1 << 3)

/* Extended feature flags (leaf 0x80000001) */
#define CPUID_FEAT_EXT_ECX_LAHF     (1 << 0)
#define CPUID_FEAT_EXT_ECX_CMP_LEGACY (1 << 1)
#define CPUID_FEAT_EXT_ECX_SVM      (1 << 2)
#define CPUID_FEAT_EXT_ECX_EXTAPIC  (1 << 3)
#define CPUID_FEAT_EXT_ECX_CR8_LEGACY (1 << 4)
#define CPUID_FEAT_EXT_ECX_ABM      (1 << 5)
#define CPUID_FEAT_EXT_ECX_SSE4A    (1 << 6)
#define CPUID_FEAT_EXT_ECX_MISALIGNSSE (1 << 7)
#define CPUID_FEAT_EXT_ECX_3DNOWPREFETCH (1 << 8)
#define CPUID_FEAT_EXT_ECX_OSVW     (1 << 9)
#define CPUID_FEAT_EXT_ECX_IBS      (1 << 10)
#define CPUID_FEAT_EXT_ECX_XOP      (1 << 11)
#define CPUID_FEAT_EXT_ECX_SKINIT   (1 << 12)
#define CPUID_FEAT_EXT_ECX_WDT      (1 << 13)
#define CPUID_FEAT_EXT_ECX_LWP      (1 << 15)
#define CPUID_FEAT_EXT_ECX_FMA4     (1 << 16)
#define CPUID_FEAT_EXT_ECX_TCE      (1 << 17)
#define CPUID_FEAT_EXT_ECX_NODEID_MSR (1 << 19)
#define CPUID_FEAT_EXT_ECX_TBM      (1 << 21)
#define CPUID_FEAT_EXT_ECX_TOPOEXT  (1 << 22)
#define CPUID_FEAT_EXT_ECX_PERFCTR_CORE (1 << 23)
#define CPUID_FEAT_EXT_ECX_PERFCTR_NB (1 << 24)
#define CPUID_FEAT_EXT_ECX_DBX      (1 << 26)
#define CPUID_FEAT_EXT_ECX_PERFTSC  (1 << 27)
#define CPUID_FEAT_EXT_ECX_PCX_L2I  (1 << 28)
#define CPUID_FEAT_EXT_SYSCALL      (1 << 11)
#define CPUID_FEAT_EXT_MP           (1 << 19)
#define CPUID_FEAT_EXT_XD           (1 << 20)
#define CPUID_FEAT_EXT_MMXEXT       (1 << 22)
#define CPUID_FEAT_EXT_FXSR_OPT     (1 << 25)
#define CPUID_FEAT_EXT_1GB_PAGE     (1 << 26)
#define CPUID_FEAT_EXT_RDTSCP       (1 << 27)
#define CPUID_FEAT_EXT_LM           (1 << 29)
#define CPUID_FEAT_EXT_64BIT        (1 << 29)
#define CPUID_FEAT_EXT_3DNOWEXT     (1 << 30)
#define CPUID_FEAT_EXT_3DNOW        (1U << 31)

/* APM flags (leaf 0x80000007) */
#define CPUID_APM_EDX_TS            (1 << 0)
#define CPUID_APM_EDX_FID           (1 << 1)
#define CPUID_APM_EDX_VID           (1 << 2)
#define CPUID_APM_EDX_TTP           (1 << 3)
#define CPUID_APM_EDX_TM            (1 << 4)
#define CPUID_APM_EDX_STC           (1 << 5)
#define CPUID_APM_EDX_MUL100        (1 << 6)
#define CPUID_APM_EDX_HWPSTATE      (1 << 7)
#define CPUID_APM_EDX_TSC_INVARIANT (1 << 8)
#define CPUID_APM_EDX_CPB           (1 << 9)
#define CPUID_APM_EDX_EFF_FREQ_RO   (1 << 10)
#define CPUID_APM_EDX_PFR           (1 << 11)
#define CPUID_APM_EDX_PA            (1 << 12)

#define CPUID_CACHE_TYPE_NULL        0
#define CPUID_CACHE_TYPE_DATA        1
#define CPUID_CACHE_TYPE_INSTRUCTION 2
#define CPUID_CACHE_TYPE_UNIFIED     3
#define CPUID_CACHE_MAX_LEVELS       8

typedef struct {
    uint8_t  type;
    uint8_t  level;
    bool     self_init;
    bool     fully_assoc;
    uint32_t ways;
    uint32_t partitions;
    uint32_t line_size;
    uint32_t sets;
    uint32_t size_kb;
    uint32_t max_ids_sharing;
    uint32_t max_ids_package;
    bool     inclusive;
    bool     complex_index;
} cpuid_cache_info_t;

typedef struct {
    uint32_t x2apic_id;
    uint32_t smt_count;
    uint32_t core_count;
} cpuid_topology_t;

typedef struct {
    uint8_t physical_bits;
    uint8_t virtual_bits;
    uint8_t guest_phys_bits;
} cpuid_address_info_t;

#define CPUID_HYPERVISOR_VENDOR_KVM        "KVMKVMKVM\0\0\0"
#define CPUID_HYPERVISOR_VENDOR_VMWARE     "VMwareVMware"
#define CPUID_HYPERVISOR_VENDOR_HYPERV     "Microsoft Hv"
#define CPUID_HYPERVISOR_VENDOR_XEN        "XenVMMXenVMM"
#define CPUID_HYPERVISOR_VENDOR_VIRTUALBOX "VBoxVBoxVBox"
#define CPUID_HYPERVISOR_VENDOR_PARALLELS  " prl hyperv "
#define CPUID_HYPERVISOR_VENDOR_BHYVE      "bhyve bhyve "

static inline void cpuid(uint32_t leaf, uint32_t subleaf, cpuid_regs_t *regs)
{
    __asm__ volatile (
        "cpuid"
        : "=a"(regs->eax), "=b"(regs->ebx), "=c"(regs->ecx), "=d"(regs->edx)
        : "a"(leaf), "c"(subleaf)
        : "memory"
    );
}

bool     cpuid_is_supported(void);
uint32_t cpuid_get_max_leaf(void);
uint32_t cpuid_get_max_extended_leaf(void);
void     cpuid_get_vendor(char vendor[13]);
void     cpuid_get_brand(char brand[49]);
uint32_t cpuid_get_stepping(void);
uint32_t cpuid_get_model(void);
uint32_t cpuid_get_family(void);
uint8_t  cpuid_get_local_apic_id(void);
uint8_t  cpuid_get_logical_processor_count(void);
uint8_t  cpuid_get_clflush_line_size(void);
bool     cpuid_has_feature(uint32_t feature_bit, uint32_t reg);
bool     cpuid_has_feature_ecx(uint32_t feature_bit);
bool     cpuid_has_feature_edx(uint32_t feature_bit);
bool     cpuid_has_feature7_ebx(uint32_t feature_bit);
bool     cpuid_has_feature7_ecx(uint32_t feature_bit);
bool     cpuid_has_feature7_edx(uint32_t feature_bit);
bool     cpuid_has_extended_feature(uint32_t feature_bit);
bool     cpuid_has_extended_feature_ecx(uint32_t feature_bit);
bool     cpuid_has_thermal_feature(uint32_t feature_bit);
bool     cpuid_has_thermal_feature_ecx(uint32_t feature_bit);
uint32_t cpuid_get_thermal_interrupt_thresholds(void);
int      cpuid_get_cache_info(cpuid_cache_info_t *caches, int max_caches);
bool     cpuid_get_topology(cpuid_topology_t *topo);
bool     cpuid_has_apm_feature(uint32_t feature_bit);
bool     cpuid_tsc_is_invariant(void);
bool     cpuid_get_address_info(cpuid_address_info_t *info);
bool     cpuid_is_hypervisor(void);
bool     cpuid_get_hypervisor_vendor(char vendor[13]);
void     cpuid_print_info(void);

#endif