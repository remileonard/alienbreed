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

/* Destruction timer initial value in seconds */
#define DESTRUCTION_TIMER_SECONDS 300

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

/* Returns the displayed timer value (minutes/seconds). */
void level_get_timer_digits(int *minutes, int *seconds_hi, int *seconds_lo);

/* Finalize level (set copper, initial camera, etc.). */
void level_finalize(void);

/* Run the per-level 50 Hz game loop.
 * Declared here; defined in game/main.c (which owns the render loop). */
void level_game_loop_external(void);

#endif /* AB_LEVEL_H */
