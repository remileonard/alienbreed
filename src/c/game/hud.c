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
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    int w, h;
    if (fread(&w, 4, 1, f) != 1 || fread(&h, 4, 1, f) != 1) { fclose(f); return -1; }
    img->w = w; img->h = h;
    img->pixels = (UBYTE *)malloc((size_t)(w * h));
    if (!img->pixels) { fclose(f); return -1; }
    fread(img->pixels, 1, (size_t)(w * h), f);
    fclose(f);
    return 0;
}

int hud_init(void)
{
    load_gfx(&s_p1_bar, "assets/gfx/player_1_status_304x8.raw");
    load_gfx(&s_p2_bar, "assets/gfx/player_2_status_304x8.raw");
    load_gfx(&s_paused, "assets/gfx/game_paused_96x7.raw");
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

/* Draw a 2-digit decimal number using timer digit sprites */
static void draw_two_digits(int hi, int lo, int x, int y)
{
    sprite_draw_digit(hi, x,     y);
    sprite_draw_digit(lo, x + 9, y);
}

/* Draw a coloured health/ammo bar using filled rectangles */
static void draw_bar(int x, int y, int w, int val, int max_val, UBYTE color)
{
    int filled = (max_val > 0) ? (val * w / max_val) : 0;
    video_fill_rect(x,          y, filled,     4, color);
    video_fill_rect(x + filled, y, w - filled, 4, 1);  /* dark = color index 1 */
}

void hud_render(void)
{
    /* ---- Destruction timer (top of screen, centred) ---- */
    int mins, sh, sl;
    level_get_timer_digits(&mins, &sh, &sl);

    int tx = 148;  /* centred in 320px */
    int ty = 2;
    sprite_draw_digit(mins, tx,      ty);
    /* colon - just a pixel dot */
    video_plot_pixel(tx + 9,  ty + 2, 3);
    video_plot_pixel(tx + 9,  ty + 5, 3);
    draw_two_digits(sh, sl, tx + 11, ty);

    /* ---- Player 1 status bar (bottom row) ---- */
    if (s_p1_bar.pixels) {
        video_blit(s_p1_bar.pixels, s_p1_bar.w,
                   8, 248, s_p1_bar.w, s_p1_bar.h, 0);
    }

    Player *p1 = &g_players[0];
    /* Health bar */
    draw_bar(16, 249, 32, p1->health, PLAYER_MAX_HEALTH, 10);
    /* Lives */
    sprite_draw_digit(p1->lives > 9 ? 9 : p1->lives, 56, 248);
    /* Ammo */
    draw_bar(70, 249, 20, p1->ammunitions, PLAYER_MAX_AMMO, 12);
    /* Weapon number */
    sprite_draw_digit(p1->cur_weapon, 100, 248);

    /* ---- Player 2 status bar ---- */
    if (g_number_players > 1 && s_p2_bar.pixels) {
        Player *p2 = &g_players[1];
        video_blit(s_p2_bar.pixels, s_p2_bar.w,
                   8, 240, s_p2_bar.w, s_p2_bar.h, 0);
        draw_bar(16, 241, 32, p2->health, PLAYER_MAX_HEALTH, 10);
        sprite_draw_digit(p2->lives > 9 ? 9 : p2->lives, 56, 240);
    }
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
