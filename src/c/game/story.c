/*
 * Alien Breed SE 92 - C port
 * Story / intro screen — translated from src/story/story.asm
 *
 * Sequence (matches the original assembly logic):
 *
 *   Phase 1 – Planet + scrolling story text
 *     Planet background (4bpp, colors_planet palette) fades in.
 *     Story text scrolls upward in the bottom band of the screen.
 *     Fire button or end-of-text → fade out → Phase 2.
 *
 *   Phase 2 – Title fade-in (display_title_screen + display_beam_title)
 *     Title image (5bpp) appears on a black background.
 *     Palette fades from colors_down → colors_up over ~52 frames.
 *     A bright horizontal scan-line sweeps down the title (beam effect).
 *     Hold ~300 frames, then fade to black and return.
 */

#include "story.h"
#include "constants.h"
#include "../hal/video.h"
#include "../hal/input.h"
#include "../hal/audio.h"
#include "../hal/timer.h"
#include "../engine/palette.h"
#include "../engine/typewriter.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Palettes (taken directly from story.asm)                           */
/* ------------------------------------------------------------------ */

/* colors_planet — 32 entries used for the planet screen.
 * The 4bpp planet image uses indices 0-15; entries 16-31 are for the
 * text overlay (all white, so story text appears bright on the planet). */
static const UWORD k_colors_planet[32] = {
    0x000, 0xFFF, 0x222, 0x222, 0x222, 0x222, 0x222, 0x222,
    0x322, 0x422, 0x522, 0x622, 0x722, 0x822, 0x922, 0xB32,
    0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF,
    0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF
};

/* colors_down — dark fade-in palette for the title screen */
static const UWORD k_colors_down[32] = {
    0x000, 0x770, 0x000, 0x110, 0x221, 0x332, 0x443, 0x554,
    0x665, 0x776, 0x887, 0x998, 0xAA9, 0xBBB, 0xCCC, 0xDDD,
    0x000, 0x000, 0x000, 0x111, 0x222, 0x333, 0x444, 0x555,
    0x666, 0x777, 0x888, 0x999, 0xAAA, 0xBBB, 0xCCC, 0xDDD
};

/* colors_up — bright final palette for the title screen */
static const UWORD k_colors_up[32] = {
    0x000, 0x990, 0x221, 0x332, 0x443, 0x554, 0x665, 0x776,
    0x887, 0x998, 0xAA9, 0xBBA, 0xCCB, 0xDDD, 0xEEE, 0xFFF,
    0x000, 0x111, 0x222, 0x333, 0x444, 0x555, 0x666, 0x777,
    0x888, 0x999, 0xAAA, 0xBBB, 0xCCC, 0xDDD, 0xEEE, 0xFFF
};

/* ------------------------------------------------------------------ */
/* Story text (from text_story in story.asm, 35-char blocks)          */
/* ------------------------------------------------------------------ */
static const char * const k_story_lines[] = {
    "THE YEAR IS 2191 AND THE GALAXY",
    "STANDS ON THE BRINK OF WAR, ONLY",
    "THE INTER PLANETARY CORPS MAINTAIN",
    "THE UNEASY PEACE. IPC MEMBERS",
    "JOHNSON AND STONE WERE HEADING FOR",
    "FEDERATION HQ AFTER SIX MONTHS ON",
    "ROUTINE PATROL AROUND THE INTEX",
    "NETWORK. NOTHING HAD HAPPENED AND",
    "NOTHING EVER DID IN THIS GOD",
    "FORSAKEN PLACE.. THEY WERE GLAD TO",
    "BE GOING HOME.",
    "", "", "", "", "", "",
    "THEN CAME THE ORDERS TO CHECK OUT A",
    "DISTANT SPACE RESEARCH CENTRE WHICH",
    "HAD FAILED TO TRANSMIT ON ANY OF",
    "THE FEDERATION WAVEBANDS. ISRC4 WAS",
    "SITUATED NEAR THE RED GIANT GIANOR",
    "AND WAS THE LAST PLACE THEY WANTED",
    "TO GO... LITTLE DID THEY KNOW WHAT",
    "LAY AHEAD... THEY WERE HEADING",
    "STRAIGHT INTO THE MIDST OF AN ALIEN",
    "BREED.",
    "", "", "", "", "", "", "", "", "", "",
    NULL
};

/* ------------------------------------------------------------------ */
/* Image helpers                                                       */
/* ------------------------------------------------------------------ */
typedef struct { UBYTE *pixels; int w, h; } StoryImg;

static int img_load(StoryImg *img, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "story: cannot open '%s'\n", path);
        return -1;
    }
    if (fread(&img->w, 4, 1, f) != 1 || fread(&img->h, 4, 1, f) != 1) {
        fprintf(stderr, "story: header read error in '%s'\n", path);
        fclose(f); return -1;
    }
    size_t sz = (size_t)(img->w * img->h);
    img->pixels = (UBYTE *)malloc(sz);
    if (!img->pixels) { fclose(f); return -1; }
    if (fread(img->pixels, 1, sz, f) != sz) {
        fprintf(stderr, "story: pixel read error in '%s'\n", path);
        free(img->pixels); img->pixels = NULL;
        fclose(f); return -1;
    }
    fclose(f);
    return 0;
}

/* ------------------------------------------------------------------ */
/* run_title_phase — display_title_screen + display_beam_title        */
/*                                                                     */
/* Shared by story_run() (called at the end after the planet) and     */
/* story_title_run() (called standalone at startup, before the menu). */
/* ------------------------------------------------------------------ */
static void run_title_phase(StoryImg *title)
{
    static UWORD s_pal[32] = {0};

    /* Fade from black → colors_down (matches display_title_screen
     * window-open animation, ~52 frames) */
    palette_prep_fade_in(k_colors_down, s_pal, 32);

    int fade_frames = 0;
    while (!g_quit_requested && !g_done_fade) {
        timer_begin_frame();
        input_poll();

        video_clear();
        if (title->pixels)
            video_blit(title->pixels, title->w, 0, 0, 320, 256, -1);

        palette_tick();
        video_present();

        if (++fade_frames >= 52) break;
    }

    if (g_quit_requested) return;

    /* Fade colors_down → colors_up while the beam sweeps down
     * (display_beam_title; hold ~300 frames) */
    palette_prep_fade_to_rgb(k_colors_up, s_pal, 32);
    {
        int beam_y    = 0;
        int beam_done = 0;
        int hold      = 0;

        while (!g_quit_requested) {
            timer_begin_frame();
            input_poll();

            if ((g_player1_input & INPUT_FIRE1) ||
                 g_key_pressed == KEY_SPACE      ||
                 g_key_pressed == KEY_RETURN      ||
                 g_key_pressed == KEY_ESC)
                break;

            video_clear();
            if (title->pixels)
                video_blit(title->pixels, title->w, 0, 0, 320, 256, -1);

            /* Bright scan-line beam (simulates hardware sprite in asm) */
            if (!beam_done) {
                video_fill_rect(0, beam_y, 320, 2, 1);
                beam_y++;
                if (beam_y >= 256) beam_done = 1;
            }

            palette_tick();
            video_present();

            if (beam_done) {
                if (++hold >= 300) break;
            }
        }
    }

    if (g_quit_requested) return;

    /* Fade to black */
    {
        static UWORD s_black[32] = {0};
        palette_prep_fade_to_rgb(s_black, s_pal, 32);
        for (int i = 0; i < 40 && !g_done_fade && !g_quit_requested; i++) {
            timer_begin_frame(); input_poll();
            palette_tick(); video_present();
        }
        palette_set_immediate(s_black, 32);
    }
}

/* ------------------------------------------------------------------ */
/* story_title_run — display_title_screen + display_beam_title only   */
/*                                                                     */
/* Reproduces the standalone title executable from the original CD32  */
/* game (no sources available).  Called once at program startup before */
/* the menu loop, so the user sees the title before the attract mode.  */
/* ------------------------------------------------------------------ */
void story_title_run(void)
{
    StoryImg title = {NULL, 0, 0};
    img_load(&title, "assets/gfx/story_title_320x256.raw");
    run_title_phase(&title);
    free(title.pixels);
}

/* ------------------------------------------------------------------ */
/* story_run — full story sequence: planet then title                 */
/*                                                                     */
/* Matches story.asm: set_planet_pic → scroll loop → fade_out_planet  */
/* → display_title_screen → display_beam_title.                        */
/* Called on auto-exit (credits exhausted) from the menu loop.         */
/* ------------------------------------------------------------------ */
void story_run(void)
{
    StoryImg planet = {NULL, 0, 0};
    StoryImg title  = {NULL, 0, 0};
    Font     font   = {0};

    img_load(&planet, "assets/gfx/story_planet_320x256.raw");
    img_load(&title,  "assets/gfx/story_title_320x256.raw");
    font_load(&font,  "assets/fonts/font_16x462.raw", 9, 11, 0);  /* TEXT_LETTER_WIDTH=9 from story.asm font_struct */

    /* ============================================================ */
    /* Phase 1: Planet + scrolling story text                       */
    /* (matches set_planet_pic → fade_in_planet → scroll loop)     */
    /* ============================================================ */
    {
        static UWORD s_cur_pal[32] = {0};
        palette_prep_fade_in(k_colors_planet, s_cur_pal, 32);

        /* scroll_y: y-coordinate of the first story line.
         * Starts at 256 (below screen) and moves upward at
         * 1 pixel per 2 frames, matching the asm blitter-scroll rate. */
        int scroll_y  = 256;
        int frame_cnt = 0;
        int num_lines = 0;

        while (k_story_lines[num_lines]) num_lines++;
        int line_h  = font.pixels ? (font.letter_h + 2) : 13; /* fallback line height */
        int total_h = num_lines * line_h;

        while (!g_quit_requested) {
            timer_begin_frame();
            input_poll();

            if (g_key_pressed == KEY_ESC) goto done;
            if ((g_player1_input & INPUT_FIRE1) &&
                !(g_player1_old_input & INPUT_FIRE1)) break;

            /* Advance scroll every 2 frames */
            frame_cnt++;
            if (frame_cnt >= 2) { frame_cnt = 0; scroll_y--; }

            /* All lines have scrolled off the top */
            if (scroll_y < -total_h) break;

            video_clear();
            if (planet.pixels)
                video_blit(planet.pixels, planet.w, 0, 0, 320, 256, -1);

            /* Draw visible story lines.
             * Font pixel index 1 maps to k_colors_planet[1] = 0xFFF (white). */
            if (font.pixels) {
                for (int i = 0; i < num_lines; i++) {
                    int ly = scroll_y + i * line_h;
                    if (ly >= 256 || ly < -line_h) continue;
                    if (!k_story_lines[i] || !k_story_lines[i][0]) continue;
                    TextCtx ctx;
                    typewriter_init_ctx(&ctx, &font,
                                        g_framebuffer, 320, 8, ly);
                    typewriter_display(&ctx, k_story_lines[i]);
                }
            }

            palette_tick();
            video_present();
        }

        if (g_quit_requested) goto done;

        /* Fade out planet (fade_out_planet in story.asm) */
        {
            static UWORD s_black[32] = {0};
            palette_prep_fade_to_rgb(s_black, s_cur_pal, 32);
            for (int i = 0; i < 40 && !g_done_fade && !g_quit_requested; i++) {
                timer_begin_frame(); input_poll();
                palette_tick(); video_present();
            }
            palette_set_immediate(s_black, 32);
        }
    }

    if (g_quit_requested) goto done;

    /* ============================================================ */
    /* Phase 2: Title on black (display_title_screen +             */
    /*          display_beam_title from story.asm)                  */
    /* ============================================================ */
    run_title_phase(&title);

done:
    free(planet.pixels);
    free(title.pixels);
    font_free(&font);
}
