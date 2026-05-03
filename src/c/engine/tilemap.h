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
 * Returns 1 if the tile blocks movement for the PLAYER (tiles_action_table
 * entries that jump to tile_wall @ main.asm#L5059-L5104):
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

/*
 * Returns 1 if the tile blocks movement for ALIENS (aliens_collisions_table
 * entries that jump to aliens_collision_stop or aliens_collision_door
 * @ main.asm#L7172-L7268):
 *   0x01        = wall
 *   0x03        = door (aliens_collision_door → stop)
 *   0x0d-0x11   = metallic floor / one-way tiles (passable for player, solid for alien)
 *   0x1c        = wall variant (solid for alien, NOT for player)
 *   0x23        = hard-climb right (solid for alien)
 *   0x2a-0x2d   = reactor walls
 *   0x38-0x3b   = diagonal one-way tiles (solid for alien)
 * NOTE: 0x1d is a player wall but NOT an alien wall.
 */
static inline int tilemap_is_alien_solid(const LevelMap *map, int col, int row)
{
    UBYTE a = tilemap_attr(map, col, row);
    return (a == 0x01 || a == 0x03 ||
            (a >= 0x0d && a <= 0x11) ||
            a == 0x1c || a == 0x23 ||
            (a >= 0x2a && a <= 0x2d) ||
            (a >= 0x38 && a <= 0x3b));
}

/*
 * Returns 1 if a projectile should be stopped by this tile.
 * Covers tiles that ultimately reach impact_on_wall / lbC00E6A8:
 *   0x01        = wall         → impact_on_wall
 *   0x03        = door         → impact_on_door → impact_on_wall
 *   0x1d        = wall variant → impact_on_wall
 *   0x23        = hard-climb wall (alien-solid) → impact_on_wall
 *   0x2a-0x2d   = reactor walls → patch_reactor_* → impact_on_wall
 * NOTE: 0x08/0x09/0x12/0x13 (fire door buttons) are NOT blocking: they only
 *   trigger a visual patch and the projectile continues (they rts without
 *   calling lbC00E6A8 / impact_on_wall).  Check with
 *   tilemap_is_projectile_trigger() for those.
 * NOTE: 0x19-0x1c are level-3 triggers that do NOT stop the projectile.
 * Ref: weapons_special_impact_table @ main.asm#L9535-L9599.
 */
static inline int tilemap_is_projectile_blocking(const LevelMap *map, int col, int row)
{
    UBYTE a = tilemap_attr(map, col, row);
    return (a == 0x01 || a == 0x03 ||
            a == 0x1d ||
            a == 0x23 ||
            (a >= 0x2a && a <= 0x2d));
}

/*
 * Returns 1 if the tile triggers a non-blocking side-effect when a projectile
 * passes through it (fire door buttons, alarm buttons).
 * The projectile is NOT stopped — the tile just triggers its effect and the
 * projectile continues moving.
 * Ref: patch_fire_door_left_btn / right_btn / alarm variants @ main.asm#L9790-L9866.
 */
static inline int tilemap_is_projectile_trigger(const LevelMap *map, int col, int row)
{
    UBYTE a = tilemap_attr(map, col, row);
    return (a == 0x08 || a == 0x09 || a == 0x12 || a == 0x13);
}

/*
 * Returns 1 if the tile calls impact_on_wall (bounce logic for FLAMEARC/LAZER):
 *   0x01, 0x1d, 0x23 directly; 0x2a-0x2d via patch_reactor_* → impact_on_wall.
 * Ref: impact_on_wall @ main.asm#L9702.
 */
static inline int tilemap_is_impact_wall(const LevelMap *map, int col, int row)
{
    UBYTE a = tilemap_attr(map, col, row);
    return (a == 0x01 || a == 0x1d || a == 0x23 || (a >= 0x2a && a <= 0x2d));
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

/*
 * Replace every tile in the map whose attribute byte equals `attr` with a
 * floor tile (calls tilemap_replace_tile for each matching cell).
 * Used to blow out an entire reactor face (attributes 0x2a-0x2d) at once,
 * mirroring patch_dat_reactors / patch_tiles @ main.asm#L9651-L9657 which
 * replaces the graphics of the whole face rather than just the hit tile.
 */
void tilemap_replace_reactor_face(LevelMap *map, UBYTE attr);

#endif /* AB_TILEMAP_H */
