#include "app_paths.h"
#include "platform_env.h"
#include "path_ci.h"
#include "xboxfs_drive.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <map>
#include <string>

#include "host.h"

// The only thing that stays with the binary. Nobody hand edits a .bin, and
// packagers want it read only.
static const char* kShippedOnly = "Data/shaders";

const char* Plat_ExeDir() {
    static char s_dir[1024] = {0};
    static bool s_init = false;
    if (s_init) return s_dir;
    s_init = true;

    if (Host_ExePath(s_dir, sizeof(s_dir))) {
        char* last = strrchr(s_dir, '/');
        char* back = strrchr(s_dir, '\\');
        if (back > last) last = back;
        if (last) *last = '\0';
    } else {
        s_dir[0] = '\0';
    }
    if (!s_dir[0]) strcpy(s_dir, ".");
    return s_dir;
}

const char* Plat_UserDataDir() {
    static char s_dir[1024] = {0};
    static bool s_init = false;
    if (s_init) return s_dir;
    s_init = true;

    Host_UserDataDir(s_dir, sizeof(s_dir));
    return s_dir;
}

const char* Plat_ShippedDir() {
    static char s_dir[1024] = {0};
    if (!s_dir[0]) Host_ShippedDir(Plat_ExeDir(), s_dir, sizeof(s_dir));
    return s_dir;
}

static bool IsUserOwned(const char* logical) {
    if (!logical || !*logical) return false;
    size_t n = strlen(kShippedOnly);
    if (strncasecmp(logical, kShippedOnly, n) == 0 &&
        (logical[n] == '\0' || logical[n] == '/' || logical[n] == '\\'))
        return false;
    return true;
}

static void CopyFile(const char* src, const char* dst) {
    // Never overwrite: the user's copy wins, we only fill gaps.
    struct stat exists;
    if (stat(dst, &exists) == 0) return;
    FILE* in = fopen(src, "rb");
    if (!in) return;
    FILE* out = fopen(dst, "wb");
    if (!out) { fclose(in); return; }
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
        fwrite(buf, 1, n, out);
    fclose(out);
    fclose(in);
}

static void CopyTree(const char* src, const char* dst) {
    struct stat st;
    if (stat(src, &st) != 0) return;
    Plat_MkdirP(dst);
    HostDir* dir = Host_OpenDir(src);
    if (!dir) return;
    char name[260];
    HostFileInfo info;
    while (Host_ReadDir(dir, name, sizeof(name), &info)) {
        if (!strcmp(name, ".") || !strcmp(name, "..")) continue;
        char s[1024], d[1024];
        snprintf(s, sizeof(s), "%s/%s", src, name);
        snprintf(d, sizeof(d), "%s/%s", dst, name);
        if (info.attrs & HOST_ATTR_DIR) CopyTree(s, d);
        else                            CopyFile(s, d);
    }
    Host_CloseDir(dir);
}

// Data/ is shipped content, so updates have to reach installs that already
// have a copy. .shipped remembers what we copied (shipped size + mtime) and
// what the user's copy looked like right after. A copy that still matches is
// ours to replace; one the user changed is theirs and stays put.
struct ShippedRec { uint64_t shipSize; int64_t shipTime; uint64_t userSize; int64_t userTime; };
static std::map<std::string, ShippedRec> s_shipped;
static bool s_shippedDirty = false;

static std::string ShippedManifestPath() {
    return std::string(Plat_UserDataDir()) + "/Data/.shipped";
}

static void LoadShippedManifest() {
    FILE* f = fopen(ShippedManifestPath().c_str(), "r");
    if (!f) return;
    char line[1400];
    while (fgets(line, sizeof(line), f)) {
        char rel[1024]; unsigned long long ss, us; long long st, ut;
        if (sscanf(line, "%llu %lld %llu %lld %1023[^\n]", &ss, &st, &us, &ut, rel) == 5)
            s_shipped[rel] = { ss, st, us, ut };
    }
    fclose(f);
}

static void SaveShippedManifest() {
    if (!s_shippedDirty) return;
    std::string path = ShippedManifestPath(), tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "w");
    if (!f) return;
    for (const auto& kv : s_shipped)
        fprintf(f, "%llu %lld %llu %lld %s\n", (unsigned long long)kv.second.shipSize, (long long)kv.second.shipTime,
                (unsigned long long)kv.second.userSize, (long long)kv.second.userTime, kv.first.c_str());
    if (fclose(f) == 0) Host_RenameReplace(tmp.c_str(), path.c_str());
}

static bool SameFileBytes(const char* a, const char* b) {
    FILE* fa = fopen(a, "rb");
    FILE* fb = fopen(b, "rb");
    bool same = fa && fb;
    char ba[65536], bb[65536];
    while (same) {
        size_t na = fread(ba, 1, sizeof(ba), fa), nb = fread(bb, 1, sizeof(bb), fb);
        if (na != nb || memcmp(ba, bb, na) != 0) same = false;
        if (na == 0) break;
    }
    if (fa) fclose(fa);
    if (fb) fclose(fb);
    return same;
}

// Copy beside the target, then swap it in, so a crash never leaves half a file.
static bool ReplaceFile(const char* src, const char* dst) {
    std::string part = std::string(dst) + ".part";
    remove(part.c_str());
    CopyFile(src, part.c_str());
    HostFileInfo pi;
    if (!Host_Stat(part.c_str(), &pi)) return false;
    if (!Host_RenameReplace(part.c_str(), dst)) { remove(part.c_str()); return false; }
    return true;
}

static void Remember(const std::string& rel, const HostFileInfo& ship, const char* dst) {
    HostFileInfo ui;
    if (!Host_Stat(dst, &ui)) return;
    s_shipped[rel] = { ship.size, ship.mtime, ui.size, ui.mtime };
    s_shippedDirty = true;
}

// Engine content: a stale copy of these breaks the dash, so on installs from
// before .shipped existed a differing copy is set aside as .old and replaced.
// Everything else (skins, music, orbs, screenshots) is left to the user.
static bool IsEngineContent(const std::string& rel) {
    static const char* kDirs[] = { "Data/Xips/", "Data/System/", "Data/Fonts/", "Data/Language/" };
    for (const char* d : kDirs)
        if (strncasecmp(rel.c_str(), d, strlen(d)) == 0) return true;
    return false;
}

static void SyncShippedFile(const char* src, const char* dst, const std::string& rel) {
    HostFileInfo ship, user;
    if (!Host_Stat(src, &ship)) return;

    if (!Host_Stat(dst, &user)) {
        CopyFile(src, dst);
        Remember(rel, ship, dst);
        return;
    }

    auto it = s_shipped.find(rel);
    if (it != s_shipped.end()) {
        const ShippedRec& r = it->second;
        bool shipChanged = r.shipSize != ship.size || r.shipTime != ship.mtime;
        bool userUntouched = r.userSize == user.size && r.userTime == user.mtime;
        if (shipChanged && userUntouched && ReplaceFile(src, dst)) {
            Remember(rel, ship, dst);
            fprintf(stderr, "[Paths] updated %s\n", rel.c_str());
        }
        return;
    }

    // Copied before .shipped existed, so we can't tell who last touched it.
    if (SameFileBytes(src, dst)) {
        Remember(rel, ship, dst);
    } else if (IsEngineContent(rel)) {
        std::string old = std::string(dst) + ".old";
        if (Host_RenameReplace(dst, old.c_str()) && ReplaceFile(src, dst)) {
            Remember(rel, ship, dst);
            fprintf(stderr, "[Paths] updated %s (previous copy kept as .old)\n", rel.c_str());
        }
    }
}

static void SyncShippedTree(const char* src, const char* dst, const std::string& rel) {
    Plat_MkdirP(dst);
    HostDir* dir = Host_OpenDir(src);
    if (!dir) return;
    char name[260];
    HostFileInfo info;
    while (Host_ReadDir(dir, name, sizeof(name), &info)) {
        if (!strcmp(name, ".") || !strcmp(name, "..")) continue;
        char s[1024], d[1024];
        snprintf(s, sizeof(s), "%s/%s", src, name);
        snprintf(d, sizeof(d), "%s/%s", dst, name);
        std::string r = rel + "/" + name;
        if (info.attrs & HOST_ATTR_DIR) SyncShippedTree(s, d, r);
        else                            SyncShippedFile(s, d, r);
    }
    Host_CloseDir(dir);
}

// Copy one tree into the user dir if it isn't there yet.
static void SeedTree(const char* rel) {
    char dst[1024], src[1024];
    snprintf(dst, sizeof(dst), "%s/%s", Plat_UserDataDir(), rel);
    snprintf(src, sizeof(src), "%s/%s", Plat_ShippedDir(), rel);
    struct stat st;
    if (stat(src, &st) == 0) CopyTree(src, dst);   // fills gaps, never clobbers
    else                     Plat_MkdirP(dst);
}

void AppPaths_Init() {
    static bool s_done = false;
    if (s_done) return;
    s_done = true;

    const char* user = Plat_UserDataDir();
    Plat_MkdirP(user);

    // Configs and Library wholesale, then Data subdir by subdir so shaders
    // can be skipped. Seeding from what shipped beside the binary doubles as
    // the one time migration; the original is left alone as a fallback.
    SeedTree("Configs");
    SeedTree("Library");

    char dataDir[1024];
    snprintf(dataDir, sizeof(dataDir), "%s/Data", Plat_ShippedDir());
    Plat_MkdirP(AppPath_Tree("Data"));
    LoadShippedManifest();

    HostDir* dir = Host_OpenDir(dataDir);
    if (!dir) return;
    char name[260];
    HostFileInfo info;
    while (Host_ReadDir(dir, name, sizeof(name), &info)) {
        if (!(info.attrs & HOST_ATTR_DIR)) continue;
        if (!strcmp(name, ".") || !strcmp(name, "..")) continue;
        if (!strcasecmp(name, "shaders")) continue;
        char src[1024], dst[1024];
        snprintf(src, sizeof(src), "%s/%s", dataDir, name);
        snprintf(dst, sizeof(dst), "%s/Data/%s", user, name);
        SyncShippedTree(src, dst, std::string("Data/") + name);
    }
    Host_CloseDir(dir);
    SaveShippedManifest();
}

const char* AppPathf(const char* fmt, ...) {
    char logical[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(logical, sizeof(logical), fmt, ap);
    va_end(ap);
    return AppPath(logical);
}

const char* AppPath_Tree(const char* tree) {
    // One slot per tree; only the drive letter roots land here.
    static struct { char name[16]; char path[1024]; } s_cache[8];
    static int s_count = 0;
    if (!tree || !*tree) return "";

    for (int i = 0; i < s_count; i++)
        if (strcasecmp(s_cache[i].name, tree) == 0) return s_cache[i].path;
    if (s_count >= 8) return tree;

    const char* root = IsUserOwned(tree) ? Plat_UserDataDir() : Plat_ShippedDir();
    snprintf(s_cache[s_count].name, sizeof(s_cache[0].name), "%s", tree);
    snprintf(s_cache[s_count].path, sizeof(s_cache[0].path), "%s/%s", root, tree);
    return s_cache[s_count++].path;
}

void Plat_OpenInFileManager(const char* dir) {
    if (!dir || !*dir) return;
    Host_OpenFolder(dir);
}

const char* AppPath(const char* logical) {
    static char s_bufs[8][1024];
    static int s_next = 0;
    char* out = s_bufs[s_next];
    s_next = (s_next + 1) & 7;

    if (!logical || !*logical) { out[0] = '\0'; return out; }

    if (logical[0] == '/' || (logical[0] && logical[1] == ':')) {
        snprintf(out, 1024, "%s", logical);
        Plat_ResolveCaseInsensitive(out, 1024);
        return out;
    }

    const char* root = IsUserOwned(logical) ? Plat_UserDataDir() : Plat_ShippedDir();
    snprintf(out, 1024, "%s/%s", root, logical);
    Plat_ResolveCaseInsensitive(out, 1024);
    return out;
}
