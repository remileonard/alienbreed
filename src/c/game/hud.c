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
#define BAR_HEALTH_OFF   24
#define BAR_HEALTH_W     64
#define BAR_LIVES_OFF   128
#define BAR_AMMO_PKS_OFF 184
#define BAR_AMMO_OFF    200
#define BAR_AMMO_W       32

/* Key dot start positions (from ASM lbL00FE8C = {$107,$10A,$10E,$112,$116,$11A}) */
static const int k_key_off[6] = { 263, 266, 270, 274, 278, 282 };

/* Draw the dynamic overlay for one player's status bar.
 * bar_y : screen Y of the bar's top row (BAR_P1_Y or BAR_P2_Y).
 * bg    : static 304×8 background image (may be NULL). */
static void draw_player_bar(const Player *p, int bar_y, const GfxImage *bg)
{
    int sx = BAR_X;  /* screen x of bar start */

    /* 1. Static background image */
    if (bg && bg->pixels)
        video_blit(bg->pixels, bg->w, sx, bar_y, bg->w, bg->h, 0);

    /* 2. Health bar (pixels 24..87 in bar, 64 px wide at max health) */
    int health_w = (p->health * BAR_HEALTH_W) / PLAYER_MAX_HEALTH;
    if (health_w < 0) health_w = 0;
    if (health_w > BAR_HEALTH_W) health_w = BAR_HEALTH_W;
    /* Filled portion (orange) */
    if (health_w > 0)
        video_fill_rect(sx + BAR_HEALTH_OFF, bar_y, health_w, BAR_H, 10);
    /* Empty portion (dark) */
    if (health_w < BAR_HEALTH_W)
        video_fill_rect(sx + BAR_HEALTH_OFF + health_w, bar_y,
                        BAR_HEALTH_W - health_w, BAR_H, 1);

    /* 3. Lives indicator: up to 4 dots at offsets 128,132,136,140 (2 px each) */
    for (int i = 0; i < 4; i++) {
        UBYTE c = (i < p->lives) ? 10 : 1;
        video_fill_rect(sx + BAR_LIVES_OFF + i * 4, bar_y, 2, BAR_H, c);
    }

    /* 4. Ammo packs: up to 4 dots at offsets 184,187,190,193 (2 px each) */
    for (int i = 0; i < 4; i++) {
        UBYTE c = (i < p->ammopacks) ? 12 : 1;
        video_fill_rect(sx + BAR_AMMO_PKS_OFF + i * 3, bar_y, 2, BAR_H, c);
    }

    /* 5. Ammo bar (pixels 200..231, 32 px wide at max ammo) */
    int ammo_w = (p->ammunitions * BAR_AMMO_W) / PLAYER_MAX_AMMO;
    if (ammo_w < 0) ammo_w = 0;
    if (ammo_w > BAR_AMMO_W) ammo_w = BAR_AMMO_W;
    if (ammo_w > 0)
        video_fill_rect(sx + BAR_AMMO_OFF, bar_y, ammo_w, BAR_H, 12);
    if (ammo_w < BAR_AMMO_W)
        video_fill_rect(sx + BAR_AMMO_OFF + ammo_w, bar_y,
                        BAR_AMMO_W - ammo_w, BAR_H, 1);

    /* 6. Keys: up to 6 dots, positions from ASM lbL00FE8C.
     *    End position = lbL00FE8C[6] = $11F = 287. */
    static const int k_key_end = 287;
    for (int i = 0; i < 6; i++) {
        if (i < p->keys) {
            int next = (i < 5) ? k_key_off[i + 1] : k_key_end;
            video_fill_rect(sx + k_key_off[i], bar_y, next - k_key_off[i], BAR_H, 10);
        }
    }
}

/* Draw a 2-digit decimal number using timer digit sprites */
static void draw_two_digits(int hi, int lo, int x, int y)
{
    sprite_draw_digit(hi, x,     y);
    sprite_draw_digit(lo, x + 9, y);
}

void hud_render(void)
{
    /* ---- Player 1 status bar: top of screen (y=0) ---- */
    draw_player_bar(&g_players[0], BAR_P1_Y, &s_p1_bar);

    /* ---- Player 2 status bar: bottom of screen (y=248) ---- */
    draw_player_bar(&g_players[1], BAR_P2_Y, &s_p2_bar);

    /* ---- Destruction timer: centred just inside the top bar ---- */
    int mins, sh, sl;
    level_get_timer_digits(&mins, &sh, &sl);

    int tx = 148;  /* centred in 320px */
    int ty = 0;
    sprite_draw_digit(mins, tx,      ty);
    /* colon dots */
    video_plot_pixel(tx + 9,  ty + 2, 3);
    video_plot_pixel(tx + 9,  ty + 5, 3);
    draw_two_digits(sh, sl, tx + 11, ty);
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
