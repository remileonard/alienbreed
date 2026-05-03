/*
 * Alien Breed SE 92 - C port
 * main.c — entry point and top-level game loop
 *
 * Sequence (mirrors main.asm begin: / game_level_loop:):
 *   1. init SDL / HAL
 *   2. story screen
 *   3. main menu (get num_players, share_credits)
 *   4. loop over 12 levels:
 *        briefing → level_run → check gameover
 *   5. ending screen
 *   6. back to menu
 */

#include "../types.h"
#include "constants.h"
#include "player.h"
#include "alien.h"
#include "level.h"
#include "hud.h"
#include "menu.h"
#include "story.h"
#include "briefing.h"
#include "intex.h"
#include "gameover.h"
#include "end.h"
#include "debug.h"
#include "../hal/hal.h"
#include "../hal/video.h"
#include "../hal/input.h"
#include "../hal/audio.h"
#include "../hal/timer.h"
#include "../engine/palette.h"
#include "../engine/tilemap.h"
#include "../engine/sprite.h"
#include "../engine/alien_gfx.h"
#include "../engine/tile_anim.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Forward declarations                                                 */
/* ------------------------------------------------------------------ */
static void game_run(int num_players, int share_credits);

/* ------------------------------------------------------------------ */
/* Main entry point                                                     */
/* ------------------------------------------------------------------ */
int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    if (hal_init() != 0) {
        fprintf(stderr, "Fatal: HAL init failed\n");
        return 1;
    }
    hud_init();
    audio_load_all();
    audio_set_music_volume(1280);

    /* Attract-mode cycle matching the original CD32 game sequence:
     *
     *   1. Title screen (display_title_screen + display_beam_title) — story_title_run()
     *      Reproduces the standalone title executable (no sources available).
     *   2. Menu attract loop (logo → stars → menu → idle timeout → credits) — menu_run()
     *   3. On credits exhausted (AUTO_EXIT):
     *      Planet + scrolling story text → title screen again — story_run()
     *      (matches story.asm: set_planet_pic … → display_title_screen)
     *   4. Back to step 2 (menu)
     *
     * On game-over: return directly to step 2 (menu), not step 1, matching
     * loop_from_gameover in main.asm which jumps straight to run_menu. */
    story_title_run();

    while (!g_quit_requested) {
        int num_players   = 1;
        int share_credits = 0;
        MenuResult mr = menu_run(&num_players, &share_credits);
        if (mr == MENU_RESULT_QUIT || g_quit_requested) break;

        if (mr == MENU_RESULT_AUTO_EXIT) {
            /* Credits exhausted without user interaction:
             * story.asm sequence (planet → title), then back to menu */
            story_run();
            continue;
        }

        /* MENU_RESULT_START: user actively selected "Start Game" */
        g_number_players = num_players;
        game_run(num_players, share_credits);
    }

    hud_quit();
    hal_quit();
    return 0;
}

/* ------------------------------------------------------------------ */
/* Run a full game session (story → levels → end)                      */
/* ------------------------------------------------------------------ */
static void game_run(int num_players, int share_credits)
{
    /* Initialise player lives */
    player_init_variables();
    for (int i = 0; i < num_players; i++) {
        g_players[i].lives = (WORD)(share_credits ?
            (i == 0 ? PLAYER_START_LIVES : 0) : PLAYER_START_LIVES);
    }

    int level_idx = 0;
    g_game_running_flag = 1;

    while (level_idx < NUM_LEVELS && g_game_running_flag && !g_quit_requested) {
        briefing_run(level_idx);
        if (g_quit_requested) break;

        level_run(level_idx);
        level_game_loop_external();

        if (g_flag_jump_to_gameover) {
            gameover_run();
            g_game_running_flag = 0;
            break;
        }

        /* Holocode jump: jump to a specific level (enter_level_N_holocode in main.asm).
         * Credits are already set in intex_run() when the code was accepted. */
        if (g_holocode_jump_level >= 0) {
            level_idx = g_holocode_jump_level;
            g_holocode_jump_level = -1;
            continue;
        }

        level_idx++;
    }

    if (g_game_running_flag && !g_quit_requested && level_idx >= NUM_LEVELS) {
        end_run();
    }
}

/* ------------------------------------------------------------------ */
/* Per-level game loop — called by level_run()                         */
/* (level_run sets up the map; this loop ticks everything at 50 Hz)   */
/* ------------------------------------------------------------------ */
void level_game_loop_external(void)
{
    g_flag_end_level      = 0;
    g_flag_jump_to_gameover = 0;
    g_self_destruct_initiated = 0;
    g_destruction_timer   = 0;
    g_map_overview_on     = 0;
    level_tick_counter_reset();

    while (!g_flag_end_level && !g_flag_jump_to_gameover && !g_quit_requested) {
        timer_begin_frame();
        input_poll();

        if (g_quit_requested) break;

        /* --- Global key checks ---------------------------------------- */
        if (g_key_pressed == KEY_ESC) {
            g_flag_jump_to_gameover = 1;
            break;
        }
        if (g_key_pressed == KEY_P) {
            /* Pause: just spin until P is pressed again */
            while (1) {
                timer_begin_frame();
                input_poll();
                if (g_quit_requested) goto end_loop;
                /* Draw paused overlay */
                video_present();          /* keep the last rendered frame */
                hud_render_pause();
                if (g_key_pressed == KEY_P) break;
            }
        }
        if (g_key_pressed == KEY_M) {
            g_map_overview_on = !g_map_overview_on;
        }
        if (g_key_pressed == KEY_D) {
            g_debug_overlay_on = !g_debug_overlay_on;
        }
        if (g_key_pressed == KEY_F) {
            debug_gfx_viewer_run();
        }
        if (g_key_pressed == KEY_G) {
            debug_palette_viewer_run();
        }
        if (g_key_pressed == KEY_H) {
            /* Debug: skip to next level */
            g_flag_end_level = 1;
        }
        if (g_key_pressed == KEY_J) {
            /* Debug: toggle god mode (invincible, infinite ammo, infinite keys) */
            g_god_mode = !g_god_mode;
            if (g_god_mode) {
                for (int i = 0; i < g_number_players; i++) {
                    g_players[i].ammunitions = PLAYER_MAX_AMMO;
                    g_players[i].ammopacks   = PLAYER_MAX_AMMOPCKS;
                    if (g_players[i].keys < 1)
                        g_players[i].keys = 1;
                }
            }
        }

        /* --- Update logic --------------------------------------------- */
        /* Player input/movement runs every frame, mirroring the Amiga VBL
         * interrupt handler (lbC00152C @ main.asm) which processes players
         * at the full 50 Hz VBL rate.                                      */
        for (int i = 0; i < g_number_players; i++) {
            if (g_players[i].alive)
                player_update(&g_players[i], player_get_input(&g_players[i]));
        }

        tile_anim_update();

        /* Alien AI, collisions and level timers mirror the game_level_loop
         * in main.asm which only runs every 2 VBLs (25 Hz on PAL).
         * The lbW0004BC counter (lines 870-874) gates the game loop signal
         * lbW0004BA to fire once per 2 VBL interrupts.
         * Running these at 50 Hz in the C port doubled alien speed.        */
        static int s_game_tick = 0;
        if (++s_game_tick >= 2) {
            s_game_tick = 0;

            alien_update_all();

            aliens_collisions_with_weapons();
            aliens_collisions_with_players();

            level_tick_timer();
            level_check_destruction();
        }

        /* Check if all players are dead */
        int any_alive = 0;
        for (int i = 0; i < g_number_players; i++) {
            if (g_players[i].alive || g_players[i].lives > 0)
                any_alive = 1;
        }
        if (!any_alive) {
            g_flag_jump_to_gameover = 1;
            break;
        }

        /* --- Render ---------------------------------------------------- */
        video_clear();

        if (g_map_overview_on) {
            hud_render_map_overview();
        } else {
            /* Centre camera on player 0 */
            Player *p = &g_players[0];
            g_camera_x = p->pos_x - SCREEN_W / 2;
            g_camera_y = p->pos_y - SCREEN_H / 2;
            /* Clamp */
            if (g_camera_x < 0) g_camera_x = 0;
            if (g_camera_y < 0) g_camera_y = 0;
            int max_cx = MAP_COLS * MAP_TILE_W - SCREEN_W;
            int max_cy = MAP_ROWS * MAP_TILE_H - SCREEN_H;
            if (g_camera_x > max_cx) g_camera_x = max_cx;
            if (g_camera_y > max_cy) g_camera_y = max_cy;

            tilemap_render(&g_cur_map, &g_tileset);

            /* Tile animation overlays: rendered on top of static tiles,
             * below sprites, so items / doors remain visible briefly after
             * collection / opening (Ref: patch_tiles / lbW012388 @ main.asm). */
            static int s_anim_tick = 0;
            s_anim_tick++;
            tile_anim_render_ship_engines(s_anim_tick);
            tile_anim_render_intex_screens(s_anim_tick);
            tile_anim_render_one_deadly_way(s_anim_tick);
            tile_anim_render();
            /* Walk cycle: frame sequence 0→1→2→1 (one tick/frame at 50 Hz).
             * Ref: lbL01B036 @ main.asm#L14384 — each frame has delay=1.
             * Compass direction used as atlas column (Ref: lbB00A228#L7077). */
            static const int k_walk_cycle[4] = {0, 1, 2, 1};
            for (int i = 0; i < g_alien_count; i++) {
                if (g_aliens[i].alive == 0) continue;
                int sx = g_aliens[i].pos_x - g_camera_x;
                int sy = g_aliens[i].pos_y - g_camera_y;
                /* pos_x/pos_y is the centre; sprite extends ±16 px around it. */
                if (sx > -48 && sx < SCREEN_W + 16 && sy > -48 && sy < SCREEN_H + 16) {
                    if (g_aliens[i].alive == 2) {
                        /* Dying: render explosion animation.
                         * Ref: lbL018C2E @ main.asm#L13907; 16 frames at delay=0. */
                        sprite_draw_alien_death(g_aliens[i].death_frame, sx, sy);
                    } else {
                        /* Walking: use compass direction as atlas column.
                         * If the alien was just hit (hit_flag > 0), use
                         * ALT WALK frame (ALIEN_WALK_FRAMES) for two rendered
                         * frames (≈40ms at 50Hz ≈ 1 game-logic tick at 25Hz),
                         * which matches the original one-VBL-frame duration at
                         * 25Hz (lbC009B80 @ main.asm#L6675 clr.w 50(a0)).
                         * Ref: lbC009B80 @ main.asm#L6675 (50(a0) → ALT WALK).
                         *
                         * If hatch_timer > HATCH_ANIM_WALK_THRESHOLD (12), show
                         * the zoom-in animation.  Each frame is held for 2 ticks
                         * (delay=1 in lbL01B6F6 / lbL01BC0A).  Frame index:
                         *   frame = (HATCH_ANIM_TIMER_INIT - hatch_timer) / 2
                         * Frames 0-2 use sprite_draw_alien_hatch(); frame 3
                         * (last 2 ticks) is a normal walk frame.
                         * Ref: lbC00A568 / lbL01B6F6 @ main.asm#L7272-7278,14544.
                         */
                        if (g_aliens[i].hatch_timer > HATCH_ANIM_WALK_THRESHOLD) {
                            int hf = (HATCH_ANIM_TIMER_INIT - g_aliens[i].hatch_timer) / 2;
                            if (hf < 3) {
                                sprite_draw_alien_hatch(hf, sx, sy);
                            } else {
                                /* Frame 3: full-size = first walk frame */
                                sprite_draw_alien(g_aliens[i].direction, 0, sx, sy);
                            }
                        } else {
                            int anim_tick  = g_aliens[i].anim_counter % 4;
                            int anim_frame = k_walk_cycle[anim_tick];
                            if (g_aliens[i].hit_flag) {
                                anim_frame = ALIEN_WALK_FRAMES; /* ALT WALK: y=96 */
                                g_aliens[i].hit_flag--;
                            }
                            sprite_draw_alien(g_aliens[i].direction, anim_frame, sx, sy);
                        }
                    }
                }
            }

            /* Draw players */
            for (int i = 0; i < g_number_players; i++) {
                if (!g_players[i].alive) continue;
                int sx = g_players[i].pos_x - g_camera_x;
                int sy = g_players[i].pos_y - g_camera_y;
                if (g_players[i].death_counter > 0) {
                    /* Death explosion: show the alien explosion atlas while the
                     * death_counter counts down.  player_update decrements the
                     * counter before rendering, so on the first rendered frame
                     * death_counter == PLAYER_DEATH_FRAMES-1 and df == 0.
                     * The modulo wraps the 16-frame atlas across all death frames.
                     * Ref: lbC00780C / lbL0146A2 @ main.asm#L4737-L4771. */
                    int df = (PLAYER_DEATH_FRAMES - 1 - g_players[i].death_counter)
                             % ALIEN_DEATH_FRAMES;
                    sprite_draw_alien_death(df, sx, sy);
                } else {
                    sprite_draw_player(i, sx, sy, g_players[i].direction);
                }
            }

            projectiles_render();
            hud_render();
        }

        palette_tick();
        video_upload_framebuffer();
        hud_render_overlay();
        if (g_debug_overlay_on && !g_map_overview_on)
            debug_render_overlay();
        video_flip();
    }
end_loop:
    /* Stop the looping alarm that may have been started by the self-destruct
     * sequence (level_start_destruction → audio_play_looping).  Safe to call
     * even if no loop is active (audio_stop_looping is a no-op in that case). */
    audio_stop_looping();
}
