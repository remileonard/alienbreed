/*
 * parse_levels.c
 *
 * Parses the Alien Breed T7MP level map files (game/L*MA, L*BO, L*AN etc.)
 * and converts them to a simple flat binary format usable by tilemap.c.
 *
 * T7MP format (from docs/levelmaps_format.txt):
 *   "T7MP"        4 bytes  — magic
 *   XBLK          4 bytes  — map width  in tiles (big-endian UWORD at +0)
 *   YBLK          4 bytes  — map height in tiles
 *   IFFP          variable — path to IFF picture background
 *   PALA          32 bytes — palette A (16 x UWORD, big-endian Amiga 12-bit)
 *   PALB          32 bytes — palette B
 *   BODY          width*height*2 bytes — tile data (big-endian UWORDs)
 *
 * Each tile word:
 *   bits 15..6  = tile graphic index
 *   bits  5..0  = attribute byte (TILE_* flags)
 *
 * This tool writes a simple binary:
 *   [4] width
 *   [4] height
 *   [4] iffp_len
 *   [iffp_len] iffp string (null-terminated)
 *   [32*2] palette A (UWORDs, little-endian for easy loading)
 *   [32*2] palette B
 *   [width*height*2] tile data (little-endian UWORDs)
 *
 * Usage: parse_levels <input_T7MP> <output.map>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned short read_be16(FILE *f)
{
    unsigned char b[2];
    if (fread(b, 1, 2, f) != 2) return 0;
    return (unsigned short)((b[0] << 8) | b[1]);
}

static unsigned int read_be32(FILE *f)
{
    unsigned char b[4];
    if (fread(b, 1, 4, f) != 4) return 0;
    return (unsigned int)((b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3]);
}

static void write_u32le(FILE *f, unsigned int v)
{
    unsigned char b[4] = {
        (unsigned char)(v & 0xFF), (unsigned char)((v >> 8) & 0xFF),
        (unsigned char)((v >> 16) & 0xFF), (unsigned char)((v >> 24) & 0xFF)
    };
    fwrite(b, 1, 4, f);
}

static void write_u16le(FILE *f, unsigned short v)
{
    unsigned char b[2] = { (unsigned char)(v & 0xFF), (unsigned char)((v >> 8) & 0xFF) };
    fwrite(b, 1, 2, f);
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <input_T7MP> <output.map>\n", argv[0]);
        return 1;
    }

    FILE *fin = fopen(argv[1], "rb");
    if (!fin) { perror(argv[1]); return 1; }

    /* Check magic */
    char magic[5] = {0};
    fread(magic, 1, 4, fin);
    if (strcmp(magic, "T7MP") != 0) {
        fprintf(stderr, "Error: not a T7MP file (got '%s')\n", magic);
        fclose(fin);
        return 1;
    }

    unsigned int width = 0, height = 0;
    char iffp[512] = {0};
    unsigned short pal_a[32] = {0};
    unsigned short pal_b[32] = {0};
    unsigned short *tiles    = NULL;

    /* Skip the T7MP header chunk: chunk_size (4 bytes) + file_size data */
    {
        unsigned int hdr_size = read_be32(fin);  /* = 4 */
        fseek(fin, (long)hdr_size, SEEK_CUR);   /* skip file_size field */
    }

    /* Parse chunks until EOF */
    while (1) {
        char id[5] = {0};
        if (fread(id, 1, 4, fin) != 4) break;
        unsigned int chunk_size = read_be32(fin);

        if (strcmp(id, "XBLK") == 0) {
            width  = read_be32(fin);
            if (chunk_size > 4) fseek(fin, (long)(chunk_size - 4), SEEK_CUR);
        } else if (strcmp(id, "YBLK") == 0) {
            height = read_be32(fin);
            if (chunk_size > 4) fseek(fin, (long)(chunk_size - 4), SEEK_CUR);
        } else if (strcmp(id, "IFFP") == 0) {
            unsigned int n = chunk_size < 511 ? chunk_size : 511;
            fread(iffp, 1, n, fin);
            iffp[n] = '\0';
            if (chunk_size > n) fseek(fin, (long)(chunk_size - n), SEEK_CUR);
        } else if (strcmp(id, "PALA") == 0) {
            /* First 64 bytes = palette filename (skip), then 32 x UWORD colors */
            fseek(fin, 64, SEEK_CUR);
            for (int i = 0; i < 32; i++)
                pal_a[i] = read_be16(fin);
            if (chunk_size > 64 + 64) fseek(fin, (long)(chunk_size - 64 - 64), SEEK_CUR);
        } else if (strcmp(id, "PALB") == 0) {
            fseek(fin, 64, SEEK_CUR);
            for (int i = 0; i < 32; i++)
                pal_b[i] = read_be16(fin);
            if (chunk_size > 64 + 64) fseek(fin, (long)(chunk_size - 64 - 64), SEEK_CUR);
        } else if (strcmp(id, "BODY") == 0) {
            unsigned int num_tiles = width * height;
            tiles = (unsigned short *)malloc(num_tiles * sizeof(unsigned short));
            if (!tiles) { fprintf(stderr, "Out of memory\n"); fclose(fin); return 1; }
            for (unsigned int i = 0; i < num_tiles; i++)
                tiles[i] = read_be16(fin);
            if (chunk_size > num_tiles * 2)
                fseek(fin, (long)(chunk_size - num_tiles * 2), SEEK_CUR);
        } else {
            /* Unknown chunk: skip */
            fseek(fin, (long)chunk_size, SEEK_CUR);
        }

        /* Pad byte for odd chunk sizes */
        if (chunk_size & 1) fseek(fin, 1, SEEK_CUR);
    }
    fclose(fin);

    if (!tiles) {
        fprintf(stderr, "Error: no BODY chunk found\n");
        free(tiles);
        return 1;
    }

    FILE *fout = fopen(argv[2], "wb");
    if (!fout) { perror(argv[2]); free(tiles); return 1; }

    write_u32le(fout, width);
    write_u32le(fout, height);

    unsigned int iffp_len = (unsigned int)(strlen(iffp) + 1);
    write_u32le(fout, iffp_len);
    fwrite(iffp, 1, iffp_len, fout);

    for (int i = 0; i < 32; i++) write_u16le(fout, pal_a[i]);
    for (int i = 0; i < 32; i++) write_u16le(fout, pal_b[i]);

    for (unsigned int i = 0; i < width * height; i++)
        write_u16le(fout, tiles[i]);

    fclose(fout);

    printf("Parsed %s: %dx%d tiles, IFFP='%s' -> %s\n",
           argv[1], width, height, iffp, argv[2]);

    free(tiles);
    return 0;
}
