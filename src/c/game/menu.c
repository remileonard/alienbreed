/*
 * Alien Breed SE 92 - C port
 * Menu screen — translated from src/menu/menu.asm
 *
 * Title sequence (matches the original assembly logic):
 *
 *   Phase 1 – Title on black
 *     Starfield backdrop + title logo (palette_logo) fades in.
 *     Wait for any button/key press before proceeding.
 *
 *   Phase 2 – Menu
 *     Stars + title at top + copyright sliding in from the bottom.
 *     Menu items drawn with typewriter effect.
 *     1000-frame idle timeout → Phase 3.
 *     Fire on START GAME → fade out → MENU_RESULT_START.
 *
 *   Phase 3 – Credits
 *     9 credit screens, each held ~350 frames.
 *     Any button press → back to Phase 2 (matches break_main_loop_flag).
 *     All 9 credits exhausted → MENU_RESULT_AUTO_EXIT (triggers story).
 */

#include "menu.h"
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
/* Menu item indices (match pos_in_menu in menu.asm: 0/1/2)           */
/* ------------------------------------------------------------------ */
#define MENU_ITEM_NUM_PLAYERS   0
#define MENU_ITEM_SHARE_CREDITS 1
#define MENU_ITEM_START         2
#define MENU_ITEMS              3

/* ------------------------------------------------------------------ */
/* Palettes (taken directly from menu.asm)                            */
/*                                                                     */
/* The Amiga copper-list switches the palette at different scanlines   */
/* to give each layer its own 8-colour set.  In the C port we arrange  */
/* the 32 palette slots so each layer occupies a distinct range:       */
/*   0- 7  title logo   (palette_logo)                                 */
/*   8-15  starfield    (stars_palette)                                */
/*  16-23  menu text    (palette_menu, index 16 = transparent)         */
/*  24-31  copyright    (copyright_palette, index 24 = transparent)    */
/* ------------------------------------------------------------------ */
static const UWORD k_palette_logo[8] = {
    0x000, 0x111, 0x100, 0x200, 0x400, 0x800, 0xD00, 0xF30
};
static const UWORD k_stars_palette[8] = {
    0x000, 0x111, 0x222, 0x333, 0x555, 0x888, 0xAAA, 0x000
};
static const UWORD k_palette_menu[8] = {
    0x000, 0x222, 0xD31, 0xB11, 0x444, 0x333, 0x222, 0xF52
};
static const UWORD k_palette_copyright[8] = {
    0x000, 0x620, 0x720, 0x830, 0x940, 0xA40, 0xB50, 0xC60
};
/* Warm-red intermediate palette used when fading out between credits */
static const UWORD k_palette_menu_red[8] = {
    0x000, 0x822, 0xA31, 0x811, 0xF44, 0x833, 0xA22, 0x852
};

/* Combined 32-entry working palette (built from the four blocks above) */
static UWORD s_pal[32];

/* ------------------------------------------------------------------ */
/* Image helpers                                                       */
/* ------------------------------------------------------------------ */
typedef struct { UBYTE *pixels; int w, h; } Img;

static int img_load(Img *img, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fread(&img->w, 4, 1, f) != 1 || fread(&img->h, 4, 1, f) != 1) {
        fclose(f); return -1;
    }
    img->pixels = (UBYTE *)malloc((size_t)(img->w * img->h));
    if (!img->pixels) { fclose(f); return -1; }
    fread(img->pixels, 1, (size_t)(img->w * img->h), f);
    fclose(f);
    return 0;
}

static void img_free(Img *img)
{
    free(img->pixels);
    img->pixels = NULL;
}

/*
 * Blit an image onto g_framebuffer with a palette-index offset.
 * Source pixel 0 is treated as transparent (not drawn).
 * Non-zero source pixels become (pixel + offset) in the framebuffer.
 */
static void blit_offset(const Img *img, int dx, int dy, int offset)
{
    if (!img || !img->pixels) return;
    int w = img->w, h = img->h;
    for (int row = 0; row < h; row++) {
        int y = dy + row;
        if (y < 0 || y >= 256) continue;
        const UBYTE *src = img->pixels + row * w;
        for (int col = 0; col < w; col++) {
            int x = dx + col;
            if (x < 0 || x >= 320) continue;
            UBYTE px = src[col];
            if (px == 0) continue;
            g_framebuffer[y * 320 + x] = (UBYTE)(px + offset);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Simple 2-D starfield (approximates the 3-D stars from menu.asm)    */
/* ------------------------------------------------------------------ */
#define NUM_STARS 81

static struct { int x, y, col; } s_stars[NUM_STARS];

static void stars_init(void)
{
    unsigned rng = 0x4D373729u;
    for (int i = 0; i < NUM_STARS; i++) {
        rng = rng * 1664525u + 1013904223u;
        s_stars[i].x = (int)(rng % 320);
        rng = rng * 1664525u + 1013904223u;
        s_stars[i].y = (int)(rng % 240);
        rng = rng * 1664525u + 1013904223u;
        s_stars[i].col = (int)(rng % 6) + 1;   /* 1–6 brightness */
    }
}

static void stars_update(void)
{
    for (int i = 0; i < NUM_STARS; i++) {
        int speed = s_stars[i].col / 2 + 1;
        s_stars[i].x = (s_stars[i].x + speed) % 320;
    }
}

/*
 * Draw stars using palette slots 8–14 (offset 8, value 1–6 → slots 9–14).
 * Stars are rendered before the title so the title logo covers them.
 */
static void stars_draw(void)
{
    for (int i = 0; i < NUM_STARS; i++) {
        int x = s_stars[i].x;
        int y = s_stars[i].y;
        if (x >= 0 && x < 320 && y >= 0 && y < 256)
            g_framebuffer[y * 320 + x] = (UBYTE)(8 + s_stars[i].col);
    }
}

/* ------------------------------------------------------------------ */
/* Palette helpers                                                     */
/* ------------------------------------------------------------------ */
static void build_combined_palette(void)
{
    for (int i = 0; i < 8; i++) {
        s_pal[i]      = k_palette_logo[i];
        s_pal[i +  8] = k_stars_palette[i];
        s_pal[i + 16] = k_palette_menu[i];
        s_pal[i + 24] = k_palette_copyright[i];
    }
}

/*
 * Fade all 32 palette slots to black and wait for completion.
 * Uses a tight frame loop (at most ~30 frames).
 */
static void fade_to_black_and_wait(void)
{
    static const UWORD k_black[32] = {0};
    palette_prep_fade_to_rgb(k_black, s_pal, 32);
    for (int i = 0; i < 40 && !g_done_fade; i++) {
        timer_begin_frame();
        palette_tick();
        video_present();
    }
    palette_set_immediate(k_black, 32);
}

/* ------------------------------------------------------------------ */
/* Credits text (from disp_credits / text_credits* in menu.asm)       */
/* ------------------------------------------------------------------ */
static const char *k_credits[9] = {
    "   PROGRAMMED BY:\n   ANDREAS TADIC\n    PETER TULEBY",
    "GRAPHICS AND CONCEPT\n         BY:\n     RICO HOLMES",
    " SOUND AND MUSIC BY:\n  ALLISTER BRIMBLE",
    "ADDITIONAL CODE AND\nCD32 PROGRAMMING BY:\n   STEFAN BOBERG",
    "SPECIAL EDITION CODING:\n     ANDREAS TADIC",
    "SPEECH COURTESY OF:\n   LYNETTE READE",
    "  GAME DESIGNED BY:\n     RICO HOLMES\n     MARTYN BROWN",
    " PROJECT MANAGEMENT\n         BY:\n    MARTYN BROWN",
    " A TEAM 17 SOFTWARE\n     PRODUCTION\n        1992"
};

/* ------------------------------------------------------------------ */
/* Draw the full menu scene (background + title + stars + optional     */
/* copyright + optional menu items).                                   */
/* ------------------------------------------------------------------ */
static void draw_bg(const Img *title, int copyright_y, const Img *copy)
{
    video_clear();
    stars_update();
    stars_draw();
    /* Title uses raw indices 0–7 (palette_logo, offset 0): transparent=0 */
    blit_offset(title, 0, 0, 0);
    /* Copyright uses indices 0–7 offset by 24 (palette_copyright) */
    if (copy && copy->pixels && copyright_y < 256)
        blit_offset(copy, 0, copyright_y, 24);
}

/* ------------------------------------------------------------------ */
/* menu_run                                                            */
/* ------------------------------------------------------------------ */
MenuResult menu_run(int *out_num_players, int *out_share_credits)
{
    build_combined_palette();

    /* Load resources */
    Img  title = {NULL, 0, 0};
    Img  copy  = {NULL, 0, 0};
    Font font  = {0};

    img_load(&title, "assets/gfx/menu_title_320x180.raw");
    img_load(&copy,  "assets/gfx/menu_copyright_320x16.raw");
    font_load(&font, "assets/fonts/font_16x672.raw", 16, 16, 0);

    stars_init();

    /* Default values */
    MenuResult result      = MENU_RESULT_START;
    *out_num_players       = 1;
    *out_share_credits     = 0;
    int cur_item           = MENU_ITEM_START;   /* same default as pos_in_menu=2 in asm */

    /* ============================================================ */
    /* Phase 1: Title on black — fade in, then wait for button      */
    /* (matches setup_copperlist fade-in + wait_button_press)       */
    /* ============================================================ */
    {
        static UWORD s_zero[32] = {0};
        palette_prep_fade_in(s_pal, s_zero, 32);

        int btn_seen = 0;
        while (!g_quit_requested) {
            timer_begin_frame();
            input_poll();

            if (g_key_pressed == KEY_ESC) { result = MENU_RESULT_QUIT; goto cleanup; }

            /* A button press is only acted on AFTER the fade is complete
             * (matches wait_button_press after setup_copperlist returns). */
            if (g_done_fade &&
                ((g_player1_input & INPUT_FIRE1) ||
                  g_key_pressed == KEY_SPACE     ||
                  g_key_pressed == KEY_RETURN)) {
                btn_seen = 1;
            }

            draw_bg(&title, 300, NULL);   /* no copyright yet */
            palette_tick();
            video_present();

            if (btn_seen) break;
        }
        if (g_quit_requested) { result = MENU_RESULT_QUIT; goto cleanup; }
    }

    /* Ensure palette is fully applied before entering Phase 2 */
    palette_set_immediate(s_pal, 32);

    /* ============================================================ */
    /* Phase 2 → Phase 3 outer loop                                 */
    /* (matches main_loop in menu.asm)                              */
    /* ============================================================ */
phase2_entry:
    {
        /* -------------------------------------------------------- */
        /* Phase 2: Menu                                            */
        /* -------------------------------------------------------- */
        int copyright_y = 256;   /* copyright slides in from below */
        int frames_idle = 0;
        int debounce    = 0;

        while (!g_quit_requested) {
            timer_begin_frame();
            input_poll();

            if (g_key_pressed == KEY_ESC) { result = MENU_RESULT_QUIT; goto cleanup; }

            /* Slide copyright up from the bottom */
            if (copyright_y > 240) copyright_y--;

            draw_bg(&title, copyright_y, &copy);

            /* Build menu label strings */
            char label_np[32], label_sc[32];
            if (*out_num_players == 1)
                (void)snprintf(label_np, sizeof(label_np), " ONE PLAYER GAME");
            else
                (void)snprintf(label_np, sizeof(label_np), " TWO PLAYER GAME");
            if (*out_share_credits)
                (void)snprintf(label_sc, sizeof(label_sc), "SHARE CREDITS  ON ");
            else
                (void)snprintf(label_sc, sizeof(label_sc), "SHARE CREDITS  OFF");

            const char *items[MENU_ITEMS];
            items[MENU_ITEM_NUM_PLAYERS]   = label_np;
            items[MENU_ITEM_SHARE_CREDITS] = label_sc;
            items[MENU_ITEM_START]         = "    START GAME    ";

            /*
             * Screen layout (matches pos_in_menu*13 + 167 from menu.asm):
             *   item 0 at y=179, item 1 at y=192, item 2 (START) at y=205
             * Text is drawn using palette_menu (slots 16-23, offset=16).
             */
            for (int i = 0; i < MENU_ITEMS; i++) {
                int item_y = 167 + i * 13;
                /* Highlight selected item with a filled bar in slot 22 */
                if (i == cur_item)
                    video_fill_rect(68, item_y, 184, 12, 22);

                TextCtx ctx;
                typewriter_init_ctx(&ctx, &font, g_framebuffer, 320, 68, item_y);
                ctx.color_offset = 16;
                typewriter_display(&ctx, items[i]);
            }

            palette_tick();
            video_present();

            /* Input with debounce */
            if (debounce > 0) {
                debounce--;
                goto count_idle;
            }

            if ((g_player1_input & INPUT_UP) && !(g_player1_old_input & INPUT_UP)) {
                cur_item = (cur_item - 1 + MENU_ITEMS) % MENU_ITEMS;
                audio_play_sample(SAMPLE_CARET_MOVE);
                debounce = 8; frames_idle = 0;

            } else if ((g_player1_input & INPUT_DOWN) && !(g_player1_old_input & INPUT_DOWN)) {
                cur_item = (cur_item + 1) % MENU_ITEMS;
                audio_play_sample(SAMPLE_CARET_MOVE);
                debounce = 8; frames_idle = 0;

            } else if ((g_player1_input & INPUT_FIRE1) && !(g_player1_old_input & INPUT_FIRE1)) {
                frames_idle = 0;
                switch (cur_item) {
                    case MENU_ITEM_START:
                        result = MENU_RESULT_START;
                        fade_to_black_and_wait();
                        goto cleanup;
                    case MENU_ITEM_NUM_PLAYERS:
                        *out_num_players = (*out_num_players == 1) ? 2 : 1;
                        debounce = 8;
                        break;
                    case MENU_ITEM_SHARE_CREDITS:
                        *out_share_credits = !(*out_share_credits);
                        debounce = 8;
                        break;
                }
            }

count_idle:
            /* Reset idle counter on any joystick input */
            if (g_player1_input & (INPUT_UP | INPUT_DOWN | INPUT_FIRE1 | INPUT_LEFT | INPUT_RIGHT))
                frames_idle = 0;
            else
                frames_idle++;

            /* 1000-frame idle timeout → credits (matches menu.asm) */
            if (frames_idle >= 1000)
                break;
        }
        if (g_quit_requested) { result = MENU_RESULT_QUIT; goto cleanup; }

        /* -------------------------------------------------------- */
        /* Phase 3: Credits                                         */
        /* (matches disp_credits in menu.asm)                      */
        /* -------------------------------------------------------- */
        for (int cidx = 0; cidx < 9; cidx++) {
            const char *credit = k_credits[cidx];

            /* Count printable characters for typewriter pacing */
            int total_chars = 0;
            for (const char *p = credit; *p; p++)
                if (*p != '\n') total_chars++;

            int chars_shown  = 0;
            int hold_frames  = 0;
            int btn_pressed  = 0;

            /* Display credit with typewriter, then hold ~350 frames */
            while (!g_quit_requested) {
                timer_begin_frame();
                input_poll();

                if (g_key_pressed == KEY_ESC) { result = MENU_RESULT_QUIT; goto cleanup; }

                /* Button during credits → back to menu (break_main_loop_flag) */
                if ((g_player1_input & INPUT_FIRE1) ||
                     g_key_pressed == KEY_SPACE      ||
                     g_key_pressed == KEY_RETURN) {
                    btn_pressed = 1;
                }

                /* Draw background */
                draw_bg(&title, 300, NULL);

                /* Render only the first chars_shown characters */
                {
                    TextCtx ctx;
                    typewriter_init_ctx(&ctx, &font, g_framebuffer, 320, 68, 136);
                    ctx.color_offset = 16;
                    int cnt = 0;
                    for (const char *p = credit; *p && cnt < chars_shown; p++) {
                        if (*p == '\n') {
                            ctx.cursor_x = ctx.start_x;
                            ctx.cursor_y += ctx.font->letter_h + 1;
                        } else {
                            typewriter_putchar(&ctx, *p);
                            cnt++;
                        }
                    }
                }

                palette_tick();
                video_present();

                /* Typewriter: reveal one more character per frame */
                if (chars_shown < total_chars)
                    chars_shown++;
                else
                    hold_frames++;

                if (btn_pressed || hold_frames >= 350) break;
            }

            if (g_quit_requested) { result = MENU_RESULT_QUIT; goto cleanup; }

            /* Button during credits → back to Phase 2 */
            if (btn_pressed) goto phase2_entry;

            /* Fade menu-text palette slots to red then to black between credits */
            {
                /* Build a target palette: menu slots → red, rest unchanged */
                UWORD pal_red[32];
                for (int i = 0;  i < 8;  i++) pal_red[i]      = k_palette_logo[i];
                for (int i = 0;  i < 8;  i++) pal_red[i +  8] = k_stars_palette[i];
                for (int i = 0;  i < 8;  i++) pal_red[i + 16] = k_palette_menu_red[i];
                for (int i = 0;  i < 8;  i++) pal_red[i + 24] = k_palette_copyright[i];

                palette_prep_fade_to_rgb(pal_red, s_pal, 32);
                for (int i = 0; i < 20 && !g_quit_requested; i++) {
                    timer_begin_frame(); input_poll();
                    palette_tick(); video_present();
                    if (g_done_fade) break;
                }
                /* Copy pal_red into s_pal for the next fade step */
                for (int i = 0; i < 32; i++) s_pal[i] = pal_red[i];

                fade_to_black_and_wait();
                /* Restore combined palette for the next credit */
                build_combined_palette();
                palette_set_immediate(s_pal, 32);
            }
        }

        /* All 9 credits exhausted: trigger story sequence */
        result = MENU_RESULT_AUTO_EXIT;
        fade_to_black_and_wait();
        goto cleanup;
    }

cleanup:
    img_free(&title);
    img_free(&copy);
    font_free(&font);
    return result;
}

