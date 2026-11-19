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

// Watch state and the facts the info card shows, common to items, seasons
// and episodes.
struct PlexDetails {
    int         durationMs = 0;
    int         offsetMs = 0;      // resume point, 0 when not started
    int         viewCount = 0;
    int         leafCount = 0;     // episodes, for a show or season
    int         viewedLeafCount = 0;
    std::string rating;            // contentRating, "PG-13"
    float       stars = 0.0f;      // out of 10
    std::string genres;            // "Horror, Thriller"

    bool Played() const { return leafCount > 0 ? viewedLeafCount >= leafCount : viewCount > 0; }
    int  Unplayed() const { return leafCount > viewedLeafCount ? leafCount - viewedLeafCount : 0; }
};

struct PlexItem : PlexDetails {
    std::string ratingKey;     // numeric id, identifies the item
    std::string title;
    std::string year;          // optional, as string
    std::string thumbUrl;      // absolute URL (with X-Plex-Token appended)
    std::string artUrl;        // absolute URL (hero/fanart)
    std::string summary;       // plot synopsis
    std::string type;          // "movie" | "show" | "track" | "artist" | ...
    std::string showTitle;     // episodes on the On Deck shelf
    std::string seasonIndex;
    std::string episodeIndex;
};

struct PlexSeason : PlexDetails {
    std::string ratingKey;
    std::string title;         // "Season 1"
    std::string index;         // season number as string
    std::string thumbUrl;
};

struct PlexEpisode : PlexDetails {
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
// Address of the server the sync actually reached. Empty until one answers.
std::string                Plex_GetActiveServerUri();

// TV drill. Results land in the pre-stage cache (Plex_Cache_GetSeasons/Episodes).
void                       Plex_FetchSeasons(const std::string& serverUri,
                                             const std::string& showRatingKey);
void                       Plex_FetchEpisodes(const std::string& serverUri,
                                              const std::string& seasonRatingKey);

// Sync direct-play URL resolver. Empty on failure.
std::string Plex_ResolveStreamUrl(const std::string& serverUri,
                                  const std::string& ratingKey);

// Pre-stage cache. Walks servers + libraries + items on sign-in.
// Seasons + episodes stay lazy with permanent in-session caching.
void        Plex_StartSync();
bool        Plex_SyncReady();
// Seasons, episodes or children for this key failed to load. A failed key
// isn't fetched again until RetryFetch.
bool        Plex_FetchFailed(const std::string& ratingKey);
void        Plex_RetryFetch(const std::string& ratingKey);
std::string Plex_SyncPhase();
int         Plex_SyncProgress();  // 0..1000 per-mille

std::vector<PlexLibrary> Plex_Cache_GetLibraries();
std::vector<PlexItem>    Plex_Cache_GetItems(const std::string& sectionKey);
bool        Plex_Cache_GetSeasons(const std::string& showRatingKey,
                                  std::vector<PlexSeason>& out);
bool        Plex_Cache_GetEpisodes(const std::string& seasonRatingKey,
                                   std::vector<PlexEpisode>& out);
// What's inside an artist, album or collection. Fetched in the background
// on first ask: false until it lands.
bool        Plex_Cache_GetChildren(const std::string& ratingKey,
                                   std::vector<PlexItem>& out);

// Cover art. Cache path is Xbox-style (E:\Plex\<rk>.jpg); xboxfs routes
// it to Library/Plex/ for the texture loader.
std::string Plex_ArtCachePath(const std::string& ratingKey);
void        Plex_QueueArtDownload(const std::string& ratingKey,
                                  const std::string& url);
// The poster is on disk. Only hand the scene a path once this is true: the
// texture cache remembers a miss for the rest of the session.
bool        Plex_ArtReady(const std::string& ratingKey);

// Playback reports (MEDIA_REPORT_* from media_player.h) through /:/timeline,
// with a scrobble once it's played past 90%. Sent in order, off the main
// thread; a stop also reloads the item and the On Deck shelf.
void Plex_ReportPlayback(const std::string& ratingKey, int event,
                         int timeMs, int durationMs, bool paused);
// Bumped whenever watch state in the cache changes, so the scene redraws.
unsigned Plex_WatchStamp();
// Scrobbles or unscrobbles it on the server; the cache shows it straight away.
void Plex_SetPlayed(const std::string& ratingKey, bool played);
