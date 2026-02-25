#ifndef ETHERNET_H
#define ETHERNET_H

#include <klibc/types.h>

#define ETH_ADDR_LEN        6       /* MAC address length in bytes          */
#define ETH_HDR_LEN         14      /* dst(6) + src(6) + ethertype(2)       */
#define ETH_MIN_PAYLOAD     46      /* Minimum payload (pad to 60 byte frame)*/
#define ETH_MAX_PAYLOAD     1500    /* Maximum payload (MTU)                 */
#define ETH_MAX_FRAME       1514    /* ETH_HDR_LEN + ETH_MAX_PAYLOAD        */
#define ETH_MIN_FRAME       60      /* Minimum frame without FCS            */

#define ETHERTYPE_IPV4      0x0800
#define ETHERTYPE_ARP       0x0806
#define ETHERTYPE_IPV6      0x86DD
#define ETHERTYPE_VLAN      0x8100
#define ETHERTYPE_LLDP      0x88CC

#define ETH_BROADCAST       { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }
#define ETH_ZERO            { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }

#define ETH_MAX_PROTOCOLS   8

typedef struct __attribute__((packed)) {
    uint8_t  dst[ETH_ADDR_LEN];     /* Destination MAC                      */
    uint8_t  src[ETH_ADDR_LEN];     /* Source MAC                           */
    uint16_t ethertype;             /* EtherType (big-endian)               */
} eth_hdr_t;

typedef struct {
    const uint8_t  *frame;          /* Pointer to start of raw Ethernet frame */
    uint16_t        frame_len;      /* Total frame length (incl. header)      */
    const uint8_t  *payload;        /* Pointer to payload (after eth header)  */
    uint16_t        payload_len;    /* Payload length                         */
    uint16_t        ethertype;      /* EtherType (host byte order)            */
    const uint8_t  *src_mac;        /* Sender's MAC address (6 bytes)         */
    const uint8_t  *dst_mac;        /* Destination MAC address (6 bytes)      */
} eth_frame_t;

typedef void (*eth_proto_handler_t)(const eth_frame_t *frame);

static inline uint16_t eth_htons(uint16_t x)
{
    return (uint16_t)((x >> 8) | (x << 8));
}

static inline uint16_t eth_ntohs(uint16_t x)
{
    return eth_htons(x);   /* Symmetric for 16-bit */
}

static inline uint32_t eth_htonl(uint32_t x)
{
    return ((x & 0xFF000000) >> 24) |
           ((x & 0x00FF0000) >>  8) |
           ((x & 0x0000FF00) <<  8) |
           ((x & 0x000000FF) << 24);
}

static inline uint32_t eth_ntohl(uint32_t x)
{
    return eth_htonl(x);   /* Symmetric for 32-bit */
}

void eth_init(void);

int eth_send(const uint8_t dst[ETH_ADDR_LEN],
             uint16_t ethertype,
             const uint8_t *payload,
             uint16_t len);

int eth_register_protocol(uint16_t ethertype, eth_proto_handler_t handler);
void eth_unregister_protocol(uint16_t ethertype);

void eth_get_mac(uint8_t buf[ETH_ADDR_LEN]);
bool eth_mac_equals(const uint8_t a[ETH_ADDR_LEN],
                    const uint8_t b[ETH_ADDR_LEN]);

bool eth_mac_is_broadcast(const uint8_t mac[ETH_ADDR_LEN]);
void eth_mac_sprint(char buf[18], const uint8_t mac[ETH_ADDR_LEN]);

void eth_print_stats(void);

#endif