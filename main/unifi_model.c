#include "unifi_model.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *const titles[] = {"网络总览", "互联网", "客户端", "VPN 连接",
    "SD-WAN", "设备运行", "告警事件", "站点切换", "设置与配网"};
static const char *const menus[U_PAGE_COUNT][5] = {
    {"WAN 线路详情", "最近七天流量", "立即刷新"}, {"延迟与丢包", "立即刷新"},
    {"全部在线客户端", "立即刷新"}, {"全部在线 VPN", "立即刷新"},
    {"链路详情", "立即刷新", "关于重新连接"},
    {"设备运行详情", "设备流量", "立即刷新"},
    {"查看本机事件", "立即刷新"}, {"选择站点", "立即刷新"},
    {"开启配置热点", "切换演示模式", "连接诊断", "立即刷新", "关闭配置热点"}
};
const char *unifi_page_title(unifi_page_t p) { return titles[p < U_PAGE_COUNT ? p : 0]; }
int unifi_menu_count(unifi_page_t p) { return p == U_SETTINGS ? 5 : (p == U_SDWAN || p == U_DEVICES || p == U_OVERVIEW ? 3 : 2); }
const char *unifi_menu_title(unifi_page_t p, int i) { return menus[p][i]; }
bool unifi_generation_current_or_newer(uint32_t incoming, uint32_t current)
{ return (uint32_t)(incoming-current) <= UINT32_MAX/2; }
static int wrap(int n, int count) { return count > 0 ? (n + count) % count : 0; }
unifi_action_t unifi_navigate(unifi_nav_t *n, unifi_key_t k, const unifi_snapshot_t *s)
{
    if (k == U_BACK) {
        if (n->mode == U_HOME) n->page = U_OVERVIEW;
        n->mode = U_HOME; n->menu = n->selection = 0; n->rates = n->wan_details = false;
        return U_NONE;
    }
    if (n->mode == U_PORTAL || n->mode == U_INFO) {
        if (k == U_OK) n->mode = U_HOME;
        return U_NONE;
    }
    int step = k == U_UP ? -1 : 1;
    if (n->mode == U_HOME) {
        if (k == U_OK) { n->mode = U_MENU; n->menu = 0; }
        else { n->page = wrap(n->page + step, U_PAGE_COUNT); n->selection = 0; n->rates = n->wan_details = false; }
        return U_NONE;
    }
    if (n->mode == U_MENU) {
        if (k != U_OK) { n->menu = wrap(n->menu + step, unifi_menu_count(n->page)); return U_NONE; }
        int m = n->menu;
        n->selection = 0; n->rates = n->wan_details = false; n->mode = U_HOME;
        if (n->page == U_SETTINGS) {
            if (m == 0) { n->mode = U_PORTAL; return U_OPEN_PORTAL; }
            if (m == 1) return U_TOGGLE_DEMO;
            if (m == 2) { n->mode = U_INFO; return U_NONE; }
            return m == 3 ? U_REFRESH : U_CLOSE_PORTAL;
        }
        if (n->page == U_SDWAN && m == 2) { n->mode = U_INFO; return U_NONE; }
        if (m == unifi_menu_count(n->page) - 1 && n->page != U_SDWAN) return U_REFRESH;
        if (n->page == U_SDWAN && m == 1) return U_REFRESH;
        n->mode = U_DETAIL; n->wan_details=n->page==U_OVERVIEW&&m==0; n->rates = n->page == U_DEVICES && m == 1;
        return U_NONE;
    }
    if (k == U_OK) {
        if (n->page == U_SITES && s->site_count) return U_SELECT_SITE;
        n->mode = U_HOME; n->selection = 0; n->rates = n->wan_details = false;
    } else {
        int count = 1;
        if (n->page == U_OVERVIEW) count = n->wan_details?(int)unifi_wan_count(s):7;
        else if (n->page == U_DEVICES) count = s->device_count;
        else if (n->page == U_SITES) count = s->site_count;
        else if (n->page == U_ALERTS) count = (int)n->alert_count;
        else if (n->page == U_CLIENTS) count = s->clients.total;
        else if (n->page == U_VPN) count = s->clients.vpn + s->clients.teleport;
        /* The API traversal cap is explicit; never offer unreachable memory. */
        if (count > 300) count = 300;
        n->selection = wrap(n->selection + step, count);
    }
    return U_NONE;
}
static void alert(unifi_alerts_t *a, const char *text, int64_t now)
{
    memmove(&a->items[1], &a->items[0], sizeof(a->items[0]) * 7);
    snprintf(a->items[0].text, sizeof(a->items[0].text), "%s", text);
    a->items[0].epoch = now;
    if (a->count < 8) a->count++;
}
void unifi_observe(unifi_alerts_t *a, const unifi_snapshot_t *s, const unifi_connection_t *c, int64_t now)
{
    if (a->baseline) {
        if (c->wifi_connected != a->wifi) alert(a, c->wifi_connected ? "Wi-Fi 已重新连接" : "Wi-Fi 连接中断", now);
        if (s->api_ok != a->api_ok) alert(a, s->api_ok ? "API 已恢复" : "API 暂不可用", now);
        if (s->devices_complete && s->device_offline != a->offline)
            alert(a, "设备在线状态发生变化", now);
        if (s->clients.latest_vpn_valid && s->clients.latest_vpn.connected_epoch > a->latest_vpn)
            alert(a, "检测到新上线的 VPN 客户端", now);
    }
    a->baseline = s->fetched_epoch > 0;
    a->wifi = c->wifi_connected; a->api_ok = s->api_ok;
    if (s->devices_complete) a->offline = s->device_offline;
    if (s->clients.latest_vpn_valid) a->latest_vpn = s->clients.latest_vpn.connected_epoch;
}
void unifi_duration(char *o, size_t z, int64_t s)
{
    if (s < 0) snprintf(o, z, "未知");
    else if (s >= 86400) snprintf(o, z, "%lld天 %lld时", (long long)(s / 86400), (long long)(s / 3600 % 24));
    else if (s >= 3600) snprintf(o, z, "%lld时 %lld分", (long long)(s / 3600), (long long)(s / 60 % 60));
    else snprintf(o, z, "%lld分 %lld秒", (long long)(s / 60), (long long)(s % 60));
}
void unifi_rate(char *o, size_t z, double v, bool bytes)
{
    if (!isfinite(v) || v < 0) { snprintf(o, z, "--"); return; }
    const char *unit = bytes ? "Bps" : "bps";
    const char *prefix = "";
    if (v >= 1e9) { v /= 1e9; prefix = "G"; }
    else if (v >= 1e6) { v /= 1e6; prefix = "M"; }
    else if (v >= 1e3) { v /= 1e3; prefix = "K"; }
    snprintf(o, z, "%.1f %s%s", v, prefix, unit);
}

bool unifi_screen_input(unifi_screen_idle_t *state, unsigned button, unifi_input_event_t event, int64_t now_ms)
{
    if(button>=3)return false;
    unsigned bit=1u<<button;
    state->last_activity_ms=now_ms;
    if(event==U_PRESS){
        state->pressed_mask|=bit;
        if(state->asleep){state->asleep=false;state->wake_mask|=bit;}
        else state->wake_mask&=~bit;
        return false;
    }
    if(event==U_RELEASE){state->pressed_mask&=~bit;return false;}
    /* Also recover safely if a queue overflow discarded the PRESS event. */
    if(state->asleep){state->asleep=false;state->wake_mask|=bit;return false;}
    /* Keep the mask through RELEASE: delayed SINGLE_CLICK follows release. */
    return !(state->wake_mask&bit) && (event==U_CLICK||event==U_LONG);
}
void unifi_screen_tick(unifi_screen_idle_t *state, uint32_t timeout_sec, int64_t now_ms)
{
    if(!timeout_sec){state->asleep=false;return;}
    if(now_ms<state->last_activity_ms){state->last_activity_ms=now_ms;return;}
    if(!state->pressed_mask && now_ms-state->last_activity_ms>=(int64_t)timeout_sec*1000)state->asleep=true;
}
void unifi_clock_text(char *out, size_t size, int64_t epoch, bool time_valid)
{
    if(!time_valid || epoch<1700000000){snprintf(out,size,"--:--");return;}
    /* Asia/Shanghai (UTC+8); independent of host TZ and historical data time. */
    int64_t seconds=(epoch%86400+28800)%86400;
    snprintf(out,size,"%02d:%02d",(int)(seconds/3600),(int)(seconds/60%60));
}

unsigned unifi_wan_count(const unifi_snapshot_t *s)
{ unsigned n=0;for(unsigned i=0;i<UNIFI_WAN_MAX;i++)if(s->wans[i].present)n++;return n; }
int unifi_wan_index(const unifi_snapshot_t *s,unsigned selection)
{ for(unsigned i=0;i<UNIFI_WAN_MAX;i++)if(s->wans[i].present){if(!selection)return (int)i;selection--;}return -1; }
