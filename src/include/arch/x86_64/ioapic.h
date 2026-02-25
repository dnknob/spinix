#ifndef IOAPIC_H
#define IOAPIC_H

#include <klibc/types.h>

#define IOAPIC_REG_ID           0x00
#define IOAPIC_REG_VER          0x01
#define IOAPIC_REG_ARB          0x02
#define IOAPIC_REG_REDTBL_BASE  0x10

#define IOAPIC_REGSEL           0x00
#define IOAPIC_REGWIN           0x10

#define IOAPIC_DEFAULT_BASE     0xFEC00000
#define IOAPIC_MAX_ENTRIES      24

#define IOAPIC_REDIR_VECTOR_MASK        0xFF
#define IOAPIC_REDIR_DELMOD_FIXED       (0 << 8)
#define IOAPIC_REDIR_DELMOD_LOWPRI      (1 << 8)
#define IOAPIC_REDIR_DELMOD_SMI         (2 << 8)
#define IOAPIC_REDIR_DELMOD_NMI         (4 << 8)
#define IOAPIC_REDIR_DELMOD_INIT        (5 << 8)
#define IOAPIC_REDIR_DELMOD_EXTINT      (7 << 8)
#define IOAPIC_REDIR_DESTMOD_PHYSICAL   (0 << 11)
#define IOAPIC_REDIR_DESTMOD_LOGICAL    (1 << 11)
#define IOAPIC_REDIR_DELIVS_IDLE        (0 << 12)
#define IOAPIC_REDIR_DELIVS_PENDING     (1 << 12)
#define IOAPIC_REDIR_INTPOL_HIGH        (0 << 13)
#define IOAPIC_REDIR_INTPOL_LOW         (1 << 13)
#define IOAPIC_REDIR_REMOTEIRR          (1 << 14)
#define IOAPIC_REDIR_TRIGGER_EDGE       (0 << 15)
#define IOAPIC_REDIR_TRIGGER_LEVEL      (1 << 15)
#define IOAPIC_REDIR_MASK               (1 << 16)

#define IOAPIC_REDIR_DEST_SHIFT         24

typedef union {
    struct {
        uint64_t vector         : 8;
        uint64_t delivery_mode  : 3;
        uint64_t dest_mode      : 1;
        uint64_t delivery_status: 1;
        uint64_t polarity       : 1;
        uint64_t remote_irr     : 1;
        uint64_t trigger_mode   : 1;
        uint64_t mask           : 1;
        uint64_t reserved       : 39;
        uint64_t destination    : 8;
    } __attribute__((packed));
    struct {
        uint32_t lower_dword;
        uint32_t upper_dword;
    };
    uint64_t raw;
} ioapic_redirection_entry_t;

typedef struct {
    phys_addr_t phys_base;
    virt_addr_t virt_base;
    uint8_t     id;
    uint8_t     version;
    uint8_t     max_redirects;
    uint32_t    gsi_base;
} ioapic_t;

typedef struct {
    uint8_t  source_irq;
    uint32_t gsi;
    uint16_t flags;
    bool     active;
} ioapic_irq_override_t;

void ioapic_init(void);

ioapic_t *ioapic_register(phys_addr_t phys_base, uint8_t id, uint32_t gsi_base);
ioapic_t *ioapic_get_by_gsi(uint32_t gsi);
ioapic_t *ioapic_get_by_id(uint8_t id);
uint32_t  ioapic_get_count(void);

uint32_t ioapic_read(ioapic_t *ioapic, uint8_t reg);
void     ioapic_write(ioapic_t *ioapic, uint8_t reg, uint32_t value);

ioapic_redirection_entry_t ioapic_read_redirection(ioapic_t *ioapic, uint8_t index);
void                       ioapic_write_redirection(ioapic_t *ioapic, uint8_t index,
                                                    ioapic_redirection_entry_t entry);

int ioapic_set_irq(uint32_t gsi, uint8_t vector, uint32_t delivery_mode,
                   uint32_t dest_mode, uint32_t polarity, uint32_t trigger_mode,
                   bool mask, uint8_t destination);

void ioapic_mask_irq(uint32_t gsi);
void ioapic_unmask_irq(uint32_t gsi);
void ioapic_mask_all(ioapic_t *ioapic);

int      ioapic_map_isa_irq(uint8_t irq, uint8_t vector, uint8_t destination);
void     ioapic_register_irq_override(uint8_t source_irq, uint32_t gsi, uint16_t flags);
uint32_t ioapic_get_gsi_for_isa_irq(uint8_t irq);

ioapic_redirection_entry_t ioapic_create_redirection_entry(
    uint8_t vector, uint32_t delivery_mode, uint32_t dest_mode,
    uint32_t polarity, uint32_t trigger_mode, bool mask, uint8_t destination);

void ioapic_print_info(void);
void ioapic_print_redirection_table(ioapic_t *ioapic);

bool    ioapic_is_gsi_valid(uint32_t gsi);
uint8_t ioapic_get_version(ioapic_t *ioapic);
uint8_t ioapic_get_max_redirects(ioapic_t *ioapic);

#endif