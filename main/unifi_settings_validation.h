#pragma once
/* Pure validation shared with host tests; no ESP-IDF or UI dependencies. */
#include "unifi_data.h"
#include <stdio.h>
#include <string.h>

static inline bool unifi_lan_url_valid(const char *url)
{
    if (!url || strncmp(url, "https://", 8) != 0) return false;
    unsigned a, b, c, d, port = 443;
    int used = 0;
    if (sscanf(url + 8, "%u.%u.%u.%u%n", &a, &b, &c, &d, &used) != 4 ||
        a > 255 || b > 255 || c > 255 || d > 255) return false;
    char canonical[16];
    int canonical_len = snprintf(canonical, sizeof(canonical), "%u.%u.%u.%u", a, b, c, d);
    if (canonical_len != used || strncmp(url + 8, canonical, (size_t)used)) return false;
    const char *end = url + 8 + used;
    if (*end == ':') {
        ++end; port = 0;
        if (*end < '0' || *end > '9') return false;
        while (*end >= '0' && *end <= '9') {
            port = port * 10 + (unsigned)(*end++ - '0');
            if (port > 65535) return false;
        }
        if (!port) return false;
    }
    if (*end == '/') ++end;
    if (*end) return false;
    for (const char *p = url + 8; *p; ++p)
        if ((*p < '0' || *p > '9') && *p != '.' && *p != ':' && *p != '/') return false;
    return a == 10 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168);
}

static inline bool unifi_identifier_valid(const char *id)
{
    for (const unsigned char *p = (const unsigned char *)id; *p; ++p)
        if (!((*p >= '0' && *p <= '9') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= 'a' && *p <= 'z') || *p == ':' || *p == '_' || *p == '-')) return false;
    return true;
}

/* Configuration is a flat object; reject nesting before cJSON can recurse on
 * a small HTTP task stack, and bound allocation count independently of bytes. */
static inline bool unifi_config_json_shape_valid(const char *body, size_t length)
{
    bool string = false, escape = false;
    unsigned depth = 0, fields = 0;
    for (size_t i = 0; i < length; ++i) {
        unsigned char ch = (unsigned char)body[i];
        if (!ch) return false;
        if (string) {
            if (escape) escape = false;
            else if (ch == '\\') escape = true;
            else if (ch == '"') string = false;
            continue;
        }
        if (ch == '"') string = true;
        else if (ch == '[' || ch == ']') return false;
        else if (ch == '{') { if (++depth > 1) return false; }
        else if (ch == '}') { if (depth != 1) return false; --depth; }
        else if (ch == ':' && ++fields > 24) return false;
    }
    return !string && !depth && !escape;
}

static inline bool unifi_config_valid(const unifi_config_t *c)
{
    if (!c || !unifi_screen_timeout_valid(c->screen_timeout_sec)) return false;
#define TERMINATED(field) (memchr(c->field, 0, sizeof(c->field)) != NULL)
    if (!TERMINATED(ssid) || !TERMINATED(password) || !TERMINATED(api_key) ||
        !TERMINATED(host_id) || !TERMINATED(site_id) || !TERMINATED(console_url) ||
        !TERMINATED(ca_pem)) return false;
#undef TERMINATED
    size_t password_len = strlen(c->password);
    if (password_len && password_len < 8) return false;
    if (password_len == 64) {
        for (size_t i = 0; i < password_len; ++i)
            if (!((c->password[i] >= '0' && c->password[i] <= '9') ||
                  (c->password[i] >= 'a' && c->password[i] <= 'f') ||
                  (c->password[i] >= 'A' && c->password[i] <= 'F'))) return false;
    }
    if (c->poll_seconds < 10 || c->poll_seconds > 3600 || c->brightness < 10 || c->brightness > 100) return false;
    if (c->local_api && !unifi_lan_url_valid(c->console_url)) return false;
    if (!unifi_identifier_valid(c->host_id) || !unifi_identifier_valid(c->site_id)) return false;
    for (const char *p = c->api_key; *p; ++p)
        if ((unsigned char)*p <= 32 || (unsigned char)*p >= 127) return false;
    if (*c->ca_pem && (!strstr(c->ca_pem, "-----BEGIN CERTIFICATE-----") ||
                      !strstr(c->ca_pem, "-----END CERTIFICATE-----"))) return false;
    return true;
}

/* Called after merging a settings patch. A new credential or endpoint must
 * rediscover its own sites, even if the request includes a stale browser ID.
 * Empty password/key inputs are already merged by the HTTP handler. */
static inline void unifi_config_reset_target_on_source_change(
    const unifi_config_t *previous, unifi_config_t *next)
{
    if (strcmp(previous->api_key, next->api_key) ||
        previous->local_api != next->local_api ||
        (next->local_api && strcmp(previous->console_url, next->console_url))) {
        memset(next->host_id, 0, sizeof(next->host_id));
        memset(next->site_id, 0, sizeof(next->site_id));
    }
}
