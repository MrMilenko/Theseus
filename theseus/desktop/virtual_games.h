// Virtual game library. Serves game entries straight from games.ini with no
// real folders, so the dashboard sees virtual folders, XBEs, and icons.
// Desktop only.

#pragma once

#include "app_paths.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <sys/stat.h>
#include "xboxfs_drive.h"
#include "safe_save.h"

// Roomy enough for a full local library plus a signed-in xCloud/Game Pass
// account injected at boot (see xcloud_vgames.cpp), and now an imported ES-DE
// library on top: a real rom collection runs to thousands of files.
#define VGAMES_MAX 16384
#define VGAMES_INI AppPath("Configs/games.ini")
#define VGAMES_ICONS AppPath_Tree("Configs/icons")

struct VirtualGame {
    char name[128];        // display name and virtual folder name
    char titleID[16];      // hex title id, e.g. "45410013"
    char launch[512];
    char drive[4];         // "E", "F", "G"
    char category[32];     // "Games", "Applications", "Homebrew", ...
    // Optional folder path under the category, "/" separated, e.g.
    // "Nintendo Entertainment System/Mario Series". Empty means the title
    // sits at the top of its category, which is everything that predates this.
    char group[128];
    // Injected at boot from somebody else's library (ES-DE today). Lives only
    // as long as the app does, same deal as the streaming entries.
    bool transient;
    bool valid;
};

struct VirtualGameDB {
    VirtualGame games[VGAMES_MAX];
    int count;
    bool loaded;
    // Bumps on every mutation so consumers can cache filtered slices keyed on
    // it and auto refresh when games.ini changes.
    unsigned int generation;
    // Bumps when a compact moves rows to new indices, so anything holding on
    // to an index knows to drop it.
    unsigned int layout;
};

extern VirtualGameDB g_vgames;

// A live streaming entry (xCloud / remote play), injected at boot from a
// signed-in account and never written to games.ini. Identified by its launch
// scheme so no extra field is needed.
inline bool VGames_IsStreaming(const VirtualGame& g) {
    return strncmp(g.launch, "xcloud://", 9) == 0 || strncmp(g.launch, "xhome://", 8) == 0;
}

inline void VGames_Load() {
    if (g_vgames.loaded) return;
    g_vgames.loaded = true;
    g_vgames.count = 0;

    FILE* fp = fopen(VGAMES_INI, "r");
    if (!fp) return;

    char line[1024];
    VirtualGame* cur = NULL;

    while (fgets(line, sizeof(line), fp)) {
        char* nl = strchr(line, '\n'); if (nl) *nl = 0;
        char* cr = strchr(line, '\r'); if (cr) *cr = 0;
        if (line[0] == 0) continue;

        // strrchr so ROM tags like [!] inside the name survive.
        if (line[0] == '[') {
            char* end = strrchr(line, ']');
            if (end && g_vgames.count < VGAMES_MAX) {
                cur = &g_vgames.games[g_vgames.count];
                memset(cur, 0, sizeof(*cur));
                *end = 0;
                strncpy(cur->name, line + 1, sizeof(cur->name) - 1);
                strcpy(cur->drive, "E");
                strcpy(cur->category, "Games");
                cur->valid = true;
                g_vgames.count++;
            }
            continue;
        }

        if (!cur) continue;

        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        const char* key = line;
        const char* val = eq + 1;

        if (strcmp(key, "TitleID") == 0)
            strncpy(cur->titleID, val, sizeof(cur->titleID) - 1);
        else if (strcmp(key, "Launch") == 0)
            strncpy(cur->launch, val, sizeof(cur->launch) - 1);
        else if (strcmp(key, "Drive") == 0)
            strncpy(cur->drive, val, sizeof(cur->drive) - 1);
        else if (strcmp(key, "Category") == 0)
            strncpy(cur->category, val, sizeof(cur->category) - 1);
        else if (strcmp(key, "Group") == 0)
            strncpy(cur->group, val, sizeof(cur->group) - 1);
    }
    fclose(fp);
    g_vgames.generation++;
    fprintf(stdout, "[VGames] Loaded %d virtual games from games.ini (gen %u)\n",
            g_vgames.count, g_vgames.generation);
}

inline bool VGames_IsLive(const VirtualGame& g) {
    return g.valid && (g.transient || VGames_IsStreaming(g));
}

// Close the gaps left by invalid rows so their slots can be used again.
// Shifts indices, same as a reload does.
inline void VGames_Compact() {
    int w = 0;
    for (int r = 0; r < g_vgames.count; r++) {
        if (!g_vgames.games[r].valid) continue;
        if (w != r) g_vgames.games[w] = g_vgames.games[r];
        w++;
    }
    g_vgames.generation++;
    if (w == g_vgames.count) return;
    g_vgames.count = w;
    g_vgames.layout++;
}

// Injected rows (xCloud, ES-DE) aren't in games.ini, so carry them across.
inline void VGames_Reload() {
    int nLive = 0;
    for (int i = 0; i < g_vgames.count; i++)
        if (VGames_IsLive(g_vgames.games[i])) nLive++;
    VirtualGame* live = nLive ? (VirtualGame*)malloc(nLive * sizeof(VirtualGame)) : NULL;
    if (!live) nLive = 0;
    for (int i = 0, j = 0; i < g_vgames.count && j < nLive; i++)
        if (VGames_IsLive(g_vgames.games[i])) live[j++] = g_vgames.games[i];

    g_vgames.loaded = false;
    g_vgames.count = 0;
    VGames_Load();

    for (int j = 0; j < nLive && g_vgames.count < VGAMES_MAX; j++)
        g_vgames.games[g_vgames.count++] = live[j];
    free(live);
    g_vgames.generation++;
}

inline void VGames_Save() {
    Plat_MkdirP(AppPath_Tree("Configs"));

    SafeSave save(VGAMES_INI);
    FILE* fp = save.f;
    if (!fp) return;

    for (int i = 0; i < g_vgames.count; i++) {
        VirtualGame& g = g_vgames.games[i];
        if (!g.valid) continue;
        if (VGames_IsStreaming(g) || g.transient) continue;   // live entries are never persisted
        fprintf(fp, "[%s]\n", g.name);
        fprintf(fp, "TitleID=%s\n", g.titleID);
        fprintf(fp, "Launch=%s\n", g.launch);
        fprintf(fp, "Drive=%s\n", g.drive);
        fprintf(fp, "Category=%s\n", g.category);
        // Only written when set, so a library with no folders reads back
        // byte for byte the way it always did.
        if (g.group[0]) fprintf(fp, "Group=%s\n", g.group);
        fprintf(fp, "\n");
    }
    save.Commit();
}

inline int VGames_Add(const char* name, const char* titleID, const char* launch,
                       const char* drive, const char* category) {
    VGames_Load();
    if (g_vgames.count >= VGAMES_MAX) return -1;

    VirtualGame& g = g_vgames.games[g_vgames.count];
    memset(&g, 0, sizeof(g));
    strncpy(g.name, name, sizeof(g.name) - 1);
    strncpy(g.titleID, titleID, sizeof(g.titleID) - 1);
    strncpy(g.launch, launch, sizeof(g.launch) - 1);
    strncpy(g.drive, drive ? drive : "X", sizeof(g.drive) - 1);
    strncpy(g.category, category ? category : "Games", sizeof(g.category) - 1);
    g.valid = true;
    g_vgames.generation++;
    return g_vgames.count++;
}

// Set the folder path after the fact, so VGames_Add keeps its five arguments
// and every existing caller stays put. Pass NULL or "" to flatten.
inline void VGames_SetGroup(int idx, const char* group) {
    if (idx < 0 || idx >= g_vgames.count) return;
    VirtualGame& g = g_vgames.games[idx];
    if (group && group[0]) strncpy(g.group, group, sizeof(g.group) - 1);
    else                   g.group[0] = 0;
    g.group[sizeof(g.group) - 1] = 0;
    g_vgames.generation++;
}

inline void VGames_Update(int idx, const char* name, const char* titleID,
                           const char* launch, const char* drive, const char* category) {
    if (idx < 0 || idx >= g_vgames.count) return;
    VirtualGame& g = g_vgames.games[idx];
    if (name && name[0])     strncpy(g.name, name, sizeof(g.name) - 1);
    if (titleID && titleID[0])  strncpy(g.titleID, titleID, sizeof(g.titleID) - 1);
    if (launch)   strncpy(g.launch, launch, sizeof(g.launch) - 1);
    if (drive && drive[0])    strncpy(g.drive, drive, sizeof(g.drive) - 1);
    if (category && category[0]) strncpy(g.category, category, sizeof(g.category) - 1);
    g_vgames.generation++;
}

inline int VGames_FindByName(const char* name) {
    VGames_Load();
    for (int i = 0; i < g_vgames.count; i++) {
        if (g_vgames.games[i].valid && strcasecmp(g_vgames.games[i].name, name) == 0)
            return i;
    }
    return -1;
}

inline void VGames_DeleteByName(const char* name) {
    VGames_Load();
    int idx = VGames_FindByName(name);
    if (idx < 0) return;

    char iconPath[512];
    snprintf(iconPath, sizeof(iconPath), "%s", AppPathf("Configs/icons/%s.jpg", g_vgames.games[idx].titleID));
    remove(iconPath);

    g_vgames.games[idx].valid = false;
    g_vgames.generation++;

    SafeSave save(AppPath("Configs/games.ini"));
    FILE* fp = save.f;
    if (fp) {
        for (int i = 0; i < g_vgames.count; i++) {
            if (!g_vgames.games[i].valid) continue;
            if (VGames_IsStreaming(g_vgames.games[i]) || g_vgames.games[i].transient) continue;
            fprintf(fp, "[%s]\n", g_vgames.games[i].name);
            fprintf(fp, "TitleID=%s\n", g_vgames.games[i].titleID);
            fprintf(fp, "Launch=%s\n", g_vgames.games[i].launch);
            fprintf(fp, "Drive=%s\n", g_vgames.games[i].drive);
            if (g_vgames.games[i].category[0])
                fprintf(fp, "Category=%s\n", g_vgames.games[i].category);
            fprintf(fp, "\n");
        }
        save.Commit();
    }
    fprintf(stderr, "[VGames] Deleted: %s\n", name);
}

// Match a disk path like "Library/Games/Road Rage" to a game index, or -1.
inline int VGames_MatchFolder(const char* localPath) {
    VGames_Load();
    char norm[512];
    int j = 0;
    for (int k = 0; localPath[k] && j < 510; k++) {
        if (localPath[k] == '/' && j > 0 && norm[j-1] == '/') continue;
        norm[j++] = localPath[k];
    }
    if (j > 0 && norm[j-1] == '/') j--;
    norm[j] = 0;

    for (int i = 0; i < g_vgames.count; i++) {
        if (!g_vgames.games[i].valid) continue;
        // Games carry an Xbox drive letter but live under prefix/category/name
        // on desktop. F/G/R never reach here since they have no analog.
        const char* prefix = (g_vgames.games[i].drive[0])
            ? XboxFS_DriveToPrefix(g_vgames.games[i].drive[0]) : 0;
        if (!prefix) continue;
        char expected[512];
        snprintf(expected, sizeof(expected), "%s/%s/%s",
                 prefix, g_vgames.games[i].category, g_vgames.games[i].name);
        if (strcasecmp(norm, expected) == 0)
            return i;
    }
    return -1;
}

// Split a dir like "Library/Games" into drive + category. Used by both
// FindFirstFile shims.
inline bool VGames_ParseDir(const char* dirPath, char* outDrive, char* outCat, size_t catLen) {
    if (!dirPath || !outDrive || !outCat || catLen < 2) return false;
    static const struct { const char* tree; char drive; } kMap[] = {
        { "Library", 'X' }, { "Data", 'Q' }, { "Configs", 'C' }
    };
    const char* p = NULL;
    outDrive[0] = 0;
    // Anchored at the resolved root, not searched for. Both platforms' user
    // data roots contain the words we're matching ("AppData" on Windows,
    // "Library/Application Support" on macOS), so a scan finds the wrong one.
    for (size_t i = 0; i < sizeof(kMap) / sizeof(kMap[0]); i++) {
        const char* root = AppPath_Tree(kMap[i].tree);
        if (!root || !*root) continue;
        size_t n = strlen(root), j = 0;
        for (; j < n; j++) {
            char a = dirPath[j], b = root[j];
            if (a == '\\') a = '/';
            if (b == '\\') b = '/';
            if (tolower((unsigned char)a) != tolower((unsigned char)b)) break;
        }
        if (j != n) continue;
        if (dirPath[n] != '/' && dirPath[n] != '\\') continue;
        outDrive[0] = kMap[i].drive;
        outDrive[1] = 0;
        p = dirPath + n + 1;
        break;
    }
    if (!p) return false;

    size_t ci = 0;
    while (*p && *p != '\\' && *p != '/' && ci < catLen - 1)
        outCat[ci++] = *p++;
    outCat[ci] = 0;

    static const char* gameCats[] = { "Games", "Applications", "Apps", "Homebrew", "Emulators", "Dashboards", "ES-DE" };
    for (size_t i = 0; i < sizeof(gameCats) / sizeof(gameCats[0]); i++) {
        if (strcasecmp(outCat, gameCats[i]) == 0) return true;
    }
    return false;
}

// X and E both resolve to Library, so a library written before the X: move
// still answers a request for X.
inline bool VGames_SameDrive(const char* a, const char* b) {
    if (!a || !b) return false;
    char ca = (char)toupper((unsigned char)a[0]);
    char cb = (char)toupper((unsigned char)b[0]);
    if (ca == 'E') ca = 'X';
    if (cb == 'E') cb = 'X';
    return ca == cb;
}

inline int VGames_GetForDirectory(const char* drive, const char* category,
                                   int* outIndices, int maxOut) {
    VGames_Load();
    int count = 0;
    for (int i = 0; i < g_vgames.count && count < maxOut; i++) {
        if (!g_vgames.games[i].valid) continue;
        if (VGames_SameDrive(g_vgames.games[i].drive, drive) &&
            strcasecmp(g_vgames.games[i].category, category) == 0) {
            outIndices[count++] = i;
        }
    }
    return count;
}

inline const char* VGames_GetIconPath(int idx) {
    static char s_iconBuf[512];
    if (idx < 0 || idx >= g_vgames.count) return NULL;
    snprintf(s_iconBuf, sizeof(s_iconBuf), "%s/%s.jpg", VGAMES_ICONS, g_vgames.games[idx].titleID);
    struct stat st;
    if (stat(s_iconBuf, &st) == 0)
        return s_iconBuf;
    snprintf(s_iconBuf, sizeof(s_iconBuf), "%s/%s.png", VGAMES_ICONS, g_vgames.games[idx].titleID);
    if (stat(s_iconBuf, &st) == 0)
        return s_iconBuf;
    return NULL;
}

inline int VGames_MakeShortcutContent(int idx, char* buf, int bufSize) {
    if (idx < 0 || idx >= g_vgames.count) return 0;
    VirtualGame& g = g_vgames.games[idx];
    return snprintf(buf, bufSize,
        "[Title]\nName=%s\nTitleID=%s\nLaunch=%s\n",
        g.name, g.titleID, g.launch);
}
