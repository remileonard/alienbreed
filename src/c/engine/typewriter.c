/*
 * Alien Breed SE 92 - C port
 * Typewriter text engine — translated from src/common/typewriter.asm
 */

#include "typewriter.h"
#include "../hal/video.h"
#include "../hal/audio.h"
#include "../game/constants.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char k_font_chars[] = FONT_CHARS;

int font_load(Font *font, const char *path, int lw, int lh, int transparent)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "font_load: cannot open %s\n", path);
        return -1;
    }

    /* Header: 4 bytes width, 4 bytes height (written by convert_bitplanes) */
    int strip_w, strip_h;
    if (fread(&strip_w, 4, 1, f) != 1 || fread(&strip_h, 4, 1, f) != 1) {
        fclose(f); return -1;
    }

    size_t sz = (size_t)(strip_w * strip_h);
    UBYTE *pixels = (UBYTE *)malloc(sz);
    if (!pixels) { fclose(f); return -1; }
    if (fread(pixels, 1, sz, f) != sz) {
        free(pixels); fclose(f); return -1;
    }
    fclose(f);

    font->pixels      = pixels;
    font->strip_w     = strip_w;
    font->glyph_w     = 16;   /* all fonts use 16-px wide glyph cells in the strip
                                  (= 1 word / 2 bytes per row in Amiga bitplane layout) */
    font->letter_w    = lw;
    font->letter_h    = lh;
    font->transparent = transparent;
    return 0;
}

void font_free(Font *font)
{
    if (font->pixels) {
        free((void *)font->pixels);
        font->pixels = NULL;
    }
}

void typewriter_init_ctx(TextCtx *ctx, Font *font,
                         UBYTE *dest, int dest_stride,
                         int start_x, int start_y)
{
    ctx->font          = font;
    ctx->dest          = dest;
    ctx->dest_stride   = dest_stride;
    ctx->start_x       = start_x;
    ctx->cursor_x      = start_x;
    ctx->cursor_y      = start_y;
    ctx->play_sound    = 9;  /* play typewriter SFX every 9 chars (matches asm) */
    ctx->sound_counter = 0;
    ctx->color_offset  = 0;
}

int typewriter_putchar(TextCtx *ctx, char c)
{
    Font *font = ctx->font;
    if (!font || !font->pixels) return 0;

    /* Find character in font table */
    int idx = -1;
    for (int i = 0; k_font_chars[i]; i++) {
        if (k_font_chars[i] == c) { idx = i; break; }
    }

    if (idx < 0) {
        /* Unknown character: advance cursor only */
        ctx->cursor_x += font->letter_w + 1;
        return 0;
    }

    if (c != ' ') {
        /* Blit the character from the font strip.
         * Source X uses glyph_w (always 16 — the bitmap cell width), NOT letter_w,
         * which is the cursor-advance value (may be smaller, e.g. 8 for briefing). */
        int src_x = idx * font->glyph_w;
        const UBYTE *src = font->pixels + src_x;

        /* Blit directly to framebuffer for simplicity.
         * Draw the full glyph_w pixels (16) — the assembly blitter always renders
         * the full 16-pixel glyph; glyphs overlap when letter_w < glyph_w. */
        for (int row = 0; row < font->letter_h; row++) {
            int dy = ctx->cursor_y + row;
            if (dy < 0 || dy >= 256) continue;
            for (int col = 0; col < font->glyph_w; col++) {
                int dx = ctx->cursor_x + col;
                if (dx < 0 || dx >= 320) continue;
                UBYTE px = src[col];
                if (px != (UBYTE)font->transparent)
                    g_framebuffer[dy * 320 + dx] |= (UBYTE)(px + ctx->color_offset);
            }
            src += font->strip_w;
        }
    }

    ctx->cursor_x += font->letter_w + 1;

    return 1;
}

void typewriter_display(TextCtx *ctx, const char *text)
{
    for (const char *p = text; *p; p++) {
        if (*p == '\n') {
            /* Newline: move to next row */
            ctx->cursor_x = ctx->start_x;
            ctx->cursor_y += ctx->font->letter_h + 1;
        } else {
            typewriter_putchar(ctx, *p);
        }
    }
}
