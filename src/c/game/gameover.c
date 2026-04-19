/*
 * Alien Breed SE 92 - C port
 * Game Over screen — translated from src/gameover/gameover.asm
 *
 * Loads gameover.anim, plays it frame-by-frame (50 Hz), then returns.
 */

#include "gameover.h"
#include "constants.h"
#include "../hal/video.h"
#include "../hal/input.h"
#include "../hal/audio.h"
#include "../hal/timer.h"
#include "../engine/palette.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Very simple anim: the file is a sequence of raw indexed frames (320×256 each),
 * preceded by a 4-byte frame count header written by the converter. */

void gameover_run(void)
{
    audio_stop_music();

    FILE *f = fopen("assets/anim/gameover.anim", "rb");
    int num_frames = 0;
    UBYTE **frames = NULL;

    if (f) {
        fread(&num_frames, 4, 1, f);
        if (num_frames > 0 && num_frames < 500) {
            frames = (UBYTE **)calloc((size_t)num_frames, sizeof(UBYTE *));
            for (int i = 0; i < num_frames && frames; i++) {
                frames[i] = (UBYTE *)malloc(320 * 256);
                if (frames[i]) fread(frames[i], 1, 320 * 256, f);
            }
        }
        fclose(f);
    }

    /* From gameover.asm: palette has only 4 colors (2bpp anim) */
    static const UWORD k_go_pal[] = {
        0x000, 0xA99, 0x766, 0x333, 0x000, 0x000, 0x000, 0x000,
        0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
        0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
        0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000
    };
    UWORD s_pal[32];
    memcpy(s_pal, k_go_pal, sizeof(s_pal));
    palette_set_immediate(k_go_pal, 32);

    int cur = 0;
    int done = 0;
    int hold = 0;

    while (!done) {
        timer_begin_frame();
        input_poll();

        if (g_quit_requested) break;

        if (frames && cur < num_frames) {
            memcpy(g_framebuffer, frames[cur], 320 * 256);
            cur++;
            if (cur >= num_frames) hold = 100;  /* pause on last frame */
        } else if (hold > 0) {
            hold--;
            if (hold == 0) done = 1;
        } else {
            done = 1;  /* no anim: just exit */
        }

        if ((g_player1_input & INPUT_FIRE1)) done = 1;

        video_present();
    }

    /* Fade to black */
    static UWORD k_black[32] = {0};
    palette_prep_fade_to_rgb(k_black, s_pal, 32);
    for (int i = 0; i < 25; i++) {
        timer_begin_frame();
        palette_tick();
        video_present();
    }

    if (frames) {
        for (int i = 0; i < num_frames; i++) free(frames[i]);
        free(frames);
    }
}
