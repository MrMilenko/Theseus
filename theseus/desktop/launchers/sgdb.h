// launchers/sgdb.h: SteamGridDB API client + settings persistence for the
// Title Maker "SteamGridDB Icons..." picker. Pure data/network layer, no
// ImGui - title_maker.cpp owns the picker dialog UI, same split as
// launchers/emulators.h and launchers/retroarch.h.

#pragma once

#include "http_util.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

struct SGDBGameMatch {
    long id;
    char name[256];
};

struct SGDBAsset {
    long id;
    char url[512];       // full-resolution image
    char thumbUrl[512];  // smaller preview; falls back to url if absent
};

// ============================================================================
// Settings persistence (Configs/sgdb.ini)
// ============================================================================

struct SGDBSettings {
    char apiKey[128];
    int  assetType;    // 0 = icons (square), 1 = grids (boxart-style)
    int  optionCount;  // how many thumbnails the picker fetches; 0 = disabled
};

static inline void SGDB_LoadSettings(SGDBSettings* s) {
    s->apiKey[0] = 0;
    s->assetType = 0;
    s->optionCount = 5;

    FILE* fp = fopen(AppPath("Configs/sgdb.ini"), "r");
    if (!fp) return;
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char* nl = strchr(line, '\n'); if (nl) *nl = 0;
        char* cr = strchr(line, '\r'); if (cr) *cr = 0;
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        const char* val = eq + 1;
        if (strcmp(line, "api_key") == 0) {
            strncpy(s->apiKey, val, sizeof(s->apiKey) - 1);
            s->apiKey[sizeof(s->apiKey) - 1] = 0;
        } else if (strcmp(line, "asset_type") == 0) {
            s->assetType = atoi(val);
        } else if (strcmp(line, "option_count") == 0) {
            s->optionCount = atoi(val);
        }
    }
    fclose(fp);
}

static inline void SGDB_SaveSettings(const SGDBSettings* s) {
    FILE* fp = fopen(AppPath("Configs/sgdb.ini"), "w");
    if (!fp) return;
    fprintf(fp, "api_key=%s\n", s->apiKey);
    fprintf(fp, "asset_type=%d\n", s->assetType);
    fprintf(fp, "option_count=%d\n", s->optionCount);
    fclose(fp);
}

// ============================================================================
// Tiny JSON field scanner
//
// SGDB's responses are a flat "data" array of mostly-flat objects. Rather
// than pull in a JSON library for three field lookups, split the top-level
// array into per-object substrings via bracket-depth matching (this
// correctly skips over nested sub-objects like "author" without needing to
// understand them) and do simple per-object key lookups - same approach
// the rest of this codebase already uses for ACF/PARAM.SFO/games.yml.
// ============================================================================

static inline bool SGDB_JsonSuccess(const std::string& json) {
    return json.find("\"success\":true") != std::string::npos;
}

static inline int SGDB_JsonSplitObjects(const std::string& json, std::vector<std::string>& out, int maxObjs) {
    size_t dataPos = json.find("\"data\"");
    if (dataPos == std::string::npos) return 0;
    size_t arrStart = json.find('[', dataPos);
    if (arrStart == std::string::npos) return 0;

    int depth = 0;
    size_t objStart = std::string::npos;
    for (size_t i = arrStart; i < json.size() && (int)out.size() < maxObjs; i++) {
        char c = json[i];
        if (c == '{') {
            if (depth == 0) objStart = i;
            depth++;
        } else if (c == '}') {
            depth--;
            if (depth == 0 && objStart != std::string::npos) {
                out.push_back(json.substr(objStart, i - objStart + 1));
                objStart = std::string::npos;
            }
        } else if (c == ']' && depth == 0) {
            break;
        }
    }
    return (int)out.size();
}

static inline bool SGDB_JsonGetString(const std::string& obj, const char* key, char* out, size_t outSize) {
    std::string needle = std::string("\"") + key + "\":\"";
    size_t p = obj.find(needle);
    if (p == std::string::npos) { out[0] = 0; return false; }
    p += needle.size();

    // Unescape as we copy - this matters a lot for URLs specifically:
    // JSON encoders (SGDB included) commonly escape forward slashes as
    // "\/", so a raw copy turns "https://host/path" into the literal,
    // un-fetchable string "https:\/\/host\/path". Handles the standard
    // JSON escapes plus \uXXXX (BMP only - no surrogate-pair handling,
    // which is fine for game names/URLs but wouldn't be for emoji).
    size_t op = 0, i = p;
    while (i < obj.size() && obj[i] != '"' && op + 1 < outSize) {
        if (obj[i] == '\\' && i + 1 < obj.size()) {
            char esc = obj[i + 1];
            if (esc == 'u' && i + 5 < obj.size()) {
                char hex[5] = { obj[i+2], obj[i+3], obj[i+4], obj[i+5], 0 };
                unsigned cp = (unsigned)strtoul(hex, NULL, 16);
                if (cp < 0x80) {
                    if (op + 1 < outSize) out[op++] = (char)cp;
                } else if (cp < 0x800) {
                    if (op + 2 < outSize) {
                        out[op++] = (char)(0xC0 | (cp >> 6));
                        out[op++] = (char)(0x80 | (cp & 0x3F));
                    }
                } else {
                    if (op + 3 < outSize) {
                        out[op++] = (char)(0xE0 | (cp >> 12));
                        out[op++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                        out[op++] = (char)(0x80 | (cp & 0x3F));
                    }
                }
                i += 6;
                continue;
            }
            char actual;
            switch (esc) {
                case '/':  actual = '/';  break;
                case '\\': actual = '\\'; break;
                case '"':  actual = '"';  break;
                case 'n':  actual = '\n'; break;
                case 't':  actual = '\t'; break;
                case 'r':  actual = '\r'; break;
                default:   actual = esc;  break;
            }
            out[op++] = actual;
            i += 2;
        } else {
            out[op++] = obj[i];
            i++;
        }
    }
    out[op] = 0;
    return true;
}

static inline bool SGDB_JsonGetLong(const std::string& obj, const char* key, long* out) {
    std::string needle = std::string("\"") + key + "\":";
    size_t p = obj.find(needle);
    if (p == std::string::npos) return false;
    p += needle.size();
    if (obj.compare(p, 4, "null") == 0) return false;
    *out = strtol(obj.c_str() + p, NULL, 10);
    return true;
}

// ============================================================================
// URL encoding for the search query text
// ============================================================================

static inline void SGDB_UrlEncode(const char* in, char* out, size_t outSize) {
    static const char* hex = "0123456789ABCDEF";
    size_t op = 0;
    if (outSize == 0) return;
    for (const char* p = in; *p && op + 4 < outSize; p++) {
        unsigned char c = (unsigned char)*p;
        bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                 || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
        if (safe) out[op++] = (char)c;
        else if (c == ' ') out[op++] = '+';
        else {
            out[op++] = '%';
            out[op++] = hex[c >> 4];
            out[op++] = hex[c & 0xF];
        }
    }
    out[op] = 0;
}

// ============================================================================
// Debug logging - prints every SGDB request/response to stderr so a bad
// request (wrong param, unexpected body shape, etc.) is visible in the
// terminal instead of just showing up as an opaque status code in the UI.
// Low frequency by nature (one call per game confirm/pick), so unlike the
// music scanner this doesn't need an opt-in gate.
// ============================================================================

static inline void SGDB_DebugLog(const char* label, const std::string& url, const HttpResponse& resp) {
    fprintf(stderr, "[SGDB] %s: GET %s\n", label, url.c_str());
    fprintf(stderr, "[SGDB] %s: status=%ld body(%zu bytes)=%s%s\n", label, resp.status, resp.body.size(),
            resp.body.substr(0, 400).c_str(), resp.body.size() > 400 ? "...(truncated)" : "");
}

// ============================================================================
// API calls
// ============================================================================

// Search for a game by name. Results come back pre-ranked by relevance, so
// index 0 is almost always the right pick - the caller only needs to show
// a disambiguation combo when there's genuine ambiguity to resolve.
static inline int SGDB_Search(const char* apiKey, const char* query,
                              SGDBGameMatch* out, int maxMatches, long* outStatus = NULL) {
    char encoded[512];
    SGDB_UrlEncode(query, encoded, sizeof(encoded));
    std::string url = std::string("https://www.steamgriddb.com/api/v2/search/autocomplete/") + encoded;

    HttpHeaders headers = { { "Authorization", std::string("Bearer ") + apiKey } };
    HttpResponse resp = Http_Get(url, headers);
    SGDB_DebugLog("search", url, resp);
    if (outStatus) *outStatus = resp.status;
    if (!resp.ok() || !SGDB_JsonSuccess(resp.body)) return 0;

    std::vector<std::string> objs;
    SGDB_JsonSplitObjects(resp.body, objs, maxMatches);

    int count = 0;
    for (size_t i = 0; i < objs.size() && count < maxMatches; i++) {
        long id = 0;
        if (!SGDB_JsonGetLong(objs[i], "id", &id)) continue;
        out[count].id = id;
        SGDB_JsonGetString(objs[i], "name", out[count].name, sizeof(out[count].name));
        count++;
    }
    fprintf(stderr, "[SGDB] search: parsed %d match(es) from %zu object(s)\n", count, objs.size());
    return count;
}

// Fetch icon or grid assets for a resolved SGDB game id.
// assetType: 0 = icons (square, matches the dashboard tile aesthetic),
//            1 = grids (taller boxart-style).
// Returns asset count (<=maxAssets), best-scored first (SGDB's default sort).
// Restricted to PNG/JPEG via the `mimes` query param - SGDB serves some
// assets as WebP, which stb_image (used to decode these client-side) can't
// read at all, silently producing a blank/undecoded thumbnail otherwise.
static inline int SGDB_GetAssets(const char* apiKey, long gameId, int assetType,
                                 SGDBAsset* out, int maxAssets, long* outStatus = NULL) {
    const char* endpoint = (assetType == 1) ? "grids" : "icons";
    // icons are only ever served as PNG/ICO on SGDB - JPEG isn't a valid
    // filter value there and gets rejected with a 400. Grids allow either.
    const char* mimes = (assetType == 1) ? "image/png,image/jpeg" : "image/png";
    char urlBuf[300];
    snprintf(urlBuf, sizeof(urlBuf), "https://www.steamgriddb.com/api/v2/%s/game/%ld?mimes=%s",
             endpoint, gameId, mimes);
    std::string url = urlBuf;

    HttpHeaders headers = { { "Authorization", std::string("Bearer ") + apiKey } };
    HttpResponse resp = Http_Get(url, headers);
    SGDB_DebugLog("assets(filtered)", url, resp);
    if (resp.status == 400) {
        // Guessed wrong about an accepted mimes value for this endpoint -
        // fall back to an unfiltered request rather than hard-failing.
        // stb_image's inability to decode WebP still gets caught by the
        // caller's decoded==0 check, just without pre-filtering here.
        char plainUrlBuf[256];
        snprintf(plainUrlBuf, sizeof(plainUrlBuf), "https://www.steamgriddb.com/api/v2/%s/game/%ld", endpoint, gameId);
        std::string plainUrl = plainUrlBuf;
        resp = Http_Get(plainUrl, headers);
        SGDB_DebugLog("assets(unfiltered retry)", plainUrl, resp);
    }
    if (outStatus) *outStatus = resp.status;
    if (!resp.ok() || !SGDB_JsonSuccess(resp.body)) return 0;

    std::vector<std::string> objs;
    SGDB_JsonSplitObjects(resp.body, objs, maxAssets);

    int count = 0;
    for (size_t i = 0; i < objs.size() && count < maxAssets; i++) {
        char urlOut[512] = "", thumbBuf[512] = "";
        if (!SGDB_JsonGetString(objs[i], "url", urlOut, sizeof(urlOut))) continue;
        SGDB_JsonGetString(objs[i], "thumb", thumbBuf, sizeof(thumbBuf));

        long id = 0;
        SGDB_JsonGetLong(objs[i], "id", &id);

        out[count].id = id;
        strncpy(out[count].url, urlOut, sizeof(out[count].url) - 1);
        out[count].url[sizeof(out[count].url) - 1] = 0;
        strncpy(out[count].thumbUrl, thumbBuf[0] ? thumbBuf : urlOut, sizeof(out[count].thumbUrl) - 1);
        out[count].thumbUrl[sizeof(out[count].thumbUrl) - 1] = 0;
        count++;
    }
    fprintf(stderr, "[SGDB] assets: parsed %d asset(s) from %zu object(s)\n", count, objs.size());
    return count;
}
