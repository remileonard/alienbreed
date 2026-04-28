/*
 * Alien Breed SE 92 - C port
 * INTEX terminal — translated from src/intex/intex.asm
 *
 * Main menu (text_main_menu in intex.asm):
 *   Choice 0: INTEX WEAPON SUPPLIES
 *   Choice 1: INTEX TOOL SUPPLIES
 *   Choice 2: INTEX RADAR SERVICE
 *   Choice 3: MISSION OBJECTIVE
 *   Choice 4: ENTER HOLOCODE
 *   Choice 5: GAME STATISTICS
 *   Choice 6: INFO BASE
 *   Choice 7: ABORT INTEX NETWORK
 *
 * Cursor Y = 68 + menu_choice * 12  (from disp_caret_in_menu in intex.asm)
 * Cursor X = 48
 */

#include "intex.h"
#include "player.h"
#include "level.h"
#include "constants.h"
#include "../hal/video.h"
#include "../hal/input.h"
#include "../hal/audio.h"
#include "../hal/timer.h"
#include "../engine/palette.h"
#include "../engine/tilemap.h"
#include "../engine/typewriter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Menu choices (menu_choice values from intex.asm) */
#define MENU_WEAPON_SUPPLIES  0
#define MENU_TOOL_SUPPLIES    1
#define MENU_RADAR_SERVICE    2
#define MENU_MISSION_OBJ      3
#define MENU_ENTER_HOLOCODE   4
#define MENU_STATS            5
#define MENU_INFO_BASE        6
#define MENU_ABORT            7
#define MENU_ITEMS            8

/* Cursor geometry from disp_caret_in_menu:  y = 68 + menu_choice*12, x = 48 */
#define MENU_CARET_X      48
#define MENU_CARET_Y0     68
#define MENU_LINE_H       12

/*
 * Weapon image atlas: assets/gfx/intex_weapons_320x264.raw
 * Ref: weapons_pic_table in intex.asm
 */
#define INTEX_WEAPON_IMG_W    160
#define INTEX_WEAPON_IMG_H     88
#define INTEX_WEAPON_ATLAS_W  320
#define INTEX_WEAPON_ATLAS_H  264

static const int k_wpic_row_px[6]  = { 176,  0,   0, 176,  88,  88 };
static const int k_wpic_col_px[6]  = {   0,  0, 160, 160, 160,   0 };
static const char *k_wpic_names[6] = {
    "MACHINEGUN", "TWINFIRE", "FLAMEARC", "PLASMAGUN", "FLAMETHROWER", "SIDEWINDERS"
};

/* Shared image type */
typedef struct { UBYTE *pixels; int w, h; } IntexImg;

/* -----------------------------------------------------------------------
 * Startup sequence helpers
 * Ref: display_intex_startup_seq / mess_up_screen in intex.asm
 * ----------------------------------------------------------------------- */

static int s_startup_interrupted = 0;

/*
 * Simulate mess_up_screen: for n frames draw the background with a random
 * vertical offset, mimicking the Amiga copper DIWSTRT/DIWSTOP jitter effect.
 * Interruptible by either fire button.
 * Ref: mess_up_screen in intex.asm — reads CIAB TOD low byte and uses it
 * to offset the display window start/stop each frame.
 */
static void intex_mess_up(int n, const IntexImg *bg)
{
    if (s_startup_interrupted) return;
    for (int i = 0; i < n; i++) {
        input_poll();
        if (g_player1_input & (INPUT_FIRE1 | INPUT_FIRE2)) {
            s_startup_interrupted = 1;
            return;
        }
        int jitter = (rand() % 80) - 40;   /* −40 .. +39 px vertical shift */
        video_clear();
        if (bg->pixels)
            video_blit(bg->pixels, bg->w, 0, jitter, bg->w, bg->h, -1);
        video_present();
        timer_begin_frame();
    }
}

/*
 * Wait n_secs × 50 frames, interruptible by either fire button.
 * Ref: wait_timed_frames / wait_timed_frames_startup in intex.asm.
 */
static void intex_wait_frames(int n_secs)
{
    if (s_startup_interrupted) return;
    int frames = n_secs * 50;
    for (int i = 0; i < frames; i++) {
        input_poll();
        if (g_player1_input & (INPUT_FIRE1 | INPUT_FIRE2)) {
            s_startup_interrupted = 1;
            return;
        }
        timer_begin_frame();
    }
}

/*
 * Display a block of text lines at (x, y_start), one per row.
 * Lines must be a NULL-terminated array of C strings.
 * Draws onto the current framebuffer without clearing it first.
 */
static void intex_display_lines(Font *font, int x, int y_start,
                                 const char * const *lines)
{
    TextCtx ctx;
    typewriter_init_ctx(&ctx, font, g_framebuffer, 320, x, y_start);
    for (int i = 0; lines[i]; i++) {
        ctx.cursor_x = x;
        typewriter_display(&ctx, lines[i]);
        ctx.cursor_y += font->letter_h;
    }
}

/*
 * Startup sequence: mess_up_screen × 3 passes, then display the three
 * text blocks (connecting / system-status / downloading) incrementally.
 * Ref: display_intex_startup_seq in intex.asm.
 *   text_connecting    dc.w 0,48
 *   text_system_status dc.w 0,84
 *   text_downloading   dc.w 0,168
 */
static void intex_startup_seq(const IntexImg *bg, Font *font)
{
    static const char * const k_connecting[] = {
        "INTEX NETWORK CONNECT: CODE ABF01DCC61",
        "CONNECTING.....................",
        NULL
    };
    static const char * const k_system_status[] = {
        "INTEX NETWORK SYSTEM V10.1",
        "2.5G RAM:         OK",
        "EXTERNAL DEVICE:  OK",
        "SYSTEM V1.32 CS:  OK",
        "VIDEODISPLAY:     CCC2.A2",
        "TEXT UPDATE ACCCELERATOR INSTALLED.",
        NULL
    };
    static const char * const k_downloading[] = {
        "EXECUTING DOS 6.0",
        "SYSTEM DOWNLOADING NETWORK DATA.... OK",
        "INTEX EXECUTED!",
        NULL
    };

    s_startup_interrupted = 0;

    /* mess_up_screen(10) → wait 1s → mess_up(25) → wait 1s → mess_up(6)
     * Ref: lines 660-678 of display_intex_startup_seq in intex.asm.
     * Each step checks interrupted_by_user_flag and branches to
     * startup_seq_interrupted if set. */
    intex_mess_up(10, bg);
    intex_wait_frames(1);
    intex_mess_up(25, bg);
    intex_wait_frames(1);
    intex_mess_up(6, bg);

    /* Draw background once — texts will accumulate on top without clearing.
     * This is the state of the screen before display_text is called in ASM. */
    video_clear();
    if (bg->pixels)
        video_blit(bg->pixels, bg->w, 0, 0, bg->w, bg->h, -1);

    if (!s_startup_interrupted) {
        /* text_connecting at y=48 */
        intex_display_lines(font, 0, 48, k_connecting);
        video_present();
        intex_wait_frames(1);
    }

    if (!s_startup_interrupted) {
        /* text_system_status at y=84  (no clear — accumulates on existing frame) */
        intex_display_lines(font, 0, 84, k_system_status);
        video_present();
        intex_wait_frames(2);
    }

    if (!s_startup_interrupted) {
        /* text_downloading at y=168  (no clear — accumulates) */
        intex_display_lines(font, 0, 168, k_downloading);
        video_present();
        intex_wait_frames(1);
    }

    s_startup_interrupted = 0;

    /* copy_bkgnd_pic — restore clean background before entering main loop */
    video_clear();
    if (bg->pixels)
        video_blit(bg->pixels, bg->w, 0, 0, bg->w, bg->h, -1);
    video_present();
}

/* -----------------------------------------------------------------------
 * Disconnecting sequence
 * Ref: scr_disconnecting in intex.asm
 *   text_disconnecting dc.w 0,64  "  DISCONNECTING.............."
 * ----------------------------------------------------------------------- */
static void intex_disconnecting(const IntexImg *bg, Font *font)
{
    static const char * const k_disconn[] = {
        "  DISCONNECTING..............",
        NULL
    };

    video_clear();
    if (bg->pixels)
        video_blit(bg->pixels, bg->w, 0, 0, bg->w, bg->h, -1);
    intex_display_lines(font, 0, 64, k_disconn);
    video_present();

    intex_wait_frames(1);
    intex_mess_up(6, bg);
}

/* -----------------------------------------------------------------------
 * Sub-screen: GAME STATISTICS
 * Ref: scr_stats in intex.asm
 * ----------------------------------------------------------------------- */
static void draw_screen_stats(int pidx, Font *font)
{
    Player *p = &g_players[pidx];
    char buf[128];
    TextCtx ctx;
    typewriter_init_ctx(&ctx, font, g_framebuffer, 320, 20, 30);
    snprintf(buf, sizeof(buf), "SCORE: %ld", (long)p->score);
    typewriter_display(&ctx, buf);
    ctx.cursor_x = 20; ctx.cursor_y += font->letter_h + 2;
    snprintf(buf, sizeof(buf), "KILLS: %d", p->aliens_killed);
    typewriter_display(&ctx, buf);
    ctx.cursor_x = 20; ctx.cursor_y += font->letter_h + 2;
    snprintf(buf, sizeof(buf), "SHOTS: %d", p->shots);
    typewriter_display(&ctx, buf);
}

/* -----------------------------------------------------------------------
 * Sub-screen: INTEX WEAPON SUPPLIES
 * Ref: scr_weapons in intex.asm
 * ----------------------------------------------------------------------- */
static void draw_screen_weapons(int pidx, Font *font,
                                const UBYTE *wpic, int cur_wpic_idx)
{
    Player *p = &g_players[pidx];
    TextCtx ctx;

    if (wpic && cur_wpic_idx >= 0 && cur_wpic_idx < 6) {
        int src_x = k_wpic_col_px[cur_wpic_idx];
        int src_y = k_wpic_row_px[cur_wpic_idx];
        const UBYTE *src = wpic + ((size_t)src_y * INTEX_WEAPON_ATLAS_W + (size_t)src_x);
        video_blit(src, INTEX_WEAPON_ATLAS_W, 8, 30, INTEX_WEAPON_IMG_W, INTEX_WEAPON_IMG_H, 0);
    }

    typewriter_init_ctx(&ctx, font, g_framebuffer, 320, 8, 20);
    if (cur_wpic_idx >= 0 && cur_wpic_idx < 6)
        typewriter_display(&ctx, k_wpic_names[cur_wpic_idx]);

    ctx.cursor_x = 175;
    ctx.cursor_y = 30;
    typewriter_display(&ctx, "WEAPONS:");
    for (int w = WEAPON_MACHINEGUN; w < WEAPON_MAX; w++) {
        int idx = w - WEAPON_MACHINEGUN;
        char line[64];
        snprintf(line, sizeof(line), "%s%s",
                 k_wpic_names[idx],
                 p->owned_weapons[w-1] ? " [OWNED]" : "");
        ctx.cursor_x = 175;
        ctx.cursor_y += font->letter_h + 2;
        if (idx == cur_wpic_idx)
            video_fill_rect(173, ctx.cursor_y - 1, 144, font->letter_h + 1, 3);
        typewriter_display(&ctx, line);
    }
}

/* -----------------------------------------------------------------------
 * intex_run — main entry point
 * ----------------------------------------------------------------------- */
void intex_run(int player_idx)
{
    audio_play_sample(SAMPLE_INTEX_SHUTDOWN);
    audio_pause_music();

    /* ------------------------------------------------------------------
     * Load assets
     * ------------------------------------------------------------------ */
    IntexImg bg = {NULL, 0, 0};
    {
        FILE *f = fopen("assets/gfx/intex_bkgnd_320x256.raw", "rb");
        if (f) {
            fread(&bg.w, 4, 1, f); fread(&bg.h, 4, 1, f);
            bg.pixels = (UBYTE *)malloc((size_t)(bg.w * bg.h));
            if (bg.pixels) fread(bg.pixels, 1, (size_t)(bg.w * bg.h), f);
            fclose(f);
        }
    }

    IntexImg wpic = {NULL, 0, 0};
    {
        FILE *fw = fopen("assets/gfx/intex_weapons_320x264.raw", "rb");
        if (fw) {
            fread(&wpic.w, 4, 1, fw); fread(&wpic.h, 4, 1, fw);
            if (wpic.w == INTEX_WEAPON_ATLAS_W && wpic.h == INTEX_WEAPON_ATLAS_H) {
                wpic.pixels = (UBYTE *)malloc((size_t)wpic.w * (size_t)wpic.h);
                if (wpic.pixels) fread(wpic.pixels, 1, (size_t)wpic.w * (size_t)wpic.h, fw);
            }
            fclose(fw);
        }
    }

    Font font = {0};
    font_load(&font, "assets/fonts/font_16x504.raw", 8, 12, 0);

    /* ------------------------------------------------------------------
     * Set INTEX palette
     * Ref: set_bitplanes_and_palette in intex.asm
     * ------------------------------------------------------------------ */
    static const UWORD k_intex_pal[] = {
        0x000, 0x010, 0x020, 0x030, 0x040, 0x050, 0x060, 0x070,
        0x080, 0x090, 0x0A0, 0x0B0, 0x0C0, 0x0D0, 0xFFF, 0xFFF,
        0x555, 0x565, 0x575, 0x585, 0x595, 0x5A5, 0x5B5, 0x5C5,
        0x5D5, 0x5E5, 0x5F5, 0x4F4, 0x3F3, 0x2F2, 0x1F1, 0x0F0
    };
    palette_set_immediate(k_intex_pal, 32);

    /* ------------------------------------------------------------------
     * Startup sequence
     * Ref: display_intex_startup_seq in intex.asm
     * ------------------------------------------------------------------ */
    intex_startup_seq(&bg, &font);

    /* ------------------------------------------------------------------
     * Main menu
     * Ref: main_loop in intex.asm, text_main_menu
     *
     * text_main_menu layout (starting at y=32, line_h=12):
     *   y= 32  "            INTEX MAIN MENU             "  (header)
     *   y= 44  blank
     *   y= 56  blank
     *   y= 68  "         INTEX WEAPON SUPPLIES          "  choice 0
     *   y= 80  "          INTEX TOOL SUPPLIES           "  choice 1
     *   y= 92  "          INTEX RADAR SERVICE           "  choice 2
     *   y=104  "           MISSION OBJECTIVE            "  choice 3
     *   y=116  "            ENTER HOLOCODE              "  choice 4
     *   y=128  "            GAME STATISTICS             "  choice 5
     *   y=140  "               INFO BASE                "  choice 6
     *   y=152  "          ABORT INTEX NETWORK           "  choice 7
     * ------------------------------------------------------------------ */
    static const char * const k_menu_text[] = {
        "            INTEX MAIN MENU             ",
        "                                        ",
        "                                        ",
        "         INTEX WEAPON SUPPLIES          ",
        "          INTEX TOOL SUPPLIES           ",
        "          INTEX RADAR SERVICE           ",
        "           MISSION OBJECTIVE            ",
        "            ENTER HOLOCODE              ",
        "            GAME STATISTICS             ",
        "               INFO BASE                ",
        "          ABORT INTEX NETWORK           ",
        NULL
    };

    int menu_choice = 0;
    int cur_wpic    = 0;   /* current weapon picture for scr_weapons */
    int debounce    = 0;
    int running     = 1;

    while (running) {
        timer_begin_frame();
        input_poll();

        if (g_quit_requested) break;

        /* copy_bkgnd_pic */
        video_clear();
        if (bg.pixels)
            video_blit(bg.pixels, bg.w, 0, 0, bg.w, bg.h, -1);

        /* Draw main menu text */
        intex_display_lines(&font, 0, 32, k_menu_text);

        /* Highlight cursor row (caret at x=48, y=68+choice*12) */
        {
            int cy = MENU_CARET_Y0 + menu_choice * MENU_LINE_H;
            video_fill_rect(0, cy, 320, MENU_LINE_H, 3);
            /* Redraw the selected line text on top of the highlight */
            TextCtx ctx;
            typewriter_init_ctx(&ctx, &font, g_framebuffer, 320, 0, cy);
            typewriter_display(&ctx, k_menu_text[3 + menu_choice]);
        }

        video_present();

        /* Input handling — UP/DOWN to navigate, FIRE to select */
        if (debounce > 0) { debounce--; continue; }

        if ((g_player1_input & INPUT_UP) && !(g_player1_old_input & INPUT_UP)) {
            if (menu_choice > 0) {
                menu_choice--;
                audio_play_sample(SAMPLE_CARET_MOVE);
            }
            debounce = 8;
        } else if ((g_player1_input & INPUT_DOWN) && !(g_player1_old_input & INPUT_DOWN)) {
            if (menu_choice < MENU_ABORT) {
                menu_choice++;
                audio_play_sample(SAMPLE_CARET_MOVE);
            }
            debounce = 8;
        } else if (((g_player1_input & INPUT_FIRE1) && !(g_player1_old_input & INPUT_FIRE1))
                   || g_key_pressed == KEY_RETURN) {

            switch (menu_choice) {
                case MENU_ABORT:
                    /* scr_disconnecting */
                    intex_disconnecting(&bg, &font);
                    running = 0;
                    break;

                case MENU_WEAPON_SUPPLIES: {
                    /* scr_weapons — weapon image browser / shop */
                    int sub_running = 1;
                    int sub_debounce = 0;
                    while (sub_running) {
                        timer_begin_frame();
                        input_poll();
                        if (g_quit_requested) { running = 0; sub_running = 0; break; }

                        video_clear();
                        if (bg.pixels) video_blit(bg.pixels, bg.w, 0, 0, bg.w, bg.h, -1);
                        draw_screen_weapons(player_idx, &font, wpic.pixels, cur_wpic);
                        video_present();

                        if (sub_debounce > 0) { sub_debounce--; continue; }
                        if ((g_player1_input & INPUT_LEFT) && !(g_player1_old_input & INPUT_LEFT)) {
                            cur_wpic = (cur_wpic - 1 + 6) % 6;
                            audio_play_sample(SAMPLE_CARET_MOVE);
                            sub_debounce = 8;
                        } else if ((g_player1_input & INPUT_RIGHT) && !(g_player1_old_input & INPUT_RIGHT)) {
                            cur_wpic = (cur_wpic + 1) % 6;
                            audio_play_sample(SAMPLE_CARET_MOVE);
                            sub_debounce = 8;
                        } else if (((g_player1_input & INPUT_FIRE1) && !(g_player1_old_input & INPUT_FIRE1))
                                   || g_key_pressed == KEY_ESC) {
                            sub_running = 0;
                        }
                    }
                    debounce = 8;
                    break;
                }

                case MENU_STATS: {
                    /* scr_stats — wait for fire to return */
                    int sub_running = 1;
                    while (sub_running) {
                        timer_begin_frame();
                        input_poll();
                        if (g_quit_requested) { running = 0; sub_running = 0; break; }

                        video_clear();
                        if (bg.pixels) video_blit(bg.pixels, bg.w, 0, 0, bg.w, bg.h, -1);
                        draw_screen_stats(player_idx, &font);
                        video_present();

                        if (((g_player1_input & INPUT_FIRE1) && !(g_player1_old_input & INPUT_FIRE1))
                            || g_key_pressed == KEY_ESC) {
                            sub_running = 0;
                        }
                    }
                    debounce = 8;
                    break;
                }

                default:
                    /* Placeholder for unimplemented sub-screens */
                    debounce = 8;
                    break;
            }
        } else if (g_key_pressed == KEY_ESC) {
            intex_disconnecting(&bg, &font);
            running = 0;
        }
    }

    /* ------------------------------------------------------------------
     * Restore game palette so the level renders correctly after exit.
     * Ref: In the original ASM the caller (main.asm) restores hardware
     * registers; in the C port we own the palette state here.
     * ------------------------------------------------------------------ */
    if (g_cur_map.valid)
        palette_set_immediate(g_cur_map.palette_a, 32);

    audio_resume_music();
    audio_play_sample(SAMPLE_INTEX_SHUTDOWN);

    free(bg.pixels);
    free(wpic.pixels);
    font_free(&font);
}
