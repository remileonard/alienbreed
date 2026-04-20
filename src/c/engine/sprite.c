/*
 * Alien Breed SE 92 - C port
 * Sprite engine — translated from src/common/sprite.asm
 *
 * Player sprites are stored as Amiga hardware-sprite format in
 * src/main/sprites/player_sprite*.raw.
 *
 * Amiga hardware sprite format (per frame):
 *   - 2 words header (VSTART/VSTOP/HSTART, flags)
 *   - Then pairs of UWORD rows: (plane0_word, plane1_word) × height
 *   - 2 words footer = 0x0000
 *
 * After conversion by tools/convert_sprites.c the files in
 * assets/sprites/ are flat: width × height UBYTE indexed pixels.
 */

#include "sprite.h"
#include "alien_gfx.h"
#include "../hal/video.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 80 player sprites + 10 timer digits */
#define PLAYER_SPRITE_COUNT 80
#define TIMER_DIGIT_COUNT   10
#define SPRITE_W 16   /* Amiga hardware sprites are 16 pixels wide */

/* Each loaded sprite image: raw indexed pixels */
typedef struct {
    UBYTE *pixels;
    int    w, h;
} SpriteImage;

static SpriteImage s_player[PLAYER_SPRITE_COUNT];
static SpriteImage s_digits[TIMER_DIGIT_COUNT];
static int         s_loaded = 0;

/* Helper: load a flat indexed raw file (output of convert_sprites) */
static int load_raw_sprite(SpriteImage *img, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    /* Header written by convert_sprites: 4 bytes width, 4 bytes height */
    int w, h;
    if (fread(&w, 4, 1, f) != 1 || fread(&h, 4, 1, f) != 1) {
        fclose(f); return -1;
    }

    img->w = w;
    img->h = h;
    img->pixels = (UBYTE *)malloc((size_t)(w * h));
    if (!img->pixels) { fclose(f); return -1; }

    if ((int)fread(img->pixels, 1, (size_t)(w * h), f) != w * h) {
        free(img->pixels); img->pixels = NULL;
        fclose(f); return -1;
    }
    fclose(f);
    return 0;
}

int sprite_load_player(void)
{
    char path[256];
    memset(s_player, 0, sizeof(s_player));
    memset(s_digits, 0, sizeof(s_digits));

    for (int i = 0; i < PLAYER_SPRITE_COUNT; i++) {
        snprintf(path, sizeof(path), "assets/sprites/player_sprite%d.raw", i + 1);
        load_raw_sprite(&s_player[i], path);
        /* Non-fatal: log missing sprites during development */
    }
    for (int i = 0; i < TIMER_DIGIT_COUNT; i++) {
        snprintf(path, sizeof(path), "assets/sprites/timer_digit_%d.raw", i);
        load_raw_sprite(&s_digits[i], path);
    }
    s_loaded = 1;
    return 0;
}

void sprite_free_all(void)
{
    for (int i = 0; i < PLAYER_SPRITE_COUNT; i++) {
        free(s_player[i].pixels); s_player[i].pixels = NULL;
    }
    for (int i = 0; i < TIMER_DIGIT_COUNT; i++) {
        free(s_digits[i].pixels); s_digits[i].pixels = NULL;
    }
    s_loaded = 0;
}

void sprite_tick(Sprite *s)
{
    if (!s->frames) return;

    const SpriteFrame *f = &s->frames[s->cur_frame];
    if (f->delay < 0) {
        /* sentinel: reset */
        s->cur_frame    = 0;
        s->frame_counter = s->frames[0].delay;
        return;
    }

    s->frame_counter--;
    if (s->frame_counter <= 0) {
        s->cur_frame++;
        f = &s->frames[s->cur_frame];
        if (f->delay < 0) {
            s->cur_frame = 0;   /* loop */
            f = &s->frames[0];
        }
        s->frame_counter = f->delay;
    }
}

void sprite_draw(const Sprite *s)
{
    if (!s->visible || !s->frames) return;
    const SpriteFrame *f = &s->frames[s->cur_frame];
    if (!f->pixels) return;

    video_blit(f->pixels, s->w, s->x, s->y, s->w, s->h, s->transparent);
}

/*
 * Player sprite layout in player_sprite*.raw:
 * The 80 sprites cover 8 directions × 10 weapon/animation frames.
 * Index formula: (direction - 1) * 10 + frame_index
 * This is a simplification; actual mapping needs cross-ref with main.asm.
 */
const SpriteFrame *sprite_get_player_frames(int dir, int weapon_id)
{
    /* TODO: build proper frame sequences when full sprite mapping is known.
     * For now return a static single-frame entry per sprite index. */
    (void)dir; (void)weapon_id;
    return NULL;
}

/* Convenience: draw a timer digit at (x, y) */
void sprite_draw_digit(int digit, int x, int y)
{
    if (digit < 0 || digit > 9) return;
    SpriteImage *img = &s_digits[digit];
    if (!img->pixels) return;
    video_blit(img->pixels, img->w, x, y, img->w, img->h, 0);
}

/* Draw animated player sprite at (x,y) using facing direction (1-8). */
void sprite_draw_player(int player_idx, int x, int y, int facing)
{
    /* Sprite index: player 0 uses sprites 0-39, player 1 uses 40-79.
     * Within each half: 8 directions × 5 frames stacked in the sheet.
     * We use one sprite image per direction for simplicity. */
    int base    = player_idx * 40;
    int dir_idx = (facing - 1) & 7;   /* clamp 0-7 */
    int idx     = base + dir_idx;
    if (idx < 0 || idx >= PLAYER_SPRITE_COUNT) return;
    SpriteImage *img = &s_player[idx];
    if (!img->pixels) return;
    video_blit(img->pixels, img->w, x, y, img->w, img->h, 0);
}

/* Draw alien sprite (type 0-based) at (x,y) using atlas decoded from the BO file.
 * anim_frame: 0, 1, or 2 (walk cycle frames).
 * Atlas layout: column = type_idx * ALIEN_SPRITE_W, row = anim_frame * ALIEN_SPRITE_W.
 * Frame size: ALIEN_SPRITE_W × ALIEN_SPRITE_H (32 × 30 px).
 * Color index 0 is transparent (Ref: main.asm#L12365, blitter minterm $CA). */
void sprite_draw_alien(int type_idx, int anim_frame, int x, int y)
{
    const UBYTE *atlas = alien_gfx_get_atlas();
    if (!atlas) return;

    /* Clamp inputs */
    if (type_idx  < 0) type_idx  = 0;
    if (type_idx  > 6) type_idx  = 6;
    if (anim_frame < 0) anim_frame = 0;
    if (anim_frame >= ALIEN_WALK_FRAMES) anim_frame = ALIEN_WALK_FRAMES - 1;

    int atlas_x = type_idx  * ALIEN_SPRITE_W;   /* 0, 32, 64, 96, 128, 160, 192 */
    int atlas_y = anim_frame * ALIEN_SPRITE_W;  /* 0, 32, or 64 */

    const UBYTE *src = atlas + (size_t)(atlas_y * ALIEN_ATLAS_W + atlas_x);
    video_blit(src, ALIEN_ATLAS_W, x, y, ALIEN_SPRITE_W, ALIEN_SPRITE_H, 0);
}
