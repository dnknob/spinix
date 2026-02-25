#include <arch/x86_64/cpuid.h>

#include <video/printk.h>

#include <klibc/string.h>

bool cpuid_is_supported(void)
{
    uint64_t flags_before, flags_after;

    __asm__ volatile (
        "pushfq\n"
        "pop %0\n"
        "mov %0, %1\n"
        "xor $0x200000, %1\n"
        "push %1\n"
        "popfq\n"
        "pushfq\n"
        "pop %1\n"
        "push %0\n"
        "popfq\n"
        : "=&r"(flags_before), "=&r"(flags_after)
        :
        : "cc"
    );

    return ((flags_before ^ flags_after) & 0x200000) != 0;
}

uint32_t cpuid_get_max_leaf(void)
{
    cpuid_regs_t regs;
    cpuid(0, 0, &regs);
    return regs.eax;
}

uint32_t cpuid_get_max_extended_leaf(void)
{
    cpuid_regs_t regs;
    cpuid(0x80000000, 0, &regs);
    return regs.eax;
}

void cpuid_get_vendor(char vendor[13])
{
    cpuid_regs_t regs;
    cpuid(0, 0, &regs);

    memcpy(vendor + 0, &regs.ebx, 4);
    memcpy(vendor + 4, &regs.edx, 4);
    memcpy(vendor + 8, &regs.ecx, 4);
    vendor[12] = '\0';
}

void cpuid_get_brand(char brand[49])
{
    cpuid_regs_t regs;

    cpuid(0x80000000, 0, &regs);
    if (regs.eax < 0x80000004) {
        brand[0] = '\0';
        return;
    }

    cpuid(0x80000002, 0, &regs);
    memcpy(brand + 0,  &regs, 16);

    cpuid(0x80000003, 0, &regs);
    memcpy(brand + 16, &regs, 16);

    cpuid(0x80000004, 0, &regs);
    memcpy(brand + 32, &regs, 16);

    brand[48] = '\0';
}

static void cpuid_decode_signature(uint32_t *family, uint32_t *model,
                                   uint32_t *stepping)
{
    cpuid_regs_t regs;
    cpuid(1, 0, &regs);

    uint32_t raw_stepping   = regs.eax & 0xF;
    uint32_t raw_model      = (regs.eax >>  4) & 0xF;
    uint32_t raw_family     = (regs.eax >>  8) & 0xF;
    uint32_t raw_ext_model  = (regs.eax >> 16) & 0xF;
    uint32_t raw_ext_family = (regs.eax >> 20) & 0xFF;

    *stepping = raw_stepping;

    if (raw_family == 0x0F)
        *family = raw_ext_family + raw_family;
    else
        *family = raw_family;

    if (raw_family == 0x06 || raw_family == 0x0F)
        *model = (raw_ext_model << 4) + raw_model;
    else
        *model = raw_model;
}

uint32_t cpuid_get_stepping(void)
{
    uint32_t family, model, stepping;
    cpuid_decode_signature(&family, &model, &stepping);
    return stepping;
}

uint32_t cpuid_get_model(void)
{
    uint32_t family, model, stepping;
    cpuid_decode_signature(&family, &model, &stepping);
    return model;
}

uint32_t cpuid_get_family(void)
{
    uint32_t family, model, stepping;
    cpuid_decode_signature(&family, &model, &stepping);
    return family;
}

uint8_t cpuid_get_local_apic_id(void)
{
    cpuid_regs_t regs;
    cpuid(1, 0, &regs);
    return (regs.ebx >> 24) & 0xFF;
}

uint8_t cpuid_get_logical_processor_count(void)
{
    cpuid_regs_t regs;
    cpuid(1, 0, &regs);
    return (regs.ebx >> 16) & 0xFF;
}

uint8_t cpuid_get_clflush_line_size(void)
{
    cpuid_regs_t regs;
    cpuid(1, 0, &regs);
    return ((regs.ebx >> 8) & 0xFF) * 8;
}

bool cpuid_has_feature(uint32_t feature_bit, uint32_t reg)
{
    cpuid_regs_t regs;
    cpuid(1, 0, &regs);

    uint32_t value;
    switch (reg) {
        case 0: value = regs.eax; break;
        case 1: value = regs.ebx; break;
        case 2: value = regs.ecx; break;
        case 3: value = regs.edx; break;
        default: return false;
    }

    return (value & feature_bit) != 0;
}

bool cpuid_has_feature_ecx(uint32_t feature_bit)
{
    cpuid_regs_t regs;
    cpuid(1, 0, &regs);
    return (regs.ecx & feature_bit) != 0;
}

bool cpuid_has_feature_edx(uint32_t feature_bit)
{
    cpuid_regs_t regs;
    cpuid(1, 0, &regs);
    return (regs.edx & feature_bit) != 0;
}

static bool cpuid_leaf7_available(void)
{
    cpuid_regs_t regs;
    cpuid(0, 0, &regs);
    return regs.eax >= 7;
}

bool cpuid_has_feature7_ebx(uint32_t feature_bit)
{
    if (!cpuid_leaf7_available()) return false;
    cpuid_regs_t regs;
    cpuid(7, 0, &regs);
    return (regs.ebx & feature_bit) != 0;
}

bool cpuid_has_feature7_ecx(uint32_t feature_bit)
{
    if (!cpuid_leaf7_available()) return false;
    cpuid_regs_t regs;
    cpuid(7, 0, &regs);
    return (regs.ecx & feature_bit) != 0;
}

bool cpuid_has_feature7_edx(uint32_t feature_bit)
{
    if (!cpuid_leaf7_available()) return false;
    cpuid_regs_t regs;
    cpuid(7, 0, &regs);
    return (regs.edx & feature_bit) != 0;
}

bool cpuid_has_extended_feature(uint32_t feature_bit)
{
    cpuid_regs_t regs;
    cpuid(0x80000000, 0, &regs);
    if (regs.eax < 0x80000001) return false;
    cpuid(0x80000001, 0, &regs);
    return (regs.edx & feature_bit) != 0;
}

bool cpuid_has_extended_feature_ecx(uint32_t feature_bit)
{
    cpuid_regs_t regs;
    cpuid(0x80000000, 0, &regs);
    if (regs.eax < 0x80000001) return false;
    cpuid(0x80000001, 0, &regs);
    return (regs.ecx & feature_bit) != 0;
}

bool cpuid_has_thermal_feature(uint32_t feature_bit)
{
    if (cpuid_get_max_leaf() < 6) return false;
    cpuid_regs_t regs;
    cpuid(6, 0, &regs);
    return (regs.eax & feature_bit) != 0;
}

bool cpuid_has_thermal_feature_ecx(uint32_t feature_bit)
{
    if (cpuid_get_max_leaf() < 6) return false;
    cpuid_regs_t regs;
    cpuid(6, 0, &regs);
    return (regs.ecx & feature_bit) != 0;
}

uint32_t cpuid_get_thermal_interrupt_thresholds(void)
{
    if (cpuid_get_max_leaf() < 6) return 0;
    cpuid_regs_t regs;
    cpuid(6, 0, &regs);
    return regs.ebx & 0xF;
}

int cpuid_get_cache_info(cpuid_cache_info_t *caches, int max_caches)
{
    if (cpuid_get_max_leaf() < 4 || !caches || max_caches <= 0)
        return 0;

    int count = 0;

    for (uint32_t subleaf = 0; count < max_caches; subleaf++) {
        cpuid_regs_t regs;
        cpuid(4, subleaf, &regs);

        uint8_t cache_type = regs.eax & 0x1F;
        if (cache_type == CPUID_CACHE_TYPE_NULL)
            break;

        cpuid_cache_info_t *c = &caches[count++];
        c->type      = cache_type;
        c->level     = (regs.eax >>  5) & 0x7;
        c->self_init = (regs.eax >>  8) & 0x1;
        c->fully_assoc = (regs.eax >> 9) & 0x1;

        c->max_ids_sharing = ((regs.eax >> 14) & 0xFFF) + 1;
        /* EAX[31:26] – max IDs of logical processors in the package minus 1 */
        c->max_ids_package = ((regs.eax >> 26) & 0x3F) + 1;

        c->line_size   = (regs.ebx & 0xFFF) + 1;           /* EBX[11:0]  + 1 */
        c->partitions  = ((regs.ebx >> 12) & 0x3FF) + 1;   /* EBX[21:12] + 1 */
        c->ways        = ((regs.ebx >> 22) & 0x3FF) + 1;   /* EBX[31:22] + 1 */
        c->sets        = regs.ecx + 1;                      /* ECX        + 1 */

        uint64_t size_bytes = (uint64_t)c->ways * c->partitions *
                              c->line_size * c->sets;
        c->size_kb = (uint32_t)(size_bytes / 1024);

        c->inclusive     = (regs.edx >> 1) & 0x1; /* EDX[1] */
        c->complex_index = (regs.edx >> 2) & 0x1; /* EDX[2] */
    }

    return count;
}

bool cpuid_get_topology(cpuid_topology_t *topo)
{
    if (!topo || cpuid_get_max_leaf() < 0xB)
        return false;

    cpuid_regs_t regs;

    cpuid(0xB, 0, &regs);
    /* EAX[4:0] = shift count for next level; 0 means leaf not valid */
    if ((regs.eax & 0x1F) == 0)
        return false;

    topo->smt_count  = regs.ebx & 0xFFFF; /* logical processors at this level */
    topo->x2apic_id  = regs.edx;          /* x2APIC ID (subleaf 0 already gives it) */

    cpuid(0xB, 1, &regs);
    uint32_t logical_per_pkg = regs.ebx & 0xFFFF;
    topo->x2apic_id = regs.edx;           /* updated; leaf B EDX = x2APIC ID always */
    topo->core_count = (topo->smt_count > 0)
                     ? logical_per_pkg / topo->smt_count
                     : logical_per_pkg;

    return true;
}

bool cpuid_has_apm_feature(uint32_t feature_bit)
{
    if (cpuid_get_max_extended_leaf() < 0x80000007) return false;
    cpuid_regs_t regs;
    cpuid(0x80000007, 0, &regs);
    return (regs.edx & feature_bit) != 0;
}

bool cpuid_tsc_is_invariant(void)
{
    return cpuid_has_apm_feature(CPUID_APM_EDX_TSC_INVARIANT);
}

bool cpuid_get_address_info(cpuid_address_info_t *info)
{
    if (!info || cpuid_get_max_extended_leaf() < 0x80000008)
        return false;

    cpuid_regs_t regs;
    cpuid(0x80000008, 0, &regs);

    info->physical_bits   = regs.eax & 0xFF;
    info->virtual_bits    = (regs.eax >> 8)  & 0xFF;
    info->guest_phys_bits = (regs.eax >> 16) & 0xFF;

    return true;
}

bool cpuid_is_hypervisor(void)
{
    return cpuid_has_feature_ecx(CPUID_FEAT_ECX_HYPERVISOR);
}

bool cpuid_get_hypervisor_vendor(char vendor[13])
{
    if (!cpuid_is_hypervisor()) {
        vendor[0] = '\0';
        return false;
    }

    cpuid_regs_t regs;
    cpuid(0x40000000, 0, &regs);

    memcpy(vendor + 0, &regs.ebx, 4);
    memcpy(vendor + 4, &regs.ecx, 4);
    memcpy(vendor + 8, &regs.edx, 4);
    vendor[12] = '\0';

    return true;
}

static const char *cache_type_str(uint8_t type)
{
    switch (type) {
        case CPUID_CACHE_TYPE_DATA:        return "Data";
        case CPUID_CACHE_TYPE_INSTRUCTION: return "Instruction";
        case CPUID_CACHE_TYPE_UNIFIED:     return "Unified";
        default:                           return "Unknown";
    }
}

void cpuid_print_info(void)
{
    char vendor[13];
    char brand[49];

    if (!cpuid_is_supported()) {
        printk("cpuid: not supported\n");
        return;
    }

    cpuid_get_vendor(vendor);
    cpuid_get_brand(brand);

    printk("cpuid: vendor: %s\n", vendor);
    if (brand[0] != '\0')
        printk("cpuid: brand:  %s\n", brand);

    uint32_t family, model, stepping;
    cpuid_decode_signature(&family, &model, &stepping);
    printk("cpuid: family: 0x%x  model: 0x%x  stepping: 0x%x\n",
           family, model, stepping);

    printk("cpuid: local APIC ID: %u  logical CPUs (pkg): %u  CLFLUSH: %u B\n",
           cpuid_get_local_apic_id(),
           cpuid_get_logical_processor_count(),
           cpuid_get_clflush_line_size());

    if (cpuid_is_hypervisor()) {
        char hv_vendor[13];
        if (cpuid_get_hypervisor_vendor(hv_vendor))
            printk("cpuid: hypervisor: %s\n", hv_vendor);
        else
            printk("cpuid: hypervisor: detected (unknown vendor)\n");
    }

    cpuid_address_info_t addr = {0};
    if (cpuid_get_address_info(&addr)) {
        printk("cpuid: address bits: physical=%u  virtual=%u",
               addr.physical_bits, addr.virtual_bits);
        if (addr.guest_phys_bits)
            printk("  guest_physical=%u", addr.guest_phys_bits);
        printk("\n");
    }

    printk("cpuid: invariant TSC: %s\n",
           cpuid_tsc_is_invariant() ? "yes" : "no");

    if (cpuid_get_max_leaf() >= 6) {
        printk("cpuid: thermal: DTS=%s  TurboBoost=%s  ARAT=%s  HWP=%s"
               "  thresholds=%u\n",
               cpuid_has_thermal_feature(CPUID_THERM_EAX_DTS)   ? "y" : "n",
               cpuid_has_thermal_feature(CPUID_THERM_EAX_TURBO) ? "y" : "n",
               cpuid_has_thermal_feature(CPUID_THERM_EAX_ARAT)  ? "y" : "n",
               cpuid_has_thermal_feature(CPUID_THERM_EAX_HWP)   ? "y" : "n",
               cpuid_get_thermal_interrupt_thresholds());
    }

    cpuid_topology_t topo = {0};
    if (cpuid_get_topology(&topo)) {
        printk("cpuid: topology: x2APIC ID=%u  SMT threads/core=%u"
               "  cores/package=%u\n",
               topo.x2apic_id, topo.smt_count, topo.core_count);
    }

    cpuid_cache_info_t caches[CPUID_CACHE_MAX_LEVELS];
    int ncaches = cpuid_get_cache_info(caches, CPUID_CACHE_MAX_LEVELS);
    if (ncaches > 0) {
        printk("cpuid: cache topology (%d level(s)):\n", ncaches);
        for (int i = 0; i < ncaches; i++) {
            cpuid_cache_info_t *c = &caches[i];
            printk("  L%u %s: %u KiB  %u-way  %u sets  %u B line"
                   "  %u sharing  %s\n",
                   c->level,
                   cache_type_str(c->type),
                   c->size_kb,
                   c->fully_assoc ? 0xFFFF : c->ways,   /* 0xFFFF = fully-assoc */
                   c->sets,
                   c->line_size,
                   c->max_ids_sharing,
                   c->inclusive ? "inclusive" : "exclusive");
        }
    }

    printk("cpuid: max leaf: 0x%x  max extended: 0x%x\n",
           cpuid_get_max_leaf(), cpuid_get_max_extended_leaf());
}
