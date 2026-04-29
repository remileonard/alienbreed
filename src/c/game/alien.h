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

/* Spawn aliens from spawn tiles (0x28/0x29) found in the current map.
 * Scans the map at level load and registers deferred spawn points.
 * Ref: lbC0049EA/lbC004A18/lbC004A28 → lbC00D22A → lbC00D236 @ main.asm. */
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
 * player_idx    : identifies the shooter for score/collision purposes.
 * weapon_type   : WEAPON_* constant for behaviour selection and rendering.
 * penetrating   : 1 = bullet passes through aliens (PLASMAGUN/FLAMETHROWER/LAZER).
 * lifetime      : auto-expire after this many 25 Hz ticks (-1 = infinite).
 * bounce_count  : remaining wall bounces (FLAMEARC=1, LAZER=5, others=0).
 * direction     : firing direction 1-8 (PLAYER_FACE_*) for sprite selection.
 * Ref: lbC00E178/lbC00E21E @ main.asm#L9431-L9511. */
void alien_spawn_projectile(int player_idx, WORD x, WORD y,
                            WORD vx, WORD vy, WORD strength,
                            int weapon_type, int penetrating,
                            int lifetime, int bounce_count, int direction);

/*
 * Flamethrower in-flight lifetime in 25 Hz ticks.
 * Matches the 8-entry lbL018D06 animation list (delay=1 each) which gives
 * 8 ticks × 8 px/tick = 64 pixels of range.
 * Ref: lbL018D06 @ main.asm#L13935.
 */
#define FLAME_LIFETIME_TICKS 8

/* Draw all active projectiles. */
void projectiles_render(void);

#endif /* AB_ALIEN_H */
