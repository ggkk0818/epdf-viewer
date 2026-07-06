#!/usr/bin/env python3
"""
SVG to 1bpp .bin converter for EPDF viewer.

Output formats:
- Icons:    [width u16 LE][height u16 LE][1bpp MSB-first row-major bitmap]
- PDF page: [magic u8=0xE5][version u8=0x01][width u16 LE][height u16 LE]
            [reserved u16 LE=0][1bpp MSB-first row-major bitmap]

Bit semantics: bit=1 means black pixel (drawn with GxEPD_BLACK), bit=0 means white.

The on-device loader reads the PDF page header, then renders only the viewport
(300x380, top-left of the page). Pages larger than the viewport are top-left
cropped; pages smaller are centered with white padding.

Usage:
    python tools/svg_to_bin.py icons        # batch-convert all icons
    python tools/svg_to_bin.py pdf          # generate test PDF page
    python tools/svg_to_bin.py all          # both
"""

import io
import struct
import sys
from pathlib import Path

import cairosvg
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
DOWNLOADS = Path(r"C:/Users/ggkk2/Downloads")
TF = ROOT / "tf"

# (svg_filename, output_name, pixel_size)
ICON_SPECS = [
    ("doc.svg",           "doc",      48),
    ("bluetoothon.svg",   "ble",      48),
    ("settings.svg",      "settings", 48),
    ("bluetooth_on.svg",  "ble_on",   16),
    ("bluetooth_off.svg", "ble_off",  16),
    ("battery (1).svg",   "battery",  16),
]

# Must match cfg::display::CONTENT_W / CONTENT_H in src/config/Config.h.
# Pages of any size load fine on-device (large=crop TL, small=center), but the
# demo defaults to the viewport size so the test page renders full-bleed.
PDF_W, PDF_H = 300, 380

PDF_MAGIC = 0xE5
PDF_VERSION = 0x01


def render_svg_to_rgba(svg_path: Path, size: int) -> Image.Image:
    """Render SVG at 4x then downsample for antialiasing, return RGBA."""
    scale = 4
    png = cairosvg.svg2png(
        url=str(svg_path),
        output_width=size * scale,
        output_height=size * scale,
    )
    img = Image.open(io.BytesIO(png)).convert("RGBA")
    return img.resize((size, size), Image.LANCZOS)


def alpha_to_1bpp(img: Image.Image) -> tuple[int, int, bytes]:
    """Pack RGBA image to 1bpp MSB-first. bit=1 where alpha>127."""
    w, h = img.size
    px = img.load()
    row_bytes = (w + 7) // 8
    out = bytearray(row_bytes * h)
    for y in range(h):
        row_base = y * row_bytes
        for xb in range(row_bytes):
            byte = 0
            for bit in range(8):
                x = xb * 8 + bit
                if x >= w:
                    break
                _, _, _, a = px[x, y]
                if a > 127:
                    byte |= (0x80 >> bit)
            out[row_base + xb] = byte
    return w, h, bytes(out)


def write_icon(svg_path: Path, out_path: Path, size: int) -> None:
    img = render_svg_to_rgba(svg_path, size)
    w, h, bitmap = alpha_to_1bpp(img)
    with open(out_path, "wb") as f:
        f.write(struct.pack("<HH", w, h))
        f.write(bitmap)
    print(f"  {out_path.name:20s} {w}x{h}  {len(bitmap)} bytes")


def write_pdf_page(out_path: Path) -> None:
    img = Image.new("RGBA", (PDF_W, PDF_H), (255, 255, 255, 0))
    draw = ImageDraw.Draw(img)

    draw.rectangle([8, 8, PDF_W - 8, PDF_H - 8], outline=(0, 0, 0, 255), width=3)

    draw.rectangle([40, 40, PDF_W - 40, 90], fill=(0, 0, 0, 255))

    for i in range(7):
        y = 120 + i * 22
        x_end = PDF_W - 40 - (i % 3) * 30
        draw.rectangle([40, y, x_end, y + 10], fill=(0, 0, 0, 255))

    cx = PDF_W // 2
    draw.rectangle([cx - 60, PDF_H - 70, cx + 60, PDF_H - 50], fill=(0, 0, 0, 255))

    for i in range(6):
        x = 60 + i * 60
        draw.rectangle([x, PDF_H - 40, x + 30, PDF_H - 30], fill=(0, 0, 0, 255))

    px = img.load()
    row_bytes = (PDF_W + 7) // 8
    out = bytearray(row_bytes * PDF_H)
    for y in range(PDF_H):
        row_base = y * row_bytes
        for xb in range(row_bytes):
            byte = 0
            for bit in range(8):
                x = xb * 8 + bit
                if x >= PDF_W:
                    break
                r, g, b, a = px[x, y]
                if a > 127 and (r + g + b) < 384:
                    byte |= (0x80 >> bit)
            out[row_base + xb] = byte

    header = struct.pack("<BBHHH", PDF_MAGIC, PDF_VERSION, PDF_W, PDF_H, 0)
    with open(out_path, "wb") as f:
        f.write(header)
        f.write(bytes(out))
    print(f"  {out_path.name:20s} {PDF_W}x{PDF_H}  {len(header) + len(out)} bytes (8B header + {len(out)} bitmap)")


def gen_icons() -> None:
    out_dir = TF / "sys" / "icon"
    out_dir.mkdir(parents=True, exist_ok=True)
    print(f"Icons -> {out_dir}")
    for svg_name, out_name, size in ICON_SPECS:
        svg_path = DOWNLOADS / svg_name
        if not svg_path.exists():
            print(f"  SKIP {svg_name}: not found")
            continue
        write_icon(svg_path, out_dir / f"{out_name}.bin", size)


def gen_pdf() -> None:
    out_dir = TF / "pdf" / "example"
    out_dir.mkdir(parents=True, exist_ok=True)
    print(f"PDF page -> {out_dir}")
    write_pdf_page(out_dir / "001.bin")


def main() -> int:
    if len(sys.argv) < 2 or sys.argv[1] not in {"icons", "pdf", "all"}:
        print(__doc__)
        return 1
    target = sys.argv[1]
    if target in {"icons", "all"}:
        gen_icons()
    if target in {"pdf", "all"}:
        gen_pdf()
    print("Done.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
