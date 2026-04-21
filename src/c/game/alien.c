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

void alien_init_variables(void)
{
    memset(g_aliens,       0, sizeof(g_aliens));
    memset(s_projectiles,  0, sizeof(s_projectiles));
    g_alien_count = 0;
}

/*
 * Maximum number of aliens placed on the map at level start.
 * The original assembly uses 7 fixed alien slots (alien1_struct..alien7_struct)
 * that spawn near the player on demand.  We use a small initial cap to keep
 * the same rough density while letting all spawn-tile types be covered.
 * (Ref: alien1_struct..alien7_struct @ main.asm#L5918-L5973)
 */
#define ALIEN_INITIAL_CAP 10

void alien_spawn_from_map(void)
{
    /* Select alien type based on level number (Ref: main.asm#L5918+).
     * Later levels use stronger alien types. */
    int alien_type = 1;  /* default */
    if (g_cur_level >= 6) alien_type = 2;
    if (g_cur_level >= 8) alien_type = 3;
    if (g_cur_level >= 10) alien_type = 4;
    if (g_cur_level == 11) alien_type = 5;
    if (g_cur_level == 12) alien_type = 6;
    if (g_cur_level >= 12) alien_type = 7;

    WORD base_hp = (alien_type >= 1 && alien_type <= 7)
                   ? k_alien_type_hp[alien_type - 1]
                   : k_alien_type_hp[0];

    /*
     * Collect all spawn-tile positions first so we can spread initial aliens
     * evenly across the map rather than clustering them at the top-left.
     *
     * Tile attributes that mark alien spawn locations
     * (Ref: levelmaps_format.txt, verified by scanning all LxMA files):
     *   0x28 – respawning location of big aliens
     *   0x29 – respawning location of small aliens
     *   0x34 – hole with aliens coming out
     *
     * NOTE: 0x0A (face-hugger hatch) is a *player-triggered* event tile, NOT a
     * static spawn point — no LxMA map contains 0x0A tiles in the BODY chunk.
     */
    typedef struct { WORD x, y; } SpawnPt;
    SpawnPt pts[MAP_ROWS * MAP_COLS];
    int     n_pts = 0;

    for (int row = 0; row < MAP_ROWS; row++) {
        for (int col = 0; col < MAP_COLS; col++) {
            UBYTE attr = tilemap_attr(&g_cur_map, col, row);
            if (attr == TILE_ALIEN_SPAWN_BIG  ||
                attr == TILE_ALIEN_SPAWN_SMALL ||
                attr == TILE_ALIEN_HOLE) {
                pts[n_pts].x = (WORD)(col * MAP_TILE_W + MAP_TILE_W / 2);
                pts[n_pts].y = (WORD)(row * MAP_TILE_H + MAP_TILE_H / 2);
                n_pts++;
            }
        }
    }

    if (n_pts == 0) return;

    /* The original game spawns aliens lazily: a spawn tile only activates when
     * it enters the player's viewport (lbC00D17E / lbC00D1B4 @ main.asm).
     * In the C port we spawn at level-load time, so we pick the spawn tiles
     * NEAREST to the player's starting position.  This ensures aliens are
     * visible (or close to visible) from the start instead of being clustered
     * at the far end of the map. */
    int px = (int)g_players[0].pos_x;
    int py = (int)g_players[0].pos_y;

    /* Partial insertion sort: keep only ALIEN_INITIAL_CAP nearest entries. */
    int cap = ALIEN_INITIAL_CAP;
    if (cap > MAX_ALIENS) cap = MAX_ALIENS;
    if (cap > n_pts)      cap = n_pts;

    /* Compute Manhattan distances and sort the first `cap` entries by distance
     * (selection sort — n_pts is at most a few hundred, so O(n*cap) is fine). */
    for (int i = 0; i < cap; i++) {
        int best = i;
        int best_d = (int)(pts[i].x >= px ? pts[i].x - px : px - pts[i].x) +
                     (int)(pts[i].y >= py ? pts[i].y - py : py - pts[i].y);
        for (int j = i + 1; j < n_pts; j++) {
            int d = (int)(pts[j].x >= px ? pts[j].x - px : px - pts[j].x) +
                    (int)(pts[j].y >= py ? pts[j].y - py : py - pts[j].y);
            if (d < best_d) { best_d = d; best = j; }
        }
        if (best != i) {
            SpawnPt tmp = pts[i];
            pts[i] = pts[best];
            pts[best] = tmp;
        }
    }

    for (int i = 0; i < cap; i++) {
        if (g_alien_count >= MAX_ALIENS) break;
        const SpawnPt *sp = &pts[i];
        Alien *a    = &g_aliens[g_alien_count++];
        a->pos_x    = sp->x;
        a->pos_y    = sp->y;
        a->speed    = (WORD)(2 + g_global_aliens_extra_strength / 5);
        a->strength = (WORD)(base_hp + g_global_aliens_extra_strength);
        a->alive    = 1;
        a->type_idx = alien_type - 1;
    }
}

/*
 * Spawn one alien at world-pixel position (wx, wy).
 * Called when the player steps on a TILE_FACEHUGGER_HATCH (0x0A) tile, which
 * triggers a hatching event.  The original game deferred spawning until the
 * player was nearby (lbC00D22A / lbC00D1B4); here we spawn immediately since
 * the player is already on the tile.
 * Ref: tile_facehuggers_hatch → lbC0082C0/CE/8302 → lbC00D22A @ main.asm#L5414.
 */
void alien_spawn_near(int wx, int wy)
{
    if (g_alien_count >= MAX_ALIENS) return;

    int alien_type = 1;
    if (g_cur_level >= 6)  alien_type = 2;
    if (g_cur_level >= 8)  alien_type = 3;
    if (g_cur_level >= 10) alien_type = 4;
    if (g_cur_level == 11) alien_type = 5;
    if (g_cur_level >= 12) alien_type = 7;

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
    audio_play_sample(SAMPLE_HATCHING_ALIEN);
}

/* Simple Manhattan-distance AI: move alien one step toward nearest player.
 * Updates alien direction (0=N,1=NE,2=E,3=SE,4=S,5=SW,6=W,7=NW).
 * Ref: alien movement @ main.asm#L6458; direction table lbB00A228 @ main.asm#L7077. */
static void alien_move(Alien *a)
{
    /* Find nearest living player */
    int best_dist = 0x7FFFFFFF;
    int tx = a->pos_x, ty = a->pos_y;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!g_players[i].alive) continue;
        int dx = g_players[i].pos_x - a->pos_x;
        int dy = g_players[i].pos_y - a->pos_y;
        int d  = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
        if (d < best_dist) { best_dist = d; tx = g_players[i].pos_x; ty = g_players[i].pos_y; }
    }

    int dx = (tx > a->pos_x) ? a->speed : (tx < a->pos_x) ? -a->speed : 0;
    int dy = (ty > a->pos_y) ? a->speed : (ty < a->pos_y) ? -a->speed : 0;

    /* Wall check */
    int nx = a->pos_x + dx;
    int ny = a->pos_y + dy;
    int col = tilemap_pixel_to_col(nx);
    int row = tilemap_pixel_to_row(ny);
    if (!tilemap_is_solid(&g_cur_map, col, row)) {
        a->pos_x = (WORD)nx;
        a->pos_y = (WORD)ny;
    }

    /* Update compass direction from movement vector (Ref: lbB00A228 @ main.asm#L7077).
     * Directions: 0=N 1=NE 2=E 3=SE 4=S 5=SW 6=W 7=NW */
    if (dx == 0 && dy == 0) {
        /* No movement — keep current direction */
    } else if (dy < 0) {
        if      (dx > 0) a->direction = 1;  /* NE */
        else if (dx < 0) a->direction = 7;  /* NW */
        else             a->direction = 0;  /* N  */
    } else if (dy > 0) {
        if      (dx > 0) a->direction = 3;  /* SE */
        else if (dx < 0) a->direction = 5;  /* SW */
        else             a->direction = 4;  /* S  */
    } else {
        /* dy == 0, horizontal only */
        if (dx > 0) a->direction = 2;  /* E  */
        else        a->direction = 6;  /* W  */
    }
}

void alien_update_all(void)
{
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
        if (g_aliens[ai].alive != 1) continue;

        for (int pi = 0; pi < MAX_PLAYERS; pi++) {
            if (!g_players[pi].alive) continue;
            if (player_is_invincible(&g_players[pi])) continue;

            int dx = g_aliens[ai].pos_x - g_players[pi].pos_x;
            int dy = g_aliens[ai].pos_y - g_players[pi].pos_y;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;

            if (dx < 10 && dy < 10) {
                player_take_damage(&g_players[pi], 2);
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
