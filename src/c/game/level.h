#ifndef AB_LEVEL_H
#define AB_LEVEL_H

/*
 * Alien Breed SE 92 - C port
 * Level module — translated from main.asm init_level_* functions.
 *
 * Each level follows the same sequence (per main.asm):
 *   install_level_tune → display_briefing_N → init_level_N →
 *   hold_briefing_screen → copy_gfx → set_destruction_timer →
 *   init_level_variables → init_players_variables →
 *   init_aliens_variables → finalize_level → game_level_loop
 */

#include "../types.h"

/* Total number of levels */
#define NUM_LEVELS 12

/* Level map filenames for each sub-map type */
typedef struct {
    const char *map_an;   /* aliens normal */
    const char *map_bo;   /* boss */
    const char *map_ma;   /* main */
    int   alien_extra_strength;
    const char *briefing_text;
    const char *music;    /* "level" or "boss" */
    /* Atlas layout type for alien sprites (Ref: main.asm lbL000554 per level).
     * ALIEN_ATLAS_COMPACT: y = frame*32 (most levels).
     * ALIEN_ATLAS_LEGACY : y = {0, 96, 128} (L0BO / levels 1, 10, 11). */
    int   atlas_type;
    /*
     * Bitmask of ship-engine tile attributes that are animated on this level.
     * Bit N = 1 means tile attribute (0x18 + N) should be animated.
     * Derived from the per-level tile dispatch table at lbC004384 + level_flag
     * (see main.asm).  A tile whose dispatch entry is `bra.w none` or `rts`
     * is NOT animated and its bit is cleared here.
     *
     *   bit 0 → tile 0x18   bit 1 → tile 0x19   bit 2 → tile 0x1A
     *   bit 3 → tile 0x1B   bit 4 → tile 0x1C
     */
    int   engine_tile_mask;
    /*
     * Self-destruct countdown duration in seconds, initialised from
     * timer_digit_hi:timer_digit_lo in each init_level_N in main.asm
     * (e.g. hi=6,lo=0 → 60 s).  0 means no countdown is defined for this
     * level (the exit is open from the start and self-destruct is never
     * triggered via the normal path).
     */
    int   timer_seconds;
} LevelDef;

extern const LevelDef k_level_defs[NUM_LEVELS];

/* Currently active level index (0-based) */
extern int  g_cur_level;

/* Level state */
extern int  g_flag_end_level;
extern int  g_flag_jump_to_gameover;
extern int  g_in_destruction_sequence;
extern int  g_flag_destruct_level;
extern LONG g_destruction_timer;     /* frames remaining */
extern int  g_self_destruct_initiated;
extern int  g_map_overview_on;
extern int  g_game_running_flag;
extern int  g_exit_unlocked;         /* 1 = exit tile is passable (Ref: main.asm#L5189) */
extern int  g_boss_active;           /* 1 while a boss encounter is in progress */
/* When >= 0, jump to this level index at next level transition (enter_level_N_holocode in main.asm). */
extern int  g_holocode_jump_level;

/* -----------------------------------------------------------------
 * Projectile-environment interaction state
 * ----------------------------------------------------------------- */

/*
 * Reactor hit counters — one per face (up=0x2a, left=0x2b, down=0x2c, right=0x2d).
 * Each face requires 6 projectile hits to "blow out".  When all 4 faces are
 * blown (all counters == 6), self-destruct is triggered.
 * Mirrors reactor_up_done / reactor_left_done / reactor_down_done / reactor_right_done
 * @ main.asm#L9661-L9664, reset in reset_game_variables @ L9747.
 */
extern int  g_reactor_up_done;
extern int  g_reactor_left_done;
extern int  g_reactor_down_done;
extern int  g_reactor_right_done;

/*
 * Door-impact accumulator (impact_on_door @ main.asm#L9669).
 * Tracks accumulated projectile damage to a specific door tile.
 * When the total reaches 300, the door is force-opened by temporarily
 * giving the firing player one key and calling force_door.
 *
 * g_door_impact_col/row: which door tile is being damaged (changed when
 *   a projectile hits a different door, resetting the counter — mirrors
 *   the lbL00E4EC / clr.w door_impact pattern @ main.asm#L9674-L9678).
 * g_door_impact_accum  : accumulated weapon strength on the current tile.
 */
extern int  g_door_impact_accum;
extern int  g_door_impact_col;
extern int  g_door_impact_row;

/*
 * Alarm-system state (level 3 specific — lbW002AC2 @ main.asm#L1053).
 * g_alarm_system_active : 1 if the alarm system is armed (only level 3).
 * g_alarm_buttons_pressed: count of distinct alarm buttons hit (lbW002AC0).
 *   When this reaches 3 and g_alarm_system_active==1, self-destruct fires.
 * g_alarm_last_col/row  : tile coords of the last alarm button hit, to
 *   prevent counting the same button twice (mirrors lbL00E756 @ main.asm#L9822).
 */
extern int  g_alarm_system_active;
extern int  g_alarm_buttons_pressed;
extern int  g_alarm_last_col;
extern int  g_alarm_last_row;

/* Initialise level-specific variables (destruction timer, flags). */
void level_init_variables(void);

/* Load and start level N (0-based). Runs the full sequence. */
void level_run(int level_idx);

/* Start the self-destruct countdown (Ref: set_destruction_timer @ main.asm#L1257). */
void level_start_destruction(void);

/* Check if the level destruction sequence should trigger.
 * Called each frame in game_level_loop. */
void level_check_destruction(void);

/* Check if game over condition is met. */
void level_check_gameover(void);

/* Called when the exit tile is reached. */
void level_trigger_end(void);

/* Tick the destruction countdown timer. */
void level_tick_timer(void);

/* Reset the internal per-second tick counter (call when starting a level). */
void level_tick_counter_reset(void);

/*
 * Run the final level-destruction explosion cinematic.
 * Mirrors do_level_destruction @ main.asm#L9155:
 *   – camera shake (4 passes with increasing amplitude)
 *   – palette flash to white
 *   – 150 frames of random explosion sprites across the map
 *   – fade to black
 * Leaves g_flag_jump_to_gameover = 1 when finished.
 */
void level_do_final_explosion(void);

/* Returns the displayed timer value (seconds split into M, SS digits). */
void level_get_timer_digits(int *minutes, int *seconds_hi, int *seconds_lo);

/* Finalize level (set copper, initial camera, etc.). */
void level_finalize(void);

/* Run the per-level 50 Hz game loop.
 * Declared here; defined in game/main.c (which owns the render loop). */
void level_game_loop_external(void);

#endif /* AB_LEVEL_H */
