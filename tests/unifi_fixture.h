#pragma once
#include "unifi_demo.h"
#include <string.h>

static inline void unifi_fixture(unifi_snapshot_t *s, unifi_connection_t *c)
{
    unifi_demo_snapshot(s, UNIFI_DEMO_EPOCH, 0, false);
    memset(c, 0, sizeof(*c));
    c->configured = c->wifi_connected = c->time_valid = true;
    c->poll_seconds = 30;
    c->brightness = 70;
    c->screen_timeout_sec = UNIFI_SCREEN_TIMEOUT_DEFAULT;
    strcpy(c->ip, "192.0.2.100");
    strcpy(c->ap_ssid, "Passport-Setup-1234");
    strcpy(c->ap_password, "12345678");
}
