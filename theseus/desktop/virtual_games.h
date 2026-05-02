// virtual_games.h: virtual game library. Serves game entries from
// games.ini without needing real folder structures; the dashboard
// sees virtual folders, XBEs, and icons. Desktop-only.

#pragma once

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sys/stat.h>

#define VGAMES_MAX 512
#define VGAMES_INI "xboxfs/C/UIX Configs/games.ini"
#define VGAMES_ICONS "xboxfs/C/UIX Configs/icons"

struct VirtualGame {
    char name[128];        // Display name / virtual folder name
    char titleID[16];      // Hex title ID (e.g. "45410013")
    char launch[512];      // Launch command
    char drive[4];         // Drive letter ("E", "F", "G")
    char category[32];     // "Games", "Applications", "Homebrew", etc.
    bool valid;
};

struct VirtualGameDB {
    VirtualGame games[VGAMES_MAX];
    int count;
    bool loaded;
};

// Global database
extern VirtualGameDB g_vgames;

// Load games.ini into memory
inline void VGames_Load() {
    if (g_vgames.loaded) return;
    g_vgames.loaded = true;
    g_vgames.count = 0;

    FILE* fp = fopen(VGAMES_INI, "r");
    if (!fp) return;

    char line[1024];
    VirtualGame* cur = NULL;

    while (fgets(line, sizeof(line), fp)) {
        // Strip newline/carriage return
        char* nl = strchr(line, '\n'); if (nl) *nl = 0;
        char* cr = strchr(line, '\r'); if (cr) *cr = 0;

        // Skip empty lines
        if (line[0] == 0) continue;

        // Section header: [Game Name]
        if (line[0] == '[') {
            char* end = strchr(line, ']');
            if (end && g_vgames.count < VGAMES_MAX) {
                cur = &g_vgames.games[g_vgames.count];
                memset(cur, 0, sizeof(*cur));
                *end = 0;
                strncpy(cur->name, line + 1, sizeof(cur->name) - 1);
                strcpy(cur->drive, "E");       // defaults
                strcpy(cur->category, "Games");
                cur->valid = true;
                g_vgames.count++;
            }
            continue;
        }

        if (!cur) continue;

        // Key=Value pairs
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
    }
    fclose(fp);
    fprintf(stdout, "[VGames] Loaded %d virtual games from games.ini\n", g_vgames.count);
}

// Reload (force re-read of games.ini)
inline void VGames_Reload() {
    g_vgames.loaded = false;
    g_vgames.count = 0;
    VGames_Load();
}

// Save current database back to games.ini
inline void VGames_Save() {
    // Ensure directory exists
    struct stat st;
    if (stat("xboxfs/C/UIX Configs", &st) != 0) {
        system("mkdir -p \"xboxfs/C/UIX Configs\"");
    }

    FILE* fp = fopen(VGAMES_INI, "w");
    if (!fp) return;

    for (int i = 0; i < g_vgames.count; i++) {
        VirtualGame& g = g_vgames.games[i];
        if (!g.valid) continue;
        fprintf(fp, "[%s]\n", g.name);
        fprintf(fp, "TitleID=%s\n", g.titleID);
        fprintf(fp, "Launch=%s\n", g.launch);
        fprintf(fp, "Drive=%s\n", g.drive);
        fprintf(fp, "Category=%s\n", g.category);
        fprintf(fp, "\n");
    }
    fclose(fp);
}

// Add a new game entry
inline int VGames_Add(const char* name, const char* titleID, const char* launch,
                       const char* drive, const char* category) {
    VGames_Load();
    if (g_vgames.count >= VGAMES_MAX) return -1;

    VirtualGame& g = g_vgames.games[g_vgames.count];
    memset(&g, 0, sizeof(g));
    strncpy(g.name, name, sizeof(g.name) - 1);
    strncpy(g.titleID, titleID, sizeof(g.titleID) - 1);
    strncpy(g.launch, launch, sizeof(g.launch) - 1);
    strncpy(g.drive, drive ? drive : "E", sizeof(g.drive) - 1);
    strncpy(g.category, category ? category : "Games", sizeof(g.category) - 1);
    g.valid = true;
    return g_vgames.count++;
}

// Update an existing game entry in-place
inline void VGames_Update(int idx, const char* name, const char* titleID,
                           const char* launch, const char* drive, const char* category) {
    if (idx < 0 || idx >= g_vgames.count) return;
    VirtualGame& g = g_vgames.games[idx];
    if (name && name[0])     strncpy(g.name, name, sizeof(g.name) - 1);
    if (titleID && titleID[0])  strncpy(g.titleID, titleID, sizeof(g.titleID) - 1);
    if (launch)   strncpy(g.launch, launch, sizeof(g.launch) - 1);
    if (drive && drive[0])    strncpy(g.drive, drive, sizeof(g.drive) - 1);
    if (category && category[0]) strncpy(g.category, category, sizeof(g.category) - 1);
}

// Find a game by name (returns index, or -1)
inline int VGames_FindByName(const char* name) {
    VGames_Load();
    for (int i = 0; i < g_vgames.count; i++) {
        if (g_vgames.games[i].valid && strcasecmp(g_vgames.games[i].name, name) == 0)
            return i;
    }
    return -1;
}

// Delete a virtual game by name: removes from games.ini and deletes icon file
inline void VGames_DeleteByName(const char* name) {
    VGames_Load();
    int idx = VGames_FindByName(name);
    if (idx < 0) return;

    // Delete icon file
    char iconPath[512];
    snprintf(iconPath, sizeof(iconPath), "xboxfs/C/UIX Configs/icons/%s.jpg", g_vgames.games[idx].titleID);
    remove(iconPath);

    // Mark as invalid
    g_vgames.games[idx].valid = false;

    // Rewrite games.ini without this entry
    FILE* fp = fopen("xboxfs/C/UIX Configs/games.ini", "w");
    if (fp) {
        for (int i = 0; i < g_vgames.count; i++) {
            if (!g_vgames.games[i].valid) continue;
            fprintf(fp, "[%s]\n", g_vgames.games[i].name);
            fprintf(fp, "TitleID=%s\n", g_vgames.games[i].titleID);
            fprintf(fp, "Launch=%s\n", g_vgames.games[i].launch);
            fprintf(fp, "Drive=%s\n", g_vgames.games[i].drive);
            if (g_vgames.games[i].category[0])
                fprintf(fp, "Category=%s\n", g_vgames.games[i].category);
            fprintf(fp, "\n");
        }
        fclose(fp);
    }
    fprintf(stderr, "[VGames] Deleted: %s\n", name);
}

// Check if a path matches a virtual game folder
// e.g. "xboxfs/E/Games/The Simpsons Road Rage" → returns game index or -1
inline int VGames_MatchFolder(const char* localPath) {
    VGames_Load();
    // Normalize: collapse double slashes and strip trailing slash
    char norm[512];
    int j = 0;
    for (int k = 0; localPath[k] && j < 510; k++) {
        if (localPath[k] == '/' && j > 0 && norm[j-1] == '/') continue; // skip double slash
        norm[j++] = localPath[k];
    }
    if (j > 0 && norm[j-1] == '/') j--; // strip trailing slash
    norm[j] = 0;

    for (int i = 0; i < g_vgames.count; i++) {
        if (!g_vgames.games[i].valid) continue;
        // Build expected path: xboxfs/{drive}/{category}/{name}
        char expected[512];
        snprintf(expected, sizeof(expected), "xboxfs/%s/%s/%s",
                 g_vgames.games[i].drive, g_vgames.games[i].category, g_vgames.games[i].name);
        if (strcasecmp(norm, expected) == 0)
            return i;
    }
    return -1;
}

// Get virtual games that belong to a specific drive+category directory
// Used by FindFirstFile/FindNextFile to inject virtual entries
inline int VGames_GetForDirectory(const char* drive, const char* category,
                                   int* outIndices, int maxOut) {
    VGames_Load();
    int count = 0;
    for (int i = 0; i < g_vgames.count && count < maxOut; i++) {
        if (!g_vgames.games[i].valid) continue;
        if (strcasecmp(g_vgames.games[i].drive, drive) == 0 &&
            strcasecmp(g_vgames.games[i].category, category) == 0) {
            outIndices[count++] = i;
        }
    }
    return count;
}

// Get the icon path for a virtual game (local filesystem path)
inline const char* VGames_GetIconPath(int idx) {
    static char s_iconBuf[512];
    if (idx < 0 || idx >= g_vgames.count) return NULL;
    snprintf(s_iconBuf, sizeof(s_iconBuf), "%s/%s.jpg", VGAMES_ICONS, g_vgames.games[idx].titleID);
    struct stat st;
    if (stat(s_iconBuf, &st) == 0)
        return s_iconBuf;
    // Try png
    snprintf(s_iconBuf, sizeof(s_iconBuf), "%s/%s.png", VGAMES_ICONS, g_vgames.games[idx].titleID);
    if (stat(s_iconBuf, &st) == 0)
        return s_iconBuf;
    return NULL;
}

// Generate a virtual .uixshortcut content for a game
inline int VGames_MakeShortcutContent(int idx, char* buf, int bufSize) {
    if (idx < 0 || idx >= g_vgames.count) return 0;
    VirtualGame& g = g_vgames.games[idx];
    return snprintf(buf, bufSize,
        "[Title]\nName=%s\nTitleID=%s\nLaunch=%s\n",
        g.name, g.titleID, g.launch);
}
