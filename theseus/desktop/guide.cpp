// guide.cpp: the guide. One panel over the dashboard, drawn natively with
// the overlay (ui_overlay.h): sections down the left, the open one's rows on
// the right, the prompts along the bottom. The dashboard keeps drawing
// underneath but gets no input while it's up.
//
// Sections: Settings (pages from the settings table, LB and RB switch
// them), Accounts, Games & Playlists, Logs & Help, Shortcuts, About,
// Desktop Tools (the ImGui windows) and Exit. B backs out a level, to the
// sections, then closes. On a first run, or with Startup set to ask, it
// opens on the Welcome screen instead, which offers and never insists.
// Folders, keys and names are typed on its own keyboard, or straight in.

#include "std.h"
#include "dashapp.h"
#include "panel_shared.h"
#include "settings_schema.h"
#include "host.h"
#include "app_paths.h"
#include "display.h"
#include "preloader.h"
#include "log_redact.h"
#include "virtual_games.h"
#include "playlist.h"
#include "esde_vgames.h"
#include "udata_synth.h"
#include "plex_client.h"
#include "jellyfin_client.h"
#include "xcloud_client.h"
#include "launchers/xemu.h"
#include "launchers/pcsx2.h"
#include "xiso.h"
#include "exe_icon.h"
#include "xdg_apps.h"
#include "guide.h"
#include "ui_overlay.h"
#include "pads.h"
#include "launchers/steam.h"
#include "settings_backup.h"
#include "launchers/stores.h"
#include <algorithm>
#include <ctype.h>
#include <strings.h>
#include <time.h>
#include <bgfx/bgfx.h>
#include <unistd.h>
#include <errno.h>
#include <SDL.h>
#include "imgui.h"
#include <string>
#include <vector>

class CLevel;
extern CLevel* g_pActiveLevel;
extern bool g_padModeEnabled, g_padModeActive;
extern bool g_mediaFullscreen, g_xcloudFullscreen;
extern void* g_pActiveKeyboard_void();
extern int g_guideTheme;

bool g_guideRequested = false;
bool g_welcomeDone = false;

extern int g_argc;
extern char** g_argv;

// Starts this program over with one mode flag in place of any other.
static void RestartWithMode(const char* flag)
{
    std::vector<char*> args;
    args.push_back(g_argv[0]);
    for (int i = 1; i < g_argc; i++) {
        if (!strcmp(g_argv[i], "--dashboard") || !strcmp(g_argv[i], "--development") ||
            !strcmp(g_argv[i], "--preview")) continue;
        args.push_back(g_argv[i]);
    }
    args.push_back((char*)flag);
    args.push_back(NULL);
    execv(g_argv[0], args.data());
    fprintf(stderr, "[Guide] restart failed: %s\n", strerror(errno));
}

// Starts this program over the way it was started.
static void Restart()
{
    execv(g_argv[0], g_argv);
    fprintf(stderr, "[Guide] restart failed: %s\n", strerror(errno));
}

static const char* ShippedVersion()
{
    static char s_ver[64] = "";
    if (s_ver[0]) return s_ver;
    char path[1024];
    snprintf(path, sizeof(path), "%s/Configs/version", Plat_ShippedDir());
    FILE* vf = fopen(path, "r");
    if (vf) {
        char line[128];
        while (fgets(line, sizeof(line), vf)) {
            if (strncmp(line, "version=", 8) == 0) {
                line[strcspn(line, "\r\n")] = 0;
                snprintf(s_ver, sizeof(s_ver), "%s", line + 8);
                break;
            }
        }
        fclose(vf);
    }
    if (!s_ver[0]) snprintf(s_ver, sizeof(s_ver), "unknown");
    return s_ver;
}

static std::string NameOnly(const std::string& path)
{
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

static std::string VersionLine()
{
    char line[256];
    const bgfx::Caps* caps = bgfx::getCaps();
    snprintf(line, sizeof(line), "UIX Desktop %s (%s, %s)", ShippedVersion(), Host_Name(),
             caps ? bgfx::getRendererName(caps->rendererType) : "no renderer");
    return line;
}

static const char* HomeDir()
{
    const char* h = getenv("HOME");
    if (!h || !*h) h = getenv("USERPROFILE");
    return h;
}

// This run's log, home folder as ~ and addresses masked, onto the clipboard.
static bool CopyLog()
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/theseus.log", Plat_UserDataDir());
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    // The tail is what matters, and a clipboard is no place for megabytes.
    const long kMax = 512 * 1024;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    long from = size > kMax ? size - kMax : 0;
    fseek(f, from, SEEK_SET);
    std::string text((size_t)(size - from), '\0');
    size_t got = fread(&text[0], 1, text.size(), f);
    fclose(f);
    text.resize(got);
    std::string out = VersionLine() + "\n\n" + Log_Redact(text, HomeDir());
    return SDL_SetClipboardText(out.c_str()) == 0;
}

int TitleMaker_ImportSteam(int* outSkipped);
int TitleMaker_ImportRetroArch(int* outSkipped);
extern int Title_SanitizeName(const char*, char*, size_t);
extern char s_esdePath[512];

// PC stores with an Import row of their own, shown only when the store is here.
static const struct Store {
    const char* label;
    const char* help;
    bool (*found)();
    void (*scan)(std::vector<StoreGame>&);
} kStores[] = {
    { "Import Epic Games", "Installed games from the Epic launcher and Heroic.", Epic_Found, Epic_Scan },
    { "Import GOG Games", "Installed games from GOG Galaxy, GOG installers and Heroic.", Gog_Found, Gog_Scan },
    { "Import Lutris Games", "Installed games from your Lutris library, with its cover art.", Lutris_Found, Lutris_Scan },
    { "Import itch.io Games", "Games installed with the itch.io app.", Itch_Found, Itch_Scan },
};
static const int kStoreCount = (int)(sizeof(kStores) / sizeof(kStores[0]));

static const char* kCategories[] = { "Games", "Applications", "Homebrew", "Emulators", "Dashboards" };
static const int   kCategoryCount = (int)(sizeof(kCategories) / sizeof(kCategories[0]));

// What an added file runs with. A disc image needs an emulator in front.
enum Runner { RUN_SELF, RUN_XEMU, RUN_PCSX2, RUN_COUNT };
static const char* kRunners[RUN_COUNT] = { "Itself", "xemu", "PCSX2" };

// A file browser entry. macOS apps are folders, but you pick them like files.
struct BrowseEntry { std::string name; bool dir; };

static bool IsAppBundle(const std::string& n)
{
    return n.size() > 4 && strcasecmp(n.c_str() + n.size() - 4, ".app") == 0;
}

static void ListDir(const std::string& dir, std::vector<BrowseEntry>& out)
{
    out.clear();
    HostDir* d = Host_OpenDir(dir.c_str());
    if (!d) return;
    char name[512];
    bool isDir = false;
    while (out.size() < 4000 && Host_ReadDirType(d, name, sizeof(name), &isDir)) {
        if (name[0] == '.') continue;
        BrowseEntry e = { name, isDir && !IsAppBundle(name) };
        out.push_back(e);
    }
    Host_CloseDir(d);
    std::sort(out.begin(), out.end(), [](const BrowseEntry& a, const BrowseEntry& b) {
        if (a.dir != b.dir) return a.dir;
        return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
    });
}

static std::string ParentDir(const std::string& dir)
{
    size_t end = dir.size();
    while (end > 1 && (dir[end - 1] == '/' || dir[end - 1] == '\\')) end--;
    size_t cut = dir.find_last_of("/\\", end - 1);
    if (cut == std::string::npos) return dir;
    if (cut == 0) return "/";
    if (cut == 2 && dir[1] == ':') return dir.substr(0, 3);
    return dir.substr(0, cut);
}

// "Some Game.app" or "/x/y/Game.exe" -> "Some Game" / "Game", made safe for
// the dashboard's font.
static std::string NameFromPath(const std::string& path)
{
    size_t slash = path.find_last_of("/\\");
    std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
    size_t dot = base.find_last_of('.');
    if (dot != std::string::npos && dot > 0) base = base.substr(0, dot);
    char clean[128];
    Title_SanitizeName(base.c_str(), clean, sizeof(clean));
    return clean;
}

// Somewhere to put things on a fresh install: Movies, TV Shows and ROMs next
// to the bundled Music folder. Only fills roots that are still empty.
static void SetUpFolders()
{
    extern char g_musicRoot[512], g_moviesRoot[512], g_tvRoot[512], g_romsDir[512];
    struct { char* root; size_t cch; const char* name; } kFolders[] = {
        { g_musicRoot,  sizeof(g_musicRoot),  "Music" },
        { g_moviesRoot, sizeof(g_moviesRoot), "Movies" },
        { g_tvRoot,     sizeof(g_tvRoot),     "TV Shows" },
        { g_romsDir,    sizeof(g_romsDir),    "ROMs" },
    };
    for (size_t i = 0; i < sizeof(kFolders) / sizeof(kFolders[0]); i++) {
        char dir[1024];
        snprintf(dir, sizeof(dir), "%s/%s", AppPath_Tree("Library"), kFolders[i].name);
        Host_MakeDir(dir);
        if (!kFolders[i].root[0]) snprintf(kFolders[i].root, kFolders[i].cch, "%s", dir);
    }
    SaveDesktopSettings();
}

// A few rows need more than their label says.
static const char* SettingHelp(const SettingDef& d)
{
    static const struct { const char* key; const char* help; } kHelp[] = {
        { "StartupMode",         "What happens when UIX Desktop starts." },
        { "UseOnScreenKeyboard", "Text fields ignore the physical keyboard and use the pad keyboard." },
        { "ShowBootAnimation",   "Play the Xbox boot animation once at startup." },
        { "MenuBarAutoHide",     "In Development Mode the tool bar shows when the pointer reaches the top." },
        { "EmulateXboxRes",      "Keep scenes framed the way they were authored." },
        { "Hwdec",               "Faster on AMD and NVIDIA. Applies to the next video." },
        { "MasterVolume",        "Dashboard sounds, music and videos." },
        { "UseMilkdropViz",      "Use projectM for the music visualizer." },
        { "ShowAlbumCover",      "Show album art in the orb when a soundtrack has it." },
        { "CRT_Enabled",         "Old TV look over the whole dashboard." },
        { "GuideTheme",          "The guide's colors." },
        { "FetchGameArt",        "Box art for games without an icon. Icons you picked are never replaced." },
        { "HideDuplicateGames",  "A game in Steam and ES-DE shows once: Steam first, then yours, then ES-DE." },
        { "Rumble",              "A short buzz when the guide can't do what you asked." },
        { "ButtonStyle",         "The buttons the guide shows. Match the Pad follows the pad you used last." },
        { "SwapConfirm",         "Nintendo style: the right face button selects and the bottom one goes back." },
    };
    for (size_t i = 0; i < sizeof(kHelp) / sizeof(kHelp[0]); i++)
        if (strcmp(kHelp[i].key, d.key) == 0) return kHelp[i].help;
    if (d.flags & SF_RESTART) return "Takes effect the next time UIX Desktop starts.";
    if (d.kind == SK_PATH)    return "Select it to type or paste the full path.";
    if (d.flags & SF_SECRET)  return "Select it to type or paste it. It stays hidden.";
    if (d.kind == SK_STRING)  return "Select it to type or paste it.";
    return "Left and right change it.";
}

static const char* kPages[] = { "General", "Display", "Audio", "Media", "Games", "Controller" };
static const int   kPageCount = (int)(sizeof(kPages) / sizeof(kPages[0]));

enum Section { SEC_SETTINGS, SEC_ACCOUNTS, SEC_LIBRARY, SEC_LOGS, SEC_SHORTCUTS, SEC_ABOUT, SEC_TOOLS, SEC_EXIT,
               SEC_COUNT };
static const char* kSections[SEC_COUNT] = {
    "Settings", "Accounts", "Games & Playlists", "Logs & Help", "Shortcuts", "About", "Desktop Tools", "Exit",
};

enum View { GV_PAGE, GV_ACCOUNTS, GV_LIBRARY, GV_ADDGAME, GV_BROWSE, GV_PLAYLISTS, GV_PLAYLIST, GV_LOGS,
            GV_SHORTCUTS, GV_ABOUT, GV_TOOLS, GV_EXIT, GV_WELCOME, GV_GAMES, GV_GAME, GV_LETTERS, GV_APPS,
            GV_COLLECTIONS, GV_GAMECOLL, GV_BACKUPS, GV_RESTORE };

// What a game list shows.
enum GameList { GL_FAVORITES, GL_RECENT, GL_HIDDEN, GL_ALL, GL_LETTER, GL_SEARCH, GL_COLLECTION };

static const View kSectionViews[SEC_COUNT] = {
    GV_PAGE, GV_ACCOUNTS, GV_LIBRARY, GV_LOGS, GV_SHORTCUTS, GV_ABOUT, GV_TOOLS, GV_EXIT,
};

enum Act {
    GA_NONE, GA_VIEW, GA_SETTING, GA_PADTOOLS, GA_WINDOW, GA_RESUME, GA_QUIT,
    GA_DEVMODE, GA_FOLDERS, GA_OPENFOLDERS,
    GA_OPENLOGS, GA_COPYVERSION, GA_COPYLOG,
    GA_STEAM, GA_RETROARCH, GA_ESDE, GA_CHOOSEFILE, GA_CATEGORY, GA_ADDGAME,
    GA_BROWSEUP, GA_BROWSE, GA_PLAYLIST,
    GA_PLEX, GA_PLEXOUT, GA_JELLYFIN, GA_JELLYFINOUT, GA_XBOX, GA_XBOXOUT,
    GA_TEXT, GA_NAME, GA_JELLYFINURL,
    GA_GAMELIST, GA_GAME, GA_LAUNCH, GA_FAVORITE, GA_HIDE, GA_LETTER, GA_SEARCH,
    GA_FINDEMU, GA_RUNNER, GA_APPS, GA_APP, GA_APPGAMES, GA_GAMEOPT, GA_STORE,
    GA_COLLECTION, GA_GAMECOLL, GA_INCOLL, GA_NEWCOLL,
    GA_BACKUP, GA_BACKUPS, GA_PICKBACKUP, GA_RESTORE, GA_OPENBACKUPS,
    GA_COUNT
};

enum Window { WIN_TITLEMAKER, WIN_HDD, WIN_PLAYLISTS, WIN_SETTINGS, WIN_INSPECTOR, WIN_XAPEDITOR };

struct Row {
    std::string label, value, help;
    int act;
    int arg;   // view to open, setting index, window...
};

// What the pointer can land on.
enum Target { T_SECTION, T_ROW, T_TAB, T_PROMPT, T_KEY };
struct HitTarget { float x0, y0, x1, y1; int kind, index; };

// The panel's place in the 640x480 layout.
enum {
    PANEL_X = 40, PANEL_Y = 30, PANEL_W = 560, PANEL_H = 420,
    PANEL_R = PANEL_X + PANEL_W, PANEL_B = PANEL_Y + PANEL_H,
    INSET = 16,
    CONTENT_L = PANEL_X + INSET, CONTENT_R = PANEL_R - INSET,
    HEADER_Y = PANEL_Y + 12, RULE_Y = PANEL_Y + 46,
    BODY_Y = PANEL_Y + 58,
    PROMPTS_Y = PANEL_B - 28, BODY_B = PROMPTS_Y - 30,
    SECTIONS_W = 152, SECTION_H = 26, SECTION_GAP = 4,
    LIST_L = CONTENT_L + SECTIONS_W + 14,
    ROW_H = 24, ROW_GAP = 4,
    REPEAT_DELAY = 400, REPEAT_INTERVAL = 110,
    FIELD_Y = BODY_Y + 4, FIELD_H = 32, KEYS_Y = BODY_Y + 50, KEY_H = 34, KEY_GAP = 5,
};

// The keyboard: four rows of ten characters, then the wide keys.
enum { KEY_CHARS = 40, K_SHIFT = KEY_CHARS, K_SYMBOLS, K_SPACE, K_DELETE, K_PASTE, K_DONE, KEY_COUNT };
static const char* const kKeyRows[3][4] = {
    { "1234567890", "qwertyuiop", "asdfghjkl:", "zxcvbnm./-" },
    { "1234567890", "QWERTYUIOP", "ASDFGHJKL:", "ZXCVBNM./-" },
    { "1234567890", "!@#$%^&*()", "~`_=+[]{}\\", "|;'\",<>?/:" },
};
// The wide keys' left edges and widths, in keys.
static const float kWideKeys[KEY_COUNT - KEY_CHARS][2] = {
    { 0, 1.5f }, { 1.5f, 1.5f }, { 3, 3 }, { 6, 1.5f }, { 7.5f, 1.25f }, { 8.75f, 1.25f },
};

enum EditTarget { ED_SETTING, ED_NAME, ED_JELLYFIN, ED_SEARCH, ED_GAMEOPT, ED_COLLECTION };

struct Editor {
    bool on, secret, shift, symbols;
    int what, arg, key;
    std::string title, text;
    size_t caret, limit;   // bytes
    Uint32 since;
};

static struct Guide {
    bool open, welcome, inPanel, startChecked, swallow, foldersDone, settle;
    int section, page, cursor, scroll;
    View view;
    struct Frame { View view; int cursor, scroll; };
    std::vector<Frame> stack;
    std::vector<Row> rows;

    unsigned held, repeatHeld;
    Uint32 repeatTime;
    int device;
    bool pointerMoved, clickMoved;
    float pointerX, pointerY, wheel;

    int copied;
    std::string result[GA_COUNT];
    std::string storeResult[kStoreCount];
    std::string browseDir, pickedPath, pickedName, added;
    std::string found[4];   // what each Find row turned up
    std::vector<XdgApp> apps;   // the Linux app menu, read when its list opens
    std::vector<BrowseEntry> entries;
    bool browseStale;
    int category, playlist, runner;
    int gameList, game;
    char letter;           // GL_LETTER's initial, '#' for digits and the rest
    std::string search;    // GL_SEARCH's text
    std::string collection;              // GL_COLLECTION's name
    std::string filter;    // the game list's filter, empty for all
    int sort;              // All Games' order, a SORT_
    std::vector<std::string> backups;   // GV_BACKUPS' rows, newest first
    std::string backup;                 // the one GV_RESTORE offers
    std::vector<std::string> collNames;  // the game page's Collections rows, kept while it's open
    unsigned gameLayout;   // g_vgames.layout when game was picked

    std::vector<HitTarget> targets;
    int promptButtons[6];
    Editor edit;
} g;

static void Add(const char* label, const char* value, const char* help, int act = GA_NONE, int arg = 0)
{
    Row r;
    r.label = label; r.value = value ? value : ""; r.help = help ? help : "";
    r.act = act; r.arg = arg;
    g.rows.push_back(r);
}

// What a name files under: A to Z, everything else is '#'.
static char Initial(const char* name)
{
    char c = (char)toupper((unsigned char)name[0]);
    return (c >= 'A' && c <= 'Z') ? c : '#';
}

// Case blind, so "halo" finds "Halo 2".
static bool NameHas(const char* name, const std::string& part)
{
    size_t n = strlen(name);
    for (size_t i = 0; i + part.size() <= n; i++) {
        size_t k = 0;
        while (k < part.size() && tolower((unsigned char)name[i + k]) == tolower((unsigned char)part[k])) k++;
        if (k == part.size()) return true;
    }
    return false;
}

// Where a game comes from, for the game list filter.
static std::string GameSource(const VirtualGame& vg)
{
    if (vg.transient) {
        // ES-DE rows sit in a folder per system.
        const char* slash = strchr(vg.group, '/');
        std::string sys = slash ? std::string(vg.group, slash - vg.group) : std::string(vg.group);
        return sys.empty() ? "ES-DE" : "ES-DE: " + sys;
    }
    if (strncmp(vg.launch, "xcloud://", 9) == 0) return "xCloud";
    if (strncmp(vg.launch, "xhome://", 8) == 0)  return "Remote Play";
    static const struct { const char* scheme; const char* name; } kStores[] = {
        { "steam://", "Steam" }, { "com.epicgames.launcher://", "Epic" }, { "heroic://", "Heroic" },
        { "goggalaxy://", "GOG" }, { "itch://", "itch.io" }, { "uplay://", "Ubisoft" }, { "origin2://", "EA" },
        { "link2ea://", "EA" }, { "battlenet://", "Battle.net" }, { "amazon-games://", "Amazon" },
        { "lutris:", "Lutris" }, { "retroarch://", "RetroArch" },
    };
    for (size_t i = 0; i < sizeof(kStores) / sizeof(kStores[0]); i++)
        if (strncasecmp(vg.launch, kStores[i].scheme, strlen(kStores[i].scheme)) == 0) return kStores[i].name;
    // The program's own name, quoted or not, minus its folder.
    const char* p = vg.launch;
    while (*p == ' ') p++;
    std::string exe;
    if (*p == '"') { const char* q = strchr(p + 1, '"'); exe = q ? std::string(p + 1, q - p - 1) : std::string(p + 1); }
    else           { const char* q = strchr(p, ' ');      exe = q ? std::string(p, q - p) : std::string(p); }
    size_t slash = exe.find_last_of("/\\");
    if (slash != std::string::npos) exe = exe.substr(slash + 1);
    if (strncasecmp(exe.c_str(), "xemu", 4) == 0 || strstr(vg.launch, "-dvd_path")) return "xemu";
    if (strncasecmp(exe.c_str(), "pcsx2", 5) == 0) return "PCSX2";
    if (strncasecmp(exe.c_str(), "retroarch", 9) == 0) return "RetroArch";
    if (strstr(vg.launch, "://")) return "Other Launchers";
    return "Programs";
}

// All Games' orders. Y steps through them.
enum { SORT_NAME, SORT_TIME, SORT_LAST, SORT_COUNT };
static const char* const kSorts[SORT_COUNT] = { "A to Z", "Time Played", "Last Played" };

// The two filters that aren't a source.
static const char kMostPlayed[] = "Most Played", kNeverPlayed[] = "Never Played";

static bool PassesFilter(const VirtualGame& vg)
{
    if (g.filter.empty()) return true;
    if (g.filter == kMostPlayed) return vg.playTime > 0;
    if (g.filter == kNeverPlayed) return !vg.playTime && !vg.lastPlayed;
    return GameSource(vg) == g.filter;
}

static void GameListRows(int list, std::vector<int>& out);
// The filters worth offering on this list: sources it has, A to Z with
// Programs last, then the play counts. Empty first, for all.
static void FilterChoices(int list, std::vector<std::string>& out)
{
    std::vector<int> rows;
    GameListRows(list, rows);
    out.clear();
    bool programs = false, played = false, never = false;
    for (size_t i = 0; i < rows.size(); i++) {
        const VirtualGame& vg = g_vgames.games[rows[i]];
        std::string src = GameSource(vg);
        if (src == "Programs") programs = true;
        else if (std::find(out.begin(), out.end(), src) == out.end()) out.push_back(src);
        if (vg.playTime > 0) played = true;
        if (!vg.playTime && !vg.lastPlayed) never = true;
    }
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
        return strcasecmp(a.c_str(), b.c_str()) < 0;
    });
    if (programs) out.push_back("Programs");
    if (played) out.push_back(kMostPlayed);
    if (never) out.push_back(kNeverPlayed);
    out.insert(out.begin(), std::string());
}

// X steps through FilterChoices, back around to all.
static void CycleFilter()
{
    std::vector<std::string> choices;
    FilterChoices(g.gameList, choices);
    size_t at = std::find(choices.begin(), choices.end(), g.filter) - choices.begin();
    g.filter = choices[(at + 1) % choices.size()];
    if (choices.size() < 2 && g.device == 1) Pads_Rumble(0.35f, 90);
    g.cursor = g.scroll = 0;
}

// A game list as the list view shows it, filter and all.
static void FilteredRows(int list, std::vector<int>& out)
{
    GameListRows(list, out);
    // A to Z already; the others keep it for ties.
    if (list == GL_ALL && g.sort == SORT_TIME)
        std::stable_sort(out.begin(), out.end(), [](int a, int b) {
            return g_vgames.games[a].playTime > g_vgames.games[b].playTime;
        });
    else if (list == GL_ALL && g.sort == SORT_LAST)
        std::stable_sort(out.begin(), out.end(), [](int a, int b) {
            return g_vgames.games[a].lastPlayed > g_vgames.games[b].lastPlayed;
        });
    if (g.filter.empty()) return;
    std::vector<int> kept;
    for (size_t i = 0; i < out.size(); i++)
        if (PassesFilter(g_vgames.games[out[i]])) kept.push_back(out[i]);
    out.swap(kept);
    if (g.filter == kMostPlayed)
        std::stable_sort(out.begin(), out.end(), [](int a, int b) {
            return g_vgames.games[a].playTime > g_vgames.games[b].playTime;
        });
}

// Whichever order the list is in now.
static int ListSort()
{
    if (g.view == GV_GAMES && g.gameList == GL_ALL && g.sort) return g.sort;
    return g.filter == kMostPlayed ? SORT_TIME : SORT_NAME;
}

// The rows of one game list, as DB indices, before any filter.
static void GameListRows(int list, std::vector<int>& out)
{
    out.clear();
    if (list == GL_FAVORITES) { VGames_MarkList(VGAMES_FAVORITES, out); return; }
    if (list == GL_RECENT)    { VGames_MarkList(VGAMES_RECENT, out); return; }
    if (list == GL_COLLECTION) { VGames_MarkList((VGAMES_COLLECTION_PREFIX + g.collection).c_str(), out); return; }
    VGames_Load();
    for (int i = 0; i < g_vgames.count; i++) {
        const VirtualGame& vg = g_vgames.games[i];
        if (list == GL_HIDDEN ? !(vg.valid && vg.hidden) : !VGames_IsShown(vg)) continue;
        if (list == GL_LETTER && Initial(vg.name) != g.letter) continue;
        if (list == GL_SEARCH && !NameHas(vg.name, g.search)) continue;
        out.push_back(i);
    }
    std::sort(out.begin(), out.end(), [](int a, int b) {
        return strcasecmp(g_vgames.games[a].name, g_vgames.games[b].name) < 0;
    });
}

static std::string PlayTime(long long s)
{
    char buf[32];
    if (s <= 0) return "None yet";
    if (s < 3600) snprintf(buf, sizeof(buf), "%lld min", s < 60 ? 1 : s / 60);
    else snprintf(buf, sizeof(buf), "%lld h %02lld min", s / 3600, (s % 3600) / 60);
    return buf;
}

static std::string PlayedOn(long long t)
{
    if (!t) return "Never";
    time_t tt = (time_t)t;
    char buf[32];
    strftime(buf, sizeof(buf), "%b %d, %H:%M", localtime(&tt));
    return buf;
}

// The picked game, or NULL once the DB has moved rows around under us.
static VirtualGame* PickedGame()
{
    if (g.gameLayout != g_vgames.layout || g.game < 0 || g.game >= g_vgames.count) return NULL;
    VirtualGame* vg = &g_vgames.games[g.game];
    return vg->valid ? vg : NULL;
}

// Button names in help text, as this pad prints them.
static const char* Shoulders()
{
    switch (Pads_Style()) {
    case UI_PAD_PLAYSTATION: return "L1 and R1";
    case UI_PAD_NINTENDO:    return "L and R";
    default:                 return "LB and RB";
    }
}

static const char* GuideCombo()
{
    switch (Pads_Style()) {
    case UI_PAD_PLAYSTATION: return "PS button, L2+Circle or F7";
    case UI_PAD_NINTENDO:    return "Home button, ZL+A or F7";
    default:                 return "Guide button, LT+B or F7";
    }
}

static const char* kGameListNames[] = { "Favorites", "Recently Played", "Hidden Games", "All Games", "", "", "" };

// Emulators the guide can look for, each under its path setting.
static const struct {
    const char* key;
    const char* findLabel;
    const char* findHelp;
    bool (*find)(char* out, size_t outSize);
} kEmus[] = {
    { "XemuPath", "Find xemu", "Looks in the usual install folders, RetroBat, scoop, Flatpak and AppImages.",
      Xemu_Find },
    { "Pcsx2Path", "Find PCSX2", "Looks in the usual install folders, RetroBat, scoop, Flatpak, Lutris and AppImages.",
      Pcsx2_Find },
};
static const int kEmuCount = (int)(sizeof(kEmus) / sizeof(kEmus[0]));

static void Build()
{
    g.rows.clear();
    switch (g.view) {
    case GV_PAGE: {
        const SettingsTable& t = DesktopSettings_Table();
        char val[512];
        for (int i = 0; i < t.count; i++) {
            const SettingDef& d = t.defs[i];
            if (d.flags & SF_HIDDEN) continue;
            if (strcmp(d.page, kPages[g.page]) != 0) continue;
            Setting_Display(d, val, sizeof(val));
            bool typed = d.kind == SK_PATH || d.kind == SK_STRING;
            Add(d.label, val, SettingHelp(d), typed ? GA_TEXT : GA_SETTING, i);
            for (int e = 0; e < kEmuCount; e++)
                if (strcmp(d.key, kEmus[e].key) == 0)
                    Add(kEmus[e].findLabel, g.found[e].c_str(), kEmus[e].findHelp, GA_FINDEMU, e);
        }
        break;
    }
    case GV_ABOUT:
        Add("Version",  ShippedVersion(), "The build you're running.");
        Add("Platform", Host_Name(), NULL);
        {
            VGames_Load();
            long long total = 0;
            for (int i = 0; i < g_vgames.count; i++)
                if (g_vgames.games[i].valid) total += g_vgames.games[i].playTime;
            Add("Time Played", PlayTime(total).c_str(), "Every game together, counted when each one closes.");
        }
        Add("Created by", "Milenko", NULL);
        Add("UIX Lite references", "BigJx, ImOkRuOk", NULL);
        Add("", "Odb718, Rocky5, TeamUIX", NULL);
        Add("PrometheOS, Toolbox", "Team-Resurgent", NULL);
        Add("Original dashboard", "Microsoft", NULL);
        Add("Community", "Xbox-Scene", NULL);
        Add("Guide font", "Noto Sans, SIL OFL 1.1", NULL);
        Add("And everyone who contributed", "", NULL);
        break;
    case GV_WELCOME: {
        const char* intro = g_welcomeDone
            ? (Pads_Style() == UI_PAD_PLAYSTATION ? "Cross or Circle starts the dashboard." : "A or B starts the dashboard.")
            : "Nothing here is required. It's all in the guide later too.";
        Add("Start the Dashboard", "", intro, GA_RESUME);
        Add("Development Mode", "", "Edit the scenes live. Restarts UIX Desktop.", GA_DEVMODE);
        if (!g_welcomeDone || g.foldersDone)
            Add("Set Up My Folders", g.foldersDone ? "Done" : "",
                "Makes Music, Movies, TV Shows and ROMs folders. Fill them when you like.", GA_FOLDERS);
        // First run: fill the launcher from what's already on this machine.
        if (!g_welcomeDone) {
            char roots[4][512];
            if (Steam_DiscoverInstallRoots(s_steamPath, roots, 4) > 0)
                Add("Import My Steam Games", g.result[GA_STEAM].c_str(), "Adds installed Steam games with their icons.",
                    GA_STEAM);
            for (int e = 0; e < kEmuCount; e++)
                Add(kEmus[e].findLabel, g.found[e].c_str(), kEmus[e].findHelp, GA_FINDEMU, e);
        }
        Add("Open My Folders", "", "Shows them in your file manager.", GA_OPENFOLDERS);
        if (const SettingDef* mode = DesktopSettings_Find("StartupMode")) {
            char val[64];
            Setting_Display(*mode, val, sizeof(val));
            Add(mode->label, val, "Ask on launch brings this screen back each time.", GA_SETTING,
                (int)(mode - DesktopSettings_Table().defs));
        }
        break;
    }
    case GV_LIBRARY: {
        Add("Search by Name", "", "Type part of a name. \"halo\" finds Halo 2.", GA_SEARCH);
        Add("Find a Game", "", "Pick the first letter, then the game.", GA_VIEW, GV_LETTERS);
        Add("Add a Game", "", "Pick a program or file and it shows up in the launcher.", GA_VIEW, GV_ADDGAME);
        Add("Import Steam Library", g.result[GA_STEAM].c_str(), "Adds installed Steam games with their icons.", GA_STEAM);
        Add("Import RetroArch Recent", g.result[GA_RETROARCH].c_str(), "Adds what RetroArch's playlists list.", GA_RETROARCH);
        Add("Refresh ES-DE", g.result[GA_ESDE].c_str(), "Reads your ES-DE game lists again.", GA_ESDE);
        for (int s = 0; s < kStoreCount; s++)
            if (kStores[s].found())
                Add(kStores[s].label, g.storeResult[s].c_str(), kStores[s].help, GA_STORE, s);
        if (strcmp(Host_Name(), "Linux") == 0)
            Add("Import Linux Apps", "", "Games and apps from your app menu, Flatpak and your Desktop.", GA_APPS);
        char n[32];
        std::vector<int> list;
        static const char* kListHelp[] = {
            "Games you starred. They get their own spot in the launcher too.",
            "The last games you started, newest first.",
            "Games kept out of the launcher. Pick one to bring it back.",
            "Everything in the launcher, A to Z.",
        };
        for (int l = GL_FAVORITES; l <= GL_ALL; l++) {
            GameListRows(l, list);
            snprintf(n, sizeof(n), "%d", (int)list.size());
            std::string help = kListHelp[l];
            if (l == GL_ALL) help = help + " " + Shoulders() + " jump a letter.";
            Add(kGameListNames[l], n, help.c_str(), GA_GAMELIST, l);
        }
        std::vector<std::string> colls;
        VGames_Collections(colls, false);
        snprintf(n, sizeof(n), "%d", (int)colls.size());
        Add("Collections", n, "Your own lists. Add a game to one from its page.", GA_VIEW, GV_COLLECTIONS);
        snprintf(n, sizeof(n), "%d", Playlist_Count());
        Add("Playlists", n, "Made in the Playlist Maker.", GA_VIEW, GV_PLAYLISTS);
        break;
    }
    case GV_COLLECTIONS: {
        std::vector<std::string> colls;
        std::vector<int> list;
        VGames_Collections(colls, false);
        for (size_t i = 0; i < colls.size(); i++) {
            VGames_MarkList((VGAMES_COLLECTION_PREFIX + colls[i]).c_str(), list);
            char n[16];
            snprintf(n, sizeof(n), "%d", (int)list.size());
            Add(colls[i].c_str(), n, "Shows in the launcher too while it has games in it.", GA_COLLECTION, (int)i);
        }
        if (g.rows.empty())
            Add("No collections yet", "", "Open a game and pick Collections to start one.");
        break;
    }
    case GV_GAMECOLL: {
        VirtualGame* vg = PickedGame();
        if (!vg) { Add("This game moved", "", "Go back and pick it again."); break; }
        // A collection emptied here keeps its row until you leave, so it can go back in.
        std::vector<std::string> colls;
        VGames_Collections(colls, false);
        for (size_t i = 0; i < colls.size(); i++) {
            bool have = false;
            for (size_t k = 0; k < g.collNames.size() && !have; k++)
                have = strcasecmp(g.collNames[k].c_str(), colls[i].c_str()) == 0;
            if (!have) g.collNames.push_back(colls[i]);
        }
        std::sort(g.collNames.begin(), g.collNames.end(), [](const std::string& a, const std::string& b) {
            return strcasecmp(a.c_str(), b.c_str()) < 0;
        });
        Add("New Collection", g.result[GA_NEWCOLL].c_str(), "Name it and this game goes in.", GA_NEWCOLL);
        for (size_t i = 0; i < g.collNames.size(); i++)
            Add(g.collNames[i].c_str(), VGames_InCollection(*vg, g.collNames[i].c_str()) ? "Yes" : "No",
                "A puts it in or takes it out.", GA_INCOLL, (int)i);
        break;
    }
    case GV_ADDGAME: {
        std::string file = g.pickedPath.empty() ? "Choose" : NameOnly(g.pickedPath);
        Add("File", file.c_str(), g.pickedPath.empty() ? "A program, a shortcut or a ROM." : g.pickedPath.c_str(),
            GA_CHOOSEFILE);
        Add("Name", g.pickedName.c_str(),
            g.pickedPath.empty() ? "Taken from the file name." : "Taken from the file name. Select it to rename it.",
            g.pickedPath.empty() ? GA_NONE : GA_NAME);
        Add("Category", kCategories[g.category], "Left and right change it.", GA_CATEGORY);
        Add("Runs With", kRunners[g.runner],
            g.runner == RUN_SELF ? "Programs, shortcuts and scripts run themselves. Left and right change it."
                                 : "The emulator is found when the game starts. Left and right change it.",
            GA_RUNNER);
        Add("Add to Launcher", g.added.c_str(),
            g.pickedPath.empty() ? "Choose a file first." : "Adds it under the category above.", GA_ADDGAME);
        break;
    }
    case GV_BROWSE:
        if (g.browseStale) { ListDir(g.browseDir, g.entries); g.browseStale = false; }
        Add("Up a Folder", "", g.browseDir.c_str(), GA_BROWSEUP);
        for (size_t i = 0; i < g.entries.size(); i++)
            Add(g.entries[i].name.c_str(), g.entries[i].dir ? "Folder" : "", g.browseDir.c_str(), GA_BROWSE, (int)i);
        break;
    case GV_GAMES: {
        std::vector<int> list;
        FilteredRows(g.gameList, list);
        for (size_t i = 0; i < list.size(); i++) {
            const VirtualGame& vg = g_vgames.games[list[i]];
            int sort = ListSort();
            std::string value = sort == SORT_TIME ? PlayTime(vg.playTime)
                              : sort == SORT_LAST || g.gameList == GL_RECENT ? PlayedOn(vg.lastPlayed)
                              : std::string(vg.category);
            Add(vg.name, value.c_str(), vg.group[0] ? vg.group : vg.category, GA_GAME, list[i]);
        }
        if (g.rows.empty())
            Add(g.gameList == GL_HIDDEN ? "Nothing hidden" : g.gameList == GL_SEARCH ? "No matches" : "Nothing here yet", "", NULL);
        break;
    }
    case GV_LETTERS: {
        // Only letters something starts with, so every row leads somewhere.
        int counts[27] = {};
        std::vector<int> all;
        GameListRows(GL_ALL, all);
        for (size_t i = 0; i < all.size(); i++) {
            char c = Initial(g_vgames.games[all[i]].name);
            counts[c == '#' ? 26 : c - 'A']++;
        }
        for (int k = 0; k < 27; k++) {
            if (!counts[k]) continue;
            char label[2] = { k == 26 ? '#' : (char)('A' + k), 0 };
            char n[16];
            snprintf(n, sizeof(n), "%d", counts[k]);
            Add(label, n, k == 26 ? "Names starting with a number or a symbol." : NULL, GA_LETTER, label[0]);
        }
        if (g.rows.empty()) Add("No games yet", "", "Add some from the Games & Playlists page.");
        break;
    }
    case GV_GAME: {
        VirtualGame* vg = PickedGame();
        if (!vg) { Add("This game moved", "", "Go back and pick it again."); break; }
        Add("Launch", "", vg->launch, GA_LAUNCH);
        Add("Favorite", vg->favorite ? "Yes" : "No", "Starred games get their own spot in the launcher.", GA_FAVORITE);
        Add("Hidden", vg->hidden ? "Yes" : "No", "Hidden games stay out of the launcher until you bring them back.",
            GA_HIDE);
        {
            std::string in = vg->collections;
            for (size_t i = 0; i < in.size(); i++) if (in[i] == ';') in.replace(i, 1, ", ");
            Add("Collections", in.empty() ? "None" : in.c_str(), "Put it in your own lists, or make a new one.",
                GA_GAMECOLL);
        }
        Add("Last Played", PlayedOn(vg->lastPlayed).c_str(), NULL);
        Add("Time Played", PlayTime(vg->playTime).c_str(),
            "Counted when the game closes. Games another launcher starts for us can't be timed.");
        std::string where = vg->group[0] ? std::string(vg->category) + "/" + vg->group : std::string(vg->category);
        Add("Category", where.c_str(), NULL);
        // Rows from someone else's library aren't saved, so neither would these be.
        if (!VGames_IsLive(*vg)) {
            Add("Arguments", vg->args[0] ? vg->args : "None", "Added after the launch line, like -windowed.",
                GA_GAMEOPT, 0);
            Add("Working Folder", vg->workdir[0] ? vg->workdir : "Its own",
                "Where the game starts. Empty starts it in the program's own folder.", GA_GAMEOPT, 1);
            Add("Environment", vg->env[0] ? vg->env : "None",
                "NAME=value, more than one split by ;. Like DXVK_HUD=1;MANGOHUD=1", GA_GAMEOPT, 2);
        }
        break;
    }
    case GV_APPS: {
        Add("Add All Games", g.result[GA_APPGAMES].c_str(), "Everything marked as a game, with its icon.", GA_APPGAMES);
        for (size_t i = 0; i < g.apps.size(); i++) {
            char clean[128];
            Title_SanitizeName(g.apps[i].name.c_str(), clean, sizeof(clean));
            const char* state = VGames_FindByName(clean) >= 0 ? "In Library" : g.apps[i].game ? "Game" : "App";
            Add(g.apps[i].name.c_str(), state, g.apps[i].exec.c_str(), GA_APP, (int)i);
        }
        if (g.apps.empty()) Add("Nothing found", "", "No .desktop entries in the app menu folders or on the Desktop.");
        break;
    }
    case GV_PLAYLISTS:
        for (int i = 0; i < Playlist_Count(); i++) {
            const Playlist* pl = Playlist_Get(i);
            if (!pl) continue;
            char n[32];
            snprintf(n, sizeof(n), "%d", (int)pl->items.size());
            Add(pl->name.c_str(), n, "Edit playlists in the Playlist Maker (F6).", GA_PLAYLIST, i);
        }
        if (g.rows.empty()) Add("No playlists yet", "", "Make one in the Playlist Maker (F6).");
        break;
    case GV_PLAYLIST: {
        const Playlist* pl = Playlist_Get(g.playlist);
        if (pl)
            for (size_t i = 0; i < pl->items.size(); i++)
                Add(pl->items[i].title.empty() ? NameOnly(pl->items[i].path).c_str() : pl->items[i].title.c_str(),
                    "", NULL);
        if (g.rows.empty()) Add("Empty", "", NULL);
        break;
    }
    case GV_ACCOUNTS: {
        std::string help;
        if (Plex_HasToken()) {
            Add("Plex", "Signed in", NULL);
            Add("Sign Out of Plex", "", NULL, GA_PLEXOUT);
        } else if (Plex_PinAuthInFlight()) {
            std::string code = Plex_GetPinCode();
            Add("Plex", code.empty() ? "Asking..." : code.c_str(),
                "On any device go to plex.tv/link and enter the code. A cancels.", GA_PLEX);
        } else {
            Add("Plex", "Sign In", "Gives you a code to enter at plex.tv/link.", GA_PLEX);
        }

        if (Jellyfin_HasToken()) {
            help = "Signed in as " + Jellyfin_GetUserName() + ".";
            Add("Jellyfin", "Signed in", help.c_str());
            Add("Sign Out of Jellyfin", "", NULL, GA_JELLYFINOUT);
        } else {
            std::string url = Jellyfin_GetServerUrl();
            Add("Jellyfin Server", url.empty() ? "Not set" : url.c_str(),
                "Your server's address, like 192.168.1.20:8096.", GA_JELLYFINURL);
            if (!url.empty() && Jellyfin_QuickConnectInFlight()) {
                std::string code = Jellyfin_GetQuickConnectCode();
                Add("Jellyfin", code.empty() ? "Asking..." : code.c_str(),
                    "In Jellyfin's web page open your account, then Quick Connect, and enter the code.", GA_JELLYFIN);
            } else if (!url.empty()) {
                Add("Jellyfin", "Sign In", "Quick Connect gives you a code to approve on the server.", GA_JELLYFIN);
            }
        }

        if (Xcloud_HasToken()) {
            std::string gt = Xcloud_GetGamertag();
            Add("Xbox", gt.empty() ? "Signed in" : gt.c_str(), NULL);
            Add("Sign Out of Xbox", "", NULL, GA_XBOXOUT);
        } else if (Xcloud_LoginInFlight()) {
            std::string code = Xcloud_GetUserCode();
            help = "Go to " + Xcloud_GetVerificationUri() + " and enter the code. A cancels.";
            Add("Xbox", code.empty() ? "Asking..." : code.c_str(), help.c_str(), GA_XBOX);
        } else {
            Add("Xbox", "Sign In", "For cloud gaming and your consoles.", GA_XBOX);
        }
        break;
    }
    case GV_LOGS:
        Add("Copy Version Info", g.copied == GA_COPYVERSION ? "Copied" : "",
            "Version, platform and renderer, ready to paste.", GA_COPYVERSION);
        Add("Copy Log", g.copied == GA_COPYLOG ? "Copied" : "",
            "This run's log. Your home folder shows as ~ and IP addresses are hidden.", GA_COPYLOG);
        Add("Open Logs Folder", "", "theseus.log is this run, theseus.prev.log the one before.", GA_OPENLOGS);
        Add("Back Up My Settings", g.result[GA_BACKUP].c_str(),
            "Copies your games, settings, icons and playlists into Backups in your user data folder.", GA_BACKUP);
        Backup_List(Plat_UserDataDir(), g.backups);
        {
            char n[16];
            snprintf(n, sizeof(n), "%d", (int)g.backups.size());
            Add("Restore a Backup", n, "Puts an earlier backup back. What you have now is backed up first.",
                GA_BACKUPS);
        }
        Add("Report a Problem", "", "Paste the version and the log into an issue on GitHub.");
        break;
    case GV_BACKUPS:
        for (size_t i = 0; i < g.backups.size(); i++)
            Add(g.backups[i].c_str(), "", "Pick it to see what restoring does.", GA_PICKBACKUP, (int)i);
        if (g.rows.empty()) Add("No backups yet", "", "Back Up My Settings makes one.");
        break;
    case GV_RESTORE:
        Add("Restore This Backup", g.result[GA_RESTORE].c_str(),
            "What you have now is backed up first, then UIX Desktop restarts with this one.", GA_RESTORE);
        Add("Open Backups Folder", "", "Each backup is a plain folder you can look through.", GA_OPENBACKUPS);
        break;
    case GV_SHORTCUTS:
        Add(GuideCombo(), "Guide", "Opens and closes this guide.");
        Add("F3",  "Title Maker", NULL);
        Add("F4",  "Settings Window", NULL);
        Add("F5",  "HDD Browser", NULL);
        Add("F6",  "Playlist Maker", NULL);
        Add("F11", "Fullscreen", NULL);
        Add("F12", "Borderless Window", NULL);
        Add("Ctrl+M", "Mute", NULL);
        Add("Ctrl+R", "Restart Dashboard", NULL);
        Add("Ctrl+Q", "Quit", NULL);
        if (g_startupMode == 2 || g_extractedMode) {
            Add("F1",  "Inspector", "Development mode.");
            Add("F2",  "XAP Editor", "Development mode.");
            Add("F10", "Hide Menu Bar", "Development mode.");
        }
        break;
    case GV_TOOLS:
        Add("Title Maker", "F3", "Names, icons and categories for the launcher.", GA_WINDOW, WIN_TITLEMAKER);
        Add("Settings Window", "F4", "Every setting, in a desktop window.", GA_WINDOW, WIN_SETTINGS);
        Add("HDD Browser", "F5", "Look through the dashboard's drives.", GA_WINDOW, WIN_HDD);
        Add("Playlist Maker", "F6", "Make and edit playlists.", GA_WINDOW, WIN_PLAYLISTS);
        if (g_padModeEnabled)
            Add("Use the Tools with the Pad", "", "The tool windows' own controller menu.", GA_PADTOOLS);
        if (g_startupMode == 2 || g_extractedMode) {
            Add("Inspector", "F1", "Development mode.", GA_WINDOW, WIN_INSPECTOR);
            Add("XAP Editor", "F2", "Development mode.", GA_WINDOW, WIN_XAPEDITOR);
        }
        break;
    case GV_EXIT:
        Add("Back to the Dashboard", "", "Closes the guide.", GA_RESUME);
        Add("Exit UIX Desktop", "", "Quit to your desktop.", GA_QUIT);
        break;
    }
    if (g.cursor >= (int)g.rows.size()) g.cursor = g.rows.empty() ? 0 : (int)g.rows.size() - 1;
    if (g.cursor < 0) g.cursor = 0;
}

static const char* ViewTitle()
{
    switch (g.view) {
    case GV_ADDGAME:   return "Add a Game";
    case GV_BROWSE:    return "Choose a File";
    case GV_PLAYLISTS: return "Playlists";
    case GV_LETTERS:   return "Find a Game";
    case GV_APPS:      return "Linux Apps";
    case GV_COLLECTIONS: return "Collections";
    case GV_BACKUPS:   return "Restore a Backup";
    case GV_RESTORE:   return g.backup.c_str();
    case GV_GAMECOLL:  return "Collections";
    case GV_GAMES: {
        static char title[32], full[96];
        if (g.gameList == GL_SEARCH) snprintf(title, sizeof(title), "Search: %.20s", g.search.c_str());
        else if (g.gameList == GL_LETTER) snprintf(title, sizeof(title), "Games: %c", g.letter);
        else if (g.gameList == GL_COLLECTION) snprintf(title, sizeof(title), "%.30s", g.collection.c_str());
        else snprintf(title, sizeof(title), "%s", kGameListNames[g.gameList]);
        snprintf(full, sizeof(full), "%s", title);
        if (!g.filter.empty()) snprintf(full + strlen(full), sizeof(full) - strlen(full), " (%.40s)", g.filter.c_str());
        if (g.gameList == GL_ALL && g.sort)
            snprintf(full + strlen(full), sizeof(full) - strlen(full), " by %s", kSorts[g.sort]);
        return full;
    }
    case GV_GAME: {
        VirtualGame* vg = PickedGame();
        return vg ? vg->name : "Game";
    }
    case GV_PLAYLIST: {
        const Playlist* pl = Playlist_Get(g.playlist);
        return pl ? pl->name.c_str() : "Playlist";
    }
    case GV_WELCOME:   return g_welcomeDone ? "Start" : "Welcome";
    default:           return kSections[g.section];
    }
}

// Opening and closing

static void ShowSection(int section)
{
    g.section = section;
    g.view = kSectionViews[section];
    g.stack.clear();
    g.cursor = g.scroll = 0;
    Build();
}

static void Open(bool welcome)
{
    g.open = true;
    g.welcome = welcome;
    g.inPanel = welcome;
    g.copied = GA_NONE;
    // What's held as it opens (the B of LT+B) isn't a press.
    g.held = Joy_GuideButtons(&g.device);
    g.repeatHeld = 0;
    g_padModeActive = false;
    if (welcome) {
        g.view = GV_WELCOME;
        g.stack.clear();
        g.cursor = g.scroll = 0;
        Build();
    } else {
        ShowSection(g.section);
    }
}

static void Close()
{
    if (g.welcome && !g_welcomeDone) {
        g_welcomeDone = true;
        SaveDesktopSettings();
    }
    g.open = false;
    g.welcome = false;
    g.edit.on = false;
    g.swallow = true;
}

bool Guide_IsOpen() { return g.open; }

bool Guide_TakesInput() { return g.open || g.swallow; }

// Acting

static void Push(View v)
{
    Guide::Frame f = { g.view, g.cursor, g.scroll };
    g.stack.push_back(f);
    g.view = v;
    g.cursor = g.scroll = 0;
    Build();
}

static bool Back()
{
    if (g.stack.empty()) return false;
    Guide::Frame f = g.stack.back();
    g.stack.pop_back();
    g.view = f.view;
    g.cursor = f.cursor;
    g.scroll = f.scroll;
    Build();
    return true;
}

static void OpenWindow(int which)
{
    switch (which) {
    case WIN_TITLEMAKER: g_titleMakerOpen = true; break;
    case WIN_HDD:        g_hddBrowserOpen = true; break;
    case WIN_PLAYLISTS: {
        extern bool g_playlistMakerOpen;
        g_playlistMakerOpen = true;
        break;
    }
    case WIN_SETTINGS: {
        extern bool g_settingsOpen;
        g_settingsOpen = true;
        break;
    }
    case WIN_INSPECTOR: g_inspectorOpen = true; break;
    case WIN_XAPEDITOR: g_xapEditorOpen = true; break;
    }
}

// Typing

static size_t PrevChar(const std::string& t, size_t i)
{
    if (i == 0) return 0;
    i--;
    while (i > 0 && ((unsigned char)t[i] & 0xC0) == 0x80) i--;
    return i;
}

static size_t NextChar(const std::string& t, size_t i)
{
    if (i >= t.size()) return t.size();
    i++;
    while (i < t.size() && ((unsigned char)t[i] & 0xC0) == 0x80) i++;
    return i;
}

bool Guide_Typing() { return g.open && g.edit.on; }

static void EditOpen(int what, int arg, const char* title, const std::string& text, size_t limit, bool secret)
{
    Editor& e = g.edit;
    e.on = true;
    e.what = what;
    e.arg = arg;
    e.title = title;
    e.text = text.size() > limit ? text.substr(0, limit) : text;
    e.caret = e.text.size();
    e.limit = limit;
    e.secret = secret;
    e.shift = e.symbols = false;
    e.key = 10;
    e.since = SDL_GetTicks();
    SDL_StartTextInput();
}

static void EditClose()
{
    g.edit.on = false;
    // Whatever closed it is still held; it isn't a press on the row.
    g.settle = true;
    Build();
}

// Trimmed, and a path copied from a file manager loses its quotes.
static std::string Tidy(std::string s, bool path)
{
    size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
    s = a == std::string::npos ? "" : s.substr(a, b - a + 1);
    if (path && s.size() >= 2 && s[0] == '"' && s[s.size() - 1] == '"') s = s.substr(1, s.size() - 2);
    return s;
}

static void EditDone()
{
    const Editor& e = g.edit;
    switch (e.what) {
    case ED_SETTING: {
        const SettingDef& d = DesktopSettings_Table().defs[e.arg];
        if (Setting_SetText(d, Tidy(e.text, d.kind == SK_PATH).c_str())) {
            if (d.apply) d.apply();
            SaveDesktopSettings();
        }
        break;
    }
    case ED_NAME: {
        char clean[128];
        Title_SanitizeName(Tidy(e.text, false).c_str(), clean, sizeof(clean));
        if (clean[0]) { g.pickedName = clean; g.added.clear(); }
        break;
    }
    case ED_JELLYFIN:
        Jellyfin_SetServerUrl(Tidy(e.text, false));
        break;
    case ED_SEARCH:
        g.search = Tidy(e.text, false);
        break;
    case ED_COLLECTION: {
        std::string name = VGames_CleanCollectionName(Tidy(e.text, false).c_str());
        VirtualGame* vg = PickedGame();
        if (name.empty() || !vg) break;
        // Typing an existing name just joins it, spelled the way it already is.
        std::vector<std::string> colls;
        VGames_Collections(colls, false);
        for (size_t i = 0; i < colls.size(); i++)
            if (strcasecmp(colls[i].c_str(), name.c_str()) == 0) name = colls[i];
        if (VGames_SetInCollection(g.game, name.c_str(), true)) {
            g.collNames.push_back(name);
            g.result[GA_NEWCOLL] = "Added to " + name;
        } else {
            g.result[GA_NEWCOLL] = "In too many already";
        }
        break;
    }
    case ED_GAMEOPT: {
        VirtualGame* vg = PickedGame();
        if (!vg) break;
        char* field = e.arg == 0 ? vg->args : e.arg == 1 ? vg->workdir : vg->env;
        snprintf(field, sizeof(vg->args), "%s", Tidy(e.text, e.arg == 1).c_str());
        VGames_Save();
        break;
    }
    }
    bool search = e.what == ED_SEARCH && !g.search.empty();
    EditClose();
    if (search) {
        g.gameList = GL_SEARCH;
        g.filter.clear();
        Push(GV_GAMES);
    }
}

static void EditInsert(const char* s)
{
    Editor& e = g.edit;
    std::string add;
    for (const char* p = s; *p; p++)
        if ((unsigned char)*p >= 0x20 && *p != 0x7F) add += *p;
    if (add.empty() || e.text.size() + add.size() > e.limit) return;
    e.text.insert(e.caret, add);
    e.caret += add.size();
}

static void EditDelete(bool forward)
{
    Editor& e = g.edit;
    size_t from = forward ? e.caret : PrevChar(e.text, e.caret);
    size_t to = forward ? NextChar(e.text, e.caret) : e.caret;
    e.text.erase(from, to - from);
    e.caret = from;
}

static void EditPaste()
{
    char* clip = SDL_GetClipboardText();
    if (!clip) return;
    std::string line = clip;
    SDL_free(clip);
    EditInsert(line.substr(0, line.find_first_of("\r\n")).c_str());
}

static int KeyLayout() { return g.edit.symbols ? 2 : g.edit.shift ? 1 : 0; }

// A key's row, left edge and width, in keys.
static void KeySpan(int key, int* row, float* x, float* w)
{
    if (key < KEY_CHARS) { *row = key / 10; *x = (float)(key % 10); *w = 1.0f; return; }
    *row = 4;
    *x = kWideKeys[key - KEY_CHARS][0];
    *w = kWideKeys[key - KEY_CHARS][1];
}

static void PressKey(int key)
{
    Editor& e = g.edit;
    if (key < KEY_CHARS) {
        char c[2] = { kKeyRows[KeyLayout()][key / 10][key % 10], 0 };
        EditInsert(c);
        e.shift = false;   // for one letter
        return;
    }
    switch (key) {
    case K_SHIFT:   e.shift = !e.shift; e.symbols = false; break;
    case K_SYMBOLS: e.symbols = !e.symbols; e.shift = false; break;
    case K_SPACE:   EditInsert(" "); break;
    case K_DELETE:  EditDelete(false); break;
    case K_PASTE:   EditPaste(); break;
    case K_DONE:    EditDone(); break;
    }
}

// Sideways wraps in the row; up and down land on the key under this one's middle.
static void MoveKey(int dx, int dy)
{
    Editor& e = g.edit;
    int row;
    float x, w;
    KeySpan(e.key, &row, &x, &w);
    if (dx) {
        if (row < 4) e.key = row * 10 + (e.key % 10 + dx + 10) % 10;
        else e.key = KEY_CHARS + (e.key - KEY_CHARS + dx + KEY_COUNT - KEY_CHARS) % (KEY_COUNT - KEY_CHARS);
        return;
    }
    float mid = x + w * 0.5f;
    row = (row + dy + 5) % 5;
    if (row < 4) { e.key = row * 10 + std::min(9, (int)mid); return; }
    e.key = K_DONE;
    for (int k = K_SHIFT; k < KEY_COUNT; k++)
        if (mid >= kWideKeys[k - KEY_CHARS][0] && mid < kWideKeys[k - KEY_CHARS][0] + kWideKeys[k - KEY_CHARS][1]) {
            e.key = k;
            break;
        }
}

// The pad on the keyboard: A types the key, X deletes, Y is a space, the
// bumpers move the cursor, Start saves and B cancels.
static void EditHandle(unsigned pressed)
{
    Editor& e = g.edit;
    if (pressed & GUIDE_B)     { EditClose(); return; }
    if (pressed & GUIDE_START) { EditDone(); return; }
    if (pressed & GUIDE_UP)    MoveKey(0, -1);
    if (pressed & GUIDE_DOWN)  MoveKey(0, 1);
    if (pressed & GUIDE_LEFT)  MoveKey(-1, 0);
    if (pressed & GUIDE_RIGHT) MoveKey(1, 0);
    if (pressed & GUIDE_LB)    e.caret = PrevChar(e.text, e.caret);
    if (pressed & GUIDE_RB)    e.caret = NextChar(e.text, e.caret);
    if (pressed & GUIDE_X)     EditDelete(false);
    if (pressed & GUIDE_Y)     EditInsert(" ");
    if (pressed & GUIDE_A)     PressKey(e.key);
}

// A physical keyboard types straight in.
static void EditEvent(const SDL_Event* ev)
{
    Editor& e = g.edit;
    // Keys from before it opened belong to whatever opened it.
    if (ev->common.timestamp < e.since) return;
    g.device = 0;
    if (ev->type == SDL_TEXTINPUT) { EditInsert(ev->text.text); return; }
    const bool repeat = ev->key.repeat != 0;
    switch (ev->key.keysym.sym) {
    case SDLK_RETURN:
    case SDLK_KP_ENTER:  if (!repeat) EditDone(); break;
    case SDLK_ESCAPE:    if (!repeat) EditClose(); break;
    case SDLK_BACKSPACE: EditDelete(false); break;
    case SDLK_DELETE:    EditDelete(true); break;
    case SDLK_LEFT:      e.caret = PrevChar(e.text, e.caret); break;
    case SDLK_RIGHT:     e.caret = NextChar(e.text, e.caret); break;
    case SDLK_HOME:      e.caret = 0; break;
    case SDLK_END:       e.caret = e.text.size(); break;
    case SDLK_v:
        if (ev->key.keysym.mod & (KMOD_CTRL | KMOD_GUI)) EditPaste();
        break;
    }
}

static bool Adjust(int dir)
{
    if (g.cursor < 0 || g.cursor >= (int)g.rows.size()) return false;
    const Row& r = g.rows[g.cursor];
    if (r.act == GA_CATEGORY) {
        g.category = (g.category + (dir < 0 ? kCategoryCount - 1 : 1)) % kCategoryCount;
        Build();
        return true;
    }
    if (r.act == GA_RUNNER) {
        g.runner = (g.runner + (dir < 0 ? RUN_COUNT - 1 : 1)) % RUN_COUNT;
        Build();
        return true;
    }
    if (r.act != GA_SETTING) return false;
    const SettingDef& d = DesktopSettings_Table().defs[r.arg];
    if (!Setting_Cycle(d, dir < 0 ? -1 : 1)) return false;
    if (d.apply) d.apply();
    SaveDesktopSettings();
    Build();
    // The button still held can mean something else now (SwapConfirm).
    g.settle = true;
    return true;
}

// The pad buzzes when A can't do anything; the keyboard just doesn't.
static void Refuse()
{
    if (g.device == 1) Pads_Rumble(0.35f, 90);
}

static void Select()
{
    if (g.cursor < 0 || g.cursor >= (int)g.rows.size()) return;
    const Row r = g.rows[g.cursor];
    switch (r.act) {
    case GA_NONE:
        Refuse();
        break;
    case GA_VIEW:
        Push((View)r.arg);
        break;
    case GA_SETTING:
    case GA_CATEGORY:
    case GA_RUNNER:
        Adjust(1);
        break;
    case GA_TEXT: {
        const SettingDef& d = DesktopSettings_Table().defs[r.arg];
        EditOpen(ED_SETTING, r.arg, d.label, (const char*)d.ptr, (size_t)d.size - 1, (d.flags & SF_SECRET) != 0);
        break;
    }
    case GA_FINDEMU: {
        char path[512];
        const SettingDef* d = DesktopSettings_Find(kEmus[r.arg].key);
        if (d && kEmus[r.arg].find(path, sizeof(path)) && Setting_SetText(*d, path)) {
            SaveDesktopSettings();
            g.found[r.arg] = "Found";
        } else {
            g.found[r.arg] = "Not found";
        }
        Build();
        break;
    }
    case GA_GAMEOPT: {
        VirtualGame* vg = PickedGame();
        if (!vg) break;
        const char* text = r.arg == 0 ? vg->args : r.arg == 1 ? vg->workdir : vg->env;
        EditOpen(ED_GAMEOPT, r.arg, r.label.c_str(), text, sizeof(vg->args) - 1, false);
        break;
    }
    case GA_NAME:
        EditOpen(ED_NAME, 0, "Name", g.pickedName, 100, false);
        break;
    case GA_JELLYFINURL:
        EditOpen(ED_JELLYFIN, 0, "Jellyfin Server", Jellyfin_GetServerUrl(), 500, false);
        break;
    case GA_SEARCH:
        EditOpen(ED_SEARCH, 0, "Search by Name", g.search, 60, false);
        break;
    case GA_NEWCOLL:
        EditOpen(ED_COLLECTION, 0, "New Collection", "", VGAMES_COLLECTION_NAME_MAX, false);
        break;
    case GA_PADTOOLS:
        Close();
        g_padModeActive = true;
        break;
    case GA_WINDOW:
        Close();
        OpenWindow(r.arg);
        break;
    case GA_RESUME:
        Close();
        break;
    case GA_DEVMODE:
        g_welcomeDone = true;
        SaveDesktopSettings();
        if (Preloader_EnsureExtracted()) RestartWithMode("--development");
        break;
    case GA_FOLDERS:
        SetUpFolders();
        g.foldersDone = true;
        Build();
        break;
    case GA_OPENFOLDERS:
        Plat_OpenInFileManager(AppPath_Tree("Library"));
        break;
    case GA_STEAM:
    case GA_RETROARCH: {
        int skipped = 0;
        int added = r.act == GA_STEAM ? TitleMaker_ImportSteam(&skipped) : TitleMaker_ImportRetroArch(&skipped);
        char msg[64];
        if (added < 0) { snprintf(msg, sizeof(msg), "Not found"); Refuse(); }
        else snprintf(msg, sizeof(msg), "%d new, %d already in", added, skipped);
        g.result[r.act] = msg;
        Build();
        break;
    }
    case GA_STORE: {
        if (r.arg < 0 || r.arg >= kStoreCount) break;
        std::vector<StoreGame> list;
        kStores[r.arg].scan(list);
        int added = 0, had = 0;
        StoreGames_Add(list, &added, &had);
        char msg[64];
        snprintf(msg, sizeof(msg), "%d new, %d already in", added, had);
        g.storeResult[r.arg] = msg;
        Build();
        break;
    }
    case GA_ESDE:
        if (!s_esdePath[0]) {
            g.result[GA_ESDE] = "Set its folder first";
            Refuse();
        } else {
            EsdeVGames_Sync(s_esdePath);
            g.result[GA_ESDE] = "Refreshed";
        }
        Build();
        break;
    case GA_CHOOSEFILE:
        if (g.browseDir.empty()) {
            const char* h = HomeDir();
            g.browseDir = h ? h : "/";
        }
        g.browseStale = true;
        Push(GV_BROWSE);
        break;
    case GA_BROWSEUP:
        g.browseDir = ParentDir(g.browseDir);
        g.browseStale = true;
        g.cursor = g.scroll = 0;
        Build();
        break;
    case GA_BROWSE: {
        if (r.arg < 0 || r.arg >= (int)g.entries.size()) break;
        const BrowseEntry e = g.entries[r.arg];
        std::string full = g.browseDir;
        if (full.empty() || (full[full.size() - 1] != '/' && full[full.size() - 1] != '\\')) full += Host_PathSep;
        full += e.name;
        if (e.dir) {
            g.browseDir = full;
            g.browseStale = true;
            g.cursor = g.scroll = 0;
            Build();
            break;
        }
        g.pickedPath = full;
        g.pickedName = NameFromPath(full);
        g.runner = RUN_SELF;
        XdgApp entry;
        if (e.name.size() > 8 && strcasecmp(e.name.c_str() + e.name.size() - 8, ".desktop") == 0 &&
            XdgApps_Parse(full.c_str(), &entry)) {
            char clean[128];
            Title_SanitizeName(entry.name.c_str(), clean, sizeof(clean));
            if (clean[0]) g.pickedName = clean;
        }
        if (Pcsx2_IsDisc(full.c_str())) {
            // An Xbox disc names itself; anything else is a PS2 guess.
            XisoTitleInfo xiso = XisoGetTitleInfo(full.c_str());
            g.runner = xiso.valid ? RUN_XEMU : RUN_PCSX2;
            char clean[128] = "";
            if (xiso.valid) Title_SanitizeName(xiso.titleName, clean, sizeof(clean));
            if (clean[0]) g.pickedName = clean;
            XisoFreeTitleInfo(&xiso);
        }
        g.added.clear();
        Back();
        break;
    }
    case GA_APPS: {
        std::vector<std::string> dirs;
        XdgApps_Dirs(dirs);
        XdgApps_Scan(dirs, g.apps);
        g.result[GA_APPGAMES].clear();
        Push(GV_APPS);
        break;
    }
    case GA_APP:
    case GA_APPGAMES: {
        int added = 0, had = 0;
        for (size_t i = 0; i < g.apps.size(); i++) {
            const XdgApp& app = g.apps[i];
            if (r.act == GA_APP ? (int)i != r.arg : !app.game) continue;
            if (XdgApps_Add(app, app.game ? "Games" : "Applications")) added++; else had++;
        }
        if (added) { VGames_Save(); UDataSynth_RebuildAll(); }
        if (r.act == GA_APPGAMES) {
            char msg[64];
            snprintf(msg, sizeof(msg), "%d new, %d already in", added, had);
            g.result[GA_APPGAMES] = msg;
        }
        Build();
        break;
    }
    case GA_ADDGAME: {
        if (g.pickedPath.empty() || g.pickedName.empty()) { Refuse(); break; }
        if (VGames_FindByName(g.pickedName.c_str()) >= 0) { g.added = "Already there"; Refuse(); Build(); break; }
        // A .desktop shortcut runs its Exec line, not the file.
        XdgApp entry;
        if (g.runner == RUN_SELF && XdgApps_Parse(g.pickedPath.c_str(), &entry) &&
            g.pickedPath.size() > 8 && strcasecmp(g.pickedPath.c_str() + g.pickedPath.size() - 8, ".desktop") == 0) {
            entry.name = g.pickedName;
            g.added = XdgApps_Add(entry, kCategories[g.category]) ? "Added" : "Already there";
            if (g.added == "Added") {
                VGames_Save();
                UDataSynth_RebuildAll();
                g.pickedPath.clear();
                g.pickedName.clear();
            }
            Build();
            break;
        }
        char genID[16];
        snprintf(genID, sizeof(genID), "%08x", (unsigned)time(NULL));
        char launch[sizeof(VirtualGame::launch)];
        bool fits = true;
        XisoTitleInfo xiso = {};
        if (g.runner == RUN_XEMU) {
            fits = Xemu_IsoLaunch(g.pickedPath.c_str(), launch, sizeof(launch));
            xiso = XisoGetTitleInfo(g.pickedPath.c_str());
            if (xiso.valid) snprintf(genID, sizeof(genID), "%08x", xiso.titleId);
        } else if (g.runner == RUN_PCSX2) {
            fits = Pcsx2_DiscLaunch(g.pickedPath.c_str(), launch, sizeof(launch));
        } else {
            int n = snprintf(launch, sizeof(launch), "\"%s\"", g.pickedPath.c_str());
            fits = n > 0 && n < (int)sizeof(launch);
        }
        if (!fits) { XisoFreeTitleInfo(&xiso); g.added = "Path too long"; Build(); break; }
        VGames_Add(g.pickedName.c_str(), genID, launch, "E", kCategories[g.category]);
        char icon[1024];
        snprintf(icon, sizeof(icon), "%s/%s.jpg", VGAMES_ICONS, genID);
        if (xiso.titleImageRGBA) {
            Host_MakeDir(VGAMES_ICONS);
            XisoSaveTitleImage(&xiso, icon);
        } else if (g.runner == RUN_SELF && ExeIcon_IsIconSource(g.pickedPath.c_str())) {
            Host_MakeDir(VGAMES_ICONS);
            ExeIcon_Save(g.pickedPath.c_str(), icon);
        }
        XisoFreeTitleInfo(&xiso);
        VGames_Save();
        UDataSynth_RebuildAll();
        g.added = "Added";
        g.pickedPath.clear();
        g.pickedName.clear();
        Build();
        break;
    }
    // Sign in starts a code flow, A again cancels it.
    case GA_PLEX:
        if (Plex_PinAuthInFlight()) Plex_CancelPinAuth(); else Plex_StartPinAuth();
        Build();
        break;
    case GA_JELLYFIN:
        if (Jellyfin_QuickConnectInFlight()) Jellyfin_CancelQuickConnect(); else Jellyfin_StartQuickConnect();
        Build();
        break;
    case GA_XBOX:
        if (Xcloud_LoginInFlight()) Xcloud_CancelLogin(); else Xcloud_StartLogin();
        Build();
        break;
    case GA_PLEXOUT:     Plex_SignOut();     SaveDesktopSettings(); Build(); break;
    case GA_JELLYFINOUT: Jellyfin_SignOut(); SaveDesktopSettings(); Build(); break;
    case GA_XBOXOUT:     Xcloud_SignOut();   SaveDesktopSettings(); Build(); break;
    case GA_PLAYLIST:
        g.playlist = r.arg;
        Push(GV_PLAYLIST);
        break;
    case GA_GAMELIST:
        g.gameList = r.arg;
        g.filter.clear();
        Push(GV_GAMES);
        break;
    case GA_COLLECTION: {
        std::vector<std::string> colls;
        VGames_Collections(colls, false);
        if (r.arg < 0 || r.arg >= (int)colls.size()) { Refuse(); break; }
        g.collection = colls[r.arg];
        g.gameList = GL_COLLECTION;
        g.filter.clear();
        Push(GV_GAMES);
        break;
    }
    case GA_GAMECOLL:
        g.collNames.clear();
        g.result[GA_NEWCOLL].clear();
        Push(GV_GAMECOLL);
        break;
    case GA_INCOLL: {
        VirtualGame* vg = PickedGame();
        if (!vg || r.arg < 0 || r.arg >= (int)g.collNames.size()) { Refuse(); break; }
        const char* name = g.collNames[r.arg].c_str();
        if (!VGames_SetInCollection(g.game, name, !VGames_InCollection(*vg, name))) {
            g.result[GA_NEWCOLL] = "In too many already";
            Refuse();
        }
        Build();
        break;
    }
    case GA_LETTER:
        g.gameList = GL_LETTER;
        g.letter = (char)r.arg;
        g.filter.clear();
        Push(GV_GAMES);
        break;
    case GA_GAME:
        g.game = r.arg;
        g.gameLayout = g_vgames.layout;
        Push(GV_GAME);
        break;
    case GA_FAVORITE:
    case GA_HIDE: {
        VirtualGame* vg = PickedGame();
        if (!vg) { Refuse(); break; }
        if (r.act == GA_FAVORITE) VGames_SetFavorite(g.game, !vg->favorite);
        else                      VGames_SetHidden(g.game, !vg->hidden);
        Build();
        break;
    }
    case GA_LAUNCH: {
        VirtualGame* vg = PickedGame();
        if (!vg) { Refuse(); break; }
        // The same Xbox-style path the launcher hands over.
        char devicePath[512];
        snprintf(devicePath, sizeof(devicePath), "\\Device\\Harddisk0\\Partition1\\%s\\%s",
                 vg->category[0] ? vg->category : "Games", vg->name);
        Close();
        extern void DesktopLaunchTitle(const char* devicePath);
        DesktopLaunchTitle(devicePath);
        break;
    }
    case GA_OPENLOGS:
        Host_OpenFolder(Plat_UserDataDir());
        break;
    case GA_BACKUP: {
        int files = 0;
        std::string name = Backup_Make(Plat_UserDataDir(), NULL, &files);
        char msg[64];
        if (name.empty()) { snprintf(msg, sizeof(msg), "Couldn't save"); Refuse(); }
        else snprintf(msg, sizeof(msg), "Saved %d files", files);
        g.result[GA_BACKUP] = msg;
        Build();
        break;
    }
    case GA_BACKUPS:
        Push(GV_BACKUPS);
        break;
    case GA_PICKBACKUP:
        if (r.arg < 0 || r.arg >= (int)g.backups.size()) { Refuse(); break; }
        g.backup = g.backups[r.arg];
        g.result[GA_RESTORE].clear();
        Push(GV_RESTORE);
        break;
    case GA_RESTORE: {
        std::string saved;
        if (!Backup_Restore(Plat_UserDataDir(), g.backup, &saved)) {
            g.result[GA_RESTORE] = saved.empty() ? "Couldn't back up first" : "Didn't finish";
            Refuse();
            Build();
            break;
        }
        fprintf(stderr, "[Guide] restored backup %s, the one before is %s\n", g.backup.c_str(), saved.c_str());
        // Straight over, before anything in memory gets saved on top of it.
        Restart();
        break;
    }
    case GA_OPENBACKUPS: {
        std::string dir = std::string(Plat_UserDataDir()) + "/" BACKUP_DIR;
        Host_OpenFolder(dir.c_str());
        break;
    }
    case GA_COPYVERSION:
        if (SDL_SetClipboardText(VersionLine().c_str()) == 0) g.copied = GA_COPYVERSION; else Refuse();
        Build();
        break;
    case GA_COPYLOG:
        if (CopyLog()) g.copied = GA_COPYLOG; else Refuse();
        Build();
        break;
    case GA_QUIT: {
        SDL_Event quit;
        quit.type = SDL_QUIT;
        SDL_PushEvent(&quit);
        break;
    }
    }
}

static void MoveCursor(int step)
{
    int n = (int)g.rows.size();
    if (n == 0) return;
    g.cursor += step;
    if (g.cursor < 0) g.cursor = 0;
    if (g.cursor >= n) g.cursor = n - 1;
}

// In a long game list LB and RB jump to the previous or next letter.
static void LetterStep(int step)
{
    int n = (int)g.rows.size();
    if (n == 0 || g.cursor < 0 || g.cursor >= n || g.rows[g.cursor].act != GA_GAME) return;
    char cur = Initial(g.rows[g.cursor].label.c_str());
    int i = g.cursor;
    if (step > 0) {
        while (i < n && Initial(g.rows[i].label.c_str()) == cur) i++;
        if (i >= n) return;
    } else {
        while (i > 0 && Initial(g.rows[i].label.c_str()) == cur) i--;
        char prev = Initial(g.rows[i].label.c_str());
        if (prev == cur) return;
        while (i > 0 && Initial(g.rows[i - 1].label.c_str()) == prev) i--;
    }
    g.cursor = i;
    g.scroll = i;
}

static void PageStep(int step)
{
    if (g.view == GV_GAMES && g.gameList == GL_ALL && ListSort() == SORT_NAME) { LetterStep(step); return; }
    if (g.view != GV_PAGE) return;
    g.page = (g.page + step + kPageCount) % kPageCount;
    g.cursor = g.scroll = 0;
    Build();
}

static void PressB()
{
    if (g.welcome) { Close(); return; }
    if (g.inPanel) {
        if (!Back()) g.inPanel = false;
        return;
    }
    Close();
}

static void Handle(unsigned pressed)
{
    if (g.edit.on) { EditHandle(pressed); return; }
    if (pressed & GUIDE_B) { PressB(); return; }
    if (pressed & GUIDE_LB) PageStep(-1);
    if (pressed & GUIDE_RB) PageStep(1);
    if (!g.inPanel) {
        if (pressed & GUIDE_UP)   ShowSection((g.section + SEC_COUNT - 1) % SEC_COUNT);
        if (pressed & GUIDE_DOWN) ShowSection((g.section + 1) % SEC_COUNT);
        if (pressed & (GUIDE_A | GUIDE_RIGHT)) g.inPanel = true;
        return;
    }
    if (pressed & GUIDE_UP)   MoveCursor(-1);
    if (pressed & GUIDE_DOWN) MoveCursor(1);
    if ((pressed & GUIDE_X) && g.view == GV_GAMES) { CycleFilter(); Build(); }
    if ((pressed & GUIDE_Y) && g.view == GV_GAMES && g.gameList == GL_ALL) {
        g.sort = (g.sort + 1) % SORT_COUNT;
        g.cursor = g.scroll = 0;
        Build();
    }
    // Left on a row that doesn't change goes back to the sections.
    if ((pressed & GUIDE_LEFT) && !Adjust(-1) && !g.welcome && g.stack.empty()) g.inPanel = false;
    if (pressed & GUIDE_RIGHT) Adjust(1);
    if (pressed & GUIDE_A) Select();
}

// New presses, with held directions repeating.
static unsigned Presses(unsigned held)
{
    unsigned pressed = held & ~g.held;
    // Directions repeat, and on the keyboard deleting and the cursor too.
    unsigned repeats = GUIDE_UP | GUIDE_DOWN | GUIDE_LEFT | GUIDE_RIGHT;
    if (g.edit.on) repeats |= GUIDE_X | GUIDE_LB | GUIDE_RB;
    unsigned dirs = held & repeats;
    Uint32 now = SDL_GetTicks();
    if (dirs && dirs == g.repeatHeld) {
        if (now - g.repeatTime >= REPEAT_DELAY) {
            pressed |= dirs;
            g.repeatTime = now - REPEAT_DELAY + REPEAT_INTERVAL;
        }
    } else {
        g.repeatHeld = dirs;
        g.repeatTime = now;
    }
    g.held = held;
    return pressed;
}

void Guide_Frame()
{
    unsigned held = Joy_GuideButtons(&g.device);

    if (g.swallow && !held) g.swallow = false;
    if (g.settle) { g.held = held; g.settle = false; }

    // First run, or Startup is "Ask": once the dashboard is up, the
    // welcome offers the ways in. Neither waits for the scene to settle.
    if (!g.startChecked && g_pActiveLevel) {
        g.startChecked = true;
        if (!g_welcomeDone || g_startupMode == 0) {
            Open(true);
            return;
        }
    }

    if (g_guideRequested) {
        g_guideRequested = false;
        // Fullscreen video and a text field keep the buttons for themselves.
        if (g.open) Close();
        else if (!g_mediaFullscreen && !g_xcloudFullscreen && !g_pActiveKeyboard_void()) Open(false);
        g.held = held;
        return;
    }

    if (!g.open) return;
    // Sign-in codes arrive in the background; the rows keep up with them.
    if (g.view == GV_ACCOUNTS) Build();
    unsigned pressed = Presses(held);
    if (pressed) Handle(pressed);
}

// The pointer

// Where a click left the guide, to tell a double click that would land on
// the next screen's rows from quick clicks on the same row.
static std::string Screen()
{
    char s[64];
    snprintf(s, sizeof(s), "%d/%d/%d/%d/%d/", (int)g.view, (int)g.stack.size(), g.section, g.page, g.edit.on);
    return s + g.browseDir;
}

static void Click(const HitTarget& t)
{
    switch (t.kind) {
    case T_SECTION:
        ShowSection(t.index);
        g.inPanel = false;
        break;
    case T_ROW:
        g.inPanel = true;
        g.cursor = t.index;
        Select();
        break;
    case T_TAB:
        g.page = t.index;
        g.cursor = g.scroll = 0;
        Build();
        break;
    case T_PROMPT:
        Handle((unsigned)t.index);
        break;
    case T_KEY:
        g.edit.key = t.index;
        PressKey(t.index);
        break;
    }
}

void Guide_HandleEvent(const SDL_Event* e)
{
    if (!g.open) return;
    extern SDL_Window* g_pSDLWindow;
    if (!g_pSDLWindow) return;
    Uint32 win = SDL_GetWindowID(g_pSDLWindow);
    // Events come in window points, the overlay lays out in pixels.
    float scale = Plat_GetDisplayScale();
    float x, y;
    // A tool window over the guide keeps its own clicks.
    const bool imgui = ImGui::GetIO().WantCaptureMouse;

    if (g.edit.on && (e->type == SDL_TEXTINPUT || e->type == SDL_KEYDOWN)) {
        if ((e->type == SDL_TEXTINPUT ? e->text.windowID : e->key.windowID) == win) EditEvent(e);
        return;
    }

    switch (e->type) {
    case SDL_MOUSEMOTION:
        if (e->motion.windowID != win || imgui) return;
        ui_overlay_to_layout((float)e->motion.x * scale, (float)e->motion.y * scale, &x, &y);
        g.pointerX = x;
        g.pointerY = y;
        g.pointerMoved = true;
        g.device = 0;
        break;
    case SDL_MOUSEWHEEL: {
        if (e->wheel.windowID != win || imgui) return;
        // Whole notches, gathered from a trackpad's fractions too.
#if SDL_VERSION_ATLEAST(2, 0, 18)
        g.wheel += e->wheel.preciseY;
#else
        g.wheel += (float)e->wheel.y;
#endif
        int step = (int)g.wheel;
        g.wheel -= (float)step;
        if (e->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) step = -step;
        for (; step > 0; step--) Handle(GUIDE_UP);
        for (; step < 0; step++) Handle(GUIDE_DOWN);
        g.device = 0;
        break;
    }
    case SDL_MOUSEBUTTONDOWN: {
        if (e->button.windowID != win || imgui) return;
        g.device = 0;
        if (e->button.button == SDL_BUTTON_RIGHT) { PressB(); return; }
        if (e->button.button != SDL_BUTTON_LEFT) return;
        // The second half of a double click that opened something.
        if (e->button.clicks > 1 && g.clickMoved) return;
        ui_overlay_to_layout((float)e->button.x * scale, (float)e->button.y * scale, &x, &y);
        for (size_t i = 0; i < g.targets.size(); i++) {
            const HitTarget t = g.targets[i];
            if (x < t.x0 || x >= t.x1 || y < t.y0 || y >= t.y1) continue;
            std::string before = Screen();
            Click(t);
            g.clickMoved = Screen() != before;
            // Spans are from the last frame drawn; a moved screen has new ones.
            if (g.clickMoved) g.targets.clear();
            return;
        }
        break;
    }
    }
}

// Hovering picks the row or section under the pointer.
static void Hover()
{
    if (!g.pointerMoved) return;
    g.pointerMoved = false;
    for (size_t i = 0; i < g.targets.size(); i++) {
        const HitTarget& t = g.targets[i];
        if (g.pointerX < t.x0 || g.pointerX >= t.x1 || g.pointerY < t.y0 || g.pointerY >= t.y1) continue;
        if (t.kind == T_KEY) g.edit.key = t.index;
        else if (t.kind == T_ROW) { g.inPanel = true; g.cursor = t.index; }
        else if (t.kind == T_SECTION && !g.inPanel && t.index != g.section) ShowSection(t.index);
        return;
    }
}

// Drawing

static void AddTarget(float x0, float y0, float x1, float y1, int kind, int index)
{
    HitTarget t = { x0, y0, x1, y1, kind, index };
    g.targets.push_back(t);
}

// The text cut short with "..." to fit width.
static std::string Fit(int font, float size, const std::string& text, float width)
{
    if (width <= 0.0f) return "";
    if (ui_overlay_text_width(font, size, text.c_str()) <= width) return text;
    std::string s = text;
    while (!s.empty()) {
        // Drop one whole UTF-8 character.
        size_t cut = s.size() - 1;
        while (cut > 0 && ((unsigned char)s[cut] & 0xC0) == 0x80) cut--;
        s.erase(cut);
        std::string t = s + "...";
        if (ui_overlay_text_width(font, size, t.c_str()) <= width) return t;
    }
    return "...";
}

static void DrawHeader(const char* title)
{
    ui_overlay_text_glow(UI_FONT_BOLD, 17.0f, CONTENT_L, HEADER_Y + 4, UI_ALIGN_LEFT,
                         ui_style_color(UI_COLOR_TITLE), ui_style_color(UI_COLOR_TITLE_GLOW), title);
    char line[96];
    snprintf(line, sizeof(line), "UIX Desktop %s", ShippedVersion());
    ui_overlay_text(UI_FONT_BOLD, 10.0f, CONTENT_R, HEADER_Y + 3, UI_ALIGN_RIGHT, ui_style_color(UI_COLOR_NAME), line);
    ui_overlay_text(UI_FONT_REGULAR, 8.5f, CONTENT_R, HEADER_Y + 17, UI_ALIGN_RIGHT, ui_style_color(UI_COLOR_FAINT),
                    Host_Name());
    ui_overlay_rect(CONTENT_L - 4, RULE_Y, CONTENT_R - CONTENT_L + 8, 1, 0, ui_style_color(UI_COLOR_RULE));
}

static void DrawSections()
{
    float x = CONTENT_L, y = BODY_Y;
    for (int i = 0; i < SEC_COUNT; i++) {
        bool picked = i == g.section, lit = picked && !g.inPanel;
        if (i == SEC_EXIT) {
            ui_overlay_rect(x + 9, y + 3, SECTIONS_W - 18, 1, 0, ui_style_color(UI_COLOR_SEPARATOR));
            y += 8;
        }
        if (picked) ui_style_row(x, y, SECTIONS_W, SECTION_H, lit);
        std::string name = Fit(UI_FONT_BOLD, 11.5f, kSections[i], SECTIONS_W - 18);
        ui_overlay_text_glow(UI_FONT_BOLD, 11.5f, x + 9, y + (SECTION_H - 11.5f) * 0.5f, UI_ALIGN_LEFT,
                             picked ? 0xFFFFFFFF : ui_style_color(UI_COLOR_NAV),
                             lit ? ui_style_color(UI_COLOR_SELECTED_GLOW) : 0, name.c_str());
        AddTarget(x, y, x + SECTIONS_W, y + SECTION_H, T_SECTION, i);
        y += SECTION_H + SECTION_GAP;
    }
}

static void DrawRows(float left, float right, float top)
{
    float width = right - left;
    int shown = (int)((BODY_B - top + ROW_GAP) / (ROW_H + ROW_GAP));
    if (shown < 1) shown = 1;
    int n = (int)g.rows.size();
    if (g.cursor < g.scroll) g.scroll = g.cursor;
    if (g.cursor >= g.scroll + shown) g.scroll = g.cursor - shown + 1;
    if (g.scroll > n - shown) g.scroll = n - shown;
    if (g.scroll < 0) g.scroll = 0;

    float y = top;
    for (int i = g.scroll; i < n && i < g.scroll + shown; i++) {
        const Row& r = g.rows[i];
        bool on = g.inPanel && i == g.cursor;
        if (r.act == GA_SETTING || r.act == GA_CATEGORY) {
            const SettingDef* d = r.act == GA_SETTING ? &DesktopSettings_Table().defs[r.arg] : NULL;
            const char* badge = d && (d->flags & SF_RESTART) ? "RESTART" : NULL;
            float labelRoom = width * 0.55f - (badge ? 50.0f : 0.0f);
            std::string label = Fit(UI_FONT_BOLD, 11.0f, r.label, labelRoom);
            std::string value = Fit(UI_FONT_BOLD, 11.0f, r.value, width * 0.45f - 50);
            ui_style_setting(left, y, width, ROW_H, 1.0f, label.c_str(), badge, value.c_str(), on);
        } else {
            ui_style_row(left, y, width, ROW_H, on);
            float ty = y + (ROW_H - 11.0f) * 0.5f;
            float valueW = r.value.empty() ? 0.0f : ui_overlay_text_width(UI_FONT_BOLD, 11.0f, r.value.c_str());
            float chevron = r.act == GA_VIEW || r.act == GA_GAMECOLL || r.act == GA_COLLECTION ||
                            r.act == GA_BACKUPS || r.act == GA_PICKBACKUP ? 14.0f : 0.0f;
            if (valueW > width * 0.45f) valueW = width * 0.45f;
            std::string label = Fit(UI_FONT_BOLD, 11.0f, r.label, width - 24 - valueW - chevron - 8);
            unsigned int labelColor = r.act == GA_NONE ? ui_style_color(UI_COLOR_STATUS) : 0xFFFFFFFF;
            ui_overlay_text(UI_FONT_BOLD, 11.0f, left + 12, ty, UI_ALIGN_LEFT, labelColor, label.c_str());
            if (!r.value.empty()) {
                std::string value = Fit(UI_FONT_BOLD, 11.0f, r.value, valueW);
                ui_overlay_text(UI_FONT_BOLD, 11.0f, right - 12 - chevron, ty, UI_ALIGN_RIGHT,
                                ui_style_color(UI_COLOR_TAB), value.c_str());
            }
            if (chevron > 0.0f)
                ui_overlay_text(UI_FONT_BOLD, 11.0f, right - 12, ty, UI_ALIGN_RIGHT, ui_style_color(UI_COLOR_TAB),
                                "\xE2\x80\xBA");
        }
        AddTarget(left, y, right, y + ROW_H, T_ROW, i);
        y += ROW_H + ROW_GAP;
    }
    // More above or below.
    if (g.scroll > 0)
        ui_overlay_text(UI_FONT_BOLD, 9.0f, right - 6, top - 12, UI_ALIGN_RIGHT, ui_style_color(UI_COLOR_LABEL), "MORE");
    if (g.scroll + shown < n)
        ui_overlay_text(UI_FONT_BOLD, 9.0f, right - 6, y - ROW_GAP + 1, UI_ALIGN_RIGHT, ui_style_color(UI_COLOR_LABEL),
                        "MORE");

    // The chosen row's help along the bottom.
    if ((g.inPanel || g.welcome) && g.cursor >= 0 && g.cursor < n && !g.rows[g.cursor].help.empty()) {
        std::string help = Fit(UI_FONT_REGULAR, 10.0f, g.rows[g.cursor].help, width);
        ui_style_text(UI_TEXT_LINE, left, BODY_B + 8, UI_ALIGN_LEFT, help.c_str());
    }
}

static void DrawPanel(float left, float right)
{
    float top = BODY_Y;
    if (g.view == GV_PAGE) {
        float starts[8], ends[8];
        ui_style_tabs(left, right, top, 20, kPages, kPageCount, g.page, "Q", "E", starts, ends);
        for (int i = 0; i < kPageCount; i++) AddTarget(starts[i], top, ends[i], top + 20, T_TAB, i);
        top += 30;
    } else {
        std::string heading = g.view == GV_WELCOME ? "Ways in" : ViewTitle();
        for (size_t i = 0; i < heading.size(); i++) heading[i] = (char)toupper((unsigned char)heading[i]);
        heading = Fit(UI_FONT_BOLD, 9.0f, heading, right - left);
        ui_style_text(UI_TEXT_HEADING, left + 1, top + 2, UI_ALIGN_LEFT, heading.c_str());
        top += 20;
    }
    DrawRows(left, right, top);
}

// The field over the keys. Secrets show as dots; a long line slides so
// the cursor stays in sight.
static void DrawEditor()
{
    const Editor& e = g.edit;
    const float left = CONTENT_L, right = CONTENT_R, width = right - left, pad = 12.0f, size = 13.0f;

    ui_style_glass(left, FIELD_Y, width, FIELD_H, 7, ui_style_color(UI_COLOR_PANE),
                   ui_style_color(UI_COLOR_SELECTED_EDGE));
    std::string shown;
    size_t caret = 0;
    if (e.secret) {
        for (size_t i = 0; i < e.text.size(); i = NextChar(e.text, i)) {
            if (i < e.caret) caret += 3;
            shown += "\xE2\x80\xA2";
        }
    } else {
        shown = e.text;
        caret = e.caret;
    }
    const float room = width - 2 * pad;
    size_t from = 0;
    std::string lead;
    while (from < caret &&
           ui_overlay_text_width(UI_FONT_BOLD, size, (lead + shown.substr(from, caret - from)).c_str()) > room - 4) {
        from = NextChar(shown, from);
        lead = "...";
    }
    const float ty = FIELD_Y + (FIELD_H - size) * 0.5f;
    if (shown.empty())
        ui_overlay_text(UI_FONT_REGULAR, 11.0f, left + pad + 4, FIELD_Y + (FIELD_H - 11.0f) * 0.5f, UI_ALIGN_LEFT,
                        ui_style_color(UI_COLOR_TEXT_DIM), "Empty");
    else
        ui_overlay_text(UI_FONT_BOLD, size, left + pad, ty, UI_ALIGN_LEFT, 0xFFFFFFFF,
                        Fit(UI_FONT_BOLD, size, lead + shown.substr(from), room).c_str());
    if ((SDL_GetTicks() - e.since) % 1000 < 600) {
        float cx = left + pad + ui_overlay_text_width(UI_FONT_BOLD, size, (lead + shown.substr(from, caret - from)).c_str());
        ui_overlay_rect(cx, ty - 1, 1.5f, size + 3, 0, ui_style_color(UI_COLOR_SELECTED_EDGE));
    }

    static const char* const kWideNames[KEY_COUNT - KEY_CHARS] = { "Shift", "#+=", "Space", "Delete", "Paste", "Done" };
    const float unit = (width + KEY_GAP) / 10.0f;
    const int layout = KeyLayout();
    for (int k = 0; k < KEY_COUNT; k++) {
        int row;
        float x, w;
        KeySpan(k, &row, &x, &w);
        float kx = left + x * unit, ky = KEYS_Y + row * (KEY_H + KEY_GAP), kw = w * unit - KEY_GAP;
        bool on = k == e.key, lit = (k == K_SHIFT && e.shift) || (k == K_SYMBOLS && e.symbols);
        ui_style_row(kx, ky, kw, KEY_H, on);
        char c[2] = { 0, 0 };
        const char* label = c;
        float ls = 13.0f;
        if (k < KEY_CHARS) {
            c[0] = kKeyRows[layout][k / 10][k % 10];
        } else {
            label = k == K_SYMBOLS && e.symbols ? "abc" : kWideNames[k - KEY_CHARS];
            ls = 10.5f;
        }
        unsigned int color = on || k < KEY_CHARS ? 0xFFFFFFFF
                           : lit ? ui_style_color(UI_COLOR_TITLE) : ui_style_color(UI_COLOR_TAB);
        ui_overlay_text(UI_FONT_BOLD, ls, kx + kw * 0.5f, ky + (KEY_H - ls) * 0.5f, UI_ALIGN_CENTER, color, label);
        AddTarget(kx, ky, kx + kw, ky + KEY_H, T_KEY, k);
    }

    ui_style_text(UI_TEXT_LINE, left, BODY_B + 8, UI_ALIGN_LEFT,
                  ui_overlay_keyboard() ? "Type away. Enter saves, Esc cancels."
                                        : (std::string(Shoulders()) + " move the cursor.").c_str());
}

static void Prompt(ui_style_prompt* p, int* n, int button, const char* key, const char* word, unsigned bits)
{
    p[*n].button = button;
    p[*n].key = key;
    p[*n].word = word;
    g.promptButtons[*n] = (int)bits;
    (*n)++;
}

static void DrawPrompts()
{
    ui_style_prompt p[6];
    int n = 0;
    if (g.edit.on) {
        if (!ui_overlay_keyboard()) {
            Prompt(p, &n, UI_BUTTON_A, NULL, "Type", GUIDE_A);
            Prompt(p, &n, UI_BUTTON_X, NULL, "Delete", GUIDE_X);
            Prompt(p, &n, UI_BUTTON_Y, NULL, "Space", GUIDE_Y);
        }
        Prompt(p, &n, UI_BUTTON_START, "ENTER", "Done", GUIDE_START);
        Prompt(p, &n, UI_BUTTON_B, "ESC", "Cancel", GUIDE_B);
    } else if (g.inPanel && g.cursor >= 0 && g.cursor < (int)g.rows.size()) {
        int act = g.rows[g.cursor].act;
        const char* word = act == GA_NONE ? NULL
                         : act == GA_SETTING || act == GA_CATEGORY ? "Change"
                         : act == GA_VIEW || act == GA_GAMECOLL || act == GA_COLLECTION ||
                           act == GA_BACKUPS || act == GA_PICKBACKUP ? "Open"
                         : act == GA_RESTORE ? "Restore"
                         : act == GA_TEXT || act == GA_NAME || act == GA_JELLYFINURL ? "Edit" : "Select";
        if (word) Prompt(p, &n, UI_BUTTON_A, "ENTER", word, GUIDE_A);
    } else if (!g.inPanel) {
        Prompt(p, &n, UI_BUTTON_A, "ENTER", "Open", GUIDE_A);
    }
    if (!g.edit.on && g.inPanel && g.view == GV_GAMES) {
        Prompt(p, &n, UI_BUTTON_X, "F", "Filter", GUIDE_X);
        if (g.gameList == GL_ALL) Prompt(p, &n, UI_BUTTON_Y, "O", "Sort", GUIDE_Y);
    }
    if (!g.edit.on) {
        bool back = !g.welcome && g.inPanel;
        Prompt(p, &n, UI_BUTTON_B, "ESC", back ? "Back" : "Close", GUIDE_B);
    }
    float starts[6], ends[6];
    ui_style_prompts(CONTENT_R, PROMPTS_Y, 15.0f, p, n, starts, ends);
    for (int i = 0; i < n; i++)
        AddTarget(starts[i] - 4, PROMPTS_Y - 4, ends[i] + 4, PROMPTS_Y + 19, T_PROMPT, g.promptButtons[i]);
    ui_overlay_rect(CONTENT_L - 4, PROMPTS_Y - 9, CONTENT_R - CONTENT_L + 8, 1, 0, ui_style_color(UI_COLOR_RULE));
}

void Guide_Render(int width, int height)
{
    if (g.open) {
        Hover();
        g.targets.clear();
        ui_overlay_set_device(g.device);
        ui_overlay_set_pad_style(Pads_Style());
        ui_overlay_set_pad_swap(g_padSwapConfirm);
        ui_style_theme(g_guideTheme);
        ui_style_clock(SDL_GetTicks());
        ui_overlay_layer(0);
        float l = ui_overlay_layout_left(), r = ui_overlay_layout_right();
        ui_overlay_rect(l - 2, -2, r - l + 4, 484, 0, ui_style_color(UI_COLOR_SHADE));
        ui_style_panel(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 1.0f);
        if (g.edit.on) {
            DrawHeader(g.edit.title.c_str());
            DrawEditor();
        } else if (g.welcome) {
            DrawPanel(CONTENT_L + 40, CONTENT_R - 40);
        } else {
            DrawHeader("Guide");
            DrawSections();
            DrawPanel(LIST_L, CONTENT_R);
        }
        DrawPrompts();
    }
    ui_overlay_present(2, width, height);
}

void Guide_Shutdown()
{
    ui_overlay_shutdown();
}
