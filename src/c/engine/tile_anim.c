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
/* Ship engine / fan animations (tiles 0x18-0x1C, per-level/atlas)   */
/* ------------------------------------------------------------------ */

/*
 * Tile attributes 0x18-0x1C animate with BOBs from the level animation
 * atlas (LxAN).  The atlas layout — and therefore the pixel coordinates —
 * differ between L0AN (level 1) and L1AN (levels 2/10/11).
 * Other atlases are not yet implemented and receive no overlay.
 *
 * Animation timing: ASM delay=N → (N+1)×2 display ticks per frame.
 * delay=0 → 2 display ticks → advance every (global_tick / 2) step.
 *
 * ---- L0AN (lbW01BECA, level 1) ----------------------------------------
 * All five tiles use 2-frame 32×32 animations (lbW01EB12..lbW01EB82).
 * Source: lbW01BECA entries 22-32.
 *   0x18: (64,64,32,32) ↔ (160,64,32,32)   entries 22,25
 *   0x19: (96,64,32,32) ↔ (192,64,32,32)   entries 23,26
 *   0x1A: (128,64,32,32) ↔ (224,64,32,32)  entries 24,27
 *   0x1B: (64,96,32,32) ↔ (160,96,32,32)   entries 28,31
 *   0x1C: (96,96,32,32) ↔ (192,96,32,32)   entries 29,32
 *
 * ---- L1AN (lbW01C52A, levels 2/10/11) ----------------------------------
 *   0x18: 4-frame fan (lbL01EC36) entries 41-44, 32×32, delay=0
 *         (128,64)→(160,64)→(192,64)→(224,64)→loop
 *   0x19: 6-frame vent ping-pong (lbL01ECBA) entries 33-36, 16×32, delay=0
 *         drawn at tile col+1: (256,0)→(272,0)→(288,0)→(304,0)→(288,0)→(272,0)→loop
 *         (addq.l #2,a3 before lbC00AE2E in lbC00493A)
 *   0x1A: 6-frame pipe ping-pong (lbL01ECF6) entries 29-32, 16×48, delay=0
 *         drawn at tile col+1: (256,0)→(272,0)→(288,0)→(304,0)→(288,0)→(272,0)→loop
 *         (addq.l #2,a3 before lbC00AE2E in lbC00494E)
 *   0x1B: not animated on L1AN (lbC004962 = rts; engine_tile_mask bit 3 = 0)
 *   0x1C: slow blink (lbL01EC8E) entries 45/47/46/47, 32×32
 *         delays 120,3,80,3 → total 420-tick cycle
 *         (0,64,32,32)→(0,96,32,32)→(32,64,32,32)→(0,96,32,32)→loop
 *
 * Per-level mask: LevelDef.engine_tile_mask (bit N = tile 0x18+N).
 */

/* L0AN: 2-frame 32×32 engine pairs (frame A and frame B atlas positions). */
typedef struct { int ax0, ay0, ax1, ay1; } EngineAnimPair;

static const EngineAnimPair k_engine_anim_l0an[5] = {
    /* 0x18 */ {  64, 64, 160, 64 },
    /* 0x19 */ {  96, 64, 192, 64 },
    /* 0x1A */ { 128, 64, 224, 64 },
    /* 0x1B */ {  64, 96, 160, 96 },
    /* 0x1C */ {  96, 96, 192, 96 },
};

/* L1AN tile 0x18: 4-frame fan animation (lbL01EC36), 32×32. */
static const int k_fan18_ax[4]   = { 128, 160, 192, 224 };
#define FAN18_AY  64
#define FAN18_W   32
#define FAN18_H   32

/* L1AN tiles 0x19 and 0x1A: 6-frame ping-pong at col+1 (lbL01ECBA/lbL01ECF6).
 * Both share x positions; 0x19 is 16×32, 0x1A is 16×48. */
static const int k_vent_ax[6]    = { 256, 272, 288, 304, 288, 272 };
#define VENT_AY  0
#define VENT19_W 16
#define VENT19_H 32
#define VENT1A_W 16
#define VENT1A_H 48

/* L1AN tile 0x1C slow-blink: delays 120,3,80,3 → display ticks 242,8,162,8.
 * Total cycle = 420 display ticks (also shared by L0AN tile 0x1F, see below). */
#define SLOW_BLINK_CYCLE        420
#define SLOW_BLINK_PHASE1_END   242   /* end of frame 0: (delay=120+1)*2 */
#define SLOW_BLINK_PHASE2_END   250   /* end of frame 1: +8  (delay=3+1)*2 */
#define SLOW_BLINK_PHASE3_END   412   /* end of frame 2: +162 (delay=80+1)*2 */
static int slow_blink_frame(int tick)
{
    int phase = tick % SLOW_BLINK_CYCLE;
    if (phase < SLOW_BLINK_PHASE1_END) return 0;   /* (0,64,32,32) */
    if (phase < SLOW_BLINK_PHASE2_END) return 1;   /* (0,96,32,32) */
    if (phase < SLOW_BLINK_PHASE3_END) return 2;   /* (32,64,32,32) */
    return 1;                                       /* (0,96,32,32) */
}
static const int k_slow_blink_ax[3] = {  0,  0, 32 };
static const int k_slow_blink_ay[3] = { 64, 96, 64 };

void tile_anim_render_ship_engines(int global_tick)
{
    const UBYTE *atlas = anim_gfx_get_atlas();
    if (!atlas) return;

    const char *map_an = k_level_defs[g_cur_level].map_an;
    int is_l0an = (strcmp(map_an, "L0AN") == 0);
    int is_l1an = (strcmp(map_an, "L1AN") == 0);
    if (!is_l0an && !is_l1an) return; /* TODO: implement other atlases */

    int engine_mask = k_level_defs[g_cur_level].engine_tile_mask;

    int start_col = g_camera_x / MAP_TILE_W;
    int start_row = g_camera_y / MAP_TILE_H;
    int off_x     = g_camera_x % MAP_TILE_W;
    int off_y     = g_camera_y % MAP_TILE_H;
    int cols_vis  = (320 + off_x + MAP_TILE_W - 1) / MAP_TILE_W;
    int rows_vis  = (256 + off_y + MAP_TILE_H - 1) / MAP_TILE_H;

    int frame2    = (global_tick / 2) & 1;      /* 2-frame alternating */
    int frame4    = (global_tick / 2) % 4;      /* 4-frame fan */
    int frame6    = (global_tick / 2) % 6;      /* 6-frame ping-pong */

    for (int tr = 0; tr <= rows_vis; tr++) {
        int map_row = start_row + tr;
        if (map_row < 0 || map_row >= MAP_ROWS) continue;

        for (int tc = 0; tc <= cols_vis; tc++) {
            int map_col = start_col + tc;
            if (map_col < 0 || map_col >= MAP_COLS) continue;

            UBYTE attr = tilemap_attr(&g_cur_map, map_col, map_row) & 0x3F;
            if (attr < 0x18 || attr > 0x1C) continue;

            /* Skip tiles not animated on this level. */
            int tile_bit = 1 << (attr - 0x18);
            if (!(engine_mask & tile_bit)) continue;

            int dst_x = tc * MAP_TILE_W - off_x;
            int dst_y = tr * MAP_TILE_H - off_y;

            if (is_l0an) {
                /* 2-frame 32×32 engine animation */
                const EngineAnimPair *ep = &k_engine_anim_l0an[attr - 0x18];
                int ax = frame2 ? ep->ax1 : ep->ax0;
                int ay = frame2 ? ep->ay1 : ep->ay0;
                if (dst_x + 32 < 0 || dst_x >= 320) continue;
                if (dst_y + 32 < 0 || dst_y >= 256) continue;
                video_blit(atlas + ay * ANIM_ATLAS_W + ax,
                           ANIM_ATLAS_W, dst_x, dst_y, 32, 32, 0);
            } else { /* is_l1an */
                switch (attr) {
                case 0x18: {
                    /* 4-frame fan, 32×32 */
                    int ax = k_fan18_ax[frame4];
                    if (dst_x + FAN18_W < 0 || dst_x >= 320) break;
                    if (dst_y + FAN18_H < 0 || dst_y >= 256) break;
                    video_blit(atlas + FAN18_AY * ANIM_ATLAS_W + ax,
                               ANIM_ATLAS_W, dst_x, dst_y, FAN18_W, FAN18_H, 0);
                    break;
                }
                case 0x19: {
                    /* 6-frame vent ping-pong, 16×32, drawn at col+1 */
                    int bx = dst_x + MAP_TILE_W;
                    if (bx + VENT19_W < 0 || bx >= 320) break;
                    if (dst_y + VENT19_H < 0 || dst_y >= 256) break;
                    int ax = k_vent_ax[frame6];
                    video_blit(atlas + VENT_AY * ANIM_ATLAS_W + ax,
                               ANIM_ATLAS_W, bx, dst_y, VENT19_W, VENT19_H, 0);
                    break;
                }
                case 0x1A: {
                    /* 6-frame pipe ping-pong, 16×48, drawn at col+1 */
                    int bx = dst_x + MAP_TILE_W;
                    if (bx + VENT1A_W < 0 || bx >= 320) break;
                    if (dst_y + VENT1A_H < 0 || dst_y >= 256) break;
                    int ax = k_vent_ax[frame6];
                    video_blit(atlas + VENT_AY * ANIM_ATLAS_W + ax,
                               ANIM_ATLAS_W, bx, dst_y, VENT1A_W, VENT1A_H, 0);
                    break;
                }
                case 0x1C: {
                    /* Slow blink: 420-tick cycle */
                    int fi = slow_blink_frame(global_tick);
                    int ax = k_slow_blink_ax[fi];
                    int ay = k_slow_blink_ay[fi];
                    if (dst_x + 32 < 0 || dst_x >= 320) break;
                    if (dst_y + 32 < 0 || dst_y >= 256) break;
                    video_blit(atlas + ay * ANIM_ATLAS_W + ax,
                               ANIM_ATLAS_W, dst_x, dst_y, 32, 32, 0);
                    break;
                }
                default:
                    break;
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Screen/console tile animations (tiles 0x1D, 0x1E, 0x1F)           */
/* ------------------------------------------------------------------ */

/*
 * These three tile attributes are dispatched from the per-level tile-handler
 * table (lbC004384 + level_flag) in main.asm.  Each has a per-atlas animation
 * overlaid on the static tilemap background.
 *
 * Animation timing: ASM delay=N → (N+1)×2 display ticks per frame.
 * delay=0 → 2 display ticks → advance every (global_tick / 2) step.
 * delay=2 → 6 display ticks, etc.
 * Exception: existing tile 0x1D L1AN keeps INTEX_FRAME_DELAY=4 for
 * compatibility with prior behaviour.
 *
 * ---- Tile 0x1D -------------------------------------------------------
 * L0AN (lbW01EB9E, level 1): 2-frame 32×32, delay=0.
 *   lbW01BECA entries 30,33: (128,96,32,32) ↔ (224,96,32,32).
 *
 * L1AN (lbL01EC62, levels 2/10/11): 4-step 16×16, delay=2 (4 ticks/frame).
 *   lbW01C52A entries 22-24: (176,0)→(176,16)→(176,32)→(176,16)→loop.
 *
 * ---- Tile 0x1E -------------------------------------------------------
 * L0AN (lbL01EC12, level 1): 3-frame 32×16, delay=0 (2 ticks/frame).
 *   lbW01BECA entries 40-42: (112,0,32,16)→(112,16,32,16)→(112,32,32,16)→loop.
 *
 * L1AN (lbL02013E, levels 2/10/11): 4-step 48×16.
 *   lbW01C52A entries 48,49,50,49 with delays 3,4,2,2:
 *   display ticks per step: 8,10,6,6 → total cycle = 30.
 *   Frames: (112,48,48,16)→(160,48,48,16)→(208,48,48,16)→(160,48,48,16)→loop.
 *
 * ---- Tile 0x1F -------------------------------------------------------
 * L0AN (lbL01EBBA, level 1): slow-blink 32×32, delays 120,3,80,3.
 *   Display ticks: 242,8,162,8 → total cycle = 420.
 *   lbW01BECA entries 34,35,36,35:
 *   (0,64,32,32)→(0,96,32,32)→(32,64,32,32)→(0,96,32,32)→loop.
 *   (Same 420-tick helper as L1AN tile 0x1C in tile_anim_render_ship_engines.)
 *
 * L1AN tile 0x1F (lbC00499A): two separate BOBs + complex non-uniform delays;
 *   not yet implemented.
 */

/* Existing tile 0x1D L1AN timing constant (kept for compatibility). */
#define INTEX_FRAME_DELAY   4

/* Tile 0x1D L0AN: 2-frame 32×32 (lbW01EB9E → lbW01BECA entries 30,33). */
static const int k_1d_l0an_ax[2]  = { 128, 224 };
#define TID1D_L0AN_AY   96
#define TID1D_L0AN_W    32
#define TID1D_L0AN_H    32

/* Tile 0x1D L1AN: 4-step 16×16 (lbL01EC62 → lbW01C52A entries 22-24). */
static const int k_1d_l1an_ay[4]  = { 0, 16, 32, 16 };
#define TID1D_L1AN_AX   176
#define TID1D_L1AN_W    16
#define TID1D_L1AN_H    16

/* Tile 0x1E L0AN: 3-frame 32×16 (lbL01EC12 → lbW01BECA entries 40-42). */
static const int k_1e_l0an_ay[3]  = { 0, 16, 32 };
#define TID1E_L0AN_AX   112
#define TID1E_L0AN_W    32
#define TID1E_L0AN_H    16

/* Tile 0x1E L1AN: 4-step 48×16 (lbL02013E → lbW01C52A entries 48,49,50,49).
 * Delays 3,4,2,2 → display ticks 8,10,6,6 → total cycle = 30. */
static const int k_1e_l1an_ax[4]   = { 112, 160, 208, 160 };
#define TID1E_L1AN_AY       48
#define TID1E_L1AN_W        48
#define TID1E_L1AN_H        16
#define TID1E_L1AN_CYCLE    30
#define TID1E_L1AN_PHASE1   8    /* end of frame 0: (delay=3+1)*2 */
#define TID1E_L1AN_PHASE2   18   /* end of frame 1: +10 (delay=4+1)*2 */
#define TID1E_L1AN_PHASE3   24   /* end of frame 2: +6  (delay=2+1)*2 */
static int tile1e_l1an_frame(int tick)
{
    int phase = tick % TID1E_L1AN_CYCLE;
    if (phase < TID1E_L1AN_PHASE1) return 0;
    if (phase < TID1E_L1AN_PHASE2) return 1;
    if (phase < TID1E_L1AN_PHASE3) return 2;
    return 3;
}

/* Tile 0x1F L0AN: slow-blink 32×32 (lbL01EBBA → lbW01BECA entries 34,35,36,35).
 * Delays 120,3,80,3 → display ticks 242,8,162,8 → total cycle = 420.
 * Uses the same slow_blink_frame() helper and k_slow_blink_ax/ay tables
 * defined above in the engine-tile section. */

void tile_anim_render_intex_screens(int global_tick)
{
    const UBYTE *atlas = anim_gfx_get_atlas();
    if (!atlas) return;

    const char *map_an = k_level_defs[g_cur_level].map_an;
    int is_l0an = (strcmp(map_an, "L0AN") == 0);
    int is_l1an = (strcmp(map_an, "L1AN") == 0);
    if (!is_l0an && !is_l1an) return; /* TODO: implement other atlases */

    int start_col = g_camera_x / MAP_TILE_W;
    int start_row = g_camera_y / MAP_TILE_H;
    int off_x     = g_camera_x % MAP_TILE_W;
    int off_y     = g_camera_y % MAP_TILE_H;
    int cols_vis  = (320 + off_x + MAP_TILE_W - 1) / MAP_TILE_W;
    int rows_vis  = (256 + off_y + MAP_TILE_H - 1) / MAP_TILE_H;

    /* Precompute frame indices used below. */
    int frame2   = (global_tick / 2) & 1;                             /* 2-frame alternating */
    int frame1d_l1an = (global_tick / INTEX_FRAME_DELAY) % 4;         /* 0x1D L1AN */
    int frame1e_l0an = (global_tick / 2) % 3;                         /* 0x1E L0AN */

    for (int tr = 0; tr <= rows_vis; tr++) {
        int map_row = start_row + tr;
        if (map_row < 0 || map_row >= MAP_ROWS) continue;

        for (int tc = 0; tc <= cols_vis; tc++) {
            int map_col = start_col + tc;
            if (map_col < 0 || map_col >= MAP_COLS) continue;

            UBYTE attr = tilemap_attr(&g_cur_map, map_col, map_row) & 0x3F;
            if (attr < 0x1D || attr > 0x1F) continue;

            int dst_x = tc * MAP_TILE_W - off_x;
            int dst_y = tr * MAP_TILE_H - off_y;

            switch (attr) {
            case 0x1D:
                if (is_l0an) {
                    /* 2-frame 32×32 (lbW01EB9E) */
                    int ax = k_1d_l0an_ax[frame2];
                    if (dst_x + TID1D_L0AN_W < 0 || dst_x >= 320) break;
                    if (dst_y + TID1D_L0AN_H < 0 || dst_y >= 256) break;
                    video_blit(atlas + TID1D_L0AN_AY * ANIM_ATLAS_W + ax,
                               ANIM_ATLAS_W, dst_x, dst_y,
                               TID1D_L0AN_W, TID1D_L0AN_H, 0);
                } else { /* is_l1an */
                    /* 4-step 16×16 (lbL01EC62) */
                    int ay = k_1d_l1an_ay[frame1d_l1an];
                    if (dst_x + TID1D_L1AN_W < 0 || dst_x >= 320) break;
                    if (dst_y + TID1D_L1AN_H < 0 || dst_y >= 256) break;
                    video_blit(atlas + ay * ANIM_ATLAS_W + TID1D_L1AN_AX,
                               ANIM_ATLAS_W, dst_x, dst_y,
                               TID1D_L1AN_W, TID1D_L1AN_H, 0);
                }
                break;

            case 0x1E:
                if (is_l0an) {
                    /* 3-frame 32×16 (lbL01EC12) */
                    int ay = k_1e_l0an_ay[frame1e_l0an];
                    if (dst_x + TID1E_L0AN_W < 0 || dst_x >= 320) break;
                    if (dst_y + TID1E_L0AN_H < 0 || dst_y >= 256) break;
                    video_blit(atlas + ay * ANIM_ATLAS_W + TID1E_L0AN_AX,
                               ANIM_ATLAS_W, dst_x, dst_y,
                               TID1E_L0AN_W, TID1E_L0AN_H, 0);
                } else { /* is_l1an */
                    /* 4-step 48×16 (lbL02013E) */
                    int fi = tile1e_l1an_frame(global_tick);
                    int ax = k_1e_l1an_ax[fi];
                    if (dst_x + TID1E_L1AN_W < 0 || dst_x >= 320) break;
                    if (dst_y + TID1E_L1AN_H < 0 || dst_y >= 256) break;
                    video_blit(atlas + TID1E_L1AN_AY * ANIM_ATLAS_W + ax,
                               ANIM_ATLAS_W, dst_x, dst_y,
                               TID1E_L1AN_W, TID1E_L1AN_H, 0);
                }
                break;

            case 0x1F:
                if (is_l0an) {
                    /* Slow-blink 32×32 (lbL01EBBA), 420-tick cycle */
                    int fi = slow_blink_frame(global_tick);
                    int ax = k_slow_blink_ax[fi];
                    int ay = k_slow_blink_ay[fi];
                    if (dst_x + 32 < 0 || dst_x >= 320) break;
                    if (dst_y + 32 < 0 || dst_y >= 256) break;
                    video_blit(atlas + ay * ANIM_ATLAS_W + ax,
                               ANIM_ATLAS_W, dst_x, dst_y, 32, 32, 0);
                }
                /* L1AN tile 0x1F (lbC00499A): two-BOB animation — not yet implemented. */
                break;

            default:
                break;
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
