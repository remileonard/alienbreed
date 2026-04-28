#ifndef AB_ANIM_GFX_H
#define AB_ANIM_GFX_H

/*
 * Alien Breed SE 92 - C port
 * Animated background tile atlas loader
 *
 * The Amiga game stores animated background tile BOBs in level-specific
 * AN files (L0AN, L1AN, …, L5AN).  Each file is a raw 5-bitplane sequential
 * bitmap: plane 0 occupies the first 5760 bytes, plane 1 the next 5760, etc.
 * The full atlas is 320×144 pixels.
 *
 * The atlas contains up to 180 animated tile frames of 16×16 pixels each
 * (20 columns × 9 rows), matching the standard tileset tile dimensions.
 *
 * The filename is given by LevelDef.map_an and located under game/ at runtime.
 */

#include "../types.h"

/* Atlas dimensions (fixed for all levels) */
#define ANIM_ATLAS_W      320
#define ANIM_ATLAS_H      144
#define ANIM_ATLAS_PLANES   5

/* Total tiles encoded in the atlas */
#define ANIM_TILE_W        16
#define ANIM_TILE_H        16
#define ANIM_TILES_PER_ROW (ANIM_ATLAS_W / ANIM_TILE_W)   /* 20 */
#define ANIM_TILE_ROWS     (ANIM_ATLAS_H / ANIM_TILE_H)   /*  9 */
#define ANIM_TILE_COUNT    (ANIM_TILES_PER_ROW * ANIM_TILE_ROWS)  /* 180 */

/*
 * Load the AN file at path, decode 5 sequential bitplanes to an indexed-color
 * atlas buffer (320×144 bytes, one byte = color index 0-31).
 * Any previously loaded atlas is freed first.
 * Returns 0 on success, -1 on error.
 */
int  anim_gfx_load(const char *path);

/* Free the decoded atlas buffer. */
void anim_gfx_free(void);

/*
 * Return a pointer to the decoded atlas, or NULL if not loaded.
 * Buffer layout: ANIM_ATLAS_W * ANIM_ATLAS_H bytes, row stride = ANIM_ATLAS_W.
 */
const UBYTE *anim_gfx_get_atlas(void);

#endif /* AB_ANIM_GFX_H */
