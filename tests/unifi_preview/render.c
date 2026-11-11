#include "unifi_lvgl.h"
#include "unifi_fonts.h"
#include "unifi_fixture.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { WIDTH = 240, HEIGHT = 320 };
static uint16_t pixels[WIDTH * HEIGHT];
static uint8_t buffer[WIDTH * 20 * 2];
static lv_obj_t *screen;
static lv_display_t *display;
static unifi_nav_t nav;
static unifi_snapshot_t snapshot;
static unifi_connection_t connection;
static unifi_alerts_t alerts;
static int64_t now = 1794067260;
static bool glyph_mode;
static const lv_font_t *test_font;
static const char *test_glyph;
static unsigned frames, flushes, pixel_checks;

static void flush(lv_display_t *d, const lv_area_t *area, uint8_t *data)
{
    assert(area->x1 >= 0 && area->x2 < WIDTH && area->y1 >= 0 && area->y2 < HEIGHT);
    unsigned width = area->x2 - area->x1 + 1;
    unsigned stride = lv_draw_buf_width_to_stride(width, LV_COLOR_FORMAT_RGB565);
    for(int y = area->y1; y <= area->y2; y++) {
        memcpy(pixels + y * WIDTH + area->x1, data, width * 2);
        data += stride;
    }
    flushes++;
    lv_display_flush_ready(d);
}

static void draw(lv_event_t *event)
{
    lv_layer_t *layer = lv_event_get_layer(event);
    if(!glyph_mode) {
        unifi_lvgl_render(layer, &nav, &snapshot, &connection, &alerts, 94, now);
        return;
    }
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.font = test_font;
    d.text = test_glyph;
    d.color = lv_color_hex(0xFFFFFF);
    lv_area_t area = {20, 20, 119, 20 + test_font->line_height - 1};
    lv_draw_label(layer, &d, &area);
}

static void refresh(void)
{
    memset(pixels, 0xDA, sizeof pixels);
    unsigned previous_flushes = flushes;
    lv_obj_invalidate(screen);
    lv_refr_now(display);
    assert(flushes > previous_flushes);
}

static unsigned gray(uint16_t color)
{
    unsigned r = ((color >> 11) & 31) * 255 / 31;
    unsigned g = ((color >> 5) & 63) * 255 / 63;
    unsigned b = (color & 31) * 255 / 31;
    return (r + g + b) / 3;
}

/* Compare actual LVGL RGB565 pixels against each font's raw four-bit bitmap.
 * This catches wrong cmaps, bit order, odd-width row padding, offsets, baseline,
 * A4->A8 expansion, and >1 MiB bitmap-index truncation in the real renderer. */
static void check_glyph(const lv_font_t *font, const char *text, uint32_t cp)
{
    glyph_mode = true; test_font = font; test_glyph = text;
    refresh();
    lv_font_glyph_dsc_t d;
    assert(lv_font_get_glyph_dsc(font, &d, cp, 0));
    assert(!d.is_placeholder && d.box_w > 0 && d.box_h > 0);
    d.req_raw_bitmap = 1;
    const uint8_t *raw = font->get_glyph_bitmap(&d, NULL);
    assert(raw != NULL);
    int left = 20 + d.ofs_x;
    int top = 20 + font->line_height - font->base_line - d.box_h - d.ofs_y;
    unsigned visible = 0;
    for(int y = 0; y < HEIGHT; y++) for(int x = 0; x < WIDTH; x++) {
        int expected = 0;
        if(x >= left && x < left + d.box_w && y >= top && y < top + d.box_h) {
            unsigned index = (y - top) * d.box_w + x - left;
            expected = ((raw[index / 2] >> (index % 2 ? 0 : 4)) & 15) * 17;
        }
        int actual = (int)gray(pixels[y * WIDTH + x]);
        if(abs(expected - actual) > 10) {
            fprintf(stderr, "glyph U+%04X font height %ld pixel(%d,%d): expected %d got %d\n",
                    cp, (long)font->line_height, x, y, expected, actual);
            assert(0);
        }
        visible += actual > 0;
        pixel_checks++;
    }
    assert(visible > 4);
}

static void check_text(void)
{
    char text[64];
    const lv_font_t *f = &unifi_font_14;
    int full = lv_font_get_glyph_width(f, 0x7F51, 0) + lv_font_get_glyph_width(f, 0x7EDC, 0);
    unifi_lvgl_fit_text(text, sizeof text, "网络", f, full);
    assert(strcmp(text, "网络") == 0); /* Do not reserve an unnecessary suffix. */
    unifi_lvgl_fit_text(text, sizeof text, "网络网络网络", f, full);
    assert(strstr(text, "...") != NULL);
    lv_point_t size;
    lv_text_get_size(&size, text, f, 0, 0, INT32_MAX, LV_TEXT_FLAG_NONE);
    assert(size.x <= full);
    unifi_lvgl_fit_text(text, sizeof text, "网络🙂", f, 100);
    assert(strcmp(text, "网络?") == 0);
    unifi_lvgl_fit_text(text, sizeof text, "\xE0\x80\xAF", f, 100);
    assert(strcmp(text, "???") == 0); /* Overlong encoding. */
    unifi_lvgl_fit_text(text, sizeof text, "\xED\xA0\x80", f, 100);
    assert(strcmp(text, "???") == 0); /* UTF-16 surrogate. */
    unifi_lvgl_fit_text(text, sizeof text, "\xF4\x90\x80\x80", f, 100);
    assert(strcmp(text, "????") == 0); /* Beyond Unicode range. */
    unifi_lvgl_fit_text(text, sizeof text, "\xE7\xBD", f, 100);
    assert(strcmp(text, "??") == 0); /* Truncated sequence. */
    unifi_lvgl_fit_text(text, 4, "网络", f, 100);
    assert(strcmp(text, "...") == 0);
    unifi_lvgl_fit_text(text, 1, "网络", f, 100);
    assert(text[0] == 0);
    unifi_lvgl_fit_text(text, sizeof text, "网络", f, 0);
    assert(text[0] == 0);
}

static void save(const char *directory, const char *name)
{
    glyph_mode = false;
    refresh();
    char path[1024];
    assert(snprintf(path, sizeof path, "%s/%s.ppm", directory, name) < (int)sizeof path);
    FILE *out = fopen(path, "wb");
    assert(out != NULL);
    fprintf(out, "P6\n%d %d\n255\n", WIDTH, HEIGHT);
    unsigned foreground = 0;
    for(unsigned i = 0; i < WIDTH * HEIGHT; i++) {
        uint16_t value = pixels[i];
        uint8_t rgb[3] = {((value >> 11) & 31) * 255 / 31,
                          ((value >> 5) & 63) * 255 / 63, (value & 31) * 255 / 31};
        assert(fwrite(rgb, 1, 3, out) == 3);
        foreground += value != pixels[0];
    }
    assert(fclose(out) == 0);
    assert(foreground > 600 && foreground < WIDTH * HEIGHT * 8 / 10);
    frames++;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    lv_init();
    display = lv_display_create(WIDTH, HEIGHT);
    assert(display);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, buffer, NULL, sizeof buffer, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen, WIDTH, HEIGHT);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(screen, draw, LV_EVENT_DRAW_MAIN, NULL);
    lv_screen_load(screen);

    check_text();
    const lv_font_t *fonts[] = {&unifi_font_12, &unifi_font_14, &unifi_font_18, &unifi_font_24};
    for(unsigned i = 0; i < 4; i++) {
        check_glyph(fonts[i], "g", 'g');
        check_glyph(fonts[i], "1", '1');
        check_glyph(fonts[i], "%", '%');
        if(i < 3) {
            check_glyph(fonts[i], "网", 0x7F51);
            check_glyph(fonts[i], "龟", 0x9F9F);
            check_glyph(fonts[i], "。", 0x3002);
        }
    }
    unifi_fixture(&snapshot, &connection);
    unifi_demo_alerts(&alerts, snapshot.fetched_epoch);
    static const char *names[] = {"01-overview", "02-internet", "03-clients", "04-vpn", "05-sdwan",
                                  "06-devices", "07-alerts", "08-sites", "09-settings"};
    for(int page = 0; page < U_PAGE_COUNT; page++) {
        nav = (unifi_nav_t){.page = page}; save(argv[1], names[page]);
    }
    nav = (unifi_nav_t){.page = U_OVERVIEW, .mode = U_DETAIL, .selection = 5}; save(argv[1], "10-daily-traffic");
    nav = (unifi_nav_t){.page = U_INTERNET, .mode = U_DETAIL}; save(argv[1], "11-internet-details");
    nav = (unifi_nav_t){.page = U_CLIENTS, .mode = U_DETAIL}; save(argv[1], "12-client-list");
    unifi_demo_snapshot(&snapshot, UNIFI_DEMO_EPOCH, 0, true);
    nav = (unifi_nav_t){.page = U_VPN, .mode = U_DETAIL}; save(argv[1], "13-vpn-list");
    unifi_fixture(&snapshot, &connection);
    nav = (unifi_nav_t){.page = U_SDWAN, .mode = U_DETAIL}; save(argv[1], "14-sdwan-details");
    nav = (unifi_nav_t){.page = U_DEVICES, .mode = U_DETAIL}; save(argv[1], "15-device-uptime");
    nav.rates = true; save(argv[1], "16-device-traffic");
    nav = (unifi_nav_t){.page = U_SETTINGS, .mode = U_PORTAL}; save(argv[1], "17-setup-hotspot");
    nav = (unifi_nav_t){.page = U_SDWAN, .mode = U_INFO}; save(argv[1], "18-reconnect-unavailable");
    nav = (unifi_nav_t){.page = U_VPN, .mode = U_MENU}; save(argv[1], "19-vpn-menu");
    strcpy(snapshot.sites[0].name, "一个非常长的中文网络名称用于确认自动省略");
    strcpy(snapshot.clients.items[0].name, "办公室非常长的远程客户端名称🙂");
    strcpy(snapshot.clients.items[0].ip, "2001:db8:1234:5678:abcd:1234:5678:9012");
    nav = (unifi_nav_t){.page = U_CLIENTS, .mode = U_DETAIL}; save(argv[1], "20-long-names-ipv6");
    snapshot.demo = false; snapshot.api_ok = false; snapshot.metric_epoch = now - 4500;
    nav = (unifi_nav_t){.page = U_OVERVIEW}; save(argv[1], "21-stale-api-error");
    memset(&snapshot, 0, sizeof snapshot); memset(&connection, 0, sizeof connection);
    nav = (unifi_nav_t){.page = U_VPN}; save(argv[1], "22-unavailable-data");
    unifi_fixture(&snapshot, &connection);
    snapshot.demo = false; snapshot.sdwan.available = false;
    strcpy(snapshot.sdwan.type, "unsupported");
    strcpy(snapshot.sdwan.error, "Mesh status not exposed by this API");
    nav = (unifi_nav_t){.page = U_SDWAN}; save(argv[1], "23-mesh-unavailable");
    unifi_fixture(&snapshot, &connection);
    nav = (unifi_nav_t){.page = U_DEVICES, .mode = U_DETAIL, .selection = 10, .rates = true};
    save(argv[1], "24-offline-device-traffic");
    nav = (unifi_nav_t){.page = U_SETTINGS, .mode = U_INFO};
    save(argv[1], "25-connection-diagnostics");
    unifi_fixture(&snapshot, &connection);snapshot.demo=false;snapshot.api_ok=true;
    snapshot.wan_epoch=now-5;snapshot.sdwan.updated_epoch=now-8;
    snapshot.metric_epoch=now-1800;
    nav=(unifi_nav_t){.page=U_INTERNET};save(argv[1],"26-current-wan");
    strcpy(snapshot.sdwan.remote_host,snapshot.sites[1].host_id);
    strcpy(snapshot.sdwan.remote_site,snapshot.sites[1].site_id);
    nav=(unifi_nav_t){.page=U_SDWAN};save(argv[1],"27-connected-mesh");
    nav=(unifi_nav_t){.page=U_OVERVIEW,.mode=U_DETAIL};save(argv[1],"28-unavailable-daily-traffic");
    unifi_fixture(&snapshot,&connection);
    nav=(unifi_nav_t){.page=U_OVERVIEW,.mode=U_DETAIL,.wan_details=true,.selection=1};save(argv[1],"29-wan-details");
    nav=(unifi_nav_t){.page=U_OVERVIEW};snapshot.wans[1].present=false;save(argv[1],"30-single-wan");
    snapshot.wans[1]=snapshot.wans[0];strcpy(snapshot.wans[1].state,"offline");snapshot.wans[1].link_up=false;save(argv[1],"31-wan-offline");
    snapshot.wans_epoch=now-300;save(argv[1],"32-wan-stale");
    unifi_fixture(&snapshot,&connection);snapshot.wans[2]=snapshot.wans[0];snapshot.wans[3]=snapshot.wans[0];save(argv[1],"33-four-wans");
    lv_mem_monitor_t memory;
    lv_mem_monitor(&memory);
    assert(memory.max_used < 24 * 1024);
    assert(lv_mem_test() == LV_RESULT_OK);
    printf("LVGL RGB565 render: PASS (%u frames, %u glyph pixels, UTF-8/width checks; peak LVGL heap %zu/24576 bytes)\n",
           frames, pixel_checks, memory.max_used);
    lv_display_delete(display);
    lv_deinit();
    return 0;
}
