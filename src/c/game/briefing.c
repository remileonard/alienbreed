/*
 * Alien Breed SE 92 - C port
 * Briefing screen — translated from src/briefingcore/briefingcore.asm
 *                   and src/briefingstart/briefingstart.asm
 *
 * Level 1 uses briefingstart: different background, palette set immediately,
 * no sprite animation.  Levels 2–12 use briefingcore: palette fade-in while
 * four 16-px sprite columns descend from y=-32 to y=264 at +3 px/frame,
 * alternating two animation frames per column every tick.
 */

#include "briefing.h"
#include "level.h"
#include "constants.h"
#include "../hal/video.h"
#include "../hal/input.h"
#include "../hal/audio.h"
#include "../hal/timer.h"
#include "../hal/vfs.h"
#include "../engine/palette.h"
#include "../engine/typewriter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Briefing texts — verbatim from main.asm text_briefing_level_1..12  */
/* (x=8, y=40 start position encoded in dc.w 8,40 header in the ASM) */
/* ------------------------------------------------------------------ */
static const char *k_briefing_texts[NUM_LEVELS] = {
    /* Level 1 — briefingstart.asm */
    "SHUTTLE BAY TWO, DECK ONE.      \n"
    " \n"
    "UPON LEAVING YOUR CRAFT, PERFORM\n"
    "A RECCE OF THE IMMEDIATE AREA.  \n"
    "                                \n"
    "LOCATE DECK LIFT THAT LEADS TO  \n"
    "POWER SUB SYSTEM AND PROCEED    \n"
    "FURTHER INTO THE HEART OF THE   \n"
    "STATION. RADAR REPORTS LITTLE   \n"
    "MOVEMENT, BUT SOME NEAR VENTS.  \n"
    "                                \n"
    "CONTROL ADVISES MAXIMUM USE OF  \n"
    "ANY AVAILABLE RESOURCES LEFT BY \n"
    "THE STATIONS PREVIOUS OCCUPANTS.",

    /* Level 2 */
    "POWER SUBSYSTEM, DECK TWO.      \n"
    " \n"
    "INITIAL PRIORITY IS TO NEGATE   \n"
    "POSSIBILITIES OF FURTHER ATTACK \n"
    "BY BLOWING THE TOP TWO DECKS.   \n"
    "                                \n"
    "TAKE OUT THE FOUR POWER DOMES   \n"
    "AND GET OUT OF THERE BEFORE THE \n"
    "BLAST HITS. ONCE UNSTABLE, YOU  \n"
    "HAVE SIXTY SECONDS TO FLEE TO   \n"
    "THE NEXT LEVEL VIA THE DECK LIFT\n"
    "                                \n"
    "RADAR INDICATES MOVEMENT        \n"
    "DIRECTLY BELOW CURRENT POSITION.\n"
    " \n"
    "ENTER CODE 55955 TO RESTART HERE",

    /* Level 3 */
    "SECURITY ZONE, DECK THREE.      \n"
    " \n"
    "UNFORTUNATELY THE BLAST FROM THE\n"
    "POWER SYSTEM MELTDOWN HAS SPREAD\n"
    "INTO THIS LEVEL AND IF IT IS NOT\n"
    "STOPPED, THE WHOLE STATION COULD\n"
    "BLOW. IMMEDIATE CLOSURE OF ALL  \n"
    "FIRE DOORS REMAINS A PRIORITY   \n"
    "                                \n"
    "ONCE ACHIEVED, RETURN TO THE DECK\n"
    "LIFT AND AWAIT INSTRUCTIONS     \n"
    "                                \n"
    "RADAR MALFUNCTIONING.. ERRATIC  \n"
    "READINGS.. ADVISE CAUTION...    ",

    /* Level 4 */
    "OVAL ZONE. DECK FOUR.           \n"
    " \n"
    "THE THREAT FROM FIRE AND BLAST  \n"
    "HAS RECEDED SOMEWHAT, GIVING YOU\n"
    "CHANCE TO GATHER YOUR THOUGHTS..\n"
    "                                \n"
    "INFORMATION SUGGESTS THAT THE   \n"
    "NEXT DECK HARBOURS A MASSIVE    \n"
    "ALIEN PRESENCE AND IT IS THERE  \n"
    "THAT YOUR NEXT MISSION AWAITS.. \n"
    "PROCEED WITH ALL SPEED TO THE   \n"
    "ENGINEERING DECK.               \n"
    " \n"
    "ENTER CODE 48361 TO RESTART HERE",

    /* Level 5 */
    "ENGINEERING ZONE ONE. DECK FIVE.\n"
    " \n"
    "AN EVIL SMELL FILLS THE AIR AS  \n"
    "YOU ARRIVE IN THE FIRST OF A    \n"
    "SERIES OF ENGINEERING DECKS WHICH\n"
    "COMPRISE THE MAJOR SECTION OF THE\n"
    "WHOLE STATION.                  \n"
    "                                \n"
    "HQ ARE PLEASED TO INFORM YOU THAT\n"
    "THERES SOMETHING BIG IN THERE.. \n"
    "                                \n"
    "TERMINATE WHATEVER YOU FIND AND \n"
    "MAKE YOUR WAY DOWN TO DECK SIX..",

    /* Level 6 */
    "ENGINEERING SUB SYSTEM. DECK SIX.\n"
    " \n"
    "THE CREATURE YOU HAVE JUST SLAIN\n"
    "IS NOW REPORTED TO BE JUST ONE  \n"
    "OF A NUMBER LOCATED IN THIS     \n"
    "STATION. CONTROL ASSUME THE QUEEN\n"
    "ALIEN TO BE LURKING SOMEWHERE    \n"
    "QUIET AND WELL GUARDED, SOMEWHERE\n"
    "DEEP IN THE HEART OF THE STATION.\n"
    "                                 \n"
    "MAKE YOUR WAY TO DECK SEVEN,     \n"
    "THE ENGINEERING MAIN DECK... \n"
    " \n"
    "ENTER CODE 63556 TO RESTART HERE",

    /* Level 7 */
    "ENGINEERING. DECK SEVEN.         \n"
    " \n"
    "ANOTHER LARGE PRESENCE SHOWS     \n"
    "ON SCANNING EQUIPMENT, IT MIGHT  \n"
    "BE THE QUEEN ALIEN.. IF IT IS,   \n"
    "THEN PROCEED WITH CAUTION..      \n"
    "                                 \n"
    "ISOLATE AND TERMINATE ANYTHING   \n"
    "YOU SEE MOVING.. SURVIVORS THIS  \n"
    "DEEP IN THE STATION ARE UNLIKELY.\n"
    "                                 \n"
    "                                 \n"
    "                                 ",

    /* Level 8 */
    "POWERMECH SYSTEMS. DECK EIGHT.  \n"
    " \n"
    "THE DECK LIFT TO LOWER LEVELS HAS\n"
    "BEEN DAMAGED AND TRAVEL WILL HAVE\n"
    "TO BE RESTRICTED TO THE MAZE LIKE\n"
    "CONFINES OF THE ENGINEERING DUCTS\n"
    "                                \n"
    "FIND DUCT THREE AND QUICKLY MAKE\n"
    "YOUR WAY DOWN TO THE CENTRAL CORE\n"
    "                                \n"
    "REMOTE LOCATION SCANNER \n"
    "HIGHLY RECOMMENDED.. \n"
    " \n"
    "ENTER CODE 86723 TO RESTART HERE",

    /* Level 9 */
    "ENGINEERING SYSTEM SHAFT WHICH\n"
    "LINKS DECK EIGHT AND REACTOR CORE\n"
    " \n"
    "QUICKLY LOCATE AND USE GRILL LIFT\n"
    "THAT WILL TAKE YOU TO THE CENTRAL\n"
    "REACTOR CORE. SECURITY SYSTEMS  \n"
    "HAVE BEEN TRIGGERED AND YOU ARE \n"
    "ADVISED TO MOVE QUICKLY.        \n"
    "                                \n"
    "FORTUNATELY THERE SEEMS TO BE NO\n"
    "MOVEMENT IN THE SHAFT COMPLEX..\n"
    "NOT THAT IT MAKES THE TASK ANY \n"
    "EASIER THOUGH..",

    /* Level 10 */
    "REACTOR CORE. DECK TEN.\n"
    " \n"
    "YOUR PRIORITY IS TO DEACTIVATE\n"
    "THE CENTRAL CORE REACTOR IN ZONE\n"
    "SIX. THIS WILL PREPARE THE WHOLE\n"
    "STATION FOR MELTDOWN AND PROVIDE\n"
    "AN ESCAPE IN THE SHUTTLE, A JUST\n"
    "VICTORY AGAINST THE RENEGADE ALIEN\n"
    "FORCE..\n"
    "      \n"
    "MAKE FOR THE SHUTTLE CRAFT LIFT\n"
    "WHEN CORE MELTDOWN INITIATED.\n"
    " \n"
    "ENTER CODE 25194 TO RESTART HERE",

    /* Level 11 */
    "LOCATION UNKNOWN.. \n"
    " \n"
    "A LIFT MALFUNCTION HAS LEFT YOU\n"
    "IN NEAR DARKNESS.. BLUE EYES ARE\n"
    "COMING AT YOU FROM ALL DIRECTIONS\n"
    "AND YOUR ONLY HOPE IS TO MOVE\n"
    "QUICKLY AND TRUST YOUR LAZER..\n"
    " \n"
    "ESCAPE FROM THIS LIVING HELL IS\n"
    "THE ONLY OPTION. IT LOOKS LIKE\n"
    "THIS WAS SOME SORT OF SECURITY\n"
    "ZONE, SO CAUTION IS ADVISED...\n"
    "                                ",

    /* Level 12 */
    "THE HATCHERY..\n"
    " \n"
    "YOU ALMOST RETCH AS A FOUL STENCH\n"
    "HITS THE BACK OF YOUR THROAT, \n"
    "YOU REALISE EXACTLY WHO IS DOWN\n"
    "HERE.. \n"
    "                                \n"
    "THERE ARE EGGS EVERYWHERE AND\n"
    "SLIME COVERS THE BIOFORM WALLS..\n"
    " \n"
    "YOUR MISSION IS SIMPLE, KILL\n"
    "THE QUEEN AND GET THE HELL OUT\n"
    "BEFORE THE STATION GOES UP..",
};

/* ------------------------------------------------------------------ */
/* Palettes — extracted from lo5 files at offset 40*256*5 = 51200     */
/* ------------------------------------------------------------------ */

/* briefingstart: src/briefingstart/gfx/bkgnd_320x256.lo5 + 51200 */
static const UWORD k_pal_briefingstart[32] = {
    0x000, 0x750, 0x100, 0x200, 0x200, 0x300, 0x300, 0x400,
    0x410, 0xFFF, 0x300, 0x400, 0x500, 0x500, 0x600, 0x700,
    0x000, 0x100, 0x111, 0x211, 0x222, 0x322, 0x333, 0x433,
    0x443, 0x544, 0x554, 0x655, 0x665, 0x766, 0x776, 0x999,
};

/* briefingcore: src/briefingcore/gfx/bkgnd_320x256.lo5 + 51200 */
/* Entries 17-31 (COLOR17-COLOR31) are the sprite colours.         */
static const UWORD k_pal_briefingcore[32] = {
    0x000, 0x999, 0xAAA, 0xBBB, 0xCCC, 0xDDD, 0xEEE, 0xFFF,
    0x0B6, 0xFFF, 0x0AF, 0x07C, 0x00F, 0x70F, 0xC0E, 0x0AF,
    0x111, 0x000, 0x222, 0x333, 0x444, 0x555, 0x666, 0x777,
    0x888, 0x04C, 0x06D, 0x08E, 0x0AF, 0x5CF, 0xADF, 0xFFF,
};

/* ------------------------------------------------------------------ */
/* Sprite elevator constants (from briefingcore.asm struct layout)    */
/* ------------------------------------------------------------------ */

/* Horizontal positions of the four 16-px sprite columns (screen pixels).
 * Derived from disp_sprite formula: screen_x = struct_x field directly. */
static const int k_sprite_x[4] = { 27, 43, 59, 75 };

#define BRIEFING_SPRITE_W   16    /* each converted sprite column width in px */
#define BRIEFING_SPRITE_H   96    /* height from dc.w 96 in sprite struct      */
#define SPRITE_Y_START    (-32)   /* initial y (off-screen top)                */
#define SPRITE_Y_END      264     /* exit y (off-screen bottom)                */
#define SPRITE_Y_SPEED    3       /* pixels per frame (addq.w #3 in loop)      */

/* ------------------------------------------------------------------ */
/* Image helpers                                                       */
/* ------------------------------------------------------------------ */
typedef struct { UBYTE *pixels; int w, h; } BriefImg;

static int briefimg_load(BriefImg *img, const char *path)
{
    VFile *f = vfs_open(path);
    if (!f) { fprintf(stderr, "briefing: cannot open '%s'\n", path); return -1; }
    if (vfs_read(&img->w, 4, 1, f) != 1 || vfs_read(&img->h, 4, 1, f) != 1) {
        vfs_close(f); return -1;
    }
    size_t sz = (size_t)(img->w * img->h);
    img->pixels = (UBYTE *)malloc(sz);
    if (!img->pixels) { vfs_close(f); return -1; }
    if (vfs_read(img->pixels, 1, sz, f) != sz) {
        free(img->pixels); img->pixels = NULL; vfs_close(f); return -1;
    }
    vfs_close(f);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Sprite loading — decode one Amiga attached pair from a converted   */
/* assets/gfx/briefing_sprite*.raw file (output of convert_sprites    */
/* with attached=1: 4-byte w, 4-byte h, w*h indexed pixels).         */
/* ------------------------------------------------------------------ */
static UBYTE *sprite_load(const char *path)
{
    VFile *f = vfs_open(path);
    if (!f) return NULL;
    int w, h;
    if (vfs_read(&w, 4, 1, f) != 1 || vfs_read(&h, 4, 1, f) != 1) {
        vfs_close(f); return NULL;
    }
    UBYTE *px = (UBYTE *)malloc((size_t)(w * h));
    if (!px) { vfs_close(f); return NULL; }
    if ((int)vfs_read(px, 1, (size_t)(w * h), f) != w * h) {
        free(px); vfs_close(f); return NULL;
    }
    vfs_close(f);
    return px;
}

/* ------------------------------------------------------------------ */
/* briefing_run                                                        */
/* ------------------------------------------------------------------ */
void briefing_run(int level_idx)
{
    if (level_idx < 0 || level_idx >= NUM_LEVELS) return;

    const int is_start = (level_idx == 0);  /* briefingstart vs briefingcore */

    /* --- Load background --- */
    BriefImg bg = {NULL, 0, 0};
    briefimg_load(&bg, is_start
        ? "assets/gfx/briefingstart_bkgnd_320x256.raw"
        : "assets/gfx/briefingcore_bkgnd_320x256.raw");

    /* --- Load font (TEXT_LETTER_WIDTH=8, letter_h=12 from font_struct in ASM) --- */
    Font font = {0};
    font_load(&font, "assets/fonts/font_16x504.raw", 8, 12, 0);

    const char *text = k_briefing_texts[level_idx];

    /* ================================================================ */
    /* briefingstart — level 1                                          */
    /*                                                                  */
    /* Sequence (mirrors briefingstart.asm start: routine):             */
    /*   1. change_palette / set_palette — apply palette immediately    */
    /*   2. display_picture             — band-fill animation           */
    /*   3. display_text                — typewriter text with sound    */
    /*   4. wait_sync                   — one extra frame               */
    /* ================================================================ */
    if (is_start) {
        UWORD cur_pal[32];
        palette_set_immediate(k_pal_briefingstart, 32);
        memcpy(cur_pal, k_pal_briefingstart, sizeof(cur_pal));

        /* ------------------------------------------------------------ */
        /* display_picture: band-fill animation                          */
        /*                                                               */
        /* The ASM outer loop runs 8 times. Each iteration (pass p):    */
        /*   - copies 32 rows spaced 8 apart starting at row p          */
        /*     (rows p, p+8, p+16, …, p+248)                           */
        /*   - waits 4 vertical blanks (4 × wait_sync)                  */
        /*                                                               */
        /* Combined, all 8 passes cover every row 0..255 exactly once.  */
        /* ------------------------------------------------------------ */
        video_clear();
        int anim_skipped = 0;
        for (int pass = 0; pass < 8 && !anim_skipped && !g_quit_requested; pass++) {
            /* Reveal rows where (row % 8) == pass */
            if (bg.pixels) {
                int cols = (bg.w < 320) ? bg.w : 320;
                for (int row = pass; row < 256; row += 8) {
                    const UBYTE *src_row = bg.pixels + row * bg.w;
                    UBYTE       *dst_row = g_framebuffer + row * 320;
                    for (int x = 0; x < cols; x++)
                        dst_row[x] = src_row[x] & 0x1F;
                }
            }
            /* Wait 4 frames (4 × wait_sync in ASM) */
            for (int f = 0; f < 4 && !anim_skipped && !g_quit_requested; f++) {
                timer_begin_frame();
                input_poll();
                video_present();
                if ((g_player1_input & INPUT_FIRE1) ||
                     g_key_pressed == KEY_SPACE      ||
                     g_key_pressed == KEY_RETURN)
                    anim_skipped = 1;
            }
        }
        /* If skipped before all passes complete, blit the full background now */
        if (anim_skipped && bg.pixels)
            video_blit(bg.pixels, bg.w, 0, 0, 320, 256, -1);

        /* ------------------------------------------------------------ */
        /* display_text: typewriter with sound                           */
        /*                                                               */
        /* Original: tiny busy-wait per char + sound every 9 non-space  */
        /* chars via wait_play_sound counter (briefingstart.asm).        */
        /* Here: 1 frame per character (visible effect like intex),      */
        /* SAMPLE_TYPE_WRITER every 9 non-space characters.              */
        /*                                                               */
        /* Text colour: palette[9] = 0xFFF = white.  The font file that */
        /* ends up in assets/ may have pixel value 1 (main lo5 font      */
        /* overwrites the lo6 briefing font); text_color=9 overrides     */
        /* all non-transparent pixels to palette index 9 = white.        */
        /* ------------------------------------------------------------ */
        if (font.pixels) {
            TextCtx ctx;
            typewriter_init_ctx(&ctx, &font, g_framebuffer, 320, 8, 40);
            ctx.text_color = 9;   /* palette[9] = 0xFFF = white */

            int sound_ctr = 0;
            int text_skipped = anim_skipped; /* fire already pressed → dump at once */

            for (const char *p = text; *p && !g_quit_requested; p++) {
                if (*p == '\n') {
                    ctx.cursor_x  = ctx.start_x;
                    ctx.cursor_y += font.letter_h + 1;
                    continue;
                }

                typewriter_putchar(&ctx, *p);

                if (!text_skipped) {
                    /* Play SAMPLE_TYPE_WRITER every 9 non-space characters
                     * (wait_play_sound counter in typewriter.asm,
                     *  reset when == 9 in briefingstart.asm play_sound path). */
                    if (*p != ' ') {
                        if (++sound_ctr >= 9) {
                            sound_ctr = 0;
                            audio_play_sample(SAMPLE_TYPE_WRITER);
                        }
                    }
                    video_present();
                    timer_begin_frame();
                    input_poll();
                    if ((g_player1_input & INPUT_FIRE1) ||
                         g_key_pressed == KEY_SPACE      ||
                         g_key_pressed == KEY_RETURN)
                        text_skipped = 1;
                }
            }
            /* Present final framebuffer (all text typed) */
            video_present();
        }

        /* wait_sync: one extra frame after display_text */
        timer_begin_frame();

        /* Hold: wait for fire or ~8 s */
        int hold = 50 * 8;
        while (hold-- > 0 && !g_quit_requested) {
            timer_begin_frame();
            input_poll();
            if ((g_player1_input & INPUT_FIRE1) ||
                 g_key_pressed == KEY_SPACE      ||
                 g_key_pressed == KEY_RETURN)
                break;
            video_present();
        }

        /* Fade to black (palette only; framebuffer stays as-is) */
        UWORD s_black[32] = {0};
        palette_prep_fade_to_rgb(s_black, cur_pal, 32);
        for (int i = 0; i < 25 && !g_done_fade && !g_quit_requested; i++) {
            timer_begin_frame();
            input_poll();
            palette_tick();
            video_present();
        }
        palette_set_immediate(s_black, 32);

        free(bg.pixels);
        font_free(&font);
        return;
    }

    /* ================================================================ */
    /* briefingcore — levels 2–12                                       */
    /* ================================================================ */

    /* --- Load sprite frames: 4 positions × 2 animation frames ---    */
    /* Position p alternates between frame[p][0] and frame[p][1].      */
    /* Sprite pixel 0 = transparent; 1-15 → palette index 16+pixel.   */
    static const char *k_sprite_paths[4][2] = {
        { "assets/gfx/briefing_sprite1.raw", "assets/gfx/briefing_sprite5.raw" },
        { "assets/gfx/briefing_sprite2.raw", "assets/gfx/briefing_sprite6.raw" },
        { "assets/gfx/briefing_sprite3.raw", "assets/gfx/briefing_sprite7.raw" },
        { "assets/gfx/briefing_sprite4.raw", "assets/gfx/briefing_sprites.raw" },
    };
    UBYTE *sprites[4][2];
    memset(sprites, 0, sizeof(sprites));
    for (int p = 0; p < 4; p++) {
        for (int f = 0; f < 2; f++) {
            sprites[p][f] = sprite_load(k_sprite_paths[p][f]);
        }
    }

    /* --- Prepare palette fade-in (FADE_SPEED=1 in briefingcore.asm) --- */
    /* cur_pal tracks the fully-faded-in palette so palette_prep_fade_to_rgb
     * at the end has the correct starting point for the fade-to-black.       */
    UWORD cur_pal[32];
    memset(cur_pal, 0, sizeof(cur_pal));
    palette_prep_fade_in(k_pal_briefingcore, cur_pal, 32);

    /* --- Play SAMPLE_DESCENT --- */
    audio_play_sample(SAMPLE_DESCENT);

    /* --- Elevator animation loop ---
     * Each tick: fade palette one step, move sprites down by SPRITE_Y_SPEED,
     * draw background + sprites at current y, check exit.
     * Animation alternates between two frames per column every tick,
     * matching delay=0 in the ptr_sprite_*_pic lists. */
    int sprite_y   = SPRITE_Y_START;
    int frame_idx  = 0;   /* toggled each tick: 0 or 1 */

    while (!g_quit_requested) {
        timer_begin_frame();
        input_poll();

        video_clear();
        if (bg.pixels)
            video_blit(bg.pixels, bg.w, 0, 0, 320, 256, -1);

        /* Draw four sprite columns at current y, current animation frame */
        for (int p = 0; p < 4; p++) {
            if (sprites[p][frame_idx])
                video_blit(sprites[p][frame_idx], BRIEFING_SPRITE_W,
                           k_sprite_x[p], sprite_y,
                           BRIEFING_SPRITE_W, BRIEFING_SPRITE_H, 0);
        }

        palette_tick();
        video_present();

        /* Advance animation frame and position */
        frame_idx ^= 1;
        sprite_y  += SPRITE_Y_SPEED;

        /* Check exit: fire button or sprites fully off screen */
        if ((g_player1_input & INPUT_FIRE1) ||
             g_key_pressed == KEY_SPACE      ||
             g_key_pressed == KEY_RETURN)
            break;
        if (sprite_y >= SPRITE_Y_END)
            break;
    }

    /* After elevator loop: palette has fully faded in to k_pal_briefingcore.
     * Record that state so the fade-to-black at the end has the right start. */
    memcpy(cur_pal, k_pal_briefingcore, sizeof(cur_pal));

    /* --- Play SAMPLE_DESCENT_END --- */
    audio_play_sample(SAMPLE_DESCENT_END);

    /* --- Render background (sprites now off screen) --- */
    video_clear();
    if (bg.pixels)
        video_blit(bg.pixels, bg.w, 0, 0, 320, 256, -1);

    /* ------------------------------------------------------------------ */
    /* display_text: typewriter with sound (same sequence as level 1)      */
    /*                                                                      */
    /* Text colour: palette[7] = 0xFFF = white in k_pal_briefingcore.     */
    /* The shared font file has pixel value 1 (main lo5 font overwrite);   */
    /* text_color=7 overrides all glyph pixels to white.                   */
    /* ------------------------------------------------------------------ */
    if (font.pixels) {
        TextCtx ctx;
        typewriter_init_ctx(&ctx, &font, g_framebuffer, 320, 8, 40);
        ctx.text_color = 7;   /* palette[7] = 0xFFF = white in briefingcore */

        int sound_ctr    = 0;
        int text_skipped = 0;

        for (const char *p = text; *p && !g_quit_requested; p++) {
            if (*p == '\n') {
                ctx.cursor_x  = ctx.start_x;
                ctx.cursor_y += font.letter_h + 1;
                continue;
            }

            typewriter_putchar(&ctx, *p);

            if (!text_skipped) {
                if (*p != ' ') {
                    if (++sound_ctr >= 9) {
                        sound_ctr = 0;
                        audio_play_sample(SAMPLE_TYPE_WRITER);
                    }
                }
                video_present();
                timer_begin_frame();
                input_poll();
                if ((g_player1_input & INPUT_FIRE1) ||
                     g_key_pressed == KEY_SPACE      ||
                     g_key_pressed == KEY_RETURN)
                    text_skipped = 1;
            }
        }
        video_present();
    }

    /* --- Hold: wait for fire or ~8 s --- */
    int hold = 50 * 8;
    while (hold-- > 0 && !g_quit_requested) {
        timer_begin_frame();
        input_poll();
        if ((g_player1_input & INPUT_FIRE1) ||
             g_key_pressed == KEY_SPACE      ||
             g_key_pressed == KEY_RETURN)
            break;
        video_present();
    }

    /* --- Fade to black (palette only; framebuffer preserved) --- */
    UWORD s_black[32] = {0};
    palette_prep_fade_to_rgb(s_black, cur_pal, 32);
    for (int i = 0; i < 25 && !g_done_fade && !g_quit_requested; i++) {
        timer_begin_frame();
        input_poll();
        palette_tick();
        video_present();
    }
    palette_set_immediate(s_black, 32);

    /* --- Cleanup --- */
    for (int p = 0; p < 4; p++) {
        free(sprites[p][0]);
        free(sprites[p][1]);
    }
    free(bg.pixels);
    font_free(&font);
}
