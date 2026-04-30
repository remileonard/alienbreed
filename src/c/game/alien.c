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

/* Alien type table: HP base values (Ref: alien1_struct–alien7_struct @ main.asm#L5918-L5967) */
static const WORD k_alien_type_hp[7] = {
    100,  /* type 1 */
    132,  /* type 2 */
    164,  /* type 3 */
    196,  /* type 4 */
    228,  /* type 5 */
    260,  /* type 6 */
    292   /* type 7 */
};

/*
 * Projectile struct — extends simple position/velocity with weapon-specific
 * behaviour flags derived from weapons_behaviour_table @ main.asm#L10000:
 *
 *   weapon_type   : WEAPON_* constant (used to select rendering and behaviour)
 *   penetrating   : 1 = passes through aliens (weapons 4/5/7).
 *                   From offset 18(a3) in the ASM projectile struct, set from
 *                   field 4 of weapons_attr_table (=01 for PLASMAGUN/FLAMETHROWER/LAZER).
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
 * Returns the index in g_aliens[] of the newly placed alien, or -1 if
 * no slot was available.  Dead slots are recycled before appending new
 * ones so that the pool does not exhaust after many spawns/deaths.
 * ----------------------------------------------------------------------- */
static int spawn_alien_at(int wx, int wy, int alien_type)
{
    WORD base_hp = (alien_type >= 1 && alien_type <= 7)
                   ? k_alien_type_hp[alien_type - 1]
                   : k_alien_type_hp[0];

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

    Alien *a    = &g_aliens[idx];
    a->pos_x    = (WORD)wx;
    a->pos_y    = (WORD)wy;
    a->speed    = (WORD)(2 + g_global_aliens_extra_strength / 5);
    a->strength = (WORD)(base_hp + g_global_aliens_extra_strength);
    a->alive    = 1;
    a->type_idx = alien_type - 1;
    a->death_frame = 0;
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
 *
 * Tile 0x34 (TILE_ALIEN_HOLE) is tile_not_used in the main tile action table
 * and is NOT a spawn point.
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
            if (attr != TILE_ALIEN_SPAWN_BIG &&
                attr != TILE_ALIEN_SPAWN_SMALL) continue;

            SpawnPoint *sp = &s_spawn_points[s_spawn_count++];
            sp->world_x    = (WORD)(col * MAP_TILE_W + MAP_TILE_W / 2);
            sp->world_y    = (WORD)(row * MAP_TILE_H + MAP_TILE_H / 2);
            sp->countdown  = SPAWN_COUNTDOWN_INIT;
            sp->alien_type = alien_type;
            sp->active     = 1;
            sp->one_shot   = 0;
            sp->spawned_alien_idx = -1;
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

        if (!sp->one_shot) {
            /*
             * Map spawn point (tile 0x28/0x29): only check in the 80-px
             * off-screen approach band.  If the tile is already on screen
             * the countdown is paused — alien spawning from a visible tile
             * would make the alien appear out of thin air.
             * (In the original ASM, lbC00D22A registers the slot only when
             * the tile first enters the viewport during scrolling, so it
             * always starts at the screen edge — never from mid-screen.)
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
        int idx = spawn_alien_at(sp->world_x, sp->world_y, sp->alien_type);
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
    /* ------------------------------------------------------------------ */
    int dx = tx - ax;
    if (dx > 4) {
        /* Move right: 3 probes at right edge (nx+14), y = -22, 0, -12 */
        int nx = ax + spd;
        if (!alien_solid_at(nx + 14, ay - 22) &&
            !alien_solid_at(nx + 14, ay +  0) &&
            !alien_solid_at(nx + 14, ay - 12) &&
            !alien_overlaps_other(self_idx, nx, ay)) {
            ax = nx;
            dir_bits |= 4;  /* right */
        }
    } else if (dx < -4) {
        /* Move left: 3 probes at left edge (nx-20), y = -22, 0, -12 */
        int nx = ax - spd;
        if (!alien_solid_at(nx - 20, ay - 22) &&
            !alien_solid_at(nx - 20, ay +  0) &&
            !alien_solid_at(nx - 20, ay - 12) &&
            !alien_overlaps_other(self_idx, nx, ay)) {
            ax = nx;
            dir_bits |= 8;  /* left */
        }
    }

    /* ------------------------------------------------------------------ */
    /* 3. Y movement — independent of X                                    */
    /*    3 probes along the leading edge of the proposed bbox, matching   */
    /*    lbW00A2EE (down) and lbW00A2E2 (up) at main.asm#L7087-7088.     */
    /*    Offsets are ASM values − 16 (centre-based pos_x/pos_y).         */
    /* ------------------------------------------------------------------ */
    int dy = ty - ay;
    if (dy > 4) {
        /* Move down: 3 probes at bottom edge (ny+4), x = -16, +6, -6 */
        int ny = ay + spd;
        if (!alien_solid_at(ax - 16, ny + 4) &&
            !alien_solid_at(ax +  6, ny + 4) &&
            !alien_solid_at(ax -  6, ny + 4) &&
            !alien_overlaps_other(self_idx, ax, ny)) {
            ay = ny;
            dir_bits |= 1;  /* down */
        }
    } else if (dy < -4) {
        /* Move up: 3 probes at top edge (ny-26), x = -16, +6, -6 */
        int ny = ay - spd;
        if (!alien_solid_at(ax - 16, ny - 26) &&
            !alien_solid_at(ax +  6, ny - 26) &&
            !alien_solid_at(ax -  6, ny - 26) &&
            !alien_overlaps_other(self_idx, ax, ny)) {
            ay = ny;
            dir_bits |= 2;  /* up */
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
    /* If dir_bits == 0 (no movement), keep the current direction. */
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

        alien_move(i, &g_aliens[i]);
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

        /* FLAMETHROWER: flight_anim_frame not used for in-flight rendering
         * (bullet is invisible during flight; animation spawned at end of life). */

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
                /* Left button: col+0, col+3, col+2, col+4 */
                tilemap_replace_tile(&g_cur_map, col,     row);
                tilemap_replace_tile(&g_cur_map, col + 3, row);
                tilemap_replace_tile(&g_cur_map, col + 2, row);
                tilemap_replace_tile(&g_cur_map, col + 4, row);
            } else {
                /* Right button: col+0, col-1, col-2, col-4 */
                tilemap_replace_tile(&g_cur_map, col,     row);
                tilemap_replace_tile(&g_cur_map, col - 1, row);
                tilemap_replace_tile(&g_cur_map, col - 2, row);
                tilemap_replace_tile(&g_cur_map, col - 4, row);
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
                    tilemap_replace_tile(&g_cur_map, col, row);
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

            int ax1 = (int)g_aliens[ai].pos_x - 16;
            int ax2 = (int)g_aliens[ai].pos_x + 16;
            int ay1 = (int)g_aliens[ai].pos_y - 16;
            int ay2 = (int)g_aliens[ai].pos_y + 16;

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
                 * flying after hitting an alien.
                 * Non-penetrating weapons deactivate and play an impact flash.
                 * Ref: tst.w 18(a1) / bne.b lbC00AC38 @ main.asm#L7739-L7744.
                 */
                if (!s_projectiles[pi].penetrating) {
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

            /* Bounding-box collision (AABB):
             *   Alien  bbox : [pos_x-16, pos_x+16] × [pos_y-16, pos_y+16]
             *                 (centre-based; equivalent to ASM [pos_x, pos_x+32])
             *   Player bbox : [pos_x+BBOX_OFFSET, pos_x+BBOX_OFFSET+SIZE]
             *                 = [pos_x−8, pos_x+8] × [pos_y−8, pos_y+8]
             *
             * Alien bbox comes from lbW008F14+4={0,0} and lbW008F14+8={32,32}.
             * Player bbox comes from add.l #$80008 (top-left +8) and
             * add.l #$100010 (size 16×16).
             * Ref: aliens_collisions_with_players @ main.asm#L7598-L7618. */
            int ax1 = (int)g_aliens[ai].pos_x - 16;
            int ax2 = (int)g_aliens[ai].pos_x + 16;
            int ay1 = (int)g_aliens[ai].pos_y - 16;
            int ay2 = (int)g_aliens[ai].pos_y + 16;

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

                /* Transition the alien to its dying state so it plays the
                 * explosion animation and is removed from future collision
                 * checks.  Mirrors the lbC00A582 "touches player" branch that
                 * eventually calls set_alien_default_vars @ main.asm#L7280. */
                alien_kill(ai);

                break;  /* alien is now dying; stop checking other players */
            }
        }
    }
}

void alien_kill(int i)
{
    if (i < 0 || i >= g_alien_count) return;
    /* Transition to dying state: play 16-frame explosion then disappear.
     * Ref: alien_dies @ main.asm#L7308; death anim lbL018C2E#L13907. */
    g_aliens[i].alive       = 2;
    g_aliens[i].death_frame = 0;
    /* Play alien death sound (not hatching sound) */
    audio_play_sample(SAMPLE_DYING_PLAYER);  /* closest match for alien death */
    
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
             * FLAMETHROWER: show frame 0 (small flame) as a static in-flight sprite.
             * The full 8-frame explosion animation is spawned as an impact_active
             * event when the bullet reaches its maximum range or hits a wall.
             */
            draw_atlas_bob(sx - 8, sy - 7,
                           k_impact_frames[0][0], k_impact_frames[0][1],
                           k_impact_frames[0][2], k_impact_frames[0][3]);
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
