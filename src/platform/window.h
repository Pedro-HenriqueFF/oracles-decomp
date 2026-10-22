#pragma once
#include <SDL3/SDL.h>
#include <stdbool.h>

// The game window of both apps: resizable, letterboxed and centred, the 160x144 image scaled by a
// whole number of real (not point) pixels with nearest sampling. size_path, when not NULL, keeps
// the window size between launches.
bool oracles_open_window(const char *title, const char *size_path, SDL_Window **win, SDL_Renderer **ren, SDL_Texture **tex);
// Turns vsync back on or off when the window moves to another display or the display's mode changes.
void oracles_window_event(const SDL_Event *ev);
// Paces a menu frame: the present already waited for vsync, else sleeps 16 ms.
void oracles_frame_wait(void);
// F11 or Cmd+F toggles fullscreen; returns true when the key was one of those.
bool oracles_window_key(SDL_Window *win, const SDL_KeyboardEvent *key);
void oracles_close_window(SDL_Window *win, const char *size_path);
