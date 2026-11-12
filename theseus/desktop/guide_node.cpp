// guide_node.cpp: the DesktopGuide node behind the guide scene (Guide.xap).
// It owns the menus (settings rows from the schema, About, Shortcuts) and the
// script just draws rows and forwards buttons.

#include "std.h"
#include "dashapp.h"
#include "node.h"
#include "runner.h"
#include "scene_groups.h"
#include "panel_shared.h"
#include "settings_schema.h"
#include "host.h"
#include "app_paths.h"
#include "preloader.h"
#include "log_redact.h"
#include "virtual_games.h"
#include "playlist.h"
#include "esde_vgames.h"
#include "udata_synth.h"
#include "plex_client.h"
#include "jellyfin_client.h"
#include "xcloud_client.h"
#include <algorithm>
#include <strings.h>
#include <time.h>
#include <bgfx/bgfx.h>
#include <unistd.h>
#include <errno.h>
#include <SDL.h>
#include <string>
#include <vector>

extern CLevel* g_pCurLevel;
extern bool g_bLevelTransition;
extern bool g_padModeEnabled, g_padModeActive;
extern bool g_mediaFullscreen, g_xcloudFullscreen;
extern void* g_pActiveKeyboard_void();

// Set from the input layer (Guide button, LT+B, F7); the node picks it up.
bool g_guideRequested = false;
bool g_guideOpen = false;
bool g_welcomeDone = false;   // desktop.ini WelcomeDone, set once the welcome has been seen

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

static const char* ShippedVersion();

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

static const char* kCategories[] = { "Games", "Applications", "Homebrew", "Emulators", "Dashboards" };
static const int   kCategoryCount = (int)(sizeof(kCategories) / sizeof(kCategories[0]));

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

// "Some Game.app" or "/x/y/Game.exe" -> "Some Game" / "Game", made safe for the
// dashboard's font.
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

// A few rows need more than their label says.
static const char* SettingHelp(const SettingDef& d)
{
    static const struct { const char* key; const char* help; } kHelp[] = {
        { "StartupMode",         "What happens when UIX Desktop starts." },
        { "UseOnScreenKeyboard", "Text fields ignore the physical keyboard and use the pad keyboard." },
        { "ShowBootAnimation",   "Play the Xbox boot animation once at startup." },
        { "MenuBarAutoHide",     "The tool menu bar shows when the pointer reaches the top edge." },
        { "EmulateXboxRes",      "Keep scenes framed the way they were authored." },
        { "Hwdec",               "Faster on AMD and NVIDIA. Applies to the next video." },
        { "MasterVolume",        "Dashboard sounds, music and videos." },
        { "UseMilkdropViz",      "Use projectM for the music visualizer." },
        { "ShowAlbumCover",      "Show album art in the orb when a soundtrack has it." },
        { "CRT_Enabled",         "Old TV look over the whole dashboard." },
    };
    for (size_t i = 0; i < sizeof(kHelp) / sizeof(kHelp[0]); i++)
        if (strcmp(kHelp[i].key, d.key) == 0) return kHelp[i].help;
    if (d.flags & SF_RESTART) return "Takes effect the next time UIX Desktop starts.";
    if (d.kind == SK_PATH)    return "Folder paths are set in Settings (F4) for now.";
    if (d.kind == SK_STRING)  return "Set in Settings (F4) for now.";
    return "Left and right change it.";
}

static const char* kPages[] = { "General", "Display", "Audio", "Media", "Games" };
static const int   kPageCount = (int)(sizeof(kPages) / sizeof(kPages[0]));

enum GuideView { GV_ROOT, GV_PAGES, GV_PAGE, GV_ABOUT, GV_SHORTCUTS, GV_START, GV_LOGS,
                 GV_LIBRARY, GV_ADDGAME, GV_BROWSE, GV_PLAYLISTS, GV_PLAYLIST, GV_ACCOUNTS };

enum GuideAct {
    GA_NONE, GA_VIEW, GA_SETTING, GA_TOOLS, GA_RESUME, GA_QUIT,
    GA_DEVMODE, GA_FOLDERS, GA_OPENFOLDERS,
    GA_OPENLOGS, GA_COPYVERSION, GA_COPYLOG,
    GA_STEAM, GA_RETROARCH, GA_ESDE, GA_CHOOSEFILE, GA_CATEGORY, GA_ADDGAME,
    GA_BROWSEUP, GA_BROWSE, GA_PLAYLIST,
    GA_PLEX, GA_PLEXOUT, GA_JELLYFIN, GA_JELLYFINOUT, GA_XBOX, GA_XBOXOUT,
};

struct GuideRow {
    std::string label, value, help;
    int act;
    int arg;   // view to open, or setting index
};

class CDesktopGuide : public CNode
{
public:
    CDesktopGuide() : m_view(GV_ROOT), m_page(0), m_cursor(0), m_backTo(NULL),
                      m_startChecked(false), m_startShown(false), m_foldersDone(false),
                      m_copied(GA_NONE), m_category(0), m_playlist(0) {}
    DECLARE_NODE(CDesktopGuide, CNode)
    DECLARE_NODE_PROPS()
    DECLARE_NODE_FUNCTIONS()

    // Opening is the script's call (OnOpen); we only say when.
    void Advance(float nSeconds)
    {
        CNode::Advance(nSeconds);

        // First run, or Startup Mode is "Ask": once the dashboard has
        // arrived, the guide asks how to start.
        if (!m_startChecked && g_pCurLevel && !g_bLevelTransition) {
            m_startChecked = true;
            if (!g_welcomeDone || g_startupMode == 0) {
                CallFunction(this, _T("OnStartup"));
                return;
            }
        }

        if (!g_guideRequested) return;
        if (g_bLevelTransition) return;
        g_guideRequested = false;
        // Fullscreen video and a text field keep the buttons for themselves.
        if (g_mediaFullscreen || g_xcloudFullscreen || g_pActiveKeyboard_void()) return;
        CallFunction(this, g_guideOpen ? _T("OnClose") : _T("OnOpen"));
    }

    // The level under the guide, so B can put you back where you were.
    void Remember()
    {
        m_backTo = g_pCurLevel;
        g_guideOpen = true;
        m_view = GV_ROOT;
        m_cursor = 0;
        m_copied = GA_NONE;
        m_stack.clear();
        Build();
    }

    // 1 when we went back to the remembered level; 0 means the script should
    // fall back to the main menu.
    int ReturnToDashboard()
    {
        g_guideOpen = false;
        if (m_startShown && !g_welcomeDone) {
            g_welcomeDone = true;
            SaveDesktopSettings();
        }
        m_startShown = false;
        CLevel* back = m_backTo;
        m_backTo = NULL;
        if (back && back != g_pCurLevel) { back->GoBackTo(); return 1; }
        return 0;
    }

    void ShowStart()
    {
        m_view = GV_START;
        m_cursor = 0;
        m_startShown = true;
        Build();
    }

    int GetCount() { return (int)m_rows.size(); }
    int GetCursor() { return m_cursor; }
    void SetCursor(int i) { if (i >= 0 && i < (int)m_rows.size()) m_cursor = i; }

    CStrObject* GetTitle()
    {
        switch (m_view) {
        case GV_PAGES:     return new CStrObject(_T("Settings"));
        case GV_PAGE:      return new CStrObject(kPages[m_page]);
        case GV_ABOUT:     return new CStrObject(_T("About"));
        case GV_SHORTCUTS: return new CStrObject(_T("Shortcuts"));
        case GV_LOGS:      return new CStrObject(_T("Logs & Help"));
        case GV_LIBRARY:   return new CStrObject(_T("Games & Playlists"));
        case GV_ADDGAME:   return new CStrObject(_T("Add a Game"));
        case GV_BROWSE:    return new CStrObject(_T("Choose a File"));
        case GV_PLAYLISTS: return new CStrObject(_T("Playlists"));
        case GV_ACCOUNTS:  return new CStrObject(_T("Accounts"));
        case GV_PLAYLIST: {
            const Playlist* pl = Playlist_Get(m_playlist);
            return new CStrObject(pl ? pl->name.c_str() : "");
        }
        case GV_START:     return new CStrObject(g_welcomeDone ? _T("Start") : _T("Welcome"));
        default:           return new CStrObject(_T("UIX Desktop"));
        }
    }
    CStrObject* GetLabel(int i) { return new CStrObject(Row(i) ? Row(i)->label.c_str() : ""); }
    CStrObject* GetValue(int i) { return new CStrObject(Row(i) ? Row(i)->value.c_str() : ""); }
    CStrObject* GetHelp(int i)  { return new CStrObject(Row(i) ? Row(i)->help.c_str() : ""); }

    // A. 1 = the list changed (redraw from GetCursor), 2 = a value changed,
    // 3 = close the guide, -1 = nothing to do here.
    int Select(int i)
    {
        const GuideRow* r = Row(i);
        if (!r) return -1;
        m_cursor = i;
        switch (r->act) {
        case GA_VIEW:
            m_stack.push_back(MakeFrame());
            if (m_view == GV_PAGES) m_page = r->arg;
            m_view = r->arg >= 100 ? (GuideView)(r->arg - 100) : GV_PAGE;
            m_cursor = 0;
            Build();
            return 1;
        case GA_SETTING:
            return Adjust(i, 1) ? 2 : -1;
        case GA_TOOLS:
            g_padModeActive = true;
            return 0;
        case GA_RESUME:
            return 3;
        case GA_DEVMODE:
            g_welcomeDone = true;
            SaveDesktopSettings();
            if (!Preloader_EnsureExtracted()) return -1;
            RestartWithMode("--development");
            return -1;
        case GA_FOLDERS:
            SetUpFolders();
            m_foldersDone = true;
            Build();
            return 2;
        case GA_OPENFOLDERS:
            Plat_OpenInFileManager(AppPath_Tree("Library"));
            return 0;
        case GA_STEAM:
        case GA_RETROARCH: {
            int skipped = 0;
            int added = r->act == GA_STEAM ? TitleMaker_ImportSteam(&skipped)
                                           : TitleMaker_ImportRetroArch(&skipped);
            char msg[64];
            if (added < 0) snprintf(msg, sizeof(msg), "Not found");
            else snprintf(msg, sizeof(msg), "%d new, %d already in", added, skipped);
            m_result[r->act] = msg;
            Build();
            return added < 0 ? -1 : 2;
        }
        case GA_ESDE: {
            if (!s_esdePath[0]) { m_result[GA_ESDE] = "Set its folder first"; Build(); return -1; }
            EsdeVGames_Sync(s_esdePath);
            m_result[GA_ESDE] = "Refreshed";
            Build();
            return 2;
        }
        case GA_CHOOSEFILE:
            m_stack.push_back(MakeFrame());
            if (m_browseDir.empty()) {
                const char* h = HomeDir();
                m_browseDir = h ? h : "/";
            }
            m_view = GV_BROWSE;
            m_cursor = 0;
            Build();
            return 1;
        case GA_BROWSEUP:
            m_browseDir = ParentDir(m_browseDir);
            m_cursor = 0;
            Build();
            return 1;
        case GA_BROWSE: {
            const BrowseEntry& e = m_entries[r->arg];
            std::string full = m_browseDir;
            if (full.empty() || (full.back() != '/' && full.back() != '\\')) full += Host_PathSep;
            full += e.name;
            if (e.dir) {
                m_browseDir = full;
                m_cursor = 0;
                Build();
                return 1;
            }
            m_pickedPath = full;
            m_pickedName = NameFromPath(full);
            m_added.clear();
            Back();
            return 1;
        }
        case GA_CATEGORY:
            m_category = (m_category + 1) % kCategoryCount;
            Build();
            return 2;
        case GA_ADDGAME: {
            if (m_pickedPath.empty() || m_pickedName.empty()) return -1;
            if (VGames_FindByName(m_pickedName.c_str()) >= 0) { m_added = "Already there"; Build(); return -1; }
            char genID[16];
            snprintf(genID, sizeof(genID), "%08x", (unsigned)time(NULL));
            std::string launch = "\"" + m_pickedPath + "\"";
            VGames_Add(m_pickedName.c_str(), genID, launch.c_str(), "E", kCategories[m_category]);
            VGames_Save();
            UDataSynth_RebuildAll();
            m_added = "Added";
            m_pickedPath.clear();
            m_pickedName.clear();
            Build();
            return 2;
        }
        // Sign in starts a code flow, A again cancels it.
        case GA_PLEX:
            if (Plex_PinAuthInFlight()) Plex_CancelPinAuth(); else Plex_StartPinAuth();
            Build();
            return 2;
        case GA_JELLYFIN:
            if (Jellyfin_GetServerUrl().empty()) return -1;
            if (Jellyfin_QuickConnectInFlight()) Jellyfin_CancelQuickConnect(); else Jellyfin_StartQuickConnect();
            Build();
            return 2;
        case GA_XBOX:
            if (Xcloud_LoginInFlight()) Xcloud_CancelLogin(); else Xcloud_StartLogin();
            Build();
            return 2;
        case GA_PLEXOUT:     Plex_SignOut();     SaveDesktopSettings(); Build(); return 2;
        case GA_JELLYFINOUT: Jellyfin_SignOut(); SaveDesktopSettings(); Build(); return 2;
        case GA_XBOXOUT:     Xcloud_SignOut();   SaveDesktopSettings(); Build(); return 2;
        case GA_PLAYLIST:
            m_stack.push_back(MakeFrame());
            m_playlist = r->arg;
            m_view = GV_PLAYLIST;
            m_cursor = 0;
            Build();
            return 1;
        case GA_OPENLOGS:
            Host_OpenFolder(Plat_UserDataDir());
            return 0;
        case GA_COPYVERSION:
            if (SDL_SetClipboardText(VersionLine().c_str()) != 0) return -1;
            m_copied = GA_COPYVERSION;
            Build();
            return 2;
        case GA_COPYLOG:
            if (!CopyLog()) return -1;
            m_copied = GA_COPYLOG;
            Build();
            return 2;
        case GA_QUIT: {
            SDL_Event quit;
            quit.type = SDL_QUIT;
            SDL_PushEvent(&quit);
            return 0;
        }
        }
        return -1;
    }

    // Left / right on a setting row.
    int Adjust(int i, int dir)
    {
        const GuideRow* r = Row(i);
        if (r && r->act == GA_CATEGORY) {
            m_category = (m_category + (dir < 0 ? kCategoryCount - 1 : 1)) % kCategoryCount;
            Build();
            return 1;
        }
        if (!r || r->act != GA_SETTING) return 0;
        const SettingDef& d = DesktopSettings_Table().defs[r->arg];
        if (!Setting_Cycle(d, dir < 0 ? -1 : 1)) return 0;
        if (d.apply) d.apply();
        SaveDesktopSettings();
        Build();
        return 1;
    }

    // Sign-in codes arrive in the background; the scene polls this and
    // redraws on 1.
    int Refresh()
    {
        if (!g_guideOpen || m_view != GV_ACCOUNTS) return 0;
        Build();
        return 1;
    }

    // 1 = popped a level, 0 = at the top (the script closes the guide).
    int Back()
    {
        if (m_stack.empty()) return 0;
        Frame f = m_stack.back();
        m_stack.pop_back();
        m_view = f.view; m_page = f.page; m_cursor = f.cursor;
        Build();
        return 1;
    }

private:
    struct Frame {
        GuideView view; int page, cursor;
    };
    Frame MakeFrame() const { Frame f = { m_view, m_page, m_cursor }; return f; }

    GuideView m_view;
    int m_page, m_cursor;
    std::vector<Frame> m_stack;
    std::vector<GuideRow> m_rows;
    CLevel* m_backTo;
    bool m_startChecked, m_startShown, m_foldersDone;
    int m_copied;   // which Copy row says Copied
    std::string m_result[GA_PLAYLIST + 1];   // what an import row says after running
    std::string m_browseDir, m_pickedPath, m_pickedName, m_added;
    std::vector<BrowseEntry> m_entries;
    int m_category, m_playlist;

    const GuideRow* Row(int i) const
    {
        return (i >= 0 && i < (int)m_rows.size()) ? &m_rows[i] : NULL;
    }

    void Add(const char* label, const char* value, const char* help, int act = GA_NONE, int arg = 0)
    {
        GuideRow r;
        r.label = label; r.value = value ? value : ""; r.help = help ? help : "";
        r.act = act; r.arg = arg;
        m_rows.push_back(r);
    }

    void Build()
    {
        m_rows.clear();
        switch (m_view) {
        case GV_ROOT:
            Add("Settings",  "", "Display, audio, media folders and startup.", GA_VIEW, 100 + GV_PAGES);
            Add("Games & Playlists", "", "Add a game, run the imports, see your playlists.", GA_VIEW, 100 + GV_LIBRARY);
            Add("Accounts", "", "Plex, Jellyfin and Xbox sign in.", GA_VIEW, 100 + GV_ACCOUNTS);
            Add("Shortcuts", "", "Keys and buttons that work everywhere.", GA_VIEW, 100 + GV_SHORTCUTS);
            Add("Logs & Help", "", "Open or copy the log for a bug report.", GA_VIEW, 100 + GV_LOGS);
            Add("About",     "", "Version and credits.", GA_VIEW, 100 + GV_ABOUT);
            if (g_padModeEnabled)
                Add("Desktop Tools", "", "Title Maker, HDD Browser and Playlist Maker with the pad.", GA_TOOLS);
            Add("Resume Dashboard", "", "Back to where you were.", GA_RESUME);
            Add("Exit to Desktop",  "", "Quit UIX Desktop.", GA_QUIT);
            break;
        case GV_PAGES:
            for (int p = 0; p < kPageCount; p++)
                Add(kPages[p], "", NULL, GA_VIEW, p);
            break;
        case GV_PAGE: {
            const SettingsTable& t = DesktopSettings_Table();
            char val[512];
            for (int i = 0; i < t.count; i++) {
                const SettingDef& d = t.defs[i];
                if (d.flags & SF_HIDDEN) continue;
                if (strcmp(d.page, kPages[m_page]) != 0) continue;
                Setting_Display(d, val, sizeof(val));
                bool editable = d.kind != SK_PATH && d.kind != SK_STRING;
                Add(d.label, val, SettingHelp(d), editable ? GA_SETTING : GA_NONE, i);
            }
            break;
        }
        case GV_ABOUT: {
            char ver[96];
            snprintf(ver, sizeof(ver), "%s", ShippedVersion());
            Add("Version",  ver, "The build you're running.");
            Add("Platform", Host_Name(), NULL);
            Add("Created by", "Milenko", NULL);
            Add("UIX Lite references", "BigJx, ImOkRuOk", NULL);
            Add("", "Odb718, Rocky5, TeamUIX", NULL);
            Add("PrometheOS, Toolbox", "Team-Resurgent", NULL);
            Add("Original dashboard", "Microsoft", NULL);
            Add("Community", "Xbox-Scene", NULL);
            Add("And everyone who contributed", "", NULL);
            break;
        }
        case GV_START: {
            const char* intro = g_welcomeDone ? "B or A starts the dashboard."
                : "Nothing here is required. It's all in the guide later too.";
            Add("Start the Dashboard", "", intro, GA_RESUME);
            Add("Development Mode", "", "Edit the scenes live. Restarts UIX Desktop.", GA_DEVMODE);
            if (!g_welcomeDone || m_foldersDone)
                Add("Set Up My Folders", m_foldersDone ? "Done" : "",
                    "Makes Music, Movies, TV Shows and ROMs folders. Fill them when you like.",
                    GA_FOLDERS);
            Add("Open My Folders", "", "Shows them in your file manager.", GA_OPENFOLDERS);
            const SettingDef* mode = DesktopSettings_Find("StartupMode");
            if (mode) {
                char val[64];
                Setting_Display(*mode, val, sizeof(val));
                Add(mode->label, val, "Ask on launch brings this screen back each time.", GA_SETTING,
                    (int)(mode - DesktopSettings_Table().defs));
            }
            break;
        }
        case GV_LIBRARY: {
            Add("Add a Game", "", "Pick a program or file and it shows up in the launcher.", GA_VIEW, 100 + GV_ADDGAME);
            Add("Import Steam Library", m_result[GA_STEAM].c_str(), "Adds installed Steam games with their icons.", GA_STEAM);
            Add("Import RetroArch Recent", m_result[GA_RETROARCH].c_str(), "Adds what RetroArch's playlists list.", GA_RETROARCH);
            Add("Refresh ES-DE", m_result[GA_ESDE].c_str(), "Reads your ES-DE game lists again.", GA_ESDE);
            char n[32];
            snprintf(n, sizeof(n), "%d", Playlist_Count());
            Add("Playlists", n, "Made in the Playlist Maker.", GA_VIEW, 100 + GV_PLAYLISTS);
            break;
        }
        case GV_ADDGAME: {
            std::string file = m_pickedPath.empty() ? "Choose" : NameOnly(m_pickedPath);
            Add("File", file.c_str(), m_pickedPath.empty() ? "A program, a shortcut or a ROM." : m_pickedPath.c_str(), GA_CHOOSEFILE);
            Add("Name", m_pickedName.c_str(), "Taken from the file name. Rename it in the Title Maker (F3).");
            Add("Category", kCategories[m_category], "Left and right change it.", GA_CATEGORY);
            Add("Add to Launcher", m_added.c_str(),
                m_pickedPath.empty() ? "Choose a file first." : "Adds it under the category above.", GA_ADDGAME);
            break;
        }
        case GV_BROWSE: {
            ListDir(m_browseDir, m_entries);
            Add("Up a Folder", "", m_browseDir.c_str(), GA_BROWSEUP);
            for (size_t i = 0; i < m_entries.size(); i++)
                Add(m_entries[i].name.c_str(), m_entries[i].dir ? "Folder" : "", m_browseDir.c_str(), GA_BROWSE, (int)i);
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
            if (m_rows.empty()) Add("No playlists yet", "", "Make one in the Playlist Maker (F6).");
            break;
        case GV_PLAYLIST: {
            const Playlist* pl = Playlist_Get(m_playlist);
            if (pl)
                for (size_t i = 0; i < pl->items.size(); i++)
                    Add(pl->items[i].title.empty() ? NameOnly(pl->items[i].path).c_str() : pl->items[i].title.c_str(),
                        "", NULL);
            if (m_rows.empty()) Add("Empty", "", NULL);
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
            } else if (Jellyfin_GetServerUrl().empty()) {
                Add("Jellyfin", "No server", "Set the server address in Settings (F4) first.", GA_JELLYFIN);
            } else if (Jellyfin_QuickConnectInFlight()) {
                std::string code = Jellyfin_GetQuickConnectCode();
                Add("Jellyfin", code.empty() ? "Asking..." : code.c_str(),
                    "In Jellyfin's web page open your account, then Quick Connect, and enter the code.", GA_JELLYFIN);
            } else {
                Add("Jellyfin", "Sign In", "Quick Connect gives you a code to approve on the server.", GA_JELLYFIN);
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
            Add("Copy Version Info", m_copied == GA_COPYVERSION ? "Copied" : "",
                "Version, platform and renderer, ready to paste.", GA_COPYVERSION);
            Add("Copy Log", m_copied == GA_COPYLOG ? "Copied" : "",
                "This run's log. Your home folder shows as ~ and IP addresses are hidden.", GA_COPYLOG);
            Add("Open Logs Folder", "", "theseus.log is this run, theseus.prev.log the one before.", GA_OPENLOGS);
            Add("Report a Problem", "", "Paste the version and the log into an issue on GitHub.");
            break;
        case GV_SHORTCUTS:
            Add("Guide button, LT+B or F7", "Guide", "Opens and closes this guide.");
            Add("F3",  "Title Maker", NULL);
            Add("F4",  "Settings", "The mouse settings window.");
            Add("F5",  "HDD Browser", NULL);
            Add("F6",  "Playlist Maker", NULL);
            Add("F10", "Hide Menu Bar", NULL);
            Add("F11", "Fullscreen", NULL);
            Add("F12", "Borderless Window", NULL);
            Add("Ctrl+M", "Mute", NULL);
            Add("Ctrl+R", "Restart Dashboard", NULL);
            Add("Ctrl+Q", "Quit", NULL);
            if (g_startupMode == 2 || g_extractedMode) {
                Add("F1", "Inspector", "Development mode.");
                Add("F2", "XAP Editor", "Development mode.");
            }
            break;
        }
        if (m_cursor >= (int)m_rows.size()) m_cursor = m_rows.empty() ? 0 : (int)m_rows.size() - 1;
    }
};

IMPLEMENT_NODE("DesktopGuide", CDesktopGuide, CNode)

START_NODE_PROPS(CDesktopGuide, CNode)
END_NODE_PROPS()

#undef _FND_CLASS
#define _FND_CLASS CDesktopGuide
START_NODE_FUN(CDesktopGuide, CNode)
	NODE_FUN_VV(Remember)
	NODE_FUN_VV(ShowStart)
	NODE_FUN_IV(ReturnToDashboard)
	NODE_FUN_IV(GetCount)
	NODE_FUN_IV(GetCursor)
	NODE_FUN_VI(SetCursor)
	NODE_FUN_SV(GetTitle)
	NODE_FUN_SI(GetLabel)
	NODE_FUN_SI(GetValue)
	NODE_FUN_SI(GetHelp)
	NODE_FUN_II(Select)
	NODE_FUN_III(Adjust)
	NODE_FUN_IV(Back)
	NODE_FUN_IV(Refresh)
END_NODE_FUN()
