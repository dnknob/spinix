#ifndef PMM_H
#define PMM_H

#include <klibc/types.h>

#define PAGE_SIZE       4096
#define PAGE_SHIFT      12

#define PMM_ZONE_DMA      0   /* < 16MB for legacy ISA DMA */
#define PMM_ZONE_DMA32    1   /* < 4GB for 32-bit PCI devices */
#define PMM_ZONE_NORMAL   2   /* >= 4GB normal memory */
#define PMM_ZONE_COUNT    3

#define PMM_DMA_LIMIT     0x1000000     /* 16 MB */
#define PMM_DMA32_LIMIT   0x100000000ULL /* 4 GB */

#define PMM_ALLOC_ZERO    (1 << 0)  /* Zero the allocated page(s) */
#define PMM_ALLOC_DMA     (1 << 1)  /* Must be from DMA zone */
#define PMM_ALLOC_DMA32   (1 << 2)  /* Must be from DMA32 zone */

typedef struct pmm_watermarks {
    uint64_t min;    /* Critical - start reclaiming aggressively */
    uint64_t low;    /* Warning - start background reclaim */
    uint64_t high;   /* Comfortable - plenty of free memory */
} pmm_watermarks_t;

typedef struct pmm_zone_stats {
    uint64_t alloc_count;      /* Total allocations */
    uint64_t free_count;       /* Total frees */
    uint64_t alloc_failed;     /* Failed allocations */
} pmm_zone_stats_t;

void pmm_init(void);

phys_addr_t pmm_alloc_page(void);
phys_addr_t pmm_alloc_page_zone(int zone);
void        pmm_free_page(phys_addr_t phys_addr);

void pmm_print_stats(void);
void pmm_print_zones(void);

phys_addr_t pmm_alloc_page_flags(uint32_t flags);
phys_addr_t pmm_alloc_pages(size_t count, int zone);

void pmm_free_pages(phys_addr_t phys_addr, size_t count);

void pmm_get_watermarks(int zone, pmm_watermarks_t *wm);

bool pmm_is_low_memory(int zone);

void pmm_get_stats(int zone, pmm_zone_stats_t *stats);

void pmm_reserve_region(phys_addr_t base, uint64_t length);

uint64_t pmm_get_total_pages(void);
uint64_t pmm_get_free_pages(void);
uint64_t pmm_get_used_pages(void);

uint64_t pmm_get_zone_free_pages(int zone);
uint64_t pmm_get_zone_total_pages(int zone);

extern phys_addr_t hhdm_offset;
#define PHYS_TO_VIRT(addr) ((void *)((phys_addr_t)(addr) + hhdm_offset))
#define VIRT_TO_PHYS(addr) ((phys_addr_t)(addr) - hhdm_offset)

#define PAGE_ALIGN_UP(addr)   (((addr) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
#define PAGE_ALIGN_DOWN(addr) ((addr) & ~(PAGE_SIZE - 1))

#define IS_PAGE_ALIGNED(addr) (((addr) & (PAGE_SIZE - 1)) == 0)

#define PAGES_TO_BYTES(pages) ((pages) * PAGE_SIZE)
#define BYTES_TO_PAGES(bytes) (((bytes) + PAGE_SIZE - 1) / PAGE_SIZE)

#endif