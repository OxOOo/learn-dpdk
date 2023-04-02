#include "configs.h"
#include "dhcp.h"
#include "common.h"

#include <vector>
#include <memory>

#include <signal.h>

#include <rte_eal.h>
#include <rte_debug.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_arp.h>
#include <rte_icmp.h>
#include <rte_ip.h>
#include <rte_udp.h>

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

// 用状态机管理所有功能
enum class Status
{
    START = 0,          // 初始化状态
    DHCP_START,         // 开始DHCP
    DHCP_DISCOVER_SENT, // 已经发送了DHCP::Discover

    MAIN_LOOP, // 初始化结束，接收数据的主循环

    END, // 结束
};

struct Context
{
    struct rte_mempool *mbuf_pool;

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

    // 一个局域网内参与测试的其他服务器的mac地址，需要用ARP协议获取。
    struct rte_ether_addr lan_dst_mac_addr;

    struct rte_ether_addr gateway_mac_addr; // 网关的MAC地址
};

// 返回`mac_addr`是否为空(全0)
bool is_mac_addr_empty(const struct rte_ether_addr *mac_addr)
{
    for (int i = 0; i < RTE_ETHER_ADDR_LEN; i++)
    {
        if (mac_addr->addr_bytes[i] > 0)
        {
            return false;
        }
    }
    return true;
}

// 表示一个任务，可能是短期任务也可能是长期任务
class Task
{
public:
    const std::string name;

protected:
    Context *context;

public:
    Task(const std::string &name, Context *context) : name(name), context(context) {}
    virtual ~Task() {}

    // 初始化任务，只在最开始运行一次。
    // 如果初始化过程中需要发送什么数据包，可以在这里发送。
    virtual void Setup() {}

    // 这个函数会被不断调用，可以在这个函数中添加定时检查，定时发送数据包。
    virtual void Tick() {}

    enum ProcessResult
    {
        NOT_PROCESSED,
        PROCESSED,
    };
    // 处理一个收到的数据包，如果这个数据包属于改Task，就返回`PROCESSED`，否则返回`NOT_PROCESSED`。
    // 每个数据包只能被一个Task处理。
    virtual ProcessResult TryProcess(struct rte_mbuf *pkt) { return ProcessResult::NOT_PROCESSED; }

    // 任务是否还活着
    virtual bool IsAlive() { return true; }
};

// 常驻任务，响应ARP Request
class ARPReplyTask : public Task
{
public:
    using Task::Task;

    virtual ProcessResult TryProcess(struct rte_mbuf *pkt) override final
    {
        struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(pkt, struct rte_ether_hdr *);
        assert(eth_hdr);
        if (rte_be_to_cpu_16(eth_hdr->ether_type) == RTE_ETHER_TYPE_ARP)
        {
            struct rte_arp_hdr *arp_hdr = (struct rte_arp_hdr *)(eth_hdr + 1);
            if (rte_be_to_cpu_16(arp_hdr->arp_hardware) == RTE_ARP_HRD_ETHER && rte_be_to_cpu_16(arp_hdr->arp_protocol) == RTE_ETHER_TYPE_IPV4 && rte_be_to_cpu_16(arp_hdr->arp_opcode) == RTE_ARP_OP_REQUEST)
            {
                if (arp_hdr->arp_data.arp_tip == context->ip_addr) // 查询的是我的IP地址
                {
                    SendARPReply(arp_hdr->arp_data.arp_sha, arp_hdr->arp_data.arp_sip);
                    return ProcessResult::PROCESSED;
                }
            }
        }
        return ProcessResult::NOT_PROCESSED;
    }

private:
    void SendARPReply(struct rte_ether_addr dst_mac_addr, rte_be32_t dst_ip_addr)
    {
        struct rte_mbuf *pkt = rte_pktmbuf_alloc(context->mbuf_pool);
        if (!pkt)
            rte_exit(EXIT_FAILURE, "Failed to alloc pkt\n");

        struct rte_ether_hdr *seth_hdr = rte_pktmbuf_mtod(pkt, struct rte_ether_hdr *);
        rte_ether_addr_copy(&context->mac_addr, &seth_hdr->src_addr);
        rte_ether_addr_copy(&dst_mac_addr, &seth_hdr->dst_addr);
        seth_hdr->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_ARP);
        struct rte_arp_hdr *sarp_hdr = (struct rte_arp_hdr *)(seth_hdr + 1);
        sarp_hdr->arp_hardware = rte_cpu_to_be_16(RTE_ARP_HRD_ETHER);
        sarp_hdr->arp_protocol = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);
        sarp_hdr->arp_hlen = 6;
        sarp_hdr->arp_plen = 4;
        sarp_hdr->arp_opcode = rte_cpu_to_be_16(RTE_ARP_OP_REPLY);
        sarp_hdr->arp_data.arp_sha = context->mac_addr;
        sarp_hdr->arp_data.arp_sip = context->ip_addr;
        sarp_hdr->arp_data.arp_tha = dst_mac_addr;
        sarp_hdr->arp_data.arp_tip = dst_ip_addr;

        // Fill other DPDK metadata
        pkt->packet_type = RTE_PTYPE_L2_ETHER_ARP;
        const uint32_t PKT_LEN = sizeof(*seth_hdr) + sizeof(*sarp_hdr);
        pkt->pkt_len = PKT_LEN;
        pkt->data_len = pkt->pkt_len;
        pkt->l2_len = sizeof(struct rte_ether_hdr) + sizeof(struct rte_arp_hdr);

        const uint16_t nb_tx = rte_eth_tx_burst(PORT, 0, &pkt, 1);
        assert(nb_tx == 1);

        printf("[ARP] Reply ARP Request\n");
    }
};

// 常驻任务，响应ICMP Ping
class PingReplyTask : public Task
{
public:
    using Task::Task;

    virtual ProcessResult TryProcess(struct rte_mbuf *pkt) override final
    {
        struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(pkt, struct rte_ether_hdr *);
        assert(eth_hdr);
        if (rte_be_to_cpu_16(eth_hdr->ether_type) == RTE_ETHER_TYPE_IPV4)
        {
            struct rte_ipv4_hdr *ip_hdr = (struct rte_ipv4_hdr *)(eth_hdr + 1);
            if (ip_hdr->next_proto_id == IPPROTO_ICMP && ip_hdr->dst_addr == context->ip_addr)
            {
                struct rte_icmp_hdr *icmp_hdr = (struct rte_icmp_hdr *)(ip_hdr + 1);
                if (icmp_hdr->icmp_type == RTE_IP_ICMP_ECHO_REQUEST)
                {
                    SendPingReply(eth_hdr->src_addr, ip_hdr->src_addr, icmp_hdr->icmp_ident, icmp_hdr->icmp_seq_nb, (uint8_t *)(icmp_hdr + 1), rte_be_to_cpu_16(ip_hdr->total_length) - sizeof(*ip_hdr) - sizeof(icmp_hdr));
                    return ProcessResult::PROCESSED;
                }
            }
        }
        return ProcessResult::NOT_PROCESSED;
    }

private:
    void SendPingReply(struct rte_ether_addr dst_mac_addr, rte_be32_t dst_ip_addr, rte_be16_t icmp_ident, rte_be16_t icmp_seq_nb, uint8_t *payload, uint32_t payload_length)
    {
        struct rte_mbuf *pkt = rte_pktmbuf_alloc(context->mbuf_pool);
        if (!pkt)
            rte_exit(EXIT_FAILURE, "Failed to alloc pkt\n");

        struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(pkt, struct rte_ether_hdr *);
        rte_ether_addr_copy(&context->mac_addr, &eth_hdr->src_addr);
        rte_ether_addr_copy(&dst_mac_addr, &eth_hdr->dst_addr);
        eth_hdr->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

        struct rte_ipv4_hdr *ip_hdr = (struct rte_ipv4_hdr *)(eth_hdr + 1);
        memset(ip_hdr, 0, sizeof(*ip_hdr));
        ip_hdr->version_ihl = IP_VHL_DEF;
        ip_hdr->type_of_service = 0;
        ip_hdr->fragment_offset = 0;
        ip_hdr->time_to_live = IP_DEFTTL;
        ip_hdr->packet_id = 0;
        ip_hdr->src_addr = context->ip_addr;
        ip_hdr->dst_addr = dst_ip_addr;
        ip_hdr->next_proto_id = IPPROTO_ICMP;
        ip_hdr->total_length = rte_cpu_to_be_16(sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_icmp_hdr) + payload_length);

        struct rte_icmp_hdr *icmp_hdr = (struct rte_icmp_hdr *)(ip_hdr + 1);
        icmp_hdr->icmp_type = RTE_IP_ICMP_ECHO_REPLY;
        icmp_hdr->icmp_code = 0;
        icmp_hdr->icmp_ident = icmp_ident;
        icmp_hdr->icmp_seq_nb = icmp_seq_nb;
        icmp_hdr->icmp_cksum = 0;
        rte_memcpy((void *)(icmp_hdr + 1), payload, payload_length);
        icmp_hdr->icmp_cksum = CalculateChecksum(reinterpret_cast<uint16_t *>(icmp_hdr), sizeof(*icmp_hdr) + payload_length);

        // Fill other DPDK metadata
        pkt->packet_type = RTE_PTYPE_L2_ETHER | RTE_PTYPE_L3_IPV4 | RTE_PTYPE_L4_ICMP;
        const uint32_t PKT_LEN = sizeof(*eth_hdr) + sizeof(*ip_hdr) + sizeof(*icmp_hdr) + payload_length;
        pkt->pkt_len = PKT_LEN;
        pkt->data_len = pkt->pkt_len;
        pkt->l2_len = sizeof(struct rte_ether_hdr);
        pkt->l3_len = sizeof(struct rte_ipv4_hdr);
        pkt->l4_len = sizeof(struct rte_icmp_hdr);
        pkt->ol_flags |= (RTE_MBUF_F_TX_IPV4 | RTE_MBUF_F_TX_IP_CKSUM);

        const uint16_t nb_tx = rte_eth_tx_burst(PORT, 0, &pkt, 1);
        assert(nb_tx == 1);

        printf("[PING] Reply Ping Request\n");
    }

    uint16_t CalculateChecksum(uint16_t *addr, int len)
    {
        uint32_t sum = 0;
        uint16_t result = 0;
        while (len > 1)
        {
            sum += *addr++;
            len -= 2;
        }
        if (len == 1)
        {
            sum += *(unsigned char *)addr;
        }
        sum = (sum >> 16) + (sum & 0xFFFF);
        sum += (sum >> 16);
        result = ~sum;
        return result;
    }
};

// 接收指定端口的UDP数据包
class UDPReceiveTask : public Task
{
    int port;

public:
    UDPReceiveTask(int port, const std::string &name, Context *context)
        : Task(name, context), port(port)
    {
    }

    virtual ProcessResult TryProcess(struct rte_mbuf *pkt) override final
    {
        struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(pkt, struct rte_ether_hdr *);
        assert(eth_hdr);
        if (rte_be_to_cpu_16(eth_hdr->ether_type) == RTE_ETHER_TYPE_IPV4)
        {
            struct rte_ipv4_hdr *ip_hdr = (struct rte_ipv4_hdr *)(eth_hdr + 1);
            if (ip_hdr->dst_addr == context->ip_addr && ip_hdr->next_proto_id == IPPROTO_UDP)
            {
                struct rte_udp_hdr *udp_hdr = (struct rte_udp_hdr *)(ip_hdr + 1);
                if (rte_be_to_cpu_16(udp_hdr->dst_port) == port)
                {
                    printf("[UDP] Received from %s:%d to %s:%d with message = `%s`\n",
                           format_ipv4(ip_hdr->src_addr).c_str(), (int)rte_be_to_cpu_16(udp_hdr->src_port),
                           format_ipv4(ip_hdr->dst_addr).c_str(), (int)rte_be_to_cpu_16(udp_hdr->dst_port),
                           std::string((char *)(udp_hdr + 1), rte_be_to_cpu_16(udp_hdr->dgram_len) - sizeof(*udp_hdr)).c_str());
                    return ProcessResult::PROCESSED;
                }
            }
        }
        return ProcessResult::NOT_PROCESSED;
    }
};

// 使用ARP协议查询MAC地址
class ARPRequestTask : public Task
{
    rte_be32_t query_ip;             // 要查询的IP
    struct rte_ether_addr *write_to; // 查询到MAC地址之后写到这里

public:
    ARPRequestTask(rte_be32_t query_ip, struct rte_ether_addr *write_to, const std::string &name, Context *context)
        : Task(name, context), query_ip(query_ip), write_to(write_to)
    {
    }

    virtual void Setup() override final
    {
        struct rte_mbuf *pkt = rte_pktmbuf_alloc(context->mbuf_pool);
        if (!pkt)
            rte_exit(EXIT_FAILURE, "Failed to alloc pkt\n");

        struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(pkt, struct rte_ether_hdr *);
        rte_ether_addr_copy(&context->mac_addr, &eth_hdr->src_addr);
        memset(&eth_hdr->dst_addr, 0xFF, sizeof(eth_hdr->dst_addr));
        eth_hdr->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_ARP);

        struct rte_arp_hdr *arp_hdr = (struct rte_arp_hdr *)(eth_hdr + 1);
        arp_hdr->arp_hardware = rte_cpu_to_be_16(RTE_ARP_HRD_ETHER);
        arp_hdr->arp_protocol = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);
        arp_hdr->arp_hlen = 6;
        arp_hdr->arp_plen = 4;
        arp_hdr->arp_opcode = rte_cpu_to_be_16(RTE_ARP_OP_REQUEST);
        arp_hdr->arp_data.arp_sha = context->mac_addr;
        arp_hdr->arp_data.arp_sip = context->ip_addr;
        memset(&arp_hdr->arp_data.arp_tha, 0, sizeof(arp_hdr->arp_data.arp_tha));
        arp_hdr->arp_data.arp_tip = query_ip;

        // Fill other DPDK metadata
        pkt->packet_type = RTE_PTYPE_L2_ETHER_ARP;
        const uint32_t PKT_LEN = sizeof(*eth_hdr) + sizeof(*arp_hdr);
        pkt->pkt_len = PKT_LEN;
        pkt->data_len = pkt->pkt_len;
        pkt->l2_len = sizeof(struct rte_ether_hdr) + sizeof(struct rte_arp_hdr);

        const uint16_t nb_tx = rte_eth_tx_burst(PORT, 0, &pkt, 1);
        assert(nb_tx == 1);

        printf("[ARP] Sent ARP request for %s\n", format_ipv4(query_ip).c_str());
    }

    virtual ProcessResult TryProcess(struct rte_mbuf *pkt) override final
    {
        if (!IsAlive())
            return ProcessResult::NOT_PROCESSED;

        struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(pkt, struct rte_ether_hdr *);
        assert(eth_hdr);
        if (rte_be_to_cpu_16(eth_hdr->ether_type) == RTE_ETHER_TYPE_ARP)
        {
            struct rte_arp_hdr *arp_hdr = (struct rte_arp_hdr *)(eth_hdr + 1);
            if (rte_be_to_cpu_16(arp_hdr->arp_hardware) == RTE_ARP_HRD_ETHER && rte_be_to_cpu_16(arp_hdr->arp_protocol) == RTE_ETHER_TYPE_IPV4 && rte_be_to_cpu_16(arp_hdr->arp_opcode) == RTE_ARP_OP_REPLY)
            {
                if (arp_hdr->arp_data.arp_sip == query_ip)
                {
                    *write_to = arp_hdr->arp_data.arp_sha;
                    printf("[ARP] MAC address for %s is " RTE_ETHER_ADDR_PRT_FMT "\n",
                           format_ipv4(query_ip).c_str(),
                           RTE_ETHER_ADDR_BYTES(write_to));
                    return ProcessResult::PROCESSED;
                }
            }
        }
        return ProcessResult::NOT_PROCESSED;
    }

    // 根据`write_to`是否被写入了数据判断本次ARP请求是否结束
    virtual bool IsAlive()
    {
        return is_mac_addr_empty(write_to);
    }
};

// 向指定IP指定端口定时发送UDP数据包
class UDPSendTask : public Task
{
    int src_port;
    rte_be32_t dst_ip;
    int dst_port;
    struct rte_ether_addr *dst_mac_addr;

    uint64_t tsc_hz;
    uint64_t last_sent_at_tsc;

public:
    UDPSendTask(int src_port, rte_be32_t dst_ip, int dst_port, struct rte_ether_addr *dst_mac_addr, const std::string &name, Context *context)
        : Task(name, context), src_port(src_port), dst_ip(dst_ip), dst_port(dst_port), dst_mac_addr(dst_mac_addr)
    {
        this->tsc_hz = rte_get_tsc_hz();
        this->last_sent_at_tsc = rte_get_tsc_cycles();
    }

    virtual void Tick()
    {
        uint64_t now_tsc = rte_get_tsc_cycles();
        if (now_tsc - last_sent_at_tsc < tsc_hz)
        {
            return;
        }
        last_sent_at_tsc = now_tsc;

        struct rte_mbuf *pkt = rte_pktmbuf_alloc(context->mbuf_pool);
        if (!pkt)
            rte_exit(EXIT_FAILURE, "Failed to alloc pkt\n");

        // UDP要发送的数据
        const char *message = "Hello DPDK\n";

        struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(pkt, struct rte_ether_hdr *);
        rte_ether_addr_copy(&context->mac_addr, &eth_hdr->src_addr);
        rte_ether_addr_copy(dst_mac_addr, &eth_hdr->dst_addr);
        eth_hdr->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

        struct rte_ipv4_hdr *ip_hdr = (struct rte_ipv4_hdr *)(eth_hdr + 1);
        memset(ip_hdr, 0, sizeof(*ip_hdr));
        ip_hdr->version_ihl = IP_VHL_DEF;
        ip_hdr->type_of_service = 0;
        ip_hdr->fragment_offset = 0;
        ip_hdr->time_to_live = IP_DEFTTL;
        ip_hdr->packet_id = 0;
        ip_hdr->src_addr = context->ip_addr;
        ip_hdr->dst_addr = dst_ip;
        ip_hdr->next_proto_id = IPPROTO_UDP;
        ip_hdr->total_length = rte_cpu_to_be_16(sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_udp_hdr) + strlen(message));

        struct rte_udp_hdr *udp_hdr = (struct rte_udp_hdr *)(ip_hdr + 1);
        udp_hdr->src_port = rte_cpu_to_be_16(src_port);
        udp_hdr->dst_port = rte_cpu_to_be_16(dst_port);
        udp_hdr->dgram_len = rte_cpu_to_be_16(sizeof(*udp_hdr) + strlen(message));
        udp_hdr->dgram_cksum = 0; // IPv4中UDP的Checksum可以不计算

        rte_memcpy((void *)(udp_hdr + 1), message, strlen(message));

        // Fill other DPDK metadata
        pkt->packet_type = RTE_PTYPE_L2_ETHER | RTE_PTYPE_L3_IPV4 | RTE_PTYPE_L4_UDP;
        const uint32_t PKT_LEN = sizeof(*eth_hdr) + sizeof(*ip_hdr) + sizeof(*udp_hdr) + strlen(message);
        pkt->pkt_len = PKT_LEN;
        pkt->data_len = pkt->pkt_len;
        pkt->l2_len = sizeof(struct rte_ether_hdr);
        pkt->l3_len = sizeof(struct rte_ipv4_hdr);
        pkt->l4_len = sizeof(struct rte_udp_hdr);
        pkt->ol_flags |= (RTE_MBUF_F_TX_IPV4 | RTE_MBUF_F_TX_IP_CKSUM);

        const uint16_t nb_tx = rte_eth_tx_burst(PORT, 0, &pkt, 1);
        assert(nb_tx == 1);

        printf("[UDP] Send UDP message from %s:%d to %s:%d\n",
               format_ipv4(context->ip_addr).c_str(), src_port,
               format_ipv4(dst_ip).c_str(), dst_port);
    }
};

static void main_loop(Context *context)
{
    printf("\nCore %u main loop. [Ctrl+C to quit]\n", rte_lcore_id());

#define MOVE_STATUS_TO(new_status)                  \
    {                                               \
        printf("move status to %s\n", #new_status); \
        context->status = Status::new_status;       \
    }

    std::vector<std::unique_ptr<Task>> running_tasks;

    // // 用于进行局域网测试的IP地址
    // rte_be32_t lan_test_ip = rte_cpu_to_be_32(RTE_IPV4(192, 168, 86, 155));
    // std::string lan_test_ip_str = "192.168.86.155";

    // // 以下这些任务还没有满足条件，因此还不能放在`running_tasks`中。
    // // 具体的触发条件见`Status::MAIN_LOOP`部分的处理逻辑。
    // Task *send_udp_to_lan = new UDPSendTask(8080, lan_test_ip, 8080, &context->lan_dst_mac_addr, "UDPSend:" + lan_test_ip_str, context);

    // 用于进行广域网测试的IP地址
    rte_be32_t wan_test_ip = rte_cpu_to_be_32(RTE_IPV4(39, 107, 102, 23));
    std::string wan_test_ip_str = "39.107.102.23";

    Task *send_udp_to_wan = new UDPSendTask(8080, wan_test_ip, 8080, &context->gateway_mac_addr, "UDPSend:" + wan_test_ip_str, context);

#define NEW_TASK(__)                                            \
    {                                                           \
        Task *task = static_cast<Task *>(__);                   \
        printf("[TASK] Created %s task\n", task->name.c_str()); \
        task->Setup();                                          \
        running_tasks.emplace_back(task);                       \
    }

    while (!force_quit)
    {
        switch (context->status)
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
                *reinterpret_cast<struct rte_ether_addr *>(&dhcp_client_id->value[1]) = context->mac_addr;
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
            dhcp->xid = context->dhcp_context.xid = rte_cpu_to_be_32(rand());
            dhcp->secs = rte_cpu_to_be_16(1);
            dhcp->flags = 0;
            rte_ether_addr_copy(&context->mac_addr, &dhcp->chaddr);
            dhcp->magic_cookie = DHCP_MAGIC_COOKIE_BE;
            // 将Options拷贝过来
            for (int pos = 0, i = 0; i < dhcp_options_n; i++)
            {
                memcpy(reinterpret_cast<uint8_t *>(dhcp + 1) + pos, dhcp_options[i], reinterpret_cast<dhcp_opt_t *>(dhcp_options[i])->len + 2);
                pos += reinterpret_cast<dhcp_opt_t *>(dhcp_options[i])->len + 2;
            }
            // 设置END
            reinterpret_cast<uint8_t *>(dhcp)[dhcp_total_length - 1] = DHCP_OPT_END_CODE;

            struct rte_mbuf *pkt = rte_pktmbuf_alloc(context->mbuf_pool);
            if (!pkt)
                rte_exit(EXIT_FAILURE, "Failed to alloc pkt\n");

            struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(pkt, struct rte_ether_hdr *);
            rte_ether_addr_copy(&context->mac_addr, &eth_hdr->src_addr);
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
                                    context->ip_addr = dhcp->yiaddr;
                                    printf("[DHCP] Got ip address %s from %s via DHCP\n", format_ipv4(context->ip_addr).c_str(), format_ipv4(dhcp->siaddr).c_str());
                                    // parse options
                                    for (int pos = 0;;)
                                    {
                                        dhcp_opt_t *option = reinterpret_cast<dhcp_opt_t *>(reinterpret_cast<uint8_t *>(dhcp + 1) + pos);
                                        if (option->code == DHCP_OPT_END_CODE)
                                            break;
                                        pos += 2 + option->len;

                                        if (option->code == DHCP_OPT_SUBNET_MASK_CODE && option->len == DHCP_OPT_SUBNET_MASK_LEN)
                                        {
                                            context->netmask = *reinterpret_cast<rte_be32_t *>(option->value);
                                            printf("\t[DHCP] Got netmask %s\n", format_ipv4(context->netmask).c_str());
                                        }
                                        if (option->code == DHCP_OPT_ROUTER_CODE && option->len > 0 && option->len % DHCP_OPT_ROUTER_LEN_DIVISIBLE == 0)
                                        {
                                            context->gateway_addr = *reinterpret_cast<rte_be32_t *>(option->value);
                                            printf("\t[DHCP] Got gateway address %s\n", format_ipv4(context->gateway_addr).c_str());
                                        }
                                        if (option->code == DHCP_OPT_BROADCAST_CODE && option->len == DHCP_OPT_BROADCAST_LEN)
                                        {
                                            context->broadcast_addr = *reinterpret_cast<rte_be32_t *>(option->value);
                                            printf("\t[DHCP] Got broadcast address %s\n", format_ipv4(context->broadcast_addr).c_str());
                                        }
                                        if (option->code == DHCP_OPT_DNS_SERVER_CODE && option->len > 0 && option->len % DHCP_OPT_DNS_SERVER_LEN_DIVISIBLE == 0)
                                        {
                                            context->dns_server_addr = *reinterpret_cast<rte_be32_t *>(option->value);
                                            printf("\t[DHCP] Got DNS server address %s\n", format_ipv4(context->dns_server_addr).c_str());
                                        }
                                    }

                                    NEW_TASK(new ARPReplyTask("ARPReply", context));
                                    NEW_TASK(new PingReplyTask("PingReply", context));
                                    NEW_TASK(new UDPReceiveTask(8080, "UDP:8080", context));
                                    // NEW_TASK(new ARPRequestTask(lan_test_ip, &context->lan_dst_mac_addr, "ARPRequest:" + lan_test_ip_str, context));
                                    NEW_TASK(new ARPRequestTask(context->gateway_addr, &context->gateway_mac_addr, "ARPRequest:gateway", context));
                                    MOVE_STATUS_TO(MAIN_LOOP);
                                }
                            }
                        }
                    }
                }

                rte_pktmbuf_free(pkt);
            }
        }
        break;
        case Status::MAIN_LOOP:
        {
            struct rte_mbuf *bufs[MAX_PKT_BURST];
            const uint16_t nb_rx = rte_eth_rx_burst(PORT, 0, bufs, MAX_PKT_BURST);

            for (int i = 0; i < nb_rx; i++)
            {
                struct rte_mbuf *pkt = bufs[i];
                for (auto &&task : running_tasks)
                {
                    if (task->TryProcess(pkt) == Task::ProcessResult::PROCESSED)
                    {
                        break;
                    }
                }
                rte_pktmbuf_free(pkt);
            }

            for (auto it = running_tasks.begin(); it != running_tasks.end();)
            {
                if (!(*it)->IsAlive())
                {
                    printf("[TASK] task %s ended\n", (*it)->name.c_str());
                    it = running_tasks.erase(it);
                }
                else
                {
                    it++;
                }
            }

            for (auto &&task : running_tasks)
            {
                task->Tick();
            }

            // // 检查是否有`Task`的条件满足了
            // if (send_udp_to_lan && !is_mac_addr_empty(&context->lan_dst_mac_addr))
            // {
            //     NEW_TASK(send_udp_to_lan);
            //     send_udp_to_lan = nullptr;
            // }

            if (send_udp_to_wan && !is_mac_addr_empty(&context->gateway_mac_addr))
            {
                NEW_TASK(send_udp_to_wan);
                send_udp_to_wan = nullptr;
            }
        }
        break;
        case Status::END:
        {
            return;
        }
        default:
            rte_exit(EXIT_FAILURE, "Unknown status %d\n", static_cast<int>(context->status));
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

    Context context;
    memset(&context, 0, sizeof(context));
    context.mbuf_pool = pktmbuf_pool;
    rte_eth_macaddr_get(PORT, &context.mac_addr);

    main_loop(&context);

    /* clean up the EAL */
    rte_eal_cleanup();
    printf("Bye...\n");

    return 0;
}
