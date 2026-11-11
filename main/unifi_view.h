#pragma once
#include "unifi_model.h"
/* Immediate vector display list. Coordinates are physical 240 x 320 pixels.
 * Host tests consume the same operations as the LVGL renderer. */
typedef struct {
    void *ctx;
    void (*text)(void *, int x, int y, int width, int font, uint32_t color, const char *);
    void (*rect)(void *, int x, int y, int w, int h, int radius, uint32_t color);
    void (*line)(void *, int x1, int y1, int x2, int y2, int width, uint32_t color);
    void (*arc)(void *, int x, int y, int r, int start, int end, int width, uint32_t color);
} unifi_canvas_t;
void unifi_view(const unifi_canvas_t *, const unifi_nav_t *, const unifi_snapshot_t *,
                const unifi_connection_t *, const unifi_alerts_t *, int battery, int64_t now);
