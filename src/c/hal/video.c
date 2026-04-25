/*
 * Alien Breed SE 92 - C port
 * Video HAL implementation
 */

#include "video.h"
#include <string.h>
#include <stdio.h>

SDL_Renderer *g_renderer  = NULL;
SDL_Window   *g_window    = NULL;
UBYTE         g_framebuffer[320 * 256];
Uint32        g_palette[32];

/* Beam effect: scanline range where non-zero pixels are rendered white */
int g_beam_y      = -1;
int g_beam_height =  1;

/* SDL texture used as intermediate upload target */
static SDL_Texture *s_screen_tex = NULL;

int video_init(void)
{
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL Video init failed: %s\n", SDL_GetError());
        return -1;
    }

    g_window = SDL_CreateWindow(
        "Alien Breed Special Edition 92",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        320 * 2, 256 * 2,  /* default 2x scale */
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
    );
    if (!g_window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return -1;
    }

    g_renderer = SDL_CreateRenderer(g_window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_renderer) {
        /* Fallback to software renderer */
        g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_SOFTWARE);
        if (!g_renderer) {
            fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
            return -1;
        }
    }

    /* Nearest-neighbor scaling for pixel-perfect look */
    SDL_RenderSetLogicalSize(g_renderer, 320, 256);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    /* Streaming texture: ARGB8888, updated every frame from the indexed fb */
    s_screen_tex = SDL_CreateTexture(g_renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        320, 256);
    if (!s_screen_tex) {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        return -1;
    }

    /* Default palette: all black */
    memset(g_palette, 0, sizeof(g_palette));
    memset(g_framebuffer, 0, sizeof(g_framebuffer));

    return 0;
}

void video_quit(void)
{
    if (s_screen_tex) { SDL_DestroyTexture(s_screen_tex); s_screen_tex = NULL; }
    if (g_renderer)   { SDL_DestroyRenderer(g_renderer);  g_renderer   = NULL; }
    if (g_window)     { SDL_DestroyWindow(g_window);       g_window     = NULL; }
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

void video_present(void)
{
    video_upload_framebuffer();
    video_flip();
}

void video_upload_framebuffer(void)
{
    void  *pixels;
    int    pitch;

    if (SDL_LockTexture(s_screen_tex, NULL, &pixels, &pitch) < 0)
        return;

    Uint32 *dst = (Uint32 *)pixels;
    const UBYTE *src = g_framebuffer;
    const Uint32 white = 0xFFFFFFFF;

    /* Precompute beam range once so the per-scanline check is a simple int compare */
    int beam_lo = g_beam_y;
    int beam_hi = (g_beam_y >= 0) ? g_beam_y + g_beam_height : -1;

    for (int y = 0; y < 256; y++) {
        int is_beam = (y >= beam_lo && y < beam_hi);
        for (int x = 0; x < 320; x++) {
            UBYTE idx = src[x] & 0x1F;
            /* On beam scanlines, all non-zero color indices appear white —
             * mirrors the Amiga copper overwriting COLOR01-31 with $FFF. */
            dst[x] = (is_beam && idx != 0) ? white : g_palette[idx];
        }
        src += 320;
        dst  = (Uint32 *)((UBYTE *)dst + pitch);
    }

    SDL_UnlockTexture(s_screen_tex);
    SDL_RenderClear(g_renderer);
    SDL_RenderCopy(g_renderer, s_screen_tex, NULL, NULL);
}

void video_flip(void)
{
    SDL_RenderPresent(g_renderer);
}

void video_overlay_fill_rect(int x, int y, int w, int h,
                              Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(g_renderer, r, g, b, a);
    SDL_Rect rect = { x, y, w, h };
    SDL_RenderFillRect(g_renderer, &rect);
}

void video_overlay_rect_outline(int x, int y, int w, int h,
                                 Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(g_renderer, r, g, b, a);
    SDL_Rect rect = { x, y, w, h };
    SDL_RenderDrawRect(g_renderer, &rect);
}

void video_overlay_draw_point(int x, int y, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(g_renderer, r, g, b, a);
    SDL_RenderDrawPoint(g_renderer, x, y);
}

void video_clear(void)
{
    memset(g_framebuffer, 0, sizeof(g_framebuffer));
}

void video_set_palette_entry(int index, UWORD amiga_rgb)
{
    if (index < 0 || index >= 32) return;

    /* Amiga palette: 0x0RGB — each channel 4 bits, expand to 8 bits by
     * duplicating the nibble: 0xF → 0xFF, 0x5 → 0x55 */
    int r = (amiga_rgb >> 8) & 0xF;
    int g = (amiga_rgb >> 4) & 0xF;
    int b = (amiga_rgb >> 0) & 0xF;

    r = (r << 4) | r;
    g = (g << 4) | g;
    b = (b << 4) | b;

    g_palette[index] = (0xFF << 24) | (r << 16) | (g << 8) | b;
}

void video_set_palette(const UWORD *amiga_palette, int count)
{
    if (count > 32) count = 32;
    for (int i = 0; i < count; i++)
        video_set_palette_entry(i, amiga_palette[i]);
}

void video_plot_pixel(int x, int y, UBYTE color_index)
{
    if (x < 0 || x >= 320 || y < 0 || y >= 256) return;
    g_framebuffer[y * 320 + x] = color_index & 0x1F;
}

void video_blit(const UBYTE *src, int src_stride,
                int dst_x, int dst_y, int w, int h,
                int transparent_index)
{
    for (int y = 0; y < h; y++) {
        int dy = dst_y + y;
        if (dy < 0 || dy >= 256) { src += src_stride; continue; }
        for (int x = 0; x < w; x++) {
            int dx = dst_x + x;
            if (dx < 0 || dx >= 320) continue;
            UBYTE c = src[x];
            if (transparent_index >= 0 && c == (UBYTE)transparent_index) continue;
            g_framebuffer[dy * 320 + dx] = c & 0x1F;
        }
        src += src_stride;
    }
}

void video_fill_rect(int x, int y, int w, int h, UBYTE color_index)
{
    for (int row = y; row < y + h; row++) {
        if (row < 0 || row >= 256) continue;
        for (int col = x; col < x + w; col++) {
            if (col < 0 || col >= 320) continue;
            g_framebuffer[row * 320 + col] = color_index & 0x1F;
        }
    }
}

void video_scroll_copy(int src_x, int src_y, int dst_x, int dst_y, int w, int h)
{
    /* Use memmove per row to handle overlap safely */
    if (dst_y <= src_y) {
        for (int row = 0; row < h; row++) {
            int sy = src_y + row, dy = dst_y + row;
            if (sy < 0 || sy >= 256 || dy < 0 || dy >= 256) continue;
            memmove(&g_framebuffer[dy * 320 + dst_x],
                    &g_framebuffer[sy * 320 + src_x],
                    (size_t)w);
        }
    } else {
        for (int row = h - 1; row >= 0; row--) {
            int sy = src_y + row, dy = dst_y + row;
            if (sy < 0 || sy >= 256 || dy < 0 || dy >= 256) continue;
            memmove(&g_framebuffer[dy * 320 + dst_x],
                    &g_framebuffer[sy * 320 + src_x],
                    (size_t)w);
        }
    }
}
