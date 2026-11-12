// esde_vgames.cpp: bridges an ES-DE install into the Virtual Games DB.
//
// Reads es_systems.xml, the rom folders and the gamelists, then injects live
// VGDB entries under one "ES-DE" category, a folder per system and per rom
// subfolder. Nothing is written back and nothing lands in games.ini.
//
// Synchronous, since this is a few hundred stats against local disk.

#include "esde.h"
#include "esde_vgames.h"
#include "virtual_games.h"

#include <stdio.h>
#include <time.h>
#include <string.h>

#define ESDE_CATEGORY "ES-DE"
#define ESDE_MAX_SYS  128
#define ESDE_MAX_GAME 16384

// Stable per-path id so a title keeps the same icon slot between runs. FNV-1a,
// which is plenty for what amounts to a cache key.
static void PathTitleID(const char* path, char* out, size_t outSz)
{
    unsigned int h = 2166136261u;
    for (const char* p = path; *p; p++) {
        h ^= (unsigned char)*p;
        h *= 16777619u;
    }
    snprintf(out, outSz, "%08x", h);
}

// Copy scraped art into Configs/icons/<titleID>.<ext> where the icon redirect
// looks, same as Title Maker does for Steam. Skipped when we already have it.
static bool CacheIcon(const char* src, const char* titleID)
{
    // The redirect only probes .jpg and .png, and stb sniffs content anyway,
    // so a .jpeg lands as .jpg rather than becoming invisible.
    const char* srcExt = strrchr(src, '.');
    const char* ext = (srcExt && strcasecmp(srcExt, ".png") == 0) ? ".png" : ".jpg";

    char dst[768];
    snprintf(dst, sizeof(dst), "%s/%s%s", VGAMES_ICONS, titleID, ext);

    struct stat st;
    if (stat(dst, &st) == 0) return true;

    FILE* in = fopen(src, "rb");
    if (!in) return false;
    Plat_MkdirP(VGAMES_ICONS);
    FILE* out = fopen(dst, "wb");
    if (!out) { fclose(in); return false; }
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) fwrite(buf, 1, n, out);
    fclose(out);
    fclose(in);
    return true;
}

static void ClearEsdeEntries()
{
    for (int i = 0; i < g_vgames.count; i++) {
        VirtualGame& g = g_vgames.games[i];
        if (g.valid && g.transient && strcasecmp(g.category, ESDE_CATEGORY) == 0)
            g.valid = false;
    }
    // Every resync re-adds the whole library, so hand the old slots back.
    VGames_Compact();
}

void EsdeVGames_Clear()
{
    VGames_Load();
    ClearEsdeEntries();
}

void EsdeVGames_Sync(const char* esdeRoot)
{
    VGames_Load();
    ClearEsdeEntries();
    if (!esdeRoot || !*esdeRoot) return;

    static EsdeSystem systems[ESDE_MAX_SYS];
    int nSys = Esde_ScanSystems(esdeRoot, systems, ESDE_MAX_SYS);
    if (nSys <= 0) return;

    static EsdeGame games[ESDE_MAX_GAME];
    int total = 0, skipped = 0, withArt = 0;
    double tScan = 0, tCmd = 0, tArt = 0;

    for (int s = 0; s < nSys; s++) {
        clock_t c0 = clock();
        int n = Esde_ScanGames(esdeRoot, &systems[s], games, ESDE_MAX_GAME);
        tScan += (double)(clock() - c0) / CLOCKS_PER_SEC;
        for (int i = 0; i < n; i++) {
            c0 = clock();
            const char* cmd = Esde_ResolveCommand(&systems[s], &games[i]);
            tCmd += (double)(clock() - c0) / CLOCKS_PER_SEC;
            // No resolved emulator means no launch, so skip the row entirely.
            if (!cmd[0]) { skipped++; continue; }

            char titleID[16];
            PathTitleID(games[i].path, titleID, sizeof(titleID));

            int idx = VGames_Add(games[i].name, titleID, cmd, "E", ESDE_CATEGORY);
            if (idx < 0) { fprintf(stderr, "[ES-DE] games DB full, stopping\n"); return; }

            // System first, then whatever subfolder the rom sat in, so the
            // launcher tree matches what they see in ES-DE.
            char group[128];
            if (games[i].folder[0])
                snprintf(group, sizeof(group), "%s/%s", systems[s].fullname, games[i].folder);
            else
                snprintf(group, sizeof(group), "%s", systems[s].fullname);
            VGames_SetGroup(idx, group);
            g_vgames.games[idx].transient = true;

            // Their scraped art, cached the way Steam icons are.
            c0 = clock();
            const char* art = Esde_FindArt(esdeRoot, &systems[s], &games[i]);
            if (art[0] && CacheIcon(art, titleID)) withArt++;
            tArt += (double)(clock() - c0) / CLOCKS_PER_SEC;
            total++;
        }
    }

    g_vgames.generation++;
    fprintf(stderr, "[ES-DE] injected %d titles from %d systems, %d with scraped art", total, nSys, withArt);
    if (skipped) fprintf(stderr, " (%d skipped, emulator not installed)", skipped);
    fprintf(stderr, "\n");
    fprintf(stderr, "[ES-DE] scan %.2fs, commands %.2fs, art %.2fs\n", tScan, tCmd, tArt);
}
