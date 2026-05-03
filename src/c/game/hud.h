#ifndef AB_HUD_H
#define AB_HUD_H

/*
 * Alien Breed SE 92 - C port
 * HUD (heads-up display) module.
 *
 * Renders:
 *   - Destruction countdown timer (top-centre)
 *   - Player 1 status bar (lives, health, ammo, weapon, score, credits)
 *   - Player 2 status bar (if 2-player game)
 *   - "PAUSED" overlay
 *   - Map overview overlay
 *
 * Original: player_1_status_304x8.lo2, player_2_status_304x8.lo2,
 *           game_paused_96x7.lo1, timer_digit_*.raw
 */

#include "../types.h"

/* Initialise HUD resources (load status bar graphics). Returns 0 on success. */
int  hud_init(void);

/* Free HUD resources. */
void hud_quit(void);

/* Render HUD elements that write to the indexed framebuffer (none currently). */
void hud_render(void);

/*
 * Render the status bars as renderer overlays using the copper-list derived
 * per-scanline RGBA palette.  Must be called AFTER video_upload_framebuffer()
 * so the overlays are composited on top of the game frame.
 */
void hud_render_overlay(void);

/* Render the pause overlay. */
void hud_render_pause(void);

/* Render the map overview overlay. */
void hud_render_map_overview(void);

/* Number of players currently playing (1 or 2) */
extern int g_number_players;

#endif /* AB_HUD_H */
