// Application lifecycle; reusable hardware remains in BSP.
#include "bsp_i2c.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_battery.h"
#include "unifi_dashboard.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "key_guard.h"
static key_guard_t s_key_guard;
static bool s_input_ready;

static void on_key(bsp_btn_t b,bsp_btn_ev_t e,void *user)
{
    (void)user;
    if (key_guard_consume(&s_key_guard, e == BSP_BTN_RELEASE, esp_timer_get_time())) return;
    if (!s_input_ready) return;
    unifi_dashboard_key(b,e);
}
void app_main(void)
{
    (void)bsp_i2c_init();
    if(bsp_display_init()!=ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE("unifi_app","Display initialization failed");return;
    }
    bsp_display_backlight(70);
    (void)bsp_battery_init();
    esp_err_t err=unifi_dashboard_start();
    if(err!=ESP_OK)ESP_LOGE("unifi_app","Application initialization: %s",esp_err_to_name(err));
    if(bsp_lvgl_lock(1000)){unifi_dashboard_enter();bsp_lvgl_unlock();}
    s_input_ready=true;
    if(bsp_button_init(on_key,NULL)!=ESP_OK)ESP_LOGE("unifi_app","Button initialization failed");
}
