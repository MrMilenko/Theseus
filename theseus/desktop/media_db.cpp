// media_db.cpp: filesystem-backed media library (movies + TV shows). Scans
// the configured roots, caches to Library/MediaDB.cache, enriches via TMDB,
// and exposes CMediaCollection / CPlaylistCollection to XAP. Split out of
// desktop_nodes.cpp.

#include "app_paths.h"
#include "std.h"
#include "dashapp.h"
#include "node.h"
#include "runner.h"
#include "media_player.h"
#include "tmdb.h"
#include "playlist.h"
#include "media_db.h"

// ============================================================================
// CMediaCollection - filesystem-backed library of movies + TV shows.
// Scans configured roots, caches results to Library/MediaDB.cache, exposes
// a stateful query API to XAP (SetCurrentShow / GetSeasonName / etc.).
// ============================================================================

#include <dirent.h>
#include <sys/stat.h>
#include <vector>
#include <string>
#include <map>
#include <thread>
#include <atomic>
#include <mutex>

#define MEDIADB_MAGIC   "TMDB"
#define MEDIADB_VERSION 2u   // v2: + overview, voteAverage, tmdbId on movies/shows

// Library roots come from desktop.ini globals (loaded by sdl_main.cpp).
// Empty by default; user configures via Settings -> Media Library tab.
// The scan no-ops on empty paths. no library shown until configured.
#define kCachePath AppPath("Library/MediaDB.cache")

extern char g_moviesRoot[512];
extern char g_tvRoot[512];

#define kMoviesRoot ((const char*)g_moviesRoot)
#define kTvRoot     ((const char*)g_tvRoot)

struct MovieRec
{
	std::string title;       // folder name as-is, e.g. "1408 (2007)"
	std::string path;        // absolute path to video file
	int         year;        // 0 if not parsed
	// TMDB enrichment (filled by MediaDB_EnrichTmdb during scan; cached in v2+)
	std::string overview;    // plot synopsis ("" if not looked up)
	float       voteAverage; // 0..10 ("" if not looked up)
	int         tmdbId;      // 0 = not attempted, -1 = no match, >0 = TMDB id
};

struct EpisodeRec
{
	std::string title;   // parsed episode title, e.g. "Asteroid Blues"
	std::string path;    // absolute path to video file
	int         season;
	int         episode;
};

struct SeasonRec
{
	std::string             name;     // "Season 1"
	std::vector<EpisodeRec> episodes;
};

struct ShowRec
{
	std::string            title;
	std::string            path;
	std::vector<SeasonRec> seasons;
	// TMDB enrichment
	std::string            overview;
	float                  voteAverage;
	int                    tmdbId;
};

static std::vector<MovieRec> g_movies;
static std::vector<ShowRec>  g_shows;
static std::mutex            g_dbMutex;
static std::atomic<bool>     g_scanRunning{false};
static std::atomic<int>      g_scanProgress{0};
static std::atomic<int>      g_scanTotal{0};
static std::mutex            g_phaseMutex;
static std::string           g_scanPhase;

static void SetPhase(const char* phase, int total)
{
	std::lock_guard<std::mutex> lock(g_phaseMutex);
	g_scanPhase  = phase;
	g_scanProgress = 0;
	g_scanTotal    = total;
}

static void BumpProgress(int n)
{
	g_scanProgress = n;
}


// Filename / path utilities --------------------------------------------------

static bool IsVideoExt(const char* name)
{
	const char* dot = strrchr(name, '.');
	if (!dot) return false;
	dot++;
	return _stricmp(dot, "mkv") == 0 || _stricmp(dot, "mp4") == 0 ||
	       _stricmp(dot, "avi") == 0 || _stricmp(dot, "m4v") == 0 ||
	       _stricmp(dot, "mov") == 0 || _stricmp(dot, "webm") == 0;
}

static int ParseYearFromTitle(const std::string& s)
{
	// "Title (1999)" -> 1999. Find LAST '(' followed by 4 digits + ')'.
	size_t p = s.rfind('(');
	if (p == std::string::npos || p + 5 >= s.size()) return 0;
	if (s[p + 5] != ')') return 0;
	int y = 0;
	for (int i = 1; i <= 4; i++)
	{
		char c = s[p + i];
		if (c < '0' || c > '9') return 0;
		y = y * 10 + (c - '0');
	}
	return y;
}

static std::string ParseEpisodeTitle(const std::string& filename, int& seasonOut, int& episodeOut)
{
	// "Show - S01E02 - Title WEBDL-1080p.mkv" -> "Title", season=1, episode=2.
	seasonOut = 0; episodeOut = 0;
	size_t s = filename.find(" - S");
	if (s == std::string::npos) return filename;
	if (s + 8 >= filename.size()) return filename;

	// Parse SxxExx
	size_t p = s + 4;
	int season = 0;
	while (p < filename.size() && filename[p] >= '0' && filename[p] <= '9')
	{
		season = season * 10 + (filename[p] - '0');
		p++;
	}
	if (p >= filename.size() || (filename[p] != 'E' && filename[p] != 'e')) return filename;
	p++;
	int episode = 0;
	while (p < filename.size() && filename[p] >= '0' && filename[p] <= '9')
	{
		episode = episode * 10 + (filename[p] - '0');
		p++;
	}

	// Find " - " after SxxExx
	size_t sep = filename.find(" - ", p);
	if (sep == std::string::npos) return filename;
	size_t titleStart = sep + 3;

	// Strip trailing quality tag and extension. Split at first " WEBDL"/"Bluray"/"BDRip"/"HDTV"/etc., or before final '.'.
	std::string rest = filename.substr(titleStart);
	const char* qualityTags[] = { " WEBDL", " WEB-DL", " WEBRip", " Bluray", " BluRay",
	                               " BDRip", " HDTV", " DVDRip", " 1080p", " 720p", " 2160p", NULL };
	size_t cut = std::string::npos;
	for (int i = 0; qualityTags[i]; i++)
	{
		size_t q = rest.find(qualityTags[i]);
		if (q != std::string::npos && (cut == std::string::npos || q < cut))
			cut = q;
	}
	if (cut == std::string::npos)
	{
		size_t dot = rest.rfind('.');
		if (dot != std::string::npos) cut = dot;
	}
	if (cut != std::string::npos) rest = rest.substr(0, cut);

	seasonOut = season;
	episodeOut = episode;
	return rest;
}

// First video at or below dir. depth 1 is this level only; 2 also covers a
// Season folder or similar sitting under the title folder.
static std::string FindFirstVideoFile(const std::string& dir, int depth = 1)
{
	DIR* d = opendir(dir.c_str());
	if (!d) return "";
	std::vector<std::string> subdirs;
	std::string out;
	struct dirent* e;
	while ((e = readdir(d)))
	{
		if (e->d_name[0] == '.') continue;
		if (IsVideoExt(e->d_name))
		{
			out = dir + "/" + e->d_name;
			break;
		}
		if (depth > 1)
		{
			std::string sub = dir + "/" + e->d_name;
			struct stat st;
			if (stat(sub.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
				subdirs.push_back(sub);
		}
	}
	closedir(d);
	if (!out.empty()) return out;

	// Files win over folders, so only descend once this level came up empty.
	for (size_t i = 0; i < subdirs.size(); i++)
	{
		out = FindFirstVideoFile(subdirs[i], depth - 1);
		if (!out.empty()) return out;
	}
	return "";
}


// Filesystem scan ------------------------------------------------------------

static void ScanMoviesInto(std::vector<MovieRec>& out)
{
	out.clear();
	DIR* d = opendir(kMoviesRoot);
	if (!d) return;
	struct dirent* e;
	while ((e = readdir(d)))
	{
		if (e->d_name[0] == '.') continue;
		std::string folder = std::string(kMoviesRoot) + "/" + e->d_name;
		struct stat st;
		if (stat(folder.c_str(), &st) != 0) continue;

		std::string videoPath;
		std::string title = e->d_name;
		if (S_ISDIR(st.st_mode)) {
			videoPath = FindFirstVideoFile(folder, 2);
		} else if (IsVideoExt(e->d_name)) {
			// Movies dropped straight in the root, no folder per title.
			videoPath = folder;
			size_t dot = title.find_last_of('.');
			if (dot != std::string::npos) title.erase(dot);
		}
		if (videoPath.empty()) continue;

		MovieRec m;
		m.title = title;
		m.path  = videoPath;
		m.year  = ParseYearFromTitle(m.title);
		out.push_back(m);
		int n = (int)out.size();
		BumpProgress(n);
		if ((n % 50) == 0) printf("[MediaDB] Movies: %d found\n", n);
	}
	closedir(d);
	printf("[MediaDB] Movie scan done: %d total\n", (int)out.size());
}

static void ScanShowSeasons(ShowRec& show)
{
	DIR* d = opendir(show.path.c_str());
	if (!d) return;
	struct dirent* e;
	while ((e = readdir(d)))
	{
		if (e->d_name[0] == '.') continue;
		std::string seasonDir = show.path + "/" + e->d_name;
		struct stat st;
		if (stat(seasonDir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) continue;
		if (strncasecmp(e->d_name, "Season", 6) != 0 &&
		    strncasecmp(e->d_name, "Specials", 8) != 0) continue;

		SeasonRec s;
		s.name = e->d_name;

		DIR* sd = opendir(seasonDir.c_str());
		if (!sd) continue;
		struct dirent* se;
		while ((se = readdir(sd)))
		{
			if (se->d_name[0] == '.') continue;
			if (!IsVideoExt(se->d_name)) continue;
			EpisodeRec ep;
			ep.path = seasonDir + "/" + se->d_name;
			ep.title = ParseEpisodeTitle(se->d_name, ep.season, ep.episode);
			s.episodes.push_back(ep);
		}
		closedir(sd);

		// Sort episodes by episode number.
		for (size_t i = 1; i < s.episodes.size(); i++)
			for (size_t j = i; j > 0 && s.episodes[j].episode < s.episodes[j - 1].episode; j--)
			{
				EpisodeRec t = s.episodes[j];
				s.episodes[j] = s.episodes[j - 1];
				s.episodes[j - 1] = t;
			}

		show.seasons.push_back(s);
	}
	closedir(d);

	// Episodes sitting straight in the show folder, no Season dirs. Common
	// enough that dropping the show entirely just looks like nothing loaded.
	if (show.seasons.empty())
	{
		SeasonRec s;
		s.name = "Season 1";
		DIR* fd = opendir(show.path.c_str());
		if (!fd) return;
		struct dirent* fe;
		while ((fe = readdir(fd)))
		{
			if (fe->d_name[0] == '.') continue;
			if (!IsVideoExt(fe->d_name)) continue;
			EpisodeRec ep;
			ep.path = show.path + "/" + fe->d_name;
			ep.title = ParseEpisodeTitle(fe->d_name, ep.season, ep.episode);
			s.episodes.push_back(ep);
		}
		closedir(fd);
		if (!s.episodes.empty()) show.seasons.push_back(s);
	}
}

static void ScanShowsInto(std::vector<ShowRec>& out)
{
	out.clear();
	DIR* d = opendir(kTvRoot);
	if (!d) return;
	struct dirent* e;
	while ((e = readdir(d)))
	{
		if (e->d_name[0] == '.') continue;
		std::string folder = std::string(kTvRoot) + "/" + e->d_name;
		struct stat st;
		if (stat(folder.c_str(), &st) != 0) continue;

		ShowRec sh;
		if (!S_ISDIR(st.st_mode))
		{
			// A loose episode in the root. Its own one episode show, so it
			// is at least reachable instead of invisible.
			if (!IsVideoExt(e->d_name)) continue;
			std::string title = e->d_name;
			size_t dot = title.find_last_of('.');
			if (dot != std::string::npos) title.erase(dot);

			SeasonRec s;
			s.name = "Season 1";
			EpisodeRec ep;
			ep.path  = folder;
			ep.title = ParseEpisodeTitle(e->d_name, ep.season, ep.episode);
			s.episodes.push_back(ep);

			// Keyed on the file itself so every loose show stays its own entry.
			sh.title = title;
			sh.path  = folder;
			sh.seasons.push_back(s);
			out.push_back(sh);
			BumpProgress((int)out.size());
			continue;
		}

		sh.title = e->d_name;
		sh.path  = folder;
		ScanShowSeasons(sh);
		if (!sh.seasons.empty()) {
			out.push_back(sh);
			int n = (int)out.size();
			BumpProgress(n);
			if ((n % 10) == 0) printf("[MediaDB] Shows: %d found\n", n);
		}
	}
	closedir(d);
	printf("[MediaDB] Show scan done: %d total\n", (int)out.size());
}


// Cache I/O ------------------------------------------------------------------

static void WriteStr(FILE* f, const std::string& s)
{
	uint16_t n = (uint16_t)s.size();
	fwrite(&n, 2, 1, f);
	if (n) fwrite(s.data(), 1, n, f);
}

static bool ReadStr(FILE* f, std::string& out)
{
	uint16_t n;
	if (fread(&n, 2, 1, f) != 1) return false;
	out.resize(n);
	if (n && fread(&out[0], 1, n, f) != n) return false;
	return true;
}

static void WriteCacheTo(const std::vector<MovieRec>& movies,
                         const std::vector<ShowRec>&  shows,
                         const char* path)
{
	// Write atomically: temp file + rename so a kill mid-write never
	// leaves the cache truncated.
	std::string tmp = std::string(path) + ".tmp";
	FILE* f = fopen(tmp.c_str(), "wb");
	if (!f) return;
	fwrite(MEDIADB_MAGIC, 4, 1, f);
	uint32_t version = MEDIADB_VERSION;
	fwrite(&version, 4, 1, f);

	uint32_t nMovies = (uint32_t)movies.size();
	fwrite(&nMovies, 4, 1, f);
	for (uint32_t i = 0; i < nMovies; i++)
	{
		WriteStr(f, movies[i].title);
		WriteStr(f, movies[i].path);
		uint16_t y = (uint16_t)movies[i].year;
		fwrite(&y, 2, 1, f);
		WriteStr(f, movies[i].overview);
		uint16_t vote = (uint16_t)(movies[i].voteAverage * 10.0f);  // 0..100
		int32_t  tid  = movies[i].tmdbId;
		fwrite(&vote, 2, 1, f);
		fwrite(&tid,  4, 1, f);
	}

	uint32_t nShows = (uint32_t)shows.size();
	fwrite(&nShows, 4, 1, f);
	for (uint32_t i = 0; i < nShows; i++)
	{
		WriteStr(f, shows[i].title);
		WriteStr(f, shows[i].path);
		WriteStr(f, shows[i].overview);
		uint16_t vote = (uint16_t)(shows[i].voteAverage * 10.0f);
		int32_t  tid  = shows[i].tmdbId;
		fwrite(&vote, 2, 1, f);
		fwrite(&tid,  4, 1, f);
		uint32_t nSeasons = (uint32_t)shows[i].seasons.size();
		fwrite(&nSeasons, 4, 1, f);
		for (uint32_t s = 0; s < nSeasons; s++)
		{
			WriteStr(f, shows[i].seasons[s].name);
			uint32_t nEps = (uint32_t)shows[i].seasons[s].episodes.size();
			fwrite(&nEps, 4, 1, f);
			for (uint32_t e = 0; e < nEps; e++)
			{
				const EpisodeRec& ep = shows[i].seasons[s].episodes[e];
				WriteStr(f, ep.title);
				WriteStr(f, ep.path);
				uint16_t sn = (uint16_t)ep.season;
				uint16_t en = (uint16_t)ep.episode;
				fwrite(&sn, 2, 1, f);
				fwrite(&en, 2, 1, f);
			}
		}
	}

	fclose(f);
#ifdef THESEUS_HOST_WIN
	// MS CRT rename() won't replace an existing file.
	MoveFileExA(tmp.c_str(), path, MOVEFILE_REPLACE_EXISTING);
#else
	rename(tmp.c_str(), path);
#endif
	printf("[MediaDB] Saved cache: %u movies, %u shows\n", nMovies, nShows);
}

static void SaveCache() { WriteCacheTo(g_movies, g_shows, kCachePath); }

static bool LoadCache()
{
	FILE* f = fopen(kCachePath, "rb");
	if (!f) return false;
	char magic[4];
	if (fread(magic, 4, 1, f) != 1 || memcmp(magic, MEDIADB_MAGIC, 4) != 0)
	{
		fclose(f); return false;
	}
	uint32_t version;
	if (fread(&version, 4, 1, f) != 1 || version != MEDIADB_VERSION)
	{
		fclose(f); return false;
	}

	g_movies.clear();
	g_shows.clear();

	uint32_t nMovies;
	if (fread(&nMovies, 4, 1, f) != 1) { fclose(f); return false; }
	for (uint32_t i = 0; i < nMovies; i++)
	{
		MovieRec m;
		uint16_t y;
		if (!ReadStr(f, m.title) || !ReadStr(f, m.path) ||
		    fread(&y, 2, 1, f) != 1)
		{
			fclose(f); g_movies.clear(); return false;
		}
		m.year = y;
		uint16_t vote = 0;
		int32_t  tid  = 0;
		if (!ReadStr(f, m.overview) ||
		    fread(&vote, 2, 1, f) != 1 ||
		    fread(&tid,  4, 1, f) != 1)
		{
			fclose(f); g_movies.clear(); return false;
		}
		m.voteAverage = vote / 10.0f;
		m.tmdbId      = tid;
		g_movies.push_back(m);
	}

	uint32_t nShows;
	if (fread(&nShows, 4, 1, f) != 1) { fclose(f); return false; }
	for (uint32_t i = 0; i < nShows; i++)
	{
		ShowRec sh;
		if (!ReadStr(f, sh.title) || !ReadStr(f, sh.path)) { fclose(f); return false; }
		uint16_t vote = 0;
		int32_t  tid  = 0;
		if (!ReadStr(f, sh.overview) ||
		    fread(&vote, 2, 1, f) != 1 ||
		    fread(&tid,  4, 1, f) != 1)
		{
			fclose(f); return false;
		}
		sh.voteAverage = vote / 10.0f;
		sh.tmdbId      = tid;
		uint32_t nSeasons;
		if (fread(&nSeasons, 4, 1, f) != 1) { fclose(f); return false; }
		for (uint32_t s = 0; s < nSeasons; s++)
		{
			SeasonRec sn;
			if (!ReadStr(f, sn.name)) { fclose(f); return false; }
			uint32_t nEps;
			if (fread(&nEps, 4, 1, f) != 1) { fclose(f); return false; }
			for (uint32_t e = 0; e < nEps; e++)
			{
				EpisodeRec ep;
				uint16_t snn, en;
				if (!ReadStr(f, ep.title) || !ReadStr(f, ep.path) ||
				    fread(&snn, 2, 1, f) != 1 || fread(&en, 2, 1, f) != 1)
				{
					fclose(f); return false;
				}
				ep.season = snn; ep.episode = en;
				sn.episodes.push_back(ep);
			}
			sh.seasons.push_back(sn);
		}
		g_shows.push_back(sh);
	}

	fclose(f);
	printf("[MediaDB] Loaded cache: %u movies, %u shows\n", nMovies, nShows);
	return true;
}


// Node binding ---------------------------------------------------------------

extern "C" void MediaDB_ScanAndCache();

class CMediaCollection : public CNode
{
public:
	CMediaCollection() : m_curShowIdx(-1), m_curSeasonIdx(-1),
	                     m_curSelKind(0), m_curSelIdx(-1)
	{
		// Try cache only. Scanning the filesystem can take seconds for
		// large libraries on network mounts; require an explicit
		// RefreshLibrary() call from XAP (or out-of-band first run) so
		// startup never stalls.
		LoadCache();
	}

	DECLARE_NODE(CMediaCollection, CNode)
	DECLARE_NODE_PROPS()
	DECLARE_NODE_FUNCTIONS()

	int m_curShowIdx;
	int m_curSeasonIdx;
	// Selection persists across XAP scene transitions where var-globals are
	// flaky (writes in handler A occasionally don't propagate to handler B).
	// SetSelectedMovie/Show stash the index here; GetSelectedXxx() pulls it
	// back. Means the wrapper doesn't rely on gSelectedIndex round-trip.
	int m_curSelKind;   // 0 = movie, 1 = show
	int m_curSelIdx;

	int GetMovieCount() { return (int)g_movies.size(); }
	CStrObject* GetMovieTitle(int i)
	{
		if (i < 0 || i >= (int)g_movies.size()) return new CStrObject;
		// Strip trailing " (YYYY)" for cleaner display.
		std::string t = g_movies[i].title;
		size_t p = t.rfind(" (");
		if (p != std::string::npos) t = t.substr(0, p);
		return new CStrObject(_T(t.c_str()));
	}
	CStrObject* GetMovieMeta(int i)
	{
		if (i < 0 || i >= (int)g_movies.size()) return new CStrObject;
		char buf[64];
		if (g_movies[i].year > 0)
			sprintf(buf, "%d  Movie", g_movies[i].year);
		else
			sprintf(buf, "Movie");
		return new CStrObject(_T(buf));
	}
	CStrObject* GetMoviePath(int i)
	{
		if (i < 0 || i >= (int)g_movies.size()) return new CStrObject;
		return new CStrObject(_T(g_movies[i].path.c_str()));
	}

	int GetShowCount() { return (int)g_shows.size(); }
	CStrObject* GetShowTitle(int i)
	{
		if (i < 0 || i >= (int)g_shows.size()) return new CStrObject;
		std::string t = g_shows[i].title;
		size_t p = t.rfind(" (");
		if (p != std::string::npos) t = t.substr(0, p);
		return new CStrObject(_T(t.c_str()));
	}
	CStrObject* GetShowMeta(int i)
	{
		if (i < 0 || i >= (int)g_shows.size()) return new CStrObject;
		int totalEps = 0;
		for (size_t s = 0; s < g_shows[i].seasons.size(); s++)
			totalEps += (int)g_shows[i].seasons[s].episodes.size();
		char buf[96];
		int nSeasons = (int)g_shows[i].seasons.size();
		sprintf(buf, "TV  %d %s  %d %s",
			nSeasons, nSeasons == 1 ? "Season" : "Seasons",
			totalEps, totalEps == 1 ? "Episode" : "Episodes");
		return new CStrObject(_T(buf));
	}

	void SetCurrentShow(int i)
	{
		m_curShowIdx = i;
		m_curSeasonIdx = -1;
	}
	int GetSeasonCount()
	{
		if (m_curShowIdx < 0 || m_curShowIdx >= (int)g_shows.size()) return 0;
		return (int)g_shows[m_curShowIdx].seasons.size();
	}
	CStrObject* GetSeasonName(int sIdx)
	{
		if (m_curShowIdx < 0 || m_curShowIdx >= (int)g_shows.size()) return new CStrObject;
		if (sIdx < 0 || sIdx >= (int)g_shows[m_curShowIdx].seasons.size()) return new CStrObject;
		return new CStrObject(_T(g_shows[m_curShowIdx].seasons[sIdx].name.c_str()));
	}

	void SetCurrentSeason(int i) { m_curSeasonIdx = i; }
	int GetEpisodeCount()
	{
		if (m_curShowIdx < 0 || m_curShowIdx >= (int)g_shows.size()) return 0;
		if (m_curSeasonIdx < 0 || m_curSeasonIdx >= (int)g_shows[m_curShowIdx].seasons.size()) return 0;
		return (int)g_shows[m_curShowIdx].seasons[m_curSeasonIdx].episodes.size();
	}
	CStrObject* GetEpisodeTitle(int epIdx)
	{
		if (m_curShowIdx < 0 || m_curShowIdx >= (int)g_shows.size()) return new CStrObject;
		if (m_curSeasonIdx < 0 || m_curSeasonIdx >= (int)g_shows[m_curShowIdx].seasons.size()) return new CStrObject;
		const std::vector<EpisodeRec>& eps = g_shows[m_curShowIdx].seasons[m_curSeasonIdx].episodes;
		if (epIdx < 0 || epIdx >= (int)eps.size()) return new CStrObject;
		return new CStrObject(_T(eps[epIdx].title.c_str()));
	}
	CStrObject* GetEpisodePath(int epIdx)
	{
		if (m_curShowIdx < 0 || m_curShowIdx >= (int)g_shows.size()) return new CStrObject;
		if (m_curSeasonIdx < 0 || m_curSeasonIdx >= (int)g_shows[m_curShowIdx].seasons.size()) return new CStrObject;
		const std::vector<EpisodeRec>& eps = g_shows[m_curShowIdx].seasons[m_curSeasonIdx].episodes;
		if (epIdx < 0 || epIdx >= (int)eps.size()) return new CStrObject;
		return new CStrObject(_T(eps[epIdx].path.c_str()));
	}

	void RefreshLibrary()
	{
		MediaDB_ScanAndCache();
	}

	// Hands a movie/episode path to libmpv and switches into our own
	// fullscreen video mode (no DVD-player XAP overlay; the existing DVD
	// player path was broken). Mutes the dashboard, sets a global flag
	// that sdl_main.cpp checks to draw the video instead of the dashboard.
	void PlayPath(const char* path, const char* displayTitle, const char* displaySubtitle)
	{
		if (!path || !*path) return;
		// MediaPlayer_Open is idempotent and will lazily call MediaPlayer_Init
		// when the previous Stop tore the pipeline down.
		if (!MediaPlayer_Open(path)) return;
		extern bool g_mediaFullscreen;
		extern char g_mediaFullscreenTitle[256];
		extern char g_mediaFullscreenSubtitle[256];
		extern void ApplyEffectiveMute_Public();
		g_mediaFullscreen = true;
		ApplyEffectiveMute_Public();
		strncpy(g_mediaFullscreenTitle, displayTitle ? displayTitle : "", sizeof(g_mediaFullscreenTitle) - 1);
		g_mediaFullscreenTitle[sizeof(g_mediaFullscreenTitle) - 1] = 0;
		strncpy(g_mediaFullscreenSubtitle, displaySubtitle ? displaySubtitle : "", sizeof(g_mediaFullscreenSubtitle) - 1);
		g_mediaFullscreenSubtitle[sizeof(g_mediaFullscreenSubtitle) - 1] = 0;
	}

	void PlayMovie(int i)
	{
		std::lock_guard<std::mutex> lock(g_dbMutex);
		if (i < 0 || i >= (int)g_movies.size()) return;
		// Title: strip trailing " (YYYY)" for cleaner display.
		std::string title = g_movies[i].title;
		size_t p = title.rfind(" (");
		if (p != std::string::npos) title = title.substr(0, p);
		int year = g_movies[i].year;

		// Subtitle: year, plus TMDB rating if available.
		char sub[64] = "";
		TmdbMovie tm = TMDB_GetMovie(title.c_str(), year);
		if (year > 0) snprintf(sub, sizeof(sub), "%d", year);
		if (tm.found && tm.voteAverage > 0.0f) {
			char rating[16];
			snprintf(rating, sizeof(rating), " - %.1f/10", tm.voteAverage);
			strncat(sub, rating, sizeof(sub) - strlen(sub) - 1);
		}
		PlayPath(g_movies[i].path.c_str(), title.c_str(), sub);
	}

	// Plot synopsis from the cached MediaDB record (filled by TMDB enrichment
	// during MediaDB_ScanAndCache). No network. Empty string if not enriched
	// (no TMDB key, or no match), caller can show meta-only.
	CStrObject* GetMoviePlot(int i)
	{
		std::lock_guard<std::mutex> lock(g_dbMutex);
		if (i < 0 || i >= (int)g_movies.size()) return new CStrObject;
		return new CStrObject(_T(g_movies[i].overview.c_str()));
	}

	CStrObject* GetShowPlot(int i)
	{
		std::lock_guard<std::mutex> lock(g_dbMutex);
		if (i < 0 || i >= (int)g_shows.size()) return new CStrObject;
		return new CStrObject(_T(g_shows[i].overview.c_str()));
	}

	// Legacy, TMDB fetch is now bundled into MediaDB_ScanAndCache so these
	// are no-ops. Keep the symbols so existing XAPs don't break.
	void PrefetchMovieTmdb(int) {}
	void PrefetchShowTmdb(int)  {}

	// Persistent selection (set in browser OnADown, read in action menu
	// OnArrival). Avoids XAP-global var-propagation flakiness.
	void SetSelectedMovie(int i)
	{
		m_curSelKind = 0;
		m_curSelIdx  = i;
	}
	void SetSelectedShow(int i)
	{
		m_curSelKind = 1;
		m_curSelIdx  = i;
		// Also update the show context so seasons/episodes lookups work
		// when entering Scene 3 from PLAY.
		m_curShowIdx = i;
		m_curSeasonIdx = -1;
	}
	int  GetSelectedKind()  { return m_curSelKind; }
	int  GetSelectedIndex() { return m_curSelIdx;  }
	CStrObject* GetSelectedTitle()
	{
		std::lock_guard<std::mutex> lock(g_dbMutex);
		if (m_curSelKind == 0) {
			if (m_curSelIdx < 0 || m_curSelIdx >= (int)g_movies.size()) return new CStrObject;
			std::string t = g_movies[m_curSelIdx].title;
			size_t p = t.rfind(" (");
			if (p != std::string::npos) t = t.substr(0, p);
			return new CStrObject(_T(t.c_str()));
		}
		if (m_curSelIdx < 0 || m_curSelIdx >= (int)g_shows.size()) return new CStrObject;
		std::string t = g_shows[m_curSelIdx].title;
		size_t p = t.rfind(" (");
		if (p != std::string::npos) t = t.substr(0, p);
		return new CStrObject(_T(t.c_str()));
	}
	CStrObject* GetSelectedMeta()
	{
		std::lock_guard<std::mutex> lock(g_dbMutex);
		if (m_curSelKind == 0) {
			if (m_curSelIdx < 0 || m_curSelIdx >= (int)g_movies.size()) return new CStrObject;
			char buf[64];
			if (g_movies[m_curSelIdx].year > 0)
				sprintf(buf, "%d  Movie", g_movies[m_curSelIdx].year);
			else
				sprintf(buf, "Movie");
			return new CStrObject(_T(buf));
		}
		if (m_curSelIdx < 0 || m_curSelIdx >= (int)g_shows.size()) return new CStrObject;
		int totalEps = 0;
		for (size_t s = 0; s < g_shows[m_curSelIdx].seasons.size(); s++)
			totalEps += (int)g_shows[m_curSelIdx].seasons[s].episodes.size();
		char buf[96];
		int nSeasons = (int)g_shows[m_curSelIdx].seasons.size();
		sprintf(buf, "TV  %d %s  %d %s",
			nSeasons, nSeasons == 1 ? "Season" : "Seasons",
			totalEps, totalEps == 1 ? "Episode" : "Episodes");
		return new CStrObject(_T(buf));
	}
	CStrObject* GetSelectedPlot()
	{
		std::lock_guard<std::mutex> lock(g_dbMutex);
		if (m_curSelKind == 0) {
			if (m_curSelIdx < 0 || m_curSelIdx >= (int)g_movies.size()) return new CStrObject;
			return new CStrObject(_T(g_movies[m_curSelIdx].overview.c_str()));
		}
		if (m_curSelIdx < 0 || m_curSelIdx >= (int)g_shows.size()) return new CStrObject;
		return new CStrObject(_T(g_shows[m_curSelIdx].overview.c_str()));
	}
	void PlaySelected()
	{
		if (m_curSelKind == 0) PlayMovie(m_curSelIdx);
		// for shows the wrapper drills into seasons; this isn't called.
	}

	// Latched pulse, set when the user leaves video playback. The XAP action
	// menu polls it and re-runs ShowMediaActionMenu to reset scene state.
	// Reading clears it.
	int ConsumePlaybackExited()
	{
		extern int g_mediaPlaybackExited;
		int v = g_mediaPlaybackExited;
		g_mediaPlaybackExited = 0;
		return v;
	}

	void PlayEpisode(int epIdx)
	{
		std::lock_guard<std::mutex> lock(g_dbMutex);
		if (m_curShowIdx < 0 || m_curShowIdx >= (int)g_shows.size()) return;
		if (m_curSeasonIdx < 0 || m_curSeasonIdx >= (int)g_shows[m_curShowIdx].seasons.size()) return;
		const std::vector<EpisodeRec>& eps = g_shows[m_curShowIdx].seasons[m_curSeasonIdx].episodes;
		if (epIdx < 0 || epIdx >= (int)eps.size()) return;
		// Title: episode name (already parsed clean by ScanShowSeasons).
		// Subtitle: "<Show> · S<NN>E<NN>"
		std::string showTitle = g_shows[m_curShowIdx].title;
		size_t p = showTitle.rfind(" (");
		if (p != std::string::npos) showTitle = showTitle.substr(0, p);
		char sub[256];
		snprintf(sub, sizeof(sub), "%s  -  S%02dE%02d",
			showTitle.c_str(), eps[epIdx].season, eps[epIdx].episode);
		PlayPath(eps[epIdx].path.c_str(), eps[epIdx].title.c_str(), sub);
	}
};


// Parallel TMDB enrichment. Skips items with tmdbId != 0. Workers pull from
// an atomic counter; checkpoints to disk so a kill mid-scan keeps progress.

#define TMDB_WORKERS         8
#define TMDB_CHECKPOINT_EVERY 10

static void EnrichMoviesTmdb(std::vector<MovieRec>& movies, std::vector<ShowRec>& showsForCheckpoint)
{
	if (!TMDB_HasKey()) return;
	int total = (int)movies.size();
	int needed = 0;
	for (auto& m : movies) if (m.tmdbId == 0) needed++;
	if (needed == 0) return;
	SetPhase("Looking up movies on TMDB", needed);
	printf("[MediaDB] Enriching %d movies via TMDB (skipping %d already-known) with %d workers...\n",
		needed, total - needed, TMDB_WORKERS);

	std::atomic<int> nextIdx{0};
	std::atomic<int> done{0};
	std::atomic<int> hits{0};
	std::atomic<int> misses{0};
	std::mutex moviesLock;  // guards 'movies' writes between workers + checkpointer

	auto worker = [&]() {
		while (true) {
			int i = nextIdx.fetch_add(1);
			if (i >= total) break;
			MovieRec snap;
			{
				std::lock_guard<std::mutex> lk(moviesLock);
				if (movies[i].tmdbId != 0) continue;  // already done
				snap = movies[i];
			}
			std::string clean = snap.title;
			size_t p = clean.rfind(" (");
			if (p != std::string::npos) clean = clean.substr(0, p);
			TmdbMovie tm = TMDB_LookupMovie(clean.c_str(), snap.year);
			{
				std::lock_guard<std::mutex> lk(moviesLock);
				MovieRec& m = movies[i];
				if (tm.found) {
					m.overview    = tm.overview;
					m.voteAverage = tm.voteAverage;
					m.tmdbId      = tm.tmdbId;
					hits.fetch_add(1);
				} else {
					m.tmdbId = -1;
					misses.fetch_add(1);
				}
			}
			int d = done.fetch_add(1) + 1;
			BumpProgress(d);
			// Checkpoint every N items: snapshot under lock, write to disk
			// without holding lock so HTTP workers don't block on file I/O.
			if ((d % TMDB_CHECKPOINT_EVERY) == 0) {
				std::vector<MovieRec> snapMovies;
				{
					std::lock_guard<std::mutex> lk(moviesLock);
					snapMovies = movies;
				}
				WriteCacheTo(snapMovies, showsForCheckpoint, kCachePath);
			}
		}
	};

	std::thread pool[TMDB_WORKERS];
	for (int t = 0; t < TMDB_WORKERS; t++) pool[t] = std::thread(worker);
	for (int t = 0; t < TMDB_WORKERS; t++) pool[t].join();

	printf("[MediaDB] Movie enrichment done: %d hits, %d misses\n",
		hits.load(), misses.load());
}

static void EnrichShowsTmdb(std::vector<ShowRec>& shows, std::vector<MovieRec>& moviesForCheckpoint)
{
	if (!TMDB_HasKey()) return;
	int total = (int)shows.size();
	int needed = 0;
	for (auto& s : shows) if (s.tmdbId == 0) needed++;
	if (needed == 0) return;
	SetPhase("Looking up TV shows on TMDB", needed);
	printf("[MediaDB] Enriching %d shows via TMDB (skipping %d already-known) with %d workers...\n",
		needed, total - needed, TMDB_WORKERS);

	std::atomic<int> nextIdx{0};
	std::atomic<int> done{0};
	std::atomic<int> hits{0};
	std::atomic<int> misses{0};
	std::mutex showsLock;

	auto worker = [&]() {
		while (true) {
			int i = nextIdx.fetch_add(1);
			if (i >= total) break;
			ShowRec snap;
			{
				std::lock_guard<std::mutex> lk(showsLock);
				if (shows[i].tmdbId != 0) continue;
				snap = shows[i];
			}
			std::string clean = snap.title;
			size_t p = clean.rfind(" (");
			if (p != std::string::npos) clean = clean.substr(0, p);
			TmdbShow ts = TMDB_LookupShow(clean.c_str());
			{
				std::lock_guard<std::mutex> lk(showsLock);
				ShowRec& s = shows[i];
				if (ts.found) {
					s.overview    = ts.overview;
					s.voteAverage = ts.voteAverage;
					s.tmdbId      = ts.tmdbId;
					hits.fetch_add(1);
				} else {
					s.tmdbId = -1;
					misses.fetch_add(1);
				}
			}
			int d = done.fetch_add(1) + 1;
			BumpProgress(d);
			if ((d % TMDB_CHECKPOINT_EVERY) == 0) {
				std::vector<ShowRec> snapShows;
				{
					std::lock_guard<std::mutex> lk(showsLock);
					snapShows = shows;
				}
				WriteCacheTo(moviesForCheckpoint, snapShows, kCachePath);
			}
		}
	};

	std::thread pool[TMDB_WORKERS];
	for (int t = 0; t < TMDB_WORKERS; t++) pool[t] = std::thread(worker);
	for (int t = 0; t < TMDB_WORKERS; t++) pool[t].join();

	printf("[MediaDB] Show enrichment done: %d hits, %d misses\n",
		hits.load(), misses.load());
}


// Carry-forward helpers: preserve TMDB enrichment across a rescan when the
// path is unchanged (tmdbId stays != 0 so EnrichMoviesTmdb skips them).
static void CarryForwardMovies(std::vector<MovieRec>& fresh)
{
	std::lock_guard<std::mutex> lock(g_dbMutex);
	std::map<std::string, const MovieRec*> oldMap;
	for (const auto& om : g_movies) oldMap[om.path] = &om;
	for (auto& nm : fresh) {
		auto it = oldMap.find(nm.path);
		if (it != oldMap.end() && it->second->tmdbId != 0) {
			nm.overview    = it->second->overview;
			nm.voteAverage = it->second->voteAverage;
			nm.tmdbId      = it->second->tmdbId;
		}
	}
}

static void CarryForwardShows(std::vector<ShowRec>& fresh)
{
	std::lock_guard<std::mutex> lock(g_dbMutex);
	std::map<std::string, const ShowRec*> oldMap;
	for (const auto& os : g_shows) oldMap[os.path] = &os;
	for (auto& ns : fresh) {
		auto it = oldMap.find(ns.path);
		if (it != oldMap.end() && it->second->tmdbId != 0) {
			ns.overview    = it->second->overview;
			ns.voteAverage = it->second->voteAverage;
			ns.tmdbId      = it->second->tmdbId;
		}
	}
}


// Refresh ONLY the movie collection. g_shows and the TV side of the cache
// are left untouched. Spawns a detached background thread; UI polls
// MediaDB_IsScanning / GetScanPhase / GetScanProgress to render the modal.
extern "C" void MediaDB_RefreshMovies()
{
	bool wasRunning = g_scanRunning.exchange(true);
	if (wasRunning) return;

	std::thread([]() {
		printf("[MediaDB] Movie refresh started: %s\n", kMoviesRoot);

		std::vector<MovieRec> tmpMovies;
		SetPhase("Scanning movie folder", 0);
		ScanMoviesInto(tmpMovies);

		CarryForwardMovies(tmpMovies);
		// Enrichment runs against the live g_shows snapshot for checkpoint writes
		// so we don't lose existing TV data if movie scan crashes mid-flight.
		std::vector<ShowRec> showsSnap;
		{
			std::lock_guard<std::mutex> lock(g_dbMutex);
			showsSnap = g_shows;
		}
		// Baseline save before enrichment so a quit during HTTP work still leaves
		// a usable catalog (and a no-key run produces a cache at all).
		printf("[MediaDB] Movie scan finished (%u movies). Saving baseline cache...\n",
			(unsigned)tmpMovies.size());
		WriteCacheTo(tmpMovies, showsSnap, kCachePath);
		{
			std::lock_guard<std::mutex> lock(g_dbMutex);
			g_movies = tmpMovies;
		}
		EnrichMoviesTmdb(tmpMovies, showsSnap);

		SetPhase("Saving cache", 0);
		{
			std::lock_guard<std::mutex> lock(g_dbMutex);
			g_movies = std::move(tmpMovies);
			SaveCache();
		}

		printf("[MediaDB] Movie refresh complete: %u movies\n",
			(unsigned)g_movies.size());
		SetPhase("", 0);
		g_scanRunning = false;
	}).detach();
}


// Refresh ONLY the TV collection. g_movies left untouched.
extern "C" void MediaDB_RefreshShows()
{
	bool wasRunning = g_scanRunning.exchange(true);
	if (wasRunning) return;

	std::thread([]() {
		printf("[MediaDB] TV refresh started: %s\n", kTvRoot);

		std::vector<ShowRec> tmpShows;
		SetPhase("Scanning TV folder", 0);
		ScanShowsInto(tmpShows);

		CarryForwardShows(tmpShows);
		std::vector<MovieRec> moviesSnap;
		{
			std::lock_guard<std::mutex> lock(g_dbMutex);
			moviesSnap = g_movies;
		}
		printf("[MediaDB] TV scan finished (%u shows). Saving baseline cache...\n",
			(unsigned)tmpShows.size());
		WriteCacheTo(moviesSnap, tmpShows, kCachePath);
		{
			std::lock_guard<std::mutex> lock(g_dbMutex);
			g_shows = tmpShows;
		}
		EnrichShowsTmdb(tmpShows, moviesSnap);

		SetPhase("Saving cache", 0);
		{
			std::lock_guard<std::mutex> lock(g_dbMutex);
			g_shows = std::move(tmpShows);
			SaveCache();
		}

		printf("[MediaDB] TV refresh complete: %u shows\n",
			(unsigned)g_shows.size());
		SetPhase("", 0);
		g_scanRunning = false;
	}).detach();
}


// Refresh both, sequentially. Used by the "Refresh All" button.
extern "C" void MediaDB_ScanAndCache()
{
	bool wasRunning = g_scanRunning.exchange(true);
	if (wasRunning) return;

	std::thread([]() {
		printf("[MediaDB] Full scan started: %s and %s\n", kMoviesRoot, kTvRoot);

		std::vector<MovieRec> tmpMovies;
		std::vector<ShowRec>  tmpShows;
		SetPhase("Scanning movie folder", 0);
		ScanMoviesInto(tmpMovies);
		SetPhase("Scanning TV folder", 0);
		ScanShowsInto(tmpShows);

		CarryForwardMovies(tmpMovies);
		CarryForwardShows(tmpShows);

		// Persist the bare catalog before TMDB enrichment starts. Without
		// this, quitting during the (possibly long) HTTP enrichment phase.
		// or running with no TMDB key at all. would leave nothing on disk.
		printf("[MediaDB] Catalog scan finished (%u movies, %u shows). Saving baseline cache...\n",
			(unsigned)tmpMovies.size(), (unsigned)tmpShows.size());
		WriteCacheTo(tmpMovies, tmpShows, kCachePath);
		{
			std::lock_guard<std::mutex> lock(g_dbMutex);
			g_movies = tmpMovies;
			g_shows  = tmpShows;
		}

		// Pass each side's tmp vector as the checkpoint partner so a kill
		// mid-scan saves both halves' progress so far.
		EnrichMoviesTmdb(tmpMovies, tmpShows);
		EnrichShowsTmdb(tmpShows, tmpMovies);

		SetPhase("Saving cache", 0);
		{
			std::lock_guard<std::mutex> lock(g_dbMutex);
			g_movies = std::move(tmpMovies);
			g_shows  = std::move(tmpShows);
			SaveCache();
		}

		printf("[MediaDB] Full scan complete: %u movies, %u shows\n",
			(unsigned)g_movies.size(), (unsigned)g_shows.size());
		SetPhase("", 0);
		g_scanRunning = false;
	}).detach();
}

extern "C" int  MediaDB_GetScanProgress() { return g_scanProgress.load(); }
extern "C" int  MediaDB_GetScanTotal()    { return g_scanTotal.load();    }
extern "C" const char* MediaDB_GetScanPhase()
{
	static thread_local char buf[128];
	std::lock_guard<std::mutex> lock(g_phaseMutex);
	strncpy(buf, g_scanPhase.c_str(), sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = 0;
	return buf;
}

extern "C" int MediaDB_GetMovieCount()
{
	std::lock_guard<std::mutex> lock(g_dbMutex);
	return (int)g_movies.size();
}
extern "C" int MediaDB_GetShowCount()
{
	std::lock_guard<std::mutex> lock(g_dbMutex);
	return (int)g_shows.size();
}
extern "C" int MediaDB_IsScanning() { return g_scanRunning.load() ? 1 : 0; }

// Boot-time hook so non-XAP consumers (Playlist Maker, etc.) see the
// cached library before the XAP MediaCollection node is constructed.
extern "C" void MediaDB_LoadCache() { LoadCache(); }

// Read accessors for non-XAP consumers (playlist_maker, etc.).
// Caller must wrap iteration in Lock/Unlock if a scan may be running.
extern "C" void MediaDB_Lock()   { g_dbMutex.lock();   }
extern "C" void MediaDB_Unlock() { g_dbMutex.unlock(); }

extern "C" const char* MediaDB_GetMovieTitleC(int i) {
    if (i < 0 || i >= (int)g_movies.size()) return "";
    return g_movies[i].title.c_str();
}
extern "C" const char* MediaDB_GetMoviePathC(int i) {
    if (i < 0 || i >= (int)g_movies.size()) return "";
    return g_movies[i].path.c_str();
}
extern "C" int MediaDB_GetMovieYearC(int i) {
    if (i < 0 || i >= (int)g_movies.size()) return 0;
    return g_movies[i].year;
}

extern "C" const char* MediaDB_GetShowTitleC(int i) {
    if (i < 0 || i >= (int)g_shows.size()) return "";
    return g_shows[i].title.c_str();
}

extern "C" int MediaDB_GetSeasonCountC(int showIdx) {
    if (showIdx < 0 || showIdx >= (int)g_shows.size()) return 0;
    return (int)g_shows[showIdx].seasons.size();
}
extern "C" const char* MediaDB_GetSeasonNameC(int showIdx, int seasonIdx) {
    if (showIdx < 0 || showIdx >= (int)g_shows.size()) return "";
    if (seasonIdx < 0 || seasonIdx >= (int)g_shows[showIdx].seasons.size()) return "";
    return g_shows[showIdx].seasons[seasonIdx].name.c_str();
}

extern "C" int MediaDB_GetEpisodeCountC(int showIdx, int seasonIdx) {
    if (showIdx < 0 || showIdx >= (int)g_shows.size()) return 0;
    if (seasonIdx < 0 || seasonIdx >= (int)g_shows[showIdx].seasons.size()) return 0;
    return (int)g_shows[showIdx].seasons[seasonIdx].episodes.size();
}
extern "C" const char* MediaDB_GetEpisodeTitleC(int showIdx, int seasonIdx, int epIdx) {
    if (showIdx < 0 || showIdx >= (int)g_shows.size()) return "";
    if (seasonIdx < 0 || seasonIdx >= (int)g_shows[showIdx].seasons.size()) return "";
    const auto& eps = g_shows[showIdx].seasons[seasonIdx].episodes;
    if (epIdx < 0 || epIdx >= (int)eps.size()) return "";
    return eps[epIdx].title.c_str();
}
extern "C" const char* MediaDB_GetEpisodePathC(int showIdx, int seasonIdx, int epIdx) {
    if (showIdx < 0 || showIdx >= (int)g_shows.size()) return "";
    if (seasonIdx < 0 || seasonIdx >= (int)g_shows[showIdx].seasons.size()) return "";
    const auto& eps = g_shows[showIdx].seasons[seasonIdx].episodes;
    if (epIdx < 0 || epIdx >= (int)eps.size()) return "";
    return eps[epIdx].path.c_str();
}
extern "C" int MediaDB_GetEpisodeSeasonNumC(int showIdx, int seasonIdx, int epIdx) {
    if (showIdx < 0 || showIdx >= (int)g_shows.size()) return 0;
    if (seasonIdx < 0 || seasonIdx >= (int)g_shows[showIdx].seasons.size()) return 0;
    const auto& eps = g_shows[showIdx].seasons[seasonIdx].episodes;
    if (epIdx < 0 || epIdx >= (int)eps.size()) return 0;
    return eps[epIdx].season;
}
extern "C" int MediaDB_GetEpisodeNumberC(int showIdx, int seasonIdx, int epIdx) {
    if (showIdx < 0 || showIdx >= (int)g_shows.size()) return 0;
    if (seasonIdx < 0 || seasonIdx >= (int)g_shows[showIdx].seasons.size()) return 0;
    const auto& eps = g_shows[showIdx].seasons[seasonIdx].episodes;
    if (epIdx < 0 || epIdx >= (int)eps.size()) return 0;
    return eps[epIdx].episode;
}

IMPLEMENT_NODE("MediaCollection", CMediaCollection, CNode)

START_NODE_PROPS(CMediaCollection, CNode)
END_NODE_PROPS()

#undef _FND_CLASS
#define _FND_CLASS CMediaCollection
START_NODE_FUN(CMediaCollection, CNode)
	NODE_FUN_IV(GetMovieCount)
	NODE_FUN_SI(GetMovieTitle)
	NODE_FUN_SI(GetMovieMeta)
	NODE_FUN_SI(GetMoviePath)
	NODE_FUN_IV(GetShowCount)
	NODE_FUN_SI(GetShowTitle)
	NODE_FUN_SI(GetShowMeta)
	NODE_FUN_VI(SetCurrentShow)
	NODE_FUN_IV(GetSeasonCount)
	NODE_FUN_SI(GetSeasonName)
	NODE_FUN_VI(SetCurrentSeason)
	NODE_FUN_IV(GetEpisodeCount)
	NODE_FUN_SI(GetEpisodeTitle)
	NODE_FUN_SI(GetEpisodePath)
	NODE_FUN_VV(RefreshLibrary)
	NODE_FUN_VI(PlayMovie)
	NODE_FUN_VI(PlayEpisode)
	NODE_FUN_SI(GetMoviePlot)
	NODE_FUN_SI(GetShowPlot)
	NODE_FUN_VI(PrefetchMovieTmdb)
	NODE_FUN_VI(PrefetchShowTmdb)
	NODE_FUN_VI(SetSelectedMovie)
	NODE_FUN_VI(SetSelectedShow)
	NODE_FUN_IV(GetSelectedKind)
	NODE_FUN_IV(GetSelectedIndex)
	NODE_FUN_SV(GetSelectedTitle)
	NODE_FUN_SV(GetSelectedMeta)
	NODE_FUN_SV(GetSelectedPlot)
	NODE_FUN_VV(PlaySelected)
	NODE_FUN_IV(ConsumePlaybackExited)
END_NODE_FUN()


// ============================================================================
// CPlaylistCollection, XAP-callable view over the user's named playlists.
// Mirrors the GetCount / GetTitle / SetCurrent / Play... shape of
// CMediaCollection so the playlist scene can be a near-clone of the TV flow.
// ============================================================================

class CPlaylistCollection : public CNode
{
public:
	CPlaylistCollection() : m_cur(-1) {}

	DECLARE_NODE(CPlaylistCollection, CNode)
	DECLARE_NODE_PROPS()
	DECLARE_NODE_FUNCTIONS()

	int m_cur;

	int GetCount() { return Playlist_Count(); }

	CStrObject* GetName(int i)
	{
		const Playlist* p = Playlist_Get(i);
		return new CStrObject(_T(p ? p->name.c_str() : ""));
	}

	CStrObject* GetMeta(int i)
	{
		const Playlist* p = Playlist_Get(i);
		if (!p) return new CStrObject;
		char buf[64];
		int n = (int)p->items.size();
		sprintf(buf, "Playlist  %d %s", n, n == 1 ? "item" : "items");
		return new CStrObject(_T(buf));
	}

	void SetCurrent(int i) { m_cur = i; }

	int GetItemCount()
	{
		const Playlist* p = Playlist_Get(m_cur);
		return p ? (int)p->items.size() : 0;
	}

	CStrObject* GetItemTitle(int i)
	{
		const Playlist* p = Playlist_Get(m_cur);
		if (!p || i < 0 || i >= (int)p->items.size()) return new CStrObject;
		const PlaylistItem& it = p->items[i];
		const char* s = it.title.empty() ? it.path.c_str() : it.title.c_str();
		return new CStrObject(_T(s));
	}

	void PlayFromIndex(int startIdx)
	{
		const Playlist* p = Playlist_Get(m_cur);
		if (!p) return;
		extern void MediaUI_PlayPlaylist(const char* playlistName, int startIdx);
		MediaUI_PlayPlaylist(p->name.c_str(), startIdx);
	}
};

IMPLEMENT_NODE("PlaylistCollection", CPlaylistCollection, CNode)

START_NODE_PROPS(CPlaylistCollection, CNode)
END_NODE_PROPS()

#undef _FND_CLASS
#define _FND_CLASS CPlaylistCollection
START_NODE_FUN(CPlaylistCollection, CNode)
	NODE_FUN_IV(GetCount)
	NODE_FUN_SI(GetName)
	NODE_FUN_SI(GetMeta)
	NODE_FUN_VI(SetCurrent)
	NODE_FUN_IV(GetItemCount)
	NODE_FUN_SI(GetItemTitle)
	NODE_FUN_VI(PlayFromIndex)
END_NODE_FUN()
