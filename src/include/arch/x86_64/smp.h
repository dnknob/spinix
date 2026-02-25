#ifndef SMP_H
#define SMP_H

#include <arch/x86_64/gdt.h>

#include <klibc/types.h>

#include <stdbool.h>

#define SMP_MAX_CPUS        64          /* hard ceiling on logical CPUs     */
#define SMP_AP_STACK_SIZE   (16 * 1024) /* per-AP kernel stack (16 KiB)    */
#define SMP_IST_STACK_SIZE  4096        /* per-AP IST stack per entry (4 KiB) */

#define SMP_IPI_RESCHEDULE  0xF0   /* ask a remote CPU to reschedule        */
#define SMP_IPI_TLB_FLUSH   0xF1   /* shootdown: remote TLB flush           */
#define SMP_IPI_HALT        0xF2   /* ask a remote CPU to stop (debug/panic)*/
#define SMP_IPI_PANIC       0xF3   /* broadcast: kernel panic on all CPUs   */

typedef struct cpu_info {
    uint32_t    cpu_id;         /* logical index: 0 = BSP, 1..N = APs      */
    uint32_t    lapic_id;       /* hardware local APIC ID                   */
    bool        is_bsp;         /* true only for the Bootstrap Processor    */

    volatile bool online;       /* set to true once AP finishes init        */

    uint32_t    _pad0;          /* keep following fields 8-byte aligned     */

    struct gdt_entry gdt[7];    /* 7-entry GDT: null, kcode, kdata,         */
                                /*              udata, ucode, tss_lo, tss_hi */
    struct gdt_ptr   gdt_ptr;   /* limit + base used by lgdt                */
    struct tss_entry tss;       /* Task State Segment (holds RSP0 + ISTs)   */

    uint8_t *ist[8];            /* [0] unused; [1..4] DF/NMI/MC/DBG        */

    uint8_t *stack_bottom;      /* kmalloc'd base address                   */
    uint8_t *stack_top;         /* initial RSP (bottom + SMP_AP_STACK_SIZE) */

    uint64_t apic_timer_hz;     /* APIC bus frequency used for this CPU     */

    void    *current_thread;    /* points to running tcb_t, NULL if idle    */

} __attribute__((aligned(64))) cpu_info_t;

extern cpu_info_t       *g_cpus[SMP_MAX_CPUS];
extern volatile uint32_t g_cpu_count;    /* total CPUs enumerated (incl. BSP) */
extern volatile uint32_t g_cpus_online;  /* CPUs that have completed init     */

void smp_init(void);

cpu_info_t *smp_get_cpu(uint32_t cpu_id);
cpu_info_t *smp_get_current_cpu(void);

uint32_t smp_get_cpu_count(void);
uint32_t smp_get_bsp_lapic_id(void);

void smp_send_ipi(uint32_t target_lapic_id, uint8_t vector);
void smp_send_ipi_all_except_self(uint8_t vector);

struct limine_mp_info;

void smp_ap_init_c(struct limine_mp_info *info);

extern void smp_ap_entry_asm(struct limine_mp_info *info);
extern void smp_ap_idle(void);

extern uint32_t smp_read_lapic_id(void);

#endif /* SMP_H */