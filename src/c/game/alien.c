/*
 * Alien Breed SE 92 - C port
 * Alien module
 */

#include "alien.h"
#include "player.h"
#include "level.h"
#include "../hal/audio.h"
#include "../hal/video.h"
#include "../engine/tilemap.h"
#include "../engine/alien_gfx.h"
#include <string.h>
#include <stdlib.h>

Alien g_aliens[MAX_ALIENS];
int   g_alien_count             = 0;
WORD  g_global_aliens_extra_strength = 0;

/* Alien type table: HP base values (Ref: alien1_struct–alien7_struct @ main.asm#L5918-L5967) */
static const WORD k_alien_type_hp[7] = {
    100,  /* type 1 */
    132,  /* type 2 */
    164,  /* type 3 */
    196,  /* type 4 */
    228,  /* type 5 */
    260,  /* type 6 */
    292   /* type 7 */
};

/* Projectile list (simplified — position + owner + strength) */
typedef struct {
    WORD  x, y;
    WORD  vx, vy;
    WORD  strength;
    int   player_idx;  /* which player fired it */
    int   active;
} Projectile;

#define MAX_PROJECTILES 32
static Projectile s_projectiles[MAX_PROJECTILES];

/* Score awarded per alien kill */
#define ALIEN_SCORE_VALUE 100

/*
 * Spawn point list — mirrors the two spawn slots (lbL00D29A / lbL00D2AA) from
 * main.asm, generalised to support any number of map spawn tiles plus
 * player-triggered facehugger hatches.
 *
 * Initial countdown value of 20 matches `move.w #20, 8(a0)` in lbC00D236.
 * Expanded viewport margin of 80 px matches the ±80 offsets in lbC00D17E.
 * (Ref: lbC00D17E / lbC00D1B4 / lbC00D22A / lbC00D236 @ main.asm#L8547-L8623)
 */
#define MAX_SPAWN_POINTS        256
#define SPAWN_COUNTDOWN_INIT     20  /* frames until first spawn once in viewport */
#define SPAWN_VIEWPORT_MARGIN    80  /* px beyond screen edges for expanded viewport */

typedef struct {
    WORD world_x, world_y;  /* pixel centre of the spawn tile */
    int  countdown;          /* decrements while tile is in viewport; spawn at −1 */
    int  alien_type;         /* 1-based alien type (1=weakest … 7=strongest) */
    int  active;             /* 1 = slot occupied */
    int  one_shot;           /* 1 = deactivate after first spawn (facehugger hatch) */
} SpawnPoint;

static SpawnPoint s_spawn_points[MAX_SPAWN_POINTS];
static int        s_spawn_count = 0;

/* -----------------------------------------------------------------------
 * Return the alien type appropriate for the current level.
 * Ref: alien type selection @ main.asm#L5918-L5973.
 * ----------------------------------------------------------------------- */
static int alien_type_for_level(void)
{
    int t = 1;
    if (g_cur_level >= 6)  t = 2;
    if (g_cur_level >= 8)  t = 3;
    if (g_cur_level >= 10) t = 4;
    if (g_cur_level == 11) t = 5;
    if (g_cur_level == 12) t = 6;
    if (g_cur_level >= 12) t = 7;
    return t;
}

/* -----------------------------------------------------------------------
 * Place one alien at world-pixel position (wx, wy) of the given type.
 * ----------------------------------------------------------------------- */
static void spawn_alien_at(int wx, int wy, int alien_type)
{
    if (g_alien_count >= MAX_ALIENS) return;

    WORD base_hp = (alien_type >= 1 && alien_type <= 7)
                   ? k_alien_type_hp[alien_type - 1]
                   : k_alien_type_hp[0];

    Alien *a    = &g_aliens[g_alien_count++];
    a->pos_x    = (WORD)wx;
    a->pos_y    = (WORD)wy;
    a->speed    = (WORD)(2 + g_global_aliens_extra_strength / 5);
    a->strength = (WORD)(base_hp + g_global_aliens_extra_strength);
    a->alive    = 1;
    a->type_idx = alien_type - 1;
}

void alien_init_variables(void)
{
    memset(g_aliens,       0, sizeof(g_aliens));
    memset(s_projectiles,  0, sizeof(s_projectiles));
    memset(s_spawn_points, 0, sizeof(s_spawn_points));
    g_alien_count = 0;
    s_spawn_count = 0;
}

/*
 * Scan the loaded map for spawn tiles and register them as pending spawn
 * points.  No aliens are created yet — spawning is deferred to
 * alien_spawn_tick() which fires lazily when each tile enters the player's
 * (expanded) viewport, exactly as in the original ASM.
 *
 * Tile attributes that mark alien spawn locations:
 *   0x28 – TILE_ALIEN_SPAWN_BIG   (respawning big-alien location)
 *   0x29 – TILE_ALIEN_SPAWN_SMALL (respawning small-alien location)
 *   0x34 – TILE_ALIEN_HOLE        (hole from which aliens emerge)
 *
 * Ref: lbC00D17E / lbC00D1B4 / lbC00D236 @ main.asm#L8547-L8623;
 *      set_all_aliens_to_default @ main.asm#L8628.
 */
void alien_spawn_from_map(void)
{
    int alien_type = alien_type_for_level();
    s_spawn_count  = 0;

    for (int row = 0; row < MAP_ROWS && s_spawn_count < MAX_SPAWN_POINTS; row++) {
        for (int col = 0; col < MAP_COLS && s_spawn_count < MAX_SPAWN_POINTS; col++) {
            UBYTE attr = tilemap_attr(&g_cur_map, col, row);
            if (attr != TILE_ALIEN_SPAWN_BIG  &&
                attr != TILE_ALIEN_SPAWN_SMALL &&
                attr != TILE_ALIEN_HOLE) continue;

            SpawnPoint *sp = &s_spawn_points[s_spawn_count++];
            sp->world_x    = (WORD)(col * MAP_TILE_W + MAP_TILE_W / 2);
            sp->world_y    = (WORD)(row * MAP_TILE_H + MAP_TILE_H / 2);
            sp->countdown  = SPAWN_COUNTDOWN_INIT;
            sp->alien_type = alien_type;
            sp->active     = 1;
            sp->one_shot   = 0;
        }
    }
}

/*
 * Register a one-shot spawn point at (wx, wy).
 *
 * Called when the player triggers a facehugger hatch tile (0x0A).  The
 * original game queues the tile through lbC00D22A → lbC00D236, which sets up
 * a spawn slot with countdown = 20, then lbC00D1B4 fires it once the tile is
 * in the viewport.  We replicate that deferred behaviour here.
 *
 * The tile itself is patched to floor by the caller before this returns, so
 * the hatch can never be triggered a second time.
 *
 * Ref: tile_facehuggers_hatch → lbC0082C0/CE/8302 → lbC00D22A → lbC00D236
 *      @ main.asm#L5414-L8623.
 */
void alien_spawn_near(int wx, int wy)
{
    if (s_spawn_count >= MAX_SPAWN_POINTS) return;

    SpawnPoint *sp = &s_spawn_points[s_spawn_count++];
    sp->world_x    = (WORD)wx;
    sp->world_y    = (WORD)wy;
    sp->countdown  = SPAWN_COUNTDOWN_INIT;
    sp->alien_type = alien_type_for_level();
    sp->active     = 1;
    sp->one_shot   = 1;
}

/*
 * Per-frame viewport scan — mirrors lbC00D17E / lbC00D1B4 @ main.asm.
 *
 * Expanded viewport (80 px beyond each screen edge):
 *   left   = g_camera_x − 80         right  = g_camera_x + 400 (= left + 480)
 *   top    = g_camera_y − 80         bottom = g_camera_y + 336 (= top  + 416)
 *
 * For each active spawn point:
 *   – If its pixel centre is inside the expanded viewport, the countdown
 *     decrements by 1.  When it reaches −1 an alien is spawned and the
 *     countdown resets to SPAWN_COUNTDOWN_INIT (mirrors lbC00D1B4 line
 *     `move.w 44(a1), 8(a0)` which reloads the timer).
 *   – If the tile is outside the viewport the countdown resets to
 *     SPAWN_COUNTDOWN_INIT (mirrors lbC00D220 which clears the slot pointer
 *     so the next viewport entry starts fresh).
 *   – One-shot slots (facehugger hatches) are deactivated after the spawn.
 *
 * Called from alien_update_all() every game tick.
 */
void alien_spawn_tick(void)
{
    int vp_left   = g_camera_x - SPAWN_VIEWPORT_MARGIN;
    int vp_right  = g_camera_x + SCREEN_W + SPAWN_VIEWPORT_MARGIN;
    int vp_top    = g_camera_y - SPAWN_VIEWPORT_MARGIN;
    int vp_bottom = g_camera_y + SCREEN_H + SPAWN_VIEWPORT_MARGIN;

    for (int i = 0; i < s_spawn_count; i++) {
        SpawnPoint *sp = &s_spawn_points[i];
        if (!sp->active) continue;

        int in_vp = (sp->world_x >= vp_left  && sp->world_x < vp_right &&
                     sp->world_y >= vp_top    && sp->world_y < vp_bottom);

        if (!in_vp) {
            /* Left viewport: reset countdown (ref lbC00D220). */
            sp->countdown = SPAWN_COUNTDOWN_INIT;
            continue;
        }

        sp->countdown--;
        if (sp->countdown >= 0) continue;

        /* Countdown expired: spawn alien and reload timer (ref lbC00D1B4). */
        spawn_alien_at(sp->world_x, sp->world_y, sp->alien_type);
        audio_play_sample(SAMPLE_HATCHING_ALIEN);

        if (sp->one_shot) {
            sp->active = 0;
        } else {
            sp->countdown = SPAWN_COUNTDOWN_INIT;
        }
    }
}

/* Return 1 if the world pixel (wx, wy) falls inside a solid tile. */
static int alien_solid_at(int wx, int wy)
{
    return tilemap_is_solid(&g_cur_map,
                            tilemap_pixel_to_col(wx),
                            tilemap_pixel_to_row(wy));
}

/*
 * Move one alien toward the nearest living player.
 *
 * Re-implementation of the ASM movement at lbC009CE2 / lbC009E1A
 * (main.asm#L6810-L6989):
 *
 *  1. Find the nearest player by Manhattan distance.
 *  2. X axis: if |dx| > 4, move by a->speed; check two leading corners
 *     of the bounding box for solid tiles before committing.
 *  3. Y axis: same treatment independently of X.
 *  4. Build a direction-bit word (bit0=down, bit1=up, bit2=right, bit3=left)
 *     from the actual movement and map it to the 0-based compass direction
 *     using the lbB00A228 look-up table (main.asm#L7077).
 *
 * Bounding box half-extents (8 px) are derived from the alien type struct
 * collision offsets (lbW00A29A … lbW00A31E, main.asm#L7085-L7096).
 */
static void alien_move(Alien *a)
{
    /* ------------------------------------------------------------------ */
    /* 1. Find nearest living player by Manhattan distance                 */
    /* ------------------------------------------------------------------ */
    int tx = -1, ty = -1;
    int best = 0x7FFFFFFF;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!g_players[i].alive) continue;
        int ddx = g_players[i].pos_x - (int)a->pos_x;
        int ddy = g_players[i].pos_y - (int)a->pos_y;
        int d   = (ddx < 0 ? -ddx : ddx) + (ddy < 0 ? -ddy : ddy);
        if (d < best) { best = d; tx = g_players[i].pos_x; ty = g_players[i].pos_y; }
    }
    if (tx < 0) return;  /* no living player */

    int spd     = (int)a->speed;
    int ax      = (int)a->pos_x;
    int ay      = (int)a->pos_y;
    int dir_bits = 0;   /* bit0=down  bit1=up  bit2=right  bit3=left */

    /* ------------------------------------------------------------------ */
    /* 2. X movement — independent of Y                                    */
    /*    Threshold of 4 px mirrors ASM `cmp.w #4,d4` (main.asm#L6926).   */
    /* ------------------------------------------------------------------ */
    int dx = tx - ax;
    if (dx > 4) {
        /* Move right: check top-right and bottom-right corners */
        int nx = ax + spd;
        if (!alien_solid_at(nx + 8, ay - 8) &&
            !alien_solid_at(nx + 8, ay + 8)) {
            ax = nx;
            dir_bits |= 4;  /* right */
        }
    } else if (dx < -4) {
        /* Move left: check top-left and bottom-left corners */
        int nx = ax - spd;
        if (!alien_solid_at(nx - 8, ay - 8) &&
            !alien_solid_at(nx - 8, ay + 8)) {
            ax = nx;
            dir_bits |= 8;  /* left */
        }
    }

    /* ------------------------------------------------------------------ */
    /* 3. Y movement — independent of X                                    */
    /* ------------------------------------------------------------------ */
    int dy = ty - ay;
    if (dy > 4) {
        /* Move down: check bottom-left and bottom-right corners */
        int ny = ay + spd;
        if (!alien_solid_at(ax - 8, ny + 8) &&
            !alien_solid_at(ax + 8, ny + 8)) {
            ay = ny;
            dir_bits |= 1;  /* down */
        }
    } else if (dy < -4) {
        /* Move up: check top-left and top-right corners */
        int ny = ay - spd;
        if (!alien_solid_at(ax - 8, ny - 8) &&
            !alien_solid_at(ax + 8, ny - 8)) {
            ay = ny;
            dir_bits |= 2;  /* up */
        }
    }

    a->pos_x = (WORD)ax;
    a->pos_y = (WORD)ay;

    /* ------------------------------------------------------------------ */
    /* 4. Direction lookup via lbB00A228 table (main.asm#L7077)            */
    /*                                                                      */
    /*    ASM table (word, 1-based direction values):                       */
    /*      [0]=0 [1]=5 [2]=1 [3]=0 [4]=3 [5]=4 [6]=2 [7]=0               */
    /*      [8]=7 [9]=6 [10]=8                                             */
    /*    Subtract 1 to get 0-based atlas column:                           */
    /*      dir_bits=1(S)=4  dir_bits=2(N)=0  dir_bits=4(E)=2              */
    /*      dir_bits=5(SE)=3 dir_bits=6(NE)=1 dir_bits=8(W)=6             */
    /*      dir_bits=9(SW)=5 dir_bits=10(NW)=7                             */
    /* ------------------------------------------------------------------ */
    /* dir_bits: 0=idle  1=S(4)  2=N(0)  4=E(2)  5=SE(3)  6=NE(1)
     *           8=W(6)  9=SW(5)  10=NW(7)  others=invalid(-1) */
    static const int k_dir_table[16] = {
        -1,  4,  0, -1,   /* 0-3  */
         2,  3,  1, -1,   /* 4-7  */
         6,  5,  7, -1,   /* 8-11 */
        -1, -1, -1, -1    /* 12-15 */
    };
    if (dir_bits > 0 && dir_bits < 16 && k_dir_table[dir_bits] >= 0)
        a->direction = k_dir_table[dir_bits];
    /* If dir_bits == 0 (no movement), keep the current direction. */
}

void alien_update_all(void)
{
    /* Lazy viewport-triggered spawning (mirrors lbC00D17E @ main.asm#L8547). */
    alien_spawn_tick();

    for (int i = 0; i < g_alien_count; i++) {
        if (g_aliens[i].alive == 0) continue;

        if (g_aliens[i].alive == 2) {
            /* Dying: advance death explosion frame; fully remove when done.
             * 16 frames at game rate ≈ 0.3 s (Ref: lbL018C2E @ main.asm#L13907,
             * each frame has delay=0 = one game tick). */
            g_aliens[i].death_frame++;
            if (g_aliens[i].death_frame >= ALIEN_DEATH_FRAMES)
                g_aliens[i].alive = 0;
            continue;
        }

        alien_move(&g_aliens[i]);
        g_aliens[i].anim_counter++;
    }

    /* Update projectiles */
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        if (!s_projectiles[i].active) continue;
        s_projectiles[i].x += s_projectiles[i].vx;
        s_projectiles[i].y += s_projectiles[i].vy;

        /* Check if projectile hit a wall */
        int col = tilemap_pixel_to_col(s_projectiles[i].x);
        int row = tilemap_pixel_to_row(s_projectiles[i].y);
        if (tilemap_is_solid(&g_cur_map, col, row)) {
            s_projectiles[i].active = 0;
        }

        /* Off-screen */
        if (s_projectiles[i].x < 0 || s_projectiles[i].x >= MAP_COLS * MAP_TILE_W ||
            s_projectiles[i].y < 0 || s_projectiles[i].y >= MAP_ROWS * MAP_TILE_H) {
            s_projectiles[i].active = 0;
        }
    }
}

/* Spawn a projectile from player p (called by player_update logic) */
void alien_spawn_projectile(int player_idx, WORD x, WORD y,
                            WORD vx, WORD vy, WORD strength)
{
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        if (!s_projectiles[i].active) {
            s_projectiles[i].x          = x;
            s_projectiles[i].y          = y;
            s_projectiles[i].vx         = vx;
            s_projectiles[i].vy         = vy;
            s_projectiles[i].strength   = strength;
            s_projectiles[i].player_idx = player_idx;
            s_projectiles[i].active     = 1;
            return;
        }
    }
}

void aliens_collisions_with_weapons(void)
{
    for (int pi = 0; pi < MAX_PROJECTILES; pi++) {
        if (!s_projectiles[pi].active) continue;

        for (int ai = 0; ai < g_alien_count; ai++) {
            if (g_aliens[ai].alive != 1) continue;

            int dx = s_projectiles[pi].x - g_aliens[ai].pos_x;
            int dy = s_projectiles[pi].y - g_aliens[ai].pos_y;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;

            if (dx < 8 && dy < 8) {
                s_projectiles[pi].active = 0;
                g_aliens[ai].strength -= s_projectiles[pi].strength;
                if (g_aliens[ai].strength <= 0) {
                    alien_kill(ai);
                    /* Award score to the firing player */
                    int owner = s_projectiles[pi].player_idx;
                    if (owner >= 0 && owner < MAX_PLAYERS)
                        g_players[owner].score += ALIEN_SCORE_VALUE;
                }
            }
        }
    }
}

void aliens_collisions_with_players(void)
{
    for (int ai = 0; ai < g_alien_count; ai++) {
        /* Only active (alive == 1) aliens can damage players.
         * Dying aliens (alive == 2) have already triggered the encounter.
         * Ref: tst.w 56(a0) / tst.w 52(a0) @ main.asm#L7636-L7656. */
        if (g_aliens[ai].alive != 1) continue;

        for (int pi = 0; pi < MAX_PLAYERS; pi++) {
            if (!g_players[pi].alive) continue;
            /* Skip players already in their death animation */
            if (g_players[pi].death_counter > 0) continue;
            if (player_is_invincible(&g_players[pi])) continue;

            /* Bounding-box collision (AABB):
             *   Alien  bbox : [pos_x,   pos_x+32] × [pos_y,   pos_y+32]
             *   Player bbox : [pos_x+8, pos_x+24] × [pos_y+8, pos_y+24]
             *
             * Alien bbox comes from lbW008F14+4={0,0} and lbW008F14+8={32,32}
             * (the offset pair added to pos when building cur_alien*_dats).
             * Player bbox comes from add.l #$80008 (top-left +8) and
             * add.l #$100010 (size 16×16).
             * Ref: aliens_collisions_with_players @ main.asm#L7598-L7618. */
            int ax1 = (int)g_aliens[ai].pos_x;
            int ax2 = ax1 + 32;
            int ay1 = (int)g_aliens[ai].pos_y;
            int ay2 = ay1 + 32;

            int px1 = (int)g_players[pi].pos_x + PLAYER_BBOX_OFFSET;
            int px2 = (int)g_players[pi].pos_x + PLAYER_BBOX_OFFSET + PLAYER_BBOX_SIZE;
            int py1 = (int)g_players[pi].pos_y + PLAYER_BBOX_OFFSET;
            int py2 = (int)g_players[pi].pos_y + PLAYER_BBOX_OFFSET + PLAYER_BBOX_SIZE;

            if (ax1 < px2 && ax2 > px1 && ay1 < py2 && ay2 > py1) {
                /* Award player credits and score for the first contact.
                 * Ref: add.l #1500,PLAYER_CREDITS / add.l #100,PLAYER_SCORE
                 *      @ main.asm#L7640-L7641. */
                g_players[pi].credits += 1500;
                g_players[pi].score   += 100;

                /* Apply damage: 36(a2)*2 = 1*2 = 2 HP.
                 * Ref: move.w 36(a2),d5 / add.w d5,d5 / sub.w d5,PLAYER_HEALTH
                 *      @ main.asm#L7650-L7652. */
                player_take_damage(&g_players[pi], 2);

                /* Transition the alien to its dying state so it plays the
                 * explosion animation and is removed from future collision
                 * checks.  Mirrors the lbC00A582 "touches player" branch that
                 * eventually calls set_alien_default_vars @ main.asm#L7280. */
                alien_kill(ai);

                break;  /* alien is now dying; stop checking other players */
            }
        }
    }
}

void alien_kill(int i)
{
    if (i < 0 || i >= g_alien_count) return;
    /* Transition to dying state: play 16-frame explosion then disappear.
     * Ref: alien_dies @ main.asm#L7308; death anim lbL018C2E#L13907. */
    g_aliens[i].alive       = 2;
    g_aliens[i].death_frame = 0;
    /* Play alien death sound (not hatching sound) */
    audio_play_sample(SAMPLE_DYING_PLAYER);  /* closest match for alien death */
    
    /* Track kill for INTEX stats (Ref: run_intex @ main.asm#L8975) */
    /* Award kill to all players (credit split between shooting/damaged) */
    for (int p = 0; p < MAX_PLAYERS; p++) {
        if (g_players[p].alive)
            g_players[p].aliens_killed++;
    }
}

int alien_living_count(void)
{
    int n = 0;
    for (int i = 0; i < g_alien_count; i++)
        if (g_aliens[i].alive == 1) n++;
    return n;
}

void projectiles_render(void)
{
    extern int g_camera_x, g_camera_y;
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        if (!s_projectiles[i].active) continue;
        int sx = s_projectiles[i].x - g_camera_x;
        int sy = s_projectiles[i].y - g_camera_y;
        if (sx < -4 || sx > 324 || sy < -4 || sy > 260) continue;
        /* Draw a 3×3 yellow pixel dot for the bullet (color 1 = first non-black) */
        video_fill_rect(sx - 1, sy - 1, 3, 3, 1);
    }
}
