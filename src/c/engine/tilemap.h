#ifndef AB_TILEMAP_H
#define AB_TILEMAP_H

/*
 * Alien Breed SE 92 - C port
 * Tilemap engine — loads T7MP level maps and renders them.
 *
 * Map format documented in docs/levelmaps_format.txt:
 *   - XBLK × YBLK tiles (all levels: 96 × 120)
 *   - Each tile = 1 UWORD: bits 0–5 = attribute, bits 6–15 = tile index
 *   - 4 palettes: PALA (normal), PALB (destruction timer)
 *   - Background tileset filename in IFFP chunk
 *
 * The camera scrolls a 320×256 viewport over the larger map.
 * Each tile is 16×16 pixels (standard for Alien Breed).
 */

#include "../types.h"

#define MAP_TILE_W  16
#define MAP_TILE_H  16
#define MAP_COLS    120   /* XBLK: horizontal tile count per map row */
#define MAP_ROWS     96   /* YBLK: vertical tile count (number of rows) */

/* Parsed level data */
typedef struct {
    UWORD  tiles[MAP_ROWS][MAP_COLS];   /* raw tile words from BODY chunk */
    UWORD  palette_a[32];               /* PALA: normal palette */
    UWORD  palette_b[32];               /* PALB: destruction-timer palette */
    char   bg_filename[8];              /* tileset name from IFFP e.g. "LABM" */
    int    valid;
} LevelMap;

/* Tileset: indexed pixel data for each tile, 16×16 px */
typedef struct {
    UBYTE *pixels;      /* tile_count × 16 × 16 bytes */
    int    tile_count;
} Tileset;

/* Camera position in pixels */
extern int g_camera_x;
extern int g_camera_y;

/* Currently loaded map and tileset */
extern LevelMap g_cur_map;
extern Tileset  g_tileset;

/*
 * Load a level map file (T7MP format) from path.
 * Returns 0 on success.
 */
int  tilemap_load(const char *path, LevelMap *map);

/*
 * Load the tileset image for a map.
 * bg_filename is the stem from the IFFP chunk (e.g. "mapbkgnd_320x256").
 * Returns 0 on success.
 */
int  tileset_load(const char *bg_filename, Tileset *ts);

/* Free tileset memory. */
void tileset_free(Tileset *ts);

/*
 * Render the visible portion of the map to the framebuffer.
 * Uses g_camera_x/y to determine the viewport.
 */
void tilemap_render(const LevelMap *map, const Tileset *ts);

/* Get the attribute byte for a tile at map coordinates (col, row). */
static inline UBYTE tilemap_attr(const LevelMap *map, int col, int row)
{
    if (col < 0 || col >= MAP_COLS || row < 0 || row >= MAP_ROWS)
        return 0x01; /* treat out-of-bounds as wall */
    return (UBYTE)(map->tiles[row][col] & 0x3F);
}

/*
 * Returns 1 if the tile blocks movement (all wall variants from tiles_action_table):
 *   0x01 = wall
 *   0x1d = wall (variant)
 *   0x2a-0x2d = reactor walls
 * All other attributes are walkable (floor, pickups, doors open with key, etc.)
 */
static inline int tilemap_is_solid(const LevelMap *map, int col, int row)
{
    UBYTE a = tilemap_attr(map, col, row);
    return (a == 0x01 || a == 0x1d || (a >= 0x2a && a <= 0x2d));
}

/* Convert pixel coordinates to tile coordinates. */
static inline int tilemap_pixel_to_col(int px) { return px / MAP_TILE_W; }
static inline int tilemap_pixel_to_row(int py) { return py / MAP_TILE_H; }

/* Scan map for the player spawn tile (attr 0x35) and return its world-pixel
 * centre.  Returns 1 on success, 0 if not found (caller uses defaults). */
int tilemap_find_spawn(const LevelMap *map, int *out_x, int *out_y);

/*
 * Replace a tile at (col, row) with a floor tile, making both the logic
 * attribute and the visible graphic disappear.  Uses the tile word from the
 * nearest adjacent floor tile so the graphic blends with the surroundings.
 * Falls back to tile word 0x0000 (attr=0/floor, tile_idx=0) if no floor
 * neighbour is found.
 * Ref: patch_tiles + and.w #$FFC0,(a3) @ main.asm#L7815 / #L5277.
 */
void tilemap_replace_tile(LevelMap *map, int col, int row);

#endif /* AB_TILEMAP_H */
