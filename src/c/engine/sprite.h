#ifndef AB_SPRITE_H
#define AB_SPRITE_H

/*
 * Alien Breed SE 92 - C port
 * Sprite engine — translated from src/common/sprite.asm
 *
 * Amiga hardware sprites are replaced by indexed-color SDL surfaces.
 * Each sprite frame is stored as a flat indexed UBYTE array (w×h).
 *
 * The animation system mirrors the original:
 *   - A sprite object holds a list of frames.
 *   - Each frame entry: { pixel_data_ptr, delay_in_frames }
 *   - A sentinel entry with delay = -1 resets the animation.
 */

#include "../types.h"

#define MAX_SPRITE_FRAMES 64

/* A single animation frame */
typedef struct {
    const UBYTE *pixels;   /* indexed color data, width × height bytes */
    int          delay;    /* frames to hold this frame; -1 = end-of-anim sentinel */
} SpriteFrame;

/* A sprite object (mirrors the sprite structure accessed in sprite.asm) */
typedef struct {
    int           x, y;           /* screen position */
    int           w, h;           /* frame dimensions */
    int           transparent;    /* palette index treated as transparent */
    const SpriteFrame *frames;    /* animation frame array, terminated by delay=-1 */
    int           cur_frame;      /* index into frames[] */
    int           frame_counter;  /* counts down to 0 then advances frame */
    int           visible;
} Sprite;

/* Load all player sprites from assets/sprites/.
 * Returns 0 on success. */
int  sprite_load_player(void);

/* Free all loaded sprite data. */
void sprite_free_all(void);

/* Advance a sprite's animation by one frame tick.
 * Equivalent to disp_sprite → .animate in sprite.asm */
void sprite_tick(Sprite *s);

/* Draw the sprite's current frame to the framebuffer. */
void sprite_draw(const Sprite *s);

/* Convenience: set sprite position */
static inline void sprite_set_pos(Sprite *s, int x, int y) { s->x = x; s->y = y; }

/* Get a pointer to the player sprite frame array for direction dir (1–8) and
 * weapon weapon_id.  Returns NULL if not available. */
const SpriteFrame *sprite_get_player_frames(int dir, int weapon_id);

/* Draw a single timer/HUD digit (0-9) at screen position x,y. */
void sprite_draw_digit(int digit, int x, int y);

/* Draw the animated player sprite for player index p at screen position. */
void sprite_draw_player(int player_idx, int x, int y, int facing);

/*
 * Draw alien sprite for type type_idx (0-based, 0=alien1 … 6=alien7) at
 * screen position (x, y).
 * anim_frame selects the walk cycle frame (0, 1, or 2).
 * Atlas column = type_idx * 32 px, row = anim_frame * 32 px, size 32×30 px.
 * Color index 0 is transparent (Ref: blitter minterm $CA, main.asm#L12365).
 */
void sprite_draw_alien(int type_idx, int anim_frame, int x, int y);

#endif /* AB_SPRITE_H */
