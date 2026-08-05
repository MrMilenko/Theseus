// launchers/emulators.h: config table + install/game detection for the
// Title Maker "Emulators" tab (PCSX2, Dolphin, RPCS3, PPSSPP, DuckStation,
// and anything else added to kEmulatorConfigs). RetroArch keeps its own
// dedicated tab/module (launchers/retroarch.h) and is not duplicated here.
//
// This header is pure data + detection logic: no ImGui, no VGames writes.
// title_maker.cpp owns the UI and turns what these functions find into
// games.ini entries, the same split retroarch.h uses (RetroArch_FindCore-
// ForSystem / RetroArch_FindBoxart are pure lookups; TM_RAImportItem in
// title_maker.cpp does the actual library writes).
//
// Ported 1:1 from the Python reference tool (emulator_launcher_maker.py):
// same EMULATORS table shape, same games.yml parser, same dev_hdd0 scan,
// same PARAM.SFO TITLE extraction, same EBOOT.BIN layout probing.

#pragma once

#include <sys/stat.h>
#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#else
#include <dirent.h>
#endif

// ============================================================================
// Config table
// ============================================================================

struct EmulatorOption {
    const char* label;       // shown next to the checkbox, e.g. "Fastboot (skip BIOS)"
    const char* flag;        // literal flag inserted into the command, e.g. "-fastboot"
    bool        defaultOn;
};

struct EmulatorConfig {
    const char* key;               // stable id, used for emulators.ini + filtering, e.g. "pcsx2"
    const char* displayName;       // sub-tab label, e.g. "PCSX2"
    const char* candidatePaths[6]; // "Find" button probes these in order; last entry must be NULL
    const EmulatorOption* options;
    int         optionCount;
    bool        needsCore;         // true only if this emulator wants a separate core/plugin file
    const char* romFlag;           // flag placed directly before the rom path; "" if none (RPCS3, PCSX2)
    const char* extensions[8];     // lowercase, no dot, e.g. {"iso","chd","bin","cso", NULL}
    bool        detectInstalled;   // show the "Detect Installed Games" button (RPCS3 today)
};

static const EmulatorOption kPCSX2Options[] = {
    { "Fastboot (skip BIOS)", "-fastboot",   true  },
    { "Fullscreen",           "-fullscreen", true  },
    { "Big Picture UI",       "-bigpicture", true  },
    { "No GUI",               "-nogui",      false },
};

static const EmulatorOption kDolphinOptions[] = {
    { "Batch mode (skip menu)", "-b", true },
};

static const EmulatorOption kRPCS3Options[] = {
    { "No GUI", "--no-gui", true },
};

static const EmulatorOption kPPSSPPOptions[] = {
    { "Fullscreen", "--fullscreen", true },
};

static const EmulatorOption kDuckStationOptions[] = {
    { "Batch mode",  "-batch",      true },
    { "Fullscreen",  "-fullscreen", true },
};

// clang-format off
static const EmulatorConfig kEmulatorConfigs[] = {
    {
        "pcsx2", "PCSX2",
        {
#ifdef __APPLE__
            "/Applications/PCSX2.app/Contents/MacOS/PCSX2",
#elif defined(_WIN32)
            "C:\\Program Files\\PCSX2\\pcsx2-qt.exe",
#else
            "/usr/bin/pcsx2-qt", "/usr/bin/pcsx2", "/var/lib/flatpak/exports/bin/net.pcsx2.PCSX2",
#endif
            NULL
        },
        kPCSX2Options, (int)(sizeof(kPCSX2Options)/sizeof(kPCSX2Options[0])),
        false, "", { "chd", "iso", "bin", "cso", NULL }, false
    },
    {
        "dolphin", "Dolphin (GC/Wii)",
        {
#ifdef __APPLE__
            "/Applications/Dolphin.app/Contents/MacOS/Dolphin",
#elif defined(_WIN32)
            "C:\\Program Files\\Dolphin\\Dolphin.exe",
#else
            "/usr/bin/dolphin-emu", "/var/lib/flatpak/exports/bin/org.DolphinEmu.dolphin-emu",
#endif
            NULL
        },
        kDolphinOptions, (int)(sizeof(kDolphinOptions)/sizeof(kDolphinOptions[0])),
        false, "-e", { "iso", "rvz", "gcm", "wbfs", NULL }, false
    },
    {
        "rpcs3", "RPCS3 (PS3)",
        {
#ifdef __APPLE__
            "/Applications/rpcs3.app/Contents/MacOS/rpcs3",
#elif defined(_WIN32)
            "C:\\Program Files\\RPCS3\\rpcs3.exe",
#else
            "/usr/bin/rpcs3", "/var/lib/flatpak/exports/bin/net.rpcs3.RPCS3",
#endif
            NULL
        },
        kRPCS3Options, (int)(sizeof(kRPCS3Options)/sizeof(kRPCS3Options[0])),
        false, "", { "bin", "iso", NULL }, true
    },
    {
        "ppsspp", "PPSSPP (PSP)",
        {
#ifdef __APPLE__
            "/Applications/PPSSPP.app/Contents/MacOS/PPSSPPSDL",
#elif defined(_WIN32)
            "C:\\Program Files\\PPSSPP\\PPSSPPWindows64.exe",
#else
            "/usr/bin/PPSSPPSDL", "/usr/bin/ppsspp", "/var/lib/flatpak/exports/bin/org.ppsspp.PPSSPP",
#endif
            NULL
        },
        kPPSSPPOptions, (int)(sizeof(kPPSSPPOptions)/sizeof(kPPSSPPOptions[0])),
        false, "", { "iso", "cso", "pbp", "chd", NULL }, false
    },
    {
        "duckstation", "DuckStation (PS1)",
        {
#ifdef __APPLE__
            "/Applications/DuckStation.app/Contents/MacOS/DuckStation",
#elif defined(_WIN32)
            "C:\\Program Files\\DuckStation\\duckstation-qt-x64-ReleaseLTCG.exe",
#else
            "/usr/bin/duckstation-qt", "/usr/bin/duckstation-nogui",
            "/var/lib/flatpak/exports/bin/org.duckstation.DuckStation",
#endif
            NULL
        },
        kDuckStationOptions, (int)(sizeof(kDuckStationOptions)/sizeof(kDuckStationOptions[0])),
        false, "", { "chd", "iso", "bin", "cue", NULL }, false
    },
};
static const int kEmulatorConfigCount = (int)(sizeof(kEmulatorConfigs)/sizeof(kEmulatorConfigs[0]));
// clang-format on

static inline const EmulatorConfig* Emulator_FindConfig(const char* key) {
    for (int i = 0; i < kEmulatorConfigCount; i++)
        if (strcmp(kEmulatorConfigs[i].key, key) == 0) return &kEmulatorConfigs[i];
    return NULL;
}

// Probe candidatePaths in order, return the first that exists on disk.
static inline bool Emulator_FindBinary(const EmulatorConfig* conf, char* out, size_t outSize) {
    for (int i = 0; conf->candidatePaths[i]; i++) {
        struct stat st;
        if (stat(conf->candidatePaths[i], &st) == 0) {
            strncpy(out, conf->candidatePaths[i], outSize - 1);
            out[outSize - 1] = 0;
            return true;
        }
    }
    return false;
}

static inline bool Emulator_ExtensionMatches(const EmulatorConfig* conf, const char* filename) {
    const char* dot = strrchr(filename, '.');
    if (!dot || !dot[1]) return false;
    for (int i = 0; conf->extensions[i]; i++) {
#ifdef _WIN32
        if (_stricmp(dot + 1, conf->extensions[i]) == 0) return true;
#else
        if (strcasecmp(dot + 1, conf->extensions[i]) == 0) return true;
#endif
    }
    return false;
}

// ============================================================================
// Per-emulator binary path persistence (Configs/emulators.ini)
//
// Deliberately self-contained: doesn't touch panel_shared.h's desktop.ini
// settings block. Same key=value-per-line shape as Icons.ini.
// ============================================================================

static inline void Emulator_LoadPaths(char paths[][512], int count) {
    for (int i = 0; i < count; i++) paths[i][0] = 0;
    const char* iniPath = AppPath("Configs/emulators.ini");
    FILE* fp = fopen(iniPath, "r");
    if (!fp) return;
    char line[600];
    while (fgets(line, sizeof(line), fp)) {
        char* nl = strchr(line, '\n'); if (nl) *nl = 0;
        char* cr = strchr(line, '\r'); if (cr) *cr = 0;
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        for (int i = 0; i < count; i++) {
            if (strcmp(line, kEmulatorConfigs[i].key) == 0) {
                strncpy(paths[i], eq + 1, 511);
                paths[i][511] = 0;
                break;
            }
        }
    }
    fclose(fp);
}

static inline void Emulator_SavePaths(char paths[][512], int count) {
    FILE* fp = fopen(AppPath("Configs/emulators.ini"), "w");
    if (!fp) return;
    for (int i = 0; i < count; i++)
        fprintf(fp, "%s=%s\n", kEmulatorConfigs[i].key, paths[i]);
    fclose(fp);
}

// ============================================================================
// RPCS3 installed-game detection
//
// Ported from the Python tool's parse_games_yml / scan_hdd_games_dir /
// parse_param_sfo_title / locate_eboot / friendly_name_for.
// ============================================================================

struct RPCS3GameEntry {
    char gameId[64];
    char folderPath[512];
    char displayName[256];
};

// games.yml is a flat "GAME_ID: /path" mapping, one per line. Same
// deliberately-minimal parser as the Python tool: avoids a yaml dependency
// since the file's structure is always this simple.
static inline int RPCS3_ParseGamesYml(const char* path, RPCS3GameEntry* out, int maxEntries) {
    int count = 0;
    FILE* fp = fopen(path, "r");
    if (!fp) return 0;
    char line[1024];
    while (fgets(line, sizeof(line), fp) && count < maxEntries) {
        char* nl = strchr(line, '\n'); if (nl) *nl = 0;
        char* cr = strchr(line, '\r'); if (cr) *cr = 0;
        char* s = line;
        while (*s == ' ' || *s == '\t') s++;
        if (!*s || *s == '#') continue;
        char* colon = strchr(s, ':');
        if (!colon) continue;
        *colon = 0;
        char* key = s;
        char* val = colon + 1;
        while (*val == ' ' || *val == '\t') val++;
        // trim trailing whitespace on val
        size_t vl = strlen(val);
        while (vl > 0 && (val[vl-1] == ' ' || val[vl-1] == '\t')) val[--vl] = 0;
        // strip surrounding quotes if present
        auto stripQuotes = [](char* v) {
            size_t l = strlen(v);
            if (l >= 2 && ((v[0] == '"' && v[l-1] == '"') || (v[0] == '\'' && v[l-1] == '\''))) {
                memmove(v, v + 1, l - 2);
                v[l - 2] = 0;
            }
        };
        stripQuotes(key);
        stripQuotes(val);
        if (!*key || !*val) continue;
        strncpy(out[count].gameId, key, sizeof(out[count].gameId) - 1);
        out[count].gameId[sizeof(out[count].gameId) - 1] = 0;
        strncpy(out[count].folderPath, val, sizeof(out[count].folderPath) - 1);
        out[count].folderPath[sizeof(out[count].folderPath) - 1] = 0;
        out[count].displayName[0] = 0;
        count++;
    }
    fclose(fp);
    return count;
}

// dev_hdd0/game/: each subfolder is a pkg-installed digital game named by ID.
static inline int RPCS3_ScanHddGamesDir(const char* hddDir, RPCS3GameEntry* out, int maxEntries) {
    int count = 0;
#ifdef _WIN32
    char searchBuf[600];
    snprintf(searchBuf, sizeof(searchBuf), "%s\\*", hddDir);
    struct _finddata_t fd;
    intptr_t hFind = _findfirst(searchBuf, &fd);
    if (hFind == -1) return 0;
    do {
        if (fd.name[0] == '.') continue;
        if (!(fd.attrib & _A_SUBDIR)) continue;
        if (count >= maxEntries) break;
        strncpy(out[count].gameId, fd.name, sizeof(out[count].gameId) - 1);
        out[count].gameId[sizeof(out[count].gameId) - 1] = 0;
        snprintf(out[count].folderPath, sizeof(out[count].folderPath), "%s/%s", hddDir, fd.name);
        out[count].displayName[0] = 0;
        count++;
    } while (_findnext(hFind, &fd) == 0 && count < maxEntries);
    _findclose(hFind);
#else
    DIR* dir = opendir(hddDir);
    if (!dir) return 0;
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL && count < maxEntries) {
        if (ent->d_name[0] == '.') continue;
        char full[600];
        snprintf(full, sizeof(full), "%s/%s", hddDir, ent->d_name);
        struct stat st;
        if (stat(full, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
        strncpy(out[count].gameId, ent->d_name, sizeof(out[count].gameId) - 1);
        out[count].gameId[sizeof(out[count].gameId) - 1] = 0;
        strncpy(out[count].folderPath, full, sizeof(out[count].folderPath) - 1);
        out[count].folderPath[sizeof(out[count].folderPath) - 1] = 0;
        out[count].displayName[0] = 0;
        count++;
    }
    closedir(dir);
#endif
    return count;
}

// Merge games.yml + dev_hdd0 scan results, de-duplicating by game ID.
// yml entries win on conflict (they may point to a more specific path).
static inline int RPCS3_DetectInstalled(const char* gamesYmlPath, const char* hddGamesDir,
                                         RPCS3GameEntry* out, int maxEntries) {
    static RPCS3GameEntry ymlEntries[512];
    static RPCS3GameEntry hddEntries[512];
    int ymlCount = RPCS3_ParseGamesYml(gamesYmlPath, ymlEntries, 512);
    int hddCount = RPCS3_ScanHddGamesDir(hddGamesDir, hddEntries, 512);

    int count = 0;
    auto addUnique = [&](RPCS3GameEntry& e) {
        if (count >= maxEntries) return;
        for (int i = 0; i < count; i++)
            if (strcmp(out[i].gameId, e.gameId) == 0) return;
        out[count++] = e;
    };
    for (int i = 0; i < ymlCount; i++) addUnique(ymlEntries[i]);
    for (int i = 0; i < hddCount; i++) addUnique(hddEntries[i]);
    return count;
}

// Minimal PARAM.SFO reader, pulls out just the TITLE field. Same binary
// layout walk as the Python parse_param_sfo_title: magic, key table
// offset, data table offset, entry count, then a 16-byte-per-entry
// index table (key_off, fmt, len, ..., data_off).
static inline bool RPCS3_ParseParamSfoTitle(const char* sfoPath, char* out, size_t outSize) {
    if (outSize) out[0] = 0;
    FILE* fp = fopen(sfoPath, "rb");
    if (!fp) return false;
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (fsize <= 20) { fclose(fp); return false; }
    std::vector<unsigned char> data((size_t)fsize);
    size_t rd = fread(data.data(), 1, (size_t)fsize, fp);
    fclose(fp);
    if (rd != (size_t)fsize) return false;

    if (!(data[0] == 0 && data[1] == 'P' && data[2] == 'S' && data[3] == 'F')) return false;

    auto rdU32 = [&](size_t off) -> unsigned {
        return (unsigned)data[off] | ((unsigned)data[off+1] << 8) |
               ((unsigned)data[off+2] << 16) | ((unsigned)data[off+3] << 24);
    };
    if ((size_t)fsize < 20) return false;
    unsigned keyTableOff  = rdU32(8);
    unsigned dataTableOff = rdU32(12);
    unsigned entries      = rdU32(16);

    for (unsigned i = 0; i < entries; i++) {
        size_t entryOff = 20 + (size_t)i * 16;
        if (entryOff + 16 > data.size()) break;
        unsigned keyOff  = (unsigned)data[entryOff] | ((unsigned)data[entryOff+1] << 8);
        unsigned dataFmt = (unsigned)data[entryOff+2] | ((unsigned)data[entryOff+3] << 8);
        unsigned dataLen = rdU32(entryOff + 4);
        unsigned dataOff = rdU32(entryOff + 12);

        size_t keyStart = (size_t)keyTableOff + keyOff;
        if (keyStart >= data.size()) continue;
        size_t keyEnd = keyStart;
        while (keyEnd < data.size() && data[keyEnd] != 0) keyEnd++;
        std::string key((const char*)&data[keyStart], keyEnd - keyStart);

        if (key == "TITLE" && (dataFmt == 0x0204 || dataFmt == 0x0004)) {
            size_t valStart = (size_t)dataTableOff + dataOff;
            if (valStart >= data.size()) return false;
            size_t valEnd = valStart + dataLen;
            if (valEnd > data.size()) valEnd = data.size();
            size_t nul = valStart;
            while (nul < valEnd && data[nul] != 0) nul++;
            std::string val((const char*)&data[valStart], nul - valStart);
            // trim trailing whitespace
            while (!val.empty() && isspace((unsigned char)val.back())) val.pop_back();
            strncpy(out, val.c_str(), outSize - 1);
            out[outSize - 1] = 0;
            return out[0] != 0;
        }
    }
    return false;
}

// PARAM.SFO TITLE -> folder name -> game ID, same fallback chain as the
// Python tool's friendly_name_for().
static inline void RPCS3_FriendlyName(const char* gameId, const char* folderPath,
                                       char* out, size_t outSize) {
    static const char* kSfoNames[] = { "PARAM.SFO", "PS3_GAME/PARAM.SFO" };
    for (int i = 0; i < 2; i++) {
        char sfoPath[700];
        snprintf(sfoPath, sizeof(sfoPath), "%s/%s", folderPath, kSfoNames[i]);
        char title[256];
        if (RPCS3_ParseParamSfoTitle(sfoPath, title, sizeof(title))) {
            strncpy(out, title, outSize - 1);
            out[outSize - 1] = 0;
            return;
        }
    }
    // folder name, else the ID itself
    const char* base = strrchr(folderPath, '/');
#ifdef _WIN32
    const char* baseW = strrchr(folderPath, '\\');
    if (baseW > base) base = baseW;
#endif
    base = base ? base + 1 : folderPath;
    if (*base && strcmp(base, gameId) != 0) {
        strncpy(out, base, outSize - 1);
        out[outSize - 1] = 0;
    } else {
        strncpy(out, gameId, outSize - 1);
        out[outSize - 1] = 0;
    }
}

// RPCS3's config dir (holds games.yml and, unless redirected, dev_hdd0/)
// defaults to $HOME/.config/rpcs3 on Linux, ~/Library/Application
// Support/rpcs3 on macOS, %APPDATA%/rpcs3 on Windows, or a portable
// "config/" folder next to the binary if the user dropped one there.
// Tries each in order, returns the first that has a games.yml.
static inline bool RPCS3_DefaultConfigDir(const char* binaryPath, char* out, size_t outSize) {
    char candidates[4][700];
    int n = 0;

    if (binaryPath && *binaryPath) {
        char binDir[600];
        strncpy(binDir, binaryPath, sizeof(binDir) - 1);
        binDir[sizeof(binDir) - 1] = 0;
        char* slash = strrchr(binDir, '/');
#ifdef _WIN32
        char* bslash = strrchr(binDir, '\\');
        if (bslash > slash) slash = bslash;
#endif
        if (slash) {
            *slash = 0;
            snprintf(candidates[n++], sizeof(candidates[0]), "%s/config", binDir);
        }
    }
#ifdef _WIN32
    const char* appdata = getenv("APPDATA");
    if (appdata) snprintf(candidates[n++], sizeof(candidates[0]), "%s/rpcs3", appdata);
#elif defined(__APPLE__)
    const char* home = getenv("HOME");
    if (home) snprintf(candidates[n++], sizeof(candidates[0]),
                        "%s/Library/Application Support/rpcs3", home);
#else
    const char* home = getenv("HOME");
    if (home) snprintf(candidates[n++], sizeof(candidates[0]), "%s/.config/rpcs3", home);
#endif

    for (int i = 0; i < n; i++) {
        char probe[750];
        snprintf(probe, sizeof(probe), "%s/games.yml", candidates[i]);
        struct stat st;
        if (stat(probe, &st) == 0) {
            strncpy(out, candidates[i], outSize - 1);
            out[outSize - 1] = 0;
            return true;
        }
    }
    if (n > 0) {
        // Nothing has games.yml yet (fresh install, no games added) - still
        // return the most likely candidate so the UI has something to show
        // rather than leaving the field blank.
        strncpy(out, candidates[0], outSize - 1);
        out[outSize - 1] = 0;
        return false;
    }
    out[0] = 0;
    return false;
}

// USRDIR/EBOOT.BIN (digital/pkg) or PS3_GAME/USRDIR/EBOOT.BIN (disc dumps).
static inline bool RPCS3_LocateEboot(const char* folderPath, char* out, size_t outSize) {
    const char* candidates[] = {
        "USRDIR/EBOOT.BIN",
        "PS3_GAME/USRDIR/EBOOT.BIN",
    };
    for (int i = 0; i < 2; i++) {
        char full[700];
        snprintf(full, sizeof(full), "%s/%s", folderPath, candidates[i]);
        struct stat st;
        if (stat(full, &st) == 0 && S_ISREG(st.st_mode)) {
            strncpy(out, full, outSize - 1);
            out[outSize - 1] = 0;
            return true;
        }
    }
    return false;
}
