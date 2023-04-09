// 所有的配置内容在这个文件中

#ifndef __CONFIGS_H__
#define __CONFIGS_H__

#include <rte_ethdev.h>

inline bool force_quit = false; // 是否收到了退出信号

#define HOSTNAME "learn-dpdk-dev" // 给自己设定一个Hostname

#define PORT 0 // 用哪个网口

#define MAX_PKT_BURST 32       // 突发数据包数量
#define MEMPOOL_CACHE_SIZE 256 // 暂时还不知道是干嘛的

// 默认RX和TX队列有多少个descriptor
#define RX_DESC_DEFAULT 1024
#define TX_DESC_DEFAULT 1024

/* IPv4 header */
#define IP_DEFTTL 64
#define IP_VERSION 0x40
#define IP_HDRLEN 0x05
#define IP_VHL_DEF (IP_VERSION | IP_HDRLEN)

#endif // __CONFIGS_H__
