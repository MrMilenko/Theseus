// title_maker.cpp: F3 Title Maker panel. Floating ImGui window for
// managing the virtual games library (the games.ini-driven entries
// the dashboard surfaces in TitleScanner). Desktop-only.

#include "app_paths.h"
#include "std.h"
#include "dashapp.h"
#include "panel_shared.h"
#include "virtual_games.h"
#include "udata_synth.h"
#include "xiso.h"
#include "launchers/steam.h"
#include "launchers/retroarch.h"
#include "launchers/emulators.h"
#include "launchers/sgdb.h"
#include "imgui.h"
#include "imfilebrowser.h"
#include "stb_image.h"
#include "http_util.h"
#include <sys/stat.h>
#include <algorithm>
#include <string>

#ifdef _WIN32
#include <io.h>
#include <direct.h>
#include <process.h>
#else
#include <dirent.h>
#endif

// Platform code uses native file I/O; undo Xbox filesystem macros.
#undef fopen
#undef _tfopen
#ifdef _WIN32
#undef CreateFile
#undef CreateFileA
#undef FindFirstFile
#undef FindFirstFileA
#undef FindNextFile
#undef FindNextFileA
#undef FindClose
#undef GetFileAttributes
#undef GetFileAttributesA
#undef GetFileAttributesEx
#undef GetFileAttributesExA
#undef RemoveDirectory
#undef RemoveDirectoryA
#endif

bool g_titleMakerOpen = false;

// ============================================================================
// Helpers
// ============================================================================

static void TM_EnsureDir(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) {
#ifdef _WIN32
        _mkdir(path);
#else
        mkdir(path, 0755);
#endif
    }
}

static void TM_SanitizeName(char* name, int maxLen) {
    for (char* c = name; *c; c++) {
        if (*c == ':' || *c == '/' || *c == '\\' || *c == '?' ||
            *c == '*' || *c == '"' || *c == '<' || *c == '>' || *c == '|')
            *c = '-';
    }
    int len = (int)strlen(name);
    while (len > 0 && (name[len-1] == ' ' || name[len-1] == '-'))
        name[--len] = 0;
}

static bool TM_CopyFile(const char* src, const char* dst) {
    FILE* sfp = fopen(src, "rb");
    FILE* dfp = fopen(dst, "wb");
    if (!sfp || !dfp) {
        if (sfp) fclose(sfp);
        if (dfp) fclose(dfp);
        return false;
    }
    unsigned char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), sfp)) > 0)
        fwrite(buf, 1, n, dfp);
    fclose(sfp);
    fclose(dfp);
    return true;
}

// Derive a title from a content path: strip dir, strip archive-prefix
// (foo.zip#inner.nes -> inner.nes), strip extension.
static void TM_DeriveRomTitle(const char* contentPath, char* out, size_t outSize) {
    if (outSize) out[0] = '\0';
    if (!contentPath || !*contentPath) return;
    const char* fwd  = strrchr(contentPath, '/');
    const char* back = strrchr(contentPath, '\\');
    const char* fname = (fwd > back) ? fwd : back;
    fname = fname ? fname + 1 : contentPath;
    const char* hash = strchr(fname, '#');
    const char* base = hash ? hash + 1 : fname;
    strncpy(out, base, outSize - 1);
    out[outSize - 1] = '\0';
    char* dot = strrchr(out, '.');
    if (dot) *dot = '\0';
}

static void TM_UrlEncode(const char* in, char* out, size_t outSize) {
    static const char* hex = "0123456789ABCDEF";
    size_t op = 0;
    if (outSize == 0) return;
    for (const char* p = in; *p && op + 4 < outSize; p++) {
        unsigned char c = (unsigned char)*p;
        bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                 || (c >= '0' && c <= '9') || c == '-' || c == '_'
                 || c == '.' || c == '~' || c == '/' || c == '\\'
                 || c == ':';
        if (safe) out[op++] = (char)c;
        else {
            out[op++] = '%';
            out[op++] = hex[c >> 4];
            out[op++] = hex[c & 0xF];
        }
    }
    out[op] = '\0';
}

struct TM_RAImportCtx {
    int added;
    int skipped;
    char installRoot[512];
};

static void TM_RegisterIcon(const char* name, const char* titleID);

static void TM_RAImportItem(const char* label, const char* path,
                            const char* dbName, const char* coreName,
                            const char* corePath, void* ud) {
    TM_RAImportCtx* ctx = (TM_RAImportCtx*)ud;

    // content_history items leave label / db_name empty. Derive title
    // from the filename and the system from core_name's
    // "<Manufacturer> - <System> (<core>)" suffix.
    char effLabel[256];
    if (label && *label) {
        strncpy(effLabel, label, sizeof(effLabel) - 1);
        effLabel[sizeof(effLabel) - 1] = 0;
    } else {
        TM_DeriveRomTitle(path, effLabel, sizeof(effLabel));
    }

    char effDbName[256];
    if (dbName && *dbName) {
        strncpy(effDbName, dbName, sizeof(effDbName) - 1);
        effDbName[sizeof(effDbName) - 1] = 0;
    } else if (coreName && *coreName) {
        strncpy(effDbName, coreName, sizeof(effDbName) - 1);
        effDbName[sizeof(effDbName) - 1] = 0;
        char* paren = strrchr(effDbName, '(');
        if (paren) {
            while (paren > effDbName && paren[-1] == ' ') paren--;
            *paren = 0;
        }
        size_t l = strlen(effDbName);
        if (l + 5 < sizeof(effDbName)) strcpy(effDbName + l, ".lpl");
    } else {
        effDbName[0] = 0;
    }

    label  = effLabel;
    dbName = effDbName;

    char coreFile[256] = "";
    bool detect = (corePath[0] == 0 || strcmp(corePath, "DETECT") == 0 ||
                   strcmp(coreName, "DETECT") == 0);
    if (detect) {
        char sysName[256];
        strncpy(sysName, dbName, sizeof(sysName) - 1);
        sysName[sizeof(sysName) - 1] = 0;
        size_t sl = strlen(sysName);
        if (sl > 4 && strcmp(sysName + sl - 4, ".lpl") == 0) sysName[sl - 4] = 0;

        // Playlists name systems as "<Manufacturer> - <System>"; .info
        // files only store <System> in systemname=. Strip the prefix.
        const char* lookup = sysName;
        const char* dash = strstr(sysName, " - ");
        if (dash) lookup = dash + 3;

        if (!RetroArch_FindCoreForSystem(ctx->installRoot, lookup,
                                          coreFile, sizeof(coreFile))) {
            fprintf(stderr, "[tm] import skip: no core for system %s\n", lookup);
            ctx->skipped++;
            return;
        }
    } else {
        const char* fwd  = strrchr(corePath, '/');
        const char* back = strrchr(corePath, '\\');
        const char* fname = (fwd > back) ? fwd : back;
        fname = fname ? fname + 1 : corePath;
        strncpy(coreFile, fname, sizeof(coreFile) - 1);
        coreFile[sizeof(coreFile) - 1] = 0;
    }

    char encCore[768], encContent[768];
    TM_UrlEncode(coreFile, encCore,    sizeof(encCore));
    TM_UrlEncode(path,     encContent, sizeof(encContent));

    char launch[2048];
    snprintf(launch, sizeof(launch),
             "retroarch://run?core=%s&content=%s", encCore, encContent);

    for (int i = 0; i < g_vgames.count; i++) {
        if (g_vgames.games[i].valid &&
            strcmp(g_vgames.games[i].launch, launch) == 0) {
            const char* titleID = g_vgames.games[i].titleID;
            char iconPng[600], iconJpg[600];
            snprintf(iconPng, sizeof(iconPng), "%s/%s.png", VGAMES_ICONS, titleID);
            snprintf(iconJpg, sizeof(iconJpg), "%s/%s.jpg", VGAMES_ICONS, titleID);
            struct stat st;
            bool haveIcon = (stat(iconPng, &st) == 0) || (stat(iconJpg, &st) == 0);
            if (!haveIcon) {
                char boxart[1024];
                if (RetroArch_FindBoxart(ctx->installRoot, dbName, label, path,
                                          boxart, sizeof(boxart))) {
                    TM_EnsureDir(VGAMES_ICONS);
                    TM_CopyFile(boxart, iconPng);
                }
            }
            TM_RegisterIcon(g_vgames.games[i].name, titleID);
            ctx->skipped++;
            return;
        }
    }

    extern int Title_SanitizeName(const char*, char*, size_t);
    char cleanName[128];
    Title_SanitizeName(label, cleanName, sizeof(cleanName));
    if (!cleanName[0]) {
        ctx->skipped++;
        return;
    }

    // Dedup by name. Same game often shows up twice in playlists (foo.zip
    // vs foo.zip#inner.nes, system playlist vs history) and we don't want
    // two entries for it.
    if (VGames_FindByName(cleanName) >= 0) {
        ctx->skipped++;
        return;
    }

    char genID[16];
    snprintf(genID, sizeof(genID), "%08x", (unsigned)time(NULL) + (unsigned)ctx->added);

    VGames_Add(cleanName, genID, launch, "E", "Emulators");
    TM_RegisterIcon(cleanName, genID);

    char boxart[1024];
    if (RetroArch_FindBoxart(ctx->installRoot, dbName, label, path,
                              boxart, sizeof(boxart))) {
        TM_EnsureDir(VGAMES_ICONS);
        char dst[600];
        snprintf(dst, sizeof(dst), "%s/%s.png", VGAMES_ICONS, genID);
        TM_CopyFile(boxart, dst);
    }

    ctx->added++;
}

// Read Icons.ini into parallel key/value arrays. Returns entry count.
#define TM_MAX_ICONS 512
static int TM_ReadIconsIni(char keys[][128], char vals[][128]) {
    int count = 0;
    const char* path = AppPath("Configs/Icons.ini");
    FILE* fp = fopen(path, "r");
    if (!fp) fp = fopen(AppPath("Configs/icons.ini"), "r");
    if (!fp) return 0;
    char line[256];
    while (fgets(line, sizeof(line), fp) && count < TM_MAX_ICONS) {
        char* nl = strchr(line, '\n'); if (nl) *nl = 0;
        char* cr = strchr(line, '\r'); if (cr) *cr = 0;
        if (line[0] == 0) continue;
        // Skip anything without an =. Don't skip on a leading [ , since ROM
        // names can start with one.
        char* eq = strchr(line, '=');
        if (eq) {
            *eq = 0;
            strncpy(keys[count], line, 127); keys[count][127] = 0;
            strncpy(vals[count], eq + 1, 127); vals[count][127] = 0;
            count++;
        }
    }
    fclose(fp);
    return count;
}

static void TM_WriteIconsIni(char keys[][128], char vals[][128], int count) {
    FILE* fp = fopen(AppPath("Configs/Icons.ini"), "w");
    if (!fp) return;
    fprintf(fp, "[default]\n");
    for (int i = 0; i < count; i++)
        fprintf(fp, "%s=%s\n", keys[i], vals[i]);
    fclose(fp);
}

// Open a URL via the platform's default handler. Falls through the
// same dispatch the launch overlay uses.
static void TM_OpenUrl(const char* url) {
    if (!url || !*url) return;
    extern void DesktopLaunch(const char*);
    DesktopLaunch(url);
}

// Load <retroarchInstall>/assets/ozone/png/retroarch.png into an ImGui
// texture, cached per resolved path. Returns 0 on failure.
static unsigned long long TM_LoadRetroArchLogo(const char* installPath, int* outW, int* outH) {
    static GuiTexture* s_tex = NULL;
    static char s_path[600] = "";
    static int s_w = 0, s_h = 0;

    if (outW) *outW = 0;
    if (outH) *outH = 0;
    if (!installPath || !*installPath) return 0;

    char probe[800];
    snprintf(probe, sizeof(probe), "%s/assets/ozone/png/retroarch.png", installPath);
    if (strcmp(probe, s_path) == 0) {
        if (outW) *outW = s_w;
        if (outH) *outH = s_h;
        return GuiTextureImId(s_tex);
    }

    GuiTextureDestroy(&s_tex);
    strncpy(s_path, probe, sizeof(s_path) - 1);
    s_path[sizeof(s_path) - 1] = 0;
    s_w = s_h = 0;

    int w = 0, h = 0, ch = 0;
    unsigned char* pixels = stbi_load(probe, &w, &h, &ch, 4);
    if (!pixels) return 0;

    s_tex = GuiTextureCreate(w, h, pixels);
    stbi_image_free(pixels);

    s_w = w;
    s_h = h;
    if (outW) *outW = w;
    if (outH) *outH = h;
    return GuiTextureImId(s_tex);
}

// Pull a "(Region)" tag out of a title's parenthetical metadata.
// Recognises common No-Intro / TOSEC region markers; returns "" if
// nothing matches.
static void TM_DeriveRegion(const char* name, char* out, size_t outSize) {
    if (outSize) out[0] = 0;
    if (!name) return;
    static const char* kRegions[] = {
        "USA", "Japan", "Europe", "World", "USA, Europe",
        "JP", "EU", "US", "PAL", "NTSC", "Asia", "Korea",
        "Australia", "Brazil", "Germany", "France", "Spain",
        "Italy", "China", "Taiwan", "Unl", "Proto", 0
    };
    for (int i = 0; kRegions[i]; i++) {
        char needle[40];
        snprintf(needle, sizeof(needle), "(%s)", kRegions[i]);
        if (strstr(name, needle)) {
            strncpy(out, kRegions[i], outSize - 1);
            out[outSize - 1] = 0;
            return;
        }
    }
}

// Extract a urlencoded field value from a retroarch:// spec into out.
// Returns true if the key was found.
static bool TM_ParseRetroArchField(const char* launch, const char* key, char* out, size_t outSize) {
    if (outSize) out[0] = 0;
    if (!launch || !key) return false;
    char needle[64];
    snprintf(needle, sizeof(needle), "%s=", key);
    const char* p = strstr(launch, needle);
    if (!p) return false;
    p += strlen(needle);
    const char* end = strchr(p, '&');
    if (!end) end = p + strlen(p);
    size_t op = 0;
    while (p < end && op + 1 < outSize) {
        if (*p == '%' && p + 2 < end) {
            auto hv = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
                if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
                return -1;
            };
            int hi = hv(p[1]), lo = hv(p[2]);
            if (hi >= 0 && lo >= 0) {
                out[op++] = (char)((hi << 4) | lo);
                p += 3;
                continue;
            }
        }
        out[op++] = *p++;
    }
    out[op] = 0;
    return true;
}

// ASCII transliteration of Latin-1 Supplement and Latin Extended-A
// codepoints. The dashboard's text atlas only renders ASCII glyphs;
// Steam library entries often carry accented chars that need flattening.
static void TM_TransliterateUTF8(char* dst, const char* src, int maxLen) {
    static const struct { unsigned char b1, b2; char repl; } map[] = {
        {0xC3,0x80,'A'},{0xC3,0x81,'A'},{0xC3,0x82,'A'},{0xC3,0x83,'A'},{0xC3,0x84,'A'},{0xC3,0x85,'A'},
        {0xC3,0x87,'C'},
        {0xC3,0x88,'E'},{0xC3,0x89,'E'},{0xC3,0x8A,'E'},{0xC3,0x8B,'E'},
        {0xC3,0x8C,'I'},{0xC3,0x8D,'I'},{0xC3,0x8E,'I'},{0xC3,0x8F,'I'},
        {0xC3,0x90,'D'},{0xC3,0x91,'N'},
        {0xC3,0x92,'O'},{0xC3,0x93,'O'},{0xC3,0x94,'O'},{0xC3,0x95,'O'},{0xC3,0x96,'O'},{0xC3,0x98,'O'},
        {0xC3,0x99,'U'},{0xC3,0x9A,'U'},{0xC3,0x9B,'U'},{0xC3,0x9C,'U'},
        {0xC3,0x9D,'Y'},{0xC3,0x9F,'s'},
        {0xC3,0xA0,'a'},{0xC3,0xA1,'a'},{0xC3,0xA2,'a'},{0xC3,0xA3,'a'},{0xC3,0xA4,'a'},{0xC3,0xA5,'a'},
        {0xC3,0xA7,'c'},
        {0xC3,0xA8,'e'},{0xC3,0xA9,'e'},{0xC3,0xAA,'e'},{0xC3,0xAB,'e'},
        {0xC3,0xAC,'i'},{0xC3,0xAD,'i'},{0xC3,0xAE,'i'},{0xC3,0xAF,'i'},
        {0xC3,0xB0,'d'},{0xC3,0xB1,'n'},
        {0xC3,0xB2,'o'},{0xC3,0xB3,'o'},{0xC3,0xB4,'o'},{0xC3,0xB5,'o'},{0xC3,0xB6,'o'},{0xC3,0xB8,'o'},
        {0xC3,0xB9,'u'},{0xC3,0xBA,'u'},{0xC3,0xBB,'u'},{0xC3,0xBC,'u'},
        {0xC3,0xBD,'y'},{0xC3,0xBF,'y'},
    };
    int di = 0;
    for (const unsigned char* s = (const unsigned char*)src; *s && di < maxLen - 1; ) {
        if (*s < 0x80) {
            dst[di++] = (char)*s++;
        } else if (s[0] >= 0xC0 && s[0] < 0xE0 && s[1]) {
            bool found = false;
            for (int m = 0; m < (int)(sizeof(map)/sizeof(map[0])); m++) {
                if (s[0] == map[m].b1 && s[1] == map[m].b2) {
                    dst[di++] = map[m].repl;
                    found = true;
                    break;
                }
            }
            if (!found) dst[di++] = '?';
            s += 2;
        } else if (s[0] >= 0xE0 && s[0] < 0xF0) {
            dst[di++] = '?'; s += 3;
        } else if (s[0] >= 0xF0) {
            dst[di++] = '?'; s += 4;
        } else {
            s++;
        }
    }
    dst[di] = 0;
}

// Read Icons.ini, upsert one name -> titleID mapping, write back.
// harddrive.xap reads this to route a tile's display name back to the
// right icon file under Configs/icons/.
static void TM_RegisterIcon(const char* name, const char* titleID) {
    if (!name || !*name || !titleID || !*titleID) return;
    static char keys[TM_MAX_ICONS][128];
    static char vals[TM_MAX_ICONS][128];
    int count = TM_ReadIconsIni(keys, vals);
    bool found = false;
    for (int i = 0; i < count; i++) {
        if (strcasecmp(keys[i], name) == 0) {
            strncpy(vals[i], titleID, 127);
            vals[i][127] = 0;
            found = true;
            break;
        }
    }
    if (!found && count < TM_MAX_ICONS) {
        strncpy(keys[count], name, 127); keys[count][127] = 0;
        strncpy(vals[count], titleID, 127); vals[count][127] = 0;
        count++;
    }
    TM_WriteIconsIni(keys, vals, count);
}

// Try a series of Steam CDN art URLs for an appid, copy the first hit
// to <VGAMES_ICONS>/<titleID>.jpg, resize to 128px on macOS / Linux.
// Returns true if an icon ended up on disk.
static bool TM_DownloadSteamIcon(int appid, const char* titleID) {
    char iconPath[512];
    snprintf(iconPath, sizeof(iconPath), "%s/%s.jpg", VGAMES_ICONS, titleID);
    struct stat ist;
    if (stat(iconPath, &ist) == 0) return true; // already have one

    const char* tryUrls[] = {
        "logo.png", "icon.jpg", "logo.jpg",
        "library_icon.jpg", "library_icon.png",
        "header.jpg",
        NULL
    };
    bool gotIcon = false;
    for (int u = 0; tryUrls[u] && !gotIcon; u++) {
        bool isPng = strstr(tryUrls[u], ".png") != NULL;
        char tmpPath[512], url[512];
        snprintf(tmpPath, sizeof(tmpPath), "%s/%s_tmp%s",
                 VGAMES_ICONS, titleID, isPng ? ".png" : ".jpg");
        snprintf(url, sizeof(url),
                 "https://cdn.akamai.steamstatic.com/steam/apps/%d/%s",
                 appid, tryUrls[u]);
        if (Http_GetToFile(url, tmpPath) && stat(tmpPath, &ist) == 0 && ist.st_size > 1000) {
#ifdef __APPLE__
            char cmd[1024];
            snprintf(cmd, sizeof(cmd),
                "sips -z 128 128 -s format jpeg \"%s\" --out \"%s\" >/dev/null 2>&1",
                tmpPath, iconPath);
            bool resizeOk = (system(cmd) == 0);
#elif defined(_WIN32)
            bool resizeOk = TM_CopyFile(tmpPath, iconPath);
#else
            char cmd[1024];
            snprintf(cmd, sizeof(cmd),
                "convert \"%s\" -resize 128x128! -quality 90 \"%s\" 2>/dev/null || cp \"%s\" \"%s\"",
                tmpPath, iconPath, tmpPath, iconPath);
            bool resizeOk = (system(cmd) == 0);
#endif
            if (resizeOk && stat(iconPath, &ist) == 0 && ist.st_size > 500)
                gotIcon = true;
        }
        remove(tmpPath);
    }
    if (!gotIcon) remove(iconPath);
    return gotIcon;
}

// Walk every installed Steam app (via Steam_DiscoverLibraries +
// appmanifest_*.acf parsing) and add fresh entries to VGames as
// steam://rungameid/<id> launches. Returns counts via outparams.
static void TM_ImportSteamLibrary(const char* steamPath,
                                   int* outCreated, int* outSkipped, int* outIcons) {
    if (outCreated) *outCreated = 0;
    if (outSkipped) *outSkipped = 0;
    if (outIcons)   *outIcons   = 0;

    struct SteamGame { int appid; char name[256]; };
    static SteamGame games[256];
    int gameCount = 0;

    char libDirs[16][512];
    int libCount = Steam_DiscoverLibraries(steamPath, libDirs, 16);

    auto ParseAppManifest = [&](const char* libDir, const char* fileName) {
        if (gameCount >= 256) return;
        if (strncmp(fileName, "appmanifest_", 12) != 0) return;
        char acfPath[512];
        snprintf(acfPath, sizeof(acfPath), "%s/%s", libDir, fileName);
        FILE* acf = fopen(acfPath, "r");
        if (!acf) return;
        int appid = 0;
        char name[256] = {};
        char acfLine[1024];
        while (fgets(acfLine, sizeof(acfLine), acf)) {
            char* aq = strstr(acfLine, "\"appid\"");
            if (aq) {
                char* v1 = strchr(aq + 7, '"');
                if (v1) { char* v2 = strchr(v1 + 1, '"'); if (v2) { *v2 = 0; appid = atoi(v1 + 1); } }
            }
            char* nq = strstr(acfLine, "\"name\"");
            if (nq) {
                char* v1 = strchr(nq + 6, '"');
                if (v1) { char* v2 = strchr(v1 + 1, '"'); if (v2) { *v2 = 0; strncpy(name, v1 + 1, 255); } }
            }
        }
        fclose(acf);
        if (appid > 0 && name[0]) {
            games[gameCount].appid = appid;
            TM_TransliterateUTF8(games[gameCount].name, name, 256);
            gameCount++;
        }
    };

    for (int d = 0; d < libCount && gameCount < 256; d++) {
#ifdef _WIN32
        char searchBuf[512];
        snprintf(searchBuf, sizeof(searchBuf), "%s\\appmanifest_*", libDirs[d]);
        struct _finddata_t fd;
        intptr_t hFind = _findfirst(searchBuf, &fd);
        if (hFind != -1) {
            do { ParseAppManifest(libDirs[d], fd.name); } while (_findnext(hFind, &fd) == 0 && gameCount < 256);
            _findclose(hFind);
        }
#else
        DIR* dir = opendir(libDirs[d]);
        if (!dir) continue;
        struct dirent* ent;
        while ((ent = readdir(dir)) != NULL && gameCount < 256) {
            ParseAppManifest(libDirs[d], ent->d_name);
        }
        closedir(dir);
#endif
    }

    if (gameCount == 0) return;

    TM_EnsureDir(VGAMES_ICONS);
    int created = 0, skipped = 0, iconsDl = 0;

    for (int i = 0; i < gameCount; i++) {
        char safeName[256];
        strncpy(safeName, games[i].name, 255); safeName[255] = 0;
        TM_SanitizeName(safeName, sizeof(safeName));

        char titleID[16];
        snprintf(titleID, sizeof(titleID), "%08x", games[i].appid);

        TM_RegisterIcon(safeName, titleID);

        if (VGames_FindByName(safeName) >= 0) { skipped++; continue; }

        char launchCmd[256];
        snprintf(launchCmd, sizeof(launchCmd), "steam://rungameid/%d", games[i].appid);
        VGames_Add(safeName, titleID, launchCmd, "E", "Games");
        created++;

        if (TM_DownloadSteamIcon(games[i].appid, titleID)) iconsDl++;
    }

    VGames_Save();
    UDataSynth_RebuildAll();
    VGames_Reload();

    if (outCreated) *outCreated = created;
    if (outSkipped) *outSkipped = skipped;
    if (outIcons)   *outIcons   = iconsDl;
}

// Emulators tab helpers

// Build the shell command for an emulator entry: quoted binary, whichever
// option flags are toggled on (table order), the rom flag if the emulator
// wants one (e.g. Dolphin's "-e"), then the quoted rom/eboot path. This is
// a fully-resolved command, same as the xemu ISO path above and Launcher_
// Build's steam:// output - not a custom scheme, so it needs zero changes
// to launch.cpp's dispatch rules.
static void TM_EmuBuildCommand(const EmulatorConfig* conf, const char* binaryPath,
                               const bool* optionOn, const char* romPath,
                               char* out, size_t outSize) {
    char buf[2048];
    size_t len = 0;
    len += snprintf(buf + len, sizeof(buf) - len, "\"%s\"", binaryPath);
    for (int o = 0; o < conf->optionCount && len < sizeof(buf); o++) {
        if (optionOn[o])
            len += snprintf(buf + len, sizeof(buf) - len, " %s", conf->options[o].flag);
    }
    if (conf->romFlag[0] && len < sizeof(buf))
        len += snprintf(buf + len, sizeof(buf) - len, " %s", conf->romFlag);
    if (len < sizeof(buf))
        len += snprintf(buf + len, sizeof(buf) - len, " \"%s\"", romPath);
    strncpy(out, buf, outSize - 1);
    out[outSize - 1] = 0;
}

// Add one game file for the given emulator config. Dedupes by sanitized
// name, same rule the RetroArch/Steam importers use. Returns true if a new
// entry was created.
static bool TM_EmuAddGame(const EmulatorConfig* conf, const char* filePath,
                          const char* binaryPath, const bool* optionOn) {
    char title[256];
    TM_DeriveRomTitle(filePath, title, sizeof(title));

    extern int Title_SanitizeName(const char*, char*, size_t);
    char cleanName[128];
    Title_SanitizeName(title, cleanName, sizeof(cleanName));
    if (!cleanName[0]) return false;
    if (VGames_FindByName(cleanName) >= 0) return false;

    char launch[2048];
    TM_EmuBuildCommand(conf, binaryPath, optionOn, filePath, launch, sizeof(launch));

    char genID[16];
    snprintf(genID, sizeof(genID), "%08x", (unsigned)time(NULL) + (unsigned)rand());
    VGames_Add(cleanName, genID, launch, "E", "Emulators");
    TM_RegisterIcon(cleanName, genID);
    return true;
}

// Add a detected RPCS3 game. Locates EBOOT.BIN inside the game folder
// (digital pkg layout or disc-dump layout) and builds the launch command
// from that, not the folder itself - RPCS3 wants the eboot path.
static bool TM_EmuAddRpcs3(const RPCS3GameEntry& g, const char* binaryPath, const bool* optionOn) {
    char eboot[700];
    if (!RPCS3_LocateEboot(g.folderPath, eboot, sizeof(eboot))) return false;

    const char* rawName = g.displayName[0] ? g.displayName : g.gameId;
    extern int Title_SanitizeName(const char*, char*, size_t);
    char cleanName[128];
    Title_SanitizeName(rawName, cleanName, sizeof(cleanName));
    if (!cleanName[0]) return false;
    if (VGames_FindByName(cleanName) >= 0) return false;

    const EmulatorConfig* conf = Emulator_FindConfig("rpcs3");
    char launch[2048];
    TM_EmuBuildCommand(conf, binaryPath, optionOn, eboot, launch, sizeof(launch));

    char genID[16];
    snprintf(genID, sizeof(genID), "%08x", (unsigned)time(NULL) + (unsigned)rand());
    VGames_Add(cleanName, genID, launch, "E", "Emulators");
    TM_RegisterIcon(cleanName, genID);
    return true;
}

// Depth-limited recursive scan of a folder for files matching conf's
// extension list. Depth cap guards against symlink loops / accidentally
// pointing this at something huge like a whole drive root.
static void TM_EmuScanFolder(const char* dir, const EmulatorConfig* conf,
                             std::vector<std::string>& out, int depth) {
    if (depth > 6) return;
#ifdef _WIN32
    char searchBuf[600];
    snprintf(searchBuf, sizeof(searchBuf), "%s\\*", dir);
    struct _finddata_t fd;
    intptr_t hFind = _findfirst(searchBuf, &fd);
    if (hFind == -1) return;
    do {
        if (fd.name[0] == '.') continue;
        char full[600];
        snprintf(full, sizeof(full), "%s\\%s", dir, fd.name);
        if (fd.attrib & _A_SUBDIR) TM_EmuScanFolder(full, conf, out, depth + 1);
        else if (Emulator_ExtensionMatches(conf, fd.name)) out.push_back(full);
    } while (_findnext(hFind, &fd) == 0);
    _findclose(hFind);
#else
    DIR* d = opendir(dir);
    if (!d) return;
    struct dirent* ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        char full[600];
        snprintf(full, sizeof(full), "%s/%s", dir, ent->d_name);
        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) TM_EmuScanFolder(full, conf, out, depth + 1);
        else if (Emulator_ExtensionMatches(conf, ent->d_name)) out.push_back(full);
    }
    closedir(d);
#endif
}

// ============================================================================
// Title Maker Panel
// ============================================================================

void RenderTitleMaker() {
    if (!g_titleMakerOpen) return;

    // Position and size on first open. Cap max size to the viewport so the
    // title bar can't get pushed off-screen if the user's window is small.
    ImVec2 vp = ImGui::GetMainViewport()->Size;
    float maxW = vp.x - 40.0f, maxH = vp.y - 80.0f;
    if (maxW < 640) maxW = 640;
    if (maxH < 480) maxH = 480;
    ImGui::SetNextWindowPos(ImVec2(20, 40), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(640, 480), ImVec2(maxW, maxH));
    ImGui::SetNextWindowSize(ImVec2(820, maxH < 680 ? maxH : 680), ImGuiCond_FirstUseEver);

    ImGui::Begin("UIX Title Maker", &g_titleMakerOpen);

    // State (persists across frames)
    #define TM_MAX_ENTRIES 512
    struct TmEntry { int vgIndex; char name[128]; char launch[512]; char titleID[16]; char drive[4]; char category[32]; };
    static TmEntry s_entries[TM_MAX_ENTRIES];
    static int s_entryCount = 0;
    static bool s_needsScan = true;
    static int s_selectedIdx = -1;
    static char s_editName[128] = "";
    static char s_editLaunch[512] = "";
    static char s_editTitleID[16] = "";
    static int s_editCategoryIdx = 0;
    static char s_newTitleName[128] = "";
    static char s_statusMsg[256] = "";
    static float s_statusTime = 0;
    static GuiTexture* s_iconTex = NULL;
    static int s_iconTexIdx = -1;
    static char s_searchFilter[128] = "";
    static const char* s_categories[] = { "Games", "Applications", "Homebrew", "Emulators", "Dashboards" };
    static const int s_categoryCount = 5;
    static int s_filterCategoryIdx = -1; // -1 = show all

    // Main-tab bulk selection: separate from s_selectedIdx (the single-item
    // editor selection) so checking boxes for a bulk action doesn't clobber
    // whatever's currently open in the editor panel. Keyed by vgIndex since
    // that's stable across a re-sort/re-filter of s_entries.
    static bool s_bulkChecked[TM_MAX_ENTRIES];
    static int  s_selectAnchor = -1; // s_entries index last touched by a plain/ctrl click; shift-click ranges from here
    static int  s_bulkCategoryIdx = 0;

    // Emulators tab: sub-tab per kEmulatorConfigs entry, RetroArch-style.
    // TODO: g_showEmulatorsTab currently lives here as a local static, so
    // it won't persist across restarts or survive SaveDesktopSettings().
    // For parity with g_showSteamTab / g_showRetroArchTab, add a matching
    // bool to panel_shared.h and Save/LoadDesktopSettings and swap this for
    // that extern.
    static bool s_showEmulatorsTab = true;
    static int  s_emuActiveTab = 0;
    static char s_emuBinaryPaths[kEmulatorConfigCount][512];
    static bool s_emuPathsLoaded = false;
    static bool s_emuOptionOn[kEmulatorConfigCount][8]; // matches EmulatorConfig::options
    static bool s_emuOptionsInit = false;
    static ImGui::FileBrowser s_emuFileBrowser(ImGuiFileBrowserFlags_CloseOnEsc);
    static ImGui::FileBrowser s_emuBinBrowser(ImGuiFileBrowserFlags_CloseOnEsc);
    static ImGui::FileBrowser s_emuFolderBrowser(
        ImGuiFileBrowserFlags_CloseOnEsc | ImGuiFileBrowserFlags_SelectDirectory);
    static bool s_emuBrowsersInit = false;
    static int  s_emuBrowserTarget = -1; // which sub-tab s_emuFileBrowser/s_emuBinBrowser/s_emuFolderBrowser is for

    // RPCS3 "Detect Installed Games" dialog state
    static RPCS3GameEntry s_rpcs3Detected[256];
    static int  s_rpcs3DetectedCount = 0;
    static bool s_rpcs3CheckedIdx[256];
    static bool s_showRpcs3DetectDialog = false;

    // SteamGridDB: settings + the Next/Prev icon-picker session
    static SGDBSettings s_sgdb;
    static bool s_sgdbLoaded = false;
    static bool s_sgdbShowKey = false;

    static bool s_sgdbDialogOpen = false;
    static int  s_sgdbSessionVg[TM_MAX_ENTRIES]; // vgIndex of each game in this picker session
    static int  s_sgdbSessionCount = 0;
    static int  s_sgdbSessionPos = 0;
    static SGDBGameMatch s_sgdbMatches[16];
    static int  s_sgdbMatchCount = 0;
    static int  s_sgdbMatchIdx = -1;
    static int  s_sgdbStage = 0; // 0 = confirm match, 1 = pick icon
    static SGDBAsset s_sgdbAssets[5];
    static int  s_sgdbAssetCount = 0;
    static GuiTexture* s_sgdbThumbTex[5] = { NULL, NULL, NULL, NULL, NULL };
    static char s_sgdbThumbPath[5][300];
    static char s_sgdbStatus[256] = "";

    if (!s_sgdbLoaded) {
        SGDB_LoadSettings(&s_sgdb);
        s_sgdbLoaded = true;
    }

    if (!s_emuPathsLoaded) {
        Emulator_LoadPaths(s_emuBinaryPaths, kEmulatorConfigCount);
        for (int e = 0; e < kEmulatorConfigCount; e++) {
            if (s_emuBinaryPaths[e][0]) continue;
            char found[512];
            if (Emulator_FindBinary(&kEmulatorConfigs[e], found, sizeof(found))) {
                strncpy(s_emuBinaryPaths[e], found, sizeof(s_emuBinaryPaths[e]) - 1);
            }
        }
        s_emuPathsLoaded = true;
    }
    if (!s_emuOptionsInit) {
        for (int e = 0; e < kEmulatorConfigCount; e++)
            for (int o = 0; o < kEmulatorConfigs[e].optionCount && o < 8; o++)
                s_emuOptionOn[e][o] = kEmulatorConfigs[e].options[o].defaultOn;
        s_emuOptionsInit = true;
    }
    if (!s_emuBrowsersInit) {
        s_emuFileBrowser.SetTitle("Select game file");
        s_emuBinBrowser.SetTitle("Select emulator binary");
        s_emuFolderBrowser.SetTitle("Select folder to scan for games");
        s_emuBrowsersInit = true;
    }

    // File browsers
    static ImGui::FileBrowser s_iconBrowser(ImGuiFileBrowserFlags_CloseOnEsc);
    static ImGui::FileBrowser s_appBrowser(ImGuiFileBrowserFlags_CloseOnEsc);
    static bool s_iconBrowserInit = false;
    static bool s_appBrowserInit = false;
    static ImGui::FileBrowser s_isoBrowser(ImGuiFileBrowserFlags_CloseOnEsc);
    static bool s_isoBrowserInit = false;
    static ImGui::FileBrowser s_steamBrowser(
        ImGuiFileBrowserFlags_CloseOnEsc | ImGuiFileBrowserFlags_SelectDirectory);
    static bool s_steamBrowserInit = false;
    static char s_isoPath[512] = "";
    static char s_isoStatus[256] = "";
    static bool s_isoParsed = false;
    static XisoTitleInfo s_isoInfo = {};
    static GuiTexture* s_isoPreviewTex = NULL;
    static bool s_showIsoResult = false;

    if (!s_iconBrowserInit) {
        s_iconBrowser.SetTitle("Select Icon Image");
        s_iconBrowser.SetTypeFilters({ ".jpg", ".jpeg", ".png", ".bmp", ".JPG", ".PNG" });
        s_iconBrowserInit = true;
    }
    if (!s_appBrowserInit) {
        s_appBrowser.SetTitle("Select Application");
        s_appBrowserInit = true;
    }
    if (!s_isoBrowserInit) {
        s_isoBrowser.SetTitle("Select Xbox ISO");
        s_isoBrowser.SetTypeFilters({ ".iso", ".ISO" });
        s_isoBrowserInit = true;
    }
    if (!s_steamBrowserInit) {
        s_steamBrowser.SetTitle("Select Steam install folder (contains steamapps/)");
        s_steamBrowserInit = true;
    }

    // Case-insensitive substring search helper
    auto CaseStrStr = [](const char* haystack, const char* needle) -> bool {
        if (!needle[0]) return true;
        for (const char* h = haystack; *h; h++) {
            const char* a = h; const char* b = needle;
            while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; }
            if (!*b) return true;
        }
        return false;
    };

    // Scan function: populate entries from VGames database
    auto DoScan = [&]() {
        s_entryCount = 0;
        s_selectedIdx = -1;
        s_iconTexIdx = -1;
        GuiTextureDestroy(&s_iconTex);

        VGames_Reload();

        // Reset dashboard icon cache so it picks up new/changed entries
        extern bool s_iconsLoaded;
        s_iconsLoaded = false;

        for (int i = 0; i < g_vgames.count && s_entryCount < TM_MAX_ENTRIES; i++) {
            VirtualGame& vg = g_vgames.games[i];
            if (!vg.valid) continue;
            TmEntry& e = s_entries[s_entryCount];
            e.vgIndex = i;
            strncpy(e.name, vg.name, sizeof(e.name) - 1); e.name[sizeof(e.name) - 1] = 0;
            strncpy(e.launch, vg.launch, sizeof(e.launch) - 1); e.launch[sizeof(e.launch) - 1] = 0;
            strncpy(e.titleID, vg.titleID, sizeof(e.titleID) - 1); e.titleID[sizeof(e.titleID) - 1] = 0;
            strncpy(e.drive, vg.drive, sizeof(e.drive) - 1); e.drive[sizeof(e.drive) - 1] = 0;
            strncpy(e.category, vg.category, sizeof(e.category) - 1); e.category[sizeof(e.category) - 1] = 0;
            s_entryCount++;
        }

        // Sort alphabetically by name
        std::sort(s_entries, s_entries + s_entryCount, [](const TmEntry& a, const TmEntry& b) {
            return strcasecmp(a.name, b.name) < 0;
        });
    };

    if (s_needsScan) { DoScan(); s_needsScan = false; }

    // SteamGridDB: search for whichever game is current in the picker
    // session and land on the confirm stage - deliberately does NOT fetch
    // assets yet, so a wrong top match doesn't cost an extra API call /
    // image downloads before the user gets a chance to correct it.
    auto SgdbSearchCurrent = [&]() {
        for (int t = 0; t < 5; t++) { GuiTextureDestroy(&s_sgdbThumbTex[t]); s_sgdbThumbPath[t][0] = 0; }
        s_sgdbAssetCount = 0;
        s_sgdbMatchCount = 0;
        s_sgdbMatchIdx = -1;
        s_sgdbStatus[0] = 0;
        s_sgdbStage = 0; // confirm

        if (s_sgdbSessionPos >= s_sgdbSessionCount) return;
        int vgIndex = s_sgdbSessionVg[s_sgdbSessionPos];
        if (vgIndex < 0 || vgIndex >= g_vgames.count || !g_vgames.games[vgIndex].valid) return;
        const char* name = g_vgames.games[vgIndex].name;

        long searchStatus = 0;
        s_sgdbMatchCount = SGDB_Search(s_sgdb.apiKey, name, s_sgdbMatches, 16, &searchStatus);
        if (s_sgdbMatchCount == 0) {
            if (searchStatus != 200)
                snprintf(s_sgdbStatus, sizeof(s_sgdbStatus),
                         "SteamGridDB search failed (HTTP %ld) - check your API key", searchStatus);
            else
                snprintf(s_sgdbStatus, sizeof(s_sgdbStatus), "No SteamGridDB match for \"%s\"", name);
            return;
        }
        s_sgdbMatchIdx = 0; // SGDB's autocomplete is already relevance-ranked
    };

    // Confirm step's "Yes" action: fetch + download thumbnails for the
    // chosen match and move to the pick stage.
    auto SgdbFetchAssetsForMatch = [&](int matchIdx) {
        for (int t = 0; t < 5; t++) { GuiTextureDestroy(&s_sgdbThumbTex[t]); s_sgdbThumbPath[t][0] = 0; }
        s_sgdbMatchIdx = matchIdx;
        s_sgdbStatus[0] = 0;
        s_sgdbStage = 1; // pick

        int want = (s_sgdb.optionCount > 0 && s_sgdb.optionCount <= 5) ? s_sgdb.optionCount : 5;
        long assetStatus = 0;
        s_sgdbAssetCount = SGDB_GetAssets(s_sgdb.apiKey, s_sgdbMatches[matchIdx].id, s_sgdb.assetType,
                                          s_sgdbAssets, want, &assetStatus);
        if (s_sgdbAssetCount == 0) {
            if (assetStatus != 200)
                snprintf(s_sgdbStatus, sizeof(s_sgdbStatus),
                         "SteamGridDB %s request failed (HTTP %ld)",
                         s_sgdb.assetType == 1 ? "grids" : "icons", assetStatus);
            else
                snprintf(s_sgdbStatus, sizeof(s_sgdbStatus),
                         "No %s uploaded for \"%s\" on SteamGridDB - try Grids in Settings",
                         s_sgdb.assetType == 1 ? "grids" : "icons", s_sgdbMatches[matchIdx].name);
            return;
        }
        TM_EnsureDir(VGAMES_ICONS);
        int decoded = 0;
        for (int t = 0; t < s_sgdbAssetCount; t++) {
            char tmpPath[300];
            snprintf(tmpPath, sizeof(tmpPath), "%s/sgdb_tmp_%d.png", VGAMES_ICONS, t);
            if (!Http_GetToFile(s_sgdbAssets[t].thumbUrl, tmpPath)) continue;
            int w, h, ch;
            unsigned char* pixels = stbi_load(tmpPath, &w, &h, &ch, 4);
            if (pixels) {
                s_sgdbThumbTex[t] = GuiTextureCreate(w, h, pixels);
                stbi_image_free(pixels);
                decoded++;
            }
            strncpy(s_sgdbThumbPath[t], tmpPath, sizeof(s_sgdbThumbPath[t]) - 1);
        }
        if (decoded == 0) {
            snprintf(s_sgdbStatus, sizeof(s_sgdbStatus),
                     "Found %d image%s but couldn't display any (download or decode failed)",
                     s_sgdbAssetCount, s_sgdbAssetCount == 1 ? "" : "s");
        }
    };

    // Open the picker for an arbitrary list of games (1 for the single-item
    // editor's button, N for the bulk panel's) and kick off the first search.
    auto SgdbStartSession = [&](const int* vgList, int count) {
        s_sgdbSessionCount = 0;
        for (int i = 0; i < count && s_sgdbSessionCount < TM_MAX_ENTRIES; i++)
            s_sgdbSessionVg[s_sgdbSessionCount++] = vgList[i];
        s_sgdbSessionPos = 0;
        s_sgdbDialogOpen = true;
        SgdbSearchCurrent();
    };

    // Header
    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "UIX Title Maker");
    ImGui::SameLine(ImGui::GetWindowWidth() - 100);
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "F3 to close");
    ImGui::Separator();

    ImGui::BeginTabBar("TMTabs");
    if (ImGui::BeginTabItem("Main")) {

    if (ImGui::CollapsingHeader("Optional Tabs")) {
        if (ImGui::Checkbox("Steam",     &g_showSteamTab))     SaveDesktopSettings();
        if (ImGui::Checkbox("RetroArch", &g_showRetroArchTab)) SaveDesktopSettings();
        ImGui::Checkbox("Emulators", &s_showEmulatorsTab); // not yet persisted, see TODO above
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
            "Hiding a tab only hides its authoring UI. Existing entries stay listed in Main.");
    }

    // xemu path setting
    {
        ImGui::Text("xemu:");
        ImGui::SameLine();
        // Reserve room for [Save][Find] + their gaps + a 12px right margin so
        // the rightmost button doesn't snug against the window edge.
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 150);
        ImGui::InputText("##xemupath", s_xemuPath, sizeof(s_xemuPath));
        ImGui::SameLine();
        if (ImGui::SmallButton("Save##xemu")) {
            SaveDesktopSettings();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Find##xemu")) {
            const char* tryPaths[] = {
#ifdef __APPLE__
                "/Applications/xemu.app",
                "/opt/homebrew/bin/xemu",
                "/usr/local/bin/xemu",
#elif defined(_WIN32)
                "C:\\Program Files\\xemu\\xemu.exe",
                "C:\\Program Files (x86)\\xemu\\xemu.exe",
#else
                "/usr/bin/xemu",
                "/usr/local/bin/xemu",
                "/snap/bin/xemu",
#endif
                NULL
            };
            bool found = false;
            for (int i = 0; tryPaths[i] && !found; i++) {
                struct stat st;
                if (stat(tryPaths[i], &st) == 0) {
                    strncpy(s_xemuPath, tryPaths[i], sizeof(s_xemuPath) - 1);
                    found = true;
                }
            }
            if (!found) {
                strncpy(s_statusMsg, "xemu not found in common locations", sizeof(s_statusMsg) - 1);
                s_statusTime = 3.0f;
            }
        }
        if (s_xemuPath[0] == 0)
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f), "Set xemu path to enable ISO launching");
    }

    ImGui::Separator();

    // Search and category filter
    {
        ImGui::Text("Search:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(180);
        ImGui::InputText("##search", s_searchFilter, sizeof(s_searchFilter));
        ImGui::SameLine();
        ImGui::Text("Category:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(130);
        const char* filterLabel = (s_filterCategoryIdx < 0) ? "All" : s_categories[s_filterCategoryIdx];
        if (ImGui::BeginCombo("##catfilter", filterLabel)) {
            if (ImGui::Selectable("All", s_filterCategoryIdx < 0))
                s_filterCategoryIdx = -1;
            for (int c = 0; c < s_categoryCount; c++) {
                if (ImGui::Selectable(s_categories[c], s_filterCategoryIdx == c))
                    s_filterCategoryIdx = c;
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::Text("%d titles", s_entryCount);
    }
    ImGui::Separator();

    // Compute the currently visible (search + category filtered) rows once,
    // so shift-click range-select operates on what's actually on screen
    // rather than raw array index (which would include filtered-out rows).
    int visIdx[TM_MAX_ENTRIES];
    int visCount = 0;
    for (int i = 0; i < s_entryCount; i++) {
        TmEntry& e = s_entries[i];
        if (s_searchFilter[0] && !CaseStrStr(e.name, s_searchFilter)) continue;
        if (s_filterCategoryIdx >= 0 && strcasecmp(e.category, s_categories[s_filterCategoryIdx]) != 0) continue;
        visIdx[visCount++] = i;
    }

    int bulkCheckedCount = 0;
    for (int i = 0; i < g_vgames.count; i++)
        if (g_vgames.games[i].valid && s_bulkChecked[i]) bulkCheckedCount++;

    // Small convenience row above the list - always available, no separate
    // "select mode" to toggle. Selection itself is driven by click / ctrl+
    // click / shift+click on the rows below, same as a file manager.
    {
        if (ImGui::SmallButton("Select All (filtered)")) {
            for (int v = 0; v < visCount; v++) s_bulkChecked[s_entries[visIdx[v]].vgIndex] = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear Selection")) {
            memset(s_bulkChecked, 0, sizeof(s_bulkChecked));
            s_selectAnchor = -1;
        }
        ImGui::SameLine();
        if (bulkCheckedCount > 0)
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                "(%d selected - shift/ctrl+click to adjust)", bulkCheckedCount);
        else
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                "(click a title; shift/ctrl+click to multi-select)");
    }
    ImGui::Separator();

    // Split view: title list on left, editor / bulk panel on right
    float listWidth = 260.0f;
    ImGui::BeginChild("TitleList", ImVec2(listWidth, -ImGui::GetFrameHeightWithSpacing() * 1.6f), true);
    for (int vp = 0; vp < visCount; vp++) {
        int i = visIdx[vp];
        TmEntry& e = s_entries[i];

        bool hasLaunch = e.launch[0] != 0;
        if (hasLaunch)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
        else
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));

        bool checked = s_bulkChecked[e.vgIndex];
        char listLabel[256];
        snprintf(listLabel, sizeof(listLabel), "%s  [%s]", e.name, e.category);
        ImGui::PushID(i);
        if (ImGui::Selectable(listLabel, checked)) {
            ImGuiIO& io = ImGui::GetIO();
            bool ctrl  = io.KeyCtrl || io.KeySuper; // KeySuper so Cmd+click works on macOS
            bool shift = io.KeyShift;

            if (shift && s_selectAnchor >= 0) {
                int anchorPos = vp;
                for (int k = 0; k < visCount; k++)
                    if (visIdx[k] == s_selectAnchor) { anchorPos = k; break; }
                int lo = (anchorPos < vp) ? anchorPos : vp;
                int hi = (anchorPos < vp) ? vp : anchorPos;
                if (!ctrl) memset(s_bulkChecked, 0, sizeof(s_bulkChecked));
                for (int k = lo; k <= hi; k++) s_bulkChecked[s_entries[visIdx[k]].vgIndex] = true;
            } else if (ctrl) {
                s_bulkChecked[e.vgIndex] = !s_bulkChecked[e.vgIndex];
                s_selectAnchor = i;
            } else {
                memset(s_bulkChecked, 0, sizeof(s_bulkChecked));
                s_bulkChecked[e.vgIndex] = true;
                s_selectAnchor = i;
            }

            // Re-derive the editor's single-item selection: only populate
            // it when exactly one row is checked overall (not just within
            // the current filter), so a multi-select doesn't leave stale
            // single-item edit fields showing.
            int newCount = 0, onlyVgIndex = -1;
            for (int gi = 0; gi < g_vgames.count; gi++) {
                if (!g_vgames.games[gi].valid || !s_bulkChecked[gi]) continue;
                newCount++;
                onlyVgIndex = gi;
            }

            if (newCount == 1) {
                s_selectedIdx = -1;
                for (int k = 0; k < s_entryCount; k++) {
                    if (s_entries[k].vgIndex == onlyVgIndex) { s_selectedIdx = k; break; }
                }
                VirtualGame& vg = g_vgames.games[onlyVgIndex];
                strncpy(s_editName, vg.name, sizeof(s_editName) - 1);
                strncpy(s_editLaunch, vg.launch, sizeof(s_editLaunch) - 1);
                strncpy(s_editTitleID, vg.titleID, sizeof(s_editTitleID) - 1);
                s_editCategoryIdx = 0;
                for (int c = 0; c < s_categoryCount; c++) {
                    if (strcasecmp(vg.category, s_categories[c]) == 0) { s_editCategoryIdx = c; break; }
                }
                s_iconTexIdx = -1; // force icon reload
            } else {
                s_selectedIdx = -1;
            }
        }
        ImGui::PopID();
        ImGui::PopStyleColor();

        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("%s", e.name);
            if (hasLaunch)
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Launch: %s", e.launch);
            ImGui::Text("TitleID: %s  Drive: %s", e.titleID, e.drive);
            ImGui::EndTooltip();
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Editor panel
    ImGui::BeginChild("TitleEditor", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 1.6f), true);
    if (s_selectedIdx >= 0 && s_selectedIdx < s_entryCount) {
        TmEntry& sel = s_entries[s_selectedIdx];
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", sel.name);
        ImGui::Separator();

        // Icon preview
        if (s_iconTexIdx != s_selectedIdx) {
            s_iconTexIdx = s_selectedIdx;
            GuiTextureDestroy(&s_iconTex);
            const char* iconPath = VGames_GetIconPath(sel.vgIndex);
            if (iconPath) {
                int w, h, ch;
                unsigned char* pixels = stbi_load(iconPath, &w, &h, &ch, 4);
                if (pixels) {
                    s_iconTex = GuiTextureCreate(w, h, pixels);
                    stbi_image_free(pixels);
                }
            }
        }

        if (s_iconTex) {
            ImGui::Image((ImTextureID)(intptr_t)GuiTextureImId(s_iconTex), ImVec2(64, 64));
            ImGui::SameLine();
        } else {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "[No icon]");
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("Browse Icon..")) {
            s_iconBrowser.Open();
        }
        if (s_sgdb.apiKey[0] && s_sgdb.optionCount > 0) {
            ImGui::SameLine();
            if (ImGui::SmallButton("SteamGridDB..")) {
                int one[1] = { sel.vgIndex };
                SgdbStartSession(one, 1);
            }
        }
        ImGui::Spacing();

        // Edit fields
        ImGui::Text("Name:");
        ImGui::SameLine(80);
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##editname", s_editName, sizeof(s_editName));

        ImGui::Text("Launch:");
        ImGui::SameLine(80);
        ImGui::SetNextItemWidth(-60);
        ImGui::InputText("##editlaunch", s_editLaunch, sizeof(s_editLaunch));
        ImGui::SameLine();
        if (ImGui::SmallButton("Browse..")) {
            s_appBrowser.Open();
        }

        ImGui::Text("Category:");
        ImGui::SameLine(80);
        ImGui::SetNextItemWidth(150);
        if (ImGui::BeginCombo("##editcat", s_categories[s_editCategoryIdx])) {
            for (int c = 0; c < s_categoryCount; c++) {
                if (ImGui::Selectable(s_categories[c], s_editCategoryIdx == c))
                    s_editCategoryIdx = c;
            }
            ImGui::EndCombo();
        }

        ImGui::Text("Title ID:");
        ImGui::SameLine(80);
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s", s_editTitleID);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Action buttons
        if (ImGui::Button("Save", ImVec2(100, 0))) {
            // Sanitize before storing so the dashboard's text atlas can
            // render the result. Lossy on purpose: smart quotes, em
            // dashes, ™/®/©, emoji, CJK all get normalized or dropped
            // rather than rendered as tofu boxes.
            extern int Title_SanitizeName(const char*, char*, size_t);
            char cleanName[sizeof(s_editName)];
            Title_SanitizeName(s_editName, cleanName, sizeof(cleanName));
            if (cleanName[0]) {
                strncpy(s_editName, cleanName, sizeof(s_editName) - 1);
                s_editName[sizeof(s_editName) - 1] = '\0';
            }

            VGames_Update(sel.vgIndex, s_editName, s_editTitleID, s_editLaunch,
                          g_vgames.games[sel.vgIndex].drive, s_categories[s_editCategoryIdx]);
            VGames_Save(); UDataSynth_RebuildAll();

            // Update Icons.ini for Xbox-side compatibility
            {
                static char iconKeys[TM_MAX_ICONS][128];
                static char iconVals[TM_MAX_ICONS][128];
                int iconCount = TM_ReadIconsIni(iconKeys, iconVals);
                bool found = false;
                for (int i = 0; i < iconCount; i++) {
                    if (strcasecmp(iconKeys[i], s_editName) == 0) {
                        strncpy(iconVals[i], s_editTitleID, 127);
                        found = true; break;
                    }
                }
                if (!found && iconCount < TM_MAX_ICONS) {
                    strncpy(iconKeys[iconCount], s_editName, 127);
                    strncpy(iconVals[iconCount], s_editTitleID, 127);
                    iconCount++;
                }
                TM_WriteIconsIni(iconKeys, iconVals, iconCount);
            }

            // Refresh local entry cache
            strncpy(sel.name, s_editName, sizeof(sel.name) - 1);
            strncpy(sel.launch, s_editLaunch, sizeof(sel.launch) - 1);
            strncpy(sel.category, s_categories[s_editCategoryIdx], sizeof(sel.category) - 1);

            snprintf(s_statusMsg, sizeof(s_statusMsg), "Saved: %s", s_editName);
            s_statusTime = 3.0f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Test Launch", ImVec2(110, 0)) && s_editLaunch[0]) {
            // Fire-and-forget; don't tear down the dashboard for a test launch.
            // DesktopLaunch fills g_launchLastResult with a status string
            // ("Launched: ..." or "CreateProcess failed (error 2): ..."),
            // so we surface that verbatim instead of guessing success.
            extern void DesktopLaunch(const char*);
            extern char g_launchLastResult[256];
            g_launchLastResult[0] = 0;
            DesktopLaunch(s_editLaunch);
            snprintf(s_statusMsg, sizeof(s_statusMsg), "%s",
                     g_launchLastResult[0] ? g_launchLastResult : s_editLaunch);
            s_statusTime = 6.0f;
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.1f, 0.1f, 1.0f));
        if (ImGui::Button("Delete", ImVec2(80, 0))) {
            ImGui::OpenPopup("ConfirmDelete");
        }
        ImGui::PopStyleColor();

        if (ImGui::BeginPopup("ConfirmDelete")) {
            ImGui::Text("Delete '%s' from library?", sel.name);
            if (ImGui::Button("Yes, Delete")) {
                const char* delName = sel.name;
                VGames_DeleteByName(delName);

                // Remove from Icons.ini
                {
                    static char iconKeys[TM_MAX_ICONS][128];
                    static char iconVals[TM_MAX_ICONS][128];
                    int iconCount = TM_ReadIconsIni(iconKeys, iconVals);
                    int newCount = 0;
                    for (int i = 0; i < iconCount; i++) {
                        if (strcasecmp(iconKeys[i], delName) != 0) {
                            if (i != newCount) {
                                strncpy(iconKeys[newCount], iconKeys[i], 127);
                                strncpy(iconVals[newCount], iconVals[i], 127);
                            }
                            newCount++;
                        }
                    }
                    TM_WriteIconsIni(iconKeys, iconVals, newCount);
                }

                snprintf(s_statusMsg, sizeof(s_statusMsg), "Deleted: %s", delName);
                s_statusTime = 3.0f;
                s_needsScan = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    } else if (bulkCheckedCount > 1) {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%d titles selected", bulkCheckedCount);
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "shift/ctrl+click in the list to adjust");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Category:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(160);
        if (ImGui::BeginCombo("##bulkcat", s_categories[s_bulkCategoryIdx])) {
            for (int c = 0; c < s_categoryCount; c++)
                if (ImGui::Selectable(s_categories[c], s_bulkCategoryIdx == c))
                    s_bulkCategoryIdx = c;
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("Apply to Selected")) {
            int changed = 0;
            for (int i = 0; i < g_vgames.count; i++) {
                if (!g_vgames.games[i].valid || !s_bulkChecked[i]) continue;
                VGames_Update(i, g_vgames.games[i].name, g_vgames.games[i].titleID,
                              g_vgames.games[i].launch, g_vgames.games[i].drive,
                              s_categories[s_bulkCategoryIdx]);
                changed++;
            }
            if (changed > 0) { VGames_Save(); UDataSynth_RebuildAll(); s_needsScan = true; }
            snprintf(s_statusMsg, sizeof(s_statusMsg), "Recategorized %d title%s",
                     changed, changed == 1 ? "" : "s");
            s_statusTime = 3.0f;
        }

        ImGui::Spacing();
        if (s_sgdb.apiKey[0] && s_sgdb.optionCount > 0) {
            if (ImGui::Button("SteamGridDB Icons for Selected...")) {
                static int vgList[TM_MAX_ENTRIES];
                int n = 0;
                for (int i = 0; i < g_vgames.count && n < TM_MAX_ENTRIES; i++)
                    if (g_vgames.games[i].valid && s_bulkChecked[i]) vgList[n++] = i;
                SgdbStartSession(vgList, n);
            }
        } else {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                "Set an API key under Settings to fetch icons from SteamGridDB.");
        }

        ImGui::Spacing();
        if (ImGui::Button("Clear Icons for Selected")) {
            int changed = 0;
            for (int i = 0; i < g_vgames.count; i++) {
                if (!g_vgames.games[i].valid || !s_bulkChecked[i]) continue;
                char pngPath[600], jpgPath[600];
                snprintf(pngPath, sizeof(pngPath), "%s/%s.png", VGAMES_ICONS, g_vgames.games[i].titleID);
                snprintf(jpgPath, sizeof(jpgPath), "%s/%s.jpg", VGAMES_ICONS, g_vgames.games[i].titleID);
                if (remove(pngPath) == 0) changed++;
                if (remove(jpgPath) == 0) changed++;
            }
            s_iconTexIdx = -1;
            snprintf(s_statusMsg, sizeof(s_statusMsg), "Cleared icons for selected titles");
            s_statusTime = 3.0f;
        }

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.1f, 0.1f, 1.0f));
        if (ImGui::Button("Delete Selected")) ImGui::OpenPopup("ConfirmBulkDelete");
        ImGui::PopStyleColor();

        if (ImGui::BeginPopup("ConfirmBulkDelete")) {
            ImGui::Text("Delete %d title%s from library? This can't be undone.",
                        bulkCheckedCount, bulkCheckedCount == 1 ? "" : "s");
            if (ImGui::Button("Yes, Delete")) {
                // Snapshot names first: VGames_DeleteByName compacts the
                // array, so deleting by live index while iterating would
                // skip entries / shift indices out from under s_bulkChecked
                // mid-loop.
                static char toDelete[TM_MAX_ENTRIES][128];
                int delCount = 0;
                for (int i = 0; i < g_vgames.count && delCount < TM_MAX_ENTRIES; i++) {
                    if (!g_vgames.games[i].valid || !s_bulkChecked[i]) continue;
                    strncpy(toDelete[delCount], g_vgames.games[i].name, 127);
                    toDelete[delCount][127] = 0;
                    delCount++;
                }
                for (int i = 0; i < delCount; i++) VGames_DeleteByName(toDelete[i]);
                VGames_Save(); UDataSynth_RebuildAll();
                memset(s_bulkChecked, 0, sizeof(s_bulkChecked));
                s_selectAnchor = -1;
                s_needsScan = true;
                snprintf(s_statusMsg, sizeof(s_statusMsg), "Deleted %d title%s",
                         delCount, delCount == 1 ? "" : "s");
                s_statusTime = 3.0f;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    } else {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Select a title from the list,");
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "or create a new one below.");
    }
    ImGui::EndChild();

    // Bottom bar: new title + ISO import + Steam import
    ImGui::Separator();
    ImGui::Text("New:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180);
    ImGui::InputText("##newtitle", s_newTitleName, sizeof(s_newTitleName));
    ImGui::SameLine();
    if (ImGui::Button("Create") && s_newTitleName[0]) {
        // Generate titleID from current time
        char genID[16];
        snprintf(genID, sizeof(genID), "%08x", (unsigned)time(NULL));

        // Determine category from filter or default to "Games"
        const char* newCat = (s_filterCategoryIdx >= 0) ? s_categories[s_filterCategoryIdx] : "Games";

        // Run the new title through the same sanitize pass that Save uses
        // so the dashboard atlas can render it.
        extern int Title_SanitizeName(const char*, char*, size_t);
        char cleanNew[sizeof(s_newTitleName)];
        Title_SanitizeName(s_newTitleName, cleanNew, sizeof(cleanNew));
        if (!cleanNew[0]) {
            snprintf(s_statusMsg, sizeof(s_statusMsg),
                     "Title \"%s\" sanitizes to empty, pick another", s_newTitleName);
            s_statusTime = 4.0f;
        } else {
            VGames_Add(cleanNew, genID, "", "E", newCat);
            VGames_Save(); UDataSynth_RebuildAll();
            snprintf(s_statusMsg, sizeof(s_statusMsg), "Created: %s (ID: %s)", cleanNew, genID);
            s_statusTime = 3.0f;
            s_newTitleName[0] = 0;
            s_needsScan = true;
        }
    }
    ImGui::SameLine();

    // Add ISO button, use the global theme so it matches Create / Import.
    if (ImGui::Button("Add ISO")) {
        ImGui::OpenPopup("AddISOPopup");
    }

    // ISO file browser + result window
    {
        if (ImGui::BeginPopup("AddISOPopup")) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
                "ISOs are launched via xemu, not natively in this app.");
            ImGui::Spacing();
            s_isoBrowser.Open();
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        s_isoBrowser.Display();

        if (s_isoBrowser.HasSelected()) {
            std::string selected = s_isoBrowser.GetSelected().string();
            strncpy(s_isoPath, selected.c_str(), sizeof(s_isoPath) - 1);
            s_isoBrowser.ClearSelected();

            GuiTextureDestroy(&s_isoPreviewTex);
            XisoFreeTitleInfo(&s_isoInfo);
            s_isoInfo = XisoGetTitleInfo(s_isoPath);
            s_isoParsed = true;

            if (s_isoInfo.valid) {
                snprintf(s_isoStatus, sizeof(s_isoStatus), "Found: %s (ID: %08X)",
                         s_isoInfo.titleName, s_isoInfo.titleId);
                if (s_isoInfo.titleImageRGBA) {
                    s_isoPreviewTex = GuiTextureCreate(
                        s_isoInfo.titleImageWidth, s_isoInfo.titleImageHeight,
                        s_isoInfo.titleImageRGBA);
                }
                s_showIsoResult = true;
            } else {
                snprintf(s_isoStatus, sizeof(s_isoStatus), "ERROR: Could not parse ISO; not a valid Xbox disc image");
                s_showIsoResult = true;
            }
        }

        // Result window after parsing
        if (s_showIsoResult) {
            ImGui::SetNextWindowSize(ImVec2(480, 220), ImGuiCond_Always);
            if (ImGui::Begin("Add Xbox ISO", &s_showIsoResult, ImGuiWindowFlags_NoResize)) {
                if (s_isoParsed && s_isoInfo.valid) {
                    if (s_isoPreviewTex) {
                        ImGui::Image((ImTextureID)(intptr_t)GuiTextureImId(s_isoPreviewTex), ImVec2(128, 128));
                        ImGui::SameLine();
                    }
                    ImGui::BeginGroup();
                    ImGui::Text("Title: %s", s_isoInfo.titleName);
                    ImGui::Text("Title ID: %08X", s_isoInfo.titleId);
                    ImGui::Spacing();
                    if (s_isoInfo.titleImageRGBA)
                        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Title image extracted");
                    else
                        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f), "No title image in XBE");
                    ImGui::EndGroup();

                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    if (ImGui::Button("Add to Library", ImVec2(140, 0))) {
                        // Sanitize name
                        char safeName[128];
                        strncpy(safeName, s_isoInfo.titleName, 127); safeName[127] = 0;
                        TM_SanitizeName(safeName, sizeof(safeName));

                        char titleIdHex[16];
                        snprintf(titleIdHex, sizeof(titleIdHex), "%08x", s_isoInfo.titleId);

                        // Build xemu launch command
                        char xemuLaunchCmd[1024] = "";
                        {
                            char xemuBin[512] = "";
                            if (s_xemuPath[0]) {
                                size_t plen = strlen(s_xemuPath);
                                if (plen > 4 && strcmp(s_xemuPath + plen - 4, ".app") == 0)
                                    snprintf(xemuBin, sizeof(xemuBin), "%s/Contents/MacOS/xemu", s_xemuPath);
                                else
                                    strncpy(xemuBin, s_xemuPath, sizeof(xemuBin) - 1);
                            }
                            if (xemuBin[0])
                                snprintf(xemuLaunchCmd, sizeof(xemuLaunchCmd),
                                    "\"%s\" -machine xbox,short-animation=on -dvd_path \"%s\"", xemuBin, s_isoPath);
                            else
                                snprintf(xemuLaunchCmd, sizeof(xemuLaunchCmd),
                                    "xemu -machine xbox,short-animation=on -dvd_path \"%s\"", s_isoPath);
                        }

                        VGames_Add(safeName, titleIdHex, xemuLaunchCmd, "E", "Games");
                        VGames_Save(); UDataSynth_RebuildAll();

                        // Save icon to VGAMES_ICONS/{titleID}.jpg
                        if (s_isoInfo.titleImageRGBA) {
                            TM_EnsureDir(VGAMES_ICONS);
                            char iconPath[512];
                            snprintf(iconPath, sizeof(iconPath), "%s/%s.jpg", VGAMES_ICONS, titleIdHex);
                            XisoSaveTitleImage(&s_isoInfo, iconPath);
                        }

                        snprintf(s_statusMsg, sizeof(s_statusMsg), "Added: %s", safeName);
                        s_statusTime = 3.0f;
                        s_needsScan = true;
                        s_showIsoResult = false;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(100, 0))) {
                        s_showIsoResult = false;
                    }
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", s_isoStatus);
                    ImGui::Spacing();
                    if (ImGui::Button("OK")) s_showIsoResult = false;
                }
            }
            ImGui::End();
        }
    }


    // Bulk-cleanup button: re-runs Title_SanitizeName over every existing
    // games.ini entry. Useful for libraries imported before the sanitize
    // pipeline existed, or after refining the substitution table. one
    // click reconciles the dashboard view to what the atlas can actually
    // render.
    ImGui::SameLine();
    if (ImGui::Button("Fix Names")) {
        extern int  Title_SanitizeName(const char*, char*, size_t);
        extern bool Title_NeedsSanitize(const char*);
        int fixed = 0;
        for (int i = 0; i < g_vgames.count; i++) {
            VirtualGame& g = g_vgames.games[i];
            if (!g.valid) continue;
            if (!Title_NeedsSanitize(g.name)) continue;
            char clean[sizeof(g.name)];
            Title_SanitizeName(g.name, clean, sizeof(clean));
            if (clean[0] && strcmp(clean, g.name) != 0) {
                strncpy(g.name, clean, sizeof(g.name) - 1);
                g.name[sizeof(g.name) - 1] = '\0';
                fixed++;
            }
        }
        if (fixed > 0) {
            VGames_Save(); UDataSynth_RebuildAll();
            VGames_Reload();
            s_needsScan = true;
        }
        snprintf(s_statusMsg, sizeof(s_statusMsg),
                 fixed > 0 ? "Sanitized %d title%s" : "All titles already clean",
                 fixed, fixed == 1 ? "" : "s");
        s_statusTime = 4.0f;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Re-run name sanitization across every games.ini entry.\n"
                          "Strips smart quotes, em dashes, ™/®/©, emoji, CJK\n"
                          "down to what the dashboard's text atlas can render.");

    // File browser callbacks
    s_iconBrowser.Display();
    if (s_iconBrowser.HasSelected() && s_selectedIdx >= 0 && s_selectedIdx < s_entryCount) {
        std::string iconSrc = s_iconBrowser.GetSelected().string();
        s_iconBrowser.ClearSelected();
        TmEntry& sel = s_entries[s_selectedIdx];

        TM_EnsureDir(VGAMES_ICONS);

        char dstPath[512];
        snprintf(dstPath, sizeof(dstPath), "%s/%s.jpg", VGAMES_ICONS, sel.titleID);
        TM_CopyFile(iconSrc.c_str(), dstPath);

        s_iconTexIdx = -1; // force icon reload
        snprintf(s_statusMsg, sizeof(s_statusMsg), "Icon saved: %s", dstPath);
        s_statusTime = 3.0f;
    }

    s_appBrowser.Display();
    if (s_appBrowser.HasSelected()) {
        std::string appPath = s_appBrowser.GetSelected().string();
        s_appBrowser.ClearSelected();
        strncpy(s_editLaunch, appPath.c_str(), sizeof(s_editLaunch) - 1);
    }

    ImGui::EndTabItem();
    } // Main tab

    if (g_showSteamTab && ImGui::BeginTabItem("Steam")) {
        // Header: tinted logo on left, project info on right.
        {
            static GuiTexture* s_stLogoTex = NULL;
            static int s_stLogoW = 0, s_stLogoH = 0;
            static bool s_stLogoTried = false;
            if (!s_stLogoTried) {
                s_stLogoTried = true;
                int w = 0, h = 0, ch = 0;
                unsigned char* pixels = stbi_load(AppPath("Configs/steamlogo.png"), &w, &h, &ch, 4);
                if (pixels) {
                    s_stLogoTex = GuiTextureCreate(w, h, pixels);
                    stbi_image_free(pixels);
                    s_stLogoW = w;
                    s_stLogoH = h;
                }
            }
            float headerH = 64.0f;
            if (s_stLogoTex && s_stLogoH > 0) {
                float scale = headerH / (float)s_stLogoH;
                float logoDisplayW = (float)s_stLogoW * scale;
                ImGui::Image((ImTextureID)(intptr_t)GuiTextureImId(s_stLogoTex),
                             ImVec2(logoDisplayW, headerH),
                             ImVec2(0, 0), ImVec2(1, 1),
                             ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
                ImGui::SameLine();
            }
            ImGui::BeginGroup();
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Steam");
            ImGui::Text("Your installed games, imported and launched via steam:// URLs.");
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "https://store.steampowered.com");
            if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (ImGui::IsItemClicked()) TM_OpenUrl("https://store.steampowered.com");
            ImGui::EndGroup();
        }
        ImGui::Separator();

        // Install path
        {
            ImGui::Text("Install:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 220);
            ImGui::InputText("##stpath", s_steamPath, sizeof(s_steamPath));
            ImGui::SameLine();
            if (ImGui::SmallButton("Save##st")) {
                SaveDesktopSettings();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Find##st")) {
                const char* user = NULL;
#ifdef _WIN32
                user = getenv("USERNAME");
#else
                user = getenv("USER");
#endif
                if (!user) user = "user";
                char buf[512];
                const char* tryRoots[] = {
#ifdef __APPLE__
                    "/Users/%s/Library/Application Support/Steam",
#elif defined(_WIN32)
                    "C:\\Program Files (x86)\\Steam",
                    "C:\\Program Files\\Steam",
                    "D:\\Steam",
                    "D:\\Program Files (x86)\\Steam",
                    "E:\\Steam",
                    "E:\\SteamLibrary",
#else
                    "/home/%s/.steam/steam",
                    "/home/%s/.local/share/Steam",
                    "/home/%s/.var/app/com.valvesoftware.Steam/data/Steam",
#endif
                    NULL
                };
                bool found = false;
                for (int i = 0; tryRoots[i] && !found; i++) {
                    snprintf(buf, sizeof(buf), tryRoots[i], user);
                    struct stat st;
                    if (stat(buf, &st) == 0) {
                        strncpy(s_steamPath, buf, sizeof(s_steamPath) - 1);
                        s_steamPath[sizeof(s_steamPath) - 1] = 0;
                        found = true;
                    }
                }
                if (!found) {
                    snprintf(s_statusMsg, sizeof(s_statusMsg),
                             "Steam not found in common locations; use Browse");
                    s_statusTime = 3.0f;
                }
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Browse##st")) {
                if (s_steamPath[0]) s_steamBrowser.SetPwd(s_steamPath);
                s_steamBrowser.Open();
            }
            s_steamBrowser.Display();
            if (s_steamBrowser.HasSelected()) {
                std::string sel = s_steamBrowser.GetSelected().string();
                strncpy(s_steamPath, sel.c_str(), sizeof(s_steamPath) - 1);
                s_steamPath[sizeof(s_steamPath) - 1] = 0;
                s_steamBrowser.ClearSelected();
            }
            if (s_steamPath[0] == 0)
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                    "Optional; leave blank to auto-detect Steam at import time");
        }
        ImGui::Separator();

        // State for the Add Game by App ID form + details panel
        static char s_stTitle[128] = "";
        static char s_stAppID[16]  = "";
        static int  s_stSelectedVi = -1;
        static GuiTexture* s_stDetailsTex = NULL;
        static char s_stDetailsTexPath[600] = "";
        static int  s_stDetailsTexW = 0, s_stDetailsTexH = 0;
        static ImGui::FileBrowser s_stIconBrowser(ImGuiFileBrowserFlags_CloseOnEsc);
        static bool s_stIconBrowserInit = false;
        if (!s_stIconBrowserInit) {
            s_stIconBrowser.SetTitle("Select icon image");
            s_stIconBrowser.SetTypeFilters({ ".jpg", ".jpeg", ".png", ".bmp", ".JPG", ".PNG" });
            s_stIconBrowserInit = true;
        }

        if (s_stSelectedVi >= 0 &&
            (s_stSelectedVi >= g_vgames.count || !g_vgames.games[s_stSelectedVi].valid))
            s_stSelectedVi = -1;

        const float kSFormPanelH = 200.0f;
        ImGui::BeginChild("##stform", ImVec2(380, kSFormPanelH), false);

        ImGui::Text("Add Game by App ID");

        ImGui::Text("Title:");
        ImGui::SameLine(80);
        ImGui::SetNextItemWidth(300);
        ImGui::InputText("##sttitle", s_stTitle, sizeof(s_stTitle));

        ImGui::Text("AppID:");
        ImGui::SameLine(80);
        ImGui::SetNextItemWidth(160);
        ImGui::InputText("##stappid", s_stAppID, sizeof(s_stAppID),
                         ImGuiInputTextFlags_CharsDecimal);

        ImGui::Spacing();
        int appidNum = atoi(s_stAppID);
        bool canAdd = s_stTitle[0] && appidNum > 0;
        if (!canAdd) ImGui::BeginDisabled();
        if (ImGui::Button("Add to Library")) {
            char launch[256];
            snprintf(launch, sizeof(launch), "steam://rungameid/%d", appidNum);
            char genID[16];
            snprintf(genID, sizeof(genID), "%08x", (unsigned)appidNum);
            extern int Title_SanitizeName(const char*, char*, size_t);
            char cleanName[128];
            Title_SanitizeName(s_stTitle, cleanName, sizeof(cleanName));
            if (!cleanName[0]) {
                snprintf(s_statusMsg, sizeof(s_statusMsg),
                         "Title sanitizes to empty, pick another");
                s_statusTime = 4.0f;
            } else {
                VGames_Add(cleanName, genID, launch, "E", "Games");
                TM_RegisterIcon(cleanName, genID);
                bool gotIcon = TM_DownloadSteamIcon(appidNum, genID);
                VGames_Save();
                UDataSynth_RebuildAll();
                snprintf(s_statusMsg, sizeof(s_statusMsg),
                         "Added: %s (%s)", cleanName,
                         gotIcon ? "with icon" : "no icon found");
                s_statusTime = 3.0f;
                s_stTitle[0] = 0;
                s_stAppID[0] = 0;
                s_needsScan = true;
            }
        }
        if (!canAdd) ImGui::EndDisabled();

        ImGui::SameLine();
        bool canImport = s_steamPath[0] != 0;
        if (!canImport) ImGui::BeginDisabled();
        if (ImGui::Button("Import Steam Library")) {
            int created = 0, skipped = 0, iconsDl = 0;
            TM_ImportSteamLibrary(s_steamPath, &created, &skipped, &iconsDl);
            s_needsScan = true;
            snprintf(s_statusMsg, sizeof(s_statusMsg),
                     "Imported %d new, %d existing, %d icons",
                     created, skipped, iconsDl);
            s_statusTime = 5.0f;
        }
        if (!canImport) ImGui::EndDisabled();

        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##stdetails", ImVec2(0, kSFormPanelH), true);

        if (s_stSelectedVi < 0) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                "Select an entry below to view details.");
        } else {
            VirtualGame& g = g_vgames.games[s_stSelectedVi];
            char iconPng[600], iconJpg[600];
            snprintf(iconPng, sizeof(iconPng), "%s", AppPathf("Configs/icons/%s.png", g.titleID));
            snprintf(iconJpg, sizeof(iconJpg), "%s", AppPathf("Configs/icons/%s.jpg", g.titleID));
            struct stat st;
            const char* iconPath = (stat(iconJpg, &st) == 0) ? iconJpg
                                 : (stat(iconPng, &st) == 0) ? iconPng : 0;

            if (iconPath && strcmp(iconPath, s_stDetailsTexPath) != 0) {
                GuiTextureDestroy(&s_stDetailsTex);
                int w = 0, h = 0, ch = 0;
                unsigned char* pixels = stbi_load(iconPath, &w, &h, &ch, 4);
                if (pixels) {
                    s_stDetailsTex = GuiTextureCreate(w, h, pixels);
                    stbi_image_free(pixels);
                    s_stDetailsTexW = w;
                    s_stDetailsTexH = h;
                }
                strncpy(s_stDetailsTexPath, iconPath, sizeof(s_stDetailsTexPath) - 1);
                s_stDetailsTexPath[sizeof(s_stDetailsTexPath) - 1] = 0;
            } else if (!iconPath) {
                GuiTextureDestroy(&s_stDetailsTex);
                s_stDetailsTexPath[0] = 0;
            }

            if (s_stDetailsTex) {
                float maxSide = 128.0f;
                float aspect = s_stDetailsTexH > 0
                    ? (float)s_stDetailsTexW / (float)s_stDetailsTexH : 1.0f;
                float dispW = (aspect >= 1.0f) ? maxSide : maxSide * aspect;
                float dispH = (aspect >= 1.0f) ? maxSide / aspect : maxSide;
                ImGui::Image((ImTextureID)(intptr_t)GuiTextureImId(s_stDetailsTex), ImVec2(dispW, dispH));
            } else {
                ImGui::Dummy(ImVec2(128, 128));
            }
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", g.name);

            int appid = 0;
            const char* rg = strstr(g.launch, "rungameid/");
            if (rg) appid = atoi(rg + 10);
            ImGui::Text("App ID: %d", appid);
            ImGui::Spacing();

            if (ImGui::Button("Open Store Page") && appid > 0) {
                char url[64];
                snprintf(url, sizeof(url), "steam://store/%d", appid);
                TM_OpenUrl(url);
            }
            ImGui::SameLine();
            if (ImGui::Button("Change Icon...")) {
                s_stIconBrowser.Open();
            }
            ImGui::EndGroup();
        }
        s_stIconBrowser.Display();
        if (s_stIconBrowser.HasSelected() && s_stSelectedVi >= 0) {
            std::string src = s_stIconBrowser.GetSelected().string();
            s_stIconBrowser.ClearSelected();
            const char* titleID = g_vgames.games[s_stSelectedVi].titleID;
            const char* ext = strrchr(src.c_str(), '.');
            bool isPng = ext && (strcasecmp(ext, ".png") == 0);
            TM_EnsureDir(VGAMES_ICONS);
            char dst[600];
            snprintf(dst, sizeof(dst), "%s/%s.%s",
                     VGAMES_ICONS, titleID, isPng ? "png" : "jpg");
            char other[600];
            snprintf(other, sizeof(other), "%s/%s.%s",
                     VGAMES_ICONS, titleID, isPng ? "jpg" : "png");
            remove(other);
            if (TM_CopyFile(src.c_str(), dst)) {
                s_stDetailsTexPath[0] = 0;
                snprintf(s_statusMsg, sizeof(s_statusMsg), "Icon updated: %s", dst);
                s_statusTime = 3.0f;
            }
        }

        ImGui::EndChild();
        ImGui::Separator();

        // Library list: every VGames entry whose launch URL is a
        // steam:// spec.
        {
            int matchIdx[TM_MAX_ENTRIES];
            int matchCount = 0;
            for (int i = 0; i < g_vgames.count && matchCount < TM_MAX_ENTRIES; i++) {
                if (!g_vgames.games[i].valid) continue;
                if (strncmp(g_vgames.games[i].launch, "steam://", 8) != 0) continue;
                matchIdx[matchCount++] = i;
            }

            ImGui::Text("Steam Library (%d)", matchCount);

            ImGuiTableFlags tflags =
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
            if (ImGui::BeginTable("##stlib", 3, tflags, ImVec2(0, 0))) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Title",  ImGuiTableColumnFlags_WidthStretch, 0.70f);
                ImGui::TableSetupColumn("App ID", ImGuiTableColumnFlags_WidthFixed,   80.0f);
                ImGui::TableSetupColumn("",       ImGuiTableColumnFlags_WidthFixed,   28.0f);
                ImGui::TableHeadersRow();

                int pendingDelete = -1;
                for (int r = 0; r < matchCount; r++) {
                    int vi = matchIdx[r];
                    VirtualGame& g = g_vgames.games[vi];
                    int appid = 0;
                    const char* rg = strstr(g.launch, "rungameid/");
                    if (rg) appid = atoi(rg + 10);

                    ImGui::TableNextRow(0, ImGui::GetFrameHeight());
                    ImGui::TableSetColumnIndex(0);
                    ImGui::AlignTextToFramePadding();
                    ImGui::PushID(r + 30000);
                    bool selected = (s_stSelectedVi == vi);
                    if (ImGui::Selectable(g.name, selected,
                            ImGuiSelectableFlags_SpanAllColumns |
                            ImGuiSelectableFlags_AllowItemOverlap)) {
                        s_stSelectedVi = selected ? -1 : vi;
                        s_stDetailsTexPath[0] = 0;
                    }
                    ImGui::PopID();

                    ImGui::TableSetColumnIndex(1);
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("%d", appid);

                    ImGui::TableSetColumnIndex(2);
                    ImGui::PushID(r + 40000);
                    if (ImGui::SmallButton("X")) pendingDelete = vi;
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Remove %s", g.name);
                    ImGui::PopID();
                }
                ImGui::EndTable();

                if (pendingDelete >= 0) {
                    char name[128];
                    strncpy(name, g_vgames.games[pendingDelete].name, sizeof(name) - 1);
                    name[sizeof(name) - 1] = 0;
                    VGames_DeleteByName(name);
                    VGames_Save();
                    UDataSynth_RebuildAll();
                    s_needsScan = true;
                    snprintf(s_statusMsg, sizeof(s_statusMsg), "Removed: %s", name);
                    s_statusTime = 3.0f;
                }
            }
        }

        ImGui::EndTabItem();
    } // Steam tab

    if (g_showRetroArchTab && ImGui::BeginTabItem("RetroArch")) {
        // Header: tinted logo on left, project info on right.
        {
            int logoW = 0, logoH = 0;
            unsigned long long logo = TM_LoadRetroArchLogo(s_retroarchPath, &logoW, &logoH);
            float headerH = 64.0f;
            float logoDisplayW = 0;
            if (logo && logoH > 0) {
                float scale = headerH / (float)logoH;
                logoDisplayW = (float)logoW * scale;
                ImGui::Image((ImTextureID)(intptr_t)logo,
                             ImVec2(logoDisplayW, headerH),
                             ImVec2(0, 0), ImVec2(1, 1),
                             ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
                ImGui::SameLine();
            }
            ImGui::BeginGroup();
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f),
                "RetroArch");
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                "Add ROMs and let RetroArch's libretro cores run them.");
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "https://www.retroarch.com");
            if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (ImGui::IsItemClicked()) TM_OpenUrl("https://www.retroarch.com");
            ImGui::EndGroup();
        }
        ImGui::Separator();

        // Install path setting
        {
            ImGui::Text("Install:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 220);
            ImGui::InputText("##rapath", s_retroarchPath, sizeof(s_retroarchPath));
            ImGui::SameLine();
            if (ImGui::SmallButton("Save##ra")) {
                SaveDesktopSettings();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Find##ra")) {
                char roots[8][512];
                int n = RetroArch_DiscoverInstall(s_retroarchPath, roots, 8);
                if (n > 0) {
                    strncpy(s_retroarchPath, roots[0], sizeof(s_retroarchPath) - 1);
                    s_retroarchPath[sizeof(s_retroarchPath) - 1] = 0;
                } else {
                    snprintf(s_statusMsg, sizeof(s_statusMsg),
                             "RetroArch not found in common locations; use Browse");
                    s_statusTime = 3.0f;
                }
            }
            ImGui::SameLine();
            static ImGui::FileBrowser s_raPathBrowser(
                ImGuiFileBrowserFlags_CloseOnEsc | ImGuiFileBrowserFlags_SelectDirectory);
            static bool s_raPathBrowserInit = false;
            if (!s_raPathBrowserInit) {
                s_raPathBrowser.SetTitle("Select RetroArch install folder");
                s_raPathBrowserInit = true;
            }
            if (ImGui::SmallButton("Browse##ra")) {
                if (s_retroarchPath[0]) s_raPathBrowser.SetPwd(s_retroarchPath);
                s_raPathBrowser.Open();
            }
            s_raPathBrowser.Display();
            if (s_raPathBrowser.HasSelected()) {
                std::string sel = s_raPathBrowser.GetSelected().string();
                strncpy(s_retroarchPath, sel.c_str(), sizeof(s_retroarchPath) - 1);
                s_retroarchPath[sizeof(s_retroarchPath) - 1] = 0;
                s_raPathBrowser.ClearSelected();
            }
            if (s_retroarchPath[0] == 0)
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f),
                    "Set RetroArch install path to enumerate cores");
        }
        ImGui::Separator();

        // Add-game form state
        static char s_raTitle[128]      = "";
        static char s_raContent[512]    = "";
        static char s_raCores[64][256]  = {};
        static int  s_raCoreCount       = 0;
        static int  s_raCoreIdx         = -1;
        static char s_raLastScannedPath[512] = "";
        static int  s_raSelectedVi      = -1;
        static GuiTexture* s_raDetailsTex = NULL;
        static char s_raDetailsTexPath[600] = "";
        static int  s_raDetailsTexW = 0;
        static int  s_raDetailsTexH = 0;
        static ImGui::FileBrowser s_raContentBrowser(ImGuiFileBrowserFlags_CloseOnEsc);
        static ImGui::FileBrowser s_raIconBrowser(ImGuiFileBrowserFlags_CloseOnEsc);
        static bool s_raContentBrowserInit = false;
        static bool s_raIconBrowserInit = false;
        if (!s_raContentBrowserInit) {
            s_raContentBrowser.SetTitle("Select ROM / content file");
            s_raContentBrowserInit = true;
        }
        if (!s_raIconBrowserInit) {
            s_raIconBrowser.SetTitle("Select icon image");
            s_raIconBrowser.SetTypeFilters({ ".jpg", ".jpeg", ".png", ".bmp", ".JPG", ".PNG" });
            s_raIconBrowserInit = true;
        }

        if (strcmp(s_raLastScannedPath, s_retroarchPath) != 0) {
            strncpy(s_raLastScannedPath, s_retroarchPath, sizeof(s_raLastScannedPath) - 1);
            s_raLastScannedPath[sizeof(s_raLastScannedPath) - 1] = 0;
            s_raCoreCount = s_retroarchPath[0]
                ? RetroArch_EnumerateCores(s_retroarchPath, s_raCores, 64)
                : 0;
            s_raCoreIdx = -1;
        }

        // Selection may have been removed; clamp.
        if (s_raSelectedVi >= 0 &&
            (s_raSelectedVi >= g_vgames.count || !g_vgames.games[s_raSelectedVi].valid))
            s_raSelectedVi = -1;

        const float kFormPanelH = 200.0f;
        ImGui::BeginChild("##raform", ImVec2(380, kFormPanelH), false);

        ImGui::Text("Add Game");

        ImGui::Text("Title:");
        ImGui::SameLine(80);
        ImGui::SetNextItemWidth(300);
        ImGui::InputText("##ratitle", s_raTitle, sizeof(s_raTitle));

        ImGui::Text("Core:");
        ImGui::SameLine(80);
        ImGui::SetNextItemWidth(300);
        const char* coreLabel =
            (s_raCoreIdx >= 0 && s_raCoreIdx < s_raCoreCount)
            ? s_raCores[s_raCoreIdx] : "(select core)";
        if (ImGui::BeginCombo("##racore", coreLabel)) {
            for (int i = 0; i < s_raCoreCount; i++) {
                if (ImGui::Selectable(s_raCores[i], s_raCoreIdx == i))
                    s_raCoreIdx = i;
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Rescan##cores")) {
            s_raLastScannedPath[0] = 0;
        }
        if (s_retroarchPath[0] && s_raCoreCount == 0)
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f),
                "No cores under %s/cores/. Install one via RetroArch's Online Updater.",
                s_retroarchPath);

        ImGui::Text("Content:");
        ImGui::SameLine(80);
        ImGui::SetNextItemWidth(300);
        ImGui::InputText("##racontent", s_raContent, sizeof(s_raContent));
        ImGui::SameLine();
        if (ImGui::SmallButton("Browse##racontent")) {
            s_raContentBrowser.Open();
        }
        s_raContentBrowser.Display();
        if (s_raContentBrowser.HasSelected()) {
            std::string sel = s_raContentBrowser.GetSelected().string();
            strncpy(s_raContent, sel.c_str(), sizeof(s_raContent) - 1);
            s_raContent[sizeof(s_raContent) - 1] = 0;
            if (!s_raTitle[0])
                TM_DeriveRomTitle(s_raContent, s_raTitle, sizeof(s_raTitle));
            s_raContentBrowser.ClearSelected();
        }

        ImGui::Spacing();
        bool canAdd = s_raTitle[0] && s_raCoreIdx >= 0 && s_raContent[0];
        if (!canAdd) ImGui::BeginDisabled();
        if (ImGui::Button("Add to Library")) {
            char encCore[768], encContent[768];
            TM_UrlEncode(s_raCores[s_raCoreIdx], encCore,    sizeof(encCore));
            TM_UrlEncode(s_raContent,            encContent, sizeof(encContent));

            char launch[2048];
            snprintf(launch, sizeof(launch),
                     "retroarch://run?core=%s&content=%s", encCore, encContent);

            char genID[16];
            snprintf(genID, sizeof(genID), "%08x", (unsigned)time(NULL));

            extern int Title_SanitizeName(const char*, char*, size_t);
            char cleanName[sizeof(s_raTitle)];
            Title_SanitizeName(s_raTitle, cleanName, sizeof(cleanName));
            if (!cleanName[0]) {
                snprintf(s_statusMsg, sizeof(s_statusMsg),
                         "Title sanitizes to empty, pick another");
                s_statusTime = 4.0f;
            } else {
                VGames_Add(cleanName, genID, launch, "E", "Emulators");
                TM_RegisterIcon(cleanName, genID);

                char system[256];
                if (RetroArch_GetSystemForCore(s_retroarchPath,
                                                s_raCores[s_raCoreIdx],
                                                system, sizeof(system))) {
                    char dbNameFake[300];
                    snprintf(dbNameFake, sizeof(dbNameFake), "%s.lpl", system);
                    char boxart[1024];
                    if (RetroArch_FindBoxart(s_retroarchPath, dbNameFake,
                                              cleanName, s_raContent,
                                              boxart, sizeof(boxart))) {
                        TM_EnsureDir(VGAMES_ICONS);
                        char dst[600];
                        snprintf(dst, sizeof(dst), "%s/%s.png", VGAMES_ICONS, genID);
                        TM_CopyFile(boxart, dst);
                    }
                }

                VGames_Save();
                UDataSynth_RebuildAll();
                snprintf(s_statusMsg, sizeof(s_statusMsg),
                         "Added: %s [%s]", cleanName, s_raCores[s_raCoreIdx]);
                s_statusTime = 3.0f;
                s_raTitle[0]   = 0;
                s_raContent[0] = 0;
                s_needsScan = true;
            }
        }
        if (!canAdd) ImGui::EndDisabled();

        ImGui::SameLine();
        bool canImport = s_retroarchPath[0] != 0;
        if (!canImport) ImGui::BeginDisabled();
        if (ImGui::Button("Import Recent Titles")) {
            char playlistsDir[600] = "";
            if (!RetroArch_DiscoverPlaylistsDir(s_retroarchPath,
                                                 playlistsDir, sizeof(playlistsDir))) {
                snprintf(s_statusMsg, sizeof(s_statusMsg),
                         "No RetroArch playlists directory found");
                s_statusTime = 4.0f;
            } else {
                TM_RAImportCtx ctx = {};
                strncpy(ctx.installRoot, s_retroarchPath, sizeof(ctx.installRoot) - 1);
                int total = RetroArch_WalkPlaylists(playlistsDir, TM_RAImportItem, &ctx);
                if (ctx.added > 0) {
                    VGames_Save();
                    UDataSynth_RebuildAll();
                    s_needsScan = true;
                }
                snprintf(s_statusMsg, sizeof(s_statusMsg),
                         "Imported %d (%d skipped, %d total in playlists)",
                         ctx.added, ctx.skipped, total);
                s_statusTime = 5.0f;
            }
        }
        if (!canImport) ImGui::EndDisabled();

        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##radetails", ImVec2(0, kFormPanelH), true);

        if (s_raSelectedVi < 0) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                "Select an entry below to view details.");
        } else {
            VirtualGame& g = g_vgames.games[s_raSelectedVi];

            char iconPng[600], iconJpg[600];
            snprintf(iconPng, sizeof(iconPng), "%s", AppPathf("Configs/icons/%s.png", g.titleID));
            snprintf(iconJpg, sizeof(iconJpg), "%s", AppPathf("Configs/icons/%s.jpg", g.titleID));
            struct stat st;
            const char* iconPath = (stat(iconPng, &st) == 0) ? iconPng
                                 : (stat(iconJpg, &st) == 0) ? iconJpg : 0;

            if (iconPath && strcmp(iconPath, s_raDetailsTexPath) != 0) {
                GuiTextureDestroy(&s_raDetailsTex);
                int w = 0, h = 0, ch = 0;
                unsigned char* pixels = stbi_load(iconPath, &w, &h, &ch, 4);
                if (pixels) {
                    s_raDetailsTex = GuiTextureCreate(w, h, pixels);
                    stbi_image_free(pixels);
                    s_raDetailsTexW = w;
                    s_raDetailsTexH = h;
                }
                strncpy(s_raDetailsTexPath, iconPath, sizeof(s_raDetailsTexPath) - 1);
                s_raDetailsTexPath[sizeof(s_raDetailsTexPath) - 1] = 0;
            } else if (!iconPath) {
                GuiTextureDestroy(&s_raDetailsTex);
                s_raDetailsTexPath[0] = 0;
            }

            if (s_raDetailsTex) {
                float maxSide = 128.0f;
                float aspect = s_raDetailsTexH > 0
                    ? (float)s_raDetailsTexW / (float)s_raDetailsTexH : 1.0f;
                float dispW = (aspect >= 1.0f) ? maxSide : maxSide * aspect;
                float dispH = (aspect >= 1.0f) ? maxSide / aspect : maxSide;
                ImGui::Image((ImTextureID)(intptr_t)GuiTextureImId(s_raDetailsTex), ImVec2(dispW, dispH));
            } else {
                ImGui::Dummy(ImVec2(128, 128));
            }
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", g.name);
            char region[64];
            TM_DeriveRegion(g.name, region, sizeof(region));
            ImGui::Text("Region: %s", region[0] ? region : "(unknown)");
            char coreNow[256];
            TM_ParseRetroArchField(g.launch, "core", coreNow, sizeof(coreNow));
            ImGui::Text("Core:   %s", coreNow);
            ImGui::Spacing();
            if (ImGui::Button("Change Icon...")) {
                s_raIconBrowser.Open();
            }
            ImGui::EndGroup();
        }
        s_raIconBrowser.Display();
        if (s_raIconBrowser.HasSelected() && s_raSelectedVi >= 0) {
            std::string src = s_raIconBrowser.GetSelected().string();
            s_raIconBrowser.ClearSelected();
            const char* titleID = g_vgames.games[s_raSelectedVi].titleID;
            const char* ext = strrchr(src.c_str(), '.');
            bool isPng = ext && (strcasecmp(ext, ".png") == 0);
            TM_EnsureDir(VGAMES_ICONS);
            char dst[600];
            snprintf(dst, sizeof(dst), "%s/%s.%s",
                     VGAMES_ICONS, titleID, isPng ? "png" : "jpg");
            // Clear any sibling extension so the picker is unambiguous.
            char other[600];
            snprintf(other, sizeof(other), "%s/%s.%s",
                     VGAMES_ICONS, titleID, isPng ? "jpg" : "png");
            remove(other);
            if (TM_CopyFile(src.c_str(), dst)) {
                s_raDetailsTexPath[0] = 0; // force reload
                snprintf(s_statusMsg, sizeof(s_statusMsg), "Icon updated: %s", dst);
                s_statusTime = 3.0f;
            }
        }

        ImGui::EndChild();

        ImGui::Separator();

        // Library list: every VGames entry whose launch URL is a
        // retroarch:// spec. Per-row delete; columns show the core
        // and content path parsed out of the launch URL.
        {
            int matchIdx[TM_MAX_ENTRIES];
            int matchCount = 0;
            for (int i = 0; i < g_vgames.count && matchCount < TM_MAX_ENTRIES; i++) {
                if (!g_vgames.games[i].valid) continue;
                if (strncmp(g_vgames.games[i].launch, "retroarch://", 12) != 0) continue;
                matchIdx[matchCount++] = i;
            }

            ImGui::Text("RetroArch Library (%d)", matchCount);

            ImGuiTableFlags tflags =
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
            if (ImGui::BeginTable("##ralib", 4, tflags, ImVec2(0, 0))) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Title",   ImGuiTableColumnFlags_WidthStretch, 0.32f);
                ImGui::TableSetupColumn("Core",    ImGuiTableColumnFlags_WidthStretch, 0.22f);
                ImGui::TableSetupColumn("Content", ImGuiTableColumnFlags_WidthStretch, 0.40f);
                ImGui::TableSetupColumn("",        ImGuiTableColumnFlags_WidthFixed,   28.0f);
                ImGui::TableHeadersRow();

                int pendingDelete = -1;
                int  changedRowVi  = -1;
                char changedCore[256] = "";
                for (int r = 0; r < matchCount; r++) {
                    int vi = matchIdx[r];
                    VirtualGame& g = g_vgames.games[vi];
                    char core[256] = "", content[512] = "";
                    TM_ParseRetroArchField(g.launch, "core",    core,    sizeof(core));
                    TM_ParseRetroArchField(g.launch, "content", content, sizeof(content));

                    ImGui::TableNextRow(0, ImGui::GetFrameHeight());
                    ImGui::TableSetColumnIndex(0);
                    ImGui::AlignTextToFramePadding();
                    ImGui::PushID(r + 20000);
                    bool selected = (s_raSelectedVi == vi);
                    if (ImGui::Selectable(g.name, selected,
                            ImGuiSelectableFlags_SpanAllColumns |
                            ImGuiSelectableFlags_AllowItemOverlap)) {
                        s_raSelectedVi = selected ? -1 : vi;
                        s_raDetailsTexPath[0] = 0;
                    }
                    ImGui::PopID();

                    ImGui::TableSetColumnIndex(1);
                    ImGui::PushID(r);
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (s_raCoreCount > 0) {
                        if (ImGui::BeginCombo("##rowcore", core)) {
                            for (int i = 0; i < s_raCoreCount; i++) {
                                bool sel = strcmp(s_raCores[i], core) == 0;
                                if (ImGui::Selectable(s_raCores[i], sel) &&
                                    strcmp(s_raCores[i], core) != 0) {
                                    changedRowVi = vi;
                                    strncpy(changedCore, s_raCores[i], sizeof(changedCore) - 1);
                                    changedCore[sizeof(changedCore) - 1] = 0;
                                }
                            }
                            ImGui::EndCombo();
                        }
                    } else {
                        ImGui::TextUnformatted(core);
                    }
                    ImGui::PopID();

                    ImGui::TableSetColumnIndex(2);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(content);
                    ImGui::TableSetColumnIndex(3);
                    ImGui::PushID(r + 10000);
                    if (ImGui::SmallButton("X")) pendingDelete = vi;
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Remove %s", g.name);
                    ImGui::PopID();
                }
                ImGui::EndTable();

                if (changedRowVi >= 0) {
                    VirtualGame& g = g_vgames.games[changedRowVi];
                    char content[512] = "";
                    TM_ParseRetroArchField(g.launch, "content", content, sizeof(content));
                    char encCore[768], encContent[768];
                    TM_UrlEncode(changedCore, encCore,    sizeof(encCore));
                    TM_UrlEncode(content,     encContent, sizeof(encContent));
                    snprintf(g.launch, sizeof(g.launch),
                             "retroarch://run?core=%s&content=%s", encCore, encContent);
                    VGames_Save();
                    UDataSynth_RebuildAll();
                    s_needsScan = true;
                    snprintf(s_statusMsg, sizeof(s_statusMsg),
                             "Core changed: %s -> %s", g.name, changedCore);
                    s_statusTime = 3.0f;
                }

                if (pendingDelete >= 0) {
                    char name[128];
                    strncpy(name, g_vgames.games[pendingDelete].name, sizeof(name) - 1);
                    name[sizeof(name) - 1] = 0;
                    VGames_DeleteByName(name);
                    VGames_Save();
                    UDataSynth_RebuildAll();
                    s_needsScan = true;
                    snprintf(s_statusMsg, sizeof(s_statusMsg), "Removed: %s", name);
                    s_statusTime = 3.0f;
                }
            }
        }

        ImGui::EndTabItem();
    } // RetroArch tab

    if (s_showEmulatorsTab && ImGui::BeginTabItem("Emulators")) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
            "Per-emulator binary path, launch options, and game import.");
        ImGui::Separator();

        if (ImGui::BeginTabBar("EmuSubTabs")) {
            for (int e = 0; e < kEmulatorConfigCount; e++) {
                const EmulatorConfig& conf = kEmulatorConfigs[e];
                if (!ImGui::BeginTabItem(conf.displayName)) continue;

                ImGui::PushID(e);

                // Binary path
                ImGui::Text("Binary:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 150);
                if (ImGui::InputText("##emubin", s_emuBinaryPaths[e], sizeof(s_emuBinaryPaths[e])))
                    Emulator_SavePaths(s_emuBinaryPaths, kEmulatorConfigCount);
                ImGui::SameLine();
                if (ImGui::SmallButton("Find")) {
                    char found[512];
                    if (Emulator_FindBinary(&conf, found, sizeof(found))) {
                        strncpy(s_emuBinaryPaths[e], found, sizeof(s_emuBinaryPaths[e]) - 1);
                        s_emuBinaryPaths[e][sizeof(s_emuBinaryPaths[e]) - 1] = 0;
                        Emulator_SavePaths(s_emuBinaryPaths, kEmulatorConfigCount);
                    } else {
                        snprintf(s_statusMsg, sizeof(s_statusMsg),
                                 "%s not found in common locations", conf.displayName);
                        s_statusTime = 3.0f;
                    }
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Browse")) {
                    s_emuBrowserTarget = e;
                    s_emuBinBrowser.Open();
                }
                if (!s_emuBinaryPaths[e][0])
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f),
                        "Set the %s binary to enable adding games", conf.displayName);

                // Launch options
                if (conf.optionCount > 0) {
                    ImGui::Spacing();
                    ImGui::Text("Launch options:");
                    for (int o = 0; o < conf.optionCount; o++)
                        ImGui::Checkbox(conf.options[o].label, &s_emuOptionOn[e][o]);
                }

                ImGui::Spacing();
                ImGui::Separator();

                // Import
                bool haveBinary = s_emuBinaryPaths[e][0] != 0;
                if (!haveBinary) ImGui::BeginDisabled();

                if (ImGui::Button("Add Game File...")) {
                    s_emuBrowserTarget = e;
                    std::vector<std::string> filters;
                    for (int x = 0; conf.extensions[x]; x++)
                        filters.push_back(std::string(".") + conf.extensions[x]);
                    s_emuFileBrowser.SetTypeFilters(filters);
                    s_emuFileBrowser.Open();
                }
                ImGui::SameLine();
                if (ImGui::Button("Add Folder (scan)...")) {
                    s_emuBrowserTarget = e;
                    s_emuFolderBrowser.Open();
                }
                if (conf.detectInstalled) {
                    ImGui::SameLine();
                    if (ImGui::Button("Detect Installed Games...")) {
                        char configDir[600];
                        RPCS3_DefaultConfigDir(s_emuBinaryPaths[e], configDir, sizeof(configDir));
                        char gamesYml[700], hddDir[700];
                        snprintf(gamesYml, sizeof(gamesYml), "%s/games.yml", configDir);
                        snprintf(hddDir, sizeof(hddDir), "%s/dev_hdd0/game", configDir);

                        s_rpcs3DetectedCount = RPCS3_DetectInstalled(gamesYml, hddDir, s_rpcs3Detected, 256);
                        for (int i = 0; i < s_rpcs3DetectedCount; i++) {
                            RPCS3_FriendlyName(s_rpcs3Detected[i].gameId, s_rpcs3Detected[i].folderPath,
                                                s_rpcs3Detected[i].displayName,
                                                sizeof(s_rpcs3Detected[i].displayName));
                            s_rpcs3CheckedIdx[i] = true;
                        }
                        if (s_rpcs3DetectedCount == 0) {
                            snprintf(s_statusMsg, sizeof(s_statusMsg),
                                     "No RPCS3 games found under %s", configDir);
                            s_statusTime = 4.0f;
                        } else {
                            s_showRpcs3DetectDialog = true;
                        }
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Reads games.yml and scans dev_hdd0/game/\n"
                                          "under RPCS3's config directory.");
                }
                if (!haveBinary) ImGui::EndDisabled();

                ImGui::Spacing();

                // Per-emulator library list. Entries are matched by whether
                // their launch command starts with this emulator's quoted
                // binary path, the same "look at what's already in games.ini"
                // approach the Steam/RetroArch tabs use with their scheme
                // prefixes - just keyed on binary instead of a URL scheme,
                // since these entries are plain shell commands.
                {
                    int matchIdx[TM_MAX_ENTRIES];
                    int matchCount = 0;
                    if (s_emuBinaryPaths[e][0]) {
                        char quotedBin[560];
                        snprintf(quotedBin, sizeof(quotedBin), "\"%s\"", s_emuBinaryPaths[e]);
                        size_t qlen = strlen(quotedBin);
                        for (int i = 0; i < g_vgames.count && matchCount < TM_MAX_ENTRIES; i++) {
                            if (!g_vgames.games[i].valid) continue;
                            if (strncmp(g_vgames.games[i].launch, quotedBin, qlen) != 0) continue;
                            matchIdx[matchCount++] = i;
                        }
                    }

                    ImGui::Text("%s Library (%d)", conf.displayName, matchCount);
                    ImGuiTableFlags tflags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                             ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
                    if (ImGui::BeginTable("##emulib", 2, tflags, ImVec2(0, 160))) {
                        ImGui::TableSetupColumn("Title", ImGuiTableColumnFlags_WidthStretch, 0.88f);
                        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 28.0f);
                        ImGui::TableHeadersRow();

                        int pendingDelete = -1;
                        for (int r = 0; r < matchCount; r++) {
                            int vi = matchIdx[r];
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::AlignTextToFramePadding();
                            ImGui::TextUnformatted(g_vgames.games[vi].name);
                            ImGui::TableSetColumnIndex(1);
                            ImGui::PushID(vi);
                            if (ImGui::SmallButton("X")) pendingDelete = vi;
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("Remove %s", g_vgames.games[vi].name);
                            ImGui::PopID();
                        }
                        ImGui::EndTable();

                        if (pendingDelete >= 0) {
                            char name[128];
                            strncpy(name, g_vgames.games[pendingDelete].name, sizeof(name) - 1);
                            name[sizeof(name) - 1] = 0;
                            VGames_DeleteByName(name);
                            VGames_Save(); UDataSynth_RebuildAll();
                            s_needsScan = true;
                            snprintf(s_statusMsg, sizeof(s_statusMsg), "Removed: %s", name);
                            s_statusTime = 3.0f;
                        }
                    }
                }

                ImGui::PopID();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        // Browser callbacks, shared across sub-tabs via s_emuBrowserTarget
        s_emuBinBrowser.Display();
        if (s_emuBinBrowser.HasSelected() && s_emuBrowserTarget >= 0) {
            std::string sel = s_emuBinBrowser.GetSelected().string();
            s_emuBinBrowser.ClearSelected();
            strncpy(s_emuBinaryPaths[s_emuBrowserTarget], sel.c_str(),
                    sizeof(s_emuBinaryPaths[0]) - 1);
            s_emuBinaryPaths[s_emuBrowserTarget][sizeof(s_emuBinaryPaths[0]) - 1] = 0;
            Emulator_SavePaths(s_emuBinaryPaths, kEmulatorConfigCount);
        }

        s_emuFileBrowser.Display();
        if (s_emuFileBrowser.HasSelected() && s_emuBrowserTarget >= 0) {
            std::string sel = s_emuFileBrowser.GetSelected().string();
            s_emuFileBrowser.ClearSelected();
            int e = s_emuBrowserTarget;
            bool ok = TM_EmuAddGame(&kEmulatorConfigs[e], sel.c_str(),
                                    s_emuBinaryPaths[e], s_emuOptionOn[e]);
            if (ok) {
                VGames_Save(); UDataSynth_RebuildAll();
                s_needsScan = true;
                snprintf(s_statusMsg, sizeof(s_statusMsg), "Added: %s", sel.c_str());
            } else {
                snprintf(s_statusMsg, sizeof(s_statusMsg), "Skipped (already in library)");
            }
            s_statusTime = 3.0f;
        }

        s_emuFolderBrowser.Display();
        if (s_emuFolderBrowser.HasSelected() && s_emuBrowserTarget >= 0) {
            std::string sel = s_emuFolderBrowser.GetSelected().string();
            s_emuFolderBrowser.ClearSelected();
            int e = s_emuBrowserTarget;
            std::vector<std::string> found;
            TM_EmuScanFolder(sel.c_str(), &kEmulatorConfigs[e], found, 0);
            int added = 0, skipped = 0;
            for (size_t i = 0; i < found.size(); i++) {
                if (TM_EmuAddGame(&kEmulatorConfigs[e], found[i].c_str(),
                                  s_emuBinaryPaths[e], s_emuOptionOn[e]))
                    added++;
                else
                    skipped++;
            }
            if (added > 0) { VGames_Save(); UDataSynth_RebuildAll(); s_needsScan = true; }
            snprintf(s_statusMsg, sizeof(s_statusMsg),
                     "Folder scan: added %d, skipped %d", added, skipped);
            s_statusTime = 4.0f;
        }

        // RPCS3 detected-games confirmation dialog
        if (s_showRpcs3DetectDialog) {
            ImGui::SetNextWindowSize(ImVec2(520, 420), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("RPCS3: Detected Games", &s_showRpcs3DetectDialog)) {
                ImGui::Text("Found %d game(s). Uncheck any you don't want added.",
                            s_rpcs3DetectedCount);
                ImGui::Separator();
                ImGui::BeginChild("rpcs3list", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), true);
                for (int i = 0; i < s_rpcs3DetectedCount; i++) {
                    ImGui::PushID(i);
                    ImGui::Checkbox("##chk", &s_rpcs3CheckedIdx[i]);
                    ImGui::SameLine();
                    ImGui::Text("%s  [%s]", s_rpcs3Detected[i].displayName, s_rpcs3Detected[i].gameId);
                    ImGui::PopID();
                }
                ImGui::EndChild();

                int rpcs3Idx = 0;
                for (int i = 0; i < kEmulatorConfigCount; i++)
                    if (strcmp(kEmulatorConfigs[i].key, "rpcs3") == 0) { rpcs3Idx = i; break; }

                if (ImGui::Button("Add Checked", ImVec2(140, 0))) {
                    int added = 0, skipped = 0;
                    for (int i = 0; i < s_rpcs3DetectedCount; i++) {
                        if (!s_rpcs3CheckedIdx[i]) continue;
                        if (TM_EmuAddRpcs3(s_rpcs3Detected[i], s_emuBinaryPaths[rpcs3Idx],
                                           s_emuOptionOn[rpcs3Idx]))
                            added++;
                        else
                            skipped++;
                    }
                    if (added > 0) { VGames_Save(); UDataSynth_RebuildAll(); s_needsScan = true; }
                    snprintf(s_statusMsg, sizeof(s_statusMsg),
                             "RPCS3: added %d, skipped %d (no EBOOT.BIN or already present)",
                             added, skipped);
                    s_statusTime = 4.0f;
                    s_showRpcs3DetectDialog = false;
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(100, 0))) s_showRpcs3DetectDialog = false;
            }
            ImGui::End();
        }

        ImGui::EndTabItem();
    } // Emulators tab

    if (ImGui::BeginTabItem("Settings")) {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "SteamGridDB");
        ImGui::Separator();

        ImGui::Text("API Key:");
        ImGui::SameLine();
        ImGuiInputTextFlags keyFlags = s_sgdbShowKey ? 0 : ImGuiInputTextFlags_Password;
        ImGui::SetNextItemWidth(320);
        ImGui::InputText("##sgdbkey", s_sgdb.apiKey, sizeof(s_sgdb.apiKey), keyFlags);
        ImGui::SameLine();
        ImGui::Checkbox("Show", &s_sgdbShowKey);
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
            "Free key: steamgriddb.com -> Preferences -> API");

        ImGui::Spacing();
        ImGui::Text("Default Icon Source:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(180);
        static const char* s_sgdbSourceNames[] = { "Icons (square)", "Grids (boxart)" };
        if (ImGui::BeginCombo("##sgdbsrc", s_sgdbSourceNames[s_sgdb.assetType])) {
            for (int i = 0; i < 2; i++)
                if (ImGui::Selectable(s_sgdbSourceNames[i], s_sgdb.assetType == i))
                    s_sgdb.assetType = i;
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Icons are square and match the dashboard tile look.\n"
                              "Grids are taller boxart-style images.");

        ImGui::Spacing();
        ImGui::Text("Option Count:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(180);
        ImGui::SliderInt("##sgdbcount", &s_sgdb.optionCount, 0, 5,
                         s_sgdb.optionCount == 0 ? "Disabled" : "%d");
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
            "How many thumbnails the picker fetches per game. 0 hides the SteamGridDB\n"
            "buttons everywhere - Main and Emulators fall back to local icons only.");

        ImGui::Spacing();
        if (ImGui::Button("Save Settings")) {
            SGDB_SaveSettings(&s_sgdb);
            snprintf(s_statusMsg, sizeof(s_statusMsg), "SteamGridDB settings saved");
            s_statusTime = 3.0f;
        }

        ImGui::EndTabItem();
    } // Settings tab

    ImGui::EndTabBar();

    // SteamGridDB icon picker - a floating dialog so it stays open no
    // matter which tab is active (the bulk panel that launched it lives on
    // Main; Emulators library rows might want the same button later).
    if (s_sgdbDialogOpen) {
        ImGui::SetNextWindowSize(ImVec2(560, 420), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("SteamGridDB Icons", &s_sgdbDialogOpen)) {
            int vgIndex = (s_sgdbSessionPos < s_sgdbSessionCount) ? s_sgdbSessionVg[s_sgdbSessionPos] : -1;
            const char* curName = (vgIndex >= 0 && vgIndex < g_vgames.count && g_vgames.games[vgIndex].valid)
                ? g_vgames.games[vgIndex].name : "(removed)";
            ImGui::Text("%d / %d", s_sgdbSessionPos + 1, s_sgdbSessionCount);
            ImGui::Separator();

            if (s_sgdbStage == 0) {
                // Confirm stage: nothing's been fetched yet on purpose - a
                // wrong top match shouldn't cost an API call and image
                // downloads before the user gets to correct it.
                ImGui::Spacing();
                if (s_sgdbMatchCount > 0) {
                    ImGui::Text("Is this the right game for \"%s\"?", curName);
                    ImGui::Spacing();
                    ImGui::SetNextItemWidth(320);
                    if (ImGui::BeginCombo("##sgdbmatch", s_sgdbMatches[s_sgdbMatchIdx].name)) {
                        for (int m = 0; m < s_sgdbMatchCount; m++)
                            if (ImGui::Selectable(s_sgdbMatches[m].name, m == s_sgdbMatchIdx))
                                s_sgdbMatchIdx = m;
                        ImGui::EndCombo();
                    }
                    ImGui::Spacing();
                    if (ImGui::Button("Yes, Show Icons", ImVec2(160, 0)))
                        SgdbFetchAssetsForMatch(s_sgdbMatchIdx);
                } else {
                    if (s_sgdbStatus[0])
                        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s", s_sgdbStatus);
                    else
                        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No match found for \"%s\".", curName);
                }
            } else {
                // Pick stage: thumbnails arranged horizontally, click one
                // to apply it.
                if (ImGui::SmallButton("<- Back")) {
                    for (int t = 0; t < 5; t++) GuiTextureDestroy(&s_sgdbThumbTex[t]);
                    s_sgdbAssetCount = 0;
                    s_sgdbStage = 0;
                } else {
                    ImGui::SameLine();
                    ImGui::Text("Icons for: %s", s_sgdbMatches[s_sgdbMatchIdx].name);
                    ImGui::Spacing();

                    if (s_sgdbStatus[0])
                        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s", s_sgdbStatus);

                    for (int t = 0; t < s_sgdbAssetCount; t++) {
                        ImGui::PushID(t);
                        bool clicked = false;
                        if (s_sgdbThumbTex[t]) {
                            clicked = ImGui::ImageButton("##thumb",
                                (ImTextureID)(intptr_t)GuiTextureImId(s_sgdbThumbTex[t]), ImVec2(96, 96));
                        } else {
                            ImGui::Dummy(ImVec2(96, 96));
                        }
                        ImGui::PopID();
                        if (t + 1 < s_sgdbAssetCount) ImGui::SameLine();

                        if (clicked && vgIndex >= 0 && g_vgames.games[vgIndex].valid) {
                            TM_EnsureDir(VGAMES_ICONS);
                            char dst[600], other[600];
                            snprintf(dst, sizeof(dst), "%s/%s.png", VGAMES_ICONS, g_vgames.games[vgIndex].titleID);
                            snprintf(other, sizeof(other), "%s/%s.jpg", VGAMES_ICONS, g_vgames.games[vgIndex].titleID);
                            TM_CopyFile(s_sgdbThumbPath[t], dst);
                            remove(other); // in case an older import left a .jpg for this title, don't leave two
                            s_iconTexIdx = -1;
                            snprintf(s_statusMsg, sizeof(s_statusMsg), "Icon set: %s", g_vgames.games[vgIndex].name);
                            s_statusTime = 3.0f;

                            // Advance to the next game automatically -
                            // picking is the common case, so this keeps a
                            // multi-game session moving without an extra
                            // click per title.
                            s_sgdbSessionPos++;
                            if (s_sgdbSessionPos < s_sgdbSessionCount) SgdbSearchCurrent();
                            else s_sgdbDialogOpen = false;
                        }
                    }
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            bool atStart = (s_sgdbSessionPos <= 0);
            if (atStart) ImGui::BeginDisabled();
            if (ImGui::Button("Prev")) { s_sgdbSessionPos--; SgdbSearchCurrent(); }
            if (atStart) ImGui::EndDisabled();

            ImGui::SameLine();
            if (ImGui::Button("Skip")) {
                s_sgdbSessionPos++;
                if (s_sgdbSessionPos < s_sgdbSessionCount) SgdbSearchCurrent();
                else s_sgdbDialogOpen = false;
            }

            ImGui::SameLine();
            bool atEnd = (s_sgdbSessionPos + 1 >= s_sgdbSessionCount);
            if (atEnd) ImGui::BeginDisabled();
            if (ImGui::Button("Next")) { s_sgdbSessionPos++; SgdbSearchCurrent(); }
            if (atEnd) ImGui::EndDisabled();

            ImGui::SameLine();
            if (ImGui::Button("Close")) s_sgdbDialogOpen = false;
        }
        ImGui::End();
    }

    // Status message
    if (s_statusTime > 0.0f) {
        s_statusTime -= ImGui::GetIO().DeltaTime;
        float alpha = s_statusTime < 1.0f ? s_statusTime : 1.0f;
        bool isError = strncmp(s_statusMsg, "ERROR", 5) == 0;
        ImVec4 col = isError ? ImVec4(1.0f, 0.3f, 0.3f, alpha) : ImVec4(0.3f, 1.0f, 0.3f, alpha);
        ImGui::TextColored(col, "%s", s_statusMsg);
    }

    ImGui::End();
}
