// tmdb.h: in-process TMDB API client. Backed by libcurl (UIX_HTTP);
// stubs out cleanly when libcurl isn't available at build time.
//
// Result records are returned by value — empty fields mean "no data" or
// "key not configured." Caller never blocks on network: lookups can be
// kicked off async via TMDB_QueueMovieLookup() / TMDB_QueueShowLookup()
// and polled via TMDB_GetMovie() / TMDB_GetShow().
//
// Disk cache lives at xboxfs/E/TMDB/{movie,show}_<slug>.json so we don't
// hit the API every dashboard launch.

#pragma once

#include <string>

struct TmdbMovie
{
    bool        ready;       // true once fetch resolved (cache or net)
    bool        found;       // true if TMDB had a hit
    std::string title;       // canonical title from TMDB
    std::string overview;    // plot synopsis
    int         year;        // release year (0 = unknown)
    int         runtime;     // minutes (0 = unknown; only set if details fetched)
    std::string posterPath;  // TMDB CDN relative path (e.g. "/abc.jpg")
    float       voteAverage; // 0..10
    int         tmdbId;      // canonical TMDB id
};

struct TmdbShow
{
    bool        ready;
    bool        found;
    std::string title;
    std::string overview;
    int         year;        // first air year
    std::string posterPath;
    float       voteAverage;
    int         tmdbId;
};


// One-time setup. Reads g_tmdbKey from desktop.ini. Idempotent.
void TMDB_Init();

// Drop in-flight requests + free libcurl handles. Call on shutdown.
void TMDB_Shutdown();

// API key configured?
bool TMDB_HasKey();

// Sync lookup (cache hit returns instantly; cache miss does a network
// fetch on the calling thread). Use TMDB_QueueX + TMDB_GetX for async.
TmdbMovie TMDB_LookupMovie(const char* title, int year);
TmdbShow  TMDB_LookupShow(const char* title);

// Async: kick off background fetch if not already cached/in-flight.
void TMDB_QueueMovieLookup(const char* title, int year);
void TMDB_QueueShowLookup(const char* title);

// Async result polling: returns ready=false if still pending.
TmdbMovie TMDB_GetMovie(const char* title, int year);
TmdbShow  TMDB_GetShow(const char* title);
