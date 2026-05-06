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
#include <string.h>

int g_music_enabled = 0;

static Mix_Chunk  *s_samples[AUDIO_MAX_SAMPLES];
static SM_Module  *s_music  = NULL;
static int         s_mix_rate = 22050;

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
    Mix_PlayChannel(-1, s_samples[sample_id], 0);
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
        return;
    }

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
    g_music_enabled = 0;
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
    /* Advance the voice sequence when the current voice has finished. */
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
