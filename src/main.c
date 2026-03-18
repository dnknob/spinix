#include <arch/x86_64/syscall.h>
#include <arch/x86_64/serial.h>
#include <arch/x86_64/cpuid.h>
#include <arch/x86_64/ioapic.h>
#include <arch/x86_64/apic.h>
#include <arch/x86_64/smp.h>
#include <arch/x86_64/gdt.h>
#include <arch/x86_64/idt.h>
#include <arch/x86_64/pic.h>
#include <arch/x86_64/tsc.h>
#include <arch/x86_64/rtc.h>

#include <drivers/storage/ahci.h>
#include <drivers/storage/ata.h>
#include <drivers/net/ethernet.h>
#include <drivers/net/rtl8139.h>
#include <drivers/input/kb.h>
#include <drivers/pci.h>

#include <core/scheduler.h>
#include <core/proc.h>

#include <blk/bcache.h>
#include <blk/blk.h>

#include <fs/elf_abi.h>
#include <fs/tmpfs.h>
#include <fs/sysfs.h>
#include <fs/sysdir.h>
#include <fs/runit.h>
#include <fs/vfs.h>

#include <video/flanterm.h>
#include <video/fb.h>
#include <video/printk.h>
#include <video/log.h>

#include <mm/paging.h>
#include <mm/heap.h>
#include <mm/pmm.h>
#include <mm/mmu.h>
#include <mm/vmm.h>

#include <klibc/string.h>
#include <stdint.h>
#include <stddef.h>
#include <limine.h>

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] =
    LIMINE_BASE_REVISION(4);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[] =
    LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[] =
    LIMINE_REQUESTS_END_MARKER;

struct flanterm_context *g_ft_ctx = NULL;

void fpu_enable(void);

extern const uint8_t _binary_bin_hello_start[];
extern const uint8_t _binary_bin_hello_end[];

void kmain(void) {
    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision))
        for (;;) __asm__("hlt");

    if (!framebuffer_request.response ||
        framebuffer_request.response->framebuffer_count < 1)
        for (;;) __asm__("hlt");

    struct limine_framebuffer *fb =
        framebuffer_request.response->framebuffers[0];

    void *framebuffer_ptr = fb->address;
    uint64_t width        = fb->width;
    uint64_t height       = fb->height;
    uint64_t pitch        = fb->pitch;
    uint8_t red_mask_size    = fb->red_mask_size;
    uint8_t red_mask_shift   = fb->red_mask_shift;
    uint8_t green_mask_size  = fb->green_mask_size;
    uint8_t green_mask_shift = fb->green_mask_shift;
    uint8_t blue_mask_size   = fb->blue_mask_size;
    uint8_t blue_mask_shift  = fb->blue_mask_shift;

    struct flanterm_context *ft_ctx = flanterm_fb_init(
        NULL, NULL,
        framebuffer_ptr, width, height, pitch,
        red_mask_size, red_mask_shift,
        green_mask_size, green_mask_shift,
        blue_mask_size, blue_mask_shift,
        NULL, NULL, NULL, NULL, NULL, NULL, NULL,
        NULL, 0, 0, 1, 0, 0, 0,
        FLANTERM_FB_ROTATE_0
    );

    g_ft_ctx = ft_ctx;

    elog_header("Booting spinix 0.1.0 (x86_64) ...");

    __asm__ volatile("cli");
    fpu_enable();

    ebegin("Starting GDT");
    gdt_init();
    eend(0, NULL);

    ebegin("Starting IDT");
    idt_init();
    eend(0, NULL);

    ebegin("Starting physical memory manager");
    pmm_init();
    eend(0, NULL);

    ebegin("Starting MMU");
    mmu_init();
    eend(0, NULL);

    ebegin("Starting virtual memory manager");
    vmm_init();
    eend(0, NULL);

    ebegin("Starting kernel heap");
    heap_init();
    eend(0, NULL);

    ebegin("Starting paging");
    paging_init();
    eend(0, NULL);

    serial_init(COM1);

    pic_init();
    apic_init();
    pic_disable();
    ioapic_init();

    smp_init();

    rtc_init();
    tsc_init();

    ebegin("Scanning PCI bus");
    pci_init();
    eend(0, NULL);

    kb_init();
    stdin_init();

    ebegin("Starting block device layer");
    blk_init();
    eend(0, NULL);

    ebegin("Starting buffer cache");
    bcache_init();
    eend(0, NULL);

    ebegin("Starting AHCI storage driver");
    ata_init();
    ahci_init();
    eend(0, NULL);

    scheduler_init();
    proc_init();
    syscall_init();

    uint64_t apic_freq     = apic_timer_get_frequency();
    uint32_t initial_count = (apic_freq / 16) / 100;
    apic_timer_init(IRQ0, LAPIC_TIMER_DIV_16, initial_count, true);

    ebegin("Starting virtual filesystem");
    vfs_init();
    tmpfs_init();
    vfs_mount(NULL, "/", "tmpfs", 0);
    eend(0, NULL);

    ebegin("Starting sysfs");
    sysfs_init();
    eend(0, NULL);

    ebegin("Starting sysdir");
    sysdir_init();
    eend(0, NULL);

    RUNIT_EMBED(test, "/bin/test");
    runit_install("/bin/test",
                  runit_test.start,
                  (size_t)(runit_test.end - runit_test.start));
    runit("/bin/test", "test", PRIORITY_NORMAL, NULL);

    rtl8139_rx_handler = NULL;
    rtl8139_init();
    eth_init();

    __asm__ volatile("sti");

    for (;;) {
        yield();
        kb_poll();
        __asm__ volatile("hlt");
    }
}
