/*
 * Alien Breed SE 92 - C port
 * Ending screen — translated from src/end/end.asm
 *
 * Shows background + scrolling victory text, then waits for fire.
 */

#include "end.h"
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

static const char *k_end_text =
    "CONGRATULATIONS!\n\n"
    "YOU HAVE DEFEATED THE ALIEN MENACE\n"
    "AND SAVED STATION SIGMA SIX\n\n"
    "THE GALAXY IS SAFE... FOR NOW.\n\n"
    "SPECIAL OPERATIONS SQUAD\n"
    "RETURNS HOME AS HEROES\n\n\n"
    "THANK YOU FOR PLAYING\n"
    "ALIEN BREED SPECIAL EDITION 92";

void end_run(void)
{
    audio_stop_music();
    audio_play_music("title");

    typedef struct { UBYTE *pixels; int w, h; } Img;
    Img bg = {NULL, 0, 0};
    VFile *f = vfs_open("assets/gfx/end_bkgnd_320x256.raw");
    if (f) {
        vfs_read(&bg.w, 4, 1, f); vfs_read(&bg.h, 4, 1, f);
        bg.pixels = (UBYTE *)malloc((size_t)(bg.w * bg.h));
        if (bg.pixels) vfs_read(bg.pixels, 1, (size_t)(bg.w * bg.h), f);
        vfs_close(f);
    }

    Img scroll_bg = {NULL, 0, 0};
    f = vfs_open("assets/gfx/end_scroll_320x1024.raw");
    if (f) {
        vfs_read(&scroll_bg.w, 4, 1, f); vfs_read(&scroll_bg.h, 4, 1, f);
        scroll_bg.pixels = (UBYTE *)malloc((size_t)(scroll_bg.w * scroll_bg.h));
        if (scroll_bg.pixels) vfs_read(scroll_bg.pixels, 1, (size_t)(scroll_bg.w * scroll_bg.h), f);
        vfs_close(f);
    }

    Font font = {0};
    font_load(&font, "assets/fonts/font_16x504.raw", 8, 12, 0);  /* TEXT_LETTER_WIDTH=8, same font as briefing/intex */

    /* From end.asm copperlist COLOR00-31 */
    static const UWORD k_end_pal[] = {
        0x000, 0xAAA, 0x222, 0x332, 0x333, 0x444, 0x543, 0x555,
        0x765, 0x666, 0x877, 0xA87, 0x999, 0x111, 0xDDD, 0xFFF,
        0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF,
        0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF
    };
    static UWORD cur_pal[32] = {0};
    palette_prep_fade_in(k_end_pal, cur_pal, 32);

    int scroll_y = 256;
    int timeout  = 50 * 45;  /* 45 seconds max */

    while (timeout-- > 0) {
        timer_begin_frame();
        input_poll();

        if (g_quit_requested) break;
        if ((g_player1_input & INPUT_FIRE1)) break;

        video_clear();

        /* Draw scrolling star background (tile vertically using video_blit) */
        if (scroll_bg.pixels) {
            int src_y = (-(scroll_y)) % scroll_bg.h;
            if (src_y < 0) src_y += scroll_bg.h;
            /* First strip from src_y downward */
            int first_h = scroll_bg.h - src_y;
            if (first_h > 256) first_h = 256;
            /* We blit a horizontal section: use pointer arithmetic + video_blit */
            video_blit(scroll_bg.pixels + src_y * scroll_bg.w,
                       scroll_bg.w, 0, 0, 320, first_h, -1);
            /* Wrap-around strip at top */
            if (first_h < 256) {
                video_blit(scroll_bg.pixels,
                           scroll_bg.w, 0, first_h, 320, 256 - first_h, -1);
            }
        } else if (bg.pixels) {
            video_blit(bg.pixels, bg.w, 0, 0, 320, 256, -1);
        }

        /* Scroll victory text */
        if (font.pixels) {
            TextCtx ctx;
            typewriter_init_ctx(&ctx, &font, g_framebuffer, 320, 20, scroll_y);
            typewriter_display(&ctx, k_end_text);
        }
        scroll_y--;
        if (scroll_y < -400) break;

        palette_tick();
        video_present();
    }

    static UWORD k_black[32] = {0};
    palette_prep_fade_to_rgb(k_black, cur_pal, 32);
    for (int i = 0; i < 30; i++) {
        timer_begin_frame();
        palette_tick();
        video_present();
    }

    audio_stop_music();
    free(bg.pixels);
    free(scroll_bg.pixels);
    font_free(&font);
}
