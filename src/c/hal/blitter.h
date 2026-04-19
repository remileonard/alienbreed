#ifndef AB_BLITTER_H
#define AB_BLITTER_H

/*
 * Alien Breed SE 92 - C port
 * Blitter HAL — replaces Amiga custom chip blitter operations.
 *
 * The Amiga blitter performed: D = (A AND mask) OR (B shift) OR C
 * For this port we implement the logical equivalents on the
 * indexed framebuffer / surface, using simple CPU copy loops
 * (SDL2's SDL_BlitSurface handles the accelerated path when possible).
 *
 * Key operations needed:
 *   - Masked copy with transparency (letter blitting in typewriter.asm)
 *   - Shifted copy (horizontal scroll in tilemap)
 *   - Clear with fill value
 */

#include "../types.h"

/*
 * Blit a rectangle from one indexed buffer to another, applying a 1-bit mask.
 *   src       : source pixel data (1 byte per pixel, indexed)
 *   mask      : 1-bit mask; pixel is copied only where mask bit = 1
 *   dst       : destination pixel data
 *   src_stride, mask_stride, dst_stride : row widths in bytes
 *   w, h      : rectangle size in pixels
 *   color_idx : palette index to write where mask bit = 1
 */
void blitter_masked_copy(
    const UBYTE *src,  int src_stride,
    const UBYTE *mask, int mask_stride,
    UBYTE       *dst,  int dst_stride,
    int w, int h);

/*
 * Copy src to dst, skipping pixels that match transparent_idx.
 */
void blitter_copy_transparent(
    const UBYTE *src, int src_stride,
    UBYTE       *dst, int dst_stride,
    int w, int h, UBYTE transparent_idx);

/*
 * Fill dst with fill_val.
 */
void blitter_fill(UBYTE *dst, int stride, int w, int h, UBYTE fill_val);

/*
 * Shift src horizontally by shift_pixels to the right, writing into dst.
 * Handles sub-tile horizontal scroll (replaces BLTCON1 shift field).
 */
void blitter_shift_copy(
    const UBYTE *src, int src_stride,
    UBYTE       *dst, int dst_stride,
    int w, int h, int shift_pixels);

#endif /* AB_BLITTER_H */
