#include <arch/x86_64/gdt.h>

#include <video/printk.h>

#include <stddef.h>
#include <stdint.h>

static struct gdt_entry gdt_entries[7];
static struct gdt_ptr   gdt_pointer;
static struct tss_entry tss;

static uint8_t ist_stack_df   [IST_STACK_SIZE] __attribute__((aligned(16)));
static uint8_t ist_stack_nmi  [IST_STACK_SIZE] __attribute__((aligned(16)));
static uint8_t ist_stack_mc   [IST_STACK_SIZE] __attribute__((aligned(16)));
static uint8_t ist_stack_debug[IST_STACK_SIZE] __attribute__((aligned(16)));

static void gdt_set_gate(int num, uint32_t base, uint32_t limit,
                         uint8_t access, uint8_t gran)
{
    gdt_entries[num].base_low    = (base & 0xFFFF);
    gdt_entries[num].base_middle = (base >> 16) & 0xFF;
    gdt_entries[num].base_high   = (base >> 24) & 0xFF;
    gdt_entries[num].limit_low   = (limit & 0xFFFF);
    gdt_entries[num].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt_entries[num].access      = access;
}

static void gdt_set_tss(int num, uint64_t base, uint32_t limit,
                        uint8_t access, uint8_t gran)
{
    gdt_entries[num].limit_low   = limit & 0xFFFF;
    gdt_entries[num].base_low    = base  & 0xFFFF;
    gdt_entries[num].base_middle = (base >> 16) & 0xFF;
    gdt_entries[num].access      = access;
    gdt_entries[num].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt_entries[num].base_high   = (base >> 24) & 0xFF;

    gdt_entries[num + 1].limit_low   = (base >> 32) & 0xFFFF;
    gdt_entries[num + 1].base_low    = (base >> 48) & 0xFFFF;
    gdt_entries[num + 1].base_middle = 0;
    gdt_entries[num + 1].access      = 0;
    gdt_entries[num + 1].granularity = 0;
    gdt_entries[num + 1].base_high   = 0;
}

void gdt_init(void)
{
    gdt_pointer.limit = (sizeof(struct gdt_entry) * 7) - 1;
    gdt_pointer.base  = (uint64_t)&gdt_entries;

    gdt_set_gate(0, 0, 0, 0, 0);

    gdt_set_gate(1, 0, 0xFFFFF,
        GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_ACCESS_CODE_DATA |
        GDT_ACCESS_EXECUTABLE | GDT_ACCESS_RW,
        GDT_GRANULARITY_4K | GDT_FLAG_64BIT);

    gdt_set_gate(2, 0, 0xFFFFF,
        GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_ACCESS_CODE_DATA |
        GDT_ACCESS_RW,
        GDT_GRANULARITY_4K | GDT_FLAG_64BIT);

    gdt_set_gate(3, 0, 0xFFFFF,
        GDT_ACCESS_PRESENT | GDT_ACCESS_RING3 | GDT_ACCESS_CODE_DATA |
        GDT_ACCESS_RW,
        GDT_GRANULARITY_4K | GDT_FLAG_64BIT);

    gdt_set_gate(4, 0, 0xFFFFF,
        GDT_ACCESS_PRESENT | GDT_ACCESS_RING3 | GDT_ACCESS_CODE_DATA |
        GDT_ACCESS_EXECUTABLE | GDT_ACCESS_RW,
        GDT_GRANULARITY_4K | GDT_FLAG_64BIT);

    uint8_t *tss_ptr = (uint8_t *)&tss;
    for (size_t i = 0; i < sizeof(struct tss_entry); i++)
        tss_ptr[i] = 0;

    tss.iomap_base = sizeof(struct tss_entry);  /* no I/O bitmap */

    tss.ist1 = (uint64_t)&ist_stack_df   [IST_STACK_SIZE]; /* Double Fault  */
    tss.ist2 = (uint64_t)&ist_stack_nmi  [IST_STACK_SIZE]; /* NMI           */
    tss.ist3 = (uint64_t)&ist_stack_mc   [IST_STACK_SIZE]; /* Machine Check */
    tss.ist4 = (uint64_t)&ist_stack_debug[IST_STACK_SIZE]; /* Debug         */

    gdt_set_tss(5, (uint64_t)&tss, sizeof(struct tss_entry) - 1,
                GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_TSS_AVAILABLE,
                GDT_GRANULARITY_BYTE);

    gdt_flush((uint64_t)&gdt_pointer);
    tss_flush();
}

void gdt_set_kernel_stack(uint64_t stack)
{
    tss.rsp0 = stack;
}

void gdt_set_ist(uint8_t ist_num, uint64_t stack)
{
    switch (ist_num) {
        case 1: tss.ist1 = stack; break;
        case 2: tss.ist2 = stack; break;
        case 3: tss.ist3 = stack; break;
        case 4: tss.ist4 = stack; break;
        case 5: tss.ist5 = stack; break;
        case 6: tss.ist6 = stack; break;
        case 7: tss.ist7 = stack; break;
    }
}