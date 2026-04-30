/*
 * Alien Breed SE 92 - C port
 * Virtual filesystem — public API
 *
 * All game asset loading goes through this layer.  When an asset has been
 * embedded into the binary (EMBED_ASSETS=ON at CMake configure time) vfs_open
 * returns an in-memory "file" backed by a const byte array; otherwise it falls
 * back to a regular FILE* on disk.  This keeps every call-site identical
 * regardless of whether assets are embedded or external.
 *
 * The API mirrors the subset of <stdio.h> used by the engine:
 *   vfs_open / vfs_read / vfs_close / vfs_seek / vfs_tell / vfs_eof
 * plus an SDL_RWops helper for SDL_mixer's Mix_LoadWAV_RW.
 */

#ifndef VFS_H
#define VFS_H

#include <stddef.h>
#include <SDL2/SDL.h>

/* Opaque virtual-file handle. */
typedef struct VFile VFile;

/*
 * vfs_open — open a virtual file by its relative game path.
 * Returns NULL if the path is neither in the bundle nor on disk.
 */
VFile *vfs_open(const char *path);

/*
 * vfs_read — read up to (elem_size * count) bytes.
 * Returns the number of complete elements read (same semantics as fread).
 */
size_t vfs_read(void *buf, size_t elem_size, size_t count, VFile *f);

/* vfs_close — release the handle (and close the underlying FILE* if any). */
int vfs_close(VFile *f);

/*
 * vfs_seek — reposition the read cursor.
 * whence: SEEK_SET / SEEK_CUR / SEEK_END  (same as fseek).
 * Returns 0 on success, -1 on error.
 */
int vfs_seek(VFile *f, long offset, int whence);

/* vfs_tell — return the current cursor position (same as ftell). */
long vfs_tell(VFile *f);

/* vfs_eof — non-zero if the cursor is at or past the end of data. */
int vfs_eof(VFile *f);

/*
 * vfs_rwops — create an SDL_RWops for use with Mix_LoadWAV_RW and similar.
 * If the path is in the bundle, returns SDL_RWFromConstMem; otherwise
 * SDL_RWFromFile.  The caller is responsible for freeing the returned object
 * (pass autoclose=1 to Mix_LoadWAV_RW).
 * Returns NULL on failure.
 */
SDL_RWops *vfs_rwops(const char *path);

#endif /* VFS_H */
