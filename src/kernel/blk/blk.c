#include <blk/blk.h>

#include <core/spinlock.h>

#include <video/printk.h>

#include <klibc/string.h>
#include <errno.h>

static struct blk_device *blk_device_list = NULL;

static spinlock_irq_t blk_list_lock = SPINLOCK_IRQ_INIT;

void blk_init(void) {
    blk_device_list = NULL;
    spinlock_irq_init(&blk_list_lock);
    printk_ts("blk: block device layer initialized\n");
}

int blk_register_device(struct blk_device *dev) {
    struct blk_device *existing;
    
    if (!dev || !dev->ops) {
        return -EINVAL;
    }

    if (!dev->ops->request) {
        return -EINVAL;
    }

    existing = blk_find_device(dev->major, dev->minor);
    if (existing) {
        return -EEXIST;
    }

    dev->open_count = 0;
    
    spinlock_irq_acquire(&blk_list_lock);
    dev->next = blk_device_list;
    blk_device_list = dev;
    spinlock_irq_release(&blk_list_lock);

    printk_ts("blk: registered %s (major=%u, minor=%u, blocks=%llu, size=%u)\n",
           dev->name, dev->major, dev->minor, dev->num_blocks, dev->block_size);

    return 0;
}

void blk_unregister_device(struct blk_device *dev) {
    struct blk_device **current;
    
    if (!dev) {
        return;
    }

    if (dev->open_count > 0) {
        printk("blk: cannot unregister %s, still open (%u refs)\n",
               dev->name, dev->open_count);
        return;
    }

    spinlock_irq_acquire(&blk_list_lock);
    current = &blk_device_list;
    while (*current) {
        if (*current == dev) {
            *current = dev->next;
            spinlock_irq_release(&blk_list_lock);
            printk("blk: unregistered %s\n", dev->name);
            return;
        }
        current = &(*current)->next;
    }
    spinlock_irq_release(&blk_list_lock);
}

struct blk_device *blk_find_device(uint16_t major, uint16_t minor) {
    struct blk_device *dev;
    
    spinlock_irq_acquire(&blk_list_lock);
    dev = blk_device_list;
    while (dev) {
        if (dev->major == major && dev->minor == minor) {
            spinlock_irq_release(&blk_list_lock);
            return dev;
        }
        dev = dev->next;
    }
    spinlock_irq_release(&blk_list_lock);
    
    return NULL;
}

struct blk_device *blk_find_device_by_name(const char *name) {
    struct blk_device *dev;
    
    if (!name) {
        return NULL;
    }

    spinlock_irq_acquire(&blk_list_lock);
    dev = blk_device_list;
    while (dev) {
        if (strcmp(dev->name, name) == 0) {
            spinlock_irq_release(&blk_list_lock);
            return dev;
        }
        dev = dev->next;
    }
    spinlock_irq_release(&blk_list_lock);
    
    return NULL;
}

int blk_open(struct blk_device *dev) {
    int ret = 0;
    
    if (!dev) {
        return -ENODEV;
    }

    if (dev->open_count == 0 && dev->ops->open) {
        ret = dev->ops->open(dev);
        if (ret != 0) {
            return ret;
        }
    }

    dev->open_count++;
    return 0;
}

void blk_close(struct blk_device *dev) {
    if (!dev || dev->open_count == 0) {
        return;
    }

    dev->open_count--;

    if (dev->open_count == 0 && dev->ops->close) {
        dev->ops->close(dev);
    }
}

int blk_read(struct blk_device *dev, uint64_t sector, uint32_t count, void *buffer) {
    struct blk_request req;
    
    if (!dev || !buffer) {
        return -EINVAL;
    }

    if (dev->open_count == 0) {
        return -ENODEV;
    }

    if (sector + count > dev->num_blocks) {
        return -EINVAL;
    }

    req.dev = dev;
    req.operation = BLK_OP_READ;
    req.flags = 0;
    req.sector = sector;
    req.count = count;
    req.buffer = buffer;
    req.status = 0;
    req.private_data = NULL;

    return dev->ops->request(dev, &req);
}

int blk_write(struct blk_device *dev, uint64_t sector, uint32_t count, const void *buffer) {
    struct blk_request req;
    
    if (!dev || !buffer) {
        return -EINVAL;
    }

    if (dev->open_count == 0) {
        return -ENODEV;
    }

    if (dev->flags & BLK_FLAG_READ_ONLY) {
        return -EROFS;
    }

    if (sector + count > dev->num_blocks) {
        return -EINVAL;
    }

    req.dev = dev;
    req.operation = BLK_OP_WRITE;
    req.flags = 0;
    req.sector = sector;
    req.count = count;
    req.buffer = (void *)buffer;
    req.status = 0;
    req.private_data = NULL;

    return dev->ops->request(dev, &req);
}

int blk_flush(struct blk_device *dev) {
    struct blk_request req;
    
    if (!dev) {
        return -EINVAL;
    }

    if (dev->open_count == 0) {
        return -ENODEV;
    }

    if (dev->ops->flush) {
        return dev->ops->flush(dev);
    }

    req.dev = dev;
    req.operation = BLK_OP_FLUSH;
    req.flags = BLK_REQ_SYNC;
    req.sector = 0;
    req.count = 0;
    req.buffer = NULL;
    req.status = 0;
    req.private_data = NULL;

    return dev->ops->request(dev, &req);
}

int blk_ioctl(struct blk_device *dev, unsigned int cmd, unsigned long arg) {
    if (!dev) {
        return -EINVAL;
    }

    if (dev->open_count == 0) {
        return -ENODEV;
    }

    if (dev->ops->ioctl) {
        return dev->ops->ioctl(dev, cmd, arg);
    }

    return -EINVAL;
}

int blk_for_each_device(int (*fn)(struct blk_device *dev, void *data), void *data) {
    if (!fn)
        return -EINVAL;

    spinlock_irq_acquire(&blk_list_lock);
    struct blk_device *dev = blk_device_list;
    while (dev) {
        struct blk_device *next = dev->next; /* snapshot before unlock */
        spinlock_irq_release(&blk_list_lock);

        int ret = fn(dev, data);
        if (ret != 0)
            return ret;

        spinlock_irq_acquire(&blk_list_lock);
        dev = next;
    }
    spinlock_irq_release(&blk_list_lock);
    return 0;
}