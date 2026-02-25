#include <arch/x86_64/ioapic.h>

#include <mm/mmu.h>

#include <video/printk.h>

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define MAX_IOAPICS 16
#define MAX_irq_OVERRIDES 16

static ioapic_t ioapics[MAX_IOAPICS];
static uint32_t ioapic_count = 0;

static ioapic_irq_override_t irq_overrides[MAX_irq_OVERRIDES];
static uint32_t override_count = 0;

uint32_t ioapic_read(ioapic_t *ioapic, uint8_t reg) {
    if (ioapic == NULL || ioapic->virt_base == 0)
        return 0;

    volatile uint32_t *ioapic_base = (volatile uint32_t *)ioapic->virt_base;

    ioapic_base[IOAPIC_REGSEL / 4] = reg;

    return ioapic_base[IOAPIC_REGWIN / 4];
}

void ioapic_write(ioapic_t *ioapic, uint8_t reg, uint32_t value) {
    if (ioapic == NULL || ioapic->virt_base == 0)
        return;

    volatile uint32_t *ioapic_base = (volatile uint32_t *)ioapic->virt_base;

    ioapic_base[IOAPIC_REGSEL / 4] = reg;

    ioapic_base[IOAPIC_REGWIN / 4] = value;
}

ioapic_redirection_entry_t ioapic_read_redirection(ioapic_t *ioapic, uint8_t index) {
    ioapic_redirection_entry_t entry;

    if (ioapic == NULL || index > ioapic->max_redirects) {
        entry.raw = 0;
        return entry;
    }

    uint8_t reg_low = IOAPIC_REG_REDTBL_BASE + (index * 2);
    uint8_t reg_high = reg_low + 1;

    entry.lower_dword = ioapic_read(ioapic, reg_low);
    entry.upper_dword = ioapic_read(ioapic, reg_high);

    return entry;
}

void ioapic_write_redirection(ioapic_t *ioapic, uint8_t index, ioapic_redirection_entry_t entry) {
    if (ioapic == NULL || index > ioapic->max_redirects)
        return;

    uint8_t reg_low = IOAPIC_REG_REDTBL_BASE + (index * 2);
    uint8_t reg_high = reg_low + 1;

    ioapic_write(ioapic, reg_high, entry.upper_dword);
    ioapic_write(ioapic, reg_low, entry.lower_dword);
}

ioapic_t *ioapic_get_by_gsi(uint32_t gsi) {
    for (uint32_t i = 0; i < ioapic_count; i++) {
        ioapic_t *ioapic = &ioapics[i];
        uint32_t gsi_end = ioapic->gsi_base + ioapic->max_redirects + 1;

        if (gsi >= ioapic->gsi_base && gsi < gsi_end)
            return ioapic;
    }
    return NULL;
}

ioapic_t *ioapic_get_by_id(uint8_t id) {
    for (uint32_t i = 0; i < ioapic_count; i++) {
        if (ioapics[i].id == id)
            return &ioapics[i];
    }
    return NULL;
}

uint32_t ioapic_get_count(void) {
    return ioapic_count;
}

bool ioapic_is_gsi_valid(uint32_t gsi) {
    return ioapic_get_by_gsi(gsi) != NULL;
}

uint8_t ioapic_get_version(ioapic_t *ioapic) {
    if (ioapic == NULL)
        return 0;
    return ioapic->version;
}

uint8_t ioapic_get_max_redirects(ioapic_t *ioapic) {
    if (ioapic == NULL)
        return 0;
    return ioapic->max_redirects;
}

ioapic_t *ioapic_register(uint64_t phys_base, uint8_t id, uint32_t gsi_base) {
    if (ioapic_count >= MAX_IOAPICS) {
        printk("ioapic: Maximum number of I/O APICs (%u) reached\n", MAX_IOAPICS);
        return NULL;
    }

    ioapic_t *ioapic = &ioapics[ioapic_count];
    ioapic->phys_base = phys_base;
    ioapic->id = id;
    ioapic->gsi_base = gsi_base;

    mmu_context_t *kernel_ctx = mmu_get_kernel_context();
    ioapic->virt_base = phys_base;  /* Identity map for simplicity */

    int result = mmu_map_page(kernel_ctx, ioapic->virt_base, phys_base,
                              MMU_MAP_PRESENT | MMU_MAP_WRITE | MMU_MAP_NOCACHE);
    if (result != 0) {
        printk("ioapic: failed to map I/O APIC at 0x%llx\n", phys_base);
        ioapic->virt_base = 0;
        return NULL;
    }

    uint32_t ver_reg = ioapic_read(ioapic, IOAPIC_REG_VER);
    ioapic->version = ver_reg & 0xFF;
    ioapic->max_redirects = (ver_reg >> 16) & 0xFF;

    uint32_t id_reg = ioapic_read(ioapic, IOAPIC_REG_ID);
    uint8_t current_id = (id_reg >> 24) & 0xF;
    
    if (current_id != id) {
        /* Set the ID if it differs */
        id_reg = (id << 24);
        ioapic_write(ioapic, IOAPIC_REG_ID, id_reg);
        
        id_reg = ioapic_read(ioapic, IOAPIC_REG_ID);
        current_id = (id_reg >> 24) & 0xF;
        
        if (current_id != id) {
            printk("ioapic: warning: failed to set ID to %u (current: %u)\n", 
                   id, current_id);
        }
    }

    ioapic_count++;
    
    return ioapic;
}

void ioapic_mask_all(ioapic_t *ioapic) {
    if (ioapic == NULL)
        return;

    for (uint8_t i = 0; i <= ioapic->max_redirects; i++) {
        ioapic_redirection_entry_t entry = ioapic_read_redirection(ioapic, i);
        entry.mask = 1;
        ioapic_write_redirection(ioapic, i, entry);
    }
}

void ioapic_mask_irq(uint32_t gsi) {
    ioapic_t *ioapic = ioapic_get_by_gsi(gsi);
    if (ioapic == NULL) {
        printk("ioapic: cannot mask gsi %u - no I/O APIC found\n", gsi);
        return;
    }

    uint8_t index = gsi - ioapic->gsi_base;
    ioapic_redirection_entry_t entry = ioapic_read_redirection(ioapic, index);
    entry.mask = 1;
    ioapic_write_redirection(ioapic, index, entry);
}

void ioapic_unmask_irq(uint32_t gsi) {
    ioapic_t *ioapic = ioapic_get_by_gsi(gsi);
    if (ioapic == NULL) {
        printk("ioapic: cannot unmask gsi %u - no I/O APIC found\n", gsi);
        return;
    }

    uint8_t index = gsi - ioapic->gsi_base;
    ioapic_redirection_entry_t entry = ioapic_read_redirection(ioapic, index);
    entry.mask = 0;
    ioapic_write_redirection(ioapic, index, entry);
}

ioapic_redirection_entry_t ioapic_create_redirection_entry(
    uint8_t vector,
    uint32_t delivery_mode,
    uint32_t dest_mode,
    uint32_t polarity,
    uint32_t trigger_mode,
    bool mask,
    uint8_t destination)
{
    ioapic_redirection_entry_t entry;
    entry.raw = 0;

    entry.vector = vector;
    entry.delivery_mode = (delivery_mode >> 8) & 0x7;
    entry.dest_mode = (dest_mode >> 11) & 0x1;
    entry.polarity = (polarity >> 13) & 0x1;
    entry.trigger_mode = (trigger_mode >> 15) & 0x1;
    entry.mask = mask ? 1 : 0;
    entry.destination = destination;

    return entry;
}

int ioapic_set_irq(uint32_t gsi, uint8_t vector, uint32_t delivery_mode,
                   uint32_t dest_mode, uint32_t polarity, uint32_t trigger_mode,
                   bool mask, uint8_t destination) {
    ioapic_t *ioapic = ioapic_get_by_gsi(gsi);
    if (ioapic == NULL) {
        printk("ioapic: cannot set gsi %u - no I/O APIC found\n", gsi);
        return -1;
    }

    uint8_t index = gsi - ioapic->gsi_base;
    if (index > ioapic->max_redirects) {
        printk("ioapic: gsi %u index %u exceeds max %u\n", 
               gsi, index, ioapic->max_redirects);
        return -1;
    }

    ioapic_redirection_entry_t entry = ioapic_create_redirection_entry(
        vector, delivery_mode, dest_mode, polarity, trigger_mode, mask, destination
    );

    ioapic_write_redirection(ioapic, index, entry);
    
    return 0;
}

void ioapic_register_irq_override(uint8_t source_irq, uint32_t gsi, uint16_t flags) {
    if (override_count >= MAX_irq_OVERRIDES) {
        printk("ioapic: Maximum irq overrides (%u) reached\n", MAX_irq_OVERRIDES);
        return;
    }

    irq_overrides[override_count].source_irq = source_irq;
    irq_overrides[override_count].gsi = gsi;
    irq_overrides[override_count].flags = flags;
    irq_overrides[override_count].active = true;
    override_count++;
}

uint32_t ioapic_get_gsi_for_isa_irq(uint8_t irq) {
    /* Check for override */
    for (uint32_t i = 0; i < override_count; i++) {
        if (irq_overrides[i].active && irq_overrides[i].source_irq == irq) {
            return irq_overrides[i].gsi;
        }
    }
    
    return irq;
}

int ioapic_map_isa_irq(uint8_t irq, uint8_t vector, uint8_t destination) {
    /* Get the gsi for this isa irq (may be overridden by ACPI) */
    uint32_t gsi = ioapic_get_gsi_for_isa_irq(irq);
    
    uint32_t polarity = IOAPIC_REDIR_INTPOL_HIGH;
    uint32_t trigger_mode = IOAPIC_REDIR_TRIGGER_EDGE;
    
    for (uint32_t i = 0; i < override_count; i++) {
        if (irq_overrides[i].active && irq_overrides[i].source_irq == irq) {
            /* Parse ACPI MADT flags:
             * Bits 0-1: Polarity (00=conforms to bus, 01=active high, 11=active low)
             * Bits 2-3: Trigger (00=conforms to bus, 01=edge, 11=level)
             */
            uint16_t flags = irq_overrides[i].flags;
            
            uint16_t pol_flags = flags & 0x3;
            if (pol_flags == 1) {
                polarity = IOAPIC_REDIR_INTPOL_HIGH;
            } else if (pol_flags == 3) {
                polarity = IOAPIC_REDIR_INTPOL_LOW;
            }
            /* else: 0 (conforms to bus) - keep default */
            
            uint16_t trig_flags = (flags >> 2) & 0x3;
            if (trig_flags == 1) {
                trigger_mode = IOAPIC_REDIR_TRIGGER_EDGE;
            } else if (trig_flags == 3) {
                trigger_mode = IOAPIC_REDIR_TRIGGER_LEVEL;
            }
            /* else: 0 (conforms to bus) - keep default */
            
            break;
        }
    }

    int result = ioapic_set_irq(gsi, vector,
                                IOAPIC_REDIR_DELMOD_FIXED,
                                IOAPIC_REDIR_DESTMOD_PHYSICAL,
                                polarity,
                                trigger_mode,
                                false,  /* Unmask */
                                destination);
    
    return result;
}

void ioapic_init(void) {
    /* TODO: Parse ACPI MADT or MP tables for actual I/O APIC configuration */


    ioapic_t *ioapic = ioapic_register(IOAPIC_DEFAULT_BASE, 0, 0);
    if (ioapic == NULL) {
        printk("ioapic: failed to initialize default I/O APIC\n");
        return;
    }

    ioapic_mask_all(ioapic);

    printk_ts("ioapic: initialized\n");
}

void ioapic_print_info(void) {
    printk("\n==============================\n");
    printk("I/O APIC Configuration\n");
    printk("Total I/O APICs: %u\n\n", ioapic_count);

    for (uint32_t i = 0; i < ioapic_count; i++) {
        ioapic_t *ioapic = &ioapics[i];
        printk("I/O APIC %u:\n", i);
        printk("  ID:           %u\n", ioapic->id);
        printk("  Version:      0x%02x\n", ioapic->version);
        printk("  Phys Base:    0x%016llx\n", ioapic->phys_base);
        printk("  Virt Base:    0x%016llx\n", ioapic->virt_base);
        printk("  gsi Base:     %u\n", ioapic->gsi_base);
        printk("  gsi range:    %u - %u\n", 
               ioapic->gsi_base, ioapic->gsi_base + ioapic->max_redirects);
        printk("  Max Redirects: %u (entries: %u)\n",
               ioapic->max_redirects, ioapic->max_redirects + 1);
        printk("\n");
    }

    if (override_count > 0) {
        printk("irq Overrides: %u\n", override_count);
        for (uint32_t i = 0; i < override_count; i++) {
            if (irq_overrides[i].active) {
                printk("  isa irq %2u -> gsi %2u (flags=0x%04x)\n",
                       irq_overrides[i].source_irq,
                       irq_overrides[i].gsi,
                       irq_overrides[i].flags);
            }
        }
        printk("\n");
    }
    
    printk("==============================\n\n");
}

void ioapic_print_redirection_table(ioapic_t *ioapic) {
    if (ioapic == NULL) {
        printk("ioapic: invalid I/O APIC pointer\n");
        return;
    }

    printk("\n==============================\n");
    printk("I/O APIC %u Redirection Table\n", ioapic->id);
    printk("entry | vec | DM | destm | pol | trig | mask | dest\n");
    printk("------|-----|----| ------|-----|------|------|-----\n");

    for (uint8_t i = 0; i <= ioapic->max_redirects; i++) {
        ioapic_redirection_entry_t entry = ioapic_read_redirection(ioapic, i);
        
        const char *dm_str;
        switch (entry.delivery_mode) {
            case 0: dm_str = "Fix"; break;
            case 1: dm_str = "Low"; break;
            case 2: dm_str = "SMI"; break;
            case 4: dm_str = "NMI"; break;
            case 5: dm_str = "INI"; break;
            case 7: dm_str = "Ext"; break;
            default: dm_str = "???"; break;
        }
        
        printk(" %3u  | %3u | %s|  %s  | %s |  %s  |  %s   | %3u\n",
               i,
               entry.vector,
               dm_str,
               entry.dest_mode ? "Log" : "Phy",
               entry.polarity ? "Low" : "Hi ",
               entry.trigger_mode ? "Lvl" : "Edg",
               entry.mask ? "Y" : "N",
               entry.destination);
    }
    
    printk("==============================\n\n");
}
