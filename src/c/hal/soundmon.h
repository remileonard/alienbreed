#ifndef AB_SOUNDMON_H
#define AB_SOUNDMON_H

/*
 * Soundmon V2 player for Alien Breed SE 92 - C port
 *
 * Implements the bpmusic/bpnext/bpsynth/bpplayit routines from main.asm.
 * Drives 4 virtual Paula channels and mixes into SDL_mixer via Mix_HookMusic.
 *
 * File format (from main.asm bpinit):
 *   [0..511]      Header: 32-B main header + 15 × 32-B instrument descriptors
 *   [512..]       Step table: num_steps × 4 voices × 4 B
 *                   each voice entry: word pat_num, byte st, byte tr
 *   [512+S..]     Pattern data: max_pat × 48 B (16 rows × 3 B/row)
 *                   row bytes: note, instr_eff (hi=instr, lo=effect), param
 *   [512+S+P..]   Wavetables: num_tables × 64 B signed PCM oscillator cycles
 *   [512+S+P+W..] Sample PCM data: up to 15 instruments, variable length
 *
 * Instrument descriptor (32 bytes, real sample):
 *   [0-23]  name (null-terminated)
 *   [24-25] sample length (words)
 *   [26-27] loop start byte offset from sample start
 *   [28-29] loop length (words), 1 = no loop
 *   [30-31] native volume (0-64)
 *
 * Instrument descriptor (32 bytes, synthetic, byte 0 == 0xFF):
 *   [0]     0xFF  (synthetic marker)
 *   [1]     wavetable index
 *   [2-3]   waveform length (words) → AUD_LEN
 *   [4]     ADSR enabled flag
 *   [5]     ADSR table index
 *   [6-7]   ADSR table length (rows before wrap)
 *   [8]     ADSR counter speed (reload value)
 *   [9]     LFO enabled flag
 *   [10]    LFO table index
 *   [11]    LFO divisor (0 = no divide)
 *   [12-13] LFO table length
 *   [14-15] LFO initial counter value
 *   [16]    LFO counter speed (reload value)
 *   [17]    EG enabled flag
 *   [18]    EG table index
 *   [19]    EG initial eg_value
 *   [20-21] EG table length
 *   [22-23] EG initial counter value
 *   [24]    EG counter speed (reload value)
 *   [25]    native volume
 */

#include <stdint.h>
#include <stddef.h>
#include <SDL2/SDL.h>

/* Opaque module handle */
typedef struct SM_Module SM_Module;

/* Load a .soundmon file from disk.  Returns NULL on error. */
SM_Module *sm_load(const char *path);

/* Free a loaded module (stops playback first if needed). */
void sm_free(SM_Module *m);

/* Start/stop playback.  sm_play also installs Mix_HookMusic. */
void sm_play(SM_Module *m, int output_rate);
void sm_stop(SM_Module *m);

/* Pause / resume without unloading. */
void sm_pause(SM_Module *m);
void sm_resume(SM_Module *m);

/* Mix_HookMusic callback — called by the SDL audio thread. */
void sm_mix_callback(void *udata, Uint8 *stream, int len);

#endif /* AB_SOUNDMON_H */
