#ifndef HEAP_H
#define HEAP_H

#include <klibc/types.h>

void heap_init(void);

void *kmalloc(size_t size);
void *kmalloc_aligned(size_t size, size_t align);

void kfree(void *ptr);

void heap_print_stats(void);

typedef struct heap_slab_cache heap_slab_cache_t;

heap_slab_cache_t *heap_create_slab_cache(const char *name, size_t obj_size, size_t align);
void *heap_slab_alloc(heap_slab_cache_t *cache);
void heap_slab_free(heap_slab_cache_t *cache, void *obj);
void heap_destroy_slab_cache(heap_slab_cache_t *cache);

#define HEAP_ZERO       (1 << 0)   /* Zero the allocated memory */
#define HEAP_ATOMIC     (1 << 1)   /* Cannot sleep/block */
#define HEAP_GUARD      (1 << 2)   /* Add guard bytes (debug) */

void *kmalloc_flags(size_t size, uint32_t flags);

typedef struct {
    /* General stats */
    uint64_t total_size;           /* Total heap size */
    uint64_t used_size;            /* Currently used */
    uint64_t peak_used;            /* Peak usage */
    uint64_t num_allocs;           /* Total allocations */
    uint64_t num_frees;            /* Total frees */
    
    uint64_t slab_allocs;          /* Allocations via slab */
    uint64_t slab_frees;           /* Frees via slab */
    uint64_t slab_hits;            /* Slab cache hits */
    uint64_t slab_misses;          /* Slab cache misses */
    
    uint64_t large_allocs;         /* Large allocations */
    uint64_t large_frees;          /* Large frees */
    uint64_t coalesce_ops;         /* Coalescing operations */
    uint64_t split_ops;            /* Block splits */
    
    uint64_t failed_allocs;        /* Failed allocations */
    uint64_t double_frees;         /* Detected double frees */
    uint64_t invalid_frees;        /* Invalid free attempts */
    uint64_t guard_violations;     /* Buffer overflows detected */
    
    uint32_t num_free_blocks;      /* Number of free blocks */
    uint32_t largest_free_block;   /* Largest contiguous free block */
    uint32_t fragmentation_pct;    /* Fragmentation percentage */
} heap_stats_t;

void heap_get_stats(heap_stats_t *stats);
void heap_print_detailed_stats(void);
void heap_print_slab_stats(void);

int heap_validate(void);           /* Check heap integrity */
void heap_dump_free_list(void);    /* Dump free blocks */
void heap_enable_guards(int enable);  /* Enable/disable guard bytes */

#endif
