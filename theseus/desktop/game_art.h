// game_art.h: fetches box art for games that have no icon yet (game_art.cpp).
#pragma once

// What an importer knows that games.ini doesn't: the ES-DE system short name
// ("snes") and the rom path. Main thread only.
void GameArt_Hint(const char* titleID, const char* system, const char* romPath);

// A public cover picture a store lists for the game. Main thread only.
void GameArt_HintArt(const char* titleID, const char* url);

// Once a frame on the main thread. Notices library changes, queues whatever
// is missing and lets the grid pick up art as it lands.
void GameArt_Tick();

// Host path of the bigger cached art for a title, or NULL. kind is "cover"
// (portrait box/capsule) or "header" (wide banner).
const char* GameArt_CachedPath(const char* titleID, const char* kind);

extern bool g_fetchGameArt;        // desktop.ini FetchGameArt
extern char g_steamGridDbKey[128]; // desktop.ini SteamGridDBKey, the user's own
