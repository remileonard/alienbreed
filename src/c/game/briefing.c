/*
 * Alien Breed SE 92 - C port
 * Briefing screen — translated from src/briefingcore/briefingcore.asm
 *                   and src/briefingstart/briefingstart.asm
 */

#include "briefing.h"
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

static const char *k_briefing_texts[] = {
    "LEVEL 1\nRESEARCH BASE SIGMA SIX\nSECURE THE AREA AND FIND THE EXIT",
    "LEVEL 2\nBIO-CONTAINMENT SECTOR\nNEUTRALISE ALL ALIEN LIFEFORMS",
    "LEVEL 3\nREACTOR CORE\nAVOID THE REACTOR EXPLOSION",
    "LEVEL 4\nALIEN HIVE ENTRANCE\nELIMINATE THE ALIEN QUEEN",
    "LEVEL 5\nSERVICE TUNNELS\nFIND THE POWER COUPLING",
    "LEVEL 6\nWEAPONS BAY\nSECURE ALL WEAPON CACHES",
    "LEVEL 7\nUPPER DECKS\nCLEAR A PATH TO THE BRIDGE",
    "LEVEL 8\nENGINE ROOM\nSHUT DOWN THE MAIN REACTOR",
    "LEVEL 9\nALIEN COMMAND\nDESTROY THE COMMAND NODE",
    "LEVEL 10\nCENTRAL HIVE\nPURGE THE INFESTATION",
    "LEVEL 11\nBREEDING GROUNDS\nSTOP THE ALIEN REPRODUCTION CYCLE",
    "LEVEL 12\nFINAL CONFRONTATION\nDEFEAT THE ALIEN OVERLORD AND ESCAPE",
};

void briefing_run(int level_idx)
{
    if (level_idx < 0 || level_idx >= NUM_LEVELS) return;

    /* Load briefing background */
    typedef struct { UBYTE *pixels; int w, h; } Img;
    Img bg = {NULL, 0, 0};
    FILE *f = fopen("assets/gfx/briefing_bkgnd_320x256.raw", "rb");
    if (f) {
        fread(&bg.w, 4, 1, f); fread(&bg.h, 4, 1, f);
        bg.pixels = (UBYTE *)malloc((size_t)(bg.w * bg.h));
        if (bg.pixels) fread(bg.pixels, 1, (size_t)(bg.w * bg.h), f);
        fclose(f);
    }

    Font font = {0};
    font_load(&font, "assets/fonts/font_16x504.raw", 8, 12, 0);  /* TEXT_LETTER_WIDTH=8 from briefingcore.asm font_struct */

    static const UWORD k_brief_pal[] = {
        0x000, 0xFFF, 0x888, 0x444, 0x0A0, 0x0F0, 0x080, 0x060,
        0xF00, 0xF80, 0xFF0, 0xFFF, 0xAAA, 0x666, 0x222, 0x00F,
        0x000, 0xFFF, 0x888, 0x444, 0x0A0, 0x0F0, 0x080, 0x060,
        0xF00, 0xF80, 0xFF0, 0xFFF, 0xAAA, 0x666, 0x222, 0x00F
    };

    static UWORD cur_pal[32] = {0};
    palette_prep_fade_in(k_brief_pal, cur_pal, 32);

    /* Play sound for entering zone */
    audio_play_sample(VOICE_ENTERING);
    audio_play_sample(VOICE_ZONE);

    int timeout = 50 * 5;   /* hold for 5 seconds or until fire */
    int phase   = 0;        /* 0=fade_in, 1=display text, 2=hold */
    int text_idx = 0;
    const char *text = k_briefing_texts[level_idx];

    while (timeout-- > 0) {
        timer_begin_frame();
        input_poll();

        if (g_quit_requested) break;
        if (phase == 2 &&
            ((g_player1_input & INPUT_FIRE1) || g_key_pressed == KEY_SPACE)) break;

        video_clear();
        if (bg.pixels)
            video_blit(bg.pixels, bg.w, 0, 0, 320, 256, -1);

        if (phase >= 1 && font.pixels) {
            TextCtx ctx;
            typewriter_init_ctx(&ctx, &font, g_framebuffer, 320, 20, 60);
            typewriter_display(&ctx, text);
        }

        palette_tick();
        if (g_done_fade && phase == 0) { phase = 1; timeout = 50 * 5; }
        if (phase == 1) { phase = 2; }

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

    free(bg.pixels);
    font_free(&font);
    (void)text_idx;
}
