/*
 * Alien Breed SE 92 - C port
 * Timer HAL implementation
 */

#include "timer.h"
#include <SDL2/SDL.h>

ULONG g_frame_counter   = 0;
ULONG g_elapsed_seconds = 0;

static Uint64 s_last_tick  = 0;
static Uint64 s_freq       = 0;
static Uint32 s_sec_frames = 0;   /* frames counted within current second */

void timer_init(void)
{
    s_freq       = SDL_GetPerformanceFrequency();
    s_last_tick  = SDL_GetPerformanceCounter();
    g_frame_counter   = 0;
    g_elapsed_seconds = 0;
    s_sec_frames      = 0;
}

int timer_begin_frame(void)
{
    /* Target: 20 ms per frame (50 Hz PAL) */
    const Uint64 target_ticks = s_freq / 50;

    Uint64 now     = SDL_GetPerformanceCounter();
    Uint64 elapsed = now - s_last_tick;

    if (elapsed < target_ticks) {
        /* Sleep most of the remaining time, then busy-wait the last bit */
        Uint64 remaining_ms = (target_ticks - elapsed) * 1000 / s_freq;
        if (remaining_ms > 2)
            SDL_Delay((Uint32)(remaining_ms - 1));
        while (SDL_GetPerformanceCounter() - s_last_tick < target_ticks)
            ; /* spin */
    }

    now = SDL_GetPerformanceCounter();
    int delta_ms = (int)((now - s_last_tick) * 1000 / s_freq);
    s_last_tick  = now;

    g_frame_counter++;

    /* Count elapsed seconds (50 frames = 1 second) */
    s_sec_frames++;
    if (s_sec_frames >= 50) {
        s_sec_frames = 0;
        g_elapsed_seconds++;
    }

    return delta_ms;
}
