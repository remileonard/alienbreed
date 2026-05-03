/*
 * Alien Breed SE 92 - C port
 * Level module
 */

#include "level.h"
#include "player.h"
#include "alien.h"
#include "../engine/tilemap.h"
#include "../engine/palette.h"
#include "../engine/alien_gfx.h"
#include "../engine/anim_gfx.h"
#include "../engine/tile_anim.h"
#include "../hal/audio.h"
#include "../hal/video.h"
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Level definitions — map filenames and settings                     */
/* Cross-referenced from main.asm level sequences                     */
/* ------------------------------------------------------------------ */
/*
 * Level BO file assignments (Ref: main.asm#L7972-L8018):
 *   Levels 1-6 each have their own BO file (L0BO-L5BO).
 *   Levels 7-9 reuse L2BO (lev7_load_struct#L7998, lev8#L8002, lev9#L8006).
 *   Level 10 reuses L1BO (lev10_load_struct#L8010).
 *   Level 11 reuses L2BO (lev11_load_struct#L8014).
 *   Level 12 reuses L5BO (lev12_load_struct#L8018).
 * Only 6 distinct BO files (L0BO-L5BO) exist; the original game reused them
 * for later levels that share the same alien sprite set.
 *
 * Atlas type per level (Ref: lbL000554 assignments @ main.asm#L1030-L1227):
 *   LEGACY  (lbW01945E): levels 1, 10, 11  — walk frames at y={0, 96, 128}
 *   COMPACT (all others): walk frames at y={0, 32, 64}
 */
/*
 * engine_tile_mask: bitmask of which ship-engine tile attributes (0x18-0x1C)
 * are animated on each level.  Derived from the per-level dispatch table at
 * lbC004384 + level_flag (main.asm).  bit 0 = tile 0x18, …, bit 4 = tile 0x1C.
 *
 *   level_flag = -256 (lvl 1):          all 5 tiles → 0x1F
 *   level_flag =    0 (lvl 2,10,11):    skip 0x1B   → 0x17
 *                                        (lbC004962 = rts for tile 0x1B)
 *   level_flag =  256 (lvl 7,8,9):      only 0x18,0x19 → 0x03
 *   level_flag =  512 (lvl 3,6):        all 5 tiles → 0x1F
 *   level_flag =  768 (lvl 4,5):        all 5 tiles → 0x1F
 *   level_flag = 1024 (lvl 12):         skip 0x19   → 0x1D
 *                                        (bra.w none for tile 0x19)
 */
const LevelDef k_level_defs[NUM_LEVELS] = {
    /*          map_an  map_bo  map_ma  str  briefing_text                         music    atlas_type          engine_tile_mask  timer_s */
    /* lvl 1  */ { "L0AN", "L0BO", "L0MA",  0, "Level 1: Research Base",          "level", ALIEN_ATLAS_LEGACY,  0x1F,  60 },
    /* lvl 2  */ { "L1AN", "L1BO", "L1MA",  0, "Level 2: Bio-Containment",        "level", ALIEN_ATLAS_COMPACT, 0x17,  60 },
    /* lvl 3  */ { "L2AN", "L2BO", "L2MA",  0, "Level 3: Reactor Core",           "level", ALIEN_ATLAS_COMPACT, 0x1F,  40 },
    /* lvl 4  */ { "L3AN", "L3BO", "L3MA",  0, "Level 4: Alien Hive",             "boss",  ALIEN_ATLAS_COMPACT, 0x1F,  90 },
    /* lvl 5  */ { "L4AN", "L4BO", "L4MA",  0, "Level 5: Service Tunnels",        "level", ALIEN_ATLAS_COMPACT, 0x1F,  90 },
    /* lvl 6  */ { "L5AN", "L5BO", "L5MA",  0, "Level 6: Weapons Bay",            "boss",  ALIEN_ATLAS_COMPACT, 0x1F,   2 },
    /* lvl 7  */ { "L3AN", "L2BO", "L6MA",  0, "Level 7: Upper Decks",            "level", ALIEN_ATLAS_COMPACT, 0x03,  99 },
    /* lvl 8  */ { "L3AN", "L2BO", "L7MA",  0, "Level 8: Engine Room",            "boss",  ALIEN_ATLAS_COMPACT, 0x03,  60 },
    /* lvl 9  */ { "L2AN", "L2BO", "L8MA",  5, "Level 9: Alien Command",          "level", ALIEN_ATLAS_COMPACT, 0x03,  77 },
    /* lvl10  */ { "L1AN", "L1BO", "L9MA", 10, "Level 10: Central Hive",          "boss",  ALIEN_ATLAS_LEGACY,  0x17,  80 },
    /* lvl11  */ { "L1AN", "L2BO", "LAMA", 15, "Level 11: Breeding Grounds",      "level", ALIEN_ATLAS_LEGACY,  0x17,  60 },
    /* lvl12  */ { "L5AN", "L5BO", "LBMA", 20, "Level 12: Final Confrontation",   "boss",  ALIEN_ATLAS_COMPACT, 0x1D,  14 },
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
int  g_holocode_jump_level      = -1; /* >= 0: jump to this level (enter_level_N_holocode in main.asm) */

/* Projectile-environment interaction state */
int  g_reactor_up_done          = 0;
int  g_reactor_left_done        = 0;
int  g_reactor_down_done        = 0;
int  g_reactor_right_done       = 0;
int  g_door_impact_accum        = 0;
int  g_door_impact_col          = -1;
int  g_door_impact_row          = -1;
int  g_alarm_system_active      = 0;
int  g_alarm_buttons_pressed    = 0;
int  g_alarm_last_col           = -1;
int  g_alarm_last_row           = -1;

/* Internal: game tick rate for timer = 25 Hz (called every other display frame) */
#define TIMER_TICKS_PER_SECOND 25

/* Internal: per-second tick counter for the destruction countdown */
static int s_destruct_tick_ctr = 0;

void level_init_variables(void)
{
    g_flag_end_level          = 0;
    g_flag_jump_to_gameover   = 0;
    g_in_destruction_sequence = 0;
    g_flag_destruct_level     = 0;
    g_self_destruct_initiated = 0;
    g_map_overview_on         = 0;
    /* g_game_running_flag is managed by game_run() — do NOT reset it here.
     * Resetting it in level_init_variables() (called at the start of every
     * level) would cause the outer game loop to exit after the first level
     * completes, sending the player back to the menu instead of the next
     * level. */
    g_boss_active             = 0;
    /* Timer is inactive until level_start_destruction() is called.
     * g_destruction_timer holds the remaining seconds (0 = not active). */
    g_destruction_timer       = 0;
    s_destruct_tick_ctr       = 0;

    /* Reactor state — reset in init_level_variables @ main.asm#L747-L750 */
    g_reactor_up_done         = 0;
    g_reactor_left_done       = 0;
    g_reactor_down_done       = 0;
    g_reactor_right_done      = 0;

    /* Door impact accumulator — reset in reset_game_variables @ main.asm#L763 */
    g_door_impact_accum       = 0;
    g_door_impact_col         = -1;
    g_door_impact_row         = -1;

    /* Alarm system:
     * lbW002AC0/AC2 are cleared globally (main.asm#L763-L764) and re-armed
     * per-level (only level 3 sets lbW002AC2=1 @ main.asm#L1053).
     * In the C port we set g_alarm_system_active per-level in level_finalize(). */
    g_alarm_system_active     = 0;
    g_alarm_buttons_pressed   = 0;
    g_alarm_last_col          = -1;
    g_alarm_last_row          = -1;
}

void level_get_timer_digits(int *minutes, int *seconds_hi, int *seconds_lo)
{
    int total_secs = (int)g_destruction_timer;
    if (total_secs < 0) total_secs = 0;
    int m  = total_secs / 60;
    int s  = total_secs % 60;
    *minutes    = m;
    *seconds_hi = s / 10;
    *seconds_lo = s % 10;
}

void level_tick_counter_reset(void)
{
    s_destruct_tick_ctr = 0;
}

void level_tick_timer(void)
{
    if (!g_self_destruct_initiated) return;
    if (g_destruction_timer <= 0) return;

    /*
     * Count 25Hz ticks: when TIMER_TICKS_PER_SECOND ticks have accumulated
     * one second has elapsed (mirrors lbW002FE0 counter in destruction_sequence
     * @ main.asm#L1288-L1293: addq.w #1 / cmp.w #25 / bne void / clr.w).
     */
    s_destruct_tick_ctr++;
    if (s_destruct_tick_ctr < TIMER_TICKS_PER_SECOND) return;
    s_destruct_tick_ctr = 0;

    /* Alarm tick each second (Ref: main.asm#L1293: move.w #14,sample_to_play). */
    audio_play_sample(SAMPLE_CARET_MOVE);

    g_destruction_timer--;

    if (g_destruction_timer <= 0) {
        /* Timer expired: level explodes → game over */
        g_destruction_timer     = 0;
        g_flag_destruct_level   = 1;
        g_flag_jump_to_gameover = 1;
        audio_stop_looping();
        return;
    }

    /*
     * Voice countdown: when the display reaches 8 the voice announces each
     * remaining second until 1 (then game over at 0).
     * Mirrors the problem spec: "lorsque le compte à rebours arrive à 8,
     * la même voix annonce alors un compte à rebours jusqu'a 0".
     */
    static const int k_countdown_voices[9] = {
        0,           /* 0 — handled by timer-expired branch above */
        VOICE_ONE,   /* 1 */
        VOICE_TWO,   /* 2 */
        VOICE_THREE, /* 3 */
        VOICE_FOUR,  /* 4 */
        VOICE_FIVE,  /* 5 */
        VOICE_SIX,   /* 6 */
        VOICE_SEVEN, /* 7 */
        VOICE_EIGHT  /* 8 */
    };
    if (g_destruction_timer >= 1 && g_destruction_timer <= 8)
        audio_play_sample(k_countdown_voices[g_destruction_timer]);
}

void level_start_destruction(void)
{
    if (g_self_destruct_initiated) return;  /* already started */
    g_self_destruct_initiated = 1;
    g_in_destruction_sequence = 1;
    /* Exit becomes passable once destruction starts (Ref: tile_exit @ main.asm#L5191) */
    g_exit_unlocked = 1;

    /*
     * Set the per-level countdown timer in seconds.
     * Mirrors set_destruction_timer @ main.asm#L1257 which loads timer_digit_hi:lo
     * into cur_timer_digit_hi:lo — values are defined per-level in init_level_N.
     */
    g_destruction_timer = (LONG)k_level_defs[g_cur_level].timer_seconds;
    s_destruct_tick_ctr = 0;

    /*
     * Switch to the destruction palette (palette_b = red-tinted palette).
     * Mirrors the palette fade in destruction_sequence @ main.asm#L1282:
     *   lea.l level_palette2,a1 / jsr prep_fade_speeds_fade_to_rgb
     * We use an immediate switch for clarity; the red tint appears instantly.
     */
    if (g_cur_map.valid) {
        palette_set_immediate(g_cur_map.palette_b, 32);
        /* Replicate copper override: COLOR02/COLOR03 forced to black in the
         * main play area (Ref: lbW09A20C @ main.asm#L18513). */
        video_set_palette_entry(2, 0x000);
        video_set_palette_entry(3, 0x000);
    }

    /*
     * Voice announcement: "Warning … destruction imminent"
     * Mirrors voice 6 + voice 23 played in destruction_sequence init block
     * (lbC0111C4 / lbC011272 @ main.asm#L1270-L1278).
     */
    audio_play_sample(VOICE_WARNING);
    audio_play_sample(VOICE_DESTRUCT_IMM);

    /*
     * Start continuous alarm loop (mirrors lbW02316A looping sample struct
     * set in destruction_sequence @ main.asm#L1289).
     */
    audio_play_looping(SAMPLE_DESTRUCT_IMM);
}

void level_check_destruction(void)
{
    if (g_self_destruct_initiated && g_flag_destruct_level) {
        g_flag_jump_to_gameover = 1;
    }

    /*
     * Alarm-system check (level 3 only, lbC000E56 @ main.asm#L536).
     * If the alarm system is armed and 3+ alarm buttons have been shot,
     * trigger self-destruct, then disarm to prevent re-triggering.
     */
    if (g_alarm_system_active && g_alarm_buttons_pressed >= 3) {
        g_alarm_system_active   = 0;  /* clr.w lbW002AC2 @ main.asm#L542 */
        g_alarm_buttons_pressed = 0;  /* clr.w lbW002AC0 @ main.asm#L544 */
        g_alarm_last_col        = -1; /* clr.l lbL00E756 @ main.asm#L543 */
        g_alarm_last_row        = -1;
        level_start_destruction();
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
    /* Stop the looping alarm when the player successfully escapes. */
    audio_stop_looping();
    audio_play_sample(SAMPLE_DESCENT);
}

void level_finalize(void)
{
    /* Apply level palette */
    if (g_cur_map.valid) {
        palette_set_immediate(g_cur_map.palette_a, 32);
        /* Replicate copper override: at beam line 51 the copper forces COLOR02
         * and COLOR03 to black for the main 5-bitplane play area regardless of
         * the loaded palette (Ref: lbW09A20C dc.w COLOR02,0,COLOR03,0
         * @ main.asm#L18513). */
        video_set_palette_entry(2, 0x000);
        video_set_palette_entry(3, 0x000);
    }

    /* Set map overview flag if player has supply */
    /* (handled when player collects SUPPLY_MAP_OVERVIEW) */

    /*
     * Arm alarm system for level 3 only.
     * Mirrors init_level_3 @ main.asm#L1053: move.w #1,lbW002AC2.
     * The alarm fires self-destruct when 3 alarm-door buttons are shot
     * (lbC000E56 @ main.asm#L536-L544).
     */
    g_alarm_system_active = (g_cur_level == 2) ? 1 : 0; /* level 3 is index 2 (0-based) */
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

    /* Load alien graphics atlas from the level BO file.
     * Ref: copy_gfx @ main.asm#L12592 — called once per level after init_level_N.
     * The BO file is a raw 5-plane sequential bitmap (320×384, 76800 bytes)
     * containing all alien type sprites arranged as a sprite atlas.
     * Ref: lev1_load_struct @ main.asm#L7972 */
    if (def->map_bo) {
        char bo_path[256];
        snprintf(bo_path, sizeof(bo_path), "game/%s", def->map_bo);
        alien_gfx_load(bo_path, def->atlas_type);
    }

    /* Load animated background tile atlas from the level AN file.
     * Ref: bkgnd_anim_block / lbL014AB6 (main.asm#L12604).
     * The AN file is a raw 5-plane sequential bitmap (320×144, 28800 bytes)
     * containing the animated tile frames for this level. */
    if (def->map_an) {
        char an_path[256];
        snprintf(an_path, sizeof(an_path), "game/%s", def->map_an);
        anim_gfx_load(an_path);
    }

    /* Set alien extra strength from level def (Ref: main.asm#L429-L465) */
    g_global_aliens_extra_strength = (WORD)def->alien_extra_strength;

    /* Initialise subsystems */
    level_init_variables();
    player_init_variables();
    player_set_starting_positions();
    alien_init_variables();
    alien_spawn_from_map();
    tile_anim_init();
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
