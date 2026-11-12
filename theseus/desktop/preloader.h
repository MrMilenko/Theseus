// preloader.h: desktop preloader public API. Companion to
// desktop/preloader.cpp.

#pragma once
#include <SDL.h>

// True when this run is development mode (scenes from unpacked folders).
bool RunPreloader(SDL_Window* window);

// Unpack the XIP archives for development mode if that hasn't happened yet.
bool Preloader_EnsureExtracted();

// Extract a single XIP to a directory. Returns number of files extracted.
int Preloader_ExtractXIP(const char* xipPath, const char* outDir);
