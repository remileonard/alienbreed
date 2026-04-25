/*
 * Soundmon V2 player for Alien Breed SE 92 - C port
 *
 * Faithful C translation of the 68000 assembly routines from main.asm:
 *   bpmusic    → sm_tick()
 *   bpnext     → sm_next()
 *   bpsynth    → sm_synth_tick()
 *   bpyessynth → sm_chan_synth()
 *   bpplayit   → sm_playit()
 *   bpplayarp  → note_to_period() applied in sm_tick()
 *
 * Called at SM_TICK_HZ (50 Hz) from the Mix_HookMusic audio callback.
 */

#include "soundmon.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Constants                                                            */
/* ------------------------------------------------------------------ */
#define SM_AMIGA_CLOCK   3546895u  /* PAL clock Hz used by Paula       */
#define SM_TICK_HZ       50        /* VBI rate: bpmusic called 50×/s   */
#define SM_MAX_INSTR     15        /* instruments per module            */
#define SM_MAX_CHANS     4         /* audio channels                    */
#define SM_WT_SIZE       64        /* wavetable bytes per entry         */
#define SM_SYNTH_BUF     32        /* synth waveform buffer bytes       */

/* ------------------------------------------------------------------ */
/* Silence buffer — used as the one-word loop for non-looping samples  */
/* ------------------------------------------------------------------ */
static const int8_t k_silence[4] = { 0, 0, 0, 0 };

/* ------------------------------------------------------------------ */
/* Period table (84 entries from main.asm)                             */
/* bpper label is at index 36.                                         */
/* note_to_period(note) = k_periods[35 + note]  (note is 1-based)     */
/* ------------------------------------------------------------------ */
static const uint16_t k_periods[84] = {
    /* pre-bpper indices 0-35 */
    0x1AC0, 0x1940, 0x17C0, 0x1680, 0x1540, 0x1400, 0x12E0, 0x11E0,
    0x10E0, 0x0FE0, 0x0F00, 0x0E20, 0x0D60, 0x0CA0, 0x0BE0, 0x0B40, 0x0AA0,
    0x0A00, 0x0970, 0x08F0, 0x0870, 0x07F0, 0x0780, 0x0710, 0x06B0, 0x0650, 0x05F0,
    0x05A0, 0x0550, 0x0500, 0x04B8, 0x0478, 0x0438, 0x03F8, 0x03C0, 0x0388,
    /* bpper indices 36-83 */
    0x0358, 0x0328, 0x02F8, 0x02D0, 0x02A8, 0x0280, 0x025C, 0x023C, 0x021C, 0x01FC,
    0x01E0, 0x01C4, 0x01AC, 0x0194, 0x017C, 0x0168, 0x0154, 0x0140, 0x012E, 0x011E,
    0x010E, 0x00FE, 0x00F0, 0x00E2, 0x00D6, 0x00CA, 0x00BE, 0x00B4, 0x00AA, 0x00A0,
    0x0097, 0x008F, 0x0087, 0x007F, 0x0078, 0x0071, 0x006B, 0x0065, 0x005F, 0x005A,
    0x0055, 0x0050, 0x004C, 0x0048, 0x0044, 0x0040, 0x003C, 0x0039
};

/* bpper[note-1] = k_periods[35+note]  (note clamped to valid range) */
static uint16_t note_to_period(int note)
{
    int idx = 35 + note;
    if (idx < 0)  idx = 0;
    if (idx > 83) idx = 83;
    return k_periods[idx];
}

/* ------------------------------------------------------------------ */
/* Big-endian 16-bit read                                              */
/* ------------------------------------------------------------------ */
static inline uint16_t be16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

/* ------------------------------------------------------------------ */
/* Per-channel state                                                    */
/* Mirrors bpcurrent (32 bytes) + bpbuffer (36 bytes) from main.asm   */
/* ------------------------------------------------------------------ */
typedef struct {
    /* bpcurrent fields */
    uint16_t period;        /* [0-1]  current Amiga period               */
    uint8_t  volume;        /* [2]    0-64, 0xFF = use native volume      */
    uint8_t  instr;         /* [3]    instrument number 1-15              */
    uint16_t repeat_len;    /* [8-9]  loop length in words (1 = no loop)  */
    uint8_t  note;          /* [10]   base note (1-based)                 */
    uint8_t  arpeggio;      /* [11]   arpeggio nibbles (hi/lo intervals)  */
    int8_t   autoslide;     /* [12]   signed period increment per frame   */
    uint8_t  autoarp;       /* [13]   auto-arpeggio nibbles               */
    uint16_t eg_pos;        /* [14-15]                                    */
    uint16_t lfo_pos;       /* [16-17]                                    */
    uint16_t adsr_pos;      /* [18-19]                                    */
    uint16_t eg_counter;    /* [20-21]                                    */
    uint16_t lfo_counter;   /* [22-23]                                    */
    uint16_t adsr_counter;  /* [24-25]                                    */
    uint8_t  is_synth;      /* [26]   synthetic instrument active         */
    uint8_t  vol_slide;     /* [27]   volume slide (unused in this music) */
    uint8_t  eg_value;      /* [28]   EG waveform byte-offset (0..31)     */
    uint8_t  eg_ooc;        /* [29]   EG enabled/one-shot flag            */
    uint8_t  lfo_ooc;       /* [30]   LFO enabled/one-shot flag           */
    uint8_t  adsr_ooc;      /* [31]   ADSR enabled/one-shot flag          */

    /* new-note trigger (bit 15 of period in the original) */
    int      new_note;

    /* sample playback pointers */
    const int8_t *init_ptr; /* initial segment start                      */
    int           init_len; /* initial segment byte count                 */
    const int8_t *loop_ptr; /* loop segment start                         */
    int           loop_len; /* loop segment byte count (2 = silence/stop) */

    /* mixer fractional position */
    double   pos;           /* byte position within current segment       */
    int      in_loop;       /* 0 = init segment, 1 = loop segment         */

    /* hardware period/volume (set by synth/arp each frame) */
    uint16_t hw_period;
    uint8_t  hw_volume;

    /* synthetic instrument waveform buffers (bpbuffer equivalent) */
    int      synth_active;               /* bpbuffer.ptr != 0             */
    int      synth_len;                  /* waveform length in bytes       */
    int8_t   synth_buf[SM_SYNTH_BUF];   /* live waveform (EG modifies)    */
    int8_t   synth_work[SM_SYNTH_BUF];  /* static copy for EG source      */
} SM_Chan;

/* ------------------------------------------------------------------ */
/* Module structure                                                     */
/* ------------------------------------------------------------------ */
struct SM_Module {
    uint8_t  *data;
    size_t    size;

    uint8_t   num_tables;
    uint16_t  num_steps;
    uint16_t  max_pattern;   /* highest pattern index + 1 (1-based count) */

    const uint8_t *step_table;              /* data + 512                 */
    const uint8_t *pat_data;               /* data + 512 + steps*16       */
    const int8_t  *wavetables;             /* after pattern data          */
    const int8_t  *smp_ptrs[SM_MAX_INSTR]; /* per-instrument PCM start   */
    const uint8_t *instr[SM_MAX_INSTR];    /* 32-byte descriptors         */

    SM_Chan   ch[SM_MAX_CHANS];

    uint16_t  bpstep;       /* current step (0..num_steps-1)              */
    uint8_t   bppatcount;   /* row byte-offset within pattern (0,3,...,45)*/
    uint8_t   bpcount;      /* tick countdown to next row                 */
    uint8_t   bpdelay;      /* ticks per row (default 6)                  */
    uint8_t   arpcount;     /* arpeggio phase counter (cycles 3→2→1→0→3) */
    uint8_t   bprepcount;   /* repeat counter for effect 7                */

    /* audio mixing */
    int       output_rate;
    double    tick_acc;     /* samples accumulated since last tick        */
    double    spf;          /* samples per frame = output_rate/SM_TICK_HZ */

    int       playing;
    int       paused;
    int       master_volume; /* 0-128, applied to final mix output        */

    SDL_mutex *mutex;
};

/* ================================================================== */
/* Loader                                                               */
/* ================================================================== */

SM_Module *sm_load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "soundmon: cannot open '%s'\n", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long fsz = ftell(f);
    rewind(f);
    if (fsz < 512) { fclose(f); return NULL; }

    uint8_t *data = (uint8_t *)malloc((size_t)fsz);
    if (!data) { fclose(f); return NULL; }

    if ((long)fread(data, 1, (size_t)fsz, f) != fsz) {
        free(data);
        fclose(f);
        return NULL;
    }
    fclose(f);

    SM_Module *m = (SM_Module *)calloc(1, sizeof(*m));
    if (!m) { free(data); return NULL; }

    m->data = data;
    m->size = (size_t)fsz;

    /* Parse main header ------------------------------------------------ */
    /* data[26]='V', data[27]='.', data[28]='2' for Soundmon V2           */
    if (data[26] == 'V' && data[27] == '.' && data[28] == '2') {
        m->num_tables = data[29];
    }
    m->num_steps = be16(data + 30);
    if (m->num_steps == 0) m->num_steps = 1;

    /* Instrument descriptors at data[32..511] (15 × 32 bytes) */
    for (int i = 0; i < SM_MAX_INSTR; i++) {
        m->instr[i] = data + 32 + i * 32;
    }

    /* Step table at offset 512 */
    m->step_table = data + 512;

    /* Find max pattern number by scanning the step table.
     * Each step: 4 voices × 4 bytes = 16 bytes.
     * Each voice entry: word(pattern_num), byte(st), byte(tr).          */
    m->max_pattern = 1;
    for (uint32_t s = 0; s < (uint32_t)m->num_steps * 4; s++) {
        uint16_t pat = be16(m->step_table + s * 4);
        if (pat > m->max_pattern) m->max_pattern = pat;
    }

    /* Pattern data immediately after step table */
    size_t step_size  = (size_t)m->num_steps * 16;
    size_t pat_size   = (size_t)m->max_pattern * 48;
    m->pat_data  = data + 512 + step_size;

    /* Wavetables immediately after pattern data */
    m->wavetables = (const int8_t *)(data + 512 + step_size + pat_size);

    /* Sample PCM data immediately after wavetables */
    size_t wt_size = (size_t)m->num_tables * SM_WT_SIZE;
    const int8_t *smp = m->wavetables + wt_size;
    for (int i = 0; i < SM_MAX_INSTR; i++) {
        m->smp_ptrs[i] = smp;
        const uint8_t *desc = m->instr[i];
        if (desc[0] != 0xFF) {
            /* Real sample: advance pointer by length_words × 2 bytes */
            uint16_t len_words = be16(desc + 24);
            smp += (size_t)len_words * 2;
        }
        /* Synthetic: no PCM data in the sample region */
    }

    m->mutex = SDL_CreateMutex();
    return m;
}

/* ================================================================== */
/* Free                                                                 */
/* ================================================================== */

void sm_free(SM_Module *m)
{
    if (!m) return;
    sm_stop(m);
    if (m->mutex) { SDL_DestroyMutex(m->mutex); m->mutex = NULL; }
    free(m->data);
    free(m);
}

/* ================================================================== */
/* Reset player state (mirrors start_music in main.asm)               */
/* ================================================================== */

static void sm_reset(SM_Module *m)
{
    memset(m->ch, 0, sizeof(m->ch));
    m->bpstep     = 0;
    m->bppatcount = 0;
    m->bpcount    = 1;   /* triggers bpnext on very first tick          */
    m->bpdelay    = 6;
    m->arpcount   = 1;
    m->bprepcount = 1;

    /* Initialise channels to silence (mirrors bpreset copy) */
    for (int i = 0; i < SM_MAX_CHANS; i++) {
        m->ch[i].repeat_len = 1;
        m->ch[i].init_ptr   = k_silence;
        m->ch[i].init_len   = 2;
        m->ch[i].loop_ptr   = k_silence;
        m->ch[i].loop_len   = 2;
        m->ch[i].hw_period  = 0x0358;
        m->ch[i].hw_volume  = 0;
        m->ch[i].volume     = 0xFF;
    }
}

/* ================================================================== */
/* sm_play / sm_stop / sm_pause / sm_resume                           */
/* ================================================================== */

void sm_play(SM_Module *m, int output_rate)
{
    if (!m) return;
    SDL_LockMutex(m->mutex);
    sm_reset(m);
    m->output_rate = output_rate;
    m->spf         = (double)output_rate / SM_TICK_HZ;
    m->tick_acc    = 0.0;
    m->playing       = 1;
    m->paused        = 0;
    m->master_volume = 128;
    SDL_UnlockMutex(m->mutex);
}

void sm_stop(SM_Module *m)
{
    if (!m) return;
    SDL_LockMutex(m->mutex);
    m->playing = 0;
    m->paused  = 0;
    SDL_UnlockMutex(m->mutex);
}

void sm_pause(SM_Module *m)
{
    if (!m) return;
    SDL_LockMutex(m->mutex);
    m->paused = 1;
    SDL_UnlockMutex(m->mutex);
}

void sm_resume(SM_Module *m)
{
    if (!m) return;
    SDL_LockMutex(m->mutex);
    m->paused = 0;
    SDL_UnlockMutex(m->mutex);
}

/* ================================================================== */
/* bpplayit — set up channel playback for a newly triggered note       */
/* Called from sm_tick() when new_note is set (bit 15 of period).     */
/* ================================================================== */

static void sm_playit(SM_Module *m, int ch_idx)
{
    SM_Chan  *ch   = &m->ch[ch_idx];
    int       instr = (int)ch->instr;         /* 1-based */
    if (instr < 1 || instr > SM_MAX_INSTR) return;

    const uint8_t *desc = m->instr[instr - 1];

    /* Clear new-note flag */
    ch->new_note = 0;

    /* Flush synth waveform to synth_buf if it was active */
    if (ch->synth_active && ch->is_synth) {
        /* mirrors: move.l (a5),a4; copy 8 longs from bpbuffer.data to a4 */
        memcpy(ch->synth_buf, ch->synth_work, SM_SYNTH_BUF);
    }

    /* Set hardware period from bpcurrent.period */
    ch->hw_period = ch->period;

    if (desc[0] == 0xFF) {
        /* ---- Synthetic instrument ---------------------------------- */
        /* mirrors bpplaysynthetic */
        ch->is_synth   = 1;
        ch->eg_pos     = 0;
        ch->lfo_pos    = 0;
        ch->adsr_pos   = 0;

        /* EG counter: desc[22-23] + 1 */
        ch->eg_counter   = be16(desc + 22) + 1;
        /* LFO counter: desc[14-15] + 1 */
        ch->lfo_counter  = be16(desc + 14) + 1;
        /* ADSR counter: 1 (fires on first tick) */
        ch->adsr_counter = 1;

        ch->eg_ooc   = desc[17];
        ch->lfo_ooc  = desc[9];
        ch->adsr_ooc = desc[4];
        ch->eg_value = desc[19];

        /* Waveform: tables[wt_idx * 64] */
        uint8_t wt_idx = desc[1];
        int     wt_len_words = (int)be16(desc + 2);
        if (wt_len_words <= 0) wt_len_words = 16;
        ch->synth_len = wt_len_words * 2;
        if (ch->synth_len > SM_SYNTH_BUF) ch->synth_len = SM_SYNTH_BUF;

        const int8_t *wt = m->wavetables + (size_t)wt_idx * SM_WT_SIZE;

        /* Copy waveform into both synth_buf and synth_work (eg2loop) */
        memcpy(ch->synth_buf,  wt, (size_t)ch->synth_len);
        memcpy(ch->synth_work, wt, (size_t)ch->synth_len);
        ch->synth_active = 1;

        /* If EG enabled and initial eg_value != 0: negate first eg_value
         * bytes of synth_buf (mirrors eg3loop)                          */
        if (desc[17] && desc[19]) {
            int n = (int)(desc[19] >> 3);   /* (eg_value_init/8) - same as lsr.l #3 */
            if (n > ch->synth_len) n = ch->synth_len;
            for (int k = 0; k < n; k++) {
                ch->synth_buf[k] = -ch->synth_buf[k];
            }
        }

        /* Loop the waveform */
        ch->init_ptr = ch->synth_buf;
        ch->init_len = ch->synth_len;
        ch->loop_ptr = ch->synth_buf;
        ch->loop_len = ch->synth_len;

        /* Native volume or bpcurrent volume */
        uint8_t native_vol = desc[25];
        if (ch->volume == 0xFF) {
            ch->hw_volume = native_vol;
        } else {
            ch->hw_volume = ch->volume;
        }

        /* If ADSR enabled, compute initial volume from first ADSR sample */
        if (desc[4]) {
            uint8_t adsr_tbl_idx = desc[5];
            const int8_t *adsr_tbl = m->wavetables + (size_t)adsr_tbl_idx * SM_WT_SIZE;
            int adsr_val = (int)(uint8_t)adsr_tbl[0];  /* first byte */
            adsr_val += 128;                             /* make unsigned */
            adsr_val >>= 2;                              /* 0..63 */
            if (ch->volume == 0xFF) {
                ch->hw_volume = desc[25];
            }
            int scaled = (int)ch->hw_volume * adsr_val >> 6;
            ch->hw_volume = (uint8_t)scaled;
        }

    } else {
        /* ---- Real sample ------------------------------------------- */
        /* mirrors non-synthetic bpplayit path */
        ch->is_synth     = 0;
        ch->synth_active = 0;
        ch->lfo_ooc      = 0;
        ch->adsr_ooc     = 0;

        const int8_t *smp_start = m->smp_ptrs[instr - 1];
        uint16_t smp_len_words  = be16(desc + 24);  /* desc[24-25] */
        uint16_t loop_off_bytes = be16(desc + 26);  /* desc[26-27] */
        uint16_t loop_len_words = be16(desc + 28);  /* desc[28-29] */
        uint8_t  native_vol     = (uint8_t)be16(desc + 30); /* desc[30-31] */

        ch->init_ptr = smp_start;
        ch->init_len = (int)smp_len_words * 2;

        if (loop_len_words > 1) {
            ch->loop_ptr = smp_start + loop_off_bytes;
            ch->loop_len = (int)loop_len_words * 2;
        } else {
            /* No loop: use silence buffer */
            ch->loop_ptr = k_silence;
            ch->loop_len = 2;
        }
        ch->repeat_len = loop_len_words;

        /* Set hardware volume */
        if (ch->volume == 0xFF) {
            ch->hw_volume = native_vol;
        } else {
            ch->hw_volume = ch->volume;
        }
    }

    /* Reset mixer position to start of initial segment */
    ch->pos     = 0.0;
    ch->in_loop = 0;
}

/* ================================================================== */
/* sm_chan_synth — bpyessynth per-channel synthetic modulation        */
/* Called once per tick for each active synthetic channel.            */
/* ================================================================== */

static void sm_chan_synth(SM_Module *m, int ch_idx)
{
    SM_Chan       *ch   = &m->ch[ch_idx];
    int            instr = (int)ch->instr;
    if (instr < 1 || instr > SM_MAX_INSTR) return;

    const uint8_t *desc = m->instr[instr - 1];
    /* d7 = instr * 32 (offset into descriptor — implicit since we use desc[]) */

    /* ---- ADSR (volume envelope) -------------------------------------- */
    /* tst.b 31(a2)  → adsr_ooc                                          */
    if (ch->adsr_ooc) {
        if (ch->adsr_counter > 0) ch->adsr_counter--;
        if (ch->adsr_counter == 0) {
            /* Reload from desc[8] */
            ch->adsr_counter = desc[8];
            if (ch->adsr_counter == 0) ch->adsr_counter = 1;

            uint8_t adsr_tbl_idx = desc[5];
            const int8_t *adsr_tbl = m->wavetables + (size_t)adsr_tbl_idx * SM_WT_SIZE;

            /* adsr_pos: current row in ADSR table */
            int adsr_row = (int)ch->adsr_pos;
            int adsr_val = (int)(uint8_t)adsr_tbl[adsr_row]; /* unsigned read */
            adsr_val += 128;
            adsr_val >>= 2;   /* 0..63 */

            /* Scale by current volume (channel volume, not native) */
            int vol = (int)ch->volume;
            if (vol == 0xFF) vol = (int)(uint8_t)desc[25];
            int hw_vol = vol * adsr_val >> 6;
            ch->hw_volume = (uint8_t)hw_vol;

            /* Advance position */
            ch->adsr_pos++;
            uint16_t adsr_len = be16(desc + 6);
            if (ch->adsr_pos >= adsr_len) {
                ch->adsr_pos = 0;
                /* One-shot: clear flag when value was 1 */
                if (ch->adsr_ooc == 1) ch->adsr_ooc = 0;
            }
        }
    }

    /* ---- LFO (period/vibrato) ---------------------------------------- */
    /* tst.b 30(a2)  → lfo_ooc                                            */
    if (ch->lfo_ooc) {
        if (ch->lfo_counter > 0) ch->lfo_counter--;
        if (ch->lfo_counter == 0) {
            /* Reload from desc[16] */
            ch->lfo_counter = desc[16];
            if (ch->lfo_counter == 0) ch->lfo_counter = 1;

            uint8_t lfo_tbl_idx = desc[10];
            const int8_t *lfo_tbl = m->wavetables + (size_t)lfo_tbl_idx * SM_WT_SIZE;

            int lfo_row = (int)ch->lfo_pos;
            int lfo_val = (int)lfo_tbl[lfo_row]; /* signed */

            /* Apply divisor: if desc[11] != 0, divide lfo_val */
            if (desc[11]) lfo_val /= (int)desc[11];

            /* hw_period = ch.period + lfo_val (write to AUD_PER) */
            int new_per = (int)ch->period + lfo_val;
            if (new_per < 1) new_per = 1;
            ch->hw_period = (uint16_t)new_per;

            /* Advance position */
            ch->lfo_pos++;
            uint16_t lfo_len = be16(desc + 12);
            if (ch->lfo_pos >= lfo_len) {
                ch->lfo_pos = 0;
                if (ch->lfo_ooc == 1) ch->lfo_ooc = 0;
            }
        }
    }

    /* ---- EG (waveform envelope generator) ----------------------------- */
    /* tst.b 29(a2)  → eg_ooc                                             */
    if (ch->eg_ooc) {
        if (ch->eg_counter > 0) ch->eg_counter--;
        if (ch->eg_counter == 0 && ch->synth_active) {

            /* Reload from desc[24] */
            ch->eg_counter = desc[24];
            if (ch->eg_counter == 0) ch->eg_counter = 1;

            uint8_t eg_tbl_idx = desc[18];
            const int8_t *eg_tbl = m->wavetables + (size_t)eg_tbl_idx * SM_WT_SIZE;

            int eg_row = (int)ch->eg_pos;
            int eg_val_raw = (int)(uint8_t)eg_tbl[eg_row]; /* unsigned */
            eg_val_raw += 128;
            eg_val_raw >>= 3;   /* 0..31 */

            int old_pos = (int)ch->eg_value;
            int new_pos = eg_val_raw;
            ch->eg_value = (uint8_t)new_pos;

            /* Shift waveform in synth_buf using synth_work as source.
             *
             * ASM: a4 = synth_buf + old_pos
             *       a6 = synth_work + old_pos  (= bpbuffer.data + old_pos)
             * If new > old (forward):
             *   copy (new-old) bytes forward from synth_work to synth_buf,
             *   negating each byte. (bpegloop1b)
             * If new < old (backward):
             *   copy (old-new) bytes backward from synth_work to synth_buf
             *   without negating. (bpegloop1a)             */
            if (new_pos != old_pos) {
                if (new_pos > old_pos) {
                    /* Forward shift: copy (negate) bytes new..old-1 from
                     * synth_work into synth_buf.  Mirrors bpegloop1b.   */
                    int delta = new_pos - old_pos;
                    for (int k = 0; k < delta && (old_pos + k) < SM_SYNTH_BUF; k++) {
                        ch->synth_buf[old_pos + k] = -ch->synth_work[old_pos + k];
                    }
                } else {
                    /* Backward shift: copy bytes new_pos..old_pos-1
                     * from synth_work into synth_buf.  Mirrors bpegloop1a. */
                    int delta = old_pos - new_pos;
                    for (int k = 0; k < delta; k++) {
                        int idx = old_pos - 1 - k;
                        if (idx >= 0) {
                            ch->synth_buf[idx] = ch->synth_work[idx];
                        }
                    }
                }
            }

            /* Update loop pointers so mixer sees new waveform */
            ch->init_ptr = ch->synth_buf;
            ch->loop_ptr = ch->synth_buf;

            /* Advance EG position */
            ch->eg_pos++;
            uint16_t eg_len = be16(desc + 20);
            if (ch->eg_pos >= eg_len) {
                ch->eg_pos = 0;
                if (ch->eg_ooc == 1) ch->eg_ooc = 0;
            }
        }
    }
}

/* ================================================================== */
/* sm_synth_tick — bpsynth: run modulation for all synthetic channels */
/* Called once per tick before bploop1.                               */
/* ================================================================== */

static void sm_synth_tick(SM_Module *m)
{
    for (int i = 0; i < SM_MAX_CHANS; i++) {
        if (m->ch[i].is_synth) {
            sm_chan_synth(m, i);
        }
    }
}

/* ================================================================== */
/* sm_next — bpnext: parse next pattern row for all channels          */
/* Called from sm_tick() when bpcount hits 0.                         */
/* ================================================================== */

static void sm_next(SM_Module *m)
{
    for (int i = 0; i < SM_MAX_CHANS; i++) {
        SM_Chan *ch = &m->ch[i];

        /* Step table voice index (reversed from channel index):
         * ch[0]=AUD0 ↔ voice 3, ch[1]=AUD1 ↔ voice 2, etc.
         * (from bploop3: d0=3,2,1,0 for ch 0,1,2,3; voice_in_step=d0) */
        int voice_in_step = 3 - i;

        /* Step table entry for this channel */
        uint32_t step_off = (uint32_t)m->bpstep * 16 + (uint32_t)voice_in_step * 4;
        const uint8_t *step_entry = m->step_table + step_off;

        uint16_t pat_num = be16(step_entry);         /* 1-based pattern number */
        uint8_t  st      = step_entry[2];            /* octave/group selector   */
        uint8_t  tr      = step_entry[3];            /* note transposition      */

        if (pat_num == 0) continue;   /* safety: skip empty entries */

        /* Pattern data pointer:
         * base = pat_data + (pat_num-1)*48 + bppatcount               */
        uint32_t pat_off = (uint32_t)(pat_num - 1) * 48 + m->bppatcount;
        const uint8_t *row = m->pat_data + pat_off;

        uint8_t row_note  = row[0];
        uint8_t instr_eff = row[1];
        uint8_t param     = row[2];

        uint8_t effect = instr_eff & 0x0F;

        /* If note != 0, trigger new note */
        if (row_note != 0) {
            /* Clear autoslide unless effect == 10 (portamento with active slide) */
            /* (effect 10 is beyond range 0-9, so it can't occur here) */
            ch->autoslide = 0;

            /* Compute final note with transpose */
            int note = (int)row_note + (int)(int8_t)tr;

            ch->note   = (uint8_t)note;
            ch->period = note_to_period(note);

            /* Set bit-15 trigger flag */
            ch->new_note = 1;

            /* Volume flag: 0xFF = use native */
            ch->volume = 0xFF;

            /* Get instrument number from high nibble */
            uint8_t instr = (instr_eff >> 4) & 0x0F;
            if (instr == 0) {
                instr = ch->instr;   /* keep current instrument */
            } else {
                /* Apply st (group offset) to instrument number */
                instr = (uint8_t)((int)instr + (int)st);
                if (instr < 1) instr = 1;
                if (instr > SM_MAX_INSTR) instr = SM_MAX_INSTR;
            }

            /* Only trigger DMA (new sample) if instrument changed or loop_len==1 */
            int need_sample_change = (ch->repeat_len == 1) || (ch->instr != instr);
            ch->instr = instr;
            if (!need_sample_change) {
                /* Same instrument, no retrigger — clear new_note flag */
                ch->new_note = 0;
            }
        }

        /* ---- Process effect ---------------------------------------- */
        /* bpoptionals: effect is in the low nibble of instr_eff,
         * param is in the full second byte.                             */
        switch (effect) {
        case 0:
            /* Arpeggio: set arpeggio nibbles */
            ch->arpeggio = param;
            break;
        case 1:
            /* Set volume */
            ch->volume    = (uint8_t)(param & 0x3F);  /* clamp 0-63 */
            ch->hw_volume = ch->volume;
            break;
        case 2:
            /* Set tempo: param must be >= 4 */
            if ((param & 0x0F) >= 4) {
                uint8_t tempo = param & 0x0F;
                m->bpcount  = tempo;
                m->bpdelay  = tempo;
            }
            break;
        case 3:
            /* NOP */
            break;
        case 4:
            /* Portamento up: period -= param (lower period = higher pitch) */
            {
                int new_per = (int)ch->period - (int)param;
                if (new_per < 1) new_per = 1;
                ch->period = (uint16_t)new_per;
                ch->arpeggio = 0;
            }
            break;
        case 5:
            /* Portamento down: period += param */
            {
                int new_per = (int)ch->period + (int)param;
                if (new_per > 0x7FFF) new_per = 0x7FFF;
                ch->period = (uint16_t)new_per;
                ch->arpeggio = 0;
            }
            break;
        case 6:
            /* Set repeat counter */
            m->bprepcount = param;
            break;
        case 7:
            /* Pattern jump with repeat count */
            m->bprepcount--;
            if (m->bprepcount != 0) {
                /* Jump to step param (convert param to step index) */
                m->bpstep = (uint16_t)param;
                /* Will be overridden at end if patcount wraps — handled below */
            }
            break;
        case 8:
            /* Set autoslide (signed) */
            ch->autoslide = (int8_t)param;
            break;
        case 9:
            /* Set auto-arpeggio */
            ch->autoarp = param;
            break;
        default:
            break;
        }
    }

    /* Advance pattern position */
    m->bppatcount += 3;
    if (m->bppatcount >= 48) {
        m->bppatcount = 0;
        m->bpstep++;
        if (m->bpstep >= m->num_steps) {
            m->bpstep = 0;  /* loop back */
        }
    }
}

/* ================================================================== */
/* sm_tick — bpmusic: one full 50-Hz tick                             */
/* ================================================================== */

static void sm_tick(SM_Module *m)
{
    /* --- bpsynth: run modulation for synthetic channels -------------- */
    sm_synth_tick(m);

    /* --- Decrement arpcount (reset to 3 happens AFTER the channel loop) */
    if (m->arpcount > 0) m->arpcount--;

    /* --- bploop1: update all 4 channels ------------------------------ */
    for (int i = 0; i < SM_MAX_CHANS; i++) {
        SM_Chan *ch = &m->ch[i];

        /* Apply autoslide: period += autoslide (signed) */
        int new_per = (int)ch->period + (int)ch->autoslide;
        if (new_per < 1)      new_per = 1;
        if (new_per > 0x7FFF) new_per = 0x7FFF;
        ch->period = (uint16_t)new_per;

        /* Set hw_period from bpcurrent.period if LFO is not active.
         * (If LFO active, hw_period was already set by sm_chan_synth.) */
        if (!ch->lfo_ooc) {
            ch->hw_period = ch->period;
        }

        /* Sample pointer update for non-synth (loop_ptr / loop_len)
         * mirrors: move.l 4(a0),(a1); move.w 8(a0),4(a1)              */
        /* (already encoded in ch->loop_ptr / ch->loop_len)            */

        /* --- Arpeggio / auto-arpeggio -------------------------------- */
        int do_arp = (ch->arpeggio != 0 || ch->autoarp != 0);
        if (do_arp) {
            int arp_note;
            if (m->arpcount == 0) {
                /* HIGH nibble */
                int h_arp  = (ch->arpeggio >> 4) & 0x0F;
                int h_auto = (ch->autoarp  >> 4) & 0x0F;
                arp_note = (int)ch->note + h_arp + h_auto;
            } else if (m->arpcount == 1) {
                /* LOW nibble */
                int l_arp  = ch->arpeggio & 0x0F;
                int l_auto = ch->autoarp  & 0x0F;
                arp_note = (int)ch->note + l_arp + l_auto;
            } else {
                /* BASE note */
                arp_note = (int)ch->note;
            }
            ch->hw_period = note_to_period(arp_note);
        }
    }

    /* --- Reset arpcount to 3 if it hit 0 (mirrors lbC02443C) ---------- */
    if (m->arpcount == 0) m->arpcount = 3;

    /* --- bpcount: decrement and check for next row ------------------ */
    if (m->bpcount > 0) m->bpcount--;
    if (m->bpcount == 0) {
        /* Reset counter and advance to next row */
        m->bpcount = m->bpdelay;
        sm_next(m);

        /* bploop2: call sm_playit for channels with new_note set */
        for (int i = 0; i < SM_MAX_CHANS; i++) {
            if (m->ch[i].new_note) {
                sm_playit(m, i);
            }
        }
    }
}

/* ================================================================== */
/* Mix_HookMusic callback                                               */
/* Called by the SDL audio thread with an empty stream to fill.        */
/* Output format: signed 16-bit stereo at m->output_rate Hz.          */
/* ================================================================== */

void sm_mix_callback(void *udata, Uint8 *stream, int len)
{
    SM_Module *m = (SM_Module *)udata;
    if (!m) { memset(stream, 0, (size_t)len); return; }

    SDL_LockMutex(m->mutex);

    if (!m->playing || m->paused) {
        SDL_UnlockMutex(m->mutex);
        memset(stream, 0, (size_t)len);
        return;
    }

    int16_t *out   = (int16_t *)stream;
    int      nframes = len / 4;  /* stereo int16 = 4 bytes per frame */

    for (int f = 0; f < nframes; f++) {
        /* Advance tick accumulator; call sm_tick every spf samples */
        m->tick_acc += 1.0;
        while (m->tick_acc >= m->spf) {
            m->tick_acc -= m->spf;
            sm_tick(m);
        }

        /* Mix 4 channels.
         * Amiga default panning: AUD0+AUD3 = left, AUD1+AUD2 = right.
         * ch[0]=AUD0=left, ch[1]=AUD1=right, ch[2]=AUD2=right, ch[3]=AUD3=left */
        int mix_l = 0;
        int mix_r = 0;

        for (int i = 0; i < SM_MAX_CHANS; i++) {
            SM_Chan *ch = &m->ch[i];

            /* Select sample buffer and length */
            const int8_t *buf;
            int           blen;
            if (ch->is_synth && ch->synth_active) {
                buf  = ch->synth_buf;
                blen = ch->synth_len;
            } else if (ch->in_loop) {
                buf  = ch->loop_ptr;
                blen = ch->loop_len;
            } else {
                buf  = ch->init_ptr;
                blen = ch->init_len;
            }

            if (!buf || blen <= 0 || ch->hw_period == 0) continue;

            /* Compute sample step size (fixed-point):
             * Paula rate = AMIGA_CLOCK / period  (Hz)
             * step = paula_rate / output_rate      */
            double step = (double)SM_AMIGA_CLOCK / (double)ch->hw_period
                          / (double)m->output_rate;

            /* Get current sample (linear interpolation between two bytes) */
            int   ipos = (int)ch->pos;
            double frac = ch->pos - (double)ipos;

            int8_t s0, s1;
            if (ch->is_synth && ch->synth_active) {
                /* Synth: loop within synth_buf */
                ipos = ipos % blen;
                s0   = buf[ipos];
                s1   = buf[(ipos + 1) % blen];
            } else if (ch->in_loop) {
                if (ipos < 0) ipos = 0;
                ipos = ipos % blen;
                s0   = buf[ipos];
                s1   = buf[(ipos + 1) % blen];
            } else {
                if (ipos >= blen) {
                    /* Transition to loop segment */
                    ch->in_loop = 1;
                    ch->pos     = 0.0;
                    ipos        = 0;
                    buf         = ch->loop_ptr;
                    blen        = ch->loop_len;
                    s0          = buf[0];
                    s1          = buf[1 % blen];
                } else {
                    s0 = buf[ipos];
                    s1 = (ipos + 1 < blen) ? buf[ipos + 1] : buf[ipos];
                }
            }

            /* Linear interpolation */
            int sample = (int)s0 + (int)((double)((int)s1 - (int)s0) * frac);

            /* Apply volume (0-64 → scale sample).
             * Divide by 4 (not 32) so each channel contributes ≈ ±2032;
             * four channels combined reach ≈ ±8128 — well within int16. */
            int vol = (int)ch->hw_volume;
            if (vol > 64) vol = 64;
            sample = sample * vol / 4;

            /* Advance position */
            ch->pos += step;

            /* Handle segment wrap for non-synth */
            if (!ch->is_synth) {
                if (!ch->in_loop) {
                    if (ch->pos >= (double)ch->init_len) {
                        ch->in_loop = 1;
                        ch->pos     = 0.0;
                    }
                } else {
                    double floop = (double)ch->loop_len;
                    if (floop > 0.0) {
                        while (ch->pos >= floop) ch->pos -= floop;
                    }
                }
            } else {
                /* Synth: always loop within synth_buf */
                double fsynth = (double)ch->synth_len;
                if (fsynth > 0.0) {
                    while (ch->pos >= fsynth) ch->pos -= fsynth;
                }
            }

            /* Panning: AUD0+AUD3=left, AUD1+AUD2=right */
            if (i == 0 || i == 3) {
                mix_l += sample;
            } else {
                mix_r += sample;
            }
        }

        /* Apply master volume (0-128) */
        mix_l = mix_l * m->master_volume / 128;
        mix_r = mix_r * m->master_volume / 128;

        /* Clamp to int16 range */
        if (mix_l >  32767) mix_l =  32767;
        if (mix_l < -32768) mix_l = -32768;
        if (mix_r >  32767) mix_r =  32767;
        if (mix_r < -32768) mix_r = -32768;

        out[f * 2 + 0] = (int16_t)mix_l;
        out[f * 2 + 1] = (int16_t)mix_r;
    }

    SDL_UnlockMutex(m->mutex);
}

/* ================================================================== */
/* sm_set_volume — set master output volume (0 = silent, 128 = full)  */
/* ================================================================== */

void sm_set_volume(SM_Module *m, int vol)
{
    if (!m) return;
    if (vol < 0)   vol = 0;
    if (vol > 128) vol = 128;
    SDL_LockMutex(m->mutex);
    m->master_volume = vol;
    SDL_UnlockMutex(m->mutex);
}
