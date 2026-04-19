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
    int   alive;
    int   cur_sprite;
    int   anim_counter;
    int   direction;   /* towards player */
    /* Pathfinding state */
    int   target_x, target_y;
} Alien;

extern Alien g_aliens[MAX_ALIENS];
extern int   g_alien_count;

/* Extra strength added to all aliens (increases with level number) */
extern WORD  g_global_aliens_extra_strength;

/* Initialise alien array. */
void alien_init_variables(void);

/* Spawn aliens from hatch tiles in the current map. */
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

/* Returns number of living aliens. */
int  alien_living_count(void);

/* Spawn a player projectile at world position (x,y) moving at (vx,vy).
 * player_idx identifies the shooter for score/collision purposes. */
void alien_spawn_projectile(int player_idx, WORD x, WORD y,
                            WORD vx, WORD vy, WORD strength);

/* Draw all active projectiles. */
void projectiles_render(void);

#endif /* AB_ALIEN_H */
