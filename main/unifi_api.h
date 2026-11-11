#pragma once
#include "unifi_data.h"
#include "esp_err.h"

/* Blocking, worker-task only; GET operations only. No credentials in logs. */
esp_err_t unifi_api_poll(const unifi_config_t *config, unifi_snapshot_t *snapshot,
                         uint32_t client_offset, bool vpn_only, int device_index);
/* Synchronous, worker-task callback after discovery, before slower statistics.
 * The snapshot is borrowed only for this call; copy it before returning and
 * reject publication if the saved configuration generation has changed.
 * refreshing=true distinguishes discovery progress from a completed refresh. */
typedef void (*unifi_api_progress_cb)(const unifi_snapshot_t *snapshot, void *context);
esp_err_t unifi_api_poll_with_progress(const unifi_config_t *config, unifi_snapshot_t *snapshot,
                                      uint32_t client_offset, bool vpn_only, int device_index,
                                      unifi_api_progress_cb progress, void *context);
/* Fill deliberate synthetic data only when explicitly in demo mode. */
void unifi_api_demo(unifi_snapshot_t *snapshot, uint32_t client_offset, bool vpn_only);

/* Worker-owned: discard cadence caches when the saved configuration changes. */
void unifi_api_reset_cache(void);
