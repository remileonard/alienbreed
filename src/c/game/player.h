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
 * Directional probe offsets for player wall collision.
 *
 * The player's visible body occupies the inner 16×16 region of its sprite,
 * confirmed by the hit-box at [pos_x−8 … pos_x+8] × [pos_y−8 … pos_y+8]
 * (PLAYER_BBOX_OFFSET = −8, PLAYER_BBOX_SIZE = 16).
 *
 * All probe offsets are chosen so that, with 16-px tiles and integer-division
 * tile lookup (pixel / 16), the body edge stops exactly at the tile boundary
 * when the leading probe first enters a solid tile:
 *
 *   LEFT  probe at center − 8  → player stops with hitbox left  = wall right+1
 *   RIGHT probe at center + 7  → player stops with hitbox right = wall left
 *   UP    probe at center − 8  → player stops with hitbox top   = ceil bottom+1
 *   DOWN  probe at center + 7  → player stops with hitbox bottom= floor top
 *
 * The three perpendicular samples {−6, 0, +6} span the body width/height
 * while remaining within the ±8 body boundary.
 *
 * A fixed centre probe at (nx, ny) catches any solid tile at the player's
 * exact centre (safety net for corner cases).
 */

/* X offset of the probe column for left/right movement */
#define PROBE_LEFT_X   (-8)    /* 1 probe at hitbox left edge  (body: −8 … +8) */
#define PROBE_RIGHT_X  (7)     /* 1 probe at hitbox right edge (body: −8 … +8) */

/* Y offset of the probe row for up/down movement */
#define PROBE_UP_Y     (-8)    /* 1 probe at hitbox top edge   (body: −8 … +8) */
#define PROBE_DOWN_Y   (7)     /* 1 probe at hitbox bottom edge(body: −8 … +8) */

/* Three y-sample offsets used when probing left or right */
static const int k_probe_hy[3] = { -6, 0, 6 };   /* spans body height ±6 ⊆ ±8 */
/* Three x-sample offsets used when probing up or down */
static const int k_probe_vx[3] = { -6, 0, 6 };   /* spans body width  ±6 ⊆ ±8 */

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

    /* INTEX tool supply purchases (persists across terminal visits).
     * Bit i = 1 means item i (0=scanner,1=ammo,2=energy,3=key,4=life) was bought.
     * Ref: purchased_supplies dc.l 0 @ intex.asm#L547 */
    int   purchased_supplies;

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

/* Open the door at the specified tile (col, row) and its paired neighbour tile,
 * consuming one key from p.  Used when a projectile destroys a door (force_door
 * path) so that the correct tile is patched rather than the player's position.
 * Ref: lbC00E56C-lbC00E574 + force_door @ main.asm#L9697-L9700. */
void open_door_at(Player *p, int col, int row);

/* Invincibility frame counter per player */
extern int g_player_invincibility[MAX_PLAYERS];

#endif /* AB_PLAYER_H */
