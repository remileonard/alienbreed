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
 * 2. CONTINUOUS engine/fan/pipe animation (tiles 0x18-0x1C, per-level):
 *    Rendered every frame as long as the tile attribute remains in range.
 *    Atlas layout and frame counts vary by level atlas (L0AN vs L1AN).
 *    Per-level rules encoded in LevelDef.engine_tile_mask.
 *    See tile_anim.c for per-atlas frame tables.
 *
 * 3. CONTINUOUS screen/console tile animation (tiles 0x1D, 0x1E, 0x1F):
 *    Rendered every frame for tiles whose attribute matches one of these IDs.
 *    Per-atlas (L0AN / L1AN): different frame sizes, counts, and timing.
 *    See tile_anim.c and tile_anim_render_intex_screens() for details.
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
 * Render engine/fan/pipe animation overlays for tiles with attribute 0x18-0x1C
 * in the current map.
 *
 * Only L0AN (level 1) and L1AN (levels 2/10/11) are implemented.
 * Per-level mask LevelDef.engine_tile_mask controls which tile IDs animate.
 *
 *   L0AN: all five tiles use 2-frame 32×32 animations (lbW01EB12..lbW01EB82).
 *   L1AN: 0x18 = 4-frame fan (32×32), 0x19 = 6-frame vent at col+1 (16×32),
 *          0x1A = 6-frame pipe at col+1 (16×48), 0x1B = none, 0x1C = slow blink.
 *
 * global_tick is the running frame counter (incremented every rendered frame).
 * Must be called after tilemap_render() and before sprite rendering.
 */
void tile_anim_render_ship_engines(int global_tick);

/*
 * Render continuous 3-frame lightning animation overlays for all tiles with
 * attribute 0x26 (TILE_ONE_DEADLY_WAY_RIGHT) or 0x2E (TILE_ONE_DEADLY_WAY_LEFT)
 * in the current map.  Both directions share the same 3-frame animation.
 *
 * The three 16×16 frames are sourced from the level animation atlas (LxAN):
 *   Frame 0: atlas column 19, row 0 → pixel (304,  0, 16, 16)
 *   Frame 1: atlas column 19, row 1 → pixel (304, 16, 16, 16)
 *   Frame 2: atlas column 19, row 2 → pixel (304, 32, 16, 16)
 * (The ASM dispatch table has bra.w none for both tiles in all levels;
 *  this animation was absent from the original per-tile handler path.)
 *
 * global_tick is the running frame counter (incremented every rendered frame).
 */
void tile_anim_render_one_deadly_way(int global_tick);

/*
 * Render continuous animation overlays for computer-screen / console tiles
 * with attributes 0x1D, 0x1E, 0x1F in the current map.
 *
 * Animations are dispatched by tile attribute (not by tile_idx / decoration
 * index), mirroring the per-level dispatch tables in main.asm (lbC004384).
 * Only L0AN (level 1) and L1AN (levels 2/10/11) are implemented; other
 * atlases produce no overlay.
 *
 *   Tile 0x1D:
 *     L0AN — 2-frame 32×32, delay=0.  lbW01EB9E → lbW01BECA entries 30,33.
 *     L1AN — 4-step 16×16, delay=2 (4 ticks/frame).  lbL01EC62 → entries 22-24.
 *
 *   Tile 0x1E:
 *     L0AN — 3-frame 32×16, delay=0 (2 ticks/frame).  lbL01EC12 → entries 40-42.
 *     L1AN — 4-step 48×16, delays 3/4/2/2 → 30-tick cycle.  lbL02013E → entries 48-50.
 *
 *   Tile 0x1F:
 *     L0AN — slow-blink 32×32, delays 120/3/80/3 → 420-tick cycle.  lbL01EBBA → entries 34-36.
 *     L1AN — two-BOB animation (lbC00499A); not yet implemented.
 *
 * global_tick is the running frame counter (incremented every rendered frame).
 */
void tile_anim_render_intex_screens(int global_tick);

#endif /* AB_TILE_ANIM_H */
