// plex_client.h: Plex Media Server client. PIN auth + cache-backed browse.

#pragma once

#include <string>
#include <vector>

struct PlexServer {
    std::string name;          // "Milenko's Plex"
    std::string clientId;      // server-side machineIdentifier
    std::string uri;           // best connection URI (prefer local)
};

struct PlexLibrary {
    std::string sectionKey;    // numeric id as string
    std::string title;         // "Movies", "TV", "Music"
    std::string type;          // "movie" | "show" | "artist" | "photo"
};

struct PlexItem {
    std::string ratingKey;     // numeric id, identifies the item
    std::string title;
    std::string year;          // optional, as string
    std::string thumbUrl;      // absolute URL (with X-Plex-Token appended)
    std::string artUrl;        // absolute URL (hero/fanart)
    std::string summary;       // plot synopsis
    std::string type;          // "movie" | "show" | "track" | "artist" | ...
};

struct PlexSeason {
    std::string ratingKey;
    std::string title;         // "Season 1"
    std::string index;         // season number as string
    std::string thumbUrl;
};

struct PlexEpisode {
    std::string ratingKey;
    std::string title;
    std::string index;         // episode number as string
    std::string summary;
    std::string thumbUrl;
};


// Lifecycle
void Plex_Init();
void Plex_Shutdown();
bool Plex_HasToken();
void Plex_SignOut();

// PIN auth (plex.tv/link 4-letter code flow)
void Plex_StartPinAuth();
void Plex_CancelPinAuth();
bool Plex_PinAuthInFlight();
std::string Plex_GetPinCode();

// Async fetchers + poll-ready flags
void                       Plex_FetchServers();
std::vector<PlexServer>    Plex_GetServers();
bool                       Plex_ServersReady();
std::string                Plex_GetActiveServerName();

void                       Plex_FetchLibraries(const std::string& serverUri);
std::vector<PlexLibrary>   Plex_GetLibraries();
bool                       Plex_LibrariesReady();

void                       Plex_FetchItems(const std::string& serverUri,
                                           const std::string& sectionKey);
std::vector<PlexItem>      Plex_GetItems();
bool                       Plex_ItemsReady();

void                       Plex_FetchSeasons(const std::string& serverUri,
                                             const std::string& showRatingKey);
std::vector<PlexSeason>    Plex_GetSeasons();
bool                       Plex_SeasonsReady();

void                       Plex_FetchEpisodes(const std::string& serverUri,
                                              const std::string& seasonRatingKey);
std::vector<PlexEpisode>   Plex_GetEpisodes();
bool                       Plex_EpisodesReady();

// Sync direct-play URL resolver. Empty on failure.
std::string Plex_ResolveStreamUrl(const std::string& serverUri,
                                  const std::string& ratingKey);

// Pre-stage cache. Walks servers + libraries + items on sign-in.
// Seasons + episodes stay lazy with permanent in-session caching.
void        Plex_StartSync();
bool        Plex_SyncReady();
std::string Plex_SyncPhase();
int         Plex_SyncProgress();  // 0..1000 per-mille

std::vector<PlexLibrary> Plex_Cache_GetLibraries();
std::vector<PlexItem>    Plex_Cache_GetItems(const std::string& sectionKey);
bool        Plex_Cache_GetSeasons(const std::string& showRatingKey,
                                  std::vector<PlexSeason>& out);
bool        Plex_Cache_GetEpisodes(const std::string& seasonRatingKey,
                                   std::vector<PlexEpisode>& out);

// Cover art. Cache path is Xbox-style (E:\Plex\<rk>.jpg); xboxfs routes
// it to Library/Plex/ for the texture loader.
std::string Plex_ArtCachePath(const std::string& ratingKey);
void        Plex_QueueArtDownload(const std::string& ratingKey,
                                  const std::string& url);
