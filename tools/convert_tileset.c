/*
 * convert_tileset.c
 *
 * Converts Amiga tile-strip files (LABM, LBBM, etc.) to a raw indexed pixel file.
 *
 * Format deduced from main.asm:
 *   - Each tile is 16x16 pixels, 5 bitplanes, stored in tile-internal planar order:
 *       [16 rows of bp0][16 rows of bp1][16 rows of bp2][16 rows of bp3][16 rows of bp4]
 *       = 5 * 16 * 2 = 160 bytes per tile
 *   - Tiles are laid out consecutively: tile 0, tile 1, ..., tile N-1
 *   - File size = num_tiles * 160 bytes  (e.g. 76800 = 480 tiles)
 *
 * Output: same raw format as convert_bitplanes:
 *   4-byte LE width, 4-byte LE height, then width*height indexed pixel bytes.
 *   Output image is a vertical strip: 16 pixels wide, (num_tiles * 16) pixels tall.
 *
 * Usage: convert_tileset <input> <output.raw>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TILE_W       16
#define TILE_H       16
#define TILE_NUM_BP  5
#define BYTES_PER_ROW (TILE_W / 8)                     /* 2 */
#define BYTES_PER_BP  (BYTES_PER_ROW * TILE_H)         /* 32 */
#define BYTES_PER_TILE (TILE_NUM_BP * BYTES_PER_BP)    /* 160 */

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <input_tileset> <output.raw>\n", argv[0]);
        return 1;
    }

    const char *infile  = argv[1];
    const char *outfile = argv[2];

    FILE *fin = fopen(infile, "rb");
    if (!fin) { perror(infile); return 1; }

    fseek(fin, 0, SEEK_END);
    long file_size = ftell(fin);
    rewind(fin);

    if (file_size % BYTES_PER_TILE != 0) {
        fprintf(stderr,
            "Warning: file size %ld is not a multiple of %d (bytes per tile).\n",
            file_size, BYTES_PER_TILE);
    }

    int num_tiles = (int)(file_size / BYTES_PER_TILE);
    if (num_tiles == 0) {
        fprintf(stderr, "Error: empty file or wrong format.\n");
        fclose(fin);
        return 1;
    }

    unsigned char *raw = (unsigned char *)malloc((size_t)file_size);
    if (!raw) { fprintf(stderr, "Out of memory\n"); fclose(fin); return 1; }

    size_t got = fread(raw, 1, (size_t)file_size, fin);
    fclose(fin);

    if ((long)got < file_size) {
        fprintf(stderr, "Warning: read %zu bytes, expected %ld.\n", got, file_size);
    }

    int out_w = TILE_W;
    int out_h = num_tiles * TILE_H;

    unsigned char *pixels = (unsigned char *)malloc((size_t)(out_w * out_h));
    if (!pixels) { fprintf(stderr, "Out of memory\n"); free(raw); return 1; }

    for (int t = 0; t < num_tiles; t++) {
        int tile_base = t * BYTES_PER_TILE;
        for (int row = 0; row < TILE_H; row++) {
            for (int x = 0; x < TILE_W; x++) {
                unsigned char px = 0;
                for (int bp = 0; bp < TILE_NUM_BP; bp++) {
                    /* tile-internal planar: all rows of bp first, then next bp */
                    int byte_offset = tile_base + bp * BYTES_PER_BP
                                      + row * BYTES_PER_ROW + x / 8;
                    int bit = 7 - (x % 8);
                    if ((raw[byte_offset] >> bit) & 1)
                        px |= (unsigned char)(1 << bp);
                }
                int py = t * TILE_H + row;
                pixels[py * out_w + x] = px;
            }
        }
    }

    FILE *fout = fopen(outfile, "wb");
    if (!fout) { perror(outfile); free(raw); free(pixels); return 1; }

    fwrite(&out_w,  4, 1, fout);
    fwrite(&out_h,  4, 1, fout);
    fwrite(pixels, 1, (size_t)(out_w * out_h), fout);
    fclose(fout);

    printf("Converted %s (%d tiles, %dx%d strip) -> %s\n",
           infile, num_tiles, out_w, out_h, outfile);

    free(raw);
    free(pixels);
    return 0;
}
