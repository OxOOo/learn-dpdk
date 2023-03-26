#ifndef __COMMON_H__
#define __COMMON_H__

#include <rte_byteorder.h>

#include <string>

std::string format_ipv4(rte_be32_t ip)
{
    char buf[1028];
    sprintf(buf, "%d.%d.%d.%d", (int)(ip & 0xFF), (int)((ip >> 8) & 0xFF), (int)((ip >> 16) & 0xFF), (int)((ip >> 24) & 0xFF));
    return buf;
}

#endif // __COMMON_H__
