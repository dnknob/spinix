#include <arch/x86_64/mmu.h>

#include <mm/paging.h>
#include <mm/mmu.h>
#include <mm/pmm.h>

#include <video/printk.h>

#include <klibc/string.h>

static paging_context_t kernel_context;

static inline uint64_t paging_to_mmu_flags(uint64_t paging_flags) {
    uint64_t mmu_flags = 0;
    
    if (paging_flags & PAGE_WRITE) mmu_flags |= MMU_MAP_WRITE;
    if (paging_flags & PAGE_USER) mmu_flags |= MMU_MAP_USER;
    if (paging_flags & PAGE_CACHEDISABLE) mmu_flags |= MMU_MAP_NOCACHE;
    if (paging_flags & PAGE_GLOBAL) mmu_flags |= MMU_MAP_GLOBAL;
    
    return mmu_flags;
}

static inline uint64_t mmu_to_paging_flags(uint64_t hw_flags) {
    uint64_t paging_flags = 0;
    
    if (hw_flags & MMU_PRESENT) paging_flags |= PAGE_PRESENT;
    if (hw_flags & MMU_WRITABLE) paging_flags |= PAGE_WRITE;
    if (hw_flags & MMU_USER) paging_flags |= PAGE_USER;
    if (hw_flags & MMU_CACHE_DISABLE) paging_flags |= PAGE_CACHEDISABLE;
    if (hw_flags & MMU_GLOBAL) paging_flags |= PAGE_GLOBAL;
    if (hw_flags & MMU_ACCESSED) paging_flags |= PAGE_ACCESSED;
    if (hw_flags & MMU_DIRTY) paging_flags |= PAGE_DIRTY;
    if (hw_flags & MMU_NX) paging_flags |= PAGE_NX;
    
    return paging_flags;
}

static inline mmu_page_size_t paging_to_mmu_page_size(paging_page_size_t size) {
    switch (size) {
        case PAGING_PAGE_2M: return MMU_PAGE_2M;
        case PAGING_PAGE_1G: return MMU_PAGE_1G;
        default: return MMU_PAGE_4K;
    }
}

static inline paging_page_size_t mmu_to_paging_page_size(mmu_page_size_t size) {
    switch (size) {
        case MMU_PAGE_2M: return PAGING_PAGE_2M;
        case MMU_PAGE_1G: return PAGING_PAGE_1G;
        default: return PAGING_PAGE_4K;
    }
}

void paging_load_pml4(uint64_t pml4_phys) {
    mmu_load_cr3(pml4_phys);
}

uint64_t paging_get_pml4(void) {
    return mmu_get_cr3();
}

void paging_invalidate_page(uint64_t virt_addr) {
    mmu_invlpg(virt_addr);
}

void paging_flush_tlb(void) {
    mmu_flush_tlb();
}

void paging_init(void) {
    mmu_context_t *mmu_ctx = mmu_get_kernel_context();
    
    kernel_context.pml4_phys = mmu_get_cr3();
    kernel_context.pml4_virt = (page_table_t *)PHYS_TO_VIRT(kernel_context.pml4_phys);
    kernel_context.mmu_ctx = mmu_ctx;
    
    if (paging_identity_map(&kernel_context, 0, 16 * 1024 * 1024, PAGE_FLAGS_KERNEL) != 0) {
        printk("paging: warning: failed to identity map low memory\n");
    }
    printk_ts("paging: initialized\n");
}

paging_context_t *paging_get_kernel_context(void) {
    return &kernel_context;
}

paging_context_t *paging_create_context(void) {
    /* Allocate paging context structure */
    uint64_t ctx_phys = pmm_alloc_page();
    if (ctx_phys == 0) {
        return NULL;
    }
    
    paging_context_t *ctx = (paging_context_t *)PHYS_TO_VIRT(ctx_phys);
    memset(ctx, 0, sizeof(paging_context_t));
    
    mmu_context_t *mmu_ctx = mmu_create_context();
    if (mmu_ctx == NULL) {
        pmm_free_page(ctx_phys);
        return NULL;
    }
    
    ctx->mmu_ctx = mmu_ctx;
    ctx->pml4_phys = mmu_get_cr3();  /* Will be updated on first map */
    ctx->pml4_virt = (page_table_t *)PHYS_TO_VIRT(ctx->pml4_phys);
    
    return ctx;
}

void paging_destroy_context(paging_context_t *ctx) {
    if (ctx == NULL || ctx == &kernel_context) {
        return;
    }
    
    if (ctx->mmu_ctx != NULL) {
        mmu_destroy_context((mmu_context_t *)ctx->mmu_ctx);
    }
    
    pmm_free_page(VIRT_TO_PHYS((uint64_t)ctx));
}

void paging_switch_context(paging_context_t *ctx) {
    if (ctx == NULL) {
        return;
    }
    
    mmu_switch_context((mmu_context_t *)ctx->mmu_ctx);
}

int paging_identity_map(paging_context_t *ctx, uint64_t phys_start,
                        size_t size, uint64_t flags) {
    if (ctx == NULL) {
        ctx = &kernel_context;
    }
    
    uint64_t mmu_flags = paging_to_mmu_flags(flags);
    return mmu_map_range((mmu_context_t *)ctx->mmu_ctx, 
                         phys_start, phys_start, size, mmu_flags);
}

int paging_map_page(paging_context_t *ctx, uint64_t virt_addr,
                    uint64_t phys_addr, uint64_t flags) {
    if (ctx == NULL) {
        ctx = &kernel_context;
    }
    
    uint64_t mmu_flags = paging_to_mmu_flags(flags);
    return mmu_map_page((mmu_context_t *)ctx->mmu_ctx, 
                        virt_addr, phys_addr, mmu_flags);
}

int paging_unmap_page(paging_context_t *ctx, uint64_t virt_addr) {
    if (ctx == NULL) {
        ctx = &kernel_context;
    }
    
    return mmu_unmap_page((mmu_context_t *)ctx->mmu_ctx, virt_addr);
}

int paging_map_range(paging_context_t *ctx, uint64_t virt_start,
                     uint64_t phys_start, size_t size, uint64_t flags) {
    if (ctx == NULL) {
        ctx = &kernel_context;
    }
    
    uint64_t mmu_flags = paging_to_mmu_flags(flags);
    return mmu_map_range((mmu_context_t *)ctx->mmu_ctx,
                         virt_start, phys_start, size, mmu_flags);
}

int paging_unmap_range(paging_context_t *ctx, uint64_t virt_start, size_t size) {
    if (ctx == NULL) {
        ctx = &kernel_context;
    }
    
    return mmu_unmap_range((mmu_context_t *)ctx->mmu_ctx, virt_start, size);
}

uint64_t paging_virt_to_phys(paging_context_t *ctx, uint64_t virt_addr) {
    if (ctx == NULL) {
        ctx = &kernel_context;
    }
    
    return mmu_virt_to_phys((mmu_context_t *)ctx->mmu_ctx, virt_addr);
}

int paging_is_mapped(paging_context_t *ctx, uint64_t virt_addr) {
    if (ctx == NULL) {
        ctx = &kernel_context;
    }
    
    return mmu_is_mapped((mmu_context_t *)ctx->mmu_ctx, virt_addr);
}

uint64_t paging_get_flags(paging_context_t *ctx, uint64_t virt_addr) {
    if (ctx == NULL) {
        ctx = &kernel_context;
    }
    
    uint64_t mmu_flags = mmu_get_flags((mmu_context_t *)ctx->mmu_ctx, virt_addr);
    return mmu_to_paging_flags(mmu_flags);
}

void paging_dump_walk(paging_context_t *ctx, uint64_t virt_addr) {
    if (ctx == NULL) {
        ctx = &kernel_context;
    }
    
    /* Forward to MMU layer's dump function */
    mmu_dump_tables((mmu_context_t *)ctx->mmu_ctx, virt_addr);
}

int paging_change_flags(paging_context_t *ctx, uint64_t virt_addr, uint64_t new_flags) {
    if (ctx == NULL) {
        ctx = &kernel_context;
    }
    
    uint64_t mmu_flags = paging_to_mmu_flags(new_flags);
    return mmu_change_flags((mmu_context_t *)ctx->mmu_ctx, virt_addr, mmu_flags);
}

int paging_change_flags_range(paging_context_t *ctx, uint64_t virt_start,
                               size_t size, uint64_t new_flags) {
    if (ctx == NULL) {
        ctx = &kernel_context;
    }
    
    uint64_t mmu_flags = paging_to_mmu_flags(new_flags);
    return mmu_change_flags_range((mmu_context_t *)ctx->mmu_ctx, 
                                  virt_start, size, mmu_flags);
}

int paging_make_readonly(paging_context_t *ctx, uint64_t virt_addr, size_t size) {
    if (ctx == NULL) {
        ctx = &kernel_context;
    }
    
    return mmu_make_readonly((mmu_context_t *)ctx->mmu_ctx, virt_addr, size);
}

int paging_make_writable(paging_context_t *ctx, uint64_t virt_addr, size_t size) {
    if (ctx == NULL) {
        ctx = &kernel_context;
    }
    
    return mmu_make_writable((mmu_context_t *)ctx->mmu_ctx, virt_addr, size);
}

int paging_mark_cow(paging_context_t *ctx, uint64_t virt_addr, size_t size) {
    if (ctx == NULL) {
        ctx = &kernel_context;
    }
    
    return mmu_mark_cow((mmu_context_t *)ctx->mmu_ctx, virt_addr, size);
}

int paging_is_cow_page(paging_context_t *ctx, uint64_t virt_addr) {
    if (ctx == NULL) {
        ctx = &kernel_context;
    }
    
    return mmu_is_cow_page((mmu_context_t *)ctx->mmu_ctx, virt_addr);
}

int paging_break_cow(paging_context_t *ctx, uint64_t virt_addr) {
    if (ctx == NULL) {
        ctx = &kernel_context;
    }
    
    return mmu_break_cow((mmu_context_t *)ctx->mmu_ctx, virt_addr);
}

paging_context_t *paging_clone_context(paging_context_t *src, bool use_cow) {
    if (src == NULL) {
        return NULL;
    }
    
    paging_context_t *dst = paging_create_context();
    if (dst == NULL) {
        return NULL;
    }
    
    mmu_context_t *cloned_mmu = mmu_clone_context((mmu_context_t *)src->mmu_ctx);
    if (cloned_mmu == NULL) {
        paging_destroy_context(dst);
        return NULL;
    }
    
    dst->mmu_ctx = cloned_mmu;
    dst->pml4_phys = mmu_get_cr3();
    dst->pml4_virt = (page_table_t *)PHYS_TO_VIRT(dst->pml4_phys);
    
    return dst;
}

int paging_map_huge_page(paging_context_t *ctx, uint64_t virt_addr,
                         uint64_t phys_addr, paging_page_size_t size, uint64_t flags) {
    if (ctx == NULL) {
        ctx = &kernel_context;
    }
    
    mmu_page_size_t mmu_size = paging_to_mmu_page_size(size);
    uint64_t mmu_flags = paging_to_mmu_flags(flags);
    
    return mmu_map_huge_page((mmu_context_t *)ctx->mmu_ctx,
                             virt_addr, phys_addr, mmu_size, mmu_flags);
}

int paging_map_range_auto(paging_context_t *ctx, uint64_t virt_start,
                          uint64_t phys_start, size_t size, uint64_t flags) {
    if (ctx == NULL) {
        ctx = &kernel_context;
    }
    
    uint64_t mmu_flags = paging_to_mmu_flags(flags);
    
    return mmu_map_range_auto((mmu_context_t *)ctx->mmu_ctx,
                              virt_start, phys_start, size, mmu_flags);
}

paging_page_size_t paging_get_page_size(paging_context_t *ctx, uint64_t virt_addr) {
    if (ctx == NULL) {
        ctx = &kernel_context;
    }
    
    mmu_page_size_t mmu_size = mmu_get_page_size((mmu_context_t *)ctx->mmu_ctx, virt_addr);
    return mmu_to_paging_page_size(mmu_size);
}

bool paging_supports_2mb_pages(void) {
    return mmu_supports_2mb_pages();
}

bool paging_supports_1gb_pages(void) {
    return mmu_supports_1gb_pages();
}

bool paging_supports_global_pages(void) {
    return mmu_supports_global_pages();
}

bool paging_supports_nx(void) {
    /* MMU doesn't expose this directly, but we can check if NX is in use */
    return true;  /* x86_64 always supports NX via IA32_EFER.NXE */
}

void paging_flush_tlb_single(uint64_t virt_addr) {
    mmu_flush_tlb_single(virt_addr);
}

void paging_flush_tlb_range(uint64_t virt_start, size_t size) {
    mmu_flush_tlb_range(virt_start, size);
}

void paging_flush_tlb_all(void) {
    mmu_flush_tlb_all();
}

void paging_get_stats(paging_context_t *ctx, paging_stats_t *stats) {
    if (ctx == NULL) {
        ctx = &kernel_context;
    }
    
    if (stats == NULL) {
        return;
    }
    
    mmu_stats_t mmu_stats;
    mmu_get_stats((mmu_context_t *)ctx->mmu_ctx, &mmu_stats);
    
    stats->pages_mapped_4k = mmu_stats.pages_mapped_4k;
    stats->pages_mapped_2m = mmu_stats.pages_mapped_2m;
    stats->pages_mapped_1g = mmu_stats.pages_mapped_1g;
    stats->pages_unmapped = mmu_stats.pages_unmapped;
    stats->tables_allocated = mmu_stats.tables_allocated;
    stats->cow_breaks = mmu_stats.cow_breaks;
    stats->page_faults = mmu_stats.page_faults;
    stats->tlb_single_flushes = mmu_stats.tlb_stats.single_flushes;
    stats->tlb_full_flushes = mmu_stats.tlb_stats.full_flushes;
    stats->context_switches = mmu_stats.tlb_stats.context_switches;
}

void paging_print_stats(paging_context_t *ctx) {
    if (ctx == NULL) {
        ctx = &kernel_context;
    }
    
    printk("paging statistics (via mmu):\n");
    mmu_print_context_stats((mmu_context_t *)ctx->mmu_ctx);
}

void paging_print_global_stats(void) {
    printk("global paging statistics:\n");
    mmu_print_stats();
}

void paging_parse_fault_error(uint64_t error_code, paging_fault_info_t *info) {
    if (info == NULL) {
        return;
    }
    
    mmu_fault_info_t mmu_info;
    mmu_parse_fault_error(error_code, &mmu_info);
    
    info->fault_addr = mmu_info.fault_addr;
    info->error_code = mmu_info.error_code;
    info->present = mmu_info.present;
    info->write = mmu_info.write;
    info->user = mmu_info.user;
    info->exec = mmu_info.exec;
    info->reserved = mmu_info.reserved;
}

int paging_handle_page_fault(paging_context_t *ctx, uint64_t fault_addr,
                             uint64_t error_code) {
    if (ctx == NULL) {
        ctx = &kernel_context;
    }
    
    return mmu_handle_page_fault((mmu_context_t *)ctx->mmu_ctx, 
                                 fault_addr, error_code);
}

int paging_map_batch(paging_context_t *ctx, const paging_mapping_t *mappings,
                     size_t count) {
    if (ctx == NULL) {
        ctx = &kernel_context;
    }
    
    if (mappings == NULL || count == 0) {
        return -1;
    }
    
    mmu_mapping_t *mmu_mappings = (mmu_mapping_t *)PHYS_TO_VIRT(pmm_alloc_page());
    if (mmu_mappings == NULL) {
        return -1;
    }
    
    for (size_t i = 0; i < count && i < (PAGE_SIZE / sizeof(mmu_mapping_t)); i++) {
        mmu_mappings[i].virt_addr = mappings[i].virt_addr;
        mmu_mappings[i].phys_addr = mappings[i].phys_addr;
        mmu_mappings[i].flags = paging_to_mmu_flags(mappings[i].flags);
    }
    
    int result = mmu_map_batch((mmu_context_t *)ctx->mmu_ctx, mmu_mappings, count);
    
    pmm_free_page(VIRT_TO_PHYS((uint64_t)mmu_mappings));
    return result;
}

int paging_unmap_batch(paging_context_t *ctx, const uint64_t *virt_addrs,
                       size_t count) {
    if (ctx == NULL) {
        ctx = &kernel_context;
    }
    
    return mmu_unmap_batch((mmu_context_t *)ctx->mmu_ctx, virt_addrs, count);
}

bool paging_is_aligned_for_huge(uint64_t addr, paging_page_size_t size) {
    mmu_page_size_t mmu_size = paging_to_mmu_page_size(size);
    return mmu_is_aligned_for_huge(addr, mmu_size);
}
