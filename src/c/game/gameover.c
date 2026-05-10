/*
 * Alien Breed SE 92 - C port
 * Game Over screen — translated from src/gameover/gameover.asm
 *
 * Parses the native IFF ANIM-5 file (gameover.anim) and plays it,
 * faithfully reproducing the assembly animation loop:
 *
 *   start → prepare_anim (decode first ILBM frame, copy buf1→buf2)
 *         → set_copper_buffer_2 (display buf2)
 *         → prep_fade_speeds_fade_in
 *   anim_loop (36 iterations):
 *         → decode_anim → buf2  → wait_2_frames → set_copper_buffer_2
 *         → fade_palette_in / input check
 *         → decode_anim → buf1  → wait_2_frames → set_copper_buffer_1
 *   wait_exit: hold 150 frames, exit on fire
 *   fade_out → return
 */

#include "gameover.h"
#include "constants.h"
#include "../hal/video.h"
#include "../hal/input.h"
#include "../hal/audio.h"
#include "../hal/timer.h"
#include "../hal/vfs.h"
#include "../engine/palette.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -----------------------------------------------------------------------
 * IFF ANIM-5 geometry (from gameover.asm start / prepare_anim)
 *   width  = 40 bytes per row per bitplane  (320 px / 8 = 40)
 *   height = 256 scanlines
 *   depth  = 2 bitplanes  (4 colours)
 *
 * Screen buffer layout — planar, plane 0 then plane 1:
 *   bytes   0 .. 10239  = bitplane 0  (256 rows × 40 bytes)
 *   bytes 10240 .. 20479 = bitplane 1  (256 rows × 40 bytes)
 * ----------------------------------------------------------------------- */
#define GO_WIDTH_PX    320
#define GO_HEIGHT      256
#define GO_DEPTH       2
#define GO_ROW_BYTES   40                               /* 320 / 8 */
#define GO_PLANE_BYTES (GO_ROW_BYTES * GO_HEIGHT)       /* 10240   */
#define GO_BUF_BYTES   (GO_PLANE_BYTES * GO_DEPTH)      /* 20480   */

/* -----------------------------------------------------------------------
 * Big-endian helpers (IFF is big-endian)
 * ----------------------------------------------------------------------- */
static uint32_t go_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
}
static uint16_t go_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

/* -----------------------------------------------------------------------
 * PackBits RLE decompressor — implements rle_depack from gameover.asm.
 *
 * Writes exactly row_bytes uncompressed bytes into dst.
 * Returns the number of source bytes consumed.
 * ----------------------------------------------------------------------- */
static size_t go_unpackbits(const uint8_t *src, uint8_t *dst, int row_bytes)
{
    const uint8_t *start = src;
    int written = 0;
    while (written < row_bytes) {
        int8_t n = (int8_t)(*src++);
        if (n >= 0) {
            /* literal run: copy n+1 bytes */
            int count = (int)n + 1;
            while (count-- > 0 && written < row_bytes) {
                *dst++ = *src++;
                written++;
            }
        } else {
            /* repeat run: write next byte (-n+1) times  */
            int count = 1 - (int)n;
            uint8_t val = *src++;
            while (count-- > 0 && written < row_bytes) {
                *dst++ = val;
                written++;
            }
        }
    }
    return (size_t)(src - start);
}

/* -----------------------------------------------------------------------
 * Decode first ILBM frame — implements prepare_anim / get_first_pic.
 *
 * form_ptr   : pointer to the FORM chunk of the first ILBM inside ANIM
 * data_end   : one byte past the end of the loaded file
 * dst        : GO_BUF_BYTES output buffer (bitplane 0 then bitplane 1)
 * pal_out    : receives up to 32 Amiga 12-bit colours from the CMAP chunk
 *
 * Returns a pointer to the byte immediately after this FORM chunk
 * (i.e. the start of the first delta FORM/ILBM), or NULL on error.
 * ----------------------------------------------------------------------- */
static const uint8_t *go_decode_first_frame(const uint8_t  *form_ptr,
                                             const uint8_t  *data_end,
                                             uint8_t        *dst,
                                             UWORD          *pal_out)
{
    if (form_ptr + 12 > data_end)                    return NULL;
    if (go_be32(form_ptr)     != 0x464F524DUL)       return NULL; /* 'FORM' */
    if (go_be32(form_ptr + 8) != 0x494C424DUL)       return NULL; /* 'ILBM' */

    uint32_t       form_size = go_be32(form_ptr + 4);
    const uint8_t *form_end  = form_ptr + 8 + form_size;
    if (form_end > data_end) form_end = data_end;

    /* Defaults (overridden by BMHD) */
    int width_bytes = GO_ROW_BYTES;
    int height      = GO_HEIGHT;
    int depth       = GO_DEPTH;

    memset(dst, 0, GO_BUF_BYTES);

    const uint8_t *p = form_ptr + 12;          /* first chunk inside ILBM */
    while (p + 8 <= form_end) {
        uint32_t       tag        = go_be32(p);
        uint32_t       chunk_size = go_be32(p + 4);
        const uint8_t *cdata      = p + 8;
        const uint8_t *next       = cdata + chunk_size;
        if (chunk_size & 1) next++;             /* IFF word-alignment */
        if (next > form_end) next = form_end;

        if (tag == 0x424D4844UL && chunk_size >= 9) {           /* 'BMHD' */
            width_bytes = (int)(go_be16(cdata) >> 3);           /* px_w / 8 */
            height      = (int)go_be16(cdata + 2);
            /* BMHD layout: w(2) h(2) x(2) y(2) nPlanes(1) ...
             * nPlanes is at byte offset 8 from start of chunk data.        */
            depth       = (int)cdata[8];                        /* nPlanes */

        } else if (tag == 0x434D4150UL && pal_out) {            /* 'CMAP' */
            /* 3-byte (R,G,B) entries → 12-bit Amiga colour */
            uint32_t nc = chunk_size / 3;
            if (nc > 32) nc = 32;
            for (uint32_t i = 0; i < nc; i++) {
                uint8_t r = cdata[i * 3];
                uint8_t g = cdata[i * 3 + 1];
                uint8_t b = cdata[i * 3 + 2];
                pal_out[i] = (UWORD)(((r >> 4) << 8) |
                                     ((g >> 4) << 4) |
                                      (b >> 4));
            }

        } else if (tag == 0x424F4459UL) {                       /* 'BODY' */
            /*
             * PackBits-compressed bitplane data, stored interleaved:
             *   for each row  (height rows):
             *     for each bitplane (depth planes):
             *       one PackBits-compressed row  (width_bytes bytes output)
             *
             * We de-interleave into the planar dst buffer:
             *   plane p, row y  →  dst + p*GO_PLANE_BYTES + y*GO_ROW_BYTES
             *
             * This matches get_first_pic / rle_depack in gameover.asm.
             */
            const uint8_t *src = cdata;
            const uint8_t *src_end = cdata + chunk_size;
            for (int row = 0; row < height && src < src_end; row++) {
                for (int pl = 0; pl < depth && src < src_end; pl++) {
                    uint8_t *row_dst = dst
                                     + (size_t)pl   * GO_PLANE_BYTES
                                     + (size_t)row  * GO_ROW_BYTES;
                    src += go_unpackbits(src, row_dst, width_bytes);
                }
            }
        }

        p = next;
    }

    /* Return pointer to the next chunk (first delta FORM/ILBM) */
    const uint8_t *after = form_ptr + 8 + form_size;
    if (form_size & 1) after++;
    return after;
}

/* -----------------------------------------------------------------------
 * ANIM-5 delta decoder — implements decode_anim from gameover.asm.
 *
 * Applies one DLTA chunk's column-based vertical delta to dst_buf in place.
 *
 * DLTA data layout (ANIM-5):
 *   bytes 0 .. depth*4-1 : per-bitplane byte-offsets (from DLTA start)
 *                           into the column data for that bitplane
 *   column data for each bitplane:
 *     for each of GO_ROW_BYTES columns:
 *       1 byte  : num_ops  (0 = no data for this column)
 *       num_ops operations:
 *         if op >= 0x80 : literal – copy (op & 0x7F) bytes from stream,
 *                         one byte per row (advancing by GO_ROW_BYTES each)
 *         if op == 0    : repeat  – read count N then value V,
 *                         write V into N consecutive rows
 *         if op 1..127  : skip    – advance row pointer by op rows
 * ----------------------------------------------------------------------- */
static void go_decode_delta5(const uint8_t *dlta, uint32_t dlta_size,
                              uint8_t *dst)
{
    if (dlta_size < (uint32_t)(GO_DEPTH * 4)) return;

    for (int plane = 0; plane < GO_DEPTH; plane++) {
        uint32_t col_off = go_be32(dlta + (size_t)plane * 4);
        if (col_off == 0 || col_off >= dlta_size) continue;

        const uint8_t *col_data  = dlta + col_off;
        const uint8_t *dlta_end  = dlta + dlta_size;
        uint8_t       *plane_buf = dst + (size_t)plane * GO_PLANE_BYTES;

        for (int col = 0; col < GO_ROW_BYTES; col++) {
            if (col_data >= dlta_end) break;
            uint8_t num_ops = *col_data++;
            if (num_ops == 0) continue;

            /* ptr points to column col, row 0 of this bitplane.
             * Advancing by GO_ROW_BYTES steps one row down (= add.l d4,a1). */
            uint8_t *ptr = plane_buf + col;

            for (int oi = 0; oi < (int)num_ops; oi++) {
                if (col_data >= dlta_end) break;
                uint8_t op = *col_data++;

                if (op >= 0x80) {
                    /* literal block: copy (op & 0x7F) bytes vertically */
                    int count = (int)(op & 0x7F);
                    for (int i = 0; i < count; i++) {
                        if (col_data < dlta_end)
                            *ptr = *col_data++;
                        ptr += GO_ROW_BYTES;
                    }
                } else if (op == 0) {
                    /* repeat block: read count and value */
                    if (col_data + 1 > dlta_end) break;
                    uint8_t count = *col_data++;
                    uint8_t val   = *col_data++;
                    for (int i = 0; i < (int)count; i++) {
                        *ptr = val;
                        ptr += GO_ROW_BYTES;
                    }
                } else {
                    /* skip: advance ptr by op rows (= mulu d4,d0; add.l d0,a1) */
                    ptr += (int)op * GO_ROW_BYTES;
                }
            }
        }
    }
}

/* -----------------------------------------------------------------------
 * Decode one delta FORM/ILBM frame — navigates ANHD+DLTA chunks.
 *
 * Returns pointer to the byte after this FORM chunk (next delta frame),
 * or NULL if the end of data has been reached or the frame is invalid.
 * ----------------------------------------------------------------------- */
static const uint8_t *go_decode_next_frame(const uint8_t *frame_ptr,
                                            const uint8_t *data_end,
                                            uint8_t       *dst)
{
    if (frame_ptr + 12 > data_end)               return NULL;
    if (go_be32(frame_ptr)     != 0x464F524DUL)  return NULL; /* 'FORM' */
    if (go_be32(frame_ptr + 8) != 0x494C424DUL)  return NULL; /* 'ILBM' */

    uint32_t       form_size = go_be32(frame_ptr + 4);
    const uint8_t *form_end  = frame_ptr + 8 + form_size;
    if (form_end > data_end) form_end = data_end;

    int found_dlta = 0;

    const uint8_t *p = frame_ptr + 12;
    while (p + 8 <= form_end) {
        uint32_t       tag        = go_be32(p);
        uint32_t       chunk_size = go_be32(p + 4);
        const uint8_t *cdata      = p + 8;
        const uint8_t *next       = cdata + chunk_size;
        if (chunk_size & 1) next++;
        if (next > form_end) next = form_end;

        if (tag == 0x414E4844UL) {                              /* 'ANHD' */
            /* Verify operation type == 5 (ANIM-5), as in gameover.asm */
            if (chunk_size >= 1 && cdata[0] != 5) return NULL;

        } else if (tag == 0x444C5441UL) {                      /* 'DLTA' */
            go_decode_delta5(cdata, chunk_size, dst);
            found_dlta = 1;
        }

        p = next;
    }

    if (!found_dlta) return NULL;

    /* Advance past this FORM chunk (word-aligned, as in gameover.asm) */
    const uint8_t *after = frame_ptr + 8 + form_size;
    if (form_size & 1) after++;
    return after;
}

/* -----------------------------------------------------------------------
 * Convert a 2-bitplane Amiga buffer to an indexed 320×256 framebuffer.
 *
 * Pixel colour index = (plane1_bit << 1) | plane0_bit  for each pixel.
 * This matches the Amiga hardware colour lookup.
 * ----------------------------------------------------------------------- */
static void go_bitplanes_to_indexed(const uint8_t *planes, uint8_t *fb)
{
    const uint8_t *p0  = planes;
    const uint8_t *p1  = planes + GO_PLANE_BYTES;
    uint8_t       *out = fb;

    for (int y = 0; y < GO_HEIGHT; y++) {
        for (int xb = 0; xb < GO_ROW_BYTES; xb++) {
            uint8_t b0 = p0[y * GO_ROW_BYTES + xb];
            uint8_t b1 = p1[y * GO_ROW_BYTES + xb];
            /* 8 pixels from MSB (bit 7) to LSB (bit 0) */
            for (int bit = 7; bit >= 0; bit--) {
                *out++ = (uint8_t)((((b1 >> bit) & 1) << 1) |
                                    ((b0 >> bit) & 1));
            }
        }
    }
}

/* -----------------------------------------------------------------------
 * gameover_run — main entry point
 *
 * Faithfully implements start / anim_loop / wait_exit / fade_out from
 * src/gameover/gameover.asm.
 * ----------------------------------------------------------------------- */
void gameover_run(void)
{
    audio_stop_music();

    /* Load the IFF ANIM file into memory */
    VFile *f = vfs_open("assets/anim/gameover.anim");
    if (!f) return;

    vfs_seek(f, 0, SEEK_END);
    long fsize = vfs_tell(f);
    vfs_seek(f, 0, SEEK_SET);
    if (fsize <= 0) { vfs_close(f); return; }

    uint8_t *anim_data = (uint8_t *)malloc((size_t)fsize);
    if (!anim_data) { vfs_close(f); return; }
    vfs_read(anim_data, 1, (size_t)fsize, f);
    vfs_close(f);

    const uint8_t *data_end = anim_data + fsize;

    /* Verify outer FORM/ANIM wrapper */
    if (fsize < 12 ||
        go_be32(anim_data)     != 0x464F524DUL ||   /* 'FORM' */
        go_be32(anim_data + 8) != 0x414E494DUL) {   /* 'ANIM' */
        free(anim_data);
        return;
    }

    /* Allocate two planar screen buffers (double-buffering as in ASM) */
    uint8_t *buf1 = (uint8_t *)calloc(GO_BUF_BYTES, 1);
    uint8_t *buf2 = (uint8_t *)calloc(GO_BUF_BYTES, 1);
    if (!buf1 || !buf2) {
        free(buf1); free(buf2); free(anim_data);
        return;
    }

    /*
     * Palette — from gameover.asm:  palette dc.w 0,$A99,$766,$333
     * Prefer the CMAP from the first ILBM if present; fall back to the
     * hardcoded ASM values.
     */
    UWORD pal[32] = {
        0x000, 0xA99, 0x766, 0x333,
        0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,
        0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0
    };

    /* --- prepare_anim / get_first_pic:  decode first ILBM into buf1 --- */
    const uint8_t *frame_ptr = anim_data + 12; /* past outer FORM/ANIM */
    frame_ptr = go_decode_first_frame(frame_ptr, data_end, buf1, pal);
    if (!frame_ptr) {
        free(buf1); free(buf2); free(anim_data);
        return;
    }

    /* Copy buf1 → buf2 (= prepare_anim's .copy_first_pic loop) */
    memcpy(buf2, buf1, GO_BUF_BYTES);

    /*
     * prep_fade_speeds_fade_in: start palette fade from black.
     * cur_pal starts at zero (all black) and palette_tick() advances it
     * toward pal each frame.
     */
    UWORD cur_pal[32] = {0};
    palette_prep_fade_in(pal, cur_pal, 32);

    /* Display the initial frame on buf2 (= set_copper_buffer_2) */
    go_bitplanes_to_indexed(buf2, g_framebuffer);
    palette_tick();
    video_present();
    timer_begin_frame();

    /*
     * anim_loop — 36 iterations, each decoding two delta frames.
     * odd_frame=0 means "decode into buf2 next" (mirroring 25(a6) in ASM).
     */
    int odd_frame = 0;

    for (int frames_counter = 1; frames_counter <= 36; frames_counter++) {

        /* === First decode in this iteration === */
        uint8_t       *target_buf = (odd_frame == 0) ? buf2 : buf1;
        const uint8_t *next       = go_decode_next_frame(frame_ptr, data_end, target_buf);
        if (!next) goto wait_exit;      /* tst.w d0; bne wait_exit */
        frame_ptr = next;
        odd_frame ^= 1;

        /* wait_2_frames + set_copper:  display decoded buffer for 2 ticks */
        go_bitplanes_to_indexed(target_buf, g_framebuffer);
        for (int tick = 0; tick < 2; tick++) {
            timer_begin_frame();
            palette_tick();
            video_present();
        }

        /* Check fire buttons (= btst CIAB_GAMEPORT0/1) */
        input_poll();
        if (g_quit_requested) goto wait_exit;
        if (g_player1_input & INPUT_FIRE1) goto wait_exit;

        /* === Second decode in this iteration === */
        target_buf = (odd_frame == 0) ? buf2 : buf1;
        next       = go_decode_next_frame(frame_ptr, data_end, target_buf);
        if (!next) goto wait_exit;
        frame_ptr = next;
        odd_frame ^= 1;

        go_bitplanes_to_indexed(target_buf, g_framebuffer);
        for (int tick = 0; tick < 2; tick++) {
            timer_begin_frame();
            palette_tick();
            video_present();
        }
    }

wait_exit:
    /* Hold last frame for 150 frames (= move.l #150,d0 / .wait loop) */
    for (int i = 0; i < 150; i++) {
        timer_begin_frame();
        input_poll();
        if (g_quit_requested) break;
        if (g_player1_input & INPUT_FIRE1) break;
        palette_tick();
        video_present();
    }

    /* fade_out: fade from current palette to black */
    {
        UWORD k_black[32] = {0};
        palette_get_current(cur_pal, 32);
        palette_prep_fade_to_rgb(k_black, cur_pal, 32);
        for (int i = 0; i < 25; i++) {
            timer_begin_frame();
            palette_tick();
            video_present();
        }
    }

    free(buf1);
    free(buf2);
    free(anim_data);
}
