<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Assets

This directory stores reusable fonts, images, music, sound effects, and battery profiles, organized by asset type.

Keep each asset in the matching subdirectory and document its destination, naming, integration method, and source/license. Do not mix binary assets with Markdown documentation.

## Fonts

Store reusable font files and generated font sources in `fonts/`.

- Use descriptive names that include the family, weight, size, and format when relevant.
- Document the source, license, character range, conversion command, and expected destination.
- Check Flash and internal-RAM impact before adding a font; the ESP32-C3 has no PSRAM.
- Do not commit fonts whose license does not permit redistribution.

### UniFi dashboard Chinese fonts

[`fonts/unifi_fonts.h`](fonts/unifi_fonts.h) declares the application-owned
`unifi_font_12`, `unifi_font_14`, `unifi_font_18`, and `unifi_font_24` faces.
Compile their matching `.c` files in the application and explicitly select the
appropriate face on every text widget. These assets do not replace BSP fonts.

| Face | Line height / baseline from bottom | Coverage | Bitmap bytes |
| --- | --- | --- | --- |
| `unifi_font_12` | 16 / 4 px | 6,893 codepoints | 515,553 |
| `unifi_font_14` | 18 / 5 px | 6,893 codepoints | 679,001 |
| `unifi_font_18` | 22 / 5 px | 6,893 codepoints | 1,104,448 |
| `unifi_font_24` | 28 / 7 px | 95 printable ASCII characters | 10,627 |

The three Chinese faces include all 6,763 GB2312 level 1/2 Han characters,
ASCII, punctuation, arrows, and the fixed application text. They do not cover
arbitrary Unicode names or emoji. For dynamic text, replace unsupported
codepoints with `?` without splitting UTF-8 sequences; `unifi_font_has_codepoint()`
checks the actual font. The 24 px face is for ASCII numbers and units only;
use a separate smaller Chinese label where needed. Draw icons as graphics.

The pinned, redistributable input is
[`fonts/unifi_NotoSansCJKsc-Regular.otf`](fonts/unifi_NotoSansCJKsc-Regular.otf),
Noto Sans CJK SC Regular 2.004 from the
[official upstream commit](https://github.com/notofonts/noto-cjk/tree/523d033d6cb47f4a80c58a35753646f5c3608a78).
Its SHA-256 is `2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b`.
The source and derived fonts use [SIL OFL 1.1](fonts/unifi_OFL.txt); keep that
license with redistributed font assets. The 16 MB source OTF is an authoring
asset and is **not** embedded into firmware.

[`tools/generate_unifi_fonts.py`](../tools/generate_unifi_fonts.py) rasterizes
with Pillow 11.3.0 / FreeType 2.13.3, then writes raw 4bpp with the high nibble
first, byte-aligned glyph starts, and no row padding. Cmaps use contiguous
ASCII `FORMAT0_TINY` plus a sorted `SPARSE_TINY` map. All bitmap, glyph, and
cmap arrays are `const` in Flash. Enable `CONFIG_LV_FONT_FMT_TXT_LARGE=y`:
the 18 px bitmap exceeds the default 20-bit index limit. Together the four
faces use about 2.68 MB of Flash including large glyph descriptors, with only
per-glyph rendering buffers needed at runtime; verify final linked size and
heap usage on the target.

```bash
python3 -m venv build/font-tools
build/font-tools/bin/pip install Pillow==11.3.0 fonttools==4.60.1
build/font-tools/bin/python tools/generate_unifi_fonts.py
python3 tools/generate_unifi_fonts.py --check
python3 tests/test_unifi_fonts.py
build/font-tools/bin/python tools/generate_unifi_fonts.py --check --verify-raster
```

Generation reads string literals in `main/unifi*.c` and `main/unifi*.h`
(including `unifi_view.c`), preserving previously recorded extra codepoints in
[`fonts/unifi_font_manifest.json`](fonts/unifi_font_manifest.json). New fixed
text must pass coverage checks at 12, 14, and 18 px. Standard-library checks
validate actual C maps, bounds, baseline, byte hashes, nonblank glyphs, missing
characters, and odd-width nibble packing. The raster check decodes every C
glyph and compares all pixels against a fresh source raster. Generation also
writes decoded-font sample PNGs and a JSON report into ignored
`build/unifi-fonts/`. These host checks do not prove on-device glyph rendering.

For a check through the **actual LVGL 9.5 renderer**, run
`build/font-tools/bin/python tools/render_unifi_preview.py` after installing
the managed LVGL component. This builds the same application view, rendering
code, and font assets using a 24 KiB LVGL heap and RGB565 partial framebuffer.
It checks UTF-8 truncation and compares representative glyphs pixel by pixel
against their packed four-bit source. Nine pages and detail/error/setup states
are saved as native 240 × 320 PNGs in ignored `build/unifi-preview/`, with
nearest-neighbor contact sheets for visual inspection. CMake, a host C/C++
compiler, and Pillow are required; no network or device connection is used.

## Images

Store reusable source images and generated display assets in `images/`.

| File | Dimensions and format | Use and source |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160, JPEG | Product hero image embedded in both project README files to foreground AI Passport and its open, maker-oriented identity. |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724, PNG RGBA | Optional technical infographic retained as a reference asset; it is no longer used as the homepage hero. Generated for this repository with the built-in image generation tool on 2026-09-17; the six labels and values were checked against the documented hardware contract. |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336, PNG RGBA | Transparent black wordmark extracted from the repository's original `images/logo.png`; embedded in both project README files for light backgrounds. |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336, PNG RGBA | White version of the extracted wordmark, used by the README `<picture>` element when GitHub is in dark mode. |

- Use descriptive names and document dimensions, pixel format, conversion steps, and destination.
- Prefer formats suitable for the 240 × 320 RGB565 display and account for Flash and internal RAM.
- Preserve editable sources where licensing permits, and record the source and license.
- Never commit device QR secrets, credentials, or personal data in images.

## Music and sound effects

Store reusable music and sound-effect sources in `music/`.

- Document the source, license, sample rate, bit depth, channels, conversion command, and destination.
- Prefer 16 kHz, 16-bit mono PCM when it matches the current BSP audio path.
- Check Flash and internal-RAM cost before embedding audio; stream or chunk long recordings.
- Do not commit media without redistribution permission.

## Battery profiles

[`battery/wanli-500mah-profile2-xil40x-20261020.txt`](battery/wanli-500mah-profile2-xil40x-20261020.txt)
contains the supplied CW2017 profile for the Wanli New Energy 500 mAh cell
(profile2, XIL40X, 2026-10-20). It is an unchanged copy of the project-supplied
file, retained at the maintainer's request for this firmware update. Its
original filename is recorded in the paired Chinese document. No separate
license was included in the supplied file.

The format is 80 lines of two hexadecimal digits, one byte per line in register
order. The BSP embeds those bytes in `components/bsp/src/bsp_battery.c`; the
host gate checks the source file and initializer for an exact match. Text-file
SHA-256: `7ea96eba4634f4495a37c25ffacd54c1deaa0693b162c892d671a2ba68460919`.
Decoded 80-byte SHA-256:
`fa7365671093ef449c5bdcfb4adb279e3d275f888109f50b6b2f21e9b96220d5`.
This is a fuel-gauge profile, not a charging configuration or a generic profile
for every 500 mAh cell; see the [hardware guide](../docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md#9-cw2017-fuel-gauge).
