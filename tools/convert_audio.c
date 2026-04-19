/*
 * convert_audio.c
 *
 * Converts raw Amiga 8-bit signed PCM samples (Paula format) to WAV files
 * that SDL2_mixer can load.
 *
 * Amiga Paula:
 *   - Signed 8-bit PCM
 *   - Standard rate: 8363 Hz (C-3 note on PAL) — some samples use other rates
 *   - Big-endian (mono)
 *
 * This tool writes a standard RIFF/WAV at the specified sample rate.
 *
 * Usage: convert_audio <input.raw> <output.wav> [sample_rate]
 *   sample_rate defaults to 8363
 *
 * Note: IFF-8SVX files (the Amiga audio format used in game/) have a 8-byte
 *   header chunk before the sample data. Pass --iff flag to skip the header.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Write a little-endian 32-bit value */
static void write_u32le(FILE *f, unsigned int v)
{
    unsigned char b[4] = {
        (unsigned char)(v & 0xFF),
        (unsigned char)((v >> 8)  & 0xFF),
        (unsigned char)((v >> 16) & 0xFF),
        (unsigned char)((v >> 24) & 0xFF)
    };
    fwrite(b, 1, 4, f);
}

/* Write a little-endian 16-bit value */
static void write_u16le(FILE *f, unsigned short v)
{
    unsigned char b[2] = {
        (unsigned char)(v & 0xFF),
        (unsigned char)((v >> 8) & 0xFF)
    };
    fwrite(b, 1, 2, f);
}

/* Skip IFF-8SVX chunks until we reach BODY */
static long find_iff_body(FILE *f, long file_size)
{
    /* IFF header: FORM + size (8 bytes) + "8SVX" (4 bytes) */
    fseek(f, 12, SEEK_SET);
    while (ftell(f) < file_size - 8) {
        char id[5] = {0};
        fread(id, 1, 4, f);
        unsigned char sb[4];
        fread(sb, 1, 4, f);
        unsigned int chunk_size = (unsigned int)((sb[0] << 24) | (sb[1] << 16) |
                                                  (sb[2] << 8)  | sb[3]);
        if (strcmp(id, "BODY") == 0)
            return ftell(f);  /* caller reads from here */
        fseek(f, (long)chunk_size, SEEK_CUR);
        if (chunk_size & 1) fseek(f, 1, SEEK_CUR);  /* pad byte */
    }
    return -1;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr,
            "Usage: %s <input.raw> <output.wav> [sample_rate] [--iff]\n",
            argv[0]);
        return 1;
    }

    const char *infile  = argv[1];
    const char *outfile = argv[2];
    int sample_rate     = 8363;
    int is_iff          = 0;

    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--iff") == 0)
            is_iff = 1;
        else
            sample_rate = atoi(argv[i]);
    }

    FILE *fin = fopen(infile, "rb");
    if (!fin) { perror(infile); return 1; }

    fseek(fin, 0, SEEK_END);
    long file_size = ftell(fin);
    fseek(fin, 0, SEEK_SET);

    long data_offset = 0;
    long data_size   = file_size;

    if (is_iff) {
        data_offset = find_iff_body(fin, file_size);
        if (data_offset < 0) {
            fprintf(stderr, "Error: BODY chunk not found in IFF file\n");
            fclose(fin);
            return 1;
        }
        data_size = file_size - data_offset;
    }

    signed char *samples = (signed char *)malloc((size_t)data_size);
    if (!samples) { fprintf(stderr, "Out of memory\n"); fclose(fin); return 1; }

    fseek(fin, data_offset, SEEK_SET);
    size_t got = fread(samples, 1, (size_t)data_size, fin);
    fclose(fin);
    data_size = (long)got;

    /* Write WAV */
    FILE *fout = fopen(outfile, "wb");
    if (!fout) { perror(outfile); free(samples); return 1; }

    unsigned int riff_size = (unsigned int)(36 + data_size);

    fwrite("RIFF", 1, 4, fout);
    write_u32le(fout, riff_size);
    fwrite("WAVE", 1, 4, fout);

    /* fmt chunk */
    fwrite("fmt ", 1, 4, fout);
    write_u32le(fout, 16);           /* chunk size */
    write_u16le(fout, 1);            /* PCM */
    write_u16le(fout, 1);            /* mono */
    write_u32le(fout, (unsigned int)sample_rate);
    write_u32le(fout, (unsigned int)sample_rate);  /* byte rate (8-bit mono) */
    write_u16le(fout, 1);            /* block align */
    write_u16le(fout, 8);            /* bits per sample */

    /* data chunk */
    fwrite("data", 1, 4, fout);
    write_u32le(fout, (unsigned int)data_size);

    /* Convert signed 8-bit to unsigned 8-bit (WAV 8-bit is unsigned) */
    for (long i = 0; i < data_size; i++) {
        unsigned char out_sample = (unsigned char)(samples[i] + 128);
        fwrite(&out_sample, 1, 1, fout);
    }

    fclose(fout);
    printf("Converted %s (%ld bytes @ %d Hz) -> %s\n",
           infile, data_size, sample_rate, outfile);

    free(samples);
    return 0;
}
