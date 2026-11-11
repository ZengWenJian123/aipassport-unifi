#pragma once
#include "esp_err.h"
#include "bsp_button.h"
void unifi_dashboard_enter(void);
void unifi_dashboard_exit(void);
void unifi_dashboard_key(bsp_btn_t btn, bsp_btn_ev_t ev);
esp_err_t unifi_dashboard_start(void);
esp_err_t unifi_dashboard_stop(void);
