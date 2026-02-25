#ifndef VMM_H
#define VMM_H

#include <klibc/types.h>

#define VMM_READ        (1 << 0)    /* Region is readable */
#define VMM_WRITE       (1 << 1)    /* Region is writable */
#define VMM_EXEC        (1 << 2)    /* Region is executable */
#define VMM_USER        (1 << 3)    /* Region is user-accessible */
#define VMM_NOCACHE     (1 << 4)    /* Region is not cached */

#define VMM_TYPE_ANON   0           /* Anonymous memory (RAM) */
#define VMM_TYPE_PHYS   1           /* Physical memory mapping (e.g., MMIO) */
#define VMM_TYPE_FILE   2           /* File-backed (future) */
#define VMM_TYPE_SHARED 3           /* Shared memory (future) */

#define VMM_ALLOC_LAZY  (1 << 0)    /* Don't allocate physical pages immediately */
#define VMM_ALLOC_ZERO  (1 << 1)    /* Zero pages on allocation (default for lazy) */
#define VMM_ALLOC_COW   (1 << 2)    /* Mark as copy-on-write */

#define VMM_FAULT_PRESENT   (1 << 0)    /* Page was present */
#define VMM_FAULT_WRITE     (1 << 1)    /* Was a write access */
#define VMM_FAULT_USER      (1 << 2)    /* User mode access */
#define VMM_FAULT_RESERVED  (1 << 3)    /* Reserved bit violation */
#define VMM_FAULT_EXEC      (1 << 4)    /* Instruction fetch */

typedef struct vm_area vm_area_t;
typedef struct vm_space vm_space_t;

struct vm_area {
    virt_addr_t virt_start;         /* Virtual address start (page-aligned) */
    virt_addr_t virt_end;           /* Virtual address end (page-aligned, exclusive) */

    uint32_t flags;                 /* VMM_READ | VMM_WRITE | VMM_EXEC | VMM_USER */
    uint32_t type;                  /* VMM_TYPE_* */

    uint32_t alloc_flags;           /* VMM_ALLOC_* flags */
    uint32_t _reserved;

    phys_addr_t phys_base;

    vm_area_t *next;
};

struct vm_space {
    void *mmu_ctx;                  /* Pointer to mmu_context_t */
    vm_area_t *areas;               /* Linked list of memory areas */

    uint64_t total_size;            /* Total virtual memory reserved (bytes) */
    uint64_t mapped_size;           /* Actually mapped memory (bytes) */
    uint64_t area_count;            /* Number of areas */
};

void vmm_init(void);

vm_space_t *vmm_get_kernel_space(void);
vm_space_t *vmm_create_space(void);
void vmm_destroy_space(vm_space_t *space);
void vmm_switch_space(vm_space_t *space);

int         vmm_map_region(vm_space_t *space, virt_addr_t virt_addr, size_t size,
                           uint32_t flags, uint32_t type, uint32_t alloc_flags,
                           phys_addr_t phys_addr);
int         vmm_unmap_region(vm_space_t *space, virt_addr_t virt_addr, size_t size);
virt_addr_t vmm_alloc_region(vm_space_t *space, size_t size, uint32_t flags,
                             uint32_t alloc_flags);
int         vmm_free_region(vm_space_t *space, virt_addr_t virt_addr);

int         vmm_handle_page_fault(vm_space_t *space, virt_addr_t virt_addr,
                                  uint64_t error_code);

vm_area_t  *vmm_find_area(vm_space_t *space, virt_addr_t virt_addr);
int         vmm_is_mapped(vm_space_t *space, virt_addr_t virt_addr);

void vmm_print_stats(vm_space_t *space);
void vmm_print_regions(vm_space_t *space);

int vmm_protect_region(vm_space_t *space, virt_addr_t virt_addr, size_t size,
                       uint32_t new_flags);
int vmm_split_region(vm_space_t *space, virt_addr_t split_addr);
int vmm_merge_regions(vm_space_t *space);

vm_space_t *vmm_fork_space(vm_space_t *parent);
int         vmm_mark_cow_region(vm_space_t *space, virt_addr_t virt_addr, size_t size);

int vmm_is_canonical_addr(virt_addr_t addr);
int vmm_validate_range(vm_space_t *space, virt_addr_t virt_addr, size_t size,
                       uint32_t required_flags);

#endif