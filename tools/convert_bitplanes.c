/*
 * convert_bitplanes.c
 *
 * Converts Amiga interleaved bitplane graphics (lo1..lo6 files) to a simple
 * raw indexed pixel file: 4-byte width, 4-byte height, then width*height bytes.
 *
 * Usage: convert_bitplanes <input.loN> <num_bitplanes> <output.raw>
 *
 * The Amiga interleaved format stores one line of bitplane 0, then bitplane 1,
 * etc. for each row.  Each bitplane row is (width/8) bytes.
 *
 * Bit 7 of the first byte is pixel 0, bit 6 is pixel 1, etc.
 *
 * The lo-file extension indicates the number of bitplanes:
 *   lo1 = 1 bp, lo2 = 2 bp, lo3 = 3 bp, lo4 = 4 bp, lo5 = 5 bp, lo6 = 6 bp
 * Pass the number explicitly as the second argument.
 *
 * Output palette index = combination of bits from each bitplane:
 *   pixel = (bp0_bit | bp1_bit<<1 | bp2_bit<<2 | ... | bpN_bit<<(N-1))
 *
 * Note: the file has NO header. Width and height must be passed as arguments.
 *       The extension suffix after "loN" (like _320x256) encodes the geometry.
 *       This tool parses it automatically when possible.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_dimensions(const char *filename, int *w, int *h)
{
    /* Try to find "WxH" in the filename */
    const char *p = filename;
    while (*p) {
        int tw, th;
        if (sscanf(p, "%dx%d", &tw, &th) == 2) {
            *w = tw; *h = th;
            return 0;
        }
        p++;
    }
    return -1;
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr,
            "Usage: %s <input.loN> <num_bitplanes> <output.raw> [width height]\n",
            argv[0]);
        return 1;
    }

    const char *infile    = argv[1];
    int         num_bp    = atoi(argv[2]);
    const char *outfile   = argv[3];

    if (num_bp < 1 || num_bp > 6) {
        fprintf(stderr, "Error: num_bitplanes must be 1..6\n");
        return 1;
    }

    int width = 0, height = 0;
    if (argc >= 6) {
        width  = atoi(argv[4]);
        height = atoi(argv[5]);
    } else {
        if (parse_dimensions(infile, &width, &height) != 0) {
            fprintf(stderr,
                "Error: cannot infer width/height from filename. "
                "Pass them explicitly.\n");
            return 1;
        }
    }

    if (width <= 0 || height <= 0) {
        fprintf(stderr, "Error: invalid dimensions %dx%d\n", width, height);
        return 1;
    }

    int bytes_per_row = width / 8;
    long expected     = (long)num_bp * bytes_per_row * height;

    FILE *fin = fopen(infile, "rb");
    if (!fin) { perror(infile); return 1; }

    unsigned char *raw = (unsigned char *)malloc((size_t)expected);
    if (!raw) { fprintf(stderr, "Out of memory\n"); fclose(fin); return 1; }

    size_t got = fread(raw, 1, (size_t)expected, fin);
    fclose(fin);

    if ((long)got < expected) {
        fprintf(stderr,
            "Warning: expected %ld bytes, got %zu. File may be truncated.\n",
            expected, got);
    }

    unsigned char *pixels = (unsigned char *)malloc((size_t)(width * height));
    if (!pixels) { fprintf(stderr, "Out of memory\n"); free(raw); return 1; }

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            unsigned char px = 0;
            for (int bp = 0; bp < num_bp; bp++) {
                /* Planar: all rows of bitplane N come first, then bitplane N+1.
                 * Row y of bitplane bp is at (bp * height + y) * bytes_per_row */
                int row_offset = (bp * height + y) * bytes_per_row;
                int byte_idx   = row_offset + x / 8;
                int bit        = 7 - (x % 8);
                if ((raw[byte_idx] >> bit) & 1)
                    px |= (unsigned char)(1 << bp);
            }
            pixels[y * width + x] = px;
        }
    }

    FILE *fout = fopen(outfile, "wb");
    if (!fout) { perror(outfile); free(raw); free(pixels); return 1; }

    fwrite(&width,  4, 1, fout);
    fwrite(&height, 4, 1, fout);
    fwrite(pixels, 1, (size_t)(width * height), fout);
    fclose(fout);

    printf("Converted %s (%dx%d, %d bpp) -> %s\n",
           infile, width, height, num_bp, outfile);

    free(raw);
    free(pixels);
    return 0;
}
