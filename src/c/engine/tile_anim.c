/*
 * Alien Breed SE 92 - C port
 * Tile animation overlay system
 */

#include "tile_anim.h"
#include "anim_gfx.h"
#include "tilemap.h"
#include "../game/constants.h"
#include "../game/level.h"
#include "../hal/video.h"
#include <string.h>

extern int g_camera_x, g_camera_y;

/* ------------------------------------------------------------------ */
/* Transient animation slot pool                                       */
/* ------------------------------------------------------------------ */

static TileAnim s_anims[TILE_ANIM_MAX];

void tile_anim_init(void)
{
    memset(s_anims, 0, sizeof(s_anims));
}

void tile_anim_queue(int col, int row, TileAnimType type)
{
    /* Avoid duplicate: if this tile already has an active animation of the
     * same type, reset it rather than stacking a second slot. */
    for (int i = 0; i < TILE_ANIM_MAX; i++) {
        if (s_anims[i].active &&
            s_anims[i].col == col && s_anims[i].row == row &&
            s_anims[i].type == type) {
            s_anims[i].frame = 0;
            s_anims[i].ticks = 0;
            return;
        }
    }

    for (int i = 0; i < TILE_ANIM_MAX; i++) {
        if (!s_anims[i].active) {
            s_anims[i].active = 1;
            s_anims[i].col    = col;
            s_anims[i].row    = row;
            s_anims[i].type   = type;
            s_anims[i].frame  = 0;
            s_anims[i].ticks  = 0;
            return;
        }
    }
    /* Pool full — silently drop (animation is cosmetic). */
}

/* ------------------------------------------------------------------ */
/* Per-frame-and-type animation descriptor tables                      */
/* ------------------------------------------------------------------ */

/* One frame of a transient animation */
typedef struct { int ax, ay, aw, ah; int duration; } TileAnimFrame;

/* Pickup animations: 1 frame shown for 12 ticks, then done.
 * Ref: lbL020EA6..lbL020F82 @ main.asm#L16128-L16153. */
static const TileAnimFrame k_pickup_1up[]         = { {  0, 128, 16, 16, 12 } };
static const TileAnimFrame k_pickup_firstaid[]     = { { 16, 128, 16, 16, 12 } };
static const TileAnimFrame k_pickup_key[]          = { { 32, 128, 16, 16, 12 } };
static const TileAnimFrame k_pickup_ammo[]         = { { 48, 128, 16, 16, 12 } };
static const TileAnimFrame k_pickup_credits100[]   = { { 64, 128, 16, 16, 12 } };
static const TileAnimFrame k_pickup_credits1000[]  = { { 80, 128, 16, 16, 12 } };

/* Horizontal door open (32×16, 3 frames).
 * Ref: lbL020CFE @ main.asm#L16090 — delays 1,1 then hold ~4 ticks. */
static const TileAnimFrame k_door_h[] = {
    { 80, 48, 32, 16, 2 },
    { 80, 32, 32, 16, 2 },
    { 48, 48, 32, 16, 4 }
};

/* Vertical door open (16×32, 3 frames).
 * Ref: lbL020D32 @ main.asm#L16095 — delays 1,1 then hold ~4 ticks. */
static const TileAnimFrame k_door_v[] = {
    { 96,  0, 16, 32, 2 },
    { 80,  0, 16, 32, 2 },
    { 64,  0, 16, 32, 4 }
};

/* Fire-door button (16×16, 2 frames).
 * Ref: lbL020D66 @ main.asm#L16100 — delay 1 then hold ~4 ticks. */
static const TileAnimFrame k_fire_door[] = {
    { 48, 32, 16, 16, 2 },
    { 64, 32, 16, 16, 4 }
};

static const TileAnimFrame *anim_frames(TileAnimType type, int *out_count)
{
    switch (type) {
    case TILEANIM_PICKUP_1UP:
        *out_count = 1; return k_pickup_1up;
    case TILEANIM_PICKUP_FIRSTAID:
        *out_count = 1; return k_pickup_firstaid;
    case TILEANIM_PICKUP_KEY:
        *out_count = 1; return k_pickup_key;
    case TILEANIM_PICKUP_AMMO:
        *out_count = 1; return k_pickup_ammo;
    case TILEANIM_PICKUP_CREDITS100:
        *out_count = 1; return k_pickup_credits100;
    case TILEANIM_PICKUP_CREDITS1000:
        *out_count = 1; return k_pickup_credits1000;
    case TILEANIM_DOOR_H:
        *out_count = 3; return k_door_h;
    case TILEANIM_DOOR_V:
        *out_count = 3; return k_door_v;
    case TILEANIM_FIRE_DOOR:
        *out_count = 2; return k_fire_door;
    default:
        *out_count = 0; return NULL;
    }
}

/* ------------------------------------------------------------------ */
/* Update                                                              */
/* ------------------------------------------------------------------ */

void tile_anim_update(void)
{
    for (int i = 0; i < TILE_ANIM_MAX; i++) {
        if (!s_anims[i].active) continue;

        int count;
        const TileAnimFrame *frames = anim_frames(s_anims[i].type, &count);
        if (!frames || count == 0) { s_anims[i].active = 0; continue; }

        s_anims[i].ticks++;
        if (s_anims[i].ticks >= frames[s_anims[i].frame].duration) {
            s_anims[i].ticks = 0;
            s_anims[i].frame++;
            if (s_anims[i].frame >= count) {
                /* Door/fire-door animations: hold on the last frame permanently.
                 * Mirrors the original ASM behaviour where lbL020CFE/lbL020D32/lbL020D66
                 * loop back at the -1 terminator, keeping the open-door BOB visible
                 * over the (already floor-patched) tile forever.
                 * All other animations (pickups) deactivate normally. */
                if (s_anims[i].type == TILEANIM_DOOR_H   ||
                    s_anims[i].type == TILEANIM_DOOR_V    ||
                    s_anims[i].type == TILEANIM_FIRE_DOOR) {
                    s_anims[i].frame = count - 1;
                    s_anims[i].ticks = 0;
                } else {
                    s_anims[i].active = 0;  /* animation finished */
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Render — transient overlays                                         */
/* ------------------------------------------------------------------ */

void tile_anim_render(void)
{
    const UBYTE *atlas = anim_gfx_get_atlas();
    if (!atlas) return;

    for (int i = 0; i < TILE_ANIM_MAX; i++) {
        if (!s_anims[i].active) continue;

        int count;
        const TileAnimFrame *frames = anim_frames(s_anims[i].type, &count);
        if (!frames || s_anims[i].frame >= count) continue;

        const TileAnimFrame *f = &frames[s_anims[i].frame];

        int dst_x = s_anims[i].col * MAP_TILE_W - g_camera_x;
        int dst_y = s_anims[i].row * MAP_TILE_H - g_camera_y;

        /* Skip if fully off-screen */
        if (dst_x + f->aw < 0 || dst_x >= 320) continue;
        if (dst_y + f->ah < 0 || dst_y >= 256) continue;

        const UBYTE *src = atlas + f->ay * ANIM_ATLAS_W + f->ax;
        video_blit(src, ANIM_ATLAS_W, dst_x, dst_y, f->aw, f->ah, 0);
    }
}

/* ------------------------------------------------------------------ */
/* Ship engine flame animation (tiles 0x18-0x1C, per-level rules)     */
/* ------------------------------------------------------------------ */

/*
 * Each of the five ship-engine tile attributes (0x18..0x1C) has a 2-frame
 * animation using 32×32 BOBs from the LxAN atlas.
 *
 * Source: lbW01BECA (level 1 BOB table @ main.asm#L14756) entries 22-32,
 * combined with the per-tile animation sequences:
 *
 *   lbC004884 / lbW01EB12: tile 0x18 → frames 22 (64,64) & 25 (160,64)
 *   lbC004896 / lbW01EB2E: tile 0x19 → frames 23 (96,64) & 26 (192,64)
 *   lbC0048A8 / lbW01EB4A: tile 0x1A → frames 24 (128,64) & 27 (224,64)
 *   lbC0048BA / lbW01EB66: tile 0x1B → frames 28 (64,96)  & 31 (160,96)
 *   lbC0048CC / lbW01EB82: tile 0x1C → frames 29 (96,96)  & 32 (192,96)
 *
 * Each sequence has delay=0, meaning 1 game tick per frame.
 * The original game runs at 25 fps (50 Hz / frames_slowdown=2); to match
 * that speed in the C port the frame is advanced every 2 display ticks
 * (global_tick / 2).
 *
 * Per-level rules (lbC004384 + level_flag dispatch table @ main.asm):
 *   Level 1 (level_flag=-256): all 5 tiles animated.
 *   Level 2/10/11 (level_flag=0): tile 0x1B dispatches to lbC004962=rts →
 *     NOT animated.
 *   Levels 7/8/9 (level_flag=256): only tiles 0x18 and 0x19 are animated.
 *   Level 12 (level_flag=1024): tile 0x19 is not animated.
 *   All other levels: all 5 tiles animated.
 * The mask is stored in LevelDef.engine_tile_mask (bit N = tile 0x18+N).
 *
 * IMPORTANT: the atlas coordinates in k_engine_anim are derived from the
 * legacy L0AN atlas (lbW01BECA, level 1).  Other levels use compact atlases
 * (lbW01C52A, lbW01CB8A, …) where those same pixel positions hold different
 * graphics (e.g. fan sprites on L1AN / level 2).  Until per-atlas coordinate
 * tables are added, the overlay is only drawn on level 1 (L0AN).
 */
typedef struct { int ax0, ay0, ax1, ay1; } EngineAnimPair;

static const EngineAnimPair k_engine_anim[5] = {
    /* 0x18 */ {  64, 64, 160, 64 },
    /* 0x19 */ {  96, 64, 192, 64 },
    /* 0x1A */ { 128, 64, 224, 64 },
    /* 0x1B */ {  64, 96, 160, 96 },
    /* 0x1C */ {  96, 96, 192, 96 },
};

void tile_anim_render_ship_engines(int global_tick)
{
    const UBYTE *atlas = anim_gfx_get_atlas();
    if (!atlas) return;

    /* The k_engine_anim atlas coordinates are valid only for the L0AN atlas
     * (level 1, lbW01BECA).  Compact atlases used by other levels (L1AN etc.)
     * have different content at those pixel positions (e.g. fan sprites on
     * level 2).  Skip the overlay entirely on non-L0AN levels. */
    if (strcmp(k_level_defs[g_cur_level].map_an, "L0AN") != 0) return;

    int engine_mask = k_level_defs[g_cur_level].engine_tile_mask;

    int start_col = g_camera_x / MAP_TILE_W;
    int start_row = g_camera_y / MAP_TILE_H;
    int off_x     = g_camera_x % MAP_TILE_W;
    int off_y     = g_camera_y % MAP_TILE_H;
    int cols_vis  = (320 + off_x + MAP_TILE_W - 1) / MAP_TILE_W;
    int rows_vis  = (256 + off_y + MAP_TILE_H - 1) / MAP_TILE_H;

    int frame_idx = (global_tick / 2) & 1;

    for (int tr = 0; tr <= rows_vis; tr++) {
        int map_row = start_row + tr;
        if (map_row < 0 || map_row >= MAP_ROWS) continue;

        for (int tc = 0; tc <= cols_vis; tc++) {
            int map_col = start_col + tc;
            if (map_col < 0 || map_col >= MAP_COLS) continue;

            UBYTE attr = tilemap_attr(&g_cur_map, map_col, map_row) & 0x3F;
            if (attr < 0x18 || attr > 0x1C) continue;

            /* Skip tiles that are not animated on this level (dispatch = rts
             * or bra.w none in the per-level section of lbC004384). */
            int tile_bit = 1 << (attr - 0x18);
            if (!(engine_mask & tile_bit)) continue;

            const EngineAnimPair *ep = &k_engine_anim[attr - 0x18];
            int ax = frame_idx ? ep->ax1 : ep->ax0;
            int ay = frame_idx ? ep->ay1 : ep->ay0;

            int dst_x = tc * MAP_TILE_W - off_x;
            int dst_y = tr * MAP_TILE_H - off_y;

            if (dst_x + 32 < 0 || dst_x >= 320) continue;
            if (dst_y + 32 < 0 || dst_y >= 256) continue;

            const UBYTE *src = atlas + ay * ANIM_ATLAS_W + ax;
            video_blit(src, ANIM_ATLAS_W, dst_x, dst_y, 32, 32, 0);
        }
    }
}

/* ------------------------------------------------------------------ */
/* INTEX computer screen animation (tiles 0x17 and 0x1D)              */
/* ------------------------------------------------------------------ */

/*
 * Tile 0x17 (TILE_INTEX): 9-frame 16×16 blinking-screen animation, active
 * on all levels.  Frames A67-A75 in the animation atlas — all in atlas row 3
 * (y=48), columns 7-15 (x = 112, 128, … 240, stepping by 16 each frame).
 *
 * Tile 0x1D: 4-step blinking animation present only on L1AN levels (2,10,11).
 *   BOBs 22-24 of lbW01C52A (L1AN table): atlas (176, 0/16/32), 16×16.
 *   Sequence from lbC004976 → lbL01EC62: delay=2 ticks/frame, 4-step loop.
 *   Ref: main.asm#L2181 (level 2 tile 0x1D dispatch) → lbL01EC62 @ L15176.
 */

/* Tile 0x17: 9 x-positions in atlas row 3 (y=48), frames A67-A75. */
static const int k_intex17_ax[9] = { 112, 128, 144, 160, 176, 192, 208, 224, 240 };
#define INTEX17_AY            48   /* atlas row 3 */
#define INTEX17_FRAMES         9
#define INTEX_FRAME_DELAY      2   /* 2 ticks per frame */

/* Tile 0x1D: 4-step sequence at atlas x=176, L1AN only. */
static const int k_intex_ax          = 176;
static const int k_intex_ay[4]       = { 0, 16, 32, 16 };
#define INTEX_CYCLE_STEPS  4

void tile_anim_render_intex_screens(int global_tick)
{
    const UBYTE *atlas = anim_gfx_get_atlas();
    if (!atlas) return;

    int is_l1an = (strcmp(k_level_defs[g_cur_level].map_an, "L1AN") == 0);

    int start_col = g_camera_x / MAP_TILE_W;
    int start_row = g_camera_y / MAP_TILE_H;
    int off_x     = g_camera_x % MAP_TILE_W;
    int off_y     = g_camera_y % MAP_TILE_H;
    int cols_vis  = (320 + off_x + MAP_TILE_W - 1) / MAP_TILE_W;
    int rows_vis  = (256 + off_y + MAP_TILE_H - 1) / MAP_TILE_H;

    /* Pre-compute frame indices for both tile types. */
    int frame17  = (global_tick / INTEX_FRAME_DELAY) % INTEX17_FRAMES;
    int ax17     = k_intex17_ax[frame17];

    int frame1D  = (global_tick / INTEX_FRAME_DELAY) % INTEX_CYCLE_STEPS;
    int ay1D     = k_intex_ay[frame1D];

    for (int tr = 0; tr <= rows_vis; tr++) {
        int map_row = start_row + tr;
        if (map_row < 0 || map_row >= MAP_ROWS) continue;

        for (int tc = 0; tc <= cols_vis; tc++) {
            int map_col = start_col + tc;
            if (map_col < 0 || map_col >= MAP_COLS) continue;

            UBYTE attr = tilemap_attr(&g_cur_map, map_col, map_row) & 0x3F;
            if (attr != TILE_INTEX && attr != 0x1D) continue;

            int dst_x = tc * MAP_TILE_W - off_x;
            int dst_y = tr * MAP_TILE_H - off_y;

            if (dst_x + 16 < 0 || dst_x >= 320) continue;
            if (dst_y + 16 < 0 || dst_y >= 256) continue;

            if (attr == TILE_INTEX) {
                /* 9-frame A67-A75 animation, all levels. */
                const UBYTE *src = atlas + INTEX17_AY * ANIM_ATLAS_W + ax17;
                video_blit(src, ANIM_ATLAS_W, dst_x, dst_y, 16, 16, 0);
            } else {
                /* tile 0x1D: L1AN levels only. */
                if (!is_l1an) continue;
                const UBYTE *src = atlas + ay1D * ANIM_ATLAS_W + k_intex_ax;
                video_blit(src, ANIM_ATLAS_W, dst_x, dst_y, 16, 16, 0);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* One-deadly-way door animation (tiles 0x26 and 0x2E)                */
/* ------------------------------------------------------------------ */

/*
 * Both one-deadly-way tile variants share the same 3-frame lightning
 * animation using 16×16 BOBs from the level animation atlas (column 19):
 *   Atlas tile indices A19, A39, A59 → pixel (304,0), (304,16), (304,32).
 *   TILE_ONE_DEADLY_WAY_RIGHT (0x26): one-way passage to the right.
 *   TILE_ONE_DEADLY_WAY_LEFT  (0x2E): one-way passage to the left.
 * The ASM dispatch table has bra.w none for both tiles in all levels;
 * the continuous 3-frame loop was missing from the C port.
 */
static const int k_deadly_way_ax = 304;
static const int k_deadly_way_ay[3] = { 0, 16, 32 };

void tile_anim_render_one_deadly_way(int global_tick)
{
    const UBYTE *atlas = anim_gfx_get_atlas();
    if (!atlas) return;

    int start_col = g_camera_x / MAP_TILE_W;
    int start_row = g_camera_y / MAP_TILE_H;
    int off_x     = g_camera_x % MAP_TILE_W;
    int off_y     = g_camera_y % MAP_TILE_H;
    int cols_vis  = (320 + off_x + MAP_TILE_W - 1) / MAP_TILE_W;
    int rows_vis  = (256 + off_y + MAP_TILE_H - 1) / MAP_TILE_H;

    int frame_idx = (global_tick / 2) % 3;
    int ay = k_deadly_way_ay[frame_idx];

    for (int tr = 0; tr <= rows_vis; tr++) {
        int map_row = start_row + tr;
        if (map_row < 0 || map_row >= MAP_ROWS) continue;

        for (int tc = 0; tc <= cols_vis; tc++) {
            int map_col = start_col + tc;
            if (map_col < 0 || map_col >= MAP_COLS) continue;

            UBYTE attr = tilemap_attr(&g_cur_map, map_col, map_row) & 0x3F;
            if (attr != TILE_ONE_DEADLY_WAY_LEFT &&
                attr != TILE_ONE_DEADLY_WAY_RIGHT) continue;

            int dst_x = tc * MAP_TILE_W - off_x;
            int dst_y = tr * MAP_TILE_H - off_y;

            if (dst_x + 16 < 0 || dst_x >= 320) continue;
            if (dst_y + 16 < 0 || dst_y >= 256) continue;

            const UBYTE *src = atlas + ay * ANIM_ATLAS_W + k_deadly_way_ax;
            video_blit(src, ANIM_ATLAS_W, dst_x, dst_y, 16, 16, 0);
        }
    }
}
