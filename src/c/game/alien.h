#ifndef AB_ALIEN_H
#define AB_ALIEN_H

/*
 * Alien Breed SE 92 - C port
 * Alien module — translated from main.asm alien_* functions.
 */

#include "../types.h"
#include "../game/constants.h"

#define MAX_ALIENS 64

typedef struct {
    WORD  pos_x, pos_y;
    WORD  speed;
    WORD  strength;
    /* alive: 1 = walking/active, 2 = dying (explosion playing), 0 = dead */
    int   alive;
    int   type_idx;      /* 0-based alien type (0=alien1…6=alien7) for stats/HP */
    int   cur_sprite;
    int   anim_counter;
    /* direction: 0=N 1=NE 2=E 3=SE 4=S 5=SW 6=W 7=NW — atlas column index
     * (Ref: lbB00A228 direction table @ main.asm#L7077) */
    int   direction;
    int   death_frame;   /* 0-15 during death explosion animation */
    /* Pathfinding state */
    int   target_x, target_y;
} Alien;

extern Alien g_aliens[MAX_ALIENS];
extern int   g_alien_count;

/* Extra strength added to all aliens (increases with level number) */
extern WORD  g_global_aliens_extra_strength;

/* Initialise alien array. */
void alien_init_variables(void);

/* Spawn aliens from hatch tiles in the current map.
 * In the ASM, tiles 0x28/0x29 are per-step-on triggers (lbC00A718), not
 * deferred spawn points; this function only resets the spawn slot state. */
void alien_spawn_from_map(void);

/* Update all living aliens (movement + AI). */
void alien_update_all(void);

/* Check collisions between alien bounding boxes and player weapons.
 * Equivalent to aliens_collisions_with_weapons in main.asm. */
void aliens_collisions_with_weapons(void);

/* Check collisions between aliens and players.
 * Equivalent to aliens_collisions_with_players in main.asm. */
void aliens_collisions_with_players(void);

/* Kill an alien at index i (awards score, plays SFX). */
void alien_kill(int i);

/*
 * Probabilistic direct spawn triggered when the player steps on a tile with
 * attribute 0x28 (TILE_ALIEN_SPAWN_BIG) or 0x29 (TILE_ALIEN_SPAWN_SMALL).
 * Mirrors lbC004914 / lbC0049D6 → lbC00A718 @ main.asm#L2513-L2568:
 * ~7.8% chance per frame (rnd%256 < 20); NO hatching sound.
 */
void alien_try_spawn_at(int wx, int wy);



/*
 * Register a one-shot spawn point at (wx, wy).
 * Called when the player triggers a facehugger hatch tile (0x0A).
 * The alien will appear once the countdown expires (≈20 frames) — mirroring
 * the lbC00D22A → lbC00D1B4 deferred-spawn path @ main.asm#L5414.
 */
void alien_spawn_near(int wx, int wy);

/*
 * Per-frame viewport scan: check all registered spawn points against the
 * expanded viewport and spawn aliens when their countdown reaches zero.
 * Mirrors lbC00D17E / lbC00D1B4 @ main.asm called every game tick.
 * Called internally from alien_update_all().
 */
void alien_spawn_tick(void);

/* Returns number of living aliens. */
int  alien_living_count(void);

/* Spawn a player projectile at world position (x,y) moving at (vx,vy).
 * player_idx identifies the shooter for score/collision purposes. */
void alien_spawn_projectile(int player_idx, WORD x, WORD y,
                            WORD vx, WORD vy, WORD strength);

/* Draw all active projectiles. */
void projectiles_render(void);

#endif /* AB_ALIEN_H */
