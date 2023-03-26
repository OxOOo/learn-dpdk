#include "configs.h"
#include "dhcp.h"
#include "common.h"

#include <signal.h>

#include <rte_eal.h>
#include <rte_debug.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_ip.h>

// 默认网卡配置
static struct rte_eth_conf port_conf = {
    .rxmode = {
        .offloads = (RTE_ETH_RX_OFFLOAD_CHECKSUM | RTE_ETH_RX_OFFLOAD_SCATTER),
    },
    .txmode = {
        .mq_mode = RTE_ETH_MQ_TX_NONE,
        .offloads = (RTE_ETH_TX_OFFLOAD_IPV4_CKSUM | RTE_ETH_TX_OFFLOAD_MULTI_SEGS),
    },
};

static void signal_handler(int signum)
{
    if (signum == SIGINT || signum == SIGTERM)
    {
        printf("\n\nSignal %d received, preparing to exit...\n",
               signum);
        force_quit = true;
    }
}

int port_init(int port, struct rte_mempool *mbuf_pool)
{
    const uint16_t rx_rings = 1, tx_rings = 1;

    if (!rte_eth_dev_is_valid_port(port))
        return -1;

    int retval = -1;

    struct rte_eth_dev_info dev_info;
    retval = rte_eth_dev_info_get(port, &dev_info);
    if (retval != 0)
    {
        printf("Error during getting device (port %u) info: %s\n",
               port, strerror(-retval));
        return retval;
    }
    if (dev_info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE)
        port_conf.txmode.offloads |= RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE;

    /* Configure the Ethernet device. */
    retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
    if (retval != 0)
        return retval;

    uint16_t nb_rxd = RX_DESC_DEFAULT;
    uint16_t nb_txd = TX_DESC_DEFAULT;
    retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
    if (retval != 0)
        return retval;

    /* Allocate and set up 1 RX queue per Ethernet port. */
    for (int q = 0; q < rx_rings; q++)
    {
        retval = rte_eth_rx_queue_setup(port, q, nb_rxd,
                                        rte_eth_dev_socket_id(port), nullptr, mbuf_pool);
        if (retval < 0)
            return retval;
    }

    struct rte_eth_txconf txconf = dev_info.default_txconf;
    txconf.offloads = port_conf.txmode.offloads;
    /* Allocate and set up 1 TX queue per Ethernet port. */
    for (int q = 0; q < tx_rings; q++)
    {
        retval = rte_eth_tx_queue_setup(port, q, nb_txd,
                                        rte_eth_dev_socket_id(port), &txconf);
        if (retval < 0)
            return retval;
    }

    /* Starting Ethernet port. 8< */
    retval = rte_eth_dev_start(port);
    /* >8 End of starting of ethernet port. */
    if (retval < 0)
        return retval;

    /* Display the port MAC address. */
    struct rte_ether_addr addr;
    retval = rte_eth_macaddr_get(port, &addr);
    if (retval != 0)
        return retval;

    printf("Port %u MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8
           " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n",
           port, RTE_ETHER_ADDR_BYTES(&addr));

    /* Enable RX in promiscuous mode for the Ethernet device. */
    retval = rte_eth_promiscuous_enable(port);
    /* End of setting RX port in promiscuous mode. */
    if (retval != 0)
        return retval;

    return 0;
}

static void check_port_link_status(int port)
{
#define CHECK_INTERVAL 100 /* 100ms */
#define MAX_CHECK_TIME 90  /* 9s (90 * 100ms) in total */
    printf("\nChecking link status");
    fflush(stdout);

    for (int count = 0; count <= MAX_CHECK_TIME; count++)
    {
        if (force_quit)
            return;

        struct rte_eth_link link;

        memset(&link, 0, sizeof(link));
        int ret = rte_eth_link_get_nowait(port, &link);
        if (ret < 0)
        {
            printf("Port %u link get failed: %s\n",
                   port, rte_strerror(-ret));
            continue;
        }
        if (link.link_status == RTE_ETH_LINK_DOWN)
        {
            printf(".");
            fflush(stdout);
            rte_delay_ms(CHECK_INTERVAL);
            continue;
        }

        printf("done\n");
        return;
    }
    rte_exit(EXIT_FAILURE, "link is not up\n");
}

static void main_loop(struct rte_mempool *mbuf_pool)
{
    printf("\nCore %u main loop. [Ctrl+C to quit]\n", rte_lcore_id());

    // 用状态机管理所有功能
    enum class Status
    {
        START = 0,          // 初始化状态
        DHCP_START,         // 开始DHCP
        DHCP_DISCOVER_SENT, // 已经发送了DHCP::Discover

        END, // 结束
    };

    struct
    {
        Status status; // 当前的状态

        struct rte_ether_addr mac_addr; // 自己的mac地址
        rte_be32_t ip_addr;             // 自己的IP地址
        rte_be32_t netmask;             // 子网掩码
        rte_be32_t broadcast_addr;      // 广播地址
        rte_be32_t gateway_addr;        // 网关地址
        rte_be32_t dns_server_addr;     // DNS服务器地址

        struct
        {
            rte_be32_t xid; // current transaction id
        } dhcp_context;
    } context;

    memset(&context, 0, sizeof(context));
    rte_eth_macaddr_get(PORT, &context.mac_addr);

#define MOVE_STATUS_TO(new_status)                  \
    {                                               \
        printf("move status to %s\n", #new_status); \
        context.status = Status::new_status;        \
    }

    while (!force_quit)
    {
        switch (context.status)
        {
        case Status::START:
        {
            MOVE_STATUS_TO(DHCP_START);
        }
        break;
        case Status::DHCP_START:
        {
            // 构造DHCP Options
            uint8_t *dhcp_options[128];
            int dhcp_options_n = 0;
            {
                dhcp_options[dhcp_options_n] = new uint8_t[DHCP_OPT_DHCP_MESSAGE_LEN + 2];
                dhcp_opt_t *dhcp_message_type = reinterpret_cast<dhcp_opt_t *>(dhcp_options[dhcp_options_n]);
                dhcp_message_type->code = DHCP_OPT_DHCP_MESSAGE_CODE;
                dhcp_message_type->len = DHCP_OPT_DHCP_MESSAGE_LEN;
                dhcp_message_type->value[0] = DHCP_OPT_DHCP_MESSAGE_VALUE_DHCPDISCOVER;
                dhcp_options_n++;

                dhcp_options[dhcp_options_n] = new uint8_t[DHCP_OPT_CLIENT_ID_LEN + 2];
                dhcp_opt_t *dhcp_client_id = reinterpret_cast<dhcp_opt_t *>(dhcp_options[dhcp_options_n]);
                dhcp_client_id->code = DHCP_OPT_CLIENT_ID_CODE;
                dhcp_client_id->len = DHCP_OPT_CLIENT_ID_LEN;
                dhcp_client_id->value[0] = DHCP_OPT_CLIENT_ID_ERHERNET_TYPE;
                *reinterpret_cast<struct rte_ether_addr *>(&dhcp_client_id->value[1]) = context.mac_addr;
                dhcp_options_n++;

                dhcp_options[dhcp_options_n] = new uint8_t[strlen(HOSTNAME) + 2];
                dhcp_opt_t *dhcp_hostname = reinterpret_cast<dhcp_opt_t *>(dhcp_options[dhcp_options_n]);
                dhcp_hostname->code = DHCP_OPT_HOSTNAME_CODE;
                dhcp_hostname->len = strlen(HOSTNAME);
                memcpy(dhcp_hostname->value, HOSTNAME, strlen(HOSTNAME));
                dhcp_options_n++;
            }
            uint32_t dhcp_options_total_length = 0; // options占用的总长度，不包含END
            for (int i = 0; i < dhcp_options_n; i++)
            {
                dhcp_options_total_length += reinterpret_cast<dhcp_opt_t *>(dhcp_options[i])->len + 2;
            }

            // 构造DHCP请求
            uint32_t dhcp_total_length = sizeof(dhcp_t) + dhcp_options_total_length + 1;
            dhcp_t *dhcp = reinterpret_cast<dhcp_t *>(new uint8_t[dhcp_total_length]);
            memset(dhcp, 0, sizeof(dhcp_t));
            dhcp->op = DHCP_OP_BOOTREQUEST;
            dhcp->htype = DHCP_HTYPE_ETHERNET;
            dhcp->hlen = DHCP_HTYPE_ETHERNET_HLEN;
            dhcp->hops = 0;
            dhcp->xid = context.dhcp_context.xid = rte_cpu_to_be_32(rand());
            dhcp->secs = rte_cpu_to_be_16(1);
            dhcp->flags = 0;
            rte_ether_addr_copy(&context.mac_addr, &dhcp->chaddr);
            dhcp->magic_cookie = DHCP_MAGIC_COOKIE_BE;
            // 将Options拷贝过来
            for (int pos = 0, i = 0; i < dhcp_options_n; i++)
            {
                memcpy(reinterpret_cast<uint8_t *>(dhcp + 1) + pos, dhcp_options[i], reinterpret_cast<dhcp_opt_t *>(dhcp_options[i])->len + 2);
                pos += reinterpret_cast<dhcp_opt_t *>(dhcp_options[i])->len + 2;
            }
            // 设置END
            reinterpret_cast<uint8_t *>(dhcp)[dhcp_total_length - 1] = DHCP_OPT_END_CODE;

            struct rte_mbuf *pkt = rte_pktmbuf_alloc(mbuf_pool);
            if (!pkt)
                rte_exit(EXIT_FAILURE, "Failed to alloc pkt\n");

            struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(pkt, struct rte_ether_hdr *);
            rte_ether_addr_copy(&context.mac_addr, &eth_hdr->src_addr);
            memset(&eth_hdr->dst_addr, 0xFF, sizeof(eth_hdr->dst_addr));
            eth_hdr->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

            struct rte_ipv4_hdr *ip_hdr = (struct rte_ipv4_hdr *)(eth_hdr + 1);
            memset(ip_hdr, 0, sizeof(*ip_hdr));
            ip_hdr->version_ihl = IP_VHL_DEF;
            ip_hdr->type_of_service = 0;
            ip_hdr->fragment_offset = 0;
            ip_hdr->time_to_live = IP_DEFTTL;
            ip_hdr->packet_id = 0;
            memset(&ip_hdr->src_addr, 0, sizeof(ip_hdr->src_addr));
            memset(&ip_hdr->dst_addr, 0xFF, sizeof(ip_hdr->dst_addr));
            ip_hdr->next_proto_id = IPPROTO_UDP;
            ip_hdr->total_length = rte_cpu_to_be_16(sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_udp_hdr) + dhcp_total_length);

            struct rte_udp_hdr *udp_hdr = (struct rte_udp_hdr *)(ip_hdr + 1);
            udp_hdr->src_port = rte_cpu_to_be_16(68);
            udp_hdr->dst_port = rte_cpu_to_be_16(67);
            udp_hdr->dgram_cksum = 0;
            udp_hdr->dgram_len = rte_cpu_to_be_16(sizeof(*udp_hdr) + dhcp_total_length);

            rte_memcpy((void *)(udp_hdr + 1), dhcp, dhcp_total_length);

            // Fill other DPDK metadata
            pkt->packet_type = RTE_PTYPE_L2_ETHER | RTE_PTYPE_L3_IPV4 | RTE_PTYPE_L4_UDP;
            const uint32_t PKT_LEN = sizeof(*eth_hdr) + sizeof(*ip_hdr) + sizeof(*udp_hdr) + dhcp_total_length;
            pkt->pkt_len = PKT_LEN;
            pkt->data_len = pkt->pkt_len;
            pkt->l2_len = sizeof(struct rte_ether_hdr);
            pkt->l3_len = sizeof(struct rte_ipv4_hdr);
            pkt->l4_len = sizeof(struct rte_udp_hdr);
            pkt->ol_flags |= (RTE_MBUF_F_TX_IPV4 | RTE_MBUF_F_TX_IP_CKSUM);

            const uint16_t nb_tx = rte_eth_tx_burst(PORT, 0, &pkt, 1);
            assert(nb_tx == 1);

            // 释放内存
            for (int i = 0; i < dhcp_options_n; i++)
            {
                delete[] reinterpret_cast<uint8_t *>(dhcp_options[i]);
                dhcp_options[i] = nullptr;
            }
            delete[] reinterpret_cast<uint8_t *>(dhcp);
            dhcp = nullptr;

            MOVE_STATUS_TO(DHCP_DISCOVER_SENT);
        }
        break;
        case Status::DHCP_DISCOVER_SENT:
        {
            struct rte_mbuf *bufs[MAX_PKT_BURST];
            const uint16_t nb_rx = rte_eth_rx_burst(PORT, 0, bufs, MAX_PKT_BURST);

            if (unlikely(nb_rx == 0))
                continue;

            for (int i = 0; i < nb_rx; i++)
            {
                struct rte_mbuf *pkt = bufs[i];
                {
                    struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(pkt, struct rte_ether_hdr *);
                    assert(eth_hdr);
                    if (rte_be_to_cpu_16(eth_hdr->ether_type) == RTE_ETHER_TYPE_IPV4)
                    {
                        struct rte_ipv4_hdr *ip_hdr = (struct rte_ipv4_hdr *)(eth_hdr + 1);
                        if (ip_hdr->next_proto_id == IPPROTO_UDP)
                        {
                            struct rte_udp_hdr *udp_hdr = (struct rte_udp_hdr *)(ip_hdr + 1);
                            if (rte_be_to_cpu_16(udp_hdr->src_port) == 67 && rte_be_to_cpu_16(udp_hdr->dst_port) == 68 && rte_be_to_cpu_16(udp_hdr->dgram_len) >= sizeof(dhcp_t))
                            {
                                dhcp_t *dhcp = (dhcp_t *)(udp_hdr + 1);
                                if (dhcp->op == DHCP_OP_BOOTREPLY && dhcp->magic_cookie == DHCP_MAGIC_COOKIE_BE)
                                {
                                    context.ip_addr = dhcp->yiaddr;
                                    printf("[DHCP] Got ip address %s from %s via DHCP\n", format_ipv4(context.ip_addr).c_str(), format_ipv4(dhcp->siaddr).c_str());
                                    // parse options
                                    for (int pos = 0;;)
                                    {
                                        dhcp_opt_t *option = reinterpret_cast<dhcp_opt_t *>(reinterpret_cast<uint8_t *>(dhcp + 1) + pos);
                                        if (option->code == DHCP_OPT_END_CODE)
                                            break;
                                        pos += 2 + option->len;

                                        if (option->code == DHCP_OPT_SUBNET_MASK_CODE && option->len == DHCP_OPT_SUBNET_MASK_LEN)
                                        {
                                            context.netmask = *reinterpret_cast<rte_be32_t *>(option->value);
                                            printf("\t[DHCP] Got netmask %s\n", format_ipv4(context.netmask).c_str());
                                        }
                                        if (option->code == DHCP_OPT_ROUTER_CODE && option->len > 0 && option->len % DHCP_OPT_ROUTER_LEN_DIVISIBLE == 0)
                                        {
                                            context.gateway_addr = *reinterpret_cast<rte_be32_t *>(option->value);
                                            printf("\t[DHCP] Got gateway address %s\n", format_ipv4(context.gateway_addr).c_str());
                                        }
                                        if (option->code == DHCP_OPT_BROADCAST_CODE && option->len == DHCP_OPT_BROADCAST_LEN)
                                        {
                                            context.broadcast_addr = *reinterpret_cast<rte_be32_t *>(option->value);
                                            printf("\t[DHCP] Got broadcast address %s\n", format_ipv4(context.broadcast_addr).c_str());
                                        }
                                        if (option->code == DHCP_OPT_DNS_SERVER_CODE && option->len > 0 && option->len % DHCP_OPT_DNS_SERVER_LEN_DIVISIBLE == 0)
                                        {
                                            context.dns_server_addr = *reinterpret_cast<rte_be32_t *>(option->value);
                                            printf("\t[DHCP] Got DNS server address %s\n", format_ipv4(context.dns_server_addr).c_str());
                                        }
                                    }

                                    MOVE_STATUS_TO(END);
                                }
                            }
                        }
                    }
                }

                rte_pktmbuf_free(pkt);
            }
        }
        break;
        case Status::END:
        {
            return;
        }
        default:
            rte_exit(EXIT_FAILURE, "Unknown status %d\n", static_cast<int>(context.status));
        }
    }
}

int main(int argc, char **argv)
{
    srand(time(nullptr));

    int ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_panic("Cannot init EAL\n");

    argc -= ret;
    argv += ret;

    force_quit = false;
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    struct rte_mempool *pktmbuf_pool = rte_pktmbuf_pool_create("mbuf_pool", 8192U,
                                                               MEMPOOL_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE,
                                                               rte_socket_id());
    if (!pktmbuf_pool)
        rte_exit(EXIT_FAILURE, "Cannot init mbuf pool\n");

    if (port_init(PORT, pktmbuf_pool) != 0)
        rte_exit(EXIT_FAILURE, "Cannot init port %" PRIu16 "\n",
                 PORT);

    check_port_link_status(PORT);

    main_loop(pktmbuf_pool);

    /* clean up the EAL */
    rte_eal_cleanup();
    printf("Bye...\n");

    return 0;
}
