#ifndef AB_PALETTE_H
#define AB_PALETTE_H

/*
 * Alien Breed SE 92 - C port
 * Palette engine — translated from src/common/palette.asm
 *
 * All Amiga palette entries are 12-bit (0x0RGB, 4 bits per channel).
 * We store them as UWORD and convert to 8-bit per channel in video.c.
 *
 * Three fade modes (matches fading_go_flag values in palette.asm):
 *   1 = fade to target RGB
 *   2 = fade in (black → palette)
 *   3 = fade out (palette → black)
 */

#include "../types.h"

#define PALETTE_FADE_SPEED 2  /* frames between palette steps (FADE_SPEED) */

/* State */
extern int   g_fading_go_flag;   /* 0=idle, 1=to_rgb, 2=fade_in, 3=fade_out */
extern int   g_done_fade;        /* 1 when the current fade is complete */

/*
 * Prepare a fade towards a target palette.
 *   source_palette : array of `count` Amiga UWORD color values (destination)
 *   cur_palette    : current displayed palette (will be modified toward source)
 *   count          : number of colors (≤32)
 * Equivalent to prep_fade_speeds_fade_to_rgb in palette.asm.
 */
void palette_prep_fade_to_rgb(const UWORD *source_palette,
                              UWORD       *cur_palette,
                              int          count);

/*
 * Prepare a fade-in from black.
 *   target_palette : palette to fade towards
 *   cur_palette    : output palette (starts all black, modified each tick)
 *   count          : number of colors
 * Equivalent to prep_fade_speeds_fade_in.
 */
void palette_prep_fade_in(const UWORD *target_palette,
                          UWORD       *cur_palette,
                          int          count);

/*
 * Prepare a fade-out to black.
 *   cur_palette : palette to fade from (modified in place)
 *   count       : number of colors
 * Equivalent to prep_fade_speeds_fade_out.
 */
void palette_prep_fade_out(UWORD *cur_palette, int count);

/*
 * Advance the active fade by one frame step.
 * Call once per game frame. Writes results into cur_palette and calls
 * video_set_palette() when changes occur.
 * Equivalent to fade_palette_to_rgb / fade_palette_in / fade_palette_out.
 */
void palette_tick(void);

/*
 * Instantly apply a palette without fading.
 * Equivalent to set_palette in palette.asm.
 */
void palette_set_immediate(const UWORD *palette, int count);

#endif /* AB_PALETTE_H */
