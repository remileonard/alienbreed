#ifndef AB_CONSTANTS_H
#define AB_CONSTANTS_H

/*
 * Alien Breed SE 92 - C port
 * Game constants translated from src/common/common.inc
 */

/* ----------------------------------------------------- */
/* Debug */
/* ----------------------------------------------------- */
#define DEBUG 0

/* ----------------------------------------------------- */
/* Keyboard scancodes (SDL2 equivalents mapped in input.c) */
/* Original Amiga raw keycodes kept here for reference.  */
/* ----------------------------------------------------- */
#define KEY_P           0x19
#define KEY_M           0x37
#define KEY_SPACE       0x40
#define KEY_RETURN      0x44
#define KEY_ESC         0x45
#define KEY_UP          0x4c
#define KEY_DOWN        0x4d
#define KEY_RIGHT       0x4e
#define KEY_LEFT        0x4f
#define KEY_LEFT_ALT    0x64
#define KEY_RIGHT_ALT   0x65
#define KEY_D           0x22
#define KEY_F           0x23

/* ----------------------------------------------------- */
/* Player limits */
/* ----------------------------------------------------- */
#define PLAYER_MAX_AMMO     32
#define PLAYER_MAX_AMMOPCKS 4
#define PLAYER_MAX_HEALTH   64
#define PLAYER_START_LIVES  3

/* ----------------------------------------------------- */
/* Player structure field offsets (kept for cross-ref)   */
/* The actual C struct is in player.h                    */
/* ----------------------------------------------------- */
#define PLAYER_HW_REGISTER          0
#define PLAYER_WEAPON_SPEED         252
#define PLAYER_CUR_WEAPON           256
#define PLAYER_WEAPON_INDEX         258
#define PLAYER_WEAPON_RATE          260
#define PLAYER_WEAPON_STRENGTH      266
#define PLAYER_POS_X                300
#define PLAYER_POS_Y                302
#define PLAYER_ALIVE                316
#define PLAYER_CUR_SPRITE           320
#define PLAYER_HEALTH               336
#define PLAYER_LIVES                340
#define PLAYER_AMMOPACKS            344
#define PLAYER_AMMUNITIONS          348
#define PLAYER_KEYS                 352
#define PLAYER_CREDITS              356
#define PLAYER_WEAPON_RATE_COUNTER  360
#define PLAYER_EXTRA_SPD_X          376
#define PLAYER_EXTRA_SPD_Y          378
#define PLAYER_WEAPON_SMP           384
#define PLAYER_SHOT_AMOUNT_COUNTER  394
#define PLAYER_SHOT_AMOUNT          398
#define PLAYER_OWNEDWEAPONS         402
#define PLAYER_OLD_POS_X            428
#define PLAYER_OLD_POS_Y            432
#define PLAYER_SHOTS                436
#define PLAYER_SCORE                440

/* Projectile offsets */
#define PROJECTILE_STRENGTH         16
#define PROJECTILE_PLAYER           20

/* ----------------------------------------------------- */
/* Player facing directions */
/* ----------------------------------------------------- */
#define PLAYER_FACE_UP          1
#define PLAYER_FACE_UP_RIGHT    2
#define PLAYER_FACE_RIGHT       3
#define PLAYER_FACE_DOWN_RIGHT  4
#define PLAYER_FACE_DOWN        5
#define PLAYER_FACE_DOWN_LEFT   6
#define PLAYER_FACE_LEFT        7
#define PLAYER_FACE_UP_LEFT     8

/* ----------------------------------------------------- */
/* Weapons */
/* ----------------------------------------------------- */
#define WEAPON_MACHINEGUN   1
#define WEAPON_TWINFIRE     2
#define WEAPON_FLAMEARC     3
#define WEAPON_PLASMAGUN    4
#define WEAPON_FLAMETHROWER 5
#define WEAPON_SIDEWINDERS  6
#define WEAPON_LAZER        7
#define WEAPON_MAX          8

/* ----------------------------------------------------- */
/* Supplies (bit flags) */
/* ----------------------------------------------------- */
#define SUPPLY_MAP_OVERVIEW 1
#define SUPPLY_AMMO_CHARGE  2
#define SUPPLY_NRG_INJECT   4
#define SUPPLY_KEY_PACK     8
#define SUPPLY_EXTRA_LIFE   16

/* ----------------------------------------------------- */
/* Sample IDs (index into samples array) */
/* ----------------------------------------------------- */
#define SAMPLE_ONE_WAY_DOOR     5
#define SAMPLE_INTEX_SHUTDOWN   13
#define SAMPLE_CARET_MOVE       14
#define SAMPLE_DESTRUCT_IMM     18
#define SAMPLE_KEY              22
#define SAMPLE_OPENING_DOOR     23
#define SAMPLE_AMMO             24
#define SAMPLE_1UP              27
#define SAMPLE_1STAID_CREDS     30
#define SAMPLE_HURT_PLAYER      33
#define SAMPLE_ACID_POOL        34
#define SAMPLE_WATER_POOL       35
#define SAMPLE_HATCHING_ALIEN   36
#define SAMPLE_FIRE_GUN         37
#define SAMPLE_DESCENT          41
#define SAMPLE_DESCENT_END      42
#define SAMPLE_TYPE_WRITER      48
#define SAMPLE_DYING_PLAYER     73
#define SAMPLE_EMPTY            76

/* ----------------------------------------------------- */
/* Voice IDs */
/* ----------------------------------------------------- */
#define VOICE_WARNING       17
#define VOICE_DESTRUCT_IMM  18
#define VOICE_ENTERING      50
#define VOICE_ZONE          51
#define VOICE_WELCOME_TO    52
#define VOICE_INTEX_SYSTEM  53
#define VOICE_DEATH         54
#define VOICE_PLAYER        57
#define VOICE_REQUIRES      58
#define VOICE_AMMO          59
#define VOICE_FIRST_AID     60
#define VOICE_DANGER        61
#define VOICE_INSERT_DISK   62
#define VOICE_KEYS          63
#define VOICE_GAME_OVER     64
#define VOICE_ONE           65
#define VOICE_TWO           66
#define VOICE_THREE         67
#define VOICE_FOUR          68
#define VOICE_FIVE          69
#define VOICE_SIX           70
#define VOICE_SEVEN         71
#define VOICE_EIGHT         72

/* ----------------------------------------------------- */
/* Alien structure offsets */
/* ----------------------------------------------------- */
#define ALIEN_SPEED     10
#define ALIEN_POS_X     30
#define ALIEN_POS_Y     32
#define ALIEN_STRENGTH  60

/* ----------------------------------------------------- */
/* Tile attributes (bits 0-5 of tile word in map BODY)   */
/* ----------------------------------------------------- */
#define TILE_FLOOR          0x00
#define TILE_WALL           0x01
#define TILE_EXIT           0x02
#define TILE_DOOR           0x03
#define TILE_KEY            0x04
#define TILE_FIRST_AID      0x05
#define TILE_AMMO           0x06
#define TILE_1UP            0x07
#define TILE_FIRE_DOOR_A    0x08
#define TILE_FIRE_DOOR_B    0x09
/* 0x0A: player-triggered face-hugger hatch (tile_facehuggers_hatch @ main.asm#L5414).
 * When the player stands on this tile it spawns facehuggers nearby. */
#define TILE_FACEHUGGER_HATCH 0x0A
#define TILE_CREDITS_100    0x0B
#define TILE_CREDITS_1000   0x0C
/* 0x0D: metallic-grid floor — functionally walkable floor (tile_not_used in ASM).
 * Ref: tiles_action_table @ main.asm#L5059 (entry 0x0D → bra tile_not_used). */
#define TILE_METALLIC_FLOOR 0x0D
/* One-way conveyor tiles — push player in the named direction each frame.
 * Ref: tiles_action_table @ main.asm#L5073-L5076: entries 0x0E-0x11. */
#define TILE_ONEWAY_UP      0x0E  /* extra_spd_y = -2 (upward push)   */
#define TILE_ONEWAY_RIGHT   0x0F  /* extra_spd_x = +2 (rightward push) */
#define TILE_ONEWAY_DOWN    0x10  /* extra_spd_y = +2 (downward push)  */
#define TILE_ONEWAY_LEFT    0x11  /* extra_spd_x = -2 (leftward push)  */
#define TILE_DEADLY_HOLE    0x14  /* instant kill (tile_deadly_hole @ main.asm#L5485).
                                   * Was incorrectly 0x13 in earlier C port; corrected
                                   * to match tile_action_table entry 0x14 in main.asm. */
#define TILE_DESTRUCT_TRIGGER 0x15 /* starts self-destruct countdown */
#define TILE_ACID_POOL      0x16  /* -1 HP every 25 frames */
#define TILE_INTEX          0x17  /* INTEX terminal — activated by FIRE2 */
/* 0x26: one-way-right that kills the player if they try to go left.
 * 0x2E: one-way-left that kills the player if they try to go right.
 * Ref: tile_one_deadly_way_right/left @ main.asm#L5600-L5630. */
#define TILE_ONE_DEADLY_WAY_RIGHT 0x26
#define TILE_CLIMB_LEFT     0x27  /* extra_spd_x = +1 (slows leftward movement)  */
/* Alien spawn tiles (Ref: levelmaps_format.txt) */
#define TILE_ALIEN_SPAWN_BIG   0x28  /* respawning location of big aliens */
#define TILE_ALIEN_SPAWN_SMALL 0x29  /* respawning location of small aliens */
#define TILE_ONE_DEADLY_WAY_LEFT  0x2E
#define TILE_CLIMB_RIGHT    0x2F  /* extra_spd_x = -1 (slows rightward movement) */
#define TILE_ALIEN_HOLE        0x34  /* hole with aliens coming out */
#define TILE_CLIMB_UP       0x37  /* extra_spd_y = +1 (slows upward movement)   */
/* Diagonal one-way tiles (push in two directions simultaneously).
 * Ref: tile_one_way_up_right/down_right/down_left/up_left @ main.asm#L5818-L5852. */
#define TILE_ONEWAY_DIAG_UR 0x38  /* extra_spd_x=+2, extra_spd_y=-2 */
#define TILE_ONEWAY_DIAG_DR 0x39  /* extra_spd_x=+2, extra_spd_y=+2 */
#define TILE_ONEWAY_DIAG_DL 0x3A  /* extra_spd_x=-2, extra_spd_y=+2 */
#define TILE_ONEWAY_DIAG_UL 0x3B  /* extra_spd_x=-2, extra_spd_y=-2 */
#define TILE_BOSS_TRIGGER      0x3D  /* triggers boss encounter */
#define TILE_CLIMB_DOWN     0x3F  /* extra_spd_y = -1 (slows downward movement)  */

/* Convenience: true if tile applies a directional push (one-way or climb).
 * Ref: tiles_action_table entry range @ main.asm#L5059. */
#define TILE_IS_ONEWAY(a)   (((a) >= TILE_ONEWAY_UP && (a) <= TILE_ONEWAY_LEFT) || \
                             (a) == TILE_ONE_DEADLY_WAY_RIGHT || \
                             (a) == TILE_ONE_DEADLY_WAY_LEFT  || \
                             (a) == TILE_CLIMB_LEFT  || (a) == TILE_CLIMB_RIGHT || \
                             (a) == TILE_CLIMB_UP    || (a) == TILE_CLIMB_DOWN  || \
                             ((a) >= TILE_ONEWAY_DIAG_UR && (a) <= TILE_ONEWAY_DIAG_UL))

/* Tile attribute mask */
#define TILE_ATTR_MASK      0x3F

/* ----------------------------------------------------- */
/* Joystick input bitmask (as in user_input routine)     */
/* Bit positions in the input byte/word.                 */
/* ----------------------------------------------------- */
#define INPUT_UP        (1 << 0)
#define INPUT_DOWN      (1 << 1)
#define INPUT_LEFT      (1 << 2)
#define INPUT_RIGHT     (1 << 3)
#define INPUT_PAUSE     (1 << 4)  /* Start / Select */
#define INPUT_NEXT_WPN  (1 << 5)  /* Weapon cycle */
#define INPUT_FIRE2     (1 << 7)  /* Second fire button */
#define INPUT_FIRE1     (1 << 6)  /* Primary fire */

/* ----------------------------------------------------- */
/* Number of levels */
/* ----------------------------------------------------- */
#define NUM_LEVELS 12

/* ----------------------------------------------------- */
/* Fade speed (frames between palette steps)             */
/* ----------------------------------------------------- */
#define FADE_SPEED 2

/* ----------------------------------------------------- */
/* Map dimensions (same for all levels per format doc)   */
/* XBLK * YBLK * 2 bytes = 23040 → 96 x 120             */
/* ----------------------------------------------------- */
#define MAP_WIDTH  96
#define MAP_HEIGHT 120

#endif /* AB_CONSTANTS_H */
