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

#endif /* AB_DEBUG_H */
