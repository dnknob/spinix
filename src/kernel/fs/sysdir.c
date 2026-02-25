#include <arch/x86_64/cpuid.h>

#include <blk/blk.h>

#include <drivers/pci.h>

#include <fs/sysdir.h>
#include <fs/sysfs.h>
#include <fs/tmpfs.h>

#include <mm/heap.h>

#include <video/printk.h>

#include <klibc/string.h>
#include <errno.h>

int sysdev_register(sysdev_t *dev) {
    if (dev == NULL || dev->name == NULL ||
        dev->subsys == NULL || dev->attrs == NULL)
        return -EINVAL;

    if (dev->registered)
        return -EEXIST;

    int n = 0;
    size_t subsys_len = strlen(dev->subsys);
    size_t name_len   = strlen(dev->name);

    if (subsys_len + 1 + name_len >= VFS_PATH_MAX)
        return -ENAMETOOLONG;

    memcpy(dev->path, dev->subsys, subsys_len);
    dev->path[subsys_len] = '/';
    memcpy(dev->path + subsys_len + 1, dev->name, name_len);
    dev->path[subsys_len + 1 + name_len] = '\0';

    int ret = sysfs_mkdir(dev->path, 0555);
    if (ret != 0 && ret != -EEXIST) {
        printk("sysdir: failed to create %s: %d\n", dev->path, ret);
        return ret;
    }

    for (n = 0; dev->attrs[n].name != NULL; n++) {
        ret = sysfs_create_attr(dev->path, &dev->attrs[n]);
        if (ret != 0) {
            printk("sysdir: failed to create attr '%s' in %s: %d\n",
                   dev->attrs[n].name, dev->path, ret);
            for (int i = 0; i < n; i++)
                sysfs_remove_attr(dev->path, dev->attrs[i].name);
            sysfs_rmdir(dev->path);
            return ret;
        }
    }

    dev->registered = true;

    if (dev->init != NULL) {
        ret = dev->init(dev);
        if (ret != 0) {
            printk("sysdir: device init hook failed for %s: %d\n",
                   dev->name, ret);
            sysdev_unregister(dev);
            return ret;
        }
    }
    
    return 0;
}

int sysdev_unregister(sysdev_t *dev) {
    if (dev == NULL || !dev->registered)
        return -EINVAL;

    if (dev->exit != NULL)
        dev->exit(dev);

    for (int i = 0; dev->attrs[i].name != NULL; i++)
        sysfs_remove_attr(dev->path, dev->attrs[i].name);

    int ret = sysfs_rmdir(dev->path);
    if (ret != 0)
        printk("sysdir: failed to remove %s: %d\n", dev->path, ret);

    dev->registered = false;
    dev->path[0]    = '\0';

    printk("sysdir: unregistered %s/%s\n", dev->subsys, dev->name);
    return 0;
}

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif

static int sysdir_buf_write(char *buf, size_t size, const char *fmt, ...) {
    if (size == 0) return 0;

    va_list ap;
    __builtin_va_start(ap, fmt);

    size_t pos = 0;

#define PUTC(c)  do { if (pos + 1 < size) buf[pos++] = (c); } while(0)

    const char *p = fmt;
    while (*p && pos + 1 < size) {
        if (*p != '%') { PUTC(*p++); continue; }
        p++;

        bool long_mod = false;
        if (*p == 'l') { long_mod = true; p++; }

        switch (*p) {
            case 's': {
                const char *s = __builtin_va_arg(ap, const char *);
                if (s == NULL) s = "(null)";
                while (*s && pos + 1 < size) buf[pos++] = *s++;
                break;
            }
            case 'u': {
                uint32_t v = __builtin_va_arg(ap, uint32_t);
                char tmp[12]; int tlen = 0;
                if (v == 0) { tmp[tlen++] = '0'; }
                else {
                    while (v) { tmp[tlen++] = '0' + (v % 10); v /= 10; }
                    for (int i = 0, j = tlen-1; i < j; i++, j--) {
                        char t = tmp[i]; tmp[i] = tmp[j]; tmp[j] = t;
                    }
                }
                for (int i = 0; i < tlen && pos + 1 < size; i++)
                    buf[pos++] = tmp[i];
                break;
            }
            case 'x': {
                const char *hex = "0123456789abcdef";
                PUTC('0'); PUTC('x');
                if (long_mod) {
                    uint64_t v = __builtin_va_arg(ap, uint64_t);
                    for (int shift = 60; shift >= 0; shift -= 4)
                        PUTC(hex[(v >> shift) & 0xF]);
                } else {
                    uint32_t v = __builtin_va_arg(ap, uint32_t);
                    for (int shift = 28; shift >= 0; shift -= 4)
                        PUTC(hex[(v >> shift) & 0xF]);
                }
                break;
            }
            default:
                PUTC('%');
                if (long_mod) PUTC('l');
                PUTC(*p);
                break;
        }
        p++;
    }

#undef PUTC

    buf[pos] = '\0';
    __builtin_va_end(ap);
    return (int)pos;
}

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

typedef struct {
    uint32_t family;
    uint32_t model;
    uint32_t stepping;
    uint32_t ecx;
    uint32_t edx;
} cpu0_info_t;

static void cpu0_read_info(cpu0_info_t *out) {
    cpuid_regs_t regs;
    cpuid(1, 0, &regs);

    uint32_t stepping  = regs.eax & 0xF;
    uint32_t model     = (regs.eax >> 4) & 0xF;
    uint32_t family    = (regs.eax >> 8) & 0xF;
    uint32_t ext_model = (regs.eax >> 16) & 0xF;
    uint32_t ext_fam   = (regs.eax >> 20) & 0xFF;

    out->family   = (family == 0x0F) ? (ext_fam + family) : family;
    out->model    = (family == 0x06 || family == 0x0F)
                    ? ((ext_model << 4) | model) : model;
    out->stepping = stepping;
    out->ecx      = regs.ecx;
    out->edx      = regs.edx;
}

static int cpu0_show_vendor(char *buf, size_t size) {
    char vendor[13]; cpuid_get_vendor(vendor);
    return sysdir_buf_write(buf, size, "%s\n", vendor);
}
static int cpu0_show_brand(char *buf, size_t size) {
    char brand[49]; cpuid_get_brand(brand);
    return (brand[0] == '\0')
        ? sysdir_buf_write(buf, size, "(unavailable)\n")
        : sysdir_buf_write(buf, size, "%s\n", brand);
}
static int cpu0_show_family(char *buf, size_t size) {
    cpu0_info_t i; cpu0_read_info(&i);
    return sysdir_buf_write(buf, size, "%u\n", i.family);
}
static int cpu0_show_model(char *buf, size_t size) {
    cpu0_info_t i; cpu0_read_info(&i);
    return sysdir_buf_write(buf, size, "%u\n", i.model);
}
static int cpu0_show_stepping(char *buf, size_t size) {
    cpu0_info_t i; cpu0_read_info(&i);
    return sysdir_buf_write(buf, size, "%u\n", i.stepping);
}
static int cpu0_show_local_apic_id(char *buf, size_t size) {
    return sysdir_buf_write(buf, size, "%u\n", (uint32_t)cpuid_get_local_apic_id());
}
static int cpu0_show_logical_cpus(char *buf, size_t size) {
    return sysdir_buf_write(buf, size, "%u\n", (uint32_t)cpuid_get_logical_processor_count());
}
static int cpu0_show_clflush_size(char *buf, size_t size) {
    return sysdir_buf_write(buf, size, "%u\n", (uint32_t)cpuid_get_clflush_line_size());
}
static int cpu0_show_features_ecx(char *buf, size_t size) {
    cpu0_info_t i; cpu0_read_info(&i);
    return sysdir_buf_write(buf, size, "%x\n", i.ecx);
}
static int cpu0_show_features_edx(char *buf, size_t size) {
    cpu0_info_t i; cpu0_read_info(&i);
    return sysdir_buf_write(buf, size, "%x\n", i.edx);
}
static int cpu0_show_features_ebx7(char *buf, size_t size) {
    cpuid_regs_t r; cpuid(0, 0, &r);
    if (r.eax < 7) return sysdir_buf_write(buf, size, "0x00000000\n");
    cpuid(7, 0, &r); return sysdir_buf_write(buf, size, "%x\n", r.ebx);
}
static int cpu0_show_features_ecx7(char *buf, size_t size) {
    cpuid_regs_t r; cpuid(0, 0, &r);
    if (r.eax < 7) return sysdir_buf_write(buf, size, "0x00000000\n");
    cpuid(7, 0, &r); return sysdir_buf_write(buf, size, "%x\n", r.ecx);
}
static int cpu0_show_features_edx7(char *buf, size_t size) {
    cpuid_regs_t r; cpuid(0, 0, &r);
    if (r.eax < 7) return sysdir_buf_write(buf, size, "0x00000000\n");
    cpuid(7, 0, &r); return sysdir_buf_write(buf, size, "%x\n", r.edx);
}
static int cpu0_show_ext_features_edx(char *buf, size_t size) {
    cpuid_regs_t r; cpuid(0x80000000, 0, &r);
    if (r.eax < 0x80000001) return sysdir_buf_write(buf, size, "0x00000000\n");
    cpuid(0x80000001, 0, &r); return sysdir_buf_write(buf, size, "%x\n", r.edx);
}
static int cpu0_show_ext_features_ecx(char *buf, size_t size) {
    cpuid_regs_t r; cpuid(0x80000000, 0, &r);
    if (r.eax < 0x80000001) return sysdir_buf_write(buf, size, "0x00000000\n");
    cpuid(0x80000001, 0, &r); return sysdir_buf_write(buf, size, "%x\n", r.ecx);
}
static int cpu0_show_physical_bits(char *buf, size_t size) {
    cpuid_address_info_t a = {0};
    return cpuid_get_address_info(&a)
        ? sysdir_buf_write(buf, size, "%u\n", (uint32_t)a.physical_bits)
        : sysdir_buf_write(buf, size, "0\n");
}
static int cpu0_show_virtual_bits(char *buf, size_t size) {
    cpuid_address_info_t a = {0};
    return cpuid_get_address_info(&a)
        ? sysdir_buf_write(buf, size, "%u\n", (uint32_t)a.virtual_bits)
        : sysdir_buf_write(buf, size, "0\n");
}
static int cpu0_show_guest_phys_bits(char *buf, size_t size) {
    cpuid_address_info_t a = {0};
    return cpuid_get_address_info(&a)
        ? sysdir_buf_write(buf, size, "%u\n", (uint32_t)a.guest_phys_bits)
        : sysdir_buf_write(buf, size, "0\n");
}
static int cpu0_show_tsc_invariant(char *buf, size_t size) {
    return sysdir_buf_write(buf, size, "%u\n", (uint32_t)cpuid_tsc_is_invariant());
}
static int cpu0_show_thermal_features(char *buf, size_t size) {
    if (cpuid_get_max_leaf() < 6) return sysdir_buf_write(buf, size, "0x00000000\n");
    cpuid_regs_t r; cpuid(6, 0, &r);
    return sysdir_buf_write(buf, size, "%x\n", r.eax);
}
static int cpu0_show_thermal_thresholds(char *buf, size_t size) {
    return sysdir_buf_write(buf, size, "%u\n", cpuid_get_thermal_interrupt_thresholds());
}
static int cpu0_show_x2apic_id(char *buf, size_t size) {
    cpuid_topology_t t = {0};
    return cpuid_get_topology(&t)
        ? sysdir_buf_write(buf, size, "%u\n", t.x2apic_id)
        : sysdir_buf_write(buf, size, "0\n");
}
static int cpu0_show_smt_count(char *buf, size_t size) {
    cpuid_topology_t t = {0};
    return cpuid_get_topology(&t)
        ? sysdir_buf_write(buf, size, "%u\n", t.smt_count)
        : sysdir_buf_write(buf, size, "0\n");
}
static int cpu0_show_core_count(char *buf, size_t size) {
    cpuid_topology_t t = {0};
    return cpuid_get_topology(&t)
        ? sysdir_buf_write(buf, size, "%u\n", t.core_count)
        : sysdir_buf_write(buf, size, "0\n");
}
static int cpu0_show_cache_info(char *buf, size_t size) {
    static const char *tnames[] = {
        [CPUID_CACHE_TYPE_DATA]        = "Data",
        [CPUID_CACHE_TYPE_INSTRUCTION] = "Instruction",
        [CPUID_CACHE_TYPE_UNIFIED]     = "Unified",
    };
    cpuid_cache_info_t caches[CPUID_CACHE_MAX_LEVELS];
    int n = cpuid_get_cache_info(caches, CPUID_CACHE_MAX_LEVELS);
    if (n == 0) return sysdir_buf_write(buf, size, "(unavailable)\n");
    size_t pos = 0;
    for (int i = 0; i < n && pos + 1 < size; i++) {
        cpuid_cache_info_t *c = &caches[i];
        const char *tn = (c->type < 4 && tnames[c->type]) ? tnames[c->type] : "Unknown";
        uint32_t ways = c->fully_assoc ? 0xFFFFu : c->ways;
        int w = sysdir_buf_write(buf + pos, size - pos,
            "L%u %s %u KiB %u-way %u sets %u B line %u sharing%s\n",
            (uint32_t)c->level, tn, c->size_kb, ways,
            c->sets, c->line_size, c->max_ids_sharing,
            c->inclusive ? " inclusive" : "");
        if (w <= 0) break;
        pos += (size_t)w;
    }
    return (int)pos;
}
static int cpu0_show_max_leaf(char *buf, size_t size) {
    return sysdir_buf_write(buf, size, "%x\n", cpuid_get_max_leaf());
}
static int cpu0_show_max_ext_leaf(char *buf, size_t size) {
    return sysdir_buf_write(buf, size, "%x\n", cpuid_get_max_extended_leaf());
}
static int cpu0_show_hypervisor(char *buf, size_t size) {
    return sysdir_buf_write(buf, size, "%u\n", (uint32_t)cpuid_is_hypervisor());
}
static int cpu0_show_hv_vendor(char *buf, size_t size) {
    char vendor[13];
    return cpuid_get_hypervisor_vendor(vendor)
        ? sysdir_buf_write(buf, size, "%s\n", vendor)
        : sysdir_buf_write(buf, size, "(none)\n");
}

static sysfs_attr_t cpu0_attrs[] = {
    SYSFS_ATTR_RO("vendor",             cpu0_show_vendor),
    SYSFS_ATTR_RO("brand",              cpu0_show_brand),
    SYSFS_ATTR_RO("family",             cpu0_show_family),
    SYSFS_ATTR_RO("model",              cpu0_show_model),
    SYSFS_ATTR_RO("stepping",           cpu0_show_stepping),
    SYSFS_ATTR_RO("local_apic_id",      cpu0_show_local_apic_id),
    SYSFS_ATTR_RO("logical_cpus",       cpu0_show_logical_cpus),
    SYSFS_ATTR_RO("clflush_size",       cpu0_show_clflush_size),
    SYSFS_ATTR_RO("features_ecx",       cpu0_show_features_ecx),
    SYSFS_ATTR_RO("features_edx",       cpu0_show_features_edx),
    SYSFS_ATTR_RO("features_ebx7",      cpu0_show_features_ebx7),
    SYSFS_ATTR_RO("features_ecx7",      cpu0_show_features_ecx7),
    SYSFS_ATTR_RO("features_edx7",      cpu0_show_features_edx7),
    SYSFS_ATTR_RO("ext_features_edx",   cpu0_show_ext_features_edx),
    SYSFS_ATTR_RO("ext_features_ecx",   cpu0_show_ext_features_ecx),
    SYSFS_ATTR_RO("physical_bits",      cpu0_show_physical_bits),
    SYSFS_ATTR_RO("virtual_bits",       cpu0_show_virtual_bits),
    SYSFS_ATTR_RO("guest_phys_bits",    cpu0_show_guest_phys_bits),
    SYSFS_ATTR_RO("tsc_invariant",      cpu0_show_tsc_invariant),
    SYSFS_ATTR_RO("thermal_features",   cpu0_show_thermal_features),
    SYSFS_ATTR_RO("thermal_thresholds", cpu0_show_thermal_thresholds),
    SYSFS_ATTR_RO("x2apic_id",          cpu0_show_x2apic_id),
    SYSFS_ATTR_RO("smt_count",          cpu0_show_smt_count),
    SYSFS_ATTR_RO("core_count",         cpu0_show_core_count),
    SYSFS_ATTR_RO("cache_info",         cpu0_show_cache_info),
    SYSFS_ATTR_RO("max_leaf",           cpu0_show_max_leaf),
    SYSFS_ATTR_RO("max_ext_leaf",       cpu0_show_max_ext_leaf),
    SYSFS_ATTR_RO("hypervisor",         cpu0_show_hypervisor),
    SYSFS_ATTR_RO("hv_vendor",          cpu0_show_hv_vendor),
    SYSFS_ATTR_SENTINEL
};

static sysdev_t cpu0_dev = {
    .name   = "cpu0",
    .subsys = SYSDEV_SUBSYS_DEVICES,
    .attrs  = cpu0_attrs,
};

int sysdev_register_cpu(void) {
    return sysdev_register(&cpu0_dev);
}

#define MAX_PCI_SYSFS  32

static pci_device_t *pci_sysfs_devs[MAX_PCI_SYSFS];

static sysfs_attr_t  pci_sysfs_attrs  [MAX_PCI_SYSFS][2];
static sysdev_t      pci_sysfs_sysdevs[MAX_PCI_SYSFS];

static char          pci_sysfs_names  [MAX_PCI_SYSFS][12];

static void pci_fmt_addr(char out[12], uint8_t bus, uint8_t dev, uint8_t fn)
{
    static const char hex[] = "0123456789abcdef";
    out[0] = hex[(bus >> 4) & 0xF];
    out[1] = hex[bus        & 0xF];
    out[2] = ':';
    out[3] = hex[(dev >> 4) & 0xF];
    out[4] = hex[dev        & 0xF];
    out[5] = '.';
    out[6] = hex[fn         & 0xF];
    out[7] = '\0';
}

static int pci_show_info(char *buf, size_t size, const pci_device_t *dev)
{
    if (dev == NULL || size == 0)
        return 0;

    size_t pos = 0;

#define W(...) do { \
    int _w = sysdir_buf_write(buf + pos, size - pos, __VA_ARGS__); \
    if (_w > 0) pos += (size_t)_w; \
} while (0)

    char addr[12];
    pci_fmt_addr(addr, dev->bus, dev->device, dev->function);

    W("location: %s\n",     addr);
    W("vendor_id: %x\n",    (uint32_t)dev->vendor_id);
    W("vendor: %s\n",       pci_get_vendor_name(dev->vendor_id));
    W("device_id: %x\n",    (uint32_t)dev->device_id);
    W("class: %s\n",        pci_get_class_name(dev->class_code));
    W("class_code: %u\n",   (uint32_t)dev->class_code);
    W("subclass: %u\n",     (uint32_t)dev->subclass);
    W("prog_if: %u\n",      (uint32_t)dev->prog_if);
    W("revision: %u\n",     (uint32_t)dev->revision_id);
    W("header_type: %u\n",  (uint32_t)dev->header_type);
    W("irq_line: %u\n",     (uint32_t)dev->interrupt_line);
    W("irq_pin: %u\n",      (uint32_t)dev->interrupt_pin);
    W("msi: %u\n",          (uint32_t)dev->has_msi);
    W("msix: %u\n",         (uint32_t)dev->has_msix);

    if (dev->has_msix) {
        int tbl = pci_msix_get_table_size((pci_device_t *)dev);
        W("msix_vectors: %u\n", (uint32_t)(tbl > 0 ? tbl : 0));
    }
    if (dev->has_msi) {
        int maxv = pci_msi_get_max_vectors((pci_device_t *)dev);
        W("msi_max_vectors: %u\n", (uint32_t)(maxv > 0 ? maxv : 0));
    }

#undef W
    return (int)pos;
}

#define PCI_SLOT_TRAMPOLINE(n) \
    static int pci_info_show_##n(char *buf, size_t size) { \
        return pci_show_info(buf, size, pci_sysfs_devs[n]); \
    }

PCI_SLOT_TRAMPOLINE( 0) PCI_SLOT_TRAMPOLINE( 1) PCI_SLOT_TRAMPOLINE( 2) PCI_SLOT_TRAMPOLINE( 3)
PCI_SLOT_TRAMPOLINE( 4) PCI_SLOT_TRAMPOLINE( 5) PCI_SLOT_TRAMPOLINE( 6) PCI_SLOT_TRAMPOLINE( 7)
PCI_SLOT_TRAMPOLINE( 8) PCI_SLOT_TRAMPOLINE( 9) PCI_SLOT_TRAMPOLINE(10) PCI_SLOT_TRAMPOLINE(11)
PCI_SLOT_TRAMPOLINE(12) PCI_SLOT_TRAMPOLINE(13) PCI_SLOT_TRAMPOLINE(14) PCI_SLOT_TRAMPOLINE(15)
PCI_SLOT_TRAMPOLINE(16) PCI_SLOT_TRAMPOLINE(17) PCI_SLOT_TRAMPOLINE(18) PCI_SLOT_TRAMPOLINE(19)
PCI_SLOT_TRAMPOLINE(20) PCI_SLOT_TRAMPOLINE(21) PCI_SLOT_TRAMPOLINE(22) PCI_SLOT_TRAMPOLINE(23)
PCI_SLOT_TRAMPOLINE(24) PCI_SLOT_TRAMPOLINE(25) PCI_SLOT_TRAMPOLINE(26) PCI_SLOT_TRAMPOLINE(27)
PCI_SLOT_TRAMPOLINE(28) PCI_SLOT_TRAMPOLINE(29) PCI_SLOT_TRAMPOLINE(30) PCI_SLOT_TRAMPOLINE(31)

static const sysfs_show_t pci_info_fns[MAX_PCI_SYSFS] = {
    pci_info_show_0,  pci_info_show_1,  pci_info_show_2,  pci_info_show_3,
    pci_info_show_4,  pci_info_show_5,  pci_info_show_6,  pci_info_show_7,
    pci_info_show_8,  pci_info_show_9,  pci_info_show_10, pci_info_show_11,
    pci_info_show_12, pci_info_show_13, pci_info_show_14, pci_info_show_15,
    pci_info_show_16, pci_info_show_17, pci_info_show_18, pci_info_show_19,
    pci_info_show_20, pci_info_show_21, pci_info_show_22, pci_info_show_23,
    pci_info_show_24, pci_info_show_25, pci_info_show_26, pci_info_show_27,
    pci_info_show_28, pci_info_show_29, pci_info_show_30, pci_info_show_31,
};

static int pci_show_count(char *buf, size_t size) {
    return sysdir_buf_write(buf, size, "%u\n",
                            (uint32_t)pci_get_device_count());
}

static sysfs_attr_t pci_bus_attrs[] = {
    SYSFS_ATTR_RO("count", pci_show_count),
    SYSFS_ATTR_SENTINEL
};

static sysdev_t pci_bus_dev = {
    .name   = "pci",
    .subsys = SYSDEV_SUBSYS_BUS,
    .attrs  = pci_bus_attrs,
};

int sysdev_register_pci(void)
{
    int ret = sysdev_register(&pci_bus_dev);
    if (ret != 0) {
        printk("sysdir: failed to register /sys/bus/pci: %d\n", ret);
        return ret;
    }

    ret = sysfs_mkdir(SYSDEV_SUBSYS_PCI_DEVS, 0555);
    if (ret != 0 && ret != -EEXIST) {
        printk("sysdir: failed to create %s: %d\n",
               SYSDEV_SUBSYS_PCI_DEVS, ret);
        return ret;
    }

    size_t count = pci_get_device_count();
    if (count > MAX_PCI_SYSFS) {
        printk("sysdir: %zu PCI devices, capping sysfs at %d\n",
               count, MAX_PCI_SYSFS);
        count = MAX_PCI_SYSFS;
    }

    for (size_t i = 0; i < count; i++) {
        pci_device_t *dev = pci_get_device(i);
        if (dev == NULL)
            continue;

        pci_sysfs_devs[i] = dev;

        pci_fmt_addr(pci_sysfs_names[i],
                     dev->bus, dev->device, dev->function);

        pci_sysfs_attrs[i][0].name  = "info";
        pci_sysfs_attrs[i][0].mode  = 0444;
        pci_sysfs_attrs[i][0].show  = pci_info_fns[i];
        pci_sysfs_attrs[i][0].store = NULL;
        pci_sysfs_attrs[i][1]       = (sysfs_attr_t)SYSFS_ATTR_SENTINEL;

        pci_sysfs_sysdevs[i].name       = pci_sysfs_names[i];
        pci_sysfs_sysdevs[i].subsys     = SYSDEV_SUBSYS_PCI_DEVS;
        pci_sysfs_sysdevs[i].attrs      = pci_sysfs_attrs[i];
        pci_sysfs_sysdevs[i].init       = NULL;
        pci_sysfs_sysdevs[i].exit       = NULL;
        pci_sysfs_sysdevs[i].priv       = dev;
        pci_sysfs_sysdevs[i].registered = false;

        ret = sysdev_register(&pci_sysfs_sysdevs[i]);
        if (ret != 0) {
            printk("sysdir: skipping PCI device %s: %d\n",
                   pci_sysfs_names[i], ret);
            /* Non-fatal — continue with remaining devices */
        }
    }

    return 0;
}

static int tmpfs_sysfs_show_nodes(char *buf, size_t size) {
    tmpfs_stats_t s = {0};
    if (tmpfs_get_stats("/", &s) != 0)
        return sysdir_buf_write(buf, size, "0\n");
    return sysdir_buf_write(buf, size, "%u\n", (uint32_t)s.total_nodes);
}

static int tmpfs_sysfs_show_size(char *buf, size_t size) {
    tmpfs_stats_t s = {0};
    if (tmpfs_get_stats("/", &s) != 0)
        return sysdir_buf_write(buf, size, "0\n");
    return sysdir_buf_write(buf, size, "%u\n", (uint32_t)s.total_size);
}

static int tmpfs_sysfs_show_max_size(char *buf, size_t size) {
    tmpfs_stats_t s = {0};
    if (tmpfs_get_stats("/", &s) != 0)
        return sysdir_buf_write(buf, size, "0\n");
    /* 0 means unlimited — surface that explicitly */
    if (s.max_size == 0)
        return sysdir_buf_write(buf, size, "unlimited\n");
    return sysdir_buf_write(buf, size, "%u\n", (uint32_t)s.max_size);
}

static sysfs_attr_t tmpfs_fs_attrs[] = {
    SYSFS_ATTR_RO("nodes",    tmpfs_sysfs_show_nodes),
    SYSFS_ATTR_RO("size",     tmpfs_sysfs_show_size),
    SYSFS_ATTR_RO("max_size", tmpfs_sysfs_show_max_size),
    SYSFS_ATTR_SENTINEL
};

static sysdev_t tmpfs_fs_dev = {
    .name   = "tmpfs",
    .subsys = SYSDEV_SUBSYS_FS,   /* -> /sys/fs/tmpfs/ */
    .attrs  = tmpfs_fs_attrs,
};

int sysdev_register_fs(void) {
    int ret = sysdev_register(&tmpfs_fs_dev);
    if (ret != 0)
        printk("sysdir: failed to register /sys/fs/tmpfs: %d\n", ret);
    return ret;
}

#define MAX_BLK_SYSFS  16
#define BLK_ATTR_COUNT  6

static struct blk_device *blk_sysfs_devs [MAX_BLK_SYSFS];
static sysfs_attr_t       blk_sysfs_attrs[MAX_BLK_SYSFS][BLK_ATTR_COUNT];
static sysdev_t           blk_sysfs_sysd [MAX_BLK_SYSFS];
static char               blk_sysfs_names[MAX_BLK_SYSFS][32];
static int                blk_sysfs_count = 0;

static int blk_show_dev_impl(char *buf, size_t size,
                              const struct blk_device *dev) {
    return sysdir_buf_write(buf, size, "%u:%u\n",
                            (uint32_t)dev->major, (uint32_t)dev->minor);
}

static int blk_show_block_size_impl(char *buf, size_t size,
                                    const struct blk_device *dev) {
    return sysdir_buf_write(buf, size, "%u\n", dev->block_size);
}

static int blk_show_num_blocks_impl(char *buf, size_t size,
                                    const struct blk_device *dev) {
    uint64_t n  = dev->num_blocks;
    uint32_t hi = (uint32_t)(n >> 32);
    uint32_t lo = (uint32_t)(n & 0xFFFFFFFF);

    if (hi == 0)
        return sysdir_buf_write(buf, size, "%u\n", lo);

    return sysdir_buf_write(buf, size, "%u%u\n", hi, lo);
}

static int blk_show_removable_impl(char *buf, size_t size,
                                   const struct blk_device *dev) {
    return sysdir_buf_write(buf, size, "%u\n",
                            (uint32_t)!!(dev->flags & BLK_FLAG_REMOVABLE));
}

static int blk_show_ro_impl(char *buf, size_t size,
                            const struct blk_device *dev) {
    return sysdir_buf_write(buf, size, "%u\n",
                            (uint32_t)!!(dev->flags & BLK_FLAG_READ_ONLY));
}

#define BLK_SLOT_TRAMPOLINES(n)                                                 \
    static int blk_show_dev_##n      (char *b, size_t s) {                      \
        return blk_show_dev_impl(b, s, blk_sysfs_devs[n]); }                    \
    static int blk_show_bsize_##n    (char *b, size_t s) {                      \
        return blk_show_block_size_impl(b, s, blk_sysfs_devs[n]); }             \
    static int blk_show_nblocks_##n  (char *b, size_t s) {                      \
        return blk_show_num_blocks_impl(b, s, blk_sysfs_devs[n]); }             \
    static int blk_show_removable_##n(char *b, size_t s) {                      \
        return blk_show_removable_impl(b, s, blk_sysfs_devs[n]); }              \
    static int blk_show_ro_##n       (char *b, size_t s) {                      \
        return blk_show_ro_impl(b, s, blk_sysfs_devs[n]); }

BLK_SLOT_TRAMPOLINES( 0) BLK_SLOT_TRAMPOLINES( 1) BLK_SLOT_TRAMPOLINES( 2)
BLK_SLOT_TRAMPOLINES( 3) BLK_SLOT_TRAMPOLINES( 4) BLK_SLOT_TRAMPOLINES( 5)
BLK_SLOT_TRAMPOLINES( 6) BLK_SLOT_TRAMPOLINES( 7) BLK_SLOT_TRAMPOLINES( 8)
BLK_SLOT_TRAMPOLINES( 9) BLK_SLOT_TRAMPOLINES(10) BLK_SLOT_TRAMPOLINES(11)
BLK_SLOT_TRAMPOLINES(12) BLK_SLOT_TRAMPOLINES(13) BLK_SLOT_TRAMPOLINES(14)
BLK_SLOT_TRAMPOLINES(15)

typedef struct {
    sysfs_show_t dev;
    sysfs_show_t block_size;
    sysfs_show_t num_blocks;
    sysfs_show_t removable;
    sysfs_show_t ro;
} blk_slot_fns_t;

static const blk_slot_fns_t blk_fns[MAX_BLK_SYSFS] = {
#define BLK_FNS(n) { blk_show_dev_##n, blk_show_bsize_##n, \
                      blk_show_nblocks_##n, blk_show_removable_##n, blk_show_ro_##n }
    BLK_FNS( 0), BLK_FNS( 1), BLK_FNS( 2), BLK_FNS( 3),
    BLK_FNS( 4), BLK_FNS( 5), BLK_FNS( 6), BLK_FNS( 7),
    BLK_FNS( 8), BLK_FNS( 9), BLK_FNS(10), BLK_FNS(11),
    BLK_FNS(12), BLK_FNS(13), BLK_FNS(14), BLK_FNS(15),
#undef BLK_FNS
};

static int blk_class_register_one(struct blk_device *dev, void *data) {
    (void)data;

    if (blk_sysfs_count >= MAX_BLK_SYSFS) {
        printk("sysdir: /sys/class/block: slot limit reached, skipping %s\n",
               dev->name);
        return 0; /* non-fatal — keep iterating */
    }

    int i = blk_sysfs_count;
    blk_sysfs_devs[i] = dev;

    strncpy(blk_sysfs_names[i], dev->name, sizeof(blk_sysfs_names[i]) - 1);
    blk_sysfs_names[i][sizeof(blk_sysfs_names[i]) - 1] = '\0';

    blk_sysfs_attrs[i][0] = (sysfs_attr_t){ "dev",        0444, blk_fns[i].dev,        NULL };
    blk_sysfs_attrs[i][1] = (sysfs_attr_t){ "block_size", 0444, blk_fns[i].block_size,  NULL };
    blk_sysfs_attrs[i][2] = (sysfs_attr_t){ "num_blocks", 0444, blk_fns[i].num_blocks,  NULL };
    blk_sysfs_attrs[i][3] = (sysfs_attr_t){ "removable",  0444, blk_fns[i].removable,   NULL };
    blk_sysfs_attrs[i][4] = (sysfs_attr_t){ "ro",         0444, blk_fns[i].ro,          NULL };
    blk_sysfs_attrs[i][5] = (sysfs_attr_t)SYSFS_ATTR_SENTINEL;

    blk_sysfs_sysd[i].name       = blk_sysfs_names[i];
    blk_sysfs_sysd[i].subsys     = SYSDEV_SUBSYS_CLASS_BLOCK;
    blk_sysfs_sysd[i].attrs      = blk_sysfs_attrs[i];
    blk_sysfs_sysd[i].init       = NULL;
    blk_sysfs_sysd[i].exit       = NULL;
    blk_sysfs_sysd[i].priv       = dev;
    blk_sysfs_sysd[i].registered = false;

    int ret = sysdev_register(&blk_sysfs_sysd[i]);
    if (ret != 0)
        printk("sysdir: skipping block class entry %s: %d\n", dev->name, ret);

    blk_sysfs_count++;
    return 0;
}

int sysdev_register_class(void) {
    int ret = sysfs_mkdir(SYSDEV_SUBSYS_CLASS_BLOCK, 0555);
    if (ret != 0 && ret != -EEXIST) {
        printk("sysdir: failed to create %s: %d\n",
               SYSDEV_SUBSYS_CLASS_BLOCK, ret);
        return ret;
    }

    blk_for_each_device(blk_class_register_one, NULL);

    return 0;
}

int sysdir_init(void) {
    int ret;

    ret = sysdev_register_cpu();
    if (ret != 0) {
        printk("sysdir: cpu0 registration failed: %d\n", ret);
        return ret;
    }

    ret = sysdev_register_pci();
    if (ret != 0) {
        printk("sysdir: PCI registration failed: %d\n", ret);
        return ret;
    }

    ret = sysdev_register_fs();
    if (ret != 0) {
        printk("sysdir: fs registration failed: %d\n", ret);
        return ret;
    }

    ret = sysdev_register_class();
    if (ret != 0) {
        printk("sysdir: class registration failed: %d\n", ret);
        return ret;
    }

    printk("sysdir: initialized\n");
    return 0;
}