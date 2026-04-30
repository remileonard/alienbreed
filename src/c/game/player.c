/*
 * Alien Breed SE 92 - C port
 * Player module
 */

#include "player.h"
#include "level.h"
#include "alien.h"
#include "intex.h"
#include "../hal/input.h"
#include "../hal/audio.h"
#include "../hal/video.h"
#include "../engine/tilemap.h"
#include <string.h>
#include <stdio.h>

/* Update acid pool damage counter (persists across frames while on acid) */
static int g_acid_damage_counter[MAX_PLAYERS] = {0, 0};

/*
 * Arc-pattern fire counter — shared between both players, matching the
 * single lbL00EAA6 global in the original ASM.
 * Cycles: 1=straight, 2=+spread, 3=-spread (then reset to 0).
 * Ref: lbL00EAA6 / addq.w #1,(a1) @ main.asm#L9470-L9482.
 */
static int s_arc_counter = 0;

Player g_players[MAX_PLAYERS];
int    g_player_invincibility[MAX_PLAYERS];

/* Invincibility duration in frames after being hit */
#define INVINCIBILITY_FRAMES 60

void player_init_variables(void)
{
    memset(g_players, 0, sizeof(g_players));
    memset(g_player_invincibility, 0, sizeof(g_player_invincibility));

    for (int i = 0; i < MAX_PLAYERS; i++) {
        Player *p    = &g_players[i];
        p->port      = i;
        p->alive       = 1;
        p->health      = PLAYER_MAX_HEALTH;
        p->lives       = 4;               /* Ref: move.w #4,PLAYER_LIVES @ init_player_dats, main.asm#L1007 */
        p->credits     = 0;
        p->score       = 0;
        p->keys        = 0;               /* Ref: clr.w PLAYER_KEYS @ init_player_dats, main.asm#L1010 */
        p->ammopacks   = 2;               /* Ref: move.w #2,PLAYER_AMMOPACKS @ init_player_dats, main.asm#L1008 */
        p->ammunitions = PLAYER_MAX_AMMO; /* Ref: move.w #PLAYER_MAX_AMMO,PLAYER_AMMUNITIONS @ init_player_dats, main.asm#L1009 */
        p->shot_amount = 4;               /* Ref: move.w #4,PLAYER_SHOT_AMOUNT @ init_player_dats, main.asm#L998 */
        p->direction   = PLAYER_FACE_DOWN;
        /* Animation: initial body pose = 3 (facing down), matching ASM move.w #3,PLAYER_CUR_SPRITE */
        p->cur_sprite        = 3;
        p->anim_flipflop     = 0;
        p->anim_fire_counter = 0;
        p->anim_state        = 0;
        p->anim_seq_frame    = 0;
        p->anim_seq_timer    = 2;
        p->anim_seq_id       = -1;
        p->death_counter     = 0;
        player_set_cur_weapon(p, WEAPON_MACHINEGUN);
        p->owned_weapons[WEAPON_MACHINEGUN - 1] = 1;
    }
}

void player_set_cur_weapon(Player *p, int weapon_id)
{
    if (weapon_id < WEAPON_MACHINEGUN || weapon_id >= WEAPON_MAX) return;
    p->cur_weapon = (WORD)weapon_id;

    /* Weapon parameters from weapons_attr_table @ main.asm#L736.
     * Format: index, speed, rate, strength, ???, sample, shots_per_ammo
     * dc.w  01,16,03, 9,00,SAMPLE_FIRE_GUN,4
     * dc.w  02,16,08,13,00,4,3
     * dc.w  03,12,09,19,00,2,2
     * dc.w  04,14,08,12,01,0,1
     * dc.w  05, 8,03,12,01,6,1
     * dc.w  06,16,08,32,00,4,1
     * dc.w  07, 8,08,18,01,3,1 */
    static const struct { WORD speed; WORD rate; WORD strength; WORD smp; WORD shot_amount; } k_wdata[] = {
        {  0,  0,  0, 0,              0 }, /* placeholder (index 0 unused) */
        { 16,  3,  9, SAMPLE_FIRE_GUN, 4 }, /* 1: MACHINEGUN   */
        { 16,  8, 13, SAMPLE_FIRE_GUN, 3 }, /* 2: TWINFIRE     */
        { 12,  9, 19, SAMPLE_FIRE_GUN, 2 }, /* 3: FLAMEARC     */
        { 14,  8, 12, SAMPLE_FIRE_GUN, 1 }, /* 4: PLASMAGUN    */
        {  8,  3, 12, SAMPLE_FIRE_GUN, 1 }, /* 5: FLAMETHROWER */
        { 16,  8, 32, SAMPLE_FIRE_GUN, 1 }, /* 6: SIDEWINDERS  */
        {  8,  8, 18, SAMPLE_FIRE_GUN, 1 }, /* 7: LAZER        */
    };

    if (weapon_id < WEAPON_MAX) {
        p->weapon_speed    = k_wdata[weapon_id].speed;
        p->weapon_rate     = k_wdata[weapon_id].rate;
        p->weapon_strength = k_wdata[weapon_id].strength;
        p->weapon_smp      = k_wdata[weapon_id].smp;
        p->shot_amount     = k_wdata[weapon_id].shot_amount;
    }
    p->weapon_rate_counter  = 0;
    p->shot_amount_counter  = 0;
}

void player_set_starting_positions(void)
{
    /* Find the spawn tile (attribute 0x35) in the loaded map, mirroring
     * search_starting_position + set_players_starting_pos in main.asm.
     * Default if not found: ASM uses (1280, 320) = tile (80, 20) top-left;
     * we use the tile centre instead. */
    int spawn_x = 80 * MAP_TILE_W + MAP_TILE_W / 2;
    int spawn_y = 20 * MAP_TILE_H + MAP_TILE_H / 2;
    tilemap_find_spawn(&g_cur_map, &spawn_x, &spawn_y);

    fprintf(stderr, "[spawn] world pos=(%d,%d) tile=(%d,%d) attr=0x%02x\n",
            spawn_x, spawn_y,
            tilemap_pixel_to_col(spawn_x), tilemap_pixel_to_row(spawn_y),
            tilemap_attr(&g_cur_map,
                         tilemap_pixel_to_col(spawn_x),
                         tilemap_pixel_to_row(spawn_y)));

    g_players[0].pos_x = (WORD)spawn_x;
    g_players[0].pos_y = (WORD)spawn_y;
    if (MAX_PLAYERS > 1) {
        /* Player 2 starts 16 pixels to the right and below player 1 */
        g_players[1].pos_x = (WORD)(spawn_x + 16);
        g_players[1].pos_y = (WORD)(spawn_y + 16);
    }
}

UWORD player_get_input(const Player *p)
{
    return (p->port == 0) ? g_player1_input : g_player2_input;
}



/*
 * Returns 1 if the tile at world pixel (x,y) is blocking for this player.
 * Walls are always blocking; doors block only when the player carries no key.
 */
static int tile_blocks(int x, int y, int has_key)
{
    int col = tilemap_pixel_to_col(x);
    int row = tilemap_pixel_to_row(y);
    if (tilemap_is_solid(&g_cur_map, col, row)) return 1;
    /* Ref: tile_door → bra tile_wall path @ main.asm#L5231 */
    if (!has_key && tilemap_attr(&g_cur_map, col, row) == TILE_DOOR) return 1;
    return 0;
}

/*
 * Resolve movement with directional wall collision.
 * Only the *leading edge* in the direction of movement is probed, at three
 * sample points that span the player's body — matching the ASM probe tables.
 * Returns 1 if movement was applied, 0 if blocked.
 */
static int try_move(Player *p, int dx, int dy)
{
    int nx = p->pos_x + dx;
    int ny = p->pos_y + dy;

    /* Clamp to world bounds */
    if (nx < MAP_TILE_W) nx = MAP_TILE_W;
    if (ny < MAP_TILE_H) ny = MAP_TILE_H;
    if (nx >= (MAP_COLS - 1) * MAP_TILE_W) nx = (MAP_COLS - 1) * MAP_TILE_W - 1;
    if (ny >= (MAP_ROWS - 1) * MAP_TILE_H) ny = (MAP_ROWS - 1) * MAP_TILE_H - 1;

    int has_key = (p->keys > 0);

    /* Horizontal leading-edge check (right or left only).
     * 3 probes from the probe table (lbW007B22/lbW007B16) + fixed centre
     * probe at (nx+10, ny+6) mirroring lbC007B98 main.asm#L5038-5054. */
    if (dx > 0) {
        int px = nx + PROBE_RIGHT_X;
        for (int i = 0; i < 3; i++)
            if (tile_blocks(px, ny + k_probe_hy[i], has_key)) return 0;
    } else if (dx < 0) {
        int px = nx + PROBE_LEFT_X;
        for (int i = 0; i < 3; i++)
            if (tile_blocks(px, ny + k_probe_hy[i], has_key)) return 0;
    }

    /* Vertical leading-edge check (down or up only).
     * 3 probes from the probe table (lbW007B3A/lbW007B2E). */
    if (dy > 0) {
        int py = ny + PROBE_DOWN_Y;
        for (int i = 0; i < 3; i++)
            if (tile_blocks(nx + k_probe_vx[i], py, has_key)) return 0;
    } else if (dy < 0) {
        int py = ny + PROBE_UP_Y;
        for (int i = 0; i < 3; i++)
            if (tile_blocks(nx + k_probe_vx[i], py, has_key)) return 0;
    }

    /* Fixed centre probe — always checked regardless of direction.
     * Catches solid tiles at the player's exact centre (safety net for
     * corner cases not covered by the three directional samples). */
    if (tile_blocks(nx, ny, has_key)) return 0;

    p->pos_x = (WORD)nx;
    p->pos_y = (WORD)ny;
    return 1;
}

/* Patch the tile at (col, row) to floor and open any adjacent paired door tile.
 * Shared by open_door() and open_door_at().
 * Ref: animation overlay at lbL020CFE/lbL020D32 covers both tiles of a 2-tile door. */
static void patch_door_tiles(int col, int row)
{
    tilemap_replace_tile(&g_cur_map, col, row);

    const int dirs[4][2] = { {0, -1}, {0, 1}, {-1, 0}, {1, 0} };
    for (int i = 0; i < 4; i++) {
        int adj_col = col + dirs[i][0];
        int adj_row = row + dirs[i][1];
        if (adj_col >= 0 && adj_col < MAP_COLS && adj_row >= 0 && adj_row < MAP_ROWS) {
            if (tilemap_attr(&g_cur_map, adj_col, adj_row) == TILE_DOOR) {
                tilemap_replace_tile(&g_cur_map, adj_col, adj_row);
            }
        }
    }
}

/* Open a door at the player's current tile position.
 * Mirrors open_door routine @ main.asm#L5246.
 *
 * The ASM detects an adjacent TILE_DOOR then patches the CURRENT tile (a3)
 * to floor.  In this port we replicate that exactly: the tile the player
 * stands on is cleared.  If that tile has a paired door partner (the other
 * half of the 2-tile door), we clear that partner in the same call so that
 * the entire doorway opens at once (the original relied on a wide sprite
 * overlay animation to cover both tiles visually). */
void open_door(Player *p)
{
    if (p->keys <= 0) return;

    int col = tilemap_pixel_to_col(p->pos_x);
    int row = tilemap_pixel_to_row(p->pos_y);

    patch_door_tiles(col, row);

    p->keys--;
    audio_play_sample(SAMPLE_OPENING_DOOR);
}

/* Open the door tile at (col, row) and its paired neighbour, consuming one key.
 * Mirrors the force_door path (lbC00E56C → open_door) used when a projectile
 * accumulates >= 300 damage on a door tile.  The door position is already known
 * (the hit tile's col/row) so we open it directly rather than re-deriving it
 * from the player's walk position. */
void open_door_at(Player *p, int col, int row)
{
    if (p->keys <= 0) return;

    patch_door_tiles(col, row);

    p->keys--;
    audio_play_sample(SAMPLE_OPENING_DOOR);
}

/* Check and handle tile interaction at player's current position.
 * Called from player_update() after each movement.
 * Mirrors logic from tiles_action_table @ main.asm#L5059.
 *
 * One-way / climb tiles work like the assembly: rather than blocking movement
 * they set extra_spd in the opposite direction.  The extra_spd is applied on
 * the next movement tick, producing a push that either slows (climb: ±1) or
 * prevents (one-way: ±2) movement in the "wrong" direction.
 * Ref: tile_one_way_* / tile_climb_* @ main.asm#L5451-L5816.
 */
/*
 * Try to collect a floor pickup at tile (col, row).
 * Returns 1 if the tile was a consumable item and was collected; 0 otherwise.
 * tilemap_replace_tile ensures each tile fires at most once even when several
 * probes hit it in the same frame.
 *
 * Ref: tile_key / tile_ammo / tile_first_aid / tile_1up /
 *      tile_add_100_credits / tile_add_1000_credits @ main.asm#L5323-L5412.
 */
static int pickup_tile_at(Player *p, int col, int row)
{
    UBYTE attr = tilemap_attr(&g_cur_map, col, row);
    switch (attr) {
    case TILE_KEY:
        /* ASM: addq.w #1,PLAYER_KEYS(a0) — no cap; HUD shows ">6" above six keys */
        p->keys++;
        tilemap_replace_tile(&g_cur_map, col, row);
        audio_play_sample(SAMPLE_KEY);
        return 1;
    case TILE_FIRST_AID:
        p->health += 20;
        if (p->health > PLAYER_MAX_HEALTH) p->health = PLAYER_MAX_HEALTH;
        tilemap_replace_tile(&g_cur_map, col, row);
        audio_play_sample(SAMPLE_1STAID_CREDS);
        return 1;
    case TILE_AMMO:
        /* ASM sets ammo to MAX first, then increments pack count */
        p->ammunitions = PLAYER_MAX_AMMO;
        p->ammopacks++;
        if (p->ammopacks > PLAYER_MAX_AMMOPCKS) p->ammopacks = PLAYER_MAX_AMMOPCKS;
        tilemap_replace_tile(&g_cur_map, col, row);
        audio_play_sample(SAMPLE_AMMO);
        return 1;
    case TILE_1UP:
        p->lives++;
        tilemap_replace_tile(&g_cur_map, col, row);
        audio_play_sample(SAMPLE_1UP);
        return 1;
    case TILE_CREDITS_100:
        p->credits += 5000;
        tilemap_replace_tile(&g_cur_map, col, row);
        audio_play_sample(SAMPLE_1STAID_CREDS);
        return 1;
    case TILE_CREDITS_1000:
        p->credits += 50000;
        tilemap_replace_tile(&g_cur_map, col, row);
        audio_play_sample(SAMPLE_1STAID_CREDS);
        return 1;
    default:
        return 0;
    }
}

void check_tile_interaction(Player *p)
{
    int col = tilemap_pixel_to_col(p->pos_x);
    int row = tilemap_pixel_to_row(p->pos_y);
    
    /*
     * The ASM dispatches the tile action table for 4 probe positions each frame:
     *   probes 1-3: leading-edge probes in the current movement direction
     *   probe  4:   fixed centre probe (~player centre)
     * All 4 probes trigger pickup tiles.  Non-pickup effects (door, exit,
     * acid …) are driven by the centre probe only.
     * Ref: lbC007B4C @ main.asm#L4952; probe tables lbW007B16-lbW007B3A.
     */

    /* Probes 1-3: leading-edge probes — pickups only. */
    {
        int d    = p->direction;
        int go_r = (d == PLAYER_FACE_RIGHT || d == PLAYER_FACE_UP_RIGHT || d == PLAYER_FACE_DOWN_RIGHT);
        int go_l = (d == PLAYER_FACE_LEFT  || d == PLAYER_FACE_UP_LEFT  || d == PLAYER_FACE_DOWN_LEFT);
        int go_d = (d == PLAYER_FACE_DOWN  || d == PLAYER_FACE_DOWN_LEFT || d == PLAYER_FACE_DOWN_RIGHT);
        int go_u = (d == PLAYER_FACE_UP    || d == PLAYER_FACE_UP_LEFT  || d == PLAYER_FACE_UP_RIGHT);

        if (go_r) {
            int px = p->pos_x + PROBE_RIGHT_X;
            for (int i = 0; i < 3; i++)
                pickup_tile_at(p, tilemap_pixel_to_col(px),
                                  tilemap_pixel_to_row(p->pos_y + k_probe_hy[i]));
        }
        if (go_l) {
            int px = p->pos_x + PROBE_LEFT_X;
            for (int i = 0; i < 3; i++)
                pickup_tile_at(p, tilemap_pixel_to_col(px),
                                  tilemap_pixel_to_row(p->pos_y + k_probe_hy[i]));
        }
        if (go_d) {
            int py = p->pos_y + PROBE_DOWN_Y;
            for (int i = 0; i < 3; i++)
                pickup_tile_at(p, tilemap_pixel_to_col(p->pos_x + k_probe_vx[i]),
                                  tilemap_pixel_to_row(py));
        }
        if (go_u) {
            int py = p->pos_y + PROBE_UP_Y;
            for (int i = 0; i < 3; i++)
                pickup_tile_at(p, tilemap_pixel_to_col(p->pos_x + k_probe_vx[i]),
                                  tilemap_pixel_to_row(py));
        }
    }

    /* Probe 4 (centre): pickups + all non-pickup tile effects. */
    pickup_tile_at(p, col, row);
    UBYTE attr = tilemap_attr(&g_cur_map, col, row);

    /* -----------------------------------------------------------------
     * Non-pickup tile effects: applied at the centre probe position only.
     * ----------------------------------------------------------------- */
    switch (attr) {

    case TILE_DOOR:
        /* Door requires key; if we have one, open_door() scans and patches it.
         * Without a key, the tile blocks movement (tilemap_is_solid could include
         * doors too, but the original keeps them as attr 0x03, not 0x01/wall). */
        if (p->keys > 0) {
            open_door(p);
        }
        break;

    case TILE_EXIT:
        /* Exit passable only once unlocked or destruction has started
         * (Ref: tile_exit @ main.asm#L5191). */
        if (g_exit_unlocked || g_in_destruction_sequence) {
            level_trigger_end();
        }
        break;

    case TILE_FACEHUGGER_HATCH:
        /* Player stands on a facehugger hatch: spawn an alien at this tile,
         * play the hatch sample, then convert tile to floor so it only fires once.
         * Ref: tile_facehuggers_hatch @ main.asm#L5414. */
        alien_spawn_near(p->pos_x, p->pos_y);
        tilemap_replace_tile(&g_cur_map, col, row);
        break;

    case TILE_INTEX:
        /* INTEX terminal: activated when the player presses fire-2 (or SPACE).
         * Ref: tile_intex_terminal @ main.asm#L5537 — checks btst #7 player input. */
        {
            UWORD inp = player_get_input(p);
            if (inp & INPUT_FIRE2) {
                intex_run(p->port);
            }
        }
        break;

    case TILE_DESTRUCT_TRIGGER:
        /* Tile 0x15: start self-destruct sequence (Ref: main.asm#L5500). */
        if (!g_self_destruct_initiated) {
            level_start_destruction();
            tilemap_replace_tile(&g_cur_map, col, row);
        }
        break;

    case TILE_BOSS_TRIGGER:
        /* Tile 0x3D: trigger boss encounter (Ref: main.asm#L5632). */
        tilemap_replace_tile(&g_cur_map, col, row);
        break;

    /* -----------------------------------------------------------------
     * Persistent hazards: applied every frame while player is on tile.
     * ----------------------------------------------------------------- */
    case TILE_ACID_POOL:
        /* -1 HP every 25 frames (~0.5 s) while on tile
         * (Ref: tile_acid_pool @ main.asm#L5515). */
        g_acid_damage_counter[p->port]++;
        if (g_acid_damage_counter[p->port] >= 25) {
            player_take_damage(p, 1);
            audio_play_sample(SAMPLE_ACID_POOL);
            g_acid_damage_counter[p->port] = 0;
        }
        break;  /* do NOT reset acid counter in the default path below */

    case TILE_DEADLY_HOLE:
        /* Instant kill (Ref: tile_deadly_hole @ main.asm#L5485). */
        player_take_damage(p, p->health);
        break;

    /* -----------------------------------------------------------------
     * One-way conveyor tiles — push player in the named direction.
     * Setting extra_spd each frame prevents movement in the other dir.
     * Ref: tile_one_way_up/right/down/left @ main.asm#L5451-L5483.
     * ----------------------------------------------------------------- */
    case TILE_ONEWAY_UP:
        p->extra_spd_y = -2;
        audio_play_sample(SAMPLE_ONE_WAY_DOOR);
        break;
    case TILE_ONEWAY_RIGHT:
        p->extra_spd_x = 2;
        audio_play_sample(SAMPLE_ONE_WAY_DOOR);
        break;
    case TILE_ONEWAY_DOWN:
        p->extra_spd_y = 2;
        audio_play_sample(SAMPLE_ONE_WAY_DOOR);
        break;
    case TILE_ONEWAY_LEFT:
        p->extra_spd_x = -2;
        audio_play_sample(SAMPLE_ONE_WAY_DOOR);
        break;

    /* -----------------------------------------------------------------
     * Diagonal one-way tiles — push in two directions simultaneously.
     * Ref: tile_one_way_up_right/etc. @ main.asm#L5818-L5852.
     * ----------------------------------------------------------------- */
    case TILE_ONEWAY_DIAG_UR:
        p->extra_spd_x =  2;  p->extra_spd_y = -2;
        audio_play_sample(SAMPLE_ONE_WAY_DOOR);
        break;
    case TILE_ONEWAY_DIAG_DR:
        p->extra_spd_x =  2;  p->extra_spd_y =  2;
        audio_play_sample(SAMPLE_ONE_WAY_DOOR);
        break;
    case TILE_ONEWAY_DIAG_DL:
        p->extra_spd_x = -2;  p->extra_spd_y =  2;
        audio_play_sample(SAMPLE_ONE_WAY_DOOR);
        break;
    case TILE_ONEWAY_DIAG_UL:
        p->extra_spd_x = -2;  p->extra_spd_y = -2;
        audio_play_sample(SAMPLE_ONE_WAY_DOOR);
        break;

    /* -----------------------------------------------------------------
     * Deadly one-way tiles — push + kill if player moves against arrow.
     * 0x26: arrow points right; 0x2E: arrow points left.
     * Ref: tile_one_deadly_way_right/left @ main.asm#L5600-L5630.
     * ----------------------------------------------------------------- */
    case TILE_ONE_DEADLY_WAY_RIGHT:
        p->extra_spd_x = 2;
        /* Kill if player is moving left (against the arrow) */
        if (p->direction == PLAYER_FACE_LEFT ||
            p->direction == PLAYER_FACE_UP_LEFT ||
            p->direction == PLAYER_FACE_DOWN_LEFT) {
            player_take_damage(p, p->health);
        }
        audio_play_sample(SAMPLE_ONE_WAY_DOOR);
        break;
    case TILE_ONE_DEADLY_WAY_LEFT:
        p->extra_spd_x = -2;
        /* Kill if player is moving right (against the arrow) */
        if (p->direction == PLAYER_FACE_RIGHT ||
            p->direction == PLAYER_FACE_UP_RIGHT ||
            p->direction == PLAYER_FACE_DOWN_RIGHT) {
            player_take_damage(p, p->health);
        }
        audio_play_sample(SAMPLE_ONE_WAY_DOOR);
        break;

    /* -----------------------------------------------------------------
     * Climb tiles — apply a small counter-force to slow movement when
     * going against the slope.  A ±1 push each frame means the player
     * moves at half speed in the hard direction (2 forward - 1 back = 1).
     * Ref: tile_climb_left/right/up/down @ main.asm#L5798-L5816.
     * ----------------------------------------------------------------- */
    case TILE_CLIMB_LEFT:
        /* Slows leftward movement: push right (+1 x) */
        p->extra_spd_x = 1;
        break;
    case TILE_CLIMB_RIGHT:
        /* Slows rightward movement: push left (-1 x) */
        p->extra_spd_x = -1;
        break;
    case TILE_CLIMB_UP:
        /* Slows upward movement: push down (+1 y) */
        p->extra_spd_y = 1;
        break;
    case TILE_CLIMB_DOWN:
        /* Slows downward movement: push up (-1 y) */
        p->extra_spd_y = -1;
        break;

    default:
        /* Plain floor / metallic floor / unknown: reset acid counter.
         * Note: TILE_ACID_POOL is handled above with break, so this default
         * is never reached for acid tiles — the counter is preserved. */
        g_acid_damage_counter[p->port] = 0;
        break;
    }
}

void player_update(Player *p, UWORD input_mask)
{
    if (!p->alive) return;

    /* Death animation: player is shown as an explosion and cannot act.
     * Mirrors the 280(a0) countdown at lbC006E96 / lbC0077DC @ main.asm#L4044-L4779.
     * The counter is started by player_take_damage when health reaches 0. */
    if (p->death_counter > 0) {
        p->death_counter--;
        if (p->death_counter == 0) {
            if (p->lives > 0) {
                /* Respawn in place: restore health and grant invincibility.
                 * Ref: lbC00788E @ main.asm#L4774-L4778. */
                p->health = PLAYER_MAX_HEALTH;
                p->anim_fire_counter = 0;
                g_player_invincibility[p->port] = INVINCIBILITY_FRAMES;
            } else {
                p->alive = 0;
            }
        }
        return;
    }

    int dx = 0, dy = 0;
    int speed = 2;

    if (input_mask & INPUT_UP)    { dy -= speed; }
    if (input_mask & INPUT_DOWN)  { dy += speed; }
    if (input_mask & INPUT_LEFT)  { dx -= speed; }
    if (input_mask & INPUT_RIGHT) { dx += speed; }

    /* Update facing direction */
    if      (dx < 0 && dy < 0) p->direction = PLAYER_FACE_UP_LEFT;
    else if (dx > 0 && dy < 0) p->direction = PLAYER_FACE_UP_RIGHT;
    else if (dx < 0 && dy > 0) p->direction = PLAYER_FACE_DOWN_LEFT;
    else if (dx > 0 && dy > 0) p->direction = PLAYER_FACE_DOWN_RIGHT;
    else if (dx < 0)            p->direction = PLAYER_FACE_LEFT;
    else if (dx > 0)            p->direction = PLAYER_FACE_RIGHT;
    else if (dy < 0)            p->direction = PLAYER_FACE_UP;
    else if (dy > 0)            p->direction = PLAYER_FACE_DOWN;

    /* Apply movement (try X and Y separately to allow wall-sliding) */
    if (dx) try_move(p, dx, 0);
    if (dy) try_move(p, 0, dy);

    /* Apply extra velocity (knockback etc.) */
    if (p->extra_spd_x || p->extra_spd_y) {
        try_move(p, p->extra_spd_x, p->extra_spd_y);
        if (p->extra_spd_x > 0) p->extra_spd_x--;
        else if (p->extra_spd_x < 0) p->extra_spd_x++;
        if (p->extra_spd_y > 0) p->extra_spd_y--;
        else if (p->extra_spd_y < 0) p->extra_spd_y++;
    }

    /* Check tile interaction at current position */
    check_tile_interaction(p);

    /* Weapon fire */
    if (p->weapon_rate_counter > 0)
        p->weapon_rate_counter--;

    if ((input_mask & INPUT_FIRE1) && p->weapon_rate_counter == 0) {
        if (p->ammunitions > 0) {
            p->weapon_rate_counter = p->weapon_rate;
            p->shots++;
            audio_play_sample(p->weapon_smp);

            /* Decrement ammo every shot_amount shots, mirroring ASM:
             *   subq.w #1,PLAYER_SHOT_AMOUNT_COUNTER
             *   bpl.b  lbC00E178          ; still shots left in this ammo unit
             *   move.w PLAYER_SHOT_AMOUNT,PLAYER_SHOT_AMOUNT_COUNTER  ; reload counter
             *   subq.w #1,PLAYER_AMMUNITIONS                          ; consume 1 ammo
             * Ref: lbC00E14A @ main.asm#L9419. */
            if (p->shot_amount_counter <= 0) {
                p->shot_amount_counter = p->shot_amount - 1;
                p->ammunitions--;
                if (p->ammunitions <= 0 && p->ammopacks > 0) {
                    p->ammopacks--;
                    p->ammunitions = PLAYER_MAX_AMMO;
                }
            } else {
                p->shot_amount_counter--;
            }

            /* Compute projectile velocity from facing direction and weapon speed.
             *
             * Direction unit vectors from lbW00E9F6 @ main.asm#L9995:
             *   dc.w 0,0, 0,-1, 1,-1, 1,0, 1,1, 0,1, -1,1, -1,0, -1,-1, 0,0
             * Each pair (dx,dy) gives the raw unit vector for cur_sprite 0-8.
             * After mulu with speed: vx = dx*speed, vy = dy*speed (stored as WORD,
             * so -1*speed wraps correctly via MULU + MOVE.W lower 16 bits).
             * Ref: move.w 0(a2,d2.w),d4 / mulu d1,d4 @ main.asm#L9450-L9461.
             */
            static const int k_dir_x[9] = { 0, 0, 1, 1, 1, 0,-1,-1,-1 };
            static const int k_dir_y[9] = { 0,-1,-1, 0, 1, 1, 1, 0,-1 };
            int dir = (p->direction >= 1 && p->direction <= 8) ? p->direction : 5;
            int dx = k_dir_x[dir];
            int dy = k_dir_y[dir];
            int spd = p->weapon_speed;
            int vxi = dx * spd;
            int vyi = dy * spd;

            /*
             * Arc spread for FLAMEARC / PLASMAGUN / SIDEWINDERS.
             * Each consecutive fire alternates: straight → CW-offset → CCW-offset.
             * The global counter cycles 1→2→3→0 (reset after 3).
             * At counter=2: vx += mult*raw_dy, vy -= mult*raw_dx  (CW rotation offset)
             * At counter=3: vx -= mult*raw_dy, vy += mult*raw_dx  (CCW rotation offset), reset
             * Ref: lbL00EAA6 / cmp.w #3 / add.w d7,d4 / sub.w d6,d5
             *      @ main.asm#L9470-L9482.
             *
             * ASM uses raw unit-vector registers d6/d7 as spread offset.
             * FLAMEARC (weapon 3) branches to arc code BEFORE the add.w d6,d6/d7 doubling,
             *   so offset multiplier = 1 (d6=unit_x, d7=unit_y).
             * PLASMAGUN (4) and SIDEWINDERS (6) branch AFTER the doubling,
             *   so offset multiplier = 2 (d6=2*unit_x, d7=2*unit_y), giving wider spread.
             * Ref: cmp.w #3,PLAYER_WEAPON_INDEX / add.w d6,d6 / add.w d7,d7
             *      / cmp.w #4 / cmp.w #6 @ main.asm#L9462-L9468.
             */
            int wt = p->cur_weapon;
            if (wt == WEAPON_FLAMEARC || wt == WEAPON_PLASMAGUN ||
                wt == WEAPON_SIDEWINDERS) {
                int arc_mult = (wt == WEAPON_PLASMAGUN || wt == WEAPON_SIDEWINDERS) ? 2 : 1;
                s_arc_counter++;
                if (s_arc_counter == 1) {
                    /* straight — no change */
                } else if (s_arc_counter == 2) {
                    /* CW offset: add.w d7,d4 / sub.w d6,d5 @ main.asm#L9476. */
                    vxi += arc_mult * dy;
                    vyi -= arc_mult * dx;
                } else {
                    /* CCW offset: sub.w d7,d4 / add.w d6,d5 + reset @ main.asm#L9480-L9482. */
                    vxi -= arc_mult * dy;
                    vyi += arc_mult * dx;
                    s_arc_counter = 0;
                }
            }

            WORD vx = (WORD)vxi;
            WORD vy = (WORD)vyi;

            /*
             * Weapon-specific parameters:
             *   penetrating  : 1 for PLASMAGUN/FLAMETHROWER/LAZER (field4=01 in
             *                    weapons_attr_table @ main.asm#L736-L742).
             *                  Ref: tst.w 18(a1) @ main.asm#L7739.
             *   lifetime     : FLAMETHROWER = 8 ticks (lbL018D06 8-frame list,
             *                    delay=1 each @ main.asm#L13935); others = -1.
             *   bounce_count : FLAMEARC=1 (cmp.w #2,24(a3) @ main.asm#L9712),
             *                  LAZER=5 (cmp.w #6,24(a3) @ main.asm#L9716), others=0.
             */
            int penetrating  = (wt == WEAPON_PLASMAGUN ||
                                 wt == WEAPON_FLAMETHROWER ||
                                 wt == WEAPON_LAZER) ? 1 : 0;
            int lifetime     = (wt == WEAPON_FLAMETHROWER) ? FLAME_LIFETIME_TICKS : -1;
            int bounce_count = 0;
            if (wt == WEAPON_FLAMEARC)  bounce_count = 1;
            else if (wt == WEAPON_LAZER) bounce_count = 5;

            /*
             * Muzzle position: add per-direction offset to the player's
             * CENTER (pos_x, pos_y).
             * ASM lbC00E264 does:
             *   move.l 16(a0),a4       ; sprite pointer
             *   move.l -4(a4),d1       ; player CENTER packed (x|y)
             *                          ; (set by lbC006BE4: move.w PLAYER_POS_X,-4(a1))
             *   move.l 0(a2,d2.w),d2   ; muzzle offset (x_off|y_off)
             *   add.w  d2,d1 / swap / add.w / swap  → spawn = CENTER + offset
             * lbW00EA3E + 0  → weapons with PLAYER_WEAPON_INDEX >= 2 (TWINFIRE…LAZER)
             * lbW00EA3E + 32 → MACHINEGUN (weapon index 1)
             * Offsets indexed by direction 1-8; entry 0 is unused.
             */
            static const int k_muzzle_x[9] = { 0,  2, 12, 16, 12,  0, -12, -16, -12 };
            static const int k_muzzle_y[9] = { 0, -16, -12,  0, 12, 16,  12,  -2, -12 };
            int safe_dir = (dir >= 1 && dir <= 8) ? dir : 5;
            int spawn_x = p->pos_x + k_muzzle_x[safe_dir];
            int spawn_y = p->pos_y + k_muzzle_y[safe_dir];

            alien_spawn_projectile(p->port, (WORD)spawn_x, (WORD)spawn_y,
                                   vx, vy, p->weapon_strength,
                                   wt, penetrating, lifetime,
                                   bounce_count, dir);
        }
    }

    /* Next weapon */
    if (input_mask & INPUT_NEXT_WPN)
        player_next_weapon(p);

    /* Decrement invincibility */
    int idx = p->port;
    if (g_player_invincibility[idx] > 0)
        g_player_invincibility[idx]--;

    /* ---------------------------------------------------------------
     * Walk-cycle pose advance + animation state machine
     * Mirrors lbC006EF4..lbC006F64 @ main.asm#L4066-L4101
     * ---------------------------------------------------------------
     * lbB00A24F: for each movement direction (1-8) and current body
     * pose (cur_sprite 1-8), gives the next body pose.  The player
     * sprite smoothly rotates toward the new facing direction.
     * Index: direction*8 + cur_sprite.
     * ---------------------------------------------------------------- */
    /* Walk-cycle direction→next-pose table (ref: lbB00A24F @ main.asm#L7079).
     * Size = 9 (dir=0 idle) + 8 directions × 8 poses = 73 entries.
     * Index: direction*8 + cur_sprite (dir 0-8, spr 1-8). */
    static const int k_walk_table[73] = {
        /* dir=0 (idle), spr=0-8: no change */
        0,0,0,0,0,0,0,0,0,
        /* dir=1 (up), spr=1-8: converges to pose 1 */
        1,1,2,3,6,7,8,1,
        /* dir=2 (up-right), spr=1-8: converges to pose 2 */
        2,2,2,3,4,7,8,1,
        /* dir=3 (right), spr=1-8: converges to pose 3 */
        2,3,3,3,4,5,8,1,
        /* dir=4 (down-right), spr=1-8: converges to pose 4 */
        2,3,4,4,4,5,6,1,
        /* dir=5 (down), spr=1-8: converges to pose 5 */
        2,3,4,5,5,5,6,7,
        /* dir=6 (down-left), spr=1-8: converges to pose 6 */
        8,3,4,5,6,6,6,7,
        /* dir=7 (left), spr=1-8: converges to pose 7 */
        8,1,4,5,6,7,7,7,
        /* dir=8 (up-left), spr=1-8: converges to pose 8 */
        8,1,2,5,6,7,8,8
    };

    /* dir_code: 0 = not moving, 1-8 = current direction */
    int dir_code = (dx || dy) ? p->direction : 0;

    /* Advance body-pose every other frame while moving (flipflop gates it) */
    if (dir_code > 0) {
        p->anim_flipflop ^= 1;
        if (p->anim_flipflop) {
            int widx = dir_code * 8 + p->cur_sprite;
            if (widx > 0 && widx < 73 && k_walk_table[widx] != 0)
                p->cur_sprite = k_walk_table[widx];
        }
    }

    /* Decrement hit-anim counter (set to 5 by player_take_damage) */
    if (p->anim_fire_counter > 0)
        p->anim_fire_counter--;

    /* Compute animation state (ref: lbC006F18..lbC006F64 @ main.asm#L4078-L4101)
     *   0   : idle (dir_code=0, no fire)
     *   1-8 : walking in direction dir_code
     *   9-17: fire button held (same base as 0-8 + 9)
     *  18-26: hit/fire countdown active (same base + 18)
     *  27-35: invincible/respawn (same base + 27) */
    int fire_held = (input_mask & INPUT_FIRE1) != 0;
    int invincible = (g_player_invincibility[p->port] > 0);

    if (p->anim_fire_counter > 0) {
        /* Hit aura has highest visual priority — the 5-frame aura animation
         * plays immediately on impact, even while the per-hit invincibility
         * timer is still running.  (ref: tst.w 292(a0) @ main.asm#L4083:
         * the ASM checks the hit counter before the respawn/invincibility
         * flag, so the aura always shows first.) */
        p->anim_state = dir_code + 18;
    } else if (invincible) {
        p->anim_state = dir_code + 27;
    } else if (fire_held) {
        p->anim_state = dir_code + 9;
    } else {
        p->anim_state = dir_code;
    }

    /* Advance animation sequence frame/timer.
     * Sequence identity: seq_group (0-3) × 16 + cur_sprite.
     * When the identity changes the frame counter resets (mirrors lbC011346). */
    int seq_group = p->anim_state / 9; /* 0=idle/walk, 1=fire, 2=hit, 3=respawn */
    int new_seq_id = (seq_group << 4) | (p->cur_sprite & 0xF);
    if (new_seq_id != p->anim_seq_id) {
        p->anim_seq_id    = new_seq_id;
        p->anim_seq_frame = 0;
        /* Initial frame hold: walk=2, respawn=4, fire/hit=1 */
        if      (seq_group == 3) p->anim_seq_timer = 4;
        else if (seq_group == 0) p->anim_seq_timer = 2;
        else                     p->anim_seq_timer = 1;
    }

    if (p->anim_seq_timer > 0) {
        p->anim_seq_timer--;
    } else {
        /* cycle_len and next frame delay per state */
        int cycle_len, next_delay;
        if      (p->anim_state == 0)              { cycle_len = 1; next_delay = 2; } /* idle: static */
        else if (p->anim_state <= 8)              { cycle_len = 4; next_delay = 2; } /* walk: 4-frame, 2 ticks */
        else if (p->anim_state == 9)              { cycle_len = 2; next_delay = 1; } /* fire-idle: 2-frame, 1 tick */
        else if (p->anim_state <= 17)             { cycle_len = 4; next_delay = 1; } /* fire-walk: 4-frame, 1 tick */
        else if (p->anim_state <= 26)             { cycle_len = 4; next_delay = 1; } /* hit: 4-frame, 1 tick */
        else                                      { cycle_len = 2; next_delay = 4; } /* respawn: 2-frame, 4 ticks */

        p->anim_seq_frame = (p->anim_seq_frame + 1) % cycle_len;
        p->anim_seq_timer = next_delay;
    }
}

void player_take_damage(Player *p, int amount)
{
    int idx = p->port;
    if (g_player_invincibility[idx] > 0) return;
    if (p->death_counter > 0) return;  /* already in death animation */

    p->health -= (WORD)amount;
    audio_play_sample(SAMPLE_HURT_PLAYER);
    p->anim_fire_counter = 5; /* trigger 5-frame hit animation (ref: move.w #5,292(a1) @ main.asm#L7642) */

    if (p->health <= 0) {
        p->health = 0;
        p->lives--;
        audio_play_sample(SAMPLE_DYING_PLAYER);
        audio_play_sample(VOICE_DEATH);
        /* Start death explosion animation; movement/shooting suspended until
         * the counter expires.  Respawn (or alive=0 if out of lives) is handled
         * in player_update when the counter reaches 0.
         * Ref: move.w #200,lbW005D64 @ main.asm#L3938. */
        p->death_counter = PLAYER_DEATH_FRAMES;
    } else {
        g_player_invincibility[idx] = INVINCIBILITY_FRAMES / 2;
    }
}

int player_is_invincible(const Player *p)
{
    return g_player_invincibility[p->port] > 0;
}

void player_collect_supply(Player *p, int supply_flags)
{
    if (supply_flags & SUPPLY_AMMO_CHARGE) {
        /* ASM: move.w #PLAYER_MAX_AMMO,PLAYER_AMMUNITIONS(a0)
         *      addq.w #2,PLAYER_AMMOPACKS(a0)  — purchases give 2 packs */
        p->ammunitions = PLAYER_MAX_AMMO;
        p->ammopacks += 2;
        if (p->ammopacks > PLAYER_MAX_AMMOPCKS)
            p->ammopacks = PLAYER_MAX_AMMOPCKS;
        audio_play_sample(SAMPLE_AMMO);
    }
    if (supply_flags & SUPPLY_NRG_INJECT) {
        /* ASM: add.w #20,d3 — energy injection heals 20 HP */
        p->health += 20;
        if (p->health > PLAYER_MAX_HEALTH) p->health = PLAYER_MAX_HEALTH;
        audio_play_sample(SAMPLE_1STAID_CREDS);
    }
    if (supply_flags & SUPPLY_KEY_PACK) {
        /* ASM: addq.w #6,PLAYER_KEYS(a0) — key pack gives 6 keys */
        p->keys += 6;
        audio_play_sample(SAMPLE_KEY);
    }
    if (supply_flags & SUPPLY_EXTRA_LIFE) {
        p->lives++;
        audio_play_sample(SAMPLE_1UP);
    }
    if (supply_flags & SUPPLY_MAP_OVERVIEW) {
        /* Enable map overview — handled by level state */
        audio_play_sample(SAMPLE_1STAID_CREDS);
    }
}

void player_next_weapon(Player *p)
{
    int start = p->cur_weapon;
    int w     = p->cur_weapon;
    do {
        w++;
        if (w >= WEAPON_MAX) w = WEAPON_MACHINEGUN;
        if (p->owned_weapons[w - 1]) {
            player_set_cur_weapon(p, w);
            return;
        }
    } while (w != start);
}
