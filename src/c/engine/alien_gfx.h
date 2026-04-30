#ifndef AB_ALIEN_GFX_H
#define AB_ALIEN_GFX_H

/*
 * Alien Breed SE 92 - C port
 * Alien graphics atlas loader
 *
 * The Amiga game stores alien BOB (Blitter Object) graphics in level-specific
 * BO files (L0BO, L1BO, …, L5BO).  Each file is a raw 5-bitplane sequential
 * bitmap: plane 0 occupies the first 15360 bytes, plane 1 the next 15360, etc.
 * The full atlas is 320×384 pixels.
 *
 * Walk sprite layout (Ref: lbW01945E / lbW019A8E @ main.asm#L14034,L14160):
 *   Column (X): direction * ALIEN_SPRITE_W
 *     8 compass directions: N(0) NE(1) E(2) SE(3) S(4) SW(5) W(6) NW(7)
 *   Row (Y):   frame * ALIEN_WALK_FRAME_STRIDE  (same for ALL atlas types)
 *   Frame size: ALIEN_SPRITE_W × ALIEN_SPRITE_H  (32 × 30 px)
 *
 * Walk frame layout — IDENTICAL for COMPACT and LEGACY atlas types:
 *   frame 0 → y =  0  (entries 0-7 of COMPACT; entries 100-107 of LEGACY)
 *   frame 1 → y = 32  (stride = ALIEN_WALK_FRAME_STRIDE = 32)
 *   frame 2 → y = 64
 *
 * (Note: lbW01945E LEGACY entries 8-23 at y=96/128 are *secondary* idle BOBs
 *  used by a different layer, NOT by the main walk cycle lbL01B036.)
 *
 * COMPACT (lbW019A8E, lbW018D4A, lbW01A1A2, lbW01A922): most levels.
 * LEGACY  (lbW01945E): L0BO / level 1, levels 10-11.
 *   Both share the same walk-frame Y layout; the atlas_type field is retained
 *   for potential future use (e.g., secondary BOB layers).
 *
 * Death/explosion sprites (Ref: lbW0188CE @ main.asm#L13833, lbL018C2E#L13907):
 *   16 frames of 32×30 px in two rows of the atlas.
 *   lbL018C2E uses BOB descriptors at indices 40-55 of lbL01790A, which receive
 *   coordinates from lbW0188CE entries 40-55:
 *     Row 1 (y=0xC0=192): 10 frames, x = frame_idx * 32  (frames 0-9)
 *     Row 2 (y=0xE0=224): 6 frames, x = (frame_idx-10) * 32  (frames 10-15)
 *   Frame i: atlas_x = (i < ALIEN_DEATH_ROW1_COUNT) ? i*32 : (i-ALIEN_DEATH_ROW1_COUNT)*32
 *            atlas_y = (i < ALIEN_DEATH_ROW1_COUNT) ? ALIEN_DEATH_ROW1_Y : ALIEN_DEATH_ROW2_Y
 *
 * Transparency: color index 0 (all bitplanes = 0) is transparent, matching
 * the Amiga blitter minterm $CA used in the original BOB rendering code
 * (Ref: main.asm#L12365-L12411).
 *
 * The BO filename per level comes from the LevelDef.map_bo field:
 *   lev1_load_struct → L0BO (LEGACY), lev2 → L1BO (COMPACT), …
 *   lev7-lev9 → L2BO, lev10 → L1BO (LEGACY), lev11 → L2BO (LEGACY), lev12 → L5BO
 * (Ref: main.asm#L7972-L8018 and atlas-descriptor assignments#L1030-L1227)
 */

#include "../types.h"

/* Atlas dimensions (fixed for all levels) */
#define ALIEN_ATLAS_W      320
#define ALIEN_ATLAS_H      384
#define ALIEN_ATLAS_PLANES   5

/* Single alien walk-frame dimensions inside the atlas */
#define ALIEN_SPRITE_W      32   /* pixels wide  (0x20) */
#define ALIEN_SPRITE_H      30   /* pixels tall  (0x1E) */

/* Number of compass walk directions */
#define ALIEN_DIR_COUNT      8   /* N NE E SE S SW W NW → atlas columns 0-7 */

/* Vertical stride between walk frames inside the atlas.
 * Each 30-px tall sprite occupies a 32-px row slot (2 px of vertical padding).
 * Ref: lbW019A8E entries (0,0),(0,32),(0,64) @ main.asm#L14160. */
#define ALIEN_WALK_FRAME_STRIDE 32

/* Number of unique walk animation frames (cycle: 0→1→2→1→loop) */
#define ALIEN_WALK_FRAMES    3

/*
 * Atlas layout types.
 * COMPACT: walk frame Y = frame_idx * ALIEN_WALK_FRAME_STRIDE  (most BO files)
 * LEGACY : walk frame Y = {0, 96, 128}               (L0BO and reuses as LEGACY)
 */
#define ALIEN_ATLAS_COMPACT  0
#define ALIEN_ATLAS_LEGACY   1

/*
 * ALT WALK sprites: used for the 1-frame "hit flash" when an alien takes
 * damage.  Both COMPACT (lbW019A8E entries 24-39) and LEGACY (lbW01945E
 * entries 8-23) store the bright hit-variant sprites at y=96 and y=128,
 * with x = direction * ALIEN_SPRITE_W (same column layout as normal walk).
 * Ref: lbC009B80 @ main.asm#L6675 (add.l #256,a6 → ALT WALK anim pointer).
 */
#define ALIEN_ALT_WALK_Y       96   /* atlas y of first ALT WALK row */

/* Death/explosion sprite dimensions and atlas position */
#define ALIEN_DEATH_FRAMES      16  /* 16 explosion frames total */
#define ALIEN_DEATH_W           32  /* pixels wide  (0x20 = ALIEN_SPRITE_W) */
#define ALIEN_DEATH_H           30  /* pixels tall  (0x1E = ALIEN_SPRITE_H) */
#define ALIEN_DEATH_ROW1_Y     192  /* first row y  = 0xC0; 10 frames */
#define ALIEN_DEATH_ROW1_COUNT  10  /* frames 0-9 in first row */
#define ALIEN_DEATH_ROW2_Y     224  /* second row y = 0xE0; 6 frames */

/*
 * Load the BO file at path, decode 5 sequential bitplanes to an indexed-color
 * atlas buffer (320×384 bytes, one byte = color index 0-31).
 * atlas_type: ALIEN_ATLAS_COMPACT or ALIEN_ATLAS_LEGACY.
 * Any previously loaded atlas is freed first.
 * Returns 0 on success, -1 on error.
 */
int  alien_gfx_load(const char *path, int atlas_type);

/* Free the decoded atlas buffer. */
void alien_gfx_free(void);

/*
 * Return a pointer to the decoded atlas, or NULL if not loaded.
 * The buffer is ALIEN_ATLAS_W * ALIEN_ATLAS_H bytes; row stride = ALIEN_ATLAS_W.
 */
const UBYTE *alien_gfx_get_atlas(void);

/* Return the atlas type (ALIEN_ATLAS_COMPACT or ALIEN_ATLAS_LEGACY). */
int alien_gfx_get_atlas_type(void);

#endif /* AB_ALIEN_GFX_H */
