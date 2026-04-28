#ifndef AB_TYPEWRITER_H
#define AB_TYPEWRITER_H

/*
 * Alien Breed SE 92 - C port
 * Typewriter text engine — translated from src/common/typewriter.asm
 *
 * Renders text character-by-character onto the framebuffer using a
 * bitmap font loaded from an indexed image (converted .lo* file).
 *
 * The original used the Amiga blitter to mask and shift each letter;
 * here we use video_blit() with transparency.
 *
 * Font format (after asset conversion):
 *   - Fixed-width characters in a horizontal strip
 *   - Each glyph cell in the strip is glyph_w × letter_h pixels (always 16 wide)
 *   - Cursor advances letter_w + 1 per character (TEXT_LETTER_WIDTH from asm font_struct)
 *   - Characters are in order of ascii_letters[] below
 */

#include "../types.h"

/* Characters available in the font (matches ascii_letters in typewriter.asm) */
#define FONT_CHARS "ABCDEFGHIJKLMNOPQRSTUVWXYZ,1234567890.!?: "

/* A font descriptor */
typedef struct {
    const UBYTE *pixels;      /* indexed pixel data for the full font strip */
    int          strip_w;     /* total width of strip in pixels */
    int          glyph_w;     /* pixel width of each glyph cell in the bitmap strip
                                 (always 16 for these fonts — matches the 2-byte/word
                                 bitplane layout used by the Amiga blitter) */
    int          letter_w;    /* cursor advance per character = TEXT_LETTER_WIDTH in
                                 font_struct dc.l data (may be < glyph_w for tight spacing:
                                 8 for briefing/intex, 9 for story, 16 for menu) */
    int          letter_h;    /* height of one character cell */
    int          transparent; /* palette index treated as background */
} Font;

/* A text context (mirrors the TEXT_* offsets accessed in typewriter.asm) */
typedef struct {
    Font       *font;
    UBYTE      *dest;          /* pointer into framebuffer or surface */
    int         dest_stride;   /* row stride of dest in bytes */
    int         start_x;       /* initial X (reset on newline) */
    int         cursor_x;      /* current cursor X */
    int         cursor_y;      /* current cursor Y */
    int         play_sound;    /* play typewriter sound every N chars */
    int         sound_counter;
    int         color_offset; /* added to every drawn pixel index (for multi-palette layers) */
    int         text_color;   /* when >= 0, use this palette index instead of (px + color_offset) */
} TextCtx;

/* Initialise a text context. */
void typewriter_init_ctx(TextCtx *ctx, Font *font,
                         UBYTE *dest, int dest_stride,
                         int start_x, int start_y);

/*
 * Display a null-terminated string through the typewriter effect.
 * Each character is blitted immediately; the caller is responsible for
 * calling timer_begin_frame() between characters if a delay is desired.
 * Equivalent to display_text / next_letter in typewriter.asm.
 */
void typewriter_display(TextCtx *ctx, const char *text);

/*
 * Display a single character at the current cursor position.
 * Advances cursor_x by letter_w + 1.
 * Returns 0 if the character is not in the font (skips it).
 */
int  typewriter_putchar(TextCtx *ctx, char c);

/* Load a font from a converted asset file.
 * path      : path to a flat indexed image (e.g. "assets/fonts/font_16x504.raw")
 * lw        : cursor advance per character (= TEXT_LETTER_WIDTH in asm font_struct;
 *             8 for briefing/intex, 9 for story, 16 for menu)
 * lh        : glyph height in pixels
 * Returns 0 on success. */
int  font_load(Font *font, const char *path, int lw, int lh, int transparent);

/* Free a loaded font. */
void font_free(Font *font);

#endif /* AB_TYPEWRITER_H */
