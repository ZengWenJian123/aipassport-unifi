#!/usr/bin/env python3
"""Compile the real LVGL renderer offline; save 240x320 PNGs and review sheets.

Requires CMake, a C compiler, the repository's installed LVGL component, and
Pillow (the font-tools venv can be reused). Never accesses devices or APIs.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("build/unifi-preview"))
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    from PIL import Image, ImageDraw, ImageFont

    root = Path(__file__).resolve().parents[1]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    build = output / "native-build"
    if not (root / "managed_components/lvgl__lvgl/CMakeLists.txt").is_file():
        parser.error("Install the project's managed LVGL component before running the offline preview.")
    subprocess.run(["cmake", "-S", str(root / "tests/unifi_preview"), "-B", str(build),
                    "-DCMAKE_BUILD_TYPE=Release"], check=True)
    subprocess.run(["cmake", "--build", str(build), "--parallel", str(max(1, args.jobs))], check=True)
    subprocess.run([str(build / "unifi_preview"), str(output)], check=True, timeout=60)
    images = []
    for ppm in sorted(output.glob("[0-9][0-9]-*.ppm")):
        with Image.open(ppm) as source:
            image = source.convert("RGB")
        assert image.size == (240, 320)
        png = ppm.with_suffix(".png")
        image.save(png)
        ppm.unlink()
        images.append((png.name, image))
    caption_font = ImageFont.truetype(str(root / "assets/fonts/unifi_NotoSansCJKsc-Regular.otf"), 16)
    # Native pixels are enlarged with nearest-neighbor, so blur cannot conceal
    # corrupted glyphs or clipping. Headers sit outside the device framebuffer.
    for name, group in (("overview-sheet", images[:9]), ("details-sheet", images[9:])):
        columns = 3
        scale = 2
        cell_w, cell_h = 520, 690
        rows = (len(group) + columns - 1) // columns
        sheet = Image.new("RGB", (columns * cell_w, rows * cell_h), "#20252d")
        draw = ImageDraw.Draw(sheet)
        for i, (filename, image) in enumerate(group):
            x, y = (i % columns) * cell_w + 20, (i // columns) * cell_h + 30
            draw.text((x, y - 24), filename, font=caption_font, fill="#cad5e2")
            sheet.paste(image.resize((240 * scale, 320 * scale), Image.Resampling.NEAREST), (x, y))
        sheet.save(output / f"{name}.png")
    (output / "preview-report.json").write_text(json.dumps({
        "renderer": "LVGL 9.5 software RGB565 with 24 KiB builtin heap; same unifi_lvgl.c/unifi_view.c/font assets as firmware",
        "width": 240, "height": 320,
        "checks": ["21 isolated glyphs compared to four-bit source bitmaps at every output pixel",
                   "UTF-8 validation and measured-width truncation",
                   "normal, detail, setup, long names, IPv6, stale and unavailable states"],
        "frames": [name for name, _ in images],
        "device_validation": False,
    }, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Saved {len(images)} native PNGs and two contact sheets: {output}")


if __name__ == "__main__":
    main()
