/*
 * Alien Breed SE 92 - C port
 * HUD module
 */

#include "hud.h"
#include "player.h"
#include "level.h"
#include "../engine/sprite.h"
#include "../engine/tilemap.h"
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
    sprite_load_player();
    return 0;
}

void hud_quit(void)
{
    free(s_p1_bar.pixels); s_p1_bar.pixels = NULL;
    free(s_p2_bar.pixels); s_p2_bar.pixels = NULL;
    free(s_paused.pixels); s_paused.pixels = NULL;
    sprite_free_all();
}

/*
 * Layout of the 304×8 pixel status bar.
 * Derived from ASM lbW00FF66 table and lbL00FE00/lbL00FE04/... positions.
 *
 *  Bar offset  Width   Element
 *    0          24     Static background ("1UP"/"2UP" text from .lo2 image)
 *   24          64     Health bar  (64 px = PLAYER_MAX_HEALTH=64)
 *   88          40     Static background gap
 *  128           8     Lives indicator: 4 dots at x+0,+4,+8,+12 (2 px each)
 *  144          40     Static background ("LIVES" label)
 *  184          12     Ammo packs: 4 dots at x+0,+3,+6,+9 (2 px each)
 *  200          32     Ammo bar    (32 px = PLAYER_MAX_AMMO=32)
 *  232          31     Static background (weapon area)
 *  263          24     Keys: up to 6 dots; start offsets from ASM lbL00FE8C:
 *                        {263,266,270,274,278,282} → widths {2,3,3,3,3,3}
 *  287          17     Static background to end of bar
 */

#define BAR_X            8    /* left edge of bar on screen */
#define BAR_P1_Y         0    /* Player 1 bar: top of screen */
#define BAR_P2_Y       248    /* Player 2 bar: bottom of screen */
#define BAR_H            8    /* bar height (full 8 scan lines) */

/* Pixel offsets within the 304-px bar */
#define BAR_HEALTH_OFF    24
#define BAR_HEALTH_W      64
#define BAR_LIVES_OFF    128
#define BAR_AMMO_PKS_OFF 184
#define BAR_AMMO_OFF     200
#define BAR_AMMO_W        32

/* Key dot start positions (from ASM lbL00FE8C = {$107,$10A,$10E,$112,$116,$11A}) */
static const int k_key_off[6] = { 263, 266, 270, 274, 278, 282 };
static const int k_key_end    = 287;

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
 * Consecutive pixels sharing the same colour index are merged into a
 * single fill_rect call to keep the overlay draw call count low.
 */
static void bar_blit_row(const GfxImage *bg, int row, int bar_y)
{
    if (!bg || !bg->pixels || row >= bg->h) return;
    const UBYTE  *src = bg->pixels + (size_t)row * (size_t)bg->w;
    const BarRGB *pal = k_bar_pal[row];
    int x = 0;
    while (x < bg->w) {
        UBYTE c = src[x] & 3u;
        int   rx = x;
        while (x < bg->w && (src[x] & 3u) == c) x++;
        const BarRGB *col = &pal[c];
        video_overlay_fill_rect(BAR_X + rx, bar_y + row,
                                x - rx, 1,
                                col->r, col->g, col->b, 255);
    }
}

/*
 * Render one player's full status bar as renderer overlays.
 * Must be called after video_upload_framebuffer() so overlays sit on top.
 *
 * The function:
 *   1. Blits the static 304×8 background image row by row, applying the
 *      copper-derived per-scanline RGBA palette (the "gradient effect").
 *   2. Draws the dynamic elements (health, lives, ammo, keys) on top using
 *      the same per-row palette, mirroring what the Amiga CPU wrote into the
 *      bar bitplane buffer each frame before display.
 */
static void render_bar_overlay(const GfxImage *bg, int bar_y, const Player *p)
{
    for (int row = 0; row < BAR_H; row++) {
        const BarRGB *pal = k_bar_pal[row];
        int           sy  = bar_y + row;

        /* 1. Static background (text labels, decorative frame) */
        if (bg && bg->pixels) {
            bar_blit_row(bg, row, bar_y);
        } else {
            /* No image: fill the row with the dark shade */
            video_overlay_fill_rect(BAR_X, sy, 304, 1,
                                    pal[1].r, pal[1].g, pal[1].b, 255);
        }

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

        /* 3. Lives indicator: up to 4 dots at offsets 128,132,136,140 (2 px each) */
        for (int i = 0; i < 4; i++) {
            const BarRGB *col = (i < p->lives) ? &pal[3] : &pal[1];
            video_overlay_fill_rect(BAR_X + BAR_LIVES_OFF + i * 4, sy, 2, 1,
                                    col->r, col->g, col->b, 255);
        }

        /* 4. Ammo packs: up to 4 dots at offsets 184,187,190,193 (2 px each) */
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

        /* 6. Keys: up to 6 dots; positions from ASM lbL00FE8C */
        for (int i = 0; i < 6; i++) {
            int next = (i < 5) ? k_key_off[i + 1] : k_key_end;
            const BarRGB *col = (i < p->keys) ? &pal[3] : &pal[1];
            video_overlay_fill_rect(BAR_X + k_key_off[i], sy,
                                    next - k_key_off[i], 1,
                                    col->r, col->g, col->b, 255);
        }
    }
}

/* Draw a 2-digit decimal number using timer digit sprites as overlays */
static void draw_two_digits_overlay(int hi, int lo, int x, int y)
{
    sprite_draw_digit_overlay(hi, x,     y, 221, 221, 221);
    sprite_draw_digit_overlay(lo, x + 9, y, 221, 221, 221);
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

    /* ---- Destruction timer: centred just inside the top bar ---- */
    int mins, sh, sl;
    level_get_timer_digits(&mins, &sh, &sl);

    /* Timer x=148 centres the "M:SS" display (≈20 px) in 320-px screen */
    int tx = 148;
    int ty = BAR_P1_Y;
    sprite_draw_digit_overlay(mins, tx,      ty, 221, 221, 221);
    /* colon dots */
    video_overlay_fill_rect(tx + 9, ty + 2, 1, 1, 221, 221, 221, 255);
    video_overlay_fill_rect(tx + 9, ty + 5, 1, 1, 221, 221, 221, 255);
    draw_two_digits_overlay(sh, sl, tx + 11, ty);
}

void hud_render_pause(void)
{
    if (!s_paused.pixels) return;
    int px = (320 - s_paused.w) / 2;
    int py = (256 - s_paused.h) / 2;
    video_blit(s_paused.pixels, s_paused.w, px, py, s_paused.w, s_paused.h, 0);
}

void hud_render_map_overview(void)
{
    /* Draw a mini-map: one pixel per tile */
    int ox = 8, oy = 20;
    for (int row = 0; row < MAP_ROWS && oy + row < 256; row++) {
        for (int col = 0; col < MAP_COLS && ox + col < 320; col++) {
            UBYTE attr = tilemap_attr(&g_cur_map, col, row);
            UBYTE color;
            switch (attr) {
                case TILE_WALL:      color = 2;  break;
                case TILE_DOOR:      color = 6;  break;
                case TILE_EXIT:      color = 10; break;
                case TILE_ALIEN_SPAWN_BIG:
                case TILE_ALIEN_SPAWN_SMALL:
                case TILE_ALIEN_HOLE: color = 4; break;
                default:             color = 1;  break;
            }
            video_plot_pixel(ox + col, oy + row, color);
        }
    }
    /* Draw player positions */
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!g_players[i].alive) continue;
        int mx = ox + g_players[i].pos_x / MAP_TILE_W;
        int my = oy + g_players[i].pos_y / MAP_TILE_H;
        video_plot_pixel(mx, my, 31);
    }
}
