/*
 * Alien Breed SE 92 - C port
 * Virtual filesystem — implementation
 *
 * Bundle lookup is a simple linear scan over g_asset_bundle[].
 * For a game with a few hundred assets this is fast enough;
 * a hash table can be substituted later if needed.
 */

#include "vfs.h"
#include "asset_bundle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct VFile {
    /* Bundle-backed (data != NULL) */
    const unsigned char *data;
    size_t               size;
    size_t               pos;
    /* Disk-backed (file != NULL) */
    FILE                *file;
};

/* ------------------------------------------------------------------ */
/* Internal helpers                                                    */
/* ------------------------------------------------------------------ */

static const AssetEntry *bundle_lookup(const char *path)
{
    for (int i = 0; i < g_asset_bundle_count; i++) {
        if (strcmp(g_asset_bundle[i].path, path) == 0)
            return &g_asset_bundle[i];
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

VFile *vfs_open(const char *path)
{
    VFile *f = (VFile *)calloc(1, sizeof(VFile));
    if (!f) return NULL;

    const AssetEntry *entry = bundle_lookup(path);
    if (entry) {
        f->data = entry->data;
        f->size = entry->size;
        f->pos  = 0;
        f->file = NULL;
    } else {
        f->file = fopen(path, "rb");
        if (!f->file) {
            free(f);
            return NULL;
        }
        f->data = NULL;
    }
    return f;
}

size_t vfs_read(void *buf, size_t elem_size, size_t count, VFile *f)
{
    if (!f || !buf) return 0;

    if (f->file)
        return fread(buf, elem_size, count, f->file);

    /* Bundle-backed */
    size_t bytes_requested = elem_size * count;
    size_t available       = f->size - f->pos;
    size_t bytes_to_copy   = bytes_requested < available ? bytes_requested : available;

    if (bytes_to_copy == 0 || elem_size == 0) return 0;

    memcpy(buf, f->data + f->pos, bytes_to_copy);
    f->pos += bytes_to_copy;

    return bytes_to_copy / elem_size;
}

int vfs_close(VFile *f)
{
    if (!f) return 0;
    int ret = 0;
    if (f->file)
        ret = fclose(f->file);
    free(f);
    return ret;
}

int vfs_seek(VFile *f, long offset, int whence)
{
    if (!f) return -1;

    if (f->file)
        return fseek(f->file, offset, whence);

    /* Bundle-backed */
    long new_pos;
    switch (whence) {
        case SEEK_SET: new_pos = offset;               break;
        case SEEK_CUR: new_pos = (long)f->pos + offset; break;
        case SEEK_END: new_pos = (long)f->size + offset; break;
        default:       return -1;
    }

    if (new_pos < 0 || (size_t)new_pos > f->size) return -1;
    f->pos = (size_t)new_pos;
    return 0;
}

long vfs_tell(VFile *f)
{
    if (!f) return -1L;
    if (f->file) return ftell(f->file);
    return (long)f->pos;
}

int vfs_eof(VFile *f)
{
    if (!f) return 1;
    if (f->file) return feof(f->file);
    return (f->pos >= f->size) ? 1 : 0;
}

SDL_RWops *vfs_rwops(const char *path)
{
    const AssetEntry *entry = bundle_lookup(path);
    if (entry)
        return SDL_RWFromConstMem(entry->data, (int)entry->size);
    return SDL_RWFromFile(path, "rb");
}
