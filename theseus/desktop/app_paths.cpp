#include "app_paths.h"
#include "platform_env.h"
#include "path_ci.h"
#include "xboxfs_drive.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "platform.h"
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

#ifdef THESEUS_HOST_WIN
    const char* appdata = getenv("APPDATA");
    if (appdata && *appdata)
        snprintf(s_dir, sizeof(s_dir), "%s\\Theseus", appdata);
    else
        snprintf(s_dir, sizeof(s_dir), "%s\\AppData\\Roaming\\Theseus", PlatformHomeDir());
#elif defined(THESEUS_HOST_MAC)
    snprintf(s_dir, sizeof(s_dir), "%s/Library/Application Support/Theseus", PlatformHomeDir());
#else
    const char* xdg = getenv("XDG_DATA_HOME");
    if (xdg && *xdg)
        snprintf(s_dir, sizeof(s_dir), "%s/theseus", xdg);
    else
        snprintf(s_dir, sizeof(s_dir), "%s/.local/share/theseus", PlatformHomeDir());
#endif
    return s_dir;
}

const char* Plat_ShippedDir() {
#ifdef THESEUS_HOST_MAC
    // Inside a .app the binary sits in Contents/MacOS and the payload in
    // Contents/Resources. Loose builds keep everything beside the binary.
    static char s_dir[1024] = {0};
    static bool s_init = false;
    if (!s_init) {
        s_init = true;
        const char* exe = Plat_ExeDir();
        size_t n = strlen(exe);
        const char kTail[] = "/Contents/MacOS";
        size_t t = sizeof(kTail) - 1;
        if (n > t && strcmp(exe + n - t, kTail) == 0)
            snprintf(s_dir, sizeof(s_dir), "%.*s/Contents/Resources", (int)(n - t), exe);
        else
            snprintf(s_dir, sizeof(s_dir), "%s", exe);
    }
    return s_dir;
#else
    return Plat_ExeDir();
#endif
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

    HostDir* dir = Host_OpenDir(dataDir);
    if (!dir) return;
    char name[260];
    HostFileInfo info;
    while (Host_ReadDir(dir, name, sizeof(name), &info)) {
        if (!(info.attrs & HOST_ATTR_DIR)) continue;
        if (!strcmp(name, ".") || !strcmp(name, "..")) continue;
        if (!strcasecmp(name, "shaders")) continue;
        char rel[1024];
        snprintf(rel, sizeof(rel), "Data/%s", name);
        SeedTree(rel);
    }
    Host_CloseDir(dir);
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
