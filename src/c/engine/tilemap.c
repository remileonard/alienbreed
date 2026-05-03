/*
 * Alien Breed SE 92 - C port
 * Tilemap engine
 */

#include "tilemap.h"
#include "../hal/video.h"
#include "../hal/vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int       g_camera_x = 0;
int       g_camera_y = 0;
LevelMap  g_cur_map;
Tileset   g_tileset;

/* Read a big-endian ULONG from a virtual file */
static ULONG read_u32_be(VFile *f)
{
    UBYTE b[4];
    if (vfs_read(b, 1, 4, f) != 4) return 0;
    return ((ULONG)b[0] << 24) | ((ULONG)b[1] << 16) |
           ((ULONG)b[2] <<  8) |  (ULONG)b[3];
}

/* Read a big-endian UWORD from a virtual file */
static UWORD read_u16_be(VFile *f)
{
    UBYTE b[2];
    if (vfs_read(b, 1, 2, f) != 2) return 0;
    return (UWORD)((b[0] << 8) | b[1]);
}

/* Match a 4-byte chunk ID */
static int match_chunk(VFile *f, const char *id)
{
    char buf[4];
    if (vfs_read(buf, 1, 4, f) != 4) return 0;
    return memcmp(buf, id, 4) == 0;
}

int tilemap_load(const char *path, LevelMap *map)
{
    VFile *f = vfs_open(path);
    if (!f) {
        fprintf(stderr, "tilemap_load: cannot open %s\n", path);
        return -1;
    }

    memset(map, 0, sizeof(*map));

    /* T7MP header */
    if (!match_chunk(f, "T7MP")) { vfs_close(f); return -1; }
    read_u32_be(f);  /* chunk size */
    read_u32_be(f);  /* file size */

    /* Walk chunks until BODY */
    while (!vfs_eof(f)) {
        char id[5] = {0};
        if (vfs_read(id, 1, 4, f) != 4) break;
        ULONG chunk_sz = read_u32_be(f);

        if (memcmp(id, "XBLK", 4) == 0) {
            read_u32_be(f);  /* horizontal size = 120 columns */
        } else if (memcmp(id, "YBLK", 4) == 0) {
            read_u32_be(f);  /* vertical size = 96 rows */
        } else if (memcmp(id, "IFFP", 4) == 0) {
            /* Background filename — only chars 13–16 matter per format doc */
            char buf[64] = {0};
            size_t rd = chunk_sz < 64 ? chunk_sz : 64;
            vfs_read(buf, 1, rd, f);
            /* ASM: reads 4 bytes from temp_map_buffer+189 = IFFP_data[13..16]
             * e.g. "ABdisk:CLIP2/LABM-IFF" → bytes 13-16 = "LABM" */
            memcpy(map->bg_filename, buf + 13, 4);
            map->bg_filename[4] = '\0';
            if ((long)chunk_sz > (long)rd) vfs_seek(f, (long)(chunk_sz - (int)rd), SEEK_CUR);
        } else if (memcmp(id, "PALA", 4) == 0) {
            /* Skip 64-byte filename, then read 64 bytes of palette (32 UWORDs) */
            vfs_seek(f, 64, SEEK_CUR);
            for (int i = 0; i < 32; i++)
                map->palette_a[i] = read_u16_be(f);
            long remaining = (long)chunk_sz - 64 - 64;
            if (remaining > 0) vfs_seek(f, remaining, SEEK_CUR);
        } else if (memcmp(id, "PALB", 4) == 0) {
            vfs_seek(f, 64, SEEK_CUR);
            for (int i = 0; i < 32; i++)
                map->palette_b[i] = read_u16_be(f);
            long remaining = (long)chunk_sz - 64 - 64;
            if (remaining > 0) vfs_seek(f, remaining, SEEK_CUR);
        } else if (memcmp(id, "BODY", 4) == 0) {
            /* 23040 bytes = 96 × 120 × 2 */
            for (int row = 0; row < MAP_ROWS; row++)
                for (int col = 0; col < MAP_COLS; col++)
                    map->tiles[row][col] = read_u16_be(f);
            break;  /* BODY is the last useful chunk */
        } else {
            /* Skip unknown chunk */
            vfs_seek(f, (long)chunk_sz, SEEK_CUR);
        }
    }

    vfs_close(f);
    map->valid = 1;
    return 0;
}

int tileset_load(const char *bg_filename, Tileset *ts)
{
    char path[256];
    snprintf(path, sizeof(path), "assets/tiles/%s.raw", bg_filename);


    VFile *f = vfs_open(path);
    if (!f) {
        /* Fallback: use the map background file */
        snprintf(path, sizeof(path), "assets/tiles/mapbkgnd.raw");
        f = vfs_open(path);
        if (!f) {
            fprintf(stderr, "tileset_load: cannot open tileset\n");
            return -1;
        }
    }

    /* File header: width (int), height (int) — written by convert_bitplanes.
     * Tiles are always 16×16; tile_count = height / 16. */
    int w, h;
    if (vfs_read(&w, 4, 1, f) != 1 || vfs_read(&h, 4, 1, f) != 1) {
        vfs_close(f); return -1;
    }
    int tile_count = h / 16;

    size_t sz = (size_t)(tile_count * 16 * 16);
    ts->pixels = (UBYTE *)malloc(sz);
    if (!ts->pixels) { vfs_close(f); return -1; }

    if (vfs_read(ts->pixels, 1, sz, f) != sz) {
        free(ts->pixels); ts->pixels = NULL;
        vfs_close(f); return -1;
    }

    ts->tile_count = tile_count;
    vfs_close(f);
    return 0;
}

void tileset_free(Tileset *ts)
{
    free(ts->pixels);
    ts->pixels     = NULL;
    ts->tile_count = 0;
}

/*
 * Scan the map for the player spawn tile (attribute 0x35 = 53).
 * Mirrors search_starting_position in main.asm, but uses tile centre
 * coordinates instead of the Amiga-specific screen offsets (+4, +58)
 * which were relative to the playfield Y start on the Amiga display.
 * Returns 1 on success, 0 if not found (caller should use defaults).
 */
int tilemap_find_spawn(const LevelMap *map, int *out_x, int *out_y)
{
    if (!map->valid) return 0;
    for (int row = 0; row < MAP_ROWS; row++) {
        for (int col = 0; col < MAP_COLS; col++) {
            if ((map->tiles[row][col] & 0x3F) == 0x35) {
                /* Centre of the spawn tile in world pixels */
                *out_x = col * MAP_TILE_W + MAP_TILE_W / 2;
                *out_y = row * MAP_TILE_H + MAP_TILE_H / 2;
                return 1;
            }
        }
    }
    return 0;
}

void tilemap_replace_tile(LevelMap *map, int col, int row)
{
    if (!map || col < 0 || col >= MAP_COLS || row < 0 || row >= MAP_ROWS) return;

    /* Look for an adjacent floor tile (attr == 0x00) and reuse its entire
     * tile word (graphic index + attribute).  This makes the replaced tile
     * blend visually with the surrounding floor rather than showing tile 0
     * in the wrong context.  Scan order: up, down, left, right.
     * Ref: patch_tiles queues a graphical replacement in the original ASM;
     * in the C port we apply it immediately. */
    static const int dirs[4][2] = {{0,-1},{0,1},{-1,0},{1,0}};
    UWORD floor_word = 0x0000;  /* fallback: tile index 0, attr = floor */
    for (int i = 0; i < 4; i++) {
        int nc = col + dirs[i][0];
        int nr = row + dirs[i][1];
        if (nc < 0 || nc >= MAP_COLS || nr < 0 || nr >= MAP_ROWS) continue;
        if ((map->tiles[nr][nc] & 0x3F) == 0x00) {
            floor_word = map->tiles[nr][nc];
            break;
        }
    }
    map->tiles[row][col] = floor_word;
}

void tilemap_replace_reactor_face(LevelMap *map, UBYTE attr)
{
    if (!map) return;
    /*
     * Scan every tile in the map; replace all that carry the given reactor
     * attribute.  tilemap_replace_tile() fills each cell with the nearest
     * adjacent floor-tile word, so border tiles pick up the real surrounding
     * floor and interior tiles (whose neighbours are also replaced on the
     * same pass) fall back to 0x0000 — both produce a passable floor cell.
     * Mirrors patch_dat_reactors / patch_tiles @ main.asm#L9651-L9657 which
     * patches the graphics of the whole reactor face in one call.
     */
    for (int row = 0; row < MAP_ROWS; row++) {
        for (int col = 0; col < MAP_COLS; col++) {
            if ((map->tiles[row][col] & 0x3F) == attr)  /* lower 6 bits = attribute */
                tilemap_replace_tile(map, col, row);
        }
    }
}

void tilemap_render(const LevelMap *map, const Tileset *ts)
{
    if (!map->valid || !ts->pixels) return;

    /* Determine which tiles are visible */
    int start_col = g_camera_x / MAP_TILE_W;
    int start_row = g_camera_y / MAP_TILE_H;
    int off_x     = g_camera_x % MAP_TILE_W;
    int off_y     = g_camera_y % MAP_TILE_H;

    int cols_visible = (320 + off_x + MAP_TILE_W - 1) / MAP_TILE_W;
    int rows_visible = (256 + off_y + MAP_TILE_H - 1) / MAP_TILE_H;

    for (int tr = 0; tr <= rows_visible; tr++) {
        int map_row = start_row + tr;
        if (map_row < 0 || map_row >= MAP_ROWS) continue;

        int dst_y = tr * MAP_TILE_H - off_y;

        for (int tc = 0; tc <= cols_visible; tc++) {
            int map_col = start_col + tc;
            if (map_col < 0 || map_col >= MAP_COLS) continue;

            UWORD tile_word  = map->tiles[map_row][map_col];
            int   tile_idx   = (tile_word >> 6) & 0x3FF;

            if (tile_idx >= ts->tile_count) continue;

            const UBYTE *tile_px = ts->pixels +
                                   (size_t)tile_idx * MAP_TILE_W * MAP_TILE_H;
            int dst_x = tc * MAP_TILE_W - off_x;

            video_blit(tile_px, MAP_TILE_W, dst_x, dst_y,
                       MAP_TILE_W, MAP_TILE_H, -1);  /* -1 = no transparency */
            
            /* debug: plot top-left pixel of each tile */
        }
    }
}
