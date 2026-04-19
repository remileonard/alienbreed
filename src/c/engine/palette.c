/*
 * Alien Breed SE 92 - C port
 * Palette engine — translated from src/common/palette.asm
 */

#include "palette.h"
#include "../hal/video.h"
#include <string.h>

int  g_fading_go_flag = 0;
int  g_done_fade      = 0;

/* Internal state (mirrors asm data variables) */
static int    s_colors_amount = 0;
static int    s_frame_counter = 0;
static UWORD  s_cur_palette[32];
static UWORD  s_target_palette[32];
/* Per-channel direction: -1, 0, +1 (to_rgb mode) */
static BYTE   s_speed_to_rgb[32 * 3];
/* Per-channel speed values (fixed-point *256, fade_in/out mode) */
static UWORD  s_speed_block[32 * 3];
static UWORD  s_cur_rgb[32 * 3];    /* fractional accumulators */

/* Extract the three 4-bit Amiga RGB channels as integers (0–15) */
static inline int amiga_r(UWORD c) { return (c >> 8) & 0xF; }
static inline int amiga_g(UWORD c) { return (c >> 4) & 0xF; }
static inline int amiga_b(UWORD c) { return (c >> 0) & 0xF; }

/* Rebuild an Amiga color word from three 4-bit channels */
static inline UWORD amiga_rgb(int r, int g, int b)
{
    return (UWORD)(((r & 0xF) << 8) | ((g & 0xF) << 4) | (b & 0xF));
}

/* ------------------------------------------------------------------ */
/* prep_fade_speeds_fade_to_rgb                                        */
/* ------------------------------------------------------------------ */
void palette_prep_fade_to_rgb(const UWORD *source_palette,
                              UWORD       *cur_palette,
                              int          count)
{
    if (count > 32) count = 32;
    s_colors_amount = count;
    s_frame_counter = 0;
    g_done_fade     = 0;
    g_fading_go_flag = 1;

    memcpy(s_cur_palette,    cur_palette,    (size_t)count * sizeof(UWORD));
    memcpy(s_target_palette, source_palette, (size_t)count * sizeof(UWORD));

    for (int i = 0; i < count; i++) {
        UWORD cur = s_cur_palette[i];
        UWORD tgt = s_target_palette[i];
        s_speed_to_rgb[i * 3 + 0] = (amiga_r(tgt) > amiga_r(cur)) ?  1 :
                                     (amiga_r(tgt) < amiga_r(cur)) ? -1 : 0;
        s_speed_to_rgb[i * 3 + 1] = (amiga_g(tgt) > amiga_g(cur)) ?  1 :
                                     (amiga_g(tgt) < amiga_g(cur)) ? -1 : 0;
        s_speed_to_rgb[i * 3 + 2] = (amiga_b(tgt) > amiga_b(cur)) ?  1 :
                                     (amiga_b(tgt) < amiga_b(cur)) ? -1 : 0;
    }
}

/* ------------------------------------------------------------------ */
/* prep_fade_speeds_fade_in                                            */
/* ------------------------------------------------------------------ */
void palette_prep_fade_in(const UWORD *target_palette,
                          UWORD       *cur_palette,
                          int          count)
{
    if (count > 32) count = 32;
    s_colors_amount = count;
    s_frame_counter = 0;
    g_done_fade     = 0;
    g_fading_go_flag = 2;

    memcpy(s_target_palette, target_palette, (size_t)count * sizeof(UWORD));

    /* Start from black */
    memset(s_cur_palette, 0, sizeof(s_cur_palette));
    memset(s_cur_rgb,     0, sizeof(s_cur_rgb));
    memset(cur_palette,   0, (size_t)count * sizeof(UWORD));

    /* Speed = target / 15 (fade over 15 steps) */
    for (int i = 0; i < count; i++) {
        s_speed_block[i * 3 + 0] = (UWORD)((amiga_r(target_palette[i]) * 256 + 7) / 15);
        s_speed_block[i * 3 + 1] = (UWORD)((amiga_g(target_palette[i]) * 256 + 7) / 15);
        s_speed_block[i * 3 + 2] = (UWORD)((amiga_b(target_palette[i]) * 256 + 7) / 15);
    }
}

/* ------------------------------------------------------------------ */
/* prep_fade_speeds_fade_out                                           */
/* ------------------------------------------------------------------ */
void palette_prep_fade_out(UWORD *cur_palette, int count)
{
    if (count > 32) count = 32;
    s_colors_amount = count;
    s_frame_counter = 0;
    g_done_fade     = 0;
    g_fading_go_flag = 3;

    memcpy(s_cur_palette, cur_palette, (size_t)count * sizeof(UWORD));

    /* Speed = current / 15 */
    for (int i = 0; i < count; i++) {
        s_speed_block[i * 3 + 0] = (UWORD)((amiga_r(cur_palette[i]) * 256 + 7) / 15);
        s_speed_block[i * 3 + 1] = (UWORD)((amiga_g(cur_palette[i]) * 256 + 7) / 15);
        s_speed_block[i * 3 + 2] = (UWORD)((amiga_b(cur_palette[i]) * 256 + 7) / 15);
        /* Initialise accumulators from current palette */
        s_cur_rgb[i * 3 + 0] = (UWORD)(amiga_r(cur_palette[i]) * 256);
        s_cur_rgb[i * 3 + 1] = (UWORD)(amiga_g(cur_palette[i]) * 256);
        s_cur_rgb[i * 3 + 2] = (UWORD)(amiga_b(cur_palette[i]) * 256);
    }
}

/* ------------------------------------------------------------------ */
/* palette_tick — advance fade by one frame                           */
/* ------------------------------------------------------------------ */
void palette_tick(void)
{
    if (g_fading_go_flag == 0 || g_done_fade) return;

    s_frame_counter++;
    if (s_frame_counter < PALETTE_FADE_SPEED) return;
    s_frame_counter = 0;

    int matched = 0;
    int n = s_colors_amount;

    if (g_fading_go_flag == 1) {
        /* fade_palette_to_rgb: step each channel toward target */
        for (int i = 0; i < n; i++) {
            int r = amiga_r(s_cur_palette[i]);
            int g = amiga_g(s_cur_palette[i]);
            int b = amiga_b(s_cur_palette[i]);
            int tr = amiga_r(s_target_palette[i]);
            int tg = amiga_g(s_target_palette[i]);
            int tb = amiga_b(s_target_palette[i]);

            if (r == tr) matched++; else r += s_speed_to_rgb[i * 3 + 0];
            if (g == tg) matched++; else g += s_speed_to_rgb[i * 3 + 1];
            if (b == tb) matched++; else b += s_speed_to_rgb[i * 3 + 2];

            s_cur_palette[i] = amiga_rgb(r, g, b);
        }
        if (matched == n * 3) { g_done_fade = 1; g_fading_go_flag = 0; }

    } else if (g_fading_go_flag == 2) {
        /* fade_palette_in: add speed to accumulator */
        int done_channels = 0;
        for (int i = 0; i < n; i++) {
            int tr = amiga_r(s_target_palette[i]);
            int tg = amiga_g(s_target_palette[i]);
            int tb = amiga_b(s_target_palette[i]);

            int cr = amiga_r(s_cur_palette[i]);
            int cg = amiga_g(s_cur_palette[i]);
            int cb = amiga_b(s_cur_palette[i]);

            if (cr < tr) { s_cur_rgb[i*3+0] += s_speed_block[i*3+0]; cr = s_cur_rgb[i*3+0] >> 8; if (cr > tr) cr = tr; } else done_channels++;
            if (cg < tg) { s_cur_rgb[i*3+1] += s_speed_block[i*3+1]; cg = s_cur_rgb[i*3+1] >> 8; if (cg > tg) cg = tg; } else done_channels++;
            if (cb < tb) { s_cur_rgb[i*3+2] += s_speed_block[i*3+2]; cb = s_cur_rgb[i*3+2] >> 8; if (cb > tb) cb = tb; } else done_channels++;

            s_cur_palette[i] = amiga_rgb(cr, cg, cb);
        }
        if (done_channels == n * 3) { g_done_fade = 1; g_fading_go_flag = 0; }

    } else if (g_fading_go_flag == 3) {
        /* fade_palette_out: subtract speed from accumulator */
        int done_channels = 0;
        for (int i = 0; i < n; i++) {
            int cr = amiga_r(s_cur_palette[i]);
            int cg = amiga_g(s_cur_palette[i]);
            int cb = amiga_b(s_cur_palette[i]);

            if (cr > 0) { if (s_cur_rgb[i*3+0] > s_speed_block[i*3+0]) { s_cur_rgb[i*3+0] -= s_speed_block[i*3+0]; cr = s_cur_rgb[i*3+0] >> 8; } else { s_cur_rgb[i*3+0] = 0; cr = 0; } } else done_channels++;
            if (cg > 0) { if (s_cur_rgb[i*3+1] > s_speed_block[i*3+1]) { s_cur_rgb[i*3+1] -= s_speed_block[i*3+1]; cg = s_cur_rgb[i*3+1] >> 8; } else { s_cur_rgb[i*3+1] = 0; cg = 0; } } else done_channels++;
            if (cb > 0) { if (s_cur_rgb[i*3+2] > s_speed_block[i*3+2]) { s_cur_rgb[i*3+2] -= s_speed_block[i*3+2]; cb = s_cur_rgb[i*3+2] >> 8; } else { s_cur_rgb[i*3+2] = 0; cb = 0; } } else done_channels++;

            s_cur_palette[i] = amiga_rgb(cr, cg, cb);
        }
        if (done_channels == n * 3) { g_done_fade = 1; g_fading_go_flag = 0; }
    }

    video_set_palette(s_cur_palette, n);
}

void palette_set_immediate(const UWORD *palette, int count)
{
    if (count > 32) count = 32;
    memcpy(s_cur_palette, palette, (size_t)count * sizeof(UWORD));
    g_fading_go_flag = 0;
    g_done_fade      = 1;
    video_set_palette(palette, count);
}
