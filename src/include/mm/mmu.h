#ifndef MMU_H
#define MMU_H

#include <klibc/types.h>

#define MMU_MAP_PRESENT     (1 << 0)
#define MMU_MAP_WRITE       (1 << 1)
#define MMU_MAP_USER        (1 << 2)
#define MMU_MAP_EXEC        (1 << 3)  /* Note: Needs NX bit handling */
#define MMU_MAP_NOCACHE     (1 << 4)

#define MMU_MAP_GLOBAL      (1 << 5)  /* Global page (not flushed on CR3 switch) */
#define MMU_MAP_PAT         (1 << 6)  /* Page Attribute Table */
#define MMU_MAP_HUGE        (1 << 7)  /* Use huge pages (2MB/1GB) when possible */
#define MMU_MAP_COW         (1 << 8)  /* Copy-on-write page */
#define MMU_MAP_SHARED      (1 << 9)  /* Shared mapping */
#define MMU_MAP_LOCKED      (1 << 10) /* Page should not be swapped */
#define MMU_MAP_WRITETHROUGH (1 << 11) /* Write-through caching */

typedef enum {
    MMU_PAGE_4K = 0,    /* 4KB standard page */
    MMU_PAGE_2M = 1,    /* 2MB huge page */
    MMU_PAGE_1G = 2     /* 1GB gigantic page */
} mmu_page_size_t;

#define MMU_PAGE_SIZE_4K    (4096UL)
#define MMU_PAGE_SIZE_2M    (2097152UL)     /* 2MB = 2^21 */
#define MMU_PAGE_SIZE_1G    (1073741824UL)  /* 1GB = 2^30 */

typedef struct mmu_context mmu_context_t;

typedef struct {
    uint64_t single_flushes;    /* invlpg count */
    uint64_t full_flushes;      /* full TLB flush count */
    uint64_t context_switches;  /* CR3 changes */
} mmu_tlb_stats_t;

typedef struct {
    uint64_t pages_mapped_4k;   /* 4KB pages mapped */
    uint64_t pages_mapped_2m;   /* 2MB pages mapped */
    uint64_t pages_mapped_1g;   /* 1GB pages mapped */
    uint64_t pages_unmapped;    /* Total pages unmapped */
    uint64_t tables_allocated;  /* Page tables allocated */
    uint64_t cow_breaks;        /* COW page breaks */
    uint64_t page_faults;       /* Page faults handled */
    mmu_tlb_stats_t tlb_stats;  /* TLB statistics */
} mmu_stats_t;

typedef struct {
    virt_addr_t fault_addr;     /* Faulting address (CR2) */
    uint64_t    error_code;     /* Page fault error code */
    bool        present;        /* Page was present */
    bool        write;          /* Was a write access */
    bool        user;           /* User mode access */
    bool        exec;           /* Instruction fetch */
    bool        reserved;       /* Reserved bit violation */
} mmu_fault_info_t;

void mmu_init(void);

mmu_context_t *mmu_create_context(void);
mmu_context_t *mmu_get_kernel_context(void);

void mmu_destroy_context(mmu_context_t *ctx);
void mmu_switch_context(mmu_context_t *ctx);

int mmu_map_page(mmu_context_t *ctx, virt_addr_t virt_addr, phys_addr_t phys_addr,
                 uint64_t flags);
int mmu_unmap_page(mmu_context_t *ctx, virt_addr_t virt_addr);
int mmu_map_range(mmu_context_t *ctx, virt_addr_t virt_start, phys_addr_t phys_start,
                  size_t size, uint64_t flags);
int mmu_unmap_range(mmu_context_t *ctx, virt_addr_t virt_start, size_t size);

phys_addr_t mmu_virt_to_phys(mmu_context_t *ctx, virt_addr_t virt_addr);
int         mmu_is_mapped(mmu_context_t *ctx, virt_addr_t virt_addr);

uint64_t mmu_get_flags(mmu_context_t *ctx, virt_addr_t virt_addr);
void     mmu_dump_tables(mmu_context_t *ctx, virt_addr_t virt_addr);
void     mmu_print_stats(void);

int mmu_map_huge_page(mmu_context_t *ctx, virt_addr_t virt_addr, phys_addr_t phys_addr,
                      mmu_page_size_t size, uint64_t flags);
int mmu_unmap_huge_page(mmu_context_t *ctx, virt_addr_t virt_addr, mmu_page_size_t size);

bool mmu_supports_2mb_pages(void);
bool mmu_supports_1gb_pages(void);
bool mmu_supports_global_pages(void);

int mmu_change_flags(mmu_context_t *ctx, virt_addr_t virt_addr, uint64_t new_flags);
int mmu_change_flags_range(mmu_context_t *ctx, virt_addr_t virt_start, size_t size,
                           uint64_t new_flags);

int mmu_protect(mmu_context_t *ctx, virt_addr_t virt_addr, size_t size, uint64_t flags);
int mmu_make_readonly(mmu_context_t *ctx, virt_addr_t virt_addr, size_t size);
int mmu_make_writable(mmu_context_t *ctx, virt_addr_t virt_addr, size_t size);

int mmu_mark_cow(mmu_context_t *ctx, virt_addr_t virt_addr, size_t size);
int mmu_is_cow_page(mmu_context_t *ctx, virt_addr_t virt_addr);
int mmu_break_cow(mmu_context_t *ctx, virt_addr_t virt_addr);

void mmu_flush_tlb_single(virt_addr_t virt_addr);
void mmu_flush_tlb_range(virt_addr_t virt_start, size_t size);
void mmu_flush_tlb_all(void);
void mmu_flush_tlb_context(mmu_context_t *ctx);

int mmu_map_range_auto(mmu_context_t *ctx, virt_addr_t virt_start, phys_addr_t phys_start,
                       size_t size, uint64_t flags);

mmu_page_size_t mmu_get_page_size(mmu_context_t *ctx, virt_addr_t virt_addr);
void            mmu_get_stats(mmu_context_t *ctx, mmu_stats_t *stats);
uint64_t        mmu_get_table_count(mmu_context_t *ctx);
uint64_t        mmu_get_mapped_pages(mmu_context_t *ctx);

void mmu_parse_fault_error(uint64_t error_code, mmu_fault_info_t *info);
int  mmu_handle_page_fault(mmu_context_t *ctx, virt_addr_t fault_addr, uint64_t error_code);

typedef struct {
    virt_addr_t virt_addr;
    phys_addr_t phys_addr;
    uint64_t    flags;
} mmu_mapping_t;

int mmu_map_batch(mmu_context_t *ctx, const mmu_mapping_t *mappings, size_t count);
int mmu_unmap_batch(mmu_context_t *ctx, const virt_addr_t *virt_addrs, size_t count);

void mmu_print_context_stats(mmu_context_t *ctx);
void mmu_print_mapping_info(mmu_context_t *ctx, virt_addr_t virt_addr);
void mmu_print_tlb_stats(mmu_context_t *ctx);

mmu_context_t *mmu_clone_context(mmu_context_t *src);
int            mmu_copy_range(mmu_context_t *dst, mmu_context_t *src,
                              virt_addr_t virt_start, size_t size, bool cow);

int  mmu_remap_page(mmu_context_t *ctx, virt_addr_t old_virt, virt_addr_t new_virt);
bool mmu_is_aligned_for_huge(virt_addr_t addr, mmu_page_size_t size);

#define MMU_ALIGN_4K_DOWN(addr)  ((addr) & ~(MMU_PAGE_SIZE_4K - 1))
#define MMU_ALIGN_4K_UP(addr)    (((addr) + MMU_PAGE_SIZE_4K - 1) & ~(MMU_PAGE_SIZE_4K - 1))
#define MMU_IS_ALIGNED_4K(addr)  (((addr) & (MMU_PAGE_SIZE_4K - 1)) == 0)

#define MMU_ALIGN_2M_DOWN(addr)  ((addr) & ~(MMU_PAGE_SIZE_2M - 1))
#define MMU_ALIGN_2M_UP(addr)    (((addr) + MMU_PAGE_SIZE_2M - 1) & ~(MMU_PAGE_SIZE_2M - 1))
#define MMU_IS_ALIGNED_2M(addr)  (((addr) & (MMU_PAGE_SIZE_2M - 1)) == 0)

#define MMU_ALIGN_1G_DOWN(addr)  ((addr) & ~(MMU_PAGE_SIZE_1G - 1))
#define MMU_ALIGN_1G_UP(addr)    (((addr) + MMU_PAGE_SIZE_1G - 1) & ~(MMU_PAGE_SIZE_1G - 1))
#define MMU_IS_ALIGNED_1G(addr)  (((addr) & (MMU_PAGE_SIZE_1G - 1)) == 0)

#define MMU_PAGES_4K(bytes)      (((bytes) + MMU_PAGE_SIZE_4K - 1) / MMU_PAGE_SIZE_4K)
#define MMU_PAGES_2M(bytes)      (((bytes) + MMU_PAGE_SIZE_2M - 1) / MMU_PAGE_SIZE_2M)
#define MMU_PAGES_1G(bytes)      (((bytes) + MMU_PAGE_SIZE_1G - 1) / MMU_PAGE_SIZE_1G)

#ifndef PML4_INDEX
#define PML4_INDEX(addr)  (((addr) >> 39) & 0x1FF)
#define PDPT_INDEX(addr)  (((addr) >> 30) & 0x1FF)
#define PD_INDEX(addr)    (((addr) >> 21) & 0x1FF)
#define PT_INDEX(addr)    (((addr) >> 12) & 0x1FF)
#endif

#define MMU_CROSSES_4K_BOUNDARY(addr, size) \
    ((MMU_ALIGN_4K_DOWN(addr)) != MMU_ALIGN_4K_DOWN((addr) + (size) - 1))

#define MMU_CROSSES_2M_BOUNDARY(addr, size) \
    ((MMU_ALIGN_2M_DOWN(addr)) != MMU_ALIGN_2M_DOWN((addr) + (size) - 1))

#ifndef MMU_ENABLE_HUGE_PAGES
#define MMU_ENABLE_HUGE_PAGES     1  /* Enable 2MB/1GB page support */
#endif

#ifndef MMU_ENABLE_COW
#define MMU_ENABLE_COW            1  /* Enable copy-on-write support */
#endif

#ifndef MMU_ENABLE_GLOBAL_PAGES
#define MMU_ENABLE_GLOBAL_PAGES   1  /* Enable global page bit */
#endif

#ifndef MMU_AUTO_HUGE_THRESHOLD
#define MMU_AUTO_HUGE_THRESHOLD   (MMU_PAGE_SIZE_2M * 4)  /* Auto huge for ranges >= 8MB */
#endif

#endif