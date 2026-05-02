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
/* Reactor engine animation                                            */
/* ------------------------------------------------------------------ */

/*
 * 12-frame cycle (lbL004C0C @ main.asm#L2693).
 * lbW01C52A entries 47-53 (true indices):
 *   frame 0 → entry 47: (  0, 96, 32, 32)
 *   frame 1 → entry 48: (112, 48, 48, 16)
 *   frame 2 → entry 49: (160, 48, 48, 16)
 *   frame 3 → entry 50: (208, 48, 48, 16)
 *   frame 4 → entry 51: ( 32, 96, 32, 32)
 *   frame 5 → entry 52: ( 64, 64, 32, 32)
 *   frame 6 → entry 53: ( 64, 96, 32, 32)
 *   7..11 mirror back through 5..1
 */
typedef struct { int ax, ay, aw, ah; } ReactorFrame;

static const ReactorFrame k_reactor_frames[12] = {
    {   0, 96, 32, 32 },  /* 0 */
    { 112, 48, 48, 16 },  /* 1 */
    { 160, 48, 48, 16 },  /* 2 */
    { 208, 48, 48, 16 },  /* 3 */
    {  32, 96, 32, 32 },  /* 4 */
    {  64, 64, 32, 32 },  /* 5 */
    {  64, 96, 32, 32 },  /* 6 */
    {  64, 64, 32, 32 },  /* 7 = mirror of 5 */
    {  32, 96, 32, 32 },  /* 8 = mirror of 4 */
    { 208, 48, 48, 16 },  /* 9 = mirror of 3 */
    { 160, 48, 48, 16 },  /* 10 = mirror of 2 */
    { 112, 48, 48, 16 },  /* 11 = mirror of 1 */
};

/*
 * The 4 original sequences start at different points in the 12-frame cycle.
 * lbC004B8A assigns:
 *   tile at row R   → lbL004D50 (starts at frame 3, entry 50)
 *   tile at row R+1 → lbL004CE4 (starts at frame 2, entry 49)
 *   tile at row R+2 → lbL004C78 (starts at frame 1, entry 48)
 *   tile at row R+3 → lbL004C0C (starts at frame 0, entry 47)
 * Phase offset relative to row mod 4: phase = (3 - row%4).
 *
 * The Amiga loop runs at 25 Hz with delay=1 per frame → 1 frame per game tick.
 * In the C port at 50 Hz we advance the animation every 2 display frames
 * (global_tick / 2) to preserve the original speed.
 */
void tile_anim_render_reactor(int global_tick)
{
    const UBYTE *atlas = anim_gfx_get_atlas();
    if (!atlas) return;

    int start_col = g_camera_x / MAP_TILE_W;
    int start_row = g_camera_y / MAP_TILE_H;
    int off_x     = g_camera_x % MAP_TILE_W;
    int off_y     = g_camera_y % MAP_TILE_H;
    int cols_vis  = (320 + off_x + MAP_TILE_W - 1) / MAP_TILE_W;
    int rows_vis  = (256 + off_y + MAP_TILE_H - 1) / MAP_TILE_H;

    for (int tr = 0; tr <= rows_vis; tr++) {
        int map_row = start_row + tr;
        if (map_row < 0 || map_row >= MAP_ROWS) continue;

        for (int tc = 0; tc <= cols_vis; tc++) {
            int map_col = start_col + tc;
            if (map_col < 0 || map_col >= MAP_COLS) continue;

            UBYTE attr = tilemap_attr(&g_cur_map, map_col, map_row) & 0x3F;
            if (attr < 0x2a || attr > 0x2d) continue;

            /* Phase offset: tiles in the same vertical reactor column
             * are staggered by 1 frame per row, matching the 4 sequences
             * in lbC004B8A.  Row mod 4 gives position within the group.
             * (map_row % 4) is in [0,3], so (3 - (map_row % 4)) is in [0,3]
             * and the final % 12 keeps the index in range. */
            int phase = (3 - (map_row % 4)) % 12;
            int anim_idx = ((global_tick / 2) + phase) % 12;

            const ReactorFrame *rf = &k_reactor_frames[anim_idx];

            int dst_x = tc * MAP_TILE_W - off_x;
            int dst_y = tr * MAP_TILE_H - off_y;

            if (dst_x + rf->aw < 0 || dst_x >= 320) continue;
            if (dst_y + rf->ah < 0 || dst_y >= 256) continue;

            const UBYTE *src = atlas + rf->ay * ANIM_ATLAS_W + rf->ax;
            video_blit(src, ANIM_ATLAS_W, dst_x, dst_y, rf->aw, rf->ah, 0);
        }
    }
}
