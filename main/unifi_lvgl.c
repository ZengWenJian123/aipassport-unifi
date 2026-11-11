#include "unifi_lvgl.h"
#include "unifi_fonts.h"
#include "unifi_view.h"
#include <string.h>

const lv_font_t *unifi_lvgl_font(int pixels)
{
    return pixels == 12 ? &unifi_font_12 : pixels == 14 ? &unifi_font_14 :
           pixels == 18 ? &unifi_font_18 : &unifi_font_24;
}

/* Invalid sequences consume one byte, so no read passes a terminating NUL. */
static size_t decode(const char *text, uint32_t *codepoint)
{
    const unsigned char *p = (const unsigned char *)text;
    size_t length;
    uint32_t value;
    if(p[0] < 0x80) { *codepoint = p[0]; return 1; }
    if(p[0] >= 0xC2 && p[0] <= 0xDF) { length = 2; value = p[0] & 31; }
    else if(p[0] >= 0xE0 && p[0] <= 0xEF) { length = 3; value = p[0] & 15; }
    else if(p[0] >= 0xF0 && p[0] <= 0xF4) { length = 4; value = p[0] & 7; }
    else { *codepoint = '?'; return 1; }
    for(size_t i = 1; i < length; i++) {
        if(p[i] == 0 || (p[i] & 0xC0) != 0x80) { *codepoint = '?'; return 1; }
        value = (value << 6) | (p[i] & 63);
    }
    if((length == 3 && value < 0x800) || (length == 4 && value < 0x10000) ||
       value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
        *codepoint = '?'; return 1;
    }
    *codepoint = value;
    return length;
}

void unifi_lvgl_fit_text(char *out, size_t capacity, const char *text,
                         const lv_font_t *font, int width)
{
    if(capacity == 0) return;
    out[0] = 0;
    if(width <= 0 || text == NULL || font == NULL) return;
    size_t used = 0;
    int advance = 0;
    while(*text) {
        uint32_t cp;
        size_t consumed = decode(text, &cp);
        bool supported = cp >= 32 && cp != 127 && unifi_font_has_codepoint(font, cp);
        /* A malformed input byte decodes as '?', but must not be copied. */
        if(cp == '?' && consumed == 1 && (unsigned char)text[0] != '?') supported = false;
        size_t bytes = supported ? consumed : 1;
        uint32_t rendered = supported ? cp : '?';
        int glyph_width = lv_font_get_glyph_width(font, rendered, 0);
        if(advance + glyph_width > width || used + bytes >= capacity) break;
        if(supported) memcpy(out + used, text, bytes);
        else out[used] = '?';
        used += bytes;
        advance += glyph_width;
        text += consumed;
    }
    out[used] = 0;
    if(*text == 0) return;

    const int dot_width = lv_font_get_glyph_width(font, '.', 0);
    int dots = 3;
    while(dots > 0 && (dots * dot_width > width || (size_t)dots >= capacity)) dots--;
    while(used > 0 && (advance + dots * dot_width > width || used + (size_t)dots >= capacity)) {
        size_t start = used - 1;
        while(start > 0 && ((unsigned char)out[start] & 0xC0) == 0x80) start--;
        uint32_t cp;
        (void)decode(out + start, &cp);
        advance -= lv_font_get_glyph_width(font, cp, 0);
        used = start;
    }
    for(int i = 0; i < dots; i++) out[used++] = '.';
    out[used] = 0;
}

static void draw_text(void *context, int x, int y, int width, int pixels,
                       uint32_t color, const char *text)
{
    const lv_font_t *font = unifi_lvgl_font(pixels);
    char fitted[240];
    unifi_lvgl_fit_text(fitted, sizeof fitted, text, font, width);
    lv_draw_label_dsc_t descriptor;
    lv_draw_label_dsc_init(&descriptor);
    descriptor.font = font;
    descriptor.color = lv_color_hex(color);
    descriptor.text = fitted;
    descriptor.text_local = 1; /* LVGL copies the transient fitted string. */
    lv_area_t area = {x, y, x + width - 1, y + font->line_height - 1};
    lv_draw_label(context, &descriptor, &area);
}

static void draw_rect(void *context, int x, int y, int width, int height,
                       int radius, uint32_t color)
{
    if(width <= 0 || height <= 0) return;
    lv_draw_rect_dsc_t descriptor;
    lv_draw_rect_dsc_init(&descriptor);
    descriptor.bg_color = lv_color_hex(color);
    descriptor.bg_opa = LV_OPA_COVER;
    descriptor.radius = radius;
    lv_area_t area = {x, y, x + width - 1, y + height - 1};
    lv_draw_rect(context, &descriptor, &area);
}

static void draw_line(void *context, int x, int y, int end_x, int end_y,
                       int width, uint32_t color)
{
    lv_draw_line_dsc_t descriptor;
    lv_draw_line_dsc_init(&descriptor);
    descriptor.p1.x = x; descriptor.p1.y = y;
    descriptor.p2.x = end_x; descriptor.p2.y = end_y;
    descriptor.width = width;
    descriptor.color = lv_color_hex(color);
    lv_draw_line(context, &descriptor);
}

static void draw_arc(void *context, int x, int y, int radius, int start,
                      int end, int width, uint32_t color)
{
    lv_draw_arc_dsc_t descriptor;
    lv_draw_arc_dsc_init(&descriptor);
    descriptor.center.x = x; descriptor.center.y = y;
    descriptor.radius = radius;
    descriptor.start_angle = start; descriptor.end_angle = end;
    descriptor.width = width;
    descriptor.color = lv_color_hex(color);
    lv_draw_arc(context, &descriptor);
}

void unifi_lvgl_render(lv_layer_t *layer, const unifi_nav_t *nav,
                       const unifi_snapshot_t *snapshot,
                       const unifi_connection_t *connection,
                       const unifi_alerts_t *alerts, int battery, int64_t now)
{
    unifi_canvas_t canvas = {layer, draw_text, draw_rect, draw_line, draw_arc};
    unifi_view(&canvas, nav, snapshot, connection, alerts, battery, now);
}
