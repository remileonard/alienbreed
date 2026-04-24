#ifndef AB_PLAYER_H
#define AB_PLAYER_H

/*
 * Alien Breed SE 92 - C port
 * Player module — translated from main.asm (player_* functions) and
 * constants from common.inc.
 *
 * Player structure fields directly mirror the offsets in common.inc.
 */

#include "../types.h"
#include "../game/constants.h"

/* Number of players supported */
#define MAX_PLAYERS 2

/* Owned weapons bitmask size */
#define OWNED_WEAPONS_SIZE 8

typedef struct {
    /* Position (pixels, fixed-point ×1) */
    WORD  pos_x;
    WORD  pos_y;
    WORD  old_pos_x;
    WORD  old_pos_y;

    /* Extra velocity applied (e.g. knockback) */
    WORD  extra_spd_x;
    WORD  extra_spd_y;

    /* Facing direction (PLAYER_FACE_*) */
    int   direction;

    /* Status */
    int   alive;
    WORD  health;         /* 0–PLAYER_MAX_HEALTH */
    WORD  lives;
    WORD  keys;
    LONG  credits;
    LONG  score;
    WORD  shots;          /* total shots fired */
    WORD  aliens_killed;  /* for INTEX stats (Ref: run_intex @ main.asm#L8975) */

    /* Ammo */
    WORD  ammopacks;      /* PLAYER_MAX_AMMOPCKS */
    WORD  ammunitions;    /* current ammo count */

    /* Weapons */
    WORD  cur_weapon;     /* WEAPON_* */
    WORD  weapon_index;
    WORD  weapon_speed;
    WORD  weapon_rate;
    WORD  weapon_rate_counter;
    WORD  weapon_strength;
    WORD  weapon_smp;     /* sound sample to play when firing */
    WORD  shot_amount;
    WORD  shot_amount_counter;
    UBYTE owned_weapons[OWNED_WEAPONS_SIZE];

    /* Animation — mirrors ASM player data walk/fire/respawn state machine.
     * Ref: lbC006E96..lbC006FD0 @ main.asm#L4042-L4114 */
    int   cur_sprite;          /* body-orientation index 1-8 (ref: PLAYER_CUR_SPRITE / 320(a0)) */
    int   anim_flipflop;       /* toggles each frame while moving to gate walk-cycle advance (ref: 288(a0)) */
    int   anim_fire_counter;   /* countdown from 5 on alien hit; drives hit/fire anim (ref: 292(a0)) */
    int   anim_state;          /* computed state 0-35 (0-8=idle/walk, 9-17=fire, 18-26=hit, 27-35=respawn) (ref: 296(a0)) */
    int   anim_seq_frame;      /* current frame index within the walk / fire cycle */
    int   anim_seq_timer;      /* ticks remaining before advancing to the next frame */
    int   anim_seq_id;         /* opaque id of the current sequence; reset triggers frame restart */

    /* Input (references to global input state) */
    int   port;           /* 0 = player 1, 1 = player 2 */
} Player;

extern Player g_players[MAX_PLAYERS];

/* Initialise both player structures to default state. */
void player_init_variables(void);

/* Set the starting weapon for a player. */
void player_set_cur_weapon(Player *p, int weapon_id);

/* Set starting positions from level data. */
void player_set_starting_positions(void);

/* Update player movement and shooting for one frame.
 * input_mask : bitmask from input_poll() for this player's port. */
void player_update(Player *p, UWORD input_mask);

/* Apply damage to player. Triggers invincibility frames. */
void player_take_damage(Player *p, int amount);

/* Check if player has invincibility frames active. */
int  player_is_invincible(const Player *p);

/* Give player a pickup item (SUPPLY_* flags). */
void player_collect_supply(Player *p, int supply_flags);

/* Advance to next weapon (wraps around owned weapons). */
void player_next_weapon(Player *p);

/* Returns the input bitmask for this player from g_player*_input. */
UWORD player_get_input(const Player *p);

/* Check and handle tile interaction at player's current position.
 * Called from player_update() after each movement. */
void check_tile_interaction(Player *p);

/* Open a door: scan 4 adjacent tiles for TILE_DOOR, patch them, decrement keys.
 * Called when player steps on or interacts with a door tile. */
void open_door(Player *p);

/* Invincibility frame counter per player */
extern int g_player_invincibility[MAX_PLAYERS];

#endif /* AB_PLAYER_H */
