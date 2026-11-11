#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* GB2312 Han characters + fixed UniFi UI text, raw 4bpp in read-only Flash.
 * Exact line heights: 12px -> 16, 14px -> 18, 18px -> 22, 24px -> 28.
 * The 24px face is ASCII-only; draw Chinese unit labels with a smaller face.
 * Unsupported dynamic codepoints must be replaced with ASCII '?' at complete
 * UTF-8 boundaries. No fallback font or private-use icon glyphs are included.
 * Draw icons separately. The 18px face requires LV_FONT_FMT_TXT_LARGE=1. */
LV_FONT_DECLARE(unifi_font_12);
LV_FONT_DECLARE(unifi_font_14);
LV_FONT_DECLARE(unifi_font_18);
LV_FONT_DECLARE(unifi_font_24);

/* Call after LVGL initialization, under the same lock as UI/font operations. */
static inline bool unifi_font_has_codepoint(const lv_font_t *font, uint32_t cp)
{
    if (font == NULL) return false;
    lv_font_glyph_dsc_t descriptor = {0};
    return lv_font_get_glyph_dsc(font, &descriptor, cp, 0)
        && !descriptor.is_placeholder;
}

#ifdef __cplusplus
}
#endif
