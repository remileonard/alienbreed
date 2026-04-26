/*
 * Alien Breed SE 92 - C port
 * Audio HAL implementation
 *
 * Music is played via the native Soundmon V2 player (soundmon.c).
 * SDL2_mixer Mix_HookMusic is used to feed the mixed audio stream.
 */

#include "audio.h"
#include "soundmon.h"
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
    {  5, "samples/one_way_door"       },
    { 13, "samples/intex_shutdown"     },
    { 14, "samples/intex_beep"         },  /* caret move reuses beep */
    { 18, "samples/destruction_horn"   },
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
        s_samples[id] = Mix_LoadWAV(path);
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
