/*
 * Alien Breed SE 92 - C port
 * Alien graphics atlas loader
 *
 * BO file format (Ref: main.asm copy_gfx#L12654, lbC01176E#L11975):
 *   Raw 5-bitplane sequential bitmap, no header.
 *   plane n occupies bytes [n*plane_size .. (n+1)*plane_size - 1]
 *   plane_size = (ALIEN_ATLAS_W/8) * ALIEN_ATLAS_H = 40 * 384 = 15360 bytes
 *   total file size = 5 * 15360 = 76800 bytes
 *
 * Pixel decode:
 *   For pixel (x, y):
 *     color_index = OR of (bit at (x,y) in each plane shifted into position)
 *   A bit at column x in plane bp at row y is:
 *     byte = raw[bp * plane_size + y * bytes_per_row + x/8]
 *     bit  = (byte >> (7 - x%8)) & 1
 *     color_index |= bit << bp
 */

#include "alien_gfx.h"
#include "../hal/vfs.h"
#include <stdlib.h>
#include <string.h>

/* Decoded atlas: ALIEN_ATLAS_W * ALIEN_ATLAS_H indexed-color bytes */
static UBYTE *s_atlas      = NULL;
static int    s_atlas_type = ALIEN_ATLAS_COMPACT;

int alien_gfx_load(const char *path, int atlas_type)
{
    alien_gfx_free();

    const int bytes_per_row = ALIEN_ATLAS_W / 8;                    /* 40 */
    const int plane_size    = ALIEN_ATLAS_H * bytes_per_row;        /* 15360 */
    const int total_raw     = ALIEN_ATLAS_PLANES * plane_size;      /* 76800 */

    VFile *f = vfs_open(path);
    if (!f) {
        fprintf(stderr, "alien_gfx_load: cannot open %s\n", path);
        return -1;
    }

    UBYTE *raw = (UBYTE *)malloc((size_t)total_raw);
    if (!raw) {
        vfs_close(f);
        return -1;
    }

    int got = (int)vfs_read(raw, 1, (size_t)total_raw, f);
    vfs_close(f);

    if (got < total_raw) {
        fprintf(stderr, "alien_gfx_load: expected %d bytes, got %d in %s\n",
                total_raw, got, path);
        free(raw);
        return -1;
    }

    s_atlas = (UBYTE *)malloc((size_t)(ALIEN_ATLAS_W * ALIEN_ATLAS_H));
    if (!s_atlas) {
        free(raw);
        return -1;
    }

    /* Decode 5 sequential bitplanes into one indexed-color byte per pixel */
    for (int y = 0; y < ALIEN_ATLAS_H; y++) {
        for (int x = 0; x < ALIEN_ATLAS_W; x++) {
            UBYTE idx = 0;
            for (int bp = 0; bp < ALIEN_ATLAS_PLANES; bp++) {
                int byte_idx = bp * plane_size + y * bytes_per_row + x / 8;
                int bit      = 7 - (x % 8);
                if ((raw[byte_idx] >> bit) & 1)
                    idx |= (UBYTE)(1 << bp);
            }
            s_atlas[y * ALIEN_ATLAS_W + x] = idx;
        }
    }

    free(raw);
    s_atlas_type = atlas_type;
    return 0;
}

void alien_gfx_free(void)
{
    free(s_atlas);
    s_atlas = NULL;
}

const UBYTE *alien_gfx_get_atlas(void)
{
    return s_atlas;
}

int alien_gfx_get_atlas_type(void)
{
    return s_atlas_type;
}
