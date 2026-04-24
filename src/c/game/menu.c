/*
 * Alien Breed SE 92 - C port
 * Menu screen — translated from src/menu/menu.asm
 *
 * Title sequence (matches the original assembly logic):
 *
 *   Phase 1 – Title on black
 *     Starfield backdrop + title logo (palette_logo) fades in.
 *     Wait for any button/key press before proceeding.
 *
 *   Phase 2 – Menu
 *     Stars + title at top + copyright sliding in from the bottom.
 *     Menu items drawn with typewriter effect.
 *     1000-frame idle timeout → Phase 3.
 *     Fire on START GAME → fade out → MENU_RESULT_START.
 *
 *   Phase 3 – Credits
 *     9 credit screens, each held ~350 frames.
 *     Any button press → back to Phase 2 (matches break_main_loop_flag).
 *     All 9 credits exhausted → MENU_RESULT_AUTO_EXIT (triggers story).
 */

#include "menu.h"
#include "constants.h"
#include "../hal/video.h"
#include "../hal/input.h"
#include "../hal/audio.h"
#include "../hal/timer.h"
#include "../engine/palette.h"
#include "../engine/typewriter.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Menu item indices (match pos_in_menu in menu.asm: 0/1/2)           */
/* ------------------------------------------------------------------ */
#define MENU_ITEM_NUM_PLAYERS   0
#define MENU_ITEM_SHARE_CREDITS 1
#define MENU_ITEM_START         2
#define MENU_ITEMS              3

/* ------------------------------------------------------------------ */
/* Palettes (taken directly from menu.asm)                            */
/*                                                                     */
/* The Amiga copper-list switches the palette at different scanlines   */
/* to give each layer its own 8-colour set.  In the C port we arrange  */
/* the 32 palette slots so each layer occupies a distinct range:       */
/*   0- 7  title logo   (palette_logo)                                 */
/*   8-15  starfield    (stars_palette)                                */
/*  16-23  menu text    (palette_menu, index 16 = transparent)         */
/*  24-31  copyright    (copyright_palette, index 24 = transparent)    */
/* ------------------------------------------------------------------ */
static const UWORD k_palette_logo[8] = {
    0x000, 0x111, 0x100, 0x200, 0x400, 0x800, 0xD00, 0xF30
};
static const UWORD k_stars_palette[8] = {
    0x000, 0x111, 0x222, 0x333, 0x555, 0x888, 0xAAA, 0x000
};
static const UWORD k_palette_menu[8] = {
    0x000, 0x222, 0xD31, 0xB11, 0x444, 0x333, 0x222, 0xF52
};
static const UWORD k_palette_copyright[8] = {
    0x000, 0x620, 0x720, 0x830, 0x940, 0xA40, 0xB50, 0xC60
};
/* Warm-red intermediate palette used when fading out between credits */
static const UWORD k_palette_menu_red[8] = {
    0x000, 0x822, 0xA31, 0x811, 0xF44, 0x833, 0xA22, 0x852
};

/* Combined 32-entry working palette (built from the four blocks above) */
static UWORD s_pal[32];

/* ------------------------------------------------------------------ */
/* Image helpers                                                       */
/* ------------------------------------------------------------------ */
typedef struct { UBYTE *pixels; int w, h; } Img;

static int img_load(Img *img, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "menu: cannot open '%s'\n", path);
        return -1;
    }
    if (fread(&img->w, 4, 1, f) != 1 || fread(&img->h, 4, 1, f) != 1) {
        fprintf(stderr, "menu: header read error in '%s'\n", path);
        fclose(f); return -1;
    }
    size_t sz = (size_t)(img->w * img->h);
    img->pixels = (UBYTE *)malloc(sz);
    if (!img->pixels) { fclose(f); return -1; }
    if (fread(img->pixels, 1, sz, f) != sz) {
        fprintf(stderr, "menu: pixel read error in '%s'\n", path);
        free(img->pixels); img->pixels = NULL;
        fclose(f); return -1;
    }
    fclose(f);
    return 0;
}

static void img_free(Img *img)
{
    free(img->pixels);
    img->pixels = NULL;
}

/*
 * Blit an image onto g_framebuffer with a palette-index offset.
 * Source pixel 0 is treated as transparent (not drawn).
 * Non-zero source pixels become (pixel + offset) in the framebuffer.
 */
static void blit_offset(const Img *img, int dx, int dy, int offset)
{
    if (!img || !img->pixels) return;
    int w = img->w, h = img->h;
    for (int row = 0; row < h; row++) {
        int y = dy + row;
        if (y < 0 || y >= 256) continue;
        const UBYTE *src = img->pixels + row * w;
        for (int col = 0; col < w; col++) {
            int x = dx + col;
            if (x < 0 || x >= 320) continue;
            UBYTE px = src[col];
            if (px == 0) continue;
            g_framebuffer[y * 320 + x] = (UBYTE)(px + offset);
        }
    }
}

/*
 * Blit a horizontal band of src_h rows starting at source row src_y.
 * Matches the bitplane-pointer arithmetic in display_title (menu.asm):
 * the title image stores two 89-row variants stacked vertically and the
 * assembly alternates between them every VBlank to create the neon effect.
 */
static void blit_offset_half(const Img *img, int dx, int dy,
                              int src_y, int src_h, int offset)
{
    if (!img || !img->pixels) return;
    int w = img->w;
    for (int row = 0; row < src_h; row++) {
        int sy = src_y + row;
        if (sy >= img->h) break;
        int y = dy + row;
        if (y < 0 || y >= 256) continue;
        const UBYTE *src = img->pixels + sy * w;
        for (int col = 0; col < w; col++) {
            int x = dx + col;
            if (x < 0 || x >= 320) continue;
            UBYTE px = src[col];
            if (px == 0) continue;
            g_framebuffer[y * 320 + x] = (UBYTE)(px + offset);
        }
    }
}

/* ------------------------------------------------------------------ */
/* 3-D starfield — exact port of menu.asm stars subsystem             */
/*                                                                     */
/* stars_directions_table (menu.asm lines 837-870):                   */
/*   dcb.w 32,V for each V in the sequence below, then dc.w -32       */
/*   as a sentinel that triggers wrap-around to the table start.       */
/*                                                                     */
/* Three independent pointers advance through this table each frame,  */
/* one per axis.  Their starting word offsets match the assembly:      */
/*   direction_x: word   0 → initial delta = 0                        */
/*   direction_y: word 256 → initial delta = 8  (at the peak)         */
/*   direction_z: word 384 → initial delta = 4  (descending)          */
/* ------------------------------------------------------------------ */
#define NUM_STARS 81   /* stars_nbr = 81-1 = 80 → 81 entries          */

#define REP32(v) v,v,v,v,v,v,v,v,v,v,v,v,v,v,v,v,v,v,v,v,v,v,v,v,v,v,v,v,v,v,v,v
static const short k_stars_dir_table[1025] = {
    /* wave up: 0→8 */
    REP32(0), REP32(1), REP32(2), REP32(3), REP32(4),
    REP32(5), REP32(6), REP32(7), REP32(8),
    /* wave down: 7→0 */
    REP32(7), REP32(6), REP32(5), REP32(4),
    REP32(3), REP32(2), REP32(1), REP32(0),
    /* wave negative: -1→-8 */
    REP32(-1), REP32(-2), REP32(-3), REP32(-4),
    REP32(-5), REP32(-6), REP32(-7), REP32(-8),
    /* wave back: -7→-1 */
    REP32(-7), REP32(-6), REP32(-5), REP32(-4),
    REP32(-3), REP32(-2), REP32(-1),
    -32   /* sentinel: wrap-around marker */
};
#undef REP32

/* Current positions in the direction table for each axis.            *
 * Reset to initial assembly offsets in stars_init().                 */
static int s_dir_x = 0;    /* word 0   → initial value 0             */
static int s_dir_y = 256;  /* word 256 → initial value 8             */
static int s_dir_z = 384;  /* word 384 → initial value 4             */

/* 3-D star coordinates matching the assembly layout:                 *
 *   x ∈ (-768/2, 768/2) = (-384, 384)                               *
 *   y ∈ (-512/2, 512/2) = (-256, 256)                               *
 *   z ∈ (64, 1024]      (z=0 becomes 960 after first wrap)          *
 * sx, sy, col: projected screen position and brightness this frame.  */
static struct { short x, y, z; short sx, sy, col; } s_stars[NUM_STARS];

/* ---- Assembly RNG (set_random_seed / get_rnd_number / rand) ------- */
static unsigned int s_rng0, s_rng1;

/* Mirrors get_rnd_number in menu.asm.  Returns seed[1] as result.    */
static unsigned int asm_get_rnd(void)
{
    unsigned int d0 = s_rng0;
    unsigned int d1 = s_rng1;

    /* and.b #$E, d0  /  or.b #$20, d0 */
    d0 = (d0 & ~(unsigned int)0xFF) | ((d0 & 0x0Eu) | 0x20u);

    unsigned int d2 = d0, d3 = d1;

    /* add.l d2,d2  /  addx.l d3,d3 */
    unsigned long long t = (unsigned long long)d2 + d2;
    int carry = (int)(t >> 32) & 1;
    d2 = (unsigned int)t;
    t = (unsigned long long)d3 + d3 + (unsigned int)carry;
    d3 = (unsigned int)t;

    /* add.l d2,d0  /  addx.l d3,d1 */
    t = (unsigned long long)d0 + d2;
    carry = (int)(t >> 32) & 1;
    d0 = (unsigned int)t;
    t = (unsigned long long)d1 + d3 + (unsigned int)carry;
    d1 = (unsigned int)t;

    /* swap d3  /  swap d2  /  move.w d2,d3  /  clr.w d2 */
    d3 = (d3 >> 16) | (d3 << 16);
    d2 = (d2 >> 16) | (d2 << 16);
    d3 = (d3 & 0xFFFF0000u) | (d2 & 0x0000FFFFu);
    d2 =  d2 & 0xFFFF0000u;

    /* add.l d2,d0  /  addx.l d3,d1 */
    t = (unsigned long long)d0 + d2;
    carry = (int)(t >> 32) & 1;
    d0 = (unsigned int)t;
    t = (unsigned long long)d1 + d3 + (unsigned int)carry;
    d1 = (unsigned int)t;

    s_rng0 = d0;
    s_rng1 = d1;
    return d1;
}

/* Mirrors set_random_seed (which falls through to get_rnd_number).   */
static void asm_seed(unsigned int d0, unsigned int d1)
{
    s_rng0 = d0;
    s_rng1 = d0 + d1;   /* add.l d0,d1 before storing */
    asm_get_rnd();       /* fall-through to get_rnd_number */
}

/* Mirrors rand(d0): returns high16(get_rnd()) % (range+1).           */
static unsigned short asm_rand(unsigned short range)
{
    if (range == 0) return 0;
    unsigned int rnd = asm_get_rnd();
    unsigned short h = (unsigned short)(rnd >> 16);
    return h % (unsigned short)(range + 1u);
}

/* ---- stars_init: mirrors create_stars_coords ---------------------- */
static void stars_init(void)
{
    /* Reset direction indices to assembly starting offsets */
    s_dir_x = 0;
    s_dir_y = 256;
    s_dir_z = 384;

    /* Seed the RNG exactly as the assembly does before the loop:       *
     *   move.l #$4D373729,d0                                           *
     *   move.l d0,d1                                                   *
     *   rol.w  #3,d1        ; rotate lower 16 bits left by 3          *
     *   swap   d1           ; swap upper/lower words                   *
     *   eor.l  #$5A5AA5A5,d1                                           *
     *   bsr    set_random_seed                                         */
    unsigned int d0 = 0x4D373729u;
    unsigned int d1 = d0;
    /* rol.w #3 on lower 16 bits of d1 */
    unsigned short lw = (unsigned short)(d1 & 0xFFFFu);
    lw = (unsigned short)((lw << 3) | (lw >> 13));
    d1 = (d1 & 0xFFFF0000u) | lw;
    /* swap d1 */
    d1 = (d1 >> 16) | (d1 << 16);
    /* eor.l #$5A5AA5A5,d1 */
    d1 ^= 0x5A5AA5A5u;
    asm_seed(d0, d1);

    /* create_x: stars_3d_coords + 0, stride 6 bytes (3 words) */
    for (int i = 0; i < NUM_STARS; i++) {
        s_stars[i].x = (short)((int)asm_rand(768) - 384);
    }
    /* create_y */
    for (int i = 0; i < NUM_STARS; i++) {
        s_stars[i].y = (short)((int)asm_rand(512) - 256);
    }
    /* create_z */
    for (int i = 0; i < NUM_STARS; i++) {
        s_stars[i].z = (short)asm_rand(1024);
    }

    /* Pre-fill sx/sy/col with 0 (no-draw until first update) */
    for (int i = 0; i < NUM_STARS; i++) {
        s_stars[i].sx = -1;
        s_stars[i].sy = -1;
        s_stars[i].col = 0;
    }
}

/* ---- stars_dir_next: advances one axis pointer, returns delta ----- *
 * Mirrors the move_stars logic: on hitting the sentinel (-32),        *
 * reset pointer to table[0] then advance to table[1].                 */
static short stars_dir_next(int *idx)
{
    short val = k_stars_dir_table[*idx];
    if (val == -32) {
        *idx = 0;
        val = k_stars_dir_table[0];
    }
    (*idx)++;
    return val;
}

/* ---- stars_update: mirrors move_stars + do_stars ------------------ */
static void stars_update(void)
{
    short dx = stars_dir_next(&s_dir_x);
    short dy = stars_dir_next(&s_dir_y);
    short dz = stars_dir_next(&s_dir_z);

    /* do_stars: add delta, wrap, project to screen */
    for (int i = 0; i < NUM_STARS; i++) {
        int x = (int)s_stars[i].x + dx;
        int y = (int)s_stars[i].y + dy;
        int z = (int)s_stars[i].z + dz;

        /* Wrap x: range (-384, 384] modulo 768 */
        if (x >= 384)  x -= 768;
        if (x <= -384) x += 768;

        /* Wrap y: range (-256, 256] modulo 512 */
        if (y >= 256)  y -= 512;
        if (y <= -256) y += 512;

        /* Wrap z: range (64, 1024] modulo 960 */
        if (z > 1024)  z -= 960;
        if (z <= 64)   z += 960;

        s_stars[i].x = (short)x;
        s_stars[i].y = (short)y;
        s_stars[i].z = (short)z;

        /* Perspective projection (stars_aspect_x=160, _y=127,          *
         * stars_centers=144,128):                                       *
         *   sx = x * 160 / z + 144                                     *
         *   sy = y * 127 / z + 128                                     */
        int sx = (x * 160) / z + 144;
        int sy = (y * 127) / z + 128;

        /* plot_star clips: 0 <= sx < 320 (384 in asm, clipped to 320), *
         * 0 <= sy < 256.  Brightness = (~(z>>7)) & 7, clamped 0–6.    */
        if (sx >= 0 && sx < 320 && sy >= 0 && sy < 256) {
            int bri = (~(z >> 7)) & 7;
            if (bri > 6) bri = 0;  /* palette[7] = black, map to 0 */
            s_stars[i].sx  = (short)sx;
            s_stars[i].sy  = (short)sy;
            s_stars[i].col = (short)bri;
        } else {
            s_stars[i].sx = -1;   /* off-screen: don't draw */
        }
    }
}

/*
 * Draw stars using palette slots 8–14 (offset 8, col 0–6 → slots 8–14).
 * Stars are rendered before the title so the title logo covers them.
 */
static void stars_draw(void)
{
    for (int i = 0; i < NUM_STARS; i++) {
        int x = s_stars[i].sx;
        int y = s_stars[i].sy;
        if (x >= 0)
            g_framebuffer[y * 320 + x] = (UBYTE)(8 + s_stars[i].col);
    }
}

/* ------------------------------------------------------------------ */
/* Palette helpers                                                     */
/* ------------------------------------------------------------------ */
static void build_combined_palette(void)
{
    for (int i = 0; i < 8; i++) {
        s_pal[i]      = k_palette_logo[i];
        s_pal[i +  8] = k_stars_palette[i];
        s_pal[i + 16] = k_palette_menu[i];
        s_pal[i + 24] = k_palette_copyright[i];
    }
}

/*
 * Fade all 32 palette slots to black and wait for completion.
 * Uses a tight frame loop (at most ~30 frames).
 */
static void fade_to_black_and_wait(void)
{
    static const UWORD k_black[32] = {0};
    palette_prep_fade_to_rgb(k_black, s_pal, 32);
    for (int i = 0; i < 40 && !g_done_fade; i++) {
        timer_begin_frame();
        palette_tick();
        video_present();
    }
    palette_set_immediate(k_black, 32);
}

/* ------------------------------------------------------------------ */
/* Credits text (from disp_credits / text_credits* in menu.asm)       */
/* ------------------------------------------------------------------ */
static const char *k_credits[9] = {
    "   PROGRAMMED BY:\n   ANDREAS TADIC\n    PETER TULEBY",
    "GRAPHICS AND CONCEPT\n         BY:\n     RICO HOLMES",
    " SOUND AND MUSIC BY:\n  ALLISTER BRIMBLE",
    "ADDITIONAL CODE AND\nCD32 PROGRAMMING BY:\n   STEFAN BOBERG",
    "SPECIAL EDITION CODING:\n     ANDREAS TADIC",
    "SPEECH COURTESY OF:\n   LYNETTE READE",
    "  GAME DESIGNED BY:\n     RICO HOLMES\n     MARTYN BROWN",
    " PROJECT MANAGEMENT\n         BY:\n    MARTYN BROWN",
    " A TEAM 17 SOFTWARE\n     PRODUCTION\n        1992"
};

/* ------------------------------------------------------------------ */
/* Draw the full menu scene (background + title + stars + optional     */
/* copyright + optional menu items).                                   */
/*                                                                     */
/* Neon title effect (matches display_title called from lev3irq):      */
/*   The title image stores two 89-row variants of the logo stacked    */
/*   vertically (rows 0–88 = first half, rows 89–177 = second half).   */
/*   flag_swap_title starts at 0; not.w toggles between 0 and 0xFFFF  */
/*   each VBlank:                                                       */
/*     non-zero result → add 89*40 offset → second half (rows 89+)     */
/*     zero result     → no offset        → first half  (rows 0+)      */
/*   Alternating at ~50 Hz produces the neon flicker effect.           */
/* ------------------------------------------------------------------ */
#define TITLE_HALF_H 89  /* height of each neon half in the title bitmap */

/* Mirrors flag_swap_title in menu.asm; 0 = use first half (rows 0–88) */
static int s_flag_swap_title = 0;

static void draw_bg(const Img *title, int copyright_y, const Img *copy)
{
    video_clear();
    stars_update();
    stars_draw();

    /* Toggle between the two halves every frame, matching not.w flag_swap_title
     * in lev3irq → display_title.  First call: flag 0→1 (non-zero) → second
     * half; second call: flag 1→0 (zero) → first half; etc. */
    s_flag_swap_title ^= 1;
    int title_src_y = s_flag_swap_title ? TITLE_HALF_H : 0;
    blit_offset_half(title, 0, 0, title_src_y, TITLE_HALF_H, 0);

    /* Copyright uses indices 0–7 offset by 24 (palette_copyright) */
    if (copy && copy->pixels && copyright_y < 256)
        blit_offset(copy, 0, copyright_y, 24);
}

/* ------------------------------------------------------------------ */
/* menu_run                                                            */
/* ------------------------------------------------------------------ */
MenuResult menu_run(int *out_num_players, int *out_share_credits)
{
    build_combined_palette();

    /* Load resources */
    Img  title = {NULL, 0, 0};
    Img  copy  = {NULL, 0, 0};
    Font font  = {0};

    img_load(&title, "assets/gfx/menu_title_320x180.raw");
    img_load(&copy,  "assets/gfx/menu_copyright_320x16.raw");
    font_load(&font, "assets/fonts/font_16x672.raw", 8, 16, 0);  /* advance=9 matches hardcoded 'add.l #9,d0' in menu.asm display_text */

    stars_init();

    /* Default values */
    MenuResult result      = MENU_RESULT_START;
    *out_num_players       = 1;
    *out_share_credits     = 0;
    int cur_item           = MENU_ITEM_START;   /* same default as pos_in_menu=2 in asm */

    /* ============================================================ */
    /* Phase 1: Title on black — fade in, then debounce            */
    /* Matches setup_copperlist (fades in logo palette,            */
    /* waits done_fade) then wait_button_press which waits until   */
    /* NO button is held (release debounce, NOT a press request).  */
    /* The menu appears immediately after — no button required.    */
    /* ============================================================ */
    {
        static UWORD s_zero[32] = {0};
        palette_prep_fade_in(s_pal, s_zero, 32);

        /* Wait for fade to complete */
        while (!g_quit_requested && !g_done_fade) {
            timer_begin_frame();
            input_poll();
            if (g_key_pressed == KEY_ESC) { result = MENU_RESULT_QUIT; goto cleanup; }
            draw_bg(&title, 300, NULL);
            palette_tick();
            video_present();
        }
        if (g_quit_requested) { result = MENU_RESULT_QUIT; goto cleanup; }

        /* wait_button_press: wait until no button is held (debounce) */
        while (!g_quit_requested) {
            timer_begin_frame();
            input_poll();
            if (g_key_pressed == KEY_ESC) { result = MENU_RESULT_QUIT; goto cleanup; }
            draw_bg(&title, 300, NULL);
            palette_tick();
            video_present();
            if (!(g_player1_input & INPUT_FIRE1)) break;
        }
        if (g_quit_requested) { result = MENU_RESULT_QUIT; goto cleanup; }
    }

    /* Ensure palette is fully applied before entering Phase 2 */
    palette_set_immediate(s_pal, 32);

    /* ============================================================ */
    /* Phase 2 / Phase 3 state machine                              */
    /* (matches main_loop in menu.asm)                              */
    /*                                                               */
    /* phase=0: menu display                                         */
    /* phase=1: credits display                                      */
    /* Pressing a button during Phase 3 resets phase back to 0,      */
    /* mirroring the break_main_loop_flag logic in menu.asm.         */
    /* ============================================================ */
    {
        int phase = 0;   /* 0 = menu, 1 = credits */

        /* Phase 2 state */
        int copyright_y = 256;
        int frames_idle = 0;
        int debounce    = 0;
        /* Build label strings once; rebuild when toggles change */
        char label_np[32];
        char label_sc[32];
        int  last_np = -1, last_sc = -1;   /* detect changes */

        /* Phase 3 state (disp_credits in menu.asm):                    *
         * credit_state: 0=wait 350 frames, 1=fade→red, 2=fade→black   */
        int   cidx          = 0;
        int   hold_frames   = 0;
        int   credit_state  = 0;
        int   fade_ctr      = 0;

        while (!g_quit_requested) {

            /* ---------------------------------------------------- */
            if (phase == 0) {
                /* Phase 2: Menu                                     */
                /* ------------------------------------------------- */
                timer_begin_frame();
                input_poll();

                if (g_key_pressed == KEY_ESC) { result = MENU_RESULT_QUIT; goto cleanup; }

                /* Slide copyright up from the bottom */
                if (copyright_y > 240) copyright_y--;

                draw_bg(&title, copyright_y, &copy);

                /* Rebuild label strings only when values change */
                if (*out_num_players != last_np) {
                    last_np = *out_num_players;
                    (void)snprintf(label_np, sizeof(label_np),
                                   last_np == 1 ? " ONE PLAYER GAME" : " TWO PLAYER GAME");
                }
                if (*out_share_credits != last_sc) {
                    last_sc = *out_share_credits;
                    (void)snprintf(label_sc, sizeof(label_sc),
                                   last_sc ? "SHARE CREDITS  ON " : "SHARE CREDITS  OFF");
                }

                const char *items[MENU_ITEMS];
                items[MENU_ITEM_NUM_PLAYERS]   = label_np;
                items[MENU_ITEM_SHARE_CREDITS] = label_sc;
                items[MENU_ITEM_START]         = "    START GAME    ";

                /*
                 * Screen layout (matches pos_in_menu*13 + 167 from menu.asm):
                 *   item 0 at y=167, item 1 at y=180, item 2 (START) at y=193
                 * Text drawn using palette_menu (slots 16-23, offset=16).
                 */
                for (int i = 0; i < MENU_ITEMS; i++) {
                    int item_y = 167 + i * 13;
                    if (i == cur_item)
                        video_fill_rect(68, item_y, 184, 12, 22);
                    TextCtx ctx;
                    typewriter_init_ctx(&ctx, &font, g_framebuffer, 320, 68, item_y);
                    ctx.color_offset = 16;
                    typewriter_display(&ctx, items[i]);
                }

                palette_tick();
                video_present();

                /* Input with debounce */
                if (debounce > 0) {
                    debounce--;
                } else if ((g_player1_input & INPUT_UP) && !(g_player1_old_input & INPUT_UP)) {
                    cur_item = (cur_item - 1 + MENU_ITEMS) % MENU_ITEMS;
                    audio_play_sample(SAMPLE_CARET_MOVE);
                    debounce = 8; frames_idle = 0;
                } else if ((g_player1_input & INPUT_DOWN) && !(g_player1_old_input & INPUT_DOWN)) {
                    cur_item = (cur_item + 1) % MENU_ITEMS;
                    audio_play_sample(SAMPLE_CARET_MOVE);
                    debounce = 8; frames_idle = 0;
                } else if ((g_player1_input & INPUT_FIRE1) && !(g_player1_old_input & INPUT_FIRE1)) {
                    frames_idle = 0;
                    switch (cur_item) {
                        case MENU_ITEM_START:
                            result = MENU_RESULT_START;
                            fade_to_black_and_wait();
                            goto cleanup;
                        case MENU_ITEM_NUM_PLAYERS:
                            *out_num_players = (*out_num_players == 1) ? 2 : 1;
                            last_np = -1;   /* force label rebuild */
                            debounce = 8;
                            break;
                        case MENU_ITEM_SHARE_CREDITS:
                            *out_share_credits = !(*out_share_credits);
                            last_sc = -1;   /* force label rebuild */
                            debounce = 8;
                            break;
                    }
                }

                /* Reset idle counter on any joystick input */
                if (g_player1_input & (INPUT_UP | INPUT_DOWN | INPUT_FIRE1 | INPUT_LEFT | INPUT_RIGHT))
                    frames_idle = 0;
                else
                    frames_idle++;

                /* 1000-frame idle timeout → switch to credits */
                if (frames_idle >= 1000) {
                    phase         = 1;
                    cidx          = 0;
                    hold_frames   = 0;
                    credit_state  = 0;
                    fade_ctr      = 0;
                    /* Ensure text palette is at normal menu colours */
                    for (int i = 0; i < 8; i++) s_pal[16 + i] = k_palette_menu[i];
                    palette_set_immediate(s_pal, 32);
                }

            } else {
                /* ------------------------------------------------- */
                /* Phase 3: Credits — mirrors disp_credits in menu.asm */
                /*   display_text: all text shown at once (no typewriter) */
                /*   .wait loop: 350 vsync frames, button → back to menu  */
                /*   fade palette_menu → palette_menu_red (FADE_SPEED=3)  */
                /*   fade palette_menu_red → black                         */
                /*   advance to next credit (or trigger story after all 9) */
                /* ------------------------------------------------- */
                timer_begin_frame();
                input_poll();

                if (g_key_pressed == KEY_ESC) { result = MENU_RESULT_QUIT; goto cleanup; }

                /* Draw background (no copyright during credits) */
                draw_bg(&title, 300, NULL);

                /* display_text: show full credit text immediately */
                {
                    TextCtx ctx;
                    typewriter_init_ctx(&ctx, &font, g_framebuffer, 320, 68, 136);
                    ctx.color_offset = 16;
                    typewriter_display(&ctx, k_credits[cidx]);
                }

                /* Button during credits → back to Phase 2
                 * (.button_pressed: break_main_loop_flag=1 in menu.asm) */
                if ((g_player1_input & INPUT_FIRE1) ||
                     g_key_pressed == KEY_SPACE      ||
                     g_key_pressed == KEY_RETURN) {
                    /* Restore normal text palette before returning to menu */
                    for (int i = 0; i < 8; i++) s_pal[16 + i] = k_palette_menu[i];
                    palette_set_immediate(s_pal, 32);
                    phase         = 0;
                    copyright_y   = 256;
                    frames_idle   = 0;
                    debounce      = 0;
                    credit_state  = 0;
                    hold_frames   = 0;
                    fade_ctr      = 0;
                    continue;
                }

                /* ---- State machine (mirrors .wait / fade sequence) ---- */
                /* FADE_SPEED equ 3 in menu.asm: one palette step per 3 frames */
#define CREDITS_FADE_SPEED 3
                static const UWORD k_text_black[8] = {
                    0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000
                };

                if (credit_state == 0) {
                    /* .wait: hold for 350 frames then start fade */
                    hold_frames++;
                    if (hold_frames >= 350) {
                        credit_state = 1;
                        fade_ctr     = 0;
                    }
                } else {
                    /* Fade one step every CREDITS_FADE_SPEED frames */
                    const UWORD *target = (credit_state == 1)
                                         ? k_palette_menu_red
                                         : k_text_black;
                    fade_ctr++;
                    if (fade_ctr >= CREDITS_FADE_SPEED) {
                        fade_ctr = 0;
                        int done = 1;
                        for (int i = 0; i < 8; i++) {
                            UWORD c  = s_pal[16 + i];
                            UWORD tc = target[i];
                            int r = (c  >> 8) & 0xF, gv = (c  >> 4) & 0xF, b = c  & 0xF;
                            int tr= (tc >> 8) & 0xF,tg = (tc >> 4) & 0xF,tb = tc & 0xF;
                            if (r != tr) { r  += (r  < tr) ?  1 : -1; done = 0; }
                            if (gv!= tg) { gv += (gv < tg) ?  1 : -1; done = 0; }
                            if (b != tb) { b  += (b  < tb) ?  1 : -1; done = 0; }
                            s_pal[16 + i] = (UWORD)(((r&0xF)<<8)|((gv&0xF)<<4)|(b&0xF));
                        }
                        if (done) {
                            if (credit_state == 1) {
                                /* palette_menu → red done: now fade red → black */
                                credit_state = 2;
                                fade_ctr     = 0;
                            } else {
                                /* Both fades done → advance to next credit */
                                cidx++;
                                if (cidx >= 9) {
                                    /* All 9 shown → trigger story (return_value=-1) */
                                    result = MENU_RESULT_AUTO_EXIT;
                                    goto cleanup;
                                }
                                /* Restore normal text palette for the next credit */
                                for (int i = 0; i < 8; i++)
                                    s_pal[16 + i] = k_palette_menu[i];
                                hold_frames  = 0;
                                credit_state = 0;
                                fade_ctr     = 0;
                            }
                        }
                    }
                }
#undef CREDITS_FADE_SPEED

                /* Apply updated text palette */
                palette_set_immediate(s_pal, 32);
                video_present();
            }
        }

        if (g_quit_requested) { result = MENU_RESULT_QUIT; goto cleanup; }
    }

cleanup:
    img_free(&title);
    img_free(&copy);
    font_free(&font);
    return result;
}

