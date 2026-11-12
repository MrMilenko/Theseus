#include "esde.h"
#include "platform_env.h"
#include "app_paths.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#define strcasecmp _stricmp
#else
#include <dirent.h>
#include <strings.h>
#endif

// Lives here so the ES-DE scanner stays linkable on its own. sdl_main points
// it at the saved value.
bool g_retroarchFullscreen = true;

// pause_nonactive freezes RetroArch whenever it loses focus, so a game
// launched from the dash sits paused. --appendconfig applies it per run.
const char* RetroArch_OverrideConfig()
{
    static char s_path[768];
    static bool s_tried = false;
    if (s_tried) return s_path;
    s_tried = true;

    snprintf(s_path, sizeof(s_path), "%s", AppPath("Configs/retroarch_uix.cfg"));
    FILE* fp = fopen(s_path, "w");
    if (!fp) { s_path[0] = '\0'; return s_path; }
    fprintf(fp, "# Written by UIX. Applies only to titles launched from the\n");
    fprintf(fp, "# dashboard, layered on with --appendconfig.\n");
    fprintf(fp, "pause_nonactive = \"false\"\n");
    fclose(fp);
    return s_path;
}

// These files are machine written and shallow, so a tag scanner beats dragging
// in an XML parser for five elements.
static bool TagValue(const char* xml, const char* tag, char* out, size_t outSz)
{
    char open[64], close[64];
    snprintf(open,  sizeof(open),  "<%s>",  tag);
    snprintf(close, sizeof(close), "</%s>", tag);
    const char* a = strstr(xml, open);
    if (!a) return false;
    a += strlen(open);
    const char* b = strstr(a, close);
    if (!b) return false;
    size_t n = (size_t)(b - a);
    if (n >= outSz) n = outSz - 1;
    memcpy(out, a, n);
    out[n] = '\0';
    return true;
}

// Every <command label="...">...</command> in document order. ES-DE lists its
// preferred emulator first, so that order is worth preserving.
static int AllCommands(const char* xml, char out[][512], int maxOut)
{
    int n = 0;
    const char* p = xml;
    while (n < maxOut) {
        const char* a = strstr(p, "<command");
        if (!a) break;
        a = strchr(a, '>');
        if (!a) break;
        a++;
        const char* b = strstr(a, "</command>");
        if (!b) break;
        size_t len = (size_t)(b - a);
        if (len >= 512) len = 511;
        memcpy(out[n], a, len);
        out[n][len] = '\0';
        n++;
        p = b + 10;
    }
    return n;
}

// The extension list is space separated and already carries both cases, but
// compare insensitively anyway since users rename things.
static bool MatchesExt(const char* name, const char* extList)
{
    if (!extList || !*extList) return false;
    const char* dot = strrchr(name, '.');
    if (!dot) return false;

    const char* p = extList;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        const char* s = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') p++;
        size_t n = (size_t)(p - s);
        if (n && n == strlen(dot) && strncasecmp(dot, s, n) == 0) return true;
    }
    return false;
}

const char* Esde_RomRoot(const char* esdeRoot)
{
    static char s_root[512];
    s_root[0] = '\0';

    char settings[600];
    snprintf(settings, sizeof(settings), "%s/settings/es_settings.xml", esdeRoot);
    FILE* fp = fopen(settings, "rb");
    if (fp) {
        char line[1024];
        while (fgets(line, sizeof(line), fp)) {
            if (!strstr(line, "ROMDirectory")) continue;
            const char* v = strstr(line, "value=\"");
            if (!v) break;
            v += 7;
            const char* e = strchr(v, '"');
            if (e && e > v) {
                size_t n = (size_t)(e - v);
                if (n >= sizeof(s_root)) n = sizeof(s_root) - 1;
                memcpy(s_root, v, n);
                s_root[n] = '\0';
            }
            break;
        }
        fclose(fp);
    }
    // Empty means ES-DE's own default, which is ~/ROMs everywhere.
    if (!s_root[0])
        snprintf(s_root, sizeof(s_root), "%s/ROMs", PlatformHomeDir());
    return s_root;
}

static bool g_esdeFoundSystemsXml = false;

// Custom systems win, same order ES-DE uses, then the copy inside the install.
// An AppImage keeps its copy inside the mount, so nothing is on disk for us to
// read; those users point us straight at an es_systems.xml instead.
static bool FindSystemsXml(const char* esdeRoot, char* out, size_t outSz)
{
    struct stat st;

    // Pointed straight at the file.
    size_t rl = strlen(esdeRoot);
    const size_t kName = sizeof("es_systems.xml") - 1;
    if (rl >= kName && strcasecmp(esdeRoot + rl - kName, "es_systems.xml") == 0 &&
        stat(esdeRoot, &st) == 0) {
        snprintf(out, outSz, "%s", esdeRoot);
        return true;
    }

    snprintf(out, outSz, "%s/custom_systems/es_systems.xml", esdeRoot);
    fprintf(stderr, "[ES-DE] probe %s\n", out);
    if (stat(out, &st) == 0) return true;

    char home[1024];
    snprintf(home, sizeof(home), "%s", PlatformHomeDir());

    char userLocal[1024], homeEsde[1024];
    snprintf(userLocal, sizeof(userLocal),
             "%s/.local/share/es-de/resources/systems/linux/es_systems.xml", home);
    snprintf(homeEsde, sizeof(homeEsde),
             "%s/ES-DE/resources/systems/linux/es_systems.xml", home);

    const char* kBundled[] = {
        "/Applications/ES-DE.app/Contents/Resources/resources/systems/macos/es_systems.xml",
        "/usr/share/es-de/resources/systems/linux/es_systems.xml",
        "/usr/local/share/es-de/resources/systems/linux/es_systems.xml",
        "/usr/share/emulationstation/resources/systems/linux/es_systems.xml",
        "/app/share/es-de/resources/systems/linux/es_systems.xml",
        "/var/lib/flatpak/app/org.es_de.frontend/current/active/files/share/es-de/resources/systems/linux/es_systems.xml",
        "/var/lib/snapd/snap/es-de/current/usr/share/es-de/resources/systems/linux/es_systems.xml",
        userLocal,
        homeEsde,
        "C:/Program Files/ES-DE/resources/systems/windows/es_systems.xml",
    };
    for (size_t i = 0; i < sizeof(kBundled) / sizeof(kBundled[0]); i++) {
        fprintf(stderr, "[ES-DE] probe %s\n", kBundled[i]);
        if (stat(kBundled[i], &st) == 0) {
            snprintf(out, outSz, "%s", kBundled[i]);
            return true;
        }
    }
    out[0] = '\0';
    return false;
}

// True when we could not find a systems definition at all, which is a
// different failure from finding one and matching no roms.
bool Esde_LastScanFoundSystemsXml(void)
{
    return g_esdeFoundSystemsXml;
}

// One directory level. cb gets (name, isDir) and stops the walk on false.
template <typename F>
static void ForEachEntry(const char* dir, F cb)
{
#ifdef _WIN32
    char pattern[1024];
    snprintf(pattern, sizeof(pattern), "%s/*", dir);
    struct _finddata_t fd;
    intptr_t h = _findfirst(pattern, &fd);
    if (h == -1) return;
    do {
        if (fd.name[0] == '.') continue;
        if (!cb(fd.name, (fd.attrib & _A_SUBDIR) != 0)) break;
    } while (_findnext(h, &fd) == 0);
    _findclose(h);
#else
    DIR* d = opendir(dir);
    if (!d) return;
    struct dirent* e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        // readdir already knows the type. stat() per entry costs a network
        // round trip, which on an SMB rom library is the whole scan.
        bool isDir;
        if (e->d_type == DT_DIR)       isDir = true;
        else if (e->d_type == DT_REG) isDir = false;
        else {
            // Symlinks and anything readdir won't commit to still need a look.
            char full[1024];
            snprintf(full, sizeof(full), "%s/%s", dir, e->d_name);
            struct stat st;
            if (stat(full, &st) != 0) continue;
            isDir = S_ISDIR(st.st_mode) != 0;
        }
        if (!cb(e->d_name, isDir)) break;
    }
    closedir(d);
#endif
}

// A .app is a directory on macOS, so extension wins over file type. Anything
// else that's a directory is a subfolder to descend into.
struct RomWalk {
    const EsdeSystem* sys;
    EsdeGame* out;
    int maxOut;
    int count;
    bool countOnly;
};

static void WalkRoms(RomWalk& w, const char* dir, const char* folder, int depth)
{
    if (w.count >= w.maxOut) return;

    ForEachEntry(dir, [&](const char* name, bool isDir) -> bool {
        if (w.count >= w.maxOut) return false;

        if (MatchesExt(name, w.sys->extensions)) {
            if (!w.countOnly) {
                EsdeGame& g = w.out[w.count];
                memset(&g, 0, sizeof(g));
                snprintf(g.system, sizeof(g.system), "%s", w.sys->name);
                snprintf(g.folder, sizeof(g.folder), "%s", folder);
                snprintf(g.path, sizeof(g.path), "%s/%s", dir, name);
                snprintf(g.name, sizeof(g.name), "%s", name);
                char* dot = strrchr(g.name, '.');
                if (dot && dot != g.name) *dot = '\0';
            }
            w.count++;
            return true;
        }

        // ES-DE nests one useful level (a folder per publisher, per disc set),
        // but stop somewhere so a stray symlink loop can't run away.
        if (isDir && depth < 4) {
            char sub[1024], subFolder[128];
            snprintf(sub, sizeof(sub), "%s/%s", dir, name);
            if (folder && *folder) snprintf(subFolder, sizeof(subFolder), "%s/%s", folder, name);
            else                   snprintf(subFolder, sizeof(subFolder), "%s", name);
            WalkRoms(w, sub, subFolder, depth + 1);
        }
        return true;
    });
}

int Esde_ScanSystems(const char* esdeRoot, EsdeSystem* out, int maxOut)
{
    if (!esdeRoot || !*esdeRoot || !out || maxOut <= 0) return 0;

    char sysXml[700];
    g_esdeFoundSystemsXml = FindSystemsXml(esdeRoot, sysXml, sizeof(sysXml));
    if (!g_esdeFoundSystemsXml) {
        fprintf(stderr, "[ES-DE] no es_systems.xml found for root %s\n", esdeRoot);
        return 0;
    }
    fprintf(stderr, "[ES-DE] systems from %s\n", sysXml);

    FILE* fp = fopen(sysXml, "rb");
    if (!fp) return 0;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0 || sz > 8 * 1024 * 1024) { fclose(fp); return 0; }
    char* buf = (char*)malloc((size_t)sz + 1);
    if (!buf) { fclose(fp); return 0; }
    buf[fread(buf, 1, (size_t)sz, fp)] = '\0';
    fclose(fp);

    const char* romRoot = Esde_RomRoot(esdeRoot);
    int count = 0;

    const char* p = buf;
    while (count < maxOut) {
        const char* a = strstr(p, "<system>");
        if (!a) break;
        const char* b = strstr(a, "</system>");
        if (!b) break;

        char block[4096];
        size_t blockLen = (size_t)(b - a);
        if (blockLen >= sizeof(block)) blockLen = sizeof(block) - 1;
        memcpy(block, a, blockLen);
        block[blockLen] = '\0';
        p = b + 9;

        EsdeSystem s;
        memset(&s, 0, sizeof(s));
        if (!TagValue(block, "name", s.name, sizeof(s.name))) continue;
        if (!TagValue(block, "extension", s.extensions, sizeof(s.extensions))) continue;

        char rawPath[512] = "";
        TagValue(block, "path", rawPath, sizeof(rawPath));
        const char* tail = strstr(rawPath, "%ROMPATH%");
        if (tail)         snprintf(s.romdir, sizeof(s.romdir), "%s%s", romRoot, tail + 9);
        else if (*rawPath) snprintf(s.romdir, sizeof(s.romdir), "%s", rawPath);
        else               snprintf(s.romdir, sizeof(s.romdir), "%s/%s", romRoot, s.name);

        struct stat st;
        if (stat(s.romdir, &st) != 0 || !S_ISDIR(st.st_mode)) continue;

        // Only "is there anything in here", so maxOut 1 stops at the first
        // hit. A real count means walking every rom folder twice.
        RomWalk w = { &s, NULL, 1, 0, true };
        WalkRoms(w, s.romdir, "", 0);
        if (w.count == 0) continue;   // folder exists but they've put nothing in it

        // Filled in by whoever calls Esde_ScanGames, which has to walk anyway.
        s.gameCount = 0;
        if (!TagValue(block, "fullname", s.fullname, sizeof(s.fullname)))
            snprintf(s.fullname, sizeof(s.fullname), "%s", s.name);
        s.commandCount = AllCommands(block, s.commands, 8);

        out[count++] = s;
    }

    free(buf);
    return count;
}

// gamelist.xml keys off the same relative path ES-DE wrote, so match on the
// tail of ours rather than trying to rebuild theirs.
static void OverlayNames(const char* esdeRoot, const EsdeSystem* sys, EsdeGame* games, int count)
{
    char gl[900];
    snprintf(gl, sizeof(gl), "%s/gamelists/%s/gamelist.xml", esdeRoot, sys->name);
    FILE* fp = fopen(gl, "rb");
    if (!fp) return;

    char block[8192];
    size_t blockLen = 0;
    bool inGame = false;
    char line[2048];

    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "<game>")) { inGame = true; blockLen = 0; block[0] = '\0'; continue; }
        if (!inGame) continue;

        if (strstr(line, "</game>")) {
            inGame = false;
            char rel[512] = "", name[128] = "";
            if (!TagValue(block, "path", rel, sizeof(rel)))  continue;
            if (!TagValue(block, "name", name, sizeof(name))) continue;

            const char* r = rel;
            if (r[0] == '.' && (r[1] == '/' || r[1] == '\\')) r += 2;
            size_t rlen = strlen(r);

            for (int i = 0; i < count; i++) {
                size_t plen = strlen(games[i].path);
                if (plen < rlen) continue;
                const char* tail = games[i].path + (plen - rlen);
                if (strcasecmp(tail, r) == 0) {
                    snprintf(games[i].name, sizeof(games[i].name), "%s", name);
                    break;
                }
            }
            continue;
        }

        size_t n = strlen(line);
        if (blockLen + n < sizeof(block) - 1) {
            memcpy(block + blockLen, line, n);
            blockLen += n;
            block[blockLen] = '\0';
        }
    }
    fclose(fp);
}

int Esde_ScanGames(const char* esdeRoot, const EsdeSystem* sys, EsdeGame* out, int maxOut)
{
    if (!esdeRoot || !sys || !out || maxOut <= 0) return 0;

    RomWalk w = { sys, out, maxOut, 0, false };
    WalkRoms(w, sys->romdir, "", 0);
    OverlayNames(esdeRoot, sys, out, w.count);
    return w.count;
}

// Scraped art lives at downloaded_media/<system>/<type>/<rom path relative to
// the rom folder, extension swapped>. Subfolders are mirrored.
const char* Esde_FindArt(const char* esdeRoot, const EsdeSystem* sys, const EsdeGame* game)
{
    static char s_art[1024];
    s_art[0] = '\0';
    if (!esdeRoot || !*esdeRoot || !sys || !game) return s_art;

    // Path relative to the rom folder, minus the extension.
    size_t rlen = strlen(sys->romdir);
    const char* rel = game->path;
    if (strncasecmp(game->path, sys->romdir, rlen) == 0 && game->path[rlen] == '/')
        rel = game->path + rlen + 1;

    // Usually the extension is dropped, but not for .app bundles, so try the
    // whole filename too rather than guessing which rule ES-DE applied.
    char stems[2][768];
    snprintf(stems[0], sizeof(stems[0]), "%s", rel);
    char* dot = strrchr(stems[0], '.');
    char* slash = strrchr(stems[0], '/');
    if (dot && (!slash || dot > slash)) *dot = '\0';
    snprintf(stems[1], sizeof(stems[1]), "%s", rel);

    // Nothing scraped for this system means 36 guaranteed misses per game, so
    // check the folder once and cache the answer.
    static char s_lastSys[64] = "";
    static bool s_lastHasMedia = false;
    if (strcmp(s_lastSys, sys->name) != 0) {
        snprintf(s_lastSys, sizeof(s_lastSys), "%s", sys->name);
        char probe[900];
        snprintf(probe, sizeof(probe), "%s/downloaded_media/%s", esdeRoot, sys->name);
        struct stat mst;
        s_lastHasMedia = (stat(probe, &mst) == 0);
    }
    if (!s_lastHasMedia) return s_art;

    // Box art reads best on the orb. The rest are stand-ins, worst last.
    static const char* kTypes[] = {
        "covers", "miximages", "3dboxes", "screenshots", "titlescreens", "marquees"
    };
    static const char* kExts[] = { ".png", ".jpg", ".jpeg" };

    struct stat st;
    for (size_t t = 0; t < sizeof(kTypes) / sizeof(kTypes[0]); t++) {
        for (size_t s = 0; s < 2; s++) {
            for (size_t e = 0; e < sizeof(kExts) / sizeof(kExts[0]); e++) {
                snprintf(s_art, sizeof(s_art), "%s/downloaded_media/%s/%s/%s%s",
                         esdeRoot, sys->name, kTypes[t], stems[s], kExts[e]);
                if (stat(s_art, &st) == 0) return s_art;
            }
            if (strcmp(stems[0], stems[1]) == 0) break;
        }
    }
    s_art[0] = '\0';
    return s_art;
}

// ---------------------------------------------------------------------------
// Launch commands. Emulator locations live in es_find_rules.xml, not in the
// system definitions, so <command> templates carry %EMULATOR_RETROARCH% style
// placeholders that mean nothing until that file is read.
// ---------------------------------------------------------------------------

static void ExpandTilde(char* path, size_t sz)
{
    if (path[0] != '~') return;
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s%s", PlatformHomeDir(), path + 1);
    snprintf(path, sz, "%s", tmp);
}

// One <emulator> or <core> block: first entry that exists on disk wins.
// systempath entries name a bare command, so they're taken on faith.
static bool ResolveRule(const char* block, char* out, size_t outSz)
{
    const char* p = block;
    bool systemPath = strstr(block, "type=\"systempath\"") != NULL;
    while ((p = strstr(p, "<entry>")) != NULL) {
        p += 7;
        const char* e = strstr(p, "</entry>");
        if (!e) break;
        char cand[1024];
        size_t n = (size_t)(e - p);
        if (n >= sizeof(cand)) n = sizeof(cand) - 1;
        memcpy(cand, p, n);
        cand[n] = '\0';
        p = e;
        ExpandTilde(cand, sizeof(cand));
        struct stat st;
        if (systemPath || stat(cand, &st) == 0) {
            snprintf(out, outSz, "%s", cand);
            return true;
        }
    }
    return false;
}

// Look up one name in es_find_rules.xml. tag is "emulator" or "core".
// File and answers are both cached; a 4000 rom library asks the same dozen
// questions tens of thousands of times.
static bool FindRule(const char* tag, const char* name, char* out, size_t outSz)
{
    static struct { char key[96]; char path[768]; bool found; } s_cache[64];
    static int s_cacheCount = 0;

    char key[96];
    snprintf(key, sizeof(key), "%s:%s", tag, name);
    for (int i = 0; i < s_cacheCount; i++) {
        if (strcmp(s_cache[i].key, key) != 0) continue;
        if (s_cache[i].found) snprintf(out, outSz, "%s", s_cache[i].path);
        return s_cache[i].found;
    }

    static char s_rules[512];
    static bool s_tried = false;
    if (!s_tried) {
        s_tried = true;
        s_rules[0] = '\0';
        static const char* kPaths[] = {
            "/Applications/ES-DE.app/Contents/Resources/resources/systems/macos/es_find_rules.xml",
            "/usr/share/es-de/resources/systems/linux/es_find_rules.xml",
            "/app/share/es-de/resources/systems/linux/es_find_rules.xml",
            "C:/Program Files/ES-DE/resources/systems/windows/es_find_rules.xml",
        };
        struct stat st;
        for (size_t i = 0; i < sizeof(kPaths) / sizeof(kPaths[0]); i++)
            if (stat(kPaths[i], &st) == 0) { snprintf(s_rules, sizeof(s_rules), "%s", kPaths[i]); break; }
    }
    static char* s_buf = NULL;
    if (!s_buf && s_rules[0]) {
        FILE* fp = fopen(s_rules, "rb");
        if (fp) {
            fseek(fp, 0, SEEK_END);
            long sz = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            if (sz > 0 && sz <= 4 * 1024 * 1024) {
                s_buf = (char*)malloc((size_t)sz + 1);
                if (s_buf) s_buf[fread(s_buf, 1, (size_t)sz, fp)] = '\0';
            }
            fclose(fp);
        }
    }
    if (!s_buf) return false;
    const char* buf = s_buf;

    char open[128], close[64];
    snprintf(open,  sizeof(open),  "<%s name=\"%s\">", tag, name);
    snprintf(close, sizeof(close), "</%s>", tag);
    bool ok = false;
    const char* a = strstr(buf, open);
    if (a) {
        const char* b = strstr(a, close);
        if (b) {
            char block[4096];
            size_t n = (size_t)(b - a);
            if (n >= sizeof(block)) n = sizeof(block) - 1;
            memcpy(block, a, n);
            block[n] = '\0';
            ok = ResolveRule(block, out, outSz);
        }
    }
    if (s_cacheCount < (int)(sizeof(s_cache) / sizeof(s_cache[0]))) {
        snprintf(s_cache[s_cacheCount].key, sizeof(s_cache[0].key), "%s", key);
        if (ok) snprintf(s_cache[s_cacheCount].path, sizeof(s_cache[0].path), "%s", out);
        s_cache[s_cacheCount].found = ok;
        s_cacheCount++;
    }
    return ok;
}

// Substitute one whitespace-delimited piece of a <command> template. False
// when it names a missing emulator, which kills the whole command.
static bool ExpandToken(const char* tok, const EsdeSystem* sys, const EsdeGame* game,
                        char* out, size_t outSz)
{
    out[0] = '\0';
    // ES-DE's own behaviour flags. They mean nothing outside ES-DE.
    if (strcmp(tok, "%ENABLESHORTCUTS%") == 0 || strcmp(tok, "%RUNINBACKGROUND%") == 0 ||
        strcmp(tok, "%EMUDIR%") == 0)
        return true;

    if (strncmp(tok, "%EMULATOR_", 10) == 0) {
        char name[64];
        snprintf(name, sizeof(name), "%s", tok + 10);
        char* pct = strchr(name, '%');
        if (pct) *pct = '\0';
        // OS-SHELL backs the "native port" and "shortcut or script" commands
        // ES-DE lists last. A shell always exists, so every system would
        // otherwise resolve and hand zsh a rom to execute.
        if (strcmp(name, "OS-SHELL") == 0) {
            struct stat st;
            if (stat(game->path, &st) != 0) return false;
            if (!(st.st_mode & S_IXUSR)) return false;
        }
        return FindRule("emulator", name, out, outSz);
    }

    // Cores appear as %CORE_RETROARCH%/mesen_libretro.dylib, so keep whatever
    // trailed the placeholder.
    if (strncmp(tok, "%CORE_", 6) == 0) {
        const char* pct = strchr(tok + 6, '%');
        if (!pct) return false;
        char name[64];
        size_t n = (size_t)(pct - (tok + 6));
        if (n >= sizeof(name)) n = sizeof(name) - 1;
        memcpy(name, tok + 6, n);
        name[n] = '\0';
        char dir[768];
        if (!FindRule("core", name, dir, sizeof(dir))) return false;
        snprintf(out, outSz, "%s%s", dir, pct + 1);
        // An installed RetroArch with none of the cores is the common case,
        // so check for the .dylib itself rather than the folder holding it.
        struct stat st;
        if (stat(out, &st) != 0) return false;
        return true;
    }

    if (strcmp(tok, "%ROM%") == 0 || strcmp(tok, "%ROMRAW%") == 0) {
        snprintf(out, outSz, "%s", game->path);
        return true;
    }
    if (strcmp(tok, "%ROMPATH%") == 0) {
        snprintf(out, outSz, "%s", sys->romdir);
        return true;
    }
    if (strcmp(tok, "%BASENAME%") == 0) {
        const char* base = strrchr(game->path, '/');
        snprintf(out, outSz, "%s", base ? base + 1 : game->path);
        char* dot = strrchr(out, '.');
        if (dot && dot != out) *dot = '\0';
        return true;
    }
    if (strcmp(tok, "%GAMEDIR%") == 0) {
        snprintf(out, outSz, "%s", game->path);
        char* slash = strrchr(out, '/');
        if (slash) *slash = '\0';
        return true;
    }
    // Anything else wrapped in percent signs is an ES-DE internal we don't
    // model (%INJECT%, %STARTDIR%, ...). Dropping it beats guessing.
    if (tok[0] == '%') return true;

    snprintf(out, outSz, "%s", tok);
    return true;
}

const char* Esde_ResolveCommand(const EsdeSystem* sys, const EsdeGame* game)
{
    static char s_cmd[2048];
    s_cmd[0] = '\0';
    if (!sys || !game || sys->commandCount <= 0) return s_cmd;

    // A macOS .app is a bundle, not something a shell can exec, and ES-DE's
    // own answer here is its OS-SHELL rule. `open` is the honest equivalent.
    size_t plen = strlen(game->path);
    if (plen > 4 && strcasecmp(game->path + plen - 4, ".app") == 0) {
        snprintf(s_cmd, sizeof(s_cmd), "open \"%s\"", game->path);
        return s_cmd;
    }

    for (int c = 0; c < sys->commandCount; c++) {
    size_t used = 0;
    const char* p = sys->commands[c];
    bool ok = true;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        // Keep quoted runs together: ares takes --system "Famicom Disk System".
        char tok[1024];
        size_t n = 0;
        bool quoted = false;
        while (*p && (quoted || (*p != ' ' && *p != '\t'))) {
            if (*p == '"') { quoted = !quoted; p++; continue; }
            if (n + 1 < sizeof(tok)) tok[n++] = *p;
            p++;
        }
        tok[n] = '\0';
        if (!n) continue;

        char exp[1536];
        if (!ExpandToken(tok, sys, game, exp, sizeof(exp))) { ok = false; break; }
        if (!exp[0]) continue;

        // Quote late, once, so a path with spaces survives /bin/sh.
        bool needQuote = strchr(exp, ' ') != NULL;
        int wrote = snprintf(s_cmd + used, sizeof(s_cmd) - used, "%s%s%s%s",
                             used ? " " : "", needQuote ? "\"" : "", exp, needQuote ? "\"" : "");
        if (wrote < 0 || (size_t)wrote >= sizeof(s_cmd) - used) { ok = false; break; }
        used += (size_t)wrote;

        // ES-DE never passes RetroArch a display flag, it expects
        // retroarch.cfg to decide. Own token, or it lands inside the quotes.
        if (strcmp(tok, "%EMULATOR_RETROARCH%") == 0) {
            const char* ovr = RetroArch_OverrideConfig();
            wrote = snprintf(s_cmd + used, sizeof(s_cmd) - used, "%s%s%s%s",
                             g_retroarchFullscreen ? " -f" : "",
                             ovr[0] ? " --appendconfig \"" : "", ovr[0] ? ovr : "",
                             ovr[0] ? "\"" : "");
            if (wrote < 0 || (size_t)wrote >= sizeof(s_cmd) - used) { ok = false; break; }
            used += (size_t)wrote;
        }
    }
    if (ok && s_cmd[0]) return s_cmd;
    s_cmd[0] = '\0';
    }
    return s_cmd;
}
