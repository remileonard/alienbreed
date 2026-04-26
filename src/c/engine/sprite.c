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
#include "../game/player.h"
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

/* Draw animated player sprite using the full ASM animation state machine.
 *
 * Sprite layout per player (40 sprites each, player 1 = 0-39, player 2 = 40-79):
 *   spr  0- 7 (1-indexed 1-8):  base idle pose, one per cur_sprite direction
 *   spr  8- 9 (9-10):           fire mid-walk for directions 1-2
 *   spr 10-17 (11-18):          walk frame A, one per direction
 *   spr 18-19 (19-20):          fire mid-walk for directions 3-4
 *   spr 20-27 (21-28):          walk frame B, one per direction
 *   spr 28-29 (29-30):          fire mid-walk for directions 5-6
 *   spr 30-37 (31-38):          fire flash pose, one per direction
 *   spr 38-39 (39-40):          fire mid-walk for directions 7-8
 *
 * Offsets within the walk cycle frames (all relative to spr = cur_sprite - 1):
 *   +0  : base idle (no legs movement)
 *   +10 : walk frame A (legs forward)
 *   +20 : walk frame B (legs back)
 *   +30 : fire flash (gun extended)
 *
 * Fire mid-walk sprite (alternates with walk frames while firing):
 *   cur_sprite 1→idx 8, 2→9, 3→18, 4→19, 5→28, 6→29, 7→38, 8→39
 *
 * Animation sequences (mirrors player_1_data sprite tables @ main.asm#L12900):
 *   Idle (state 0):       [base]
 *   Walk (state 1-8):     [base, +10, +20, +10]  2 ticks/frame
 *   Fire-idle (state 9):  [+30, +20]              1 tick/frame
 *   Fire-walk (10-17):    [+30, base, +30, +10]   1 tick/frame
 *   Hit/fire (18-26):     [base, fire-mid, +10, fire-mid]  1 tick/frame
 *   Respawn (27-35):      blink at 4-tick rate using g_player_invincibility
 */
void sprite_draw_player(int player_idx, int x, int y, int facing)
{
    (void)facing; /* direction is tracked in p->cur_sprite via the walk table */

    if (player_idx < 0 || player_idx >= MAX_PLAYERS) return;
    const Player *p = &g_players[player_idx];
    if (!p->alive) return;

    int cur    = p->cur_sprite;    /* body-orientation pose 1-8 */
    int state  = p->anim_state;    /* 0-35 */
    int sframe = p->anim_seq_frame;
    int base   = player_idx * 40;  /* sprite sheet offset for this player */
    int has_ammo = (p->ammunitions >= 1);

    /* Fire mid-walk sprite offsets indexed by cur_sprite 1-8
     * (ref: lbL013E02-lbL013F1A @ main.asm#L13020-L13051) */
    static const int k_fire_mid[9] = { 0, 8, 9, 18, 19, 28, 29, 38, 39 };

    int sprite_idx;

    if (state >= 27) {
        /* Respawn / invincible: blink at 4-frame intervals.
         * (ref: lbL014002 = {lbL098E2C,4},{spr_base,4} @ main.asm#L13068) */
        if ((g_player_invincibility[player_idx] / 4) & 1) return; /* invisible phase */
        sprite_idx = base + (cur - 1);

    } else if (state >= 18) {
        /* Hit animation — 4-frame cycle: [base, fire-mid, walk-A, fire-mid]
         * (ref: lbL013E02 @ main.asm#L13020) */
        static const int k_hit[4] = { 0, 1, 2, 1 }; /* 0=base, 1=fire-mid, 2=+10 */
        switch (k_hit[sframe & 3]) {
            case 0:  sprite_idx = base + (cur - 1);           break;
            case 1:  sprite_idx = base + k_fire_mid[cur];     break;
            default: sprite_idx = base + (cur - 1) + 10;      break;
        }

    } else if (state >= 9) {
        /* Fire animation */
        if (!has_ammo) {
            /* No ammo: fall back to plain walk/idle animation
             * (ref: state 9-17 handlers: cmp.w #1,PLAYER_AMMUNITIONS; bpl fire; else walk) */
            if (state == 9) {
                sprite_idx = base + (cur - 1);                /* idle */
            } else {
                static const int k_walk[4] = { 0, 10, 20, 10 };
                sprite_idx = base + (cur - 1) + k_walk[sframe & 3];
            }
        } else if (state == 9) {
            /* Fire idle: 2-frame [fire-flash, walk-B]
             * (ref: lbL013B02 = {spr31,1},{spr21,1} @ main.asm#L12940) */
            static const int k_fire_idle[2] = { 30, 20 };
            sprite_idx = base + (cur - 1) + k_fire_idle[sframe & 1];
        } else {
            /* Fire walk: 4-frame [fire-flash, base, fire-flash, walk-A]
             * (ref: lbL013BC2 = {spr31,0},{spr1,2},{spr31,0},{spr11,2}... @ main.asm#L12956) */
            static const int k_fire_walk[4] = { 30, 0, 30, 10 };
            sprite_idx = base + (cur - 1) + k_fire_walk[sframe & 3];
        }

    } else if (state >= 1) {
        /* Walking: 4-frame cycle [base, +10, +20, +10]
         * (ref: lbL0139C2 = {spr1,2},{spr11,2},{spr21,2},{spr11,2} @ main.asm#L12908) */
        static const int k_walk[4] = { 0, 10, 20, 10 };
        sprite_idx = base + (cur - 1) + k_walk[sframe & 3];

    } else {
        /* Idle: static base pose
         * (ref: lbL013942 = {spr1,2,-1} @ main.asm#L12900) */
        sprite_idx = base + (cur - 1);
    }

    if (sprite_idx < base || sprite_idx >= base + 40) return;
    if (sprite_idx < 0 || sprite_idx >= PLAYER_SPRITE_COUNT) return;
    SpriteImage *img = &s_player[sprite_idx];
    if (!img->pixels) return;
    video_blit(img->pixels, img->w, x-(img->w/2), y-(img->h/2), img->w, img->h, 0);
    video_plot_pixel(x, y, 12);  /* debug: centre of player sprite */
    video_plot_pixel(x + PROBE_LEFT_X,  y,              15);  /* debug: left  wall probe */
    video_plot_pixel(x + PROBE_RIGHT_X, y,              15);  /* debug: right wall probe */
    video_plot_pixel(x,                 y + PROBE_UP_Y, 15);  /* debug: up    wall probe */
    video_plot_pixel(x,                 y + PROBE_DOWN_Y, 15);/* debug: down  wall probe */
}

/* Draw alien walk sprite (direction=0-7 compass, anim_frame=0-2) at (x,y).
 * Atlas column = direction * ALIEN_SPRITE_W.
 * Atlas row depends on atlas type (COMPACT: y=frame*32; LEGACY: y={0,96,128}).
 * Ref: lbW01945E / lbW019A8E @ main.asm#L14034,L14160; blitter minterm $CA. */
void sprite_draw_alien(int direction, int anim_frame, int x, int y)
{
    const UBYTE *atlas = alien_gfx_get_atlas();
    if (!atlas) return;

    if (direction  < 0) direction  = 0;
    if (direction  >= ALIEN_DIR_COUNT) direction = ALIEN_DIR_COUNT - 1;
    if (anim_frame < 0) anim_frame = 0;
    if (anim_frame >= ALIEN_WALK_FRAMES) anim_frame = ALIEN_WALK_FRAMES - 1;

    int atlas_x = direction * ALIEN_SPRITE_W;

    /* Walk frame Y = frame_idx * 32 for ALL atlas types.
     * Both COMPACT (lbW019A8E) and LEGACY (lbW01945E) store the main walk
     * cycle at y=0, y=32, y=64: lbL01B036 references entries 100-123 in
     * lbW01945E which are at (dir*32, frame*32) — identical layout to COMPACT.
     * (lbW01945E entries 8-23 at y=96/128 are SECONDARY BOBs for a different
     *  idle-animation layer and must NOT be used here.) */
    int atlas_y = anim_frame * ALIEN_WALK_FRAME_STRIDE;

    const UBYTE *src = atlas + (size_t)(atlas_y * ALIEN_ATLAS_W + atlas_x);
    /* (x,y) is the centre of the 32×32 alien world bbox; blit at top-left. */
    video_blit(src, ALIEN_ATLAS_W, x - 16, y - 16, ALIEN_SPRITE_W, ALIEN_SPRITE_H, 0);
}

/* Draw a death/explosion frame (0-15) at screen position (x,y).
 * Frames are 32×30 px (same size as walk sprites), laid out in two rows:
 *   Row 1 (y=0xC0=192): frames  0-9,  x = frame_idx * 32
 *   Row 2 (y=0xE0=224): frames 10-15, x = (frame_idx-10) * 32
 * Ref: lbL018C2E @ main.asm#L13907 → lbW0188CE entries 40-55 (#L13874-L13889). */
void sprite_draw_alien_death(int death_frame, int x, int y)
{
    const UBYTE *atlas = alien_gfx_get_atlas();
    if (!atlas) return;

    if (death_frame < 0) death_frame = 0;
    if (death_frame >= ALIEN_DEATH_FRAMES) death_frame = ALIEN_DEATH_FRAMES - 1;

    int atlas_x, atlas_y;
    if (death_frame < ALIEN_DEATH_ROW1_COUNT) {
        atlas_x = death_frame * ALIEN_DEATH_W;
        atlas_y = ALIEN_DEATH_ROW1_Y;
    } else {
        atlas_x = (death_frame - ALIEN_DEATH_ROW1_COUNT) * ALIEN_DEATH_W;
        atlas_y = ALIEN_DEATH_ROW2_Y;
    }

    const UBYTE *src = atlas + (size_t)(atlas_y * ALIEN_ATLAS_W + atlas_x);
    /* (x,y) is the centre of the 32×32 alien world bbox; blit at top-left. */
    video_blit(src, ALIEN_ATLAS_W, x - 16, y - 16, ALIEN_DEATH_W, ALIEN_DEATH_H, 0);
}

int sprite_get_player_raw(int idx, const UBYTE **pixels, int *w, int *h)
{
    if (idx < 0 || idx >= PLAYER_SPRITE_COUNT) return -1;
    if (!s_player[idx].pixels) return -1;
    *pixels = s_player[idx].pixels;
    *w      = s_player[idx].w;
    *h      = s_player[idx].h;
    return 0;
}
