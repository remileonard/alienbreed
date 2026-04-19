/*
 * Alien Breed SE 92 - C port
 * INTEX terminal — translated from src/intex/intex.asm
 *
 * Screens: WEAPONS / TOOL SUPPLIES / MAP / BRIEFING / STATS / INFOS / HOLOCODE
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
#include "../engine/typewriter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INTEX_SCREEN_WEAPONS  0
#define INTEX_SCREEN_SUPPLIES 1
#define INTEX_SCREEN_MAP      2
#define INTEX_SCREEN_BRIEFING 3
#define INTEX_SCREEN_STATS    4
#define INTEX_SCREEN_EXIT     5
#define INTEX_SCREENS         6

static const char *k_screen_names[] = {
    "WEAPONS", "SUPPLIES", "MAP", "BRIEFING", "STATS", "EXIT"
};

static void draw_screen_weapons(int pidx, Font *font)
{
    Player *p = &g_players[pidx];
    TextCtx ctx;
    typewriter_init_ctx(&ctx, font, g_framebuffer, 320, 20, 30);
    typewriter_display(&ctx, "AVAILABLE WEAPONS:");

    static const char *k_wnames[] = {"", "MACHINEGUN", "TWINFIRE", "FLAMEARC",
                                      "PLASMAGUN", "FLAMETHROWER", "SIDEWINDERS", "LAZER"};
    for (int w = WEAPON_MACHINEGUN; w < WEAPON_MAX; w++) {
        char line[64];
        snprintf(line, sizeof(line), "%s%s",
                 k_wnames[w],
                 p->owned_weapons[w-1] ? " [OWNED]" : "");
        ctx.cursor_x = 20;
        ctx.cursor_y += font->letter_h + 2;
        typewriter_display(&ctx, line);
    }
}

static void draw_screen_stats(int pidx, Font *font)
{
    Player *p = &g_players[pidx];
    char buf[128];
    TextCtx ctx;
    typewriter_init_ctx(&ctx, font, g_framebuffer, 320, 20, 30);
    snprintf(buf, sizeof(buf), "SCORE: %ld", (long)p->score);
    typewriter_display(&ctx, buf);
    ctx.cursor_x = 20; ctx.cursor_y += font->letter_h + 2;
    snprintf(buf, sizeof(buf), "KILLS: %d", 0);  /* TODO: track kills */
    typewriter_display(&ctx, buf);
    ctx.cursor_x = 20; ctx.cursor_y += font->letter_h + 2;
    snprintf(buf, sizeof(buf), "SHOTS: %d", p->shots);
    typewriter_display(&ctx, buf);
}

void intex_run(int player_idx)
{
    audio_play_sample(SAMPLE_INTEX_SHUTDOWN);  /* intex_startup reuses same sfx id */
    audio_pause_music();

    /* Load INTEX background */
    typedef struct { UBYTE *pixels; int w, h; } Img;
    Img bg = {NULL, 0, 0};
    FILE *f = fopen("assets/gfx/intex_bkgnd_320x256.raw", "rb");
    if (f) {
        fread(&bg.w, 4, 1, f); fread(&bg.h, 4, 1, f);
        bg.pixels = (UBYTE *)malloc((size_t)(bg.w * bg.h));
        if (bg.pixels) fread(bg.pixels, 1, (size_t)(bg.w * bg.h), f);
        fclose(f);
    }

    Font font = {0};
    font_load(&font, "assets/fonts/font_16x504.raw", 16, 12, 0);

    /* From intex.asm: COLOR16-23 = green tones for the terminal glow */
    static const UWORD k_intex_pal[] = {
        /* COLOR00-15: loaded from background image, default black */
        0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
        0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
        /* COLOR16-23: static green terminal glow from intex.asm */
        0x555, 0x565, 0x575, 0x585, 0x595, 0x5A5, 0x5B5, 0x5C5,
        /* COLOR24-31: brighter glow extensions */
        0x5D5, 0x5E5, 0x5F5, 0x4F4, 0x3F3, 0x2F2, 0x1F1, 0x0F0
    };
    palette_set_immediate(k_intex_pal, 32);

    int cur_screen = INTEX_SCREEN_WEAPONS;
    int debounce   = 0;
    int running    = 1;

    while (running) {
        timer_begin_frame();
        input_poll();

        if (g_quit_requested) break;

        video_clear();
        if (bg.pixels)
            video_blit(bg.pixels, bg.w, 0, 0, 320, 256, -1);

        /* Draw menu tabs */
        for (int i = 0; i < INTEX_SCREENS; i++) {
            TextCtx ctx;
            typewriter_init_ctx(&ctx, &font, g_framebuffer, 320, 8 + i * 50, 10);
            if (i == cur_screen) video_fill_rect(6 + i * 50, 8, 44, 12, 3);
            typewriter_display(&ctx, k_screen_names[i]);
        }

        /* Draw active screen */
        switch (cur_screen) {
            case INTEX_SCREEN_WEAPONS:  draw_screen_weapons(player_idx, &font); break;
            case INTEX_SCREEN_STATS:    draw_screen_stats(player_idx, &font);   break;
            case INTEX_SCREEN_EXIT:     running = 0; break;
            default: break;
        }

        video_present();

        if (debounce > 0) { debounce--; continue; }

        if ((g_player1_input & INPUT_LEFT) && !(g_player1_old_input & INPUT_LEFT)) {
            cur_screen = (cur_screen - 1 + INTEX_SCREENS) % INTEX_SCREENS;
            audio_play_sample(SAMPLE_CARET_MOVE);
            debounce = 8;
        } else if ((g_player1_input & INPUT_RIGHT) && !(g_player1_old_input & INPUT_RIGHT)) {
            cur_screen = (cur_screen + 1) % INTEX_SCREENS;
            audio_play_sample(SAMPLE_CARET_MOVE);
            debounce = 8;
        } else if ((g_player1_input & INPUT_FIRE1) && !(g_player1_old_input & INPUT_FIRE1)) {
            if (cur_screen == INTEX_SCREEN_EXIT) running = 0;
            debounce = 8;
        } else if (g_key_pressed == KEY_ESC) {
            running = 0;
        }
    }

    audio_resume_music();
    audio_play_sample(SAMPLE_INTEX_SHUTDOWN);

    free(bg.pixels);
    font_free(&font);
}
