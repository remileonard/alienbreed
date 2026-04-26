/*
 * Alien Breed SE 92 - C port
 * Debug overlay implementation
 */

#include "debug.h"
#include "player.h"
#include "alien.h"
#include "constants.h"
#include "../engine/tilemap.h"
#include "../engine/alien_gfx.h"
#include "../engine/sprite.h"
#include "../hal/video.h"
#include "../hal/input.h"
#include "../hal/timer.h"
#include <stdio.h>
#include <string.h>

extern int g_camera_x, g_camera_y;

int g_debug_overlay_on = 0;

/* ------------------------------------------------------------------ */
/* Minimal 5×7 bitmap font for the info bar                           */
/* Each entry: 7 bytes, each byte = one row, bits 4-0 = columns      */
/* left-to-right (bit 4 = leftmost pixel).                            */
/* ------------------------------------------------------------------ */
static const Uint8 k_font5x7[128][7] = {
    [' '] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['-'] = {0x00,0x00,0x00,0x0E,0x00,0x00,0x00},
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
    ['B'] = {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    ['C'] = {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
    ['D'] = {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C},
    ['E'] = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
    ['F'] = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    ['G'] = {0x0E,0x10,0x10,0x13,0x11,0x11,0x0F},
    ['H'] = {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    ['I'] = {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
    ['K'] = {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
    ['L'] = {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    ['M'] = {0x11,0x1B,0x15,0x11,0x11,0x11,0x11},
    ['N'] = {0x11,0x19,0x15,0x13,0x11,0x11,0x11},
    ['O'] = {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
    ['P'] = {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    ['R'] = {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    ['S'] = {0x0E,0x10,0x10,0x0E,0x01,0x01,0x0E},
    ['T'] = {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    ['V'] = {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
    ['W'] = {0x11,0x11,0x11,0x15,0x15,0x1B,0x11},
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

#define COLOR_SPAWN_R  255
#define COLOR_SPAWN_G  140
#define COLOR_SPAWN_B    0  /* orange — alien spawn tiles 0x28/0x29 */

#define COLOR_OTHER_R  255
#define COLOR_OTHER_G  255
#define COLOR_OTHER_B    0  /* yellow */

/* Returns 1 if attr is a spawn tile (0x28 ALIEN_SPAWN_BIG / 0x29 ALIEN_SPAWN_SMALL). */
static int is_spawn(UBYTE a)
{
    return (a == TILE_ALIEN_SPAWN_BIG || a == TILE_ALIEN_SPAWN_SMALL);
}

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
/* Bounding-box colors                                                */
/* ------------------------------------------------------------------ */
#define COLOR_ALIEN_BBOX_R  255
#define COLOR_ALIEN_BBOX_G    0
#define COLOR_ALIEN_BBOX_B    0   /* red   — alien 32×32 collision box */

#define COLOR_PLAYER_BBOX_R   0
#define COLOR_PLAYER_BBOX_G  255
#define COLOR_PLAYER_BBOX_B  255  /* cyan  — player 16×16 hit box (+8,+8) */

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
            } else if (is_spawn(attr)) {
                r = COLOR_SPAWN_R; g = COLOR_SPAWN_G; b = COLOR_SPAWN_B;
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

    /* ---- Alien collision bounding boxes (32×32 — red) ---- */
    for (int i = 0; i < g_alien_count; i++) {
        if (g_aliens[i].alive == 0) continue;   /* skip fully dead slots */
        /* pos_x/pos_y is the centre of the 32×32 bbox; draw centred. */
        int sx = (int)g_aliens[i].pos_x - g_camera_x - 16;
        int sy = (int)g_aliens[i].pos_y - g_camera_y - 16;
        video_overlay_rect_outline(sx, sy, 32, 32,
                                   COLOR_ALIEN_BBOX_R,
                                   COLOR_ALIEN_BBOX_G,
                                   COLOR_ALIEN_BBOX_B, 220);
    }

    /* ---- Player collision bounding boxes (16×16 at +8,+8 — cyan) ---- */
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!g_players[i].alive) continue;
        int sx = (int)g_players[i].pos_x + PLAYER_BBOX_OFFSET - g_camera_x;
        int sy = (int)g_players[i].pos_y + PLAYER_BBOX_OFFSET - g_camera_y;
        video_overlay_rect_outline(sx, sy, PLAYER_BBOX_SIZE, PLAYER_BBOX_SIZE,
                                   COLOR_PLAYER_BBOX_R,
                                   COLOR_PLAYER_BBOX_G,
                                   COLOR_PLAYER_BBOX_B, 220);
    }
}

/* ================================================================== */
/* Graphics Viewer — full-screen scrollable asset browser             */
/* ================================================================== */

/* Layout constants */
#define GFX_HEADER_H      10   /* sticky top bar height */
#define GFX_TILE_CELL_W   20   /* tile cell: 16px tile + 4px margin  */
#define GFX_TILE_CELL_H   26   /* 2px top + 16px tile + 1px gap + 7px label */
#define GFX_TILES_PER_ROW 16   /* 16 × 20 = 320 px wide */
#define GFX_BOB_CELL_W    36   /* bob cell: 32px sprite + 4px margin */
#define GFX_BOB_CELL_H    42   /* 2px top + 30px sprite + 3px gap + 7px label */
#define GFX_BOBS_PER_ROW   8   /* 8 × 36 = 288 px (+ 32 px right margin) */
#define GFX_SPR_CELL_W    20   /* player sprite cell width */
#define GFX_SPR_CELL_H    42   /* 2px + up-to-32px sprite + 3px + 7px label */
#define GFX_SPRS_PER_ROW  16   /* 16 × 20 = 320 px wide */
#define GFX_SECTION_H     10   /* section header height */
#define GFX_SECTION_GAP    4   /* vertical gap between sections */

/* Compute virtual content Y positions given the current tileset size.
 * All positions are in "virtual canvas" coordinates; screen_y = vy - scroll_y + GFX_HEADER_H. */
typedef struct {
    int vy_tiles_hdr;
    int vy_tiles_content;
    int vy_walk_hdr;
    int vy_walk_content;
    int vy_death_hdr;
    int vy_death_content;
    int vy_spr_hdr;
    int vy_spr_content;
    int total_vh;
    int max_scroll;
} GfxLayout;

static GfxLayout gfx_compute_layout(void)
{
    GfxLayout L;
    int tc         = g_tileset.tile_count;
    int n_tile_rows = tc > 0 ? (tc + GFX_TILES_PER_ROW - 1) / GFX_TILES_PER_ROW : 0;
    int n_walk_rows = (ALIEN_DIR_COUNT * ALIEN_WALK_FRAMES + GFX_BOBS_PER_ROW - 1) / GFX_BOBS_PER_ROW;
    int n_death_rows= (ALIEN_DEATH_FRAMES + GFX_BOBS_PER_ROW - 1) / GFX_BOBS_PER_ROW;
    int n_spr_rows  = (PLAYER_SPRITE_TOTAL + GFX_SPRS_PER_ROW - 1) / GFX_SPRS_PER_ROW;

    L.vy_tiles_hdr     = 0;
    L.vy_tiles_content = L.vy_tiles_hdr + GFX_SECTION_H;
    int vy_tiles_end   = L.vy_tiles_content + n_tile_rows * GFX_TILE_CELL_H;

    L.vy_walk_hdr      = vy_tiles_end + GFX_SECTION_GAP;
    L.vy_walk_content  = L.vy_walk_hdr + GFX_SECTION_H;
    int vy_walk_end    = L.vy_walk_content + n_walk_rows * GFX_BOB_CELL_H;

    L.vy_death_hdr     = vy_walk_end + GFX_SECTION_GAP;
    L.vy_death_content = L.vy_death_hdr + GFX_SECTION_H;
    int vy_death_end   = L.vy_death_content + n_death_rows * GFX_BOB_CELL_H;

    L.vy_spr_hdr       = vy_death_end + GFX_SECTION_GAP;
    L.vy_spr_content   = L.vy_spr_hdr + GFX_SECTION_H;
    int vy_spr_end     = L.vy_spr_content + n_spr_rows * GFX_SPR_CELL_H;

    L.total_vh  = vy_spr_end;
    int viewport_h = 256 - GFX_HEADER_H;
    L.max_scroll = L.total_vh > viewport_h ? L.total_vh - viewport_h : 0;
    return L;
}

/* Convert virtual Y to screen Y */
#define VY_TO_SY(vy, scroll) ((vy) - (scroll) + GFX_HEADER_H)

/* Render one frame of the gfx viewer.  Returns max_scroll for the caller. */
static int gfx_viewer_render(int scroll_y)
{
    GfxLayout L = gfx_compute_layout();

    /* ---- Clear framebuffer ---- */
    video_clear();

    /* ---- Blit tiles into framebuffer ---- */
    if (g_tileset.pixels && g_tileset.tile_count > 0) {
        int tc = g_tileset.tile_count;
        for (int ti = 0; ti < tc; ti++) {
            int row = ti / GFX_TILES_PER_ROW;
            int col = ti % GFX_TILES_PER_ROW;
            int sx  = col * GFX_TILE_CELL_W + 2;
            int sy  = VY_TO_SY(L.vy_tiles_content + row * GFX_TILE_CELL_H, scroll_y) + 2;
            if (sy + GFX_TILE_CELL_H < GFX_HEADER_H || sy >= 256) continue;
            const UBYTE *src = g_tileset.pixels + (size_t)(ti * MAP_TILE_W * MAP_TILE_H);
            video_blit(src, MAP_TILE_W, sx, sy, MAP_TILE_W, MAP_TILE_H, -1);
        }
    }

    /* ---- Blit alien walk sprites ---- */
    const UBYTE *atlas = alien_gfx_get_atlas();
    if (atlas) {
        int total_walk = ALIEN_DIR_COUNT * ALIEN_WALK_FRAMES;
        for (int i = 0; i < total_walk; i++) {
            int dir   = i / ALIEN_WALK_FRAMES;
            int frame = i % ALIEN_WALK_FRAMES;
            int row   = i / GFX_BOBS_PER_ROW;
            int col   = i % GFX_BOBS_PER_ROW;
            int sx    = col * GFX_BOB_CELL_W + 2;
            int sy    = VY_TO_SY(L.vy_walk_content + row * GFX_BOB_CELL_H, scroll_y) + 2;
            if (sy + GFX_BOB_CELL_H < GFX_HEADER_H || sy >= 256) continue;
            int atlas_x = dir   * ALIEN_SPRITE_W;
            int atlas_y = frame * ALIEN_WALK_FRAME_STRIDE;
            const UBYTE *src = atlas + (size_t)(atlas_y * ALIEN_ATLAS_W + atlas_x);
            video_blit(src, ALIEN_ATLAS_W, sx, sy, ALIEN_SPRITE_W, ALIEN_SPRITE_H, 0);
        }

        /* ---- Blit alien death sprites ---- */
        for (int i = 0; i < ALIEN_DEATH_FRAMES; i++) {
            int row = i / GFX_BOBS_PER_ROW;
            int col = i % GFX_BOBS_PER_ROW;
            int sx  = col * GFX_BOB_CELL_W + 2;
            int sy  = VY_TO_SY(L.vy_death_content + row * GFX_BOB_CELL_H, scroll_y) + 2;
            if (sy + GFX_BOB_CELL_H < GFX_HEADER_H || sy >= 256) continue;
            int atlas_x, atlas_y;
            if (i < ALIEN_DEATH_ROW1_COUNT) {
                atlas_x = i * ALIEN_DEATH_W;
                atlas_y = ALIEN_DEATH_ROW1_Y;
            } else {
                atlas_x = (i - ALIEN_DEATH_ROW1_COUNT) * ALIEN_DEATH_W;
                atlas_y = ALIEN_DEATH_ROW2_Y;
            }
            const UBYTE *src = atlas + (size_t)(atlas_y * ALIEN_ATLAS_W + atlas_x);
            video_blit(src, ALIEN_ATLAS_W, sx, sy, ALIEN_DEATH_W, ALIEN_DEATH_H, 0);
        }
    }

    /* ---- Blit player sprites ---- */
    for (int i = 0; i < PLAYER_SPRITE_TOTAL; i++) {
        int row = i / GFX_SPRS_PER_ROW;
        int col = i % GFX_SPRS_PER_ROW;
        int sx  = col * GFX_SPR_CELL_W + 2;
        int sy  = VY_TO_SY(L.vy_spr_content + row * GFX_SPR_CELL_H, scroll_y) + 2;
        if (sy + GFX_SPR_CELL_H < GFX_HEADER_H || sy >= 256) continue;
        const UBYTE *pix;
        int pw, ph;
        if (sprite_get_player_raw(i, &pix, &pw, &ph) == 0) {
            /* Centre the sprite horizontally in the cell */
            int ox = sx + (GFX_TILE_CELL_W - 2 - pw) / 2;
            if (ox < sx) ox = sx;
            video_blit(pix, pw, ox, sy, pw, ph, 0);
        }
    }

    /* ---- Upload framebuffer before drawing overlays ---- */
    video_upload_framebuffer();

    /* ---- Sticky header bar (drawn last so it covers anything blitted there) ---- */
    video_overlay_fill_rect(0, 0, 320, GFX_HEADER_H, 0, 0, 0, 255);
    draw_string(2, 1,
        "GFX VIEWER   F-CLOSE   ARROWS-SCROLL",
        255, 220, 0);

    /* ---- Section headers ---- */
    {
        /* Helper macro: draw a section header at virtual vy */
#define DRAW_SECTION_HDR(vy_hdr, r, g, b, label) \
        do { \
            int _sy = VY_TO_SY((vy_hdr), scroll_y); \
            if (_sy >= GFX_HEADER_H && _sy < 256) { \
                video_overlay_fill_rect(0, _sy, 320, GFX_SECTION_H, (r), (g), (b), 230); \
                draw_string(4, _sy + 1, (label), 255, 255, 255); \
            } \
        } while (0)

        DRAW_SECTION_HDR(L.vy_tiles_hdr,  30,  30,  90, "TILES");
        DRAW_SECTION_HDR(L.vy_walk_hdr,   20,  80,  20, "ALIEN WALK");
        DRAW_SECTION_HDR(L.vy_death_hdr,  90,  20,  20, "ALIEN DEATH");
        DRAW_SECTION_HDR(L.vy_spr_hdr,    70,  20,  90, "PLAYER SPRITES");
#undef DRAW_SECTION_HDR
    }

    /* ---- Item labels ---- */

    /* Tile labels */
    if (g_tileset.tile_count > 0) {
        char buf[12];
        int tc = g_tileset.tile_count;
        for (int ti = 0; ti < tc; ti++) {
            int row = ti / GFX_TILES_PER_ROW;
            int col = ti % GFX_TILES_PER_ROW;
            int lx  = col * GFX_TILE_CELL_W + 2;
            int ly  = VY_TO_SY(L.vy_tiles_content + row * GFX_TILE_CELL_H, scroll_y)
                      + 2 + MAP_TILE_H + 1;           /* just below tile */
            if (ly < GFX_HEADER_H || ly >= 256) continue;
            snprintf(buf, sizeof(buf), "%d", ti);
            draw_string(lx, ly, buf, 180, 180, 180);
        }
    }

    /* Alien walk labels */
    if (atlas) {
        char buf[12];
        int total_walk = ALIEN_DIR_COUNT * ALIEN_WALK_FRAMES;
        for (int i = 0; i < total_walk; i++) {
            int dir   = i / ALIEN_WALK_FRAMES;
            int frame = i % ALIEN_WALK_FRAMES;
            int row   = i / GFX_BOBS_PER_ROW;
            int col   = i % GFX_BOBS_PER_ROW;
            int lx    = col * GFX_BOB_CELL_W + 2;
            int ly    = VY_TO_SY(L.vy_walk_content + row * GFX_BOB_CELL_H, scroll_y)
                        + 2 + ALIEN_SPRITE_H + 1;
            if (ly < GFX_HEADER_H || ly >= 256) continue;
            snprintf(buf, sizeof(buf), "D%dF%d", dir, frame);
            draw_string(lx, ly, buf, 180, 220, 180);
        }

        /* Alien death labels */
        for (int i = 0; i < ALIEN_DEATH_FRAMES; i++) {
            int row = i / GFX_BOBS_PER_ROW;
            int col = i % GFX_BOBS_PER_ROW;
            int lx  = col * GFX_BOB_CELL_W + 2;
            int ly  = VY_TO_SY(L.vy_death_content + row * GFX_BOB_CELL_H, scroll_y)
                      + 2 + ALIEN_DEATH_H + 1;
            if (ly < GFX_HEADER_H || ly >= 256) continue;
            char buf[8];
            snprintf(buf, sizeof(buf), "DT%d", i);
            draw_string(lx, ly, buf, 220, 180, 180);
        }
    }

    /* Player sprite labels */
    {
        char buf[8];
        for (int i = 0; i < PLAYER_SPRITE_TOTAL; i++) {
            int row = i / GFX_SPRS_PER_ROW;
            int col = i % GFX_SPRS_PER_ROW;
            int lx  = col * GFX_SPR_CELL_W + 2;
            int ly  = VY_TO_SY(L.vy_spr_content + row * GFX_SPR_CELL_H, scroll_y)
                      + 2 + 28 + 1;                  /* 28px reserved for sprite */
            if (ly < GFX_HEADER_H || ly >= 256) continue;
            snprintf(buf, sizeof(buf), "%d", i + 1); /* 1-based sprite number */
            draw_string(lx, ly, buf, 180, 180, 220);
        }
    }

    /* ---- Scrollbar indicator (right edge) ---- */
    if (L.max_scroll > 0) {
        int viewport_h = 256 - GFX_HEADER_H;
        int bar_h = viewport_h * viewport_h / L.total_vh;
        if (bar_h < 4) bar_h = 4;
        int bar_y = GFX_HEADER_H + scroll_y * (viewport_h - bar_h) / L.max_scroll;
        video_overlay_fill_rect(317, bar_y, 3, bar_h, 160, 160, 160, 200);
    }

    return L.max_scroll;
}

void debug_gfx_viewer_run(void)
{
    /* Drain the KEY_F event that triggered entry so the viewer loop
     * starts with a clean key state. */
    input_poll();

    int scroll_y  = 0;
    int max_scroll = 0;

    while (!g_quit_requested) {
        timer_begin_frame();
        input_poll();

        /* Exit on F or ESC */
        if (g_key_pressed == KEY_F || g_key_pressed == KEY_ESC)
            break;

        /* Smooth scroll via held arrow keys (SDL keyboard state) */
        const Uint8 *ks = SDL_GetKeyboardState(NULL);
        if (ks[SDL_SCANCODE_UP])    scroll_y -= 3;
        if (ks[SDL_SCANCODE_DOWN])  scroll_y += 3;
        if (ks[SDL_SCANCODE_LEFT])  scroll_y -= 48;  /* fast page-up   */
        if (ks[SDL_SCANCODE_RIGHT]) scroll_y += 48;  /* fast page-down */

        if (scroll_y < 0)           scroll_y = 0;
        if (scroll_y > max_scroll)  scroll_y = max_scroll;

        max_scroll = gfx_viewer_render(scroll_y);
        video_flip();
    }
}
