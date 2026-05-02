#ifndef AB_DEBUG_H
#define AB_DEBUG_H

/*
 * Alien Breed SE 92 - C port
 * Debug overlay — toggled with the D key during gameplay.
 *
 * When active, draws on top of the rendered frame (via SDL renderer
 * overlay functions after video_upload_framebuffer but before video_flip):
 *   - Tile attribute borders:
 *       pink   = wall tiles
 *       green  = collectible tiles (key, credits, health, ammo)
 *       cyan   = door tiles
 *       blue   = one-way / conveyor tiles (0x0E-0x11, 0x26-0x27, 0x2E-0x2F,
 *                                          0x37-0x3B, 0x3F) — hex code shown
 *       yellow = all other special tiles
 *   - Collision bounding boxes:
 *       red  (32×32) = alien collision box (origin = alien pos_x, pos_y)
 *       cyan (16×16) = player hit box (origin = pos_x+8, pos_y+8)
 *   - Info bar at the top of the screen showing player 1 stats:
 *       X/Y tile coordinates, health, credits, keys, ammo,
 *       ammo packs, lives.
 */

/* Non-zero when the debug overlay is active. */
extern int g_debug_overlay_on;

/* Draw the debug overlay.  Must be called between
 * video_upload_framebuffer() and video_flip(). */
void debug_render_overlay(void);

/*
 * Full-screen scrollable graphics viewer.
 * Pauses the game and shows all current-level graphics in four sections:
 *   TILES    — every tile in the loaded tileset, labelled by tile index.
 *   WALK     — alien walk sprites for all 8 directions × 3 frames.
 *   DEATH    — alien death/explosion animation frames.
 *   SPRITES  — all 80 player sprite images, labelled by 1-based sprite number.
 * Arrow keys scroll the view; press F or ESC to return to the game.
 * Triggered by pressing the F key during gameplay.
 */
void debug_gfx_viewer_run(void);

#endif /* AB_DEBUG_H */
