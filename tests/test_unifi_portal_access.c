#include "unifi_portal_access.h"
#include <assert.h>
#include <stdio.h>

static socklen_t address(struct sockaddr_storage *out, int family, const char *ip)
{
    memset(out, 0, sizeof(*out));
    if (family == AF_INET) {
        struct sockaddr_in value = {0};
        value.sin_family = AF_INET;
        assert(inet_pton(AF_INET, ip, &value.sin_addr) == 1);
        memcpy(out, &value, sizeof(value));
        return sizeof(value);
    }
    struct sockaddr_in6 value = {0};
    value.sin6_family = AF_INET6;
    assert(inet_pton(AF_INET6, ip, &value.sin6_addr) == 1);
    memcpy(out, &value, sizeof(value));
    return sizeof(value);
}

int main(void)
{
    struct sockaddr_storage local, peer;
    const char *local_ips[] = {"192.168.4.1", "::ffff:192.168.4.1"};
    const char *peer_ips[] = {"192.168.4.2", "::ffff:192.168.4.2"};
    /* Same HTTP connection in native IPv4, dual-stack, or mixed representation. */
    for (unsigned i = 0; i < 2; i++) for (unsigned j = 0; j < 2; j++) {
        socklen_t ll = address(&local, i ? AF_INET6 : AF_INET, local_ips[i]);
        socklen_t pl = address(&peer, j ? AF_INET6 : AF_INET, peer_ips[j]);
        assert(unifi_portal_ap_socket(&local, ll, &peer, pl));
        assert(!unifi_portal_ap_socket(&local, ll - 1, &peer, pl));
        assert(!unifi_portal_ap_socket(&local, ll, &peer, pl - 1));
        uint32_t normalized = 0;
        assert(unifi_portal_ipv4(&peer, pl, &normalized));
        assert(normalized == UINT32_C(0xc0a80402));
    }
    /* lwIP's dual-stack sockaddr_in6 is longer than the old sockaddr_in buffer. */
    socklen_t ll = address(&local, AF_INET6, "::ffff:192.168.4.1");
    socklen_t pl = address(&peer, AF_INET6, "::ffff:192.168.4.254");
    assert(sizeof(struct sockaddr_in6) > sizeof(struct sockaddr_in));
    assert(peer.ss_family != AF_INET); /* The previous guard rejected this. */
    assert(unifi_portal_ap_socket(&local, ll, &peer, pl));
    assert(!unifi_portal_ap_socket(&local, sizeof(struct sockaddr_in), &peer, pl));
    assert(!unifi_portal_ap_socket(&local, ll, &peer, sizeof(struct sockaddr_in)));

    const char *foreign[] = {"::ffff:192.168.5.2", "::ffff:10.0.0.2",
        "::ffff:127.0.0.1", "fe80::1", "2001:db8::1", "::1", "::",
        "::192.168.4.2", "64:ff9b::c0a8:402"};
    for (unsigned i = 0; i < sizeof(foreign)/sizeof(foreign[0]); i++) {
        pl = address(&peer, AF_INET6, foreign[i]);
        assert(!unifi_portal_ap_socket(&local, ll, &peer, pl));
    }
    pl = address(&peer, AF_INET, "192.168.4.2");
    ll = address(&local, AF_INET, "192.168.10.1");
    assert(!unifi_portal_ap_socket(&local, ll, &peer, pl));
    ll = address(&local, AF_INET6, "::ffff:192.168.10.1");
    assert(!unifi_portal_ap_socket(&local, ll, &peer, pl));
    ll = address(&local, AF_INET, "192.168.4.1");
    pl = address(&peer, AF_INET, "192.168.3.2");
    assert(!unifi_portal_ap_socket(&local, ll, &peer, pl));
    pl = address(&peer, AF_INET, "192.168.4.2");
    uint32_t ignored;
    assert(!unifi_portal_ipv4(NULL, pl, &ignored));
    assert(!unifi_portal_ipv4(&peer, 0, &ignored));
    assert(!unifi_portal_ipv4(&peer, pl, NULL));
    peer.ss_family = AF_UNSPEC;
    assert(!unifi_portal_ap_socket(&local, ll, &peer, pl));
    puts("UniFi portal access: IPv4/mapped IPv6 AP, truncated addresses, native IPv6 and non-AP rejection PASS");
    return 0;
}
