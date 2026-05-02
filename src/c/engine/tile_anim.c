/*
 * Alien Breed SE 92 - C port
 * Tile animation overlay system
 */

#include "tile_anim.h"
#include "anim_gfx.h"
#include "tilemap.h"
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
                s_anims[i].active = 0;  /* animation finished */
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
/* Ship engine flame animation (level 1, tiles 0x18-0x1C)             */
/* ------------------------------------------------------------------ */

/*
 * Each of the five ship-engine tile attributes (0x18..0x1C) has a 2-frame
 * animation using 32×32 BOBs from the L1AN atlas.
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
