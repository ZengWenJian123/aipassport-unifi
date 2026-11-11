#include "unifi_settings.h"
#include "unifi_settings_validation.h"
#include "unifi_portal_access.h"
#include "unifi_site_names.h"
#include "sdkconfig.h"
#include "demo_radio.h"
#include "cJSON.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "nvs.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Application lifetime service; worker owns Wi-Fi changes, scans, flash and server.
 * Event registration/checked netif construction follows demo/blufi-provisioning
 * 9c039cc5127f22072afa83bedb7fa3d8efe635ad and the current demo_wifi rollback.
 * No BLE, LVGL, secret logging, NVS erase, eFuse writes or parallel TLS worker. */
#define PORTAL_SECONDS 600
#define POST_MAX 4096
#define SCAN_MAX 8
#define OPEN_PORTAL BIT0
#define CLOSE_PORTAL BIT1
#define APPLY_CONFIG BIT2
#define SCAN_REQUEST BIT3
#define SCAN_DONE BIT4
#define GOT_IP BIT5

static SemaphoreHandle_t s_lock;
static TaskHandle_t s_task;
static unifi_config_t s_config, s_pending, s_apply;
static bool s_pending_valid, s_nvs_ready, s_wifi_ready, s_wifi_started;
static bool s_wifi_handler_ready, s_ip_handler_ready, s_ntp_started;
static bool s_scan_busy;
static uint8_t s_scan_count;
static wifi_ap_record_t s_scan[SCAN_MAX];
static char s_scan_error[48], s_save_error[64];
static uint32_t s_save_sequence;
static bool s_save_busy;
static unifi_connection_t s_connection;
static unifi_site_t s_sites[UNIFI_SITE_MAX];
static uint8_t s_site_count;
static bool s_api_ok, s_api_refreshing, s_connection_tested, s_connection_ok;
static int s_connection_http;
static int64_t s_connection_epoch;
static char s_connection_error[UNIFI_ERROR_LEN];
static char s_query_stage[32];
static uint32_t s_query_count,s_query_duration_ms;
static int s_query_http;
static int s_api_http;
static char s_api_error[UNIFI_ERROR_LEN];
static int64_t s_api_epoch;
static esp_netif_t *s_sta, *s_ap;
static esp_event_handler_instance_t s_wifi_handler, s_ip_handler;
static httpd_handle_t s_server;
static int64_t s_portal_deadline, s_retry_at;
static uint32_t s_retry_sec = 5;
extern const unsigned char unifi_portal_html_start[] asm("_binary_unifi_portal_html_start");
extern const unsigned char unifi_portal_html_end[] asm("_binary_unifi_portal_html_end");

static void lock(void) { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_lock); }
static void wipe(void *data, size_t len) { volatile unsigned char *p = data; while (len--) *p++ = 0; }
static void report_error(const char *message) { lock(); strlcpy(s_connection.error, message, sizeof(s_connection.error)); unlock(); }
static esp_err_t reject_setting(esp_err_t err, const char *message)
{
    if (s_lock) report_error(message);
    return err;
}
static void clear_setting_error(void)
{
    /* Caller holds s_lock. Preserve unrelated Wi-Fi errors/events. */
    if (!strncmp(s_connection.error, "Settings ", 9) || !strncmp(s_connection.error, "NVS save failed", 15))
        s_connection.error[0] = 0;
}
static void notify(uint32_t bits) { if (s_task) xTaskNotify(s_task, bits, eSetBits); }

void unifi_settings_get(unifi_config_t *out)
{
    if (!out) return;
    if (!s_lock) { memset(out, 0, sizeof(*out)); out->poll_seconds = 30; out->brightness = 70; out->screen_timeout_sec = UNIFI_SCREEN_TIMEOUT_DEFAULT; return; }
    lock(); *out = s_config; unlock();
}

void unifi_settings_connection(unifi_connection_t *out)
{
    if (!out) return;
    if (!s_lock) { memset(out, 0, sizeof(*out)); return; }
    lock();
    *out = s_connection;
    out->poll_seconds = s_config.poll_seconds;
    out->brightness = s_config.brightness;
    out->screen_timeout_sec = s_config.screen_timeout_sec;
    unlock();
}

void unifi_settings_publish_sites(const unifi_snapshot_t *snapshot, uint32_t generation)
{
    if (!s_lock || !snapshot) return;
    lock();
    if (generation != s_connection.config_generation) { unlock(); return; }
    /* Synthetic site IDs must never be offered for saving as a real target. */
    s_site_count = snapshot->demo ? 0 :
        (snapshot->site_count > UNIFI_SITE_MAX ? UNIFI_SITE_MAX : snapshot->site_count);
    memcpy(s_sites, snapshot->sites, s_site_count * sizeof(s_sites[0]));
    s_api_ok = snapshot->api_ok;
    s_api_refreshing = snapshot->refreshing;
    s_connection_tested=snapshot->connection_tested;s_connection_ok=snapshot->connection_ok;
    s_connection_http=snapshot->connection_http;s_connection_epoch=snapshot->connection_epoch;
    strlcpy(s_connection_error,snapshot->connection_error,sizeof(s_connection_error));
    strlcpy(s_query_stage,snapshot->query_stage,sizeof(s_query_stage));
    s_query_count=snapshot->query_count;s_query_duration_ms=snapshot->query_duration_ms;s_query_http=snapshot->query_http;
    s_api_http = snapshot->http_status;
    s_api_epoch = snapshot->fetched_epoch;
    strlcpy(s_api_error, snapshot->error, sizeof(s_api_error));
    unlock();
}

void unifi_settings_open_portal(void) { notify(OPEN_PORTAL); }
void unifi_settings_close_portal(void) { notify(CLOSE_PORTAL); }

static esp_err_t queue_config(const unifi_config_t *config)
{
    if (!s_task || !unifi_config_valid(config))
        return reject_setting(ESP_ERR_INVALID_ARG, "Settings validation failed");
    lock();
    if (s_save_busy) {
        strlcpy(s_connection.error, "Settings save busy; try again", sizeof(s_connection.error));
        unlock(); return ESP_ERR_INVALID_STATE;
    }
    s_pending = *config;
    unifi_config_reset_target_on_source_change(&s_config, &s_pending);
    s_pending_valid = true; s_save_busy = true; s_save_error[0] = 0;
    unlock(); notify(APPLY_CONFIG); return ESP_OK;
}

esp_err_t unifi_settings_select_site(const char *host_id, const char *site_id)
{
    if (!s_task || !host_id || !site_id || strlen(host_id) >= sizeof(s_config.host_id) ||
        strlen(site_id) >= sizeof(s_config.site_id) || !unifi_identifier_valid(host_id) ||
        !unifi_identifier_valid(site_id)) return reject_setting(ESP_ERR_INVALID_ARG, "Settings site selection invalid");
    lock();
    if (s_save_busy) {
        strlcpy(s_connection.error, "Settings save busy; try again", sizeof(s_connection.error));
        unlock(); return ESP_ERR_INVALID_STATE;
    }
    s_pending = s_config;
    strlcpy(s_pending.host_id, host_id, sizeof(s_pending.host_id));
    strlcpy(s_pending.site_id, site_id, sizeof(s_pending.site_id));
    s_pending_valid = true; s_save_busy = true; s_save_error[0] = 0;
    unlock(); notify(APPLY_CONFIG); return ESP_OK;
}

esp_err_t unifi_settings_set_demo(bool enabled)
{
    if (!s_task) return reject_setting(ESP_ERR_INVALID_STATE, "Settings not started");
    lock();
    if (s_save_busy) {
        strlcpy(s_connection.error, "Settings save busy; try again", sizeof(s_connection.error));
        unlock(); return ESP_ERR_INVALID_STATE;
    }
    s_pending = s_config; s_pending.demo = enabled;
    s_pending_valid = true; s_save_busy = true; s_save_error[0] = 0;
    unlock(); notify(APPLY_CONFIG); return ESP_OK;
}

static esp_err_t store_config(const unifi_config_t *config)
{
    if (!s_nvs_ready) return ESP_ERR_INVALID_STATE;
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("unifi_cfg", NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(nvs, "config_v1", config, UNIFI_CONFIG_V1_BYTES);
    if (err == ESP_OK) err = nvs_set_u32(nvs, "screen_idle_s", config->screen_timeout_sec);
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs); return err;
}

static void random_hex(char *out, size_t bytes)
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < bytes; ++i) {
        uint8_t value = (uint8_t)esp_random();
        out[i * 2] = digits[value >> 4]; out[i * 2 + 1] = digits[value & 15];
    }
    out[bytes * 2] = 0;
}

static void common_headers(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "Referrer-Policy", "no-referrer");
    httpd_resp_set_hdr(req, "Content-Security-Policy", "default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; img-src 'self' data:; frame-ancestors 'none'; form-action 'self'; base-uri 'none'");
}

static esp_err_t reply(httpd_req_t *req, const char *status, const char *body)
{
    common_headers(req); httpd_resp_set_status(req, status); httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, body);
}

static bool authorized(httpd_req_t *req)
{
    int fd = httpd_req_to_sockfd(req);
    struct sockaddr_storage peer = {0}, local = {0};
    socklen_t peer_length = sizeof(peer), local_length = sizeof(local);
    if (getpeername(fd, (struct sockaddr *)&peer, &peer_length) ||
        getsockname(fd, (struct sockaddr *)&local, &local_length) ||
        !unifi_portal_ap_socket(&local, local_length, &peer, peer_length)) return false;
    char host[32] = {0}, origin[48] = {0};
    if (httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK ||
        (strcmp(host, "192.168.4.1") && strcmp(host, "192.168.4.1:80"))) return false;
    if (httpd_req_get_hdr_value_len(req, "Origin") &&
        (httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin)) != ESP_OK ||
         (strcmp(origin, "http://192.168.4.1") && strcmp(origin, "http://192.168.4.1:80")))) return false;
    lock(); bool active = s_connection.portal_active; unlock();
    return active;
}

static esp_err_t page_handler(httpd_req_t *req)
{
    if (!authorized(req)) return reply(req, "403 Forbidden", "{\"error\":\"AP access only\"}");
    common_headers(req); httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, (const char *)unifi_portal_html_start, unifi_portal_html_end - unifi_portal_html_start);
}

static esp_err_t json_response(httpd_req_t *req, cJSON *root)
{
    if (!root) return reply(req, "503 Service Unavailable", "{\"error\":\"memory\"}");
    char *json = cJSON_PrintUnformatted(root); cJSON_Delete(root);
    if (!json) return reply(req, "503 Service Unavailable", "{\"error\":\"memory\"}");
    esp_err_t err = reply(req, "200 OK", json); free(json); return err;
}

static esp_err_t status_handler(httpd_req_t *req)
{
    if (!authorized(req)) return reply(req, "403 Forbidden", "{\"error\":\"AP access only\"}");
    cJSON *root = cJSON_CreateObject(); if (!root) return json_response(req, root);
    lock();
    cJSON_AddBoolToObject(root, "wifi_connected", s_connection.wifi_connected);
    cJSON_AddBoolToObject(root, "time_valid", s_connection.time_valid);
    cJSON_AddStringToObject(root, "ssid", s_config.ssid);
    cJSON_AddStringToObject(root, "ip", s_connection.ip);
    cJSON_AddStringToObject(root, "error", s_connection.error);
    cJSON_AddBoolToObject(root, "password_set", *s_config.password != 0);
    cJSON_AddBoolToObject(root, "api_key_set", *s_config.api_key != 0);
    cJSON_AddBoolToObject(root, "ca_set", *s_config.ca_pem != 0);
    cJSON_AddBoolToObject(root, "local_api", s_config.local_api);
    cJSON_AddBoolToObject(root, "demo", s_config.demo);
    cJSON_AddStringToObject(root, "console_url", s_config.console_url);
    cJSON_AddStringToObject(root, "host_id", s_config.host_id);
    cJSON_AddStringToObject(root, "site_id", s_config.site_id);
    cJSON_AddNumberToObject(root, "poll_seconds", s_config.poll_seconds);
    cJSON_AddNumberToObject(root, "brightness", s_config.brightness);
    cJSON_AddNumberToObject(root, "screen_timeout_sec", s_config.screen_timeout_sec);
    cJSON_AddNumberToObject(root, "generation", s_connection.config_generation);
    cJSON_AddNumberToObject(root, "save_sequence", s_save_sequence);
    cJSON_AddBoolToObject(root, "saving", s_save_busy);
    cJSON_AddStringToObject(root, "save_error", s_save_error);
    cJSON_AddBoolToObject(root, "api_ok", s_api_ok);
    cJSON_AddBoolToObject(root, "api_refreshing", s_api_refreshing);
    cJSON_AddBoolToObject(root, "connection_tested", s_connection_tested);
    cJSON_AddBoolToObject(root, "connection_ok", s_connection_ok);
    cJSON_AddNumberToObject(root, "connection_http", s_connection_http);
    cJSON_AddNumberToObject(root, "connection_epoch", (double)s_connection_epoch);
    cJSON_AddStringToObject(root, "connection_error", s_connection_error);
    cJSON_AddStringToObject(root,"query_stage",s_query_stage);
    cJSON_AddNumberToObject(root,"query_count",s_query_count);
    cJSON_AddNumberToObject(root,"query_duration_ms",s_query_duration_ms);
    cJSON_AddNumberToObject(root,"query_http",s_query_http);
    cJSON_AddNumberToObject(root, "api_http", s_api_http);
    cJSON_AddNumberToObject(root, "api_epoch", (double)s_api_epoch);
    cJSON_AddStringToObject(root, "api_error", s_api_error);
    cJSON_AddBoolToObject(root, "scanning", s_scan_busy);
    cJSON_AddStringToObject(root, "scan_error", s_scan_error);
    cJSON *aps = cJSON_AddArrayToObject(root, "networks");
    for (unsigned i = 0; aps && i < s_scan_count; ++i) {
        cJSON *item = cJSON_CreateObject(); if (!item) break;
        cJSON_AddStringToObject(item, "ssid", (const char *)s_scan[i].ssid);
        cJSON_AddNumberToObject(item, "rssi", s_scan[i].rssi);
        cJSON_AddBoolToObject(item, "open", s_scan[i].authmode == WIFI_AUTH_OPEN);
        cJSON_AddItemToArray(aps, item);
    }
    cJSON *sites = cJSON_AddArrayToObject(root, "sites");
    for (unsigned i = 0; sites && i < s_site_count; ++i) {
        cJSON *item = cJSON_CreateObject(); if (!item) break;
        cJSON_AddStringToObject(item, "name", unifi_site_display_name(s_sites[i].name));
        cJSON_AddStringToObject(item, "host_id", s_sites[i].host_id);
        cJSON_AddStringToObject(item, "site_id", s_config.local_api && *s_sites[i].network_site_id ? s_sites[i].network_site_id : s_sites[i].site_id);
        cJSON_AddItemToArray(sites, item);
    }
    unlock(); return json_response(req, root);
}

static bool patch_string(cJSON *json, const char *key, char *dest, size_t capacity, bool keep_empty)
{
    cJSON *value = cJSON_GetObjectItemCaseSensitive(json, key);
    if (!value) return true;
    if (!cJSON_IsString(value) || strlen(value->valuestring) >= capacity) return false;
    if (!keep_empty || *value->valuestring) { wipe(dest, capacity); strlcpy(dest, value->valuestring, capacity); }
    return true;
}

static bool patch_bool(cJSON *json, const char *key, bool *dest)
{
    cJSON *value = cJSON_GetObjectItemCaseSensitive(json, key);
    if (!value) return true;
    if (!cJSON_IsBool(value)) return false;
    *dest = cJSON_IsTrue(value); return true;
}

static esp_err_t config_handler(httpd_req_t *req)
{
    if (!authorized(req)) return reply(req, "403 Forbidden", "{\"error\":\"AP access only\"}");
    char type[48] = {0};
    if (httpd_req_get_hdr_value_str(req, "Content-Type", type, sizeof(type)) != ESP_OK || strncmp(type, "application/json", 16))
        return reply(req, "415 Unsupported Media Type", "{\"error\":\"JSON required\"}");
    if (req->content_len < 2 || req->content_len > POST_MAX)
        return reply(req, "413 Content Too Large", "{\"error\":\"maximum 4096 bytes\"}");
    char *body = calloc(1, req->content_len + 1);
    unifi_config_t *config = malloc(sizeof(*config));
    if (!body || !config) { free(body); free(config); return reply(req, "503 Service Unavailable", "{\"error\":\"memory\"}"); }
    size_t received = 0; bool valid = true;
    while (received < req->content_len) {
        int count = httpd_req_recv(req, body + received, req->content_len - received);
        if (count <= 0) { valid = false; break; } received += (size_t)count;
    }
    cJSON *json = valid && unifi_config_json_shape_valid(body, received) && !strstr(body, "\\u0000") ?
        cJSON_ParseWithLengthOpts(body, received + 1, NULL, true) : NULL;
    unifi_settings_get(config);
    valid = cJSON_IsObject(json);
    if (valid) {
        valid = patch_string(json, "ssid", config->ssid, sizeof(config->ssid), false) &&
            patch_string(json, "password", config->password, sizeof(config->password), true) &&
            patch_string(json, "api_key", config->api_key, sizeof(config->api_key), true) &&
            patch_string(json, "host_id", config->host_id, sizeof(config->host_id), false) &&
            patch_string(json, "site_id", config->site_id, sizeof(config->site_id), false) &&
            patch_string(json, "console_url", config->console_url, sizeof(config->console_url), false) &&
            patch_string(json, "ca_pem", config->ca_pem, sizeof(config->ca_pem), true) &&
            patch_bool(json, "local_api", &config->local_api) && patch_bool(json, "demo", &config->demo);
        bool clear_password = false, clear_key = false, clear_ca = false;
        valid = valid && patch_bool(json, "clear_password", &clear_password) && patch_bool(json, "clear_api_key", &clear_key) && patch_bool(json, "clear_ca", &clear_ca);
        if (clear_password) wipe(config->password, sizeof(config->password));
        if (clear_key) wipe(config->api_key, sizeof(config->api_key));
        if (clear_ca) wipe(config->ca_pem, sizeof(config->ca_pem));
        cJSON *poll = cJSON_GetObjectItemCaseSensitive(json, "poll_seconds");
        cJSON *brightness = cJSON_GetObjectItemCaseSensitive(json, "brightness");
        cJSON *screen_timeout = cJSON_GetObjectItemCaseSensitive(json, "screen_timeout_sec");
        if (screen_timeout) {
            if (!cJSON_IsNumber(screen_timeout) || screen_timeout->valuedouble != screen_timeout->valueint ||
                screen_timeout->valueint < 0 || !unifi_screen_timeout_valid((uint32_t)screen_timeout->valueint)) valid = false;
            else config->screen_timeout_sec = (uint32_t)screen_timeout->valueint;
        }
        if (poll) {
            if (!cJSON_IsNumber(poll) || poll->valuedouble != poll->valueint || poll->valueint < 10 || poll->valueint > 3600) valid = false;
            else config->poll_seconds = (uint32_t)poll->valueint;
        }
        if (brightness) {
            if (!cJSON_IsNumber(brightness) || brightness->valuedouble != brightness->valueint || brightness->valueint < 10 || brightness->valueint > 100) valid = false;
            else config->brightness = (uint8_t)brightness->valueint;
        }
    }
    esp_err_t result = valid && unifi_config_valid(config) ? queue_config(config) : ESP_ERR_INVALID_ARG;
    if (json) {
        cJSON *v = NULL;
        cJSON_ArrayForEach(v, json) { if (cJSON_IsString(v)) wipe(v->valuestring, strlen(v->valuestring)); }
        cJSON_Delete(json);
    }
    wipe(body, req->content_len + 1); free(body); wipe(config, sizeof(*config)); free(config);
    if (result == ESP_ERR_INVALID_STATE) return reply(req, "409 Conflict", "{\"error\":\"save busy\"}");
    if (result != ESP_OK) return reply(req, "400 Bad Request", "{\"error\":\"invalid configuration\"}");
    return reply(req, "202 Accepted", "{\"queued\":true}");
}

static esp_err_t action_handler(httpd_req_t *req)
{
    if (!authorized(req)) return reply(req, "403 Forbidden", "{\"error\":\"AP access only\"}");
    if (req->content_len) return reply(req, "400 Bad Request", "{\"error\":\"empty body required\"}");
    if (!strcmp(req->uri, "/api/scan")) notify(SCAN_REQUEST);
    else notify(CLOSE_PORTAL);
    return reply(req, "202 Accepted", "{\"queued\":true}");
}

static void stop_portal(void)
{
    if (s_server) { httpd_stop(s_server); s_server = NULL; }
    if (s_wifi_ready) esp_wifi_set_mode(WIFI_MODE_STA);
    lock(); s_connection.portal_active = false;
    wipe(s_connection.ap_password, sizeof(s_connection.ap_password)); unlock();
    s_portal_deadline = 0;
}

static esp_err_t start_portal(void)
{
    lock(); bool active = s_connection.portal_active; unlock();
    if (active) { s_portal_deadline = esp_timer_get_time() + (int64_t)PORTAL_SECONDS * 1000000; return ESP_OK; }
    if (!s_ap) {
        esp_netif_config_t netif = ESP_NETIF_DEFAULT_WIFI_AP();
        s_ap = esp_netif_new(&netif);
        if (!s_ap) return ESP_ERR_NO_MEM;
        esp_err_t err = esp_netif_attach_wifi_ap(s_ap);
        if (err == ESP_OK) err = esp_wifi_set_default_wifi_ap_handlers();
        if (err != ESP_OK) { esp_netif_destroy_default_wifi(s_ap); s_ap = NULL; return err; }
    }
    wifi_config_t ap = {0};
    /* User-selected, easy-to-enter password for the temporary setup AP only. */
    char suffix[7], password[17] = "12345678";
    random_hex(suffix, 3);
    snprintf((char *)ap.ap.ssid, sizeof(ap.ap.ssid), "Passport-%s", suffix);
    strlcpy((char *)ap.ap.password, password, sizeof(ap.ap.password));
    ap.ap.ssid_len = strlen((char *)ap.ap.ssid); ap.ap.channel = 1;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK; ap.ap.max_connection = 1;
    ap.ap.pmf_cfg.capable = true;
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_AP, &ap);
    if (err == ESP_OK) {
        lock(); strlcpy(s_connection.ap_ssid, (char *)ap.ap.ssid, sizeof(s_connection.ap_ssid));
        strlcpy(s_connection.ap_password, password, sizeof(s_connection.ap_password));
        s_connection.portal_active = true; unlock();
        httpd_config_t cfg = HTTPD_DEFAULT_CONFIG(); cfg.stack_size = 6144;
        cfg.max_open_sockets = 2; cfg.lru_purge_enable = true; cfg.max_uri_handlers = 5;
        cfg.recv_wait_timeout = 4; cfg.send_wait_timeout = 4;
        err = httpd_start(&s_server, &cfg);
        const httpd_uri_t routes[] = {
            {.uri="/", .method=HTTP_GET, .handler=page_handler},
            {.uri="/api/status", .method=HTTP_GET, .handler=status_handler},
            {.uri="/api/config", .method=HTTP_POST, .handler=config_handler},
            {.uri="/api/scan", .method=HTTP_POST, .handler=action_handler},
            {.uri="/api/close", .method=HTTP_POST, .handler=action_handler},
        };
        for (unsigned i = 0; err == ESP_OK && i < sizeof(routes)/sizeof(routes[0]); ++i)
            err = httpd_register_uri_handler(s_server, &routes[i]);
    }
    wipe(password, sizeof(password)); wipe(&ap, sizeof(ap));
    if (err != ESP_OK) stop_portal();
    else s_portal_deadline = esp_timer_get_time() + (int64_t)PORTAL_SECONDS * 1000000;
    return err;
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = data;
        lock(); s_connection.wifi_connected = true; s_connection.error[0] = 0;
        snprintf(s_connection.ip, sizeof(s_connection.ip), IPSTR, IP2STR(&event->ip_info.ip)); unlock();
        notify(GOT_IP);
    } else if (id == WIFI_EVENT_STA_DISCONNECTED && base == WIFI_EVENT) {
        wifi_event_sta_disconnected_t *event = data;
        lock(); s_connection.wifi_connected = false; s_connection.ip[0] = 0;
        snprintf(s_connection.error, sizeof(s_connection.error), "Wi-Fi disconnected (%u)", event->reason); unlock();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) notify(SCAN_DONE);
}

static esp_err_t apply_station(const unifi_config_t *config)
{
    wifi_config_t station = {0};
    strlcpy((char *)station.sta.ssid, config->ssid, sizeof(station.sta.ssid));
    /* Driver has exact-size buffers; memcpy preserves valid 32-byte SSIDs / 64-hex PSKs. */
    memcpy(station.sta.ssid, config->ssid, strlen(config->ssid));
    memcpy(station.sta.password, config->password, strlen(config->password));
    station.sta.pmf_cfg.capable = true;
    esp_wifi_disconnect();
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &station);
    wipe(&station, sizeof(station));
    lock(); s_connection.wifi_connected = false; s_connection.ip[0] = 0; unlock();
    s_retry_at = 0; s_retry_sec = 5; return err;
}

static void wifi_cleanup(void)
{
    stop_portal();
    if (s_wifi_started) { esp_wifi_stop(); s_wifi_started = false; }
    if (s_wifi_handler_ready) { esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_handler); s_wifi_handler_ready = false; }
    if (s_ip_handler_ready) { esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_ip_handler); s_ip_handler_ready = false; }
    if (s_wifi_ready) { esp_wifi_deinit(); s_wifi_ready = false; }
    if (s_sta) { esp_netif_destroy_default_wifi(s_sta); s_sta = NULL; }
    if (s_ap) { esp_netif_destroy_default_wifi(s_ap); s_ap = NULL; }
}

static esp_err_t wifi_init(void)
{
    esp_err_t err = demo_radio_network_prepare(); if (err != ESP_OK) return err;
    esp_netif_config_t netif = ESP_NETIF_DEFAULT_WIFI_STA();
    s_sta = esp_netif_new(&netif); if (!s_sta) return ESP_ERR_NO_MEM;
    err = esp_netif_attach_wifi_station(s_sta); if (err != ESP_OK) goto fail;
    err = esp_wifi_set_default_wifi_sta_handlers(); if (err != ESP_OK) goto fail;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    init.nvs_enable = false; init.static_rx_buf_num = 6; init.dynamic_rx_buf_num = 12;
    init.dynamic_tx_buf_num = 12;
    err = esp_wifi_init(&init); if (err != ESP_OK) goto fail; s_wifi_ready = true;
    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL, &s_wifi_handler);
    if (err != ESP_OK) goto fail;
    s_wifi_handler_ready = true;
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL, &s_ip_handler);
    if (err != ESP_OK) goto fail;
    s_ip_handler_ready = true;
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM); if (err != ESP_OK) goto fail;
    err = esp_wifi_set_mode(WIFI_MODE_STA); if (err != ESP_OK) goto fail;
    err = esp_wifi_start(); if (err != ESP_OK) goto fail; s_wifi_started = true;
    /* A fresh device must reach its setup AP without asking the driver to
     * validate an empty station SSID. The worker only connects after save. */
    if (*s_config.ssid) { err = apply_station(&s_config); if (err != ESP_OK) goto fail; }
    return ESP_OK;
fail: wifi_cleanup(); return err;
}

static void settings_worker(void *arg)
{
    (void)arg;
    esp_err_t err = wifi_init();
    if (err != ESP_OK) { report_error("Wi-Fi initialization failed"); vTaskSuspend(NULL); }
    if (!*s_config.ssid || !*s_config.api_key) {
        if (start_portal() != ESP_OK) report_error("Setup portal failed");
    }
    for (;;) {
        uint32_t bits = 0; xTaskNotifyWait(0, UINT32_MAX, &bits, pdMS_TO_TICKS(1000));
        if (bits & CLOSE_PORTAL) stop_portal();
        if (bits & OPEN_PORTAL) if (start_portal() != ESP_OK) report_error("Setup portal failed");
        if (bits & APPLY_CONFIG) {
            lock(); bool apply = s_pending_valid;
            if (apply) { s_apply = s_pending; wipe(&s_pending, sizeof(s_pending)); s_pending_valid = false; }
            unlock();
            if (apply) {
                err = store_config(&s_apply);
                lock();
                bool wifi_changed = strcmp(s_config.ssid, s_apply.ssid) || strcmp(s_config.password, s_apply.password);
                if (err == ESP_OK) {
                    s_config = s_apply; s_connection.configured = *s_config.ssid && *s_config.api_key;
                    clear_setting_error();
                    ++s_connection.config_generation;
                    s_site_count = 0; s_api_ok = s_api_refreshing = false;
                    s_connection_tested=s_connection_ok=false;s_connection_http=0;
                    s_connection_epoch=0;s_connection_error[0]=0;
                    s_query_stage[0]=0;s_query_count=s_query_duration_ms=0;s_query_http=0;
                    s_api_http = 0; s_api_error[0] = 0; s_api_epoch = 0;
                } else {
                    strlcpy(s_save_error, "NVS save failed; configuration unchanged", sizeof(s_save_error));
                    strlcpy(s_connection.error, "NVS save failed; configuration unchanged", sizeof(s_connection.error));
                }
                ++s_save_sequence; s_save_busy = false; unlock();
                if (err == ESP_OK && wifi_changed && apply_station(&s_apply) != ESP_OK) report_error("Wi-Fi configuration failed");
                wipe(&s_apply, sizeof(s_apply));
            }
        }
        if (bits & GOT_IP) {
            s_retry_sec = 5;
            if (!s_ntp_started) {
                esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
                esp_sntp_setservername(0, "pool.ntp.org"); esp_sntp_init(); s_ntp_started = true;
            }
        }
        if (bits & SCAN_DONE) {
            uint16_t count = SCAN_MAX;
            lock(); err = esp_wifi_scan_get_ap_records(&count, s_scan);
            for (unsigned i = 0; i < count; ++i) s_scan[i].ssid[32] = 0;
            s_scan_count = err == ESP_OK ? count : 0; s_scan_busy = false;
            if (err != ESP_OK) strlcpy(s_scan_error, "Scan failed", sizeof(s_scan_error));
            unlock();
        }
        if (bits & SCAN_REQUEST) {
            lock(); bool scanning = s_scan_busy; unlock();
            if (!scanning) {
                err = esp_wifi_scan_start(NULL, false);
                lock(); s_scan_busy = err == ESP_OK;
                strlcpy(s_scan_error, err == ESP_OK ? "" : "Wi-Fi busy; retry scan", sizeof(s_scan_error)); unlock();
            }
        }
        int64_t now = esp_timer_get_time();
        if (s_portal_deadline && now >= s_portal_deadline) stop_portal();
        lock(); bool connected = s_connection.wifi_connected, has_ssid = *s_config.ssid;
        s_connection.time_valid = time(NULL) >= 1704067200;
        bool scanning = s_scan_busy; unlock();
        if (!connected && has_ssid && !scanning && now >= s_retry_at) {
            esp_wifi_connect(); s_retry_at = now + (int64_t)s_retry_sec * 1000000;
            if (s_retry_sec < 60) s_retry_sec = s_retry_sec > 30 ? 60 : s_retry_sec * 2;
        }
        if (connected) {
            wifi_ap_record_t record;
            if (esp_wifi_sta_get_ap_info(&record) == ESP_OK) { lock(); s_connection.rssi = record.rssi; unlock(); }
        }
    }
}

esp_err_t unifi_settings_start(void)
{
    if (s_task) return ESP_OK;
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    memset(&s_config, 0, sizeof(s_config)); s_config.poll_seconds = 30; s_config.brightness = 70; s_config.screen_timeout_sec = UNIFI_SCREEN_TIMEOUT_DEFAULT;
#ifdef CONFIG_UNIFI_DEMO_MODE
    s_config.demo = CONFIG_UNIFI_DEMO_MODE;
#endif
    esp_err_t err = demo_radio_nvs_prepare(); s_nvs_ready = err == ESP_OK;
    if (s_nvs_ready) {
        nvs_handle_t nvs;
        err = nvs_open("unifi_cfg", NVS_READONLY, &nvs);
        if (err == ESP_OK) {
            /* Read exactly the legacy prefix. Missing/new display preference
             * must never invalidate saved Wi-Fi or API credentials. */
            s_pending = s_config;
            size_t size = UNIFI_CONFIG_V1_BYTES;
            err = nvs_get_blob(nvs, "config_v1", &s_pending, &size);
            uint32_t screen_timeout = UNIFI_SCREEN_TIMEOUT_DEFAULT;
            if (nvs_get_u32(nvs, "screen_idle_s", &screen_timeout) == ESP_OK && unifi_screen_timeout_valid(screen_timeout))
                s_pending.screen_timeout_sec = screen_timeout;
            nvs_close(nvs);
            if (err == ESP_OK && size == UNIFI_CONFIG_V1_BYTES && unifi_config_valid(&s_pending)) s_config = s_pending;
            else if (err != ESP_ERR_NVS_NOT_FOUND) report_error("Saved configuration invalid; use setup");
            wipe(&s_pending, sizeof(s_pending));
        }
    } else report_error("NVS unavailable; settings cannot be saved");
    s_connection.configured = *s_config.ssid && *s_config.api_key;
    s_connection.config_generation = 1;
    if (xTaskCreate(settings_worker, "unifi_settings", 6144, NULL, 4, &s_task) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}
