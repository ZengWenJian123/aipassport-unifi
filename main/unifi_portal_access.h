#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "lwip/sockets.h"
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

/* ESP-IDF HTTPD uses a dual-stack socket when IPv6 is enabled. lwIP returns
 * IPv4 peers as ::ffff:a.b.c.d on that socket, even for an IPv4 browser URL.
 * Normalize that representation only; native IPv6 is not a setup AP route. */
static inline bool unifi_portal_ipv4(const struct sockaddr_storage *address,
                                    socklen_t length, uint32_t *host_order)
{
    if (!address || !host_order ||
        length < offsetof(struct sockaddr_storage, ss_family) + sizeof(address->ss_family))
        return false;
    if (address->ss_family == AF_INET) {
        if (length < sizeof(struct sockaddr_in)) return false;
        struct sockaddr_in v4;
        memcpy(&v4, address, sizeof(v4));
        *host_order = ntohl(v4.sin_addr.s_addr);
        return true;
    }
#if !defined(ESP_PLATFORM) || LWIP_IPV6
    if (address->ss_family == AF_INET6) {
        if (length < sizeof(struct sockaddr_in6)) return false;
        struct sockaddr_in6 v6;
        memcpy(&v6, address, sizeof(v6));
        static const unsigned char mapped_prefix[12] = {0,0,0,0,0,0,0,0,0,0,0xff,0xff};
        const unsigned char *bytes = (const unsigned char *)&v6.sin6_addr;
        if (memcmp(bytes, mapped_prefix, sizeof(mapped_prefix))) return false;
        uint32_t network_order;
        memcpy(&network_order, bytes + sizeof(mapped_prefix), sizeof(network_order));
        *host_order = ntohl(network_order);
        return true;
    }
#endif
    return false;
}

static inline bool unifi_portal_ap_socket(const struct sockaddr_storage *local,
                                         socklen_t local_length,
                                         const struct sockaddr_storage *peer,
                                         socklen_t peer_length)
{
    uint32_t local_ip, peer_ip;
    return unifi_portal_ipv4(local, local_length, &local_ip) &&
           unifi_portal_ipv4(peer, peer_length, &peer_ip) &&
           local_ip == UINT32_C(0xc0a80401) &&
           (peer_ip & UINT32_C(0xffffff00)) == UINT32_C(0xc0a80400);
}
