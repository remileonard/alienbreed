#ifndef AB_INPUT_H
#define AB_INPUT_H

/*
 * Alien Breed SE 92 - C port
 * Input HAL — replaces CIA port reads and POTINP register accesses.
 *
 * Original: btst CIAB_GAMEPORT0/1, CIAA  +  CUSTOM+POTINP
 * Replacement: SDL2 event polling mapped to the same bitmask layout.
 *
 * Bitmask layout (matches the layout produced by user_input in main.asm):
 *   bit 6 : fire 1 (primary)
 *   bit 7 : fire 2 (secondary)
 *   bit 4 : pause / select (CD32 green button)
 *   bit 5 : next weapon (CD32 blue button)
 *   bit 0 : up
 *   bit 1 : down
 *   bit 2 : left
 *   bit 3 : right
 */

#include "../types.h"
#include <SDL2/SDL.h>

/* Current and previous frame input states for both players */
extern UWORD g_player1_input;
extern UWORD g_player2_input;
extern UWORD g_player1_old_input;
extern UWORD g_player2_old_input;

/* Last Amiga-style keycode pressed (0 = none) */
extern UBYTE g_key_pressed;

/* Flag set when the user requests quit (e.g. OS close button) */
extern int   g_quit_requested;

/* Initialise the input subsystem (SDL2 events). Returns 0 on success. */
int  input_init(void);

/* Shut down input subsystem. */
void input_quit(void);

/* Poll SDL2 events and update all g_player*_input globals.
 * Must be called once per game frame. */
void input_poll(void);

/* Returns 1 if input is currently enabled. */
extern int g_input_enabled;

#endif /* AB_INPUT_H */
