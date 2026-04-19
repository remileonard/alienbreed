#ifndef AB_TYPES_H
#define AB_TYPES_H

/*
 * Alien Breed SE 92 - C port
 * Amiga-compatible type aliases for easy translation from 68k ASM.
 */

#include <stdint.h>
#include <stdbool.h>

typedef uint8_t  UBYTE;
typedef int8_t   BYTE;
typedef uint16_t UWORD;
typedef int16_t  WORD;
typedef uint32_t ULONG;
typedef int32_t  LONG;

#define TRUE  1
#define FALSE 0

/* Screen dimensions (PAL Amiga) */
#define SCREEN_W  320
#define SCREEN_H  256
#define SCREEN_SCALE 2          /* Integer upscale factor for modern displays */

/* Game timing */
#define TARGET_FPS 50           /* PAL VBL = 50 Hz */
#define FRAME_MS   (1000 / TARGET_FPS)

/* Palette */
#define PALETTE_SIZE 32         /* 32 colors (5 bitplanes max) */

/* Number of audio channels (Paula) */
#define AUDIO_CHANNELS 4

/* Max simultaneous sprites */
#define MAX_SPRITES 8

#endif /* AB_TYPES_H */
