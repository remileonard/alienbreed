/*
 * Alien Breed SE 92 - C port
 * Alien module
 */

#include "alien.h"
#include "player.h"
#include "level.h"
#include "../hal/audio.h"
#include "../hal/video.h"
#include "../engine/tilemap.h"
#include "../engine/alien_gfx.h"
#include "../engine/tile_anim.h"
#include <string.h>
#include <stdlib.h>

Alien g_aliens[MAX_ALIENS];
int   g_alien_count             = 0;
WORD  g_global_aliens_extra_strength = 0;

/*
 * Player position cache — mirrors lbL0097EA / lbC0097F6 in main.asm.
 * The original only refreshes the player positions used by alien AI every
 * lbW0097F2 = 20 VBL frames (~400 ms at 50 Hz), giving aliens their
 * characteristic "drift past walls before correcting" feel.
 */
#define TARGET_REFRESH_FRAMES 20
static int s_cached_player_x[MAX_PLAYERS];
static int s_cached_player_y[MAX_PLAYERS];
static int s_cached_player_alive[MAX_PLAYERS];
static int s_target_refresh_countdown = 0;

/*
 * Boss retreat state — mirrors lbW009C62 (countdown) and lbL009C64 (flag).
 *
 * lbW009C62: retreat countdown.  Loaded with 40 by the random trigger
 *   (rand(300) < 2).  Decremented each frame while > 0.  When it hits 0
 *   the "permanent retreat if player alive" path (lbC009D80-lbC009D98) runs.
 *
 * lbL009C64: retreat direction flag.
 *   0 = chase (move TOWARD player) — only active when both players are dead.
 *   1 = retreat (move AWAY from player) — active whenever any player is alive.
 *   When retreating the boss moves without the ±4 px near-player threshold
 *   used in chase mode (mirrors the lbC009EB2/lbC009EFC inversion paths).
 *
 * Ref: lbC009CE2 @ main.asm#L6811-L6853.
 */
static int s_boss_retreat_countdown = 0;
static int s_boss_retreat_flag      = 0;

/*
 * Per-class alien combat stats — sourced directly from the spawning data
 * structs in main.asm.
 *
 * The original ASM stores per-class stats in each alien type struct at fixed
 * offsets (lbC00A872 / set_alien_random_speed @ main.asm#L7463-L7490):
 *   32(a1) = max speed
 *   34(a1) = base HP  (global_aliens_extra_strength is added on top)
 *
 * --- Normal large alien (is_facehugger=0, is_boss=0) ---
 *   Primary struct: lbW008F94 @ main.asm#L6001 (tile 0x0A, level_flag=0)
 *     32(a1)=2  → NORMAL_MAX_SPEED = 2
 *     34(a1)=$14=20 → NORMAL_BASE_HP = 20
 *   Secondary:     lbW009094 @ main.asm#L6046 (tile 0x0A, level_flag=$100)
 *     34(a1)=$1E=30  (same speed; higher HP for the secondary spawn path)
 *   The C port uses the primary struct value for all normal alien spawns.
 *   Per-level difficulty is handled exclusively by g_global_aliens_extra_strength
 *   (levels 9-12 add 5/10/15/20), mirroring the ASM exactly.
 *
 * --- Face hugger (is_facehugger=1) ---
 *   Primary struct: lbW009414 @ main.asm#L6148
 *     32(a1)=4  → FACEHUGGER_MAX_SPEED = 4
 *     34(a1)=8  → FACEHUGGER_BASE_HP = 8
 *
 * --- Boss (is_boss=1) ---
 *   Each boss_nbr spawns a group of 3 (boss_nbr 1-3) or 7 (boss_nbr 4) aliens.
 *   Stats per struct type (ASM offset 32=speed, 34=hp):
 *     boss_nbr 1: lbW009114(4,$100=256), lbW009154(4,$100=256), lbW009194(4,$100=256)
 *     boss_nbr 2: lbW009254(4,$100=256), lbW009294(4,$200=512), lbW0092D4(4,$100=256)
 *     boss_nbr 3: lbW009314(4,$140=320), lbW009354(5,$140=320), lbW009394(5,$140=320)
 *     boss_nbr 4: lbW009014(0,$40=64) ×7  (patrol AI lbC009AFC; speed=0 in struct,
 *                 but C uses BOSS_SPEED=4 as functional approximation)
 */

/* --- Normal large alien --- */
static const WORD NORMAL_BASE_HP    =  20;  /* lbW008F94+34 = $14 */
static const WORD NORMAL_MAX_SPEED  =   2;  /* lbW008F94+32 */

/* --- Face hugger (is_facehugger=1) --- */
static const WORD FACEHUGGER_BASE_HP   =  8;  /* lbW009414+34 = 8 */
static const WORD FACEHUGGER_MAX_SPEED =  4;  /* lbW009414+32 */

/* Default boss speed (used when is_boss=1 via standard spawn_alien_at path). */
static const WORD BOSS_SPEED = 4;  /* lbW009114+32 = 4 */

/*
 * Per-boss encounter spawn table — mirrors the boss_nbr_N handlers in
 * tile_boss_trigger @ main.asm#L5664-L5796.
 *
 * Each entry describes one alien in the boss group:
 *   row, col  : IFF tile coordinates (pixel = row/col × MAP_TILE_H/W).
 *               Derived from the ASM map-buffer address passed to patch_boss_door:
 *                 offset = addr - cur_map_top_ptr
 *                 IFF_row = (offset/248) - 3  (buffer has 3 header rows, C has none)
 *                 col     = (offset%248) / 2
 *               boss_nbr_2 secondary: lbW063DB8 falls outside MAP_ROWS=96 in the C
 *               port (IFF_row≈130 in the L6MA buffer), so the main boss position is
 *               used as a fallback.
 *   hp        : base HP from struct+34 (global_aliens_extra_strength added at spawn).
 *   speed     : base speed from struct+32 (boss_nbr_4 ASM speed=0; C uses 4 instead
 *               as a functional stand-in for the missing lbC009AFC patrol AI).
 *   boss_rank : 0=primary (lbC009CE2/lbC009AFC AI; triggers self-destruct on death),
 *               1+=secondary (lbC009C68 AI; tracks primary's position each frame).
 *
 * ASM references:
 *   boss_nbr_1 @ main.asm#L5716: 3 aliens, alien1=lbW0619E8, alien2/3=lbW064204
 *   boss_nbr_2 @ main.asm#L5744: 3 aliens, alien1=lbW05F7A8, alien2/3=lbW063DB8
 *   boss_nbr_3 @ main.asm#L5767: 3 aliens, all=lbW062872
 *   boss_nbr_4 @ main.asm#L5664: 7 aliens (lbW06188C … lbW0627FC)
 */
typedef struct { int row; int col; WORD hp; WORD speed; int boss_rank; } BossSpawnDef;
typedef struct { int count; BossSpawnDef defs[7]; } BossGroup;

/* Highest valid boss_nbr (boss_nbr=4 → 7 patrol aliens, lbC009AFC). */
#define MAX_BOSS_NBR 4

static const BossGroup k_boss_groups[5] = {
    /* [0] unused */
    { 0, {{0,0,0,0,0}} },

    /*
     * boss_nbr=1 (level 5, L4MA):
     *   alien1: lbW009114 @ lbW0619E8 → IFF(49,104); AI lbC009CE2; HP=$100 speed=4
     *   alien2: lbW009154 @ lbW064204 → IFF(91, 30); AI lbC009C68; HP=$100 speed=4
     *   alien3: lbW009194 @ lbW064204 → IFF(91, 30); AI lbC009C68; HP=$100 speed=4
     * boss_nbr_1 also calls patch_tiles(lbL020196,lbW062D52) to close entry door.
     * Ref: main.asm#L5716-L5742.
     */
    { 3, {
        { 49, 104, 256, 4, 0 },  /* primary  — lbW009114 @ lbW0619E8 */
        { 91,  30, 256, 4, 1 },  /* secondary — lbW009154 @ lbW064204 */
        { 91,  30, 256, 4, 1 },  /* secondary — lbW009194 @ lbW064204 */
        {0,0,0,0,0},{0,0,0,0,0},{0,0,0,0,0},{0,0,0,0,0}
    }},

    /*
     * boss_nbr=2 (level 7, L6MA — alien queen; level 8, L7MA — reactor shield):
     *
     * Level 7: three alien-queen bodies (1 primary + 2 secondaries).
     *   Killing the primary triggers level_start_destruction() via alien_kill()
     *   / lbC00A0EE @ main.asm#L7049.
     *
     * Level 8: same 3 boss aliens spawn as the REACTOR SHIELD — they orbit the
     *   reactor (tiles 0x2a-0x2d) and must be avoided or destroyed.  On level 8
     *   self-destruct can be triggered either by killing the primary boss OR by
     *   destroying all 4 reactor faces (6 hits each, check_reactors logic).
     *   Zone-boundary tiles (0x30-0x33) in L7MA announce "ZONE ONE".."ZONE SIX"
     *   as the player advances into the reactor room.
     *
     *   alien1: lbW009254 @ lbW05F7A8 → IFF(57,107); AI lbC009CE2; HP=$100 speed=4
     *   alien2: lbW009294 @ lbW063DB8 → IFF_row≈130 (out of C MAP_ROWS=96);
     *           fallback: use main pos (57,107); AI lbC009C68; HP=$200=512 speed=4
     *   alien3: lbW0092D4 @ lbW063DB8 → same fallback; HP=$100 speed=4
     * Ref: main.asm#L5744-L5765 (boss_nbr_2); lbC00A0EE @ main.asm#L7049 (die handler).
     */
    { 3, {
        { 57, 107, 256, 4, 0 },  /* primary   — lbW009254 @ lbW05F7A8 */
        { 57, 107, 512, 4, 1 },  /* secondary — lbW009294 @ lbW063DB8 (fallback pos) */
        { 57, 107, 256, 4, 1 },  /* secondary — lbW0092D4 @ lbW063DB8 (fallback pos) */
        {0,0,0,0,0},{0,0,0,0,0},{0,0,0,0,0},{0,0,0,0,0}
    }},

    /*
     * boss_nbr=3 (level 12, LBMA):
     *   alien1: lbW009314 @ lbW062872 → IFF(52,103); AI lbC009CE2; HP=$140=320 speed=4
     *   alien2: lbW009354 @ lbW062872 → same pos;    AI lbC009C68; HP=$140=320 speed=5
     *   alien3: lbW009394 @ lbW062872 → same pos;    AI lbC009C68; HP=$140=320 speed=5
     * boss_nbr_3 also calls patch_tiles twice to close doors.
     * Ref: main.asm#L5767-L5796.
     */
    { 3, {
        { 52, 103, 320, 4, 0 },  /* primary   — lbW009314 @ lbW062872 */
        { 52, 103, 320, 5, 1 },  /* secondary — lbW009354 @ lbW062872 */
        { 52, 103, 320, 5, 1 },  /* secondary — lbW009394 @ lbW062872 */
        {0,0,0,0,0},{0,0,0,0,0},{0,0,0,0,0},{0,0,0,0,0}
    }},

    /*
     * boss_nbr=4 (level 10, L9MA):
     *   7 patrol bosses using lbW009014 (AI lbC009AFC; HP=$40=64; speed=0 in ASM
     *   struct but C uses 4 as stand-in for the patrol AI).  All are rank=0 since
     *   lbC009AFC has no lbC009F62 self-destruct branch; exit is via lbW0004EE.
     *   Positions derived from ASM map-buffer addresses relative to anchor
     *   lbW06188C (IFF row=46, col=97), map_start=0x05E852:
     *     lbW06188C → buffer_row=49, IFF_row=46, col=97
     *     lbW061AFC → buffer_row=52, IFF_row=49, col=37
     *     lbW061D6C → buffer_row=54, IFF_row=51, col=101
     *     lbW061FDC → buffer_row=57, IFF_row=54, col=41
     *     lbW06224C → buffer_row=59, IFF_row=56, col=105
     *     lbW06258C → buffer_row=63, IFF_row=60, col=25
     *     lbW0627FC → buffer_row=65, IFF_row=62, col=89
     * Ref: boss_nbr_4 @ main.asm#L5664-L5714.
     */
    { 7, {
        { 46,  97, 64, 4, 0 },  /* lbW06188C */
        { 49,  37, 64, 4, 0 },  /* lbW061AFC */
        { 51, 101, 64, 4, 0 },  /* lbW061D6C */
        { 54,  41, 64, 4, 0 },  /* lbW061FDC */
        { 56, 105, 64, 4, 0 },  /* lbW06224C */
        { 60,  25, 64, 4, 0 },  /* lbW06258C */
        { 62,  89, 64, 4, 0 },  /* lbW0627FC */
    }}
};

/*
 * Projectile struct — extends simple position/velocity with weapon-specific
 * behaviour flags derived from weapons_behaviour_table @ main.asm#L10000:
 *
 *   weapon_type   : WEAPON_* constant (used to select rendering and behaviour)
 *   penetrating   : 1 = passes through aliens (weapons 4/5/7).
 *                   From offset 18(a3) in the ASM projectile struct, copied from
 *                   player offset 270(a0) = the 5th field (offset 8 per entry) of
 *                   weapons_attr_table (=01 for PLASMAGUN/FLAMETHROWER/LAZER).
 *                   Mirrors move.w 270(a0),18(a3) @ main.asm#L9486.
 *                   Ref: tst.w 18(a1) @ main.asm#L7739.
 *   lifetime      : frames remaining before auto-expire (-1 = infinite).
 *                   Used by FLAMETHROWER (8 ticks @ 25 Hz = 64 px range).
 *                   Matches lbL018D06 8-entry animation list (delay=1 each).
 *   bounce_count  : remaining wall bounces.
 *                   FLAMEARC: 1 bounce (cmp.w #2,24(a3) @ main.asm#L9712).
 *                   LAZER: 5 bounces (cmp.w #6,24(a3) @ main.asm#L9716).
 *                   Others: 0.
 *   direction     : firing direction (1-8, PLAYER_FACE_*) used for bounce axis
 *                   selection and sprite rendering.
 *   impact_active : 1 = playing end-of-life impact animation (8 frames).
 *                   Mirrors lbL018CBA BOB animation @ main.asm#L13925.
 *   impact_x/y    : world position where the impact occurred.
 *   impact_frame  : current impact animation frame (0-7).
 *   impact_timer  : countdown per impact frame (ticks per frame).
 */
typedef struct {
    WORD  x, y;
    WORD  vx, vy;
    WORD  strength;
    int   player_idx;     /* which player fired it */
    int   active;         /* 1=flying, 0=inactive */
    int   weapon_type;    /* WEAPON_* */
    int   penetrating;    /* 1=pass through aliens */
    int   lifetime;       /* ticks remaining; -1=infinite */
    int   bounce_count;   /* remaining wall bounces */
    int   direction;      /* firing direction 1-8 (PLAYER_FACE_*) */
    /* Impact animation (played after projectile deactivates on wall/alien hit) */
    int   impact_active;      /* 1=impact animation playing */
    int   impact_x;           /* world x of impact */
    int   impact_y;           /* world y of impact */
    int   impact_frame;       /* 0-7 */
    int   impact_timer;       /* ticks remaining on current impact frame */
    int   flight_anim_frame;  /* cycling frame for animated in-flight BOBs (FLAMEARC: 0-7) */
} Projectile;

/*
 * Impact animation atlas coordinates (BOBs 56-63 from lbL018CBA @ main.asm#L13925).
 * Each entry: {atlas_x, atlas_y, width=16, height=16}.
 * Ref: lbW018D4A entries 56-63 @ main.asm#L14001-L14008.
 */
static const int k_impact_frames[8][4] = {
    { 192, 224, 16, 14 },   /* frame 0: lbL0185CE → lbW0188CE[56]: $C0,$E0,$10,$0E */
    { 208, 224, 16, 14 },   /* frame 1: lbL0185FE → lbW0188CE[57]: $D0,$E0,$10,$0E */
    { 224, 224, 16, 14 },   /* frame 2: lbL01862E → lbW0188CE[58]: $E0,$E0,$10,$0E */
    { 240, 224, 16, 14 },   /* frame 3: lbL01865E → lbW0188CE[59]: $F0,$E0,$10,$0E */
    { 192, 240, 16, 14 },   /* frame 4: lbL01868E → lbW0188CE[60]: $C0,$F0,$10,$0E */
    { 208, 240, 16, 14 },   /* frame 5: lbL0186BE → lbW0188CE[61]: $D0,$F0,$10,$0E */
    { 224, 240, 16, 14 },   /* frame 6: lbL0186EE → lbW0188CE[62]: $E0,$F0,$10,$0E */
    { 240, 240, 16, 14 },   /* frame 7: lbL01871E → lbW0188CE[63]: $F0,$F0,$10,$0E */
};
#define IMPACT_ANIM_FRAMES   8
#define IMPACT_FRAME_TICKS   1  /* 1 tick per impact frame (25 Hz cadence) */

/*
 * Flamethrower flame atlas coordinates (BOBs 56-63 used for in-flight flame,
 * same sprite region as impact; lbL018D06 @ main.asm#L13935).
 * At 25 Hz with delay=1 each, the 8 frames play over FLAME_LIFETIME_TICKS ticks.
 */

/*
 * Lazer in-flight atlas coordinates: 16×14 sprites from lbW0188CE entries 68-71.
 * Mapping from lbL00EDAA/lbL00EDCE-lbL00EE22 @ main.asm#L10119-L10135:
 *   dirs 0,1,5 → BOB entry 68: lbW0188CE $100,$F0,$10,$0E = (256,240,16,14)
 *   dirs 2,6   → BOB entry 71: lbW0188CE $130,$F0,$10,$0E = (304,240,16,14)
 *   dirs 3,7   → BOB entry 70: lbW0188CE $120,$F0,$10,$0E = (288,240,16,14)
 *   dirs 4,8   → BOB entry 69: lbW0188CE $110,$F0,$10,$0E = (272,240,16,14)
 */
static const int k_lazer_atlas[9][4] = {
    { 256, 240, 16, 14 },  /* dir 0 idle  → lbW0188CE[68] */
    { 256, 240, 16, 14 },  /* dir 1 up    → lbW0188CE[68] */
    { 304, 240, 16, 14 },  /* dir 2 up-R  → lbW0188CE[71] */
    { 288, 240, 16, 14 },  /* dir 3 right → lbW0188CE[70] */
    { 272, 240, 16, 14 },  /* dir 4 dn-R  → lbW0188CE[69] */
    { 256, 240, 16, 14 },  /* dir 5 down  → lbW0188CE[68] */
    { 304, 240, 16, 14 },  /* dir 6 dn-L  → lbW0188CE[71] */
    { 288, 240, 16, 14 },  /* dir 7 left  → lbW0188CE[70] */
    { 272, 240, 16, 14 },  /* dir 8 up-L  → lbW0188CE[69] */
};

/*
 * MACHINEGUN in-flight BOB sprites: direction-dependent 16×14 sprite.
 * BOB animation structs lbL017FCE-lbL01811E → lbL01790A entries 24-31.
 * Each maps to one lbW0188CE descriptor (entries 24-31, all at y=176=0xB0):
 *   dir 0,1 → entry 24: $40,$B0,$10,$0E = (64, 176,16,14)
 *   dir 2   → entry 25: $50,$B0  dir 3 → entry 26: $60,$B0
 *   dir 4   → entry 27: $70,$B0  dir 5 → entry 28: $80,$B0
 *   dir 6   → entry 29: $90,$B0  dir 7 → entry 30: $A0,$B0
 *   dir 8   → entry 31: $B0,$B0
 * In the ASM the bullet shows for 1 frame then turns blank (delay 0 then
 * 32000); we render it for the full flight as there is no animation-frame
 * timer in this C port.
 * Ref: lbL00EACA @ main.asm#L10010-L10034.
 */
static const int k_machinegun_atlas[9][4] = {
    {  64, 176, 16, 14 },  /* dir 0       → lbW0188CE[24] */
    {  64, 176, 16, 14 },  /* dir 1 up    → lbW0188CE[24] */
    {  80, 176, 16, 14 },  /* dir 2 up-R  → lbW0188CE[25] */
    {  96, 176, 16, 14 },  /* dir 3 right → lbW0188CE[26] */
    { 112, 176, 16, 14 },  /* dir 4 dn-R  → lbW0188CE[27] */
    { 128, 176, 16, 14 },  /* dir 5 down  → lbW0188CE[28] */
    { 144, 176, 16, 14 },  /* dir 6 dn-L  → lbW0188CE[29] */
    { 160, 176, 16, 14 },  /* dir 7 left  → lbW0188CE[30] */
    { 176, 176, 16, 14 },  /* dir 8 up-L  → lbW0188CE[31] */
};

/*
 * TWINFIRE in-flight BOB sprites: direction-dependent 16×14 static frame.
 * BOB animation structs lbL017B4E-lbL017C9E → lbL01790A entries 0-7.
 * Each maps to one lbW0188CE descriptor (entries 0-7, all at y=160=0xA0):
 *   dir 0,1 → entry 0: $C0,$A0,$10,$0E = (192,160,16,14)
 *   dir 2   → entry 1: $D0,$A0  dir 3 → entry 2: $E0,$A0
 *   dir 4   → entry 3: $F0,$A0  dir 5 → entry 4: $100,$A0
 *   dir 6   → entry 5: $110,$A0 dir 7 → entry 6: $120,$A0
 *   dir 8   → entry 7: $130,$A0
 * Ref: lbL00EB8E/lbL00EBB2-lbL00EC06 @ main.asm#L10036-L10052.
 */
static const int k_twinfire_atlas[9][4] = {
    { 192, 160, 16, 14 },  /* dir 0       → lbW0188CE[0] */
    { 192, 160, 16, 14 },  /* dir 1 up    → lbW0188CE[0] */
    { 208, 160, 16, 14 },  /* dir 2 up-R  → lbW0188CE[1] */
    { 224, 160, 16, 14 },  /* dir 3 right → lbW0188CE[2] */
    { 240, 160, 16, 14 },  /* dir 4 dn-R  → lbW0188CE[3] */
    { 256, 160, 16, 14 },  /* dir 5 down  → lbW0188CE[4] */
    { 272, 160, 16, 14 },  /* dir 6 dn-L  → lbW0188CE[5] */
    { 288, 160, 16, 14 },  /* dir 7 left  → lbW0188CE[6] */
    { 304, 160, 16, 14 },  /* dir 8 up-L  → lbW0188CE[7] */
};

/*
 * FLAMEARC in-flight BOB sprites: 8-frame animated 16×14 cycling sequence.
 * BOB animation structs lbL017E4E-lbL017F9E → lbL01790A entries 16-23.
 * Each maps to one lbW0188CE descriptor (entries 16-23, all at y=160=0xA0):
 *   frame 0 → entry 16: $40,$A0,$10,$0E = (64,160,16,14)
 *   frame 1 → entry 17: $50,$A0  frame 2 → entry 18: $60,$A0
 *   frame 3 → entry 19: $70,$A0  frame 4 → entry 20: $80,$A0
 *   frame 5 → entry 21: $90,$A0  frame 6 → entry 22: $A0,$A0
 *   frame 7 → entry 23: $B0,$A0
 * All directions share the same 8-frame loop (delay=0 → one frame per tick).
 * Ref: lbL00EC36 @ main.asm#L10063-L10070.
 */
static const int k_flamearc_atlas[8][4] = {
    {  64, 160, 16, 14 },  /* frame 0: lbL017E4E → lbW0188CE[16] */
    {  80, 160, 16, 14 },  /* frame 1: lbL017E7E → lbW0188CE[17] */
    {  96, 160, 16, 14 },  /* frame 2: lbL017EAE → lbW0188CE[18] */
    { 112, 160, 16, 14 },  /* frame 3: lbL017EDE → lbW0188CE[19] */
    { 128, 160, 16, 14 },  /* frame 4: lbL017F0E → lbW0188CE[20] */
    { 144, 160, 16, 14 },  /* frame 5: lbL017F3E → lbW0188CE[21] */
    { 160, 160, 16, 14 },  /* frame 6: lbL017F6E → lbW0188CE[22] */
    { 176, 160, 16, 14 },  /* frame 7: lbL017F9E → lbW0188CE[23] */
};
#define FLAMEARC_ANIM_FRAMES 8

/*
 * PLASMAGUN in-flight BOB sprites: direction-dependent 16×14 static frame.
 * BOB animation structs lbL01814E-lbL01829E → lbL01790A entries 32-39.
 * Each maps to one lbW0188CE descriptor (entries 32-39):
 *   dir 0,1 → entry 32: $00,$A0,$10,$0E = (0, 160,16,14)
 *   dir 2   → entry 33: $10,$A0  dir 3 → entry 34: $20,$A0
 *   dir 4   → entry 35: $30,$A0  dir 5 → entry 36: $00,$B0
 *   dir 6   → entry 37: $10,$B0  dir 7 → entry 38: $20,$B0
 *   dir 8   → entry 39: $30,$B0
 * Ref: lbL00ECFE/lbL00ED22-lbL00ED76 @ main.asm#L10090-L10106.
 */
static const int k_plasmagun_atlas[9][4] = {
    {  0, 160, 16, 14 },  /* dir 0       → lbW0188CE[32] */
    {  0, 160, 16, 14 },  /* dir 1 up    → lbW0188CE[32] */
    { 16, 160, 16, 14 },  /* dir 2 up-R  → lbW0188CE[33] */
    { 32, 160, 16, 14 },  /* dir 3 right → lbW0188CE[34] */
    { 48, 160, 16, 14 },  /* dir 4 dn-R  → lbW0188CE[35] */
    {  0, 176, 16, 14 },  /* dir 5 down  → lbW0188CE[36] */
    { 16, 176, 16, 14 },  /* dir 6 dn-L  → lbW0188CE[37] */
    { 32, 176, 16, 14 },  /* dir 7 left  → lbW0188CE[38] */
    { 48, 176, 16, 14 },  /* dir 8 up-L  → lbW0188CE[39] */
};

/*
 * SIDEWINDERS in-flight BOB sprites: direction-dependent 16×14 static frame.
 * BOB animation structs lbL017CCE-lbL017E1E → lbL01790A entries 8-15.
 * Each maps to one lbW0188CE descriptor (entries 8-15, all at y=176=0xB0):
 *   dir 0,1 → entry 8:  $C0,$B0,$10,$0E = (192,176,16,14)
 *   dir 2   → entry 9:  $D0,$B0  dir 3 → entry 10: $E0,$B0
 *   dir 4   → entry 11: $F0,$B0  dir 5 → entry 12: $100,$B0
 *   dir 6   → entry 13: $110,$B0 dir 7 → entry 14: $120,$B0
 *   dir 8   → entry 15: $130,$B0
 * Ref: lbL00EC7A/lbL00EC9E-lbL00ECF2 @ main.asm#L10072-L10088.
 */
static const int k_sidewinders_atlas[9][4] = {
    { 192, 176, 16, 14 },  /* dir 0       → lbW0188CE[8]  */
    { 192, 176, 16, 14 },  /* dir 1 up    → lbW0188CE[8]  */
    { 208, 176, 16, 14 },  /* dir 2 up-R  → lbW0188CE[9]  */
    { 224, 176, 16, 14 },  /* dir 3 right → lbW0188CE[10] */
    { 240, 176, 16, 14 },  /* dir 4 dn-R  → lbW0188CE[11] */
    { 256, 176, 16, 14 },  /* dir 5 down  → lbW0188CE[12] */
    { 272, 176, 16, 14 },  /* dir 6 dn-L  → lbW0188CE[13] */
    { 288, 176, 16, 14 },  /* dir 7 left  → lbW0188CE[14] */
    { 304, 176, 16, 14 },  /* dir 8 up-L  → lbW0188CE[15] */
};

#define MAX_PROJECTILES 32
static Projectile s_projectiles[MAX_PROJECTILES];

/*
 * Fallback velocity used when a bounced projectile ends up with zero vx or vy.
 * Mirrors lbW0004D6 (dc.w 8) @ main.asm#L197, which is negated on every bounce
 * call (neg.w lbW0004D6 @ main.asm#L9759), alternating between +8 and -8.
 */
static int s_bounce_fallback = 8;

/*
 * Map a post-bounce LAZER velocity pair (vx,vy) to a direction index (1–8).
 * Matches lbW00EA1E velocity table @ main.asm#L9996 and the direction-BOB
 * lookup loop lbC00E68A-lbC00E698 @ main.asm#L9772-L9781.
 *
 * lbW00EA1E: (0,-8) (8,-8) (8,0) (8,8) (0,8) (-8,8) (-8,0) (-8,-8)
 *             dir1   dir2   dir3  dir4   dir5  dir6   dir7   dir8
 * Fallback when no entry matches: direction 8 (last entry = lbL00EE22).
 */
static int lazer_velocity_to_dir(int16_t vx, int16_t vy)
{
    static const int16_t k[8][2] = {
        {  0, -8 }, /* dir 1: up         */
        {  8, -8 }, /* dir 2: up-right   */
        {  8,  0 }, /* dir 3: right      */
        {  8,  8 }, /* dir 4: down-right */
        {  0,  8 }, /* dir 5: down       */
        { -8,  8 }, /* dir 6: down-left  */
        { -8,  0 }, /* dir 7: left       */
        { -8, -8 }, /* dir 8: up-left    */
    };
    for (int j = 0; j < 8; j++) {
        if (vx == k[j][0] && vy == k[j][1])
            return j + 1;
    }
    return 8; /* fallback: lbL00EE22, same as ASM when d1 reaches 8 without match */
}

/* Score awarded per alien kill */
#define ALIEN_SCORE_VALUE 100

/*
 * Spawn point list — mirrors the two spawn slots (lbL00D29A / lbL00D2AA) from
 * main.asm, generalised to support any number of map spawn tiles plus
 * player-triggered facehugger hatches.
 *
 * Initial countdown value of 20 matches `move.w #20, 8(a0)` in lbC00D236.
 * Expanded viewport margin of 80 px matches the ±80 offsets in lbC00D17E.
 * (Ref: lbC00D17E / lbC00D1B4 / lbC00D22A / lbC00D236 @ main.asm#L8547-L8623)
 */
#define MAX_SPAWN_POINTS        256
#define SPAWN_COUNTDOWN_INIT     20  /* frames until first spawn once in viewport */
#define SPAWN_VIEWPORT_MARGIN    80  /* px beyond screen edges for expanded viewport */

typedef struct {
    WORD world_x, world_y;  /* pixel centre of the spawn tile */
    int  countdown;          /* decrements while tile is in viewport; spawn at −1 */
    int  alien_type;         /* 1-based alien type (1=weakest … 7=strongest) */
    int  active;             /* 1 = slot occupied */
    int  one_shot;           /* 1 = deactivate after first spawn (facehugger hatch) */
    int  is_hole_spawn;      /* 1 = tile 0x34 (TILE_ALIEN_HOLE): play hatch sound +
                              * zoom animation.  Ref: lbC0049EA @ main.asm#L2570,
                              * do_alien_hatch sets play_alien_hatching_sample=1 and
                              * alien+76 (hatch_timer) = struct+46 = $14 = 20. */
    int  is_facehugger;      /* 1 = small face hugger (tile 0x29 or hatch on level 12):
                              * spawns Alien with is_facehugger=1, rendered as 16×16.
                              * Ref: lbW009414 (size 4,4,8,8) via tile 0x29 dispatch
                              * lbC0049D6 / level-12 dispatch lbC004A28 @ main.asm. */
    int  is_boss;            /* reserved for future boss implementation.
                              * Actual bosses are spawned via tile_boss_trigger
                              * (boss_nbr 1-4, structs lbW009114/lbW009254/
                              * lbW009374/lbW009014, AI lbC009CE2/lbC009AFC).
                              * The large aliens from tile 0x0A are regular
                              * aliens; this field is always 0 for those.
                              * Ref: tile_boss_trigger @ main.asm#L5632. */
    int  spawned_alien_idx;  /* index of the alien last spawned from this point,
                              * or −1 if none yet.  Re-spawn is suppressed while
                              * g_aliens[spawned_alien_idx].alive != 0 so that at
                              * most one alien exists per spawn point at any time.
                              * (Mirrors the cur_alien_dats bbox overlap check in
                              * lbC00A7EA @ main.asm, generalised to per-point
                              * tracking per user requirement.) */
} SpawnPoint;

static SpawnPoint s_spawn_points[MAX_SPAWN_POINTS];
static int        s_spawn_count = 0;

/* -----------------------------------------------------------------------
 * Return the alien type appropriate for the current level.
 * Ref: alien type selection @ main.asm#L5918-L5973.
 * ----------------------------------------------------------------------- */
static int alien_type_for_level(void)
{
    int t = 1;
    if (g_cur_level >= 6)  t = 2;
    if (g_cur_level >= 8)  t = 3;
    if (g_cur_level >= 10) t = 4;
    if (g_cur_level == 11) t = 5;
    if (g_cur_level == 12) t = 6;
    if (g_cur_level >= 12) t = 7;
    return t;
}

/* Forward declaration — defined later in this file. */
static int alien_overlaps_other(int self_idx, int nx, int ny);

/* -----------------------------------------------------------------------
 * Place one alien at world-pixel position (wx, wy) of the given type.
 *
 * Three mutually-exclusive classes, each with distinct stats sourced from
 * the ASM data structs (offsets 32/34 from each lbWxxxxxx struct):
 *
 *   Normal large alien (is_facehugger=0, is_boss=0):
 *     speed    = NORMAL_MAX_SPEED (2) — lbW008F94+32
 *     strength = NORMAL_BASE_HP (20) + g_global_aliens_extra_strength
 *
 *   Face hugger (is_facehugger=1):
 *     speed    = FACEHUGGER_MAX_SPEED (4) — lbW009414+32
 *     strength = FACEHUGGER_BASE_HP (8) + g_global_aliens_extra_strength
 *
 *   Boss (is_boss=1):
 *     speed    = BOSS_SPEED (4) — lbW009114+32
 *     strength = BOSS_BASE_HP (256) + g_global_aliens_extra_strength
 *
 * Returns the index in g_aliens[] of the newly placed alien, or -1 if
 * no slot was available.  Dead slots are recycled before appending new
 * ones so that the pool does not exhaust after many spawns/deaths.
 * ----------------------------------------------------------------------- */
static int spawn_alien_at(int wx, int wy, int alien_type,
                          int is_facehugger, int is_boss)
{
    /* Recycle the first dead slot before falling back to appending. */
    int idx = -1;
    for (int i = 0; i < g_alien_count; i++) {
        if (g_aliens[i].alive == 0) { idx = i; break; }
    }
    if (idx < 0) {
        if (g_alien_count >= MAX_ALIENS) return -1;
        idx = g_alien_count++;
    }

    /* Do not spawn if the target position already overlaps a living alien.
     * The alien-alien separation logic only cancels *movement* into an overlap;
     * it cannot resolve an overlap that already exists at birth, which leaves
     * both aliens permanently stuck.  Return -1 so the caller retries later. */
    if (alien_overlaps_other(idx, wx, wy)) return -1;

    Alien *a         = &g_aliens[idx];
    a->pos_x         = (WORD)wx;
    a->pos_y         = (WORD)wy;
    a->alive         = 1;
    a->type_idx      = alien_type - 1;
    a->death_frame   = 0;
    a->is_facehugger = is_facehugger;
    a->is_boss       = is_boss;
    a->boss_rank     = 0;   /* default: primary / not-a-boss */

    /*
     * Per-class speed and HP initialisation — mirrors the three code paths
     * in the ASM spawning routine (lbC00A872 / set_alien_random_speed
     * @ main.asm#L7463-L7490):
     *
     *   Normal alien  (is_facehugger=0, is_boss=0):
     *     speed    = NORMAL_MAX_SPEED (= lbW008F94+32 = 2)
     *     strength = NORMAL_BASE_HP + g_global_aliens_extra_strength
     *                (= lbW008F94+34 = $14 = 20 + extra)
     *
     *   Face hugger   (is_facehugger=1):
     *     speed    = FACEHUGGER_MAX_SPEED (= lbW009414+32 = 4)
     *     strength = FACEHUGGER_BASE_HP + g_global_aliens_extra_strength
     *                (= lbW009414+34 = 8 + extra)
     *
     *   Boss          (is_boss=1):
     *     Default speed/HP for the boss class; overridden by caller for
     *     per-struct values from k_boss_groups (see alien_boss_trigger).
     *     speed    = BOSS_SPEED (= lbW009114+32 = 4; exact — no random in ASM)
     *     strength = 256 + g_global_aliens_extra_strength
     *                (= lbW009114+34 = $100 = 256 + extra; overridden after return)
     */
    if (is_facehugger) {
        a->speed    = FACEHUGGER_MAX_SPEED;
        a->strength = (WORD)(FACEHUGGER_BASE_HP + g_global_aliens_extra_strength);
    } else if (is_boss) {
        a->speed    = BOSS_SPEED;
        a->strength = (WORD)(256 + g_global_aliens_extra_strength);
    } else {
        a->speed    = NORMAL_MAX_SPEED;
        a->strength = (WORD)(NORMAL_BASE_HP + g_global_aliens_extra_strength);
    }
    return idx;
}

void alien_init_variables(void)
{
    memset(g_aliens,       0, sizeof(g_aliens));
    memset(s_projectiles,  0, sizeof(s_projectiles));
    memset(s_spawn_points, 0, sizeof(s_spawn_points));
    memset(s_cached_player_x,     0, sizeof(s_cached_player_x));
    memset(s_cached_player_y,     0, sizeof(s_cached_player_y));
    memset(s_cached_player_alive, 0, sizeof(s_cached_player_alive));
    g_alien_count = 0;
    s_spawn_count = 0;
    s_target_refresh_countdown = 0;
    s_boss_retreat_countdown   = 0;
    s_boss_retreat_flag        = 0;
}

/*
 * Scan the loaded map for alien spawn tiles and register them as deferred
 * spawn points.  No aliens are created immediately — spawning is driven by
 * alien_spawn_tick() which fires when each tile enters the expanded viewport,
 * exactly as in the original ASM.
 *
 * Tile attributes that mark alien spawn locations:
 *   0x28 – TILE_ALIEN_SPAWN_BIG   (respawning large alien, e.g. lbW008F94/lbW009094)
 *   0x29 – TILE_ALIEN_SPAWN_SMALL (respawning small alien, e.g. lbW008FD4/lbW009414)
 *   0x34 – TILE_ALIEN_HOLE        (respawning hole: alien emerges with zoom animation
 *                                  and hatch sound.  Ref: lbC0049EA → lbW008F94[52]=
 *                                  lbL01B6F6 @ main.asm#L2204; do_alien_hatch#L7817.)
 *
 * In the original game the second tile-scan table (lbC0041B8 loop) registers
 * nearby tiles of these types into the two spawn slots (lbL00D29A / lbL00D2AA)
 * via lbC00D22A → lbC00D236.  Pre-registering all map tiles at level load is
 * an equivalent approximation (the viewport gate in alien_spawn_tick() ensures
 * spawning only happens when the tile is on screen).
 *
 * Ref: lbC0049EA / lbC004A18 / lbC004A28 → lbC00D22A → lbC00D236 @
 *      main.asm#L2204-L2585; lbC00D17E / lbC00D1B4 @ main.asm#L8547-L8575.
 */
void alien_spawn_from_map(void)
{
    int alien_type = alien_type_for_level();
    s_spawn_count  = 0;

    for (int row = 0; row < MAP_ROWS && s_spawn_count < MAX_SPAWN_POINTS; row++) {
        for (int col = 0; col < MAP_COLS && s_spawn_count < MAX_SPAWN_POINTS; col++) {
            UBYTE attr = tilemap_attr(&g_cur_map, col, row);
            if (attr != TILE_ALIEN_SPAWN_BIG  &&
                attr != TILE_ALIEN_SPAWN_SMALL &&
                attr != TILE_ALIEN_HOLE) continue;

            SpawnPoint *sp = &s_spawn_points[s_spawn_count++];
            sp->world_x    = (WORD)(col * MAP_TILE_W + MAP_TILE_W / 2);
            sp->world_y    = (WORD)(row * MAP_TILE_H + MAP_TILE_H / 2);
            sp->countdown  = SPAWN_COUNTDOWN_INIT;
            sp->alien_type = alien_type;
            sp->active     = 1;
            sp->one_shot   = 0;
            /* Tile 0x34 (TILE_ALIEN_HOLE): alien emerges with zoom animation and
             * hatch sound, mirroring lbC0049EA → lbW008F94[52]=lbL01B6F6 in ASM. */
            sp->is_hole_spawn      = (attr == TILE_ALIEN_HOLE) ? 1 : 0;
            /* Tile 0x29 (TILE_ALIEN_SPAWN_SMALL) always spawns small face huggers.
             * Ref: tile dispatch lbC0049D6 → lbW008FD4 (size 4,4,8,8) for all
             * level flags; lbC004A28 → lbW009414 for level_flag=1024 (level 12).
             * Both data structs use the small-alien animation table (lbL0094FC /
             * lbL00969C) which references 16×16 BOBs at atlas x=256-304. */
            sp->is_facehugger      = (attr == TILE_ALIEN_SPAWN_SMALL) ? 1 : 0;
            /* Map-scanned tiles (0x28, 0x29, 0x34) are never bosses.
             * Bosses are only spawned by TILE_FACEHUGGER_HATCH (0x0A) on
             * non-level-12 levels via alien_spawn_near(). */
            sp->is_boss            = 0;
            sp->spawned_alien_idx  = -1;
        }
    }
}

/*
 * Register a one-shot spawn point at (wx, wy).
 *
 * Called when the player triggers a facehugger hatch tile (0x0A).  The
 * original game queues the tile through lbC00D22A → lbC00D236, which sets up
 * a spawn slot with countdown = 20, then lbC00D1B4 fires it once the tile is
 * in the viewport.  We replicate that deferred behaviour here.
 *
 * The tile itself is patched to floor by the caller before this returns, so
 * the hatch can never be triggered a second time.
 *
 * Ref: tile_facehuggers_hatch → lbC0082C0/CE/8302 → lbC00D22A → lbC00D236
 *      @ main.asm#L5414-L8623.
 */
void alien_spawn_near(int wx, int wy)
{
    if (s_spawn_count >= MAX_SPAWN_POINTS) return;

    SpawnPoint *sp = &s_spawn_points[s_spawn_count++];
    sp->world_x    = (WORD)wx;
    sp->world_y    = (WORD)wy;
    sp->countdown  = SPAWN_COUNTDOWN_INIT;
    sp->alien_type = alien_type_for_level();
    sp->active     = 1;
    sp->one_shot   = 1;
    sp->is_hole_spawn = 0;
    /* tile_facehuggers_hatch (tile 0x0A) behaviour depends on level_flag:
     *   level_flag=1024 (level 12)         → lbW009414 (4,4,8,8)  = face hugger
     *   level_flag=0    (levels 2, 10, 11) → lbW008F94 (10,10,20,20) = regular large alien
     *   level_flag=256  (levels 7, 8, 9)   → lbW009094 (10,10,20,20) = regular large alien
     *   other level_flags (1,3,4,5,6)      → rts, nothing spawned
     * All three struct types use the standard AI (lbC00987E). Actual bosses are
     * spawned by the separate tile_boss_trigger mechanism (not tile 0x0A).
     * Ref: tile_facehuggers_hatch lbC0082C0/CE/8302 @ main.asm#L5428-L5441. */
    sp->is_facehugger     = (g_cur_level == 12) ? 1 : 0;
    sp->is_boss           = 0;
    sp->spawned_alien_idx = -1;
}

/*
 * Per-frame viewport scan — mirrors lbC00D17E / lbC00D1B4 @ main.asm.
 *
 * ASM zone (lbC00D17E):
 *   vp_left   = map_pos_x − 80        vp_right  = map_pos_x + 400 (screen+80)
 *   vp_top    = map_pos_y − 80        vp_bottom = map_pos_y + 416 (screen+80)
 *
 * In the original game, slots are registered via lbC00D22A ONLY when a tile
 * enters the viewport during scrolling (tile action fired at scroll edge).
 * So a registered slot always starts near the screen border, and the 20-frame
 * countdown (lbC00D236: `move.w #20, 8(a0)` / reload from alien struct offset
 * 44 = $14 = 20) fires just as the tile approaches visibility.
 *
 * The C port pre-registers all map spawn tiles at level load, so we must
 * replicate the "scroll-edge trigger" by restricting spawn checks to the
 * 80-pixel approach band — the area that is inside the expanded viewport but
 * NOT yet visible on screen.  When the tile is on screen proper the countdown
 * is simply paused; when it leaves the expanded zone entirely the countdown
 * and the dead-alien reference are reset so the next approach starts fresh.
 *
 * Summary of per-tick state machine for map spawn points (one_shot = 0):
 *   OUTSIDE expanded vp   → reset countdown; clear dead alien ref (fresh state)
 *   ON SCREEN proper      → pause (do nothing)
 *   IN APPROACH BAND      → decrement countdown; when expired check occupancy:
 *                             alien alive  → reload timer, wait
 *                             alien dead   → spawn, reload timer
 *
 * Facehugger hatches (one_shot = 1) use the simpler full-expanded-vp zone
 * since they are always triggered by direct player contact (already visible).
 *
 * Alien struct offset 44 = $14 = 20 confirms the reload value (lbC00D1B4:
 * `move.w 44(a1), 8(a0)` with a1 = lbW008F94 etc. @ main.asm#L8567).
 *
 * Called from alien_update_all() every game tick.
 */

void alien_spawn_tick(void)
{
    /* Screen visible area */
    int sc_left   = g_camera_x;
    int sc_right  = g_camera_x + SCREEN_W;
    int sc_top    = g_camera_y;
    int sc_bottom = g_camera_y + SCREEN_H;

    /* Expanded viewport: 80 px beyond each screen edge (ref lbC00D17E). */
    int vp_left   = g_camera_x - SPAWN_VIEWPORT_MARGIN;
    int vp_right  = g_camera_x + SCREEN_W + SPAWN_VIEWPORT_MARGIN;
    int vp_top    = g_camera_y - SPAWN_VIEWPORT_MARGIN;
    int vp_bottom = g_camera_y + SCREEN_H + SPAWN_VIEWPORT_MARGIN;

    for (int i = 0; i < s_spawn_count; i++) {
        SpawnPoint *sp = &s_spawn_points[i];
        if (!sp->active) continue;

        int in_expanded = (sp->world_x >= vp_left  && sp->world_x < vp_right &&
                           sp->world_y >= vp_top    && sp->world_y < vp_bottom);

        if (!in_expanded) {
            /*
             * Tile is outside the expanded zone: reset countdown.
             * Also clear the alien reference if that alien has since died,
             * so the next approach can hatch a fresh one (mirrors lbC00D220
             * which clears the slot pointer entirely).
             */
            sp->countdown = SPAWN_COUNTDOWN_INIT;
            if (sp->spawned_alien_idx >= 0 &&
                g_aliens[sp->spawned_alien_idx].alive == 0)
                sp->spawned_alien_idx = -1;
            continue;
        }

        if (!sp->one_shot && !sp->is_hole_spawn) {
            /*
             * Map spawn point (tile 0x28/0x29): only count down in the 80-px
             * off-screen approach band.  If the tile is already on screen
             * the countdown is paused — an alien appearing out of thin air
             * on a visible tile looks wrong.
             * (In the original ASM, lbC00D22A registers the slot only when
             * the tile first enters the viewport during scrolling, so it
             * always starts at the screen edge — never from mid-screen.)
             *
             * Tile 0x34 (is_hole_spawn) is intentionally excluded: it MUST
             * spawn when the tile is visible so the zoom-in hatch animation
             * is seen by the player.  The ASM lbC00D17E applies the same
             * ±80px zone to 0x34 without any stricter visibility gate.
             */
            int on_screen = (sp->world_x >= sc_left  && sp->world_x < sc_right &&
                             sp->world_y >= sc_top    && sp->world_y < sc_bottom);
            if (on_screen) continue;  /* visible: pause, don't spawn */
        }

        /* In approach band (map point) or expanded vp (one_shot): tick. */
        sp->countdown--;
        if (sp->countdown >= 0) continue;

        /*
         * Countdown expired.  Check per-point occupancy:
         * at most one living alien may exist per spawn point at any time.
         */
        if (sp->spawned_alien_idx >= 0 &&
            g_aliens[sp->spawned_alien_idx].alive != 0) {
            /* Alien still alive — reload and keep waiting. */
            sp->countdown = SPAWN_COUNTDOWN_INIT;
            continue;
        }

        /* Hatch an alien (ref lbC00A718 / do_alien_hatch @ main.asm#L7455). */
        int idx = spawn_alien_at(sp->world_x, sp->world_y, sp->alien_type,
                                 sp->is_facehugger, sp->is_boss);
        if (idx >= 0)
            sp->spawned_alien_idx = idx;

        if (sp->one_shot) {
            /* Facehugger hatch: play sound and deactivate slot. */
            audio_play_sample(SAMPLE_HATCHING_ALIEN);
            sp->active = 0;
        } else {
            /* Map point: reload timer; next spawn gated by occupancy check. */
            sp->countdown = SPAWN_COUNTDOWN_INIT;
        }

        /*
         * Tile 0x34 (TILE_ALIEN_HOLE): alien emerges with zoom-in animation
         * and hatch sound.  Mirrors ASM do_alien_hatch @ main.asm#L7817:
         *   move.w 46(a1),76(a0)  — stores struct+46 ($14=20) into alien+76
         *   move.w #1,play_alien_hatching_sample — triggers SAMPLE_HATCHING_ALIEN
         * Ref: lbC00A568 / lbC00987E @ main.asm#L7272-L7278 for the counter
         * decrement and animation pointer update each tick.
         */
        if (sp->is_hole_spawn && idx >= 0) {
            g_aliens[idx].hatch_timer = HATCH_ANIM_TIMER_INIT;
            audio_play_sample(SAMPLE_HATCHING_ALIEN);
        }
    }
}

/* Return 1 if the world pixel (wx, wy) falls inside a solid tile. */
static int alien_solid_at(int wx, int wy)
{
    return tilemap_is_alien_solid(&g_cur_map,
                                  tilemap_pixel_to_col(wx),
                                  tilemap_pixel_to_row(wy));
}

/*
 * Alien-alien separation check — mirrors lbC00A96C (X-axis) and lbC00A9D6
 * (Y-axis) in main.asm#L7515-L7596.
 *
 * The ASM keeps an array of bounding-box records (cur_alien1_dats …
 * cur_alien7_dats, each dc.w x1,y1,x2,y2 + dc.l struct_ptr) and iterates
 * them to detect whether a proposed X or Y step would produce an AABB overlap
 * with another alien.  If it would, the movement is cancelled for that axis.
 *
 * The alien bounding box size comes from lbW008F14: dc.w 0,0,$20,$20
 * (offsets 0,0 → size 32,32 relative to pos), so the box is
 * [pos_x, pos_x+32] × [pos_y, pos_y+32].
 *
 * Returns 1 if placing alien self_idx at world pixel (nx, ny) would overlap
 * any other living (alive==1) alien using the same 32×32 box centred on pos.
 * The overlap test nx < ox+32 && nx+32 > ox is equivalent to |nx-ox| < 32
 * which holds for both top-left and centre conventions when box size = 32.
 */
static int alien_overlaps_other(int self_idx, int nx, int ny)
{
    for (int j = 0; j < g_alien_count; j++) {
        if (j == self_idx) continue;
        if (g_aliens[j].alive != 1) continue;
        int ox = (int)g_aliens[j].pos_x;
        int oy = (int)g_aliens[j].pos_y;
        /* AABB overlap test.  Both nx/ox and ny/oy are the centres of 32×32
         * boxes.  The test `nx < ox+32 && nx+32 > ox` is equivalent to
         * `|nx-ox| < 32`, which is identical to testing [nx-16,nx+16] vs
         * [ox-16,ox+16] since (nx-16) < (ox+16) ↔ nx < ox+32, and
         * (nx+16) > (ox-16) ↔ nx+32 > ox.  No change needed vs top-left form. */
        if (nx < ox + 32 && nx + 32 > ox &&
            ny < oy + 32 && ny + 32 > oy)
            return 1;
    }
    return 0;
}

/*
 * Move one alien toward the nearest living player.
 *
 * Re-implementation of the ASM movement at lbC009CE2 / lbC009E1A
 * (main.asm#L6810-L6989):
 *
 *  1. Find the nearest player by Manhattan distance.
 *  2. X axis: if |dx| > 4, move by a->speed; check three leading-edge probe
 *     points of the proposed new bounding box for solid tiles.
 *  3. Y axis: same treatment independently of X.
 *  4. Build a direction-bit word (bit0=down, bit1=up, bit2=right, bit3=left)
 *     from the actual movement and map it to the 0-based compass direction
 *     using the lbB00A228 look-up table (main.asm#L7077).
 *
 * Probe offsets exactly mirror the ASM type-struct collision tables for a
 * 32×32 bbox (dc.w 0,0,$20,$20 at offset 4 of lbW008F14, main.asm#L5979).
 * Each direction uses 3 probe points from the table layout dc.w x0,x1,x2,y0,y1,y2
 * (probes executed in order 0, 2, 1 by the ASM — all 3 must be clear to move):
 *   RIGHT  lbW00A2D6: dc.w 30,30,30,-6,4,16  → (nx+30,ay-6),(nx+30,ay+16),(nx+30,ay+4)
 *   LEFT   lbW00A2CA: dc.w -4,-4,-4,-6,4,16  → (nx-4,ay-6),(nx-4,ay+16),(nx-4,ay+4)
 *   DOWN   lbW00A2EE: dc.w 0,10,22,20,20,20  → (ax+0,ny+20),(ax+22,ny+20),(ax+10,ny+20)
 *   UP     lbW00A2E2: dc.w 0,10,22,-10,-10,-10→(ax+0,ny-10),(ax+22,ny-10),(ax+10,ny-10)
 * where nx/ny = proposed position after applying speed.
 * No orientation-based rotation: probes are purely direction-of-movement driven,
 * matching the original ASM (no rotation in check_aliens_collisions).
 *
 * In the C port pos_x/pos_y is the CENTRE of the 32×32 bbox (sprite is blitted
 * at x-16, y-16), whereas the ASM uses the top-left corner.  All probe offsets
 * are shifted by -16 relative to the raw ASM values:  C_offset = ASM_offset - 16.
 */
static void alien_move(int self_idx, Alien *a)
{
    /* ------------------------------------------------------------------ */
    /* 1. Find nearest living player using the cached positions.           */
    /*    The cache is refreshed every TARGET_REFRESH_FRAMES frames by     */
    /*    alien_update_all(), mirroring lbC0097F6 / lbW0097F2=20 in the   */
    /*    original ASM (main.asm#L6396-L6415).                             */
    /* ------------------------------------------------------------------ */
    int tx = -1, ty = -1;
    int best = 0x7FFFFFFF;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!s_cached_player_alive[i]) continue;
        int ddx = s_cached_player_x[i] - (int)a->pos_x;
        int ddy = s_cached_player_y[i] - (int)a->pos_y;
        int d   = (ddx < 0 ? -ddx : ddx) + (ddy < 0 ? -ddy : ddy);
        if (d < best) { best = d; tx = s_cached_player_x[i]; ty = s_cached_player_y[i]; }
    }
    if (tx < 0) return;  /* no living player */

    int spd     = (int)a->speed;
    int ax      = (int)a->pos_x;
    int ay      = (int)a->pos_y;
    int dir_bits = 0;   /* bit0=down  bit1=up  bit2=right  bit3=left */

    /* ------------------------------------------------------------------ */
    /* 2. X movement — independent of Y                                    */
    /*    Threshold of 4 px mirrors ASM `cmp.w #4,d4` (main.asm#L6926).   */
    /*    3 probes along the leading edge of the proposed bbox, matching   */
    /*    lbW00A2D6 (right) and lbW00A2CA (left) at main.asm#L7089-7090.  */
    /*    Offsets are ASM values − 16 (centre-based pos_x/pos_y).         */
    /*                                                                      */
    /*    evade_x (ASM 78(a0)): decremented by 2 per tick via the ASM      */
    /*    double-decrement pattern at lbC009912 (main.asm#L6489-L6494).    */
    /*    While > 0 the intended X direction is reversed (right↔left).     */
    /* ------------------------------------------------------------------ */

    /* Double-decrement of X evasion timer, clamped at 0 (lbC009912). */
    if (a->evade_x > 0) a->evade_x--;
    if (a->evade_x > 0) a->evade_x--;

    int dx = tx - ax;
    if (dx > 4) {
        if (a->evade_x != 0) {
            /* Evasion active: reverse direction — try LEFT (lbC009956 path). */
            int nx = ax - spd;
            if (!alien_solid_at(nx - 20, ay - 22) &&
                !alien_solid_at(nx - 20, ay +  0) &&
                !alien_solid_at(nx - 20, ay - 12) &&
                !alien_overlaps_other(self_idx, nx, ay)) {
                ax = nx;
                dir_bits |= 8;  /* left */
            } else {
                a->blocked_axis = 0;  /* X blocked */
            }
        } else {
            /* Normal: move right (lbC009938 path). */
            int nx = ax + spd;
            if (!alien_solid_at(nx + 14, ay - 22) &&
                !alien_solid_at(nx + 14, ay +  0) &&
                !alien_solid_at(nx + 14, ay - 12) &&
                !alien_overlaps_other(self_idx, nx, ay)) {
                ax = nx;
                dir_bits |= 4;  /* right */
            } else {
                a->blocked_axis = 0;  /* X blocked */
            }
        }
    } else if (dx < -4) {
        if (a->evade_x != 0) {
            /* Evasion active: reverse direction — try RIGHT (lbC009938 path). */
            int nx = ax + spd;
            if (!alien_solid_at(nx + 14, ay - 22) &&
                !alien_solid_at(nx + 14, ay +  0) &&
                !alien_solid_at(nx + 14, ay - 12) &&
                !alien_overlaps_other(self_idx, nx, ay)) {
                ax = nx;
                dir_bits |= 4;  /* right */
            } else {
                a->blocked_axis = 0;  /* X blocked */
            }
        } else {
            /* Normal: move left (lbC009956 path). */
            int nx = ax - spd;
            if (!alien_solid_at(nx - 20, ay - 22) &&
                !alien_solid_at(nx - 20, ay +  0) &&
                !alien_solid_at(nx - 20, ay - 12) &&
                !alien_overlaps_other(self_idx, nx, ay)) {
                ax = nx;
                dir_bits |= 8;  /* left */
            } else {
                a->blocked_axis = 0;  /* X blocked */
            }
        }
    }

    /* ------------------------------------------------------------------ */
    /* 3. Y movement — independent of X                                    */
    /*    3 probes along the leading edge of the proposed bbox, matching   */
    /*    lbW00A2EE (down) and lbW00A2E2 (up) at main.asm#L7087-7088.     */
    /*    Offsets are ASM values − 16 (centre-based pos_x/pos_y).         */
    /*                                                                      */
    /*    evade_y (ASM 80(a0)): NOT decremented here (unlike evade_x).     */
    /*    It is toggled between 50 and 0 by the stuck counter.  While > 0  */
    /*    the intended Y direction is reversed (down↔up).                  */
    /* ------------------------------------------------------------------ */
    int dy = ty - ay;
    if (dy > 4) {
        if (a->evade_y != 0) {
            /* Evasion active: reverse direction — try UP. */
            int ny = ay - spd;
            if (!alien_solid_at(ax - 16, ny - 26) &&
                !alien_solid_at(ax +  6, ny - 26) &&
                !alien_solid_at(ax -  6, ny - 26) &&
                !alien_overlaps_other(self_idx, ax, ny)) {
                ay = ny;
                dir_bits |= 2;  /* up */
            } else {
                a->blocked_axis = 1;  /* Y blocked */
            }
        } else {
            /* Normal: move down. */
            int ny = ay + spd;
            if (!alien_solid_at(ax - 16, ny + 4) &&
                !alien_solid_at(ax +  6, ny + 4) &&
                !alien_solid_at(ax -  6, ny + 4) &&
                !alien_overlaps_other(self_idx, ax, ny)) {
                ay = ny;
                dir_bits |= 1;  /* down */
            } else {
                a->blocked_axis = 1;  /* Y blocked */
            }
        }
    } else if (dy < -4) {
        if (a->evade_y != 0) {
            /* Evasion active: reverse direction — try DOWN. */
            int ny = ay + spd;
            if (!alien_solid_at(ax - 16, ny + 4) &&
                !alien_solid_at(ax +  6, ny + 4) &&
                !alien_solid_at(ax -  6, ny + 4) &&
                !alien_overlaps_other(self_idx, ax, ny)) {
                ay = ny;
                dir_bits |= 1;  /* down */
            } else {
                a->blocked_axis = 1;  /* Y blocked */
            }
        } else {
            /* Normal: move up. */
            int ny = ay - spd;
            if (!alien_solid_at(ax - 16, ny - 26) &&
                !alien_solid_at(ax +  6, ny - 26) &&
                !alien_solid_at(ax -  6, ny - 26) &&
                !alien_overlaps_other(self_idx, ax, ny)) {
                ay = ny;
                dir_bits |= 2;  /* up */
            } else {
                a->blocked_axis = 1;  /* Y blocked */
            }
        }
    }

    a->pos_x = (WORD)ax;
    a->pos_y = (WORD)ay;

    /* ------------------------------------------------------------------ */
    /* 4. Direction lookup via lbB00A228 table (main.asm#L7077)            */
    /*                                                                      */
    /*    ASM table (word, 1-based direction values):                       */
    /*      [0]=0 [1]=5 [2]=1 [3]=0 [4]=3 [5]=4 [6]=2 [7]=0               */
    /*      [8]=7 [9]=6 [10]=8                                             */
    /*    Subtract 1 to get 0-based atlas column:                           */
    /*      dir_bits=1(S)=4  dir_bits=2(N)=0  dir_bits=4(E)=2              */
    /*      dir_bits=5(SE)=3 dir_bits=6(NE)=1 dir_bits=8(W)=6             */
    /*      dir_bits=9(SW)=5 dir_bits=10(NW)=7                             */
    /* ------------------------------------------------------------------ */
    /* dir_bits: 0=idle  1=S(4)  2=N(0)  4=E(2)  5=SE(3)  6=NE(1)
     *           8=W(6)  9=SW(5)  10=NW(7)  others=invalid(-1) */
    static const int k_dir_table[16] = {
        -1,  4,  0, -1,   /* 0-3  */
         2,  3,  1, -1,   /* 4-7  */
         6,  5,  7, -1,   /* 8-11 */
        -1, -1, -1, -1    /* 12-15 */
    };
    if (dir_bits > 0 && dir_bits < 16 && k_dir_table[dir_bits] >= 0)
        a->direction = k_dir_table[dir_bits];

    /* ------------------------------------------------------------------ */
    /* 5. Stuck counter and evasion timer toggle (ASM lbC009A16-lbC009A60) */
    /*                                                                      */
    /*    When the alien can't move at all (dir_bits == 0), a stuck counter */
    /*    increments.  After 25 consecutive stuck ticks the counter resets  */
    /*    to 0 (ASM `clr.w 88(a0)`) and the evasion timer for the blocked  */
    /*    axis is toggled:                                                   */
    /*      blocked_axis == 0 (X was blocked) → toggle evade_y             */
    /*      blocked_axis != 0 (Y was blocked) → toggle evade_x             */
    /*    "Toggle" means: if the timer is already set (≠ 0) clear it,      */
    /*    otherwise set it to 50 so the alien evades for ~25 ticks.         */
    /*    The counter resets to 0 and re-fires every 25 more stuck ticks   */
    /*    to keep toggling the evasion timer until the alien finds a free   */
    /*    path.  This exactly mirrors main.asm#L6575-L6578.                */
    /* ------------------------------------------------------------------ */
    if (dir_bits == 0) {
        a->stuck_counter++;
        if (a->stuck_counter >= 25) {
            a->stuck_counter = 0;
            if (a->blocked_axis == 0) {
                /* X was blocked → evade on Y (lbC009A4A) */
                a->evade_y = (a->evade_y != 0) ? 0 : 50;
            } else {
                /* Y was blocked → evade on X (lbC009A40) */
                a->evade_x = (a->evade_x != 0) ? 0 : 50;
            }
        }
    }
}

/*
 * Pre-computed orbit waypoints for boss_nbr=4 reactor-shield elements.
 *
 * Mirrors lbW0256B4 @ main.asm#L18324-L18330 (lbW0256B4..lbW02578C then
 * dc.w -1,-1 as sentinel).  62 relative (X, Y) positions describe a complete
 * elliptical orbit around the reactor, approximately centred at (75, 75).
 *
 * In lbC009AFC the world offset is added after setting the position:
 *   add.w #1662, ALIEN_POS_X(a0)   (@ main.asm#L6730)
 *   add.w  #731, ALIEN_POS_Y(a0)
 * These constants map relative (0,0) to world pixel (1662, 731), placing the
 * orbit over the reactor room on level 10 (L9MA).
 *
 * The 7 shield-element aliens start at evenly-spaced offsets in the table:
 *   alien 0 (k=0): orbit index 0  (lbW0256B4)
 *   alien 1 (k=1): orbit index 9  (lbW0256D8)
 *   ...
 *   alien 6 (k=6): orbit index 54 (lbW02578C)
 * starting_index = k * BOSS4_ORBIT_STEP,  BOSS4_ORBIT_STEP = 9.
 */
#define BOSS4_ORBIT_COUNT   62
#define BOSS4_ORBIT_STEP     9
#define BOSS4_ORBIT_WORLD_X  1662
#define BOSS4_ORBIT_WORLD_Y  731

static const WORD k_boss4_orbit[BOSS4_ORBIT_COUNT][2] = {
    /* lbW0256B4 — alien 0 starts here */
    {0x4F, 0x00}, {0x56, 0x01}, {0x5E, 0x02}, {0x65, 0x05},
    {0x6B, 0x07}, {0x72, 0x0B}, {0x78, 0x0F}, {0x7E, 0x14},
    {0x84, 0x1A},
    /* lbW0256D8 — alien 1 starts here (index 9) */
    {0x88, 0x20}, {0x8C, 0x26}, {0x90, 0x2D}, {0x92, 0x34},
    {0x94, 0x3B}, {0x96, 0x43}, {0x96, 0x4B}, {0x96, 0x53},
    {0x94, 0x5B},
    /* lbW0256FC — alien 2 starts here (index 18) */
    {0x92, 0x62}, {0x90, 0x69}, {0x8D, 0x6F}, {0x88, 0x76},
    {0x84, 0x7C}, {0x7F, 0x81}, {0x79, 0x86}, {0x72, 0x8B},
    {0x6C, 0x8E},
    /* lbW025720 — alien 3 starts here (index 27) */
    {0x65, 0x91}, {0x5E, 0x94}, {0x56, 0x95}, {0x4F, 0x96},
    {0x47, 0x96}, {0x3F, 0x95}, {0x38, 0x94}, {0x31, 0x91},
    {0x2A, 0x8E},
    /* lbW025744 — alien 4 starts here (index 36) */
    {0x24, 0x8B}, {0x1D, 0x86}, {0x18, 0x82}, {0x12, 0x7C},
    {0x0E, 0x76}, {0x09, 0x6F}, {0x06, 0x69}, {0x04, 0x62},
    {0x02, 0x5A},
    /* lbW025768 — alien 5 starts here (index 45) */
    {0x00, 0x53}, {0x00, 0x4B}, {0x00, 0x43}, {0x02, 0x3C},
    {0x03, 0x35}, {0x06, 0x2E}, {0x09, 0x27}, {0x0D, 0x21},
    {0x11, 0x1B},
    /* lbW02578C — alien 6 starts here (index 54) */
    {0x17, 0x15}, {0x1D, 0x10}, {0x23, 0x0C}, {0x2A, 0x08},
    {0x31, 0x05}, {0x38, 0x02}, {0x3F, 0x01}, {0x47, 0x00}
};

/*
 * Advance one boss_nbr=4 reactor-shield element along its pre-computed orbit.
 *
 * Re-implementation of lbC009AFC @ main.asm#L6640 (movement section only):
 *   1. Advance the 72(a0) waypoint pointer by 4 bytes (one X,Y entry).
 *   2. If the current entry is the sentinel (dc.w -1,-1) wrap to the start
 *      (68(a0) = lbW0256B4).  In the C port this is a simple index wrap.
 *   3. Set ALIEN_POS_X/Y from the waypoint entry.
 *   4. Add the world offset (1662, 731) so the orbit is placed over the reactor.
 *
 * The direction field is set via an approximate translation of the lbC009BC4
 * helper (@ main.asm#L6756): it computes the 8-direction compass bearing from
 * the current relative position toward the orbit centre (75, 75) so the shield
 * element faces the reactor as it orbits.
 */
static void boss4_orbit_move(Alien *a)
{
    /* Advance and wrap the waypoint index. */
    a->orbit_idx++;
    if (a->orbit_idx >= BOSS4_ORBIT_COUNT)
        a->orbit_idx = 0;

    int rx = (int)k_boss4_orbit[a->orbit_idx][0];  /* relative X */
    int ry = (int)k_boss4_orbit[a->orbit_idx][1];  /* relative Y */

    /* Apply world offset: mirrors add.w #1662/731,ALIEN_POS_X/Y @ main.asm#L6730. */
    a->pos_x = (WORD)(rx + BOSS4_ORBIT_WORLD_X);
    a->pos_y = (WORD)(ry + BOSS4_ORBIT_WORLD_Y);

    /*
     * Approximate lbC009BC4 direction computation.
     * The helper maps the vector from current position to centre (75,75) onto
     * an 8-direction compass:
     *   dx = 75 - rx,  dy = 75 - ry
     * Each quadrant contributes directions 0-7 (N=0,NE=1,E=2,SE=3,S=4,SW=5,W=6,NW=7).
     */
    int dx = 75 - rx;
    int dy = 75 - ry;
    int adx = (dx < 0) ? -dx : dx;
    int ady = (dy < 0) ? -dy : dy;
    int dir_bits = 0;
    if (dy < 0) dir_bits |= 2;  /* moving up   (toward centre is upward)  */
    else if (dy > 0) dir_bits |= 1;  /* moving down */
    if (dx > 0) dir_bits |= 4;  /* moving right */
    else if (dx < 0) dir_bits |= 8;  /* moving left */
    /* Suppress the weaker axis when the other dominates (>2:1 ratio). */
    if (adx > 0 && ady > adx * 2) dir_bits &= ~(4 | 8);
    if (ady > 0 && adx > ady * 2) dir_bits &= ~(1 | 2);
    static const int k_dir_table[16] = {
        -1,  4,  0, -1,   2,  3,  1, -1,
         6,  5,  7, -1,  -1, -1, -1, -1
    };
    if (dir_bits > 0 && dir_bits < 16 && k_dir_table[dir_bits] >= 0)
        a->direction = k_dir_table[dir_bits];
}

/*
 * Move one boss alien (is_boss=1) toward the nearest living player.
 *
 * Re-implementation of the movement section of lbC009CE2 @ main.asm#L6883.
 * The boss AI is identical to alien_move() in structure but uses boss-sized
 * collision probes derived from the boss probe tables in main.asm:
 *
 *   lbW00A2FA (LEFT) : dc.w -6,-6,-6,-6,4,16
 *   lbW00A306 (RIGHT): dc.w 100,100,100,-6,4,16
 *   lbW00A312 (UP)   : dc.w 4,50,90,-10,-10,-10
 *   lbW00A31E (DOWN) : dc.w 4,50,90,112,112,112
 *
 * All C probe offsets = ASM offset − half_size (centre-based convention):
 *   half_w = BOSS_SPRITE_W/2 = 48, half_h = BOSS_SPRITE_H/2 = 64.
 *
 * Resulting C probe offsets (relative to proposed new position nx/ny):
 *   RIGHT : nx+52; ay-70, ay-60, ay-48   (ASM 100−48=52; −6−64=−70, 4−64=−60, 16−64=−48)
 *   LEFT  : nx-54; ay-70, ay-60, ay-48   (ASM  −6−48=−54)
 *   DOWN  : ny+48; ax-44, ax+2,  ax+42   (ASM 112−64=48;  4−48=−44, 50−48=2, 90−48=42)
 *   UP    : ny-74; ax-44, ax+2,  ax+42   (ASM −10−64=−74)
 *
 * The position-seek target (d4, d5 in ASM) is offset by (−24, −104) from the
 * nearest player's top-left position so that the boss's "aim point" (which is
 * roughly 24 px from its left edge and 104 px from its top) aligns with the
 * player.  In the C port, both boss and player use centre-based coordinates,
 * so the offset is applied directly to the player world position.
 * Ref: sub.w #24,d4 / sub.w #104,d5 @ main.asm#L6915-L6916.
 *
 * The boss does NOT use the evade/stuck system (it always seeks the player
 * directly via its large collision probes).  Evade fields remain zeroed.
 */
static void boss_move(int self_idx, Alien *a)
{
    /*
     * 1. Retreat timer logic — mirrors lbC009CE2 @ main.asm#L6825-L6853.
     *
     * Only the primary boss (boss_rank=0) drives the shared retreat state.
     * Secondary aliases (boss_rank>0) just use whatever state the primary set.
     *
     * Each frame when the primary runs:
     *   a. rand(300) is checked only when the countdown is currently 0.
     *      If rand < 2 (1/150 chance) → start retreat: flag=1, countdown=40.
     *   b. If countdown > 0: decrement it.
     *   c. If countdown == 0 (lbC009D80-lbC009D98): clear flag then re-set to 1
     *      if any player is alive.  Net result: boss always retreats while any
     *      player is alive; only chases when both players are dead.
     */
    if (a->boss_rank == 0) {
        if (s_boss_retreat_countdown == 0) {
            if ((rand() % 300) < 2) {
                s_boss_retreat_flag      = 1;
                s_boss_retreat_countdown = 40;
            }
        }
        if (s_boss_retreat_countdown > 0) {
            s_boss_retreat_countdown--;
        } else {
            /* lbC009D80: clear flag; lbC009D98: re-set if any player alive. */
            s_boss_retreat_flag = 0;
            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (s_cached_player_alive[i]) {
                    s_boss_retreat_flag = 1;
                    break;
                }
            }
        }
    }

    /* 2. Find nearest living player (same as alien_move). */
    int tx = -1, ty = -1;
    int best = 0x7FFFFFFF;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!s_cached_player_alive[i]) continue;
        int ddx = s_cached_player_x[i] - (int)a->pos_x;
        int ddy = s_cached_player_y[i] - (int)a->pos_y;
        int d   = (ddx < 0 ? -ddx : ddx) + (ddy < 0 ? -ddy : ddy);
        if (d < best) { best = d; tx = s_cached_player_x[i]; ty = s_cached_player_y[i]; }
    }
    if (tx < 0) return;

    /* 3. Apply the ASM target offset: boss aims toward (player_pos − 24, − 104).
     *    sub.w #24,d4 / sub.w #104,d5 @ main.asm#L6915-L6916. */
    tx -= 24;
    ty -= 104;

    int spd      = (int)a->speed;
    int ax       = (int)a->pos_x;
    int ay       = (int)a->pos_y;
    int dir_bits = 0;

    int dx = tx - ax;
    int dy = ty - ay;

    if (s_boss_retreat_flag) {
        /*
         * 4a. RETREAT mode (lbL009C64=1) — boss moves AWAY from player.
         *     No ±4 px dead-zone: boss moves even when right next to the player.
         *     Mirrors lbC009EA8/lbC009EB2 (X) and lbC009EF2/lbC009EFC (Y) paths.
         *
         *     lbW00A2FA/A306/A312/A31E collision probes remain identical.
         */
        /* X: if player is to the right (dx > 0) → go LEFT (away). */
        if (dx > 0) {
            int nx = ax - spd;
            if (!alien_solid_at(nx - 54, ay - 70) &&
                !alien_solid_at(nx - 54, ay - 48) &&
                !alien_solid_at(nx - 54, ay - 60)) {
                ax = nx;
                dir_bits |= 8;  /* left */
            }
        } else if (dx < 0) {
            /* Player is to the left → go RIGHT (away). */
            int nx = ax + spd;
            if (!alien_solid_at(nx + 52, ay - 70) &&
                !alien_solid_at(nx + 52, ay - 48) &&
                !alien_solid_at(nx + 52, ay - 60)) {
                ax = nx;
                dir_bits |= 4;  /* right */
            }
        }

        /* Y: if player is below (dy > 0) → go UP (away). */
        if (dy > 0) {
            int ny = ay - spd;
            if (!alien_solid_at(ax - 44, ny - 74) &&
                !alien_solid_at(ax +  2, ny - 74) &&
                !alien_solid_at(ax + 42, ny - 74)) {
                ay = ny;
                dir_bits |= 2;  /* up */
            }
        } else if (dy < 0) {
            /* Player is above → go DOWN (away). */
            int ny = ay + spd;
            if (!alien_solid_at(ax - 44, ny + 48) &&
                !alien_solid_at(ax +  2, ny + 48) &&
                !alien_solid_at(ax + 42, ny + 48)) {
                ay = ny;
                dir_bits |= 1;  /* down */
            }
        }
    } else {
        /*
         * 4b. CHASE mode (lbL009C64=0) — boss moves TOWARD player.
         *     Only active when both players are dead (no player to target,
         *     so this path is effectively a no-op in practice).
         *     Threshold 4 px mirrors `cmp.w #4,d4` @ main.asm#L6926.
         *
         *     lbW00A2FA (LEFT):  x=−6, y=−6/4/16 → C: x−54, y−70/−60/−48
         *     lbW00A306 (RIGHT): x=100, y=−6/4/16 → C: x+52, y−70/−60/−48
         *     lbW00A312 (UP):   x=4/50/90, y=−10  → C: x−44/+2/+42, y−74
         *     lbW00A31E (DOWN): x=4/50/90, y=112  → C: x−44/+2/+42, y+48
         */
        if (dx > 4) {
            int nx = ax + spd;
            if (!alien_solid_at(nx + 52, ay - 70) &&
                !alien_solid_at(nx + 52, ay - 48) &&
                !alien_solid_at(nx + 52, ay - 60)) {
                ax = nx;
                dir_bits |= 4;  /* right */
            }
        } else if (dx < -4) {
            int nx = ax - spd;
            if (!alien_solid_at(nx - 54, ay - 70) &&
                !alien_solid_at(nx - 54, ay - 48) &&
                !alien_solid_at(nx - 54, ay - 60)) {
                ax = nx;
                dir_bits |= 8;  /* left */
            }
        }

        if (dy > 4) {
            int ny = ay + spd;
            if (!alien_solid_at(ax - 44, ny + 48) &&
                !alien_solid_at(ax +  2, ny + 48) &&
                !alien_solid_at(ax + 42, ny + 48)) {
                ay = ny;
                dir_bits |= 1;  /* down */
            }
        } else if (dy < -4) {
            int ny = ay - spd;
            if (!alien_solid_at(ax - 44, ny - 74) &&
                !alien_solid_at(ax +  2, ny - 74) &&
                !alien_solid_at(ax + 42, ny - 74)) {
                ay = ny;
                dir_bits |= 2;  /* up */
            }
        }
    }

    a->pos_x = (WORD)ax;
    a->pos_y = (WORD)ay;

    /* 5. Direction lookup (same table as alien_move). */
    static const int k_dir_table[16] = {
        -1,  4,  0, -1,   /* 0-3  */
         2,  3,  1, -1,   /* 4-7  */
         6,  5,  7, -1,   /* 8-11 */
        -1, -1, -1, -1    /* 12-15 */
    };
    if (dir_bits > 0 && dir_bits < 16 && k_dir_table[dir_bits] >= 0)
        a->direction = k_dir_table[dir_bits];

    (void)self_idx;
}

void alien_update_all(void)
{
    /* Lazy viewport-triggered spawning (mirrors lbC00D17E @ main.asm#L8547). */
    alien_spawn_tick();

    /*
     * Refresh the player position cache every TARGET_REFRESH_FRAMES frames.
     * Mirrors lbC0097F6 in the original ASM: the countdown starts at
     * lbW0097F2=20 and is only reloaded when it underflows, so the alien AI
     * works from a position snapshot that is up to 20 VBL frames (~400 ms)
     * old.  This is what gives the original aliens their characteristic
     * "momentum" — they don't instantly correct course every frame.
     * (main.asm#L6396-L6415)
     */
    if (--s_target_refresh_countdown <= 0) {
        s_target_refresh_countdown = TARGET_REFRESH_FRAMES;
        for (int i = 0; i < MAX_PLAYERS; i++) {
            s_cached_player_alive[i] = g_players[i].alive;
            s_cached_player_x[i]    = g_players[i].pos_x;
            s_cached_player_y[i]    = g_players[i].pos_y;
        }
    }

    for (int i = 0; i < g_alien_count; i++) {
        if (g_aliens[i].alive == 0) continue;

        if (g_aliens[i].alive == 2) {
            /* Dying: advance death explosion frame; fully remove when done.
             * 16 frames at game rate ≈ 0.3 s (Ref: lbL018C2E @ main.asm#L13907,
             * each frame has delay=0 = one game tick). */
            g_aliens[i].death_frame++;
            if (g_aliens[i].death_frame >= ALIEN_DEATH_FRAMES)
                g_aliens[i].alive = 0;
            continue;
        }

        /* While the hatch zoom animation is playing, block all movement.
         * ASM lbC00A568 (main.asm#L7272): `subq.w #1,76(a0)` then `rts` —
         * the check at main.asm#L6448-6449 (`tst.w 76(a0)` / `bne lbC00A568`)
         * causes an early return before the movement code is ever reached.
         * Only when hatch_timer reaches 0 does the alien start moving. */
        if (g_aliens[i].hatch_timer > 0) {
            g_aliens[i].hatch_timer--;
            continue;
        }

        WORD prev_x = g_aliens[i].pos_x;
        WORD prev_y = g_aliens[i].pos_y;
        if (g_aliens[i].is_boss) {
            if (g_boss_nbr == 4) {
                /*
                 * boss_nbr=4 reactor shield elements — lbC009AFC AI.
                 * All 7 aliens follow their own pre-computed orbit around the
                 * reactor independently; none copy another's position.
                 * Ref: lbC009AFC @ main.asm#L6640 (waypoint follower).
                 */
                boss4_orbit_move(&g_aliens[i]);
            } else if (g_aliens[i].boss_rank > 0) {
                /*
                 * Secondary boss (turret) — mirrors lbC009C68 @ main.asm#L6777.
                 * The secondary alien has no movement AI of its own: it copies
                 * the primary boss's (boss_rank=0) position every frame so that
                 * it stays on top of the main boss and acts as a separate-HP
                 * hit-zone.  Velocity is zeroed (clr.l 34(a0)/clr.l 38(a0)).
                 */
                for (int j = 0; j < g_alien_count; j++) {
                    if (j == i) continue;
                    if (g_aliens[j].alive == 1 && g_aliens[j].is_boss &&
                        g_aliens[j].boss_rank == 0) {
                        g_aliens[i].pos_x = g_aliens[j].pos_x;
                        g_aliens[i].pos_y = g_aliens[j].pos_y;
                        break;
                    }
                }
            } else {
                boss_move(i, &g_aliens[i]);
            }
        } else {
            alien_move(i, &g_aliens[i]);
        }
        /* Only advance the walk-cycle animation when the alien actually moved.
         * If it is physically blocked (by a wall or another alien) its position
         * stays the same and we do NOT tick the counter, so the sprite freezes
         * on the current frame rather than running visibly on the spot. */
        if (g_aliens[i].pos_x != prev_x || g_aliens[i].pos_y != prev_y)
            g_aliens[i].anim_counter++;
    }

    /* Update projectiles */
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        /* Advance impact animation for deactivated projectiles that hit something */
        if (s_projectiles[i].impact_active) {
            if (--s_projectiles[i].impact_timer <= 0) {
                s_projectiles[i].impact_timer = IMPACT_FRAME_TICKS;
                s_projectiles[i].impact_frame++;
                if (s_projectiles[i].impact_frame >= IMPACT_ANIM_FRAMES)
                    s_projectiles[i].impact_active = 0;
            }
            continue;
        }

        if (!s_projectiles[i].active) continue;

        s_projectiles[i].x += s_projectiles[i].vx;
        s_projectiles[i].y += s_projectiles[i].vy;

        /* Advance FLAMEARC flight animation (8-frame looping sequence, delay=0
         * per frame means one frame per tick.
         * Ref: lbL00EC36 all delay=0 @ main.asm#L10063-L10070). */
        if (s_projectiles[i].weapon_type == WEAPON_FLAMEARC)
            s_projectiles[i].flight_anim_frame =
                (s_projectiles[i].flight_anim_frame + 1) % FLAMEARC_ANIM_FRAMES;

        /* MACHINEGUN: advance flight_anim_frame from 0 → 1 after the first
         * tick so that the bullet sprite (muzzle flash) shows only at the
         * cannon position for one frame, then becomes invisible.
         * Mirrors ASM lbL00EAEE: dc.l BOB,0 / dc.l lbW01389C,32000,-1
         * (delay=0 → show once, then blank for 32000 ticks).
         * Ref: lbL00EAEE @ main.asm#L10019-L10034. */
        if (s_projectiles[i].weapon_type == WEAPON_MACHINEGUN &&
                s_projectiles[i].flight_anim_frame == 0)
            s_projectiles[i].flight_anim_frame = 1;

        /* FLAMETHROWER: in-flight frame is computed from lifetime in the render
         * pass (frame = (elapsed / 2) % 8, delay=1 → 2 ticks/frame, mirroring
         * lbL018D06 @ main.asm#L13935 with -1 looping at 25 Hz). */

        /* Flamethrower lifetime countdown — 16 ticks (8 frames × delay+1=2 ticks/frame)
         * matching lbL018D06 @ main.asm#L13935 with delay=1 each.
         * On natural expiry, spawn an impact flash at the bullet's final position
         * so the 8-frame explosion animation plays where the flame terminates
         * (matching how lbL018D06 transitions to -1 at max range). */
        if (s_projectiles[i].lifetime > 0) {
            s_projectiles[i].lifetime--;
            if (s_projectiles[i].lifetime == 0) {
                /* Spawn impact animation at current position, same as wall hit. */
                s_projectiles[i].active       = 0;
                s_projectiles[i].impact_active = 1;
                s_projectiles[i].impact_x     = (WORD)s_projectiles[i].x;
                s_projectiles[i].impact_y     = (WORD)s_projectiles[i].y;
                s_projectiles[i].impact_frame  = 0;
                s_projectiles[i].impact_timer  = IMPACT_FRAME_TICKS;
                continue;
            }
        }

        /* --- Impact dispatch (weapons_special_impact_table) ---
         * Ref: calc_shot_impact → weapons_special_impact_table @ main.asm#L9513-L9599.
         * tile_attr & 0x3F selects the dispatch entry (0x00–0x3F).
         *
         * Two categories:
         *  a) Non-blocking triggers (fire door buttons 0x08/0x09/0x12/0x13):
         *     apply tile effect and let the projectile continue.
         *  b) Blocking tiles (0x01/0x03/0x1d/0x23/0x2a-0x2d): stop or bounce.
         */
        int px = (int)(WORD)s_projectiles[i].x;
        int py = (int)(WORD)s_projectiles[i].y;
        int col = tilemap_pixel_to_col(px);
        int row = tilemap_pixel_to_row(py);
        UBYTE attr = tilemap_attr(&g_cur_map, col, row) & 0x3F;

        /* ── Non-blocking trigger tiles (fire door buttons) ─────────────
         * patch_fire_door_left_btn (0x08) / right_btn (0x09):
         *   Play door sound, floor-replace the button and 3 adjacent door
         *   panel tiles.  Projectile is NOT stopped.
         *   Tile column offsets:
         *     left  button: +0, +3, +2, +4  (Ref: main.asm#L9790-L9804)
         *     right button: +0, -1, -2, -4  (Ref: main.asm#L9806-L9820)
         *
         * patch_fire_door_left_btn_alarm  (0x12) / right_btn_alarm (0x13):
         *   Same as above but also increment alarm counter (level 3 only).
         *   When counter reaches 3 → self-destruct (checked in
         *   level_check_destruction, Ref: lbC000E56 @ main.asm#L536).
         *   Dedup: ignore re-hits of the same tile (lbL00E756 @ main.asm#L9822).
         *   Ref: main.asm#L9824-L9866.
         */
        if (tilemap_is_projectile_trigger(&g_cur_map, col, row)) {
            audio_play_sample(SAMPLE_OPENING_DOOR);
            if (attr == 0x08 || attr == 0x12) {
                /* Left button (0x08/0x12): activate fire door panels.
                 * Ref: patch_fire_door_left_btn @ main.asm#L9790-L9804.
                 *
                 * In the original ASM the BOB animation system writes specific
                 * tile_words to the live map while animating and holds the
                 * final-frame tile_words permanently.  The final values come
                 * from lbW01C52A / lbW01BECA entries (same across all atlases):
                 *   col+0  (button)      : $5B00  attr=0x00  → floor
                 *   col+2  rows 0-2      : $05A3  attr=0x23  → tile_hard_climb_right
                 *                                               = physical wall for alive players
                 *   col+3  rows 0-2      : $0080  attr=0x00  → floor
                 *   col+4  (right button): $563C  attr=0x3C  → fire-door panel graphic
                 *
                 * BOB row coverage: lbL020DE2/lbL020E3A use linked-tile offsets
                 * {0, 248, 496, -1} (248 bytes = 1 map row) → 3 rows.
                 * lbL020DBE (col+4) uses offset {0,-1,-1,-1} → 1 row only.
                 * Ref: main.asm#L16108-L16122 (animation sequences),
                 *      lbC01165A @ main.asm#L11878 (atlas→BOB copy),
                 *      lbC00AF20 @ main.asm (linked-tile pointer setup). */
                tile_anim_queue(col, row, TILEANIM_FIRE_DOOR);
                tilemap_replace_tile(&g_cur_map, col, row);       /* button → floor */
                for (int r = 0; r < 3; r++) {
                    /* col+2: wall tile (tile_hard_climb_right, attr=0x23) */
                    tilemap_set_tile_word(&g_cur_map, col + 2, row + r, 0x05A3);
                    /* col+3: floor */
                    tilemap_set_tile_word(&g_cur_map, col + 3, row + r, 0x0080);
                }
                /* col+4 (right button): fire-door panel graphic */
                tilemap_set_tile_word(&g_cur_map, col + 4, row, 0x563C);
            } else {
                /* Right button (0x09/0x13): mirror of the left button.
                 * Ref: patch_fire_door_right_btn @ main.asm#L9806-L9820.
                 *   col+0  (button)     : $5B00  → floor
                 *   col-2  rows 0-2     : $05A3  → tile_hard_climb_right (wall)
                 *   col-1  rows 0-2     : $0080  → floor
                 *   col-4  (left button): $563C  → fire-door panel graphic */
                tile_anim_queue(col, row, TILEANIM_FIRE_DOOR);
                tilemap_replace_tile(&g_cur_map, col, row);       /* button → floor */
                for (int r = 0; r < 3; r++) {
                    tilemap_set_tile_word(&g_cur_map, col - 2, row + r, 0x05A3);
                    tilemap_set_tile_word(&g_cur_map, col - 1, row + r, 0x0080);
                }
                tilemap_set_tile_word(&g_cur_map, col - 4, row, 0x563C);
            }
            /* Alarm variants: increment counter if this is not the last-hit button.
             * Mirrors lbL00E756 / cmp.l / beq guard @ main.asm#L9827-L9832.
             * NOTE: only consecutive re-hits of the same tile are skipped; if
             * button A → button B → button A, all three increments will count.
             * This exactly matches the ASM behavior. */
            if ((attr == 0x12 || attr == 0x13) &&
                    g_alarm_system_active &&
                    (col != g_alarm_last_col || row != g_alarm_last_row)) {
                g_alarm_buttons_pressed++;
                g_alarm_last_col = col;
                g_alarm_last_row = row;
            }
            /* Projectile continues — do NOT deactivate */
        } else if (tilemap_is_projectile_blocking(&g_cur_map, col, row)) {
            /*
             * ── impact_on_door (tile 0x03) ─────────────────────────────
             * Ref: impact_on_door @ main.asm#L9669-L9700.
             * Play sample 4 on every hit.  Track cumulative damage on
             * the current door tile; reset the accumulator when the
             * projectile hits a different tile (lbL00E4EC / clr door_impact).
             * When accumulated strength >= 300, give the firing player a
             * temporary key and call open_door (= force_door path in ASM).
             * Always falls through to impact_on_wall (bounce/die) below.
             */
            if (attr == 0x03) {
                audio_play_sample(SAMPLE_DOOR_HIT);
                if (col != g_door_impact_col || row != g_door_impact_row) {
                    g_door_impact_col   = col;
                    g_door_impact_row   = row;
                    g_door_impact_accum = 0;
                }
                g_door_impact_accum += (int)(WORD)s_projectiles[i].strength;
                if (g_door_impact_accum >= 300) {
                    g_door_impact_accum = 0;
                    int pi = s_projectiles[i].player_idx;
                    if (pi >= 0 && pi < MAX_PLAYERS) {
                        Player *p = &g_players[pi];
                        /* Temporarily grant one key so open_door can work
                         * without requiring the player to carry a key.
                         * Mirrors: addq.w #1,PLAYER_KEYS(a0) / bsr force_door
                         * then the key is consumed by open_door itself.
                         * Ref: lbC00E56C-lbC00E574 @ main.asm#L9697-L9699. */
                        p->keys++;
                        open_door_at(p, col, row);
                    }
                }
                /* falls through to impact_on_wall below */
            }

            /*
             * ── patch_reactor_* (tiles 0x2a-0x2d) ─────────────────────
             * Ref: patch_reactor_up/left/down/right @ main.asm#L9603-L9658.
             * Each reactor face needs exactly 6 projectile hits to blow out.
             * Hits 1-5 and 7+: pure impact_on_wall (bounce or die).
             * Hit 6: floor-replace the face tile, play sample 11, then also
             *   check if all 4 faces are done (all counters non-zero) →
             *   trigger self-destruct (check_reactors / lbC000E56 path).
             * Falls through to impact_on_wall in all cases.
             */
            else if (attr >= 0x2a && attr <= 0x2d) {
                int *hit_count;
                switch (attr) {
                case 0x2a: hit_count = &g_reactor_up_done;    break;
                case 0x2b: hit_count = &g_reactor_left_done;  break;
                case 0x2c: hit_count = &g_reactor_down_done;  break;
                default:   hit_count = &g_reactor_right_done; break;
                }
                (*hit_count)++;
                if (*hit_count == 6) {
                    /*
                     * Replace the entire reactor face (all tiles sharing this
                     * attribute) with floor tiles, mirroring patch_dat_reactors
                     * / patch_tiles @ main.asm#L9651-L9657 which patches the
                     * whole face rather than just the one projectile-hit tile.
                     */
                    tilemap_replace_reactor_face(&g_cur_map, attr);
                    audio_play_sample(SAMPLE_REACTOR_BLAST);
                    /*
                     * check_reactors: all 4 faces done (all counters != 0)?
                     * Ref: check_reactors @ main.asm#L9640-L9649.
                     * In ASM, beq.b patch_reactor means "if == 0 jump"
                     * (i.e. NOT done yet); fall-through = all non-zero = all done.
                     */
                    if (g_reactor_up_done    != 0 &&
                        g_reactor_left_done  != 0 &&
                        g_reactor_down_done  != 0 &&
                        g_reactor_right_done != 0) {
                        level_start_destruction();
                    }
                }
                /* falls through to impact_on_wall below */
            }

            /*
             * ── impact_on_wall (tiles 0x01, 0x1d, 0x23, plus all above) ──
             * FLAMEARC bounces 1×, LAZER bounces 5×.  All other weapons
             * (or bounces exhausted) deactivate here.
             * Ref: impact_on_wall @ main.asm#L9702-L9788.
             */
            if (tilemap_is_impact_wall(&g_cur_map, col, row) &&
                    s_projectiles[i].bounce_count > 0) {
                s_projectiles[i].bounce_count--;
                /*
                 * Bounce axis detection — exact ASM algorithm:
                 *   vx >= 0 (moving right): check tile at (col-1, row).
                 *     If tile type == 0x01 → negate vy.
                 *   vx < 0  (moving left):  check tile at (col+1, row).
                 *     If tile type == 0x01 → negate vy.
                 *   vy >= 0 (moving down, using UPDATED vy from above):
                 *     check tile at (col, row+1). If type == 0x01 → negate vx.
                 *   vy < 0  (moving up, using UPDATED vy from above):
                 *     check tile at (col, row-1). If type == 0x01 → negate vx.
                 * Ref: lbC00E5C4-lbC00E64A @ main.asm#L9718-L9765.
                 * Note: ASM checks neighbor attribute == 0x01 specifically
                 *       (and.w #$3F,d4 / cmp.w #1,d4 @ main.asm#L9729-L9730).
                 */
                int16_t vx = (int16_t)s_projectiles[i].vx;
                int16_t vy = (int16_t)s_projectiles[i].vy;

                if (vx < 0) {
                    /* Moving left: check tile to the RIGHT */
                    if ((tilemap_attr(&g_cur_map, col + 1, row) & 0x3F) == 0x01)
                        vy = (int16_t)-vy;
                } else {
                    /* Moving right (or zero): check tile to the LEFT.
                     * Ricochet sound is only played in this branch (vx >= 0),
                     * matching the ASM where sample 46 is emitted at lbC00E5C4
                     * before the vx≥0 neighbor check and never for vx<0.
                     * This asymmetry is intentional — faithful to original ASM.
                     * Ref: move.w #46,d0 / jsr trigger_sample_select_channel
                     *      @ main.asm#L9723-L9726. */
                    if (!g_in_destruction_sequence)
                        audio_play_sample(SAMPLE_RICOCHET);
                    if ((tilemap_attr(&g_cur_map, col - 1, row) & 0x3F) == 0x01)
                        vy = (int16_t)-vy;
                }

                /* Vertical check uses the UPDATED vy from above */
                if (vy < 0) {
                    /* Moving up: check tile ABOVE */
                    if ((tilemap_attr(&g_cur_map, col, row - 1) & 0x3F) == 0x01)
                        vx = (int16_t)-vx;
                } else {
                    /* Moving down (or zero): check tile BELOW */
                    if ((tilemap_attr(&g_cur_map, col, row + 1) & 0x3F) == 0x01)
                        vx = (int16_t)-vx;
                }

                /*
                 * Fallback: lbW0004D6 alternates ±8 every bounce call.
                 * If vx or vy is still zero, assign the current fallback so
                 * the projectile never gets stuck with zero velocity.
                 * Ref: neg.w lbW0004D6 / tst.w 4(a3) / move.w … @ main.asm#L9759-L9765.
                 */
                s_bounce_fallback = -s_bounce_fallback;
                if (vx == 0) vx = (int16_t)s_bounce_fallback;
                if (vy == 0) vy = (int16_t)s_bounce_fallback;

                s_projectiles[i].vx = (WORD)vx;
                s_projectiles[i].vy = (WORD)vy;

                /*
                 * LAZER only: update direction sprite from new velocity.
                 * FLAMEARC returns without this step (cmp.l #lbL00EC36 / beq return
                 * @ main.asm#L9766-L9767).
                 * Ref: lbC00E66C-lbC00E698 @ main.asm#L9766-L9781.
                 */
                if (s_projectiles[i].weapon_type == WEAPON_LAZER)
                    s_projectiles[i].direction = lazer_velocity_to_dir(vx, vy);

            } else {
                /* Standard wall hit (all other weapons, or bounces exhausted):
                 * deactivate projectile and trigger impact flash animation.
                 * Ref: lbC00E6A8 @ main.asm#L9783-L9788:
                 *   clr.w 24(a3)           ; clear bounce counter
                 *   move.w #32000,0(a3)    ; disable projectile (move off-screen)
                 *   move.l #lbL018CBA,40(a4) ; set impact animation
                 */
                s_projectiles[i].active       = 0;
                s_projectiles[i].impact_active = 1;
                s_projectiles[i].impact_x     = px;
                s_projectiles[i].impact_y     = py;
                s_projectiles[i].impact_frame  = 0;
                s_projectiles[i].impact_timer  = IMPACT_FRAME_TICKS;
            }
            continue;
        }

        /* Off-screen */
        if (px < 0 || px >= MAP_COLS * MAP_TILE_W ||
            py < 0 || py >= MAP_ROWS * MAP_TILE_H) {
            s_projectiles[i].active = 0;
        }
    }
}

/* Spawn a projectile from player p (called by player_update logic).
 * weapon_type : WEAPON_* constant
 * penetrating : 1=passes through aliens (PLASMAGUN/FLAMETHROWER/LAZER)
 * lifetime    : ticks until auto-expire (-1=infinite; FLAME_LIFETIME_TICKS for FLAMETHROWER)
 * bounce_count: remaining wall bounces (1=FLAMEARC, 5=LAZER, 0=others)
 * direction   : firing direction 1-8 (PLAYER_FACE_*) for sprite selection
 * Ref: lbC00E178/lbC00E1DC @ main.asm#L9431-L9511.
 */
void alien_spawn_projectile(int player_idx, WORD x, WORD y,
                            WORD vx, WORD vy, WORD strength,
                            int weapon_type, int penetrating,
                            int lifetime, int bounce_count, int direction)
{
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        if (!s_projectiles[i].active && !s_projectiles[i].impact_active) {
            s_projectiles[i].x             = x;
            s_projectiles[i].y             = y;
            s_projectiles[i].vx            = vx;
            s_projectiles[i].vy            = vy;
            s_projectiles[i].strength      = strength;
            s_projectiles[i].player_idx    = player_idx;
            s_projectiles[i].active        = 1;
            s_projectiles[i].weapon_type   = weapon_type;
            s_projectiles[i].penetrating   = penetrating;
            s_projectiles[i].lifetime      = lifetime;
            s_projectiles[i].bounce_count  = bounce_count;
            s_projectiles[i].direction     = direction;
            s_projectiles[i].impact_active = 0;
            s_projectiles[i].impact_frame  = 0;
            s_projectiles[i].impact_timer  = 0;
            s_projectiles[i].flight_anim_frame = 0;
            return;
        }
    }
}

void aliens_collisions_with_weapons(void)
{
    for (int pi = 0; pi < MAX_PROJECTILES; pi++) {
        if (!s_projectiles[pi].active) continue;

        for (int ai = 0; ai < g_alien_count; ai++) {
            if (g_aliens[ai].alive != 1) continue;

            /* AABB collision: projectile 10×10 box (offset +4) vs alien 32×32 box.
             * Ref: aliens_collisions_with_weapons @ main.asm:
             *   projectile bbox: add.l #$40004 (offset x+4, y+4)
             *                    add.l #$A000A (width 10, height 10)
             *   alien bbox:      [pos_x-16, pos_x+16] × [pos_y-16, pos_y+16]
             *                    centre-based; equivalent to ASM [pos_x, pos_x+32].
             *                    (from lbW008F14 dc.w 0,0,$20,$20). */
            int bx1 = (int)s_projectiles[pi].x + 4;
            int bx2 = bx1 + 10;
            int by1 = (int)s_projectiles[pi].y + 4;
            int by2 = by1 + 10;

            /* Default AABB for normal 32×32 aliens (centre-based pos_x/pos_y).
             * Ref: lbW008F14 dc.w 0,0,$20,$20 @ main.asm#L5979. */
            int ax1 = (int)g_aliens[ai].pos_x - 16;
            int ax2 = (int)g_aliens[ai].pos_x + 16;
            int ay1 = (int)g_aliens[ai].pos_y - 16;
            int ay2 = (int)g_aliens[ai].pos_y + 16;

            /* Boss aliens use a larger AABB matching their 96×128 px sprite.
             * Derived from boss probe tables lbW00A2FA/lbW00A306/lbW00A312/lbW00A31E:
             *   left=−54, right=+52, up=−74, down=+48 (C centre-based offsets).
             * Ref: main.asm#L7093-L7096. */
            if (g_aliens[ai].is_boss) {
                ax1 = (int)g_aliens[ai].pos_x - 54;
                ax2 = (int)g_aliens[ai].pos_x + 52;
                ay1 = (int)g_aliens[ai].pos_y - 74;
                ay2 = (int)g_aliens[ai].pos_y + 48;
            }

            if (ax1 < bx2 && ax2 > bx1 && ay1 < by2 && ay2 > by1) {
                /* Apply damage */
                g_aliens[ai].strength -= s_projectiles[pi].strength;
                /* Trigger ALT WALK hit-flash for two rendered frames (≈40ms
                 * at 50Hz = one 25Hz game-logic tick, same duration as the
                 * original single-VBL flash).
                 * Mirrors move.w #1,50(a0) @ main.asm#L7724. */
                g_aliens[ai].hit_flag = 2;
                if (g_aliens[ai].strength <= 0) {
                    alien_kill(ai);
                    /* Award score to the firing player */
                    int owner = s_projectiles[pi].player_idx;
                    if (owner >= 0 && owner < MAX_PLAYERS)
                        g_players[owner].score += ALIEN_SCORE_VALUE;
                }

                /*
                 * Penetrating weapons (PLASMAGUN/FLAMETHROWER/LAZER) keep
                 * flying after hitting a normal alien.
                 * Non-penetrating weapons always deactivate on hit.
                 * Boss aliens (is_boss=1) stop even penetrating projectiles —
                 *   mirrors the check cmp.w #1,38(a6) @ main.asm#L7737 where
                 *   38(a6) is the boss flag in the alien type descriptor (=1 for
                 *   lbW009114/lbW009254/lbW009374/lbW009014 boss structs, =0 for
                 *   normal alien structs lbW008F94/lbW009094/lbW008FD4).
                 * Ref: beq.b lbC00AC26 / tst.w 18(a1) / bne.b lbC00AC38
                 *      @ main.asm#L7738-L7740.
                 */
                if (!s_projectiles[pi].penetrating || g_aliens[ai].is_boss) {
                    s_projectiles[pi].active       = 0;
                    s_projectiles[pi].impact_active = 1;
                    s_projectiles[pi].impact_x     = (int)s_projectiles[pi].x;
                    s_projectiles[pi].impact_y     = (int)s_projectiles[pi].y;
                    s_projectiles[pi].impact_frame  = 0;
                    s_projectiles[pi].impact_timer  = IMPACT_FRAME_TICKS;
                    break; /* projectile gone, stop checking other aliens */
                }
                /* Penetrating: keep active, continue checking remaining aliens */
            }
        }
    }
}

void aliens_collisions_with_players(void)
{
    for (int ai = 0; ai < g_alien_count; ai++) {
        /* Only active (alive == 1) aliens can damage players.
         * Dying aliens (alive == 2) have already triggered the encounter.
         * Ref: tst.w 56(a0) / tst.w 52(a0) @ main.asm#L7636-L7656. */
        if (g_aliens[ai].alive != 1) continue;

        for (int pi = 0; pi < MAX_PLAYERS; pi++) {
            if (!g_players[pi].alive) continue;
            /* Skip players already in their death animation */
            if (g_players[pi].death_counter > 0) continue;
            if (player_is_invincible(&g_players[pi])) continue;

            /* Default AABB for normal 32×32 aliens (centre-based pos_x/pos_y).
             * Alien bbox comes from lbW008F14+4={0,0} and lbW008F14+8={32,32}.
             * Player bbox comes from add.l #$80008 (top-left +8) and
             * add.l #$100010 (size 16×16).
             * Ref: aliens_collisions_with_players @ main.asm#L7598-L7618. */
            int ax1 = (int)g_aliens[ai].pos_x - 16;
            int ax2 = (int)g_aliens[ai].pos_x + 16;
            int ay1 = (int)g_aliens[ai].pos_y - 16;
            int ay2 = (int)g_aliens[ai].pos_y + 16;

            /* Boss aliens use a larger AABB matching their 96×128 px sprite.
             * Ref: lbW00A2FA/lbW00A306/lbW00A312/lbW00A31E @ main.asm#L7093-L7096. */
            if (g_aliens[ai].is_boss) {
                ax1 = (int)g_aliens[ai].pos_x - 54;
                ax2 = (int)g_aliens[ai].pos_x + 52;
                ay1 = (int)g_aliens[ai].pos_y - 74;
                ay2 = (int)g_aliens[ai].pos_y + 48;
            }

            int px1 = (int)g_players[pi].pos_x + PLAYER_BBOX_OFFSET;
            int px2 = (int)g_players[pi].pos_x + PLAYER_BBOX_OFFSET + PLAYER_BBOX_SIZE;
            int py1 = (int)g_players[pi].pos_y + PLAYER_BBOX_OFFSET;
            int py2 = (int)g_players[pi].pos_y + PLAYER_BBOX_OFFSET + PLAYER_BBOX_SIZE;

            if (ax1 < px2 && ax2 > px1 && ay1 < py2 && ay2 > py1) {
                /* Award player credits and score for the first contact.
                 * Ref: add.l #1500,PLAYER_CREDITS / add.l #100,PLAYER_SCORE
                 *      @ main.asm#L7640-L7641. */
                g_players[pi].credits += 1500;
                g_players[pi].score   += 100;

                /* Apply damage: 36(a2)*2 = 1*2 = 2 HP.
                 * Ref: move.w 36(a2),d5 / add.w d5,d5 / sub.w d5,PLAYER_HEALTH
                 *      @ main.asm#L7650-L7652. */
                player_take_damage(&g_players[pi], 2);

                /* In the ASM, aliens_collisions_with_players NEVER kills an
                 * alien — it only sets 52(a0)=1 (touch flag) and damages the
                 * player, then returns (move.w #1,52(a0) / rts @
                 * main.asm#L7656-L7657).  The actual alien death-from-contact
                 * is driven by the alien's own AI via an internal touch counter
                 * (lbC00A582 / 58(a0)).
                 *
                 * For boss aliens (is_boss=1) this is especially critical:
                 * lbC009CE2 has NO "die from player contact" path — the boss is
                 * only killed when its strength reaches 0 via a weapon hit
                 * (lbC00AC0E: move.w #1,56(a0)) or when its companion alien2
                 * dies (lbL008D2E check at lbC009CE2).  Calling alien_kill() on
                 * a boss from here would immediately trigger level_start_
                 * destruction() the moment the boss spawns near the player.
                 *
                 * Normal aliens: keep the simplified immediate-kill for now,
                 * which is a close-enough approximation of the touch counter. */
                if (!g_aliens[ai].is_boss) {
                    alien_kill(ai);
                }

                break;  /* stop checking other players for this alien */
            }
        }
    }
}

/*
 * Scan the map from the trigger tile to find a valid spawn position for the
 * boss that lies within the same room.
 *
 * "Same room" is defined as: no alien-solid wall tile between the trigger and
 * the candidate spawn along the scan path.  The scan stops the moment it hits
 * a wall tile.
 *
 * The boss sprite is 96×128 px = 6×8 tiles.  A candidate tile at (col, row)
 * is valid only when the entire boss bounding box — 3 tiles to each side and
 * 4 tiles above/below the centre — is free of alien-solid tiles.
 *
 * Scan order (Y first since Y = up/down = "forward/backward" in the corridor):
 *   1. +Y  (downward on screen — most common boss-room direction)
 *   2. -Y  (upward)
 *   3. +X  (right)
 *   4. -X  (left)
 *
 * Returns 1 and writes (*out_wx, *out_wy) on success.
 * Returns 0 and falls back to trigger_wy + BOSS_SCAN_MAX_TILES*TILE_H on failure.
 *
 * The search starts at step BOSS_SCAN_MIN_TILES from the trigger (so we skip
 * the trigger tile itself and its immediate neighbours) and goes up to
 * BOSS_SCAN_MAX_TILES tiles away.
 */
#define BOSS_SCAN_MIN_TILES  3   /* start looking this many tiles from trigger */
#define BOSS_SCAN_MAX_TILES 30   /* give up after this many tiles (one room width) */
/* Boss bounding half-size in tiles (boss = 6×8 tiles, half = 3×4). */
#define BOSS_HALF_W_TILES    3
#define BOSS_HALF_H_TILES    4

static int find_boss_spawn(int trigger_wx, int trigger_wy,
                           int *out_wx, int *out_wy)
{
    int sc = tilemap_pixel_to_col(trigger_wx);
    int sr = tilemap_pixel_to_row(trigger_wy);

    /* Four scan directions: [0]=+Y, [1]=-Y, [2]=+X, [3]=-X */
    static const int k_dirs[4][2] = { {0,1}, {0,-1}, {1,0}, {-1,0} };

    for (int d = 0; d < 4; d++) {
        int dc = k_dirs[d][0];
        int dr = k_dirs[d][1];

        for (int step = BOSS_SCAN_MIN_TILES; step <= BOSS_SCAN_MAX_TILES; step++) {
            int c = sc + dc * step;
            int r = sr + dr * step;

            /* Stay within map bounds */
            if (c < 0 || c >= MAP_COLS || r < 0 || r >= MAP_ROWS) break;

            /* If the path itself is blocked, we have left the room — stop */
            if (tilemap_is_alien_solid(&g_cur_map, c, r)) break;

            /* Check that the boss bounding box fits with no solid tiles */
            int fits = 1;
            for (int cr = -BOSS_HALF_H_TILES; cr <= BOSS_HALF_H_TILES && fits; cr++) {
                for (int cc = -BOSS_HALF_W_TILES; cc <= BOSS_HALF_W_TILES && fits; cc++) {
                    if (tilemap_is_alien_solid(&g_cur_map, c + cc, r + cr))
                        fits = 0;
                }
            }

            if (fits) {
                /* Return the world pixel centre of the candidate tile */
                *out_wx = c * MAP_TILE_W + MAP_TILE_W / 2;
                *out_wy = r * MAP_TILE_H + MAP_TILE_H / 2;
                return 1;
            }
        }
    }

    /* Fallback: place boss BOSS_SCAN_MIN_TILES tiles below the trigger.
     * This will at least stay on the Y axis ("forward"). */
    *out_wx = trigger_wx + MAP_TILE_W / 2;
    *out_wy = trigger_wy + BOSS_SCAN_MIN_TILES * MAP_TILE_H;
    return 0;
}

/*
 * Trigger the boss encounter for the current level.
 *
 * Mirrors tile_boss_trigger → boss_nbr_N dispatch @ main.asm#L5632-L5796.
 *
 * ASM overview:
 *   1. tile_boss_trigger (L5632): guards, starts boss music, dispatches on boss_nbr.
 *   2. boss_nbr_N handler: calls set_all_aliens_to_default, then calls
 *      patch_boss_door for each alien in the encounter group (3 or 7 aliens).
 *   3. patch_boss_door (L7418): computes pixel position from map-buffer pointer
 *      (a3), sets alien speed from struct+32, HP from struct+34.
 *      pixel_x = (offset%248)*8 + struct+48 (= col*16 + 12)
 *      pixel_y = (offset/248)*16 + struct+50 (= row*16 +  4)
 *      The C port stores IFF row/col directly (no 3-row header offset).
 *
 * Boss group sizes and HP/speed from ASM structs:
 *   boss_nbr=1 (L5716): 3 aliens — lbW009114(256,4), lbW009154(256,4), lbW009194(256,4)
 *   boss_nbr=2 (L5744): 3 aliens — lbW009254(256,4), lbW009294(512,4), lbW0092D4(256,4)
 *   boss_nbr=3 (L5767): 3 aliens — lbW009314(320,4), lbW009354(320,5), lbW009394(320,5)
 *   boss_nbr=4 (L5664): 7 aliens — lbW009014(64,0 →C:4) ×7
 * Full spawn table in k_boss_groups[] above.
 */

void alien_boss_trigger(int trigger_wx, int trigger_wy)
{
    /* Erase every TILE_BOSS_TRIGGER (0x3D) tile from the map.
     * Done unconditionally before all other guards so the trigger zone is
     * always cleared regardless of boss state or level configuration.
     * Without this, tiles stepped on while g_boss_active==1 (or g_boss_nbr==0)
     * would be cleared one-by-one by player.c but the rest of the zone would
     * stay as 0x3D and could re-activate the encounter after the boss dies.
     * This is a one-time cost per activation: MAP_ROWS × MAP_COLS = 11,520
     * iterations — negligible compared to the rest of the frame budget.
     * Mirrors: patch_tiles and.w #$FFC0,(a3) @ main.asm#L5277 / L7815. */
    for (int r = 0; r < MAP_ROWS; r++) {
        for (int c = 0; c < MAP_COLS; c++) {
            if ((g_cur_map.tiles[r][c] & 0x3F) == TILE_BOSS_TRIGGER)
                tilemap_replace_tile(&g_cur_map, c, r);
        }
    }

    /* Guard: boss already active or level has no boss. */
    if (g_boss_active) return;
    if (g_boss_nbr == 0 || g_boss_nbr > MAX_BOSS_NBR) return;

    /*
     * Switch to boss music and play the "danger" voice sample.
     * Mirrors: copy boss_tune to bpsong → start_music @ main.asm#L5639-L5648;
     *          move.w #VOICE_DANGER,sample_to_play / jsr trigger_sample @ L5649-L5650.
     */
    audio_play_music("boss");
    audio_play_sample(VOICE_DANGER);

    /*
     * Kill all currently living non-boss aliens (including dying ones).
     * Mirrors: bsr set_all_aliens_to_default @ main.asm#L5721 (boss_nbr_1),
     *          called from boss_nbr_N handlers before spawning.
     */
    for (int j = 0; j < g_alien_count; j++) {
        if (!g_aliens[j].is_boss)
            g_aliens[j].alive = 0;
    }

    /*
     * Spawn the full boss encounter group for this level's boss_nbr.
     *
     * Each alien in the group is defined by k_boss_groups[g_boss_nbr].
     * Positions come from the IFF row/col stored in each BossSpawnDef (derived
     * from the ASM map-buffer pointer lbWxxxxxx passed to patch_boss_door).
     * HP and speed override the defaults set by spawn_alien_at().
     * boss_rank=0: primary boss (AI lbC009CE2 → boss_move());
     * boss_rank>0: secondary/turret (AI lbC009C68 → follows primary each frame).
     *
     * Fallback: if no k_boss_groups entry exists (boss_nbr out of range or
     * row/col = -1 via LevelDef), use find_boss_spawn().
     * Ref: patch_boss_door @ main.asm#L7418; boss_nbr_N @ main.asm#L5664.
     */
    const BossGroup *grp = &k_boss_groups[g_boss_nbr];

    if (grp->count > 0) {
        for (int k = 0; k < grp->count; k++) {
            const BossSpawnDef *def = &grp->defs[k];
            int wx = def->col * MAP_TILE_W;
            int wy = def->row * MAP_TILE_H;
            /* Clamp to map bounds — safety net for secondary positions that may
             * be slightly outside the C map (e.g. boss_nbr_2 fallback). */
            if (wx < 0) wx = 0;
            if (wy < 0) wy = 0;
            if (wx > (MAP_COLS - 1) * MAP_TILE_W) wx = (MAP_COLS - 1) * MAP_TILE_W;
            if (wy > (MAP_ROWS - 1) * MAP_TILE_H) wy = (MAP_ROWS - 1) * MAP_TILE_H;
            int idx = spawn_alien_at(wx, wy, 1, 0, 1);
            if (idx >= 0) {
                /* Override defaults with per-struct HP and speed from k_boss_groups. */
                g_aliens[idx].strength  = (WORD)(def->hp + g_global_aliens_extra_strength);
                g_aliens[idx].speed     = def->speed;
                g_aliens[idx].boss_rank = def->boss_rank;
                /*
                 * boss_nbr=4 orbit initialisation — mirrors the per-alien
                 * current-waypoint initialisation in boss_nbr_4 @ main.asm#L5664:
                 *   alien k starts at lbW0256B4 + k*36 bytes = k*9 entries.
                 * move.l #lbW0256xx,72(a0) (current ptr) sets the starting
                 * position; each frame lbC009AFC advances by one entry (4 bytes).
                 */
                if (g_boss_nbr == 4)
                    g_aliens[idx].orbit_idx = k * BOSS4_ORBIT_STEP;
            }
        }
    } else {
        /* Fallback for unconfigured levels: spawn one boss at find_boss_spawn(). */
        int bx, by;
        const LevelDef *ld = &k_level_defs[g_cur_level];
        if (ld->boss_spawn_row >= 0 && ld->boss_spawn_col >= 0) {
            bx = ld->boss_spawn_col * MAP_TILE_W;
            by = ld->boss_spawn_row * MAP_TILE_H;
        } else {
            find_boss_spawn(trigger_wx, trigger_wy, &bx, &by);
        }
        spawn_alien_at(bx, by, 1, 0, 1);
    }

    /* Mark boss encounter as active.
     * Mirrors: move.w #1,lbW0004EA @ main.asm#L5740. */
    g_boss_active = 1;
}

void alien_kill(int i)
{
    if (i < 0 || i >= g_alien_count) return;
    /* Transition to dying state: play 16-frame explosion then disappear.
     * Ref: alien_dies @ main.asm#L7308; death anim lbL018C2E#L13907. */
    g_aliens[i].alive       = 2;
    g_aliens[i].death_frame = 0;
    /* Play alien death sound.
     * Ref: alien_dies @ main.asm#L7308; samples_table[21] = smp_dying_alien. */
    audio_play_sample(SAMPLE_DYING_ALIEN);

    /*
     * Boss death handling — mirrors lbC009F62 @ main.asm#L6991.
     *
     * Only the PRIMARY boss (boss_rank=0) triggers the full boss-death sequence:
     *   1. Clear g_boss_active (clr.w lbW0004EA @ main.asm#L6993).
     *   2. Kill all remaining companion boss aliens so they don't linger.
     *   3. boss_nbr=1: self-destruct triggered by AI lbC009CE2 (L6818).
     *   4. boss_nbr=2 or 3: self-destruct via lbC00A0EE/lbC00A1BA (L7049/L7067).
     *   5. boss_nbr=4: no self-destruct; exit is handled via lbW0004EE elsewhere.
     *
     * Secondary bosses (boss_rank>0, AI lbC009C68) have no death handler —
     * they just disappear.  Self-destruct is NOT triggered when they die.
     * Ref: lbC009C68 @ main.asm#L6777 (no branch to lbC009F62).
     */
    if (g_aliens[i].is_boss && g_aliens[i].boss_rank == 0) {
        g_boss_active = 0;
        /* Kill all companion boss aliens (secondary turrets, other primaries).
         * Mirrors: companions deactivated as a side-effect of the boss-death
         * sequence (the game state that drives them is cleared). */
        for (int j = 0; j < g_alien_count; j++) {
            if (j == i) continue;
            if (g_aliens[j].alive == 1 && g_aliens[j].is_boss) {
                g_aliens[j].alive       = 2;
                g_aliens[j].death_frame = 0;
            }
        }
        /* Trigger self-destruct for boss_nbr 1, 2, 3.
         * Ref: move.w #1,self_destruct_initiated @ main.asm#L6818 (nbr1),
         *      L7050 (nbr2), L7067 (nbr3). */
        if (g_boss_nbr >= 1 && g_boss_nbr <= 3) {
            if (!g_self_destruct_initiated)
                level_start_destruction();
        }
    }

    /* Track kill for INTEX stats (Ref: run_intex @ main.asm#L8975) */
    /* Award kill to all players (credit split between shooting/damaged) */
    for (int p = 0; p < MAX_PLAYERS; p++) {
        if (g_players[p].alive)
            g_players[p].aliens_killed++;
    }
}

int alien_living_count(void)
{
    int n = 0;
    for (int i = 0; i < g_alien_count; i++)
        if (g_aliens[i].alive == 1) n++;
    return n;
}

/*
 * Draw a BOB (Blitter Object) from the alien atlas at screen position (sx,sy).
 * atlas_x, atlas_y : top-left of the sprite in the atlas (pixel coordinates).
 * w, h             : sprite dimensions in pixels.
 * Transparency: color index 0 is treated as transparent (blitter minterm $CA).
 * Ref: disp_sprite / blitter BOB rendering @ main.asm#L12365-L12411.
 */
static void draw_atlas_bob(int sx, int sy, int atlas_x, int atlas_y, int w, int h)
{
    const UBYTE *atlas = alien_gfx_get_atlas();
    if (!atlas) return;
    /* Clip to viewport */
    if (sx >= 320 || sy >= 256 || sx + w <= 0 || sy + h <= 0) return;
    int src_x = atlas_x, src_y = atlas_y;
    int dst_x = sx, dst_y = sy;
    int bw = w, bh = h;
    if (dst_x < 0) { src_x -= dst_x; bw += dst_x; dst_x = 0; }
    if (dst_y < 0) { src_y -= dst_y; bh += dst_y; dst_y = 0; }
    if (dst_x + bw > 320) bw = 320 - dst_x;
    if (dst_y + bh > 256) bh = 256 - dst_y;
    if (bw <= 0 || bh <= 0) return;
    const UBYTE *src = atlas + src_y * ALIEN_ATLAS_W + src_x;
    video_blit(src, ALIEN_ATLAS_W, dst_x, dst_y, bw, bh, 0);
}

void projectiles_render(void)
{
    extern int g_camera_x, g_camera_y;

    for (int i = 0; i < MAX_PROJECTILES; i++) {
        /*
         * Render impact flash animation (8-frame 16×16 explosion BOB).
         * Shown after the projectile deactivates on wall or alien hit.
         * Mirrors lbL018CBA BOB animation @ main.asm#L13925.
         * BOBs 56-63: lbW018D4A entries 56-63 @ main.asm#L14001-L14008.
         */
        if (s_projectiles[i].impact_active) {
            int f = s_projectiles[i].impact_frame;
            if (f < IMPACT_ANIM_FRAMES) {
                int ix = s_projectiles[i].impact_x - g_camera_x - 8;
                int iy = s_projectiles[i].impact_y - g_camera_y - 7;
                draw_atlas_bob(ix, iy,
                               k_impact_frames[f][0], k_impact_frames[f][1],
                               k_impact_frames[f][2], k_impact_frames[f][3]);
            }
            continue;
        }

        if (!s_projectiles[i].active) continue;

        int sx = (int)(WORD)s_projectiles[i].x - g_camera_x;
        int sy = (int)(WORD)s_projectiles[i].y - g_camera_y;

        switch (s_projectiles[i].weapon_type) {

        case WEAPON_MACHINEGUN:
            /*
             * MACHINEGUN: Direction-dependent 16×14 muzzle-flash BOB shown
             * for ONE tick only (muzzle flash at the cannon position).
             * ASM lbL00EAEE: dc.l BOB,0 (delay=0 = 1 tick) then
             *                dc.l lbW01389C,32000,-1 (blank for the rest).
             * flight_anim_frame==0: first tick → draw sprite.
             * flight_anim_frame==1: all later ticks → bullet invisible.
             * Ref: lbL00EACA @ main.asm#L10010-L10034.
             */
            if (s_projectiles[i].flight_anim_frame == 0) {
                int dir = s_projectiles[i].direction;
                if (dir < 0 || dir > 8) dir = 0;
                draw_atlas_bob(sx - 8, sy - 7,
                               k_machinegun_atlas[dir][0], k_machinegun_atlas[dir][1],
                               k_machinegun_atlas[dir][2], k_machinegun_atlas[dir][3]);
            }
            break;

        case WEAPON_TWINFIRE:
            /*
             * TWINFIRE: Direction-dependent single-frame 16×14 BOB.
             * BOB animation structs lbL017B4E-lbL017C9E → lbL01790A entries 0-7.
             * lbW0188CE descriptors at entries 0-7, all at y=160=0xA0.
             * Ref: lbL00EB8E/lbL00EBB2-lbL00EC06 @ main.asm#L10036-L10052.
             * Sprite centred on projectile position (blit at sx-8, sy-7).
             */
            {
                int dir = s_projectiles[i].direction;
                if (dir < 0 || dir > 8) dir = 0;
                draw_atlas_bob(sx - 8, sy - 7,
                               k_twinfire_atlas[dir][0], k_twinfire_atlas[dir][1],
                               k_twinfire_atlas[dir][2], k_twinfire_atlas[dir][3]);
            }
            break;

        case WEAPON_FLAMEARC:
            /*
             * FLAMEARC: 8-frame animated 16×14 BOB, same sequence for all directions.
             * BOB animation structs lbL017E4E-lbL017F9E → lbL01790A entries 16-23.
             * lbW0188CE descriptors at entries 16-23 (all at y=160=0xA0), delay=0.
             * Ref: lbL00EC36 @ main.asm#L10063-L10070.
             */
            {
                int f = s_projectiles[i].flight_anim_frame;
                draw_atlas_bob(sx - 8, sy - 7,
                               k_flamearc_atlas[f][0], k_flamearc_atlas[f][1],
                               k_flamearc_atlas[f][2], k_flamearc_atlas[f][3]);
            }
            break;

        case WEAPON_PLASMAGUN:
            /*
             * PLASMAGUN: Direction-dependent single-frame 16×14 BOB.
             * BOB animation structs lbL01814E-lbL01829E → lbL01790A entries 32-39.
             * lbW0188CE descriptors at entries 32-39.
             * Ref: lbL00ECFE/lbL00ED22-lbL00ED76 @ main.asm#L10090-L10106.
             * Sprite centred on projectile position (blit at sx-8, sy-7).
             */
            {
                int dir = s_projectiles[i].direction;
                if (dir < 0 || dir > 8) dir = 0;
                draw_atlas_bob(sx - 8, sy - 7,
                               k_plasmagun_atlas[dir][0], k_plasmagun_atlas[dir][1],
                               k_plasmagun_atlas[dir][2], k_plasmagun_atlas[dir][3]);
            }
            break;

        case WEAPON_FLAMETHROWER:
            /*
             * FLAMETHROWER: cycle through all 8 impact frames at 2 ticks/frame
             * (delay=1 in lbL018D06 @ main.asm#L13935).  The sequence loops back
             * to frame 0 via the -1 terminator exactly as lbC011B37 does on the
             * Amiga blitter at 25 Hz.
             * elapsed = FLAME_LIFETIME_TICKS - lifetime → 0..15
             * frame  = (elapsed / 2) % 8              → 0..7, 2 ticks each
             */
            {
                int elapsed = FLAME_LIFETIME_TICKS - (int)s_projectiles[i].lifetime;
                int frame   = (elapsed / 2) % IMPACT_ANIM_FRAMES;
                draw_atlas_bob(sx - 8, sy - 7,
                               k_impact_frames[frame][0], k_impact_frames[frame][1],
                               k_impact_frames[frame][2], k_impact_frames[frame][3]);
            }
            break;

        case WEAPON_SIDEWINDERS:
            /*
             * SIDEWINDERS: Direction-dependent single-frame 16×14 BOB.
             * BOB animation structs lbL017CCE-lbL017E1E → lbL01790A entries 8-15.
             * lbW0188CE descriptors at entries 8-15 (all at y=176=0xB0).
             * Ref: lbL00EC7A/lbL00EC9E-lbL00ECF2 @ main.asm#L10072-L10088.
             * Fire pattern: arc-spread (3-shot alternating burst, same mechanism as
             * FLAMEARC and PLASMAGUN — lbC00E200 @ main.asm#L9470), but with
             * doubled direction-vector offsets (d6/d7 doubled before branching,
             * giving a wider spread than FLAMEARC).
             * Sprite centred on projectile position (blit at sx-8, sy-7).
             */
            {
                int dir = s_projectiles[i].direction;
                if (dir < 0 || dir > 8) dir = 0;
                draw_atlas_bob(sx - 8, sy - 7,
                               k_sidewinders_atlas[dir][0], k_sidewinders_atlas[dir][1],
                               k_sidewinders_atlas[dir][2], k_sidewinders_atlas[dir][3]);
            }
            break;

        case WEAPON_LAZER:
            /*
             * LAZER: Penetrating beam — 16×14 BOB, direction-dependent.
             * BOB animation structs → lbL01790A entries 68-71; lbW0188CE entries 68-71.
             * Ref: lbL00EDAA / lbL00EDCE-lbL00EE22 @ main.asm#L10119-L10135.
             */
            {
                int dir = s_projectiles[i].direction;
                if (dir < 0 || dir > 8) dir = 0;
                draw_atlas_bob(sx - 8, sy - 7,
                               k_lazer_atlas[dir][0], k_lazer_atlas[dir][1],
                               k_lazer_atlas[dir][2], k_lazer_atlas[dir][3]);
            }
            break;

        default:
            /* Unknown weapon type — fall back to simple dot */
            if (sx >= -2 && sx < 322 && sy >= -2 && sy < 258)
                video_fill_rect(sx - 1, sy - 1, 3, 3, 1);
            break;
        }
    }
}
