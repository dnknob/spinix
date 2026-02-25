#ifndef _BCACHE_H
#define _BCACHE_H

#include <klibc/types.h>





struct blk_device;

#define B_VALID     (1 << 0)  /* Buffer contains valid data */
#define B_DIRTY     (1 << 1)  /* Buffer needs to be written to disk */
#define B_BUSY      (1 << 2)  /* Buffer is being used */

#define BSIZE       4096

struct buf {
    uint32_t flags;              /* Buffer flags (B_VALID, B_DIRTY, etc.) */
    struct blk_device *dev;      /* Block device */
    uint64_t blockno;            /* Block number on device */
    uint32_t refcnt;             /* Reference count */
    uint32_t pincount;           /* Pin count (prevents eviction) */
    
    struct buf *prev;            /* Previous buffer in LRU list */
    struct buf *next;            /* Next buffer in LRU list */
    struct buf *qnext;           /* Next buffer in hash chain */
    
    uint8_t *data;               /* Actual cached data */
    
    void *lock_data;             /* Opaque lock data (spinlock_irq_t) */
};

void bcache_init(void);

struct buf *bread(struct blk_device *dev, uint64_t blockno);
int bwrite(struct buf *buf, bool sync);

void brelse(struct buf *buf);

void bpin(struct buf *buf);
void bunpin(struct buf *buf);

int bsync(struct blk_device *dev);

int bcache_flush(void);

void bcache_invalidate(struct blk_device *dev);

void bcache_stats(uint64_t *hits, uint64_t *misses, uint64_t *evictions);

void bcache_print_stats(void);

#endif
