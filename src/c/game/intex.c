/*
 * Alien Breed SE 92 - C port
 * INTEX terminal — translated from src/intex/intex.asm
 *
 * Main menu (text_main_menu in intex.asm):
 *   Choice 0: INTEX WEAPON SUPPLIES
 *   Choice 1: INTEX TOOL SUPPLIES
 *   Choice 2: INTEX RADAR SERVICE
 *   Choice 3: MISSION OBJECTIVE
 *   Choice 4: ENTER HOLOCODE
 *   Choice 5: GAME STATISTICS
 *   Choice 6: INFO BASE
 *   Choice 7: ABORT INTEX NETWORK
 *
 * Cursor Y = 68 + menu_choice * 12  (from disp_caret_in_menu in intex.asm)
 * Cursor X = 48
 */

#include "intex.h"
#include "player.h"
#include "level.h"
#include "constants.h"
#include "../hal/video.h"
#include "../hal/input.h"
#include "../hal/audio.h"
#include "../hal/timer.h"
#include "../engine/palette.h"
#include "../engine/tilemap.h"
#include "../engine/typewriter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Menu choices (menu_choice values from intex.asm) */
#define MENU_WEAPON_SUPPLIES  0
#define MENU_TOOL_SUPPLIES    1
#define MENU_RADAR_SERVICE    2
#define MENU_MISSION_OBJ      3
#define MENU_ENTER_HOLOCODE   4
#define MENU_STATS            5
#define MENU_INFO_BASE        6
#define MENU_ABORT            7

/* Cursor geometry from disp_caret_in_menu:  y = 68 + menu_choice*12 */
#define MENU_CARET_Y0   68
#define MENU_LINE_H     12

/*
 * Weapon image atlas: assets/gfx/intex_weapons_320x264.raw
 * Each weapon image: 160x88 px. Blit destination: (80, 116).
 * Ref: weapons_pic_table, lea bitplanes+(116*40)+10,a1 @ intex.asm
 */
#define INTEX_WEAPON_IMG_W    160
#define INTEX_WEAPON_IMG_H     88
#define INTEX_WEAPON_ATLAS_W  320
#define INTEX_WEAPON_ATLAS_H  264
#define INTEX_WEAPON_DST_X     80
#define INTEX_WEAPON_DST_Y    116

/* Weapon picture positions in atlas (row_px, col_px).
 * Intex weapon index 0..5 = TWINFIRE..LAZER (no MACHINEGUN here).
 * Ref: weapons_pic_table dc.l entries @ intex.asm */
static const int k_wpic_row_px[6] = { 176,  0,   0, 176,  88,  88 };
static const int k_wpic_col_px[6] = {   0,  0, 160, 160, 160,   0 };

/*
 * Purchasable weapon data.
 * Intex idx: 0=TWINFIRE, 1=FLAMEARC, 2=PLASMAGUN, 3=FLAMETHROWER,
 *            4=SIDEWINDERS, 5=LAZER.
 * Ref: text_weapon_1..6, weapons_prices_list @ intex.asm
 */
static const char * const k_weapon_name[6] = {
    "        BROADHURST DJ TWINFIRE 3LG      ",
    "             DALTON ARC FLAME           ",
    "           ROBINSON PLASMA GUN          ",
    "           RYXX FIREBOLT MK22           ",
    "            STIRLING MULTIMATIC         ",
    "          HIGH IMPACT ASTRO LAZER       ",
};
static const char * const k_weapon_type[6] = {
    "               RAPID FIRE               ",
    "                RAPID FIRE              ",
    "               PUMP ACTION              ",
    "              FLAMETHROWER              ",
    "               TRIPLE BARREL            ",
    "                  LAZER                 ",
};
static const char * const k_weapon_cost[6] = {
    "                         COST: 10000 CR ",
    "                         COST: 24000 CR ",
    "                         COST: 35000 CR ",
    "                         COST: 48000 CR ",
    "                         COST: 60000 CR ",
    "                         COST: 75000 CR ",
};
/* Display prices (CR). ASM internal ÷ 50 = display value. */
static const int k_weapon_prices[6] = { 10000, 24000, 35000, 48000, 60000, 75000 };
/* C WEAPON_* id for each intex weapon index */
static const int k_weapon_ids[6] = {
    WEAPON_TWINFIRE, WEAPON_FLAMEARC, WEAPON_PLASMAGUN,
    WEAPON_FLAMETHROWER, WEAPON_SIDEWINDERS, WEAPON_LAZER
};

/*
 * Tool supply data.
 * Items 0-4: purchasable. Item 5: EXIT.
 * Ref: text_tool_supplies, supplies_prices_list @ intex.asm#L579-L598
 * ASM internal prices / 50 = display prices used by C port (credits in display units).
 */
static const int k_tool_prices[5]       = { 10000, 2000, 5000, 5000, 30000 };
static const int k_tool_supply_flags[5] = {
    SUPPLY_MAP_OVERVIEW, SUPPLY_AMMO_CHARGE, SUPPLY_NRG_INJECT,
    SUPPLY_KEY_PACK, SUPPLY_EXTRA_LIFE
};

/* Shared image type */
typedef struct { UBYTE *pixels; int w, h; } IntexImg;

/*
 * Amiga blitter bitplane-OR simulation for INTEX text rendering.
 * The display_text blitter (BLTCON0 LF=$E2, copy_text_to_screen in intex.asm)
 * blits the font across 4 bitplanes. Where a font bitplane bit = 1 the output
 * bit = 1; where = 0 the background bit is preserved. The 5th screen bitplane
 * (bit4) is never touched by the font loop (only 4 bitplanes copied).
 *
 * Since the INTEX background is a 4-bitplane image (all pixel values 0-15,
 * so bit4 = 0 everywhere) and font glyph pixels have value 0x0F (all 4 lower
 * bitplanes = 1), the result is always palette index 15 (= COLOR15).
 *
 * COLOR15 is set to $0D0 by the copper at the first visible scanline
 * ($2C01,$FF00,COLOR15,$0D0 in intex.asm), giving bright green (#00DD00).
 *
 * bitplane_planes=4 in TextCtx triggers this simulation in typewriter_putchar.
 */
#define INTEX_FONT_BITPLANES  4

/*
 * Initialise a TextCtx for INTEX text rendering using bitplane-OR blending.
 */
static void intex_init_ctx(TextCtx *ctx, Font *font, int x, int y)
{
    typewriter_init_ctx(ctx, font, g_framebuffer, 320, x, y);
    ctx->bitplane_planes = INTEX_FONT_BITPLANES;
}

/* -----------------------------------------------------------------------
 * Startup / helper utilities
 * ----------------------------------------------------------------------- */
static int s_startup_interrupted = 0;

static void intex_mess_up(int n, const IntexImg *bg)
{
    if (s_startup_interrupted) return;
    for (int i = 0; i < n; i++) {
        input_poll();
        if (g_player1_input & (INPUT_FIRE1 | INPUT_FIRE2)) {
            s_startup_interrupted = 1;
            return;
        }
        int jitter = (rand() % 80) - 40;
        video_clear();
        if (bg->pixels)
            video_blit(bg->pixels, bg->w, 0, jitter, bg->w, bg->h, -1);
        video_present();
        timer_begin_frame();
    }
}

static void intex_wait_frames(int n_secs)
{
    if (s_startup_interrupted) return;
    int frames = n_secs * TARGET_FPS;
    for (int i = 0; i < frames; i++) {
        input_poll();
        if (g_player1_input & (INPUT_FIRE1 | INPUT_FIRE2)) {
            s_startup_interrupted = 1;
            return;
        }
        timer_begin_frame();
    }
}

/*
 * Render a NULL-terminated array of text lines at (x, y_start).
 * Y advances by font->letter_h (= 12) per line, matching intex.asm
 * display_text Y advance (add.l TEXT_LETTER_HEIGHT(a1),d1, TEXT_LETTER_HEIGHT=12).
 * Text is blitted using bitplane-OR (LF=$E2): font pixels (value 0x0F) become
 * palette index 15 = COLOR15 = $0D0 = bright green.
 * Ref: display_text in intex.asm.
 */
static void intex_display_lines(Font *font, int x, int y_start,
                                 const char * const *lines)
{
    TextCtx ctx;
    intex_init_ctx(&ctx, font, x, y_start);
    for (int i = 0; lines[i]; i++) {
        ctx.cursor_x = x;
        typewriter_display(&ctx, lines[i]);
        ctx.cursor_y += font->letter_h;
    }
}

/*
 * Highlight a selected row: fill background with color_bg then re-render
 * text so it appears brighter (font pixels OR'd over colored fill).
 * Ref: disp_caret_* in intex.asm positions caret sprite at selected row Y.
 */
static void intex_highlight_line(Font *font, int x, int y,
                                  const char *line, UBYTE color_bg)
{
    video_fill_rect(0, y, 320, font->letter_h, color_bg);
    TextCtx ctx;
    intex_init_ctx(&ctx, font, x, y);
    typewriter_display(&ctx, line);
}

/*
 * Show a short message at (x,y), wait 1 second, then return.
 * Used for "INSUFFUCIENT FUNDS", "ALREADY PURCHASED", etc.
 */
static void intex_flash_message(Font *font, int x, int y, const char *msg,
                                 const IntexImg *bg)
{
    video_clear();
    if (bg->pixels)
        video_blit(bg->pixels, bg->w, 0, 0, bg->w, bg->h, -1);
    TextCtx ctx;
    intex_init_ctx(&ctx, font, x, y);
    typewriter_display(&ctx, msg);
    video_present();
    intex_wait_frames(1);
    s_startup_interrupted = 0;
}

/* -----------------------------------------------------------------------
 * Startup sequence
 * Ref: display_intex_startup_seq in intex.asm
 * ----------------------------------------------------------------------- */
static void intex_startup_seq(const IntexImg *bg, Font *font)
{
    static const char * const k_connecting[] = {
        "INTEX NETWORK CONNECT: CODE ABF01DCC61",
        "CONNECTING.....................",
        NULL
    };
    static const char * const k_system_status[] = {
        "INTEX NETWORK SYSTEM V10.1",
        "2.5G RAM:         OK",
        "EXTERNAL DEVICE:  OK",
        "SYSTEM V1.32 CS:  OK",
        "VIDEODISPLAY:     CCC2.A2",
        "TEXT UPDATE ACCCELERATOR INSTALLED.",
        NULL
    };
    static const char * const k_downloading[] = {
        "EXECUTING DOS 6.0",
        "SYSTEM DOWNLOADING NETWORK DATA.... OK",
        "INTEX EXECUTED!",
        NULL
    };

    s_startup_interrupted = 0;
    intex_mess_up(10, bg);
    intex_wait_frames(1);
    intex_mess_up(25, bg);
    intex_wait_frames(1);
    intex_mess_up(6, bg);

    video_clear();
    if (bg->pixels)
        video_blit(bg->pixels, bg->w, 0, 0, bg->w, bg->h, -1);

    if (!s_startup_interrupted) {
        intex_display_lines(font, 0, 48, k_connecting);
        video_present();
        intex_wait_frames(1);
    }
    if (!s_startup_interrupted) {
        intex_display_lines(font, 0, 84, k_system_status);
        video_present();
        intex_wait_frames(2);
    }
    if (!s_startup_interrupted) {
        intex_display_lines(font, 0, 168, k_downloading);
        video_present();
        intex_wait_frames(1);
    }

    s_startup_interrupted = 0;
    video_clear();
    if (bg->pixels)
        video_blit(bg->pixels, bg->w, 0, 0, bg->w, bg->h, -1);
    video_present();
}

/* -----------------------------------------------------------------------
 * Disconnecting sequence
 * Ref: scr_disconnecting — text_disconnecting dc.w 0,64
 * ----------------------------------------------------------------------- */
static void intex_disconnecting(const IntexImg *bg, Font *font)
{
    static const char * const k_disconn[] = {
        "  DISCONNECTING..............",
        NULL
    };
    video_clear();
    if (bg->pixels)
        video_blit(bg->pixels, bg->w, 0, 0, bg->w, bg->h, -1);
    intex_display_lines(font, 0, 64, k_disconn);
    video_present();
    intex_wait_frames(1);
    intex_mess_up(6, bg);
}

/* -----------------------------------------------------------------------
 * Sub-screen: INTEX WEAPON SUPPLIES
 * Ref: scr_weapons, user_change_weapon, user_select_buy_weapon @ intex.asm
 *
 * text_weapons layout (x=0, y=24, line_h=12):
 *   Row 0  y=24 : "         INTEX WEAPON SUPPLIES          "
 *   Row 1  y=36 : blank
 *   Row 2  y=48 : "   WEAPON SUPPLIES REQUEST:             "
 *   Rows 3-15   : blank
 *   Row 16 y=216: " PURCHASE?            YOUR CREDIT LIMIT "
 *   Row 17 y=228: "                        IS: NNNN CR    " (dynamic)
 *
 * text_weapon_N overlay (y=84, line_h=12):
 *   Row 0  y=84 : weapon full name
 *   Row 1  y=96 : weapon type
 *   Rows 2-7    : blank (skip 72 px)
 *   Row 8  y=180: cost line
 *   Rows 9-11   : blank (skip 36 px)
 *   Row 12 y=228: "             YES"  (OR-blends with credit row 17)
 *   Row 13 y=240: "             NO"
 *
 * Caret: YES y=228, NO y=240 (ref: disp_caret_in_weapons, buy_weapon_yes_no).
 * ----------------------------------------------------------------------- */
static void draw_weapons_layout(Font *font, int pidx, int cur_weapon,
                                 int yes_no, const UBYTE *wpic)
{
    Player *p = &g_players[pidx];
    TextCtx ctx;

    /* Static header rows 0-16 (text_weapons) */
    static const char * const k_header[] = {
        "         INTEX WEAPON SUPPLIES          ",
        "                                        ",
        "   WEAPON SUPPLIES REQUEST:             ",
        "                                        ",
        "                                        ",
        "                                        ",
        "                                        ",
        "                                        ",
        "                                        ",
        "                                        ",
        "                                        ",
        "                                        ",
        "                                        ",
        "                                        ",
        "                                        ",
        "                                        ",
        " PURCHASE?            YOUR CREDIT LIMIT ",
        NULL
    };
    intex_display_lines(font, 0, 24, k_header);

    /* Row 17 (y=228): dynamic credit limit */
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "                        IS: %ld CR", (long)p->credits);
        intex_init_ctx(&ctx, font, 0, 228);
        typewriter_display(&ctx, buf);
    }

    /* Weapon image at (INTEX_WEAPON_DST_X, INTEX_WEAPON_DST_Y) */
    if (wpic && cur_weapon >= 0 && cur_weapon < 6) {
        int sx = k_wpic_col_px[cur_weapon];
        int sy = k_wpic_row_px[cur_weapon];
        const UBYTE *src = wpic + ((size_t)sy * INTEX_WEAPON_ATLAS_W + (size_t)sx);
        video_blit(src, INTEX_WEAPON_ATLAS_W,
                   INTEX_WEAPON_DST_X, INTEX_WEAPON_DST_Y,
                   INTEX_WEAPON_IMG_W, INTEX_WEAPON_IMG_H, 0);
    }

    /* Weapon-specific text block (text_weapon_N, starting at y=84) */
    if (cur_weapon >= 0 && cur_weapon < 6) {
        /* Row 0 y=84: name */
        intex_init_ctx(&ctx, font, 0, 84);
        typewriter_display(&ctx, k_weapon_name[cur_weapon]);
        /* Row 1 y=96: type */
        ctx.cursor_x = 0; ctx.cursor_y += font->letter_h;
        typewriter_display(&ctx, k_weapon_type[cur_weapon]);
        /* Skip 6 blank rows (+72 px) → y=180 */
        ctx.cursor_y += 6 * font->letter_h;
        /* Row 8 y=180: cost */
        ctx.cursor_x = 0;
        typewriter_display(&ctx, k_weapon_cost[cur_weapon]);
        /* Row 12 y=228: YES (OR-blends with credit row at same Y) */
        ctx.cursor_x = 0; ctx.cursor_y = 228;
        typewriter_display(&ctx, "             YES                        ");
        /* Row 13 y=240: NO */
        ctx.cursor_x = 0; ctx.cursor_y = 240;
        typewriter_display(&ctx, "             NO                         ");
    }

    /* Highlight selected YES or NO row */
    if (yes_no == 1)
        intex_highlight_line(font, 0, 228, "             YES                        ", 8);
    else
        intex_highlight_line(font, 0, 240, "             NO                         ", 8);
}

static void run_screen_weapons(int pidx, Font *font,
                                const IntexImg *bg, const UBYTE *wpic)
{
    Player *p  = &g_players[pidx];
    int cur_wp = 0;      /* intex weapon index 0..5 */
    int yes_no = 0;      /* 0 = NO (default per ASM), 1 = YES */
    int debounce = 8;
    int running  = 1;

    while (running) {
        timer_begin_frame();
        input_poll();
        if (g_quit_requested) return;

        video_clear();
        if (bg->pixels)
            video_blit(bg->pixels, bg->w, 0, 0, bg->w, bg->h, -1);
        draw_weapons_layout(font, pidx, cur_wp, yes_no, wpic);
        video_present();

        if (debounce > 0) { debounce--; continue; }

        UWORD inp = g_player1_input;
        UWORD old = g_player1_old_input;

        /* L/R: change weapon (ref: user_change_weapon, $300=L, $003=R) */
        if ((inp & INPUT_LEFT) && !(old & INPUT_LEFT)) {
            cur_wp = (cur_wp - 1 + 6) % 6;
            yes_no = 0;
            audio_play_sample(SAMPLE_CARET_MOVE);
            debounce = 8;
        } else if ((inp & INPUT_RIGHT) && !(old & INPUT_RIGHT)) {
            cur_wp = (cur_wp + 1) % 6;
            yes_no = 0;
            audio_play_sample(SAMPLE_CARET_MOVE);
            debounce = 8;
        }
        /* UP: YES (ref: $100=UP → buy_weapon_yes_no=1) */
        else if ((inp & INPUT_UP) && !(old & INPUT_UP)) {
            yes_no = 1;
            audio_play_sample(SAMPLE_CARET_MOVE);
            debounce = 8;
        }
        /* DOWN: NO (ref: $001=DOWN → yes_no=0) */
        else if ((inp & INPUT_DOWN) && !(old & INPUT_DOWN)) {
            yes_no = 0;
            audio_play_sample(SAMPLE_CARET_MOVE);
            debounce = 8;
        }
        /* FIRE: confirm */
        else if (((inp & INPUT_FIRE1) && !(old & INPUT_FIRE1))
                 || g_key_pressed == KEY_RETURN) {
            if (yes_no == 0) {
                /* NO: exit weapons screen */
                running = 0;
            } else {
                int wid = k_weapon_ids[cur_wp];
                if (p->owned_weapons[wid - 1]) {
                    intex_flash_message(font, 0, 211,
                        "     YOU ALREADY OWN THAT WEAPON.", bg);
                } else if (p->credits < k_weapon_prices[cur_wp]) {
                    /* Note: intentional typo matches original ASM text */
                    intex_flash_message(font, 0, 211,
                        "              INSUFFUCIENT FUNDS.", bg);
                } else {
                    p->credits -= k_weapon_prices[cur_wp];
                    p->owned_weapons[wid - 1] = 1;
                    audio_play_sample(SAMPLE_CARET_MOVE);
                    /* Show "CREDITS DEBITED." at y=62 (ref: text_credits_debited) */
                    video_clear();
                    if (bg->pixels)
                        video_blit(bg->pixels, bg->w, 0, 0, bg->w, bg->h, -1);
                    draw_weapons_layout(font, pidx, cur_wp, yes_no, wpic);
                    {
                        TextCtx ctx2;
                        intex_init_ctx(&ctx2, font, 0, 62);
                        typewriter_display(&ctx2, "     CREDITS DEBITED.");
                    }
                    video_present();
                    intex_wait_frames(1);
                    s_startup_interrupted = 0;
                    yes_no = 0;
                }
                debounce = 8;
            }
        }
        /* ESC: exit */
        else if (g_key_pressed == KEY_ESC) {
            running = 0;
        }
    }
}

/* -----------------------------------------------------------------------
 * Sub-screen: INTEX TOOL SUPPLIES
 * Ref: scr_tool_supplies, disp_caret_in_tool_supplies @ intex.asm#L448-L608
 *
 * text_tool_supplies layout (x=0, y=36, line_h=12):
 *   Row 0  y=36 : "          INTEX TOOL SUPPLIES           "
 *   Row 1  y=48 : blank
 *   Row 2  y=60 : blank
 *   Row 3  y=72 : "    REMOTE LOCATION SCANNER  10000 CR   "  caret pos 0
 *   Row 4  y=84 : blank
 *   Row 5  y=96 : "    AMMO CHARGE               2000 CR   "  caret pos 1
 *   Row 6  y=108: blank
 *   Row 7  y=120: "    HIGH ENERGY INJECTION     5000 CR   "  caret pos 2
 *   Row 8  y=132: blank
 *   Row 9  y=144: "    KEY PACK                  5000 CR   "  caret pos 3
 *   Row 10 y=156: blank
 *   Row 11 y=168: "    ADDITIONAL LIFE          30000 CR   "  caret pos 4
 *   Row 12 y=180: blank
 *   Row 13 y=192: "                  EXIT                  "  caret pos 5
 *   Row 14 y=204: blank
 *   Row 15 y=216: "             CREDIT LIMIT: NNNN CR"
 *
 * Caret: y = caret_pos*24 + 72, x = 16.
 * After purchase: " BOUGHT." written at col 29 of item line.
 * Ref: intex.asm#L521-L533.
 * ----------------------------------------------------------------------- */
static const char * const k_item_lines[5] = {
    "    REMOTE LOCATION SCANNER  10000 CR   ",
    "    AMMO CHARGE               2000 CR   ",
    "    HIGH ENERGY INJECTION     5000 CR   ",
    "    KEY PACK                  5000 CR   ",
    "    ADDITIONAL LIFE          30000 CR   ",
};

static void render_item_line(Font *font, int item, int purchased, int y)
{
    TextCtx ctx;
    intex_init_ctx(&ctx, font, 0, y);
    if (purchased & (1 << item)) {
        char buf[41];
        memcpy(buf, k_item_lines[item], 40);
        buf[40] = '\0';
        /* overwrite col 29 with " BOUGHT." matching ASM text_tool_bought write */
        memcpy(buf + 29, " BOUGHT.", 8);
        typewriter_display(&ctx, buf);
    } else {
        typewriter_display(&ctx, k_item_lines[item]);
    }
}

static void draw_tool_layout(Font *font, int pidx, int caret_pos)
{
    Player *p = &g_players[pidx];

    /* Header */
    static const char * const k_hdr[] = {
        "          INTEX TOOL SUPPLIES           ",
        "                                        ",
        "                                        ",
        NULL
    };
    intex_display_lines(font, 0, 36, k_hdr);

    /* Item lines with optional BOUGHT marker */
    for (int i = 0; i < 5; i++)
        render_item_line(font, i, p->purchased_supplies, 72 + i * 24);

    /* EXIT at y=192 */
    {
        TextCtx ctx;
        intex_init_ctx(&ctx, font, 0, 192);
        typewriter_display(&ctx, "                  EXIT                  ");
    }

    /* Credit limit at y=216 */
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "             CREDIT LIMIT: %ld CR", (long)p->credits);
        TextCtx ctx;
        intex_init_ctx(&ctx, font, 0, 216);
        typewriter_display(&ctx, buf);
    }

    /* Highlight selected row (caret y = caret_pos*24 + 72) */
    {
        int hy = caret_pos * 24 + 72;
        if (caret_pos < 5) {
            char buf[41];
            memcpy(buf, k_item_lines[caret_pos], 40);
            buf[40] = '\0';
            if (p->purchased_supplies & (1 << caret_pos))
                memcpy(buf + 29, " BOUGHT.", 8);
            intex_highlight_line(font, 0, hy, buf, 8);
        } else {
            intex_highlight_line(font, 0, hy,
                "                  EXIT                  ", 8);
        }
    }
}

static void run_screen_tool_supplies(int pidx, Font *font,
                                      const IntexImg *bg)
{
    Player *p    = &g_players[pidx];
    int caret    = 0;
    int debounce = 8;
    int running  = 1;

    while (running) {
        timer_begin_frame();
        input_poll();
        if (g_quit_requested) return;

        video_clear();
        if (bg->pixels)
            video_blit(bg->pixels, bg->w, 0, 0, bg->w, bg->h, -1);
        draw_tool_layout(font, pidx, caret);
        video_present();

        if (debounce > 0) { debounce--; continue; }

        UWORD inp = g_player1_input;
        UWORD old = g_player1_old_input;

        /* UP (ref: $100 → subq caret) */
        if ((inp & INPUT_UP) && !(old & INPUT_UP)) {
            if (caret > 0) { caret--; audio_play_sample(SAMPLE_CARET_MOVE); }
            debounce = 8;
        }
        /* DOWN (ref: $001 → addq caret, cmp.l #5) */
        else if ((inp & INPUT_DOWN) && !(old & INPUT_DOWN)) {
            if (caret < 5) { caret++; audio_play_sample(SAMPLE_CARET_MOVE); }
            debounce = 8;
        }
        /* FIRE / RETURN */
        else if (((inp & INPUT_FIRE1) && !(old & INPUT_FIRE1))
                 || g_key_pressed == KEY_RETURN) {
            audio_play_sample(SAMPLE_CARET_MOVE);
            if (caret == 5) {
                /* EXIT (ref: cmp.l #5,supplies_caret_pos; beq return) */
                running = 0;
            } else if (p->purchased_supplies & (1 << caret)) {
                /* Already purchased — show message */
                intex_flash_message(font, 0, 232,
                    "                ALREADY PURCHASED.", bg);
            } else if (p->credits < k_tool_prices[caret]) {
                /* Insufficient funds (text_insufficient_funds_2 at y=232) */
                intex_flash_message(font, 0, 232,
                    "                INSUFFICIENT FUNDS.", bg);
            } else {
                p->credits -= k_tool_prices[caret];
                p->purchased_supplies |= (1 << caret);
                /* Apply effect — energy injection is a direct health add
                 * (ref: cmp.w #2,d0; bne add_player_health → add.l #32,player_health) */
                if (caret == 2) {
                    p->health = (WORD)(p->health + 32);
                    if (p->health > PLAYER_MAX_HEALTH)
                        p->health = PLAYER_MAX_HEALTH;
                } else {
                    player_collect_supply(p, k_tool_supply_flags[caret]);
                }
            }
            debounce = 8;
        }
        /* ESC */
        else if (g_key_pressed == KEY_ESC) {
            running = 0;
        }
    }
}

/* -----------------------------------------------------------------------
 * Sub-screen: INTEX RADAR SERVICE (minimap)
 * Ref: scr_map, plot_map @ intex.asm#L774-L870
 *
 * plot_map iterates d0=248→2 (inner, columns) and d1=202→2 (outer, rows)
 * reading the map backwards.  Each solid tile (attr==1) is drawn as a 2×2
 * pixel block at screen (d0+32, d1+28) in the bitplane.
 *
 * C port: MAP_COLS=120, MAP_ROWS=96, 2 px/tile.
 * Minimap at x=MINIMAP_DST_X, y=MINIMAP_DST_Y (240×192 px fits in screen).
 *
 * Player caret: player_pos_x>>3 + 29, player_pos_y>>3 + 24 (ref: scr_map L779-L786).
 * In C: tile_col = pos_x/16, tile_row = pos_y/16; dot at (40+col*2, 24+row*2).
 * ----------------------------------------------------------------------- */
#define MINIMAP_DST_X  40
#define MINIMAP_DST_Y  24

static void draw_radar_screen(Font *font, int pidx)
{
    /* text_map_system (y=12): header + 18 blank rows + footer at y=240 */
    static const char * const k_hdr[] = {
        "        INTEX MAPSYSTEM V2.0          ",
        NULL
    };
    intex_display_lines(font, 0, 12, k_hdr);
    {
        TextCtx ctx;
        intex_init_ctx(&ctx, font, 0, 240);
        typewriter_display(&ctx, "  YOU ARE LOCATED AT CURSORS POSITION!");
    }

    if (!g_cur_map.valid) return;

    /* Plot minimap tiles */
    for (int row = 0; row < MAP_ROWS; row++) {
        for (int col = 0; col < MAP_COLS; col++) {
            UBYTE attr = tilemap_attr(&g_cur_map, col, row);
            if (attr == 0) continue;

            int px = MINIMAP_DST_X + col * 2;
            int py = MINIMAP_DST_Y + row * 2;
            if (px < 0 || px + 1 >= 320 || py < 0 || py + 1 >= 256) continue;

            UBYTE color = (attr == 3) ? 5 : (attr == 1 ? 9 : 4);
            g_framebuffer[ py      * 320 + px    ] = color;
            g_framebuffer[ py      * 320 + px + 1] = color;
            g_framebuffer[(py + 1) * 320 + px    ] = color;
            g_framebuffer[(py + 1) * 320 + px + 1] = color;
        }
    }

    /* Player cursor dot (index 14 = white) */
    if (pidx >= 0 && pidx < MAX_PLAYERS) {
        int tc = g_players[pidx].pos_x / MAP_TILE_W;
        int tr = g_players[pidx].pos_y / MAP_TILE_H;
        int cx = MINIMAP_DST_X + tc * 2;
        int cy = MINIMAP_DST_Y + tr * 2;
        if (cx >= 0 && cx + 1 < 320 && cy >= 0 && cy + 1 < 256) {
            g_framebuffer[ cy      * 320 + cx    ] = 14;
            g_framebuffer[ cy      * 320 + cx + 1] = 14;
            g_framebuffer[(cy + 1) * 320 + cx    ] = 14;
            g_framebuffer[(cy + 1) * 320 + cx + 1] = 14;
        }
    }
}

static void run_screen_radar(int pidx, Font *font, const IntexImg *bg)
{
    int running = 1;
    while (running) {
        timer_begin_frame();
        input_poll();
        if (g_quit_requested) return;

        video_clear();
        if (bg->pixels)
            video_blit(bg->pixels, bg->w, 0, 0, bg->w, bg->h, -1);
        draw_radar_screen(font, pidx);
        video_present();

        UWORD inp = g_player1_input;
        UWORD old = g_player1_old_input;
        if (((inp & INPUT_FIRE1) && !(old & INPUT_FIRE1))
            || g_key_pressed == KEY_ESC) {
            running = 0;
        }
    }
}

/* -----------------------------------------------------------------------
 * Sub-screen: MISSION OBJECTIVE
 * Ref: scr_briefing @ intex.asm#L627-L634 —
 *   copy_bkgnd_pic; display briefing_text; wait_joy_button; rts.
 * briefing_text is a pointer set from main game (level briefing string).
 * In C port: k_level_defs[g_cur_level].briefing_text.
 * ----------------------------------------------------------------------- */
static void run_screen_briefing(Font *font, const IntexImg *bg)
{
    const char *brief = (g_cur_level >= 0 && g_cur_level < NUM_LEVELS)
                        ? k_level_defs[g_cur_level].briefing_text
                        : "NO MISSION DATA AVAILABLE.";

    static const char * const k_hdr[] = {
        "            MISSION OBJECTIVE           ",
        "                                        ",
        NULL
    };

    video_clear();
    if (bg->pixels)
        video_blit(bg->pixels, bg->w, 0, 0, bg->w, bg->h, -1);
    intex_display_lines(font, 0, 60, k_hdr);
    {
        TextCtx ctx;
        intex_init_ctx(&ctx, font, 0, 84);
        typewriter_display(&ctx, brief);
    }
    video_present();

    int running = 1;
    while (running) {
        timer_begin_frame();
        input_poll();
        if (g_quit_requested) return;
        UWORD inp = g_player1_input;
        UWORD old = g_player1_old_input;
        if (((inp & INPUT_FIRE1) && !(old & INPUT_FIRE1))
            || g_key_pressed == KEY_ESC) {
            running = 0;
        }
    }
}

/* -----------------------------------------------------------------------
 * Sub-screen: GAME STATISTICS
 * Ref: scr_stats in intex.asm
 * ----------------------------------------------------------------------- */
static void draw_screen_stats(int pidx, Font *font)
{
    Player *p = &g_players[pidx];
    char buf[80];
    TextCtx ctx;

    static const char * const k_hdr[] = {
        "           INTEX GAME STATISTICS        ",
        "                                        ",
        NULL
    };
    intex_display_lines(font, 0, 24, k_hdr);

    intex_init_ctx(&ctx, font, 0, 48);
    snprintf(buf, sizeof(buf), " SCORE:          %ld", (long)p->score);
    typewriter_display(&ctx, buf);
    ctx.cursor_x = 0; ctx.cursor_y += font->letter_h;

    snprintf(buf, sizeof(buf), " ALIENS KILLED:  %d", p->aliens_killed);
    typewriter_display(&ctx, buf);
    ctx.cursor_x = 0; ctx.cursor_y += font->letter_h;

    snprintf(buf, sizeof(buf), " SHOTS FIRED:    %d", p->shots);
    typewriter_display(&ctx, buf);
    ctx.cursor_x = 0; ctx.cursor_y += font->letter_h * 2;

    typewriter_display(&ctx, " WEAPONS OWNED:");
    ctx.cursor_x = 0; ctx.cursor_y += font->letter_h;

    static const char * const k_wnames[6] = {
        "TWINFIRE", "FLAMEARC", "PLASMAGUN",
        "FLAMETHROWER", "SIDEWINDERS", "LAZER"
    };
    for (int i = 0; i < 6; i++) {
        int wid = k_weapon_ids[i];
        snprintf(buf, sizeof(buf), "  %-16s %s",
                 k_wnames[i], p->owned_weapons[wid - 1] ? "YES" : "NO");
        typewriter_display(&ctx, buf);
        ctx.cursor_x = 0; ctx.cursor_y += font->letter_h;
    }

    ctx.cursor_y += font->letter_h;
    snprintf(buf, sizeof(buf), " CREDITS:        %ld CR", (long)p->credits);
    typewriter_display(&ctx, buf);
}

/* -----------------------------------------------------------------------
 * intex_run — main entry point
 * ----------------------------------------------------------------------- */
void intex_run(int player_idx)
{
    audio_play_sample(SAMPLE_INTEX_SHUTDOWN);
    audio_pause_music();

    /* Load assets */
    IntexImg bg = {NULL, 0, 0};
    {
        FILE *f = fopen("assets/gfx/intex_bkgnd_320x256.raw", "rb");
        if (f) {
            fread(&bg.w, 4, 1, f); fread(&bg.h, 4, 1, f);
            bg.pixels = (UBYTE *)malloc((size_t)(bg.w * bg.h));
            if (bg.pixels) fread(bg.pixels, 1, (size_t)(bg.w * bg.h), f);
            fclose(f);
        }
    }

    IntexImg wpic = {NULL, 0, 0};
    {
        FILE *fw = fopen("assets/gfx/intex_weapons_320x264.raw", "rb");
        if (fw) {
            fread(&wpic.w, 4, 1, fw); fread(&wpic.h, 4, 1, fw);
            if (wpic.w == INTEX_WEAPON_ATLAS_W && wpic.h == INTEX_WEAPON_ATLAS_H) {
                wpic.pixels = (UBYTE *)malloc((size_t)wpic.w * (size_t)wpic.h);
                if (wpic.pixels)
                    fread(wpic.pixels, 1, (size_t)wpic.w * (size_t)wpic.h, fw);
            }
            fclose(fw);
        }
    }

    /*
     * Load font with letter_w=7 → advance = 7+1 = 8 px per character.
     * intex.asm display_text uses TEXT_LETTER_WIDTH=8 (no extra +1 over
     * character width), which the typewriter loop models as lw+1 = 8 when lw=7.
     * Previously lw=8 gave advance=9, shifting text right by 1 px per char.
     * Ref: font_struct dc.l ...,8,12,... (TEXT_LETTER_WIDTH=8, HEIGHT=12).
     */
    Font font = {0};
    font_load(&font, "assets/fonts/font_16x504.raw", 7, 12, 0);

    /* Set INTEX palette (ref: set_bitplanes_and_palette in intex.asm) */
    /*
     * INTEX palette (ref: set_bitplanes_and_palette + copper raster, intex.asm).
     *
     * Colors 0-13: linear green ramp from background_pic palette ($000-$0D0).
     * Color 14:    $FFF (bright highlight, from background_pic palette).
     * Color 15:    TEXT color.  In the original the copper raster raises this
     *              dynamically per scanline: $0D0 at y=44, $2F2 at y=92, $6F6
     *              at y=116, $4F4 at y=138 (copper raster in intex.asm).
     *              The C port uses static $3F3 (bright green, clearly visible
     *              against the $0D0 background maximum) for all menu rows.
     * Color 31:    reserved for the pulsing caret block (updated each frame).
     * Colors 16-30: static green gradient from copper list (upper bitplane 4).
     */
    static const UWORD k_intex_pal[] = {
        0x000, 0x010, 0x020, 0x030, 0x040, 0x050, 0x060, 0x070,
        0x080, 0x090, 0x0A0, 0x0B0, 0x0C0, 0x0D0, 0xFFF, 0x3F3,
        0x555, 0x565, 0x575, 0x585, 0x595, 0x5A5, 0x5B5, 0x5C5,
        0x5D5, 0x5E5, 0x5F5, 0x4F4, 0x3F3, 0x2F2, 0x1F1, 0x0F0
    };
    palette_set_immediate(k_intex_pal, 32);

    intex_startup_seq(&bg, &font);

    /* ------------------------------------------------------------------
     * Main menu loop
     * Ref: main_loop, text_main_menu (dc.w 0,32) @ intex.asm
     *
     * text_main_menu layout (y=32, line_h=12):
     *   y= 32  "            INTEX MAIN MENU             "  header
     *   y= 44  blank
     *   y= 56  blank
     *   y= 68  "         INTEX WEAPON SUPPLIES          "  choice 0
     *   y= 80  "          INTEX TOOL SUPPLIES           "  choice 1
     *   y= 92  "          INTEX RADAR SERVICE           "  choice 2
     *   y=104  "           MISSION OBJECTIVE            "  choice 3
     *   y=116  "            ENTER HOLOCODE              "  choice 4
     *   y=128  "            GAME STATISTICS             "  choice 5
     *   y=140  "               INFO BASE                "  choice 6
     *   y=152  "          ABORT INTEX NETWORK           "  choice 7
     * ------------------------------------------------------------------ */
    static const char * const k_menu_text[] = {
        "            INTEX MAIN MENU             ",
        "                                        ",
        "                                        ",
        "         INTEX WEAPON SUPPLIES          ",
        "          INTEX TOOL SUPPLIES           ",
        "          INTEX RADAR SERVICE           ",
        "           MISSION OBJECTIVE            ",
        "            ENTER HOLOCODE              ",
        "            GAME STATISTICS             ",
        "               INFO BASE                ",
        "          ABORT INTEX NETWORK           ",
        NULL
    };

    /*
     * Pulsing caret colour table — matches caret_color_table in intex.asm:
     *   $040→$0F0→$040, advances every 2 VBL (slowdown_caret_flash=2).
     * Palette entry 31 is dedicated to the caret and updated each frame.
     */
    static const UWORD k_caret_colors[] = {
        0x040, 0x050, 0x060, 0x070, 0x080, 0x090, 0x0A0, 0x0B0,
        0x0C0, 0x0D0, 0x0E0, 0x0F0, 0x0E0, 0x0D0, 0x0C0, 0x0B0,
        0x0A0, 0x090, 0x080, 0x070, 0x060, 0x050, 0x040
    };
    enum { CARET_N_COLORS = 23, CARET_PAL_IDX = 31 };

    int menu_choice = 0;
    int debounce    = 0;
    int running     = 1;
    int caret_slow  = 0;
    int caret_idx   = 0;

    while (running) {
        timer_begin_frame();
        input_poll();
        if (g_quit_requested) break;

        /* Advance pulsing caret colour every 2 frames (ref: slowdown_caret_flash=2) */
        if (++caret_slow >= 2) {
            caret_slow = 0;
            caret_idx  = (caret_idx + 1) % CARET_N_COLORS;
        }
        video_set_palette_entry(CARET_PAL_IDX, k_caret_colors[caret_idx]);

        video_clear();
        if (bg.pixels)
            video_blit(bg.pixels, bg.w, 0, 0, bg.w, bg.h, -1);

        /* Draw ALL menu lines with the same text colour (no per-line highlight).
         * Ref: display_text in intex.asm — every line uses the same blitter op;
         * the cursor position is set separately by disp_caret_in_menu. */
        for (int i = 0; k_menu_text[i]; i++) {
            TextCtx ctx;
            intex_init_ctx(&ctx, &font, 0, 32 + i * MENU_LINE_H);
            typewriter_display(&ctx, k_menu_text[i]);
        }

        /* Draw pulsing caret block at selected item.
         * Ref: disp_caret_in_menu: x=48, y=68+choice*12;
         *      caret_pic: %1111111100000000 × 11 rows → 8 px wide, 11 px tall. */
        {
            int cy = MENU_CARET_Y0 + menu_choice * MENU_LINE_H;
            video_fill_rect(48, cy, 8, 11, CARET_PAL_IDX);
        }

        video_present();

        if (debounce > 0) { debounce--; continue; }

        if ((g_player1_input & INPUT_UP) && !(g_player1_old_input & INPUT_UP)) {
            if (menu_choice > 0) {
                menu_choice--;
                audio_play_sample(SAMPLE_CARET_MOVE);
            }
            debounce = 8;
        } else if ((g_player1_input & INPUT_DOWN) && !(g_player1_old_input & INPUT_DOWN)) {
            if (menu_choice < MENU_ABORT) {
                menu_choice++;
                audio_play_sample(SAMPLE_CARET_MOVE);
            }
            debounce = 8;
        } else if (((g_player1_input & INPUT_FIRE1) && !(g_player1_old_input & INPUT_FIRE1))
                   || g_key_pressed == KEY_RETURN) {

            switch (menu_choice) {
                case MENU_ABORT:
                    intex_disconnecting(&bg, &font);
                    running = 0;
                    break;

                case MENU_WEAPON_SUPPLIES:
                    run_screen_weapons(player_idx, &font, &bg, wpic.pixels);
                    debounce = 8;
                    break;

                case MENU_TOOL_SUPPLIES:
                    run_screen_tool_supplies(player_idx, &font, &bg);
                    debounce = 8;
                    break;

                case MENU_RADAR_SERVICE:
                    run_screen_radar(player_idx, &font, &bg);
                    debounce = 8;
                    break;

                case MENU_MISSION_OBJ:
                    run_screen_briefing(&font, &bg);
                    debounce = 8;
                    break;

                case MENU_STATS: {
                    int sub = 1;
                    while (sub) {
                        timer_begin_frame();
                        input_poll();
                        if (g_quit_requested) { running = 0; sub = 0; break; }
                        video_clear();
                        if (bg.pixels)
                            video_blit(bg.pixels, bg.w, 0, 0, bg.w, bg.h, -1);
                        draw_screen_stats(player_idx, &font);
                        video_present();
                        if (((g_player1_input & INPUT_FIRE1)
                             && !(g_player1_old_input & INPUT_FIRE1))
                            || g_key_pressed == KEY_ESC) {
                            sub = 0;
                        }
                    }
                    debounce = 8;
                    break;
                }

                default:
                    debounce = 8;
                    break;
            }
        } else if (g_key_pressed == KEY_ESC) {
            intex_disconnecting(&bg, &font);
            running = 0;
        }
    }

    /* Restore game palette */
    if (g_cur_map.valid)
        palette_set_immediate(g_cur_map.palette_a, 32);

    audio_resume_music();
    audio_play_sample(SAMPLE_INTEX_SHUTDOWN);

    free(bg.pixels);
    free(wpic.pixels);
    font_free(&font);
}
