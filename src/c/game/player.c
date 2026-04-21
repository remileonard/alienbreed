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
#include "../engine/tilemap.h"
#include <string.h>
#include <stdio.h>

/* Update acid pool damage counter (persists across frames while on acid) */
static int g_acid_damage_counter[MAX_PLAYERS] = {0, 0};

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
        p->alive     = 1;
        p->health    = PLAYER_MAX_HEALTH;
        p->lives     = 3;
        p->credits   = 0;
        p->score     = 0;
        p->keys      = 0;
        p->ammopacks = 0;
        p->direction = PLAYER_FACE_DOWN;
        player_set_cur_weapon(p, WEAPON_MACHINEGUN);
        p->owned_weapons[WEAPON_MACHINEGUN - 1] = 1;
    }
}

void player_set_cur_weapon(Player *p, int weapon_id)
{
    if (weapon_id < WEAPON_MACHINEGUN || weapon_id >= WEAPON_MAX) return;
    p->cur_weapon = (WORD)weapon_id;

    /* Weapon parameters (speed, rate, strength) from original game data.
     * Values cross-referenced from main.asm weapon tables. */
    static const struct { WORD speed; WORD rate; WORD strength; WORD smp; } k_wdata[] = {
        { 0, 0, 0, 0 },               /* placeholder (index 0 unused) */
        { 6, 3,  2, SAMPLE_FIRE_GUN }, /* 1: MACHINEGUN  */
        { 7, 2,  3, SAMPLE_FIRE_GUN }, /* 2: TWINFIRE    */
        { 5, 4,  4, SAMPLE_FIRE_GUN }, /* 3: FLAMEARC    */
        { 8, 5,  5, SAMPLE_FIRE_GUN }, /* 4: PLASMAGUN   */
        { 4, 3,  6, SAMPLE_FIRE_GUN }, /* 5: FLAMETHROWER*/
        { 6, 4,  7, SAMPLE_FIRE_GUN }, /* 6: SIDEWINDERS */
        {10, 8, 10, SAMPLE_FIRE_GUN }, /* 7: LAZER       */
    };

    if (weapon_id < WEAPON_MAX) {
        p->weapon_speed    = k_wdata[weapon_id].speed;
        p->weapon_rate     = k_wdata[weapon_id].rate;
        p->weapon_strength = k_wdata[weapon_id].strength;
        p->weapon_smp      = k_wdata[weapon_id].smp;
    }
    p->weapon_rate_counter = 0;
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
 * Directional probe offsets translated from the ASM probe tables.
 *
 * The original game stores four probe tables in main.asm:
 *   lbW007B16 (LEFT):  x = -4,       y = {-6,  4, 16}
 *   lbW007B22 (RIGHT): x = +30,      y = {-6,  4, 16}
 *   lbW007B2E (UP):    x = {0,10,22}, y = -10
 *   lbW007B3A (DOWN):  x = {0,10,22}, y = +20
 *
 * The ASM origin is pos_x = col*16+4, pos_y = row*16+58 (with a +3-row
 * header in cur_map_datas that shifts all row lookups by 3).
 * The C port uses pos_x = col*16+8, pos_y = row*16+8 (tile centre).
 *
 * Conversion so that both reach the same map tile:
 *   c_x_offset = asm_x_offset - 4   (pos_x differs by +4)
 *   c_y_offset = asm_y_offset + 2   (pos_y differs by -50; +3-row header
 *                                    adds 48 px; net: +58-48-8 = +2)
 *
 * Resulting C-space offsets:
 *   LEFT  x : pos_x - 8              (ASM -4  → C -4-4  = -8)
 *   RIGHT x : pos_x + 26             (ASM +30 → C 30-4  = +26)
 *   UP    y : pos_y - 8              (ASM -10 → C -10+2 = -8)
 *   DOWN  y : pos_y + 22             (ASM +20 → C 20+2  = +22)
 *   H y pts : pos_y + {-4, +6, +18}  (ASM {-6,4,16} → C {-4,6,18})
 *   V x pts : pos_x + {-4, +6, +18}  (ASM {0,10,22} → C {-4,6,18})
 */

/* X offset of the single probe column for left/right movement */
#define PROBE_LEFT_X   (-8)
#define PROBE_RIGHT_X  (26)

/* Y offset of the single probe row for up/down movement */
#define PROBE_UP_Y     (-8)
#define PROBE_DOWN_Y   (22)

/* Three y-sample offsets used when probing left or right */
static const int k_probe_hy[3] = { -4, 6, 18 };
/* Three x-sample offsets used when probing up or down */
static const int k_probe_vx[3] = { -4, 6, 18 };

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

    /* Horizontal leading-edge check (right or left only) */
    if (dx > 0) {
        int px = nx + PROBE_RIGHT_X;
        for (int i = 0; i < 3; i++)
            if (tile_blocks(px, ny + k_probe_hy[i], has_key)) return 0;
    } else if (dx < 0) {
        int px = nx + PROBE_LEFT_X;
        for (int i = 0; i < 3; i++)
            if (tile_blocks(px, ny + k_probe_hy[i], has_key)) return 0;
    }

    /* Vertical leading-edge check (down or up only) */
    if (dy > 0) {
        int py = ny + PROBE_DOWN_Y;
        for (int i = 0; i < 3; i++)
            if (tile_blocks(nx + k_probe_vx[i], py, has_key)) return 0;
    } else if (dy < 0) {
        int py = ny + PROBE_UP_Y;
        for (int i = 0; i < 3; i++)
            if (tile_blocks(nx + k_probe_vx[i], py, has_key)) return 0;
    }

    p->pos_x = (WORD)nx;
    p->pos_y = (WORD)ny;
    return 1;
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

    /* Patch the current tile (the one the player is standing on). */
    tilemap_replace_tile(&g_cur_map, col, row);

    /* Also open the adjacent paired door tile, if any, without an extra key.
     * Doors come in pairs (horizontal or vertical).  Ref: the animation data
     * at lbL020CFE / lbL020D32 covers a 32-px-wide area over both tiles. */
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
void check_tile_interaction(Player *p)
{
    int col = tilemap_pixel_to_col(p->pos_x);
    int row = tilemap_pixel_to_row(p->pos_y);
    UBYTE attr = tilemap_attr(&g_cur_map, col, row);

    /* -----------------------------------------------------------------
     * One-time consumption: pickups and triggers.
     * After handling, tile is patched to floor so it can't fire again.
     * ----------------------------------------------------------------- */
    switch (attr) {

    case TILE_KEY:
        p->keys++;
        if (p->keys > 6) p->keys = 6;  /* display cap: shows ">6" */
        tilemap_replace_tile(&g_cur_map, col, row);
        audio_play_sample(SAMPLE_KEY);
        break;

    case TILE_FIRST_AID:
        p->health += 20;
        if (p->health > PLAYER_MAX_HEALTH) p->health = PLAYER_MAX_HEALTH;
        tilemap_replace_tile(&g_cur_map, col, row);
        audio_play_sample(SAMPLE_1STAID_CREDS);
        break;

    case TILE_AMMO:
        p->ammopacks++;
        if (p->ammopacks > PLAYER_MAX_AMMOPCKS) p->ammopacks = PLAYER_MAX_AMMOPCKS;
        p->ammunitions = PLAYER_MAX_AMMO;
        tilemap_replace_tile(&g_cur_map, col, row);
        audio_play_sample(SAMPLE_AMMO);
        break;

    case TILE_1UP:
        p->lives++;
        tilemap_replace_tile(&g_cur_map, col, row);
        audio_play_sample(SAMPLE_1UP);
        break;

    case TILE_CREDITS_100:
        p->credits += 5000;
        tilemap_replace_tile(&g_cur_map, col, row);
        audio_play_sample(SAMPLE_1STAID_CREDS);
        break;

    case TILE_CREDITS_1000:
        p->credits += 50000;
        tilemap_replace_tile(&g_cur_map, col, row);
        audio_play_sample(SAMPLE_1STAID_CREDS);
        break;

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
            p->ammunitions--;
            audio_play_sample(p->weapon_smp);

            /* Compute projectile velocity from facing direction and weapon speed.
             * ASM: PLAYER_WEAPON_SPEED = 16 (much higher than movement speed). */
            static const WORD k_vx[9] = { 0,  0,  6,  8,  6,  0, -6, -8, -6 };
            static const WORD k_vy[9] = { 0, -8, -6,  0,  6,  8,  6,  0, -6 };
            int dir = (p->direction >= 1 && p->direction <= 8) ? p->direction : 5;
            WORD vx = (WORD)((k_vx[dir] * p->weapon_speed) / 8);
            WORD vy = (WORD)((k_vy[dir] * p->weapon_speed) / 8);
            alien_spawn_projectile(p->port, p->pos_x, p->pos_y,
                                   vx, vy, p->weapon_strength);
        }
    }

    /* Next weapon */
    if (input_mask & INPUT_NEXT_WPN)
        player_next_weapon(p);

    /* Decrement invincibility */
    int idx = p->port;
    if (g_player_invincibility[idx] > 0)
        g_player_invincibility[idx]--;
}

void player_take_damage(Player *p, int amount)
{
    int idx = p->port;
    if (g_player_invincibility[idx] > 0) return;

    p->health -= (WORD)amount;
    audio_play_sample(SAMPLE_HURT_PLAYER);

    if (p->health <= 0) {
        p->health = 0;
        p->lives--;
        audio_play_sample(SAMPLE_DYING_PLAYER);
        audio_play_sample(VOICE_DEATH);
        if (p->lives <= 0) {
            p->alive = 0;
        } else {
            p->health = PLAYER_MAX_HEALTH;
            g_player_invincibility[idx] = INVINCIBILITY_FRAMES;
        }
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
        p->ammopacks++;
        if (p->ammopacks > PLAYER_MAX_AMMOPCKS)
            p->ammopacks = PLAYER_MAX_AMMOPCKS;
        p->ammunitions = PLAYER_MAX_AMMO;
        audio_play_sample(SAMPLE_AMMO);
    }
    if (supply_flags & SUPPLY_NRG_INJECT) {
        p->health += 16;
        if (p->health > PLAYER_MAX_HEALTH) p->health = PLAYER_MAX_HEALTH;
        audio_play_sample(SAMPLE_1STAID_CREDS);
    }
    if (supply_flags & SUPPLY_KEY_PACK) {
        p->keys++;
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
