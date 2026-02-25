#ifndef _DRIVERS_ATA_H
#define _DRIVERS_ATA_H

#include <klibc/types.h>

#define ATA_PRIMARY_IO          0x1F0
#define ATA_PRIMARY_CONTROL     0x3F6
#define ATA_PRIMARY_IRQ         14

#define ATA_SECONDARY_IO        0x170
#define ATA_SECONDARY_CONTROL   0x376
#define ATA_SECONDARY_IRQ       15

#define ATA_REG_DATA            0x00    /* Data register (R/W) */
#define ATA_REG_ERROR           0x01    /* Error register (R) */
#define ATA_REG_FEATURES        0x01    /* Features register (W) */
#define ATA_REG_SECCOUNT0       0x02    /* Sector count register (R/W) */
#define ATA_REG_LBA0            0x03    /* LBA low register (R/W) */
#define ATA_REG_LBA1            0x04    /* LBA mid register (R/W) */
#define ATA_REG_LBA2            0x05    /* LBA high register (R/W) */
#define ATA_REG_HDDEVSEL        0x06    /* Drive/head register (R/W) */
#define ATA_REG_COMMAND         0x07    /* Command register (W) */
#define ATA_REG_STATUS          0x07    /* Status register (R) */

#define ATA_REG_CONTROL         0x00    /* Device control register (W) */
#define ATA_REG_ALTSTATUS       0x00    /* Alternate status register (R) */
#define ATA_REG_DEVADDRESS      0x01    /* Drive address register (R) */

#define ATA_CMD_READ_PIO        0x20    /* Read sectors with retry (LBA28) */
#define ATA_CMD_READ_PIO_EXT    0x24    /* Read sectors with retry (LBA48) */
#define ATA_CMD_WRITE_PIO       0x30    /* Write sectors with retry (LBA28) */
#define ATA_CMD_WRITE_PIO_EXT   0x34    /* Write sectors with retry (LBA48) */
#define ATA_CMD_CACHE_FLUSH     0xE7    /* Flush cache (LBA28) */
#define ATA_CMD_CACHE_FLUSH_EXT 0xEA    /* Flush cache (LBA48) */
#define ATA_CMD_IDENTIFY        0xEC    /* Identify device */
#define ATA_CMD_IDENTIFY_PACKET 0xA1    /* Identify packet device (ATAPI) */

#define ATA_SR_BSY              0x80    /* Busy */
#define ATA_SR_DRDY             0x40    /* Drive ready */
#define ATA_SR_DF               0x20    /* Drive write fault */
#define ATA_SR_DSC              0x10    /* Drive seek complete */
#define ATA_SR_DRQ              0x08    /* Data request ready */
#define ATA_SR_CORR             0x04    /* Corrected data */
#define ATA_SR_IDX              0x02    /* Index */
#define ATA_SR_ERR              0x01    /* Error */

#define ATA_ER_BBK              0x80    /* Bad block */
#define ATA_ER_UNC              0x40    /* Uncorrectable data */
#define ATA_ER_MC               0x20    /* Media changed */
#define ATA_ER_IDNF             0x10    /* ID mark not found */
#define ATA_ER_MCR              0x08    /* Media change request */
#define ATA_ER_ABRT             0x04    /* Command aborted */
#define ATA_ER_TK0NF            0x02    /* Track 0 not found */
#define ATA_ER_AMNF             0x01    /* Address mark not found */

#define ATA_CTL_SRST            0x04    /* Software reset */
#define ATA_CTL_NIEN            0x02    /* Disable interrupts */

#define ATA_DH_LBA              0x40    /* LBA mode */
#define ATA_DH_DEV              0x10    /* Select drive (0=master, 1=slave) */

#define ATA_IDENT_DEVICETYPE    0       /* Device type */
#define ATA_IDENT_CYLINDERS     1       /* Number of cylinders (obsolete) */
#define ATA_IDENT_HEADS         3       /* Number of heads (obsolete) */
#define ATA_IDENT_SECTORS       6       /* Sectors per track (obsolete) */
#define ATA_IDENT_SERIAL        10      /* Serial number (20 chars) */
#define ATA_IDENT_MODEL         27      /* Model number (40 chars) */
#define ATA_IDENT_CAPABILITIES  49      /* Capabilities */
#define ATA_IDENT_FIELDVALID    53      /* Field validity flags */
#define ATA_IDENT_MAX_LBA       60      /* Max LBA for LBA28 mode (2 words) */
#define ATA_IDENT_COMMANDSETS   83      /* Supported command sets */
#define ATA_IDENT_MAX_LBA_EXT   100     /* Max LBA for LBA48 mode (4 words) */

#define ATA_TYPE_NONE           0
#define ATA_TYPE_ATA            1
#define ATA_TYPE_ATAPI          2

#define ATA_MAX_DEVICES         4       /* 2 channels × 2 drives */

#define ATA_MAJOR               3

typedef struct {
    uint16_t base;              /* I/O base port */
    uint16_t ctrl;              /* Control base port */
    uint8_t irq;                /* IRQ number */
    bool irq_invoked;           /* IRQ status flag */
} ata_channel_t;

typedef struct {
    bool present;               /* Device exists */
    uint8_t channel;            /* Channel number (0=primary, 1=secondary) */
    uint8_t drive;              /* Drive number (0=master, 1=slave) */
    uint8_t type;               /* Device type (ATA_TYPE_*) */
    uint16_t signature;         /* Drive signature */
    uint16_t capabilities;      /* Device capabilities */
    uint32_t command_sets;      /* Supported command sets */
    uint64_t size;              /* Size in sectors */
    char model[41];             /* Model string */
    char serial[21];            /* Serial number string */
} ata_device_t;

void ata_init(void);

int ata_read_sectors(uint8_t device, uint64_t lba, uint32_t count, void *buffer);
int ata_write_sectors(uint8_t device, uint64_t lba, uint32_t count, const void *buffer);

ata_device_t *ata_get_device(uint8_t device);

int ata_flush_cache(uint8_t device);

#endif
