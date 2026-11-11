#include "unifi_dashboard.h"
#include "unifi_api.h"
#include "unifi_demo.h"
#include "unifi_settings.h"
#include "unifi_view.h"
#include "unifi_lvgl.h"
#include "bsp_display.h"
#include "bsp_battery.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#define STOP_REQUEST BIT0
#define STOPPED BIT1
static const char *TAG="unifi_app";
static lv_obj_t *s_screen;
static lv_timer_t *s_timer;
static QueueHandle_t s_keys, s_actions;
static SemaphoreHandle_t s_lock;
static EventGroupHandle_t s_lifecycle;
static TaskHandle_t s_worker;
/* Two bounded snapshots; JSON parsing and HTTP never run on the LVGL stack. */
static unifi_snapshot_t s_visible, s_work;
static unifi_config_t s_config;
static unifi_connection_t s_connection;
static unifi_nav_t s_nav;
static unifi_alerts_t s_alerts;
static int s_battery=-1;
static uint32_t s_revision, s_seen_revision, s_visible_generation;
static bool s_clear_alerts;
static unifi_screen_idle_t s_idle;
static int s_applied_backlight=-1;
typedef struct { unsigned button; unifi_input_event_t event; } input_t;
typedef struct { uint32_t offset, version; bool vpn; int device; } request_t;
typedef struct {
    unifi_action_t action;
    char host_id[96], site_id[64];
    unsigned site_index;
    bool demo;
} action_t;
static request_t s_request={.device=-1};

static void on_draw(lv_event_t *e)
{
    xSemaphoreTake(s_lock,portMAX_DELAY);
    int64_t now=time(NULL);
    if(s_visible.demo && now<1704067200)now=s_visible.fetched_epoch;
    unifi_lvgl_render(lv_event_get_layer(e),&s_nav,&s_visible,&s_connection,&s_alerts,s_battery,now);
    xSemaphoreGive(s_lock);
}
static void update_request(void)
{
    request_t req={.vpn=s_nav.page==U_VPN,.device=-1};
    if(s_nav.mode==U_DETAIL&&(s_nav.page==U_CLIENTS||s_nav.page==U_VPN))
        req.offset=(unsigned)s_nav.selection/UNIFI_CLIENT_PAGE_SIZE*UNIFI_CLIENT_PAGE_SIZE;
    if(s_nav.mode==U_DETAIL&&s_nav.page==U_DEVICES)req.device=s_nav.selection;
    xSemaphoreTake(s_lock,portMAX_DELAY);
    if(req.vpn!=s_request.vpn||req.offset!=s_request.offset||req.device!=s_request.device) {
        req.version=s_request.version+1;s_request=req;
    }
    xSemaphoreGive(s_lock);
}
static void tick(lv_timer_t *t)
{
    (void)t; input_t input; bool changed=false;
    int64_t now_ms=esp_timer_get_time()/1000;
    while(xQueueReceive(s_keys,&input,0)==pdTRUE) {
        changed=true;
        if(!unifi_screen_input(&s_idle,input.button,input.event,now_ms))continue;
        unifi_key_t key;
        if(input.event==U_LONG && input.button==BSP_BTN_OK)key=U_BACK;
        else if(input.event==U_CLICK)key=input.button==BSP_BTN_UP?U_UP:input.button==BSP_BTN_DOWN?U_DOWN:U_OK;
        else continue;
        xSemaphoreTake(s_lock,portMAX_DELAY);
        s_nav.alert_count=s_alerts.count;
        unifi_action_t act=unifi_navigate(&s_nav,key,&s_visible);
        action_t action={.action=act};
        if(act==U_SELECT_SITE && s_nav.selection<s_visible.site_count) {
            const unifi_site_t *st=&s_visible.sites[s_nav.selection];
            action.site_index=s_nav.selection;
            action.demo=s_visible.demo;
            snprintf(action.host_id,sizeof action.host_id,"%s",st->host_id);
            snprintf(action.site_id,sizeof action.site_id,"%s",st->site_id[0]?st->site_id:st->network_site_id);
            s_nav.mode=U_HOME;
        }
        xSemaphoreGive(s_lock);
        if(act!=U_NONE) (void)xQueueSend(s_actions,&action,0);
        update_request();changed=true;
    }
    static int64_t last_second;
    int64_t now=esp_timer_get_time()/1000000;
    if(now!=last_second) {
        unifi_connection_t current;unifi_settings_connection(&current);
        static bool initial_setup_shown, portal_was_active;
        xSemaphoreTake(s_lock,portMAX_DELAY);
        if(unifi_generation_current_or_newer(current.config_generation,s_connection.config_generation))
            s_connection=current;
        /* A save can happen while an older HTTP round is still in flight. */
        if(s_visible_generation!=s_connection.config_generation) {
            memset(&s_visible,0,sizeof s_visible);memset(&s_alerts,0,sizeof s_alerts);
            s_visible_generation=s_connection.config_generation;s_seen_revision=s_revision;
        }
        if(!initial_setup_shown && s_connection.portal_active && !s_connection.configured && !s_visible.demo) {
            s_nav.page=U_SETTINGS;s_nav.mode=U_PORTAL;initial_setup_shown=true;
        }
        if(portal_was_active && !s_connection.portal_active && s_nav.mode==U_PORTAL) s_nav.mode=U_HOME;
        portal_was_active=s_connection.portal_active;
        if(s_clear_alerts) { memset(&s_alerts,0,sizeof s_alerts);s_clear_alerts=false; }
        if(s_revision!=s_seen_revision) {
            if(s_visible.demo) unifi_demo_alerts(&s_alerts,s_visible.fetched_epoch);
            else if(!s_visible.refreshing) unifi_observe(&s_alerts,&s_visible,&s_connection,time(NULL));
            s_seen_revision=s_revision;
        }
        xSemaphoreGive(s_lock);
        last_second=now;changed=true;
    }
    xSemaphoreTake(s_lock,portMAX_DELAY);
    uint32_t timeout=s_connection.screen_timeout_sec;
    uint8_t brightness=s_connection.brightness;
    xSemaphoreGive(s_lock);
    bool was_asleep=s_idle.asleep;
    unifi_screen_tick(&s_idle,timeout,now_ms);
    int backlight=s_idle.asleep?0:brightness;
    /* Backlight has one owner (this UI timer), so an API save cannot relight
     * an idle display. LEDC update is bounded; no display sleep/reinit needed. */
    if(backlight!=s_applied_backlight){bsp_display_backlight((uint8_t)backlight);s_applied_backlight=backlight;changed=true;}
    if(was_asleep!=s_idle.asleep)changed=true;
    if(changed&&!s_idle.asleep&&s_screen)lv_obj_invalidate(s_screen);
}
static void publish(uint32_t generation)
{
    unifi_connection_t current;unifi_settings_connection(&current);
    xSemaphoreTake(s_lock,portMAX_DELAY);
    if(current.config_generation==generation &&
       unifi_generation_current_or_newer(generation,s_connection.config_generation)) {
        s_connection=current;
        s_visible=s_work;s_visible_generation=generation;s_revision++;
    }
    xSemaphoreGive(s_lock);
    unifi_settings_publish_sites(&s_work,generation);
}
typedef struct { uint32_t generation, request_version; } progress_context_t;
static void publish_progress(const unifi_snapshot_t *snapshot,void *context)
{
    const progress_context_t *expected=context;
    unifi_connection_t current;unifi_settings_connection(&current);
    if(current.config_generation!=expected->generation)return;
    xSemaphoreTake(s_lock,portMAX_DELAY);
    bool still_selected=s_request.version==expected->request_version &&
        unifi_generation_current_or_newer(expected->generation,s_connection.config_generation);
    if(still_selected){s_connection=current;s_visible=*snapshot;s_visible_generation=expected->generation;s_revision++;}
    xSemaphoreGive(s_lock);
    if(still_selected)unifi_settings_publish_sites(snapshot,expected->generation);
}
static void worker(void *arg)
{
    (void)arg; uint32_t generation=UINT32_MAX, version=UINT32_MAX; int64_t next_poll=0,last_battery=-10;
    unsigned demo_site=0;
    bool was_ready=false;
    while(!(xEventGroupGetBits(s_lifecycle)&STOP_REQUEST)) {
        unifi_connection_t conn;unifi_settings_connection(&conn);
        unifi_settings_get(&s_config);
        action_t a;
        while(xQueueReceive(s_actions,&a,0)==pdTRUE) {
            if(a.action==U_OPEN_PORTAL)unifi_settings_open_portal();
            else if(a.action==U_CLOSE_PORTAL)unifi_settings_close_portal();
            else if(a.action==U_TOGGLE_DEMO)unifi_settings_set_demo(!s_config.demo);
            else if(a.action==U_SELECT_SITE) {
                /* Demo navigation must never replace a real saved site ID. */
                if(s_config.demo && a.demo) { demo_site=a.site_index;next_poll=0; }
                else if(!s_config.demo && !a.demo && a.site_id[0])
                    unifi_settings_select_site(a.host_id,a.site_id);
            }
            else if(a.action==U_REFRESH)next_poll=0;
        }
        request_t req;
        xSemaphoreTake(s_lock,portMAX_DELAY);req=s_request;xSemaphoreGive(s_lock);
        int64_t now=esp_timer_get_time()/1000000;
        bool ready=s_config.demo||(conn.wifi_connected&&conn.time_valid&&conn.configured);
        if(ready&&!was_ready)next_poll=0;
        was_ready=ready;
        if(conn.config_generation!=generation) {
            unifi_api_reset_cache();
            memset(&s_work,0,sizeof s_work);s_work.demo=s_config.demo;
            generation=conn.config_generation;publish(generation);next_poll=0;
            xSemaphoreTake(s_lock,portMAX_DELAY);s_clear_alerts=true;xSemaphoreGive(s_lock);
        }
        if(req.version!=version){version=req.version;next_poll=0;}
        if(now-last_battery>=10) {
            int battery=bsp_battery_soc();
            xSemaphoreTake(s_lock,portMAX_DELAY);s_battery=battery;xSemaphoreGive(s_lock);last_battery=now;
        }
        if(now>=next_poll) {
            if(ready) {
                progress_context_t context={generation,req.version};
                s_work.refreshing=true;publish_progress(&s_work,&context);
                (void)unifi_api_poll_with_progress(&s_config,&s_work,req.offset,req.vpn,req.device,
                                                  publish_progress,&context);
                if(s_work.demo && demo_site<s_work.site_count) s_work.selected_site=demo_site;
                unifi_connection_t current;unifi_settings_connection(&current);
                xSemaphoreTake(s_lock,portMAX_DELAY);bool still_selected=s_request.version==req.version;xSemaphoreGive(s_lock);
                if(current.config_generation==generation&&still_selected)publish(generation);
            } else {
                s_work.api_ok=false;s_work.refreshing=false;
                snprintf(s_work.error,sizeof s_work.error,"%s",!conn.wifi_connected?"Wi-Fi disconnected":!conn.time_valid?"Waiting for time synchronization":"API not configured");
                publish(generation);
            }
            uint32_t interval=s_config.poll_seconds<10?10:s_config.poll_seconds;
            /* Cadence is measured from poll start, not duration + interval. */
            int64_t finished=esp_timer_get_time()/1000000;
            next_poll=now+(s_config.demo?5:interval);
            if(next_poll<finished+2)next_poll=finished+2;
        }
        ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(500));
    }
    s_worker=NULL;xEventGroupSetBits(s_lifecycle,STOPPED);vTaskDelete(NULL);
}
void unifi_dashboard_enter(void)
{
    /* Caller holds BSP LVGL lock. */
    if(s_screen||!s_lock||!s_keys)return;
    memset(&s_idle,0,sizeof(s_idle));s_idle.last_activity_ms=esp_timer_get_time()/1000;s_applied_backlight=-1;
    unifi_connection_t current;unifi_settings_connection(&current);
    xSemaphoreTake(s_lock,portMAX_DELAY);s_connection=current;xSemaphoreGive(s_lock);
    s_screen=lv_obj_create(NULL);lv_obj_remove_style_all(s_screen);
    lv_obj_set_size(s_screen,240,320);lv_obj_set_style_bg_opa(s_screen,LV_OPA_COVER,0);
    lv_obj_set_style_bg_color(s_screen,lv_color_hex(0x101317),0);
    lv_obj_remove_flag(s_screen,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_screen,on_draw,LV_EVENT_DRAW_MAIN,NULL);
    lv_screen_load(s_screen);s_timer=lv_timer_create(tick,100,NULL);
}
void unifi_dashboard_exit(void)
{
    /* Call stop first outside LVGL lock. A timeout retains the UI safely. */
    if(!s_lifecycle || !(xEventGroupGetBits(s_lifecycle)&STOPPED))return;
    if(s_timer){lv_timer_delete(s_timer);s_timer=NULL;}
    if(s_screen){lv_obj_t *blank=lv_obj_create(NULL);lv_screen_load(blank);lv_obj_delete(s_screen);s_screen=NULL;}
}
void unifi_dashboard_key(bsp_btn_t b,bsp_btn_ev_t e)
{
    if(!s_keys)return;
    input_t input={.button=(unsigned)b};
    switch(e){
    case BSP_BTN_PRESS:input.event=U_PRESS;break;
    case BSP_BTN_CLICK:input.event=U_CLICK;break;
    case BSP_BTN_LONG:input.event=U_LONG;break;
    case BSP_BTN_RELEASE:input.event=U_RELEASE;break;
    case BSP_BTN_DOUBLE:input.event=U_DOUBLE;break;
    default:return;
    }
    (void)xQueueSend(s_keys,&input,0);
}
esp_err_t unifi_dashboard_start(void)
{
    if(s_worker)return ESP_OK;
    if(!s_lock)s_lock=xSemaphoreCreateMutex();
    if(!s_keys)s_keys=xQueueCreate(16,sizeof(input_t));
    if(!s_actions)s_actions=xQueueCreate(6,sizeof(action_t));
    if(!s_lifecycle)s_lifecycle=xEventGroupCreate();
    if(!s_lock||!s_keys||!s_actions||!s_lifecycle)return ESP_ERR_NO_MEM;
    esp_err_t err=unifi_settings_start();
    if(err!=ESP_OK)ESP_LOGE(TAG,"Settings startup failed: %s",esp_err_to_name(err));
    xEventGroupClearBits(s_lifecycle,STOP_REQUEST|STOPPED);
    if(xTaskCreate(worker,"unifi_fetch",12288,NULL,3,&s_worker)!=pdPASS)return ESP_ERR_NO_MEM;
    return err;
}
esp_err_t unifi_dashboard_stop(void)
{
    if(!s_lifecycle)return ESP_OK;
    xEventGroupSetBits(s_lifecycle,STOP_REQUEST);
    return xEventGroupWaitBits(s_lifecycle,STOPPED,pdFALSE,pdTRUE,pdMS_TO_TICKS(60000))&STOPPED?ESP_OK:ESP_ERR_TIMEOUT;
}
