// stores.h: installed games from the PC stores, read off the files each
// store leaves on disk. No logins, no accounts. The guide's Games & Playlists
// page shows an Import row for each store that's here.

#pragma once

#include <map>
#include <string>
#include <vector>

struct StoreGame {
	std::string name;       // as the store spells it, UTF-8
	std::string launch;     // the games.ini Launch line
	std::string workdir;    // empty lets the launcher work it out
	std::string icon;       // picture, .ico or .exe on disk to take the icon from
	std::string artUrl;     // public cover art to fetch when there's no icon
	std::string category;   // empty means Games
};

// Into the library, skipping anything already there by name or launch line.
// Saves when something was added. store_import.cpp.
void StoreGames_Add(const std::vector<StoreGame>& games, int* added, int* had);

// Epic Games Store: the launcher's manifests, and Heroic's Epic installs.
bool Epic_Found();
void Epic_Scan(std::vector<StoreGame>& out);

// GOG: Galaxy and offline installs from the Windows registry, and Heroic's
// GOG installs.
bool Gog_Found();
void Gog_Scan(std::vector<StoreGame>& out);

// Shared by the readers (stores.cpp).
std::string Store_Home();
std::string Store_Join(const std::string& dir, const std::string& name);
bool Store_IsDir(const std::string& path);
bool Store_IsFile(const std::string& path);
bool Store_ReadFile(const std::string& path, std::string& out);
void Store_ListDir(const std::string& dir, std::vector<std::string>& names);
// JSON keys of the object itself, never one nested inside it. Store_JsonKey
// is the offset of the key's opening quote, or npos.
size_t Store_JsonKey(const std::string& obj, const char* key);
std::string Store_JsonString(const std::string& obj, const char* key);
bool Store_JsonBool(const std::string& obj, const char* key);

// Heroic's config folders that exist: native and Flatpak on Linux, macOS,
// Windows.
void Store_HeroicDirs(std::vector<std::string>& out);

// What Heroic's store cache knows about a game it lists.
struct HeroicInfo { std::string title, art; };
// file is under store_cache, like "legendary_library.json". Keyed by app_name.
void Store_HeroicLibrary(const std::string& heroicDir, const char* file,
                         std::map<std::string, HeroicInfo>& out);
