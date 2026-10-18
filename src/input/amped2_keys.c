/*
 * amped2_keys.c -- Amped 2's own keys on top of the runtime's keyboard pad.
 *
 * The runtime's map (xboxrecomp/src/input/keyboard.c) is the same for every
 * title: arrows = d-pad, W/A/S/D = left stick, I/J/K/L = right stick,
 * Z/X/C/V = A/B/X/Y, Enter = Start, Backspace = Back. In Amped 2 the left
 * stick steers and spins while A is held to accelerate and released to jump
 * (the controls picture on the level loading screen, checked in xemu), so A
 * on Z puts both under the left hand. These keys add to that map; every
 * runtime key keeps working:
 *
 *   Space   A      accelerate / jump (left thumb, next to W/A/S/D)
 *   U       Y      lip trick         (right hand, above the grab keys I/J/K/L)
 *   O       B      jib / rail slide
 *   Escape  Start  pause
 *   Tab     Back   reset boarder
 *
 * Read by key position (SDL scancodes), so U and O sit in the same place on
 * QWERTZ and AZERTY. The runtime only turns letters, digits and a few named
 * keys into its key state, so Space, Escape and Tab come from SDL's own
 * keyboard state, which SDL keeps for the focused window and clears when the
 * window loses the focus -- like the runtime's keys. On Windows (GDI window)
 * the runtime's key state holds the raw Win32 virtual-key codes, so they are
 * read from there. Only reached while RECOMP_KEYBOARD is on (host_pad.c).
 */

#include "xinput_xbox.h"
#include "keyboard.h"
#if !defined(_WIN32)
#include <SDL.h>
#endif

static const struct {
    int scancode;   /* SDL_Scancode */
    int vk;         /* Win32 virtual-key code */
    int kind;       /* XBOX_KEY_BUTTON or XBOX_KEY_ANALOG */
    int target;     /* wButtons bit or bAnalogButtons index */
} s_keys[] = {
#if !defined(_WIN32)
#define SC(x) SDL_SCANCODE_##x
#else
#define SC(x) 0
#endif
    { SC(SPACE),  0x20, XBOX_KEY_ANALOG, XBOX_BUTTON_A },
    { SC(U),      'U',  XBOX_KEY_ANALOG, XBOX_BUTTON_Y },
    { SC(O),      'O',  XBOX_KEY_ANALOG, XBOX_BUTTON_B },
    { SC(ESCAPE), 0x1B, XBOX_KEY_BUTTON, XBOX_GAMEPAD_START },
    { SC(TAB),    0x09, XBOX_KEY_BUTTON, XBOX_GAMEPAD_BACK },
#undef SC
};

/* Called by host_pad.c with the keyboard pad's state; ORs the keys above in.
 * The runtime moves the packet number on every poll, so a press made here
 * is seen as an edge like any other key. */
void game_keys_merge(XBOX_INPUT_STATE *kb)
{
#if !defined(_WIN32)
    int numkeys = 0;
    const Uint8 *down = SDL_GetKeyboardState(&numkeys);
#endif
    size_t i;

    for (i = 0; i < sizeof s_keys / sizeof s_keys[0]; i++) {
#if !defined(_WIN32)
        int held = down && s_keys[i].scancode < numkeys && down[s_keys[i].scancode];
#else
        int held = xbox_FramebufferKeyDown(s_keys[i].vk);
#endif
        if (!held)
            continue;
        if (s_keys[i].kind == XBOX_KEY_BUTTON)
            kb->Gamepad.wButtons |= (WORD)s_keys[i].target;
        else
            kb->Gamepad.bAnalogButtons[s_keys[i].target] = 255;
    }
}
