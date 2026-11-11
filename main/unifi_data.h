#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UNIFI_SITE_MAX 8
#define UNIFI_DEVICE_MAX 12
#define UNIFI_CLIENT_PAGE_SIZE 6
#define UNIFI_HISTORY_POINTS 48
#define UNIFI_NAME_LEN 64
#define UNIFI_ERROR_LEN 96

typedef struct {
    char ssid[33];
    char password[65];
    char api_key[257];
    char host_id[96];
    char site_id[64];
    char console_url[128];
    /* Optional PEM trust anchor for a LAN console; never disable TLS validation. */
    char ca_pem[2048];
    uint32_t poll_seconds;
    uint8_t brightness;
    bool local_api;
    bool demo;
    /* Kept outside the unchanged config_v1 NVS blob for backward compatibility. */
    uint32_t screen_timeout_sec;
} unifi_config_t;
#define UNIFI_CONFIG_V1_BYTES offsetof(unifi_config_t, screen_timeout_sec)
_Static_assert(UNIFI_CONFIG_V1_BYTES == 2700, "Preserve the installed config_v1 layout");
#define UNIFI_SCREEN_TIMEOUT_DEFAULT 60u
static inline bool unifi_screen_timeout_valid(uint32_t seconds)
{ return seconds==0 || seconds==30 || seconds==60 || seconds==120 || seconds==300; }

#define UNIFI_WAN_MAX 8
/* WAN indices follow the controller's WAN/wan1, WAN2/wan2 identifiers. */
typedef struct {
    bool present, link_known, link_up, enabled_known, enabled, active;
    char state[20];
    double availability_pct;
    int64_t history_epoch;
    uint32_t period_sec;
} unifi_wan_t;

typedef struct {
    char name[UNIFI_NAME_LEN];
    char host_id[96];
    char site_id[64];
    char network_site_id[64];
    char status[20];
    char isp_name[64], isp_organization[64];
    int internet_issues;
    bool internet_issues_valid;
    int clients, devices_online, devices_total;
    double wan_uptime_pct;
    bool counts_valid, uptime_valid;
} unifi_site_t;

typedef struct {
    char id[48], name[UNIFI_NAME_LEN], state[24];
    int64_t startup_epoch, uptime_sec;
    double rx_bps, tx_bps, cpu_pct, memory_pct;
    bool rates_valid, uptime_valid, cpu_valid, memory_valid;
} unifi_device_t;

typedef struct {
    char name[UNIFI_NAME_LEN], ip[48], type[16];
    int64_t connected_epoch;
    bool guest;
} unifi_client_t;

typedef struct {
    double down_bytes, up_bytes;
    uint16_t samples;
    int32_t day_epoch;
    bool valid;
} unifi_day_t;

typedef struct {
    bool available, complete, vpn_complete;
    int total, wireless, wired, guest, vpn, teleport;
    uint32_t page_offset, returned;
    unifi_client_t items[UNIFI_CLIENT_PAGE_SIZE];
    unifi_client_t latest_vpn;
    bool latest_vpn_valid;
    char error[UNIFI_ERROR_LEN];
} unifi_clients_t;

typedef struct {
    bool available;
    int64_t updated_epoch;
    char remote_host[96], remote_site[64];
    int peers;
    int connected, total, hubs, spokes, errors, warnings;
    double primary_latency_ms, backup_latency_ms;
    bool primary_latency_valid, backup_latency_valid;
    char type[24], primary[24], backup[24], error[UNIFI_ERROR_LEN];
} unifi_sdwan_t;

typedef struct {
    bool demo, api_ok, refreshing;
    bool connection_tested, connection_ok;
    int connection_http;
    int64_t connection_epoch;
    char connection_error[UNIFI_ERROR_LEN];
    char query_stage[32];
    uint32_t query_count, query_duration_ms;
    int query_http;
    int http_status;
    int64_t fetched_epoch, metric_epoch;
    uint32_t retry_after_sec;
    char error[UNIFI_ERROR_LEN];
    unifi_site_t sites[UNIFI_SITE_MAX];
    uint8_t site_count, selected_site;
    unifi_device_t devices[UNIFI_DEVICE_MAX];
    uint8_t device_count;
    uint16_t device_total, device_offline;
    bool devices_complete;
    unifi_clients_t clients;
    unifi_sdwan_t sdwan;
    /* Controller WAN byte rates are separate from cloud ISP measurements. */
    unifi_wan_t wans[UNIFI_WAN_MAX];
    bool wans_available, wans_complete;
    int64_t wans_epoch;
    char wans_error[UNIFI_ERROR_LEN], wan_gateway_mac[18];
    bool wan_valid;
    int64_t wan_epoch, clients_epoch;
    double wan_down_bps, wan_up_bps;
    char wan_error[UNIFI_ERROR_LEN];
    float live_down[UNIFI_HISTORY_POINTS], live_up[UNIFI_HISTORY_POINTS];
    int64_t live_epoch[UNIFI_HISTORY_POINTS];
    uint8_t live_count;
    bool metrics_valid;
    double avg_latency_ms, max_latency_ms, loss_pct, down_bps, up_bps;
    float history_down[UNIFI_HISTORY_POINTS], history_up[UNIFI_HISTORY_POINTS];
    /* Same half-hour buckets as traffic: ms / percent, NAN for absent samples. */
    float history_latency[UNIFI_HISTORY_POINTS], history_loss[UNIFI_HISTORY_POINTS];
    uint8_t history_count;
    unifi_day_t days[7];
} unifi_snapshot_t;

typedef struct {
    bool wifi_connected, portal_active, configured, time_valid;
    uint32_t poll_seconds;
    uint32_t screen_timeout_sec;
    uint8_t brightness;
    int rssi;
    char ip[16], ap_ssid[33], ap_password[17];
    char error[UNIFI_ERROR_LEN];
    uint32_t config_generation;
} unifi_connection_t;
