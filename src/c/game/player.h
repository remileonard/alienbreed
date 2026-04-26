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

/*
 * Directional probe offsets from the ASM probe tables for the player.
 *
 * The original game stores four probe tables in main.asm:
 *   lbW007B16 (LEFT):  dc.w -4,-4,-4,-6,4,16   → x=-4,  y={-6,4,16}
 *   lbW007B22 (RIGHT): dc.w 30,30,30,-6,4,16   → x=+30, y={-6,4,16}
 *   lbW007B2E (UP):    dc.w 0,10,22,-10,-10,-10 → x={0,10,22}, y=-10
 *   lbW007B3A (DOWN):  dc.w 0,10,22,20,20,20   → x={0,10,22}, y=+20
 * Plus a fixed 4th centre probe at (pos_x+10, pos_y+6) added inline.
 *
 * These are the SAME offsets as the regular alien probe tables
 * (lbW00A2CA/D6/E2/EE) — the player and small alien share a 32×32 bbox.
 * No orientation rotation applies; probes are purely direction-of-movement.
 * The C predictive approach checks the proposed position (nx/ny) just as
 * the ASM checks the current position before applying speed.
 */

/* X offset of the probe column for left/right movement */
#define PROBE_LEFT_X   (-4)
#define PROBE_RIGHT_X  (30)

/* Y offset of the probe row for up/down movement */
#define PROBE_UP_Y     (-10)
#define PROBE_DOWN_Y   (20)

/* Three y-sample offsets used when probing left or right (lbW007B16/22) */
static const int k_probe_hy[3] = { -6, 4, 16 };
/* Three x-sample offsets used when probing up or down (lbW007B2E/3A) */
static const int k_probe_vx[3] = { 0, 10, 22 };

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

    /* Death animation counter (equivalent to 280(a0) in main.asm).
     * Set to PLAYER_DEATH_FRAMES when health reaches 0; counts down each frame.
     * While > 0 the player is shown as an explosion and cannot move or take damage.
     * When it reaches 0: respawn with full health (if lives > 0) or set alive = 0.
     * Ref: lbC006C7A / lbC0077DC @ main.asm#L3934-L4046. */
    int   death_counter;

    /* Input (references to global input state) */
    int   port;           /* 0 = player 1, 1 = player 2 */
} Player;

/* Number of frames the player death explosion animation plays before respawn.
 * The original ASM counter is 200 frames; we use a shorter value that still
 * gives a visible explosion (≈2 full cycles through the 16-frame atlas at
 * the rate one atlas-frame advances per rendered frame).
 * Ref: move.w #200,lbW005D64 @ main.asm#L3938. */
#define PLAYER_DEATH_FRAMES  32

/* Player hit-box offset and size relative to pos_x/pos_y.
 *
 * In the C port pos_x/pos_y is the *centre* of the 32×32 player sprite
 * (the sprite is blitted at x-w/2, y-h/2).  The ASM origin is the sprite
 * top-left, and the hit box is the inner 16×16 starting 8 px from that
 * top-left (add.l #$80008 / add.l #$100010 @ main.asm#L7600-L7603).
 *
 * Converting to C centre-origin:
 *   ASM top-left + 8 = sprite_centre − 16 + 8 = sprite_centre − 8
 *
 * So the hit box spans [pos_x−8 … pos_x+8] × [pos_y−8 … pos_y+8]. */
#define PLAYER_BBOX_OFFSET  (-8)
#define PLAYER_BBOX_SIZE     16

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
