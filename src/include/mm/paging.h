#ifndef PAGING_H
#define PAGING_H

#include <arch/x86_64/mmu.h>

#include <klibc/types.h>

#define PAGE_SIZE           4096
#define PAGE_SHIFT          12

#define PAGING_ENTRIES      512

#define PAGE_PRESENT        (1UL << 0)   /* Page is present in memory */
#define PAGE_WRITE          (1UL << 1)   /* Page is writable */
#define PAGE_USER           (1UL << 2)   /* Page is accessible from user mode */
#define PAGE_WRITETHROUGH   (1UL << 3)   /* Write-through caching */
#define PAGE_CACHEDISABLE   (1UL << 4)   /* Disable caching */
#define PAGE_ACCESSED       (1UL << 5)   /* Set by CPU when accessed */
#define PAGE_DIRTY          (1UL << 6)   /* Set by CPU when written to */
#define PAGE_HUGE           (1UL << 7)   /* 2MB or 1GB page */
#define PAGE_GLOBAL         (1UL << 8)   /* Not flushed from TLB on CR3 load */
#define PAGE_NX             (1UL << 63)  /* No execute */

#define PAGE_FLAGS_KERNEL   (PAGE_PRESENT | PAGE_WRITE)
#define PAGE_FLAGS_USER     (PAGE_PRESENT | PAGE_WRITE | PAGE_USER)

#define PAGE_ADDR_MASK      0x000FFFFFFFFFF000UL

#define PML4_INDEX(addr)    (((addr) >> 39) & 0x1FF)
#define PDPT_INDEX(addr)    (((addr) >> 30) & 0x1FF)
#define PD_INDEX(addr)      (((addr) >> 21) & 0x1FF)
#define PT_INDEX(addr)      (((addr) >> 12) & 0x1FF)

typedef struct paging_context {
    phys_addr_t   pml4_phys;       /* Physical address of PML4 */
    page_table_t *pml4_virt;       /* Virtual address of PML4 */
    void         *mmu_ctx;         /* Internal: pointer to mmu_context_t */
} paging_context_t;

void        paging_load_pml4(phys_addr_t pml4_phys);
phys_addr_t paging_get_pml4(void);
void        paging_invalidate_page(virt_addr_t virt_addr);
void        paging_flush_tlb(void);

void paging_init(void);

paging_context_t *paging_create_context(void);
paging_context_t *paging_get_kernel_context(void);
void              paging_switch_context(paging_context_t *ctx);
void              paging_destroy_context(paging_context_t *ctx);

int paging_identity_map(paging_context_t *ctx, phys_addr_t phys_start,
                        size_t size, uint64_t flags);
int paging_map_page(paging_context_t *ctx, virt_addr_t virt_addr,
                    phys_addr_t phys_addr, uint64_t flags);
int paging_unmap_page(paging_context_t *ctx, virt_addr_t virt_addr);
int paging_map_range(paging_context_t *ctx, virt_addr_t virt_start,
                     phys_addr_t phys_start, size_t size, uint64_t flags);

phys_addr_t paging_virt_to_phys(paging_context_t *ctx, virt_addr_t virt_addr);
int         paging_is_mapped(paging_context_t *ctx, virt_addr_t virt_addr);
void        paging_dump_walk(paging_context_t *ctx, virt_addr_t virt_addr);

int paging_unmap_range(paging_context_t *ctx, virt_addr_t virt_start, size_t size);

int paging_change_flags(paging_context_t *ctx, virt_addr_t virt_addr, uint64_t new_flags);
int paging_change_flags_range(paging_context_t *ctx, virt_addr_t virt_start,
                              size_t size, uint64_t new_flags);
int paging_make_readonly(paging_context_t *ctx, virt_addr_t virt_addr, size_t size);
int paging_make_writable(paging_context_t *ctx, virt_addr_t virt_addr, size_t size);

int               paging_mark_cow(paging_context_t *ctx, virt_addr_t virt_addr, size_t size);
int               paging_is_cow_page(paging_context_t *ctx, virt_addr_t virt_addr);
int               paging_break_cow(paging_context_t *ctx, virt_addr_t virt_addr);
paging_context_t *paging_clone_context(paging_context_t *src, bool use_cow);

typedef enum {
    PAGING_PAGE_4K = 0,
    PAGING_PAGE_2M = 1,
    PAGING_PAGE_1G = 2
} paging_page_size_t;

int paging_map_huge_page(paging_context_t *ctx, virt_addr_t virt_addr,
                         phys_addr_t phys_addr, paging_page_size_t size, uint64_t flags);
int paging_map_range_auto(paging_context_t *ctx, virt_addr_t virt_start,
                          phys_addr_t phys_start, size_t size, uint64_t flags);
paging_page_size_t paging_get_page_size(paging_context_t *ctx, virt_addr_t virt_addr);

bool paging_supports_2mb_pages(void);
bool paging_supports_1gb_pages(void);
bool paging_supports_global_pages(void);
bool paging_supports_nx(void);

void paging_flush_tlb_single(virt_addr_t virt_addr);
void paging_flush_tlb_range(virt_addr_t virt_start, size_t size);
void paging_flush_tlb_all(void);

typedef struct {
    uint64_t pages_mapped_4k;
    uint64_t pages_mapped_2m;
    uint64_t pages_mapped_1g;
    uint64_t pages_unmapped;
    uint64_t tables_allocated;
    uint64_t cow_breaks;
    uint64_t page_faults;
    uint64_t tlb_single_flushes;
    uint64_t tlb_full_flushes;
    uint64_t context_switches;
} paging_stats_t;

void paging_get_stats(paging_context_t *ctx, paging_stats_t *stats);
void paging_print_stats(paging_context_t *ctx);
void paging_print_global_stats(void);

typedef struct {
    virt_addr_t fault_addr;
    uint64_t    error_code;
    bool        present;
    bool        write;
    bool        user;
    bool        exec;
    bool        reserved;
} paging_fault_info_t;

void paging_parse_fault_error(uint64_t error_code, paging_fault_info_t *info);
int  paging_handle_page_fault(paging_context_t *ctx, virt_addr_t fault_addr,
                              uint64_t error_code);

typedef struct {
    virt_addr_t virt_addr;
    phys_addr_t phys_addr;
    uint64_t    flags;
} paging_mapping_t;

int paging_map_batch(paging_context_t *ctx, const paging_mapping_t *mappings,
                     size_t count);
int paging_unmap_batch(paging_context_t *ctx, const virt_addr_t *virt_addrs,
                       size_t count);

uint64_t paging_get_flags(paging_context_t *ctx, virt_addr_t virt_addr);
bool     paging_is_aligned_for_huge(virt_addr_t addr, paging_page_size_t size);

#endif