// jellyfin_client.h: Jellyfin Media Server client. Quick Connect auth + browse.

#pragma once

#include <string>
#include <vector>

struct JellyfinLibrary {
    std::string id;            // CollectionFolder ItemId
    std::string name;          // "Movies", "TV Shows"
    std::string type;          // "movies" | "tvshows" | "music" | ...
};

struct JellyfinItem {
    std::string id;
    std::string name;
    std::string year;          // optional, as string
    std::string overview;      // plot
    std::string type;          // "Movie" | "Series" | "Audio" | ...
    std::string primaryTag;    // image tag for Primary image URL
};

struct JellyfinSeason {
    std::string id;
    std::string name;          // "Season 1"
    std::string index;         // season number as string
    std::string primaryTag;
};

struct JellyfinEpisode {
    std::string id;
    std::string name;
    std::string index;         // episode number as string
    std::string overview;
    std::string primaryTag;
};

// Lifecycle
void Jellyfin_Init();
void Jellyfin_Shutdown();
bool Jellyfin_HasToken();
void Jellyfin_SignOut();

// Quick Connect: user enters a 6-letter code on the server's web UI.
// Start spawns a poller; UI shows GetCode each frame, HasToken flips true
// once the user approves on the server side.
void        Jellyfin_StartQuickConnect();
void        Jellyfin_CancelQuickConnect();
bool        Jellyfin_QuickConnectInFlight();
std::string Jellyfin_GetQuickConnectCode();

// Active server URL (whatever the user set in the Settings tab).
std::string Jellyfin_GetServerUrl();
void        Jellyfin_SetServerUrl(const std::string& url);

// User-friendly auth state for the Settings tab.
std::string Jellyfin_GetUserName();   // empty until token acquired

// Pre-stage cache. Walks libraries + items on sign-in; seasons + episodes
// stay lazy with permanent in-session caching (same shape as Plex).
void        Jellyfin_StartSync();
bool        Jellyfin_SyncReady();
std::string Jellyfin_SyncPhase();
int         Jellyfin_SyncProgress();  // 0..1000 per-mille

std::vector<JellyfinLibrary> Jellyfin_Cache_GetLibraries();
std::vector<JellyfinItem>    Jellyfin_Cache_GetItems(const std::string& libraryId);
bool Jellyfin_Cache_GetSeasons (const std::string& showId,    std::vector<JellyfinSeason>&  out);
bool Jellyfin_Cache_GetEpisodes(const std::string& seasonId,  std::vector<JellyfinEpisode>& out);

// Direct-play URL with api_key. Empty on failure.
std::string Jellyfin_StreamUrl(const std::string& itemId);

// Cover art. Same E:\Jellyfin\<id>.jpg trick as Plex.
std::string Jellyfin_ArtCachePath(const std::string& itemId);
void        Jellyfin_QueueArtDownload(const std::string& itemId,
                                      const std::string& primaryTag);
