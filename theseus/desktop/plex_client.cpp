// plex_client.cpp: Plex Media Server HTTPS client.

#include "platform.h"
#include "app_paths.h"
#include "plex_client.h"
#include "http_util.h"
#include "json_util.h"
#include "client_util.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <map>

extern char g_plexToken[256];
extern char g_plexClientId[64];
extern void SaveDesktopSettings();


// ============================================================================
// Plex client identity
// ============================================================================


static HttpHeaders PlexHeaders(bool wantJson, bool withToken)
{
    HttpHeaders h;
    if (wantJson) h.push_back({ "Accept", "application/json" });
    h.push_back({ "X-Plex-Client-Identifier", g_plexClientId });
    h.push_back({ "X-Plex-Product",           "UIX Desktop" });
    h.push_back({ "X-Plex-Version",           "0.3" });
    h.push_back({ "X-Plex-Device-Name",       "UIX Desktop" });
#if defined(__APPLE__)
    h.push_back({ "X-Plex-Platform",          "macOS" });
#elif defined(THESEUS_HOST_WIN)
    h.push_back({ "X-Plex-Platform",          "Windows" });
#else
    h.push_back({ "X-Plex-Platform",          "Linux" });
#endif
    if (withToken && g_plexToken[0]) {
        h.push_back({ "X-Plex-Token", g_plexToken });
    }
    return h;
}


// ============================================================================
// In-flight state. One slot per fetch op, guarded by g_mtx + ready atomic.
// ============================================================================

static std::mutex g_mtx;

// PIN auth
static std::atomic<bool> s_pinInFlight{false};
static std::atomic<bool> s_pinCancel{false};
static std::string       s_pinCode;
static std::thread       s_pinThread;

// Server discovery
static std::atomic<bool>       s_serversReady{false};
static std::vector<PlexServer> s_servers;
static std::thread             s_serversThread;
static std::string             s_activeServerName;
static std::string             s_activeServerUri;

// TV drill. One worker thread per season/episode fetch; results land in the
// pre-stage cache below.
static std::thread s_seasonsThread;
static std::thread s_episodesThread;
// The scene polls the cache every frame, so a fetch already running means
// "ask again next frame", never "wait for it here".
static std::atomic<bool> s_seasonsBusy{false};
static std::atomic<bool> s_episodesBusy{false};

// Art download queue
static std::mutex                                  s_artMtx;
static std::map<std::string, std::atomic<bool>*>   s_artInFlight;

// ---------- Pre-stage cache ----------
static std::atomic<bool>                              s_syncReady{false};
static std::atomic<bool>                              s_syncRunning{false};
static std::atomic<int>                               s_syncProgress{0};   // 0..1000
static std::string                                    s_syncPhase;
static std::thread                                    s_syncThread;
// Bumped by sign out. A worker from an older generation finishes on its own
// but publishes nothing, and owns s_syncRunning until it does.
static std::atomic<unsigned>                          s_syncGen{0};
static std::vector<PlexLibrary>                       s_cacheLibs;
static std::map<std::string, std::vector<PlexItem>>   s_cacheItems;        // sectionKey -> items
static std::map<std::string, std::vector<PlexSeason>> s_cacheSeasons;      // showRk -> seasons
static std::map<std::string, std::vector<PlexEpisode>> s_cacheEpisodes;    // seasonRk -> episodes


// ============================================================================
// Init / shutdown
// ============================================================================

void Plex_Init()
{
    static bool done = false;
    if (done) return;
    done = true;

    srand((unsigned)time(NULL));
    if (g_plexClientId[0] == 0) {
        GenerateUUID(g_plexClientId);
        SaveDesktopSettings();
    }
    fprintf(stderr, "[Plex] init (token=%s, clientId=%s)\n",
            g_plexToken[0] ? "yes" : "no", g_plexClientId);
}

void Plex_Shutdown()
{
    s_pinCancel = true;
    JoinIf(s_pinThread);
    JoinIf(s_serversThread);
    JoinIf(s_seasonsThread);
    JoinIf(s_episodesThread);
    JoinIf(s_syncThread);
}

bool Plex_HasToken() { return g_plexToken[0] != 0; }

void Plex_SignOut()
{
    g_plexToken[0] = 0;
    std::lock_guard<std::mutex> lk(g_mtx);
    s_servers.clear();   s_serversReady   = false;
    s_activeServerName.clear();
    s_activeServerUri.clear();
    s_cacheLibs.clear();
    s_cacheItems.clear();
    s_cacheSeasons.clear();
    s_cacheEpisodes.clear();
    s_syncGen++;
    s_syncReady    = false;
    s_syncProgress = 0;
    s_syncPhase.clear();
}


// ============================================================================
// PIN auth flow
// ============================================================================

static void PinAuthWorker()
{
    // No strong=true. Strong pins return a 24-char token plex.tv/link rejects.
    HttpResponse r = Http_Post("https://plex.tv/api/v2/pins", "",
                               PlexHeaders(true, false));
    if (!r.ok()) {
        fprintf(stderr, "[Plex] pin create failed (status %ld)\n", r.status);
        s_pinInFlight = false;
        return;
    }
    int pinId = Json_GetInt(r.body, "id");
    std::string code = Json_GetString(r.body, "code");
    if (pinId == 0 || code.empty()) {
        fprintf(stderr, "[Plex] pin create returned bad json: %s\n", r.body.c_str());
        s_pinInFlight = false;
        return;
    }

    {
        std::lock_guard<std::mutex> lk(g_mtx);
        s_pinCode = code;
    }
    fprintf(stderr, "[Plex] pin code = %s (id %d). Visit plex.tv/link\n",
            code.c_str(), pinId);

    // Plex pins live ~15min; cap polling at 10.
    char url[256];
    snprintf(url, sizeof(url), "https://plex.tv/api/v2/pins/%d", pinId);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(10);
    while (std::chrono::steady_clock::now() < deadline) {
        if (s_pinCancel) break;
        std::this_thread::sleep_for(std::chrono::seconds(2));
        if (s_pinCancel) break;

        HttpResponse p = Http_Get(url, PlexHeaders(true, false));
        if (!p.ok()) continue;
        std::string tok = Json_GetString(p.body, "authToken");
        if (tok.empty() || tok == "null") continue;

        strncpy(g_plexToken, tok.c_str(), sizeof(g_plexToken) - 1);
        g_plexToken[sizeof(g_plexToken) - 1] = 0;
        SaveDesktopSettings();
        fprintf(stderr, "[Plex] token acquired\n");
        break;
    }

    {
        std::lock_guard<std::mutex> lk(g_mtx);
        s_pinCode.clear();
    }
    s_pinInFlight = false;
}

void Plex_StartPinAuth()
{
    Plex_Init();
    if (s_pinInFlight) return;
    JoinIf(s_pinThread);
    s_pinCancel   = false;
    s_pinInFlight = true;
    s_pinThread   = std::thread(PinAuthWorker);
}

void Plex_CancelPinAuth()
{
    s_pinCancel = true;
}

bool Plex_PinAuthInFlight() { return s_pinInFlight; }

std::string Plex_GetPinCode()
{
    std::lock_guard<std::mutex> lk(g_mtx);
    return s_pinCode;
}


// ============================================================================
// Server discovery
// ============================================================================

// Prefer local URIs, fall back to first https, then first http.
static std::string PickConnection(const std::string& serverJson)
{
    size_t arr = Json_FindArray(serverJson, "connections");
    if (arr == std::string::npos) return "";
    std::vector<std::string> conns = Json_SplitArray(serverJson, arr);
    std::string firstHttps, firstHttp;
    for (const auto& c : conns) {
        std::string uri      = Json_GetString(c, "uri");
        std::string addr     = Json_GetString(c, "address");
        // "local": true is the most reliable signal.
        size_t lk = Json_FindKey(c, "local");
        bool isLocal = (lk != std::string::npos &&
                        strncmp(c.c_str() + lk, "true", 4) == 0);
        if (isLocal && !uri.empty()) return uri;
        if (firstHttps.empty() && uri.rfind("https://", 0) == 0) firstHttps = uri;
        if (firstHttp.empty()  && uri.rfind("http://",  0) == 0) firstHttp  = uri;
    }
    if (!firstHttps.empty()) return firstHttps;
    return firstHttp;
}

static void ServersWorker()
{
    const unsigned gen = s_syncGen;
    HttpResponse r = Http_Get(
        "https://plex.tv/api/v2/resources?includeHttps=1&includeRelay=1",
        PlexHeaders(true, true));
    std::vector<PlexServer> servers;
    if (r.ok()) {
        // Top level is an array; wrap so the splitter finds the '['.
        std::string wrapped = "{\"resources\":" + r.body + "}";
        size_t arr = Json_FindArray(wrapped, "resources");
        if (arr != std::string::npos) {
            for (const auto& obj : Json_SplitArray(wrapped, arr)) {
                std::string product = Json_GetString(obj, "product");
                if (product != "Plex Media Server") continue;
                PlexServer s;
                s.name     = Json_GetString(obj, "name");
                s.clientId = Json_GetString(obj, "clientIdentifier");
                s.uri      = PickConnection(obj);
                if (!s.uri.empty()) servers.push_back(s);
            }
        }
    } else {
        fprintf(stderr, "[Plex] resources fetch failed (status %ld)\n", r.status);
    }

    size_t n = servers.size();
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        if (gen != s_syncGen) return;   // signed out meanwhile
        s_servers = std::move(servers);
        s_activeServerName = s_servers.empty() ? std::string() : s_servers[0].name;
        s_activeServerUri  = s_servers.empty() ? std::string() : s_servers[0].uri;
    }
    s_serversReady = true;
    fprintf(stderr, "[Plex] discovered %zu server(s)\n", n);
}

std::string Plex_GetActiveServerName()
{
    std::lock_guard<std::mutex> lk(g_mtx);
    return s_activeServerName;
}

void Plex_FetchServers()
{
    Plex_Init();
    JoinIf(s_serversThread);
    s_serversReady = false;
    s_serversThread = std::thread(ServersWorker);
}

std::string Plex_GetActiveServerUri()
{
    std::lock_guard<std::mutex> lk(g_mtx);
    return s_activeServerUri;
}

std::vector<PlexServer> Plex_GetServers()
{
    std::lock_guard<std::mutex> lk(g_mtx);
    return s_servers;
}

bool Plex_ServersReady() { return s_serversReady; }


// "{serverUri}{path}?X-Plex-Token={token}" for thumb / art / stream URLs.
static std::string BuildAssetUrl(const std::string& serverUri,
                                 const std::string& path)
{
    if (path.empty()) return "";
    std::string out = serverUri + path;
    out += (path.find('?') == std::string::npos) ? "?" : "&";
    out += "X-Plex-Token=";
    out += g_plexToken;
    return out;
}

// ============================================================================
// TV drill: /library/metadata/{rk}/children returns seasons or episodes.
// ============================================================================

static void SeasonsWorker(std::string serverUri, std::string showRk)
{
    std::string url = serverUri + "/library/metadata/" + showRk + "/children";
    HttpResponse r = Http_Get(url, PlexHeaders(true, true));
    std::vector<PlexSeason> seasons;
    if (r.ok()) {
        size_t arr = Json_FindArray(r.body, "Metadata");
        if (arr != std::string::npos) {
            for (const auto& obj : Json_SplitArray(r.body, arr)) {
                // Skip Plex's synthetic "All episodes" pseudo-season (idx 0).
                int idx = Json_GetInt(obj, "index");
                if (idx <= 0) continue;
                PlexSeason s;
                s.ratingKey = Json_GetString(obj, "ratingKey");
                s.title     = Json_GetString(obj, "title");
                char ibuf[8]; snprintf(ibuf, sizeof(ibuf), "%d", idx);
                s.index     = ibuf;
                s.thumbUrl  = BuildAssetUrl(serverUri, Json_GetString(obj, "thumb"));
                if (!s.ratingKey.empty()) seasons.push_back(s);
            }
        }
    } else {
        fprintf(stderr, "[Plex] seasons fetch failed (status %ld)\n", r.status);
    }

    size_t n = seasons.size();
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        s_cacheSeasons[showRk] = std::move(seasons);
    }
    fprintf(stderr, "[Plex] %zu season(s) under show %s\n", n, showRk.c_str());
    s_seasonsBusy = false;
}

void Plex_FetchSeasons(const std::string& serverUri, const std::string& showRatingKey)
{
    Plex_Init();
    if (s_seasonsBusy.exchange(true)) return;
    JoinIf(s_seasonsThread);   // already finished, so this is instant
    s_seasonsThread = std::thread(SeasonsWorker, serverUri, showRatingKey);
}

static void EpisodesWorker(std::string serverUri, std::string seasonRk)
{
    std::string url = serverUri + "/library/metadata/" + seasonRk + "/children";
    HttpResponse r = Http_Get(url, PlexHeaders(true, true));
    std::vector<PlexEpisode> episodes;
    if (r.ok()) {
        size_t arr = Json_FindArray(r.body, "Metadata");
        if (arr != std::string::npos) {
            for (const auto& obj : Json_SplitArray(r.body, arr)) {
                PlexEpisode e;
                e.ratingKey = Json_GetString(obj, "ratingKey");
                e.title     = Json_GetString(obj, "title");
                int idx     = Json_GetInt(obj, "index");
                if (idx > 0) {
                    char ibuf[8]; snprintf(ibuf, sizeof(ibuf), "%d", idx);
                    e.index = ibuf;
                }
                e.summary   = Json_GetString(obj, "summary");
                e.thumbUrl  = BuildAssetUrl(serverUri, Json_GetString(obj, "thumb"));
                if (!e.ratingKey.empty()) episodes.push_back(e);
            }
        }
    } else {
        fprintf(stderr, "[Plex] episodes fetch failed (status %ld)\n", r.status);
    }

    size_t n = episodes.size();
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        s_cacheEpisodes[seasonRk] = std::move(episodes);
    }
    fprintf(stderr, "[Plex] %zu episode(s) under season %s\n", n, seasonRk.c_str());
    s_episodesBusy = false;
}

void Plex_FetchEpisodes(const std::string& serverUri, const std::string& seasonRatingKey)
{
    Plex_Init();
    if (s_episodesBusy.exchange(true)) return;
    JoinIf(s_episodesThread);
    s_episodesThread = std::thread(EpisodesWorker, serverUri, seasonRatingKey);
}


// ============================================================================
// Stream URL resolution. Direct-play only; Plex transcoder negotiation skipped.
// ============================================================================

std::string Plex_ResolveStreamUrl(const std::string& serverUri,
                                  const std::string& ratingKey)
{
    if (serverUri.empty() || ratingKey.empty() || g_plexToken[0] == 0) return "";

    std::string url = serverUri + "/library/metadata/" + ratingKey;
    HttpResponse r = Http_Get(url, PlexHeaders(true, true));
    if (!r.ok()) {
        fprintf(stderr, "[Plex] metadata fetch failed for %s (status %ld)\n",
                ratingKey.c_str(), r.status);
        return "";
    }

    // First Media.Part[0] is the direct-play candidate; no alternate walk.
    size_t mArr = Json_FindArray(r.body, "Media");
    if (mArr == std::string::npos) return "";
    auto medias = Json_SplitArray(r.body, mArr);
    if (medias.empty()) return "";

    size_t pArr = Json_FindArray(medias[0], "Part");
    if (pArr == std::string::npos) return "";
    auto parts = Json_SplitArray(medias[0], pArr);
    if (parts.empty()) return "";

    std::string partKey = Json_GetString(parts[0], "key");
    if (partKey.empty()) return "";

    std::string out = serverUri + partKey;
    out += (partKey.find('?') == std::string::npos) ? "?" : "&";
    out += "X-Plex-Token=";
    out += g_plexToken;
    return out;
}


// ============================================================================
// Pre-stage cache walker. Eager: servers + libraries + items. Seasons +
// episodes stay lazy (the fanout is too wide to walk on every sign-in).
// ============================================================================

static void SetSyncPhase(unsigned gen, const std::string& phase, int progressOf1000)
{
    std::lock_guard<std::mutex> lk(g_mtx);
    if (gen != s_syncGen) return;
    s_syncPhase    = phase;
    s_syncProgress = progressOf1000;
}

// Marks the sync done, unless a sign out made this run stale.
static void FinishSync(unsigned gen, const char* phase)
{
    SetSyncPhase(gen, phase, 1000);
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        if (gen == s_syncGen) s_syncReady = true;
    }
    s_syncRunning = false;
}

static void SyncWorker()
{
    const unsigned gen = s_syncGen;
    s_syncReady = false;

    SetSyncPhase(gen, "Finding Plex server", 0);
    Plex_FetchServers();
    while (!Plex_ServersReady()) {
        if (gen != s_syncGen) { s_syncRunning = false; return; }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::vector<PlexServer> servers = Plex_GetServers();
    if (servers.empty()) {
        // Signed in but nothing came back from plex.tv. Distinct from "signed
        // in and the server has no libraries", and the UI shows both as 0.
        fprintf(stderr, "[Plex] no servers returned for this account/token\n");
        FinishSync(gen, "No Plex servers reachable");
        return;
    }
    // Ask each server in turn rather than betting on the first. An account
    // can list a server that's simply unreachable from here (someone else's
    // share, a LAN address while you're remote, a machine that's off), and
    // taking servers[0] on faith renders as "0 libraries" with no complaint.
    SetSyncPhase(gen, "Loading library list", 50);
    std::string serverUri;
    HttpResponse r;
    std::vector<PlexLibrary> libs;

    for (size_t si = 0; si < servers.size(); si++) {
        r = Http_Get(servers[si].uri + "/library/sections", PlexHeaders(true, true));
        if (!r.ok()) {
            fprintf(stderr, "[Plex] %s unreachable (status %ld), trying next\n",
                    servers[si].name.c_str(), r.status);
            continue;
        }
        serverUri = servers[si].uri;
        {
            std::lock_guard<std::mutex> lk(g_mtx);
            if (gen == s_syncGen) {
                s_activeServerName = servers[si].name;
                s_activeServerUri  = servers[si].uri;
            }
        }
        fprintf(stderr, "[Plex] using %s (%s)\n",
                servers[si].name.c_str(), serverUri.c_str());
        break;
    }

    if (serverUri.empty()) {
        fprintf(stderr, "[Plex] none of the %zu server(s) answered\n", servers.size());
        FinishSync(gen, "No Plex servers reachable");
        return;
    }

    if (r.ok()) {
        size_t arr = Json_FindArray(r.body, "Directory");
        if (arr != std::string::npos) {
            for (const auto& obj : Json_SplitArray(r.body, arr)) {
                PlexLibrary lib;
                lib.sectionKey = Json_GetString(obj, "key");
                lib.title      = Json_GetString(obj, "title");
                lib.type       = Json_GetString(obj, "type");
                if (!lib.sectionKey.empty()) libs.push_back(lib);
            }
        }
    }

    std::map<std::string, std::vector<PlexItem>> itemsByKey;
    const int libCount = (int)libs.size();
    for (int i = 0; i < libCount; i++) {
        const PlexLibrary& lib = libs[i];
        char phaseBuf[128];
        snprintf(phaseBuf, sizeof(phaseBuf),
                 "Loading %s (%d/%d)", lib.title.c_str(), i + 1, libCount);
        SetSyncPhase(gen, phaseBuf, 100 + (i * 900) / (libCount > 0 ? libCount : 1));

        std::string url = serverUri + "/library/sections/" + lib.sectionKey + "/all";
        HttpResponse ir = Http_Get(url, PlexHeaders(true, true));
        std::vector<PlexItem> items;
        if (ir.ok()) {
            size_t mArr = Json_FindArray(ir.body, "Metadata");
            if (mArr != std::string::npos) {
                for (const auto& obj : Json_SplitArray(ir.body, mArr)) {
                    PlexItem it;
                    it.ratingKey = Json_GetString(obj, "ratingKey");
                    it.title     = Json_GetString(obj, "title");
                    int y        = Json_GetInt(obj, "year");
                    if (y > 0) {
                        char ybuf[8]; snprintf(ybuf, sizeof(ybuf), "%d", y);
                        it.year = ybuf;
                    }
                    it.summary   = Json_GetString(obj, "summary");
                    it.thumbUrl  = BuildAssetUrl(serverUri, Json_GetString(obj, "thumb"));
                    it.artUrl    = BuildAssetUrl(serverUri, Json_GetString(obj, "art"));
                    it.type      = Json_GetString(obj, "type");
                    if (!it.ratingKey.empty()) items.push_back(it);
                }
            }
        }
        itemsByKey[lib.sectionKey] = std::move(items);
    }

    // Publish atomically. Readers see old snapshot or whole new one.
    size_t nLibs = libs.size();
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        if (gen == s_syncGen) {
            s_cacheLibs  = std::move(libs);
            s_cacheItems = std::move(itemsByKey);
        }
    }

    FinishSync(gen, "Plex library ready");
    fprintf(stderr, "[Plex] sync complete (%zu libraries)\n", nLibs);
}

void Plex_StartSync()
{
    Plex_Init();
    if (!Plex_HasToken()) return;
    if (s_syncRunning) return;
    if (s_syncReady)   return;
    JoinIf(s_syncThread);
    s_syncRunning = true;
    s_syncThread  = std::thread(SyncWorker);
}

bool        Plex_SyncReady()    { return s_syncReady; }
int         Plex_SyncProgress() { return s_syncProgress; }
std::string Plex_SyncPhase()
{
    std::lock_guard<std::mutex> lk(g_mtx);
    return s_syncPhase;
}

std::vector<PlexLibrary> Plex_Cache_GetLibraries()
{
    std::lock_guard<std::mutex> lk(g_mtx);
    return s_cacheLibs;
}

std::vector<PlexItem> Plex_Cache_GetItems(const std::string& sectionKey)
{
    std::lock_guard<std::mutex> lk(g_mtx);
    auto it = s_cacheItems.find(sectionKey);
    if (it == s_cacheItems.end()) return {};
    return it->second;
}

bool Plex_Cache_GetSeasons(const std::string& showRatingKey,
                           std::vector<PlexSeason>& out)
{
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        auto it = s_cacheSeasons.find(showRatingKey);
        if (it != s_cacheSeasons.end()) {
            out = it->second;
            return true;
        }
    }
    // Not cached, fire a fetch; SeasonsWorker promotes the result.
    // Same server the library came from, not just the first one listed.
    std::string uri = Plex_GetActiveServerUri();
    if (!uri.empty()) Plex_FetchSeasons(uri, showRatingKey);
    return false;
}

bool Plex_Cache_GetEpisodes(const std::string& seasonRatingKey,
                            std::vector<PlexEpisode>& out)
{
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        auto it = s_cacheEpisodes.find(seasonRatingKey);
        if (it != s_cacheEpisodes.end()) {
            out = it->second;
            return true;
        }
    }
    std::string uri = Plex_GetActiveServerUri();
    if (!uri.empty()) Plex_FetchEpisodes(uri, seasonRatingKey);
    return false;
}


// ============================================================================
// Art cache
// ============================================================================


// Host path: curl writes here. Lives at Library/Plex/<rk>.jpg next to binary.
static std::string Plex_ArtCacheHostPath(const std::string& ratingKey)
{
    Mkdir_(AppPath_Tree("Library"));
    Mkdir_(AppPath_Tree("Library/Plex"));
    return std::string(AppPath_Tree("Library/Plex")) + "/" + ratingKey + ".jpg";
}

// XBox-style path for the XAP. xboxfs routes E:\ -> Library/.
std::string Plex_ArtCachePath(const std::string& ratingKey)
{
    if (!Http_IsSafeFileId(ratingKey)) return "";
    return "E:\\Plex\\" + ratingKey + ".jpg";
}

static void ArtWorker(std::string ratingKey, std::string url,
                      std::atomic<bool>* flag)
{
    std::string path = Plex_ArtCacheHostPath(ratingKey);
    struct stat st;
    if (stat(path.c_str(), &st) != 0 || st.st_size == 0) {
        if (!Http_GetToFile(url, path)) {
            fprintf(stderr, "[Plex] art fetch failed for %s\n", ratingKey.c_str());
        }
    }
    {
        std::lock_guard<std::mutex> lk(s_artMtx);
        s_artInFlight.erase(ratingKey);
    }
    delete flag;
}

void Plex_QueueArtDownload(const std::string& ratingKey, const std::string& url)
{
    // The key comes off the server and ends up in a file path.
    if (!Http_IsSafeFileId(ratingKey) || url.empty()) return;
    // Workers erase from the map on other threads, so it is only touched under the lock.
    std::atomic<bool>* flag;
    {
        std::lock_guard<std::mutex> lk(s_artMtx);
        if (s_artInFlight.count(ratingKey)) return;
        flag = new std::atomic<bool>(false);
        s_artInFlight[ratingKey] = flag;
    }
    std::thread(ArtWorker, ratingKey, url, flag).detach();
}
