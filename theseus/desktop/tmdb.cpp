// tmdb.cpp: in-process TMDB v3 client. libcurl HTTPS GETs, slug-keyed
// JSON cache at Library/TMDB/. Targeted field extractor instead of a
// full JSON parser. TMDB shapes are stable and we want ~5 fields.

#include "app_paths.h"
#include "tmdb.h"
#include "host.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctype.h>
#include <sys/stat.h>
#include <string>
#include <map>
#include <thread>
#include <mutex>
#include <atomic>

#include "http_util.h"
#include "json_util.h"

extern char g_tmdbKey[128];   // defined in sdl_main.cpp; loaded from desktop.ini

static std::mutex                       g_resultMutex;
static std::map<std::string, TmdbMovie> s_movieResults;
static std::map<std::string, TmdbShow>  s_showResults;
static std::map<std::string, std::atomic<bool>*> s_inFlight;


// ============================================================================
// Helpers
// ============================================================================

static std::string Slugify(const char* s)
{
    std::string out;
    out.reserve(strlen(s));
    for (const char* p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (isalnum(c)) out.push_back((char)tolower(c));
        else if (!out.empty() && out.back() != '-') out.push_back('-');
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out;
}

static std::string MovieKey(const char* title, int year)
{
    char buf[16] = "";
    if (year > 0) snprintf(buf, sizeof(buf), "-%d", year);
    return Slugify(title) + buf;
}

static std::string ShowKey(const char* title)
{
    return Slugify(title);
}

// Make one folder. 0 when it exists afterwards.
static inline int Mkdir(const char* path)
{
    return Host_MakeDir(path) ? 0 : -1;
}

static std::string CachePath(const char* prefix, const std::string& slug)
{
    std::string p = std::string(AppPath_Tree("Library/TMDB")) + "/";
    Mkdir(AppPath_Tree("Library"));
    Mkdir(p.c_str());
    p += prefix;
    p += '_';
    p += slug;
    p += ".json";
    return p;
}

static std::string ReadFile(const std::string& path)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return "";
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::string out;
    if (sz > 0) {
        out.resize((size_t)sz);
        fread(&out[0], 1, (size_t)sz, f);
    }
    fclose(f);
    return out;
}

static void WriteFile(const std::string& path, const std::string& data)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return;
    if (!data.empty()) fwrite(data.data(), 1, data.size(), f);
    fclose(f);
}


// HTTP (via http_util)

static std::string HttpGet(const std::string& url)
{
    // Redact api_key for the log.
    std::string redacted = url;
    size_t k = redacted.find("api_key=");
    if (k != std::string::npos) {
        size_t e = redacted.find('&', k);
        redacted.replace(k + 8, (e == std::string::npos ? redacted.size() : e) - (k + 8), "***");
    }
    fprintf(stderr, "[TMDB] GET %s\n", redacted.c_str());

    std::string out = Http_GetToString(url);
    fprintf(stderr, "[TMDB] %zu bytes\n", out.size());
    return out;
}

// URL-encode a query string component (alpha/digit/-_. preserved, rest
// percent-encoded). Sufficient for TMDB query params.
static std::string UrlEncode(const char* s)
{
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (const char* p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back((char)c);
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0xF]);
        }
    }
    return out;
}


// First object in "results": [{...}, ...], or empty.
static std::string GetFirstResult(const std::string& json)
{
    size_t arr = Json_FindArray(json, "results");
    if (arr == std::string::npos) return "";
    std::vector<std::string> items = Json_SplitArray(json, arr);
    return items.empty() ? "" : items[0];
}

// A real search answer always carries "results", even when it's empty.
// Anything else is an error page and must not be cached.
static bool IsSearchResponse(const std::string& body)
{
    return !body.empty() && Json_FindArray(body, "results") != std::string::npos;
}

// First 4 chars after a YYYY-MM-DD field as int.
static int YearFromDate(const std::string& dateStr)
{
    if (dateStr.size() < 4) return 0;
    return (int)strtol(dateStr.substr(0, 4).c_str(), NULL, 10);
}


// ============================================================================
// Real lookup: cache check, then HTTP, parse, store.
// ============================================================================

static TmdbMovie ParseMovieJson(const std::string& json)
{
    TmdbMovie m{};
    m.ready = true;
    std::string first = GetFirstResult(json);
    if (first.empty()) return m;
    m.found       = true;
    m.title       = Json_GetString(first, "title");
    m.overview    = Json_GetString(first, "overview");
    m.year        = YearFromDate(Json_GetString(first, "release_date"));
    m.voteAverage = Json_GetFloat(first, "vote_average");
    m.tmdbId      = Json_GetInt(first, "id");
    return m;
}

static TmdbShow ParseShowJson(const std::string& json)
{
    TmdbShow s{};
    s.ready = true;
    std::string first = GetFirstResult(json);
    if (first.empty()) return s;
    s.found       = true;
    s.title       = Json_GetString(first, "name");
    s.overview    = Json_GetString(first, "overview");
    s.year        = YearFromDate(Json_GetString(first, "first_air_date"));
    s.voteAverage = Json_GetFloat(first, "vote_average");
    s.tmdbId      = Json_GetInt(first, "id");
    return s;
}

static TmdbMovie DoMovieLookup(const std::string& title, int year, const std::string& key)
{
    std::string cachePath = CachePath("movie", MovieKey(title.c_str(), year));

    // Try cache first.
    std::string body = ReadFile(cachePath);
    if (IsSearchResponse(body))
        return ParseMovieJson(body);

    if (g_tmdbKey[0] == 0) {
        TmdbMovie m{}; m.ready = true; return m;
    }
    std::string url = "https://api.themoviedb.org/3/search/movie?api_key=";
    url += g_tmdbKey;
    url += "&query=";
    url += UrlEncode(title.c_str());
    if (year > 0) {
        char y[16];
        snprintf(y, sizeof(y), "&year=%d", year);
        url += y;
    }
    url += "&include_adult=false";
    body = HttpGet(url);
    (void)key;
    if (!IsSearchResponse(body)) {
        TmdbMovie m{}; m.ready = true; m.failed = true; return m;
    }
    WriteFile(cachePath, body);
    return ParseMovieJson(body);
}

static TmdbShow DoShowLookup(const std::string& title, const std::string& key)
{
    std::string cachePath = CachePath("show", ShowKey(title.c_str()));

    std::string body = ReadFile(cachePath);
    if (IsSearchResponse(body))
        return ParseShowJson(body);

    if (g_tmdbKey[0] == 0) {
        TmdbShow s{}; s.ready = true; return s;
    }
    std::string url = "https://api.themoviedb.org/3/search/tv?api_key=";
    url += g_tmdbKey;
    url += "&query=";
    url += UrlEncode(title.c_str());
    url += "&include_adult=false";
    body = HttpGet(url);
    (void)key;
    if (!IsSearchResponse(body)) {
        TmdbShow s{}; s.ready = true; s.failed = true; return s;
    }
    WriteFile(cachePath, body);
    return ParseShowJson(body);
}


// ============================================================================
// Public API
// ============================================================================

void TMDB_Init() {}
void TMDB_Shutdown() {}

bool TMDB_HasKey()
{
    return g_tmdbKey[0] != 0;
}


TmdbMovie TMDB_LookupMovie(const char* title, int year)
{
    if (!title || !*title) { TmdbMovie m{}; m.ready = true; return m; }
    TMDB_Init();
    std::string key = MovieKey(title, year);
    {
        std::lock_guard<std::mutex> lock(g_resultMutex);
        auto it = s_movieResults.find(key);
        if (it != s_movieResults.end()) return it->second;
    }
    TmdbMovie m = DoMovieLookup(title, year, key);
    if (!m.failed) {
        std::lock_guard<std::mutex> lock(g_resultMutex);
        s_movieResults[key] = m;
    }
    return m;
}

TmdbShow TMDB_LookupShow(const char* title)
{
    if (!title || !*title) { TmdbShow s{}; s.ready = true; return s; }
    TMDB_Init();
    std::string key = ShowKey(title);
    {
        std::lock_guard<std::mutex> lock(g_resultMutex);
        auto it = s_showResults.find(key);
        if (it != s_showResults.end()) return it->second;
    }
    TmdbShow s = DoShowLookup(title, key);
    if (!s.failed) {
        std::lock_guard<std::mutex> lock(g_resultMutex);
        s_showResults[key] = s;
    }
    return s;
}


void TMDB_QueueMovieLookup(const char* title, int year)
{
    if (!title || !*title) return;
    TMDB_Init();
    std::string key = MovieKey(title, year);
    {
        std::lock_guard<std::mutex> lock(g_resultMutex);
        if (s_movieResults.count(key)) return;
        if (s_inFlight.count(key)) return;
    }
    // No key + no on-disk cache means nothing to do; skip the worker.
    if (g_tmdbKey[0] == 0) {
        struct stat st;
        if (stat(CachePath("movie", key).c_str(), &st) != 0) {
            std::lock_guard<std::mutex> lock(g_resultMutex);
            TmdbMovie m{}; m.ready = true;
            s_movieResults[key] = m;
            return;
        }
    }
    {
        std::lock_guard<std::mutex> lock(g_resultMutex);
        s_inFlight[key] = new std::atomic<bool>(true);
    }
    fprintf(stderr, "[TMDB] queue movie lookup: %s (%d) key=%s\n", title, year, key.c_str());
    std::string t = title;
    std::thread([t, year, key]() {
        fprintf(stderr, "[TMDB] worker started for %s\n", key.c_str());
        TmdbMovie m = DoMovieLookup(t, year, key);
        fprintf(stderr, "[TMDB] worker done for %s: ready=%d found=%d overview=%zu\n",
            key.c_str(), (int)m.ready, (int)m.found, m.overview.size());
        std::lock_guard<std::mutex> lock(g_resultMutex);
        if (!m.failed) s_movieResults[key] = m;
        auto it = s_inFlight.find(key);
        if (it != s_inFlight.end()) { delete it->second; s_inFlight.erase(it); }
    }).detach();
}

void TMDB_QueueShowLookup(const char* title)
{
    if (!title || !*title) return;
    TMDB_Init();
    std::string key = ShowKey(title);
    {
        std::lock_guard<std::mutex> lock(g_resultMutex);
        if (s_showResults.count(key)) return;
        if (s_inFlight.count(key)) return;
    }
    if (g_tmdbKey[0] == 0) {
        struct stat st;
        if (stat(CachePath("show", key).c_str(), &st) != 0) {
            std::lock_guard<std::mutex> lock(g_resultMutex);
            TmdbShow s{}; s.ready = true;
            s_showResults[key] = s;
            return;
        }
    }
    {
        std::lock_guard<std::mutex> lock(g_resultMutex);
        s_inFlight[key] = new std::atomic<bool>(true);
    }
    fprintf(stderr, "[TMDB] queue show lookup: %s key=%s\n", title, key.c_str());
    std::string t = title;
    std::thread([t, key]() {
        fprintf(stderr, "[TMDB] worker started for %s\n", key.c_str());
        TmdbShow s = DoShowLookup(t, key);
        fprintf(stderr, "[TMDB] worker done for %s: ready=%d found=%d overview=%zu\n",
            key.c_str(), (int)s.ready, (int)s.found, s.overview.size());
        std::lock_guard<std::mutex> lock(g_resultMutex);
        if (!s.failed) s_showResults[key] = s;
        auto it = s_inFlight.find(key);
        if (it != s_inFlight.end()) { delete it->second; s_inFlight.erase(it); }
    }).detach();
}


TmdbMovie TMDB_GetMovie(const char* title, int year)
{
    if (!title || !*title) { TmdbMovie m{}; return m; }
    std::string key = MovieKey(title, year);
    std::lock_guard<std::mutex> lock(g_resultMutex);
    auto it = s_movieResults.find(key);
    if (it != s_movieResults.end()) return it->second;
    TmdbMovie m{};
    return m;
}

TmdbShow TMDB_GetShow(const char* title)
{
    if (!title || !*title) { TmdbShow s{}; return s; }
    std::string key = ShowKey(title);
    std::lock_guard<std::mutex> lock(g_resultMutex);
    auto it = s_showResults.find(key);
    if (it != s_showResults.end()) return it->second;
    TmdbShow s{};
    return s;
}
