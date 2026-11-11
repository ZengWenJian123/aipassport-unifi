idf_component_register(
    SRCS "main.c" "unifi_dashboard.c" "unifi_model.c" "unifi_view.c" "unifi_lvgl.c"
         "unifi_api.c" "unifi_api_parse.c" "unifi_json_stream.c" "unifi_demo.c"
         "unifi_settings.c" "demo_radio.c"
         "../assets/fonts/unifi_font_12.c" "../assets/fonts/unifi_font_14.c"
         "../assets/fonts/unifi_font_18.c" "../assets/fonts/unifi_font_24.c"
    INCLUDE_DIRS "." "../assets/fonts"
    REQUIRES bsp esp_event esp_netif esp_timer esp_wifi nvs_flash
             esp_http_client esp_http_server json lwip mbedtls esp-tls
    EMBED_FILES "unifi_portal.html"
)
