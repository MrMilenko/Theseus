// tmdb.cpp: in-process TMDB v3 client. libcurl HTTPS GETs, slug-keyed
// JSON cache at Library/TMDB/. Targeted field extractor instead of a
// full JSON parser -- TMDB shapes are stable and we want ~5 fields.

#include "tmdb.h"

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

extern char g_tmdbKey[128];   // defined in sdl_main.cpp; loaded from desktop.ini

static std::mutex                       g_resultMutex;
static std::map<std::string, TmdbMovie> s_movieResults;
static std::map<std::string, TmdbShow>  s_showResults;
static std::map<std::string, std::atomic<bool>*> s_inFlight;
static bool                             s_initialized = false;


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

// Portable mkdir wrapper. POSIX mkdir takes (path, mode); MinGW's
// <sys/stat.h> redeclares it as the MS-CRT one-arg variant.
static inline int Mkdir(const char* path)
{
#ifdef _WIN32
    return mkdir(path);
#else
    return mkdir(path, 0755);
#endif
}

static std::string CachePath(const char* prefix, const std::string& slug)
{
    std::string p = "Library/TMDB/";
    Mkdir("Library");
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


// ============================================================================
// Targeted JSON field extraction.
// We never need full nested parsing. Just grab the first occurrence of
// "key": "..." or "key": N. Greedy enough for TMDB's flat result shapes.
// ============================================================================

// Find "key": then return offset of the value start, or std::string::npos.
static size_t FindKey(const std::string& json, const char* key)
{
    std::string needle = "\"";
    needle += key;
    needle += "\"";
    size_t k = json.find(needle);
    if (k == std::string::npos) return std::string::npos;
    size_t c = json.find(':', k + needle.size());
    if (c == std::string::npos) return std::string::npos;
    c++;
    while (c < json.size() && (json[c] == ' ' || json[c] == '\t')) c++;
    return c;
}

// Decode \" \\ \/ \n \t \uXXXX (latin-1 only); enough for TMDB overview text.
static std::string DecodeJsonString(const char* s, size_t len)
{
    std::string out;
    out.reserve(len);
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if (c != '\\' || i + 1 >= len) { out.push_back(c); continue; }
        char n = s[++i];
        switch (n) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'u': {
                if (i + 4 < len) {
                    char hex[5] = { s[i+1], s[i+2], s[i+3], s[i+4], 0 };
                    int code = (int)strtol(hex, NULL, 16);
                    i += 4;
                    if (code < 0x80) out.push_back((char)code);
                    else if (code < 0x800) {
                        out.push_back((char)(0xC0 | (code >> 6)));
                        out.push_back((char)(0x80 | (code & 0x3F)));
                    } else {
                        out.push_back((char)(0xE0 | (code >> 12)));
                        out.push_back((char)(0x80 | ((code >> 6) & 0x3F)));
                        out.push_back((char)(0x80 | (code & 0x3F)));
                    }
                }
                break;
            }
            default: out.push_back(n); break;
        }
    }
    return out;
}

static std::string GetString(const std::string& json, const char* key)
{
    size_t v = FindKey(json, key);
    if (v == std::string::npos || v >= json.size() || json[v] != '"') return "";
    v++;
    size_t end = v;
    while (end < json.size()) {
        if (json[end] == '\\' && end + 1 < json.size()) { end += 2; continue; }
        if (json[end] == '"') break;
        end++;
    }
    if (end >= json.size()) return "";
    return DecodeJsonString(json.data() + v, end - v);
}

static int GetInt(const std::string& json, const char* key)
{
    size_t v = FindKey(json, key);
    if (v == std::string::npos) return 0;
    return (int)strtol(json.c_str() + v, NULL, 10);
}

static float GetFloat(const std::string& json, const char* key)
{
    size_t v = FindKey(json, key);
    if (v == std::string::npos) return 0.0f;
    return (float)strtod(json.c_str() + v, NULL);
}

// First object in "results": [{...}, ...] — returns substring or empty.
static std::string GetFirstResult(const std::string& json)
{
    size_t r = json.find("\"results\"");
    if (r == std::string::npos) return "";
    size_t lb = json.find('[', r);
    if (lb == std::string::npos) return "";
    size_t ob = json.find('{', lb);
    if (ob == std::string::npos) return "";
    int depth = 0;
    size_t i = ob;
    for (; i < json.size(); i++) {
        if (json[i] == '{') depth++;
        else if (json[i] == '}') {
            depth--;
            if (depth == 0) return json.substr(ob, i - ob + 1);
        }
    }
    return "";
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
    m.title       = GetString(first, "title");
    m.overview    = GetString(first, "overview");
    m.year        = YearFromDate(GetString(first, "release_date"));
    m.posterPath  = GetString(first, "poster_path");
    m.voteAverage = GetFloat(first, "vote_average");
    m.tmdbId      = GetInt(first, "id");
    return m;
}

static TmdbShow ParseShowJson(const std::string& json)
{
    TmdbShow s{};
    s.ready = true;
    std::string first = GetFirstResult(json);
    if (first.empty()) return s;
    s.found       = true;
    s.title       = GetString(first, "name");
    s.overview    = GetString(first, "overview");
    s.year        = YearFromDate(GetString(first, "first_air_date"));
    s.posterPath  = GetString(first, "poster_path");
    s.voteAverage = GetFloat(first, "vote_average");
    s.tmdbId      = GetInt(first, "id");
    return s;
}

static TmdbMovie DoMovieLookup(const std::string& title, int year, const std::string& key)
{
    std::string cachePath = CachePath("movie", MovieKey(title.c_str(), year));

    // Try cache first.
    std::string body = ReadFile(cachePath);
    if (!body.empty())
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
    if (!body.empty()) WriteFile(cachePath, body);
    (void)key;
    return ParseMovieJson(body);
}

static TmdbShow DoShowLookup(const std::string& title, const std::string& key)
{
    std::string cachePath = CachePath("show", ShowKey(title.c_str()));

    std::string body = ReadFile(cachePath);
    if (!body.empty())
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
    if (!body.empty()) WriteFile(cachePath, body);
    (void)key;
    return ParseShowJson(body);
}


// ============================================================================
// Public API
// ============================================================================

void TMDB_Init()
{
    s_initialized = true;
}

void TMDB_Shutdown()
{
    s_initialized = false;
}

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
    {
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
    {
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
        s_movieResults[key] = m;
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
        s_showResults[key] = s;
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
