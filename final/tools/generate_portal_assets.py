"""Pack Portal artwork, icons and Spanish glyphs for LVGL 9.

Requires Pillow and the LVGL sources installed by the firmware build.
Use --source-dir for alternative character originals, --check to verify outputs.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont, __version__ as pillow_version

ROOT = Path(__file__).resolve().parents[1]
LENS = (690, 712, 33)
MARGIN = 4
ICON_SIZE = 64
REVIEW = ROOT / "design/revision-0.6.1"
APPROVED = ROOT / "assets/portal/approved-icons"
SOURCES = (
    ("glados_suspended.png", "portal_head_image", (288, 248)),
    ("potatOS.png", "portal_potato_image", (248, 240)),
)
ICON_NAMES = ("print", "file", "move", "details", "settings", "macro", "stop",
              "pause", "play", "cancel", "extrude", "retract", "nozzle", "bed",
              "home", "refresh", "power", "back", "plus", "minus", "backspace",
              "up", "down", "left", "right", "wifi", "exclude", "last_job", "audio", "mute", "cool", "motors_off")
SYMBOL_ALIASES = {
    "FILE": "file", "DIRECTORY": "file", "SHUFFLE": "move", "SETTINGS": "settings",
    "EDIT": "macro", "STOP": "stop", "PAUSE": "pause", "PLAY": "play",
    "CLOSE": "cancel", "CHARGE": "nozzle", "IMAGE": "bed", "HOME": "home",
    "REFRESH": "refresh", "POWER": "power", "PLUS": "plus", "MINUS": "minus",
    "BACKSPACE": "backspace", "UP": "up", "DOWN": "down", "LEFT": "left",
    "RIGHT": "right", "WIFI": "wifi", "LIST": "exclude", "EYE_OPEN": "details",
}
CORE_STATES = ("base", "complete", "cancelled", "offline", "error", "warning")


def approved_manifest():
    manifest = json.loads((APPROVED / "manifest.json").read_text())
    digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    assert manifest["selection_sha256"] == digest(REVIEW / "selection.json")
    assert manifest["font_sha256"] == digest(REVIEW / "sources/uicons-solid-rounded.woff2")
    assert {i["name"] for i in manifest["icons"]} == set(ICON_NAMES)
    for item in manifest["icons"]:
        assert digest(APPROVED / item["file"]) == item["sha256"]
        if "file" in item["source"]:
            assert digest(REVIEW / "sources" / item["source"]["file"]) == item["source"]["sha256"]
    return manifest


def icon(name):
    image = Image.open(APPROVED / (name + ".png")).convert("RGBA")
    assert image.size == (ICON_SIZE, ICON_SIZE) and image.getchannel("A").getbbox()
    return image


def logos():
    # Display export only: preserve the approved pixels, geometry and alpha.
    images = []
    for source, symbol, size in (
        (REVIEW / "logos" / "aperture-transparente.png", "portal_aperture_image", (118, 118)),
        (ROOT / "IMAGENES" / "klipper.png", "portal_klipper_image", (100, 80)),
        (ROOT / "IMAGENES" / "aperture inicio.png", "portal_start_aperture_image", (120, 120)),
        (ROOT / "IMAGENES" / "WIFI.png", "portal_start_wifi_image", (76, 84)),
    ):
        image = Image.open(source).convert("RGBA")
        bounds = image.getchannel("A").point(lambda a: 255 if a >= 16 else 0).getbbox()
        if bounds is None:
            raise ValueError("Empty approved logo")
        fitted = image.crop(bounds)
        fitted.thumbnail(size, Image.Resampling.LANCZOS, reducing_gap=None)
        canvas = Image.new("RGBA", size)
        canvas.paste(fitted, ((size[0]-fitted.width)//2, (size[1]-fitted.height)//2))
        images.append((str(source.relative_to(ROOT)), symbol, canvas))
    return images


def png_bytes(image, **options):
    output = io.BytesIO()
    image.save(output, format="PNG", **options)
    return output.getvalue()


def c_bytes(data):
    return ",\n".join("    " + ", ".join(f"0x{b:02X}" for b in data[i:i + 24])
                     for i in range(0, len(data), 24))


SAVER_BG = (10, 17, 24)
SAVER_ART = ROOT / "assets/portal/cores/saver"
SAVER_FACES = {"blue": 55, "green": 65, "yellow": 56, "red": 61}


def dimmed(image):
    background = Image.new("RGBA", image.size, (*SAVER_BG, 0))
    result = Image.blend(background, image, 0.2)
    result.putalpha(image.getchannel("A"))
    return result


def saver_artwork():
    source = SAVER_ART / "shell.png"
    shell = dimmed(Image.open(source).convert("RGBA"))
    assert shell.size == (255, 303)
    shell = shell.quantize(colors=256, method=Image.Quantize.FASTOCTREE)
    yield source, "portal_saver_shell", shell, png_bytes(shell, optimize=True), "RAW_ALPHA", 0
    for face, lens_width in SAVER_FACES.items():
        source = SAVER_ART / f"{face}-iris.png"
        iris = Image.open(source).convert("RGBA").resize((136, 135), Image.Resampling.LANCZOS)
        background = Image.new("RGBA", iris.size, (*SAVER_BG, 255))
        iris = Image.alpha_composite(background, dimmed(iris))
        pixels = packed_pixels(iris)
        yield source, f"portal_saver_iris_{face}", iris, pixels[:136 * 135 * 2], "RGB565", 272
        source = SAVER_ART / f"{face}-lens.png"
        lens = Image.open(source).convert("RGBA")
        lens = lens.crop(lens.getchannel("A").getbbox())
        height = round(lens.height * lens_width / lens.width)
        lens = dimmed(lens.resize((lens_width, height), Image.Resampling.LANCZOS))
        yield source, f"portal_saver_lens_{face}", lens, packed_pixels(lens), "RGB565A8", lens_width * 2


def final_artwork():
    source = ROOT / "IMAGENES/fondo.png"
    background = Image.open(source).convert("RGBA").convert("L").resize((800, 480), Image.Resampling.LANCZOS)
    yield source, "portal_background_image", background, background.tobytes(), "L8", 800
    for state in CORE_STATES:
        source = ROOT / "assets/portal/cores" / f"{state}.png"
        # Preserve the full transparent canvas exported from the final PowerPoint.
        sprite = Image.open(source).convert("RGBA")
        assert sprite.size == ((209, 251) if state == "warning" else (255, 303))
        sprite = sprite.quantize(colors=256, method=Image.Quantize.FASTOCTREE)
        yield source, f"portal_core_{state}", sprite, png_bytes(sprite, optimize=True), "RAW_ALPHA", 0
    yield from saver_artwork()


def montserrat_glyphs(size, points, bpp):
    source = ROOT / "managed_components/lvgl__lvgl/scripts/built_in_font/Montserrat-Medium.ttf"
    font = ImageFont.truetype(str(source), size)
    bitmap = bytearray()
    metrics = ["    {0},"]
    for point in points:
        mask, (left, top) = font.getmask2(chr(point), mode="L", anchor="ls")
        width, height = mask.size
        advance = round(font.getlength(chr(point)) * 16)
        metrics.append(f"    {{.bitmap_index = {len(bitmap)}, .adv_w = {advance}, .box_w = {width}, "
                       f".box_h = {height}, .ofs_x = {left}, .ofs_y = {-top - height}}},")
        data = bytes(mask)
        if bpp == 8:
            bitmap.extend(data)
        else:
            data += b"\x00" if len(data) % 2 else b""
            bitmap.extend((a & 0xF0) | (b >> 4) for a, b in zip(data[::2], data[1::2]))
    return bitmap, metrics


def spanish_font_definitions():
    # LVGL's stock Montserrat covers ASCII, degree and its own symbols.
    # Add only the missing glyphs, using the same upstream font.
    points = sorted(ord(c) for c in "¡¿ÁÉÍÓÚÜÑáéíóúüñ…")
    first = points[0]
    offsets = ", ".join(str(point - first) for point in points)
    definitions = [f"static const uint16_t spanish_points[] = {{{offsets}}};",
                   "static const lv_font_fmt_txt_cmap_t spanish_cmap = {",
                   f"    .range_start = {first}, .range_length = {points[-1] - first + 1}, .glyph_id_start = 1,",
                   f"    .unicode_list = spanish_points, .list_length = {len(points)}, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY",
                   "};"]
    for size in (14, 16, 20):
        bitmap, metrics = montserrat_glyphs(size, points, 8)
        definitions.extend([
            f"static const uint8_t spanish_pixels_{size}[] = {{\n{c_bytes(bitmap)}\n}};",
            f"static const lv_font_fmt_txt_glyph_dsc_t spanish_glyphs_{size}[] = {{\n" + "\n".join(metrics) + "\n};",
            f"static const lv_font_fmt_txt_dsc_t spanish_data_{size} = {{",
            f"    .glyph_bitmap = spanish_pixels_{size}, .glyph_dsc = spanish_glyphs_{size}, .cmaps = &spanish_cmap,",
            "    .cmap_num = 1, .bpp = 8",
            "};",
            f"static const lv_font_t spanish_fallback_{size} = {{",
            "    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt, .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,",
            f"    .dsc = &spanish_data_{size}",
            "};",
        ])
    definitions.extend([
        "const lv_font_t *portal_font(const lv_font_t *font)",
        "{",
        "    static lv_font_t localized[3];",
        "    static const lv_font_t *const base[] = {&lv_font_montserrat_14, &lv_font_montserrat_16, &lv_font_montserrat_20};",
        "    static const lv_font_t *const fallback[] = {&spanish_fallback_14, &spanish_fallback_16, &spanish_fallback_20};",
        "    for (unsigned i = 0; i < 3; ++i) {",
        "        if (font != base[i]) continue;",
        "        if (!localized[i].fallback) {",
        "            localized[i] = *font;",
        "            localized[i].fallback = fallback[i];",
        "        }",
        "        return &localized[i];",
        "    }",
        "    return font;",
        "}",
        "",
    ])
    return definitions


def saver_font_definitions():
    points = sorted(map(ord, " %-0123456789"))
    bitmap, metrics = montserrat_glyphs(104, points, 4)
    offsets = ", ".join(str(point - points[0]) for point in points)
    return [
        f"static const uint8_t saver_font_pixels[] = {{\n{c_bytes(bitmap)}\n}};",
        "static const lv_font_fmt_txt_glyph_dsc_t saver_font_glyphs[] = {\n" + "\n".join(metrics) + "\n};",
        f"static const uint16_t saver_font_points[] = {{{offsets}}};",
        "static const lv_font_fmt_txt_cmap_t saver_font_cmap = {",
        f"    .range_start = 32, .range_length = 26, .glyph_id_start = 1, .list_length = {len(points)},",
        "    .unicode_list = saver_font_points, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY",
        "};",
        "static const lv_font_fmt_txt_dsc_t saver_font_data = {",
        "    .glyph_bitmap = saver_font_pixels, .glyph_dsc = saver_font_glyphs,",
        "    .cmaps = &saver_font_cmap, .cmap_num = 1, .bpp = 4",
        "};",
        "const lv_font_t portal_saver_progress_font = {",
        "    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt, .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,",
        "    .line_height = 117, .base_line = 22, .dsc = &saver_font_data",
        "};",
    ]


def packed_pixels(sprite):
    # LVGL 9 has complete RGB565 LE and A8 planes, not interleaved triplets.
    rgb, alpha = bytearray(), bytearray()
    rgba = sprite.tobytes()
    for index in range(0, len(rgba), 4):
        r, g, b, a = rgba[index:index + 4]
        color = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        rgb.extend((color & 255, color >> 8))
        alpha.append(a)
    return rgb + alpha


def contact_sheet(sprites):
    canvas = Image.new("RGB", (960, 830), "#0B121C")
    draw = ImageDraw.Draw(canvas)
    font = ImageFont.load_default(size=14)
    draw.text((20, 16), "PORTAL / APPROVED V2 / Uicons by Flaticon", font=font, fill="#E7F3FA")
    x = 24
    for name in ("portal_aperture_image", "portal_klipper_image"):
        image = sprites[name]
        canvas.paste(image, (x, 58), image)
        draw.text((x, 185), name.removeprefix("portal_").removesuffix("_image"), font=font, fill="#8BA4B8")
        x += 150
    # Show the character bitmaps unchanged, downscaled only for this QA sheet.
    for name in ("portal_head_image", "portal_potato_image"):
        image = sprites[name].copy()
        image.thumbnail((160, 126), Image.Resampling.LANCZOS)
        canvas.paste(image, (x, 52), image)
        x += 185
    draw.text((24, 222), "APPROVED ICONS / 64 px / neutral + tintable alpha", font=font, fill="#E7F3FA")
    for i, name in enumerate(ICON_NAMES):
        x, y = 24 + i % 8 * 117, 252 + i // 8 * 110
        image = sprites[f"portal_icon_{name}"]
        canvas.paste(image, (x + 16, y), image)
        draw.text((x, y + 74), name, font=font, fill="#8BA4B8")
    draw.text((24, 708), "SECONDARY SIZES / same source, LVGL scale / 24, 28, 32 px", font=font, fill="#E7F3FA")
    for i, name in enumerate(("nozzle", "bed", "wifi", "back", "plus", "minus", "backspace")):
        x = 24 + i * 120
        for j, size in enumerate((24, 28, 32)):
            image = sprites[f"portal_icon_{name}"].resize((size, size), Image.Resampling.LANCZOS)
            tint = Image.new("RGBA", image.size, "#48CFF3")
            tint.putalpha(image.getchannel("A"))
            canvas.paste(tint, (x + j * 35, 744), tint)
        draw.text((x, 788), name, font=font, fill="#8BA4B8")
    return canvas


def generate(source_dir: Path) -> dict[Path, bytes]:
    approved = approved_manifest()
    selected = {item["name"]: item for item in approved["icons"]}
    outputs, sprites, declarations = {}, {}, []
    metadata = {"pillow_version": pillow_version, "alpha_crop_threshold": 16,
                "margin_per_side": MARGIN, "approved_selection_sha256": approved["selection_sha256"],
                "approval": approved["approval"], "credit": "Uicons by Flaticon - www.flaticon.com/uicons",
                "format": "LVGL9 RGB565A8 planar, little endian",
                "source_project": "KLIPPER_ES3C28P_SOLID/assets/glados", "images": []}
    definitions = ['/* Generated by tools/generate_portal_assets.py; do not edit. */',
                   '#include "portal_assets.h"', '#include <string.h>', '']
    images = []
    for filename, symbol, size in SOURCES:
        original = Image.open(source_dir / filename).convert("RGBA")
        bounds = original.getchannel("A").point(lambda a: 255 if a >= 16 else 0).getbbox()
        if bounds is None:
            raise ValueError(f"No visible pixels: {filename}")
        trimmed = original.crop(bounds)
        trimmed.thumbnail((size[0] - MARGIN * 2, size[1] - MARGIN * 2), Image.Resampling.LANCZOS, reducing_gap=None)
        offset = ((size[0] - trimmed.width) // 2, (size[1] - trimmed.height) // 2)
        sprite = Image.new("RGBA", size)
        sprite.paste(trimmed, offset)
        item = {"source": filename, "source_sha256": hashlib.sha256((source_dir / filename).read_bytes()).hexdigest(),
                "source_size": list(original.size), "crop": list(bounds), "scaled_size": list(trimmed.size), "offset": list(offset)}
        if symbol == "portal_head_image":
            sx, sy = trimmed.width / (bounds[2] - bounds[0]), trimmed.height / (bounds[3] - bounds[1])
            lens = (round(offset[0] + (LENS[0] - bounds[0]) * sx),
                    round(offset[1] + (LENS[1] - bounds[1]) * sy), round(LENS[2] * sx))
            assert lens == (152, 145, 7), "The preserved GLaDOS eye contract changed"
            declarations += [f"#define PORTAL_EYE_{name} {value}" for name, value in zip(("X", "Y", "RADIUS"), lens)]
            item.update(lens_source=list(LENS), lens_canvas=list(lens))
        images.append((symbol, sprite, item))
    for source, symbol, sprite in logos():
        images.append((symbol, sprite, {"source": source, "source_sha256": hashlib.sha256((ROOT / source).read_bytes()).hexdigest(), "recolorable": False}))
    for name in ICON_NAMES:
        images.append((f"portal_icon_{name}", icon(name), {"source": str((APPROVED / selected[name]["file"]).relative_to(ROOT)),
                         "source_sha256": selected[name]["sha256"], "approved_id": selected[name]["approved_id"],
                         "provenance": selected[name]["source"], "recolorable": True}))
    for symbol, sprite, item in images:
        size = sprite.size
        packed = packed_pixels(sprite)
        stride = size[0] * 2
        assert stride % 4 == 0
        assert len(packed) == stride * size[1] + stride // 2 * size[1]
        definitions += [f"static const uint8_t {symbol}_pixels[] __attribute__((aligned(4))) = {{\n{c_bytes(packed)}\n}};",
                        f"const lv_image_dsc_t {symbol} = {{\n"
                        f"    .header = {{.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565A8,\n"
                        f"               .flags = 0, .w = {size[0]}, .h = {size[1]}, .stride = {stride}}},\n"
                        f"    .data_size = sizeof({symbol}_pixels),\n"
                        f"    .data = {symbol}_pixels,\n    .reserved = NULL,\n    .reserved_2 = NULL\n}};\n"]
        declarations.append(f"extern const lv_image_dsc_t {symbol};")
        item.update(canvas=list(size), stride=stride, data_size=len(packed), packed_sha256=hashlib.sha256(packed).hexdigest())
        metadata["images"].append(item)
        sprites[symbol] = sprite
        outputs[ROOT / "assets/portal" / (symbol + ".png")] = png_bytes(sprite)
    for source, symbol, sprite, packed, color_format, stride in final_artwork():
        width, height = sprite.size
        definitions += [
            f"static const uint8_t {symbol}_pixels[] __attribute__((aligned(4))) = {{\n{c_bytes(packed)}\n}};",
            f"const lv_image_dsc_t {symbol} = {{",
            f"    .header = {{.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_{color_format},",
            f"               .w = {width}, .h = {height}, .stride = {stride}}},",
            f"    .data_size = sizeof({symbol}_pixels), .data = {symbol}_pixels",
            "};\n",
        ]
        declarations.append(f"extern const lv_image_dsc_t {symbol};")
        metadata["images"].append({"source": source.relative_to(ROOT).as_posix(),
            "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(), "canvas": [width, height],
            "format": color_format, "stride": stride, "data_size": len(packed),
            "packed_sha256": hashlib.sha256(packed).hexdigest(), "recolorable": False})
    definitions.extend(spanish_font_definitions())
    declarations.append("const lv_font_t *portal_font(const lv_font_t *font);")
    definitions.extend(saver_font_definitions())
    declarations.append("extern const lv_font_t portal_saver_progress_font;")
    definitions += ["const lv_image_dsc_t *portal_icon_for_symbol(const char *symbol)", "{", "    if (!symbol) return NULL;"]
    for name in ICON_NAMES:
        definitions.append(f'    if (strcmp(symbol, "{name}") == 0) return &portal_icon_{name};')
    for symbol, name in SYMBOL_ALIASES.items():
        definitions.append(f'    if (strcmp(symbol, LV_SYMBOL_{symbol}) == 0) return &portal_icon_{name};')
    definitions += ["    return NULL;", "}", ""]
    declarations += ["/* Approved monochrome sources: 64 px main; scale 128 gives 32 px controls. */",
                     "#define PORTAL_ICON_SIZE 64",
                     "/* Semantic names or supported LV_SYMBOL_*; NULL for unknown/NULL input. */",
                     "const lv_image_dsc_t *portal_icon_for_symbol(const char *symbol);"]
    header = ('/* Generated by tools/generate_portal_assets.py; do not edit. */\n'
              '#pragma once\n#include <lvgl.h>\n#ifdef __cplusplus\nextern "C" {\n#endif\n' +
              "\n".join(declarations) + '\n#ifdef __cplusplus\n}\n#endif\n')
    metadata["total_pixel_bytes"] = sum(item["data_size"] for item in metadata["images"])
    metadata["icon_pixel_bytes"] = len(ICON_NAMES) * ICON_SIZE * ICON_SIZE * 3
    metadata["symbol_aliases"] = SYMBOL_ALIASES
    outputs[ROOT / "components/portal_assets/include/portal_assets.h"] = header.encode()
    outputs[ROOT / "components/portal_assets/portal_assets.c"] = "\n".join(definitions).encode()
    outputs[ROOT / "assets/portal/manifest.json"] = (json.dumps(metadata, indent=2) + "\n").encode()
    outputs[ROOT / "assets/portal/contact-sheet.png"] = png_bytes(contact_sheet(sprites))
    return outputs


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, default=ROOT / "assets/portal/originals")
    parser.add_argument("--check", action="store_true", help="Compare all outputs without writing")
    args = parser.parse_args()
    for path, data in generate(args.source_dir).items():
        if args.check:
            if not path.exists() or path.read_bytes() != data:
                raise SystemExit(f"Outdated output: {path}")
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        print(f"{'Verified' if args.check else 'Generated'} {path.relative_to(ROOT)} ({len(data)} bytes)")


if __name__ == "__main__":
    main()
