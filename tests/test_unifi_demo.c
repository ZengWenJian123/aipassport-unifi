#include "unifi_demo.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void check_all_client_pages(void)
{
    unifi_snapshot_t s;
    unifi_client_t all[92];
    unsigned wireless = 0, wired = 0, vpn = 0, guest = 0, count = 0;
    for(unsigned offset = 0; offset < 92; offset += UNIFI_CLIENT_PAGE_SIZE) {
        unifi_demo_snapshot(&s, UNIFI_DEMO_EPOCH, offset, false);
        unsigned expected = 92 - offset;
        if(expected > UNIFI_CLIENT_PAGE_SIZE) expected = UNIFI_CLIENT_PAGE_SIZE;
        assert(s.clients.page_offset == offset && s.clients.returned == expected);
        assert(s.clients.total == 92 && s.clients.complete && s.clients.vpn_complete);
        for(unsigned i = 0; i < s.clients.returned; i++) {
            unifi_client_t *client = &s.clients.items[i];
            for(unsigned j = 0; j < count; j++) assert(strcmp(client->ip, all[j].ip));
            all[count++] = *client;
            wireless += !strcmp(client->type, "WIRELESS");
            wired += !strcmp(client->type, "WIRED");
            vpn += !strcmp(client->type, "VPN");
            guest += client->guest;
            if(client->guest) assert(!strcmp(client->type, "WIRELESS"));
            assert(client->connected_epoch > 0 && client->connected_epoch < UNIFI_DEMO_EPOCH);
        }
    }
    assert(count == 92 && wireless == 51 && wired == 38 && vpn == 3 && guest == 4);
    assert(s.clients.wireless == (int)wireless && s.clients.wired == (int)wired);
    assert(s.clients.vpn == (int)vpn && s.clients.guest == (int)guest && !s.clients.teleport);
    for(unsigned i = 0; i < 4; i++) {
        unifi_demo_snapshot(&s, UNIFI_DEMO_EPOCH, i, true);
        assert(s.clients.returned == 3 - i && s.clients.page_offset == i);
        for(unsigned j = 0; j < s.clients.returned; j++) {
            assert(!memcmp(&s.clients.items[j], &all[89 + i + j], sizeof(unifi_client_t)));
            assert(s.clients.items[j].connected_epoch <= s.clients.latest_vpn.connected_epoch);
        }
        assert(!memcmp(&s.clients.latest_vpn, &all[91], sizeof(unifi_client_t)));
        assert(s.clients.latest_vpn_valid);
        assert(s.clients.latest_vpn.connected_epoch == UNIFI_DEMO_EPOCH - 1080);
    }
    unifi_demo_snapshot(&s, UNIFI_DEMO_EPOCH, 92, false);
    assert(s.clients.returned == 0);
    unifi_demo_snapshot(&s, UNIFI_DEMO_EPOCH, UINT32_MAX, true);
    assert(s.clients.returned == 0);
}

int main(void)
{
    check_all_client_pages();
    unifi_snapshot_t s;
    unifi_demo_snapshot(&s, UNIFI_DEMO_EPOCH, 0, false);
    assert(s.demo && s.site_count == 2 && s.device_count == 11);
    assert(s.devices_complete && s.device_total == 11 && s.device_offline == 1);
    unsigned online = 0;
    for(unsigned i = 0; i < s.device_count; i++) online += !strcmp(s.devices[i].state, "ONLINE");
    assert(online == 10 && s.sites[0].devices_online == (int)online);
    assert(s.devices[0].uptime_sec == 168060 && s.devices[0].startup_epoch == UNIFI_DEMO_EPOCH - 168060);
    assert(s.devices[0].cpu_pct == 19 && s.devices[0].memory_pct == 43);
    assert(s.sdwan.available && s.sdwan.total == 1 && s.sdwan.connected == 1);
    assert(!strcmp(s.sdwan.type, "Mesh"));
    unifi_alerts_t alerts;
    unifi_demo_alerts(&alerts, UNIFI_DEMO_EPOCH);
    assert(alerts.count == 3 && alerts.items[0].epoch == s.clients.latest_vpn.connected_epoch);
    puts("UniFi shared demo: 92 unique clients, guest subset, pagination, VPN latest and device/alert consistency PASS");
    return 0;
}
