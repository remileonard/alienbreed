/*
 * Alien Breed SE 92 - C port
 * Level module
 */

#include "level.h"
#include "player.h"
#include "alien.h"
#include "../engine/tilemap.h"
#include "../engine/palette.h"
#include "../hal/audio.h"
#include "../hal/video.h"
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Level definitions — map filenames and settings                     */
/* Cross-referenced from main.asm level sequences                     */
/* ------------------------------------------------------------------ */
const LevelDef k_level_defs[NUM_LEVELS] = {
    /* lvl 1 */ { "L0AN", "L0BO", "L0MA",  0, "Level 1: Research Base",          "level" },
    /* lvl 2 */ { "L1AN", "L1BO", "L1MA",  0, "Level 2: Bio-Containment",        "level" },
    /* lvl 3 */ { "L2AN", "L2BO", "L2MA",  0, "Level 3: Reactor Core",           "level" },
    /* lvl 4 */ { "L3AN", "L3BO", "L3MA",  0, "Level 4: Alien Hive",             "boss"  },
    /* lvl 5 */ { "L4AN", "L4BO", "L4MA",  0, "Level 5: Service Tunnels",        "level" },
    /* lvl 6 */ { "L5AN", "L5BO", "L5MA",  0, "Level 6: Weapons Bay",            "boss"  },
    /* lvl 7 */ { "L6MA", NULL,   "L6MA",  0, "Level 7: Upper Decks",            "level" },
    /* lvl 8 */ { "L7MA", NULL,   "L7MA",  0, "Level 8: Engine Room",            "boss"  },
    /* lvl 9 */ { "L8MA", NULL,   "L8MA",  5, "Level 9: Alien Command",          "level" },
    /* lvl10 */ { "L9MA", NULL,   "L9MA", 10, "Level 10: Central Hive",          "boss"  },
    /* lvl11 */ { "LAMA", "LABM", "LAMA", 15, "Level 11: Breeding Grounds",      "level" },
    /* lvl12 */ { "LBMA", "LBBM", "LBMA", 20, "Level 12: Final Confrontation",   "boss"  },
};

/* ------------------------------------------------------------------ */
/* Global state                                                        */
/* ------------------------------------------------------------------ */
int  g_cur_level                = 0;
int  g_flag_end_level           = 0;
int  g_flag_jump_to_gameover    = 0;
int  g_in_destruction_sequence  = 0;
int  g_flag_destruct_level      = 0;
LONG g_destruction_timer        = 0;
int  g_self_destruct_initiated  = 0;
int  g_map_overview_on          = 0;
int  g_game_running_flag        = 0;
int  g_exit_unlocked            = 0;
int  g_boss_active              = 0;

/* Internal: frames per second = 50, timer displayed in M:SS */
#define TIMER_FRAMES_PER_SECOND 50

void level_init_variables(void)
{
    g_flag_end_level          = 0;
    g_flag_jump_to_gameover   = 0;
    g_in_destruction_sequence = 0;
    g_flag_destruct_level     = 0;
    g_self_destruct_initiated = 0;
    g_map_overview_on         = 0;
    g_game_running_flag       = 0;
    g_boss_active             = 0;
    /* Timer set by level_finalize based on level def */
    g_destruction_timer = (LONG)DESTRUCTION_TIMER_SECONDS * TIMER_FRAMES_PER_SECOND;
}

void level_get_timer_digits(int *minutes, int *seconds_hi, int *seconds_lo)
{
    int total_secs = (int)(g_destruction_timer / TIMER_FRAMES_PER_SECOND);
    if (total_secs < 0) total_secs = 0;
    int m  = total_secs / 60;
    int s  = total_secs % 60;
    *minutes    = m;
    *seconds_hi = s / 10;
    *seconds_lo = s % 10;
}

void level_tick_timer(void)
{
    if (!g_self_destruct_initiated) return;
    if (g_destruction_timer <= 0) return;

    g_destruction_timer--;

    /* Every 25 frames (~0.5 s) play the destruction sample (Ref: main.asm#L1275) */
    if (g_destruction_timer % 25 == 0)
        audio_play_sample(SAMPLE_DESTRUCT_IMM);

    /* Last second: warning bip (Ref: main.asm#L1275) */
    if (g_destruction_timer == TIMER_FRAMES_PER_SECOND)
        audio_play_sample(SAMPLE_CARET_MOVE);

    /* Switch palette to destruction colors when sequence starts */
    if (g_destruction_timer == (LONG)DESTRUCTION_TIMER_SECONDS * TIMER_FRAMES_PER_SECOND - 1)
        palette_set_immediate(g_cur_map.palette_b, 32);

    /* Timer expired: game over */
    if (g_destruction_timer == 0) {
        g_flag_destruct_level   = 1;
        g_flag_jump_to_gameover = 1;
    }
}

void level_start_destruction(void)
{
    if (g_self_destruct_initiated) return;  /* already started */
    g_self_destruct_initiated = 1;
    g_in_destruction_sequence = 1;
    /* Exit becomes passable once destruction starts (Ref: tile_exit @ main.asm#L5191) */
    g_exit_unlocked = 1;
    audio_play_sample(VOICE_DESTRUCT_IMM);
    audio_play_sample(SAMPLE_DESTRUCT_IMM);
}

void level_check_destruction(void)
{
    if (g_self_destruct_initiated && g_flag_destruct_level) {
        g_flag_jump_to_gameover = 1;
    }
}

void level_check_gameover(void)
{
    /* All players dead = game over */
    int any_alive = 0;
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (g_players[i].alive) any_alive = 1;

    if (!any_alive)
        g_flag_jump_to_gameover = 1;
}

void level_trigger_end(void)
{
    g_flag_end_level = 1;
    audio_play_sample(SAMPLE_DESCENT);
}

void level_finalize(void)
{
    /* Apply level palette */
    if (g_cur_map.valid)
        palette_set_immediate(g_cur_map.palette_a, 32);

    /* Set map overview flag if player has supply */
    /* (handled when player collects SUPPLY_MAP_OVERVIEW) */
}

void level_run(int level_idx)
{
    if (level_idx < 0 || level_idx >= NUM_LEVELS) return;
    g_cur_level = level_idx;

    const LevelDef *def = &k_level_defs[level_idx];

    /* exit_unlocked: mirrors per-level init in main.asm (Ref: main.asm#L1023-L1219).
     * Levels 1,4,6,8,9,11 (0-based: 0,3,5,7,8,10) start with exit open.
     * Others require boss kill to unlock. */
    static const int k_exit_open[] = { 1, 0, 0, 1, 0, 1, 0, 1, 1, 0, 1, 0 };
    g_exit_unlocked = k_exit_open[level_idx];

    /* Credits bonus given at the start of certain levels (Ref: main.asm#L8347).
     * Applied additively on top of credits carried from previous level. */
    static const LONG k_level_credits[] = {
        0, 500000, 0, 1000000, 0, 1500000, 0, 2000000, 0, 2500000, 0, 0
    };
    LONG bonus = k_level_credits[level_idx];
    if (bonus > 0) {
        for (int i = 0; i < MAX_PLAYERS; i++)
            g_players[i].credits += bonus;
    }

    /* Load music */
    audio_play_music(def->music);

    /* Load map (use main map) */
    char path[256];
    snprintf(path, sizeof(path), "game/%s", def->map_ma);
    tilemap_load(path, &g_cur_map);

    /* Load tileset */
    tileset_load(g_cur_map.bg_filename, &g_tileset);

    /* Set alien extra strength from level def (Ref: main.asm#L429-L465) */
    g_global_aliens_extra_strength = (WORD)def->alien_extra_strength;

    /* Initialise subsystems */
    level_init_variables();
    player_init_variables();
    player_set_starting_positions();
    alien_init_variables();
    alien_spawn_from_map();
    level_finalize();

    /* Centre camera on player 1.
     * ASM: map_pos = player_pos - (128, 120), clamped to [16,1632] x [20,1344].
     * In C we use the same world-pixel positions; the SDL viewport is 320×256
     * so we subtract half-screen (160, 128) to centre the player. */
    {
        int px = g_players[0].pos_x;
        int py = g_players[0].pos_y;
        g_camera_x = px - 160;
        g_camera_y = py - 128;
        if (g_camera_x < 0) g_camera_x = 0;
        if (g_camera_y < 0) g_camera_y = 0;
        int max_cam_x = MAP_COLS * MAP_TILE_W - 320;
        int max_cam_y = MAP_ROWS * MAP_TILE_H - 256;
        if (g_camera_x > max_cam_x) g_camera_x = max_cam_x;
        if (g_camera_y > max_cam_y) g_camera_y = max_cam_y;
    }
}
