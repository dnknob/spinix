#include <core/spinlock.h>

#include <mm/heap.h>
#include <mm/vmm.h>
#include <mm/pmm.h>

#include <video/printk.h>

#include <klibc/string.h>

#define HEAP_INITIAL_SIZE   (2 * 1024 * 1024)   /* 2 MB initial heap */
#define HEAP_EXPAND_SIZE    (1 * 1024 * 1024)   /* 1 MB expansion chunks */
#define HEAP_MAX_SIZE       (256 * 1024 * 1024) /* 256 MB max heap */

#define HEAP_MIN_BLOCK_SIZE 32

#define SLAB_MAX_SIZE       512    /* Objects larger than this use general allocator */
#define SLAB_MIN_SIZE       16     /* Minimum slab object size */

#define NUM_SIZE_CLASSES    10
static const size_t SIZE_CLASSES[NUM_SIZE_CLASSES] = {
    32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384
};

#define GUARD_MAGIC         0xDEADC0DE
#define GUARD_SIZE          8      /* Bytes before and after allocation */

#define HEAP_MAGIC_FREE     0xDEADBEEF
#define HEAP_MAGIC_USED     0xCAFEBABE
#define HEAP_MAGIC_SLAB     0xABCDEF01

typedef struct heap_block {
    uint32_t magic;                 /* Magic number for validation */
    uint32_t flags;                 /* Block flags */
    size_t size;                    /* Size of usable data area */
    struct heap_block *next;        /* Next block in free list */
    struct heap_block *prev;        /* Previous block in free list (for bidirectional) */
    struct heap_block *phys_prev;   /* Previous block in physical memory */
} heap_block_t;

typedef struct slab_obj {
    struct slab_obj *next;          /* Next free object */
} slab_obj_t;

typedef struct slab {
    struct slab *next;              /* Next slab in cache */
    void *mem;                      /* Slab memory */
    slab_obj_t *free_list;          /* Free objects in this slab */
    uint32_t num_free;              /* Number of free objects */
    uint32_t num_total;             /* Total objects */
} slab_t;

struct heap_slab_cache {
    char name[32];                  /* Cache name */
    size_t obj_size;                /* Object size */
    size_t align;                   /* Alignment */
    slab_t *slabs_partial;          /* Slabs with free objects */
    slab_t *slabs_full;             /* Slabs with no free objects */
    slab_t *slabs_empty;            /* Empty slabs */
    uint64_t num_allocs;            /* Total allocations */
    uint64_t num_frees;             /* Total frees */
    uint64_t num_slabs;             /* Number of slabs */
    spinlock_irq_t lock;                /* Per-cache lock */
};

static struct {
    heap_block_t *free_lists[NUM_SIZE_CLASSES];  /* Segregated free lists */
    heap_block_t *large_free_list;                /* For blocks > largest class */
    
    uint64_t heap_start;
    uint64_t heap_end;
    size_t total_size;
    size_t used_size;
    size_t peak_used;
    
    uint64_t num_allocs;
    uint64_t num_frees;
    uint64_t failed_allocs;
    uint64_t double_frees;
    uint64_t invalid_frees;
    uint64_t guard_violations;
    
    uint64_t slab_allocs;
    uint64_t slab_frees;
    uint64_t large_allocs;
    uint64_t large_frees;
    uint64_t coalesce_ops;
    uint64_t split_ops;
    
    int guards_enabled;
    int initialized;
} heap_state = {0};

#define ALIGN_UP(addr, align)   (((addr) + (align) - 1) & ~((align) - 1))
#define IS_ALIGNED(addr, align) (((addr) & ((align) - 1)) == 0)

static spinlock_irq_t heap_lock = SPINLOCK_IRQ_INIT;

static heap_slab_cache_t *slab_16 = NULL;
static heap_slab_cache_t *slab_32 = NULL;
static heap_slab_cache_t *slab_64 = NULL;
static heap_slab_cache_t *slab_128 = NULL;
static heap_slab_cache_t *slab_256 = NULL;
static heap_slab_cache_t *slab_512 = NULL;

#define SLAB_SIZE  (4 * PAGE_SIZE)  /* 16KB per slab */

static slab_t *slab_create(heap_slab_cache_t *cache) {
    /* Allocate memory for slab control structure */
    uint64_t slab_ctrl_phys = pmm_alloc_page();
    if (slab_ctrl_phys == 0) {
        return NULL;
    }
    
    slab_t *slab = (slab_t *)PHYS_TO_VIRT(slab_ctrl_phys);
    memset(slab, 0, sizeof(slab_t));
    
    uint64_t slab_mem = vmm_alloc_region(
        vmm_get_kernel_space(),
        SLAB_SIZE,
        VMM_READ | VMM_WRITE,
        0  /* Eager allocation */
    );
    
    if (slab_mem == 0) {
        pmm_free_page(slab_ctrl_phys);
        return NULL;
    }
    
    slab->mem = (void *)slab_mem;
    slab->next = NULL;
    
    size_t aligned_size = ALIGN_UP(cache->obj_size, cache->align);
    slab->num_total = SLAB_SIZE / aligned_size;
    slab->num_free = slab->num_total;
    
    slab->free_list = NULL;
    for (uint32_t i = 0; i < slab->num_total; i++) {
        slab_obj_t *obj = (slab_obj_t *)((uint8_t *)slab->mem + (i * aligned_size));
        obj->next = slab->free_list;
        slab->free_list = obj;
    }
    
    return slab;
}

heap_slab_cache_t *heap_create_slab_cache(const char *name, size_t obj_size, size_t align) {
    if (obj_size > SLAB_MAX_SIZE || obj_size < SLAB_MIN_SIZE) {
        printk("heap: invalid slab object size %lu\n", obj_size);
        return NULL;
    }
    
    uint64_t cache_phys = pmm_alloc_page();
    if (cache_phys == 0) {
        return NULL;
    }
    
    heap_slab_cache_t *cache = (heap_slab_cache_t *)PHYS_TO_VIRT(cache_phys);
    memset(cache, 0, sizeof(heap_slab_cache_t));
    
    strncpy(cache->name, name, sizeof(cache->name) - 1);
    cache->obj_size = obj_size;
    cache->align = (align == 0) ? sizeof(void *) : align;
    cache->lock.lock = 0;
    
    slab_t *initial_slab = slab_create(cache);
    if (initial_slab == NULL) {
        pmm_free_page(cache_phys);
        return NULL;
    }
    
    cache->slabs_partial = initial_slab;
    cache->num_slabs = 1;
    
    return cache;
}

void *heap_slab_alloc(heap_slab_cache_t *cache) {
    if (cache == NULL) {
        return NULL;
    }
    
    spinlock_irq_acquire(&cache->lock);
    
    slab_t *slab = cache->slabs_partial;
    
    if (slab == NULL) {
        slab = cache->slabs_empty;
        if (slab != NULL) {
            /* Move from empty to partial */
            cache->slabs_empty = slab->next;
            slab->next = cache->slabs_partial;
            cache->slabs_partial = slab;
        }
    }
    
    if (slab == NULL) {
        slab = slab_create(cache);
        if (slab == NULL) {
            spinlock_irq_release(&cache->lock);
            return NULL;
        }
        
        slab->next = cache->slabs_partial;
        cache->slabs_partial = slab;
        cache->num_slabs++;
    }
    
    slab_obj_t *obj = slab->free_list;
    if (obj == NULL) {
        spinlock_irq_release(&cache->lock);
        return NULL;  /* Shouldn't happen */
    }
    
    slab->free_list = obj->next;
    slab->num_free--;
    
    if (slab->num_free == 0) {
        cache->slabs_partial = slab->next;
        slab->next = cache->slabs_full;
        cache->slabs_full = slab;
    }
    
    cache->num_allocs++;
    
    spinlock_irq_release(&cache->lock);
    
    return (void *)obj;
}

void heap_slab_free(heap_slab_cache_t *cache, void *obj) {
    if (cache == NULL || obj == NULL) {
        return;
    }
    
    spinlock_irq_acquire(&cache->lock);
    
    slab_t *slab = cache->slabs_full;
    slab_t **list_ptr = &cache->slabs_full;
    
    while (slab != NULL) {
        if ((uint64_t)obj >= (uint64_t)slab->mem &&
            (uint64_t)obj < (uint64_t)slab->mem + SLAB_SIZE) {
            goto found;
        }
        list_ptr = &slab->next;
        slab = slab->next;
    }
    
    slab = cache->slabs_partial;
    list_ptr = &cache->slabs_partial;
    
    while (slab != NULL) {
        if ((uint64_t)obj >= (uint64_t)slab->mem &&
            (uint64_t)obj < (uint64_t)slab->mem + SLAB_SIZE) {
            goto found;
        }
        list_ptr = &slab->next;
        slab = slab->next;
    }
    
    spinlock_irq_release(&cache->lock);
    return;  /* Object not found */
    
found:
    /* Return object to slab's free list */
    slab_obj_t *sobj = (slab_obj_t *)obj;
    sobj->next = slab->free_list;
    slab->free_list = sobj;
    slab->num_free++;
    
    cache->num_frees++;
    
    if (slab->num_free == 1 && list_ptr == &cache->slabs_full) {
        /* Was full, now partial */
        *list_ptr = slab->next;
        slab->next = cache->slabs_partial;
        cache->slabs_partial = slab;
    } else if (slab->num_free == slab->num_total) {
        /* Slab is now empty */
        *list_ptr = slab->next;
        slab->next = cache->slabs_empty;
        cache->slabs_empty = slab;
    }
    
    spinlock_irq_release(&cache->lock);
}

static int get_size_class(size_t size) {
    for (int i = 0; i < NUM_SIZE_CLASSES; i++) {
        if (size <= SIZE_CLASSES[i]) {
            return i;
        }
    }
    return -1;  /* Too large for size classes */
}

static void add_to_free_list(heap_block_t *block) {
    int class_idx = get_size_class(block->size);
    
    if (class_idx >= 0) {
        /* Add to appropriate size class */
        block->next = heap_state.free_lists[class_idx];
        block->prev = NULL;
        
        if (heap_state.free_lists[class_idx] != NULL) {
            heap_state.free_lists[class_idx]->prev = block;
        }
        
        heap_state.free_lists[class_idx] = block;
    } else {
        /* Add to large block list */
        block->next = heap_state.large_free_list;
        block->prev = NULL;
        
        if (heap_state.large_free_list != NULL) {
            heap_state.large_free_list->prev = block;
        }
        
        heap_state.large_free_list = block;
    }
}

static void remove_from_free_list(heap_block_t *block) {
    int class_idx = get_size_class(block->size);
    
    if (class_idx >= 0) {
        /* Remove from size class list */
        if (block->prev != NULL) {
            block->prev->next = block->next;
        } else {
            heap_state.free_lists[class_idx] = block->next;
        }
        
        if (block->next != NULL) {
            block->next->prev = block->prev;
        }
    } else {
        /* Remove from large block list */
        if (block->prev != NULL) {
            block->prev->next = block->next;
        } else {
            heap_state.large_free_list = block->next;
        }
        
        if (block->next != NULL) {
            block->next->prev = block->prev;
        }
    }
    
    block->next = NULL;
    block->prev = NULL;
}

static void coalesce_blocks(heap_block_t *block) {
    /* Forward coalesce */
    uint8_t *block_end = (uint8_t *)(block + 1) + block->size;
    heap_block_t *next_block = (heap_block_t *)block_end;
    
    if ((uint64_t)next_block < heap_state.heap_end &&
        next_block->magic == HEAP_MAGIC_FREE) {
        
        block->size += sizeof(heap_block_t) + next_block->size;
        remove_from_free_list(next_block);
        heap_state.coalesce_ops++;
    }
    
    if (block->phys_prev != NULL && block->phys_prev->magic == HEAP_MAGIC_FREE) {
        heap_block_t *prev_block = block->phys_prev;
        
        prev_block->size += sizeof(heap_block_t) + block->size;
        remove_from_free_list(block);
        remove_from_free_list(prev_block);
        add_to_free_list(prev_block);
        heap_state.coalesce_ops++;
    }
}

static heap_block_t *split_block(heap_block_t *block, size_t size) {
    size_t remaining = block->size - size;
    
    if (remaining < sizeof(heap_block_t) + HEAP_MIN_BLOCK_SIZE) {
        return NULL;
    }
    
    heap_block_t *new_block = (heap_block_t *)((uint8_t *)(block + 1) + size);
    new_block->magic = HEAP_MAGIC_FREE;
    new_block->flags = 0;
    new_block->size = remaining - sizeof(heap_block_t);
    new_block->next = NULL;
    new_block->prev = NULL;
    new_block->phys_prev = block;
    
    block->size = size;
    
    add_to_free_list(new_block);
    heap_state.split_ops++;
    
    return new_block;
}

static int heap_expand(size_t min_size) {
    size_t expand_size = ALIGN_UP(min_size, HEAP_EXPAND_SIZE);
    
    if (heap_state.total_size + expand_size > HEAP_MAX_SIZE) {
        printk("heap: cannot expand beyond maximum size\n");
        return -1;
    }
    
    uint64_t new_region = vmm_alloc_region(
        vmm_get_kernel_space(),
        expand_size,
        VMM_READ | VMM_WRITE,
        VMM_ALLOC_ZERO
    );
    
    if (new_region == 0) {
        return -1;
    }
    
    spinlock_irq_acquire(&heap_lock);
    
    heap_block_t *new_block = (heap_block_t *)new_region;
    new_block->magic = HEAP_MAGIC_FREE;
    new_block->flags = 0;
    new_block->size = expand_size - sizeof(heap_block_t);
    new_block->next = NULL;
    new_block->prev = NULL;
    new_block->phys_prev = NULL;  /* First block in new region */
    
    add_to_free_list(new_block);
    
    heap_state.total_size += expand_size;
    
    if (new_region + expand_size > heap_state.heap_end) {
        heap_state.heap_end = new_region + expand_size;
    }
    
    spinlock_irq_release(&heap_lock);
    return 0;
}

void heap_init(void) {
    if (heap_state.initialized) {
        return;
    }
    
    
    memset(&heap_state, 0, sizeof(heap_state));
    
    for (int i = 0; i < NUM_SIZE_CLASSES; i++) {
        heap_state.free_lists[i] = NULL;
    }
    heap_state.large_free_list = NULL;
    
    uint64_t heap_region = vmm_alloc_region(
        vmm_get_kernel_space(),
        HEAP_INITIAL_SIZE,
        VMM_READ | VMM_WRITE,
        0  /* Eager allocation */
    );
    
    if (heap_region == 0) {
        printk("heap: failed to allocate initial heap!\n");
        for (;;) __asm__("hlt");
    }
    
    heap_state.heap_start = heap_region;
    heap_state.heap_end = heap_region + HEAP_INITIAL_SIZE;
    heap_state.total_size = HEAP_INITIAL_SIZE;
    
    heap_block_t *initial_block = (heap_block_t *)heap_region;
    initial_block->magic = HEAP_MAGIC_FREE;
    initial_block->flags = 0;
    initial_block->size = HEAP_INITIAL_SIZE - sizeof(heap_block_t);
    initial_block->next = NULL;
    initial_block->prev = NULL;
    initial_block->phys_prev = NULL;
    
    add_to_free_list(initial_block);
    
    slab_16 = heap_create_slab_cache("kmalloc-16", 16, 8);
    slab_32 = heap_create_slab_cache("kmalloc-32", 32, 8);
    slab_64 = heap_create_slab_cache("kmalloc-64", 64, 8);
    slab_128 = heap_create_slab_cache("kmalloc-128", 128, 8);
    slab_256 = heap_create_slab_cache("kmalloc-256", 256, 8);
    slab_512 = heap_create_slab_cache("kmalloc-512", 512, 8);
    
    heap_state.initialized = 1;
    heap_state.guards_enabled = 0;  /* Disabled by default for performance */
    
    printk_ts("heap: initialized\n");
}

void *kmalloc_flags(size_t size, uint32_t flags) {
    if (!heap_state.initialized) {
        return NULL;
    }
    
    if (size == 0) {
        return NULL;
    }
    
    void *ptr = NULL;
    
    if (size <= SLAB_MAX_SIZE) {
        heap_slab_cache_t *cache = NULL;
        
        if (size <= 16) cache = slab_16;
        else if (size <= 32) cache = slab_32;
        else if (size <= 64) cache = slab_64;
        else if (size <= 128) cache = slab_128;
        else if (size <= 256) cache = slab_256;
        else if (size <= 512) cache = slab_512;
        
        if (cache != NULL) {
            ptr = heap_slab_alloc(cache);
            if (ptr != NULL) {
                heap_state.slab_allocs++;
                heap_state.num_allocs++;
                heap_state.used_size += cache->obj_size;
                
                if (heap_state.used_size > heap_state.peak_used) {
                    heap_state.peak_used = heap_state.used_size;
                }
                
                if (flags & HEAP_ZERO) {
                    memset(ptr, 0, cache->obj_size);
                }
                
                return ptr;
            }
        }
    }
    
    return kmalloc_aligned(size, sizeof(void *));
}

void *kmalloc(size_t size) {
    return kmalloc_flags(size, 0);
}

void *kmalloc_aligned(size_t size, size_t align) {
    if (!heap_state.initialized) {
        return NULL;
    }
    
    if (size == 0) {
        return NULL;
    }
    
    if (size <= SLAB_MAX_SIZE && align <= 8) {
        void *ptr = kmalloc_flags(size, 0);
        if (ptr != NULL) {
            return ptr;
        }
    }
    
    spinlock_irq_acquire(&heap_lock);
    
    if (align == 0 || (align & (align - 1)) != 0) {
        align = sizeof(void *);
    }
    
    size_t aligned_size = ALIGN_UP(size, align);
    
    int class_idx = get_size_class(aligned_size);
    heap_block_t *block = NULL;
    
    if (class_idx >= 0) {
        /* Search exact size class first */
        block = heap_state.free_lists[class_idx];
        while (block != NULL) {
            if (block->magic != HEAP_MAGIC_FREE) {
                heap_state.invalid_frees++;
                spinlock_irq_release(&heap_lock);
                return NULL;
            }
            
            if (block->size >= aligned_size) {
                break;
            }
            block = block->next;
        }
        
        if (block == NULL) {
            for (int i = class_idx + 1; i < NUM_SIZE_CLASSES; i++) {
                block = heap_state.free_lists[i];
                if (block != NULL && block->size >= aligned_size) {
                    break;
                }
            }
        }
    }
    
    if (block == NULL) {
        block = heap_state.large_free_list;
        while (block != NULL) {
            if (block->size >= aligned_size) {
                break;
            }
            block = block->next;
        }
    }
    
    if (block == NULL) {
        spinlock_irq_release(&heap_lock);
        
        if (heap_expand(aligned_size + sizeof(heap_block_t)) != 0) {
            heap_state.failed_allocs++;
            return NULL;
        }
        
        return kmalloc_aligned(size, align);
    }
    
    remove_from_free_list(block);
    split_block(block, aligned_size);
    
    block->magic = HEAP_MAGIC_USED;
    
    heap_state.used_size += block->size;
    heap_state.num_allocs++;
    heap_state.large_allocs++;
    
    if (heap_state.used_size > heap_state.peak_used) {
        heap_state.peak_used = heap_state.used_size;
    }
    
    spinlock_irq_release(&heap_lock);
    
    return (void *)(block + 1);
}

void kfree(void *ptr) {
    if (!heap_state.initialized || ptr == NULL) {
        return;
    }

    heap_slab_cache_t *caches[] = {slab_16, slab_32, slab_64, slab_128, slab_256, slab_512};
    for (int i = 0; i < 6; i++) {
        if (caches[i] == NULL) continue;
    
        slab_t *slab = caches[i]->slabs_full;
        while (slab != NULL) {
            if ((uint64_t)ptr >= (uint64_t)slab->mem &&
                (uint64_t)ptr < (uint64_t)slab->mem + SLAB_SIZE) {
                heap_slab_free(caches[i], ptr);
                heap_state.slab_frees++;
                heap_state.num_frees++;
                return;
            }
            slab = slab->next;
        }
    
        slab = caches[i]->slabs_partial;
        while (slab != NULL) {
            if ((uint64_t)ptr >= (uint64_t)slab->mem &&
                (uint64_t)ptr < (uint64_t)slab->mem + SLAB_SIZE) {
                heap_slab_free(caches[i], ptr);
                heap_state.slab_frees++;
                heap_state.num_frees++;
                return;
            }
            slab = slab->next;
        }
    }
    
    spinlock_irq_acquire(&heap_lock);
    
    heap_block_t *block = ((heap_block_t *)ptr) - 1;
    
    if (block->magic != HEAP_MAGIC_USED) {
        if (block->magic == HEAP_MAGIC_FREE) {
            heap_state.double_frees++;
        } else {
            heap_state.invalid_frees++;
        }
        spinlock_irq_release(&heap_lock);
        return;
    }
    
    block->magic = HEAP_MAGIC_FREE;
    heap_state.used_size -= block->size;
    heap_state.num_frees++;
    heap_state.large_frees++;
    
    memset(ptr, 0, block->size);
    
    coalesce_blocks(block);
    add_to_free_list(block);
    
    spinlock_irq_release(&heap_lock);
}

void heap_print_stats(void) {
    if (!heap_state.initialized) {
        return;
    }
    
    printk("heap statistics:\n");
    printk("total size:         %8lu KB\n", heap_state.total_size / 1024);
    printk("used:        %8lu KB\n", heap_state.used_size / 1024);
    printk("peak:         %8lu KB\n", heap_state.peak_used / 1024);
    printk("free:        %8lu KB\n",
           (heap_state.total_size - heap_state.used_size) / 1024);
    printk("\n");
    printk("total allocs:       %8lu\n", heap_state.num_allocs);
    printk("    slab:      %8lu\n", heap_state.slab_allocs);
    printk("    large:     %8lu\n", heap_state.large_allocs);
    printk("total frees:        %8lu\n", heap_state.num_frees);
    printk("active: %8lu\n",
           heap_state.num_allocs - heap_state.num_frees);
    printk("\n");
    printk("failed allocs:      %8lu\n", heap_state.failed_allocs);
    printk("double frees:       %8lu\n", heap_state.double_frees);
    printk("invalid frees:      %8lu\n", heap_state.invalid_frees);
}

void heap_print_slab_stats(void) {
    printk("slab allocator statistics:\n");
    
    heap_slab_cache_t *caches[] = {slab_16, slab_32, slab_64, slab_128, slab_256, slab_512};
    
    for (int i = 0; i < 6; i++) {
        if (caches[i] != NULL) {
            printk("%s: allocs=%lu frees=%lu slabs=%lu\n",
                   caches[i]->name,
                   caches[i]->num_allocs,
                   caches[i]->num_frees,
                   caches[i]->num_slabs);
        }
    }
}

void heap_enable_guards(int enable) {
    heap_state.guards_enabled = enable;
}

int heap_validate(void) {
    /* TODO: Walk heap and validate integrity */
    return 0;
}

void heap_dump_free_list(void) {
    printk("free lists:\n");
    for (int i = 0; i < NUM_SIZE_CLASSES; i++) {
        int count = 0;
        heap_block_t *block = heap_state.free_lists[i];
        while (block != NULL) {
            count++;
            block = block->next;
        }
        if (count > 0) {
            printk("class %lu bytes: %d blocks\n", SIZE_CLASSES[i], count);
        }
    }
}

void heap_get_stats(heap_stats_t *stats) {
    if (stats == NULL) {
        return;
    }
    
    stats->total_size = heap_state.total_size;
    stats->used_size = heap_state.used_size;
    stats->peak_used = heap_state.peak_used;
    stats->num_allocs = heap_state.num_allocs;
    stats->num_frees = heap_state.num_frees;
    
    stats->slab_allocs = heap_state.slab_allocs;
    stats->slab_frees = heap_state.slab_frees;
    stats->large_allocs = heap_state.large_allocs;
    stats->large_frees = heap_state.large_frees;
    stats->coalesce_ops = heap_state.coalesce_ops;
    stats->split_ops = heap_state.split_ops;
    
    stats->failed_allocs = heap_state.failed_allocs;
    stats->double_frees = heap_state.double_frees;
    stats->invalid_frees = heap_state.invalid_frees;
    stats->guard_violations = heap_state.guard_violations;
    
    /* TODO: Implement proper fragmentation calculation */
    stats->fragmentation_pct = 0;
}

void heap_print_detailed_stats(void) {
    heap_print_stats();
    heap_print_slab_stats();
    heap_dump_free_list();
}
