/*
 * Alien Breed SE 92 - C port
 * Debug overlay implementation
 */

#include "debug.h"
#include "player.h"
#include "constants.h"
#include "../engine/tilemap.h"
#include "../hal/video.h"
#include <stdio.h>
#include <string.h>

int g_debug_overlay_on = 0;

/* ------------------------------------------------------------------ */
/* Minimal 5×7 bitmap font for the info bar                           */
/* Each entry: 7 bytes, each byte = one row, bits 4-0 = columns      */
/* left-to-right (bit 4 = leftmost pixel).                            */
/* ------------------------------------------------------------------ */
static const Uint8 k_font5x7[128][7] = {
    [' '] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    [':'] = {0x00,0x04,0x04,0x00,0x04,0x04,0x00},
    ['/'] = {0x01,0x01,0x02,0x04,0x08,0x10,0x10},
    ['0'] = {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
    ['1'] = {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    ['2'] = {0x0E,0x11,0x01,0x06,0x08,0x10,0x1F},
    ['3'] = {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E},
    ['4'] = {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
    ['5'] = {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
    ['6'] = {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},
    ['7'] = {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    ['8'] = {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
    ['9'] = {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
    ['A'] = {0x04,0x0A,0x11,0x11,0x1F,0x11,0x11},
    ['C'] = {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
    ['H'] = {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    ['K'] = {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
    ['L'] = {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    ['M'] = {0x11,0x1B,0x15,0x11,0x11,0x11,0x11},
    ['P'] = {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    ['R'] = {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    ['V'] = {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
    ['X'] = {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    ['Y'] = {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},
};

/* Draw one character of the 5×7 font using SDL overlay points.      */
/* Returns the x advance (6 pixels: 5 glyph + 1 gap).               */
static int draw_char(int x, int y, unsigned char c,
                     Uint8 r, Uint8 g, Uint8 b)
{
    if (c >= 128) return 6;
    const Uint8 *rows = k_font5x7[(int)c];
    for (int row = 0; row < 7; row++) {
        Uint8 bits = rows[row];
        for (int col = 0; col < 5; col++) {
            if (bits & (0x10 >> col))
                video_overlay_draw_point(x + col, y + row, r, g, b, 255);
        }
    }
    return 6;
}

/* Draw a null-terminated ASCII string using the font.               */
static void draw_string(int x, int y, const char *s,
                        Uint8 r, Uint8 g, Uint8 b)
{
    for (; *s; s++)
        x += draw_char(x, y, (unsigned char)*s, r, g, b);
}

/* ------------------------------------------------------------------ */
/* Tile border colors                                                 */
/* ------------------------------------------------------------------ */
#define COLOR_WALL_R   255
#define COLOR_WALL_G   128
#define COLOR_WALL_B   255  /* pink */

#define COLOR_ITEM_R     0
#define COLOR_ITEM_G   255
#define COLOR_ITEM_B     0  /* green */

#define COLOR_DOOR_R     0
#define COLOR_DOOR_G   255
#define COLOR_DOOR_B   255  /* cyan */

#define COLOR_OTHER_R  255
#define COLOR_OTHER_G  255
#define COLOR_OTHER_B    0  /* yellow */

/* Returns 1 if attr is a wall tile.                                 */
static int is_wall(UBYTE a)
{
    return (a == TILE_WALL || a == 0x1D || (a >= 0x2A && a <= 0x2D));
}

/* Returns 1 if attr is a collectible tile (key/credits/health/ammo).*/
static int is_item(UBYTE a)
{
    return (a == TILE_KEY        ||
            a == TILE_FIRST_AID  ||
            a == TILE_AMMO       ||
            a == TILE_CREDITS_100||
            a == TILE_CREDITS_1000);
}

/* Returns 1 if attr is a door tile.                                 */
static int is_door(UBYTE a)
{
    return (a == TILE_DOOR        ||
            a == TILE_FIRE_DOOR_A ||
            a == TILE_FIRE_DOOR_B);
}

/* ------------------------------------------------------------------ */
/* Main overlay renderer                                              */
/* ------------------------------------------------------------------ */
void debug_render_overlay(void)
{
    Player *p1 = &g_players[0];

    /* ---- Tile borders ---- */
    int tile_x0 = g_camera_x / MAP_TILE_W;
    int tile_y0 = g_camera_y / MAP_TILE_H;
    int tile_x1 = (g_camera_x + SCREEN_W) / MAP_TILE_W + 1;
    int tile_y1 = (g_camera_y + SCREEN_H) / MAP_TILE_H + 1;
    if (tile_x1 >= MAP_COLS) tile_x1 = MAP_COLS - 1;
    if (tile_y1 >= MAP_ROWS) tile_y1 = MAP_ROWS - 1;

    for (int ty = tile_y0; ty <= tile_y1; ty++) {
        for (int tx = tile_x0; tx <= tile_x1; tx++) {
            UBYTE attr = tilemap_attr(&g_cur_map, tx, ty);
            /* Skip plain floor — no border to keep the display readable */
            if (attr == TILE_FLOOR) continue;

            int sx = tx * MAP_TILE_W - g_camera_x;
            int sy = ty * MAP_TILE_H - g_camera_y;

            Uint8 r, g, b;
            if (is_wall(attr)) {
                r = COLOR_WALL_R;  g = COLOR_WALL_G;  b = COLOR_WALL_B;
            } else if (is_item(attr)) {
                r = COLOR_ITEM_R;  g = COLOR_ITEM_G;  b = COLOR_ITEM_B;
            } else if (is_door(attr)) {
                r = COLOR_DOOR_R;  g = COLOR_DOOR_G;  b = COLOR_DOOR_B;
            } else {
                r = COLOR_OTHER_R; g = COLOR_OTHER_G; b = COLOR_OTHER_B;
            }

            video_overlay_rect_outline(sx, sy, MAP_TILE_W, MAP_TILE_H,
                                       r, g, b, 220);
        }
    }

    /* ---- Info bar background (semi-transparent dark strip) ---- */
    video_overlay_fill_rect(0, 0, SCREEN_W, 9, 0, 0, 0, 180);

    /* ---- Info bar text ---- */
    char buf[128];
    int tile_px = p1->pos_x / MAP_TILE_W;
    int tile_py = p1->pos_y / MAP_TILE_H;

    snprintf(buf, sizeof(buf),
             "X:%d Y:%d HP:%d CR:%ld K:%d AM:%d P:%d L:%d",
             tile_px,
             tile_py,
             (int)p1->health,
             (long)p1->credits,
             (int)p1->keys,
             (int)p1->ammunitions,
             (int)p1->ammopacks,
             (int)p1->lives);

    draw_string(2, 1, buf, 255, 255, 255);
}
