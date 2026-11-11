#pragma once
#include "unifi_data.h"
#include "unifi_json_stream.h"

typedef enum { UNIFI_PARSE_HOSTS, UNIFI_PARSE_SITES, UNIFI_PARSE_NETWORK_SITES, UNIFI_PARSE_DEVICES,
    UNIFI_PARSE_NETWORK_DEVICES, UNIFI_PARSE_DEVICE_STATS, UNIFI_PARSE_CLIENTS,
    UNIFI_PARSE_METRICS_5M, UNIFI_PARSE_METRICS_1H,
    UNIFI_PARSE_SDWAN_CONFIG, UNIFI_PARSE_SDWAN_STATUS, UNIFI_PARSE_WAN, UNIFI_PARSE_VPN, UNIFI_PARSE_WAN_LINKS } unifi_parse_kind_t;

/* Allocate on the heap, never on the worker stack. */
typedef struct {
    ujson_stream_t json;
    unifi_parse_kind_t kind;
    unifi_snapshot_t *out;
    int64_t now;
    char wanted_host[96], wanted_site[64], wanted_reference[64];
    char host[96], site[64], reference[64], selected_reference[64];
    char next_token[160], config_id[64], config_type[24];
    char network_candidate[64];
    unsigned rows, total_count, page_base, selected_offset, selected_seen, seen;
    bool vpn_only, matched, truncated, schema_ok;
    int target_device;
    unifi_site_t row_site;
    int wifi, wired, total_devices, offline_devices;
    bool site_count_fields;
    unifi_device_t row_device, group_devices[UNIFI_DEVICE_MAX];
    unsigned group_count, group_total, group_offline;
    unifi_client_t row_client;
    char client_id[48];
    uint64_t client_hashes[300];
    unsigned client_hash_count;
    int64_t metric_time, latest;
    double avg, max, loss, down, up;
    unsigned metric_fields;
    double latest_avg, latest_max, latest_loss, latest_down, latest_up;
    double hist_down[UNIFI_HISTORY_POINTS], hist_up[UNIFI_HISTORY_POINTS];
    double hist_latency[UNIFI_HISTORY_POINTS], hist_loss[UNIFI_HISTORY_POINTS];
    uint8_t hist_mask[UNIFI_HISTORY_POINTS];
    uint8_t hist_latency_mask[UNIFI_HISTORY_POINTS], hist_loss_mask[UNIFI_HISTORY_POINTS];
    unifi_day_t days[7];
    uint32_t day_mask[7];
    bool sd_has_host, sd_status_seen;
    bool row_up, mesh_member, mesh_connected, mesh_known, mesh_endpoint_connected, mesh_endpoint_offline;
    unsigned mesh_endpoints, mesh_flags;
    int64_t vpn_uptime, vpn_last_seen;
    bool legacy_ok;
    unifi_wan_t row_wans[UNIFI_WAN_MAX];
    char gateway_type[12], gateway_mac[18];
    bool wan_overflow;
    bool primary_configured, backup_configured, primary_issues_known, backup_issues_known;
    unsigned primary_issues, backup_issues;
    double primary_latency, backup_latency;
} unifi_parse_t;

void unifi_parse_init(unifi_parse_t *, unifi_parse_kind_t, unifi_snapshot_t *,
                      const char *host, const char *site, const char *reference, int64_t now);
bool unifi_parse_feed(unifi_parse_t *, const char *, size_t);
bool unifi_parse_finish(unifi_parse_t *);
/* Next page of the same client scan: preserve counters and duplicate guard. */
void unifi_parse_next_client_page(unifi_parse_t *);
int64_t unifi_parse_time(const char *);
void unifi_copy_utf8(char *, size_t, const char *);
