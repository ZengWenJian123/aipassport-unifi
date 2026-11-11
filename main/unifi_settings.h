#pragma once
#include "unifi_data.h"
#include "esp_err.h"

/* Settings/Wi-Fi own their task and locks. Calls below do not access LVGL. */
esp_err_t unifi_settings_start(void);
void unifi_settings_get(unifi_config_t *out);
void unifi_settings_connection(unifi_connection_t *out);
/* Queue portal request; networking work runs outside UI / button context. */
void unifi_settings_open_portal(void);
void unifi_settings_close_portal(void);
esp_err_t unifi_settings_select_site(const char *host_id, const char *site_id);
esp_err_t unifi_settings_set_demo(bool enabled);
/* Optional bounded read-only cache used by the embedded web configuration UI. */
void unifi_settings_publish_sites(const unifi_snapshot_t *snapshot, uint32_t generation);
