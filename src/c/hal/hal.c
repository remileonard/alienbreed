/*
 * Alien Breed SE 92 - C port
 * HAL master implementation — init/quit dispatcher
 */

#include "hal.h"
#include <stdio.h>
#include <SDL2/SDL.h>

int hal_init(void)
{
    if (SDL_Init(0) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }

    if (video_init() < 0) return -1;
    if (input_init() < 0) return -1;
    if (audio_init() < 0) return -1;

    timer_init();
    return 0;
}

void hal_quit(void)
{
    audio_quit();
    input_quit();
    video_quit();
    SDL_Quit();
}
