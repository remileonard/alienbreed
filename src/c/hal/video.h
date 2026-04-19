#ifndef AB_VIDEO_H
#define AB_VIDEO_H

/*
 * Alien Breed SE 92 - C port
 * Video HAL — replaces Copper lists, bitplane DMA, COLOR registers.
 *
 * The game renders to an internal 320x256 software framebuffer and
 * SDL2 upscales it to the window at SCREEN_SCALE.
 */

#include "../types.h"
#include <SDL2/SDL.h>

/* The active SDL renderer and window (read-only outside video.c) */
extern SDL_Renderer *g_renderer;
extern SDL_Window   *g_window;

/* Indexed framebuffer: one byte per pixel, index into g_palette */
extern UBYTE g_framebuffer[320 * 256];

/* Current 32-color palette in ARGB8888 format */
extern Uint32 g_palette[32];

/* Initialise SDL2 window + renderer. Returns 0 on success. */
int  video_init(void);

/* Shut down SDL2 video. */
void video_quit(void);

/* Present the framebuffer to the screen.
 * Converts the indexed g_framebuffer through g_palette and blits. */
void video_present(void);

/* Clear the framebuffer to color index 0. */
void video_clear(void);

/* Set a palette entry from an Amiga 12-bit RGB word ($RGB, 4 bits each).
 * index : 0-31
 * amiga_rgb : e.g. 0x0F00 = red */
void video_set_palette_entry(int index, UWORD amiga_rgb);

/* Set all 32 palette entries from an array of Amiga RGB words. */
void video_set_palette(const UWORD *amiga_palette, int count);

/* Draw a single 8x1 row of pixels at (x,y) with given palette index.
 * This is the main low-level plot used by the blitter replacement. */
void video_plot_pixel(int x, int y, UBYTE color_index);

/* Blit a rectangular region of src_pixels (indexed) onto the framebuffer. */
void video_blit(const UBYTE *src, int src_stride,
                int dst_x, int dst_y, int w, int h,
                int transparent_index);

/* Fill a rectangle with a solid color index. */
void video_fill_rect(int x, int y, int w, int h, UBYTE color_index);

/* Copy a region of the framebuffer to another location (scroll helper). */
void video_scroll_copy(int src_x, int src_y, int dst_x, int dst_y, int w, int h);

#endif /* AB_VIDEO_H */
