#ifndef AB_HAL_H
#define AB_HAL_H

/*
 * Alien Breed SE 92 - C port
 * Hardware Abstraction Layer — master include.
 *
 * This header replaces the Amiga-specific:
 *   custom.i, cia.i, dmabits.i, intbits.i
 *
 * All direct register accesses (CUSTOM+*, CIAA, etc.) are
 * replaced by the function interfaces declared here.
 */

#include "../types.h"
#include "video.h"
#include "input.h"
#include "audio.h"
#include "timer.h"
#include "blitter.h"

/* Initialise all HAL subsystems. Returns 0 on success, -1 on error. */
int  hal_init(void);

/* Shut down all HAL subsystems. */
void hal_quit(void);

#endif /* AB_HAL_H */
