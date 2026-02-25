#include <drivers/net/ethernet.h>
#include <drivers/net/rtl8139.h>

#include <mm/heap.h>

#include <video/printk.h>

#include <klibc/string.h>

typedef struct {
    uint16_t           ethertype;   /* 0 = slot unused                      */
    eth_proto_handler_t handler;
} proto_entry_t;

static struct {
    uint8_t       mac[ETH_ADDR_LEN];
    proto_entry_t protos[ETH_MAX_PROTOCOLS];
    uint8_t       proto_count;

    uint64_t rx_frames;
    uint64_t rx_bytes;
    uint64_t rx_dropped;        /* No handler registered for EtherType      */
    uint64_t rx_errors;         /* Malformed / too-short frames             */
    uint64_t tx_frames;
    uint64_t tx_bytes;
    uint64_t tx_errors;
} g_eth;

static void eth_receive(const uint8_t *data, uint16_t length)
{
    if (length < ETH_HDR_LEN) {
        g_eth.rx_errors++;
        return;
    }

    g_eth.rx_frames++;
    g_eth.rx_bytes += length;

    const eth_hdr_t *hdr = (const eth_hdr_t *)data;

    uint16_t ethertype = eth_ntohs(hdr->ethertype);

    if (ethertype == ETHERTYPE_VLAN) {
        g_eth.rx_dropped++;
        return;
    }

    eth_frame_t frame = {
        .frame       = data,
        .frame_len   = length,
        .payload     = data + ETH_HDR_LEN,
        .payload_len = (uint16_t)(length - ETH_HDR_LEN),
        .ethertype   = ethertype,
        .src_mac     = hdr->src,
        .dst_mac     = hdr->dst,
    };

    for (int i = 0; i < ETH_MAX_PROTOCOLS; i++) {
        if (g_eth.protos[i].ethertype == ethertype &&
            g_eth.protos[i].handler   != NULL)
        {
            g_eth.protos[i].handler(&frame);
            return;
        }
    }

    g_eth.rx_dropped++;
}

void eth_init(void)
{
    __builtin_memset(&g_eth, 0, sizeof(g_eth));

    rtl8139_get_mac(g_eth.mac);

    rtl8139_rx_handler = eth_receive;

    char macbuf[18];
    eth_mac_sprint(macbuf, g_eth.mac);
    printk("eth: initialised, MAC %s\n", macbuf);
}

int eth_send(const uint8_t dst[ETH_ADDR_LEN],
             uint16_t ethertype,
             const uint8_t *payload,
             uint16_t len)
{
    if (!payload && len > 0) {
        g_eth.tx_errors++;
        return -1;
    }

    if (len > ETH_MAX_PAYLOAD) {
        printk("eth: send: payload too large (%u > %u)\n",
               len, ETH_MAX_PAYLOAD);
        g_eth.tx_errors++;
        return -1;
    }

    uint16_t payload_len = len;
    if (payload_len < ETH_MIN_PAYLOAD)
        payload_len = ETH_MIN_PAYLOAD;   /* Will zero-pad below */

    uint16_t frame_len = ETH_HDR_LEN + payload_len;

    uint8_t *frame = kmalloc(frame_len);
    if (!frame) {
        printk("eth: send: kmalloc failed\n");
        g_eth.tx_errors++;
        return -1;
    }

    eth_hdr_t *hdr = (eth_hdr_t *)frame;
    __builtin_memcpy(hdr->dst, dst,      ETH_ADDR_LEN);
    __builtin_memcpy(hdr->src, g_eth.mac, ETH_ADDR_LEN);
    hdr->ethertype = eth_htons(ethertype);   /* host → network byte order */

    if (len > 0)
        __builtin_memcpy(frame + ETH_HDR_LEN, payload, len);
    if (len < ETH_MIN_PAYLOAD)
        __builtin_memset(frame + ETH_HDR_LEN + len, 0, ETH_MIN_PAYLOAD - len);

    int ret = rtl8139_send(frame, frame_len);

    if (ret == 0) {
        g_eth.tx_frames++;
        g_eth.tx_bytes += frame_len;
    } else {
        g_eth.tx_errors++;
    }

    kfree(frame);
    return ret;
}

int eth_register_protocol(uint16_t ethertype, eth_proto_handler_t handler)
{
    if (!handler)
        return -1;

    for (int i = 0; i < ETH_MAX_PROTOCOLS; i++) {
        if (g_eth.protos[i].ethertype == ethertype &&
            g_eth.protos[i].handler   != NULL) {
            printk("eth: protocol 0x%04x already registered\n", ethertype);
            return -1;
        }
    }

    for (int i = 0; i < ETH_MAX_PROTOCOLS; i++) {
        if (g_eth.protos[i].handler == NULL) {
            g_eth.protos[i].ethertype = ethertype;
            g_eth.protos[i].handler   = handler;
            g_eth.proto_count++;
            printk("eth: registered protocol 0x%04x\n", ethertype);
            return 0;
        }
    }

    printk("eth: protocol table full (max %d)\n", ETH_MAX_PROTOCOLS);
    return -1;
}

void eth_unregister_protocol(uint16_t ethertype)
{
    for (int i = 0; i < ETH_MAX_PROTOCOLS; i++) {
        if (g_eth.protos[i].ethertype == ethertype &&
            g_eth.protos[i].handler   != NULL) {
            g_eth.protos[i].ethertype = 0;
            g_eth.protos[i].handler   = NULL;
            g_eth.proto_count--;
            return;
        }
    }
}

void eth_get_mac(uint8_t buf[ETH_ADDR_LEN])
{
    __builtin_memcpy(buf, g_eth.mac, ETH_ADDR_LEN);
}

bool eth_mac_equals(const uint8_t a[ETH_ADDR_LEN],
                    const uint8_t b[ETH_ADDR_LEN])
{
    for (int i = 0; i < ETH_ADDR_LEN; i++)
        if (a[i] != b[i])
            return false;
    return true;
}

bool eth_mac_is_broadcast(const uint8_t mac[ETH_ADDR_LEN])
{
    for (int i = 0; i < ETH_ADDR_LEN; i++)
        if (mac[i] != 0xFF)
            return false;
    return true;
}

void eth_mac_sprint(char buf[18], const uint8_t mac[ETH_ADDR_LEN])
{
    /* Manual hex formatting — avoids pulling in snprintf for a hot path */
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < ETH_ADDR_LEN; i++) {
        buf[i * 3 + 0] = hex[(mac[i] >> 4) & 0xF];
        buf[i * 3 + 1] = hex[ mac[i]       & 0xF];
        buf[i * 3 + 2] = (i < ETH_ADDR_LEN - 1) ? ':' : '\0';
    }
}

void eth_print_stats(void)
{
    printk("eth stats:\n");
    printk("  rx: %llu frames, %llu bytes, %llu dropped, %llu errors\n",
           g_eth.rx_frames, g_eth.rx_bytes,
           g_eth.rx_dropped, g_eth.rx_errors);
    printk("  tx: %llu frames, %llu bytes, %llu errors\n",
           g_eth.tx_frames, g_eth.tx_bytes, g_eth.tx_errors);
}