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

enum GuideView { GV_ROOT, GV_PAGES, GV_PAGE, GV_ABOUT, GV_SHORTCUTS, GV_START };

enum GuideAct {
    GA_NONE, GA_VIEW, GA_SETTING, GA_TOOLS, GA_RESUME, GA_QUIT,
    GA_DEVMODE, GA_FOLDERS, GA_OPENFOLDERS,
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
                      m_startChecked(false), m_startShown(false), m_foldersDone(false) {}
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
        if (!r || r->act != GA_SETTING) return 0;
        const SettingDef& d = DesktopSettings_Table().defs[r->arg];
        if (!Setting_Cycle(d, dir < 0 ? -1 : 1)) return 0;
        if (d.apply) d.apply();
        SaveDesktopSettings();
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
            Add("Shortcuts", "", "Keys and buttons that work everywhere.", GA_VIEW, 100 + GV_SHORTCUTS);
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
END_NODE_FUN()
