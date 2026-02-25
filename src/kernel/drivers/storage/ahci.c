#include <drivers/storage/ahci.h>
#include <drivers/pci.h>

#include <blk/blk.h>

#include <mm/pmm.h>
#include <mm/heap.h>
#include <mm/paging.h>
#include <mm/vmm.h>

#include <arch/x86_64/io.h>
#include <arch/x86_64/intr.h>

#include <core/spinlock.h>

#include <video/printk.h>

#include <klibc/string.h>
#include <errno.h>

#define AHCI_MAX_CONTROLLERS    4

static ahci_controller_t ahci_controllers[AHCI_MAX_CONTROLLERS];
static size_t ahci_controller_count = 0;
static spinlock_irq_t ahci_lock = SPINLOCK_IRQ_INIT;

static inline uint32_t ahci_port_read32(hba_port_t *port, uint32_t reg)
{
    return *(volatile uint32_t *)((uint8_t *)port + reg);
}

static inline void ahci_port_write32(hba_port_t *port, uint32_t reg, uint32_t val)
{
    *(volatile uint32_t *)((uint8_t *)port + reg) = val;
}

static void ahci_port_clear_serr(hba_port_t *port)
{
    port->serr = port->serr;  /* Write 1 to clear */
}

static void ahci_port_clear_is(hba_port_t *port)
{
    port->is = port->is;  /* Write 1 to clear */
}

static int ahci_wait_port_ready(hba_port_t *port, uint32_t timeout_ms)
{
    uint32_t timeout = timeout_ms * 1000;
    
    while (timeout--) {
        uint32_t tfd = port->tfd;
        if (!(tfd & (AHCI_PORT_TFD_STS_BSY | AHCI_PORT_TFD_STS_DRQ))) {
            return 0;
        }
        io_wait();
    }
    
    return -ETIMEDOUT;
}

static int ahci_find_free_slot(ahci_port_t *port)
{
    uint32_t slots = port->regs->sact | port->regs->ci;
    
    for (int i = 0; i < AHCI_MAX_CMDS; i++) {
        if (!(slots & (1 << i))) {
            return i;
        }
    }
    
    return -1;
}

const char *ahci_port_type_string(uint32_t type)
{
    switch (type) {
        case AHCI_SIG_ATA:
            return "SATA";
        case AHCI_SIG_ATAPI:
            return "SATAPI";
        case AHCI_SIG_SEMB:
            return "SEMB";
        case AHCI_SIG_PM:
            return "PM";
        case 0x00000000:
            return "No Device";
        case 0xFFFFFFFF:
            return "Invalid";
        default:
            return "Unknown";
    }
}

static void ahci_fixstring(char *str, size_t len)
{
    /* ATA strings are byte-swapped */
    for (size_t i = 0; i < len; i += 2) {
        char tmp = str[i];
        str[i] = str[i + 1];
        str[i + 1] = tmp;
    }
    
    for (size_t i = len - 1; i > 0 && str[i] == ' '; i--) {
        str[i] = '\0';
    }
    str[len] = '\0';
}

int ahci_port_stop(ahci_port_t *port)
{
    hba_port_t *regs = port->regs;
    
    regs->cmd &= ~AHCI_PORT_CMD_ST;
    
    uint32_t timeout = 500000;
    while (timeout--) {
        if (!(regs->cmd & AHCI_PORT_CMD_CR)) {
            break;
        }
        io_wait();
    }
    
    if (regs->cmd & AHCI_PORT_CMD_CR) {
        printk("ahci: port %u failed to stop command engine\n", port->port_num);
        return -ETIMEDOUT;
    }
    
    regs->cmd &= ~AHCI_PORT_CMD_FRE;
    
    timeout = 500000;
    while (timeout--) {
        if (!(regs->cmd & AHCI_PORT_CMD_FR)) {
            break;
        }
        io_wait();
    }
    
    if (regs->cmd & AHCI_PORT_CMD_FR) {
        printk("ahci: port %u failed to stop FIS receive\n", port->port_num);
        return -ETIMEDOUT;
    }
    
    return 0;
}

int ahci_port_start(ahci_port_t *port)
{
    hba_port_t *regs = port->regs;
    
    ahci_port_clear_serr(regs);
    ahci_port_clear_is(regs);
    
    regs->cmd |= AHCI_PORT_CMD_FRE;
    
    uint32_t timeout = 500000;
    while (timeout--) {
        if (regs->cmd & AHCI_PORT_CMD_FR) {
            break;
        }
        io_wait();
    }
    
    if (!(regs->cmd & AHCI_PORT_CMD_FR)) {
        printk("ahci: port %u failed to start FIS receive\n", port->port_num);
        return -ETIMEDOUT;
    }
    
    regs->cmd |= AHCI_PORT_CMD_ST;
    
    timeout = 500000;
    while (timeout--) {
        if (regs->cmd & AHCI_PORT_CMD_CR) {
            break;
        }
        io_wait();
    }
    
    if (!(regs->cmd & AHCI_PORT_CMD_CR)) {
        printk("ahci: port %u failed to start command engine\n", port->port_num);
        return -ETIMEDOUT;
    }
    
    return 0;
}

static int ahci_port_alloc_memory(ahci_port_t *port)
{
    /* Allocate command list (1KB, 1KB aligned) */
    uint64_t cmd_list_phys = pmm_alloc_page();
    if (cmd_list_phys == 0) {
        return -ENOMEM;
    }
    port->cmd_list_phys = cmd_list_phys;
    port->cmd_list = (hba_cmd_header_t *)PHYS_TO_VIRT(cmd_list_phys);
    memset(port->cmd_list, 0, AHCI_CMD_LIST_SIZE);
    
    uint64_t rx_fis_phys = pmm_alloc_page();
    if (rx_fis_phys == 0) {
        pmm_free_page(cmd_list_phys);
        return -ENOMEM;
    }
    port->rx_fis_phys = rx_fis_phys;
    port->rx_fis = (hba_fis_t *)PHYS_TO_VIRT(rx_fis_phys);
    memset(port->rx_fis, 0, AHCI_RX_FIS_SIZE);
    
    for (int i = 0; i < AHCI_MAX_CMDS; i++) {
        uint64_t cmd_tbl_phys = pmm_alloc_page();
        if (cmd_tbl_phys == 0) {
            /* Free previously allocated tables */
            for (int j = 0; j < i; j++) {
                pmm_free_page(port->cmd_table_phys[j]);
            }
            pmm_free_page(rx_fis_phys);
            pmm_free_page(cmd_list_phys);
            return -ENOMEM;
        }
        
        port->cmd_table_phys[i] = cmd_tbl_phys;
        port->cmd_tables[i] = (hba_cmd_tbl_t *)PHYS_TO_VIRT(cmd_tbl_phys);
        memset(port->cmd_tables[i], 0, PAGE_SIZE);
        
        port->cmd_list[i].ctba = (uint32_t)(cmd_tbl_phys & 0xFFFFFFFF);
        port->cmd_list[i].ctbau = (uint32_t)(cmd_tbl_phys >> 32);
    }
    
    return 0;
}

static void ahci_port_free_memory(ahci_port_t *port)
{
    if (port->cmd_list_phys) {
        pmm_free_page(port->cmd_list_phys);
        port->cmd_list_phys = 0;
        port->cmd_list = NULL;
    }
    
    if (port->rx_fis_phys) {
        pmm_free_page(port->rx_fis_phys);
        port->rx_fis_phys = 0;
        port->rx_fis = NULL;
    }
    
    for (int i = 0; i < AHCI_MAX_CMDS; i++) {
        if (port->cmd_table_phys[i]) {
            pmm_free_page(port->cmd_table_phys[i]);
            port->cmd_table_phys[i] = 0;
            port->cmd_tables[i] = NULL;
        }
    }
}

int ahci_port_identify(ahci_port_t *port)
{
    hba_port_t *regs = port->regs;
    uint16_t *identify_buf;
    uint64_t identify_phys;
    int slot;
    int ret = 0;
    
    uint32_t sig = regs->sig;
    if (sig != AHCI_SIG_ATA) {
        return -ENODEV;  /* Only support ATA drives */
    }
    
    identify_phys = pmm_alloc_page();
    if (identify_phys == 0) {
        return -ENOMEM;
    }
    identify_buf = (uint16_t *)PHYS_TO_VIRT(identify_phys);
    memset(identify_buf, 0, 512);
    
    slot = ahci_find_free_slot(port);
    if (slot < 0) {
        ret = -EBUSY;
        goto cleanup;
    }
    
    hba_cmd_header_t *cmd_hdr = &port->cmd_list[slot];
    cmd_hdr->flags = sizeof(fis_reg_h2d_t) / 4;  /* Command FIS size in dwords */
    cmd_hdr->prdtl = 1;  /* One PRDT entry */
    cmd_hdr->prdbc = 0;
    
    hba_cmd_tbl_t *cmd_tbl = port->cmd_tables[slot];
    memset(cmd_tbl, 0, sizeof(hba_cmd_tbl_t) + sizeof(hba_prdt_entry_t));
    
    cmd_tbl->prdt[0].dba = (uint32_t)(identify_phys & 0xFFFFFFFF);
    cmd_tbl->prdt[0].dbau = (uint32_t)(identify_phys >> 32);
    cmd_tbl->prdt[0].dbc = 511;  /* 512 bytes (0-indexed) */
    
    fis_reg_h2d_t *fis = (fis_reg_h2d_t *)&cmd_tbl->cfis;
    memset(fis, 0, sizeof(fis_reg_h2d_t));
    
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->c = 1;  /* Command */
    fis->command = ATA_CMD_IDENTIFY;
    fis->device = 0;
    
    regs->ci = (1 << slot);
    
    uint32_t timeout = 1000000;
    while (timeout--) {
        if (!(regs->ci & (1 << slot))) {
            break;
        }
        
        if (regs->is & AHCI_PORT_INT_TFES) {
            printk("ahci: port %u IDENTIFY failed (error)\n", port->port_num);
            ret = -EIO;
            goto cleanup;
        }
        
        io_wait();
    }
    
    if (regs->ci & (1 << slot)) {
        printk("ahci: port %u IDENTIFY timeout\n", port->port_num);
        ret = -ETIMEDOUT;
        goto cleanup;
    }
    
    ahci_port_clear_is(regs);
    
    port->sectors = *(uint64_t *)(&identify_buf[100]);  /* LBA48 sectors */
    if (port->sectors == 0) {
        port->sectors = *(uint32_t *)(&identify_buf[60]);  /* LBA28 sectors */
    }
    
    memcpy(port->model, &identify_buf[27], 40);
    ahci_fixstring(port->model, 40);
    
    memcpy(port->serial, &identify_buf[10], 20);
    ahci_fixstring(port->serial, 20);
    
    port->type = AHCI_SIG_ATA;
    
cleanup:
    pmm_free_page(identify_phys);
    return ret;
}

static int ahci_port_comreset(ahci_port_t *port)
{
    hba_port_t *regs = port->regs;
    
    printk("ahci: port %u: performing COMRESET\n", port->port_num);
    
    ahci_port_stop(port);
    
    uint32_t sctl = regs->sctl;
    sctl = (sctl & ~0xF) | 0x1;  /* Set DET to 1 */
    regs->sctl = sctl;
    
    for (int i = 0; i < 100000; i++) {
        io_wait();
    }
    
    sctl = sctl & ~0xF;
    regs->sctl = sctl;
    
    int timeout = 1000000;
    while (timeout--) {
        uint32_t ssts = regs->ssts;
        uint8_t det = ssts & AHCI_PORT_SSTS_DET_MASK;
        
        if (det == AHCI_PORT_SSTS_DET_ESTABLISHED) {
            printk("ahci: port %u: link re-established after COMRESET\n", 
                   port->port_num);
            
            ahci_port_clear_serr(regs);
            ahci_port_clear_is(regs);
            
            return 0;
        }
        
        io_wait();
    }
    
    printk("ahci: port %u: COMRESET timeout\n", port->port_num);
    return -ETIMEDOUT;
}

int ahci_port_init(ahci_controller_t *ctrl, uint32_t port_num)
{
    ahci_port_t *port = &ctrl->ports[port_num];
    hba_port_t *regs = (hba_port_t *)((uint8_t *)ctrl->hba_mem + AHCI_PORT_BASE + (port_num * AHCI_PORT_SIZE));
    
    port->regs = regs;
    port->port_num = port_num;
    port->implemented = 1;
    
    uint32_t ssts = regs->ssts;
    uint8_t det = ssts & AHCI_PORT_SSTS_DET_MASK;
    uint8_t ipm = (ssts >> AHCI_PORT_SSTS_IPM_SHIFT) & 0xF;
    
    if (det != AHCI_PORT_SSTS_DET_ESTABLISHED || ipm != 1) {
        port->implemented = 0;
        return -ENODEV;
    }
    
    int ret = ahci_port_stop(port);
    if (ret != 0) {
        return ret;
    }
    
    ret = ahci_port_alloc_memory(port);
    if (ret != 0) {
        printk("ahci: port %u failed to allocate memory\n", port_num);
        return ret;
    }
    
    regs->clb = (uint32_t)(port->cmd_list_phys & 0xFFFFFFFF);
    regs->clbu = (uint32_t)(port->cmd_list_phys >> 32);
    regs->fb = (uint32_t)(port->rx_fis_phys & 0xFFFFFFFF);
    regs->fbu = (uint32_t)(port->rx_fis_phys >> 32);
    
    ahci_port_clear_serr(regs);
    ahci_port_clear_is(regs);
    
    regs->ie = 0;  /* Disable for polling mode */
    
    ret = ahci_port_start(port);
    if (ret != 0) {
        ahci_port_free_memory(port);
        return ret;
    }
    
    ret = ahci_wait_port_ready(regs, 1000);  /* 1 second timeout */
    if (ret != 0) {
        printk("ahci: port %u: device not ready (TFD=0x%x)\n", port_num, regs->tfd);
        /* Continue anyway - might still work */
    }
    
    for (int i = 0; i < 1000; i++) {
        io_wait();
    }
    
    uint32_t sig = 0;
    for (int retry = 0; retry < 10; retry++) {
        sig = regs->sig;
        if (sig != 0 && sig != 0xFFFFFFFF) {
            break;
        }
        /* Wait a bit longer */
        for (int i = 0; i < 10000; i++) {
            io_wait();
        }
    }
    
    if (sig == 0 || sig == 0xFFFFFFFF) {
        printk("ahci: port %u: invalid signature 0x%08x, trying COMRESET\n", 
               port_num, sig);
        
        ret = ahci_port_comreset(port);
        if (ret == 0) {
            /* Re-start the port */
            ret = ahci_port_start(port);
            if (ret != 0) {
                ahci_port_free_memory(port);
                return ret;
            }
            
            for (int i = 0; i < 100000; i++) {
                io_wait();
            }
            
            sig = regs->sig;
            printk("ahci: port %u: signature after COMRESET = 0x%08x\n", 
                   port_num, sig);
        }
    }
    
    port->type = sig;
    
    printk("ahci: port %u: signature = 0x%08x (%s)\n", 
           port_num, sig, ahci_port_type_string(sig));
    
    if (sig == 0xFFFFFFFF) {
        printk("ahci: port %u: no device present\n", port_num);
        ahci_port_stop(port);
        ahci_port_free_memory(port);
        port->implemented = 0;
        return -ENODEV;
    }
    
    if (sig == AHCI_SIG_ATAPI) {
        printk("ahci: port %u: ATAPI device not supported\n", port_num);
        ahci_port_stop(port);
        ahci_port_free_memory(port);
        port->implemented = 0;
        return -ENODEV;
    }
    
    if (sig != AHCI_SIG_ATA && sig != 0) {
        printk("ahci: port %u: unexpected signature, will try IDENTIFY anyway\n", port_num);
    }
    
    ret = ahci_port_identify(port);
    if (ret != 0) {
        printk("ahci: port %u failed to identify device\n", port_num);
        ahci_port_stop(port);
        ahci_port_free_memory(port);
        return ret;
    }
    
    printk("ahci: port %u: %s (%llu MB) - %s\n",
           port_num,
           port->model,
           (port->sectors * 512) / (1024 * 1024),
           ahci_port_type_string(port->type));
    
    return 0;
}

int ahci_port_read(ahci_port_t *port, uint64_t lba, uint32_t count, void *buffer)
{
    hba_port_t *regs = port->regs;
    int slot;
    int ret = 0;
    
    if (lba + count > port->sectors) {
        return -EINVAL;
    }
    
    slot = ahci_find_free_slot(port);
    if (slot < 0) {
        return -EBUSY;
    }
    
    paging_context_t *ctx = paging_get_kernel_context();
    uint64_t buffer_phys = paging_virt_to_phys(ctx, (uint64_t)buffer);
    if (buffer_phys == 0) {
        return -EFAULT;
    }
    
    hba_cmd_header_t *cmd_hdr = &port->cmd_list[slot];
    cmd_hdr->flags = sizeof(fis_reg_h2d_t) / 4;  /* Command FIS size in dwords */
    cmd_hdr->flags &= ~AHCI_CMD_FLAGS_W;  /* Read (D2H) */
    cmd_hdr->prdtl = 1;  /* One PRDT entry */
    cmd_hdr->prdbc = 0;
    
    hba_cmd_tbl_t *cmd_tbl = port->cmd_tables[slot];
    memset(cmd_tbl, 0, sizeof(hba_cmd_tbl_t) + sizeof(hba_prdt_entry_t));
    
    uint32_t bytes = count * 512;
    cmd_tbl->prdt[0].dba = (uint32_t)(buffer_phys & 0xFFFFFFFF);
    cmd_tbl->prdt[0].dbau = (uint32_t)(buffer_phys >> 32);
    cmd_tbl->prdt[0].dbc = (bytes - 1);  /* 0-indexed */
    
    fis_reg_h2d_t *fis = (fis_reg_h2d_t *)&cmd_tbl->cfis;
    memset(fis, 0, sizeof(fis_reg_h2d_t));
    
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->c = 1;  /* Command */
    
    if (lba >= 0x10000000) {
        /* LBA48 */
        fis->command = ATA_CMD_READ_DMA_EXT;
        fis->lba0 = (uint8_t)(lba & 0xFF);
        fis->lba1 = (uint8_t)((lba >> 8) & 0xFF);
        fis->lba2 = (uint8_t)((lba >> 16) & 0xFF);
        fis->lba3 = (uint8_t)((lba >> 24) & 0xFF);
        fis->lba4 = (uint8_t)((lba >> 32) & 0xFF);
        fis->lba5 = (uint8_t)((lba >> 40) & 0xFF);
        fis->device = (1 << 6);  /* LBA mode */
        fis->count_low = (uint8_t)(count & 0xFF);
        fis->count_high = (uint8_t)((count >> 8) & 0xFF);
    } else {
        /* LBA28 */
        fis->command = ATA_CMD_READ_DMA;
        fis->lba0 = (uint8_t)(lba & 0xFF);
        fis->lba1 = (uint8_t)((lba >> 8) & 0xFF);
        fis->lba2 = (uint8_t)((lba >> 16) & 0xFF);
        fis->device = (1 << 6) | ((lba >> 24) & 0xF);  /* LBA mode + LBA bits 27:24 */
        fis->count_low = (uint8_t)(count & 0xFF);
    }
    
    regs->ci = (1 << slot);
    
    uint32_t timeout = 5000000;  /* 5 second timeout */
    while (timeout--) {
        if (!(regs->ci & (1 << slot))) {
            break;
        }
        
        if (regs->is & AHCI_PORT_INT_TFES) {
            printk("ahci: port %u read error at LBA %llu\n", port->port_num, lba);
            ahci_port_clear_is(regs);
            ret = -EIO;
            goto out;
        }
        
        io_wait();
    }
    
    if (regs->ci & (1 << slot)) {
        printk("ahci: port %u read timeout at LBA %llu\n", port->port_num, lba);
        ret = -ETIMEDOUT;
        goto out;
    }
    
    ahci_port_clear_is(regs);
    
out:
    return ret;
}

int ahci_port_write(ahci_port_t *port, uint64_t lba, uint32_t count, const void *buffer)
{
    hba_port_t *regs = port->regs;
    int slot;
    int ret = 0;
    
    if (lba + count > port->sectors) {
        return -EINVAL;
    }
    
    slot = ahci_find_free_slot(port);
    if (slot < 0) {
        return -EBUSY;
    }
    
    paging_context_t *ctx = paging_get_kernel_context();
    uint64_t buffer_phys = paging_virt_to_phys(ctx, (uint64_t)buffer);
    if (buffer_phys == 0) {
        return -EFAULT;
    }
    
    hba_cmd_header_t *cmd_hdr = &port->cmd_list[slot];
    cmd_hdr->flags = sizeof(fis_reg_h2d_t) / 4;  /* Command FIS size in dwords */
    cmd_hdr->flags |= AHCI_CMD_FLAGS_W;  /* Write (H2D) */
    cmd_hdr->prdtl = 1;  /* One PRDT entry */
    cmd_hdr->prdbc = 0;
    
    hba_cmd_tbl_t *cmd_tbl = port->cmd_tables[slot];
    memset(cmd_tbl, 0, sizeof(hba_cmd_tbl_t) + sizeof(hba_prdt_entry_t));
    
    uint32_t bytes = count * 512;
    cmd_tbl->prdt[0].dba = (uint32_t)(buffer_phys & 0xFFFFFFFF);
    cmd_tbl->prdt[0].dbau = (uint32_t)(buffer_phys >> 32);
    cmd_tbl->prdt[0].dbc = (bytes - 1);  /* 0-indexed */
    
    fis_reg_h2d_t *fis = (fis_reg_h2d_t *)&cmd_tbl->cfis;
    memset(fis, 0, sizeof(fis_reg_h2d_t));
    
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->c = 1;  /* Command */
    
    if (lba >= 0x10000000) {
        /* LBA48 */
        fis->command = ATA_CMD_WRITE_DMA_EXT;
        fis->lba0 = (uint8_t)(lba & 0xFF);
        fis->lba1 = (uint8_t)((lba >> 8) & 0xFF);
        fis->lba2 = (uint8_t)((lba >> 16) & 0xFF);
        fis->lba3 = (uint8_t)((lba >> 24) & 0xFF);
        fis->lba4 = (uint8_t)((lba >> 32) & 0xFF);
        fis->lba5 = (uint8_t)((lba >> 40) & 0xFF);
        fis->device = (1 << 6);  /* LBA mode */
        fis->count_low = (uint8_t)(count & 0xFF);
        fis->count_high = (uint8_t)((count >> 8) & 0xFF);
    } else {
        /* LBA28 */
        fis->command = ATA_CMD_WRITE_DMA;
        fis->lba0 = (uint8_t)(lba & 0xFF);
        fis->lba1 = (uint8_t)((lba >> 8) & 0xFF);
        fis->lba2 = (uint8_t)((lba >> 16) & 0xFF);
        fis->device = (1 << 6) | ((lba >> 24) & 0xF);  /* LBA mode + LBA bits 27:24 */
        fis->count_low = (uint8_t)(count & 0xFF);
    }
    
    regs->ci = (1 << slot);
    
    uint32_t timeout = 5000000;  /* 5 second timeout */
    while (timeout--) {
        if (!(regs->ci & (1 << slot))) {
            break;
        }
        
        if (regs->is & AHCI_PORT_INT_TFES) {
            printk("ahci: port %u write error at LBA %llu\n", port->port_num, lba);
            ahci_port_clear_is(regs);
            ret = -EIO;
            goto out;
        }
        
        io_wait();
    }
    
    if (regs->ci & (1 << slot)) {
        printk("ahci: port %u write timeout at LBA %llu\n", port->port_num, lba);
        ret = -ETIMEDOUT;
        goto out;
    }
    
    ahci_port_clear_is(regs);
    
out:
    return ret;
}

static int ahci_blk_open(struct blk_device *blk_dev)
{
    return 0;
}

static void ahci_blk_close(struct blk_device *blk_dev)
{
}

static int ahci_blk_request(struct blk_device *blk_dev, struct blk_request *req)
{
    ahci_port_t *port = (ahci_port_t *)blk_dev->private_data;
    int ret = 0;
    
    spinlock_irq_acquire(&ahci_lock);
    
    switch (req->operation) {
        case BLK_OP_READ:
            ret = ahci_port_read(port, req->sector, req->count, req->buffer);
            break;
            
        case BLK_OP_WRITE:
            ret = ahci_port_write(port, req->sector, req->count, req->buffer);
            break;
            
        case BLK_OP_FLUSH:
            /* TODO: Implement cache flush command */
            ret = 0;
            break;
            
        default:
            ret = -EINVAL;
            break;
    }
    
    spinlock_irq_release(&ahci_lock);
    
    req->status = ret;
    return ret;
}

static int ahci_blk_flush(struct blk_device *blk_dev)
{
    /* TODO: Implement cache flush command */
    return 0;
}

static const struct blk_ops ahci_blk_ops = {
    .open = ahci_blk_open,
    .close = ahci_blk_close,
    .request = ahci_blk_request,
    .ioctl = NULL,
    .flush = ahci_blk_flush,
};

static int ahci_register_blk_device(ahci_port_t *port, uint32_t ctrl_num)
{
    struct blk_device *blk_dev = kmalloc(sizeof(struct blk_device));
    if (!blk_dev) {
        return -ENOMEM;
    }
    
    memset(blk_dev, 0, sizeof(struct blk_device));
    
    static uint8_t drive_letter = 0;
    snprintk(blk_dev->name, sizeof(blk_dev->name), "sd%c", 'a' + drive_letter++);
    
    blk_dev->major = AHCI_MAJOR;
    blk_dev->minor = (ctrl_num << 4) | port->port_num;
    blk_dev->flags = 0;
    blk_dev->block_size = 512;
    blk_dev->num_blocks = port->sectors;
    blk_dev->ops = &ahci_blk_ops;
    blk_dev->private_data = port;
    
    int ret = blk_register_device(blk_dev);
    if (ret != 0) {
        printk("ahci: failed to register block device for port %u\n", port->port_num);
        kfree(blk_dev);
        return ret;
    }
    
    port->blk_dev = blk_dev;
    return 0;
}

static int ahci_hba_reset(ahci_controller_t *ctrl)
{
    hba_mem_t *hba = ctrl->hba_mem;
    
    hba->ghc |= AHCI_GHC_HR;
    
    uint32_t timeout = 1000000;
    while (timeout--) {
        if (!(hba->ghc & AHCI_GHC_HR)) {
            break;
        }
        io_wait();
    }
    
    if (hba->ghc & AHCI_GHC_HR) {
        printk("ahci: HBA reset timeout\n");
        return -ETIMEDOUT;
    }
    
    hba->ghc |= AHCI_GHC_AE;
    
    return 0;
}

void ahci_print_info(ahci_controller_t *ctrl)
{
    hba_mem_t *hba = ctrl->hba_mem;
    
    uint32_t version = hba->vs;
    uint16_t major = (version >> 16) & 0xFFFF;
    uint16_t minor = version & 0xFFFF;
    
    printk("\n");
}

int ahci_probe_controller(pci_device_t *pci_dev)
{
    int ret;
    
    if (ahci_controller_count >= AHCI_MAX_CONTROLLERS) {
        printk("ahci: maximum number of controllers reached\n");
        return -ENOMEM;
    }
    
    ahci_controller_t *ctrl = &ahci_controllers[ahci_controller_count];
    memset(ctrl, 0, sizeof(ahci_controller_t));
    
    ctrl->pci_dev = pci_dev;
    
    uint32_t bar5_low = pci_config_read_dword(pci_dev->bus, pci_dev->device, 
                                              pci_dev->function, PCI_BAR5);
    
    if (bar5_low == 0 || bar5_low == 0xFFFFFFFF) {
        printk("ahci: BAR5 is not valid (0x%x)\n", bar5_low);
        return -ENODEV;
    }
    
    if (bar5_low & PCI_BAR_TYPE_IO) {
        printk("ahci: BAR5 is I/O space, expected memory space\n");
        return -ENODEV;
    }
    
    uint64_t phys_addr;
    if ((bar5_low & PCI_BAR_MEMORY_TYPE_MASK) == PCI_BAR_MEMORY_TYPE_64) {
        /* 64-bit BAR */
        uint32_t bar5_high = pci_config_read_dword(pci_dev->bus, pci_dev->device,
                                                   pci_dev->function, PCI_BAR5 + 4);
        phys_addr = ((uint64_t)bar5_high << 32) | (bar5_low & 0xFFFFFFF0);
    } else {
        /* 32-bit BAR */
        phys_addr = bar5_low & 0xFFFFFFF0;
    }
    
    ctrl->hba_mem_phys = phys_addr;
    
    size_t abar_size = 8192;  /* Use 8KB to be safe, 2 pages */
    
    pci_enable_bus_mastering(pci_dev);
    pci_enable_memory_space(pci_dev);
    
    uint64_t mmio_base = 0xFFFFFE0000000000UL;
    uint64_t virt_addr = mmio_base + (ahci_controller_count * 0x1000000);  /* 16MB per controller */
    
    vm_space_t *kernel_space = vmm_get_kernel_space();
    uint32_t flags = VMM_READ | VMM_WRITE | VMM_NOCACHE;
    
    ret = vmm_map_region(kernel_space, virt_addr, abar_size, flags,
                         VMM_TYPE_PHYS, 0, phys_addr);
    if (ret != 0) {
        printk("ahci: failed to map ABAR (phys=0x%llx, virt=0x%llx, ret=%d)\n", 
               phys_addr, virt_addr, ret);
        return -ENOMEM;
    }
    
    ctrl->hba_mem = (hba_mem_t *)virt_addr;
    
    ret = ahci_hba_reset(ctrl);
    if (ret != 0) {
        return ret;
    }
    
    hba_mem_t *hba = ctrl->hba_mem;
    ctrl->caps = hba->cap;
    ctrl->version = hba->vs;
    ctrl->num_ports = (ctrl->caps & AHCI_CAP_NP_MASK) + 1;
    ctrl->num_cmd_slots = ((ctrl->caps & AHCI_CAP_NCS_MASK) >> AHCI_CAP_NCS_SHIFT) + 1;
    
    ahci_print_info(ctrl);
    
    uint32_t pi = hba->pi;
    for (uint32_t i = 0; i < AHCI_MAX_PORTS; i++) {
        if (!(pi & (1 << i))) {
            continue;  /* Port not implemented */
        }
        
        ret = ahci_port_init(ctrl, i);
        if (ret == 0) {
            /* Register block device */
            ahci_register_blk_device(&ctrl->ports[i], ahci_controller_count);
        }
    }
    
    hba->ghc |= AHCI_GHC_IE;
    
    ahci_controller_count++;
    return 0;
}

void ahci_init(void)
{
    spinlock_irq_init(&ahci_lock);
    
    ahci_controller_count = 0;
    memset(ahci_controllers, 0, sizeof(ahci_controllers));
    
    pci_device_t *dev = pci_find_device_by_class(AHCI_PCI_CLASS, AHCI_PCI_SUBCLASS);
    while (dev) {
        if (dev->prog_if == AHCI_PCI_PROGIF) {
            printk("ahci: found controller at %02x:%02x.%x\n",
                   dev->bus, dev->device, dev->function);
            
            int ret = ahci_probe_controller(dev);
            if (ret != 0) {
                printk("ahci: failed to probe controller (error %d)\n", ret);
            }
        }
        
        dev = NULL;  /* For now, only handle first controller */
    }
    
    if (ahci_controller_count == 0) {
        printk("ahci: no controllers found\n");
    } else {
        printk("ahci: initialized %zu controller(s)\n", ahci_controller_count);
    }
}
