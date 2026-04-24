#ifndef AB_STORY_H
#define AB_STORY_H
/* Run only the title screen (display_title_screen + display_beam_title).
 * Reproduces the standalone title executable from the original CD32 game.
 * Call once at startup, before the menu loop. */
void story_title_run(void);
/* Run the full story sequence: planet + scrolling text, then title screen.
 * Matches story.asm. Call on auto-exit (credits exhausted) from the menu. */
void story_run(void);
#endif
