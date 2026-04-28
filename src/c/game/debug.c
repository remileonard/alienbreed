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
#include "../engine/anim_gfx.h"
#include "../engine/sprite.h"
#include "../hal/video.h"
#include "../hal/input.h"
#include "../hal/timer.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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

/* Decoded INTEX background (320×256).
 * Loaded from assets/gfx/intex_bkgnd_320x256.raw when the viewer opens. */
static UBYTE *s_intex_bg = NULL;
static int    s_intex_bg_w = 0;
static int    s_intex_bg_h = 0;

/* HUD images already held by hud.c are not accessible directly; we keep
 * our own lightweight copies for the viewer. */
typedef struct { UBYTE *pixels; int w, h; } DbgImg;

static DbgImg s_dbg_p1_bar  = {NULL,0,0};
static DbgImg s_dbg_p2_bar  = {NULL,0,0};
static DbgImg s_dbg_paused  = {NULL,0,0};

/* INTEX weapon atlas (320×264, 4-bitplane sequential).
 * Loaded from assets/gfx/intex_weapons_320x264.raw when the viewer opens.
 * Six weapon images in a 2-column × 3-row grid, each 160×88 px.
 * Ref: intex.asm weapons_pic_table / disp_weapon_pic. */
#define DBG_WEAPON_ATLAS_W  320
#define DBG_WEAPON_ATLAS_H  264
static DbgImg s_dbg_weapon_atlas = {NULL,0,0};

/* Font strip: assets/fonts/font_16x504.raw
 * Filename encodes letter_w=16 and nominal_h=504; actual strip = 672×12 px (42 glyphs × 16px each).
 * Loaded when the viewer opens. */
static DbgImg s_dbg_font = {NULL,0,0};

/* ------------------------------------------------------------------
 * Generic helpers
 * ------------------------------------------------------------------ */

/* Load a .raw image file (4-byte w, 4-byte h, then w*h indexed bytes). */
static int dbg_img_load(DbgImg *img, const char *path)
{
    if (img->pixels) { free(img->pixels); img->pixels = NULL; }
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fread(&img->w, 4, 1, f) != 1 || fread(&img->h, 4, 1, f) != 1) {
        fclose(f); return -1;
    }
    img->pixels = (UBYTE *)malloc((size_t)img->w * (size_t)img->h);
    if (!img->pixels) { fclose(f); return -1; }
    size_t expected = (size_t)img->w * (size_t)img->h;
    if (fread(img->pixels, 1, expected, f) != expected) {
        free(img->pixels); img->pixels = NULL;
        fclose(f); return -1;
    }
    fclose(f);
    return 0;
}

static void dbg_img_free(DbgImg *img)
{
    free(img->pixels);
    img->pixels = NULL;
    img->w = img->h = 0;
}

/* Load all viewer-specific assets that are not already resident. */
static void gfx_viewer_load_assets(void)
{
    /* INTEX background */
    if (!s_intex_bg) {
        DbgImg tmp = {NULL,0,0};
        if (dbg_img_load(&tmp, "assets/gfx/intex_bkgnd_320x256.raw") == 0) {
            s_intex_bg   = tmp.pixels;
            s_intex_bg_w = tmp.w;
            s_intex_bg_h = tmp.h;
        } else {
            free(tmp.pixels);
        }
    }
    /* INTEX weapon atlas */
    if (!s_dbg_weapon_atlas.pixels)
        dbg_img_load(&s_dbg_weapon_atlas, "assets/gfx/intex_weapons_320x264.raw");
    /* HUD graphics */
    if (!s_dbg_p1_bar.pixels)
        dbg_img_load(&s_dbg_p1_bar, "assets/gfx/main_player_1_status_304x8.raw");
    if (!s_dbg_p2_bar.pixels)
        dbg_img_load(&s_dbg_p2_bar, "assets/gfx/main_player_2_status_304x8.raw");
    if (!s_dbg_paused.pixels)
        dbg_img_load(&s_dbg_paused, "assets/gfx/main_game_paused_96x7.raw");
    /* Font strip */
    if (!s_dbg_font.pixels)
        dbg_img_load(&s_dbg_font, "assets/fonts/font_16x504.raw");
}

static void gfx_viewer_free_assets(void)
{
    free(s_intex_bg);     s_intex_bg = NULL;
    s_intex_bg_w = s_intex_bg_h = 0;
    dbg_img_free(&s_dbg_weapon_atlas);
    dbg_img_free(&s_dbg_p1_bar);
    dbg_img_free(&s_dbg_p2_bar);
    dbg_img_free(&s_dbg_paused);
    dbg_img_free(&s_dbg_font);
}

/* ------------------------------------------------------------------
 * Layout structure
 * ------------------------------------------------------------------ */
typedef struct {
    /* All positions are virtual Y (canvas coords). */
    int vy_tiles_hdr,      vy_tiles_content;
    int vy_anim_hdr,       vy_anim_content;
    int vy_bo_hdr,         vy_bo_content;      /* Full BO atlas (320×384) */
    int vy_weapons_hdr,    vy_weapons_content; /* INTEX weapon atlas (320×264) */
    int vy_intex_hdr,      vy_intex_content;
    int vy_hud_hdr,        vy_hud_content;
    int vy_font_hdr,       vy_font_content;
    int vy_spr_hdr,        vy_spr_content;
    int total_vh;
    int max_scroll;
} GfxLayout;

/* Anim tile cell (same pixel size as regular tiles) */
#define GFX_ANIM_TILE_W  ANIM_TILE_W
#define GFX_ANIM_TILE_H  ANIM_TILE_H
/* Cell with label — identical to regular tile cell */
#define GFX_ANIM_CELL_W  GFX_TILE_CELL_W
#define GFX_ANIM_CELL_H  GFX_TILE_CELL_H
#define GFX_ANIM_PER_ROW GFX_TILES_PER_ROW

static GfxLayout gfx_compute_layout(void)
{
    GfxLayout L;

    /* Tileset */
    int tc           = g_tileset.tile_count;
    int n_tile_rows  = tc > 0 ? (tc + GFX_TILES_PER_ROW - 1) / GFX_TILES_PER_ROW : 0;

    /* Animated tiles */
    int ac           = anim_gfx_get_atlas() ? ANIM_TILE_COUNT : 0;
    int n_anim_rows  = ac > 0 ? (ac + GFX_ANIM_PER_ROW - 1) / GFX_ANIM_PER_ROW : 0;

    /* Full BO atlas: 320×384 raw image, one full-width image.
     * Ref: lbW019A8E/lbW01945E/lbW0188CE in main.asm — the complete atlas contains
     * alien walk/alt frames (y=0-159), bullet/projectile sprites (x=256+, y=0-155),
     * death/explosion frames (y=160-255), and large weapon-effect sprites (y=256-383). */
    int bo_h         = alien_gfx_get_atlas() ? (ALIEN_ATLAS_H + 9) : 0;

    /* INTEX weapon atlas: 320×264 raw image (6 weapon images 2-col×3-row, each 160×88).
     * Ref: intex.asm weapons_pic / weapons_pic_table. */
    int weapons_h    = s_dbg_weapon_atlas.pixels ? (DBG_WEAPON_ATLAS_H + 9) : 0;

    /* INTEX bg: one full-width image + label */
    int intex_h      = s_intex_bg ? (s_intex_bg_h + 9) : 0;

    /* HUD: three images stacked, each with a 2px gap + 7px label */
    int hud_h        = 0;
    if (s_dbg_p1_bar.pixels)  hud_h += s_dbg_p1_bar.h  + 9;
    if (s_dbg_p2_bar.pixels)  hud_h += s_dbg_p2_bar.h  + 9;
    if (s_dbg_paused.pixels)  hud_h += s_dbg_paused.h  + 9;

    /* Font */
    int font_h       = s_dbg_font.pixels ? (s_dbg_font.h + 9) : 0;

    /* Player sprites */
    int n_spr_rows   = (PLAYER_SPRITE_TOTAL + GFX_SPRS_PER_ROW - 1) / GFX_SPRS_PER_ROW;

    /* Build virtual layout */
    int vy = 0;

#define NEXT_SECTION(hdr_f, content_f, content_h) \
    (hdr_f) = vy;  vy += GFX_SECTION_H; \
    (content_f) = vy; vy += (content_h) + GFX_SECTION_GAP;

    NEXT_SECTION(L.vy_tiles_hdr,   L.vy_tiles_content,   n_tile_rows  * GFX_TILE_CELL_H)
    NEXT_SECTION(L.vy_anim_hdr,    L.vy_anim_content,    n_anim_rows  * GFX_ANIM_CELL_H)
    NEXT_SECTION(L.vy_bo_hdr,      L.vy_bo_content,      bo_h)
    NEXT_SECTION(L.vy_weapons_hdr, L.vy_weapons_content, weapons_h)
    NEXT_SECTION(L.vy_intex_hdr,   L.vy_intex_content,   intex_h)
    NEXT_SECTION(L.vy_hud_hdr,     L.vy_hud_content,     hud_h)
    NEXT_SECTION(L.vy_font_hdr,    L.vy_font_content,    font_h)
    NEXT_SECTION(L.vy_spr_hdr,     L.vy_spr_content,     n_spr_rows   * GFX_SPR_CELL_H)

#undef NEXT_SECTION

    L.total_vh  = vy;
    int viewport_h = 256 - GFX_HEADER_H;
    L.max_scroll   = L.total_vh > viewport_h ? L.total_vh - viewport_h : 0;
    return L;
}

/* Convert virtual Y to screen Y */
#define VY_TO_SY(vy, scroll) ((vy) - (scroll) + GFX_HEADER_H)

/* ------------------------------------------------------------------
 * Render one frame of the GFX viewer.  Returns max_scroll.
 * ------------------------------------------------------------------ */
static int gfx_viewer_render(int scroll_y)
{
    GfxLayout L = gfx_compute_layout();

    /* ---- Clear framebuffer ---- */
    video_clear();

    /* ================================================================
     * FRAMEBUFFER BLITTING PASS
     * ================================================================ */

    /* ---- TILESET ---- */
    if (g_tileset.pixels && g_tileset.tile_count > 0) {
        int tc = g_tileset.tile_count;
        for (int ti = 0; ti < tc; ti++) {
            int row = ti / GFX_TILES_PER_ROW;
            int col = ti % GFX_TILES_PER_ROW;
            int sx  = col * GFX_TILE_CELL_W + 2;
            int sy  = VY_TO_SY(L.vy_tiles_content + row * GFX_TILE_CELL_H, scroll_y) + 2;
            if (sy + GFX_TILE_CELL_H < GFX_HEADER_H || sy >= 256) continue;
            const UBYTE *src = g_tileset.pixels + (size_t)ti * (MAP_TILE_W * MAP_TILE_H);
            video_blit(src, MAP_TILE_W, sx, sy, MAP_TILE_W, MAP_TILE_H, -1);
        }
    }

    /* ---- ANIMATED TILES ---- */
    {
        const UBYTE *aa = anim_gfx_get_atlas();
        if (aa) {
            for (int ti = 0; ti < ANIM_TILE_COUNT; ti++) {
                int row = ti / GFX_ANIM_PER_ROW;
                int col = ti % GFX_ANIM_PER_ROW;
                int sx  = col * GFX_ANIM_CELL_W + 2;
                int sy  = VY_TO_SY(L.vy_anim_content + row * GFX_ANIM_CELL_H, scroll_y) + 2;
                if (sy + GFX_ANIM_CELL_H < GFX_HEADER_H || sy >= 256) continue;
                int atlas_col = ti % ANIM_TILES_PER_ROW;
                int atlas_row = ti / ANIM_TILES_PER_ROW;
                int ax = atlas_col * ANIM_TILE_W;
                int ay = atlas_row * ANIM_TILE_H;
                const UBYTE *src = aa + ((size_t)ay * ANIM_ATLAS_W + (size_t)ax);
                video_blit(src, ANIM_ATLAS_W, sx, sy, ANIM_TILE_W, ANIM_TILE_H, -1);
            }
        }
    }

    /* ---- FULL BO ATLAS (320×384) ---- */
    /* Displays the complete L?BO file atlas at 1:1 scale.
     * Layout from lbW019A8E/lbW01945E/lbW0188CE (main.asm):
     *   y=  0- 95  Alien walk cycle: 8 compass dirs × 3 frames, 32×30 px each
     *   y= 96-159  Secondary walk/alt alien sprites, 32×30 px
     *   x=256-319  Projectile & bullet sprites (16×16 and 16×14)
     *   y=160-191  Bullet/shot animations, 16×14 px
     *   y=192-253  Death/explosion: 16 frames of 32×30 px
     *   y=256-383  Large weapon-effect sprites (96×128) + misc (32×32) */
    {
        const UBYTE *alien = alien_gfx_get_atlas();
        if (alien) {
            int sy = VY_TO_SY(L.vy_bo_content, scroll_y) + 2;
            /* Blit the full atlas row by row — only visible rows */
            for (int row = 0; row < ALIEN_ATLAS_H; row++) {
                int dst_y = sy + row;
                if (dst_y < GFX_HEADER_H || dst_y >= 256) continue;
                const UBYTE *src = alien + ((size_t)row * ALIEN_ATLAS_W);
                video_blit(src, ALIEN_ATLAS_W, 0, dst_y, ALIEN_ATLAS_W, 1, -1);
            }
        }
    }

    /* ---- INTEX WEAPON ATLAS (320×264) ---- */
    /* Displays the raw weapon sprite file at 1:1 scale.
     * Layout from intex.asm weapons_pic_table (each image is 160×88 px):
     *   Row 0 (y=  0): TWINFIRE (x=0)   | FLAMEARC (x=160)
     *   Row 1 (y= 88): SIDEWINDERS (x=0)| FLAMETHROWER (x=160)
     *   Row 2 (y=176): MACHINEGUN (x=0) | PLASMAGUN (x=160) */
    if (s_dbg_weapon_atlas.pixels && s_dbg_weapon_atlas.w > 0) {
        int sy = VY_TO_SY(L.vy_weapons_content, scroll_y) + 2;
        for (int row = 0; row < DBG_WEAPON_ATLAS_H; row++) {
            int dst_y = sy + row;
            if (dst_y < GFX_HEADER_H || dst_y >= 256) continue;
            const UBYTE *src = s_dbg_weapon_atlas.pixels
                               + (size_t)row * (size_t)s_dbg_weapon_atlas.w;
            video_blit(src, s_dbg_weapon_atlas.w, 0, dst_y,
                       s_dbg_weapon_atlas.w, 1, -1);
        }
    }

    /* ---- INTEX BACKGROUND ---- */
    if (s_intex_bg && s_intex_bg_w > 0) {
        int sy = VY_TO_SY(L.vy_intex_content, scroll_y) + 2;
        if (sy + s_intex_bg_h >= GFX_HEADER_H && sy < 256)
            video_blit(s_intex_bg, s_intex_bg_w, 0, sy, s_intex_bg_w, s_intex_bg_h, -1);
    }

    /* ---- HUD GRAPHICS ---- */
    {
        int vy_cur = L.vy_hud_content;
        DbgImg *hud_imgs[] = { &s_dbg_p1_bar, &s_dbg_p2_bar, &s_dbg_paused };
        for (int i = 0; i < 3; i++) {
            DbgImg *img = hud_imgs[i];
            if (!img->pixels) continue;
            int sy = VY_TO_SY(vy_cur, scroll_y) + 2;
            if (sy + img->h >= GFX_HEADER_H && sy < 256)
                video_blit(img->pixels, img->w, 0, sy, img->w, img->h, -1);
            vy_cur += img->h + 9;
        }
    }

    /* ---- FONT STRIP ---- */
    if (s_dbg_font.pixels && s_dbg_font.w > 0) {
        int sy = VY_TO_SY(L.vy_font_content, scroll_y) + 2;
        /* Blit the font strip (width may exceed 320px — blit only visible part). */
        int dw = s_dbg_font.w < 320 ? s_dbg_font.w : 320;
        if (sy + s_dbg_font.h >= GFX_HEADER_H && sy < 256)
            video_blit(s_dbg_font.pixels, s_dbg_font.w, 0, sy, dw, s_dbg_font.h, -1);
    }

    /* ---- PLAYER SPRITES ---- */
    for (int i = 0; i < PLAYER_SPRITE_TOTAL; i++) {
        int row = i / GFX_SPRS_PER_ROW;
        int col = i % GFX_SPRS_PER_ROW;
        int sx  = col * GFX_SPR_CELL_W + 2;
        int sy  = VY_TO_SY(L.vy_spr_content + row * GFX_SPR_CELL_H, scroll_y) + 2;
        if (sy + GFX_SPR_CELL_H < GFX_HEADER_H || sy >= 256) continue;
        const UBYTE *pix;
        int pw, ph;
        if (sprite_get_player_raw(i, &pix, &pw, &ph) == 0) {
            int ox = sx + (GFX_SPR_CELL_W - 2 - pw) / 2;
            if (ox < sx) ox = sx;
            video_blit(pix, pw, ox, sy, pw, ph, 0);
        }
    }

    /* ---- Upload framebuffer before drawing overlays ---- */
    video_upload_framebuffer();

    /* ================================================================
     * OVERLAY / LABEL PASS (SDL renderer — not affected by palette)
     * ================================================================ */

    /* Sticky header bar */
    video_overlay_fill_rect(0, 0, 320, GFX_HEADER_H, 0, 0, 0, 255);
    draw_string(2, 1, "GFX VIEWER   F-CLOSE   ARROWS-SCROLL", 255, 220, 0);

/* Helper macro: draw a section header */
#define DRAW_HDR(vy_hdr, r, g, b, label) \
    do { \
        int _sy = VY_TO_SY((vy_hdr), scroll_y); \
        if (_sy >= GFX_HEADER_H && _sy < 256) { \
            video_overlay_fill_rect(0, _sy, 320, GFX_SECTION_H, (r), (g), (b), 230); \
            draw_string(4, _sy + 1, (label), 255, 255, 255); \
        } \
    } while (0)

    DRAW_HDR(L.vy_tiles_hdr,    30,  30,  90, "TILES");
    DRAW_HDR(L.vy_anim_hdr,     30,  70,  90, "ANIMATED TILES (L?AN)");
    DRAW_HDR(L.vy_bo_hdr,       20,  80,  20, "BO ATLAS FULL (L?BO) - 320x384 - ALL BOBs");
    DRAW_HDR(L.vy_weapons_hdr,  80,  60,  20, "INTEX WEAPONS ATLAS (weapons_264x40.lo4) - 320x264");
    DRAW_HDR(L.vy_intex_hdr,    30,  70,  40, "INTEX BACKGROUND");
    DRAW_HDR(L.vy_hud_hdr,      50,  50,  50, "HUD GRAPHICS");
    DRAW_HDR(L.vy_font_hdr,     50,  30,  70, "FONT");
    DRAW_HDR(L.vy_spr_hdr,      70,  20,  90, "PLAYER SPRITES");

#undef DRAW_HDR

    /* ---- Tileset labels ---- */
    if (g_tileset.tile_count > 0) {
        char buf[12];
        int tc = g_tileset.tile_count;
        for (int ti = 0; ti < tc; ti++) {
            int row = ti / GFX_TILES_PER_ROW;
            int col = ti % GFX_TILES_PER_ROW;
            int lx  = col * GFX_TILE_CELL_W + 2;
            int ly  = VY_TO_SY(L.vy_tiles_content + row * GFX_TILE_CELL_H, scroll_y)
                      + 2 + MAP_TILE_H + 1;
            if (ly < GFX_HEADER_H || ly >= 256) continue;
            snprintf(buf, sizeof(buf), "%d", ti);
            draw_string(lx, ly, buf, 180, 180, 180);
        }
    }

    /* ---- Animated tile labels ---- */
    if (anim_gfx_get_atlas()) {
        char buf[12];
        for (int ti = 0; ti < ANIM_TILE_COUNT; ti++) {
            int row = ti / GFX_ANIM_PER_ROW;
            int col = ti % GFX_ANIM_PER_ROW;
            int lx  = col * GFX_ANIM_CELL_W + 2;
            int ly  = VY_TO_SY(L.vy_anim_content + row * GFX_ANIM_CELL_H, scroll_y)
                      + 2 + GFX_ANIM_TILE_H + 1;
            if (ly < GFX_HEADER_H || ly >= 256) continue;
            snprintf(buf, sizeof(buf), "A%d", ti);
            draw_string(lx, ly, buf, 180, 220, 220);
        }
    }

    /* ---- BO atlas region annotations (overlaid on the atlas image) ----
     * Based on lbW019A8E (COMPACT) and lbW0188CE BOB descriptor tables.
     * Each annotation marks a horizontal band in the 320×384 atlas. */
    if (alien_gfx_get_atlas()) {
        /* Key Y boundaries within the atlas (ref: main.asm BOB tables) */
        static const struct { int atlas_y; const char *label; } k_bo_bands[] = {
            {   0, "y=0   WALK (8 dirs x3 frames, 32x30)" },
            {  96, "y=96  ALT WALK / SECONDARY SPRITES (32x30)" },
            { 160, "y=160 BULLETS/SHOTS (16x14)  x=256: PROJECTILES (16x16)" },
            { 192, "y=192 DEATH/EXPLOSION frames 0-9 (32x30)" },
            { 224, "y=224 DEATH frames 10-15 (32x30)  + MISC SHOTS (16x14)" },
            { 256, "y=256 LARGE WEAPON EFFECTS (96x128)  x=288: MISC 32x32" },
        };
        int n_bands = (int)(sizeof(k_bo_bands) / sizeof(k_bo_bands[0]));
        int atlas_top = VY_TO_SY(L.vy_bo_content, scroll_y) + 2;
        for (int b = 0; b < n_bands; b++) {
            int sy = atlas_top + k_bo_bands[b].atlas_y;
            if (sy < GFX_HEADER_H || sy >= 256) continue;
            /* Translucent dark bar + label */
            video_overlay_fill_rect(0, sy, 320, 8, 0, 0, 0, 140);
            draw_string(2, sy, k_bo_bands[b].label, 255, 220, 80);
        }
    }

    /* ---- Weapon atlas region annotations ----
     * From intex.asm weapons_pic_table: each cell is 160×88 px.
     * (Note: the debug viewer uses the level palette, not the INTEX green palette;
     *  colours will differ from the INTEX menu but all sprite data is visible.) */
    if (s_dbg_weapon_atlas.pixels) {
        static const struct { int atlas_y; int atlas_x; const char *label; } k_wpic[] = {
            {   0,   0, "y=0   x=0   TWINFIRE"     },
            {   0, 160, "y=0   x=160 FLAMEARC"     },
            {  88,   0, "y=88  x=0   SIDEWINDERS"  },
            {  88, 160, "y=88  x=160 FLAMETHROWER" },
            { 176,   0, "y=176 x=0   MACHINEGUN"   },
            { 176, 160, "y=176 x=160 PLASMAGUN"    },
        };
        int n_wp = (int)(sizeof(k_wpic) / sizeof(k_wpic[0]));
        int wpic_top = VY_TO_SY(L.vy_weapons_content, scroll_y) + 2;
        for (int w = 0; w < n_wp; w++) {
            int sy = wpic_top + k_wpic[w].atlas_y;
            int sx = k_wpic[w].atlas_x;
            if (sy < GFX_HEADER_H || sy >= 256) continue;
            /* Draw a thin translucent bar across just the cell column */
            video_overlay_fill_rect(sx, sy, 160, 8, 0, 0, 0, 140);
            draw_string(sx + 2, sy, k_wpic[w].label, 255, 200, 80);
        }
        /* Vertical divider between left and right columns */
        {
            int top = VY_TO_SY(L.vy_weapons_content, scroll_y) + 2;
            int bot = top + DBG_WEAPON_ATLAS_H;
            if (bot > 256) bot = 256;
            if (top < GFX_HEADER_H) top = GFX_HEADER_H;
            if (top < bot)
                video_overlay_fill_rect(160, top, 1, bot - top, 255, 200, 80, 120);
        }
    }

    /* ---- INTEX background label ---- */
    if (s_intex_bg) {
        int ly = VY_TO_SY(L.vy_intex_content, scroll_y) + 2 + s_intex_bg_h + 2;
        if (ly >= GFX_HEADER_H && ly < 256)
            draw_string(2, ly, "INTEX TERMINAL BG", 180, 230, 180);
    }

    /* ---- HUD labels ---- */
    {
        int vy_cur = L.vy_hud_content;
        const char *hud_names[] = { "P1 STATUS BAR", "P2 STATUS BAR", "GAME PAUSED" };
        DbgImg *hud_imgs[]      = { &s_dbg_p1_bar, &s_dbg_p2_bar, &s_dbg_paused };
        for (int i = 0; i < 3; i++) {
            DbgImg *img = hud_imgs[i];
            if (!img->pixels) continue;
            int ly = VY_TO_SY(vy_cur, scroll_y) + 2 + img->h + 2;
            if (ly >= GFX_HEADER_H && ly < 256)
                draw_string(2, ly, hud_names[i], 200, 200, 200);
            vy_cur += img->h + 9;
        }
    }

    /* ---- Font label ---- */
    if (s_dbg_font.pixels) {
        int ly = VY_TO_SY(L.vy_font_content, scroll_y) + 2 + s_dbg_font.h + 2;
        if (ly >= GFX_HEADER_H && ly < 256)
            draw_string(2, ly, "IN-GAME FONT (672x12 - 42 GLYPHS)", 200, 180, 230);
    }

    /* ---- Player sprite labels ---- */
    {
        char buf[8];
        for (int i = 0; i < PLAYER_SPRITE_TOTAL; i++) {
            int row = i / GFX_SPRS_PER_ROW;
            int col = i % GFX_SPRS_PER_ROW;
            int lx  = col * GFX_SPR_CELL_W + 2;
            int ly  = VY_TO_SY(L.vy_spr_content + row * GFX_SPR_CELL_H, scroll_y)
                      + 2 + 28 + 1;
            if (ly < GFX_HEADER_H || ly >= 256) continue;
            snprintf(buf, sizeof(buf), "%d", i + 1);
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
    /* Drain the KEY_F event that triggered entry */
    input_poll();

    gfx_viewer_load_assets();

    int scroll_y   = 0;
    int max_scroll = 0;

    while (!g_quit_requested) {
        timer_begin_frame();
        input_poll();

        if (g_key_pressed == KEY_F || g_key_pressed == KEY_ESC)
            break;

        const Uint8 *ks = SDL_GetKeyboardState(NULL);
        if (ks[SDL_SCANCODE_UP])    scroll_y -= 3;
        if (ks[SDL_SCANCODE_DOWN])  scroll_y += 3;
        if (ks[SDL_SCANCODE_LEFT])  scroll_y -= 48;
        if (ks[SDL_SCANCODE_RIGHT]) scroll_y += 48;

        if (scroll_y < 0)           scroll_y = 0;
        if (scroll_y > max_scroll)  scroll_y = max_scroll;

        max_scroll = gfx_viewer_render(scroll_y);
        video_flip();
    }

    gfx_viewer_free_assets();
}
