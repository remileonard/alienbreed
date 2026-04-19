/*
 * Alien Breed SE 92 - C port
 * Input HAL implementation
 *
 * Keyboard mapping (Player 1):
 *   Arrow keys / WASD = directions
 *   Left Ctrl or Z    = fire 1
 *   Left Alt  or X    = fire 2 / next weapon
 *   Enter             = pause
 *   P                 = pause (matches Amiga KEY_P = $19)
 *   M                 = map overview
 *   ESC               = game over / quit menu item
 *
 * Gamepad (Player 1, port 0 / Player 2, port 1):
 *   D-pad             = directions
 *   Button A / South  = fire 1
 *   Button B / East   = fire 2
 *   Button X / West   = next weapon
 *   Start             = pause
 */

#include "input.h"
#include "../game/constants.h"
#include <string.h>

UWORD g_player1_input     = 0;
UWORD g_player2_input     = 0;
UWORD g_player1_old_input = 0;
UWORD g_player2_old_input = 0;
UBYTE g_key_pressed       = 0;
int   g_quit_requested    = 0;
int   g_input_enabled     = 1;

/* SDL_GameController handles for up to 2 controllers */
static SDL_GameController *s_ctrl[2] = { NULL, NULL };

int input_init(void)
{
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) < 0)
        return -1;

    /* Open available controllers */
    int num = SDL_NumJoysticks();
    for (int i = 0; i < num && i < 2; i++) {
        if (SDL_IsGameController(i))
            s_ctrl[i] = SDL_GameControllerOpen(i);
    }
    return 0;
}

void input_quit(void)
{
    for (int i = 0; i < 2; i++) {
        if (s_ctrl[i]) { SDL_GameControllerClose(s_ctrl[i]); s_ctrl[i] = NULL; }
    }
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS);
}

/* Build input bitmask from keyboard state for player 1 */
static UWORD build_keyboard_input(void)
{
    const Uint8 *ks = SDL_GetKeyboardState(NULL);
    UWORD mask = 0;

    if (ks[SDL_SCANCODE_UP]    || ks[SDL_SCANCODE_W]) mask |= INPUT_UP;
    if (ks[SDL_SCANCODE_DOWN]  || ks[SDL_SCANCODE_S]) mask |= INPUT_DOWN;
    if (ks[SDL_SCANCODE_LEFT]  || ks[SDL_SCANCODE_A]) mask |= INPUT_LEFT;
    if (ks[SDL_SCANCODE_RIGHT] || ks[SDL_SCANCODE_D]) mask |= INPUT_RIGHT;
    if (ks[SDL_SCANCODE_LCTRL] || ks[SDL_SCANCODE_Z]) mask |= INPUT_FIRE1;
    if (ks[SDL_SCANCODE_LALT]  || ks[SDL_SCANCODE_X]) mask |= INPUT_FIRE2;
    if (ks[SDL_SCANCODE_RETURN]|| ks[SDL_SCANCODE_P]) mask |= INPUT_PAUSE;
    if (ks[SDL_SCANCODE_RALT]  || ks[SDL_SCANCODE_Q]) mask |= INPUT_NEXT_WPN;

    return mask;
}

/* Build input bitmask from a gamepad */
static UWORD build_gamepad_input(int idx)
{
    if (!s_ctrl[idx]) return 0;
    SDL_GameController *c = s_ctrl[idx];
    UWORD mask = 0;

    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_UP))    mask |= INPUT_UP;
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_DOWN))  mask |= INPUT_DOWN;
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_LEFT))  mask |= INPUT_LEFT;
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) mask |= INPUT_RIGHT;
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_A))          mask |= INPUT_FIRE1;
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_B))          mask |= INPUT_FIRE2;
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_X))          mask |= INPUT_NEXT_WPN;
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_START))      mask |= INPUT_PAUSE;

    return mask;
}

/* Map SDL scancode to Amiga raw keycode for g_key_pressed */
static UBYTE sdl_to_amiga_key(SDL_Keycode sym)
{
    switch (sym) {
        case SDLK_p:       return KEY_P;
        case SDLK_m:       return KEY_M;
        case SDLK_SPACE:   return KEY_SPACE;
        case SDLK_RETURN:  return KEY_RETURN;
        case SDLK_ESCAPE:  return KEY_ESC;
        case SDLK_UP:      return KEY_UP;
        case SDLK_DOWN:    return KEY_DOWN;
        case SDLK_RIGHT:   return KEY_RIGHT;
        case SDLK_LEFT:    return KEY_LEFT;
        case SDLK_LALT:    return KEY_LEFT_ALT;
        case SDLK_RALT:    return KEY_RIGHT_ALT;
        default:           return 0;
    }
}

void input_poll(void)
{
    /* Save previous state */
    g_player1_old_input = g_player1_input;
    g_player2_old_input = g_player2_input;
    g_key_pressed = 0;

    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
            case SDL_QUIT:
                g_quit_requested = 1;
                break;
            case SDL_KEYDOWN:
                g_key_pressed = sdl_to_amiga_key(ev.key.keysym.sym);
                /* Controller plug/unplug handled below */
                break;
            case SDL_CONTROLLERDEVICEADDED:
                for (int i = 0; i < 2; i++) {
                    if (!s_ctrl[i]) {
                        s_ctrl[i] = SDL_GameControllerOpen(ev.cdevice.which);
                        break;
                    }
                }
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                for (int i = 0; i < 2; i++) {
                    if (s_ctrl[i] &&
                        SDL_GameControllerGetJoystick(s_ctrl[i]) ==
                        SDL_JoystickFromInstanceID(ev.cdevice.which)) {
                        SDL_GameControllerClose(s_ctrl[i]);
                        s_ctrl[i] = NULL;
                    }
                }
                break;
            default:
                break;
        }
    }

    if (!g_input_enabled) {
        g_player1_input = 0;
        g_player2_input = 0;
        return;
    }

    /* Player 1: keyboard OR gamepad 0 */
    g_player1_input = build_keyboard_input() | build_gamepad_input(0);
    /* Player 2: gamepad 1 only */
    g_player2_input = build_gamepad_input(1);
}
