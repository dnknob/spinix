#ifndef PCI_H
#define PCI_H

#include <klibc/types.h>

#define PCI_CONFIG_ADDRESS      0xCF8
#define PCI_CONFIG_DATA         0xCFC

#define PCI_VENDOR_ID           0x00
#define PCI_DEVICE_ID           0x02
#define PCI_COMMAND             0x04
#define PCI_STATUS              0x06
#define PCI_REVISION_ID         0x08
#define PCI_PROG_IF             0x09
#define PCI_SUBCLASS            0x0A
#define PCI_CLASS               0x0B
#define PCI_CACHE_LINE_SIZE     0x0C
#define PCI_LATENCY_TIMER       0x0D
#define PCI_HEADER_TYPE         0x0E
#define PCI_BIST                0x0F
#define PCI_BAR0                0x10
#define PCI_BAR1                0x14
#define PCI_BAR2                0x18
#define PCI_BAR3                0x1C
#define PCI_BAR4                0x20
#define PCI_BAR5                0x24
#define PCI_INTERRUPT_LINE      0x3C
#define PCI_INTERRUPT_PIN       0x3D
#define PCI_CAPABILITY_POINTER  0x34

#define PCI_COMMAND_IO          (1 << 0)    /* Enable I/O Space */
#define PCI_COMMAND_MEMORY      (1 << 1)    /* Enable Memory Space */
#define PCI_COMMAND_MASTER      (1 << 2)    /* Enable Bus Mastering */
#define PCI_COMMAND_SPECIAL     (1 << 3)    /* Enable Special Cycles */
#define PCI_COMMAND_INVALIDATE  (1 << 4)    /* Memory Write and Invalidate */
#define PCI_COMMAND_VGA_PALETTE (1 << 5)    /* VGA Palette Snoop */
#define PCI_COMMAND_PARITY      (1 << 6)    /* Parity Error Response */
#define PCI_COMMAND_SERR        (1 << 8)    /* SERR# Enable */
#define PCI_COMMAND_FAST_BACK   (1 << 9)    /* Fast Back-to-Back Enable */
#define PCI_COMMAND_INTX_DISABLE (1 << 10)  /* Interrupt Disable */

#define PCI_STATUS_CAP_LIST     (1 << 4)    /* Capabilities List */
#define PCI_STATUS_66MHZ        (1 << 5)    /* 66 MHz Capable */
#define PCI_STATUS_FAST_BACK    (1 << 7)    /* Fast Back-to-Back Capable */
#define PCI_STATUS_PARITY       (1 << 8)    /* Master Data Parity Error */

#define PCI_HEADER_TYPE_NORMAL      0x00
#define PCI_HEADER_TYPE_BRIDGE      0x01
#define PCI_HEADER_TYPE_CARDBUS     0x02
#define PCI_HEADER_TYPE_MULTIFUNCTION 0x80

#define PCI_CLASS_UNCLASSIFIED      0x00
#define PCI_CLASS_STORAGE           0x01
#define PCI_CLASS_NETWORK           0x02
#define PCI_CLASS_DISPLAY           0x03
#define PCI_CLASS_MULTIMEDIA        0x04
#define PCI_CLASS_MEMORY            0x05
#define PCI_CLASS_BRIDGE            0x06
#define PCI_CLASS_SIMPLE_COMM       0x07
#define PCI_CLASS_BASE_SYSTEM       0x08
#define PCI_CLASS_INPUT             0x09
#define PCI_CLASS_DOCKING           0x0A
#define PCI_CLASS_PROCESSOR         0x0B
#define PCI_CLASS_SERIAL_BUS        0x0C
#define PCI_CLASS_WIRELESS          0x0D
#define PCI_CLASS_INTELLIGENT       0x0E
#define PCI_CLASS_SATELLITE         0x0F
#define PCI_CLASS_ENCRYPTION        0x10
#define PCI_CLASS_SIGNAL_PROCESSING 0x11

#define PCI_SUBCLASS_BRIDGE_HOST    0x00
#define PCI_SUBCLASS_BRIDGE_ISA     0x01
#define PCI_SUBCLASS_BRIDGE_EISA    0x02
#define PCI_SUBCLASS_BRIDGE_MCA     0x03
#define PCI_SUBCLASS_BRIDGE_PCI     0x04
#define PCI_SUBCLASS_BRIDGE_PCMCIA  0x05
#define PCI_SUBCLASS_BRIDGE_NUBUS   0x06
#define PCI_SUBCLASS_BRIDGE_CARDBUS 0x07

#define PCI_BAR_TYPE_MEMORY         0x00
#define PCI_BAR_TYPE_IO             0x01

#define PCI_BAR_MEMORY_TYPE_MASK    0x06
#define PCI_BAR_MEMORY_TYPE_32      0x00
#define PCI_BAR_MEMORY_TYPE_1M      0x02  /* Below 1MB (deprecated) */
#define PCI_BAR_MEMORY_TYPE_64      0x04
#define PCI_BAR_MEMORY_PREFETCHABLE 0x08

#define PCI_CAP_ID_PM               0x01  /* Power Management */
#define PCI_CAP_ID_AGP              0x02  /* Accelerated Graphics Port */
#define PCI_CAP_ID_VPD              0x03  /* Vital Product Data */
#define PCI_CAP_ID_SLOTID           0x04  /* Slot Identification */
#define PCI_CAP_ID_MSI              0x05  /* Message Signalled Interrupts */
#define PCI_CAP_ID_CHSWP            0x06  /* CompactPCI HotSwap */
#define PCI_CAP_ID_PCIX             0x07  /* PCI-X */
#define PCI_CAP_ID_HT               0x08  /* HyperTransport */
#define PCI_CAP_ID_VNDR             0x09  /* Vendor-Specific */
#define PCI_CAP_ID_DBG              0x0A  /* Debug port */
#define PCI_CAP_ID_CCRC             0x0B  /* CompactPCI Central Resource Control */
#define PCI_CAP_ID_SHPC             0x0C  /* PCI Standard Hot-Plug Controller */
#define PCI_CAP_ID_SSVID            0x0D  /* Bridge subsystem vendor/device ID */
#define PCI_CAP_ID_AGP3             0x0E  /* AGP Target PCI-PCI bridge */
#define PCI_CAP_ID_SECDEV           0x0F  /* Secure Device */
#define PCI_CAP_ID_EXP              0x10  /* PCI Express */
#define PCI_CAP_ID_MSIX             0x11  /* MSI-X */
#define PCI_CAP_ID_SATA             0x12  /* SATA Data/Index Conf. */
#define PCI_CAP_ID_AF               0x13  /* PCI Advanced Features */

#define PCI_MSI_CTRL                0x02  /* Message Control */
#define PCI_MSI_ADDR_LO             0x04  /* Message Address (lower 32 bits) */
#define PCI_MSI_ADDR_HI             0x08  /* Message Address (upper 32 bits, 64-bit only) */
#define PCI_MSI_DATA_32             0x08  /* Message Data (32-bit MSI) */
#define PCI_MSI_DATA_64             0x0C  /* Message Data (64-bit MSI) */
#define PCI_MSI_MASK_32             0x0C  /* Interrupt Mask (32-bit with per-vector masking) */
#define PCI_MSI_MASK_64             0x10  /* Interrupt Mask (64-bit with per-vector masking) */
#define PCI_MSI_PENDING_32          0x10  /* Interrupt Pending (32-bit with per-vector masking) */
#define PCI_MSI_PENDING_64          0x14  /* Interrupt Pending (64-bit with per-vector masking) */

#define PCI_MSI_CTRL_ENABLE         (1 << 0)   /* MSI Enable */
#define PCI_MSI_CTRL_MMC_MASK       (7 << 1)   /* Multiple Message Capable */
#define PCI_MSI_CTRL_MMC_SHIFT      1
#define PCI_MSI_CTRL_MME_MASK       (7 << 4)   /* Multiple Message Enable */
#define PCI_MSI_CTRL_MME_SHIFT      4
#define PCI_MSI_CTRL_64BIT          (1 << 7)   /* 64-bit Address Capable */
#define PCI_MSI_CTRL_PVM            (1 << 8)   /* Per-Vector Masking Capable */

#define PCI_MSIX_CTRL               0x02  /* Message Control */
#define PCI_MSIX_TABLE              0x04  /* Table Offset & BIR */
#define PCI_MSIX_PBA                0x08  /* Pending Bit Array Offset & BIR */

#define PCI_MSIX_CTRL_ENABLE        (1 << 15)  /* MSI-X Enable */
#define PCI_MSIX_CTRL_FMASK         (1 << 14)  /* Function Mask */
#define PCI_MSIX_CTRL_TBL_SIZE_MASK 0x7FF      /* Table Size */

#define PCI_MSIX_ENTRY_SIZE         16
#define PCI_MSIX_ENTRY_ADDR_LO      0
#define PCI_MSIX_ENTRY_ADDR_HI      4
#define PCI_MSIX_ENTRY_DATA         8
#define PCI_MSIX_ENTRY_VECTOR_CTRL  12
#define PCI_MSIX_ENTRY_CTRL_MASKBIT (1 << 0)

#define PCI_MAX_BUS                 256
#define PCI_MAX_DEVICE              32
#define PCI_MAX_FUNCTION            8
#define PCI_MAX_BARS                6

#define PCI_MSI_MAX_VECTORS         32
#define PCI_MSIX_MAX_VECTORS        2048

typedef struct {
    uint8_t bus;
    uint8_t device;
    uint8_t function;

    uint16_t vendor_id;
    uint16_t device_id;

    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision_id;

    uint8_t header_type;
    uint8_t interrupt_line;
    uint8_t interrupt_pin;

    uint32_t bar[PCI_MAX_BARS];
    uint32_t bar_size[PCI_MAX_BARS];
    uint8_t bar_type[PCI_MAX_BARS];

    uint8_t capabilities_offset;  /* First capability offset, 0 if none */
    bool has_msi;
    bool has_msix;
    uint8_t msi_cap_offset;       /* Offset to MSI capability */
    uint8_t msix_cap_offset;      /* Offset to MSI-X capability */
} pci_device_t;

typedef struct {
    uint32_t address_lo;
    uint32_t address_hi;
    uint16_t data;
    uint8_t vector_count;  /* Number of vectors to enable (1, 2, 4, 8, 16, 32) */
    bool is_64bit;
    bool per_vector_masking;
} pci_msi_config_t;

typedef struct {
    void *table_virt;      /* Virtual address of MSI-X table */
    void *pba_virt;        /* Virtual address of Pending Bit Array */
    uint16_t table_size;   /* Number of table entries */
    uint8_t table_bir;     /* BAR containing table */
    uint8_t pba_bir;       /* BAR containing PBA */
    uint32_t table_offset; /* Offset into BAR */
    uint32_t pba_offset;   /* Offset into PBA */
} pci_msix_config_t;

uint8_t pci_config_read_byte(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
uint16_t pci_config_read_word(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
uint32_t pci_config_read_dword(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);

void pci_config_write_byte(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint8_t value);
void pci_config_write_word(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint16_t value);
void pci_config_write_dword(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value);

bool pci_device_exists(uint8_t bus, uint8_t device, uint8_t function);
void pci_read_device_info(pci_device_t *dev);
void pci_enable_bus_mastering(pci_device_t *dev);
void pci_enable_memory_space(pci_device_t *dev);
void pci_enable_io_space(pci_device_t *dev);
void pci_disable_intx(pci_device_t *dev);

void pci_read_bar(pci_device_t *dev, uint8_t bar_num);
void *pci_map_bar(pci_device_t *dev, uint8_t bar_num);

uint8_t pci_find_capability(pci_device_t *dev, uint8_t cap_id);
void pci_scan_capabilities(pci_device_t *dev);

bool pci_msi_supported(pci_device_t *dev);
int pci_msi_get_max_vectors(pci_device_t *dev);
int pci_msi_configure(pci_device_t *dev, pci_msi_config_t *config);
int pci_msi_enable(pci_device_t *dev);
void pci_msi_disable(pci_device_t *dev);
void pci_msi_mask_vector(pci_device_t *dev, uint8_t vector, bool mask);

bool pci_msix_supported(pci_device_t *dev);
int pci_msix_get_table_size(pci_device_t *dev);
int pci_msix_map_table(pci_device_t *dev, pci_msix_config_t *msix);
int pci_msix_configure_vector(pci_device_t *dev, pci_msix_config_t *msix,
                               uint16_t vector, uint32_t address_lo,
                               uint32_t address_hi, uint16_t data);
int pci_msix_enable(pci_device_t *dev);
void pci_msix_disable(pci_device_t *dev);
void pci_msix_mask_vector(pci_device_t *dev, pci_msix_config_t *msix,
                          uint16_t vector, bool mask);
void pci_msix_mask_all(pci_device_t *dev, bool mask);

void pci_msi_get_address_data(uint32_t vector, uint32_t destination_apic_id,
                               uint32_t *address_lo, uint32_t *address_hi,
                               uint16_t *data);

void pci_init(void);
void pci_scan_bus(uint8_t bus);
void pci_scan_all_buses(void);
void pci_print_devices(void);

pci_device_t *pci_find_device(uint16_t vendor_id, uint16_t device_id);
pci_device_t *pci_find_device_by_class(uint8_t class_code, uint8_t subclass);

const char *pci_get_class_name(uint8_t class_code);
const char *pci_get_vendor_name(uint16_t vendor_id);

size_t       pci_get_device_count(void);
pci_device_t *pci_get_device(size_t index);

#endif
