#ifndef _DRIVERS_BLK_H
#define _DRIVERS_BLK_H

#include <klibc/types.h>





struct blk_device;
struct blk_request;

#define BLK_FLAG_REMOVABLE  (1 << 0)  /* Removable media */
#define BLK_FLAG_READ_ONLY  (1 << 1)  /* Read-only device */
#define BLK_FLAG_NO_PART    (1 << 2)  /* No partition support */

#define BLK_OP_READ         0
#define BLK_OP_WRITE        1
#define BLK_OP_FLUSH        2
#define BLK_OP_DISCARD      3

#define BLK_REQ_SYNC        (1 << 0)  /* Synchronous request */
#define BLK_REQ_URGENT      (1 << 1)  /* Urgent/high priority */

struct blk_request {
    struct blk_device *dev;
    uint8_t operation;
    uint16_t flags;
    uint64_t sector;
    uint32_t count;
    void *buffer;
    int status;
    void *private_data;
};

struct blk_ops {
    int (*open)(struct blk_device *dev);
    void (*close)(struct blk_device *dev);
    int (*request)(struct blk_device *dev, struct blk_request *req);
    int (*ioctl)(struct blk_device *dev, unsigned int cmd, unsigned long arg);
    int (*flush)(struct blk_device *dev);
};

struct blk_device {
    char name[32];
    uint16_t major;
    uint16_t minor;
    uint32_t flags;
    uint32_t block_size;
    uint64_t num_blocks;
    const struct blk_ops *ops;
    uint32_t open_count;
    void *private_data;
    struct blk_device *next;
};

void blk_init(void);

int blk_register_device(struct blk_device *dev);
void blk_unregister_device(struct blk_device *dev);

struct blk_device *blk_find_device(uint16_t major, uint16_t minor);
struct blk_device *blk_find_device_by_name(const char *name);

int blk_open(struct blk_device *dev);
void blk_close(struct blk_device *dev);

int blk_read(struct blk_device *dev, uint64_t sector, uint32_t count, void *buffer);
int blk_write(struct blk_device *dev, uint64_t sector, uint32_t count, const void *buffer);

int blk_flush(struct blk_device *dev);
int blk_ioctl(struct blk_device *dev, unsigned int cmd, unsigned long arg);

int blk_for_each_device(int (*fn)(struct blk_device *dev, void *data), void *data);

static inline uint64_t blk_sector_to_bytes(struct blk_device *dev, uint64_t sector) {
    return sector * dev->block_size;
}

static inline uint64_t blk_bytes_to_sector(struct blk_device *dev, uint64_t bytes) {
    return bytes / dev->block_size;
}

static inline uint64_t blk_get_capacity(struct blk_device *dev) {
    return dev->num_blocks * dev->block_size;
}

#endif
