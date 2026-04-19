#ifndef AB_TIMER_H
#define AB_TIMER_H

/*
 * Alien Breed SE 92 - C port
 * Timer HAL — replaces VBL interrupt synchronisation.
 *
 * Original: lev4irq fires at 50 Hz (PAL VBL), lbW0004BA flag cleared each frame.
 * Replacement: SDL2 ticks-based frame limiter at 50 fps.
 */

#include "../types.h"

/* Initialise the timer. Call once before the game loop. */
void timer_init(void);

/* Call at the start of each frame.
 * Blocks until the next 50 Hz tick if the frame was faster than 20 ms.
 * Returns the actual elapsed milliseconds since the last frame. */
int timer_begin_frame(void);

/* Total number of frames elapsed since timer_init() */
extern ULONG g_frame_counter;

/* Elapsed seconds of gameplay (incremented by the game logic) */
extern ULONG g_elapsed_seconds;

#endif /* AB_TIMER_H */
