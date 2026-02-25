#ifndef ARCH_X86_64_MMU_H
#define ARCH_X86_64_MMU_H

#include <klibc/types.h>

#define MMU_TABLE_ENTRIES   512

#define MMU_PAGE_SIZE       4096
#define MMU_PAGE_SHIFT      12

#define MMU_KERNEL_BASE     0xFFFFFFFF80000000UL
#define MMU_HIGHER_HALF     0xFFFF800000000000UL

#define MMU_PRESENT         (1UL << 0)
#define MMU_WRITABLE        (1UL << 1)
#define MMU_USER            (1UL << 2)
#define MMU_WRITE_THROUGH   (1UL << 3)
#define MMU_CACHE_DISABLE   (1UL << 4)
#define MMU_ACCESSED        (1UL << 5)
#define MMU_DIRTY           (1UL << 6)
#define MMU_HUGE            (1UL << 7)
#define MMU_GLOBAL          (1UL << 8)
#define MMU_NX              (1UL << 63)

#define MMU_FLAGS_KERNEL    (MMU_PRESENT | MMU_WRITABLE)
#define MMU_FLAGS_USER      (MMU_PRESENT | MMU_WRITABLE | MMU_USER)
#define MMU_FLAGS_KERNEL_RO (MMU_PRESENT)
#define MMU_FLAGS_USER_RO   (MMU_PRESENT | MMU_USER)

#define MMU_ADDR_MASK       0x000FFFFFFFFFF000UL
#define MMU_FLAGS_MASK      0xFFF0000000000FFFUL

typedef uint64_t pte_t;

typedef struct page_table {
    pte_t entries[MMU_TABLE_ENTRIES];
} __attribute__((aligned(MMU_PAGE_SIZE))) page_table_t;

#define PML4_INDEX(addr)    (((addr) >> 39) & 0x1FF)
#define PDPT_INDEX(addr)    (((addr) >> 30) & 0x1FF)
#define PD_INDEX(addr)      (((addr) >> 21) & 0x1FF)
#define PT_INDEX(addr)      (((addr) >> 12) & 0x1FF)

#define MMU_RECURSIVE_SLOT  510
#define MMU_RECURSIVE_BASE  0xFFFFFF0000000000UL

void        mmu_load_cr3(phys_addr_t pml4_phys);
phys_addr_t mmu_get_cr3(void);
void        mmu_invlpg(virt_addr_t vaddr);
void        mmu_flush_tlb(void);

static inline phys_addr_t pte_get_addr(pte_t pte) {
    return pte & MMU_ADDR_MASK;
}

static inline uint64_t pte_get_flags(pte_t pte) {
    return pte & MMU_FLAGS_MASK;
}

static inline int pte_is_present(pte_t pte) {
    return pte & MMU_PRESENT;
}

static inline pte_t pte_create(phys_addr_t phys_addr, uint64_t flags) {
    return (phys_addr & MMU_ADDR_MASK) | (flags & MMU_FLAGS_MASK);
}

#endif