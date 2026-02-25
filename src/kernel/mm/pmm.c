#include <core/spinlock.h>

#include <video/printk.h>

#include <mm/pmm.h>

#include <klibc/string.h>
#include <limine.h>

extern volatile struct limine_hhdm_request hhdm_request;
extern volatile struct limine_memmap_request memmap_request;

uint64_t hhdm_offset = 0;

typedef struct page_frame {
    struct page_frame *next;
} page_frame_t;

typedef struct pmm_zone {
    const char *name;
    uint64_t start_addr;        /* Physical start address of zone */
    uint64_t end_addr;          /* Physical end address of zone */
    uint64_t total_pages;       /* Total pages in this zone */
    uint64_t free_pages;        /* Currently free pages */
    page_frame_t *free_stack;   /* Head of free page stack (O(1) access) */
    
    uint64_t watermark_min;
    uint64_t watermark_low;
    uint64_t watermark_high;
    
    pmm_zone_stats_t stats;
} pmm_zone_t;

static pmm_zone_t zones[PMM_ZONE_COUNT];

static uint64_t total_memory_bytes = 0;
static uint64_t usable_memory_bytes = 0;

static spinlock_irq_t pmm_locks[PMM_ZONE_COUNT] = {
    SPINLOCK_IRQ_INIT,
    SPINLOCK_IRQ_INIT,
    SPINLOCK_IRQ_INIT
};

static int pmm_addr_to_zone(uint64_t addr) {
    if (addr < PMM_DMA_LIMIT) {
        return PMM_ZONE_DMA;
    } else if (addr < PMM_DMA32_LIMIT) {
        return PMM_ZONE_DMA32;
    }
    return PMM_ZONE_NORMAL;
}

static void pmm_calculate_watermarks(pmm_zone_t *zone) {
    /* min = total_pages / 128, clamped between 128 and 1024 pages */
    zone->watermark_min = zone->total_pages / 128;
    if (zone->watermark_min < 128) zone->watermark_min = 128;
    if (zone->watermark_min > 1024) zone->watermark_min = 1024;
    
    zone->watermark_low = zone->watermark_min * 2;
    
    zone->watermark_high = zone->watermark_min * 3;
}

static void pmm_add_page_to_zone(pmm_zone_t *zone, uint64_t phys_addr) {
    page_frame_t *frame = (page_frame_t *)PHYS_TO_VIRT(phys_addr);
    frame->next = zone->free_stack;
    zone->free_stack = frame;
    zone->total_pages++;
    zone->free_pages++;
}

static void pmm_add_region(uint64_t base, uint64_t length) {
    /* Align base up to page boundary */
    uint64_t aligned_base = PAGE_ALIGN_UP(base);
    
    uint64_t lost = aligned_base - base;
    if (lost >= length) {
        return;  /* Region too small after alignment */
    }
    
    length -= lost;
    length = PAGE_ALIGN_DOWN(length);
    
    if (length == 0) {
        return;
    }
    
    uint64_t num_pages = length / PAGE_SIZE;
    
    for (uint64_t i = 0; i < num_pages; i++) {
        uint64_t phys_addr = aligned_base + (i * PAGE_SIZE);
        int zone_idx = pmm_addr_to_zone(phys_addr);
        pmm_zone_t *zone = &zones[zone_idx];
        
        pmm_add_page_to_zone(zone, phys_addr);
    }
}

void pmm_init(void) {
    if (hhdm_request.response == NULL) {
        printk("pmm: panic: no HHDM response from bootloader!\n");
        for(;;);
    }
    hhdm_offset = hhdm_request.response->offset;
    
    zones[PMM_ZONE_DMA].name = "DMA";
    zones[PMM_ZONE_DMA].start_addr = 0;
    zones[PMM_ZONE_DMA].end_addr = PMM_DMA_LIMIT;
    zones[PMM_ZONE_DMA].total_pages = 0;
    zones[PMM_ZONE_DMA].free_pages = 0;
    zones[PMM_ZONE_DMA].free_stack = NULL;
    
    zones[PMM_ZONE_DMA32].name = "DMA32";
    zones[PMM_ZONE_DMA32].start_addr = PMM_DMA_LIMIT;
    zones[PMM_ZONE_DMA32].end_addr = PMM_DMA32_LIMIT;
    zones[PMM_ZONE_DMA32].total_pages = 0;
    zones[PMM_ZONE_DMA32].free_pages = 0;
    zones[PMM_ZONE_DMA32].free_stack = NULL;
    
    zones[PMM_ZONE_NORMAL].name = "normal";
    zones[PMM_ZONE_NORMAL].start_addr = PMM_DMA32_LIMIT;
    zones[PMM_ZONE_NORMAL].end_addr = UINT64_MAX;
    zones[PMM_ZONE_NORMAL].total_pages = 0;
    zones[PMM_ZONE_NORMAL].free_pages = 0;
    zones[PMM_ZONE_NORMAL].free_stack = NULL;
    
    if (memmap_request.response == NULL) {
        printk("pmm: panic: no memory map from bootloader!\n");
        for(;;);
    }
    
    struct limine_memmap_response *memmap = memmap_request.response;
    
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        
        total_memory_bytes += entry->length;
        
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            usable_memory_bytes += entry->length;
            pmm_add_region(entry->base, entry->length);
        }
    }
    
    for (int i = 0; i < PMM_ZONE_COUNT; i++) {
        pmm_calculate_watermarks(&zones[i]);
    }
    
   printk_ts("pmm: initialized\n");
}

uint64_t pmm_alloc_page(void) {
    /* Try NORMAL first (preferred), then DMA32, then DMA */
    uint64_t addr = pmm_alloc_page_zone(PMM_ZONE_NORMAL);
    if (addr != 0) {
        return addr;
    }
    
    addr = pmm_alloc_page_zone(PMM_ZONE_DMA32);
    if (addr != 0) {
        return addr;
    }
    
    addr = pmm_alloc_page_zone(PMM_ZONE_DMA);
    if (addr != 0) {
        return addr;
    }
    
    printk("pmm: warning: out of memory!\n");
    return 0;
}

uint64_t pmm_alloc_page_zone(int zone_idx) {
    if (zone_idx < 0 || zone_idx >= PMM_ZONE_COUNT) {
        return 0;
    }
    
    spinlock_irq_acquire(&pmm_locks[zone_idx]);
    
    pmm_zone_t *zone = &zones[zone_idx];
    
    if (zone->free_stack == NULL) {
        zone->stats.alloc_failed++;
        spinlock_irq_release(&pmm_locks[zone_idx]);
        return 0;
    }
    
    page_frame_t *frame = zone->free_stack;
    zone->free_stack = frame->next;
    zone->free_pages--;
    zone->stats.alloc_count++;
    
    uint64_t phys_addr = VIRT_TO_PHYS((uint64_t)frame);
    
    spinlock_irq_release(&pmm_locks[zone_idx]);
    
    memset((void *)frame, 0, PAGE_SIZE);
    
    return phys_addr;
}

void pmm_free_page(uint64_t phys_addr) {
    if (phys_addr == 0) {
        return;
    }
    
    if (!IS_PAGE_ALIGNED(phys_addr)) {
        printk("pmm: warning: Attempted to free unaligned address 0x%016lx\n", phys_addr);
        return;
    }
    
    int zone_idx = pmm_addr_to_zone(phys_addr);
    
    spinlock_irq_acquire(&pmm_locks[zone_idx]);
    
    pmm_zone_t *zone = &zones[zone_idx];
    
    page_frame_t *frame = (page_frame_t *)PHYS_TO_VIRT(phys_addr);
    frame->next = zone->free_stack;
    zone->free_stack = frame;
    zone->free_pages++;
    zone->stats.free_count++;
    
    spinlock_irq_release(&pmm_locks[zone_idx]);
}

void pmm_print_stats(void) {
    uint64_t total_free_pages = 0;
    uint64_t total_used_pages = 0;
    
    for (int i = 0; i < PMM_ZONE_COUNT; i++) {
        total_free_pages += zones[i].free_pages;
        total_used_pages += zones[i].total_pages - zones[i].free_pages;
    }
    
    uint64_t free_memory = total_free_pages * PAGE_SIZE;
    uint64_t used_memory = total_used_pages * PAGE_SIZE;
    
    printk("pmm statistics:\n");
    printk("total memory:    %lu MB\n", total_memory_bytes / (1024 * 1024));
    printk("usable memory:   %lu MB\n", usable_memory_bytes / (1024 * 1024));
    printk("free memory:     %lu MB (%lu pages)\n",
           free_memory / (1024 * 1024), total_free_pages);
    printk("used memory:     %lu MB (%lu pages)\n",
           used_memory / (1024 * 1024), total_used_pages);
}

void pmm_print_zones(void) {
    printk("pmm zones:\n");
    
    for (int i = 0; i < PMM_ZONE_COUNT; i++) {
        pmm_zone_t *zone = &zones[i];
        uint64_t free_mb = (zone->free_pages * PAGE_SIZE) / (1024 * 1024);
        uint64_t total_mb = (zone->total_pages * PAGE_SIZE) / (1024 * 1024);
        uint64_t used_pages = zone->total_pages - zone->free_pages;
        
        printk("Zone %d (%s):\n", i, zone->name);
        printk("  range:       0x%016lx - 0x%016lx\n",
               zone->start_addr, zone->end_addr);
        printk("  total pages: %lu (%lu MB)\n", zone->total_pages, total_mb);
        printk("  free pages:  %lu (%lu MB)\n", zone->free_pages, free_mb);
        printk("  used pages:  %lu\n", used_pages);
        printk("  watermarks:  min=%lu low=%lu high=%lu\n",
               zone->watermark_min, zone->watermark_low, zone->watermark_high);
        printk("  stats:       allocs=%lu frees=%lu failed=%lu\n",
               zone->stats.alloc_count, zone->stats.free_count, 
               zone->stats.alloc_failed);
    }
    
}

uint64_t pmm_alloc_page_flags(uint32_t flags) {
    uint64_t addr = 0;
    
    if (flags & PMM_ALLOC_DMA) {
        addr = pmm_alloc_page_zone(PMM_ZONE_DMA);
    } else if (flags & PMM_ALLOC_DMA32) {
        /* Try DMA32 first, fall back to DMA if needed */
        addr = pmm_alloc_page_zone(PMM_ZONE_DMA32);
        if (addr == 0) {
            addr = pmm_alloc_page_zone(PMM_ZONE_DMA);
        }
    } else {
        addr = pmm_alloc_page();
    }
    
    if (addr == 0) {
        return 0;
    }
    
    if (flags & PMM_ALLOC_ZERO) {
        memset(PHYS_TO_VIRT(addr), 0, PAGE_SIZE);
    }
    
    return addr;
}

uint64_t pmm_alloc_pages(size_t count, int zone_idx) {
    if (count == 0) {
        return 0;
    }
    
    if (count == 1) {
        return pmm_alloc_page_zone(zone_idx);
    }
    
    if (zone_idx < 0 || zone_idx >= PMM_ZONE_COUNT) {
        return 0;
    }
    
    spinlock_irq_acquire(&pmm_locks[zone_idx]);
    
    pmm_zone_t *zone = &zones[zone_idx];
    
    if (zone->free_pages < count) {
        zone->stats.alloc_failed++;
        spinlock_irq_release(&pmm_locks[zone_idx]);
        return 0;
    }
    
    page_frame_t *prev = NULL;
    page_frame_t *current = zone->free_stack;
    
    while (current != NULL) {
        uint64_t start_addr = VIRT_TO_PHYS((uint64_t)current);
        bool is_contiguous = true;
        
        page_frame_t *check = current;
        for (size_t i = 0; i < count && check != NULL; i++) {
            uint64_t expected_addr = start_addr + (i * PAGE_SIZE);
            uint64_t actual_addr = VIRT_TO_PHYS((uint64_t)check);
            
            if (actual_addr != expected_addr) {
                is_contiguous = false;
                break;
            }
            
            check = check->next;
        }
        
        if (is_contiguous) {
            /* found contiguous pages! Remove them from free list */
            page_frame_t *to_remove = current;
            for (size_t i = 0; i < count; i++) {
                page_frame_t *next = to_remove->next;
                
                if (prev == NULL) {
                    zone->free_stack = next;
                } else {
                    prev->next = next;
                }
                
                zone->free_pages--;
                to_remove = next;
            }
            
            zone->stats.alloc_count += count;
            
            spinlock_irq_release(&pmm_locks[zone_idx]);
            
            for (size_t i = 0; i < count; i++) {
                memset(PHYS_TO_VIRT(start_addr + (i * PAGE_SIZE)), 0, PAGE_SIZE);
            }
            
            return start_addr;
        }
        
        prev = current;
        current = current->next;
    }
    
    zone->stats.alloc_failed++;
    spinlock_irq_release(&pmm_locks[zone_idx]);
    
    printk("pmm: warning: could not find %lu contiguous pages in zone %s\n",
           count, zone->name);
    return 0;
}

void pmm_free_pages(uint64_t phys_addr, size_t count) {
    for (size_t i = 0; i < count; i++) {
        pmm_free_page(phys_addr + (i * PAGE_SIZE));
    }
}

void pmm_get_watermarks(int zone, pmm_watermarks_t *wm) {
    if (zone < 0 || zone >= PMM_ZONE_COUNT || wm == NULL) {
        return;
    }
    
    wm->min = zones[zone].watermark_min;
    wm->low = zones[zone].watermark_low;
    wm->high = zones[zone].watermark_high;
}

bool pmm_is_low_memory(int zone) {
    if (zone < 0 || zone >= PMM_ZONE_COUNT) {
        return true;  /* Treat invalid zone as low memory */
    }
    
    return zones[zone].free_pages < zones[zone].watermark_low;
}

void pmm_get_stats(int zone, pmm_zone_stats_t *stats) {
    if (zone < 0 || zone >= PMM_ZONE_COUNT || stats == NULL) {
        return;
    }
    
    spinlock_irq_acquire(&pmm_locks[zone]);
    *stats = zones[zone].stats;
    spinlock_irq_release(&pmm_locks[zone]);
}

void pmm_reserve_region(uint64_t base, uint64_t length) {
    /* Align base down and length up to page boundaries */
    uint64_t aligned_base = PAGE_ALIGN_DOWN(base);
    uint64_t aligned_end = PAGE_ALIGN_UP(base + length);
    uint64_t aligned_length = aligned_end - aligned_base;
    
    uint64_t num_pages = aligned_length / PAGE_SIZE;
    
    printk("pmm: reserving region 0x%016lx - 0x%016lx (%lu pages)\n",
           aligned_base, aligned_end, num_pages);
    
}

uint64_t pmm_get_total_pages(void) {
    uint64_t total = 0;
    for (int i = 0; i < PMM_ZONE_COUNT; i++) {
        total += zones[i].total_pages;
    }
    return total;
}

uint64_t pmm_get_free_pages(void) {
    uint64_t total = 0;
    for (int i = 0; i < PMM_ZONE_COUNT; i++) {
        total += zones[i].free_pages;
    }
    return total;
}

uint64_t pmm_get_used_pages(void) {
    return pmm_get_total_pages() - pmm_get_free_pages();
}

uint64_t pmm_get_zone_free_pages(int zone) {
    if (zone < 0 || zone >= PMM_ZONE_COUNT) {
        return 0;
    }
    return zones[zone].free_pages;
}

uint64_t pmm_get_zone_total_pages(int zone) {
    if (zone < 0 || zone >= PMM_ZONE_COUNT) {
        return 0;
    }
    return zones[zone].total_pages;
}
