#pragma once
// What the screen calls each button: the menu hints, the Controls page's pad names and the status
// bar's letters follow the device last used, the controller style and the bindings. No SDL here: the
// app turns its gamepad type into a PadStyle and passes scancodes and pad buttons as ints.
#include "ui/controls.h"

typedef enum { STYLE_AUTO, STYLE_NINTENDO, STYLE_XBOX, STYLE_PLAYSTATION, STYLES } PadStyle;
typedef enum { DEVICE_KEYBOARD, DEVICE_PAD, DEVICE_TOUCH } InputDevice;

// Glyph codes for the status bar: 'A'-'Z' and '0'-'9' stand for themselves; the PlayStation shapes
// take codes below the printable range. 0 means "leave the game's own letter".
enum { GLYPH_CROSS = 1, GLYPH_CIRCLE, GLYPH_SQUARE, GLYPH_TRIANGLE };

// The four status bar slots, in screen order.
typedef enum { HUD_X, HUD_Y, HUD_B, HUD_A, HUD_SLOTS } HudSlot;

extern const char *const pad_style_keys[STYLES];     // "auto", "nintendo", "xbox", "playstation"
extern const char *const pad_style_labels[STYLES];   // "Auto", "Nintendo", "Xbox", "PlayStation"

// The style in effect: the setting, or for Auto the detected one (STYLE_AUTO when no pad has
// been seen, which is treated as Xbox).
PadStyle pad_style_resolve(PadStyle setting, PadStyle detected);
// The menu hint words for accept and back ("Enter"/"Esc", "A"/"B", "Cross"/"Circle"); menus
// navigate with fixed buttons (pad SOUTH and EAST), not the bindings.
void menu_hint_keys(InputDevice d, PadStyle resolved, const char **ok, const char **back);
// The Controls page's name for a pad button in the style, or NULL to keep SDL's own name
// (Nintendo, and buttons the style names the same way).
const char *pad_style_name(PadStyle resolved, int button);
// The glyph for one status bar slot: the key or pad button bound to that action, falling back to
// the Game Boy letter (X, Y, B, A) where there is no glyph for it.
uint8_t hud_slot_glyph(InputDevice d, PadStyle resolved, const Bindings *b, HudSlot slot);
