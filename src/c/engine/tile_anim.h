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
 * 2. CONTINUOUS ship engine flame animation (level 1, tiles 0x18-0x1C):
 *    Rendered every frame as long as the tile attribute remains in range.
 *    Each tile has a 2-frame, 32×32 BOB animation sourced from L1AN atlas.
 *    Source: lbC004884-lbC0048CC (tile handlers) + lbW01EB12-lbW01EB82 (sequences).
 *    Atlas pairs (lbW01BECA entries, all 32×32):
 *      0x18: frame A (64, 64) / frame B (160, 64)  entries 22 & 25
 *      0x19: frame A (96, 64) / frame B (192, 64)  entries 23 & 26
 *      0x1A: frame A (128,64) / frame B (224, 64)  entries 24 & 27
 *      0x1B: frame A (64, 96) / frame B (160, 96)  entries 28 & 31
 *      0x1C: frame A (96, 96) / frame B (192, 96)  entries 29 & 32
 *    Sequence delay=0 (1 game tick per frame); cycle advances every 2
 *    display ticks (global_tick / 2) to match the original 25 fps rate.
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
 * Render ship engine flame animation overlays for all tiles with
 * attribute 0x18-0x1C in the current map (level 1 spaceship engines).
 * global_tick is the running frame counter (incremented every rendered frame).
 * Must be called after tilemap_render() and before sprite rendering.
 */
void tile_anim_render_ship_engines(int global_tick);

#endif /* AB_TILE_ANIM_H */
