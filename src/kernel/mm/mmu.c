#include <arch/x86_64/cpuid.h>
#include <arch/x86_64/mmu.h>

#include <mm/mmu.h>
#include <mm/pmm.h>

#include <video/printk.h>

#include <klibc/string.h>

extern char __kernel_start;
extern char __kernel_end;

struct mmu_context {
    uint64_t pml4_phys;
    page_table_t *pml4_virt;
    uint64_t page_count;
    mmu_stats_t stats;          /* Per-context statistics */
};

static struct mmu_context kernel_ctx;
static int mmu_initialized = 0;

static struct {
    uint64_t pages_mapped;
    uint64_t pages_unmapped;
    uint64_t tables_allocated;
    uint64_t tlb_flushes;
} mmu_stats;

static struct {
    bool supports_2mb_pages;
    bool supports_1gb_pages;
    bool supports_global_pages;
    bool supports_nx;
    bool supports_pcid;
} cpu_features;

#ifndef MMU_AVAILABLE_1
#define MMU_AVAILABLE_1 (1ULL << 9)
#endif

static void detect_cpu_features(void) {
    cpuid_regs_t regs;

    cpuid(0x01, 0, &regs);
    cpu_features.supports_2mb_pages = (regs.edx & CPUID_FEAT_EDX_PSE) != 0;
    cpu_features.supports_global_pages = (regs.edx & CPUID_FEAT_EDX_PGE) != 0;
    cpu_features.supports_pcid = (regs.ecx & CPUID_FEAT_ECX_PCID) != 0;

    cpuid(0x80000001, 0, &regs);
    cpu_features.supports_1gb_pages = (regs.edx & CPUID_FEAT_EXT_1GB_PAGE) != 0;
    cpu_features.supports_nx = (regs.edx & CPUID_FEAT_EXT_XD) != 0;

}

static page_table_t *alloc_page_table(void) {
    uint64_t phys = pmm_alloc_page();
    if (phys == 0) {
        printk("mmu: panic: failed to allocate page table!\n");
        return NULL;
    }

    page_table_t *table = (page_table_t *)PHYS_TO_VIRT(phys);
    memset(table, 0, PAGE_SIZE);

    mmu_stats.tables_allocated++;
    return table;
}

static page_table_t *get_or_create_table(page_table_t *table, size_t index, uint64_t flags) {
    pte_t *entry = &table->entries[index];

    if (pte_is_present(*entry)) {
        if (*entry & MMU_NX) {
            *entry &= ~MMU_NX;   /* ← THE FIX: clear NX on intermediate entry */
        }
        return (page_table_t *)PHYS_TO_VIRT(pte_get_addr(*entry));
    }

    page_table_t *new_table = alloc_page_table();
    if (new_table == NULL) {
        return NULL;
    }

    uint64_t phys = VIRT_TO_PHYS((uint64_t)new_table);
    /* New intermediate entries are created without NX */
    *entry = pte_create(phys, (flags & ~MMU_NX) | MMU_PRESENT | MMU_WRITABLE);

    return new_table;
}

static pte_t *walk_page_tables(mmu_context_t *ctx, uint64_t virt_addr, int create, uint64_t flags) {
    page_table_t *pml4 = ctx->pml4_virt;

    size_t pml4_idx = PML4_INDEX(virt_addr);
    size_t pdpt_idx = PDPT_INDEX(virt_addr);
    size_t pd_idx = PD_INDEX(virt_addr);
    size_t pt_idx = PT_INDEX(virt_addr);

    page_table_t *pdpt;
    if (create) {
        pdpt = get_or_create_table(pml4, pml4_idx, flags);
    } else {
        if (!pte_is_present(pml4->entries[pml4_idx])) {
            return NULL;
        }
        pdpt = (page_table_t *)PHYS_TO_VIRT(pte_get_addr(pml4->entries[pml4_idx]));
    }
    if (pdpt == NULL) return NULL;

    page_table_t *pd;
    if (create) {
        pd = get_or_create_table(pdpt, pdpt_idx, flags);
    } else {
        if (!pte_is_present(pdpt->entries[pdpt_idx])) {
            return NULL;
        }
        pd = (page_table_t *)PHYS_TO_VIRT(pte_get_addr(pdpt->entries[pdpt_idx]));
    }
    if (pd == NULL) return NULL;

    page_table_t *pt;
    if (create) {
        pt = get_or_create_table(pd, pd_idx, flags);
    } else {
        if (!pte_is_present(pd->entries[pd_idx])) {
            return NULL;
        }
        pt = (page_table_t *)PHYS_TO_VIRT(pte_get_addr(pd->entries[pd_idx]));
    }
    if (pt == NULL) return NULL;

    return &pt->entries[pt_idx];
}

void mmu_init(void) {
    memset(&mmu_stats, 0, sizeof(mmu_stats));
    detect_cpu_features();

    uint64_t current_cr3 = mmu_get_cr3();
    kernel_ctx.pml4_phys = current_cr3;
    kernel_ctx.pml4_virt = (page_table_t *)PHYS_TO_VIRT(current_cr3);
    kernel_ctx.page_count = 0;
    memset(&kernel_ctx.stats, 0, sizeof(mmu_stats_t));

    page_table_t *pml4 = kernel_ctx.pml4_virt;
    if (!pte_is_present(pml4->entries[MMU_RECURSIVE_SLOT])) {
        pml4->entries[MMU_RECURSIVE_SLOT] = pte_create(kernel_ctx.pml4_phys,
                                                         MMU_PRESENT | MMU_WRITABLE);
        mmu_flush_tlb();
    }

    mmu_initialized = 1;
    printk_ts("mmu: initialized\n");
}

mmu_context_t *mmu_get_kernel_context(void) {
    return &kernel_ctx;
}

mmu_context_t *mmu_create_context(void) {
    mmu_context_t *ctx = (mmu_context_t *)PHYS_TO_VIRT(pmm_alloc_page());
    if (ctx == NULL) {
        return NULL;
    }

    page_table_t *pml4 = alloc_page_table();
    if (pml4 == NULL) {
        pmm_free_page(VIRT_TO_PHYS((uint64_t)ctx));
        return NULL;
    }

    ctx->pml4_phys = VIRT_TO_PHYS((uint64_t)pml4);
    ctx->pml4_virt = pml4;
    ctx->page_count = 1;
    memset(&ctx->stats, 0, sizeof(mmu_stats_t));

    for (size_t i = 256; i < MMU_TABLE_ENTRIES; i++) {
        pml4->entries[i] = kernel_ctx.pml4_virt->entries[i];
    }

    return ctx;
}

void mmu_destroy_context(mmu_context_t *ctx) {
    if (ctx == NULL || ctx == &kernel_ctx) {
        return;
    }

    pmm_free_page(ctx->pml4_phys);
    pmm_free_page(VIRT_TO_PHYS((uint64_t)ctx));
}

void mmu_switch_context(mmu_context_t *ctx) {
    if (ctx == NULL) {
        return;
    }

    uint64_t current_cr3 = mmu_get_cr3();
    if (current_cr3 != ctx->pml4_phys) {
        mmu_load_cr3(ctx->pml4_phys);
        mmu_stats.tlb_flushes++;
        ctx->stats.tlb_stats.context_switches++;
    }
}

int mmu_map_page(mmu_context_t *ctx, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags) {
    if (ctx == NULL) {
        ctx = &kernel_ctx;
    }

    virt_addr &= ~(PAGE_SIZE - 1);
    phys_addr &= ~(PAGE_SIZE - 1);

    uint64_t arch_flags = MMU_PRESENT;
    if (flags & MMU_MAP_WRITE) arch_flags |= MMU_WRITABLE;
    if (flags & MMU_MAP_USER) arch_flags |= MMU_USER;
    if (flags & MMU_MAP_NOCACHE) arch_flags |= MMU_CACHE_DISABLE;
    if (flags & MMU_MAP_GLOBAL && cpu_features.supports_global_pages) arch_flags |= MMU_GLOBAL;

    pte_t *pte = walk_page_tables(ctx, virt_addr, 1, arch_flags);
    if (pte == NULL) {
        return -1;
    }

    *pte = pte_create(phys_addr, arch_flags);
    mmu_invlpg(virt_addr);

    mmu_stats.pages_mapped++;
    ctx->stats.pages_mapped_4k++;
    return 0;
}

int mmu_unmap_page(mmu_context_t *ctx, uint64_t virt_addr) {
    if (ctx == NULL) {
        ctx = &kernel_ctx;
    }

    virt_addr &= ~(PAGE_SIZE - 1);

    pte_t *pte = walk_page_tables(ctx, virt_addr, 0, 0);
    if (pte == NULL || !pte_is_present(*pte)) {
        return -1;
    }

    *pte = 0;
    mmu_invlpg(virt_addr);

    mmu_stats.pages_unmapped++;
    ctx->stats.pages_unmapped++;
    return 0;
}

int mmu_map_range(mmu_context_t *ctx, uint64_t virt_start, uint64_t phys_start,
                  size_t size, uint64_t flags) {
    if (ctx == NULL) {
        ctx = &kernel_ctx;
    }

    size_t num_pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;

    for (size_t i = 0; i < num_pages; i++) {
        uint64_t virt = virt_start + (i * PAGE_SIZE);
        uint64_t phys = phys_start + (i * PAGE_SIZE);

        if (mmu_map_page(ctx, virt, phys, flags) != 0) {
            for (size_t j = 0; j < i; j++) {
                mmu_unmap_page(ctx, virt_start + (j * PAGE_SIZE));
            }
            return -1;
        }
    }

    return 0;
}

int mmu_unmap_range(mmu_context_t *ctx, uint64_t virt_start, size_t size) {
    if (ctx == NULL) {
        ctx = &kernel_ctx;
    }

    size_t num_pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;

    for (size_t i = 0; i < num_pages; i++) {
        mmu_unmap_page(ctx, virt_start + (i * PAGE_SIZE));
    }

    return 0;
}

uint64_t mmu_virt_to_phys(mmu_context_t *ctx, uint64_t virt_addr) {
    if (ctx == NULL) {
        ctx = &kernel_ctx;
    }

    pte_t *pte = walk_page_tables(ctx, virt_addr, 0, 0);
    if (pte == NULL || !pte_is_present(*pte)) {
        return 0;
    }

    uint64_t phys_page = pte_get_addr(*pte);
    uint64_t offset = virt_addr & (PAGE_SIZE - 1);

    return phys_page + offset;
}

int mmu_is_mapped(mmu_context_t *ctx, uint64_t virt_addr) {
    if (ctx == NULL) {
        ctx = &kernel_ctx;
    }

    pte_t *pte = walk_page_tables(ctx, virt_addr, 0, 0);
    return (pte != NULL && pte_is_present(*pte));
}

uint64_t mmu_get_flags(mmu_context_t *ctx, uint64_t virt_addr) {
    if (ctx == NULL) {
        ctx = &kernel_ctx;
    }

    pte_t *pte = walk_page_tables(ctx, virt_addr, 0, 0);
    if (pte == NULL || !pte_is_present(*pte)) {
        return 0;
    }

    return pte_get_flags(*pte);
}

void mmu_dump_tables(mmu_context_t *ctx, uint64_t virt_addr) {
    if (ctx == NULL) {
        ctx = &kernel_ctx;
    }

    printk("mmu page table walk for 0x%016lx:\n", virt_addr);

    page_table_t *pml4 = ctx->pml4_virt;
    size_t pml4_idx = PML4_INDEX(virt_addr);
    size_t pdpt_idx = PDPT_INDEX(virt_addr);
    size_t pd_idx = PD_INDEX(virt_addr);
    size_t pt_idx = PT_INDEX(virt_addr);

    printk("pml4[%lu] = 0x%016lx", pml4_idx, pml4->entries[pml4_idx]);
    if (!pte_is_present(pml4->entries[pml4_idx])) {
        printk(" (not present)\n");
        return;
    }
    printk("\n");

    page_table_t *pdpt = (page_table_t *)PHYS_TO_VIRT(pte_get_addr(pml4->entries[pml4_idx]));
    printk("pdpt[%lu] = 0x%016lx", pdpt_idx, pdpt->entries[pdpt_idx]);
    if (!pte_is_present(pdpt->entries[pdpt_idx])) {
        printk(" (not present)\n");
        return;
    }
    printk("\n");

    page_table_t *pd = (page_table_t *)PHYS_TO_VIRT(pte_get_addr(pdpt->entries[pdpt_idx]));
    printk("pd[%lu]   = 0x%016lx", pd_idx, pd->entries[pd_idx]);
    if (!pte_is_present(pd->entries[pd_idx])) {
        printk(" (not present)\n");
        return;
    }
    printk("\n");

    page_table_t *pt = (page_table_t *)PHYS_TO_VIRT(pte_get_addr(pd->entries[pd_idx]));
    printk("pt[%lu]   = 0x%016lx", pt_idx, pt->entries[pt_idx]);
    if (!pte_is_present(pt->entries[pt_idx])) {
        printk(" (not present)\n");
        return;
    }

    uint64_t phys = pte_get_addr(pt->entries[pt_idx]);
    uint64_t flags = pte_get_flags(pt->entries[pt_idx]);
    printk(" -> phys 0x%016lx\n", phys);
    printk("flags: %c%c%c%c%c%c%c\n",
           (flags & MMU_PRESENT) ? 'P' : '-',
           (flags & MMU_WRITABLE) ? 'W' : '-',
           (flags & MMU_USER) ? 'U' : '-',
           (flags & MMU_GLOBAL) ? 'G' : '-',
           (flags & MMU_ACCESSED) ? 'A' : '-',
           (flags & MMU_DIRTY) ? 'D' : '-',
           (flags & MMU_NX) ? 'X' : '-');

}

void mmu_print_stats(void) {
    printk("mmu statistics:\n");
    printk("pages mapped:       %lu\n", mmu_stats.pages_mapped);
    printk("pages unmapped:     %lu\n", mmu_stats.pages_unmapped);
    printk("page tables alloc:  %lu\n", mmu_stats.tables_allocated);
    printk("tlb flushes:        %lu\n", mmu_stats.tlb_flushes);
    printk("memory used:        %lu KB\n",
           (mmu_stats.tables_allocated * PAGE_SIZE) / 1024);
}

bool mmu_supports_2mb_pages(void) {
    return cpu_features.supports_2mb_pages;
}

bool mmu_supports_1gb_pages(void) {
    return cpu_features.supports_1gb_pages;
}

bool mmu_supports_global_pages(void) {
    return cpu_features.supports_global_pages;
}

bool mmu_is_aligned_for_huge(uint64_t addr, mmu_page_size_t size) {
    switch (size) {
        case MMU_PAGE_2M:
            return MMU_IS_ALIGNED_2M(addr);
        case MMU_PAGE_1G:
            return MMU_IS_ALIGNED_1G(addr);
        default:
            return MMU_IS_ALIGNED_4K(addr);
    }
}

int mmu_map_huge_page(mmu_context_t *ctx, uint64_t virt_addr, uint64_t phys_addr,
                      mmu_page_size_t size, uint64_t flags) {
    if (ctx == NULL) {
        ctx = &kernel_ctx;
    }

    if (!mmu_is_aligned_for_huge(virt_addr, size) ||
        !mmu_is_aligned_for_huge(phys_addr, size)) {
        printk("mmu: Huge page addresses must be properly aligned\n");
        return -1;
    }

    uint64_t arch_flags = MMU_PRESENT | MMU_HUGE;  /* PS bit for huge page */
    if (flags & MMU_MAP_WRITE) arch_flags |= MMU_WRITABLE;
    if (flags & MMU_MAP_USER) arch_flags |= MMU_USER;
    if (flags & MMU_MAP_GLOBAL && cpu_features.supports_global_pages) arch_flags |= MMU_GLOBAL;

    page_table_t *pml4 = ctx->pml4_virt;

    if (size == MMU_PAGE_2M) {
        if (!cpu_features.supports_2mb_pages) {
            printk("mmu: CPU doesn't support 2MB pages\n");
            return -1;
        }

        size_t pml4_idx = PML4_INDEX(virt_addr);
        size_t pdpt_idx = PDPT_INDEX(virt_addr);
        size_t pd_idx = PD_INDEX(virt_addr);

        page_table_t *pdpt = get_or_create_table(pml4, pml4_idx, MMU_PRESENT | MMU_WRITABLE);
        if (!pdpt) return -1;

        page_table_t *pd = get_or_create_table(pdpt, pdpt_idx, MMU_PRESENT | MMU_WRITABLE);
        if (!pd) return -1;

        pd->entries[pd_idx] = pte_create(phys_addr, arch_flags);
        mmu_invlpg(virt_addr);

        ctx->stats.pages_mapped_2m++;
        return 0;

    } else if (size == MMU_PAGE_1G) {
        if (!cpu_features.supports_1gb_pages) {
            printk("mmu: CPU doesn't support 1GB pages\n");
            return -1;
        }

        size_t pml4_idx = PML4_INDEX(virt_addr);
        size_t pdpt_idx = PDPT_INDEX(virt_addr);

        page_table_t *pdpt = get_or_create_table(pml4, pml4_idx, MMU_PRESENT | MMU_WRITABLE);
        if (!pdpt) return -1;

        pdpt->entries[pdpt_idx] = pte_create(phys_addr, arch_flags);
        mmu_invlpg(virt_addr);

        ctx->stats.pages_mapped_1g++;
        return 0;
    }

    return mmu_map_page(ctx, virt_addr, phys_addr, flags);
}

int mmu_map_range_auto(mmu_context_t *ctx, uint64_t virt_start, uint64_t phys_start,
                       size_t size, uint64_t flags) {
    if (ctx == NULL) {
        ctx = &kernel_ctx;
    }

    uint64_t virt = virt_start;
    uint64_t phys = phys_start;
    size_t remaining = size;

    while (remaining > 0) {
        if (cpu_features.supports_1gb_pages &&
            remaining >= MMU_PAGE_SIZE_1G &&
            MMU_IS_ALIGNED_1G(virt) &&
            MMU_IS_ALIGNED_1G(phys)) {

            if (mmu_map_huge_page(ctx, virt, phys, MMU_PAGE_1G, flags) == 0) {
                virt += MMU_PAGE_SIZE_1G;
                phys += MMU_PAGE_SIZE_1G;
                remaining -= MMU_PAGE_SIZE_1G;
                continue;
            }
        }

        if (cpu_features.supports_2mb_pages &&
            remaining >= MMU_PAGE_SIZE_2M &&
            MMU_IS_ALIGNED_2M(virt) &&
            MMU_IS_ALIGNED_2M(phys)) {

            if (mmu_map_huge_page(ctx, virt, phys, MMU_PAGE_2M, flags) == 0) {
                virt += MMU_PAGE_SIZE_2M;
                phys += MMU_PAGE_SIZE_2M;
                remaining -= MMU_PAGE_SIZE_2M;
                continue;
            }
        }

        if (mmu_map_page(ctx, virt, phys, flags) != 0) {
            return -1;
        }

        virt += MMU_PAGE_SIZE_4K;
        phys += MMU_PAGE_SIZE_4K;
        remaining -= (remaining < MMU_PAGE_SIZE_4K) ? remaining : MMU_PAGE_SIZE_4K;
    }

    return 0;
}

void mmu_flush_tlb_single(uint64_t virt_addr) {
    mmu_invlpg(virt_addr);
    kernel_ctx.stats.tlb_stats.single_flushes++;
}

void mmu_flush_tlb_range(uint64_t virt_start, size_t size) {
    size_t num_pages = (size + MMU_PAGE_SIZE_4K - 1) / MMU_PAGE_SIZE_4K;

    if (num_pages > 32) {
        mmu_flush_tlb_all();
        return;
    }

    for (size_t i = 0; i < num_pages; i++) {
        mmu_invlpg(virt_start + (i * MMU_PAGE_SIZE_4K));
    }

    kernel_ctx.stats.tlb_stats.single_flushes += num_pages;
}

void mmu_flush_tlb_all(void) {
    mmu_flush_tlb();
    kernel_ctx.stats.tlb_stats.full_flushes++;
}

void mmu_flush_tlb_context(mmu_context_t *ctx) {
    if (ctx) {
        mmu_load_cr3(ctx->pml4_phys);
        ctx->stats.tlb_stats.full_flushes++;
    }
}

int mmu_mark_cow(mmu_context_t *ctx, uint64_t virt_addr, size_t size) {
    if (ctx == NULL) {
        ctx = &kernel_ctx;
    }

    size_t num_pages = (size + MMU_PAGE_SIZE_4K - 1) / MMU_PAGE_SIZE_4K;

    for (size_t i = 0; i < num_pages; i++) {
        uint64_t virt = virt_addr + (i * MMU_PAGE_SIZE_4K);
        pte_t *pte = walk_page_tables(ctx, virt, 0, 0);

        if (pte && pte_is_present(*pte)) {
            uint64_t flags = pte_get_flags(*pte);
            flags &= ~MMU_WRITABLE;
            flags |= MMU_AVAILABLE_1;

            uint64_t phys = pte_get_addr(*pte);
            *pte = pte_create(phys, flags);

            mmu_invlpg(virt);
        }
    }

    return 0;
}

int mmu_is_cow_page(mmu_context_t *ctx, uint64_t virt_addr) {
    if (ctx == NULL) {
        ctx = &kernel_ctx;
    }

    pte_t *pte = walk_page_tables(ctx, virt_addr, 0, 0);
    if (!pte || !pte_is_present(*pte)) {
        return 0;
    }

    uint64_t flags = pte_get_flags(*pte);
    return (flags & MMU_AVAILABLE_1) != 0;
}

int mmu_break_cow(mmu_context_t *ctx, uint64_t virt_addr) {
    if (ctx == NULL) {
        ctx = &kernel_ctx;
    }

    pte_t *pte = walk_page_tables(ctx, virt_addr, 0, 0);
    if (!pte || !pte_is_present(*pte)) {
        return -1;
    }

    uint64_t flags = pte_get_flags(*pte);
    if (!(flags & MMU_AVAILABLE_1)) {
        return 0;  /* not a COW page */
    }

    uint64_t new_phys = pmm_alloc_page();
    if (new_phys == 0) {
        return -1;
    }

    uint64_t old_phys = pte_get_addr(*pte);
    void *old_virt = PHYS_TO_VIRT(old_phys);
    void *new_virt = PHYS_TO_VIRT(new_phys);
    memcpy(new_virt, old_virt, MMU_PAGE_SIZE_4K);

    flags |= MMU_WRITABLE;
    flags &= ~MMU_AVAILABLE_1;
    *pte = pte_create(new_phys, flags);

    mmu_invlpg(virt_addr);

    ctx->stats.cow_breaks++;
    return 0;
}

int mmu_change_flags(mmu_context_t *ctx, uint64_t virt_addr, uint64_t new_flags) {
    if (ctx == NULL) {
        ctx = &kernel_ctx;
    }

    pte_t *pte = walk_page_tables(ctx, virt_addr, 0, 0);
    if (!pte || !pte_is_present(*pte)) {
        return -1;
    }

    uint64_t phys = pte_get_addr(*pte);

    uint64_t arch_flags = MMU_PRESENT;
    if (new_flags & MMU_MAP_WRITE) arch_flags |= MMU_WRITABLE;
    if (new_flags & MMU_MAP_USER) arch_flags |= MMU_USER;
    if (new_flags & MMU_MAP_NOCACHE) arch_flags |= MMU_CACHE_DISABLE;
    if (new_flags & MMU_MAP_GLOBAL) arch_flags |= MMU_GLOBAL;

    *pte = pte_create(phys, arch_flags);
    mmu_invlpg(virt_addr);

    return 0;
}

int mmu_change_flags_range(mmu_context_t *ctx, uint64_t virt_start, size_t size,
                           uint64_t new_flags) {
    size_t num_pages = (size + MMU_PAGE_SIZE_4K - 1) / MMU_PAGE_SIZE_4K;

    for (size_t i = 0; i < num_pages; i++) {
        if (mmu_change_flags(ctx, virt_start + (i * MMU_PAGE_SIZE_4K), new_flags) != 0) {
            return -1;
        }
    }

    return 0;
}

int mmu_make_readonly(mmu_context_t *ctx, uint64_t virt_addr, size_t size) {
    if (ctx == NULL) {
        ctx = &kernel_ctx;
    }

    size_t num_pages = (size + MMU_PAGE_SIZE_4K - 1) / MMU_PAGE_SIZE_4K;

    for (size_t i = 0; i < num_pages; i++) {
        uint64_t virt = virt_addr + (i * MMU_PAGE_SIZE_4K);
        pte_t *pte = walk_page_tables(ctx, virt, 0, 0);

        if (pte && pte_is_present(*pte)) {
            uint64_t flags = pte_get_flags(*pte);
            flags &= ~MMU_WRITABLE;

            uint64_t phys = pte_get_addr(*pte);
            *pte = pte_create(phys, flags);

            mmu_invlpg(virt);
        }
    }

    return 0;
}

int mmu_make_writable(mmu_context_t *ctx, uint64_t virt_addr, size_t size) {
    if (ctx == NULL) {
        ctx = &kernel_ctx;
    }

    size_t num_pages = (size + MMU_PAGE_SIZE_4K - 1) / MMU_PAGE_SIZE_4K;

    for (size_t i = 0; i < num_pages; i++) {
        uint64_t virt = virt_addr + (i * MMU_PAGE_SIZE_4K);
        pte_t *pte = walk_page_tables(ctx, virt, 0, 0);

        if (pte && pte_is_present(*pte)) {
            uint64_t flags = pte_get_flags(*pte);
            flags |= MMU_WRITABLE;

            uint64_t phys = pte_get_addr(*pte);
            *pte = pte_create(phys, flags);

            mmu_invlpg(virt);
        }
    }

    return 0;
}

mmu_page_size_t mmu_get_page_size(mmu_context_t *ctx, uint64_t virt_addr) {
    if (ctx == NULL) {
        ctx = &kernel_ctx;
    }

    page_table_t *pml4 = ctx->pml4_virt;
    size_t pml4_idx = PML4_INDEX(virt_addr);

    if (!pte_is_present(pml4->entries[pml4_idx])) {
        return MMU_PAGE_4K;
    }

    page_table_t *pdpt = (page_table_t *)PHYS_TO_VIRT(pte_get_addr(pml4->entries[pml4_idx]));
    size_t pdpt_idx = PDPT_INDEX(virt_addr);

    if (!pte_is_present(pdpt->entries[pdpt_idx])) {
        return MMU_PAGE_4K;
    }

    if (pte_get_flags(pdpt->entries[pdpt_idx]) & MMU_HUGE) {
        return MMU_PAGE_1G;
    }

    page_table_t *pd = (page_table_t *)PHYS_TO_VIRT(pte_get_addr(pdpt->entries[pdpt_idx]));
    size_t pd_idx = PD_INDEX(virt_addr);

    if (!pte_is_present(pd->entries[pd_idx])) {
        return MMU_PAGE_4K;
    }

    if (pte_get_flags(pd->entries[pd_idx]) & MMU_HUGE) {
        return MMU_PAGE_2M;
    }

    return MMU_PAGE_4K;
}

void mmu_get_stats(mmu_context_t *ctx, mmu_stats_t *stats) {
    if (stats == NULL) {
        return;
    }

    if (ctx == NULL) {
        ctx = &kernel_ctx;
    }

    *stats = ctx->stats;
}

void mmu_print_context_stats(mmu_context_t *ctx) {
    if (ctx == NULL) {
        ctx = &kernel_ctx;
    }

    printk("mmu context statistics:\n");
    printk("4KB pages mapped:   %lu\n", ctx->stats.pages_mapped_4k);
    printk("2MB pages mapped:   %lu\n", ctx->stats.pages_mapped_2m);
    printk("1GB pages mapped:   %lu\n", ctx->stats.pages_mapped_1g);
    printk("pages unmapped:     %lu\n", ctx->stats.pages_unmapped);
    printk("page tables alloc:  %lu\n", ctx->stats.tables_allocated);
    printk("cow breaks:         %lu\n", ctx->stats.cow_breaks);
    printk("page faults:        %lu\n", ctx->stats.page_faults);
    printk("TLB single flushes: %lu\n", ctx->stats.tlb_stats.single_flushes);
    printk("TLB full flushes:   %lu\n", ctx->stats.tlb_stats.full_flushes);
    printk("Context switches:   %lu\n", ctx->stats.tlb_stats.context_switches);
}

void mmu_print_tlb_stats(mmu_context_t *ctx) {
    if (ctx == NULL) {
        ctx = &kernel_ctx;
    }

    printk("tlb statistics:\n");
    printk("Single page flushes: %lu\n", ctx->stats.tlb_stats.single_flushes);
    printk("Full tlb flushes:    %lu\n", ctx->stats.tlb_stats.full_flushes);
    printk("Context switches:    %lu\n", ctx->stats.tlb_stats.context_switches);
}

void mmu_parse_fault_error(uint64_t error_code, mmu_fault_info_t *info) {
    if (!info) {
        return;
    }

    info->error_code = error_code;
    info->present = (error_code & 0x1) != 0;
    info->write = (error_code & 0x2) != 0;
    info->user = (error_code & 0x4) != 0;
    info->reserved = (error_code & 0x8) != 0;
    info->exec = (error_code & 0x10) != 0;
}

int mmu_handle_page_fault(mmu_context_t *ctx, uint64_t fault_addr, uint64_t error_code) {
    if (ctx == NULL) {
        ctx = &kernel_ctx;
    }

    mmu_fault_info_t info;
    mmu_parse_fault_error(error_code, &info);
    info.fault_addr = fault_addr;

    ctx->stats.page_faults++;

    if (info.present && info.write && mmu_is_cow_page(ctx, fault_addr)) {
        return mmu_break_cow(ctx, fault_addr);
    }

    return -1;
}

int mmu_protect(mmu_context_t *ctx, virt_addr_t virt_addr, size_t size, uint64_t flags) {
    return mmu_change_flags_range(ctx, virt_addr, size, flags);
}

uint64_t mmu_get_table_count(mmu_context_t *ctx) {
    return ctx ? ctx->stats.tables_allocated : 0;
}

uint64_t mmu_get_mapped_pages(mmu_context_t *ctx) {
    return ctx ? (ctx->stats.pages_mapped_4k +
                  ctx->stats.pages_mapped_2m +
                  ctx->stats.pages_mapped_1g) : 0;
}

mmu_context_t *mmu_clone_context(mmu_context_t *src) {
    if (!src) return NULL;
    mmu_context_t *dst = mmu_create_context();
    if (!dst) return NULL;
    for (size_t i = 0; i < 256; i++) {
        if (pte_is_present(src->pml4_virt->entries[i]))
            dst->pml4_virt->entries[i] = src->pml4_virt->entries[i];
    }
    return dst;
}

int mmu_copy_range(mmu_context_t *dst, mmu_context_t *src,
                   uint64_t virt_start, size_t size, bool cow) {
    if (!dst || !src) return -1;
    size_t n = (size + MMU_PAGE_SIZE_4K - 1) / MMU_PAGE_SIZE_4K;
    for (size_t i = 0; i < n; i++) {
        uint64_t virt = virt_start + i * MMU_PAGE_SIZE_4K;
        pte_t *src_pte = walk_page_tables(src, virt, 0, 0);
        if (!src_pte || !pte_is_present(*src_pte)) continue;
        uint64_t phys  = pte_get_addr(*src_pte);
        uint64_t flags = pte_get_flags(*src_pte);
        if (cow) flags = (flags & ~MMU_WRITABLE) | MMU_AVAILABLE_1;
        pte_t *dst_pte = walk_page_tables(dst, virt, 1, flags);
        if (!dst_pte) return -1;
        *dst_pte = pte_create(phys, flags);
        mmu_invlpg(virt);
    }
    return 0;
}

int mmu_remap_page(mmu_context_t *ctx, uint64_t old_virt, uint64_t new_virt) {
    if (!ctx) ctx = &kernel_ctx;
    pte_t *old = walk_page_tables(ctx, old_virt, 0, 0);
    if (!old || !pte_is_present(*old)) return -1;
    uint64_t phys  = pte_get_addr(*old);
    uint64_t flags = pte_get_flags(*old);
    *old = 0;
    mmu_invlpg(old_virt);
    pte_t *newpte = walk_page_tables(ctx, new_virt, 1, flags);
    if (!newpte) return -1;
    *newpte = pte_create(phys, flags);
    mmu_invlpg(new_virt);
    return 0;
}

int mmu_map_batch(mmu_context_t *ctx, const mmu_mapping_t *mappings, size_t count) {
    for (size_t i = 0; i < count; i++)
        if (mmu_map_page(ctx, mappings[i].virt_addr,
                             mappings[i].phys_addr,
                             mappings[i].flags) != 0) return -1;
    return 0;
}

int mmu_unmap_batch(mmu_context_t *ctx, const uint64_t *virt_addrs, size_t count) {
    for (size_t i = 0; i < count; i++)
        mmu_unmap_page(ctx, virt_addrs[i]);
    return 0;
}

void mmu_print_mapping_info(mmu_context_t *ctx, uint64_t virt_addr) {
    mmu_dump_tables(ctx, virt_addr);
}