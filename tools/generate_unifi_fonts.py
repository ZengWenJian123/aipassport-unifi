#!/usr/bin/env python3
"""Generate and validate compact, uncompressed LVGL 9 UniFi fonts.

Generation: Pillow 11.3.0 (FreeType 2.13.3), fonttools 4.60.1.
Structural/coverage checks use the Python standard library only. The optional
--verify-raster check rereads every generated C glyph and compares its pixels
with a fresh FreeType raster, rather than trusting the converter's output.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from bisect import bisect_left
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FONT_DIR = ROOT / "assets" / "fonts"
SOURCE_NAME = "unifi_NotoSansCJKsc-Regular.otf"
SOURCE_SHA256 = "2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b"
SOURCE_URL = (
    "https://raw.githubusercontent.com/notofonts/noto-cjk/"
    "523d033d6cb47f4a80c58a35753646f5c3608a78/"
    "Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf"
)
LICENSE_SHA256 = "6a73f9541c2de74158c0e7cf6b0a58ef774f5a780bf191f2d7ec9cc53efe2bf2"
MANIFEST_NAME = "unifi_font_manifest.json"
SIZES = {12: 16, 14: 18, 18: 22, 24: 28}
ASCII = set(range(0x20, 0x7F))
SYMBOLS = "\u3000·：，。！？（）【】《》、—–…℃％￥×÷±°→←↑↓✓●○▶◀▲▼"
# Comments are tokens too, so their quoted examples cannot become UI text.
C_TOKENS = re.compile(r'//[^\n]*|/\*.*?\*/|(?:u8|[uUL])?"(?P<text>(?:\\.|[^"\\])*)"', re.S)
GLYPH_RE = re.compile(
    r"\{\.bitmap_index = (\d+), \.adv_w = (\d+), \.box_w = (\d+), "
    r"\.box_h = (\d+), \.ofs_x = (-?\d+), \.ofs_y = (-?\d+)\}"
)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def gb2312_hanzi() -> set[int]:
    result: set[int] = set()
    for lead in range(0xB0, 0xF8):
        for trail in range(0xA1, 0xFF):
            try:
                result.add(ord(bytes((lead, trail)).decode("gb2312")))
            except UnicodeDecodeError:
                pass
    if len(result) != 6763:
        raise ValueError("Unexpected GB2312 Han character mapping")
    return result


def literal_codepoints(text: str) -> set[int]:
    """Printable codepoints in C string literals, including Unicode escapes."""
    result: set[int] = set()
    for token in C_TOKENS.finditer(text):
        value = token.group("text")
        if value is None:
            continue
        value = re.sub(
            r"\\(?:u([0-9a-fA-F]{4})|U([0-9a-fA-F]{8})|x([0-9a-fA-F]+)|([0-7]{1,3}))",
            lambda m: chr(int(next(g for g in m.groups() if g), 8 if m.group(4) else 16)),
            value,
        )
        # These escapes are control characters, not printable letters in a UI.
        value = re.sub(r"\\[abfnrtv]", "", value)
        value = re.sub(r"\\(.)", r"\1", value)
        result.update(ord(c) for c in value if c.isprintable() or c == "\u3000")
    return result


def ui_codepoints(root: Path = ROOT) -> set[int]:
    result: set[int] = set()
    # Includes unifi_view.c when added; tests fail for new unsupported literals.
    for extension in ("c", "h"):
        for path in (root / "main").glob(f"unifi*.{extension}"):
            result.update(literal_codepoints(path.read_text(encoding="utf-8")))
    return result


def pack_4bpp(pixels: list[int]) -> bytes:
    """High nibble first; odd rows continue into the same byte (stride = 0)."""
    output = bytearray((len(pixels) + 1) // 2)
    for i, pixel in enumerate(pixels):
        if not 0 <= pixel <= 15:
            raise ValueError("A 4bpp pixel must be in 0..15")
        output[i // 2] |= pixel << (4 if i % 2 == 0 else 0)
    return bytes(output)


def unpack_4bpp(data: bytes, count: int) -> list[int]:
    return [(data[i // 2] >> (4 if i % 2 == 0 else 0)) & 15 for i in range(count)]


def load_renderer():
    import PIL
    from PIL import Image, ImageDraw, ImageFont, features
    from fontTools.ttLib import TTFont

    if PIL.__version__ != "11.3.0" or features.version_module("freetype2") != "2.13.3":
        raise ValueError("Regeneration requires Pillow 11.3.0 with FreeType 2.13.3")
    return Image, ImageDraw, ImageFont, TTFont


def raster_glyph(font, codepoint: int):
    from PIL import Image, ImageDraw

    char = chr(codepoint)
    left, top, right, bottom = font.getbbox(char, anchor="ls")
    width, height = right - left, bottom - top
    advance = round(font.getlength(char) * 16)
    if not height or char.isspace():
        return (0, advance, 0, 0, 0, 0), []
    bitmap = Image.new("L", (width, height))
    ImageDraw.Draw(bitmap).text((-left, -top), char, font=font, fill=255, anchor="ls")
    pixels = [(p + 8) // 17 for p in bitmap.getdata()]
    # LVGL places y at line_height - base_line - box_h - ofs_y.
    return (0, advance, width, height, left, -bottom), pixels


def c_array(name: str, ctype: str, values, hexadecimal=False) -> str:
    width = 16 if ctype == "uint8_t" else 12
    lines = []
    for start in range(0, len(values), width):
        chunk = values[start:start + width]
        parts = [f"0x{v:02x}" if hexadecimal else str(v) for v in chunk]
        lines.append("    " + ", ".join(parts) + ",")
    return f"static const {ctype} {name}[] = {{\n" + "\n".join(lines) + "\n};\n"


def generate_font(size: int, codepoints: list[int], source: Path) -> tuple[str, dict]:
    _, _, ImageFont, _ = load_renderer()
    font = ImageFont.truetype(str(source), size, layout_engine=ImageFont.Layout.BASIC)
    glyphs = [(0, 0, 0, 0, 0, 0)]
    bitmap = bytearray()
    min_top, max_bottom = 0, 0
    for cp in codepoints:
        glyph, pixels = raster_glyph(font, cp)
        _, advance, width, height, offset_x, offset_y = glyph
        glyphs.append((len(bitmap), advance, width, height, offset_x, offset_y))
        bitmap.extend(pack_4bpp(pixels))
        if height:
            min_top = min(min_top, -height - offset_y)
            max_bottom = max(max_bottom, -offset_y)
    line_height = SIZES[size]
    ink_height = max_bottom - min_top
    if ink_height > line_height:
        raise ValueError(f"{size}px font needs {ink_height}px; requested line height {line_height}")
    baseline_y = (line_height - ink_height) // 2 - min_top
    base_line = line_height - baseline_y
    non_ascii = [cp for cp in codepoints if cp not in ASCII]
    if non_ascii and non_ascii[-1] - non_ascii[0] >= 65535:
        raise ValueError("Fixed UI must use BMP text; draw non-BMP icons as graphics")
    sparse_start = non_ascii[0] if non_ascii else 0
    offsets = [cp - sparse_start for cp in non_ascii]
    large = len(bitmap) >= 1 << 20
    text = (
        "/* Generated by tools/generate_unifi_fonts.py; do not edit.\n"
        " * Source: Noto Sans CJK SC Regular 2.004, SIL Open Font License 1.1.\n"
        " * See unifi_OFL.txt. PLAIN 4bpp, high nibble first, byte-aligned glyphs. */\n"
        '#include "lvgl.h"\n\n'
    )
    if large:
        text += '#if !LV_FONT_FMT_TXT_LARGE\n#error "unifi_font_18 needs CONFIG_LV_FONT_FMT_TXT_LARGE=y"\n#endif\n\n'
    text += c_array("glyph_bitmap", "uint8_t", bitmap, True) + "\n"
    text += "static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {\n"
    for index, advance, width, height, offset_x, offset_y in glyphs:
        text += (f"    {{.bitmap_index = {index}, .adv_w = {advance}, .box_w = {width}, "
                 f".box_h = {height}, .ofs_x = {offset_x}, .ofs_y = {offset_y}}},\n")
    text += "};\n\n"
    if non_ascii:
        text += c_array("unicode_list", "uint16_t", offsets) + "\n"
    text += (
        "static const lv_font_fmt_txt_cmap_t cmaps[] = {\n"
        "    {.range_start = 32, .range_length = 95, .glyph_id_start = 1,\n"
        "     .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0,\n"
        "     .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY},\n"
    )
    if non_ascii:
        text += (f"    {{.range_start = {sparse_start}, .range_length = {offsets[-1] + 1}, .glyph_id_start = 96,\n"
                 f"     .unicode_list = unicode_list, .glyph_id_ofs_list = NULL, .list_length = {len(offsets)},\n"
                 "     .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY},\n")
    text += (
        "};\n\nstatic const lv_font_fmt_txt_dsc_t font_dsc = {\n"
        "    .glyph_bitmap = glyph_bitmap, .glyph_dsc = glyph_dsc, .cmaps = cmaps,\n"
        f"    .kern_dsc = NULL, .kern_scale = 0, .cmap_num = {2 if non_ascii else 1},\n"
        "    .bpp = 4, .kern_classes = 0, .bitmap_format = LV_FONT_FMT_TXT_PLAIN, .stride = 0,\n"
        "};\n\n"
        f"const lv_font_t unifi_font_{size} = {{\n"
        "    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,\n"
        "    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,\n"
        f"    .line_height = {line_height}, .base_line = {base_line},\n"
        "    .subpx = LV_FONT_SUBPX_NONE, .underline_position = -1, .underline_thickness = 1,\n"
        "    .dsc = &font_dsc, .fallback = NULL,\n};\n"
    )
    metadata = {"size": size, "line_height": line_height, "base_line": base_line,
                "glyphs": len(codepoints), "bitmap_bytes": len(bitmap),
                "bitmap_sha256": sha256(bitmap), "source_c_sha256": sha256(text.encode()),
                "requires_large_descriptor": large, "codepoints": codepoints}
    return text, metadata


def parse_font(path: Path) -> dict:
    """Read actual C arrays; don't use the manifest to infer a cmap."""
    text = path.read_text(encoding="utf-8")
    def array(name: str):
        match = re.search(r"\b" + name + r"\[\] = \{(.*?)\n\};", text, re.S)
        if not match:
            raise ValueError(f"Missing C array: {name}")
        return [int(v, 0) for v in re.findall(r"0x[0-9a-f]+|\d+", match.group(1))]
    bitmap = bytes(array("glyph_bitmap"))
    glyphs = [tuple(map(int, m)) for m in GLYPH_RE.findall(text)]
    cmaps = []
    for entry in re.findall(r"\{\.range_start =.*?\}", text, re.S):
        def field(name: str):
            match = re.search(r"\." + name + r" = (\w+)", entry)
            if not match:
                raise ValueError(f"Missing cmap field: {name}")
            return match.group(1)
        if field("glyph_id_ofs_list") != "NULL":
            raise ValueError("Unexpected glyph offset map")
        cmaps.append({"start": int(field("range_start")), "length": int(field("range_length")),
                      "glyph_start": int(field("glyph_id_start")), "count": int(field("list_length")),
                      "type": field("type"), "offsets": [] if field("unicode_list") == "NULL" else array(field("unicode_list"))})
    line = re.search(r"\.line_height = (\d+), \.base_line = (\d+)", text)
    if not line or not glyphs or not cmaps:
        raise ValueError("Incomplete font descriptor")
    return {"text": text, "bitmap": bitmap, "glyphs": glyphs, "cmaps": cmaps,
            "line_height": int(line[1]), "base_line": int(line[2])}


def glyph_id(parsed: dict, codepoint: int) -> int:
    for cmap in parsed["cmaps"]:
        relative = codepoint - cmap["start"]
        if not 0 <= relative < cmap["length"]:
            continue
        if cmap["type"] == "LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY":
            return cmap["glyph_start"] + relative
        if cmap["type"] == "LV_FONT_FMT_TXT_CMAP_SPARSE_TINY":
            index = bisect_left(cmap["offsets"], relative)
            if index < len(cmap["offsets"]) and cmap["offsets"][index] == relative:
                return cmap["glyph_start"] + index
            return 0
        raise ValueError("Unsupported cmap type")
    return 0


def validate_font(parsed: dict, metadata: dict) -> None:
    bitmap, glyphs = parsed["bitmap"], parsed["glyphs"]
    if sha256(parsed["text"].encode()) != metadata["source_c_sha256"]:
        raise ValueError("Generated C checksum mismatch")
    if sha256(bitmap) != metadata["bitmap_sha256"] or len(bitmap) != metadata["bitmap_bytes"]:
        raise ValueError("Glyph bitmap checksum/length mismatch")
    if len(glyphs) != metadata["glyphs"] + 1 or glyphs[0] != (0, 0, 0, 0, 0, 0):
        raise ValueError("Missing reserved glyph or descriptor count mismatch")
    if (parsed["line_height"], parsed["base_line"]) != (metadata["line_height"], metadata["base_line"]):
        raise ValueError("Line height or baseline mismatch")
    for cmap in parsed["cmaps"]:
        if cmap["offsets"] != sorted(set(cmap["offsets"])) or len(cmap["offsets"]) != cmap["count"]:
            raise ValueError("Sparse cmap offsets are not sorted and unique")
    cursor = 0
    for expected, cp in enumerate(metadata["codepoints"], 1):
        if glyph_id(parsed, cp) != expected:
            raise ValueError(f"Bad cmap lookup for U+{cp:04X}")
        index, advance, width, height, offset_x, offset_y = glyphs[expected]
        count = width * height
        length = (count + 1) // 2
        if index != cursor or index + length > len(bitmap) or advance <= 0:
            raise ValueError(f"Invalid glyph bounds/advance for U+{cp:04X}")
        top = parsed["line_height"] - parsed["base_line"] - height - offset_y
        if height and (top < 0 or top + height > parsed["line_height"]):
            raise ValueError(f"Glyph clipped vertically: U+{cp:04X}")
        if count and not any(bitmap[index:index + length]):
            raise ValueError(f"Non-space glyph is blank: U+{cp:04X}")
        if count % 2 and bitmap[index + length - 1] & 15:
            raise ValueError(f"Odd-pixel glyph has nonzero trailing nibble: U+{cp:04X}")
        cursor += length
    if cursor != len(bitmap):
        raise ValueError("Trailing/unaddressed glyph data")
    if not all(token in parsed["text"] for token in (".stride = 0", ".bpp = 4", ".bitmap_format = LV_FONT_FMT_TXT_PLAIN")):
        raise ValueError("Expected uncompressed 4bpp without row padding")


def check(output_dir: Path = FONT_DIR, root: Path = ROOT, verify_raster=False) -> dict:
    manifest = json.loads((output_dir / MANIFEST_NAME).read_text(encoding="utf-8"))
    if sha256((output_dir / SOURCE_NAME).read_bytes()) != SOURCE_SHA256:
        raise ValueError("Source font checksum mismatch")
    if sha256((output_dir / "unifi_OFL.txt").read_bytes()) != LICENSE_SHA256:
        raise ValueError("Source font license checksum mismatch")
    required = ASCII | gb2312_hanzi() | set(map(ord, SYMBOLS)) | ui_codepoints(root)
    report = {"fonts": [], "raster_verified": verify_raster}
    if verify_raster:
        _, _, ImageFont, _ = load_renderer()
    for metadata in manifest["fonts"]:
        size = metadata["size"]
        parsed = parse_font(output_dir / f"unifi_font_{size}.c")
        validate_font(parsed, metadata)
        missing = (ASCII if size == 24 else required) - set(metadata["codepoints"])
        if missing:
            raise ValueError(f"{size}px missing " + ", ".join(f"U+{cp:04X}" for cp in sorted(missing)))
        if verify_raster:
            font = ImageFont.truetype(str(output_dir / SOURCE_NAME), size, layout_engine=ImageFont.Layout.BASIC)
            for cp in metadata["codepoints"]:
                expected, pixels = raster_glyph(font, cp)
                actual = parsed["glyphs"][glyph_id(parsed, cp)]
                index, _, width, height, _, _ = actual
                decoded = unpack_4bpp(parsed["bitmap"][index:], width * height)
                if actual[1:] != expected[1:] or decoded != pixels:
                    raise ValueError(f"Raster round-trip mismatch: {size}px U+{cp:04X}")
        report["fonts"].append({k: v for k, v in metadata.items() if k != "codepoints"})
    return report


def sample_sheet(output_dir: Path, destination: Path) -> None:
    """Render decoded C bitmaps at native size and 3x, never a substitute font."""
    from PIL import Image
    rows = [(12, "网络总览 192.168.10.1 接收 12.6 Mbps"),
            (14, "客户端 最近上线 配网连接中 离线重试"),
            (18, "互联网 VPN 设置与配网"), (24, "0123456789 / 94%"),
            (14, "上下行 · 警告！测试龟鬟齄\u3000? ↑↓")]
    sheet = Image.new("RGB", (720, 240), "#111315")
    for row, (size, text) in enumerate(rows):
        data = parse_font(output_dir / f"unifi_font_{size}.c")
        x, y = 12, row * 46 + 5
        for char in text:
            gid = glyph_id(data, ord(char)) or glyph_id(data, ord("?"))
            index, advance, width, height, offset_x, offset_y = data["glyphs"][gid]
            if width and height:
                pixels = unpack_4bpp(data["bitmap"][index:], width * height)
                mask = Image.new("L", (width, height))
                mask.putdata([pixel * 17 for pixel in pixels])
                top = y + data["line_height"] - data["base_line"] - height - offset_y
                sheet.paste("#edf0f4", (x + offset_x, top, x + offset_x + width, top + height), mask)
            x += (advance + 8) // 16
    destination.mkdir(parents=True, exist_ok=True)
    sheet.save(destination / "font-sample-native.png")
    sheet.resize((2160, 720), Image.Resampling.NEAREST).save(destination / "font-sample-3x.png")


def generate(output_dir: Path = FONT_DIR, report_dir: Path = ROOT / "build" / "unifi-fonts") -> dict:
    _, _, _, TTFont = load_renderer()
    source = output_dir / SOURCE_NAME
    if sha256(source.read_bytes()) != SOURCE_SHA256:
        raise ValueError("Download the pinned source font before regeneration")
    requested = ASCII | gb2312_hanzi() | set(map(ord, SYMBOLS)) | ui_codepoints()
    old_manifest = output_dir / MANIFEST_NAME
    if old_manifest.exists():
        for font in json.loads(old_manifest.read_text())["fonts"]:
            if font["size"] != 24:
                requested.update(font["codepoints"])
    available = TTFont(source).getBestCmap()
    missing = requested - available.keys()
    if missing:
        raise ValueError("Source lacks glyphs: " + ", ".join(f"U+{cp:04X}" for cp in sorted(missing)))
    manifest = {"family": "Noto Sans CJK SC Regular", "version": "2.004", "license": "SIL OFL 1.1",
                "source_url": SOURCE_URL, "source_sha256": SOURCE_SHA256,
                "renderer": "Pillow 11.3.0 / FreeType 2.13.3 / BASIC", "fonts": []}
    for size in SIZES:
        cps = sorted(ASCII if size == 24 else requested)
        source_c, metadata = generate_font(size, cps, source)
        (output_dir / f"unifi_font_{size}.c").write_text(source_c, encoding="utf-8")
        manifest["fonts"].append(metadata)
        print(f"Generated {size}px: {metadata['glyphs']} glyphs; {metadata['bitmap_bytes']} bitmap bytes")
    old_manifest.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    report = check(output_dir, verify_raster=True)
    report_dir.mkdir(parents=True, exist_ok=True)
    (report_dir / "font-verification.json").write_text(json.dumps(report, indent=2) + "\n")
    sample_sheet(output_dir, report_dir)
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Validate checked-in C maps/bitmaps and all fixed UI strings")
    parser.add_argument("--verify-raster", action="store_true", help="Also compare all pixels against pinned FreeType")
    args = parser.parse_args()
    report = check(verify_raster=args.verify_raster) if args.check else generate()
    print(f"Font validation: PASS ({sum(f['glyphs'] for f in report['fonts'])} glyphs)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
