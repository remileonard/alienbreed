/*
 * Alien Breed SE 92 - C port
 * Player module
 */

#include "player.h"
#include "level.h"
#include "alien.h"
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

/* Resolve movement with wall collision.
 * Returns 1 if movement was possible, 0 if blocked. */
static int try_move(Player *p, int dx, int dy)
{
    int nx = p->pos_x + dx;
    int ny = p->pos_y + dy;

    /* Clamp to world bounds */
    if (nx < MAP_TILE_W) nx = MAP_TILE_W;
    if (ny < MAP_TILE_H) ny = MAP_TILE_H;
    if (nx >= (MAP_COLS - 1) * MAP_TILE_W) nx = (MAP_COLS - 1) * MAP_TILE_W - 1;
    if (ny >= (MAP_ROWS - 1) * MAP_TILE_H) ny = (MAP_ROWS - 1) * MAP_TILE_H - 1;

    /* Check corners of player bounding box (12×12, centred on pos).
     * Use half=5 so both corners stay within the same tile as the player
     * when the player is at a tile centre, avoiding false wall hits. */
    int half = 5;
    int col_l = tilemap_pixel_to_col(nx - half);
    int col_r = tilemap_pixel_to_col(nx + half);
    int row_t = tilemap_pixel_to_row(ny - half);
    int row_b = tilemap_pixel_to_row(ny + half);

    /* Check wall collisions */
    if (tilemap_is_solid(&g_cur_map, col_l, row_t) ||
        tilemap_is_solid(&g_cur_map, col_r, row_t) ||
        tilemap_is_solid(&g_cur_map, col_l, row_b) ||
        tilemap_is_solid(&g_cur_map, col_r, row_b)) {
        return 0;
    }

    /* Check one-way doors: block movement in opposite direction (Ref: main.asm#L5273).
     * One-way passage tiles allow entry only from certain directions. */
    if (dy < 0) {
        /* Moving up: can't pass through TILE_ONEWAY_DOWN */
        if (tilemap_attr(&g_cur_map, col_l, row_t) == TILE_ONEWAY_DOWN ||
            tilemap_attr(&g_cur_map, col_r, row_t) == TILE_ONEWAY_DOWN) return 0;
    } else if (dy > 0) {
        /* Moving down: can't pass through TILE_ONEWAY_UP */
        if (tilemap_attr(&g_cur_map, col_l, row_b) == TILE_ONEWAY_UP ||
            tilemap_attr(&g_cur_map, col_r, row_b) == TILE_ONEWAY_UP) return 0;
    }
    if (dx < 0) {
        /* Moving left: can't pass through TILE_ONEWAY_RIGHT */
        if (tilemap_attr(&g_cur_map, col_l, row_t) == TILE_ONEWAY_RIGHT ||
            tilemap_attr(&g_cur_map, col_l, row_b) == TILE_ONEWAY_RIGHT) return 0;
    } else if (dx > 0) {
        /* Moving right: can't pass through TILE_ONEWAY_LEFT */
        if (tilemap_attr(&g_cur_map, col_r, row_t) == TILE_ONEWAY_LEFT ||
            tilemap_attr(&g_cur_map, col_r, row_b) == TILE_ONEWAY_LEFT) return 0;
    }

    p->pos_x = (WORD)nx;
    p->pos_y = (WORD)ny;
    return 1;
}

/* Scan 4 adjacent tiles (up, down, left, right) for TILE_DOOR and patch them.
 * Mirrors open_door routine @ main.asm#L5242.
 * Each door tile found: erase attribute bits (& 0xFFC0) to make it floor,
 * decrement player.keys, increment level.doors_opened, play sample 23. */
void open_door(Player *p)
{
    if (p->keys <= 0) return;

    int col = tilemap_pixel_to_col(p->pos_x);
    int row = tilemap_pixel_to_row(p->pos_y);

    /* Check 4 adjacent tiles for door */
    int dirs[][2] = { {0, -1}, {0, 1}, {-1, 0}, {1, 0} };
    for (int i = 0; i < 4; i++) {
        int adj_col = col + dirs[i][0];
        int adj_row = row + dirs[i][1];
        if (adj_col >= 0 && adj_col < MAP_COLS && adj_row >= 0 && adj_row < MAP_ROWS) {
            UBYTE attr = tilemap_attr(&g_cur_map, adj_col, adj_row);
            if (attr == TILE_DOOR) {
                /* Patch door to floor */
                tilemap_replace_tile(&g_cur_map, adj_col, adj_row);
                /* Reduce keys, ensure positive key count on display */
                if (p->keys > 0) p->keys--;
                audio_play_sample(SAMPLE_OPENING_DOOR);
            }
        }
    }
}

/* Check and handle tile interaction at player's current position.
 * Called from player_update() after each movement.
 * Mirrors logic from tiles_action_table @ main.asm#L5059. */
void check_tile_interaction(Player *p)
{
    int col = tilemap_pixel_to_col(p->pos_x);
    int row = tilemap_pixel_to_row(p->pos_y);
    UBYTE attr = tilemap_attr(&g_cur_map, col, row);

    /* One-time consumption: pickups, supplies, triggers. After handling, tile is patched to floor. */
    switch (attr) {
    case TILE_KEY:
        p->keys++;
        if (p->keys > 6) p->keys = 6;  /* max 6 as in original (shows ">6" on HUD) */
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
        /* Door requires key; if we have one, open_door() scans and patches it */
        if (p->keys > 0) {
            open_door(p);
        }
        break;

    case TILE_EXIT:
        /* Exit available if unlocked or destruction sequence started (Ref: main.asm#L5191) */
        if (g_exit_unlocked || g_in_destruction_sequence) {
            level_trigger_end();
        }
        break;

    case TILE_INTEX:
        /* INTEX terminal: pause game, run terminal UI, return */
        /* TODO: call intex_run(p->port); */
        break;

    case TILE_DESTRUCT_TRIGGER:
        /* Tile 0x15: start self-destruct sequence (Ref: main.asm#L5500) */
        if (!g_self_destruct_initiated) {
            level_start_destruction();
            tilemap_replace_tile(&g_cur_map, col, row);
        }
        break;

    case TILE_ACID_POOL:
        /* -1 HP every 25 frames (~0.5 s) while on tile (Ref: main.asm#L5545)
         * Persistent damage, no consumption. */
        g_acid_damage_counter[p->port]++;
        if (g_acid_damage_counter[p->port] >= 25) {
            player_take_damage(p, 1);
            audio_play_sample(SAMPLE_ACID_POOL);
            g_acid_damage_counter[p->port] = 0;
        }
        break;

    case TILE_DEADLY_HOLE:
        /* Instant kill (Ref: main.asm#L5600) */
        player_take_damage(p, p->health);
        tilemap_replace_tile(&g_cur_map, col, row);
        break;

    case TILE_BOSS_TRIGGER:
        /* Tile 0x3D: trigger boss encounter (Ref: main.asm#L5632)
         * TODO: call boss_trigger(g_cur_level); set g_boss_active = 1; */
        tilemap_replace_tile(&g_cur_map, col, row);
        break;

    default:
        /* Not on special tile; reset acid counter */
        if (!TILE_IS_ONEWAY(attr)) {
            g_acid_damage_counter[p->port] = 0;
        }
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
