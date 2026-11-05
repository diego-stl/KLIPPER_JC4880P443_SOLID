"""Pack approved Portal artwork and semantic LVGL 9 RGB565A8 icons.

Requires Pillow. No network, runtime image decoding, or heap allocation.
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
    for file, symbol, size in (("aperture-transparente.png", "portal_aperture_image", (118, 118)),
                               ("klipper-transparente.png", "portal_klipper_image", (100, 80))):
        source = REVIEW / "logos" / file
        image = Image.open(source).convert("RGBA")
        bounds = image.getchannel("A").point(lambda a: 255 if a >= 16 else 0).getbbox()
        if bounds is None:
            raise ValueError("Empty approved logo")
        fitted = image.crop(bounds)
        fitted.thumbnail(size, Image.Resampling.LANCZOS)
        canvas = Image.new("RGBA", size)
        canvas.paste(fitted, ((size[0]-fitted.width)//2, (size[1]-fitted.height)//2))
        images.append((str(source.relative_to(ROOT)), symbol, canvas))
    return images


def png_bytes(image):
    output = io.BytesIO()
    image.save(output, format="PNG")
    return output.getvalue()


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
        trimmed.thumbnail((size[0] - MARGIN * 2, size[1] - MARGIN * 2), Image.Resampling.LANCZOS)
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
        rows = [", ".join(f"0x{b:02X}" for b in packed[i:i + 24]) for i in range(0, len(packed), 24)]
        definitions += [f"static const uint8_t {symbol}_pixels[] __attribute__((aligned(4))) = {{\n    " + ",\n    ".join(rows) + "\n};",
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
