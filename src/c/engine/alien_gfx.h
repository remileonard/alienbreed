#ifndef AB_ALIEN_GFX_H
#define AB_ALIEN_GFX_H

/*
 * Alien Breed SE 92 - C port
 * Alien graphics atlas loader
 *
 * The Amiga game stores alien BOB (Blitter Object) graphics in level-specific
 * BO files (L0BO, L1BO, …, L5BO).  Each file is a raw 5-bitplane sequential
 * bitmap: plane 0 occupies the first 15360 bytes, plane 1 the next 15360, etc.
 * The full atlas is 320×384 pixels and holds 7 alien types × 3 walk frames:
 *
 *   Column (X): type_idx * ALIEN_SPRITE_W   (7 types, stride = 32 px)
 *   Row    (Y): anim_frame * ALIEN_SPRITE_W  (3 frames at y = 0, 32, 64)
 *   Frame size: ALIEN_SPRITE_W × ALIEN_SPRITE_H  (32 × 30 px)
 *
 * Transparency: color index 0 (all bitplanes = 0) is transparent, matching
 * the Amiga blitter minterm $CA used in the original BOB rendering code
 * (Ref: main.asm#L12365-L12411).
 *
 * The BO filename per level comes from the LevelDef.map_bo field:
 *   lev1_load_struct → L0BO, lev2_load_struct → L1BO, …
 *   lev7-lev9        → L2BO, lev10 → L1BO, lev11 → L2BO, lev12 → L5BO
 * (Ref: main.asm#L7972-L8018)
 */

#include "../types.h"

/* Atlas dimensions (fixed for all levels) */
#define ALIEN_ATLAS_W      320
#define ALIEN_ATLAS_H      384
#define ALIEN_ATLAS_PLANES   5

/* Single alien frame dimensions inside the atlas */
#define ALIEN_SPRITE_W      32   /* pixels wide  (0x20) */
#define ALIEN_SPRITE_H      30   /* pixels tall  (0x1E) */

/* Number of unique walk animation frames per alien type */
#define ALIEN_WALK_FRAMES    3

/*
 * Load the BO file at path, decode 5 sequential bitplanes to an indexed-color
 * atlas buffer (320×384 bytes, one byte = color index 0-31).
 * Any previously loaded atlas is freed first.
 * Returns 0 on success, -1 on error.
 */
int  alien_gfx_load(const char *path);

/* Free the decoded atlas buffer. */
void alien_gfx_free(void);

/*
 * Return a pointer to the decoded atlas, or NULL if not loaded.
 * The buffer is ALIEN_ATLAS_W * ALIEN_ATLAS_H bytes; row stride = ALIEN_ATLAS_W.
 */
const UBYTE *alien_gfx_get_atlas(void);

#endif /* AB_ALIEN_GFX_H */
