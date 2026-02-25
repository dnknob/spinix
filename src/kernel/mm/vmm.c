#include <mm/mmu.h>
#include <mm/pmm.h>
#include <mm/vmm.h>

#include <video/printk.h>

#include <core/spinlock.h>

#include <klibc/string.h>

#define ALIGN_DOWN(addr, align) ((addr) & ~((align) - 1))
#define ALIGN_UP(addr, align)   (((addr) + (align) - 1) & ~((align) - 1))
#define IS_ALIGNED(addr, align) (((addr) & ((align) - 1)) == 0)

#define CANONICAL_USER_MAX   0x00007FFFFFFFFFFFULL  /* User space max */
#define CANONICAL_KERNEL_MIN 0xFFFF800000000000ULL  /* Kernel space min */

static vm_space_t kernel_space;
static int vmm_initialized = 0;
static spinlock_irq_t vmm_lock = SPINLOCK_IRQ_INIT;

static struct {
    uint64_t lazy_allocations;      /* Pages allocated on page fault */
    uint64_t eager_allocations;     /* Pages allocated immediately */
    uint64_t page_faults_handled;   /* Page faults successfully handled */
    uint64_t cow_faults_handled;    /* COW page faults handled */
    uint64_t protection_faults;     /* Protection violation faults */
    uint64_t regions_created;       /* Total regions created */
    uint64_t regions_destroyed;     /* Total regions destroyed */
    uint64_t regions_split;         /* Regions split */
    uint64_t regions_merged;        /* Regions merged */
} vmm_stats;

int vmm_is_canonical_addr(uint64_t addr) {
    /* Check if address is in canonical form for x86_64 */
    return (addr <= CANONICAL_USER_MAX) || (addr >= CANONICAL_KERNEL_MIN);
}

int vmm_validate_range(vm_space_t *space, uint64_t virt_addr, size_t size,
                       uint32_t required_flags) {
    if (!vmm_is_canonical_addr(virt_addr) ||
        !vmm_is_canonical_addr(virt_addr + size - 1)) {
        return -1;  /* non-canonical address */
    }

    vm_area_t *area = vmm_find_area(space, virt_addr);
    if (area == NULL) {
        return -1;  /* no area found */
    }

    if (virt_addr + size > area->virt_end) {
        return -1;  /* Range extends beyond area */
    }

    if ((area->flags & required_flags) != required_flags) {
        return -1;  /* Insufficient permissions */
    }

    return 0;
}

static uint64_t vmm_find_free_area(vm_space_t *space, size_t size, int user) {
    uint64_t search_start, search_end;

    if (user) {
        /* User space: 0x0000000000001000 to 0x00007FFFFFFFFFFF */
        search_start = 0x0000000000001000UL;  /* Skip NULL page */
        search_end   = 0x00007FFFFFFFF000UL;
    } else {
        /* Kernel space: Start after HHDM region */
        extern uint64_t hhdm_offset;
        search_start = 0xFFFF900000000000UL;  /* Above typical HHDM */
        search_end   = 0xFFFFFFFF80000000UL;  /* Below kernel .text */
    }

    uint64_t aligned_size = ALIGN_UP(size, PAGE_SIZE);

    if (space->areas == NULL) {
        return search_start;
    }

    vm_area_t *area = space->areas;
    if (area->virt_start >= search_start + aligned_size) {
        return search_start;
    }

    while (area->next != NULL) {
        uint64_t gap_start = area->virt_end;
        uint64_t gap_end = area->next->virt_start;

        if (gap_end - gap_start >= aligned_size) {
            return gap_start;
        }

        area = area->next;
    }

    if (search_end - area->virt_end >= aligned_size) {
        return area->virt_end;
    }

    return 0;
}

static void vmm_insert_area(vm_space_t *space, vm_area_t *new_area) {
    if (space->areas == NULL) {
        space->areas = new_area;
        new_area->next = NULL;
        return;
    }

    if (new_area->virt_start < space->areas->virt_start) {
        new_area->next = space->areas;
        space->areas = new_area;
        return;
    }

    vm_area_t *prev = space->areas;
    while (prev->next != NULL && prev->next->virt_start < new_area->virt_start) {
        prev = prev->next;
    }

    new_area->next = prev->next;
    prev->next = new_area;
}

static void vmm_remove_area(vm_space_t *space, vm_area_t *area) {
    if (space->areas == area) {
        space->areas = area->next;
    } else {
        vm_area_t *prev = space->areas;
        while (prev != NULL && prev->next != area) {
            prev = prev->next;
        }

        if (prev != NULL) {
            prev->next = area->next;
        }
    }

    pmm_free_page(VIRT_TO_PHYS((uint64_t)area));
}

static int vmm_can_merge_areas(vm_area_t *a, vm_area_t *b) {
    if (a == NULL || b == NULL) {
        return 0;
    }

    if (a->virt_end != b->virt_start) {
        return 0;
    }

    if (a->flags != b->flags || a->type != b->type ||
        a->alloc_flags != b->alloc_flags) {
        return 0;
    }

    if (a->type == VMM_TYPE_PHYS) {
        uint64_t a_phys_end = a->phys_base + (a->virt_end - a->virt_start);
        if (a_phys_end != b->phys_base) {
            return 0;
        }
    }

    return 1;
}

static uint64_t vmm_flags_to_mmu(uint32_t vmm_flags) {
    uint64_t mmu_flags = 0;

    if (vmm_flags & VMM_WRITE) mmu_flags |= MMU_MAP_WRITE;
    if (vmm_flags & VMM_USER) mmu_flags |= MMU_MAP_USER;
    if (vmm_flags & VMM_NOCACHE) mmu_flags |= MMU_MAP_NOCACHE;

    if (!(vmm_flags & VMM_EXEC)) {
        /* Most architectures use NX bit, but it's defined in arch/x86_64/mmu.h */
        /* We'll assume the MMU layer handles this via the EXEC flag */
        mmu_flags |= MMU_MAP_EXEC;  /* Let MMU layer invert this to NX */
    }

    return mmu_flags;
}

void vmm_init(void) {

    memset(&vmm_stats, 0, sizeof(vmm_stats));

    kernel_space.mmu_ctx = mmu_get_kernel_context();
    kernel_space.areas = NULL;
    kernel_space.total_size = 0;
    kernel_space.mapped_size = 0;
    kernel_space.area_count = 0;

    vmm_initialized = 1;
    printk_ts("vmm: initialized\n");
}

vm_space_t *vmm_get_kernel_space(void) {
    return &kernel_space;
}

vm_space_t *vmm_create_space(void) {
    uint64_t phys = pmm_alloc_page();
    if (phys == 0) {
        return NULL;
    }

    vm_space_t *space = (vm_space_t *)PHYS_TO_VIRT(phys);
    memset(space, 0, sizeof(vm_space_t));

    space->mmu_ctx = mmu_create_context();
    if (space->mmu_ctx == NULL) {
        pmm_free_page(phys);
        return NULL;
    }

    space->areas = NULL;
    space->total_size = 0;
    space->mapped_size = 0;
    space->area_count = 0;

    return space;
}

void vmm_destroy_space(vm_space_t *space) {
    if (space == NULL || space == &kernel_space) {
        return;
    }

    vm_area_t *area = space->areas;
    while (area != NULL) {
        vm_area_t *next = area->next;

	if (area->type == VMM_TYPE_ANON) {
           uint64_t virt = area->virt_start;
            while (virt < area->virt_end) {
                if (mmu_is_mapped((mmu_context_t *)space->mmu_ctx, virt)) {
                    uint64_t phys = mmu_virt_to_phys((mmu_context_t *)space->mmu_ctx, virt);
                    if (phys != 0) {
                        pmm_free_page(phys);
                    }
                    mmu_unmap_page((mmu_context_t *)space->mmu_ctx, virt);
                }
                virt += PAGE_SIZE;
            }
        }

        pmm_free_page(VIRT_TO_PHYS((uint64_t)area));
        vmm_stats.regions_destroyed++;
        area = next;
    }

    mmu_destroy_context((mmu_context_t *)space->mmu_ctx);

    pmm_free_page(VIRT_TO_PHYS((uint64_t)space));
}

void vmm_switch_space(vm_space_t *space) {
    if (space == NULL) {
        return;
    }

    mmu_switch_context((mmu_context_t *)space->mmu_ctx);
}

static int vmm_map_region_internal(vm_space_t *space, uint64_t virt_addr, size_t size,
                                    uint32_t flags, uint32_t type, uint32_t alloc_flags,
                                    uint64_t phys_addr) {
    if (space == NULL) {
        space = &kernel_space;
    }

    if (!vmm_is_canonical_addr(virt_addr)) {
        printk("vmm: non-canonical address 0x%016lx\n", virt_addr);
        return -1;
    }

    uint64_t virt_start = ALIGN_DOWN(virt_addr, PAGE_SIZE);
    uint64_t virt_end = ALIGN_UP(virt_addr + size, PAGE_SIZE);
    size_t aligned_size = virt_end - virt_start;

    if (aligned_size == 0) {
        return -1;
    }

    vm_area_t *area = space->areas;
    while (area != NULL) {
        if (!(virt_end <= area->virt_start || virt_start >= area->virt_end)) {
            printk("vmm: Region 0x%016lx-0x%016lx overlaps existing region\n",
                   virt_start, virt_end);
            return -1;
        }
        area = area->next;
    }

    uint64_t area_phys = pmm_alloc_page();
    if (area_phys == 0) {
        return -1;
    }

    vm_area_t *new_area = (vm_area_t *)PHYS_TO_VIRT(area_phys);
    memset(new_area, 0, sizeof(vm_area_t));

    new_area->virt_start = virt_start;
    new_area->virt_end = virt_end;
    new_area->flags = flags;
    new_area->type = type;
    new_area->alloc_flags = alloc_flags;
    new_area->phys_base = phys_addr;
    new_area->next = NULL;

    uint64_t mmu_flags = vmm_flags_to_mmu(flags);

    if (type == VMM_TYPE_PHYS) {
        /* Physical mapping - map immediately */
        uint64_t phys = phys_addr;
        for (uint64_t virt = virt_start; virt < virt_end; virt += PAGE_SIZE) {
            if (mmu_map_page((mmu_context_t *)space->mmu_ctx, virt, phys, mmu_flags) != 0) {
                /* Cleanup on failure */
                for (uint64_t v = virt_start; v < virt; v += PAGE_SIZE) {
                    mmu_unmap_page((mmu_context_t *)space->mmu_ctx, v);
                }
                pmm_free_page(area_phys);
                return -1;
            }
            phys += PAGE_SIZE;
        }
        space->mapped_size += aligned_size;
        vmm_stats.eager_allocations += aligned_size / PAGE_SIZE;

    } else if (type == VMM_TYPE_ANON) {
        if (alloc_flags & VMM_ALLOC_LAZY) {
            /* Lazy allocation - don't map anything yet */
        } else if (alloc_flags & VMM_ALLOC_COW) {
            /* COW allocation - don't allocate, will be set up by fork */
        } else {
            /* Eager allocation - allocate and map now */
            for (uint64_t virt = virt_start; virt < virt_end; virt += PAGE_SIZE) {
                uint64_t phys = pmm_alloc_page();
                if (phys == 0) {
                    /* Cleanup on failure */
                    for (uint64_t v = virt_start; v < virt; v += PAGE_SIZE) {
                        uint64_t p = mmu_virt_to_phys((mmu_context_t *)space->mmu_ctx, v);
                        if (p != 0) {
                            pmm_free_page(p);
                        }
                        mmu_unmap_page((mmu_context_t *)space->mmu_ctx, v);
                    }
                    pmm_free_page(area_phys);
                    return -1;
                }

                /* Zero the page if requested (default) */
                if (alloc_flags & VMM_ALLOC_ZERO || !(alloc_flags & ~VMM_ALLOC_ZERO)) {
                    memset(PHYS_TO_VIRT(phys), 0, PAGE_SIZE);
                }

                if (mmu_map_page((mmu_context_t *)space->mmu_ctx, virt, phys, mmu_flags) != 0) {
                    pmm_free_page(phys);
                    /* Cleanup */
                    for (uint64_t v = virt_start; v < virt; v += PAGE_SIZE) {
                        uint64_t p = mmu_virt_to_phys((mmu_context_t *)space->mmu_ctx, v);
                        if (p != 0) {
                            pmm_free_page(p);
                        }
                        mmu_unmap_page((mmu_context_t *)space->mmu_ctx, v);
                    }
                    pmm_free_page(area_phys);
                    return -1;
                }
            }
            space->mapped_size += aligned_size;
            vmm_stats.eager_allocations += aligned_size / PAGE_SIZE;
        }
    }

    vmm_insert_area(space, new_area);
    space->total_size += aligned_size;
    space->area_count++;
    vmm_stats.regions_created++;

    return 0;
}

int vmm_map_region(vm_space_t *space, uint64_t virt_addr, size_t size,
                   uint32_t flags, uint32_t type, uint32_t alloc_flags,
                   uint64_t phys_addr) {
    spinlock_irq_acquire(&vmm_lock);
    int ret = vmm_map_region_internal(space, virt_addr, size, flags, type, alloc_flags, phys_addr);
    spinlock_irq_release(&vmm_lock);
    return ret;
}

static int vmm_unmap_region_internal(vm_space_t *space, uint64_t virt_addr, size_t size) {
    if (space == NULL) {
        space = &kernel_space;
    }

    uint64_t virt_start = ALIGN_DOWN(virt_addr, PAGE_SIZE);
    uint64_t virt_end = ALIGN_UP(virt_addr + size, PAGE_SIZE);

    vm_area_t *area = space->areas;

    while (area != NULL) {
        vm_area_t *next = area->next;

        if (!(virt_end <= area->virt_start || virt_start >= area->virt_end)) {
            if (virt_start == area->virt_start && virt_end == area->virt_end) {
                /* Exact match - remove entire area */
                /* Free physical pages for anonymous memory */
                if (area->type == VMM_TYPE_ANON) {
                    for (uint64_t virt = area->virt_start; virt < area->virt_end; virt += PAGE_SIZE) {
                        if (mmu_is_mapped((mmu_context_t *)space->mmu_ctx, virt)) {
                            uint64_t phys = mmu_virt_to_phys((mmu_context_t *)space->mmu_ctx, virt);
                            if (phys != 0) {
                                pmm_free_page(phys);
                            }
                        }
                    }
                }

                mmu_unmap_range((mmu_context_t *)space->mmu_ctx, area->virt_start,
                               area->virt_end - area->virt_start);

                space->total_size -= (area->virt_end - area->virt_start);
                space->area_count--;

                vmm_remove_area(space, area);
                vmm_stats.regions_destroyed++;

                return 0;
            } else {
                /* Partial overlap - would need splitting, not implemented in basic version */
                printk("vmm: Partial unmap not supported, use vmm_split_region first\n");
                return -1;
            }
        }

        area = next;
    }

    return -1;
}

int vmm_unmap_region(vm_space_t *space, uint64_t virt_addr, size_t size) {
    spinlock_irq_acquire(&vmm_lock);
    int ret = vmm_unmap_region_internal(space, virt_addr, size);
    spinlock_irq_release(&vmm_lock);
    return ret;
}

uint64_t vmm_alloc_region(vm_space_t *space, size_t size, uint32_t flags,
                          uint32_t alloc_flags) {
    spinlock_irq_acquire(&vmm_lock);
    
    if (space == NULL) {
        space = &kernel_space;
    }

    int user = (flags & VMM_USER) ? 1 : 0;
    uint64_t virt_addr = vmm_find_free_area(space, size, user);

    if (virt_addr == 0) {
        spinlock_irq_release(&vmm_lock);
        return 0;
    }

    if (vmm_map_region_internal(space, virt_addr, size, flags, VMM_TYPE_ANON,
                                 alloc_flags, 0) != 0) {
        spinlock_irq_release(&vmm_lock);
        return 0;
    }

    spinlock_irq_release(&vmm_lock);
    return virt_addr;
}

int vmm_free_region(vm_space_t *space, uint64_t virt_addr) {
    spinlock_irq_acquire(&vmm_lock);
    
    if (space == NULL) {
        space = &kernel_space;
    }

    vm_area_t *area = vmm_find_area(space, virt_addr);
    if (area == NULL || area->virt_start != virt_addr) {
        spinlock_irq_release(&vmm_lock);
        return -1;
    }

    size_t size = area->virt_end - area->virt_start;
    
    int ret = vmm_unmap_region_internal(space, virt_addr, size);
    
    spinlock_irq_release(&vmm_lock);
    return ret;
}

int vmm_handle_page_fault(vm_space_t *space, uint64_t virt_addr,
                          uint64_t error_code) {
    spinlock_irq_acquire(&vmm_lock);
    
    if (space == NULL) {
        space = &kernel_space;
    }

    vm_area_t *area = vmm_find_area(space, virt_addr);
    if (area == NULL) {
        printk("vmm: Page fault at 0x%016lx - no area found\n", virt_addr);
        spinlock_irq_release(&vmm_lock);
        return -1;  /* no area contains this address */
    }

    uint64_t page_addr = ALIGN_DOWN(virt_addr, PAGE_SIZE);
    int is_present = error_code & VMM_FAULT_PRESENT;
    int is_write = error_code & VMM_FAULT_WRITE;

    int already_mapped = mmu_is_mapped((mmu_context_t *)space->mmu_ctx, page_addr);

    if (is_present && is_write && already_mapped) {
        /* Check if this is a COW page */
        if (mmu_is_cow_page((mmu_context_t *)space->mmu_ctx, page_addr)) {
            /* Break COW - this allocates a new page and copies data */
            if (mmu_break_cow((mmu_context_t *)space->mmu_ctx, page_addr) != 0) {
                printk("vmm: failed to break COW at 0x%016lx\n", page_addr);
                spinlock_irq_release(&vmm_lock);
                return -1;
            }

            vmm_stats.cow_faults_handled++;
            vmm_stats.page_faults_handled++;
            spinlock_irq_release(&vmm_lock);
            return 0;
        }

        if (!(area->flags & VMM_WRITE)) {
            printk("vmm: Write to read-only region at 0x%016lx\n", page_addr);
            vmm_stats.protection_faults++;
            spinlock_irq_release(&vmm_lock);
            return -1;
        }
    }

    if (!already_mapped && area->type == VMM_TYPE_ANON &&
        (area->alloc_flags & VMM_ALLOC_LAZY)) {

        uint64_t phys = pmm_alloc_page();
        if (phys == 0) {
            printk("vmm: out of memory during lazy allocation!\n");
            spinlock_irq_release(&vmm_lock);
            return -1;
        }

        memset(PHYS_TO_VIRT(phys), 0, PAGE_SIZE);

        uint64_t mmu_flags = vmm_flags_to_mmu(area->flags);

        if (mmu_map_page((mmu_context_t *)space->mmu_ctx, page_addr, phys, mmu_flags) != 0) {
            pmm_free_page(phys);
            spinlock_irq_release(&vmm_lock);
            return -1;
        }

        space->mapped_size += PAGE_SIZE;
        vmm_stats.lazy_allocations++;
        vmm_stats.page_faults_handled++;

        spinlock_irq_release(&vmm_lock);
        return 0;
    }

    if (is_present || already_mapped) {
        if (is_write && !(area->flags & VMM_WRITE)) {
            printk("vmm: Write permission fault at 0x%016lx\n", page_addr);
        } else if (error_code & VMM_FAULT_EXEC && !(area->flags & VMM_EXEC)) {
            printk("vmm: Execute permission fault at 0x%016lx\n", page_addr);
        } else if (error_code & VMM_FAULT_USER && !(area->flags & VMM_USER)) {
            printk("vmm: User access fault at 0x%016lx\n", page_addr);
        } else {
            printk("vmm: Unknown permission fault at 0x%016lx\n", page_addr);
        }
        vmm_stats.protection_faults++;
        spinlock_irq_release(&vmm_lock);
        return -1;
    }

    printk("vmm: invalid page fault at 0x%016lx (error=0x%lx)\n",
           virt_addr, error_code);
    spinlock_irq_release(&vmm_lock);
    return -1;
}

int vmm_protect_region(vm_space_t *space, uint64_t virt_addr, size_t size,
                       uint32_t new_flags) {
    spinlock_irq_acquire(&vmm_lock);
    
    if (space == NULL) {
        space = &kernel_space;
    }

    uint64_t virt_start = ALIGN_DOWN(virt_addr, PAGE_SIZE);
    uint64_t virt_end = ALIGN_UP(virt_addr + size, PAGE_SIZE);

    vm_area_t *area = vmm_find_area(space, virt_start);
    if (area == NULL) {
        spinlock_irq_release(&vmm_lock);
        return -1;
    }

    if (virt_start < area->virt_start || virt_end > area->virt_end) {
        printk("vmm: Protection range crosses area boundaries\n");
        spinlock_irq_release(&vmm_lock);
        return -1;
    }

    area->flags = new_flags;

    uint64_t mmu_flags = vmm_flags_to_mmu(new_flags);

    if (mmu_change_flags_range((mmu_context_t *)space->mmu_ctx,
                                virt_start, virt_end - virt_start,
                                mmu_flags) != 0) {
        spinlock_irq_release(&vmm_lock);
        return -1;
    }

    spinlock_irq_release(&vmm_lock);
    return 0;
}

int vmm_split_region(vm_space_t *space, uint64_t split_addr) {
    spinlock_irq_acquire(&vmm_lock);
    
    if (space == NULL) {
        space = &kernel_space;
    }

    uint64_t split_page = ALIGN_DOWN(split_addr, PAGE_SIZE);

    vm_area_t *area = vmm_find_area(space, split_page);
    if (area == NULL) {
        spinlock_irq_release(&vmm_lock);
        return -1;
    }

    if (split_page == area->virt_start || split_page >= area->virt_end) {
        spinlock_irq_release(&vmm_lock);
        return -1;
    }

    uint64_t new_area_phys = pmm_alloc_page();
    if (new_area_phys == 0) {
        spinlock_irq_release(&vmm_lock);
        return -1;
    }

    vm_area_t *new_area = (vm_area_t *)PHYS_TO_VIRT(new_area_phys);
    memset(new_area, 0, sizeof(vm_area_t));

    new_area->virt_start = split_page;
    new_area->virt_end = area->virt_end;
    new_area->flags = area->flags;
    new_area->type = area->type;
    new_area->alloc_flags = area->alloc_flags;

    if (area->type == VMM_TYPE_PHYS) {
        uint64_t offset = split_page - area->virt_start;
        new_area->phys_base = area->phys_base + offset;
    }

    area->virt_end = split_page;

    new_area->next = area->next;
    area->next = new_area;

    space->area_count++;
    vmm_stats.regions_split++;

    spinlock_irq_release(&vmm_lock);
    return 0;
}

int vmm_merge_regions(vm_space_t *space) {
    spinlock_irq_acquire(&vmm_lock);
    
    if (space == NULL) {
        space = &kernel_space;
    }

    int merged_count = 0;
    vm_area_t *area = space->areas;

    while (area != NULL && area->next != NULL) {
        if (vmm_can_merge_areas(area, area->next)) {
            vm_area_t *next = area->next;

            area->virt_end = next->virt_end;

            area->next = next->next;

            pmm_free_page(VIRT_TO_PHYS((uint64_t)next));

            space->area_count--;
            vmm_stats.regions_merged++;
            merged_count++;

        } else {
            area = area->next;
        }
    }

    spinlock_irq_release(&vmm_lock);
    return merged_count;
}

int vmm_mark_cow_region(vm_space_t *space, uint64_t virt_addr, size_t size) {
    spinlock_irq_acquire(&vmm_lock);
    
    if (space == NULL) {
        space = &kernel_space;
    }

    uint64_t virt_start = ALIGN_DOWN(virt_addr, PAGE_SIZE);
    uint64_t virt_end = ALIGN_UP(virt_addr + size, PAGE_SIZE);

    vm_area_t *area = vmm_find_area(space, virt_start);
    if (area == NULL || virt_end > area->virt_end) {
        spinlock_irq_release(&vmm_lock);
        return -1;
    }

    if (mmu_mark_cow((mmu_context_t *)space->mmu_ctx, virt_start,
                     virt_end - virt_start) != 0) {
        spinlock_irq_release(&vmm_lock);
        return -1;
    }

    spinlock_irq_release(&vmm_lock);
    return 0;
}

vm_space_t *vmm_fork_space(vm_space_t *parent) {
    if (parent == NULL) {
        return NULL;
    }

    vm_space_t *child = vmm_create_space();
    if (child == NULL) {
        return NULL;
    }

    spinlock_irq_acquire(&vmm_lock);

    vm_area_t *parent_area = parent->areas;
    while (parent_area != NULL) {
        /* Allocate new area descriptor */
        uint64_t area_phys = pmm_alloc_page();
        if (area_phys == 0) {
            spinlock_irq_release(&vmm_lock);
            vmm_destroy_space(child);
            return NULL;
        }

        vm_area_t *child_area = (vm_area_t *)PHYS_TO_VIRT(area_phys);
        memcpy(child_area, parent_area, sizeof(vm_area_t));
        child_area->next = NULL;

        vmm_insert_area(child, child_area);

        if (parent_area->type == VMM_TYPE_ANON) {
            /* Copy the page tables with COW flag using MMU layer */
            if (mmu_copy_range((mmu_context_t *)child->mmu_ctx,
                              (mmu_context_t *)parent->mmu_ctx,
                              parent_area->virt_start,
                              parent_area->virt_end - parent_area->virt_start,
                              true) != 0) {  /* true = use COW */
                spinlock_irq_release(&vmm_lock);
                vmm_destroy_space(child);
                return NULL;
            }
        } else if (parent_area->type == VMM_TYPE_PHYS) {
            /* Physical mappings are shared (e.g., MMIO) */
            uint64_t mmu_flags = vmm_flags_to_mmu(parent_area->flags);
            uint64_t phys = parent_area->phys_base;

            for (uint64_t virt = parent_area->virt_start;
                 virt < parent_area->virt_end;
                 virt += PAGE_SIZE) {
                mmu_map_page((mmu_context_t *)child->mmu_ctx, virt, phys, mmu_flags);
                phys += PAGE_SIZE;
            }
        }

        child->total_size += parent_area->virt_end - parent_area->virt_start;
        child->area_count++;

        parent_area = parent_area->next;
    }

    spinlock_irq_release(&vmm_lock);
    return child;
}

vm_area_t *vmm_find_area(vm_space_t *space, uint64_t virt_addr) {
    /* Note: This is called from within locked functions, so don't lock here */
    if (space == NULL) {
        space = &kernel_space;
    }

    vm_area_t *area = space->areas;
    while (area != NULL) {
        if (virt_addr >= area->virt_start && virt_addr < area->virt_end) {
            return area;
        }
        area = area->next;
    }

    return NULL;
}

int vmm_is_mapped(vm_space_t *space, uint64_t virt_addr) {
    if (space == NULL) {
        space = &kernel_space;
    }

    return mmu_is_mapped((mmu_context_t *)space->mmu_ctx, virt_addr);
}

void vmm_print_stats(vm_space_t *space) {
    spinlock_irq_acquire(&vmm_lock);
    
    if (space == NULL) {
        space = &kernel_space;
    }

    printk("vmm statistics:\n");
    printk("address space:\n");
    printk("  total reserved:   %lu MB (%lu pages)\n",
           space->total_size / (1024 * 1024),
           space->total_size / PAGE_SIZE);
    printk("  actually mapped:  %lu MB (%lu pages)\n",
           space->mapped_size / (1024 * 1024),
           space->mapped_size / PAGE_SIZE);
    printk("  region count:     %lu\n", space->area_count);

    printk("\nglobal statistics:\n");
    printk("  eager allocations:  %lu pages\n", vmm_stats.eager_allocations);
    printk("  lazy allocations:   %lu pages\n", vmm_stats.lazy_allocations);
    printk("  Page faults handled: %lu\n", vmm_stats.page_faults_handled);
    printk("  cow faults handled: %lu\n", vmm_stats.cow_faults_handled);
    printk("  protection faults:  %lu\n", vmm_stats.protection_faults);
    printk("  regions created:    %lu\n", vmm_stats.regions_created);
    printk("  regions destroyed:  %lu\n", vmm_stats.regions_destroyed);
    printk("  regions split:      %lu\n", vmm_stats.regions_split);
    printk("  regions merged:     %lu\n", vmm_stats.regions_merged);
    
    spinlock_irq_release(&vmm_lock);
}

void vmm_print_regions(vm_space_t *space) {
    spinlock_irq_acquire(&vmm_lock);
    
    if (space == NULL) {
        space = &kernel_space;
    }

    const char *type_names[] = {"ANON", "PHYS", "FILE", "SHARED"};

    printk("vmm regions:\n");
    printk("%-18s %-18s %-10s %-8s %s\n",
           "Start", "End", "Size", "Type", "Flags");

    vm_area_t *area = space->areas;
    while (area != NULL) {
        size_t size = area->virt_end - area->virt_start;

        char flags_str[8];
        flags_str[0] = (area->flags & VMM_READ) ? 'R' : '-';
        flags_str[1] = (area->flags & VMM_WRITE) ? 'W' : '-';
        flags_str[2] = (area->flags & VMM_EXEC) ? 'X' : '-';
        flags_str[3] = (area->flags & VMM_USER) ? 'U' : '-';
        flags_str[4] = (area->alloc_flags & VMM_ALLOC_LAZY) ? 'L' : '-';
        flags_str[5] = (area->alloc_flags & VMM_ALLOC_COW) ? 'C' : '-';
        flags_str[6] = '\0';

        printk("0x%016lx 0x%016lx %8lu KB %-8s %s\n",
               area->virt_start,
               area->virt_end,
               size / 1024,
               type_names[area->type],
               flags_str);

        area = area->next;
    }

    printk("flags: R=Read, W=Write, X=Execute, U=User, L=Lazy, C=COW\n");
    
    spinlock_irq_release(&vmm_lock);
}
