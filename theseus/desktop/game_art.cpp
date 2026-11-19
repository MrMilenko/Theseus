// game_art.cpp: box art for games that came in without an icon.
//
// Four places to look, no key needed for the first three:
//   Steam's public CDN by appid, for steam:// launches. Also keeps the bigger
//     capsule and header art in Configs/art for whatever wants it.
//   The cover a store importer handed over (GameArt_HintArt).
//   libretro-thumbnails by system and exact rom name, for RetroArch and ES-DE.
//   SteamGridDB, only when the user typed their own API key into Settings.
//
// The icon lands in Configs/icons/<titleID>.jpg, the same slot the Title Maker
// and the importers use, and only when that slot is empty. An icon somebody
// picked is never touched. Misses are remembered in Configs/art/misses.txt so
// a library without art doesn't ask the same servers again every launch.

#include "game_art.h"
#include "virtual_games.h"
#include "http_util.h"
#include "json_util.h"
#include "icon_compress.h"
#include "safe_save.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <string>
#include <vector>
#include <deque>
#include <map>
#include <set>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <ctype.h>
#include <stdlib.h>

bool g_fetchGameArt = true;
char g_steamGridDbKey[128] = "";

static const int kWorkers = 4;
static const int kIconDim = 256;

// ES-DE's system short names against libretro's playlist names, which are
// also the thumbnail repo names.
static const struct { const char* esde; const char* libretro; } kSystems[] = {
    { "3do",            "The 3DO Company - 3DO" },
    { "3ds",            "Nintendo - Nintendo 3DS" },
    { "amiga",          "Commodore - Amiga" },
    { "amstradcpc",     "Amstrad - CPC" },
    { "arcade",         "MAME" },
    { "mame",           "MAME" },
    { "fbneo",          "FBNeo - Arcade Games" },
    { "atari2600",      "Atari - 2600" },
    { "atari5200",      "Atari - 5200" },
    { "atari7800",      "Atari - 7800" },
    { "atarijaguar",    "Atari - Jaguar" },
    { "atarilynx",      "Atari - Lynx" },
    { "atarist",        "Atari - ST" },
    { "c64",            "Commodore - 64" },
    { "colecovision",   "Coleco - ColecoVision" },
    { "dreamcast",      "Sega - Dreamcast" },
    { "fds",            "Nintendo - Family Computer Disk System" },
    { "gamegear",       "Sega - Game Gear" },
    { "gb",             "Nintendo - Game Boy" },
    { "gba",            "Nintendo - Game Boy Advance" },
    { "gbc",            "Nintendo - Game Boy Color" },
    { "gc",             "Nintendo - GameCube" },
    { "genesis",        "Sega - Mega Drive - Genesis" },
    { "megadrive",      "Sega - Mega Drive - Genesis" },
    { "mastersystem",   "Sega - Master System - Mark III" },
    { "msx",            "Microsoft - MSX" },
    { "n64",            "Nintendo - Nintendo 64" },
    { "nds",            "Nintendo - Nintendo DS" },
    { "neogeo",         "SNK - Neo Geo" },
    { "neogeocd",       "SNK - Neo Geo CD" },
    { "nes",            "Nintendo - Nintendo Entertainment System" },
    { "famicom",        "Nintendo - Nintendo Entertainment System" },
    { "ngp",            "SNK - Neo Geo Pocket" },
    { "ngpc",           "SNK - Neo Geo Pocket Color" },
    { "pcengine",       "NEC - PC Engine - TurboGrafx 16" },
    { "tg16",           "NEC - PC Engine - TurboGrafx 16" },
    { "pcenginecd",     "NEC - PC Engine CD - TurboGrafx-CD" },
    { "tg-cd",          "NEC - PC Engine CD - TurboGrafx-CD" },
    { "pcfx",           "NEC - PC-FX" },
    { "pokemini",       "Nintendo - Pokemon Mini" },
    { "ps2",            "Sony - PlayStation 2" },
    { "psp",            "Sony - PlayStation Portable" },
    { "psx",            "Sony - PlayStation" },
    { "saturn",         "Sega - Saturn" },
    { "sega32x",        "Sega - 32X" },
    { "segacd",         "Sega - Mega-CD - Sega CD" },
    { "megacd",         "Sega - Mega-CD - Sega CD" },
    { "sg-1000",        "Sega - SG-1000" },
    { "snes",           "Nintendo - Super Nintendo Entertainment System" },
    { "sfc",            "Nintendo - Super Nintendo Entertainment System" },
    { "vectrex",        "GCE - Vectrex" },
    { "virtualboy",     "Nintendo - Virtual Boy" },
    { "wii",            "Nintendo - Wii" },
    { "wonderswan",     "Bandai - WonderSwan" },
    { "wonderswancolor","Bandai - WonderSwan Color" },
    { "x68000",         "Sharp - X68000" },
    { "xbox",           "Microsoft - Xbox" },
    { "zxspectrum",     "Sinclair - ZX Spectrum" },
    { "intellivision",  "Mattel - Intellivision" },
    { "odyssey2",       "Magnavox - Odyssey2" },
    { "dos",            "DOS" },
    { "scummvm",        "ScummVM" },
};

// Extensions that only ever mean one system. Disc images and zips don't.
static const struct { const char* ext; const char* sys; } kExts[] = {
    { "sfc", "snes" }, { "smc", "snes" }, { "nes", "nes" }, { "fds", "fds" },
    { "gba", "gba" }, { "gb", "gb" }, { "gbc", "gbc" },
    { "n64", "n64" }, { "z64", "n64" }, { "v64", "n64" },
    { "md", "genesis" }, { "gen", "genesis" }, { "smd", "genesis" },
    { "sms", "mastersystem" }, { "gg", "gamegear" }, { "32x", "sega32x" },
    { "nds", "nds" }, { "pce", "pcengine" }, { "a26", "atari2600" },
    { "a78", "atari7800" }, { "lnx", "atarilynx" }, { "j64", "atarijaguar" },
    { "ws", "wonderswan" }, { "wsc", "wonderswancolor" },
    { "ngp", "ngp" }, { "ngc", "ngpc" }, { "vb", "virtualboy" },
    { "col", "colecovision" }, { "min", "pokemini" }, { "vec", "vectrex" },
    { "gcm", "gc" }, { "rvz", "gc" }, { "wbfs", "wii" }, { "3ds", "3ds" },
};

// RetroArch cores by file name prefix, for zips and discs.
static const struct { const char* core; const char* sys; } kCores[] = {
    { "snes9x", "snes" }, { "bsnes", "snes" }, { "mesen-s", "snes" },
    { "nestopia", "nes" }, { "fceumm", "nes" }, { "quicknes", "nes" }, { "mesen", "nes" },
    { "mgba", "gba" }, { "vba", "gba" }, { "gpsp", "gba" },
    { "gambatte", "gbc" }, { "sameboy", "gbc" }, { "gearboy", "gbc" },
    { "mupen64plus", "n64" }, { "parallel_n64", "n64" },
    { "genesis_plus_gx", "genesis" }, { "picodrive", "genesis" }, { "blastem", "genesis" },
    { "swanstation", "psx" }, { "pcsx", "psx" }, { "mednafen_psx", "psx" }, { "beetle_psx", "psx" },
    { "duckstation", "psx" },
    { "ppsspp", "psp" }, { "flycast", "dreamcast" }, { "melonds", "nds" }, { "desmume", "nds" },
    { "mednafen_pce", "pcengine" }, { "beetle_pce", "pcengine" },
    { "mednafen_saturn", "saturn" }, { "beetle_saturn", "saturn" }, { "yabause", "saturn" },
    { "stella", "atari2600" }, { "prosystem", "atari7800" }, { "handy", "atarilynx" },
    { "virtualjaguar", "atarijaguar" }, { "dolphin", "gc" }, { "pcsx2", "ps2" },
    { "fbneo", "fbneo" }, { "mame", "mame" },
};

static const char* LibretroSystem(const char* esde)
{
    if (!esde || !*esde) return NULL;
    for (size_t i = 0; i < sizeof(kSystems) / sizeof(kSystems[0]); i++)
        if (strcasecmp(kSystems[i].esde, esde) == 0) return kSystems[i].libretro;
    return NULL;
}

static const char* SystemForExt(const char* ext)
{
    for (size_t i = 0; i < sizeof(kExts) / sizeof(kExts[0]); i++)
        if (strcasecmp(kExts[i].ext, ext) == 0) return kExts[i].sys;
    return NULL;
}

static const char* SystemForCore(const char* core)
{
    for (size_t i = 0; i < sizeof(kCores) / sizeof(kCores[0]); i++)
        if (strncasecmp(core, kCores[i].core, strlen(kCores[i].core)) == 0) return kCores[i].sys;
    return NULL;
}

static std::string UrlDecode(const std::string& s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size()) {
            char hex[3] = { s[i + 1], s[i + 2], 0 };
            out.push_back((char)strtol(hex, NULL, 16));
            i += 2;
        } else {
            out.push_back(s[i] == '+' ? ' ' : s[i]);
        }
    }
    return out;
}

static std::string UrlEncode(const std::string& s)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') out.push_back((char)c);
        else { out.push_back('%'); out.push_back(hex[c >> 4]); out.push_back(hex[c & 15]); }
    }
    return out;
}

// "a/b/Super Mario World (USA).sfc" -> stem and ext. An archive member
// ("game.zip#inner.nes") counts as the inner file.
static void SplitRom(const std::string& path, std::string& stem, std::string& ext)
{
    std::string p = path;
    size_t hash = p.rfind('#');
    if (hash != std::string::npos) p = p.substr(hash + 1);
    size_t slash = p.find_last_of("/\\");
    std::string file = (slash == std::string::npos) ? p : p.substr(slash + 1);
    size_t dot = file.rfind('.');
    stem = (dot == std::string::npos) ? file : file.substr(0, dot);
    ext  = (dot == std::string::npos) ? "" : file.substr(dot + 1);
}

// libretro's thumbnail names swap these for underscores.
static std::string LibretroName(const std::string& name)
{
    std::string out = name;
    for (char& c : out)
        if (strchr("&*/:`<>?\\|\"", c)) c = '_';
    return out;
}

static bool Exists(const std::string& path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0 && st.st_size > 0;
}

struct ArtJob {
    std::string titleID, name;
    int appid = 0;
    std::string system;    // libretro name, empty when it isn't an emulated game
    std::string romStem;
    std::string artUrl;    // a store's cover
    bool wantIcon = false, wantBig = false, trySgdb = false;
    std::string iconDir, artDir, sgdbKey;
};

struct ArtDone { std::string titleID; bool gotIcon, miss, triedSgdb; };

static struct {
    std::mutex mtx;
    std::deque<ArtJob> jobs;
    std::vector<ArtDone> done;
    int workers = 0;
} s_q;

// Main thread only from here down to the workers.
struct Hint { std::string system, rom, art; };
static std::map<std::string, Hint> s_hints;
static std::set<std::string> s_seen;               // queued this run
static std::map<std::string, unsigned> s_misses;   // titleID -> KeyTag of the SteamGridDB key tried, 0 for none
static bool s_missesLoaded = false;

// Which key a miss was tried with, so a new or fixed key looks again. Never 0.
static unsigned KeyTag(const char* key)
{
    unsigned h = 2166136261u;
    for (const char* p = key; *p; p++) { h ^= (unsigned char)*p; h *= 16777619u; }
    return h ? h : 1;
}

static std::string ArtDir()   { return std::string(AppPath_Tree("Configs")) + "/art"; }
static std::string MissFile() { return ArtDir() + "/misses.txt"; }

void GameArt_Hint(const char* titleID, const char* system, const char* romPath)
{
    if (!titleID || !*titleID) return;
    Hint& h = s_hints[titleID];
    h.system = system ? system : "";
    h.rom = romPath ? romPath : "";
}

void GameArt_HintArt(const char* titleID, const char* url)
{
    if (!titleID || !*titleID || !url || strncmp(url, "https://", 8) != 0) return;
    s_hints[titleID].art = url;
}

const char* GameArt_CachedPath(const char* titleID, const char* kind)
{
    static char buf[1024];
    if (!titleID || !Http_IsSafeFileId(titleID)) return NULL;
    snprintf(buf, sizeof(buf), "%s/%s.%s.jpg", ArtDir().c_str(), titleID, kind);
    return Exists(buf) ? buf : NULL;
}

// Worker side

static bool FetchTo(const std::string& url, const std::string& path)
{
    if (!Http_GetToFile(url, path)) return false;
    struct stat st;
    // A few hundred bytes is an error page or a blank placeholder, not art.
    if (stat(path.c_str(), &st) != 0 || st.st_size < 1000) { remove(path.c_str()); return false; }
    return true;
}

// Writes the icon unless one showed up while we were downloading.
static bool PlaceIcon(const ArtJob& j, const std::string& src)
{
    std::string jpg = j.iconDir + "/" + j.titleID + ".jpg";
    std::string png = j.iconDir + "/" + j.titleID + ".png";
    if (Exists(jpg) || Exists(png)) return false;
    Plat_MkdirP(j.iconDir.c_str());
    std::string tmp = j.artDir + "/" + j.titleID + ".icon.jpg";
    if (!Icons_FitFile(src.c_str(), tmp.c_str(), kIconDim, 90)) return false;
    if (Exists(jpg) || Exists(png)) { remove(tmp.c_str()); return false; }
    return Host_RenameReplace(tmp.c_str(), jpg.c_str());
}

static bool FromSteam(const ArtJob& j)
{
    std::string base = "https://cdn.akamai.steamstatic.com/steam/apps/" + std::to_string(j.appid) + "/";
    std::string cover  = j.artDir + "/" + j.titleID + ".cover.jpg";
    std::string header = j.artDir + "/" + j.titleID + ".header.jpg";
    if (!Exists(cover))  FetchTo(base + "library_600x900.jpg", cover);
    if (!Exists(header)) FetchTo(base + "header.jpg", header);
    if (!j.wantIcon) return false;
    if (Exists(cover)  && PlaceIcon(j, cover))  return true;
    if (Exists(header) && PlaceIcon(j, header)) return true;
    return false;
}

static bool FromLibretro(const ArtJob& j)
{
    std::string repo = j.system;
    for (char& c : repo) if (c == ' ') c = '_';
    std::string base = "https://raw.githubusercontent.com/libretro-thumbnails/" + UrlEncode(repo)
                     + "/master/Named_Boxarts/";

    // The rom's own file name is usually the No-Intro name libretro keys on.
    // A scraped display name without the region tag gets a couple of guesses.
    std::vector<std::string> names;
    auto add = [&](const std::string& n) {
        if (n.empty()) return;
        for (size_t i = 0; i < names.size(); i++) if (names[i] == n) return;
        names.push_back(n);
    };
    add(j.romStem);
    add(j.name);
    if (j.name.find('(') == std::string::npos) { add(j.name + " (USA)"); add(j.name + " (World)"); }

    std::string tmp = j.artDir + "/" + j.titleID + ".cover.png";
    for (size_t i = 0; i < names.size(); i++) {
        if (!FetchTo(base + UrlEncode(LibretroName(names[i])) + ".png", tmp)) continue;
        std::string cover = j.artDir + "/" + j.titleID + ".cover.jpg";
        Icons_FitFile(tmp.c_str(), cover.c_str(), 600, 90);
        bool placed = PlaceIcon(j, tmp);
        remove(tmp.c_str());
        return placed;
    }
    return false;
}

static bool FromStore(const ArtJob& j)
{
    std::string tmp = j.artDir + "/" + j.titleID + ".store";
    if (!FetchTo(j.artUrl, tmp)) return false;
    std::string cover = j.artDir + "/" + j.titleID + ".cover.jpg";
    Icons_FitFile(tmp.c_str(), cover.c_str(), 600, 90);
    bool placed = PlaceIcon(j, tmp);
    remove(tmp.c_str());
    return placed;
}

// SteamGridDB: square grids first, they suit the orb. Never webp, stb can't read it.
static bool FromSteamGridDb(const ArtJob& j)
{
    HttpHeaders auth = { { "Authorization", "Bearer " + j.sgdbKey } };
    const std::string api = "https://www.steamgriddb.com/api/v2/";
    std::string grids;
    if (j.appid > 0) {
        grids = api + "grids/steam/" + std::to_string(j.appid);
    } else {
        HttpResponse r = Http_Get(api + "search/autocomplete/" + UrlEncode(j.name), auth);
        if (!r.ok()) return false;
        int id = Json_GetInt(r.body, "id");
        if (id <= 0) return false;
        grids = api + "grids/game/" + std::to_string(id);
    }
    const char* shapes[] = { "?dimensions=512x512,1024x1024&mimes=image/png,image/jpeg",
                             "?mimes=image/png,image/jpeg" };
    for (int s = 0; s < 2; s++) {
        HttpResponse r = Http_Get(grids + shapes[s], auth);
        if (!r.ok()) return false;
        std::string url = Json_GetString(r.body, "url");
        if (url.empty()) continue;
        std::string tmp = j.artDir + "/" + j.titleID + ".sgdb";
        if (!FetchTo(url, tmp)) return false;
        bool placed = PlaceIcon(j, tmp);
        remove(tmp.c_str());
        return placed;
    }
    return false;
}

static void Worker()
{
    for (;;) {
        ArtJob j;
        {
            std::lock_guard<std::mutex> lk(s_q.mtx);
            if (s_q.jobs.empty()) { s_q.workers--; return; }
            j = s_q.jobs.front();
            s_q.jobs.pop_front();
        }
        Plat_MkdirP(j.artDir.c_str());
        bool got = false;
        if (j.appid > 0)                    got = FromSteam(j);
        if (!got && j.wantIcon && !j.artUrl.empty()) got = FromStore(j);
        if (!got && j.wantIcon && !j.system.empty()) got = FromLibretro(j);
        if (!got && j.wantIcon && j.trySgdb) got = FromSteamGridDb(j);
        if (got) fprintf(stderr, "[art] %s: got art\n", j.name.c_str());

        bool miss = (j.wantIcon && !got) ||
                    (j.wantBig && !Exists(j.artDir + "/" + j.titleID + ".cover.jpg"));
        std::lock_guard<std::mutex> lk(s_q.mtx);
        s_q.done.push_back({ j.titleID, got, miss, j.trySgdb });
    }
}

// Main thread side

static void LoadMisses()
{
    s_missesLoaded = true;
    FILE* f = fopen(MissFile().c_str(), "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char id[128];
        unsigned sg = 0;
        if (sscanf(line, "%127s %u", id, &sg) >= 1) s_misses[id] = sg;
    }
    fclose(f);
}

static void SaveMisses()
{
    Plat_MkdirP(ArtDir().c_str());
    SafeSave save(MissFile().c_str());
    if (!save.f) return;
    for (std::map<std::string, unsigned>::iterator it = s_misses.begin(); it != s_misses.end(); ++it)
        fprintf(save.f, "%s %u\n", it->first.c_str(), it->second);
    save.Commit();
}

static int SteamAppId(const char* launch)
{
    const char* p = NULL;
    if (strncmp(launch, "steam://rungameid/", 18) == 0) p = launch + 18;
    else if (strncmp(launch, "steam://run/", 12) == 0) p = launch + 12;
    else if ((p = strstr(launch, "steam://rungameid/")) != NULL) p += 18;
    if (!p) return 0;
    long id = strtol(p, NULL, 10);
    // Non-Steam shortcuts carry 64 bit ids that the CDN has nothing for.
    return (id > 0 && id < 0x7fffffff) ? (int)id : 0;
}

// Pulls a rom path out of a launch line: RetroArch's url, else the last
// quoted or bare token with an extension we know.
static void RomFromLaunch(const char* launch, std::string& rom, std::string& core)
{
    rom.clear();
    core.clear();
    if (strncmp(launch, "retroarch://", 12) == 0) {
        std::string s = launch;
        size_t c = s.find("core=");
        if (c != std::string::npos) core = UrlDecode(s.substr(c + 5, s.find('&', c) - (c + 5)));
        size_t r = s.find("content=");
        if (r != std::string::npos) rom = UrlDecode(s.substr(r + 8, s.find('&', r) - (r + 8)));
        return;
    }
    std::string s = launch;
    std::vector<std::string> toks;
    for (size_t i = 0; i < s.size();) {
        if (s[i] == '"') {
            size_t e = s.find('"', i + 1);
            if (e == std::string::npos) e = s.size();
            toks.push_back(s.substr(i + 1, e - i - 1));
            i = e + 1;
        } else if (s[i] == ' ') {
            i++;
        } else {
            size_t e = s.find(' ', i);
            if (e == std::string::npos) e = s.size();
            toks.push_back(s.substr(i, e - i));
            i = e;
        }
    }
    for (size_t i = toks.size(); i-- > 0;) {
        std::string stem, ext;
        SplitRom(toks[i], stem, ext);
        if (!ext.empty() && SystemForExt(ext.c_str())) { rom = toks[i]; return; }
    }
}

static void Queue(const ArtJob& j)
{
    std::lock_guard<std::mutex> lk(s_q.mtx);
    s_q.jobs.push_back(j);
    if (s_q.workers < kWorkers) {
        s_q.workers++;
        std::thread(Worker).detach();
    }
}

static void Scan()
{
    if (!s_missesLoaded) LoadMisses();
    std::string iconDir = VGAMES_ICONS, artDir = ArtDir();
    bool haveKey = g_steamGridDbKey[0] != 0;
    int queued = 0;

    for (int i = 0; i < g_vgames.count; i++) {
        const VirtualGame& g = g_vgames.games[i];
        if (!g.valid || VGames_IsStreaming(g)) continue;
        // The copy that shows already has a source of its own.
        if (g_hideDuplicateGames && (VGames_FindDuplicates(), g.duplicate)) continue;
        if (!Http_IsSafeFileId(g.titleID) || s_seen.count(g.titleID)) continue;

        ArtJob j;
        j.titleID = g.titleID;
        j.name = g.name;
        j.appid = SteamAppId(g.launch);
        j.wantIcon = !VGames_GetIconPath(i);
        j.wantBig = j.appid > 0 && !GameArt_CachedPath(g.titleID, "cover");
        if (!j.wantIcon && !j.wantBig) continue;

        std::map<std::string, unsigned>::iterator miss = s_misses.find(j.titleID);
        bool missed = miss != s_misses.end();
        // A miss stays a miss until there's somewhere new to look.
        if (missed && !(haveKey && miss->second != KeyTag(g_steamGridDbKey))) continue;

        if (j.appid == 0) {
            std::string sys, rom, core, stem, ext;
            std::map<std::string, Hint>::iterator h = s_hints.find(j.titleID);
            if (h != s_hints.end()) { sys = h->second.system; rom = h->second.rom; }
            if (h == s_hints.end() || (sys.empty() && rom.empty())) RomFromLaunch(g.launch, rom, core);
            if (h != s_hints.end() && !missed) j.artUrl = h->second.art;
            SplitRom(rom, stem, ext);
            if (sys.empty() && !ext.empty() && SystemForExt(ext.c_str())) sys = SystemForExt(ext.c_str());
            if (sys.empty() && !core.empty() && SystemForCore(core.c_str())) sys = SystemForCore(core.c_str());
            const char* lr = LibretroSystem(sys.c_str());
            if (lr && !missed) { j.system = lr; j.romStem = stem; }
        }
        j.trySgdb = haveKey && j.wantIcon;
        if (!j.wantBig && j.system.empty() && j.artUrl.empty() && !j.trySgdb) continue;

        j.iconDir = iconDir;
        j.artDir = artDir;
        j.sgdbKey = g_steamGridDbKey;
        s_seen.insert(j.titleID);
        Queue(j);
        queued++;
    }
    if (queued) fprintf(stderr, "[art] looking for art for %d games\n", queued);
}

void GameArt_Tick()
{
    static unsigned s_gen = ~0u;
    static std::string s_key;
    static std::chrono::steady_clock::time_point s_lastScan;

    std::vector<ArtDone> done;
    {
        std::lock_guard<std::mutex> lk(s_q.mtx);
        done.swap(s_q.done);
    }
    bool gotAny = false, missesChanged = false;
    for (size_t i = 0; i < done.size(); i++) {
        gotAny |= done[i].gotIcon;
        if (!done[i].miss) {
            missesChanged |= s_misses.erase(done[i].titleID) > 0;
        } else {
            s_misses[done[i].titleID] = done[i].triedSgdb ? KeyTag(s_key.c_str()) : 0;
            missesChanged = true;
        }
    }
    if (missesChanged) SaveMisses();
    if (gotAny) {
        // The memory grid's icon list is built once; have it look again.
        extern bool s_iconsLoaded;
        s_iconsLoaded = false;
    }

    if (!g_fetchGameArt) return;
    // A new key is somewhere new to look for everything that missed.
    if (s_key != g_steamGridDbKey) {
        s_key = g_steamGridDbKey;
        s_seen.clear();
        s_gen = ~0u;
    }
    if (s_gen == g_vgames.generation) return;
    // Imports bump the generation many times in a row; let them settle.
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    if (now - s_lastScan < std::chrono::seconds(2)) return;
    s_lastScan = now;
    s_gen = g_vgames.generation;
    Scan();
}
