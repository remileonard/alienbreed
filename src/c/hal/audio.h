#ifndef AB_AUDIO_H
#define AB_AUDIO_H

/*
 * Alien Breed SE 92 - C port
 * Audio HAL — replaces Paula DMA channels and Soundmon player.
 *
 * Original: AUD0LCH–AUD3LCH registers, lev5irq → bpmusic + start_audio_channel_4
 * Replacement: SDL2_mixer
 *   - Music  : MOD/XM files (Soundmon modules converted with soundmon2mod)
 *   - Samples: WAV files (raw 8-bit PCM converted with convert_audio tool)
 *   - Voices : WAV files (same conversion)
 *
 * Sample files live in  assets/samples/ and assets/voices/.
 * Music files live in   assets/music/.
 */

#include "../types.h"

/* Maximum number of samples/voices that can be loaded */
#define AUDIO_MAX_SAMPLES  128

/* Initialise SDL2_mixer. Returns 0 on success. */
int  audio_init(void);

/* Shut down SDL2_mixer and free all loaded clips. */
void audio_quit(void);

/* Load all samples from assets/samples/ and assets/voices/.
 * Must be called after audio_init(). */
int  audio_load_all(void);

/* Play a sound effect by sample ID (SAMPLE_* or VOICE_* constants).
 * Non-blocking. Multiple samples can play simultaneously. */
void audio_play_sample(int sample_id);

/* Stop all currently playing sound effects. */
void audio_stop_samples(void);

/*
 * Play a sample in a continuous loop on a dedicated channel.
 * Only one looping sample can be active at a time.
 * Designed for the self-destruct alarm: plays until audio_stop_looping() is
 * called.  The loop is automatically stopped by audio_stop_samples().
 */
void audio_play_looping(int sample_id);

/* Stop the currently looping sample. Safe to call when nothing is looping. */
void audio_stop_looping(void);

/* Start playing a music track by filename (without path or extension).
 *   name: "title", "level", "boss"
 * Loops indefinitely. */
void audio_play_music(const char *name);

/* Stop the currently playing music. */
void audio_stop_music(void);

/* Set music volume (0–128, SDL_mixer scale). */
void audio_set_music_volume(int vol);

/* Pause/resume music (e.g. when entering INTEX or pause screen). */
void audio_pause_music(void);
void audio_resume_music(void);

/* Global music enabled flag (mirrors music_enabled from main.asm) */
extern int g_music_enabled;

#endif /* AB_AUDIO_H */
