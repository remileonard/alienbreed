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

/*
 * Draw a timer digit as a renderer overlay using the given RGB colour.
 * Must be called after video_upload_framebuffer().
 * Transparent pixels (index 0) are skipped.
 */
void sprite_draw_digit_overlay(int digit, int x, int y, UBYTE r, UBYTE g, UBYTE b);

/* Draw the animated player sprite for player index p at screen position. */
void sprite_draw_player(int player_idx, int x, int y, int facing);

/*
 * Draw alien walk sprite for the given compass direction (0=N,1=NE,…,7=NW)
 * at screen position (x, y).
 * anim_frame: 0, 1, or 2 — index into the 3-frame walk cycle.
 * Atlas column = direction * 32 px.
 * Atlas row = anim_frame * 32 (identical for COMPACT and LEGACY atlas types;
 *   lbL01B036 main walk sequence uses entries at y=0,32,64 in both lbW019A8E
 *   and lbW01945E — see alien_gfx.h for details).
 * Color index 0 is transparent (Ref: blitter minterm $CA, main.asm#L12365).
 */
void sprite_draw_alien(int direction, int anim_frame, int x, int y);

/*
 * Draw a death/explosion frame for a dying alien at screen position (x, y).
 * death_frame: 0-15 (16-frame explosion sequence).
 * Sprite size: ALIEN_DEATH_W × ALIEN_DEATH_H (32 × 30 px, same as walk sprites).
 * Atlas layout:
 *   frames  0-9:  x = death_frame * 32,          y = ALIEN_DEATH_ROW1_Y (192)
 *   frames 10-15: x = (death_frame-10) * 32,     y = ALIEN_DEATH_ROW2_Y (224)
 * (Ref: lbW0188CE entries 40-55 @ main.asm#L13874, death anim lbL018C2E#L13907)
 */
void sprite_draw_alien_death(int death_frame, int x, int y);

/*
 * Draw a single frame of the alien hatch zoom-in animation at screen position
 * (x, y).  This is used for TILE_ALIEN_HOLE (0x34) spawns while hatch_timer
 * is still counting down (mirrors lbC00A568 @ main.asm#L7272-L7278).
 *
 * hatch_frame: 0, 1, or 2 (small → medium → large zoom).
 *   Frame 3 (full size = walk) is handled by the normal sprite_draw_alien path.
 *
 * Both COMPACT and LEGACY alien atlases store these 32×32 BOBs at:
 *   x = 288 (0x120), y = 288 + hatch_frame * 32
 * (COMPACT lbW019A8E entries 83-85 @ main.asm#L14244-L14246;
 *  LEGACY  lbW01945E entries 96-98 @ main.asm#L14130-L14132).
 */
void sprite_draw_alien_hatch(int hatch_frame, int x, int y);

/*
 * Draw a face hugger (small alien) walk sprite at screen position (x, y).
 * Face huggers use 16×16 sprites from atlas x=256-304, y=0-144 rather than
 * the 32×30 large-alien sprites.
 *
 * direction : 0=N 1=NE 2=E 3=SE 4=S 5=SW 6=W 7=NW (same encoding as large alien)
 * anim_frame: 0, 1, or 2 — index into the 3-frame walk cycle.
 *             Pass ALIEN_WALK_FRAMES (3) for the hit-flash orange/red variant
 *             (uses dedicated atlas rows y=96 for dirs 0-3, y=112 for dirs 4-7).
 *
 * Atlas layout (identical for all BO atlas types):
 *   dirs 0-3: atlas_x = 256 + (dir   )*16, atlas_y = frame*32
 *   dirs 4-7: atlas_x = 256 + (dir-4 )*16, atlas_y = 16 + frame*32
 * (Ref: lbW009414 / lbL00969C / lbL01B982 @ main.asm#L6059,L6315,L14613)
 */
void sprite_draw_facehugger(int direction, int anim_frame, int x, int y);

/* Total number of player sprites available (both players, 1-based in game = 1-80) */
#define PLAYER_SPRITE_TOTAL 80

/*
 * Get raw pixel data for a player sprite by 0-based index (0 to PLAYER_SPRITE_TOTAL-1).
 * On success sets *pixels, *w, *h and returns 0.
 * Returns -1 if the index is out of range or the sprite was not loaded.
 */
int sprite_get_player_raw(int idx, const UBYTE **pixels, int *w, int *h);

#endif /* AB_SPRITE_H */
