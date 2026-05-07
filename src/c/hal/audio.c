/*
 * Alien Breed SE 92 - C port
 * Audio HAL implementation
 *
 * Music is played via the native Soundmon V2 player (soundmon.c).
 * SDL2_mixer Mix_HookMusic is used to feed the mixed audio stream.
 */

#include "audio.h"
#include "soundmon.h"
#include "vfs.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_music_enabled = 0;

static Mix_Chunk  *s_samples[AUDIO_MAX_SAMPLES];
static SM_Module  *s_music  = NULL;
static int         s_mix_rate = 22050;
static char        s_current_music_name[256] = "";

/* -----------------------------------------------------------------------
 * Per-sample parameters derived from ASM samples_table (main.asm#L18049).
 * Each ASM entry: dc.l ptr ; dc.w len, vol, per, loop, dur, pitch_ramp, vol_ramp
 *
 *   vol        : Paula volume 0–64  →  SDL volume 0–128 (× 2)
 *   per        : Paula period (playback pitch; kept for reference, pitch ramp NYI)
 *   loop       : 1 = loop indefinitely (looping samples skip the dur countdown)
 *   dur        : auto-stop after this many 50 Hz ticks  (0 = unlimited)
 *   vol_ramp   : word: high byte = enable flag, low byte = signed step.
 *                  step < 0  → fade-out: subtract 8 Paula units every abs(step) ticks
 *                  step > 0  → fade-in:  add      8 Paula units every    step  ticks
 *                  (mirrors lbC024142 vol-ramp branch, ±8 Paula = ±16 SDL units)
 *   pitch_ramp : word: same encoding; direction: step < 0 → pitch up  (per−50),
 *                  step > 0 → pitch down (per+50). Implemented via
 *                  Mix_RegisterEffect: a silent looping chunk holds the SDL
 *                  channel while pitch_effect_cb resamples the raw PCM at
 *                  ratio per_initial/per_current each audio buffer.
 * ----------------------------------------------------------------------- */
typedef struct {
    int vol;           /* 0–64 Paula scale                     */
    int per;           /* Paula period (pitch, informational)  */
    int loop;          /* 1 = loop indefinitely                */
    int dur;           /* duration in 50 Hz ticks (0=unlimited)*/
    int vol_ramp_en;   /* non-zero → vol ramp active           */
    int vol_ramp_step; /* signed; <0=fade-out, >0=fade-in      */
    int pitch_ramp_en; /* non-zero → pitch ramp active           */
    int pitch_ramp_step;/* signed; <0=pitch-up, >0=pitch-down  */
} SampleParams;

/*
 * Table indexed by sample ID (0–76); remaining entries are zero-initialised.
 * Columns: vol, per, loop, dur, vol_re, vol_rs, pit_re, pit_rs
 *   vol_re / pit_re = ramp-enable (high byte of ASM ramp word)
 *   vol_rs / pit_rs = signed step (low byte of ASM ramp word cast to Sint8)
 *
 * Ramp word decoding reference (examples):
 *   508 = 0x01FC → en=1, step=0xFC=−4  (fade-out every 4 ticks)
 *   506 = 0x01FA → en=1, step=0xFA=−6  (fade-out every 6 ticks)
 *   511 = 0x01FF → en=1, step=0xFF=−1  (fade-out every 1 tick)
 *   504 = 0x01F8 → en=1, step=0xF8=−8  (fade-out every 8 ticks)
 *   510 = 0x01FE → en=1, step=0xFE=−2  (fade-out every 2 ticks)
 *   260 = 0x0104 → en=1, step=  4      (fade-in  every 4 ticks)
 *   257 = 0x0101 → en=1, step=  1      (pitch-dn every 1 tick)
 *   272 = 0x0110 → en=1, step= 16      (pitch-dn every 16 ticks)
 *   494 = 0x01EE → en=1, step=0xEE=−18 (fade-out every 18 ticks)
 */
static const SampleParams k_sample_params[AUDIO_MAX_SAMPLES] = {
/* ID  vol   per  lp  dur   vol_ramp_en  vol_ramp_step  pitch_ramp_en  pitch_ramp_step */
/*  0 */{ 62, 428, 0,  28,   0,    0,   0,    0  },
/*  1 */{ 32, 428, 0,  10,   1,   -4,   1,    1  },
/*  2 */{ 32, 284, 0,  31,   0,    0,   0,    0  },
/*  3 */{ 32, 284, 0,   8,   0,    0,   0,    0  },
/*  4 */{ 64, 284, 0,  30,   0,    0,   0,    0  },
/*  5 */{ 16, 284, 0,  15,   0,    0,   0,    0  },
/*  6 */{ 48, 120, 0,  29,   1,   -4,   1,    1  },
/*  7 */{ 16, 120, 0,  10,   1,   -4,   1,    1  },
/*  8 */{ 32, 140, 0,  40,   1,   -4,   0,    0  },
/*  9 */{ 62,1400, 0,  38,   1,   -4,   0,    0  },
/* 10 */{ 64, 428, 0,  30,   0,    0,   0,    0  },
/* 11 */{ 64, 856, 0,  60,   0,    0,   0,    0  },
/* 12 */{ 63,1400, 0, 150,   1,  -18,   1,   16  },
/* 13 */{ 64, 856, 0,  36,   0,    0,   0,    0  },
/* 14 */{ 48, 428, 0,   4,   0,    0,   0,    0  },
/* 15 */{ 24, 428, 1,  10,   0,    0,   0,    0  },
/* 16 */{ 16, 428, 1,  10,   1,    4,   0,    0  },
/* 17 */{ 64, 428, 0,  33,   0,    0,   0,    0  },
/* 18 */{ 64, 428, 0,  80,   0,    0,   0,    0  },
/* 19 */{ 32, 540, 0,  30,   1,   -4,   0,    0  },
/* 20 */{ 64, 428, 0,  27,   0,    0,   0,    0  },
/* 21 */{ 59, 300, 0,  40,   1,   -4,   0,    0  },
/* 22 */{ 32, 480, 0,  35,   1,   -6,   0,    0  },
/* 23 */{ 64, 480, 0,  31,   0,    0,   0,    0  },
/* 24 */{ 64, 480, 0,  32,   0,    0,   0,    0  },
/* 25 */{ 32,1000, 1,  28,   0,    0,   0,    0  },
/* 26 */{ 32,1000, 0,  40,   1,   -4,   1,    4  },
/* 27 */{ 60, 200, 0,  40,   1,   -4,   0,    0  },
/* 28 */{ 16, 900, 0,  12,   1,   -6,   0,    0  },
/* 29 */{ 22,2000, 0,  15,   1,   -6,   0,    0  },
/* 30 */{ 62, 180, 0,  15,   1,   -1,   0,    0  },
/* 31 */{ 62, 280, 0,  15,   1,   -1,   0,    0  },
/* 32 */{ 62, 400, 0,  16,   1,   -1,   1,   -4  },
/* 33 */{ 32, 480, 0,  10,   0,    0,   0,    0  },
/* 34 */{ 64, 428, 0,   6,   0,    0,   0,    0  },
/* 35 */{ 62, 900, 0,  50,   1,   -6,   0,    0  },
/* 36 */{ 62, 568, 0,  30,   0,    0,   0,    0  },
/* 37 */{ 64, 360, 0,   5,   0,    0,   0,    0  },
/* 38 */{ 62,1200, 0,   8,   0,    0,   0,    0  },
/* 39 */{  8,1400, 1,  26,   0,    0,   0,    0  },
/* 40 */{  8,1000, 0,  10,   1,   -4,   0,    0  },
/* 41 */{ 40,1000, 1,  10,   0,    0,   0,    0  },
/* 42 */{ 37,1000, 0,  61,   0,    0,   0,    0  },
/* 43 */{ 32, 202, 0,  22,   0,    0,   0,    0  },
/* 44 */{ 16, 202, 0,  18,   0,    0,   0,    0  },
/* 45 */{ 44, 190, 0,  38,   1,   -4,   1,    1  },
/* 46 */{ 16, 280, 0,  28,   1,   -4,   1,    1  },
/* 47 */{ 32, 480, 0,  10,   0,    0,   0,    0  },
/* 48 */{ 16, 428, 0,   4,   0,    0,   0,    0  },
/* 49 */{ 16, 600, 0,   4,   0,    0,   0,    0  },
/* 50 */{ 32, 404, 0,  33,   0,    0,   0,    0  },
/* 51 */{ 32, 404, 0,  20,   0,    0,   0,    0  },
/* 52 */{ 64, 400, 0,  33,   0,    0,   0,    0  },
/* 53 */{ 42, 480, 0,  60,   0,    0,   0,    0  },
/* 54 */{ 32, 470, 0,  18,   0,    0,   0,    0  },
/* 55 */{  4,1000, 0,  57,   1,    4,   0,    0  },
/* 56 */{ 62, 690, 0,  40,   1,   -8,   0,    0  },
/* 57 */{ 32, 440, 0,  25,   0,    0,   0,    0  },
/* 58 */{ 32, 390, 0,  31,   0,    0,   0,    0  },
/* 59 */{ 32, 440, 0,  21,   0,    0,   0,    0  },
/* 60 */{ 32, 450, 0,  43,   0,    0,   0,    0  },
/* 61 */{ 64, 480, 0,  28,   0,    0,   0,    0  },
/* 62 */{ 32, 440, 0,  44,   0,    0,   0,    0  },
/* 63 */{ 32, 440, 0,  24,   0,    0,   0,    0  },
/* 64 */{ 64, 428, 0,  36,   0,    0,   0,    0  },
/* 65 */{ 32, 400, 0,  18,   0,    0,   0,    0  },
/* 66 */{ 32, 380, 0,  20,   0,    0,   0,    0  },
/* 67 */{ 32, 380, 0,  20,   0,    0,   0,    0  },
/* 68 */{ 32, 380, 0,  20,   0,    0,   0,    0  },
/* 69 */{ 32, 380, 0,  24,   0,    0,   0,    0  },
/* 70 */{ 32, 380, 0,  20,   0,    0,   0,    0  },
/* 71 */{ 32, 380, 0,  24,   0,    0,   0,    0  },
/* 72 */{ 32, 380, 0,  20,   0,    0,   0,    0  },
/* 73 */{ 64, 600, 0,  25,   0,    0,   0,    0  },
/* 74 */{ 16, 500, 1,   0,   0,    0,   0,    0  },
/* 75 */{ 16, 500, 0,   0,   1,   -2,   0,    0  },
/* 76 */{  1, 428, 0,   0,   0,    0,   0,    0  },
/* 77–127: unused, zero-initialised by C default */
};

/* -----------------------------------------------------------------------
 * Per-channel runtime state for envelope processing (SFX channels 0–11).
 * Mirrors the 14-byte channel block used by lbC024142 in main.asm.
 * ----------------------------------------------------------------------- */
#define NUM_SFX_CHANNELS 12

typedef struct {
    int active;              /* 1 while the channel is playing           */
    int dur_remaining;       /* ticks until auto-stop  (0 = unlimited)   */
    int vol_ramp_en;         /* vol ramp enabled                         */
    int vol_ramp_raw_step;   /* original signed step from samples_table  */
    int vol_ramp_countdown;  /* working countdown; decrements each tick  */
    int vol_sdl;             /* current SDL channel volume   (0–128)     */
    /* Pitch ramp — real-time resampling via Mix_RegisterEffect.
     * A silent looping chunk occupies the SDL channel; the effect
     * overwrites it each audio buffer with pitch-shifted audio drawn
     * from a malloc'd copy of the original chunk PCM. */
    int          pitch_ramp_en;       /* non-zero → pitch ramp active              */
    int          pitch_ramp_raw_step; /* signed; <0 = pitch up, >0 = pitch down    */
    int          pitch_ramp_countdown;/* ticks until next per change               */
    int          per_initial;         /* Paula period at play time (from table)    */
    volatile int per_current_fp;      /* current per × 256; written by game thread */
    Uint8       *pitch_raw;           /* malloc'd copy of chunk PCM (freed on stop)*/
    Uint32       pitch_raw_len;       /* size of pitch_raw in bytes                */
    double       pitch_read_pos;      /* fractional frame index (audio thread only)*/
    volatile int pitch_done;          /* 1 when PCM exhausted (set by audio thread)*/
} ChannelState;

static ChannelState s_chan_state[16]; /* indexed by SDL channel number */

/* -----------------------------------------------------------------------
 * Silent looping chunk used as placeholder for pitch-ramp channels.
 * The pitch effect callback overwrites the silence with resampled audio.
 * Four zero bytes = one frame of silence in any S16 stereo format.
 * allocated=0 prevents SDL_mixer from freeing s_silence_buf.
 * ----------------------------------------------------------------------- */
static Uint8     s_silence_buf[4]  = {0, 0, 0, 0};
static Mix_Chunk s_silent_chunk    = {0, s_silence_buf, 4, MIX_MAX_VOLUME};

/* -----------------------------------------------------------------------
 * Pitch effect callback — registered via Mix_RegisterEffect on channels
 * that have pitch_ramp_en set.  Runs in the SDL audio thread.
 *
 * Receives a buffer pre-filled with silence (from s_silent_chunk) and
 * overwrites it with linearly-interpolated samples read from pitch_raw
 * at the speed ratio  per_initial / per_current.
 *
 *   ratio > 1  →  read faster  →  higher pitch  (per decreased)
 *   ratio < 1  →  read slower  →  lower  pitch  (per increased)
 *
 * Output format assumed: S16 signed, stereo (4 bytes per frame), which
 * matches Mix_OpenAudio(22050, MIX_DEFAULT_FORMAT, 2, ...).
 * ----------------------------------------------------------------------- */
static void pitch_effect_cb(int chan, void *stream, int len, void *udata)
{
    ChannelState *cs = (ChannelState *)udata;

    if (cs->pitch_done || !cs->pitch_raw) {
        memset(stream, 0, len);
        return;
    }

    int per_fp = cs->per_current_fp;
    if (per_fp <= 0) per_fp = cs->per_initial * 256;

    double ratio      = (double)(cs->per_initial * 256) / (double)per_fp;
    int    out_frames = len / 4;                          /* S16 stereo = 4 B/frame */
    int    raw_frames = (int)(cs->pitch_raw_len / 4);
    Sint16      *out  = (Sint16 *)stream;
    const Sint16 *raw = (const Sint16 *)cs->pitch_raw;

    for (int i = 0; i < out_frames; i++) {
        int idx0 = (int)cs->pitch_read_pos;
        if (idx0 >= raw_frames - 1) {
            /* PCM exhausted — zero-fill the rest and signal game thread */
            memset(out + i * 2, 0, (out_frames - i) * 4);
            cs->pitch_done = 1;
            return;
        }
        double frac = cs->pitch_read_pos - idx0;
        int    idx1 = idx0 + 1;
        /* Linear interpolation — stereo L then R */
        out[i * 2    ] = (Sint16)(raw[idx0 * 2    ]
                         + frac * (raw[idx1 * 2    ] - raw[idx0 * 2    ]));
        out[i * 2 + 1] = (Sint16)(raw[idx0 * 2 + 1]
                         + frac * (raw[idx1 * 2 + 1] - raw[idx0 * 2 + 1]));
        cs->pitch_read_pos += ratio;
    }
}

static void channel_state_clear(int ch)
{
    ChannelState *cs = &s_chan_state[ch];
    /* Free the pitch PCM copy if not already released by a prior HaltChannel */
    if (cs->pitch_raw) {
        free(cs->pitch_raw);
        cs->pitch_raw = NULL;
    }
    cs->active             = 0;
    cs->dur_remaining      = 0;
    cs->vol_ramp_en        = 0;
    cs->vol_ramp_raw_step  = 0;
    cs->vol_ramp_countdown = 0;
    cs->vol_sdl            = MIX_MAX_VOLUME;
    cs->pitch_ramp_en       = 0;
    cs->pitch_ramp_raw_step = 0;
    cs->pitch_ramp_countdown= 0;
    cs->per_initial         = 0;
    cs->per_current_fp      = 0;
    cs->pitch_raw_len       = 0;
    cs->pitch_read_pos      = 0.0;
    cs->pitch_done          = 0;
}

/* Maps sample ID to filename stem (assets/samples/ or assets/voices/) */
typedef struct { int id; const char *file; } SampleEntry;

static const SampleEntry k_sample_map[] = {
    /* Weapon fire sounds — from weapons_attr_table @ main.asm#L736.
     * Indices 0,2,3,4,6 map directly to the sample table entries. */
    {  0, "samples/sample1"            },  /* SAMPLE_WEAPON_PLASMAGUN  */
    {  2, "samples/sample2"            },  /* SAMPLE_WEAPON_FLAMEARC   */
    {  3, "samples/sample3"            },  /* SAMPLE_WEAPON_LAZER      */
    {  4, "samples/sample4"            },  /* SAMPLE_WEAPON_TWINFIRE / SIDEWINDERS */
    {  5, "samples/one_way_door"       },
    {  6, "samples/intex_noise"        },  /* SAMPLE_WEAPON_FLAMETHROWER */
    { 13, "samples/intex_shutdown"     },
    { 14, "samples/intex_beep"         },  /* caret move reuses beep */
    { 18, "samples/destruction_horn"   },
    { 20, "samples/dying_alien"        },  /* dying alien (variant A) */
    { 21, "samples/dying_alien"        },  /* dying alien (variant B, same file) */
    { 22, "samples/getting_key"        },
    { 23, "samples/opening_door"       },
    { 24, "samples/ammo"               },
    { 27, "samples/descent_end"        },  /* 1UP jingle */
    { 30, "samples/first_aid_and_credits" },
    { 33, "samples/hurt_player"        },
    { 34, "samples/acid_pool"          },
    { 35, "samples/water_pool"         },
    { 36, "samples/hatching_alien"     },
    { 37, "samples/fire_gun"           },
    { 41, "samples/descent"            },
    { 42, "samples/descent_end"        },
    { 47, "samples/reloading_weapon"   },  /* SAMPLE_RELOADING_WEAPON */
    { 48, "samples/intex_beep"         },  /* typewriter tick */
    { 73, "samples/dying_player"       },
    /* Voices */
    { 17, "voices/warning"             },
    { 18, "voices/destruction_imminent"},
    { 50, "voices/entering"            },
    { 51, "voices/zone"                },
    { 52, "voices/welcome_to"          },
    { 53, "voices/intex_systems"       },
    { 54, "voices/death"               },
    { 57, "voices/player"              },
    { 58, "voices/requires"            },
    { 59, "voices/ammo"                },
    { 60, "voices/first_aid"           },
    { 61, "voices/danger"              },
    { 62, "voices/insert_disk"         },
    { 63, "voices/keys"                },
    { 64, "voices/game_over"           },
    { 65, "voices/one"                 },
    { 66, "voices/two"                 },
    { 67, "voices/three"               },
    { 68, "voices/four"                },
    { 69, "voices/five"                },
    { 70, "voices/six"                 },
    { 71, "voices/seven"               },
    { 72, "voices/eight"               },
    { -1, NULL }
};

int audio_init(void)
{
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "SDL Audio init failed: %s\n", SDL_GetError());
        return -1;
    }

    /* 22050 Hz is a reasonable quality for 8-bit Amiga samples.
     * Original Paula rate was 8363 Hz; 22050 is the SDL_mixer default. */
    if (Mix_OpenAudio(22050, MIX_DEFAULT_FORMAT, 2, 1024) < 0) {
        fprintf(stderr, "Mix_OpenAudio failed: %s\n", Mix_GetError());
        return -1;
    }

    /* Retrieve the actual output rate negotiated by SDL_mixer */
    {
        int freq = 22050; int channels = 2; Uint16 fmt = 0;
        Mix_QuerySpec(&freq, &fmt, &channels);
        s_mix_rate = freq;
    }

    Mix_AllocateChannels(16);
    memset(s_samples, 0, sizeof(s_samples));
    memset(s_chan_state, 0, sizeof(s_chan_state));
    return 0;
}

void audio_quit(void)
{
    audio_stop_music();
    audio_stop_samples();
    for (int i = 0; i < AUDIO_MAX_SAMPLES; i++) {
        if (s_samples[i]) { Mix_FreeChunk(s_samples[i]); s_samples[i] = NULL; }
    }
    Mix_HookMusic(NULL, NULL);
    Mix_CloseAudio();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

int audio_load_all(void)
{
    char path[256];
    int loaded = 0;

    for (int i = 0; k_sample_map[i].id >= 0; i++) {
        int id = k_sample_map[i].id;
        if (id < 0 || id >= AUDIO_MAX_SAMPLES) continue;
        if (s_samples[id]) continue;  /* already loaded (shared IDs) */

        snprintf(path, sizeof(path), "assets/%s.wav", k_sample_map[i].file);
        SDL_RWops *rw = vfs_rwops(path);
        s_samples[id] = rw ? Mix_LoadWAV_RW(rw, 1) : NULL;
        if (!s_samples[id]) {
            fprintf(stderr, "Warning: could not load %s: %s\n", path, Mix_GetError());
        } else {
            loaded++;
        }
    }
    return loaded;
}

void audio_play_sample(int sample_id)
{
    if (sample_id < 0 || sample_id >= AUDIO_MAX_SAMPLES) return;
    if (!s_samples[sample_id]) return;

    const SampleParams *p = &k_sample_params[sample_id];

    /* Convert Paula vol 0–64 → SDL 0–128 */
    int sdl_vol = p->vol * 2;
    if (sdl_vol > MIX_MAX_VOLUME) sdl_vol = MIX_MAX_VOLUME;

    /* Find a free SFX channel (0 to NUM_SFX_CHANNELS-1).
     * Channels 12–13 unused, 14 = voice sequence, 15 = alarm loop. */
    int ch = -1;
    for (int i = 0; i < NUM_SFX_CHANNELS; i++) {
        if (!Mix_Playing(i)) { ch = i; break; }
    }
    if (ch == -1) ch = 0; /* all busy: steal channel 0 */

    /* Keep chunk volume at max; use per-channel volume for the envelope.
     * This allows the same Mix_Chunk to play simultaneously at different
     * volumes on different channels (e.g. dying_alien IDs 20 vs 21). */
    Mix_VolumeChunk(s_samples[sample_id], MIX_MAX_VOLUME);

    int loops = p->loop ? -1 : 0;

    /* Initialise channel envelope state */
    ChannelState *cs = &s_chan_state[ch];
    /* Release any stale pitch buffer from a previous use of this channel */
    if (cs->pitch_raw) { free(cs->pitch_raw); cs->pitch_raw = NULL; }

    cs->active        = 1;
    /* Looping samples never auto-stop via dur (mirrors ASM lbC024184 tst.b 1(a0)) */
    cs->dur_remaining = p->loop ? 0 : p->dur;
    cs->vol_sdl       = sdl_vol;

    /* Vol-ramp state — mirrors lbC024142 channel init in main.asm */
    cs->vol_ramp_en       = p->vol_ramp_en;
    cs->vol_ramp_raw_step = p->vol_ramp_step;
    if (p->vol_ramp_en && p->vol_ramp_step != 0) {
        int abs_step = p->vol_ramp_step < 0 ? -p->vol_ramp_step : p->vol_ramp_step;
        cs->vol_ramp_countdown = abs_step;
    } else {
        cs->vol_ramp_countdown = 0;
    }

    /* Pitch-ramp state */
    cs->pitch_ramp_en       = p->pitch_ramp_en;
    cs->pitch_ramp_raw_step = p->pitch_ramp_step;
    cs->pitch_done          = 0;
    cs->pitch_raw           = NULL;
    cs->pitch_raw_len       = 0;
    cs->pitch_read_pos      = 0.0;
    cs->per_initial         = p->per;
    cs->per_current_fp      = p->per * 256;

    if (p->pitch_ramp_en && p->pitch_ramp_step != 0) {
        int abs_step = p->pitch_ramp_step < 0
                       ? -p->pitch_ramp_step : p->pitch_ramp_step;
        cs->pitch_ramp_countdown = abs_step;

        /* Copy the chunk PCM so the effect callback can resample it at
         * variable speed.  The chunk itself is not used for playback;
         * a silent looping placeholder keeps the SDL channel open. */
        Mix_Chunk *chunk = s_samples[sample_id];
        cs->pitch_raw = (Uint8 *)malloc(chunk->alen);
        if (cs->pitch_raw) {
            memcpy(cs->pitch_raw, chunk->abuf, chunk->alen);
            cs->pitch_raw_len = chunk->alen;
            Mix_PlayChannel(ch, &s_silent_chunk, -1);
            Mix_Volume(ch, sdl_vol);
            Mix_RegisterEffect(ch, pitch_effect_cb, NULL, cs);
            return;
        }
        /* malloc failed — fall through to normal playback without pitch */
        cs->pitch_ramp_en = 0;
    } else {
        cs->pitch_ramp_countdown = 0;
    }

    /* Normal (non-pitch-ramp) playback */
    Mix_PlayChannel(ch, s_samples[sample_id], loops);
    Mix_Volume(ch, sdl_vol);
}

/* Dedicated channel reserved for the looping self-destruct alarm. */
#define ALARM_LOOP_CHANNEL 15

void audio_play_looping(int sample_id)
{
    if (sample_id < 0 || sample_id >= AUDIO_MAX_SAMPLES) return;
    if (!s_samples[sample_id]) return;
    Mix_PlayChannel(ALARM_LOOP_CHANNEL, s_samples[sample_id], -1);
}

void audio_stop_looping(void)
{
    Mix_HaltChannel(ALARM_LOOP_CHANNEL);
}

void audio_stop_samples(void)
{
    Mix_HaltChannel(-1);
    for (int i = 0; i < NUM_SFX_CHANNELS; i++)
        channel_state_clear(i);
}

void audio_play_music(const char *name)
{
    char path[256];

    /* Stop and free any currently playing module */
    if (s_music) {
        Mix_HookMusic(NULL, NULL);
        sm_free(s_music);
        s_music = NULL;
    }

    snprintf(path, sizeof(path), "assets/music/%s.soundmon", name);
    s_music = sm_load(path);
    if (!s_music) {
        fprintf(stderr, "Warning: could not load music '%s'\n", path);
        s_current_music_name[0] = '\0';
        return;
    }

    snprintf(s_current_music_name, sizeof(s_current_music_name), "%s", name);
    sm_play(s_music, s_mix_rate);
    Mix_HookMusic(sm_mix_callback, s_music);
    g_music_enabled = 1;
}

void audio_stop_music(void)
{
    if (s_music) {
        Mix_HookMusic(NULL, NULL);
        sm_free(s_music);
        s_music = NULL;
    }
    s_current_music_name[0] = '\0';
    g_music_enabled = 0;
}

const char *audio_get_current_music_name(void)
{
    return s_current_music_name;
}

void audio_set_music_volume(int vol)
{
    /* vol: 0-128 (SDL_mixer scale), forwarded directly to the Soundmon player. */
    sm_set_volume(s_music, vol);
}

void audio_pause_music(void)
{
    if (s_music) sm_pause(s_music);
}

void audio_resume_music(void)
{
    if (s_music) sm_resume(s_music);
}

/* -----------------------------------------------------------------------
 * Voice sequence player
 * Plays up to VOICE_SEQ_MAX voices sequentially on a dedicated channel.
 * Ref: schedule_sample_to_play / smp_player_requires_struct_*
 *      @ main.asm#L16806-L16816 and main.asm#L16919-L16924.
 * ----------------------------------------------------------------------- */
#define VOICE_CHANNEL    14
#define VOICE_SEQ_MAX     4

static int s_voice_seq[VOICE_SEQ_MAX];
static int s_voice_seq_idx   = 0;
static int s_voice_seq_count = 0;

void audio_play_voice_seq(int v1, int v2, int v3, int v4)
{
    /* Refuse to interrupt a sequence that is still in progress.
     * A sequence is "in progress" when there are still voices left to play
     * OR the last voice is still playing on the channel.  Both conditions
     * must be false for a new sequence to start.
     * Ref: smp_player_requires_struct_* @ main.asm#L16806-L16816. */
    if ((s_voice_seq_idx > 0 && s_voice_seq_idx <= s_voice_seq_count)
            && Mix_Playing(VOICE_CHANNEL))
        return;

    s_voice_seq[0] = v1;
    s_voice_seq[1] = v2;
    s_voice_seq[2] = v3;
    s_voice_seq[3] = v4;
    s_voice_seq_count = VOICE_SEQ_MAX;
    s_voice_seq_idx   = 0;

    /* Play first sample immediately */
    if (v1 >= 0 && v1 < AUDIO_MAX_SAMPLES && s_samples[v1]) {
        Mix_PlayChannel(VOICE_CHANNEL, s_samples[v1], 0);
        s_voice_seq_idx = 1;
    }
}

void audio_update(void)
{
    /* ---------------------------------------------------------------
     * SFX channel envelopes (channels 0 to NUM_SFX_CHANNELS-1).
     * Called at 50 Hz — mirrors the per-VBL interrupt lbC024142 in
     * main.asm which processes dur countdown and vol/pitch ramps.
     * --------------------------------------------------------------- */
    for (int ch = 0; ch < NUM_SFX_CHANNELS; ch++) {
        ChannelState *cs = &s_chan_state[ch];
        if (!cs->active) continue;

        /* Channel may have ended naturally before dur expired */
        if (!Mix_Playing(ch)) {
            channel_state_clear(ch);
            continue;
        }

        /* Duration countdown (non-looping samples only).
         * Mirrors: tst.b 1(a0) / subq.w #1, 12(a0) / beq switch_sel_channel_off */
        if (cs->dur_remaining > 0) {
            cs->dur_remaining--;
            if (cs->dur_remaining == 0) {
                Mix_HaltChannel(ch);
                channel_state_clear(ch);
                continue;
            }
        }

        /* Volume ramp.
         * Mirrors lbC024142 vol-ramp branch (lbC0241C2–lbC024204).
         * Ramp only applies while 0 < paula_vol < 64  (i.e. 0 < sdl_vol < 128).
         * Each time the countdown reaches 0 it is reloaded from abs(raw_step)
         * and the volume changes by ±8 Paula units (±16 SDL units). */
        if (cs->vol_ramp_en && cs->vol_ramp_raw_step != 0) {
            /* Only ramp while strictly between the floor (0) and ceiling (128) */
            if (cs->vol_sdl > 0 && cs->vol_sdl < MIX_MAX_VOLUME) {
                if (cs->vol_ramp_countdown == 0) {
                    /* Reload countdown — mirror: move.b 5(a0), 6(a0) + neg.b */
                    int abs_step = cs->vol_ramp_raw_step < 0
                                   ? -cs->vol_ramp_raw_step
                                   :  cs->vol_ramp_raw_step;
                    /* Reload then immediately decrement (mirrors ASM fall-through
                     * to subq.b at lbC024204 after applying the change). */
                    cs->vol_ramp_countdown = abs_step - 1;

                    if (cs->vol_ramp_raw_step < 0) {
                        cs->vol_sdl -= 16; /* fade-out: −8 Paula = −16 SDL */
                        if (cs->vol_sdl < 0) cs->vol_sdl = 0;
                    } else {
                        cs->vol_sdl += 16; /* fade-in:  +8 Paula = +16 SDL */
                        if (cs->vol_sdl > MIX_MAX_VOLUME) cs->vol_sdl = MIX_MAX_VOLUME;
                    }
                    Mix_Volume(ch, cs->vol_sdl);
                } else {
                    cs->vol_ramp_countdown--;
                }
            }
        }

        /* Pitch ramp: update per_current_fp every abs(step) ticks.
         * per decreasing → frequency increasing → pitch up   (step < 0).
         * per increasing → frequency decreasing → pitch down (step > 0).
         * Mirrors the pitch-ramp branch of lbC024142 in main.asm. */
        if (cs->pitch_ramp_en && cs->pitch_ramp_raw_step != 0) {
            /* Effect callback signals PCM exhaustion via pitch_done */
            if (cs->pitch_done) {
                Mix_HaltChannel(ch);
                channel_state_clear(ch);
                continue;
            }
            if (cs->pitch_ramp_countdown == 0) {
                int abs_step = cs->pitch_ramp_raw_step < 0
                               ? -cs->pitch_ramp_raw_step
                               :  cs->pitch_ramp_raw_step;
                cs->pitch_ramp_countdown = abs_step - 1;
                if (cs->pitch_ramp_raw_step < 0)
                    cs->per_current_fp -= 50 * 256; /* pitch up */
                else
                    cs->per_current_fp += 50 * 256; /* pitch down */
                /* Clamp to a sane Paula period range */
                if (cs->per_current_fp <   50 * 256) cs->per_current_fp =   50 * 256;
                if (cs->per_current_fp > 4096 * 256) cs->per_current_fp = 4096 * 256;
            } else {
                cs->pitch_ramp_countdown--;
            }
        }
    }

    /* ---------------------------------------------------------------
     * Voice sequence state machine.
     * Advances to the next voice in the sequence when the current one
     * finishes.  Ref: smp_player_requires_struct_* @ main.asm#L16806.
     * --------------------------------------------------------------- */
    if (s_voice_seq_idx < s_voice_seq_count && !Mix_Playing(VOICE_CHANNEL)) {
        int id = s_voice_seq[s_voice_seq_idx++];
        if (id >= 0 && id < AUDIO_MAX_SAMPLES && s_samples[id])
            Mix_PlayChannel(VOICE_CHANNEL, s_samples[id], 0);
    }
}

int audio_sample_loaded(int sample_id)
{
    if (sample_id < 0 || sample_id >= AUDIO_MAX_SAMPLES) return 0;
    return s_samples[sample_id] != NULL;
}
