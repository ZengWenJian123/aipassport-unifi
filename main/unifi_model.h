#pragma once
#include "unifi_data.h"

typedef enum { U_OVERVIEW, U_INTERNET, U_CLIENTS, U_VPN, U_SDWAN,
               U_DEVICES, U_ALERTS, U_SITES, U_SETTINGS, U_PAGE_COUNT } unifi_page_t;
typedef enum { U_HOME, U_MENU, U_DETAIL, U_PORTAL, U_INFO } unifi_mode_t;
typedef enum { U_UP, U_DOWN, U_OK, U_BACK } unifi_key_t;
typedef enum { U_NONE, U_REFRESH, U_OPEN_PORTAL, U_CLOSE_PORTAL, U_TOGGLE_DEMO,
               U_SELECT_SITE } unifi_action_t;
typedef struct {
    unifi_page_t page;
    unifi_mode_t mode;
    int menu, selection;
    bool rates, wan_details;
    unsigned alert_count;
} unifi_nav_t;
typedef struct { char text[80]; int64_t epoch; } unifi_alert_t;
typedef struct {
    unifi_alert_t items[8];
    unsigned count;
    bool baseline, wifi, api_ok;
    unsigned offline;
    int64_t latest_vpn;
} unifi_alerts_t;

const char *unifi_page_title(unifi_page_t page);
int unifi_menu_count(unifi_page_t page);
const char *unifi_menu_title(unifi_page_t page, int item);
/* Serial-number order; adjacent configuration saves may wrap UINT32_MAX. */
bool unifi_generation_current_or_newer(uint32_t incoming, uint32_t current);
unifi_action_t unifi_navigate(unifi_nav_t *n, unifi_key_t key,
                              const unifi_snapshot_t *s);
void unifi_observe(unifi_alerts_t *a, const unifi_snapshot_t *s,
                   const unifi_connection_t *c, int64_t now);
void unifi_duration(char *out, size_t size, int64_t seconds);
void unifi_rate(char *out, size_t size, double value, bool byte_rate);

/* UI-owned monotonic idle state: API updates never count as user activity. */
typedef enum { U_PRESS, U_CLICK, U_LONG, U_RELEASE, U_DOUBLE } unifi_input_event_t;
typedef struct {
    int64_t last_activity_ms;
    unsigned pressed_mask, wake_mask;
    bool asleep;
} unifi_screen_idle_t;
/* true only for a navigation event that should reach the page state machine. */
bool unifi_screen_input(unifi_screen_idle_t *state, unsigned button, unifi_input_event_t event, int64_t now_ms);
void unifi_screen_tick(unifi_screen_idle_t *state, uint32_t timeout_sec, int64_t now_ms);
void unifi_clock_text(char *out, size_t size, int64_t epoch, bool time_valid);

unsigned unifi_wan_count(const unifi_snapshot_t *s);
int unifi_wan_index(const unifi_snapshot_t *s, unsigned selection);
