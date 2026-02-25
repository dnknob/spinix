#ifndef RTL8139_H
#define RTL8139_H

#include <klibc/types.h>

#define RTL8139_VENDOR_ID       0x10EC
#define RTL8139_DEVICE_ID       0x8139

#define RTL_MAC0                0x00
#define RTL_MAC4                0x04

#define RTL_MAR0                0x08
#define RTL_MAR4                0x0C

#define RTL_TXSTATUS0           0x10
#define RTL_TXSTATUS1           0x14
#define RTL_TXSTATUS2           0x18
#define RTL_TXSTATUS3           0x1C

#define RTL_TXADDR0             0x20
#define RTL_TXADDR1             0x24
#define RTL_TXADDR2             0x28
#define RTL_TXADDR3             0x2C

#define RTL_RXBUF               0x30

#define RTL_RXEARLYCNT          0x34
#define RTL_RXEARLYSTATUS       0x36

#define RTL_CHIPCMD             0x37
#define RTL_CMD_RXENABLE        (1 << 3)
#define RTL_CMD_TXENABLE        (1 << 2)
#define RTL_CMD_RESET           (1 << 4)
#define RTL_CMD_BUFE            (1 << 0)    /* RX buffer empty */

#define RTL_RXBUFPTR            0x38        /* CAPR: software write pointer */
#define RTL_RXBUFADDR           0x3A        /* CBR:  hardware write pointer  */

#define RTL_INTRMASK            0x3C
#define RTL_INTRSTATUS          0x3E

#define RTL_ISR_ROK             (1 << 0)    /* Receive OK             */
#define RTL_ISR_RER             (1 << 1)    /* Receive Error          */
#define RTL_ISR_TOK             (1 << 2)    /* Transmit OK            */
#define RTL_ISR_TER             (1 << 3)    /* Transmit Error         */
#define RTL_ISR_RXOVW           (1 << 4)    /* RX buffer overflow     */
#define RTL_ISR_PUN             (1 << 5)    /* Packet Under-run / Link Change */
#define RTL_ISR_FOVW            (1 << 6)    /* RX FIFO overflow       */
#define RTL_ISR_TDU             (1 << 7)    /* TX descriptor unavail  */
#define RTL_ISR_SERR            (1 << 15)   /* System error           */

#define RTL_TXCONFIG            0x40
#define RTL_TXCFG_IFG_NORMAL    (3 << 24)   /* inter-frame gap        */
#define RTL_TXCFG_MXDMA_2048   (7 << 8)    /* max DMA burst 2048 B   */
#define RTL_TXCFG_CRC           (0 << 16)   /* append CRC (default)   */

#define RTL_RXCONFIG            0x44
#define RTL_RXCFG_MXDMA_UNLIMITED (7 << 8)
#define RTL_RXCFG_RBLEN_8K      (0 << 11)   /* 8K+16 buffer           */
#define RTL_RXCFG_RBLEN_16K     (1 << 11)
#define RTL_RXCFG_RBLEN_32K     (2 << 11)
#define RTL_RXCFG_RBLEN_64K     (3 << 11)
#define RTL_RXCFG_WRAP          (1 << 7)    /* WRAP bit: allow wrapping */
#define RTL_RXCFG_AER           (1 << 5)    /* Accept error packets   */
#define RTL_RXCFG_AR            (1 << 4)    /* Accept runt packets    */
#define RTL_RXCFG_AB            (1 << 3)    /* Accept broadcast       */
#define RTL_RXCFG_AM            (1 << 2)    /* Accept multicast       */
#define RTL_RXCFG_APM           (1 << 1)    /* Accept physical match  */
#define RTL_RXCFG_AAP           (1 << 0)    /* Accept all (promiscuous)*/

#define RTL_CFG9346             0x50
#define RTL_9346_UNLOCK         0xC0
#define RTL_9346_LOCK           0x00

#define RTL_CONFIG1             0x52
#define RTL_CFG1_PM_ENABLE      (1 << 0)

#define RTL_MEDIASTATUS         0x58
#define RTL_MSR_LINKB           (1 << 2)    /* Link Bad (active-low) */

#define RTL_TX_DESC_COUNT       4
#define RTL_TX_MAX_SIZE         1500        /* Max Ethernet payload   */
#define RTL_TX_BUF_SIZE         1536        /* Rounded up, per-desc   */

#define RTL_TX_OWN              (1 << 13)   /* DMA still owns buffer  */
#define RTL_TX_OK               (1 << 15)   /* TX succeeded           */
#define RTL_TX_UNDERRUN         (1 << 14)   /* FIFO underrun          */
#define RTL_TX_ABORTED          (1 << 30)   /* TX aborted             */
#define RTL_TX_CARRIER_LOST     (1 << 31)

#define RTL_TX_ERTXTH_256       (2 << 16)

#define RTL_RX_BUF_SIZE         (8192 + 16 + 1500)   /* ring + guard space */
#define RTL_RX_PAD              4                      /* offset after header*/

typedef struct __attribute__((packed)) {
    uint16_t status;    /* ROK, FAE, CRC, etc. */
    uint16_t length;    /* length including 4-byte CRC */
} rtl_rx_header_t;

#define RTL_RX_STATUS_ROK       (1 << 0)
#define RTL_RX_STATUS_FAE       (1 << 1)    /* Frame alignment error */
#define RTL_RX_STATUS_CRC       (1 << 2)    /* CRC error             */
#define RTL_RX_STATUS_LONG      (1 << 3)    /* Packet too long        */
#define RTL_RX_STATUS_RUNT      (1 << 4)    /* Runt packet            */
#define RTL_RX_STATUS_ISE       (1 << 5)    /* Invalid symbol error   */
#define RTL_RX_STATUS_BAR       (1 << 13)   /* Broadcast              */
#define RTL_RX_STATUS_PAM       (1 << 14)   /* Physical address match */
#define RTL_RX_STATUS_MAR       (1 << 15)   /* Multicast              */

typedef struct {
    uint16_t io_base;           /* I/O BAR base port                      */
    uint8_t  mac[6];            /* Card's MAC address                     */
    uint8_t  irq;               /* IRQ line (0-15)                        */

    uint8_t *rx_buf_virt;       /* Virtual address of RX buffer           */
    phys_addr_t rx_buf_phys;       /* Physical address of RX buffer          */
    uint16_t rx_offset;         /* Current read offset into ring          */

    uint8_t *tx_buf_virt[RTL_TX_DESC_COUNT];
    phys_addr_t tx_buf_phys[RTL_TX_DESC_COUNT];
    uint8_t  tx_desc_cur;       /* Next descriptor to use for TX          */

    bool     link_up;
} rtl8139_dev_t;

typedef void (*rtl8139_rx_callback_t)(const uint8_t *data, uint16_t length);
extern rtl8139_rx_callback_t rtl8139_rx_handler;

int rtl8139_init(void);

int rtl8139_send(const uint8_t *data, uint16_t length);

void rtl8139_get_mac(uint8_t buf[6]);

bool rtl8139_link_up(void);

#endif