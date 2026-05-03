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
    /* Hit flash: set to 1 by aliens_collisions_with_weapons when the alien
     * takes damage; cleared by the render loop after drawing the ALT WALK
     * sprite (y=96) for one frame.
     * Mirrors ASM offset 50(a0) used in lbC009B80 @ main.asm#L6675. */
    int   hit_flag;
    /* Hatch animation counter — set to 20 when an alien spawns from a
     * TILE_ALIEN_HOLE (0x34) tile.  While > 12, the zoom-in animation is
     * rendered (frames 0-2 at atlas x=288, y=288/320/352); after that the
     * alien shows the normal walk frame until the timer reaches 0.
     * Mirrors ASM alien+76 set by do_alien_hatch (move.w 46(a1),76(a0),
     * struct+46 = $14 = 20) and decremented by lbC00A568 each tick while
     * the AI is in the "hatching" state. */
    int   hatch_timer;
    /* Face hugger flag — set to 1 when this alien is a small face hugger
     * (spawned from TILE_ALIEN_SPAWN_SMALL (0x29) or from the facehugger
     * hatch tile on level 12, which selects lbW009414 with size 4,4,8,8).
     * Face huggers use 16×16 sprites from atlas x=256-304, y=0-80 rather
     * than the 32×30 large-alien sprites.
     * Ref: lbW009414 / lbL00969C @ main.asm#L6059-L6168; tile dispatch
     * lbC0049D6 (tile 0x29 → lbW008FD4) and lbC004A28 (lbW009414). */
    int   is_facehugger;
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
 * lbL018D06 has 8 entries with delay=1 each. delay=1 means each frame is
 * held for delay+1 = 2 ticks (compare FLAMEARC delay=0 = 1 tick/frame).
 * Total lifetime = 8 frames × 2 ticks = 16 ticks → range = 16 × 8 px = 128 px.
 * With fire rate=3 (~4 ticks between shots) this gives 4 simultaneous bullets,
 * each at a different animation stage, creating the continuous stream effect.
 * Ref: lbL018D06 @ main.asm#L13935.
 */
#define FLAME_LIFETIME_TICKS 16

/*
 * Hatch animation timer initial value for TILE_ALIEN_HOLE (0x34) spawns.
 * Mirrors do_alien_hatch @ main.asm#L7817: `move.w 46(a1),76(a0)` with
 * lbW008F94[46] = $14 = 20.  While hatch_timer > HATCH_ANIM_WALK_THRESHOLD
 * (12) the 3-frame zoom-in animation is shown; from 12 down to 0 the alien
 * displays the normal walk frame but remains stationary.
 * Ref: lbC00A568 / lbC00987E @ main.asm#L7272-L7278.
 */
#define HATCH_ANIM_TIMER_INIT    20
#define HATCH_ANIM_WALK_THRESHOLD 12

/* Draw all active projectiles. */
void projectiles_render(void);

#endif /* AB_ALIEN_H */
