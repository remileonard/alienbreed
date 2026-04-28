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

/*
 * Weapon image atlas: assets/gfx/intex_weapons_320x264.raw
 * Decoded from weapons_264x40.lo4 (4 planes, sequential, 320×264).
 * Six images in a 2-column × 3-row grid, each 160×88 px.
 * Ref: weapons_pic_table in intex.asm — offsets within the sequential file:
 *   MACHINEGUN   → y=176, x=0      (dc.l weapons_pic+(176*40))
 *   TWINFIRE     → y=0,   x=0      (dc.l weapons_pic)
 *   FLAMEARC     → y=0,   x=160    (dc.l weapons_pic+20)
 *   PLASMAGUN    → y=176, x=160    (dc.l weapons_pic+(176*40)+20)
 *   FLAMETHROWER → y=88,  x=160    (dc.l weapons_pic+(88*40)+20)
 *   SIDEWINDERS  → y=88,  x=0      (dc.l weapons_pic+(88*40))
 */
#define INTEX_WEAPON_IMG_W    160
#define INTEX_WEAPON_IMG_H     88
#define INTEX_WEAPON_ATLAS_W  320
#define INTEX_WEAPON_ATLAS_H  264

/* Atlas row and column pixel origin per weapon (index matches WEAPON_* enum - 1). */
static const int k_wpic_row_px[6]  = { 176,  0,   0, 176,  88,  88 };
static const int k_wpic_col_px[6]  = {   0,  0, 160, 160, 160,   0 };
static const char *k_wpic_names[6] = {
    "MACHINEGUN", "TWINFIRE", "FLAMEARC", "PLASMAGUN", "FLAMETHROWER", "SIDEWINDERS"
};

static void draw_screen_weapons(int pidx, Font *font,
                                const UBYTE *wpic, int cur_wpic_idx)
{
    Player *p = &g_players[pidx];
    TextCtx ctx;

    /* ---- Weapon image (left half of screen) ---- */
    if (wpic && cur_wpic_idx >= 0 && cur_wpic_idx < 6) {
        int src_x = k_wpic_col_px[cur_wpic_idx];
        int src_y = k_wpic_row_px[cur_wpic_idx];
        const UBYTE *src = wpic + ((size_t)src_y * INTEX_WEAPON_ATLAS_W + (size_t)src_x);
        video_blit(src, INTEX_WEAPON_ATLAS_W, 8, 30, INTEX_WEAPON_IMG_W, INTEX_WEAPON_IMG_H, 0);
    }

    /* ---- Weapon name ---- */
    typewriter_init_ctx(&ctx, font, g_framebuffer, 320, 8, 20);
    if (cur_wpic_idx >= 0 && cur_wpic_idx < 6)
        typewriter_display(&ctx, k_wpic_names[cur_wpic_idx]);

    /* ---- Weapon list (right side) ---- */
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
        /* Highlight currently selected weapon */
        if (idx == cur_wpic_idx) video_fill_rect(173, ctx.cursor_y - 1, 144, font->letter_h + 1, 3);
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

    /* Load weapon atlas (weapons_264x40.lo4 decoded as 320×264, 4 planes sequential).
     * Ref: intex.asm weapons_pic / weapons_pic_table */
    Img wpic = {NULL, 0, 0};
    FILE *fw = fopen("assets/gfx/intex_weapons_320x264.raw", "rb");
    if (fw) {
        fread(&wpic.w, 4, 1, fw); fread(&wpic.h, 4, 1, fw);
        if (wpic.w == INTEX_WEAPON_ATLAS_W && wpic.h == INTEX_WEAPON_ATLAS_H) {
            wpic.pixels = (UBYTE *)malloc((size_t)wpic.w * (size_t)wpic.h);
            if (wpic.pixels) fread(wpic.pixels, 1, (size_t)wpic.w * (size_t)wpic.h, fw);
        }
        fclose(fw);
    }

    Font font = {0};
    font_load(&font, "assets/fonts/font_16x504.raw", 8, 12, 0);  /* TEXT_LETTER_WIDTH=8 from intex.asm font_struct */

    /* Palette from intex.asm set_bitplanes_and_palette:
     * COLOR00-15: green gradient stored as the trailing 16 UWORD entries in
     *   bkgnd_320x256.lo4 at offset (4 * 256 * 40) = 40960 bytes.
     *   Values: 0x000, 0x010, 0x020, ..., 0x0D0, 0xFFF, 0xFFF
     *   These are the palette indices used by both the background AND the weapon
     *   sprite images (4-bitplane images use indices 0-15).
     * COLOR16-31: static green terminal glow (not used by image data). */
    static const UWORD k_intex_pal[] = {
        /* COLOR00-15: from bkgnd_320x256.lo4 trailing palette (green gradient) */
        0x000, 0x010, 0x020, 0x030, 0x040, 0x050, 0x060, 0x070,
        0x080, 0x090, 0x0A0, 0x0B0, 0x0C0, 0x0D0, 0xFFF, 0xFFF,
        /* COLOR16-23: static green terminal glow from intex.asm */
        0x555, 0x565, 0x575, 0x585, 0x595, 0x5A5, 0x5B5, 0x5C5,
        /* COLOR24-31: brighter glow extensions */
        0x5D5, 0x5E5, 0x5F5, 0x4F4, 0x3F3, 0x2F2, 0x1F1, 0x0F0
    };
    palette_set_immediate(k_intex_pal, 32);

    /* Weapon selection: index 0 = MACHINEGUN .. 5 = SIDEWINDERS (matches k_wpic_names).
     * Separate from the player's active combat weapon. */
    int cur_wpic = 0;

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
            case INTEX_SCREEN_WEAPONS:
                draw_screen_weapons(player_idx, &font, wpic.pixels, cur_wpic);
                break;
            case INTEX_SCREEN_STATS:    draw_screen_stats(player_idx, &font);   break;
            case INTEX_SCREEN_EXIT:     running = 0; break;
            default: break;
        }

        video_present();

        if (debounce > 0) { debounce--; continue; }

        if ((g_player1_input & INPUT_LEFT) && !(g_player1_old_input & INPUT_LEFT)) {
            if (cur_screen == INTEX_SCREEN_WEAPONS) {
                cur_wpic = (cur_wpic - 1 + 6) % 6;
            } else {
                cur_screen = (cur_screen - 1 + INTEX_SCREENS) % INTEX_SCREENS;
            }
            audio_play_sample(SAMPLE_CARET_MOVE);
            debounce = 8;
        } else if ((g_player1_input & INPUT_RIGHT) && !(g_player1_old_input & INPUT_RIGHT)) {
            if (cur_screen == INTEX_SCREEN_WEAPONS) {
                cur_wpic = (cur_wpic + 1) % 6;
            } else {
                cur_screen = (cur_screen + 1) % INTEX_SCREENS;
            }
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
    free(wpic.pixels);
    font_free(&font);
}
