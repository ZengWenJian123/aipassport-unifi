#!/usr/bin/env python3
"""Exercise real LVGL cmap/bitmap artifacts, without requiring Pillow on CI."""

import copy
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("unifi_font_generator", ROOT / "tools" / "generate_unifi_fonts.py")
fonts = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fonts)


class UniFiFontTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest = json.loads((fonts.FONT_DIR / fonts.MANIFEST_NAME).read_text())
        cls.metadata = {f["size"]: f for f in cls.manifest["fonts"]}
        cls.parsed = {size: fonts.parse_font(fonts.FONT_DIR / f"unifi_font_{size}.c") for size in fonts.SIZES}

    def test_source_is_pinned_and_license_present(self):
        self.assertEqual(fonts.sha256((fonts.FONT_DIR / fonts.SOURCE_NAME).read_bytes()), fonts.SOURCE_SHA256)
        license_text = (fonts.FONT_DIR / "unifi_OFL.txt").read_bytes()
        self.assertEqual(fonts.sha256(license_text), fonts.LICENSE_SHA256)
        self.assertIn(b"SIL OPEN FONT LICENSE Version 1.1", license_text)

    def test_generated_arrays_and_descriptors_are_consistent(self):
        for size, parsed in self.parsed.items():
            with self.subTest(size=size):
                fonts.validate_font(parsed, self.metadata[size])

    def test_all_6763_gb2312_hanzi_have_real_nonblank_glyphs(self):
        hanzi = fonts.gb2312_hanzi()
        self.assertEqual(len(hanzi), 6763)
        for size in (12, 14, 18):
            parsed = self.parsed[size]
            for cp in hanzi:
                gid = fonts.glyph_id(parsed, cp)
                self.assertGreater(gid, 0, (size, hex(cp)))
                index, _, width, height, _, _ = parsed["glyphs"][gid]
                self.assertGreater(width * height, 0, (size, hex(cp)))
                self.assertTrue(any(parsed["bitmap"][index:index + (width * height + 1) // 2]), (size, hex(cp)))

    def test_fixed_application_literals_are_covered_at_all_chinese_sizes(self):
        required = fonts.ui_codepoints()
        self.assertTrue(required)
        for size in (12, 14, 18):
            missing = [cp for cp in required if not fonts.glyph_id(self.parsed[size], cp)]
            self.assertEqual(missing, [], (size, [hex(cp) for cp in missing]))

    def test_ascii_and_missing_character_negative_cases(self):
        for size, parsed in self.parsed.items():
            self.assertTrue(all(fonts.glyph_id(parsed, cp) for cp in range(32, 127)), size)
            self.assertEqual(fonts.glyph_id(parsed, 0x1F600), 0)
            self.assertEqual(fonts.glyph_id(parsed, 0), 0)
            self.assertEqual(fonts.glyph_id(parsed, 0x10FFFF), 0)
        self.assertEqual(fonts.glyph_id(self.parsed[24], ord("中")), 0)
        self.assertGreater(fonts.glyph_id(self.parsed[14], ord("中")), 0)

    def test_line_heights_and_every_glyph_fit(self):
        for size, parsed in self.parsed.items():
            self.assertEqual(parsed["line_height"], fonts.SIZES[size])
            for _, _, width, height, _, offset_y in parsed["glyphs"][1:]:
                if not width * height:
                    continue
                y = parsed["line_height"] - parsed["base_line"] - height - offset_y
                self.assertGreaterEqual(y, 0, size)
                self.assertLessEqual(y + height, fonts.SIZES[size], size)

    def test_spaces_advance_without_missing_glyph_boxes(self):
        for size, parsed in self.parsed.items():
            for cp in ([32, 0x3000] if size < 24 else [32]):
                gid = fonts.glyph_id(parsed, cp)
                self.assertGreater(gid, 0)
                _, advance, width, height, _, _ = parsed["glyphs"][gid]
                self.assertGreater(advance, 0)
                self.assertEqual(width * height, 0)

    def test_cmap_format_and_sparse_holes(self):
        parsed = self.parsed[14]
        ascii_map, sparse = parsed["cmaps"]
        self.assertEqual(ascii_map["type"], "LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY")
        self.assertEqual(sparse["type"], "LV_FONT_FMT_TXT_CMAP_SPARSE_TINY")
        self.assertEqual(fonts.glyph_id(parsed, 32), 1)
        self.assertEqual(fonts.glyph_id(parsed, 126), 95)
        self.assertEqual(fonts.glyph_id(parsed, 127), 0)
        for cp in (sparse["start"] - 1, sparse["start"] + sparse["length"]):
            self.assertEqual(fonts.glyph_id(parsed, cp), 0)
        hole = next(i for i in range(sparse["length"]) if i not in set(sparse["offsets"]))
        self.assertEqual(fonts.glyph_id(parsed, sparse["start"] + hole), 0)

    def test_plain_4bpp_crosses_odd_width_rows_without_padding(self):
        # A 3x3 glyph: the second row begins in the low nibble of byte 1.
        pixels = [0, 1, 2, 3, 4, 5, 6, 7, 15]
        expected = bytes((0x01, 0x23, 0x45, 0x67, 0xF0))
        self.assertEqual(fonts.pack_4bpp(pixels), expected)
        self.assertEqual(fonts.unpack_4bpp(expected, 9), pixels)
        with self.assertRaises(ValueError):
            fonts.pack_4bpp([16])

    def test_detects_corrupted_bitmap_and_wrong_cmap(self):
        parsed = copy.deepcopy(self.parsed[14])
        bitmap = bytearray(parsed["bitmap"])
        bitmap[17] ^= 0xF0
        parsed["bitmap"] = bytes(bitmap)
        with self.assertRaisesRegex(ValueError, "bitmap checksum"):
            fonts.validate_font(parsed, self.metadata[14])
        parsed = copy.deepcopy(self.parsed[14])
        parsed["cmaps"][1]["glyph_start"] += 1
        with self.assertRaisesRegex(ValueError, "Bad cmap"):
            fonts.validate_font(parsed, self.metadata[14])

    def test_ui_scanner_ignores_comments_but_reads_escaped_text(self):
        source = '// "不应收录"\n/* "另一个注释" */\nconst char *x = "网络\\u5ba2\\U00006237\\n";'
        self.assertEqual(fonts.literal_codepoints(source), set(map(ord, "网络客户")))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "main").mkdir()
            (root / "main" / "unifi_view.c").write_text('const char *s = "稀有龘";')
            self.assertEqual(fonts.ui_codepoints(root), set(map(ord, "稀有龘")))
            self.assertEqual(fonts.glyph_id(self.parsed[14], ord("龘")), 0)

    def test_18px_large_bitmap_has_compile_time_guard(self):
        parsed = self.parsed[18]
        self.assertGreater(len(parsed["bitmap"]), 1 << 20)
        self.assertIn("#if !LV_FONT_FMT_TXT_LARGE", parsed["text"])
        self.assertIn("CONFIG_LV_FONT_FMT_TXT_LARGE=y", parsed["text"])


if __name__ == "__main__":
    unittest.main()
