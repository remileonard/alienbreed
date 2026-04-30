/*
 * Alien Breed SE 92 - C port
 * Asset bundle — type shared between the VFS and the generated bundle index.
 *
 * When EMBED_ASSETS is ON at CMake configure time every file from assets/ and
 * game/ is converted to a C byte array by the bin2c tool and registered here.
 * The VFS (vfs.c) uses this table to serve data from memory; when a path is
 * not found in the table it falls back to opening the file from disk.
 *
 * When EMBED_ASSETS is OFF, asset_bundle_empty.c provides an empty table so
 * the VFS always falls back to disk (development / quick-iteration workflow).
 */

#ifndef ASSET_BUNDLE_H
#define ASSET_BUNDLE_H

#include <stddef.h>

typedef struct {
    const char          *path;  /* relative path used by the game (e.g. "assets/gfx/foo.raw") */
    const unsigned char *data;  /* embedded byte array                                         */
    size_t               size;  /* byte count                                                  */
} AssetEntry;

/* Defined in either the CMake-generated asset_bundle_index.c
 * or in asset_bundle_empty.c (when EMBED_ASSETS=OFF).          */
extern const AssetEntry g_asset_bundle[];
extern const int        g_asset_bundle_count;

#endif /* ASSET_BUNDLE_H */
