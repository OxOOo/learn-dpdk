#ifndef __DHCP_H__
#define __DHCP_H__

#include <cstdint>

#include <rte_ether.h>
#include <rte_ip.h>

// Ref: https://www.rfc-editor.org/rfc/rfc1533
struct dhcp_opt_t
{
    uint8_t code;
    uint8_t len;
    uint8_t value[0];
};

// Ref: https://www.rfc-editor.org/rfc/rfc1533#section-9.4
// Format: [code][len=1][value]
#define DHCP_OPT_DHCP_MESSAGE_CODE 53
#define DHCP_OPT_DHCP_MESSAGE_LEN 1
#define DHCP_OPT_DHCP_MESSAGE_VALUE_DHCPDISCOVER 1
#define DHCP_OPT_DHCP_MESSAGE_VALUE_DHCPOFFER 2
#define DHCP_OPT_DHCP_MESSAGE_VALUE_DHCPREQUEST 3
#define DHCP_OPT_DHCP_MESSAGE_VALUE_DHCPDECLINE 4
#define DHCP_OPT_DHCP_MESSAGE_VALUE_DHCPACK 5
#define DHCP_OPT_DHCP_MESSAGE_VALUE_DHCPNAK 6
#define DHCP_OPT_DHCP_MESSAGE_VALUE_DHCPRELEASE 7

// Ref: https://www.rfc-editor.org/rfc/rfc1533#section-9.12
// Format: [code][len=7][type=1][mac]
#define DHCP_OPT_CLIENT_ID_CODE 61
#define DHCP_OPT_CLIENT_ID_LEN 7
#define DHCP_OPT_CLIENT_ID_ERHERNET_TYPE 1

// Ref: https://www.rfc-editor.org/rfc/rfc1533#section-3.14
// Format: [code][len][hostname] hostname is a non-null-terminated string
#define DHCP_OPT_HOSTNAME_CODE 12

// Ref: https://www.rfc-editor.org/rfc/rfc1533#section-3.3
// Format: [code][len=4][value]
#define DHCP_OPT_SUBNET_MASK_CODE 1
#define DHCP_OPT_SUBNET_MASK_LEN 4

// Ref: https://www.rfc-editor.org/rfc/rfc1533#section-3.5
// Format: [code][len=/4][value]
#define DHCP_OPT_ROUTER_CODE 3
#define DHCP_OPT_ROUTER_LEN_DIVISIBLE 4

// Ref: https://www.rfc-editor.org/rfc/rfc1533#section-5.3
// Format: [code][len=4][value]
#define DHCP_OPT_BROADCAST_CODE 28
#define DHCP_OPT_BROADCAST_LEN 4

// Ref: https://www.rfc-editor.org/rfc/rfc1533#section-3.8
// Format: [code][len=4][value]
#define DHCP_OPT_DNS_SERVER_CODE 6
#define DHCP_OPT_DNS_SERVER_LEN_DIVISIBLE 4

// Ref: https://www.rfc-editor.org/rfc/rfc1533#section-3.2
#define DHCP_OPT_END_CODE 255

// Ref: https://www.rfc-editor.org/rfc/rfc2131
struct dhcp_t
{
    uint8_t op;
    uint8_t htype;
    uint8_t hlen;
    uint8_t hops;
    rte_be32_t xid;
    rte_be16_t secs;
    rte_be16_t flags;

    rte_be32_t ciaddr;
    rte_be32_t yiaddr;
    rte_be32_t siaddr;
    rte_be32_t giaddr;

    struct rte_ether_addr chaddr;
    uint8_t chaddr_padding[10];

    unsigned char server_name[64];
    unsigned char file_name[128];

    rte_be32_t magic_cookie; // see https://www.rfc-editor.org/rfc/rfc2131#section-3
};
static_assert(sizeof(dhcp_t) == 7 * 4 + 16 + 64 + 128 + 4);

#define DHCP_OP_BOOTREQUEST 1
#define DHCP_OP_BOOTREPLY 2
#define DHCP_HTYPE_ETHERNET 1
#define DHCP_HTYPE_ETHERNET_HLEN 6
#define DHCP_MAGIC_COOKIE_BE 0x63538263U // 已经是网络序了

#endif // __DHCP_H__
