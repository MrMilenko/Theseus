// jellyfin_client.cpp: Jellyfin Media Server HTTPS client.

#include "jellyfin_client.h"
#include "http_util.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <map>
#include <sys/stat.h>

extern char g_jellyfinUrl[512];
extern char g_jellyfinToken[256];
extern char g_jellyfinUserId[64];
extern char g_jellyfinClientId[64];
extern char g_jellyfinUserName[128];
extern void SaveDesktopSettings();


// ============================================================================
// Identity + headers
// ============================================================================

static void GenerateUUID(char out[37])
{
    unsigned char b[16];
    for (int i = 0; i < 16; i++) b[i] = (unsigned char)(rand() & 0xFF);
    b[6] = (b[6] & 0x0F) | 0x40;
    b[8] = (b[8] & 0x3F) | 0x80;
    snprintf(out, 37,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        b[0],b[1],b[2],b[3], b[4],b[5], b[6],b[7],
        b[8],b[9], b[10],b[11],b[12],b[13],b[14],b[15]);
}

// Jellyfin/Emby auth header. Inherits Emby's quirky format: a single
// Authorization header with comma-separated key=value pairs.
static std::string AuthHeader(const char* token)
{
    std::string h = "MediaBrowser Client=\"UIX Desktop\", ";
    h += "Device=\"UIX Desktop\", ";
    h += "DeviceId=\"";
    h += g_jellyfinClientId;
    h += "\", Version=\"0.3\"";
    if (token && *token) {
        h += ", Token=\"";
        h += token;
        h += "\"";
    }
    return h;
}

static HttpHeaders JellyfinHeaders(const char* token)
{
    HttpHeaders h;
    h.push_back({ "Accept", "application/json" });
    h.push_back({ "Content-Type", "application/json" });
    h.push_back({ "Authorization", AuthHeader(token) });
    return h;
}


// ============================================================================
// Minimal JSON helpers (same shape as plex_client.cpp). Jellyfin's JSON keys
// are PascalCase ("AccessToken", "Name") -- pass them literally.
// ============================================================================

static size_t Json_FindKey(const std::string& s, const char* key, size_t from = 0)
{
    std::string needle = "\"";
    needle += key;
    needle += "\"";
    size_t k = s.find(needle, from);
    if (k == std::string::npos) return std::string::npos;
    size_t c = s.find(':', k + needle.size());
    if (c == std::string::npos) return std::string::npos;
    c++;
    while (c < s.size() && (s[c] == ' ' || s[c] == '\t')) c++;
    return c;
}

static std::string Json_GetString(const std::string& s, const char* key, size_t from = 0)
{
    size_t v = Json_FindKey(s, key, from);
    if (v == std::string::npos || v >= s.size() || s[v] != '"') return "";
    v++;
    size_t end = v;
    while (end < s.size()) {
        if (s[end] == '\\' && end + 1 < s.size()) { end += 2; continue; }
        if (s[end] == '"') break;
        end++;
    }
    if (end >= s.size()) return "";
    return s.substr(v, end - v);
}

static bool Json_GetBool(const std::string& s, const char* key)
{
    size_t v = Json_FindKey(s, key);
    if (v == std::string::npos) return false;
    return strncmp(s.c_str() + v, "true", 4) == 0;
}

static int Json_GetInt(const std::string& s, const char* key, size_t from = 0)
{
    size_t v = Json_FindKey(s, key, from);
    if (v == std::string::npos) return 0;
    return (int)strtol(s.c_str() + v, NULL, 10);
}

static size_t Json_FindArray(const std::string& s, const char* key)
{
    size_t v = Json_FindKey(s, key);
    if (v == std::string::npos) return std::string::npos;
    while (v < s.size() && (s[v] == ' ' || s[v] == '\t')) v++;
    if (v >= s.size() || s[v] != '[') return std::string::npos;
    return v;
}

static std::vector<std::string> Json_SplitArray(const std::string& s, size_t arrStart)
{
    std::vector<std::string> out;
    if (arrStart >= s.size() || s[arrStart] != '[') return out;
    size_t i = arrStart + 1;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\t' || s[i] == ',')) i++;
        if (i >= s.size() || s[i] == ']') break;
        if (s[i] != '{') { i++; continue; }
        size_t start = i;
        int depth = 0;
        bool inStr = false;
        for (; i < s.size(); i++) {
            char c = s[i];
            if (inStr) {
                if (c == '\\' && i + 1 < s.size()) { i++; continue; }
                if (c == '"') inStr = false;
            } else {
                if (c == '"') inStr = true;
                else if (c == '{') depth++;
                else if (c == '}') { depth--; if (depth == 0) { i++; break; } }
            }
        }
        out.push_back(s.substr(start, i - start));
    }
    return out;
}

// Pull ImageTags.Primary out of an item object. Nested object lookup.
static std::string ParsePrimaryTag(const std::string& obj)
{
    size_t tags = Json_FindKey(obj, "ImageTags");
    if (tags == std::string::npos) return "";
    return Json_GetString(obj, "Primary", tags);
}


// ============================================================================
// State
// ============================================================================

static std::mutex g_mtx;

static std::atomic<bool> s_qcInFlight{false};
static std::atomic<bool> s_qcCancel{false};
static std::string       s_qcCode;
static std::string       s_qcSecret;
static std::thread       s_qcThread;

// ---------- Pre-stage cache ----------
static std::atomic<bool>                                 s_syncReady{false};
static std::atomic<bool>                                 s_syncRunning{false};
static std::atomic<int>                                  s_syncProgress{0};
static std::string                                       s_syncPhase;
static std::thread                                       s_syncThread;
static std::vector<JellyfinLibrary>                      s_cacheLibs;
static std::map<std::string, std::vector<JellyfinItem>>  s_cacheItems;
static std::map<std::string, std::vector<JellyfinSeason>>  s_cacheSeasons;
static std::map<std::string, std::vector<JellyfinEpisode>> s_cacheEpisodes;

// ---------- Art download queue ----------
static std::mutex                                  s_artMtx;
static std::map<std::string, std::atomic<bool>*>   s_artInFlight;


// ============================================================================
// Init / shutdown
// ============================================================================

void Jellyfin_Init()
{
    static bool done = false;
    if (done) return;
    done = true;

    srand((unsigned)time(NULL));
    if (g_jellyfinClientId[0] == 0) {
        GenerateUUID(g_jellyfinClientId);
        SaveDesktopSettings();
    }
    fprintf(stderr, "[Jellyfin] init (token=%s, url=%s)\n",
            g_jellyfinToken[0] ? "yes" : "no",
            g_jellyfinUrl[0] ? g_jellyfinUrl : "(unset)");
}

static void JoinIf(std::thread& t) {
    if (t.joinable()) t.join();
}

void Jellyfin_Shutdown()
{
    s_qcCancel = true;
    JoinIf(s_qcThread);
    JoinIf(s_syncThread);
}

bool Jellyfin_HasToken() { return g_jellyfinToken[0] != 0; }

void Jellyfin_SignOut()
{
    g_jellyfinToken[0] = 0;
    g_jellyfinUserId[0] = 0;
    g_jellyfinUserName[0] = 0;
    std::lock_guard<std::mutex> lk(g_mtx);
    s_qcCode.clear();
    s_qcSecret.clear();
    s_cacheLibs.clear();
    s_cacheItems.clear();
    s_cacheSeasons.clear();
    s_cacheEpisodes.clear();
    s_syncReady    = false;
    s_syncRunning  = false;
    s_syncProgress = 0;
    s_syncPhase.clear();
}


// ============================================================================
// Server URL
// ============================================================================

std::string Jellyfin_GetServerUrl()
{
    return g_jellyfinUrl;
}

void Jellyfin_SetServerUrl(const std::string& url)
{
    std::string u = url;
    while (!u.empty() && u.back() == '/') u.pop_back();
    if (u.find("://") == std::string::npos && !u.empty()) {
        u = "http://" + u;
    }
    strncpy(g_jellyfinUrl, u.c_str(), sizeof(g_jellyfinUrl) - 1);
    g_jellyfinUrl[sizeof(g_jellyfinUrl) - 1] = 0;
    SaveDesktopSettings();
}

std::string Jellyfin_GetUserName()
{
    return g_jellyfinUserName;
}


// ============================================================================
// Quick Connect
// ============================================================================

static void QuickConnectWorker()
{
    if (g_jellyfinUrl[0] == 0) {
        fprintf(stderr, "[Jellyfin] no server URL set\n");
        s_qcInFlight = false;
        return;
    }

    // 1. POST /QuickConnect/Initiate -> { Code, Secret }
    std::string url = std::string(g_jellyfinUrl) + "/QuickConnect/Initiate";
    HttpResponse r = Http_Post(url, "", JellyfinHeaders(""));
    if (!r.ok()) {
        fprintf(stderr, "[Jellyfin] QC init failed (status %ld)\n", r.status);
        s_qcInFlight = false;
        return;
    }
    std::string code   = Json_GetString(r.body, "Code");
    std::string secret = Json_GetString(r.body, "Secret");
    if (code.empty() || secret.empty()) {
        fprintf(stderr, "[Jellyfin] QC init bad json: %s\n", r.body.c_str());
        s_qcInFlight = false;
        return;
    }

    {
        std::lock_guard<std::mutex> lk(g_mtx);
        s_qcCode   = code;
        s_qcSecret = secret;
    }
    fprintf(stderr, "[Jellyfin] QC code = %s. Approve at %s\n",
            code.c_str(), g_jellyfinUrl);

    // 2. Poll /QuickConnect/Connect?Secret=... until Authenticated == true.
    std::string pollUrl = std::string(g_jellyfinUrl) +
                          "/QuickConnect/Connect?Secret=" + secret;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(10);
    bool authenticated = false;
    while (std::chrono::steady_clock::now() < deadline) {
        if (s_qcCancel) break;
        std::this_thread::sleep_for(std::chrono::seconds(2));
        if (s_qcCancel) break;

        HttpResponse p = Http_Get(pollUrl, JellyfinHeaders(""));
        if (!p.ok()) continue;
        if (Json_GetBool(p.body, "Authenticated")) {
            authenticated = true;
            break;
        }
    }

    if (!authenticated) {
        fprintf(stderr, "[Jellyfin] QC timed out or cancelled\n");
        std::lock_guard<std::mutex> lk(g_mtx);
        s_qcCode.clear();
        s_qcSecret.clear();
        s_qcInFlight = false;
        return;
    }

    // 3. POST /Users/AuthenticateWithQuickConnect { Secret } -> user + token
    std::string body = "{\"Secret\":\"" + secret + "\"}";
    std::string authUrl = std::string(g_jellyfinUrl) +
                          "/Users/AuthenticateWithQuickConnect";
    HttpResponse a = Http_Post(authUrl, body, JellyfinHeaders(""));
    if (!a.ok()) {
        fprintf(stderr, "[Jellyfin] AuthenticateWithQuickConnect failed (status %ld)\n", a.status);
        std::lock_guard<std::mutex> lk(g_mtx);
        s_qcCode.clear();
        s_qcSecret.clear();
        s_qcInFlight = false;
        return;
    }

    std::string accessToken = Json_GetString(a.body, "AccessToken");
    // The user object is nested: "User": { "Id": "...", "Name": "..." }
    size_t userObj = Json_FindKey(a.body, "User");
    std::string userId, userName;
    if (userObj != std::string::npos) {
        userId   = Json_GetString(a.body, "Id", userObj);
        userName = Json_GetString(a.body, "Name", userObj);
    }

    if (accessToken.empty() || userId.empty()) {
        fprintf(stderr, "[Jellyfin] auth response missing fields: %s\n", a.body.c_str());
    } else {
        strncpy(g_jellyfinToken, accessToken.c_str(), sizeof(g_jellyfinToken) - 1);
        g_jellyfinToken[sizeof(g_jellyfinToken) - 1] = 0;
        strncpy(g_jellyfinUserId, userId.c_str(), sizeof(g_jellyfinUserId) - 1);
        g_jellyfinUserId[sizeof(g_jellyfinUserId) - 1] = 0;
        strncpy(g_jellyfinUserName, userName.c_str(), sizeof(g_jellyfinUserName) - 1);
        g_jellyfinUserName[sizeof(g_jellyfinUserName) - 1] = 0;
        SaveDesktopSettings();
        fprintf(stderr, "[Jellyfin] signed in as %s\n", userName.c_str());
    }

    {
        std::lock_guard<std::mutex> lk(g_mtx);
        s_qcCode.clear();
        s_qcSecret.clear();
    }
    s_qcInFlight = false;
}

void Jellyfin_StartQuickConnect()
{
    Jellyfin_Init();
    if (s_qcInFlight) return;
    JoinIf(s_qcThread);
    s_qcCancel   = false;
    s_qcInFlight = true;
    s_qcThread   = std::thread(QuickConnectWorker);
}

void Jellyfin_CancelQuickConnect()
{
    s_qcCancel = true;
}

bool Jellyfin_QuickConnectInFlight() { return s_qcInFlight; }

std::string Jellyfin_GetQuickConnectCode()
{
    std::lock_guard<std::mutex> lk(g_mtx);
    return s_qcCode;
}


// ============================================================================
// Library + item parsing
// ============================================================================

static JellyfinItem ParseItem(const std::string& obj)
{
    JellyfinItem it;
    it.id        = Json_GetString(obj, "Id");
    it.name      = Json_GetString(obj, "Name");
    it.overview  = Json_GetString(obj, "Overview");
    it.type      = Json_GetString(obj, "Type");
    it.primaryTag = ParsePrimaryTag(obj);
    int y = Json_GetInt(obj, "ProductionYear");
    if (y > 0) {
        char buf[8]; snprintf(buf, sizeof(buf), "%d", y);
        it.year = buf;
    }
    return it;
}

static std::vector<JellyfinItem> FetchItemsForLibrary(const std::string& libraryId)
{
    char url[1024];
    snprintf(url, sizeof(url),
        "%s/Users/%s/Items?ParentId=%s&Recursive=false&Fields=Overview,ProductionYear",
        g_jellyfinUrl, g_jellyfinUserId, libraryId.c_str());
    HttpResponse r = Http_Get(url, JellyfinHeaders(g_jellyfinToken));
    std::vector<JellyfinItem> out;
    if (!r.ok()) {
        fprintf(stderr, "[Jellyfin] items fetch failed for %s (status %ld)\n",
                libraryId.c_str(), r.status);
        return out;
    }
    size_t arr = Json_FindArray(r.body, "Items");
    if (arr == std::string::npos) return out;
    for (const auto& obj : Json_SplitArray(r.body, arr)) {
        JellyfinItem it = ParseItem(obj);
        if (!it.id.empty()) out.push_back(it);
    }
    return out;
}


// ============================================================================
// Sync worker (mirrors Plex_StartSync)
// ============================================================================

static void SetSyncPhase(const std::string& phase, int progressOf1000)
{
    std::lock_guard<std::mutex> lk(g_mtx);
    s_syncPhase    = phase;
    s_syncProgress = progressOf1000;
}

static void SyncWorker()
{
    s_syncReady = false;

    SetSyncPhase("Loading library list", 0);
    char url[1024];
    snprintf(url, sizeof(url), "%s/Users/%s/Views", g_jellyfinUrl, g_jellyfinUserId);
    HttpResponse r = Http_Get(url, JellyfinHeaders(g_jellyfinToken));
    std::vector<JellyfinLibrary> libs;
    if (r.ok()) {
        size_t arr = Json_FindArray(r.body, "Items");
        if (arr != std::string::npos) {
            for (const auto& obj : Json_SplitArray(r.body, arr)) {
                JellyfinLibrary lib;
                lib.id   = Json_GetString(obj, "Id");
                lib.name = Json_GetString(obj, "Name");
                lib.type = Json_GetString(obj, "CollectionType");
                if (!lib.id.empty()) libs.push_back(lib);
            }
        }
    }

    std::map<std::string, std::vector<JellyfinItem>> itemsById;
    const int libCount = (int)libs.size();
    for (int i = 0; i < libCount; i++) {
        const JellyfinLibrary& lib = libs[i];
        char phaseBuf[128];
        snprintf(phaseBuf, sizeof(phaseBuf),
                 "Loading %s (%d/%d)", lib.name.c_str(), i + 1, libCount);
        SetSyncPhase(phaseBuf, 100 + (i * 900) / (libCount > 0 ? libCount : 1));
        itemsById[lib.id] = FetchItemsForLibrary(lib.id);
    }

    {
        std::lock_guard<std::mutex> lk(g_mtx);
        s_cacheLibs  = std::move(libs);
        s_cacheItems = std::move(itemsById);
    }

    SetSyncPhase("Jellyfin library ready", 1000);
    s_syncReady   = true;
    s_syncRunning = false;
    fprintf(stderr, "[Jellyfin] sync complete (%zu libraries)\n", s_cacheLibs.size());
}

void Jellyfin_StartSync()
{
    Jellyfin_Init();
    if (!Jellyfin_HasToken()) return;
    if (s_syncRunning) return;
    if (s_syncReady)   return;
    JoinIf(s_syncThread);
    s_syncRunning = true;
    s_syncThread  = std::thread(SyncWorker);
}

bool        Jellyfin_SyncReady()    { return s_syncReady; }
int         Jellyfin_SyncProgress() { return s_syncProgress; }
std::string Jellyfin_SyncPhase()
{
    std::lock_guard<std::mutex> lk(g_mtx);
    return s_syncPhase;
}

std::vector<JellyfinLibrary> Jellyfin_Cache_GetLibraries()
{
    std::lock_guard<std::mutex> lk(g_mtx);
    return s_cacheLibs;
}

std::vector<JellyfinItem> Jellyfin_Cache_GetItems(const std::string& libraryId)
{
    std::lock_guard<std::mutex> lk(g_mtx);
    auto it = s_cacheItems.find(libraryId);
    if (it == s_cacheItems.end()) return {};
    return it->second;
}


// ============================================================================
// Seasons + episodes (lazy, fetched on first drill)
// ============================================================================

static std::vector<JellyfinSeason> FetchSeasonsForShow(const std::string& showId)
{
    char url[1024];
    snprintf(url, sizeof(url), "%s/Shows/%s/Seasons?UserId=%s",
             g_jellyfinUrl, showId.c_str(), g_jellyfinUserId);
    HttpResponse r = Http_Get(url, JellyfinHeaders(g_jellyfinToken));
    std::vector<JellyfinSeason> out;
    if (!r.ok()) return out;
    size_t arr = Json_FindArray(r.body, "Items");
    if (arr == std::string::npos) return out;
    for (const auto& obj : Json_SplitArray(r.body, arr)) {
        JellyfinSeason s;
        s.id   = Json_GetString(obj, "Id");
        s.name = Json_GetString(obj, "Name");
        int idx = Json_GetInt(obj, "IndexNumber");
        if (idx <= 0) continue;   // skip "Specials"-like pseudo-seasons
        char buf[8]; snprintf(buf, sizeof(buf), "%d", idx);
        s.index = buf;
        s.primaryTag = ParsePrimaryTag(obj);
        if (!s.id.empty()) out.push_back(s);
    }
    return out;
}

static std::vector<JellyfinEpisode> FetchEpisodesForSeason(const std::string& seasonId,
                                                           const std::string& showId)
{
    char url[1024];
    snprintf(url, sizeof(url),
             "%s/Shows/%s/Episodes?seasonId=%s&UserId=%s&Fields=Overview",
             g_jellyfinUrl, showId.c_str(), seasonId.c_str(), g_jellyfinUserId);
    HttpResponse r = Http_Get(url, JellyfinHeaders(g_jellyfinToken));
    std::vector<JellyfinEpisode> out;
    if (!r.ok()) return out;
    size_t arr = Json_FindArray(r.body, "Items");
    if (arr == std::string::npos) return out;
    for (const auto& obj : Json_SplitArray(r.body, arr)) {
        JellyfinEpisode e;
        e.id       = Json_GetString(obj, "Id");
        e.name     = Json_GetString(obj, "Name");
        e.overview = Json_GetString(obj, "Overview");
        int idx = Json_GetInt(obj, "IndexNumber");
        if (idx > 0) {
            char buf[8]; snprintf(buf, sizeof(buf), "%d", idx);
            e.index = buf;
        }
        e.primaryTag = ParsePrimaryTag(obj);
        if (!e.id.empty()) out.push_back(e);
    }
    return out;
}

// Map from seasonId -> parent showId, populated as we fetch seasons. Lets
// Jellyfin_Cache_GetEpisodes hit the /Shows/{showId}/Episodes endpoint
// with the right show id; we never have to plumb it from the caller.
static std::map<std::string, std::string> s_seasonToShow;

bool Jellyfin_Cache_GetSeasons(const std::string& showId,
                               std::vector<JellyfinSeason>& out)
{
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        auto it = s_cacheSeasons.find(showId);
        if (it != s_cacheSeasons.end()) {
            out = it->second;
            return true;
        }
    }
    std::vector<JellyfinSeason> fetched = FetchSeasonsForShow(showId);
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        s_cacheSeasons[showId] = fetched;
        for (const auto& s : fetched) s_seasonToShow[s.id] = showId;
        out = fetched;
    }
    return true;
}

bool Jellyfin_Cache_GetEpisodes(const std::string& seasonId,
                                std::vector<JellyfinEpisode>& out)
{
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        auto it = s_cacheEpisodes.find(seasonId);
        if (it != s_cacheEpisodes.end()) {
            out = it->second;
            return true;
        }
    }
    std::string showId;
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        auto it = s_seasonToShow.find(seasonId);
        if (it != s_seasonToShow.end()) showId = it->second;
    }
    if (showId.empty()) return false;
    std::vector<JellyfinEpisode> fetched = FetchEpisodesForSeason(seasonId, showId);
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        s_cacheEpisodes[seasonId] = fetched;
        out = fetched;
    }
    return true;
}


// ============================================================================
// Stream URL
// ============================================================================

static std::string Base()
{
    std::string u = g_jellyfinUrl;
    if (!u.empty() && u.find("://") == std::string::npos) u = "http://" + u;
    return u;
}

std::string Jellyfin_StreamUrl(const std::string& itemId)
{
    if (itemId.empty() || g_jellyfinUrl[0] == 0 || g_jellyfinToken[0] == 0) return "";
    std::string out = Base();
    out += "/Videos/";
    out += itemId;
    out += "/stream?Static=true&api_key=";
    out += g_jellyfinToken;
    return out;
}


// ============================================================================
// Cover art
// ============================================================================

static inline int Mkdir_(const char* path) {
#ifdef _WIN32
    return mkdir(path);
#else
    return mkdir(path, 0755);
#endif
}

static std::string ArtHostPath(const std::string& itemId)
{
    Mkdir_("Library");
    Mkdir_("Library/Jellyfin");
    return "Library/Jellyfin/" + itemId + ".jpg";
}

std::string Jellyfin_ArtCachePath(const std::string& itemId)
{
    return "E:\\Jellyfin\\" + itemId + ".jpg";
}

static void ArtWorker(std::string itemId, std::string tag,
                      std::atomic<bool>* flag)
{
    std::string path = ArtHostPath(itemId);
    struct stat st;
    if (stat(path.c_str(), &st) != 0 || st.st_size == 0) {
        char url[1024];
        if (tag.empty()) {
            snprintf(url, sizeof(url),
                     "%s/Items/%s/Images/Primary?fillHeight=600",
                     g_jellyfinUrl, itemId.c_str());
        } else {
            snprintf(url, sizeof(url),
                     "%s/Items/%s/Images/Primary?fillHeight=600&tag=%s",
                     g_jellyfinUrl, itemId.c_str(), tag.c_str());
        }
        if (!Http_GetToFile(url, path)) {
            fprintf(stderr, "[Jellyfin] art fetch failed for %s\n", itemId.c_str());
        }
    }
    {
        std::lock_guard<std::mutex> lk(s_artMtx);
        s_artInFlight.erase(itemId);
    }
    delete flag;
}

void Jellyfin_QueueArtDownload(const std::string& itemId, const std::string& tag)
{
    if (itemId.empty()) return;
    {
        std::lock_guard<std::mutex> lk(s_artMtx);
        if (s_artInFlight.count(itemId)) return;
        s_artInFlight[itemId] = new std::atomic<bool>(false);
    }
    std::thread(ArtWorker, itemId, tag, s_artInFlight[itemId]).detach();
}
