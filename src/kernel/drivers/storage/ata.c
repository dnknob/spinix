#include <arch/x86_64/intr.h>
#include <arch/x86_64/io.h>

#include <drivers/storage/ata.h>

#include <blk/blk.h>

#include <core/spinlock.h>

#include <video/printk.h>

#include <klibc/string.h>
#include <errno.h>

static ata_channel_t ata_channels[2];

static ata_device_t ata_devices[ATA_MAX_DEVICES];

static struct blk_device ata_blk_devices[ATA_MAX_DEVICES];

static spinlock_irq_t ata_lock = SPINLOCK_IRQ_INIT;

static inline void ata_io_wait(ata_channel_t *chan) {
    inb(chan->ctrl + ATA_REG_ALTSTATUS);
    inb(chan->ctrl + ATA_REG_ALTSTATUS);
    inb(chan->ctrl + ATA_REG_ALTSTATUS);
    inb(chan->ctrl + ATA_REG_ALTSTATUS);
}

static inline uint8_t ata_status_read(ata_channel_t *chan) {
    return inb(chan->base + ATA_REG_STATUS);
}

static inline uint8_t ata_altstatus_read(ata_channel_t *chan) {
    return inb(chan->ctrl + ATA_REG_ALTSTATUS);
}

static int ata_wait_bsy(ata_channel_t *chan) {
    uint32_t timeout = 100000;
    
    while (timeout--) {
        uint8_t status = ata_altstatus_read(chan);
        if (!(status & ATA_SR_BSY)) {
            return 0;
        }
        io_wait();
    }
    
    return -ETIMEDOUT;
}

static int ata_wait_drq(ata_channel_t *chan) {
    uint32_t timeout = 100000;
    
    while (timeout--) {
        uint8_t status = ata_altstatus_read(chan);
        
        if (status & ATA_SR_ERR) {
            return -EIO;
        }
        
        if (status & ATA_SR_DRQ) {
            return 0;
        }
        
        io_wait();
    }
    
    return -ETIMEDOUT;
}

static int ata_wait_ready(ata_channel_t *chan) {
    int ret;
    
    ret = ata_wait_bsy(chan);
    if (ret != 0) {
        return ret;
    }
    
    uint8_t status = ata_altstatus_read(chan);
    if (status & ATA_SR_ERR) {
        return -EIO;
    }
    
    if (status & ATA_SR_DF) {
        return -EIO;
    }
    
    return 0;
}

static void ata_soft_reset(ata_channel_t *chan) {
    outb(chan->ctrl + ATA_REG_CONTROL, ATA_CTL_SRST);
    ata_io_wait(chan);
    outb(chan->ctrl + ATA_REG_CONTROL, 0);
    ata_io_wait(chan);
    ata_wait_bsy(chan);
}

static void ata_select_drive(ata_channel_t *chan, uint8_t drive) {
    outb(chan->base + ATA_REG_HDDEVSEL, 0xA0 | (drive << 4));
    ata_io_wait(chan);
}

static void ata_fixstring(char *str, size_t len) {
    size_t i;
    
    for (i = 0; i < len; i += 2) {
        char tmp = str[i];
        str[i] = str[i + 1];
        str[i + 1] = tmp;
    }
    
    for (i = len - 1; i > 0 && str[i] == ' '; i--) {
        str[i] = '\0';
    }
    str[len] = '\0';
}

static uint8_t ata_identify(uint8_t channel, uint8_t drive) {
    ata_channel_t *chan = &ata_channels[channel];
    ata_device_t *dev = &ata_devices[channel * 2 + drive];
    uint16_t identify_data[256];
    uint8_t status;
    int i;
    
    ata_select_drive(chan, drive);
    
    outb(chan->base + ATA_REG_SECCOUNT0, 0);
    outb(chan->base + ATA_REG_LBA0, 0);
    outb(chan->base + ATA_REG_LBA1, 0);
    outb(chan->base + ATA_REG_LBA2, 0);
    
    outb(chan->base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    ata_io_wait(chan);
    
    status = ata_status_read(chan);
    
    if (status == 0) {
        return ATA_TYPE_NONE;
    }
    
    if (ata_wait_bsy(chan) != 0) {
        return ATA_TYPE_NONE;
    }
    
    uint8_t lba1 = inb(chan->base + ATA_REG_LBA1);
    uint8_t lba2 = inb(chan->base + ATA_REG_LBA2);
    
    if (lba1 == 0x14 && lba2 == 0xEB) {
        /* ATAPI device - we don't support these yet */
        dev->type = ATA_TYPE_ATAPI;
        dev->present = false;
        return ATA_TYPE_ATAPI;
    }
    
    if (lba1 != 0 || lba2 != 0) {
        /* Not ATA */
        return ATA_TYPE_NONE;
    }
    
    status = ata_altstatus_read(chan);
    while (!(status & (ATA_SR_DRQ | ATA_SR_ERR))) {
        status = ata_altstatus_read(chan);
    }
    
    if (status & ATA_SR_ERR) {
        return ATA_TYPE_NONE;
    }
    
    for (i = 0; i < 256; i++) {
        identify_data[i] = inw(chan->base + ATA_REG_DATA);
    }
    
    dev->present = true;
    dev->channel = channel;
    dev->drive = drive;
    dev->type = ATA_TYPE_ATA;
    dev->signature = identify_data[ATA_IDENT_DEVICETYPE];
    dev->capabilities = identify_data[ATA_IDENT_CAPABILITIES];
    dev->command_sets = *(uint32_t *)(identify_data + ATA_IDENT_COMMANDSETS);
    
    if (dev->command_sets & (1 << 26)) {
        /* LBA48 supported */
        dev->size = *(uint64_t *)(identify_data + ATA_IDENT_MAX_LBA_EXT);
    } else {
        /* LBA28 only */
        dev->size = *(uint32_t *)(identify_data + ATA_IDENT_MAX_LBA);
    }
    
    memcpy(dev->model, identify_data + ATA_IDENT_MODEL, 40);
    ata_fixstring(dev->model, 40);
    
    memcpy(dev->serial, identify_data + ATA_IDENT_SERIAL, 20);
    ata_fixstring(dev->serial, 20);
    
    return ATA_TYPE_ATA;
}

static int ata_pio_read(ata_device_t *dev, uint64_t lba, uint32_t count, void *buffer) {
    ata_channel_t *chan = &ata_channels[dev->channel];
    uint16_t *buf = (uint16_t *)buffer;
    uint8_t cmd;
    bool lba48 = false;
    int ret;
    uint32_t i, j;
    
    if (lba >= 0x10000000 || count > 256) {
        if (!(dev->command_sets & (1 << 26))) {
            return -EINVAL;  /* LBA48 not supported */
        }
        lba48 = true;
        cmd = ATA_CMD_READ_PIO_EXT;
    } else {
        cmd = ATA_CMD_READ_PIO;
    }
    
    ata_select_drive(chan, dev->drive);
    
    ret = ata_wait_ready(chan);
    if (ret != 0) {
        return ret;
    }
    
    if (lba48) {
        /* LBA48 mode */
        outb(chan->base + ATA_REG_SECCOUNT0, (count >> 8) & 0xFF);
        outb(chan->base + ATA_REG_LBA0, (lba >> 24) & 0xFF);
        outb(chan->base + ATA_REG_LBA1, (lba >> 32) & 0xFF);
        outb(chan->base + ATA_REG_LBA2, (lba >> 40) & 0xFF);
        outb(chan->base + ATA_REG_SECCOUNT0, count & 0xFF);
        outb(chan->base + ATA_REG_LBA0, lba & 0xFF);
        outb(chan->base + ATA_REG_LBA1, (lba >> 8) & 0xFF);
        outb(chan->base + ATA_REG_LBA2, (lba >> 16) & 0xFF);
    } else {
        /* LBA28 mode */
        outb(chan->base + ATA_REG_HDDEVSEL, 0xE0 | (dev->drive << 4) | ((lba >> 24) & 0x0F));
        outb(chan->base + ATA_REG_SECCOUNT0, count);
        outb(chan->base + ATA_REG_LBA0, lba & 0xFF);
        outb(chan->base + ATA_REG_LBA1, (lba >> 8) & 0xFF);
        outb(chan->base + ATA_REG_LBA2, (lba >> 16) & 0xFF);
    }
    
    outb(chan->base + ATA_REG_COMMAND, cmd);
    
    for (i = 0; i < count; i++) {
        /* Wait for DRQ */
        ret = ata_wait_drq(chan);
        if (ret != 0) {
            return ret;
        }
        
        for (j = 0; j < 256; j++) {
            buf[i * 256 + j] = inw(chan->base + ATA_REG_DATA);
        }
        
        ata_io_wait(chan);
    }
    
    return 0;
}

static int ata_pio_write(ata_device_t *dev, uint64_t lba, uint32_t count, const void *buffer) {
    ata_channel_t *chan = &ata_channels[dev->channel];
    const uint16_t *buf = (const uint16_t *)buffer;
    uint8_t cmd;
    bool lba48 = false;
    int ret;
    uint32_t i, j;
    
    if (lba >= 0x10000000 || count > 256) {
        if (!(dev->command_sets & (1 << 26))) {
            return -EINVAL;  /* LBA48 not supported */
        }
        lba48 = true;
        cmd = ATA_CMD_WRITE_PIO_EXT;
    } else {
        cmd = ATA_CMD_WRITE_PIO;
    }
    
    ata_select_drive(chan, dev->drive);
    
    ret = ata_wait_ready(chan);
    if (ret != 0) {
        return ret;
    }
    
    if (lba48) {
        /* LBA48 mode */
        outb(chan->base + ATA_REG_SECCOUNT0, (count >> 8) & 0xFF);
        outb(chan->base + ATA_REG_LBA0, (lba >> 24) & 0xFF);
        outb(chan->base + ATA_REG_LBA1, (lba >> 32) & 0xFF);
        outb(chan->base + ATA_REG_LBA2, (lba >> 40) & 0xFF);
        outb(chan->base + ATA_REG_SECCOUNT0, count & 0xFF);
        outb(chan->base + ATA_REG_LBA0, lba & 0xFF);
        outb(chan->base + ATA_REG_LBA1, (lba >> 8) & 0xFF);
        outb(chan->base + ATA_REG_LBA2, (lba >> 16) & 0xFF);
    } else {
        /* LBA28 mode */
        outb(chan->base + ATA_REG_HDDEVSEL, 0xE0 | (dev->drive << 4) | ((lba >> 24) & 0x0F));
        outb(chan->base + ATA_REG_SECCOUNT0, count);
        outb(chan->base + ATA_REG_LBA0, lba & 0xFF);
        outb(chan->base + ATA_REG_LBA1, (lba >> 8) & 0xFF);
        outb(chan->base + ATA_REG_LBA2, (lba >> 16) & 0xFF);
    }
    
    outb(chan->base + ATA_REG_COMMAND, cmd);
    
    for (i = 0; i < count; i++) {
        /* Wait for DRQ */
        ret = ata_wait_drq(chan);
        if (ret != 0) {
            return ret;
        }
        
        for (j = 0; j < 256; j++) {
            outw(chan->base + ATA_REG_DATA, buf[i * 256 + j]);
        }
        
        ata_io_wait(chan);
    }
    
    ret = ata_wait_ready(chan);
    if (ret != 0) {
        return ret;
    }
    
    return 0;
}

static int ata_flush(ata_device_t *dev) {
    ata_channel_t *chan = &ata_channels[dev->channel];
    uint8_t cmd;
    int ret;
    
    ata_select_drive(chan, dev->drive);
    
    ret = ata_wait_ready(chan);
    if (ret != 0) {
        return ret;
    }
    
    if (dev->command_sets & (1 << 26)) {
        cmd = ATA_CMD_CACHE_FLUSH_EXT;
    } else {
        cmd = ATA_CMD_CACHE_FLUSH;
    }
    
    outb(chan->base + ATA_REG_COMMAND, cmd);
    
    ret = ata_wait_ready(chan);
    if (ret != 0) {
        return ret;
    }
    
    return 0;
}

static int ata_blk_open(struct blk_device *blk_dev) {
    /* Nothing special needed */
    return 0;
}

static void ata_blk_close(struct blk_device *blk_dev) {
    /* Nothing special needed */
}

static int ata_blk_request(struct blk_device *blk_dev, struct blk_request *req) {
    ata_device_t *dev = (ata_device_t *)blk_dev->private_data;
    int ret = 0;
    
    spinlock_irq_acquire(&ata_lock);
    
    switch (req->operation) {
        case BLK_OP_READ:
            ret = ata_pio_read(dev, req->sector, req->count, req->buffer);
            break;
            
        case BLK_OP_WRITE:
            ret = ata_pio_write(dev, req->sector, req->count, req->buffer);
            break;
            
        case BLK_OP_FLUSH:
            ret = ata_flush(dev);
            break;
            
        default:
            ret = -EINVAL;
            break;
    }
    
    spinlock_irq_release(&ata_lock);
    
    req->status = ret;
    return ret;
}

static int ata_blk_flush(struct blk_device *blk_dev) {
    ata_device_t *dev = (ata_device_t *)blk_dev->private_data;
    int ret;
    
    spinlock_irq_acquire(&ata_lock);
    ret = ata_flush(dev);
    spinlock_irq_release(&ata_lock);
    
    return ret;
}

static const struct blk_ops ata_blk_ops = {
    .open = ata_blk_open,
    .close = ata_blk_close,
    .request = ata_blk_request,
    .ioctl = NULL,
    .flush = ata_blk_flush,
};

static void ata_register_blk_device(uint8_t device) {
    ata_device_t *dev = &ata_devices[device];
    struct blk_device *blk_dev = &ata_blk_devices[device];
    
    if (!dev->present || dev->type != ATA_TYPE_ATA) {
        return;
    }
    
    if (dev->channel == 0 && dev->drive == 0) {
        snprintk(blk_dev->name, sizeof(blk_dev->name), "hda");
    } else if (dev->channel == 0 && dev->drive == 1) {
        snprintk(blk_dev->name, sizeof(blk_dev->name), "hdb");
    } else if (dev->channel == 1 && dev->drive == 0) {
        snprintk(blk_dev->name, sizeof(blk_dev->name), "hdc");
    } else {
        snprintk(blk_dev->name, sizeof(blk_dev->name), "hdd");
    }
    
    blk_dev->major = ATA_MAJOR;
    blk_dev->minor = device;
    blk_dev->flags = 0;
    blk_dev->block_size = 512;
    blk_dev->num_blocks = dev->size;
    blk_dev->ops = &ata_blk_ops;
    blk_dev->private_data = dev;
    
    if (blk_register_device(blk_dev) != 0) {
        printk("ata: failed to register %s\n", blk_dev->name);
        return;
    }
    
    printk("ata: %s - %s (%llu MB)\n",
           blk_dev->name,
           dev->model,
           (dev->size * 512) / (1024 * 1024));
}

static void ata_primary_irq(struct interrupt_frame *frame) {
    /* Read status to clear IRQ */
    ata_status_read(&ata_channels[0]);
    ata_channels[0].irq_invoked = true;
}

static void ata_secondary_irq(struct interrupt_frame *frame) {
    /* Read status to clear IRQ */
    ata_status_read(&ata_channels[1]);
    ata_channels[1].irq_invoked = true;
}

ata_device_t *ata_get_device(uint8_t device) {
    if (device >= ATA_MAX_DEVICES) {
        return NULL;
    }
    
    if (!ata_devices[device].present) {
        return NULL;
    }
    
    return &ata_devices[device];
}

int ata_read_sectors(uint8_t device, uint64_t lba, uint32_t count, void *buffer) {
    ata_device_t *dev = ata_get_device(device);
    int ret;
    
    if (!dev) {
        return -ENODEV;
    }
    
    spinlock_irq_acquire(&ata_lock);
    ret = ata_pio_read(dev, lba, count, buffer);
    spinlock_irq_release(&ata_lock);
    
    return ret;
}

int ata_write_sectors(uint8_t device, uint64_t lba, uint32_t count, const void *buffer) {
    ata_device_t *dev = ata_get_device(device);
    int ret;
    
    if (!dev) {
        return -ENODEV;
    }
    
    spinlock_irq_acquire(&ata_lock);
    ret = ata_pio_write(dev, lba, count, buffer);
    spinlock_irq_release(&ata_lock);
    
    return ret;
}

int ata_flush_cache(uint8_t device) {
    ata_device_t *dev = ata_get_device(device);
    int ret;
    
    if (!dev) {
        return -ENODEV;
    }
    
    spinlock_irq_acquire(&ata_lock);
    ret = ata_flush(dev);
    spinlock_irq_release(&ata_lock);
    
    return ret;
}

void ata_init(void) {
    uint8_t i, channel, drive;
    uint8_t type;
    
    spinlock_irq_init(&ata_lock);
    
    ata_channels[0].base = ATA_PRIMARY_IO;
    ata_channels[0].ctrl = ATA_PRIMARY_CONTROL;
    ata_channels[0].irq = ATA_PRIMARY_IRQ;
    ata_channels[0].irq_invoked = false;
    
    ata_channels[1].base = ATA_SECONDARY_IO;
    ata_channels[1].ctrl = ATA_SECONDARY_CONTROL;
    ata_channels[1].irq = ATA_SECONDARY_IRQ;
    ata_channels[1].irq_invoked = false;
    
    outb(ata_channels[0].ctrl + ATA_REG_CONTROL, ATA_CTL_NIEN);
    outb(ata_channels[1].ctrl + ATA_REG_CONTROL, ATA_CTL_NIEN);
    
    memset(ata_devices, 0, sizeof(ata_devices));
    memset(ata_blk_devices, 0, sizeof(ata_blk_devices));
    
    for (channel = 0; channel < 2; channel++) {
        for (drive = 0; drive < 2; drive++) {
            type = ata_identify(channel, drive);
            
            if (type == ATA_TYPE_ATA) {
                i = channel * 2 + drive;
                ata_register_blk_device(i);
            }
        }
    }
    
}
