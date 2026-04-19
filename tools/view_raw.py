#!/usr/bin/env python3
"""
view_raw.py — Convert Amiga .raw indexed pixel files to PNG for inspection.

Each .raw file has an 8-byte header (LE32 width, LE32 height) followed by
width×height indexed bytes.  If a palette is provided it is applied; otherwise
a unique colour is assigned to each index value so shapes are clearly visible.

Usage:
    python3 tools/view_raw.py [path ...]   # specific files or directories
    python3 tools/view_raw.py              # scans all of assets/

PNGs are written alongside each .raw file (same name, .png extension).
Requires: Pillow  (pip install pillow)
"""

import struct
import sys
import os
import random
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("Pillow is required. Install with:")
    print("  pip3 install pillow --break-system-packages")
    print("  or: brew install pillow")
    sys.exit(1)

# ---------------------------------------------------------------------------
# Amiga 12-bit colour (0xRGB) to (r, g, b) 8-bit
# ---------------------------------------------------------------------------
def amiga_color(v):
    r = ((v >> 8) & 0xF) * 17
    g = ((v >> 4) & 0xF) * 17
    b = (v & 0xF) * 17
    return (r, g, b)

# ---------------------------------------------------------------------------
# Known palettes extracted from ASM source files.
# Format: list of 12-bit Amiga colour values (up to 64 entries).
# A None entry means "auto" (generated from colour index).
# ---------------------------------------------------------------------------
_PAL_STORY_PLANET = [
    0x000, 0xFFF, 0x222, 0x222, 0x222, 0x222, 0x222, 0x222,
    0x322, 0x422, 0x522, 0x622, 0x722, 0x822, 0x922, 0xB32,
] + [0xFFF] * 16

_PAL_STORY_TITLE = [
    0x000, 0x990, 0x221, 0x332, 0x443, 0x554, 0x665, 0x776,
    0x887, 0x998, 0xAA9, 0xBBA, 0xCCB, 0xDDD, 0xEEE, 0xFFF,
    0x000, 0x111, 0x222, 0x333, 0x444, 0x555, 0x666, 0x777,
    0x888, 0x999, 0xAAA, 0xBBB, 0xCCC, 0xDDD, 0xEEE, 0xFFF,
]

_PAL_MENU = [
    0x000, 0x111, 0x100, 0x200, 0x400, 0x800, 0xD00, 0xF30,
    0x000, 0x222, 0xD31, 0xB11, 0x444, 0x333, 0x222, 0xF52,
    0x000, 0x111, 0x222, 0x333, 0x444, 0x500, 0x700, 0x900,
    0x000, 0x111, 0x222, 0x333, 0x444, 0x555, 0x666, 0x777,
]

_PAL_END = [
    0x000, 0xAAA, 0x222, 0x332, 0x333, 0x444, 0x543, 0x555,
    0x765, 0x666, 0x877, 0xA87, 0x999, 0x111, 0xDDD, 0xFFF,
] + [0xFFF] * 16

_PAL_GAMEOVER = [0x000, 0xA99, 0x766, 0x333] + [0x000] * 28

_PAL_INTEX = (
    [0x000] * 16 +
    [0x555, 0x565, 0x575, 0x585, 0x595, 0x5A5, 0x5B5, 0x5C5,
     0x5D5, 0x5E5, 0x5F5, 0x4F4, 0x3F3, 0x2F2, 0x1F1, 0x0F0]
)

_PAL_BRIEFING = [
    0x000, 0x111, 0x222, 0x333, 0x444, 0x555, 0x666, 0x777,
    0x888, 0x999, 0xAAA, 0xBBB, 0xCCC, 0xDDD, 0xEEE, 0xFFF,
    0x000, 0x111, 0x222, 0x333, 0x444, 0x555, 0x666, 0x777,
    0x888, 0x999, 0xAAA, 0xBBB, 0xCCC, 0xDDD, 0xEEE, 0xFFF,
]

# Map stem → palette (first match wins)
PALETTE_MAP = {
    "story_planet":      _PAL_STORY_PLANET,
    "planet_320x256":    _PAL_STORY_PLANET,
    "story_title":       _PAL_STORY_TITLE,
    "title_320x256":     _PAL_STORY_TITLE,
    "menu_title":        _PAL_MENU,
    "menu_copyright":    _PAL_MENU,
    "title_320x180":     _PAL_MENU,
    "copyright_320x16":  _PAL_MENU,
    "end_bkgnd":         _PAL_END,
    "end_scroll":        _PAL_END,
    "gameover":          _PAL_GAMEOVER,
    "intex_bkgnd":       _PAL_INTEX,
    "intex_weapons":     _PAL_INTEX,
    "intex_font":        _PAL_INTEX,
    "briefingcore":      _PAL_BRIEFING,
    "briefingstart":     _PAL_BRIEFING,
    "briefing_bkgnd":    _PAL_BRIEFING,
}

# ---------------------------------------------------------------------------
# Build a flat 768-byte PIL palette for a given palette list
# ---------------------------------------------------------------------------
def build_pil_palette(pal_list, num_entries=64):
    """Return a flat list of 768 bytes (256 × RGB) for PIL."""
    flat = []
    # Auto-generate distinct colours for any index not in the provided palette
    rng = random.Random(42)
    auto = [(rng.randint(30, 220), rng.randint(30, 220), rng.randint(30, 220))
            for _ in range(256)]

    for i in range(256):
        if i < len(pal_list):
            flat.extend(amiga_color(pal_list[i]))
        else:
            flat.extend(auto[i])
    return flat

def auto_palette(num_entries):
    """Distinct-colour palette for unknown files."""
    rng = random.Random(99)
    flat = []
    for i in range(256):
        # Make index 0 always black (transparent background)
        if i == 0:
            flat.extend((0, 0, 0))
        else:
            hue = (i * 137) % 360  # golden-angle spread
            import colorsys
            r, g, b = colorsys.hsv_to_rgb(hue / 360, 0.7, 0.85)
            flat.extend((int(r * 255), int(g * 255), int(b * 255)))
    return flat

# ---------------------------------------------------------------------------
# Convert one .raw file to PNG
# ---------------------------------------------------------------------------
def convert_raw(raw_path: Path, out_path: Path | None = None) -> Path:
    data = raw_path.read_bytes()
    if len(data) < 8:
        raise ValueError(f"{raw_path}: too small to be a valid .raw")

    width, height = struct.unpack_from("<II", data, 0)
    pixels = data[8:]
    expected = width * height
    if len(pixels) < expected:
        # Pad with zeros if truncated
        pixels = pixels + bytes(expected - len(pixels))
    else:
        pixels = pixels[:expected]

    # Choose palette
    stem = raw_path.stem.lower()
    chosen_pal = None
    for key, pal in PALETTE_MAP.items():
        if key.lower() in stem:
            chosen_pal = pal
            break

    if chosen_pal is not None:
        flat_pal = build_pil_palette(chosen_pal)
    else:
        flat_pal = auto_palette(64)

    img = Image.frombytes("P", (width, height), pixels)
    img.putpalette(flat_pal)

    if out_path is None:
        out_path = raw_path.with_suffix(".png")

    img.save(out_path)
    return out_path

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def collect_raw_files(paths):
    result = []
    for p in paths:
        p = Path(p)
        if p.is_dir():
            result.extend(sorted(p.rglob("*.raw")))
        elif p.suffix == ".raw":
            result.append(p)
    return result

def main():
    if len(sys.argv) > 1:
        targets = sys.argv[1:]
    else:
        # Default: scan all of assets/
        repo = Path(__file__).parent.parent
        targets = [repo / "assets"]

    raw_files = collect_raw_files(targets)
    if not raw_files:
        print("No .raw files found.")
        return

    ok = fail = 0
    for rf in raw_files:
        try:
            out = convert_raw(rf)
            print(f"  {rf.relative_to(rf.parent.parent.parent)}  →  {out.name}")
            ok += 1
        except Exception as e:
            print(f"  ERROR {rf}: {e}")
            fail += 1

    print(f"\n{ok} converted, {fail} failed.")

if __name__ == "__main__":
    main()
