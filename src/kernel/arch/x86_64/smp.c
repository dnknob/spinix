#include <arch/x86_64/smp.h>
#include <arch/x86_64/apic.h>
#include <arch/x86_64/gdt.h>
#include <arch/x86_64/idt.h>
#include <arch/x86_64/io.h>

#include <mm/heap.h>

#include <video/printk.h>

#include <klibc/string.h>

#include <limine.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

__attribute__((used, section(".limine_requests")))
static volatile struct limine_mp_request smp_mp_request = {
    .id       = LIMINE_MP_REQUEST_ID,
    .revision = 0,
    .flags    = 0,
};

cpu_info_t       *g_cpus[SMP_MAX_CPUS];
volatile uint32_t g_cpu_count   = 0;
volatile uint32_t g_cpus_online = 0;

static void cpu_gdt_set_gate(struct gdt_entry *gdt, int n,
                              uint32_t base, uint32_t limit,
                              uint8_t access, uint8_t gran)
{
    gdt[n].base_low    = base & 0xFFFF;
    gdt[n].base_middle = (base >> 16) & 0xFF;
    gdt[n].base_high   = (base >> 24) & 0xFF;
    gdt[n].limit_low   = limit & 0xFFFF;
    gdt[n].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[n].access      = access;
}

static void cpu_gdt_set_tss(struct gdt_entry *gdt, int n,
                             uint64_t base, uint32_t limit)
{
    gdt[n].limit_low   = limit & 0xFFFF;
    gdt[n].base_low    = base  & 0xFFFF;
    gdt[n].base_middle = (base >> 16) & 0xFF;
    gdt[n].access      = GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_TSS_AVAILABLE;
    gdt[n].granularity = ((limit >> 16) & 0x0F) | GDT_GRANULARITY_BYTE;
    gdt[n].base_high   = (base >> 24) & 0xFF;

    gdt[n + 1].limit_low   = (base >> 32) & 0xFFFF;
    gdt[n + 1].base_low    = (base >> 48) & 0xFFFF;
    gdt[n + 1].base_middle = 0;
    gdt[n + 1].access      = 0;
    gdt[n + 1].granularity = 0;
    gdt[n + 1].base_high   = 0;
}

static void smp_build_cpu_gdt(cpu_info_t *cpu)
{
    struct gdt_entry *g = cpu->gdt;

    cpu_gdt_set_gate(g, 0, 0, 0, 0, 0);
    cpu_gdt_set_gate(g, 1, 0, 0xFFFFF,
        GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_ACCESS_CODE_DATA |
        GDT_ACCESS_EXECUTABLE | GDT_ACCESS_RW,
        GDT_GRANULARITY_4K | GDT_FLAG_64BIT);
    cpu_gdt_set_gate(g, 2, 0, 0xFFFFF,
        GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_ACCESS_CODE_DATA |
        GDT_ACCESS_RW,
        GDT_GRANULARITY_4K | GDT_FLAG_64BIT);
    cpu_gdt_set_gate(g, 3, 0, 0xFFFFF,
        GDT_ACCESS_PRESENT | GDT_ACCESS_RING3 | GDT_ACCESS_CODE_DATA |
        GDT_ACCESS_RW,
        GDT_GRANULARITY_4K | GDT_FLAG_64BIT);
    cpu_gdt_set_gate(g, 4, 0, 0xFFFFF,
        GDT_ACCESS_PRESENT | GDT_ACCESS_RING3 | GDT_ACCESS_CODE_DATA |
        GDT_ACCESS_EXECUTABLE | GDT_ACCESS_RW,
        GDT_GRANULARITY_4K | GDT_FLAG_64BIT);

    memset(&cpu->tss, 0, sizeof(struct tss_entry));
    cpu->tss.iomap_base = sizeof(struct tss_entry);
    cpu->tss.rsp0       = (uint64_t)cpu->stack_top;

    if (cpu->ist[IST_DOUBLE_FAULT])
        cpu->tss.ist1 = (uint64_t)cpu->ist[IST_DOUBLE_FAULT]  + SMP_IST_STACK_SIZE;
    if (cpu->ist[IST_NMI])
        cpu->tss.ist2 = (uint64_t)cpu->ist[IST_NMI]           + SMP_IST_STACK_SIZE;
    if (cpu->ist[IST_MACHINE_CHECK])
        cpu->tss.ist3 = (uint64_t)cpu->ist[IST_MACHINE_CHECK] + SMP_IST_STACK_SIZE;
    if (cpu->ist[IST_DEBUG])
        cpu->tss.ist4 = (uint64_t)cpu->ist[IST_DEBUG]         + SMP_IST_STACK_SIZE;

    cpu_gdt_set_tss(g, 5, (uint64_t)&cpu->tss, sizeof(struct tss_entry) - 1);

    cpu->gdt_ptr.limit = (uint16_t)(sizeof(struct gdt_entry) * 7 - 1);
    cpu->gdt_ptr.base  = (uint64_t)cpu->gdt;
}

static cpu_info_t *smp_alloc_cpu_info(uint32_t cpu_id, uint32_t lapic_id,
                                       bool is_bsp)
{
    cpu_info_t *cpu = (cpu_info_t *)kmalloc_flags(sizeof(cpu_info_t), HEAP_ZERO);
    if (!cpu) return NULL;

    cpu->cpu_id   = cpu_id;
    cpu->lapic_id = lapic_id;
    cpu->is_bsp   = is_bsp;
    cpu->online   = is_bsp;

    cpu->stack_bottom = (uint8_t *)kmalloc(SMP_AP_STACK_SIZE);
    if (!cpu->stack_bottom) return NULL;
    cpu->stack_top = cpu->stack_bottom + SMP_AP_STACK_SIZE;

    for (int i = 1; i <= 4; i++) {
        cpu->ist[i] = (uint8_t *)kmalloc(SMP_IST_STACK_SIZE);
        if (!cpu->ist[i]) return NULL;
    }

    smp_build_cpu_gdt(cpu);
    return cpu;
}

static void smp_ap_enable_lapic(void)
{
    if (apic_is_x2apic_enabled()) {
        uint64_t msr = rdmsr(IA32_APIC_BASE_MSR);
        msr |= IA32_APIC_BASE_ENABLE | IA32_APIC_BASE_X2APIC;
        wrmsr(IA32_APIC_BASE_MSR, msr);
    }

    uint32_t svr = apic_read(LAPIC_SVR);
    svr |= LAPIC_SVR_ENABLE | APIC_SPURIOUS_VECTOR;
    apic_write(LAPIC_SVR, svr);

    apic_lvt_set_mask(LAPIC_TIMER,   true);
    apic_lvt_set_mask(LAPIC_THERMAL, true);
    apic_lvt_set_mask(LAPIC_PERF,    true);
    apic_lvt_set_mask(LAPIC_LINT0,   true);
    apic_lvt_set_mask(LAPIC_LINT1,   true);
    apic_lvt_set_mask(LAPIC_ERROR,   true);

    apic_clear_errors();
    apic_eoi();
}

void smp_ap_init_c(struct limine_mp_info *info)
{
    cpu_info_t *cpu   = NULL;
    uint32_t    count = __atomic_load_n(&g_cpu_count, __ATOMIC_ACQUIRE);

    for (uint32_t i = 0; i < count; i++) {
        if (g_cpus[i] && g_cpus[i]->lapic_id == info->lapic_id) {
            cpu = g_cpus[i];
            break;
        }
    }

    if (!cpu)
        for (;;) __asm__ volatile("cli; hlt");

    gdt_flush((uint64_t)&cpu->gdt_ptr);
    tss_flush();

    /* idt_get_ptr() must be used here instead of sidt: on an AP, sidt returns
     * Limine's stub IDT, not the kernel's, causing a triple fault on the first
     * exception. */
    idt_flush((uint64_t)idt_get_ptr());

    smp_ap_enable_lapic();

    uint64_t apic_hz   = apic_timer_get_frequency();
    cpu->apic_timer_hz = apic_hz;

    if (apic_hz > 0) {
        uint32_t initial_count = (uint32_t)((apic_hz / 16) / 100);
        apic_timer_init(IRQ0, LAPIC_TIMER_DIV_16, initial_count, true);
    }

    __atomic_store_n(&cpu->online, true, __ATOMIC_RELEASE);
    __atomic_fetch_add(&g_cpus_online, 1, __ATOMIC_RELEASE);

    __asm__ volatile("sti");
    smp_ap_idle();

    for (;;) __asm__ volatile("hlt");
}

void smp_init(void)
{
    struct limine_mp_response *resp = smp_mp_request.response;

    if (!resp || resp->cpu_count == 0) {
        printk("smp: no Limine MP response — uniprocessor\n");
        cpu_info_t *bsp = smp_alloc_cpu_info(0, apic_get_id(), true);
        if (bsp) {
            g_cpus[0]     = bsp;
            g_cpu_count   = 1;
            g_cpus_online = 1;
        }
        return;
    }

    uint64_t total = resp->cpu_count;
    if (total > SMP_MAX_CPUS)
        total = SMP_MAX_CPUS;

    printk("smp: %llu CPU(s) detected, BSP LAPIC ID = %u\n",
           total, resp->bsp_lapic_id);

    uint32_t next_id = 0;

    for (uint64_t i = 0; i < total; i++) {
        struct limine_mp_info *mi = resp->cpus[i];
        if (mi->lapic_id != resp->bsp_lapic_id) continue;

        cpu_info_t *bsp = smp_alloc_cpu_info(next_id, mi->lapic_id, true);
        if (!bsp) {
            printk("smp: FATAL: failed to allocate BSP cpu_info\n");
            return;
        }
        g_cpus[next_id] = bsp;
        next_id++;
        g_cpus_online = 1;
        break;
    }

    __atomic_store_n(&g_cpu_count, next_id, __ATOMIC_RELEASE);

    for (uint64_t i = 0; i < total; i++) {
        struct limine_mp_info *mi = resp->cpus[i];
        if (mi->lapic_id == resp->bsp_lapic_id) continue;
        if (next_id >= SMP_MAX_CPUS) break;

        cpu_info_t *cpu = smp_alloc_cpu_info(next_id, mi->lapic_id, false);
        if (!cpu) {
            printk("smp: failed to alloc cpu_info for LAPIC %u\n", mi->lapic_id);
            continue;
        }

        g_cpus[next_id] = cpu;
        next_id++;
        __atomic_store_n(&g_cpu_count, next_id, __ATOMIC_RELEASE);

        printk("smp: waking CPU %u (LAPIC %u)\n", cpu->cpu_id, cpu->lapic_id);

        mi->extra_argument = (uint64_t)cpu->stack_top;
        __asm__ volatile("" ::: "memory");
        mi->goto_address = smp_ap_entry_asm;

        const uint64_t TIMEOUT = 10000000ULL;
        uint64_t n = 0;

        while (!__atomic_load_n(&cpu->online, __ATOMIC_ACQUIRE) && n < TIMEOUT) {
            __asm__ volatile("pause" ::: "memory");
            n++;
        }

        if (!cpu->online)
            printk("smp: WARNING: CPU %u (LAPIC %u) timed out\n",
                   cpu->cpu_id, cpu->lapic_id);
        else
            printk("smp: CPU %u (LAPIC %u) online\n",
                   cpu->cpu_id, cpu->lapic_id);
    }

    __asm__ volatile("" ::: "memory");
    printk_ts("smp: %u/%u CPU(s) online\n", g_cpus_online, g_cpu_count);
}

cpu_info_t *smp_get_cpu(uint32_t cpu_id)
{
    if (cpu_id >= g_cpu_count) return NULL;
    return g_cpus[cpu_id];
}

cpu_info_t *smp_get_current_cpu(void)
{
    uint32_t lapic_id = apic_get_id();
    for (uint32_t i = 0; i < g_cpu_count; i++)
        if (g_cpus[i] && g_cpus[i]->lapic_id == lapic_id)
            return g_cpus[i];
    return NULL;
}

uint32_t smp_get_cpu_count(void)    { return g_cpu_count; }
uint32_t smp_get_bsp_lapic_id(void) { return g_cpus[0] ? g_cpus[0]->lapic_id : 0; }

void smp_send_ipi(uint32_t target_lapic_id, uint8_t vector)
{
    apic_send_ipi(target_lapic_id, vector,
                  LAPIC_ICR_DELIVERY_FIXED,
                  LAPIC_ICR_DEST_PHYSICAL, 0);
}

void smp_send_ipi_all_except_self(uint8_t vector)
{
    apic_send_ipi(0, vector,
                  LAPIC_ICR_DELIVERY_FIXED,
                  LAPIC_ICR_DEST_PHYSICAL,
                  LAPIC_ICR_DEST_ALL_OTHER);
}