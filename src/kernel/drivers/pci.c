#include <arch/x86_64/io.h>
#include <arch/x86_64/apic.h>

#include <core/spinlock.h>

#include <video/printk.h>

#include <mm/vmm.h>
#include <mm/pmm.h>

#include <drivers/pci.h>

#include <klibc/string.h>

#define MAX_PCI_DEVICES 256

static spinlock_irq_t pci_config_lock = SPINLOCK_IRQ_INIT;

static pci_device_t pci_devices[MAX_PCI_DEVICES];
static size_t pci_device_count = 0;

static inline uint32_t pci_config_address(uint8_t bus, uint8_t device,
                                          uint8_t function, uint8_t offset)
{
    return (uint32_t)(
        (1U << 31) |                    /* Enable bit */
        ((uint32_t)bus << 16) |         /* Bus number */
        ((uint32_t)device << 11) |      /* Device number */
        ((uint32_t)function << 8) |     /* Function number */
        (offset & 0xFC)                 /* Register offset (aligned) */
    );
}

uint8_t pci_config_read_byte(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset)
{
    spinlock_irq_acquire(&pci_config_lock);
    uint32_t address = pci_config_address(bus, device, function, offset);
    outl(PCI_CONFIG_ADDRESS, address);
    uint8_t value = inb(PCI_CONFIG_DATA + (offset & 3));
    spinlock_irq_release(&pci_config_lock);
    return value;
}

uint16_t pci_config_read_word(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset)
{
    spinlock_irq_acquire(&pci_config_lock);
    uint32_t address = pci_config_address(bus, device, function, offset);
    outl(PCI_CONFIG_ADDRESS, address);
    uint16_t value = inw(PCI_CONFIG_DATA + (offset & 2));
    spinlock_irq_release(&pci_config_lock);
    return value;
}

uint32_t pci_config_read_dword(uint8_t bus, uint8_t device,
                                uint8_t function, uint8_t offset) {
    spinlock_irq_acquire(&pci_config_lock);
    uint32_t address = pci_config_address(bus, device, function, offset);
    outl(PCI_CONFIG_ADDRESS, address);
    uint32_t value = inl(PCI_CONFIG_DATA);
    spinlock_irq_release(&pci_config_lock);
    return value;
}

void pci_config_write_byte(uint8_t bus, uint8_t device, uint8_t function,
                           uint8_t offset, uint8_t value)
{
    spinlock_irq_acquire(&pci_config_lock);
    uint32_t address = pci_config_address(bus, device, function, offset);
    outl(PCI_CONFIG_ADDRESS, address);
    outb(PCI_CONFIG_DATA + (offset & 3), value);
    spinlock_irq_release(&pci_config_lock);
}

void pci_config_write_word(uint8_t bus, uint8_t device, uint8_t function,
                           uint8_t offset, uint16_t value)
{
    spinlock_irq_acquire(&pci_config_lock);
    uint32_t address = pci_config_address(bus, device, function, offset);
    outl(PCI_CONFIG_ADDRESS, address);
    outw(PCI_CONFIG_DATA + (offset & 2), value);
    spinlock_irq_release(&pci_config_lock);
}

void pci_config_write_dword(uint8_t bus, uint8_t device, uint8_t function,
                            uint8_t offset, uint32_t value)
{
    spinlock_irq_acquire(&pci_config_lock);
    uint32_t address = pci_config_address(bus, device, function, offset);
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
    spinlock_irq_release(&pci_config_lock);
}

bool pci_device_exists(uint8_t bus, uint8_t device, uint8_t function)
{
    uint16_t vendor_id = pci_config_read_word(bus, device, function, PCI_VENDOR_ID);
    return vendor_id != 0xFFFF;
}

uint8_t pci_find_capability(pci_device_t *dev, uint8_t cap_id)
{
    /* Check if device has capability list */
    uint16_t status = pci_config_read_word(dev->bus, dev->device, dev->function, PCI_STATUS);
    if (!(status & PCI_STATUS_CAP_LIST))
        return 0;

    uint8_t cap_ptr = pci_config_read_byte(dev->bus, dev->device, dev->function,
                                           PCI_CAPABILITY_POINTER) & 0xFC;

    uint8_t iterations = 0;
    while (cap_ptr && iterations < 48) {  /* Prevent infinite loops */
        uint8_t cap_id_read = pci_config_read_byte(dev->bus, dev->device,
                                                   dev->function, cap_ptr);
        if (cap_id_read == cap_id)
            return cap_ptr;

        /* Get next capability */
        cap_ptr = pci_config_read_byte(dev->bus, dev->device, dev->function,
                                      cap_ptr + 1) & 0xFC;
        iterations++;
    }

    return 0;  /* not found */
}

void pci_scan_capabilities(pci_device_t *dev)
{
    /* Check if device has capability list */
    uint16_t status = pci_config_read_word(dev->bus, dev->device, dev->function, PCI_STATUS);
    if (!(status & PCI_STATUS_CAP_LIST)) {
        dev->capabilities_offset = 0;
        dev->has_msi = false;
        dev->has_msix = false;
        return;
    }

    dev->capabilities_offset = pci_config_read_byte(dev->bus, dev->device,
                                                    dev->function,
                                                    PCI_CAPABILITY_POINTER) & 0xFC;

    dev->msi_cap_offset = pci_find_capability(dev, PCI_CAP_ID_MSI);
    dev->msix_cap_offset = pci_find_capability(dev, PCI_CAP_ID_MSIX);
    dev->has_msi = (dev->msi_cap_offset != 0);
    dev->has_msix = (dev->msix_cap_offset != 0);
}

void pci_read_device_info(pci_device_t *dev)
{
    dev->vendor_id = pci_config_read_word(dev->bus, dev->device, dev->function, PCI_VENDOR_ID);
    dev->device_id = pci_config_read_word(dev->bus, dev->device, dev->function, PCI_DEVICE_ID);
    dev->class_code = pci_config_read_byte(dev->bus, dev->device, dev->function, PCI_CLASS);
    dev->subclass = pci_config_read_byte(dev->bus, dev->device, dev->function, PCI_SUBCLASS);
    dev->prog_if = pci_config_read_byte(dev->bus, dev->device, dev->function, PCI_PROG_IF);
    dev->revision_id = pci_config_read_byte(dev->bus, dev->device, dev->function, PCI_REVISION_ID);
    dev->header_type = pci_config_read_byte(dev->bus, dev->device, dev->function, PCI_HEADER_TYPE);
    dev->interrupt_line = pci_config_read_byte(dev->bus, dev->device, dev->function, PCI_INTERRUPT_LINE);
    dev->interrupt_pin = pci_config_read_byte(dev->bus, dev->device, dev->function, PCI_INTERRUPT_PIN);

    pci_scan_capabilities(dev);

    /* TEMPORARY: Disable BAR READING
    for (int i = 0; i < PCI_MAX_BARS; i++) {
        pci_read_bar(dev, i);
    }
    */
    
}

void pci_enable_bus_mastering(pci_device_t *dev)
{
    uint16_t command = pci_config_read_word(dev->bus, dev->device, dev->function, PCI_COMMAND);
    command |= PCI_COMMAND_MASTER;
    pci_config_write_word(dev->bus, dev->device, dev->function, PCI_COMMAND, command);
}

void pci_enable_memory_space(pci_device_t *dev)
{
    uint16_t command = pci_config_read_word(dev->bus, dev->device, dev->function, PCI_COMMAND);
    command |= PCI_COMMAND_MEMORY;
    pci_config_write_word(dev->bus, dev->device, dev->function, PCI_COMMAND, command);
}

void pci_enable_io_space(pci_device_t *dev)
{
    uint16_t command = pci_config_read_word(dev->bus, dev->device, dev->function, PCI_COMMAND);
    command |= PCI_COMMAND_IO;
    pci_config_write_word(dev->bus, dev->device, dev->function, PCI_COMMAND, command);
}

void pci_disable_intx(pci_device_t *dev)
{
    uint16_t command = pci_config_read_word(dev->bus, dev->device, dev->function, PCI_COMMAND);
    command |= PCI_COMMAND_INTX_DISABLE;
    pci_config_write_word(dev->bus, dev->device, dev->function, PCI_COMMAND, command);
}

void pci_read_bar(pci_device_t *dev, uint8_t bar_num)
{
    if (bar_num >= PCI_MAX_BARS)
        return;

    uint8_t offset = PCI_BAR0 + (bar_num * 4);

    uint16_t old_command = pci_config_read_word(dev->bus, dev->device, dev->function, PCI_COMMAND);
    pci_config_write_word(dev->bus, dev->device, dev->function, PCI_COMMAND,
                         old_command & ~(PCI_COMMAND_MEMORY | PCI_COMMAND_IO));

    uint32_t bar = pci_config_read_dword(dev->bus, dev->device, dev->function, offset);
    dev->bar[bar_num] = bar;

    if (bar == 0) {
        dev->bar_type[bar_num] = 0xFF;  /* not implemented */
        dev->bar_size[bar_num] = 0;
        pci_config_write_word(dev->bus, dev->device, dev->function, PCI_COMMAND, old_command);
        return;
    }

    if (bar & PCI_BAR_TYPE_IO) {
        dev->bar_type[bar_num] = PCI_BAR_TYPE_IO;
    } else {
        dev->bar_type[bar_num] = PCI_BAR_TYPE_MEMORY;
    }

    pci_config_write_dword(dev->bus, dev->device, dev->function, offset, 0xFFFFFFFF);
    uint32_t size = pci_config_read_dword(dev->bus, dev->device, dev->function, offset);

    pci_config_write_dword(dev->bus, dev->device, dev->function, offset, bar);

    if (dev->bar_type[bar_num] == PCI_BAR_TYPE_IO) {
        size &= 0xFFFFFFFC;  /* mask lower bits */
        dev->bar_size[bar_num] = (~size) + 1;
    } else {
        size &= 0xFFFFFFF0;  /* mask lower bits */
        dev->bar_size[bar_num] = (~size) + 1;

        if ((bar & PCI_BAR_MEMORY_TYPE_MASK) == PCI_BAR_MEMORY_TYPE_64) {
            /* Next BAR contains upper 32 bits */
            if (bar_num + 1 < PCI_MAX_BARS) {
                dev->bar_type[bar_num + 1] = 0xFE;  /* Mark as upper half of 64-bit BAR */
                dev->bar_size[bar_num + 1] = 0;

                uint32_t bar_high = pci_config_read_dword(dev->bus, dev->device,
                                                          dev->function,
                                                          PCI_BAR0 + ((bar_num + 1) * 4));
                dev->bar[bar_num + 1] = bar_high;

                if (size == 0) {
                    pci_config_write_dword(dev->bus, dev->device, dev->function,
                                         PCI_BAR0 + ((bar_num + 1) * 4), 0xFFFFFFFF);
                    uint32_t size_high = pci_config_read_dword(dev->bus, dev->device,
                                                              dev->function,
                                                              PCI_BAR0 + ((bar_num + 1) * 4));
                    pci_config_write_dword(dev->bus, dev->device, dev->function,
                                         PCI_BAR0 + ((bar_num + 1) * 4), bar_high);

                    if (size_high != 0) {
                        uint64_t size64 = ((uint64_t)(~size_high) << 32) | (~size);
                        /* note: This might overflow uint32_t, but we store what we can */
                        dev->bar_size[bar_num] = (uint32_t)((size64 + 1) & 0xFFFFFFFF);
                    }
                }
            }
        }
    }

    pci_config_write_word(dev->bus, dev->device, dev->function, PCI_COMMAND, old_command);
}

void *pci_map_bar(pci_device_t *dev, uint8_t bar_num)
{
    if (bar_num >= PCI_MAX_BARS)
        return NULL;

    if (dev->bar_type[bar_num] != PCI_BAR_TYPE_MEMORY)
        return NULL;

    if (dev->bar_size[bar_num] == 0)
        return NULL;

    uint64_t phys_addr;
    uint32_t flags = VMM_READ | VMM_WRITE | VMM_NOCACHE;

    if ((dev->bar[bar_num] & PCI_BAR_MEMORY_TYPE_MASK) == PCI_BAR_MEMORY_TYPE_64) {
        /* 64-bit BAR */
        if (bar_num + 1 >= PCI_MAX_BARS)
            return NULL;

        uint32_t bar_high = dev->bar[bar_num + 1];
        phys_addr = ((uint64_t)bar_high << 32) | (dev->bar[bar_num] & 0xFFFFFFF0);
    } else {
        /* 32-bit BAR */
        phys_addr = dev->bar[bar_num] & 0xFFFFFFF0;
    }

    size_t map_size = (dev->bar_size[bar_num] + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    vm_space_t *kernel_space = vmm_get_kernel_space();
    uint64_t virt_addr = vmm_alloc_region(kernel_space, map_size, flags, 0);

    if (virt_addr == 0) {
        printk("pci: failed to allocate virtual address for bar%d\n", bar_num);
        return NULL;
    }

    int ret = vmm_map_region(kernel_space, virt_addr, map_size, flags,
                            VMM_TYPE_PHYS, 0, phys_addr);

    if (ret != 0) {
        printk("pci: failed to map bar%d (phys=%p, size=%zu)\n",
               bar_num, (void*)phys_addr, map_size);
        vmm_free_region(kernel_space, virt_addr);
        return NULL;
    }

    return (void*)virt_addr;
}

bool pci_msi_supported(pci_device_t *dev)
{
    return dev->has_msi;
}

int pci_msi_get_max_vectors(pci_device_t *dev)
{
    if (!dev->has_msi)
        return 0;

    uint16_t ctrl = pci_config_read_word(dev->bus, dev->device, dev->function,
                                        dev->msi_cap_offset + PCI_MSI_CTRL);

    uint8_t mmc = (ctrl & PCI_MSI_CTRL_MMC_MASK) >> PCI_MSI_CTRL_MMC_SHIFT;

    if (mmc > 5)
        mmc = 5;

    return 1 << mmc;
}

void pci_msi_get_address_data(uint32_t vector, uint32_t destination_apic_id,
                               uint32_t *address_lo, uint32_t *address_hi,
                               uint16_t *data)
{
    /* MSI address format (Intel):
     * Bits 31-20: 0xFEE (fixed)
     * Bits 19-12: destination ID
     * Bit 3: Redirection hint (0 = no redirection)
     * Bit 2: destination mode (0 = physical)
     * Bits 1-0: Reserved (00)
     */
    *address_lo = 0xFEE00000 | ((destination_apic_id & 0xFF) << 12);
    *address_hi = 0;  /* Upper 32 bits are 0 for x86 */

    *data = vector & 0xFF;
}

int pci_msi_configure(pci_device_t *dev, pci_msi_config_t *config)
{
    if (!dev->has_msi)
        return -1;

    uint8_t cap_offset = dev->msi_cap_offset;

    uint16_t ctrl = pci_config_read_word(dev->bus, dev->device, dev->function,
                                        cap_offset + PCI_MSI_CTRL);

    config->is_64bit = (ctrl & PCI_MSI_CTRL_64BIT) != 0;
    config->per_vector_masking = (ctrl & PCI_MSI_CTRL_PVM) != 0;

    int max_vectors = pci_msi_get_max_vectors(dev);
    if (config->vector_count > max_vectors || config->vector_count == 0)
        return -1;

    if ((config->vector_count & (config->vector_count - 1)) != 0)
        return -1;

    ctrl &= ~PCI_MSI_CTRL_ENABLE;
    pci_config_write_word(dev->bus, dev->device, dev->function,
                         cap_offset + PCI_MSI_CTRL, ctrl);

    pci_config_write_dword(dev->bus, dev->device, dev->function,
                          cap_offset + PCI_MSI_ADDR_LO, config->address_lo);

    if (config->is_64bit) {
        pci_config_write_dword(dev->bus, dev->device, dev->function,
                              cap_offset + PCI_MSI_ADDR_HI, config->address_hi);
        pci_config_write_word(dev->bus, dev->device, dev->function,
                             cap_offset + PCI_MSI_DATA_64, config->data);
    } else {
        pci_config_write_word(dev->bus, dev->device, dev->function,
                             cap_offset + PCI_MSI_DATA_32, config->data);
    }

    uint8_t mme;
    switch (config->vector_count) {
        case 1: mme = 0; break;
        case 2: mme = 1; break;
        case 4: mme = 2; break;
        case 8: mme = 3; break;
        case 16: mme = 4; break;
        case 32: mme = 5; break;
        default: return -1;
    }

    ctrl &= ~PCI_MSI_CTRL_MME_MASK;
    ctrl |= (mme << PCI_MSI_CTRL_MME_SHIFT);
    pci_config_write_word(dev->bus, dev->device, dev->function,
                         cap_offset + PCI_MSI_CTRL, ctrl);

    return 0;
}

int pci_msi_enable(pci_device_t *dev)
{
    if (!dev->has_msi)
        return -1;

    uint8_t cap_offset = dev->msi_cap_offset;
    uint16_t ctrl = pci_config_read_word(dev->bus, dev->device, dev->function,
                                        cap_offset + PCI_MSI_CTRL);

    ctrl |= PCI_MSI_CTRL_ENABLE;
    pci_config_write_word(dev->bus, dev->device, dev->function,
                         cap_offset + PCI_MSI_CTRL, ctrl);

    pci_disable_intx(dev);

    return 0;
}

void pci_msi_disable(pci_device_t *dev)
{
    if (!dev->has_msi)
        return;

    uint8_t cap_offset = dev->msi_cap_offset;
    uint16_t ctrl = pci_config_read_word(dev->bus, dev->device, dev->function,
                                        cap_offset + PCI_MSI_CTRL);

    ctrl &= ~PCI_MSI_CTRL_ENABLE;
    pci_config_write_word(dev->bus, dev->device, dev->function,
                         cap_offset + PCI_MSI_CTRL, ctrl);
}

void pci_msi_mask_vector(pci_device_t *dev, uint8_t vector, bool mask)
{
    if (!dev->has_msi)
        return;

    uint8_t cap_offset = dev->msi_cap_offset;
    uint16_t ctrl = pci_config_read_word(dev->bus, dev->device, dev->function,
                                        cap_offset + PCI_MSI_CTRL);

    if (!(ctrl & PCI_MSI_CTRL_PVM))
        return;

    bool is_64bit = (ctrl & PCI_MSI_CTRL_64BIT) != 0;
    uint8_t mask_offset = is_64bit ? PCI_MSI_MASK_64 : PCI_MSI_MASK_32;

    uint32_t mask_bits = pci_config_read_dword(dev->bus, dev->device, dev->function,
                                               cap_offset + mask_offset);

    if (mask)
        mask_bits |= (1 << vector);
    else
        mask_bits &= ~(1 << vector);

    pci_config_write_dword(dev->bus, dev->device, dev->function,
                          cap_offset + mask_offset, mask_bits);
}

bool pci_msix_supported(pci_device_t *dev)
{
    return dev->has_msix;
}

int pci_msix_get_table_size(pci_device_t *dev)
{
    if (!dev->has_msix)
        return 0;

    uint16_t ctrl = pci_config_read_word(dev->bus, dev->device, dev->function,
                                        dev->msix_cap_offset + PCI_MSIX_CTRL);

    return (ctrl & PCI_MSIX_CTRL_TBL_SIZE_MASK) + 1;
}

int pci_msix_map_table(pci_device_t *dev, pci_msix_config_t *msix)
{
    if (!dev->has_msix)
        return -1;

    uint8_t cap_offset = dev->msix_cap_offset;

    uint32_t table_reg = pci_config_read_dword(dev->bus, dev->device, dev->function,
                                               cap_offset + PCI_MSIX_TABLE);
    msix->table_bir = table_reg & 0x7;
    msix->table_offset = table_reg & ~0x7;

    uint32_t pba_reg = pci_config_read_dword(dev->bus, dev->device, dev->function,
                                             cap_offset + PCI_MSIX_PBA);
    msix->pba_bir = pba_reg & 0x7;
    msix->pba_offset = pba_reg & ~0x7;

    msix->table_size = pci_msix_get_table_size(dev);

    void *bar_base = pci_map_bar(dev, msix->table_bir);
    if (!bar_base)
        return -1;

    msix->table_virt = (void*)((uintptr_t)bar_base + msix->table_offset);

    if (msix->pba_bir != msix->table_bir) {
        void *pba_bar_base = pci_map_bar(dev, msix->pba_bir);
        if (!pba_bar_base)
            return -1;
        msix->pba_virt = (void*)((uintptr_t)pba_bar_base + msix->pba_offset);
    } else {
        msix->pba_virt = (void*)((uintptr_t)bar_base + msix->pba_offset);
    }

    return 0;
}

int pci_msix_configure_vector(pci_device_t *dev, pci_msix_config_t *msix,
                               uint16_t vector, uint32_t address_lo,
                               uint32_t address_hi, uint16_t data)
{
    if (!dev->has_msix || !msix->table_virt)
        return -1;

    if (vector >= msix->table_size)
        return -1;

    volatile uint32_t *entry = (volatile uint32_t *)
        ((uintptr_t)msix->table_virt + (vector * PCI_MSIX_ENTRY_SIZE));

    entry[0] = address_lo;
    entry[1] = address_hi;
    entry[2] = data;
    entry[3] = 0;  /* Unmask */

    return 0;
}

int pci_msix_enable(pci_device_t *dev)
{
    if (!dev->has_msix)
        return -1;

    uint8_t cap_offset = dev->msix_cap_offset;
    uint16_t ctrl = pci_config_read_word(dev->bus, dev->device, dev->function,
                                        cap_offset + PCI_MSIX_CTRL);

    ctrl |= PCI_MSIX_CTRL_ENABLE;
    ctrl &= ~PCI_MSIX_CTRL_FMASK;  /* Unmask all vectors */
    pci_config_write_word(dev->bus, dev->device, dev->function,
                         cap_offset + PCI_MSIX_CTRL, ctrl);

    pci_disable_intx(dev);

    return 0;
}

void pci_msix_disable(pci_device_t *dev)
{
    if (!dev->has_msix)
        return;

    uint8_t cap_offset = dev->msix_cap_offset;
    uint16_t ctrl = pci_config_read_word(dev->bus, dev->device, dev->function,
                                        cap_offset + PCI_MSIX_CTRL);

    ctrl &= ~PCI_MSIX_CTRL_ENABLE;
    pci_config_write_word(dev->bus, dev->device, dev->function,
                         cap_offset + PCI_MSIX_CTRL, ctrl);
}

void pci_msix_mask_vector(pci_device_t *dev, pci_msix_config_t *msix,
                          uint16_t vector, bool mask)
{
    if (!dev->has_msix || !msix->table_virt)
        return;

    if (vector >= msix->table_size)
        return;

    volatile uint32_t *entry = (volatile uint32_t *)
        ((uintptr_t)msix->table_virt + (vector * PCI_MSIX_ENTRY_SIZE));

    if (mask)
        entry[3] |= PCI_MSIX_ENTRY_CTRL_MASKBIT;
    else
        entry[3] &= ~PCI_MSIX_ENTRY_CTRL_MASKBIT;
}

void pci_msix_mask_all(pci_device_t *dev, bool mask)
{
    if (!dev->has_msix)
        return;

    uint8_t cap_offset = dev->msix_cap_offset;
    uint16_t ctrl = pci_config_read_word(dev->bus, dev->device, dev->function,
                                        cap_offset + PCI_MSIX_CTRL);

    if (mask)
        ctrl |= PCI_MSIX_CTRL_FMASK;
    else
        ctrl &= ~PCI_MSIX_CTRL_FMASK;

    pci_config_write_word(dev->bus, dev->device, dev->function,
                         cap_offset + PCI_MSIX_CTRL, ctrl);
}

static void pci_check_function(uint8_t bus, uint8_t device, uint8_t function)
{
    if (!pci_device_exists(bus, device, function))
        return;

    if (pci_device_count >= MAX_PCI_DEVICES) {
        printk("pci: device limit reached\n");
        return;
    }

    pci_device_t *dev = &pci_devices[pci_device_count];
    dev->bus = bus;
    dev->device = device;
    dev->function = function;

    pci_read_device_info(dev);
    pci_device_count++;

    /* TEMPORARY: Disable bridge scanning to fix hang
    if (dev->class_code == PCI_CLASS_BRIDGE &&
        dev->subclass == PCI_SUBCLASS_BRIDGE_PCI) {
        uint8_t secondary_bus = pci_config_read_byte(bus, device, function, 0x19);
        pci_scan_bus(secondary_bus);
    }
    */
}

static void pci_check_device(uint8_t bus, uint8_t device)
{
    if (!pci_device_exists(bus, device, 0))
        return;

    pci_check_function(bus, device, 0);

    uint8_t header_type = pci_config_read_byte(bus, device, 0, PCI_HEADER_TYPE);
    if (header_type & PCI_HEADER_TYPE_MULTIFUNCTION) {
        /* Scan all functions */
        for (uint8_t function = 1; function < PCI_MAX_FUNCTION; function++) {
            pci_check_function(bus, device, function);
        }
    }
}

void pci_scan_bus(uint8_t bus)
{
    for (uint8_t device = 0; device < PCI_MAX_DEVICE; device++) {
        pci_check_device(bus, device);
    }
}

void pci_scan_all_buses(void)
{
    /* Check if host controller is multi-function */
    uint8_t header_type = pci_config_read_byte(0, 0, 0, PCI_HEADER_TYPE);

    if ((header_type & PCI_HEADER_TYPE_MULTIFUNCTION) == 0) {
        /* Single host controller */
        pci_scan_bus(0);
    } else {
        /* Multiple host controllers */
        for (uint8_t function = 0; function < PCI_MAX_FUNCTION; function++) {
            if (!pci_device_exists(0, 0, function))
                break;
            pci_scan_bus(function);
        }
    }
}

pci_device_t *pci_find_device(uint16_t vendor_id, uint16_t device_id)
{
    for (size_t i = 0; i < pci_device_count; i++) {
        if (pci_devices[i].vendor_id == vendor_id &&
            pci_devices[i].device_id == device_id) {
            return &pci_devices[i];
        }
    }
    return NULL;
}

pci_device_t *pci_find_device_by_class(uint8_t class_code, uint8_t subclass)
{
    for (size_t i = 0; i < pci_device_count; i++) {
        if (pci_devices[i].class_code == class_code &&
            pci_devices[i].subclass == subclass) {
            return &pci_devices[i];
        }
    }
    return NULL;
}

size_t pci_get_device_count(void)
{
    return pci_device_count;
}

pci_device_t *pci_get_device(size_t index)
{
    if (index >= pci_device_count)
        return NULL;
    return &pci_devices[index];
}

const char *pci_get_class_name(uint8_t class_code)
{
    switch (class_code) {
        case PCI_CLASS_UNCLASSIFIED: return "Unclassified";
        case PCI_CLASS_STORAGE: return "Storage";
        case PCI_CLASS_NETWORK: return "Network";
        case PCI_CLASS_DISPLAY: return "Display";
        case PCI_CLASS_MULTIMEDIA: return "Multimedia";
        case PCI_CLASS_MEMORY: return "Memory";
        case PCI_CLASS_BRIDGE: return "Bridge";
        case PCI_CLASS_SIMPLE_COMM: return "Communication";
        case PCI_CLASS_BASE_SYSTEM: return "System";
        case PCI_CLASS_INPUT: return "Input";
        case PCI_CLASS_DOCKING: return "Docking";
        case PCI_CLASS_PROCESSOR: return "Processor";
        case PCI_CLASS_SERIAL_BUS: return "Serial Bus";
        case PCI_CLASS_WIRELESS: return "Wireless";
        case PCI_CLASS_INTELLIGENT: return "Intelligent I/O";
        case PCI_CLASS_SATELLITE: return "Satellite";
        case PCI_CLASS_ENCRYPTION: return "Encryption";
        case PCI_CLASS_SIGNAL_PROCESSING: return "Signal Processing";
        default: return "Unknown";
    }
}

const char *pci_get_vendor_name(uint16_t vendor_id)
{
    switch (vendor_id) {
        case 0x8086: return "Intel";
        case 0x1022: return "AMD";
        case 0x10DE: return "NVIDIA";
        case 0x1002: return "ATI/AMD";
        case 0x1234: return "QEMU";
        case 0x1AF4: return "Red Hat (VirtIO)";
        case 0x15AD: return "VMware";
        case 0x80EE: return "VirtualBox";
        default: return "Unknown";
    }
}

void pci_print_devices(void)
{
    printk("\nPCI devices (%zu found):\n", pci_device_count);
    printk("%-4s\t%-4s\t%-4s\t%-6s\t%-6s\t%-14s\t%-3s\t%-3s\t%-5s\n",
           "BUS", "DEV", "FN", "VEN", "DEV", "CLASS", "IRQ", "MSI", "MSI-X");
    printk("----\t----\t----\t------\t------\t--------------\t---\t---\t-----\n");
    for (size_t i = 0; i < pci_device_count; i++) {
        pci_device_t *dev = &pci_devices[i];
        const char *msi_str = dev->has_msi ? "Y" : "N";
        const char *msix_str = dev->has_msix ? "Y" : "N";
        printk("%02x\t%02x\t%x\t%04x\t%04x\t%-14s\t%3d\t%s\t%s",
               dev->bus,
               dev->device,
               dev->function,
               dev->vendor_id,
               dev->device_id,
               pci_get_class_name(dev->class_code),
               dev->interrupt_line,
               msi_str,
               msix_str);
        if (dev->has_msix) {
            int tbl_size = pci_msix_get_table_size(dev);
            printk("(%d)", tbl_size);
        }
        printk("\n");
    }
    printk("\n");
}

void pci_init(void)
{

    pci_device_count = 0;
    memset(pci_devices, 0, sizeof(pci_devices));

    pci_scan_all_buses();
}