"""Build the microSD clip variants with Pillow and lz4 installed.

Usage: python pack_core_benchmark.py PNG_ROOT SD_ROOT
"""

import argparse
from array import array
from pathlib import Path
import struct
import sys

from PIL import Image
import lz4.block


SIZE = 300
FRAMES = 72
FPS = 12
SECTOR = 512
MAGIC = b"PCORE1\r\n"
FORMATS = {"png": 1, "rgb565a8": 2, "lz4": 3}


def rgb565a8(image):
    red, green, blue, alpha = image.convert("RGBA").split()
    color = array("H", ((r >> 3) << 11 | (g >> 2) << 5 | b >> 3
                        for r, g, b in zip(red.tobytes(), green.tobytes(), blue.tobytes())))
    if sys.byteorder == "big":
        color.byteswap()
    return color.tobytes() + alpha.tobytes()


def write_package(path, codec, frames):
    header = struct.pack("<8sHHHBB", MAGIC, SIZE, SIZE, FRAMES, FPS, codec)
    lengths = struct.pack(f"<{FRAMES}I", *(len(frame) for frame in frames))
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(".tmp")
    with temporary.open("wb") as package:
        package.write((header + lengths).ljust(SECTOR, b"\0"))
        for frame in frames:
            package.write(frame)
            package.write(bytes(-len(frame) % SECTOR))
    temporary.replace(path)
    print(f"{path.relative_to(path.parents[2])}: {path.stat().st_size:,} bytes")


def pack_clip(source, destination, kind):
    folder = source / kind
    expected = [f"{kind}_{index:04d}.png" for index in range(1, FRAMES + 1)]
    if sorted(path.name for path in folder.glob("*.png")) != expected:
        raise ValueError(f"Se esperaban {FRAMES} PNG numerados en {folder}")

    variants = {name: [] for name in FORMATS}
    for name in expected:
        path = folder / name
        original = path.read_bytes()
        with Image.open(path) as image:
            if image.format != "PNG" or image.size != (SIZE, SIZE) or len(original) > 1024 * 1024:
                raise ValueError(f"PNG inválido o demasiado grande: {path}")
            pixels = rgb565a8(image)
        variants["png"].append(original)
        variants["rgb565a8"].append(pixels)
        variants["lz4"].append(lz4.block.compress(pixels, mode="high_compression", compression=9, store_size=False))

    for name, codec in FORMATS.items():
        write_package(destination / "animaciones" / "prueba_core" / name / f"{kind}.pcore",
                      codec, variants[name])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="Carpeta que contiene pensativo/ e imprimiendo/")
    parser.add_argument("destination", type=Path, help="Raíz de la estructura para la microSD")
    args = parser.parse_args()
    for kind in ("pensativo", "imprimiendo"):
        pack_clip(args.source, args.destination, kind)


if __name__ == "__main__":
    main()
