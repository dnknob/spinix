#include <core/spinlock.h>

#include <blk/blk.h>

#include <mm/heap.h>

#include <video/printk.h>

#include <klibc/string.h>
#include <errno.h>

struct buf;

static struct buf *bcache_lookup(struct blk_device *dev, uint64_t blockno);
static void lru_move_front(struct buf *b);
static struct buf *bcache_evict(void);
void brelse(struct buf *buf);

#define B_VALID     (1 << 0)
#define B_DIRTY     (1 << 1)
#define B_BUSY      (1 << 2)

#define BSIZE       4096

struct buf {
    uint32_t flags;
    struct blk_device *dev;
    uint64_t blockno;
    uint32_t refcnt;
    uint32_t pincount;
    
    struct buf *prev;
    struct buf *next;
    struct buf *qnext;
    
    uint8_t *data;
    void *lock_data;
};

#define NBUF            256     /* Number of buffers */
#define HASH_SIZE       64      /* Hash table size (power of 2) */
#define HASH_MASK       (HASH_SIZE - 1)

struct {
    spinlock_irq_t lock;        /* Global cache lock */
    struct buf buf[NBUF];       /* Buffer pool */
    
    struct buf *hash[HASH_SIZE];
    
    struct buf head;            /* LRU list head */
    
    uint64_t hits;              /* Cache hits */
    uint64_t misses;            /* Cache misses */
    uint64_t evictions;         /* Buffer evictions */
    uint64_t writes;            /* Disk writes */
    uint64_t reads;             /* Disk reads */
} bcache;

static inline uint32_t hash_block(struct blk_device *dev, uint64_t blockno)
{
    /* Simple hash: XOR device pointer with block number */
    uint64_t h = ((uint64_t)dev >> 4) ^ blockno;
    return (uint32_t)(h & HASH_MASK);
}

void bcache_init(void)
{
    spinlock_irq_init(&bcache.lock);
    
    bcache.head.prev = &bcache.head;
    bcache.head.next = &bcache.head;
    
    for (int i = 0; i < NBUF; i++) {
        struct buf *b = &bcache.buf[i];
        
        b->data = kmalloc(BSIZE);
        if (b->data == NULL) {
            printk("bcache: failed to allocate buffer %d\n", i);
            continue;
        }
        
        b->lock_data = kmalloc(sizeof(spinlock_irq_t));
        if (b->lock_data == NULL) {
            kfree(b->data);
            printk("bcache: failed to allocate lock for buffer %d\n", i);
            continue;
        }
        spinlock_irq_init((spinlock_irq_t *)b->lock_data);
        
        b->flags = 0;
        b->dev = NULL;
        b->blockno = 0;
        b->refcnt = 0;
        b->pincount = 0;
        b->qnext = NULL;
        
        b->next = bcache.head.next;
        b->prev = &bcache.head;
        bcache.head.next->prev = b;
        bcache.head.next = b;
    }
    
    for (int i = 0; i < HASH_SIZE; i++) {
        bcache.hash[i] = NULL;
    }
    
    bcache.hits = 0;
    bcache.misses = 0;
    bcache.evictions = 0;
    bcache.writes = 0;
    bcache.reads = 0;
    
    printk_ts("bcache: initialized with %d buffers (%d KB total)\n",
           NBUF, (NBUF * BSIZE) / 1024);
}

static struct buf *bcache_lookup(struct blk_device *dev, uint64_t blockno)
{
    uint32_t h = hash_block(dev, blockno);
    
    for (struct buf *b = bcache.hash[h]; b != NULL; b = b->qnext) {
        if (b->dev == dev && b->blockno == blockno) {
            return b;
        }
    }
    
    return NULL;
}

static void lru_move_front(struct buf *b)
{
    /* Remove from current position */
    b->prev->next = b->next;
    b->next->prev = b->prev;
    
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
}

static struct buf *bcache_evict(void)
{
    /* Scan LRU list from back (least recently used) */
    for (struct buf *b = bcache.head.prev; b != &bcache.head; b = b->prev) {
        if (b->refcnt == 0 && b->pincount == 0) {
            /* Found a buffer we can evict */
            
            if (b->flags & B_DIRTY) {
                /* Temporarily acquire buffer lock for writing */
                spinlock_irq_acquire((spinlock_irq_t *)b->lock_data);
                
                spinlock_irq_release(&bcache.lock);
                
                if (b->dev != NULL) {
                    uint64_t sector = (b->blockno * BSIZE) / b->dev->block_size;
                    uint32_t count = BSIZE / b->dev->block_size;
                    blk_write(b->dev, sector, count, b->data);
                    bcache.writes++;
                }
                
                b->flags &= ~B_DIRTY;
                
                spinlock_irq_acquire(&bcache.lock);
                spinlock_irq_release((spinlock_irq_t *)b->lock_data);
            }
            
            if (b->dev != NULL) {
                uint32_t h = hash_block(b->dev, b->blockno);
                struct buf **pp;
                for (pp = &bcache.hash[h]; *pp != NULL; pp = &(*pp)->qnext) {
                    if (*pp == b) {
                        *pp = b->qnext;
                        break;
                    }
                }
            }
            
            b->dev = NULL;
            b->blockno = 0;
            b->flags = 0;
            b->qnext = NULL;
            
            bcache.evictions++;
            return b;
        }
    }
    
    return NULL;  /* No evictable buffer found */
}

struct buf *bread(struct blk_device *dev, uint64_t blockno)
{
    if (dev == NULL) {
        return NULL;
    }
    
    spinlock_irq_acquire(&bcache.lock);
    
    struct buf *b = bcache_lookup(dev, blockno);
    
    if (b != NULL) {
        /* Cache hit! */
        b->refcnt++;
        lru_move_front(b);
        bcache.hits++;
        
        while (b->flags & B_BUSY) {
            // TODO: sleep instead
            spinlock_irq_release(&bcache.lock);
            spinlock_irq_acquire(&bcache.lock);
        }
        
        spinlock_irq_release(&bcache.lock);
        
        spinlock_irq_acquire((spinlock_irq_t *)b->lock_data);
        
        return b;
    }
    
    bcache.misses++;
    
    b = bcache_evict();
    if (b == NULL) {
        /* No buffer available - all are in use */
        spinlock_irq_release(&bcache.lock);
        printk("bcache: out of buffers!\n");
        return NULL;
    }
    
    b->dev = dev;
    b->blockno = blockno;
    b->flags = B_BUSY;
    b->refcnt = 1;
    
    uint32_t h = hash_block(dev, blockno);
    b->qnext = bcache.hash[h];
    bcache.hash[h] = b;
    
    lru_move_front(b);
    
    spinlock_irq_acquire((spinlock_irq_t *)b->lock_data);
    spinlock_irq_release(&bcache.lock);
    
    uint64_t sector = (blockno * BSIZE) / dev->block_size;
    uint32_t count = BSIZE / dev->block_size;
    int ret = blk_read(dev, sector, count, b->data);
    
    if (ret == 0) {
        b->flags |= B_VALID;
        b->flags &= ~B_BUSY;
        bcache.reads++;
    } else {
        /* Read failed */
        b->flags &= ~B_BUSY;
        spinlock_irq_release((spinlock_irq_t *)b->lock_data);
        brelse(b);
        return NULL;
    }
    
    return b;
}

int bwrite(struct buf *buf, bool sync)
{
    if (buf == NULL || buf->dev == NULL) {
        return -EINVAL;
    }
    
    buf->flags |= B_DIRTY;
    
    if (sync) {
        uint64_t sector = (buf->blockno * BSIZE) / buf->dev->block_size;
        uint32_t count = BSIZE / buf->dev->block_size;
        int ret = blk_write(buf->dev, sector, count, buf->data);
        
        if (ret == 0) {
            buf->flags &= ~B_DIRTY;
            bcache.writes++;
        }
        
        return ret;
    }
    
    return 0;
}

void brelse(struct buf *buf)
{
    if (buf == NULL) {
        return;
    }
    
    spinlock_irq_release((spinlock_irq_t *)buf->lock_data);
    
    spinlock_irq_acquire(&bcache.lock);
    
    buf->refcnt--;
    
    if (buf->refcnt == 0) {
        /* Remove from current position */
        buf->prev->next = buf->next;
        buf->next->prev = buf->prev;
        
        buf->prev = bcache.head.prev;
        buf->next = &bcache.head;
        bcache.head.prev->next = buf;
        bcache.head.prev = buf;
    }
    
    spinlock_irq_release(&bcache.lock);
}

void bpin(struct buf *buf)
{
    if (buf == NULL) {
        return;
    }
    
    spinlock_irq_acquire(&bcache.lock);
    buf->pincount++;
    spinlock_irq_release(&bcache.lock);
}

void bunpin(struct buf *buf)
{
    if (buf == NULL) {
        return;
    }
    
    spinlock_irq_acquire(&bcache.lock);
    if (buf->pincount > 0) {
        buf->pincount--;
    }
    spinlock_irq_release(&bcache.lock);
}

int bsync(struct blk_device *dev)
{
    int errors = 0;
    
    spinlock_irq_acquire(&bcache.lock);
    
    for (int i = 0; i < NBUF; i++) {
        struct buf *b = &bcache.buf[i];
        
        if (!(b->flags & B_DIRTY)) {
            continue;
        }
        if (dev != NULL && b->dev != dev) {
            continue;
        }
        if (b->dev == NULL) {
            continue;
        }
        
        spinlock_irq_acquire((spinlock_irq_t *)b->lock_data);
        
        b->refcnt++;
        
        spinlock_irq_release(&bcache.lock);
        
        uint64_t sector = (b->blockno * BSIZE) / b->dev->block_size;
        uint32_t count = BSIZE / b->dev->block_size;
        int ret = blk_write(b->dev, sector, count, b->data);
        
        if (ret == 0) {
            b->flags &= ~B_DIRTY;
            bcache.writes++;
        } else {
            errors++;
        }
        
        spinlock_irq_acquire(&bcache.lock);
        b->refcnt--;
        spinlock_irq_release((spinlock_irq_t *)b->lock_data);
    }
    
    spinlock_irq_release(&bcache.lock);
    
    return errors > 0 ? -EIO : 0;
}

int bcache_flush(void)
{
    return bsync(NULL);
}

void bcache_invalidate(struct blk_device *dev)
{
    if (dev == NULL) {
        return;
    }
    
    spinlock_irq_acquire(&bcache.lock);
    
    for (int i = 0; i < NBUF; i++) {
        struct buf *b = &bcache.buf[i];
        
        if (b->dev == dev) {
            /* Wait for buffer to be free */
            if (b->refcnt > 0 || b->pincount > 0) {
                continue;  /* Skip buffers in use */
            }
            
            if (b->flags & B_DIRTY) {
                spinlock_irq_acquire((spinlock_irq_t *)b->lock_data);
                spinlock_irq_release(&bcache.lock);
                
                uint64_t sector = (b->blockno * BSIZE) / b->dev->block_size;
                uint32_t count = BSIZE / b->dev->block_size;
                blk_write(b->dev, sector, count, b->data);
                
                spinlock_irq_acquire(&bcache.lock);
                spinlock_irq_release((spinlock_irq_t *)b->lock_data);
            }
            
            uint32_t h = hash_block(b->dev, b->blockno);
            struct buf **pp;
            for (pp = &bcache.hash[h]; *pp != NULL; pp = &(*pp)->qnext) {
                if (*pp == b) {
                    *pp = b->qnext;
                    break;
                }
            }
            
            b->dev = NULL;
            b->blockno = 0;
            b->flags = 0;
            b->qnext = NULL;
        }
    }
    
    spinlock_irq_release(&bcache.lock);
}

void bcache_stats(uint64_t *hits, uint64_t *misses, uint64_t *evictions)
{
    spinlock_irq_acquire(&bcache.lock);
    
    if (hits) *hits = bcache.hits;
    if (misses) *misses = bcache.misses;
    if (evictions) *evictions = bcache.evictions;
    
    spinlock_irq_release(&bcache.lock);
}

void bcache_print_stats(void)
{
    spinlock_irq_acquire(&bcache.lock);
    
    uint64_t total = bcache.hits + bcache.misses;
    uint64_t hit_rate = (total > 0) ? (bcache.hits * 100) / total : 0;
    
    printk("bcache statistics:\n");
    printk("  buffers:      %d x %d KB = %d KB total\n",
           NBUF, BSIZE / 1024, (NBUF * BSIZE) / 1024);
    printk("  cache hits:   %llu\n", bcache.hits);
    printk("  cache misses: %llu\n", bcache.misses);
    printk("  hit rate:     %llu%%\n", hit_rate);
    printk("  evictions:    %llu\n", bcache.evictions);
    printk("  disk reads:   %llu\n", bcache.reads);
    printk("  disk writes:  %llu\n", bcache.writes);
    
    int in_use = 0, pinned = 0, dirty = 0, valid = 0;
    for (int i = 0; i < NBUF; i++) {
        struct buf *b = &bcache.buf[i];
        if (b->refcnt > 0) in_use++;
        if (b->pincount > 0) pinned++;
        if (b->flags & B_DIRTY) dirty++;
        if (b->flags & B_VALID) valid++;
    }
    
    printk("  in use:       %d\n", in_use);
    printk("  pinned:       %d\n", pinned);
    printk("  dirty:        %d\n", dirty);
    printk("  valid:        %d\n", valid);
    
    spinlock_irq_release(&bcache.lock);
}
