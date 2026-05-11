/*
 * Alien Breed SE 92 - C port
 * Ending screen — translated from src/end/end.asm
 *
 * Sequence (matches the original assembly logic):
 *
 *   Phase 1 – Wipe-in (scroll_background, 26 frames):
 *     copper_diwstrt high byte: $FF → $2C (by -8 per frame).
 *     In C: background slides down from y=211 to y=0 over 26 frames.
 *
 *   Phase 2 – Scroll text (scroll_text, 768 rows × 4 frames/row):
 *     Background displayed in full.
 *     1-bitplane scroll image (scroll_320x1024.lo1) overlaid as white text
 *     (Amiga bitplane 5 ORs bit4 into the color index → COLOR16-31 = $FFF).
 *     Advances 1 row per 4 frames.  Fire button (CIAB_GAMEPORT1) exits early.
 *
 *   Phase 3 – Wipe-out (remove_background, 26 frames):
 *     copper_diwstrt high byte: $2C → $FF (by +8 per frame).
 *     In C: background slides back up and off screen.
 */

#include "end.h"
#include "constants.h"
#include "../hal/video.h"
#include "../hal/input.h"
#include "../hal/audio.h"
#include "../hal/timer.h"
#include "../hal/vfs.h"
#include "../engine/palette.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Image loader (mirrors story.c pattern)                             */
/* ------------------------------------------------------------------ */
typedef struct { UBYTE *pixels; int w, h; } EndImg;

static int end_img_load(EndImg *img, const char *path)
{
    VFile *f = vfs_open(path);
    if (!f) {
        fprintf(stderr, "end: cannot open '%s'\n", path);
        return -1;
    }
    if (vfs_read(&img->w, 4, 1, f) != 1 || vfs_read(&img->h, 4, 1, f) != 1) {
        fprintf(stderr, "end: header read error in '%s'\n", path);
        vfs_close(f); return -1;
    }
    size_t sz = (size_t)(img->w * img->h);
    img->pixels = (UBYTE *)malloc(sz);
    if (!img->pixels) { vfs_close(f); return -1; }
    if (vfs_read(img->pixels, 1, sz, f) != sz) {
        fprintf(stderr, "end: pixel read error in '%s'\n", path);
        free(img->pixels); img->pixels = NULL;
        vfs_close(f); return -1;
    }
    vfs_close(f);
    return 0;
}

/* ------------------------------------------------------------------ */
/* end_run                                                             */
/* ------------------------------------------------------------------ */
void end_run(void)
{
    /*
     * Palette from end.asm copperlist COLOR00-31:
     *   COLOR00-15: background image palette (4-bitplane, lo4 file)
     *   COLOR16-31: scroll text overlay — all $FFF (white).
     *               The Amiga 5th bitplane ORs bit4 into the color index,
     *               mapping every lit scroll pixel to COLOR16-31 = white.
     */
    static const UWORD k_end_pal[32] = {
        0x000, 0xAAA, 0x222, 0x332, 0x333, 0x444, 0x543, 0x555,
        0x765, 0x666, 0x877, 0xA87, 0x999, 0x111, 0xDDD, 0xFFF,
        0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF,
        0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF
    };

    audio_stop_music();
    audio_play_music("title");

    /* Load 4-bitplane background image (320×256) */
    EndImg bg = {NULL, 0, 0};
    end_img_load(&bg, "assets/gfx/end_bkgnd_320x256.raw");

    /* Load 1-bitplane scroll text image (320×1024) */
    EndImg scroll = {NULL, 0, 0};
    end_img_load(&scroll, "assets/gfx/end_scroll_320x1024.raw");

    /*
     * Remap scroll pixels: 0 stays 0 (transparent), 1 → 16 (COLOR16 = white).
     * On the Amiga, bitplane 5 ORs bit4 into the color index so every set pixel
     * maps to COLOR16-31 (all $FFF).  In C we pre-shift to index 16 so that
     * video_blit() with transparent_index=0 correctly overlays white text.
     */
    if (scroll.pixels) {
        size_t npix = (size_t)(scroll.w * scroll.h);
        for (size_t i = 0; i < npix; i++) {
            if (scroll.pixels[i] != 0)
                scroll.pixels[i] = 16;
        }
    }

    /* Apply palette immediately — end.asm loads the copper list directly,
     * there is no fade-in at the start. */
    palette_set_immediate(k_end_pal, 32);

    /* ---------------------------------------------------------------
     * Phase 1: Wipe-in  (scroll_background in end.asm)
     * copper_diwstrt high byte: $FF (255) → forced to $2C (44)
     * by subtracting 8 each frame for 26 frames.
     * In C: background offset y_off = diwstrt_y - 44 slides from 211 → 0.
     * --------------------------------------------------------------- */
    int diwstrt_y = 255;
    for (int frame = 0; frame < 26 && !g_quit_requested; frame++) {
        timer_begin_frame();
        input_poll();

        video_clear();
        if (bg.pixels) {
            int y_off = diwstrt_y - 44;          /* 211 → 0 */
            int rows  = bg.h - y_off;
            if (rows > 0)
                video_blit(bg.pixels, bg.w, 0, y_off, 320, rows, -1);
        }
        video_present();
        diwstrt_y -= 8;
    }
    diwstrt_y = 44; /* move.b #$2C,copper_diwstrt */

    if (g_quit_requested) goto done;

    /* ---------------------------------------------------------------
     * Phase 2: Scroll text  (scroll_text in end.asm)
     * Scroll through 768 rows of the scroll image, 1 row per 4 frames.
     * Fire button (CIAB_GAMEPORT1 in ASM) exits early.
     * --------------------------------------------------------------- */
    {
        int scroll_row = 0;
        int frame_cnt  = 0;

        while (scroll_row < 768 && !g_quit_requested) {
            timer_begin_frame();
            input_poll();

            /* btst #CIAB_GAMEPORT1,CIAA → beq.b exit */
            if (g_player1_input & INPUT_FIRE1)
                break;

            video_clear();

            /* Background (full screen, DIWSTRT = $2C = fully open) */
            if (bg.pixels)
                video_blit(bg.pixels, bg.w, 0, 0, 320, 256, -1);

            /* Scroll image overlay: index 16 (white) over transparent (0).
             * Mirrors the 5th bitplane overlaying COLOR16-31 = $FFF on the bg. */
            if (scroll.pixels) {
                int rows = 256;
                if (scroll_row + rows > scroll.h)
                    rows = scroll.h - scroll_row;
                if (rows > 0)
                    video_blit(scroll.pixels + (size_t)scroll_row * (size_t)scroll.w,
                               scroll.w, 0, 0, 320, rows, 0);
            }

            video_present();

            /* Advance scroll pointer by 40 bytes (1 row) every 4 frames */
            frame_cnt++;
            if (frame_cnt >= 4) {
                frame_cnt = 0;
                scroll_row++;
            }
        }
    }

    if (g_quit_requested) goto done;

    /* ---------------------------------------------------------------
     * Phase 3: Wipe-out  (remove_background in end.asm)
     * copper_diwstrt high byte: $2C (44) → $FF by adding 8 per frame,
     * for 26 frames.
     * In C: background offset y_off = diwstrt_y - 44 slides from 0 → 208.
     * --------------------------------------------------------------- */
    diwstrt_y = 44;
    for (int frame = 0; frame < 26 && !g_quit_requested; frame++) {
        timer_begin_frame();
        input_poll();

        video_clear();
        if (bg.pixels) {
            int y_off = diwstrt_y - 44;          /* 0 → 208 */
            int rows  = bg.h - y_off;
            if (rows > 0)
                video_blit(bg.pixels, bg.w, 0, y_off, 320, rows, -1);
        }
        video_present();
        diwstrt_y += 8;
    }

done:
    audio_stop_music();
    free(bg.pixels);
    free(scroll.pixels);
}
