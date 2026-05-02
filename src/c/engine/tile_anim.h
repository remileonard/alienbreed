#ifndef AB_TILE_ANIM_H
#define AB_TILE_ANIM_H

/*
 * Alien Breed SE 92 - C port
 * Tile animation overlay system
 *
 * Implements two kinds of animated tile overlays:
 *
 * 1. TRANSIENT animations (pickup items, doors, fire-door buttons):
 *    Queued by gameplay events.  Each plays for a fixed number of ticks,
 *    then removes itself.  The underlying tile has already been replaced
 *    with floor by the time the animation plays — the overlay is purely
 *    cosmetic, keeping the item / door visible briefly.
 *
 *    Atlas positions (all from lbW01C52A @ main.asm#L14801, level 1 layout;
 *    the same entries are identical for all level atlases):
 *
 *      Pickups (16×16):
 *        1UP        entry  0 → atlas (  0, 128, 16, 16)  lbL0165EA, delay=12
 *        FIRSTAID   entry  1 → atlas ( 16, 128, 16, 16)  lbL01661A, delay=12
 *        KEY        entry  2 → atlas ( 32, 128, 16, 16)  lbL01664A, delay=12
 *        AMMO       entry  3 → atlas ( 48, 128, 16, 16)  lbL01667A, delay=12
 *        CREDITS100 entry  4 → atlas ( 64, 128, 16, 16)  lbL0166AA, delay=12
 *        CREDITS1K  entry  5 → atlas ( 80, 128, 16, 16)  lbL0166DA, delay=12
 *
 *      Horizontal door open (32×16, covers 2-tile wide door pair):
 *        entry 16 → atlas ( 80,  48, 32, 16)  for 2 ticks  lbL0168EA
 *        entry 17 → atlas ( 80,  32, 32, 16)  for 2 ticks  lbL01691A
 *        entry 18 → atlas ( 48,  48, 32, 16)  for 4 ticks  lbL01694A
 *
 *      Vertical door open (16×32, covers 2-tile tall door pair):
 *        entry 19 → atlas ( 96,   0, 16, 32)  for 2 ticks  lbL01697A
 *        entry 20 → atlas ( 80,   0, 16, 32)  for 2 ticks  lbL0169AA
 *        entry 21 → atlas ( 64,   0, 16, 32)  for 4 ticks  lbL0169DA
 *
 *      Fire-door button (16×16):
 *        entry 12 → atlas ( 48,  32, 16, 16)  for 2 ticks  lbL01682A
 *        entry 13 → atlas ( 64,  32, 16, 16)  for 4 ticks  lbL01685A
 *
 * 2. CONTINUOUS reactor engine animation (level 1, tiles 0x2a-0x2d):
 *    Rendered every frame as long as the tile attribute remains 0x2a-0x2d.
 *    Uses a global tick counter + per-row phase to create a moving-wave
 *    effect across the vertical reactor column.
 *    12-frame cycle from lbL004C0C/lbL004C78/lbL004CE4/lbL004D50.
 *    Atlas frames (lbW01C52A entries 47-53):
 *      frame 0: (  0,  96, 32, 32)
 *      frame 1: (112,  48, 48, 16)
 *      frame 2: (160,  48, 48, 16)
 *      frame 3: (208,  48, 48, 16)
 *      frame 4: ( 32,  96, 32, 32)
 *      frame 5: ( 64,  64, 32, 32)
 *      frame 6: ( 64,  96, 32, 32)
 *      7..11 mirror back through 5..1
 *    Phase: each tile has phase = (3 - row%4) so adjacent tiles offset by 1.
 *
 * Ref: patch_tiles / lbW012388 background sprites @ main.asm.
 */

#include "../types.h"

/* ------------------------------------------------------------------ */
/* Transient animation types                                           */
/* ------------------------------------------------------------------ */
typedef enum {
    TILEANIM_PICKUP_1UP,
    TILEANIM_PICKUP_FIRSTAID,
    TILEANIM_PICKUP_KEY,
    TILEANIM_PICKUP_AMMO,
    TILEANIM_PICKUP_CREDITS100,
    TILEANIM_PICKUP_CREDITS1000,
    TILEANIM_DOOR_H,    /* horizontal door — 32×16 overlay at top-left tile of pair */
    TILEANIM_DOOR_V,    /* vertical   door — 16×32 overlay at top-left tile of pair */
    TILEANIM_FIRE_DOOR  /* fire-door button — 16×16 flash */
} TileAnimType;

/* Maximum simultaneous transient animations */
#define TILE_ANIM_MAX 32

typedef struct {
    int          active;
    int          col, row;   /* map tile coordinates */
    TileAnimType type;
    int          frame;      /* current sub-frame within the animation */
    int          ticks;      /* ticks remaining in current sub-frame */
} TileAnim;

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

/* Reset all active animations (call on level load). */
void tile_anim_init(void);

/*
 * Queue a transient animation at map tile (col, row).
 * If the slot pool is full the request is silently dropped (cosmetic only).
 */
void tile_anim_queue(int col, int row, TileAnimType type);

/* Advance all active animations by one tick.  Call once per frame. */
void tile_anim_update(void);

/*
 * Render all active transient animations.
 * Must be called after tilemap_render() so overlays appear on top.
 * Uses the current g_camera_x / g_camera_y scroll position.
 */
void tile_anim_render(void);

/*
 * Render reactor engine animation overlays for all tiles with
 * attribute 0x2a-0x2d in the current map.
 * global_tick is the running frame counter (incremented every rendered frame).
 * Must be called after tilemap_render() and before sprite rendering.
 */
void tile_anim_render_reactor(int global_tick);

#endif /* AB_TILE_ANIM_H */
