/*
 * Alien Breed SE 92 - C port
 * Animated background tile atlas loader
 *
 * AN file format (Ref: main.asm bkgnd_anim_block, copy_gfx#L12604):
 *   Raw 5-bitplane sequential bitmap, no header.
 *   plane n occupies bytes [n*plane_size .. (n+1)*plane_size - 1]
 *   plane_size = (ANIM_ATLAS_W/8) * ANIM_ATLAS_H = 40 * 144 = 5760 bytes
 *   total file size = 5 * 5760 = 28800 bytes
 *
 * Format is identical to the BO alien atlas except for different dimensions
 * (320×144 vs 320×384) and 5 bitplanes in both cases.
 */

#include "anim_gfx.h"
#include "../hal/vfs.h"
#include <stdlib.h>
#include <string.h>

static UBYTE *s_atlas = NULL;

int anim_gfx_load(const char *path)
{
    anim_gfx_free();

    const int bytes_per_row = ANIM_ATLAS_W / 8;                     /* 40 */
    const int plane_size    = ANIM_ATLAS_H * bytes_per_row;         /* 5760 */
    const int total_raw     = ANIM_ATLAS_PLANES * plane_size;       /* 28800 */

    VFile *f = vfs_open(path);
    if (!f) {
        fprintf(stderr, "anim_gfx_load: cannot open %s\n", path);
        return -1;
    }

    UBYTE *raw = (UBYTE *)malloc((size_t)total_raw);
    if (!raw) { vfs_close(f); return -1; }

    int got = (int)vfs_read(raw, 1, (size_t)total_raw, f);
    vfs_close(f);

    if (got < total_raw) {
        fprintf(stderr, "anim_gfx_load: expected %d bytes, got %d in %s\n",
                total_raw, got, path);
        free(raw);
        return -1;
    }

    s_atlas = (UBYTE *)malloc((size_t)(ANIM_ATLAS_W * ANIM_ATLAS_H));
    if (!s_atlas) { free(raw); return -1; }

    for (int y = 0; y < ANIM_ATLAS_H; y++) {
        for (int x = 0; x < ANIM_ATLAS_W; x++) {
            UBYTE idx = 0;
            for (int bp = 0; bp < ANIM_ATLAS_PLANES; bp++) {
                int byte_idx = bp * plane_size + y * bytes_per_row + x / 8;
                int bit      = 7 - (x % 8);
                if ((raw[byte_idx] >> bit) & 1)
                    idx |= (UBYTE)(1 << bp);
            }
            s_atlas[y * ANIM_ATLAS_W + x] = idx;
        }
    }

    free(raw);
    return 0;
}

void anim_gfx_free(void)
{
    free(s_atlas);
    s_atlas = NULL;
}

const UBYTE *anim_gfx_get_atlas(void)
{
    return s_atlas;
}
