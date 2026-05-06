// boot_anim.h: one-shot fullscreen boot animation playback. Call once at
// app startup before the dashboard initializes. Blocks until the video ends
// or the user presses Esc/Enter/Space.

#pragma once

struct SDL_Window;

// Plays the boot animation video at `path` fullscreen via libmpv. Blocks
// until END_FILE or a skip key. Returns true if anything was rendered;
// false if the video could not be opened (caller should just move on).
bool BootAnim_PlayAndWait(SDL_Window* win, const char* path);
