/*
 * Alien Breed SE 92 - C port
 * Empty asset bundle — compiled when EMBED_ASSETS is OFF.
 *
 * Provides the symbols declared in asset_bundle.h so that vfs.c links
 * correctly.  The VFS will always fall back to opening files from disk.
 */

#include "asset_bundle.h"

const AssetEntry g_asset_bundle[]  = { { NULL, NULL, 0 } };
const int        g_asset_bundle_count = 0;
