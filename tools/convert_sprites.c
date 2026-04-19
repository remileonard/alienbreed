/*
 * convert_sprites.c
 *
 * Converts Amiga hardware sprite files to indexed pixel .raw files.
 *
 * Amiga hardware sprite format (one strip):
 *   WORD  SPRxPOS  (position, ignored)
 *   WORD  SPRxCTL
 *   then for each line:
 *     WORD SPRxDATA  (bitplane A)
 *     WORD SPRxDATB  (bitplane B)
 *   WORD  0, 0   (end-of-strip marker)
 *
 * Each strip is 16px wide.  One strip = (lines+2)*4 bytes.
 *
 * Amiga "attached sprite" pairs (16-color sprites):
 *   The Amiga hardware combines an even sprite (N) and the following odd
 *   sprite (N+1, with ATTACH bit set) to produce a 4-bitplane, 16-color
 *   sprite.  The even strip provides the low 2 bits of each pixel color
 *   and the odd strip provides the high 2 bits:
 *
 *     pixel = (odd.datb_bit << 3) | (odd.data_bit << 2)
 *           | (even.datb_bit << 1) | (even.data_bit << 0)
 *
 *   pixel == 0  → transparent
 *   pixel 1-15  → palette entries COLOR17-COLOR31 = palette index 16+pixel
 *
 * Player sprite file layout (num_pairs=2, 32px wide):
 *   [SPR0 even left][SPR1 odd/attached left][SPR2 even right][SPR3 odd/attached right]
 *   = 4 strips × (lines+2)*4 bytes
 *
 * Output: 4-byte LE width, 4-byte LE height, width*height indexed pixel bytes.
 *   Transparent → 0, color N (1-15) → 16+N  (maps to Amiga COLOR17-COLOR31).
 *
 * Usage: convert_sprites <input> <lines_per_frame> <num_pairs> <output.raw>
 *   num_pairs=1 → 16px wide single 4-color strip (simple sprite, no attachment)
 *   num_pairs=2 → 32px wide attached pair (player)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Read one hardware strip into two arrays: data[lines][16] and datb[lines][16].
 * Each value is 0 or 1 (one bit per pixel per bitplane). */
static int read_strip(FILE *f, int lines,
                      unsigned char *bp_a, /* bit 0 of color (SPRxDATA bits) */
                      unsigned char *bp_b) /* bit 1 of color (SPRxDATB bits) */
{
    unsigned char hdr[4];
    if (fread(hdr, 1, 4, f) != 4) return -1;

    for (int y = 0; y < lines; y++) {
        unsigned char w[4];
        if (fread(w, 1, 4, f) != 4) return -1;
        unsigned short data = (unsigned short)((w[0] << 8) | w[1]);
        unsigned short datb = (unsigned short)((w[2] << 8) | w[3]);
        for (int x = 0; x < 16; x++) {
            int bit = 15 - x;
            bp_a[y * 16 + x] = (unsigned char)((data >> bit) & 1);
            bp_b[y * 16 + x] = (unsigned char)((datb >> bit) & 1);
        }
    }

    unsigned char ftr[4];
    fread(ftr, 1, 4, f);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 5) {
        fprintf(stderr,
            "Usage: %s <input> <lines_per_frame> <num_pairs> <output.raw>\n"
            "  num_pairs=1  single 16px simple sprite (4 colors)\n"
            "  num_pairs=2  two attached pairs = 32px wide, 16 colors (player)\n",
            argv[0]);
        return 1;
    }

    const char *infile  = argv[1];
    int lines           = atoi(argv[2]);
    int num_pairs       = atoi(argv[3]);
    const char *outfile = argv[4];

    if (lines <= 0 || num_pairs < 1 || num_pairs > 4) {
        fprintf(stderr, "Error: invalid parameters\n");
        return 1;
    }

    int out_w = 16 * num_pairs;
    int out_h = lines;

    unsigned char *pixels = (unsigned char *)calloc((size_t)(out_w * out_h), 1);
    if (!pixels) { fprintf(stderr, "Out of memory\n"); return 1; }

    /* Temporary storage for one strip's bitplane bits */
    unsigned char *even_a = (unsigned char *)malloc((size_t)(lines * 16));
    unsigned char *even_b = (unsigned char *)malloc((size_t)(lines * 16));
    unsigned char *odd_a  = (unsigned char *)malloc((size_t)(lines * 16));
    unsigned char *odd_b  = (unsigned char *)malloc((size_t)(lines * 16));
    if (!even_a || !even_b || !odd_a || !odd_b) {
        fprintf(stderr, "Out of memory\n"); return 1;
    }

    FILE *fin = fopen(infile, "rb");
    if (!fin) { perror(infile); free(pixels); return 1; }

    for (int p = 0; p < num_pairs; p++) {
        int col_off = p * 16;

        if (num_pairs == 1) {
            /* Simple (non-attached) sprite: 2bpp, colors 0-3 */
            if (read_strip(fin, lines, even_a, even_b) != 0) {
                fprintf(stderr, "Warning: could not read strip %d\n", p * 2);
                break;
            }
            for (int y = 0; y < lines; y++) {
                for (int x = 0; x < 16; x++) {
                    unsigned char c = (unsigned char)
                        ((even_b[y * 16 + x] << 1) | even_a[y * 16 + x]);
                    pixels[y * out_w + col_off + x] = c; /* 0=transparent, 1-3 */
                }
            }
        } else {
            /* Attached pair: even strip = low 2 bits, odd strip = high 2 bits.
             * Combined pixel 0 = transparent, 1-15 = COLOR17-COLOR31 (index 16+pixel). */
            if (read_strip(fin, lines, even_a, even_b) != 0) {
                fprintf(stderr, "Warning: could not read even strip %d\n", p * 2);
                break;
            }
            if (read_strip(fin, lines, odd_a, odd_b) != 0) {
                fprintf(stderr, "Warning: could not read odd strip %d\n", p * 2 + 1);
                break;
            }
            for (int y = 0; y < lines; y++) {
                for (int x = 0; x < 16; x++) {
                    int idx = y * 16 + x;
                    unsigned char combined = (unsigned char)(
                        (odd_b[idx]  << 3) |
                        (odd_a[idx]  << 2) |
                        (even_b[idx] << 1) |
                         even_a[idx]);
                    /* combined==0 → transparent (store 0)
                     * combined 1-15 → Amiga COLOR17-COLOR31 = palette index 16+combined */
                    pixels[y * out_w + col_off + x] =
                        (combined == 0) ? 0 : (unsigned char)(16 + combined);
                }
            }
        }
    }

    fclose(fin);
    free(even_a); free(even_b); free(odd_a); free(odd_b);

    FILE *fout = fopen(outfile, "wb");
    if (!fout) { perror(outfile); free(pixels); return 1; }

    fwrite(&out_w, 4, 1, fout);
    fwrite(&out_h, 4, 1, fout);
    fwrite(pixels, 1, (size_t)(out_w * out_h), fout);
    fclose(fout);

    printf("Converted %s (%d pairs, %dx%d, %s) -> %s\n",
           infile, num_pairs, out_w, out_h,
           num_pairs == 1 ? "4-color" : "16-color attached",
           outfile);

    free(pixels);
    return 0;
}
