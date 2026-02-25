#ifndef _DRIVERS_AHCI_H
#define _DRIVERS_AHCI_H

#include <drivers/pci.h>

#include <klibc/types.h>

struct blk_device;

#define AHCI_PCI_CLASS          0x01    /* Mass Storage Controller */
#define AHCI_PCI_SUBCLASS       0x06    /* SATA Controller */
#define AHCI_PCI_PROGIF         0x01    /* AHCI 1.0 */

#define AHCI_PCI_BAR            5       /* BAR5 = ABAR (AHCI Base Address) */

#define AHCI_GHC_CAP            0x00    /* Host Capabilities */
#define AHCI_GHC_GHC            0x04    /* Global Host Control */
#define AHCI_GHC_IS             0x08    /* Interrupt Status */
#define AHCI_GHC_PI             0x0C    /* Ports Implemented */
#define AHCI_GHC_VS             0x10    /* Version */
#define AHCI_GHC_CCC_CTL        0x14    /* Command Completion Coalescing Control */
#define AHCI_GHC_CCC_PORTS      0x18    /* Command Completion Coalescing Ports */
#define AHCI_GHC_EM_LOC         0x1C    /* Enclosure Management Location */
#define AHCI_GHC_EM_CTL         0x20    /* Enclosure Management Control */
#define AHCI_GHC_CAP2           0x24    /* Host Capabilities Extended */
#define AHCI_GHC_BOHC           0x28    /* BIOS/OS Handoff Control and Status */

#define AHCI_CAP_NP_MASK        0x1F    /* Number of Ports (bits 4:0) */
#define AHCI_CAP_SXS            (1 << 5)    /* External SATA */
#define AHCI_CAP_EMS            (1 << 6)    /* Enclosure Management */
#define AHCI_CAP_CCCS           (1 << 7)    /* Command Completion Coalescing */
#define AHCI_CAP_NCS_SHIFT      8           /* Number of Command Slots (bits 12:8) */
#define AHCI_CAP_NCS_MASK       0x1F00
#define AHCI_CAP_PSC            (1 << 13)   /* Partial State Capable */
#define AHCI_CAP_SSC            (1 << 14)   /* Slumber State Capable */
#define AHCI_CAP_PMD            (1 << 15)   /* PIO Multiple DRQ Block */
#define AHCI_CAP_FBSS           (1 << 16)   /* FIS-based Switching */
#define AHCI_CAP_SPM            (1 << 17)   /* Port Multiplier */
#define AHCI_CAP_SAM            (1 << 18)   /* AHCI mode only */
#define AHCI_CAP_SCLO           (1 << 24)   /* Command List Override */
#define AHCI_CAP_SAL            (1 << 25)   /* Activity LED */
#define AHCI_CAP_SALP           (1 << 26)   /* Aggressive Link PM */
#define AHCI_CAP_SSS            (1 << 27)   /* Staggered Spin-up */
#define AHCI_CAP_SMPS           (1 << 28)   /* Mechanical Presence Switch */
#define AHCI_CAP_SSNTF          (1 << 29)   /* SNotification Register */
#define AHCI_CAP_SNCQ           (1 << 30)   /* Native Command Queuing */
#define AHCI_CAP_S64A           (1UL << 31) /* 64-bit Addressing */

#define AHCI_GHC_HR             (1 << 0)    /* HBA Reset */
#define AHCI_GHC_IE             (1 << 1)    /* Interrupt Enable */
#define AHCI_GHC_MRSM           (1 << 2)    /* MSI Revert to Single Message */
#define AHCI_GHC_AE             (1UL << 31) /* AHCI Enable */

#define AHCI_PORT_BASE          0x100
#define AHCI_PORT_SIZE          0x80

#define AHCI_PORT_CLB           0x00    /* Command List Base Address (lower 32-bit) */
#define AHCI_PORT_CLBU          0x04    /* Command List Base Address (upper 32-bit) */
#define AHCI_PORT_FB            0x08    /* FIS Base Address (lower 32-bit) */
#define AHCI_PORT_FBU           0x0C    /* FIS Base Address (upper 32-bit) */
#define AHCI_PORT_IS            0x10    /* Interrupt Status */
#define AHCI_PORT_IE            0x14    /* Interrupt Enable */
#define AHCI_PORT_CMD           0x18    /* Command and Status */
#define AHCI_PORT_TFD           0x20    /* Task File Data */
#define AHCI_PORT_SIG           0x24    /* Signature */
#define AHCI_PORT_SSTS          0x28    /* Serial ATA Status (SCR0: SStatus) */
#define AHCI_PORT_SCTL          0x2C    /* Serial ATA Control (SCR2: SControl) */
#define AHCI_PORT_SERR          0x30    /* Serial ATA Error (SCR1: SError) */
#define AHCI_PORT_SACT          0x34    /* Serial ATA Active (SCR3: SActive) */
#define AHCI_PORT_CI            0x38    /* Command Issue */
#define AHCI_PORT_SNTF          0x3C    /* Serial ATA Notification (SCR4) */
#define AHCI_PORT_FBS           0x40    /* FIS-based Switching Control */

#define AHCI_PORT_INT_DHRS      (1 << 0)    /* Device to Host Register FIS */
#define AHCI_PORT_INT_PSS       (1 << 1)    /* PIO Setup FIS */
#define AHCI_PORT_INT_DSS       (1 << 2)    /* DMA Setup FIS */
#define AHCI_PORT_INT_SDBS      (1 << 3)    /* Set Device Bits FIS */
#define AHCI_PORT_INT_UFS       (1 << 4)    /* Unknown FIS */
#define AHCI_PORT_INT_DPS       (1 << 5)    /* Descriptor Processed */
#define AHCI_PORT_INT_PCS       (1 << 6)    /* Port Connect Change */
#define AHCI_PORT_INT_DMPS      (1 << 7)    /* Device Mechanical Presence */
#define AHCI_PORT_INT_PRCS      (1 << 22)   /* PhyRdy Change */
#define AHCI_PORT_INT_IPMS      (1 << 23)   /* Incorrect Port Multiplier */
#define AHCI_PORT_INT_OFS       (1 << 24)   /* Overflow */
#define AHCI_PORT_INT_INFS      (1 << 26)   /* Interface Non-fatal Error */
#define AHCI_PORT_INT_IFS       (1 << 27)   /* Interface Fatal Error */
#define AHCI_PORT_INT_HBDS      (1 << 28)   /* Host Bus Data Error */
#define AHCI_PORT_INT_HBFS      (1 << 29)   /* Host Bus Fatal Error */
#define AHCI_PORT_INT_TFES      (1 << 30)   /* Task File Error */
#define AHCI_PORT_INT_CPDS      (1UL << 31) /* Cold Presence Detect */

#define AHCI_PORT_INT_ERROR     (AHCI_PORT_INT_TFES | AHCI_PORT_INT_HBFS | \
                                 AHCI_PORT_INT_HBDS | AHCI_PORT_INT_IFS)

#define AHCI_PORT_CMD_ST        (1 << 0)    /* Start */
#define AHCI_PORT_CMD_SUD       (1 << 1)    /* Spin-Up Device */
#define AHCI_PORT_CMD_POD       (1 << 2)    /* Power On Device */
#define AHCI_PORT_CMD_CLO       (1 << 3)    /* Command List Override */
#define AHCI_PORT_CMD_FRE       (1 << 4)    /* FIS Receive Enable */
#define AHCI_PORT_CMD_CCS_SHIFT 8           /* Current Command Slot (bits 12:8) */
#define AHCI_PORT_CMD_CCS_MASK  0x1F00
#define AHCI_PORT_CMD_MPSS      (1 << 13)   /* Mechanical Presence Switch State */
#define AHCI_PORT_CMD_FR        (1 << 14)   /* FIS Receive Running */
#define AHCI_PORT_CMD_CR        (1 << 15)   /* Command List Running */
#define AHCI_PORT_CMD_CPS       (1 << 16)   /* Cold Presence State */
#define AHCI_PORT_CMD_PMA       (1 << 17)   /* Port Multiplier Attached */
#define AHCI_PORT_CMD_HPCP      (1 << 18)   /* Hot Plug Capable Port */
#define AHCI_PORT_CMD_MPSP      (1 << 19)   /* Mechanical Presence Switch */
#define AHCI_PORT_CMD_CPD       (1 << 20)   /* Cold Presence Detection */
#define AHCI_PORT_CMD_ESP       (1 << 21)   /* External SATA Port */
#define AHCI_PORT_CMD_FBSCP     (1 << 22)   /* FIS-based Switching Capable */
#define AHCI_PORT_CMD_APSTE     (1 << 23)   /* Automatic Partial to Slumber */
#define AHCI_PORT_CMD_ATAPI     (1 << 24)   /* Device is ATAPI */
#define AHCI_PORT_CMD_DLAE      (1 << 25)   /* Drive LED on ATAPI Enable */
#define AHCI_PORT_CMD_ALPE      (1 << 26)   /* Aggressive Link PM Enable */
#define AHCI_PORT_CMD_ASP       (1 << 27)   /* Aggressive Slumber/Partial */
#define AHCI_PORT_CMD_ICC_SHIFT 28          /* Interface Communication Control (bits 31:28) */
#define AHCI_PORT_CMD_ICC_MASK  0xF0000000
#define AHCI_PORT_CMD_ICC_ACTIVE (1 << 28)  /* Active state */

#define AHCI_PORT_TFD_STS_MASK  0xFF        /* Status (bits 7:0) */
#define AHCI_PORT_TFD_ERR_MASK  0xFF00      /* Error (bits 15:8) */
#define AHCI_PORT_TFD_STS_ERR   (1 << 0)    /* Error bit in status */
#define AHCI_PORT_TFD_STS_DRQ   (1 << 3)    /* Data transfer requested */
#define AHCI_PORT_TFD_STS_BSY   (1 << 7)    /* Busy */

#define AHCI_PORT_SSTS_DET_MASK 0x0F        /* Device Detection (bits 3:0) */
#define AHCI_PORT_SSTS_DET_NONE 0x0         /* No device detected */
#define AHCI_PORT_SSTS_DET_PRESENT 0x1      /* Device present, no Phy comm */
#define AHCI_PORT_SSTS_DET_ESTABLISHED 0x3  /* Device present, Phy comm established */
#define AHCI_PORT_SSTS_SPD_SHIFT 4          /* Current Interface Speed (bits 7:4) */
#define AHCI_PORT_SSTS_SPD_MASK 0xF0
#define AHCI_PORT_SSTS_IPM_SHIFT 8          /* Interface Power Management (bits 11:8) */
#define AHCI_PORT_SSTS_IPM_MASK 0xF00

#define AHCI_SIG_ATA            0x00000101  /* SATA drive */
#define AHCI_SIG_ATAPI          0xEB140101  /* SATAPI drive */
#define AHCI_SIG_SEMB           0xC33C0101  /* Enclosure management bridge */
#define AHCI_SIG_PM             0x96690101  /* Port multiplier */

#define FIS_TYPE_REG_H2D        0x27        /* Register FIS - host to device */
#define FIS_TYPE_REG_D2H        0x34        /* Register FIS - device to host */
#define FIS_TYPE_DMA_ACT        0x39        /* DMA activate FIS - device to host */
#define FIS_TYPE_DMA_SETUP      0x41        /* DMA setup FIS - bidirectional */
#define FIS_TYPE_DATA           0x46        /* Data FIS - bidirectional */
#define FIS_TYPE_BIST           0x58        /* BIST activate FIS - bidirectional */
#define FIS_TYPE_PIO_SETUP      0x5F        /* PIO setup FIS - device to host */
#define FIS_TYPE_DEV_BITS       0xA1        /* Set device bits FIS - device to host */

#define ATA_CMD_READ_DMA        0xC8        /* READ DMA */
#define ATA_CMD_READ_DMA_EXT    0x25        /* READ DMA EXT (LBA48) */
#define ATA_CMD_WRITE_DMA       0xCA        /* WRITE DMA */
#define ATA_CMD_WRITE_DMA_EXT   0x35        /* WRITE DMA EXT (LBA48) */
#define ATA_CMD_IDENTIFY        0xEC        /* IDENTIFY DEVICE */
#define ATA_CMD_IDENTIFY_PACKET 0xA1        /* IDENTIFY PACKET DEVICE */
#define ATA_CMD_READ_FPDMA      0x60        /* READ FPDMA QUEUED (NCQ) */
#define ATA_CMD_WRITE_FPDMA     0x61        /* WRITE FPDMA QUEUED (NCQ) */

#define AHCI_MAX_PORTS          32
#define AHCI_MAX_CMDS           32          /* Command slots per port */
#define AHCI_CMD_LIST_SIZE      1024        /* 32 entries * 32 bytes */
#define AHCI_RX_FIS_SIZE        256         /* Received FIS area size */
#define AHCI_CMD_TBL_SIZE       128         /* Command table header */
#define AHCI_MAX_PRDT           168         /* Max PRDT entries (page size limit) */

#define AHCI_MAJOR              8           /* Major device number */

#define AHCI_CMD_FLAGS_CFL_SHIFT 0          /* Command FIS Length (bits 4:0) */
#define AHCI_CMD_FLAGS_CFL_MASK  0x1F
#define AHCI_CMD_FLAGS_A         (1 << 5)   /* ATAPI */
#define AHCI_CMD_FLAGS_W         (1 << 6)   /* Write (1 = H2D, 0 = D2H) */
#define AHCI_CMD_FLAGS_P         (1 << 7)   /* Prefetchable */
#define AHCI_CMD_FLAGS_R         (1 << 8)   /* Reset */
#define AHCI_CMD_FLAGS_B         (1 << 9)   /* BIST */
#define AHCI_CMD_FLAGS_C         (1 << 10)  /* Clear Busy upon R_OK */
#define AHCI_CMD_FLAGS_PMP_SHIFT 12         /* Port Multiplier Port (bits 15:12) */
#define AHCI_CMD_FLAGS_PMP_MASK  0xF000
#define AHCI_CMD_FLAGS_PRDTL_SHIFT 16       /* PRDT Length (bits 31:16) */
#define AHCI_CMD_FLAGS_PRDTL_MASK 0xFFFF0000

#define AHCI_PRDT_DBC_MASK      0x3FFFFF    /* Data Byte Count (bits 21:0), max 4MB */
#define AHCI_PRDT_I             (1UL << 31) /* Interrupt on Completion */

typedef struct {
    uint8_t fis_type;           /* FIS_TYPE_REG_H2D */
    uint8_t pm_port:4;          /* Port multiplier */
    uint8_t rsv0:3;             /* Reserved */
    uint8_t c:1;                /* 1 = Command, 0 = Control */
    uint8_t command;            /* Command register */
    uint8_t feature_low;        /* Feature register (7:0) */

    uint8_t lba0;               /* LBA bits 7:0 */
    uint8_t lba1;               /* LBA bits 15:8 */
    uint8_t lba2;               /* LBA bits 23:16 */
    uint8_t device;             /* Device register */

    uint8_t lba3;               /* LBA bits 31:24 */
    uint8_t lba4;               /* LBA bits 39:32 */
    uint8_t lba5;               /* LBA bits 47:40 */
    uint8_t feature_high;       /* Feature register (15:8) */

    uint8_t count_low;          /* Count register (7:0) */
    uint8_t count_high;         /* Count register (15:8) */
    uint8_t icc;                /* Isochronous command completion */
    uint8_t control;            /* Control register */

    uint8_t rsv1[4];            /* Reserved */
} __attribute__((packed)) fis_reg_h2d_t;

typedef struct {
    uint8_t fis_type;           /* FIS_TYPE_REG_D2H */
    uint8_t pm_port:4;          /* Port multiplier */
    uint8_t rsv0:2;             /* Reserved */
    uint8_t i:1;                /* Interrupt bit */
    uint8_t rsv1:1;             /* Reserved */
    uint8_t status;             /* Status register */
    uint8_t error;              /* Error register */

    uint8_t lba0;               /* LBA bits 7:0 */
    uint8_t lba1;               /* LBA bits 15:8 */
    uint8_t lba2;               /* LBA bits 23:16 */
    uint8_t device;             /* Device register */

    uint8_t lba3;               /* LBA bits 31:24 */
    uint8_t lba4;               /* LBA bits 39:32 */
    uint8_t lba5;               /* LBA bits 47:40 */
    uint8_t rsv2;               /* Reserved */

    uint8_t count_low;          /* Count register (7:0) */
    uint8_t count_high;         /* Count register (15:8) */
    uint8_t rsv3[2];            /* Reserved */

    uint8_t rsv4[4];            /* Reserved */
} __attribute__((packed)) fis_reg_d2h_t;

typedef struct {
    uint8_t fis_type;           /* FIS_TYPE_DMA_SETUP */
    uint8_t pm_port:4;          /* Port multiplier */
    uint8_t rsv0:1;             /* Reserved */
    uint8_t d:1;                /* Data transfer direction (1 = device to host) */
    uint8_t i:1;                /* Interrupt bit */
    uint8_t a:1;                /* Auto-activate */
    uint8_t rsv1[2];            /* Reserved */

    uint64_t dma_buffer_id;     /* DMA Buffer Identifier */
    uint32_t rsv2;              /* Reserved */
    uint32_t dma_buffer_offset; /* DMA Buffer Offset */
    uint32_t transfer_count;    /* Transfer count */
    uint32_t rsv3;              /* Reserved */
} __attribute__((packed)) fis_dma_setup_t;

typedef struct {
    uint8_t fis_type;           /* FIS_TYPE_PIO_SETUP */
    uint8_t pm_port:4;          /* Port multiplier */
    uint8_t rsv0:1;             /* Reserved */
    uint8_t d:1;                /* Data transfer direction */
    uint8_t i:1;                /* Interrupt bit */
    uint8_t rsv1:1;             /* Reserved */
    uint8_t status;             /* Status register */
    uint8_t error;              /* Error register */

    uint8_t lba0;               /* LBA bits 7:0 */
    uint8_t lba1;               /* LBA bits 15:8 */
    uint8_t lba2;               /* LBA bits 23:16 */
    uint8_t device;             /* Device register */

    uint8_t lba3;               /* LBA bits 31:24 */
    uint8_t lba4;               /* LBA bits 39:32 */
    uint8_t lba5;               /* LBA bits 47:40 */
    uint8_t rsv2;               /* Reserved */

    uint8_t count_low;          /* Count register (7:0) */
    uint8_t count_high;         /* Count register (15:8) */
    uint8_t rsv3;               /* Reserved */
    uint8_t e_status;           /* New value of status register */

    uint16_t transfer_count;    /* Transfer count */
    uint8_t rsv4[2];            /* Reserved */
} __attribute__((packed)) fis_pio_setup_t;

typedef struct {
    uint8_t fis_type;           /* FIS_TYPE_DEV_BITS */
    uint8_t pm_port:4;          /* Port multiplier */
    uint8_t rsv0:2;             /* Reserved */
    uint8_t i:1;                /* Interrupt bit */
    uint8_t n:1;                /* Notification */
    uint8_t status_low:3;       /* Status Low (bits 2:0) */
    uint8_t rsv1:1;             /* Reserved */
    uint8_t status_high:3;      /* Status High (bits 6:4) */
    uint8_t rsv2:1;             /* Reserved */
    uint8_t error;              /* Error register */

    uint32_t protocol_specific; /* Protocol Specific */
} __attribute__((packed)) fis_set_dev_bits_t;

typedef struct {
    fis_dma_setup_t dsfis;      /* DMA Setup FIS */
    uint8_t pad0[4];
    fis_pio_setup_t psfis;      /* PIO Setup FIS */
    uint8_t pad1[12];
    fis_reg_d2h_t rfis;         /* Register - Device to Host FIS */
    uint8_t pad2[4];
    fis_set_dev_bits_t sdbfis;  /* Set Device Bits FIS */
    uint8_t ufis[64];           /* Unknown FIS */
    uint8_t rsv[96];            /* Reserved */
} __attribute__((packed, aligned(256))) hba_fis_t;

typedef struct {
    uint32_t dba;               /* Data Base Address (lower 32-bit) */
    uint32_t dbau;              /* Data Base Address (upper 32-bit) */
    uint32_t rsv;               /* Reserved */
    uint32_t dbc;               /* Data Byte Count and Interrupt */
} __attribute__((packed)) hba_prdt_entry_t;

typedef struct {
    uint8_t cfis[64];           /* Command FIS */
    uint8_t acmd[16];           /* ATAPI Command */
    uint8_t rsv[48];            /* Reserved */
    hba_prdt_entry_t prdt[];    /* Physical Region Descriptor Table */
} __attribute__((packed, aligned(128))) hba_cmd_tbl_t;

typedef struct {
    uint16_t flags;             /* Command flags (DW0) */
    uint16_t prdtl;             /* PRDT length */
    uint32_t prdbc;             /* PRD byte count */
    uint32_t ctba;              /* Command table base address (lower 32-bit) */
    uint32_t ctbau;             /* Command table base address (upper 32-bit) */
    uint32_t rsv[4];            /* Reserved */
} __attribute__((packed)) hba_cmd_header_t;

typedef struct {
    reg32_t cap;      /* 0x00: Host Capabilities */
    reg32_t ghc;      /* 0x04: Global Host Control */
    reg32_t is;       /* 0x08: Interrupt Status */
    reg32_t pi;       /* 0x0C: Ports Implemented */
    reg32_t vs;       /* 0x10: Version */
    reg32_t ccc_ctl;  /* 0x14: Command Completion Coalescing Control */
    reg32_t ccc_ports;/* 0x18: Command Completion Coalescing Ports */
    reg32_t em_loc;   /* 0x1C: Enclosure Management Location */
    reg32_t em_ctl;   /* 0x20: Enclosure Management Control */
    reg32_t cap2;     /* 0x24: Host Capabilities Extended */
    reg32_t bohc;     /* 0x28: BIOS/OS Handoff Control and Status */
    uint8_t rsv[0xA0 - 0x2C];   /* 0x2C - 0x9F: Reserved */
    uint8_t vendor[0x100 - 0xA0]; /* 0xA0 - 0xFF: Vendor specific */
} __attribute__((packed)) hba_mem_t;

typedef struct {
    reg32_t clb;      /* 0x00: Command List Base Address (lower 32-bit) */
    reg32_t clbu;     /* 0x04: Command List Base Address (upper 32-bit) */
    reg32_t fb;       /* 0x08: FIS Base Address (lower 32-bit) */
    reg32_t fbu;      /* 0x0C: FIS Base Address (upper 32-bit) */
    reg32_t is;       /* 0x10: Interrupt Status */
    reg32_t ie;       /* 0x14: Interrupt Enable */
    reg32_t cmd;      /* 0x18: Command and Status */
    reg32_t rsv0;     /* 0x1C: Reserved */
    reg32_t tfd;      /* 0x20: Task File Data */
    reg32_t sig;      /* 0x24: Signature */
    reg32_t ssts;     /* 0x28: Serial ATA Status (SCR0: SStatus) */
    reg32_t sctl;     /* 0x2C: Serial ATA Control (SCR2: SControl) */
    reg32_t serr;     /* 0x30: Serial ATA Error (SCR1: SError) */
    reg32_t sact;     /* 0x34: Serial ATA Active (SCR3: SActive) */
    reg32_t ci;       /* 0x38: Command Issue */
    reg32_t sntf;     /* 0x3C: Serial ATA Notification (SCR4) */
    reg32_t fbs;      /* 0x40: FIS-based Switching Control */
    uint32_t rsv1[11];          /* 0x44 - 0x6F: Reserved */
    uint32_t vendor[4];         /* 0x70 - 0x7F: Vendor specific */
} __attribute__((packed)) hba_port_t;

typedef struct ahci_port {
    hba_port_t *regs;           /* Port registers */
    hba_cmd_header_t *cmd_list; /* Command list (virtual) */
    hba_fis_t *rx_fis;          /* Received FIS (virtual) */
    hba_cmd_tbl_t *cmd_tables[AHCI_MAX_CMDS]; /* Command tables (virtual) */
    
    uint64_t cmd_list_phys;     /* Command list (physical) */
    uint64_t rx_fis_phys;       /* Received FIS (physical) */
    uint64_t cmd_table_phys[AHCI_MAX_CMDS]; /* Command tables (physical) */
    
    uint32_t port_num;          /* Port number */
    uint32_t implemented;       /* Port is implemented */
    uint32_t type;              /* Device type (SIG_*) */
    
    uint64_t sectors;           /* Number of sectors */
    char model[41];             /* Device model */
    char serial[21];            /* Device serial */
    
    struct blk_device *blk_dev; /* Block device */
} ahci_port_t;

typedef struct ahci_controller {
    pci_device_t *pci_dev;      /* PCI device */
    hba_mem_t *hba_mem;         /* HBA memory registers (virtual) */
    uint64_t hba_mem_phys;      /* HBA memory registers (physical) */
    
    uint32_t num_ports;         /* Number of implemented ports */
    uint32_t num_cmd_slots;     /* Number of command slots per port */
    uint32_t caps;              /* Capabilities */
    uint32_t version;           /* AHCI version */
    
    ahci_port_t ports[AHCI_MAX_PORTS]; /* Port structures */
} ahci_controller_t;

void ahci_init(void);
int ahci_probe_controller(pci_device_t *pci_dev);

int ahci_port_init(ahci_controller_t *ctrl, uint32_t port_num);
int ahci_port_start(ahci_port_t *port);
int ahci_port_stop(ahci_port_t *port);
int ahci_port_identify(ahci_port_t *port);

int ahci_port_read(ahci_port_t *port, uint64_t lba, uint32_t count, void *buffer);
int ahci_port_write(ahci_port_t *port, uint64_t lba, uint32_t count, const void *buffer);

const char *ahci_port_type_string(uint32_t type);
void ahci_print_info(ahci_controller_t *ctrl);

#endif
