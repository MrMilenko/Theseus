#pragma once

// Read accessors for the media DB owned by desktop_nodes.cpp.
// XAP-side reads go through CMediaCollection; non-XAP consumers
// (playlist_maker, etc.) come in here.
//
// Returned strings are valid until the next scan / mutation.
// Wrap iteration in MediaDB_Lock/Unlock if a scan could be in flight.

#ifdef __cplusplus
extern "C" {
#endif

void MediaDB_Lock();
void MediaDB_Unlock();

// Read disk cache into the in-memory DB. Idempotent. Safe to call at boot
// before any XAP scene has instantiated CMediaCollection.
void MediaDB_LoadCache();

int         MediaDB_GetMovieCount();
const char* MediaDB_GetMovieTitleC(int i);
const char* MediaDB_GetMoviePathC(int i);
int         MediaDB_GetMovieYearC(int i);

int         MediaDB_GetShowCount();
const char* MediaDB_GetShowTitleC(int i);

int         MediaDB_GetSeasonCountC(int showIdx);
const char* MediaDB_GetSeasonNameC(int showIdx, int seasonIdx);

int         MediaDB_GetEpisodeCountC(int showIdx, int seasonIdx);
const char* MediaDB_GetEpisodeTitleC(int showIdx, int seasonIdx, int epIdx);
const char* MediaDB_GetEpisodePathC(int showIdx, int seasonIdx, int epIdx);
int         MediaDB_GetEpisodeSeasonNumC(int showIdx, int seasonIdx, int epIdx);
int         MediaDB_GetEpisodeNumberC(int showIdx, int seasonIdx, int epIdx);

#ifdef __cplusplus
}
#endif
