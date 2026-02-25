#include <arch/x86_64/serial.h>
#include <arch/x86_64/ioapic.h>
#include <arch/x86_64/intr.h>
#include <arch/x86_64/idt.h>
#include <arch/x86_64/io.h>

#include <drivers/net/rtl8139.h>
#include <drivers/pci.h>

#include <mm/pmm.h>

#include <video/printk.h>

#include <klibc/string.h>

static rtl8139_dev_t g_rtl;
static bool          g_rtl_ready = false;

static bool g_tx_in_flight[RTL_TX_DESC_COUNT];

rtl8139_rx_callback_t rtl8139_rx_handler = NULL;

static inline uint8_t  rtl_r8 (uint16_t reg) { return inb (g_rtl.io_base + reg); }
static inline uint16_t rtl_r16(uint16_t reg) { return inw (g_rtl.io_base + reg); }
static inline uint32_t rtl_r32(uint16_t reg) { return inl (g_rtl.io_base + reg); }

static inline void rtl_w8 (uint16_t reg, uint8_t  v) { outb(g_rtl.io_base + reg, v); }
static inline void rtl_w16(uint16_t reg, uint16_t v) { outw(g_rtl.io_base + reg, v); }
static inline void rtl_w32(uint16_t reg, uint32_t v) { outl(g_rtl.io_base + reg, v); }

static int rtl_wait_cmd(uint8_t mask, uint8_t target, uint32_t timeout_iters)
{
    for (uint32_t i = 0; i < timeout_iters; i++) {
        if ((rtl_r8(RTL_CHIPCMD) & mask) == target)
            return 0;
        io_wait();
    }
    return -1;
}

static void rtl8139_irq_handler(struct interrupt_frame *frame)
{
    (void)frame;

    if (!g_rtl_ready)
        return;

    uint16_t isr = rtl_r16(RTL_INTRSTATUS);
    if (!isr)
        return;   /* Shared IRQ line — not us */

    rtl_w16(RTL_INTRSTATUS, isr);

    if (isr & RTL_ISR_ROK) {
        while (!(rtl_r8(RTL_CHIPCMD) & RTL_CMD_BUFE)) {
            uint8_t *hdr_ptr = g_rtl.rx_buf_virt + g_rtl.rx_offset;

            rtl_rx_header_t hdr;
            __builtin_memcpy(&hdr, hdr_ptr, sizeof(hdr));

            if (!(hdr.status & RTL_RX_STATUS_ROK)) {
                serial_write_string(COM1, "rtl8139: bad rx status, resetting\r\n");
                rtl_w8(RTL_CHIPCMD, RTL_CMD_RESET);
                break;
            }

            uint16_t pkt_len  = hdr.length - 4;
            uint8_t *pkt_data = hdr_ptr + sizeof(rtl_rx_header_t);

            if (pkt_len > 0 && pkt_len <= 1514) {
                if (rtl8139_rx_handler)
                    rtl8139_rx_handler(pkt_data, pkt_len);
            }

            uint16_t consumed = (sizeof(rtl_rx_header_t) + hdr.length + 3) & ~3;
            g_rtl.rx_offset   = (g_rtl.rx_offset + consumed) % (8192 + 16);
            rtl_w16(RTL_RXBUFPTR, (uint16_t)(g_rtl.rx_offset - 0x10));
        }
    }

    if (isr & (RTL_ISR_RER | RTL_ISR_RXOVW | RTL_ISR_FOVW)) {
        serial_write_string(COM1, "rtl8139: rx error\r\n");
        g_rtl.rx_offset = 0;
        rtl_w16(RTL_RXBUFPTR, (uint16_t)(0 - 0x10));
    }

    if (isr & RTL_ISR_TOK) {
        for (int i = 0; i < RTL_TX_DESC_COUNT; i++) {
            if (g_tx_in_flight[i]) {
                if (!(rtl_r32(RTL_TXSTATUS0 + i * 4) & RTL_TX_OWN))
                    g_tx_in_flight[i] = false;
            }
        }
    }

    if (isr & RTL_ISR_TER) {
        printk("rtl8139: tx error isr=0x%04x\n", isr);
    }

    if (isr & RTL_ISR_PUN) {
        g_rtl.link_up = !(rtl_r8(RTL_MEDIASTATUS) & RTL_MSR_LINKB);
        serial_write_string(COM1, "rtl8139: link changed\r\n");
    }
}

int rtl8139_init(void)
{
    pci_device_t *pdev = pci_find_device(RTL8139_VENDOR_ID, RTL8139_DEVICE_ID);
    if (!pdev) {
        printk("rtl8139: device not found\n");
        return -1;
    }

    printk("rtl8139: found at PCI %02x:%02x.%x irq=%d\n",
           pdev->bus, pdev->device, pdev->function, pdev->interrupt_line);

    pci_read_bar(pdev, 0);
    if (!(pdev->bar[0] & PCI_BAR_TYPE_IO)) {
        printk("rtl8139: BAR0 is not I/O space (bar=0x%08x)\n", pdev->bar[0]);
        return -1;
    }
    g_rtl.io_base = (uint16_t)(pdev->bar[0] & 0xFFFFFFFC);
    g_rtl.irq     = pdev->interrupt_line;

    pci_enable_io_space(pdev);
    pci_enable_bus_mastering(pdev);

    rtl_w8(RTL_CFG9346, RTL_9346_UNLOCK);
    rtl_w8(RTL_CONFIG1, 0x00);
    rtl_w8(RTL_CFG9346, RTL_9346_LOCK);

    rtl_w8(RTL_CHIPCMD, RTL_CMD_RESET);
    if (rtl_wait_cmd(RTL_CMD_RESET, 0, 100000) < 0) {
        printk("rtl8139: reset timed out\n");
        return -1;
    }

    for (int i = 0; i < 6; i++)
        g_rtl.mac[i] = rtl_r8(RTL_MAC0 + i);

    printk("rtl8139: MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
           g_rtl.mac[0], g_rtl.mac[1], g_rtl.mac[2],
           g_rtl.mac[3], g_rtl.mac[4], g_rtl.mac[5]);

    size_t rx_pages = BYTES_TO_PAGES(RTL_RX_BUF_SIZE);
    g_rtl.rx_buf_phys = pmm_alloc_pages(rx_pages, PMM_ZONE_DMA32);
    if (!g_rtl.rx_buf_phys) {
        printk("rtl8139: failed to allocate RX buffer\n");
        return -1;
    }
    g_rtl.rx_buf_virt = (uint8_t *)PHYS_TO_VIRT(g_rtl.rx_buf_phys);
    __builtin_memset(g_rtl.rx_buf_virt, 0, RTL_RX_BUF_SIZE);
    g_rtl.rx_offset = 0;

    for (int i = 0; i < RTL_TX_DESC_COUNT; i++) {
        g_rtl.tx_buf_phys[i] = pmm_alloc_page_zone(PMM_ZONE_DMA32);
        if (!g_rtl.tx_buf_phys[i]) {
            printk("rtl8139: failed to allocate TX buffer %d\n", i);
            return -1;
        }
        g_rtl.tx_buf_virt[i] = (uint8_t *)PHYS_TO_VIRT(g_rtl.tx_buf_phys[i]);
    }
    g_rtl.tx_desc_cur = 0;

    for (int i = 0; i < RTL_TX_DESC_COUNT; i++)
        g_tx_in_flight[i] = false;

    rtl_w32(RTL_RXBUF, (uint32_t)(g_rtl.rx_buf_phys & 0xFFFFFFFF));

    rtl_w16(RTL_RXBUFPTR, (uint16_t)(0 - 0x10));

    rtl_w8(RTL_CHIPCMD, RTL_CMD_RXENABLE | RTL_CMD_TXENABLE);

    rtl_w32(RTL_RXCONFIG,
            RTL_RXCFG_MXDMA_UNLIMITED |
            RTL_RXCFG_RBLEN_8K        |
            RTL_RXCFG_WRAP            |
            RTL_RXCFG_AB              |
            RTL_RXCFG_APM);

    rtl_w32(RTL_TXCONFIG,
            RTL_TXCFG_IFG_NORMAL |
            RTL_TXCFG_MXDMA_2048);

    rtl_w16(RTL_INTRSTATUS, 0xFFFF);
    rtl_w16(RTL_INTRMASK,
            RTL_ISR_ROK   |
            RTL_ISR_RER   |
            RTL_ISR_TOK   |
            RTL_ISR_TER   |
            RTL_ISR_RXOVW |
            RTL_ISR_PUN);

    irq_install_handler(g_rtl.irq, rtl8139_irq_handler);

    ioapic_map_isa_irq((uint8_t)g_rtl.irq,
                       (uint8_t)(IRQ0 + g_rtl.irq),
                       0);   /* destination = APIC ID 0 (BSP) */

    g_rtl.link_up = !(rtl_r8(RTL_MEDIASTATUS) & RTL_MSR_LINKB);
    printk("rtl8139: link %s\n", g_rtl.link_up ? "up" : "down");

    g_rtl_ready = true;
    printk("rtl8139: initialised ok\n");
    return 0;
}

int rtl8139_send(const uint8_t *data, uint16_t length)
{
    if (!g_rtl_ready)
        return -1;

    if (!data || length == 0 || length > RTL_TX_MAX_SIZE) {
        printk("rtl8139: send: invalid length %u\n", length);
        return -1;
    }

    uint8_t desc = g_rtl.tx_desc_cur;

    if (g_tx_in_flight[desc]) {
        for (uint32_t spin = 0; spin < 100000; spin++) {
            if (!(rtl_r32(RTL_TXSTATUS0 + desc * 4) & RTL_TX_OWN)) {
                g_tx_in_flight[desc] = false;
                break;
            }
            io_wait();
        }
        if (g_tx_in_flight[desc]) {
            printk("rtl8139: tx descriptor %d still busy after timeout\n", desc);
            return -1;
        }
    }

    __builtin_memcpy(g_rtl.tx_buf_virt[desc], data, length);
    if (length < 60) {
        __builtin_memset(g_rtl.tx_buf_virt[desc] + length, 0, 60 - length);
        length = 60;
    }

    rtl_w32(RTL_TXADDR0   + desc * 4,
            (uint32_t)(g_rtl.tx_buf_phys[desc] & 0xFFFFFFFF));

    g_tx_in_flight[desc] = true;
    rtl_w32(RTL_TXSTATUS0 + desc * 4, length | RTL_TX_ERTXTH_256);

    g_rtl.tx_desc_cur = (desc + 1) % RTL_TX_DESC_COUNT;
    return 0;
}

void rtl8139_get_mac(uint8_t buf[6])
{
    __builtin_memcpy(buf, g_rtl.mac, 6);
}

bool rtl8139_link_up(void)
{
    return g_rtl.link_up;
}