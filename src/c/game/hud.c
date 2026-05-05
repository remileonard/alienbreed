/*
 * Alien Breed SE 92 - C port
 * HUD module
 */

#include "hud.h"
#include "player.h"
#include "level.h"
#include "../engine/sprite.h"
#include "../engine/tilemap.h"
#include "../engine/palette.h"
#include "../engine/typewriter.h"
#include "../hal/video.h"
#include "../hal/vfs.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int g_number_players = 1;

/* Status bar graphics (converted from .lo2 files) */
typedef struct { UBYTE *pixels; int w, h; } GfxImage;

static GfxImage s_p1_bar;
static GfxImage s_p2_bar;
static GfxImage s_paused;
static GfxImage s_mapbkgnd;
static Font     s_mapfont;

static int load_gfx(GfxImage *img, const char *path)
{
    VFile *f = vfs_open(path);
    if (!f) return -1;
    int w, h;
    if (vfs_read(&w, 4, 1, f) != 1 || vfs_read(&h, 4, 1, f) != 1) { vfs_close(f); return -1; }
    img->w = w; img->h = h;
    img->pixels = (UBYTE *)malloc((size_t)(w * h));
    if (!img->pixels) { vfs_close(f); return -1; }
    if (vfs_read(img->pixels, 1, (size_t)(w * h), f) != (size_t)(w * h)) {
        free(img->pixels); img->pixels = NULL;
        vfs_close(f); return -1;
    }
    vfs_close(f);
    return 0;
}

int hud_init(void)
{
    load_gfx(&s_p1_bar, "assets/gfx/main_player_1_status_304x8.raw");
    load_gfx(&s_p2_bar, "assets/gfx/main_player_2_status_304x8.raw");
    load_gfx(&s_paused, "assets/gfx/main_game_paused_96x7.raw");
    load_gfx(&s_mapbkgnd, "assets/tiles/mapbkgnd.raw");
    /* Map-overview font: same font_pic used by on_map_font_struct (main.asm#L8879),
     * letter_w=9, letter_h=12. */
    font_load(&s_mapfont, "assets/fonts/font_16x504.raw", 9, 12, 0);
    sprite_load_player();
    return 0;
}

void hud_quit(void)
{
    free(s_p1_bar.pixels); s_p1_bar.pixels = NULL;
    free(s_p2_bar.pixels); s_p2_bar.pixels = NULL;
    free(s_paused.pixels); s_paused.pixels = NULL;
    free(s_mapbkgnd.pixels); s_mapbkgnd.pixels = NULL;
    font_free(&s_mapfont);
    sprite_free_all();
}

/*
 * Layout of the 304×8 pixel status bar (pixel positions from ASM lbW00FF66).
 * The bar renders at BAR_X=0, spanning x=0..303; x=304..319 filled black.
 *
 *  Bar offset  Width   Element
 *    0          24     Static background ("1UP"/"2UP" text from .lo2 image)
 *   24          64     Health bar  (64 px = PLAYER_MAX_HEALTH=64)
 *   88          40     Static background gap
 *  128           8     Lives: 4 × (2px lit + 2px gap) at x=128,132,136,140
 *  136          48     Static background (includes lives label area)
 *  184          12     Ammo packs: 4 × (2px lit + 1px gap) at x=184,187,190,193
 *  196           4     Static gap
 *  200          32     Ammo bar    (32 px = PLAYER_MAX_AMMO=32)
 *  232          31     Static background (weapon icon, etc.)
 *  263          24     Keys: 6 × 2-px independent bars (lbW00FF66 derived):
 *                        fills at x=264,268,272,276,280,284 (width 2 each)
 *                        border pixels 263,266,270,274,278,282,287 left to BG
 *                        '+' cross at x=288..292 (rows 1-5) if keys > 6
 *  287          17     Static background to end of bar (304)
 * 304          16     Filled black to reach 320-px screen edge
 */

/*
 * The bar image is 304 pixels wide; the screen is 320 pixels wide.
 * BAR_X = 0: bar starts at the left edge of the screen.
 * Rows are explicitly filled to x=319 so the full width is covered.
 */
#define SCREEN_W        320    /* full screen width */
#define SCREEN_H        256    /* full screen height */
#define BAR_X            0    /* left edge of bar on screen */
#define BAR_W          304    /* bar image pixel width */
#define BAR_P1_Y         0    /* Player 1 bar: top of screen */
#define BAR_P2_Y       248    /* Player 2 bar: bottom of screen */
#define BAR_H            8    /* bar height (full 8 scan lines) */

/* Pixel offsets within the 304-px bar (match lbW00FF66 table in main.asm) */
#define BAR_HEALTH_OFF    24
#define BAR_HEALTH_W      64
#define BAR_LIVES_OFF    128
#define BAR_AMMO_PKS_OFF 184
#define BAR_AMMO_OFF     200
#define BAR_AMMO_W        32

/*
 * 2-pixel fill positions for each key slot, derived from lbW00FF66 table:
 *   slot 0: pixels 264-265  (3-px slot at 263-265: 1px border + 2px fill)
 *   slots 1-5: pixels slot_base+2..slot_base+3  (4/5-px slot: 2px border + 2px fill)
 * Positions from lbL00FE8C: {263,266,270,274,278,282,287}.
 * '+' sign (keys > 6): 5×1 horizontal at row 3, x=288..292;
 *                       1×1 vertical at x=290, rows 1,2,4,5.
 */
static const int k_key_fill_x[6] = { 264, 268, 272, 276, 280, 284 };
#define KEY_PLUS_CX        290   /* centre column of '+' sign */
#define KEY_PLUS_LX        288   /* left  column of horizontal bar */
#define KEY_PLUS_W           5   /* width of horizontal bar */
#define KEY_PLUS_TOP_ROW     1   /* first bar row showing '+' (ASM rows 1-5) */
#define KEY_PLUS_BOT_ROW     5   /* last  bar row showing '+' */
#define KEY_PLUS_HBAR_ROW    3   /* row where horizontal bar of '+' is drawn */

/*
 * Per-row overlay palette for the 8 scan lines of each status bar.
 *
 * Derived from the Amiga copper list in main.asm:
 *   main_top_bar_bps    (lines 18450-18507) — top bar, Player 1
 *   main_bottom_bar_bps (lines 18541-18599) — bottom bar, Player 2
 * Both bars use the identical vertical colour gradient.
 *
 * Amiga 12-bit colour $XYZ  →  R = X×17, G = Y×17, B = Z×17.
 * COLOR03 dither: each scanline has two very similar values; we use the
 * "B" value (the slightly higher of the pair) as the canonical colour:
 *   row 0: $620,  row 1: $940,  row 2: $C80,  row 3: $EA0 (peak),
 *   row 4: $C80,  row 5: $A40,  row 6: $720,  row 7: $520.
 *
 * 2-bitplane .lo2 colour index mapping:
 *   0 = background (black)
 *   1 = dark shade  (borders, empty bar, text shadow)
 *   2 = light shade (labels, decorative elements)
 *   3 = hot orange  (filled health / ammo / keys indicator)
 */
typedef struct { UBYTE r, g, b; } BarRGB;

static const BarRGB k_bar_pal[BAR_H][4] = {
    /* row 0 ($2B): C1=$111, C2=$444, C3=$620 */
    { {0,0,0}, {17,17,17},   {68,68,68},   {102,34,0}  },
    /* row 1 ($2C): C1=$222, C2=$888, C3=$940 */
    { {0,0,0}, {34,34,34},   {136,136,136},{153,68,0}  },
    /* row 2 ($2D): C1=$333, C2=$CCC, C3=$C80 */
    { {0,0,0}, {51,51,51},   {204,204,204},{204,136,0} },
    /* row 3 ($2E): C1=$444, C2=$DDD, C3=$EA0  ← brightest row */
    { {0,0,0}, {68,68,68},   {221,221,221},{238,170,0} },
    /* row 4 ($2F): C1=$333, C2=$DDD, C3=$C80 */
    { {0,0,0}, {51,51,51},   {221,221,221},{204,136,0} },
    /* row 5 ($30): C1=$222, C2=$CCC, C3=$A40 */
    { {0,0,0}, {34,34,34},   {204,204,204},{170,68,0}  },
    /* row 6 ($31): C1=$111, C2=$888, C3=$720 */
    { {0,0,0}, {17,17,17},   {136,136,136},{119,34,0}  },
    /* row 7 ($32): C1=$111, C2=$444, C3=$520 */
    { {0,0,0}, {17,17,17},   {68,68,68},   {85,34,0}   },
};

/*
 * Blit one scanline row of the static bar image as an overlay.
 * The bar image is BAR_W (304) pixels wide starting at BAR_X (0).
 * After blitting the image the remaining pixels to the right edge of the
 * 320-pixel screen (x = BAR_W .. 319) are filled with the background colour
 * (palette index 0 = black) so that the bar spans the full screen width.
 * Consecutive pixels sharing the same colour index are merged into a
 * single fill_rect call to keep the overlay draw call count low.
 */
static void bar_blit_row(const GfxImage *bg, int row, int bar_y)
{
    if (row < 0 || row >= BAR_H) return;
    const BarRGB *pal = k_bar_pal[row];
    int sy = bar_y + row;

    /* --- static image (304 px) --- */
    if (bg && bg->pixels && row < bg->h) {
        const UBYTE *src = bg->pixels + (size_t)row * (size_t)bg->w;
        int x = 0;
        while (x < bg->w) {
            UBYTE c = src[x] & 3u;
            int   rx = x;
            while (x < bg->w && (src[x] & 3u) == c) x++;
            const BarRGB *col = &pal[c];
            video_overlay_fill_rect(BAR_X + rx, sy, x - rx, 1,
                                    col->r, col->g, col->b, 255);
        }
    } else {
        /* No image: fill the row with the dark shade */
        video_overlay_fill_rect(BAR_X, sy, BAR_W, 1,
                                pal[1].r, pal[1].g, pal[1].b, 255);
    }

    /* --- fill the remaining pixels to reach full screen width --- */
    video_overlay_fill_rect(BAR_X + BAR_W, sy, SCREEN_W - BAR_W, 1,
                            pal[0].r, pal[0].g, pal[0].b, 255);
}

/*
 * Render one player's full status bar as renderer overlays.
 * Must be called after video_upload_framebuffer() so overlays sit on top.
 *
 * The function:
 *   1. Blits the static 304×8 background image row by row, applying the
 *      copper-derived per-scanline RGBA palette (the "gradient effect"),
 *      then fills x=304..319 with black so the bar spans the full 320 px.
 *   2. Draws the dynamic elements (health, lives, ammo, keys) on top using
 *      the same per-row palette, mirroring what the Amiga CPU wrote into the
 *      bar bitplane buffer each frame before display.
 *
 * Key rendering (from ASM lbW00FF66 pixel table + lbL00FE8C positions):
 *   Each of the 6 key slots has exactly 2 lit pixels within it.  When the
 *   player holds more than 6 keys a '+' cross is drawn at x=288..292 for
 *   bar rows 1-5 (rows 0, 6, 7 are blank), matching print_more_6_keys_sign.
 */
static void render_bar_overlay(const GfxImage *bg, int bar_y, const Player *p)
{
    for (int row = 0; row < BAR_H; row++) {
        const BarRGB *pal = k_bar_pal[row];
        int           sy  = bar_y + row;

        /* 1. Static background + right margin fill */
        bar_blit_row(bg, row, bar_y);

        /* 2. Health bar (pixels 24..87 in bar, 64 px wide at max health) */
        int hp = (p->health * BAR_HEALTH_W) / PLAYER_MAX_HEALTH;
        if (hp < 0) hp = 0;
        if (hp > BAR_HEALTH_W) hp = BAR_HEALTH_W;
        if (hp > 0)
            video_overlay_fill_rect(BAR_X + BAR_HEALTH_OFF, sy, hp, 1,
                                    pal[3].r, pal[3].g, pal[3].b, 255);
        if (hp < BAR_HEALTH_W)
            video_overlay_fill_rect(BAR_X + BAR_HEALTH_OFF + hp, sy,
                                    BAR_HEALTH_W - hp, 1,
                                    pal[1].r, pal[1].g, pal[1].b, 255);

        /* 3. Lives indicator: 4 slots × 2 px lit, stride 4 (from lbW00FF66) */
        for (int i = 0; i < 4; i++) {
            const BarRGB *col = (i < p->lives) ? &pal[3] : &pal[1];
            video_overlay_fill_rect(BAR_X + BAR_LIVES_OFF + i * 4, sy, 2, 1,
                                    col->r, col->g, col->b, 255);
        }

        /* 4. Ammo packs: 4 slots × 2 px lit at positions 184,187,190,193 */
        for (int i = 0; i < 4; i++) {
            const BarRGB *col = (i < p->ammopacks) ? &pal[3] : &pal[1];
            video_overlay_fill_rect(BAR_X + BAR_AMMO_PKS_OFF + i * 3, sy, 2, 1,
                                    col->r, col->g, col->b, 255);
        }

        /* 5. Ammo bar (pixels 200..231, 32 px wide at max ammo) */
        int ammo = (p->ammunitions * BAR_AMMO_W) / PLAYER_MAX_AMMO;
        if (ammo < 0) ammo = 0;
        if (ammo > BAR_AMMO_W) ammo = BAR_AMMO_W;
        if (ammo > 0)
            video_overlay_fill_rect(BAR_X + BAR_AMMO_OFF, sy, ammo, 1,
                                    pal[3].r, pal[3].g, pal[3].b, 255);
        if (ammo < BAR_AMMO_W)
            video_overlay_fill_rect(BAR_X + BAR_AMMO_OFF + ammo, sy,
                                    BAR_AMMO_W - ammo, 1,
                                    pal[1].r, pal[1].g, pal[1].b, 255);

        /* 6. Keys: 6 independent 2-px bars (from lbW00FF66 + lbL00FE8C).
         *    Each slot has exactly 2 lit pixels; the surrounding border pixels
         *    are provided by the static background image and are not overwritten.
         *    '+' sign (mirroring print_more_6_keys_sign) when keys > 6:
         *      rows 1,2,4,5 → 1px at x=290; row 3 → 5px at x=288..292. */
        int disp_keys = p->keys;
        if (disp_keys > 6) disp_keys = 6;
        for (int i = 0; i < 6; i++) {
            const BarRGB *col = (i < disp_keys) ? &pal[3] : &pal[1];
            video_overlay_fill_rect(BAR_X + k_key_fill_x[i], sy, 2, 1,
                                    col->r, col->g, col->b, 255);
        }
        /* '+' sign */
        if (p->keys > 6) {
            if (row == KEY_PLUS_HBAR_ROW) {
                video_overlay_fill_rect(BAR_X + KEY_PLUS_LX, sy, KEY_PLUS_W, 1,
                                        pal[3].r, pal[3].g, pal[3].b, 255);
            } else if (row >= KEY_PLUS_TOP_ROW && row <= KEY_PLUS_BOT_ROW) {
                video_overlay_fill_rect(BAR_X + KEY_PLUS_CX, sy, 1, 1,
                                        pal[3].r, pal[3].g, pal[3].b, 255);
            }
        }
    }
}

void hud_render(void)
{
    /* Bars and timer are now rendered in hud_render_overlay() after framebuffer
     * upload so that the copper-list gradient palette is applied correctly. */
}

void hud_render_overlay(void)
{
    /* ---- Player 1 status bar: top of screen (y=0) ---- */
    render_bar_overlay(&s_p1_bar, BAR_P1_Y, &g_players[0]);

    /* ---- Player 2 status bar: bottom of screen (y=248) ---- */
    render_bar_overlay(&s_p2_bar, BAR_P2_Y, &g_players[1]);

    /* ---- Destruction timer: displayed only during the self-destruct sequence ---- */
    if (g_self_destruct_initiated) {
        /*
         * Position from ASM sprite structs:
         *   lbW0122F0: dc.w 6,24,32,128  (hi digit: x_struct=6, y_struct=24)
         *   lbW01230C: dc.w 23,24,32,128 (lo digit: x_struct=23, y_struct=24)
         *
         * Conversion to screen pixels (DIWSTRT=$2B8E on Amiga PAL):
         *   screen_x = x_struct + (HSTART_offset - DIWSTRT_H)
         *            = x_struct + (143 - 142) = x_struct + 1
         *   screen_y = y_struct + VSTRT_offset - DIWSTRT_V
         *            = y_struct + 35 - 43 = y_struct - 8
         * → hi digit at screen (7, 16), lo digit at screen (24, 16)
         */
        int tx = 7;   /* x_struct=6:  6 + 1 = 7  */
        int ty = 16;  /* y_struct=24: 24 - 8 = 16 */

        /*
         * Color: sprite COLOR17 = g_palette[17] (first color for Amiga sprite
         * pair 0/1, set to a red shade by the destruction palette palette_b).
         * Fallback to bright red when the palette is not loaded.
         * Ref: copper_main_palette COLOR17 entry @ main.asm#L18444;
         *      palette_set_immediate(g_cur_map.palette_b, 32) in level_start_destruction().
         */
        UBYTE tr = (UBYTE)((g_palette[17] >> 16) & 0xFF);
        UBYTE tg = (UBYTE)((g_palette[17] >>  8) & 0xFF);
        UBYTE tb = (UBYTE)((g_palette[17] >>  0) & 0xFF);
        if (tr == 0 && tg == 0 && tb == 0) { tr = 0xCC; tg = 0x00; tb = 0x00; }

        /*
         * Format: two raw decimal digits — matches ASM cur_timer_digit_hi:lo
         * which are decremented directly (no minutes conversion).
         * Ref: display_timer_digits @ main.asm#L1321 which writes hi then lo.
         *
         * Digit spacing: lbW01230C.x (23) - lbW0122F0.x (6) = 17 pixels.
         */
#define TIMER_DIGIT_SPACING  17  /* screen_x distance: struct_x 23-6 = 17 */
        int hi = (int)g_destruction_timer / 10;
        int lo = (int)g_destruction_timer % 10;
        sprite_draw_digit_overlay(hi, tx,                     ty, tr, tg, tb);
        sprite_draw_digit_overlay(lo, tx + TIMER_DIGIT_SPACING, ty, tr, tg, tb);
    }
}

void hud_render_pause(void)
{
    if (!s_paused.pixels) return;
    int px = (320 - s_paused.w) / 2;
    int py = (256 - s_paused.h) / 2;
    video_blit(s_paused.pixels, s_paused.w, px, py, s_paused.w, s_paused.h, 0);
}

/*
 * Map overview palette: 16 background colours from mapbkgnd_320x256.lo4 +
 * 16 overlay colours from map_overview_overlay_palette @ main.asm#L8970.
 *
 * Background (indices 0-15):  Amiga 12-bit $0RGB from the .lo4 palette tail.
 * Overlay    (indices 16-31): Amiga 12-bit colours from map_overview_overlay_palette.
 *
 * Reference:
 *   load_map_overview @ main.asm#L8941-L8967
 *   map_overview_overlay_palette dc.w $346,$457,$556,$568,$866,$679,$976,$B76,
 *                                      $78A,$C86,$D96,$FD6,$FFB,$89B,$9AC,$ABD
 */
static const UWORD k_overmap_pal[32] = {
    /* Background palette (from mapbkgnd_320x256.lo4 at offset 40960) */
    0x0000, 0x0111, 0x0210, 0x0222, 0x0520, 0x0333, 0x0630, 0x0830,
    0x0444, 0x0940, 0x0A50, 0x0F90, 0x0FB5, 0x0555, 0x0666, 0x0777,
    /* Overlay palette (map_overview_overlay_palette @ main.asm#L8970) */
    0x0346, 0x0457, 0x0556, 0x0568, 0x0866, 0x0679, 0x0976, 0x0B76,
    0x078A, 0x0C86, 0x0D96, 0x0FD6, 0x0FFB, 0x089B, 0x09AC, 0x0ABD
};

/*
 * Render the in-game map overview (toggled by the M key).
 *
 * Mirrors plot_map_overview_data + get_players_position @ main.asm#L8739-L8917.
 *
 * Coordinate derivation (from ASM reverse-engineering):
 *   - The 6-bitplane display section starts at Amiga screen line 52 = C screen y=8.
 *   - Bitplane row r → C screen y = 8 + r.
 *   - plot_map_overview_data places tile (map_row, map_col) at bitplane row
 *       d1 = 8 + 2*map_row  and  bitplane col d0 = 2 + 2*map_col.
 *   → C screen position: x = 2 + 2*col,  y = (8 + d1) = 16 + 2*row.
 *
 * Map centering:
 *   Rendered map pixel size = MAP_COLS×2 × MAP_ROWS×2 = 240×192 px.
 *   Origin is centred on the background image:
 *     map_ox = (bg_w - 240) / 2,  map_oy = (bg_h - 192) / 2.
 *   For a 320×256 background this gives (40, 32).
 *
 *   Wall   (attr==1) → colour = bg_pixel + 16  (plane-5 overlay, mirrors Amiga
 *                       6-bitplane colour index combining background + plane 5).
 *   Door   (attr==3) → EHB colour: bg_pixel as-is but drawn with the door plane
 *                       colour index bg_pixel + 20 (slight visual distinction).
 *   Player label      → '1'/'2' character from on_map_font_struct (font_16x504,
 *                       letter_w=9, letter_h=12) in colour OVERMAP_PAL_MAX.
 *                       Positioned at (dot_x+18, dot_y-16) per ASM
 *                       print_player_pos_on_map @ main.asm#L8860-L8868.
 */
#define OVERMAP_PAL_MAX      ((int)(sizeof(k_overmap_pal)/sizeof(k_overmap_pal[0])) - 1)
#define OVERMAP_WALL_OFFSET  16   /* plane-5 colour offset: bg_col + 16 = wall colour */
#define OVERMAP_DOOR_OFFSET  20   /* door plane colour offset (EHB simulation) */
void hud_render_map_overview(void)
{
    /* Apply the dedicated map-overview palette (background + overlay). */
    palette_set_immediate(k_overmap_pal, (int)(sizeof(k_overmap_pal)/sizeof(k_overmap_pal[0])));

    /* Blit the background image at (0, 0) so the decorative frame is visible. */
    if (s_mapbkgnd.pixels)
        video_blit(s_mapbkgnd.pixels, s_mapbkgnd.w, 0, 0,
                   s_mapbkgnd.w, s_mapbkgnd.h, -1);

    if (!g_cur_map.valid) return;

    /* Compute the rendered map size in pixels (2 px per tile) and centre it
     * on the background image so the map always appears in the middle of the
     * decorative frame regardless of map or screen dimensions. */
    const int map_pixel_w = MAP_COLS * 2;
    const int map_pixel_h = MAP_ROWS * 2;
    const int bg_w = s_mapbkgnd.pixels ? s_mapbkgnd.w : SCREEN_W;
    const int bg_h = s_mapbkgnd.pixels ? s_mapbkgnd.h : SCREEN_H;
    const int map_ox = (bg_w - map_pixel_w) / 2;
    const int map_oy = (bg_h - map_pixel_h) / 2;

    /* Plot 2×2 pixel blocks for solid tiles (walls and doors).
     * Only attr==1 (TILE_WALL) and attr==3 (TILE_DOOR) are drawn, mirroring
     * the ASM which only tests those two tile attribute values. */
    for (int row = 0; row < MAP_ROWS; row++) {
        for (int col = 0; col < MAP_COLS; col++) {
            UBYTE attr = tilemap_attr(&g_cur_map, col, row);
            if (attr != TILE_WALL && attr != TILE_DOOR) continue;

            int px = map_ox + col * 2;
            int py = map_oy + row * 2;
            if (px < 0 || px + 1 >= SCREEN_W || py < 0 || py + 1 >= SCREEN_H) continue;

            /* Wall colour: bg_pixel + OVERMAP_WALL_OFFSET exactly replicates
             * Amiga plane-5 overlay (6-bitplane colour index = background bits
             * + plane-5 bit).
             * Door colour: use bg_pixel + OVERMAP_DOOR_OFFSET (a different band
             * of the overlay palette) to give a visually distinct appearance
             * from walls, since doors used EHB (plane 6) in the original
             * hardware which produced a half-brightness tint not reproducible
             * as a simple offset.                                              */
            UBYTE bg_col = 0;
            if (s_mapbkgnd.pixels
                    && py < s_mapbkgnd.h && px < s_mapbkgnd.w)
                bg_col = s_mapbkgnd.pixels[py * s_mapbkgnd.w + px] & 0x0F;
            int offset = (attr == TILE_DOOR) ? OVERMAP_DOOR_OFFSET
                                             : OVERMAP_WALL_OFFSET;
            UBYTE color = (UBYTE)(bg_col + offset);
            if (color > OVERMAP_PAL_MAX) color = (UBYTE)OVERMAP_PAL_MAX;

            g_framebuffer[ py      * SCREEN_W + px    ] = color;
            g_framebuffer[ py      * SCREEN_W + px + 1] = color;
            g_framebuffer[(py + 1) * SCREEN_W + px    ] = color;
            g_framebuffer[(py + 1) * SCREEN_W + px + 1] = color;
        }
    }

    /* Draw player position as a '1'/'2' character label.
     * Mirrors print_players_pos_on_map @ main.asm#L8850-L8868:
     *   get_players_position computes tile pos (pos_x>>3+24, pos_y>>3+20);
     *   print_player_pos_on_map shifts by (+18, -16) then draws '1' or '2'
     *   with on_map_font_struct (font_pic, letter_w=9, letter_h=12).
     * In the C port with centred map:
     *   dot position: (map_ox + pos_x/8, map_oy + pos_y/8)
     *   label origin: dot + (18, -16)  (same relative offsets as ASM).     */
    if (s_mapfont.pixels) {
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (!g_players[i].alive) continue;
            int dot_x = map_ox + g_players[i].pos_x / 8;
            int dot_y = map_oy + g_players[i].pos_y / 8;
            TextCtx ctx;
            typewriter_init_ctx(&ctx, &s_mapfont, g_framebuffer, SCREEN_W,
                                dot_x + 18, dot_y - 16);
            ctx.text_color = OVERMAP_PAL_MAX;
            char label[2] = { (char)('1' + i), '\0' };
            typewriter_display(&ctx, label);
        }
    }
}
