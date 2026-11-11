#pragma once

#include "lvgl.h"
#include "unifi_model.h"

/* The same renderer is linked into firmware and the offline host preview.
 * Call only from the LVGL task, or while holding the BSP LVGL lock. */
void unifi_lvgl_render(lv_layer_t *layer, const unifi_nav_t *nav,
                       const unifi_snapshot_t *snapshot,
                       const unifi_connection_t *connection,
                       const unifi_alerts_t *alerts, int battery, int64_t now);

/* Real embedded-font width and UTF-8-safe truncation, exposed for host checks.
 * Unsupported characters become '?'; only truncated strings gain '...'. */
const lv_font_t *unifi_lvgl_font(int pixels);
void unifi_lvgl_fit_text(char *out, size_t capacity, const char *text,
                         const lv_font_t *font, int width);
