/*
 * Alien Breed SE 92 - C port
 * Menu screen — translated from src/menu/menu.asm
 *
 * Displays the main menu with options:
 *   START GAME / NUMBER OF PLAYERS / SHARE CREDITS
 * and handles joystick/keyboard navigation.
 */

#include "menu.h"
#include "hud.h"
#include "constants.h"
#include "../hal/video.h"
#include "../hal/input.h"
#include "../hal/audio.h"
#include "../hal/timer.h"
#include "../engine/palette.h"
#include "../engine/typewriter.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Menu items (mirrors check_menu_pos logic in menu.asm)              */
/* ------------------------------------------------------------------ */
#define MENU_ITEM_START         0
#define MENU_ITEM_NUM_PLAYERS   1
#define MENU_ITEM_SHARE_CREDITS 2
#define MENU_ITEMS              3

static int s_cur_item   = 0;
static int s_debounce   = 0;  /* frames to wait before accepting input again */

/* Background graphic */
typedef struct { UBYTE *pixels; int w, h; } GfxImg;
static GfxImg s_title;
static GfxImg s_copyright;
static Font   s_font = {0};

static int load_gfx(GfxImg *img, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    int w, h;
    if (fread(&w, 4, 1, f) != 1 || fread(&h, 4, 1, f) != 1) { fclose(f); return -1; }
    img->w = w; img->h = h;
    img->pixels = (UBYTE *)malloc((size_t)(w * h));
    if (!img->pixels) { fclose(f); return -1; }
    fread(img->pixels, 1, (size_t)(w * h), f);
    fclose(f);
    return 0;
}

MenuResult menu_run(int *out_num_players, int *out_share_credits)
{
    /* Load resources */
    load_gfx(&s_title,     "assets/gfx/title_320x180.raw");
    load_gfx(&s_copyright, "assets/gfx/copyright_320x16.raw");
    font_load(&s_font,     "assets/fonts/font_16x672.raw", 16, 16, 0);

    /* palette_logo + palette_menu from menu.asm */
    static const UWORD k_menu_palette[] = {
        /* palette_logo — 8 colors for the title graphic (lo3 = 3bpp) */
        0x000, 0x111, 0x100, 0x200, 0x400, 0x800, 0xD00, 0xF30,
        /* palette_menu — 8 colors for menu font/UI */
        0x000, 0x222, 0xD31, 0xB11, 0x444, 0x333, 0x222, 0xF52,
        /* extra 16 colors (shadows/sprites) */
        0x000, 0x111, 0x222, 0x333, 0x444, 0x500, 0x700, 0x900,
        0x000, 0x111, 0x222, 0x333, 0x444, 0x555, 0x666, 0x777
    };
    palette_set_immediate(k_menu_palette, 32);

    int running     = 1;
    MenuResult result = MENU_RESULT_START;
    *out_num_players   = 1;
    *out_share_credits = 0;
    s_cur_item         = 0;
    s_debounce         = 0;

    while (running) {
        timer_begin_frame();
        input_poll();

        if (g_quit_requested) { result = MENU_RESULT_QUIT; break; }

        /* Draw background */
        video_clear();
        if (s_title.pixels)
            video_blit(s_title.pixels, s_title.w, 0, 0, 320, 180, -1);
        if (s_copyright.pixels)
            video_blit(s_copyright.pixels, s_copyright.w, 0, 240, 320, 16, -1);

        /* Draw menu items */
        static const char *k_labels[] = {
            "START GAME",
            "1 PLAYER",
            "NO SHARE CREDITS"
        };
        char label_np[32], label_sc[32];
        snprintf(label_np, sizeof(label_np), "%d PLAYER%s",
                 *out_num_players, *out_num_players > 1 ? "S" : "");
        snprintf(label_sc, sizeof(label_sc), "%s SHARE CREDITS",
                 *out_share_credits ? "YES" : "NO");

        const char *items[MENU_ITEMS] = {
            "START GAME",
            label_np,
            label_sc
        };
        (void)k_labels;

        for (int i = 0; i < MENU_ITEMS; i++) {
            TextCtx ctx;
            typewriter_init_ctx(&ctx, &s_font,
                                g_framebuffer, 320,
                                96, 190 + i * 18);
            /* Highlight selected item */
            if (i == s_cur_item) {
                video_fill_rect(90, 190 + i * 18 - 1, 140, 17, 3);
            }
            typewriter_display(&ctx, items[i]);
        }

        palette_tick();
        video_present();

        /* Input handling with debounce */
        if (s_debounce > 0) { s_debounce--; continue; }

        if ((g_player1_input & INPUT_UP) &&
            !(g_player1_old_input & INPUT_UP)) {
            s_cur_item = (s_cur_item - 1 + MENU_ITEMS) % MENU_ITEMS;
            audio_play_sample(SAMPLE_CARET_MOVE);
            s_debounce = 8;
        } else if ((g_player1_input & INPUT_DOWN) &&
                   !(g_player1_old_input & INPUT_DOWN)) {
            s_cur_item = (s_cur_item + 1) % MENU_ITEMS;
            audio_play_sample(SAMPLE_CARET_MOVE);
            s_debounce = 8;
        } else if ((g_player1_input & INPUT_FIRE1) &&
                   !(g_player1_old_input & INPUT_FIRE1)) {
            switch (s_cur_item) {
                case MENU_ITEM_START:
                    running = 0;
                    result  = MENU_RESULT_START;
                    break;
                case MENU_ITEM_NUM_PLAYERS:
                    *out_num_players = (*out_num_players == 1) ? 2 : 1;
                    s_debounce = 8;
                    break;
                case MENU_ITEM_SHARE_CREDITS:
                    *out_share_credits = !(*out_share_credits);
                    s_debounce = 8;
                    break;
            }
        } else if (g_key_pressed == KEY_ESC) {
            result  = MENU_RESULT_QUIT;
            running = 0;
        }
    }

    /* Fade out before returning */
    static UWORD k_black[32] = {0};
    palette_prep_fade_to_rgb(k_black, (UWORD *)k_menu_palette, 32);
    for (int i = 0; i < 30; i++) {
        timer_begin_frame();
        palette_tick();
        video_present();
    }

    free(s_title.pixels);     s_title.pixels     = NULL;
    free(s_copyright.pixels); s_copyright.pixels = NULL;
    font_free(&s_font);

    return result;
}
