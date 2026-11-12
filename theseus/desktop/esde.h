// Reads an existing ES-DE setup in place. Nothing gets moved or rewritten.
// es_systems.xml lists the systems, the rom folder is the index, and
// gamelists/<system>/gamelist.xml is optional scraped metadata layered on top.

#pragma once

struct EsdeSystem {
    char name[64];        // "nes", matches the gamelists/ subfolder
    char fullname[128];   // "Nintendo Entertainment System", shown as the category
    char romdir[512];     // absolute, %ROMPATH% already resolved
    char extensions[512]; // space separated, both cases, straight from ES-DE
    // Launch templates in ES-DE's preference order, %VARS% still unresolved.
    // First one that resolves wins, same as picking an alternative emulator.
    char commands[8][512];
    int  commandCount;
    int  gameCount;
};

struct EsdeGame {
    char name[128];
    char path[512];       // absolute
    char folder[128];     // subfolder under romdir, empty when it's at the top
    char system[64];
};

// Systems whose rom folder exists and holds at least one matching file.
int Esde_ScanSystems(const char* esdeRoot, EsdeSystem* out, int maxOut);

// False when the last scan never located an es_systems.xml, which is a
// different failure from locating one and matching no roms.
bool Esde_LastScanFoundSystemsXml(void);

// Games for one system, subfolders included. Names come from gamelist.xml
// where it has them, the filename otherwise.
int Esde_ScanGames(const char* esdeRoot, const EsdeSystem* sys, EsdeGame* out, int maxOut);

// Where ES-DE keeps its ROMs. Honours ROMDirectory in es_settings.xml and
// falls back to their default when it's empty.
const char* Esde_RomRoot(const char* esdeRoot);

// Launch command with %EMULATOR_*%, %CORE_*% and %ROM% filled in from ES-DE's
// es_find_rules.xml. Returns "" when the emulator isn't installed.
const char* Esde_ResolveCommand(const EsdeSystem* sys, const EsdeGame* game);

// ES-DE leaves fullscreen to the user's retroarch.cfg, which is the wrong
// default on a TV. -f forces it per run, borderless vs exclusive still theirs.
extern bool g_retroarchFullscreen;

// Tiny RetroArch config layered on with --appendconfig, written on first use.
// Applies only to runs we launch. Returns "" if it can't be created.
const char* RetroArch_OverrideConfig();

// Best scraped image for a game, from downloaded_media/. Box art first, then
// progressively less ideal stand-ins. Returns "" when nothing was scraped.
const char* Esde_FindArt(const char* esdeRoot, const EsdeSystem* sys, const EsdeGame* game);
