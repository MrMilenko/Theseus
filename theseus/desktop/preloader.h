// preloader.h: desktop preloader public API. Companion to
// desktop/preloader.cpp.

#pragma once
#include <SDL.h>

// Run the preloader UI. Returns true if user chose extracted mode.
bool RunPreloader(SDL_Window* window);

// Extract a single XIP to a directory. Returns number of files extracted.
int Preloader_ExtractXIP(const char* xipPath, const char* outDir);
