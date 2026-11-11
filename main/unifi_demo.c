#include "unifi_demo.h"

#include <stdio.h>
#include <string.h>

enum { DEMO_WIRELESS = 51, DEMO_WIRED = 38, DEMO_VPN = 3,
       DEMO_CLIENTS = DEMO_WIRELESS + DEMO_WIRED + DEMO_VPN };

static void demo_client(unifi_client_t *client, unsigned index, int64_t now)
{
    memset(client, 0, sizeof(*client));
    if(index < DEMO_WIRELESS) {
        snprintf(client->name, sizeof(client->name), "无线客户端 %02u", index + 1);
        strcpy(client->type, "WIRELESS");
        client->guest = index < 4; /* Guests are a subset of wireless clients. */
    } else if(index < DEMO_WIRELESS + DEMO_WIRED) {
        snprintf(client->name, sizeof(client->name), "有线客户端 %02u", index - DEMO_WIRELESS + 1);
        strcpy(client->type, "WIRED");
    } else {
        unsigned vpn = index - DEMO_WIRELESS - DEMO_WIRED;
        static const int64_t durations[DEMO_VPN] = {172800, 7200, 1080};
        snprintf(client->name, sizeof(client->name), "远程客户端 %02u", vpn + 1);
        strcpy(client->type, "VPN");
        snprintf(client->ip, sizeof(client->ip), "198.51.100.%u", vpn + 1);
        client->connected_epoch = now - durations[vpn];
        return;
    }
    /* Documentation address ranges, never addresses from a user's network. */
    snprintf(client->ip, sizeof(client->ip), "192.0.2.%u", index + 1);
    client->connected_epoch = now - (int64_t)(DEMO_CLIENTS - index) * 1800;
}

void unifi_demo_snapshot(unifi_snapshot_t *s, int64_t now,
                         uint32_t offset, bool vpn_only)
{
    memset(s, 0, sizeof(*s));
    s->demo = s->api_ok = true;
    s->fetched_epoch = now;
    s->metric_epoch = now / 300 * 300;
    s->http_status = 200;
    s->site_count = 2;
    for(unsigned i = 0; i < s->site_count; i++) {
        unifi_site_t *site = &s->sites[i];
        strcpy(site->name, i ? "Demo Home" : "Demo Office");
        strcpy(site->host_id, i ? "demo-home-host" : "demo-office-host");
        strcpy(site->site_id, i ? "demo-home" : "demo-office");
        strcpy(site->network_site_id, i ? "demo-home-network" : "demo-office-network");
        strcpy(site->status, "devices_online");
        strcpy(site->isp_name, "演示运营商");
        strcpy(site->isp_organization, "演示网络");
        site->internet_issues_valid = site->counts_valid = site->uptime_valid = true;
        site->clients = DEMO_CLIENTS;
        site->devices_online = 10;
        site->devices_total = 11;
        site->wan_uptime_pct = 100;
    }

    s->device_count = s->device_total = 11;
    s->device_offline = 1;
    s->devices_complete = true;
    for(unsigned i = 0; i < s->device_count; i++) {
        unifi_device_t *device = &s->devices[i];
        snprintf(device->id, sizeof(device->id), "demo-device-%02u", i + 1);
        if(i == 0) strcpy(device->name, "演示网关");
        else snprintf(device->name, sizeof(device->name), "演示设备 %02u", i);
        bool online = i < 10;
        strcpy(device->state, online ? "ONLINE" : "OFFLINE");
        device->uptime_valid = device->rates_valid = online;
        device->cpu_valid = device->memory_valid = online;
        if(online) {
            device->uptime_sec = 168060 + i * 7200; /* Gateway: 1 d 22 h 41 m. */
            device->startup_epoch = now - device->uptime_sec;
            device->cpu_pct = 19;
            device->memory_pct = 43;
            device->rx_bps = 320000.0 / (i + 1);
            device->tx_bps = 90000.0 / (i + 1);
        }
    }

    s->wans_available=s->wans_complete=true;s->wans_epoch=now;
    for(unsigned i=0;i<2;i++){
        s->wans[i]=(unifi_wan_t){.present=true,.link_known=true,.link_up=true,.enabled_known=true,.enabled=true,.active=i==0,.availability_pct=i?99.9:100,.history_epoch=now,.period_sec=86400};
        strcpy(s->wans[i].state,"online");
    }
    s->wan_valid=true;s->wan_epoch=s->clients_epoch=s->sdwan.updated_epoch=now;
    s->wan_down_bps=2.64e6;s->wan_up_bps=715000;
    s->live_count=UNIFI_HISTORY_POINTS;
    for(unsigned i=0;i<UNIFI_HISTORY_POINTS;i++){s->live_epoch[i]=now-(UNIFI_HISTORY_POINTS-1-i)*30;s->live_down[i]=(float)(1e6+(i%7)*400000);s->live_up[i]=(float)(150000+(i%5)*100000);}
    s->metrics_valid = true;
    s->avg_latency_ms = 5;
    s->max_latency_ms = 46;
    s->loss_pct = 0;
    s->down_bps = 2640000;
    s->up_bps = 720000;
    s->history_count = UNIFI_HISTORY_POINTS;
    for(unsigned i = 0; i < s->history_count; i++) {
        s->history_down[i] = (float)(2000000 + (i % 7) * 500000 + (i == 34 ? 45000000 : 0));
        s->history_up[i] = (float)(300000 + (i % 5) * 100000);
        s->history_latency[i] = (float)(4 + i % 3 + (i == 34 ? 18 : 0));
        s->history_loss[i] = i == 34 ? 0.5f : 0.0f;
    }
    int64_t today = (now + 28800) / 86400 * 86400 - 28800;
    for(unsigned i = 0; i < 7; i++) {
        s->days[i].day_epoch = (int32_t)(today - (6 - i) * 86400);
        s->days[i].samples = i == 6 ? 8 : 24;
        s->days[i].down_bytes = (18 + i * 4) * 1e9;
        s->days[i].up_bytes = (2 + i) * 1e9;
        s->days[i].valid = true;
    }

    s->clients.available = s->clients.complete = s->clients.vpn_complete = true;
    s->clients.total = DEMO_CLIENTS;
    s->clients.wireless = DEMO_WIRELESS;
    s->clients.wired = DEMO_WIRED;
    s->clients.guest = 4;
    s->clients.vpn = DEMO_VPN;
    s->clients.page_offset = offset;
    unsigned filtered_total = vpn_only ? DEMO_VPN : DEMO_CLIENTS;
    for(unsigned i = offset; i < filtered_total && s->clients.returned < UNIFI_CLIENT_PAGE_SIZE; i++) {
        unsigned index = vpn_only ? DEMO_WIRELESS + DEMO_WIRED + i : i;
        demo_client(&s->clients.items[s->clients.returned++], index, now);
    }
    demo_client(&s->clients.latest_vpn, DEMO_CLIENTS - 1, now);
    s->clients.latest_vpn_valid = true;

    /* A deliberate illustration only: the real API never invents Mesh health. */
    s->sdwan.available = true;s->sdwan.peers=2;
    s->sdwan.connected = s->sdwan.total = 1;
    strcpy(s->sdwan.type, "Mesh");
    strcpy(s->sdwan.primary, "no_issues");
    strcpy(s->sdwan.backup, "no_issues");
    s->sdwan.primary_latency_ms = 8;
    s->sdwan.backup_latency_ms = 12;
    s->sdwan.primary_latency_valid = s->sdwan.backup_latency_valid = true;
}

void unifi_demo_alerts(unifi_alerts_t *alerts, int64_t now)
{
    memset(alerts, 0, sizeof(*alerts));
    alerts->count = 3;
    strcpy(alerts->items[0].text, "远程客户端已上线");
    alerts->items[0].epoch = now - 1080;
    strcpy(alerts->items[1].text, "备用链路恢复连接");
    alerts->items[1].epoch = now - 1800;
    strcpy(alerts->items[2].text, "演示设备离线");
    alerts->items[2].epoch = now - 3600;
}
