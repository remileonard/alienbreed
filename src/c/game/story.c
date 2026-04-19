/*
 * Alien Breed SE 92 - C port
 * Story/intro screen — translated from src/story/story.asm
 *
 * Displays the planet picture with title, then scrolls intro text.
 * Equivalent sequence: set_planet_pic → display_title_screen →
 *   scroll_text (move_beam loop) → wait fire or timeout → fade_out_pic
 */

#include "story.h"
#include "constants.h"
#include "../hal/video.h"
#include "../hal/input.h"
#include "../hal/audio.h"
#include "../hal/timer.h"
#include "../engine/palette.h"
#include "../engine/typewriter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *k_story_text =
    "IN THE YEAR 2191  THE UNITED EARTH SPACE CORPS\n"
    "ESTABLISHED A SERIES OF RESEARCH STATIONS\n"
    "IN THE REMOTE OUTER REACHES OF THE GALAXY\n\n"
    "ALL CONTACT WITH STATION SIGMA SIX HAS BEEN LOST\n\n"
    "SPECIAL OPERATIONS SQUAD HAS BEEN DISPATCHED\n"
    "TO INVESTIGATE";

void story_run(void)
{
    /* Load resources */
    typedef struct { UBYTE *pixels; int w, h; } Img;
    Img planet = {NULL, 0, 0}, title = {NULL, 0, 0};

    FILE *f = fopen("assets/gfx/planet_320x256.raw", "rb");
    if (f) {
        fread(&planet.w, 4, 1, f); fread(&planet.h, 4, 1, f);
        planet.pixels = (UBYTE *)malloc((size_t)(planet.w * planet.h));
        if (planet.pixels) fread(planet.pixels, 1, (size_t)(planet.w * planet.h), f);
        fclose(f);
    }
    f = fopen("assets/gfx/title_320x256.raw", "rb");
    if (f) {
        fread(&title.w, 4, 1, f); fread(&title.h, 4, 1, f);
        title.pixels = (UBYTE *)malloc((size_t)(title.w * title.h));
        if (title.pixels) fread(title.pixels, 1, (size_t)(title.w * title.h), f);
        fclose(f);
    }

    Font font = {0};
    font_load(&font, "assets/fonts/font_16x462.raw", 16, 11, 0);

    /* Fade in */
    /* colors_planet from story.asm — used for the planet background */
    static const UWORD k_planet_pal[] = {
        0x000, 0xFFF, 0x222, 0x222, 0x222, 0x222, 0x222, 0x222,
        0x322, 0x422, 0x522, 0x622, 0x722, 0x822, 0x922, 0xB32,
        0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF,
        0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF
    };
    /* colors_up from story.asm — used for the title/text screen */
    static const UWORD k_story_pal[] = {
        0x000, 0x990, 0x221, 0x332, 0x443, 0x554, 0x665, 0x776,
        0x887, 0x998, 0xAA9, 0xBBA, 0xCCB, 0xDDD, 0xEEE, 0xFFF,
        0x000, 0x111, 0x222, 0x333, 0x444, 0x555, 0x666, 0x777,
        0x888, 0x999, 0xAAA, 0xBBB, 0xCCC, 0xDDD, 0xEEE, 0xFFF
    };

    /* Phase 1: show planet with planet palette */
    static UWORD cur_pal[32] = {0};
    palette_prep_fade_in(k_planet_pal, cur_pal, 32);
    for (int i = 0; i < 25; i++) {
        timer_begin_frame();
        if (planet.pixels) video_blit(planet.pixels, planet.w, 0, 0, 320, 256, -1);
        palette_tick(); video_present();
    }

    /* Phase 2: switch to title palette for text scroll */
    memcpy(cur_pal, k_planet_pal, sizeof(cur_pal));
    palette_prep_fade_in(k_story_pal, cur_pal, 32);

    int scroll_y  = 256;   /* text starts below screen, scrolls up */
    int done      = 0;
    int timeout   = 50 * 20;  /* max 20 seconds */

    while (!done && timeout-- > 0) {
        timer_begin_frame();
        input_poll();

        if (g_quit_requested) break;
        if ((g_player1_input & INPUT_FIRE1) || g_key_pressed == KEY_SPACE) break;

        /* Draw planet */
        video_clear();
        if (planet.pixels)
            video_blit(planet.pixels, planet.w, 0, 0, 320, 256, -1);

        /* Draw title overlay */
        if (title.pixels)
            video_blit(title.pixels, title.w, 0, 0, 320,
                       (title.h < 256 ? title.h : 256), 0);

        /* Draw scrolling story text */
        if (font.pixels) {
            TextCtx ctx;
            typewriter_init_ctx(&ctx, &font, g_framebuffer, 320, 20, scroll_y);
            typewriter_display(&ctx, k_story_text);
        }
        scroll_y--;
        if (scroll_y < -300) scroll_y = 256;  /* loop */

        palette_tick();
        video_present();
    }

    /* Fade out */
    static UWORD k_black[32] = {0};
    palette_prep_fade_to_rgb(k_black, cur_pal, 32);
    for (int i = 0; i < 25; i++) {
        timer_begin_frame();
        palette_tick();
        video_present();
    }

    free(planet.pixels);
    free(title.pixels);
    font_free(&font);
}
