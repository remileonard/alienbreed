/*
 * Alien Breed SE 92 - C port
 * Blitter HAL implementation
 */

#include "blitter.h"
#include <string.h>

void blitter_masked_copy(
    const UBYTE *src,  int src_stride,
    const UBYTE *mask, int mask_stride,
    UBYTE       *dst,  int dst_stride,
    int w, int h)
{
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            /* mask is stored as packed 1-bit; one byte covers 8 pixels */
            int byte_idx = x / 8;
            int bit_idx  = 7 - (x % 8);
            if (mask[byte_idx] & (1 << bit_idx))
                dst[x] = src[x];
        }
        src  += src_stride;
        mask += mask_stride;
        dst  += dst_stride;
    }
}

void blitter_copy_transparent(
    const UBYTE *src, int src_stride,
    UBYTE       *dst, int dst_stride,
    int w, int h, UBYTE transparent_idx)
{
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            if (src[x] != transparent_idx)
                dst[x] = src[x];
        }
        src += src_stride;
        dst += dst_stride;
    }
}

void blitter_fill(UBYTE *dst, int stride, int w, int h, UBYTE fill_val)
{
    for (int y = 0; y < h; y++) {
        memset(dst, fill_val, (size_t)w);
        dst += stride;
    }
}

void blitter_shift_copy(
    const UBYTE *src, int src_stride,
    UBYTE       *dst, int dst_stride,
    int w, int h, int shift_pixels)
{
    if (shift_pixels == 0) {
        for (int y = 0; y < h; y++) {
            memcpy(dst, src, (size_t)w);
            src += src_stride;
            dst += dst_stride;
        }
        return;
    }
    for (int y = 0; y < h; y++) {
        if (shift_pixels > 0) {
            /* shift right: first shift_pixels pixels = 0 */
            memset(dst, 0, (size_t)shift_pixels);
            memcpy(dst + shift_pixels, src, (size_t)(w - shift_pixels));
        } else {
            /* shift left */
            int s = -shift_pixels;
            memcpy(dst, src + s, (size_t)(w - s));
            memset(dst + (w - s), 0, (size_t)s);
        }
        src += src_stride;
        dst += dst_stride;
    }
}
