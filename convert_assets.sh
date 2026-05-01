#!/bin/sh
set -e
cd "$(dirname "$0")"

BUILD="${BUILD:-./build}"

mkdir -p assets/sprites assets/samples assets/voices assets/music assets/gfx assets/fonts assets/maps assets/tiles

echo "=== Converting level maps ==="
# L?MA: T7MP format map files — parsed to extract tile data, palettes, IFFP path.
for f in game/L0MA game/L1MA game/L2MA game/L3MA game/L4MA game/L5MA \
          game/L6MA game/L7MA game/L8MA game/L9MA game/LAMA game/LBMA; do
    [ -f "$f" ] || continue
    name=$(basename "$f")
    printf "  %s -> " "$f"
    $BUILD/parse_levels "$f" "assets/maps/${name}.map" && echo "OK" || echo "FAIL"
done
# L?BO: boss/object sprite blocks — raw binary loaded directly by the game.
# L?AN: background animation tile blocks — raw binary loaded directly by the game.
# Just copy them as-is.
for f in game/L0BO game/L1BO game/L2BO game/L3BO game/L4BO game/L5BO \
          game/L0AN game/L1AN game/L2AN game/L3AN game/L4AN game/L5AN; do
    [ -f "$f" ] || continue
    name=$(basename "$f")
    cp "$f" "assets/maps/${name}.bin" && echo "  $name -> OK" || echo "  $name -> FAIL"
done

echo "=== Converting tilesets ==="
for f in game/LABM game/LBBM game/LCBM game/LDBM game/LEBM game/LFBM; do
    [ -f "$f" ] || continue
    name=$(basename "$f")
    printf "  %s -> " "$f"
    $BUILD/convert_tileset "$f" "assets/tiles/${name}.raw" && echo "OK" || echo "FAIL"
done

echo "=== Converting fonts ==="
# Fonts are stored with all 42 glyphs side-by-side: actual width = 672px.
# The filename encodes nominal_height = 42 * glyph_height; actual height = nominal/42.
# The intex font is converted first under its own name so it is never overwritten
# by fonts from other modules that share the same base filename.
INTEX_FONT=src/intex/gfx/font_16x504.lo6
if [ -f "$INTEX_FONT" ]; then
    printf "  %s (6bp, actual 672x12) -> " "$INTEX_FONT"
    $BUILD/convert_bitplanes "$INTEX_FONT" 6 assets/fonts/intex_font_16x504.raw 672 12 && echo "OK" || echo "FAIL"
fi
for f in $(find src \( -name "font_*.lo2" -o -name "font_*.lo3" -o -name "font_*.lo4" \
               -o -name "font_*.lo5" -o -name "font_*.lo6" \) \
           | grep -v "src/intex/"); do
    name=$(basename "$f")
    ext="${f##*.lo}"
    bp=$(echo "$ext" | cut -c1)
    nom_h=$(echo "$name" | sed 's/.*x\([0-9]*\)\..*/\1/')
    glyph_h=$(( nom_h / 42 ))
    out="assets/fonts/${name%.*}.raw"
    printf "  %s (%sbp, actual 672x%d) -> " "$f" "$bp" "$glyph_h"
    $BUILD/convert_bitplanes "$f" "$bp" "$out" 672 "$glyph_h" && echo "OK" || echo "FAIL"
done

echo "=== Converting .loN graphics ==="
for f in $(find src -name "*.lo1" -o -name "*.lo2" -o -name "*.lo3" \
               -o -name "*.lo4" -o -name "*.lo5" -o -name "*.lo6" \
               | grep -v "/font_" \
               | grep -v "weapons_264x40"); do
    name=$(basename "$f")
    # extract bp count from extension: .lo4 -> 4
    ext="${f##*.lo}"
    bp=$(echo "$ext" | cut -c1)
    # strip path prefix for output name: use subdir as prefix
    subdir=$(echo "$f" | sed 's|src/||' | sed 's|/gfx/.*||' | sed 's|/||g')
    out="assets/gfx/${subdir}_${name%.*}.raw"
    printf "  %s (%sbp) -> " "$f" "$bp"
    $BUILD/convert_bitplanes "$f" "$bp" "$out" && echo "OK" || echo "FAIL"
done

# Weapons sprite sheet: the filename says 264x40 but the actual bitmap is
# 320 pixels wide × 264 rows, 4 bitplanes (= 6 weapon images in a 2×3 grid,
# each image 160×88 px).  Pass the real dimensions explicitly.
printf "  src/intex/gfx/weapons_264x40.lo4 (4bp, actual 320x264) -> "
$BUILD/convert_bitplanes src/intex/gfx/weapons_264x40.lo4 4 \
    assets/gfx/intex_weapons_320x264.raw 320 264 && echo "OK" || echo "FAIL"

printf "  game/mapbkgnd_320x256.lo4 -> "
$BUILD/convert_bitplanes game/mapbkgnd_320x256.lo4 4 assets/tiles/mapbkgnd.raw && echo "OK" || echo "FAIL"

echo "=== Converting briefing sprites ==="
# Each sprite file contains one Amiga hardware attached sprite pair
# (even + odd strip, 96 lines each) → 16px wide, 16-color indexed output.
# sprite1-7.raw: one frame each (784 bytes).
# sprites.raw: first 784 bytes used as frame 2 for sprite position 4 (1P mode).
for f in src/briefingcore/gfx/sprite1.raw src/briefingcore/gfx/sprite2.raw \
          src/briefingcore/gfx/sprite3.raw src/briefingcore/gfx/sprite4.raw \
          src/briefingcore/gfx/sprite5.raw src/briefingcore/gfx/sprite6.raw \
          src/briefingcore/gfx/sprite7.raw; do
    [ -f "$f" ] || continue
    name=$(basename "$f" .raw)
    printf "  %s -> " "$f"
    $BUILD/convert_sprites "$f" 96 1 "assets/gfx/briefing_${name}.raw" 1 && echo "OK" || echo "FAIL"
done
# sprites.raw: convert first attached pair (first 784 bytes = 1P frame for position 4)
f=src/briefingcore/gfx/sprites.raw
if [ -f "$f" ]; then
    printf "  %s (first frame) -> " "$f"
    $BUILD/convert_sprites "$f" 96 1 "assets/gfx/briefing_sprites.raw" 1 && echo "OK" || echo "FAIL"
fi


# Player sprites: paired hardware sprites (SPR_A + SPR_B = 32px wide), 32 lines.
for f in src/main/sprites/player_sprite*.raw; do
    [ -f "$f" ] || continue
    name=$(basename "$f" .raw)
    printf "  %s -> " "$name"
    $BUILD/convert_sprites "$f" 32 2 "assets/sprites/${name}.raw" && echo "OK" || echo "FAIL"
done
# Timer digit sprites: single strip, 32 lines.
for f in src/main/sprites/timer_digit*.raw; do
    [ -f "$f" ] || continue
    name=$(basename "$f" .raw)
    printf "  %s -> " "$name"
    $BUILD/convert_sprites "$f" 32 1 "assets/sprites/${name}.raw" && echo "OK" || echo "FAIL"
done

echo "=== Converting audio samples ==="
for f in src/main/samples/*.raw; do
    name=$(basename "$f" .raw)
    printf "  %s -> " "$name"
    $BUILD/convert_audio "$f" "assets/samples/${name}.wav" 8363 && echo "OK" || echo "FAIL"
done

echo "=== Converting voices ==="
for f in src/main/voices/*.raw; do
    name=$(basename "$f" .raw)
    printf "  %s -> " "$name"
    $BUILD/convert_audio "$f" "assets/voices/${name}.wav" 8363 && echo "OK" || echo "FAIL"
done

echo "=== Copying Soundmon music ==="
cp game/boss.soundmon assets/music/boss.soundmon
cp game/level.soundmon assets/music/level.soundmon
cp game/title.soundmon assets/music/title.soundmon
echo "  Copied 3 music files"

echo ""
echo "=== DONE ==="
echo "Assets:"
for d in assets/*/; do
    count=$(ls "$d" 2>/dev/null | wc -l | tr -d ' ')
    echo "  $d  ($count files)"
done
