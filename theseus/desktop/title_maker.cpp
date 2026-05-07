// title_maker.cpp: F3 Title Maker panel. Floating ImGui window for
// managing the virtual games library (the games.ini-driven entries
// the dashboard surfaces in TitleScanner). Desktop-only.

#include "std.h"
#include "dashapp.h"
#include "panel_shared.h"
#include "virtual_games.h"
#include "udata_synth.h"
#include "xiso.h"
#include "launchers/steam.h"
#include "imgui.h"
#include "imfilebrowser.h"
#include "stb_image.h"
#include "http_util.h"
#include <sys/stat.h>
#include <algorithm>
#include <string>

#ifdef _WIN32
#include <io.h>
#include <direct.h>
#include <process.h>
#else
#include <dirent.h>
#endif

// Platform code uses native file I/O; undo Xbox filesystem macros.
#undef fopen
#undef _tfopen
#ifdef _WIN32
#undef CreateFile
#undef CreateFileA
#undef FindFirstFile
#undef FindFirstFileA
#undef FindNextFile
#undef FindNextFileA
#undef FindClose
#undef GetFileAttributes
#undef GetFileAttributesA
#undef GetFileAttributesEx
#undef GetFileAttributesExA
#undef RemoveDirectory
#undef RemoveDirectoryA
#endif

// OpenGL headers (for glGenTextures, glBindTexture, etc.)
#ifdef __APPLE__
    #define GL_SILENCE_DEPRECATION
    #include <OpenGL/gl3.h>
#elif defined(_WIN32)
    #include <GL/glew.h>
#else
    #define GL_GLEXT_PROTOTYPES
    #include <GL/gl.h>
#endif

bool g_titleMakerOpen = false;

// ============================================================================
// Helpers
// ============================================================================

static void TM_EnsureDir(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) {
#ifdef _WIN32
        _mkdir(path);
#else
        mkdir(path, 0755);
#endif
    }
}

static void TM_SanitizeName(char* name, int maxLen) {
    for (char* c = name; *c; c++) {
        if (*c == ':' || *c == '/' || *c == '\\' || *c == '?' ||
            *c == '*' || *c == '"' || *c == '<' || *c == '>' || *c == '|')
            *c = '-';
    }
    int len = (int)strlen(name);
    while (len > 0 && (name[len-1] == ' ' || name[len-1] == '-'))
        name[--len] = 0;
}

static bool TM_CopyFile(const char* src, const char* dst) {
    FILE* sfp = fopen(src, "rb");
    FILE* dfp = fopen(dst, "wb");
    if (!sfp || !dfp) {
        if (sfp) fclose(sfp);
        if (dfp) fclose(dfp);
        return false;
    }
    unsigned char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), sfp)) > 0)
        fwrite(buf, 1, n, dfp);
    fclose(sfp);
    fclose(dfp);
    return true;
}

// Read Icons.ini into parallel key/value arrays. Returns entry count.
#define TM_MAX_ICONS 512
static int TM_ReadIconsIni(char keys[][128], char vals[][128]) {
    int count = 0;
    const char* path = "Configs/Icons.ini";
    FILE* fp = fopen(path, "r");
    if (!fp) fp = fopen("Configs/icons.ini", "r");
    if (!fp) return 0;
    char line[256];
    while (fgets(line, sizeof(line), fp) && count < TM_MAX_ICONS) {
        char* nl = strchr(line, '\n'); if (nl) *nl = 0;
        char* cr = strchr(line, '\r'); if (cr) *cr = 0;
        if (line[0] == '[' || line[0] == 0) continue;
        char* eq = strchr(line, '=');
        if (eq) {
            *eq = 0;
            strncpy(keys[count], line, 127); keys[count][127] = 0;
            strncpy(vals[count], eq + 1, 127); vals[count][127] = 0;
            count++;
        }
    }
    fclose(fp);
    return count;
}

static void TM_WriteIconsIni(char keys[][128], char vals[][128], int count) {
    FILE* fp = fopen("Configs/Icons.ini", "w");
    if (!fp) return;
    fprintf(fp, "[default]\n");
    for (int i = 0; i < count; i++)
        fprintf(fp, "%s=%s\n", keys[i], vals[i]);
    fclose(fp);
}

// ============================================================================
// Title Maker Panel
// ============================================================================

void RenderTitleMaker() {
    if (!g_titleMakerOpen) return;

    // Position and size on first open. Cap max size to the viewport so the
    // title bar can't get pushed off-screen if the user's window is small.
    ImVec2 vp = ImGui::GetMainViewport()->Size;
    float maxW = vp.x - 40.0f, maxH = vp.y - 80.0f;
    if (maxW < 640) maxW = 640;
    if (maxH < 480) maxH = 480;
    ImGui::SetNextWindowPos(ImVec2(20, 40), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(640, 480), ImVec2(maxW, maxH));
    ImGui::SetNextWindowSize(ImVec2(820, maxH < 680 ? maxH : 680), ImGuiCond_FirstUseEver);

    ImGui::Begin("UIX Title Maker", &g_titleMakerOpen);

    // State (persists across frames)
    #define TM_MAX_ENTRIES 512
    struct TmEntry { int vgIndex; char name[128]; char launch[512]; char titleID[16]; char drive[4]; char category[32]; };
    static TmEntry s_entries[TM_MAX_ENTRIES];
    static int s_entryCount = 0;
    static bool s_needsScan = true;
    static int s_selectedIdx = -1;
    static char s_editName[128] = "";
    static char s_editLaunch[512] = "";
    static char s_editTitleID[16] = "";
    static int s_editCategoryIdx = 0;
    static char s_newTitleName[128] = "";
    static char s_statusMsg[256] = "";
    static float s_statusTime = 0;
    static GLuint s_iconTex = 0;
    static int s_iconTexIdx = -1;
    static char s_searchFilter[128] = "";
    static const char* s_categories[] = { "Games", "Applications", "Homebrew", "Emulators", "Dashboards" };
    static const int s_categoryCount = 5;
    static int s_filterCategoryIdx = -1; // -1 = show all

    // File browsers
    static ImGui::FileBrowser s_iconBrowser(ImGuiFileBrowserFlags_CloseOnEsc);
    static ImGui::FileBrowser s_appBrowser(ImGuiFileBrowserFlags_CloseOnEsc);
    static bool s_iconBrowserInit = false;
    static bool s_appBrowserInit = false;
    static ImGui::FileBrowser s_isoBrowser(ImGuiFileBrowserFlags_CloseOnEsc);
    static bool s_isoBrowserInit = false;
    static ImGui::FileBrowser s_steamBrowser(
        ImGuiFileBrowserFlags_CloseOnEsc | ImGuiFileBrowserFlags_SelectDirectory);
    static bool s_steamBrowserInit = false;
    static char s_isoPath[512] = "";
    static char s_isoStatus[256] = "";
    static bool s_isoParsed = false;
    static XisoTitleInfo s_isoInfo = {};
    static GLuint s_isoPreviewTex = 0;
    static bool s_showIsoResult = false;

    if (!s_iconBrowserInit) {
        s_iconBrowser.SetTitle("Select Icon Image");
        s_iconBrowser.SetTypeFilters({ ".jpg", ".jpeg", ".png", ".bmp", ".JPG", ".PNG" });
        s_iconBrowserInit = true;
    }
    if (!s_appBrowserInit) {
        s_appBrowser.SetTitle("Select Application");
        s_appBrowserInit = true;
    }
    if (!s_isoBrowserInit) {
        s_isoBrowser.SetTitle("Select Xbox ISO");
        s_isoBrowser.SetTypeFilters({ ".iso", ".ISO" });
        s_isoBrowserInit = true;
    }
    if (!s_steamBrowserInit) {
        s_steamBrowser.SetTitle("Select Steam install folder (contains steamapps/)");
        s_steamBrowserInit = true;
    }

    // Case-insensitive substring search helper
    auto CaseStrStr = [](const char* haystack, const char* needle) -> bool {
        if (!needle[0]) return true;
        for (const char* h = haystack; *h; h++) {
            const char* a = h; const char* b = needle;
            while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; }
            if (!*b) return true;
        }
        return false;
    };

    // Scan function: populate entries from VGames database
    auto DoScan = [&]() {
        s_entryCount = 0;
        s_selectedIdx = -1;
        s_iconTexIdx = -1;
        if (s_iconTex) { glDeleteTextures(1, &s_iconTex); s_iconTex = 0; }

        VGames_Reload();

        // Reset dashboard icon cache so it picks up new/changed entries
        extern bool s_iconsLoaded;
        s_iconsLoaded = false;

        for (int i = 0; i < g_vgames.count && s_entryCount < TM_MAX_ENTRIES; i++) {
            VirtualGame& vg = g_vgames.games[i];
            if (!vg.valid) continue;
            TmEntry& e = s_entries[s_entryCount];
            e.vgIndex = i;
            strncpy(e.name, vg.name, sizeof(e.name) - 1); e.name[sizeof(e.name) - 1] = 0;
            strncpy(e.launch, vg.launch, sizeof(e.launch) - 1); e.launch[sizeof(e.launch) - 1] = 0;
            strncpy(e.titleID, vg.titleID, sizeof(e.titleID) - 1); e.titleID[sizeof(e.titleID) - 1] = 0;
            strncpy(e.drive, vg.drive, sizeof(e.drive) - 1); e.drive[sizeof(e.drive) - 1] = 0;
            strncpy(e.category, vg.category, sizeof(e.category) - 1); e.category[sizeof(e.category) - 1] = 0;
            s_entryCount++;
        }

        // Sort alphabetically by name
        std::sort(s_entries, s_entries + s_entryCount, [](const TmEntry& a, const TmEntry& b) {
            return strcasecmp(a.name, b.name) < 0;
        });
    };

    if (s_needsScan) { DoScan(); s_needsScan = false; }

    // Header
    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "UIX Title Maker");
    ImGui::SameLine(ImGui::GetWindowWidth() - 100);
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "F3 to close");
    ImGui::Separator();

    // xemu path setting
    {
        ImGui::Text("xemu:");
        ImGui::SameLine();
        // Reserve room for [Save][Find] + their gaps + a 12px right margin so
        // the rightmost button doesn't snug against the window edge.
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 150);
        ImGui::InputText("##xemupath", s_xemuPath, sizeof(s_xemuPath));
        ImGui::SameLine();
        if (ImGui::SmallButton("Save##xemu")) {
            SaveDesktopSettings();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Find##xemu")) {
            const char* tryPaths[] = {
#ifdef __APPLE__
                "/Applications/xemu.app",
                "/opt/homebrew/bin/xemu",
                "/usr/local/bin/xemu",
#elif defined(_WIN32)
                "C:\\Program Files\\xemu\\xemu.exe",
                "C:\\Program Files (x86)\\xemu\\xemu.exe",
#else
                "/usr/bin/xemu",
                "/usr/local/bin/xemu",
                "/snap/bin/xemu",
#endif
                NULL
            };
            bool found = false;
            for (int i = 0; tryPaths[i] && !found; i++) {
                struct stat st;
                if (stat(tryPaths[i], &st) == 0) {
                    strncpy(s_xemuPath, tryPaths[i], sizeof(s_xemuPath) - 1);
                    found = true;
                }
            }
            if (!found) {
                strncpy(s_statusMsg, "xemu not found in common locations", sizeof(s_statusMsg) - 1);
                s_statusTime = 3.0f;
            }
        }
        if (s_xemuPath[0] == 0)
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f), "Set xemu path to enable ISO launching");
    }

    // Steam install path setting
    {
        ImGui::Text("Steam:");
        ImGui::SameLine();
        // [Save][Find][Browse] + gaps + right margin.
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 220);
        ImGui::InputText("##steampath", s_steamPath, sizeof(s_steamPath));
        ImGui::SameLine();
        if (ImGui::SmallButton("Save##steam")) {
            SaveDesktopSettings();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Find##steam")) {
            const char* user = NULL;
#ifdef _WIN32
            user = getenv("USERNAME");
#else
            user = getenv("USER");
#endif
            if (!user) user = "user";

            char buf[512];
            const char* tryRoots[] = {
#ifdef __APPLE__
                "/Users/%s/Library/Application Support/Steam",
#elif defined(_WIN32)
                "C:\\Program Files (x86)\\Steam",
                "C:\\Program Files\\Steam",
                "D:\\Steam",
                "D:\\Program Files (x86)\\Steam",
                "E:\\Steam",
                "E:\\SteamLibrary",
#else
                "/home/%s/.steam/steam",
                "/home/%s/.local/share/Steam",
#endif
                NULL
            };
            bool found = false;
            for (int i = 0; tryRoots[i] && !found; i++) {
                snprintf(buf, sizeof(buf), tryRoots[i], user);
                struct stat st;
                if (stat(buf, &st) == 0) {
                    strncpy(s_steamPath, buf, sizeof(s_steamPath) - 1);
                    s_steamPath[sizeof(s_steamPath) - 1] = 0;
                    found = true;
                }
            }
            if (!found) {
                strncpy(s_statusMsg, "Steam not found in common locations; use Browse",
                        sizeof(s_statusMsg) - 1);
                s_statusTime = 3.0f;
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Browse##steam")) {
            if (s_steamPath[0]) s_steamBrowser.SetPwd(s_steamPath);
            s_steamBrowser.Open();
        }
        s_steamBrowser.Display();
        if (s_steamBrowser.HasSelected()) {
            std::string sel = s_steamBrowser.GetSelected().string();
            strncpy(s_steamPath, sel.c_str(), sizeof(s_steamPath) - 1);
            s_steamPath[sizeof(s_steamPath) - 1] = 0;
            s_steamBrowser.ClearSelected();
        }
        if (s_steamPath[0] == 0)
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                "Optional; leave blank to auto-detect Steam at import time");
    }
    ImGui::Separator();

    // Search and category filter
    {
        ImGui::Text("Search:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(180);
        ImGui::InputText("##search", s_searchFilter, sizeof(s_searchFilter));
        ImGui::SameLine();
        ImGui::Text("Category:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(130);
        const char* filterLabel = (s_filterCategoryIdx < 0) ? "All" : s_categories[s_filterCategoryIdx];
        if (ImGui::BeginCombo("##catfilter", filterLabel)) {
            if (ImGui::Selectable("All", s_filterCategoryIdx < 0))
                s_filterCategoryIdx = -1;
            for (int c = 0; c < s_categoryCount; c++) {
                if (ImGui::Selectable(s_categories[c], s_filterCategoryIdx == c))
                    s_filterCategoryIdx = c;
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::Text("%d titles", s_entryCount);
    }
    ImGui::Separator();

    // Split view: title list on left, editor on right
    float listWidth = 260.0f;
    ImGui::BeginChild("TitleList", ImVec2(listWidth, -ImGui::GetFrameHeightWithSpacing() * 1.6f), true);
    for (int i = 0; i < s_entryCount; i++) {
        TmEntry& e = s_entries[i];

        // Apply search filter
        if (s_searchFilter[0] && !CaseStrStr(e.name, s_searchFilter))
            continue;
        // Apply category filter
        if (s_filterCategoryIdx >= 0 && strcasecmp(e.category, s_categories[s_filterCategoryIdx]) != 0)
            continue;

        bool hasLaunch = e.launch[0] != 0;
        if (hasLaunch)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
        else
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));

        bool selected = (s_selectedIdx == i);
        char listLabel[256];
        snprintf(listLabel, sizeof(listLabel), "%s  [%s]", e.name, e.category);
        if (ImGui::Selectable(listLabel, selected)) {
            s_selectedIdx = i;
            // Populate editor fields from VGames entry
            VirtualGame& vg = g_vgames.games[e.vgIndex];
            strncpy(s_editName, vg.name, sizeof(s_editName) - 1);
            strncpy(s_editLaunch, vg.launch, sizeof(s_editLaunch) - 1);
            strncpy(s_editTitleID, vg.titleID, sizeof(s_editTitleID) - 1);
            s_editCategoryIdx = 0;
            for (int c = 0; c < s_categoryCount; c++) {
                if (strcasecmp(vg.category, s_categories[c]) == 0) { s_editCategoryIdx = c; break; }
            }
            s_iconTexIdx = -1; // force icon reload
        }
        ImGui::PopStyleColor();

        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("%s", e.name);
            if (hasLaunch)
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Launch: %s", e.launch);
            ImGui::Text("TitleID: %s  Drive: %s", e.titleID, e.drive);
            ImGui::EndTooltip();
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Editor panel
    ImGui::BeginChild("TitleEditor", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 1.6f), true);
    if (s_selectedIdx >= 0 && s_selectedIdx < s_entryCount) {
        TmEntry& sel = s_entries[s_selectedIdx];
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", sel.name);
        ImGui::Separator();

        // Icon preview
        if (s_iconTexIdx != s_selectedIdx) {
            s_iconTexIdx = s_selectedIdx;
            if (s_iconTex) { glDeleteTextures(1, &s_iconTex); s_iconTex = 0; }
            const char* iconPath = VGames_GetIconPath(sel.vgIndex);
            if (iconPath) {
                int w, h, ch;
                unsigned char* pixels = stbi_load(iconPath, &w, &h, &ch, 4);
                if (pixels) {
                    glGenTextures(1, &s_iconTex);
                    glBindTexture(GL_TEXTURE_2D, s_iconTex);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
                    stbi_image_free(pixels);
                }
            }
        }

        if (s_iconTex) {
            ImGui::Image((ImTextureID)(intptr_t)s_iconTex, ImVec2(64, 64));
            ImGui::SameLine();
        } else {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "[No icon]");
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("Browse Icon..")) {
            s_iconBrowser.Open();
        }
        ImGui::Spacing();

        // Edit fields
        ImGui::Text("Name:");
        ImGui::SameLine(80);
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##editname", s_editName, sizeof(s_editName));

        ImGui::Text("Launch:");
        ImGui::SameLine(80);
        ImGui::SetNextItemWidth(-60);
        ImGui::InputText("##editlaunch", s_editLaunch, sizeof(s_editLaunch));
        ImGui::SameLine();
        if (ImGui::SmallButton("Browse..")) {
            s_appBrowser.Open();
        }

        ImGui::Text("Category:");
        ImGui::SameLine(80);
        ImGui::SetNextItemWidth(150);
        if (ImGui::BeginCombo("##editcat", s_categories[s_editCategoryIdx])) {
            for (int c = 0; c < s_categoryCount; c++) {
                if (ImGui::Selectable(s_categories[c], s_editCategoryIdx == c))
                    s_editCategoryIdx = c;
            }
            ImGui::EndCombo();
        }

        ImGui::Text("Title ID:");
        ImGui::SameLine(80);
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s", s_editTitleID);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Action buttons
        if (ImGui::Button("Save", ImVec2(100, 0))) {
            // Sanitize before storing so the dashboard's text atlas can
            // render the result. Lossy on purpose: smart quotes, em
            // dashes, ™/®/©, emoji, CJK all get normalized or dropped
            // rather than rendered as tofu boxes.
            extern int Title_SanitizeName(const char*, char*, size_t);
            char cleanName[sizeof(s_editName)];
            Title_SanitizeName(s_editName, cleanName, sizeof(cleanName));
            if (cleanName[0]) {
                strncpy(s_editName, cleanName, sizeof(s_editName) - 1);
                s_editName[sizeof(s_editName) - 1] = '\0';
            }

            VGames_Update(sel.vgIndex, s_editName, s_editTitleID, s_editLaunch,
                          g_vgames.games[sel.vgIndex].drive, s_categories[s_editCategoryIdx]);
            VGames_Save(); UDataSynth_RebuildAll();

            // Update Icons.ini for Xbox-side compatibility
            {
                static char iconKeys[TM_MAX_ICONS][128];
                static char iconVals[TM_MAX_ICONS][128];
                int iconCount = TM_ReadIconsIni(iconKeys, iconVals);
                bool found = false;
                for (int i = 0; i < iconCount; i++) {
                    if (strcasecmp(iconKeys[i], s_editName) == 0) {
                        strncpy(iconVals[i], s_editTitleID, 127);
                        found = true; break;
                    }
                }
                if (!found && iconCount < TM_MAX_ICONS) {
                    strncpy(iconKeys[iconCount], s_editName, 127);
                    strncpy(iconVals[iconCount], s_editTitleID, 127);
                    iconCount++;
                }
                TM_WriteIconsIni(iconKeys, iconVals, iconCount);
            }

            // Refresh local entry cache
            strncpy(sel.name, s_editName, sizeof(sel.name) - 1);
            strncpy(sel.launch, s_editLaunch, sizeof(sel.launch) - 1);
            strncpy(sel.category, s_categories[s_editCategoryIdx], sizeof(sel.category) - 1);

            snprintf(s_statusMsg, sizeof(s_statusMsg), "Saved: %s", s_editName);
            s_statusTime = 3.0f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Test Launch", ImVec2(110, 0)) && s_editLaunch[0]) {
            // Fire-and-forget; don't tear down the dashboard for a test launch.
            // DesktopLaunch fills g_launchLastResult with a status string
            // ("Launched: ..." or "CreateProcess failed (error 2): ..."),
            // so we surface that verbatim instead of guessing success.
            extern void DesktopLaunch(const char*);
            extern char g_launchLastResult[256];
            g_launchLastResult[0] = 0;
            DesktopLaunch(s_editLaunch);
            snprintf(s_statusMsg, sizeof(s_statusMsg), "%s",
                     g_launchLastResult[0] ? g_launchLastResult : s_editLaunch);
            s_statusTime = 6.0f;
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.1f, 0.1f, 1.0f));
        if (ImGui::Button("Delete", ImVec2(80, 0))) {
            ImGui::OpenPopup("ConfirmDelete");
        }
        ImGui::PopStyleColor();

        if (ImGui::BeginPopup("ConfirmDelete")) {
            ImGui::Text("Delete '%s' from library?", sel.name);
            if (ImGui::Button("Yes, Delete")) {
                const char* delName = sel.name;
                VGames_DeleteByName(delName);

                // Remove from Icons.ini
                {
                    static char iconKeys[TM_MAX_ICONS][128];
                    static char iconVals[TM_MAX_ICONS][128];
                    int iconCount = TM_ReadIconsIni(iconKeys, iconVals);
                    int newCount = 0;
                    for (int i = 0; i < iconCount; i++) {
                        if (strcasecmp(iconKeys[i], delName) != 0) {
                            if (i != newCount) {
                                strncpy(iconKeys[newCount], iconKeys[i], 127);
                                strncpy(iconVals[newCount], iconVals[i], 127);
                            }
                            newCount++;
                        }
                    }
                    TM_WriteIconsIni(iconKeys, iconVals, newCount);
                }

                snprintf(s_statusMsg, sizeof(s_statusMsg), "Deleted: %s", delName);
                s_statusTime = 3.0f;
                s_needsScan = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    } else {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Select a title from the list,");
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "or create a new one below.");
    }
    ImGui::EndChild();

    // Bottom bar: new title + ISO import + Steam import
    ImGui::Separator();
    ImGui::Text("New:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180);
    ImGui::InputText("##newtitle", s_newTitleName, sizeof(s_newTitleName));
    ImGui::SameLine();
    if (ImGui::Button("Create") && s_newTitleName[0]) {
        // Generate titleID from current time
        char genID[16];
        snprintf(genID, sizeof(genID), "%08x", (unsigned)time(NULL));

        // Determine category from filter or default to "Games"
        const char* newCat = (s_filterCategoryIdx >= 0) ? s_categories[s_filterCategoryIdx] : "Games";

        // Run the new title through the same sanitize pass that Save uses
        // so the dashboard atlas can render it.
        extern int Title_SanitizeName(const char*, char*, size_t);
        char cleanNew[sizeof(s_newTitleName)];
        Title_SanitizeName(s_newTitleName, cleanNew, sizeof(cleanNew));
        if (!cleanNew[0]) {
            snprintf(s_statusMsg, sizeof(s_statusMsg),
                     "Title \"%s\" sanitizes to empty -- pick another", s_newTitleName);
            s_statusTime = 4.0f;
        } else {
            VGames_Add(cleanNew, genID, "", "E", newCat);
            VGames_Save(); UDataSynth_RebuildAll();
            snprintf(s_statusMsg, sizeof(s_statusMsg), "Created: %s (ID: %s)", cleanNew, genID);
            s_statusTime = 3.0f;
            s_newTitleName[0] = 0;
            s_needsScan = true;
        }
    }
    ImGui::SameLine();

    // Add ISO button — use the global theme so it matches Create / Import.
    if (ImGui::Button("Add ISO")) {
        ImGui::OpenPopup("AddISOPopup");
    }

    // ISO file browser + result window
    {
        if (ImGui::BeginPopup("AddISOPopup")) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
                "ISOs are launched via xemu, not natively in this app.");
            ImGui::Spacing();
            s_isoBrowser.Open();
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        s_isoBrowser.Display();

        if (s_isoBrowser.HasSelected()) {
            std::string selected = s_isoBrowser.GetSelected().string();
            strncpy(s_isoPath, selected.c_str(), sizeof(s_isoPath) - 1);
            s_isoBrowser.ClearSelected();

            if (s_isoPreviewTex) { glDeleteTextures(1, &s_isoPreviewTex); s_isoPreviewTex = 0; }
            XisoFreeTitleInfo(&s_isoInfo);
            s_isoInfo = XisoGetTitleInfo(s_isoPath);
            s_isoParsed = true;

            if (s_isoInfo.valid) {
                snprintf(s_isoStatus, sizeof(s_isoStatus), "Found: %s (ID: %08X)",
                         s_isoInfo.titleName, s_isoInfo.titleId);
                if (s_isoInfo.titleImageRGBA) {
                    glGenTextures(1, &s_isoPreviewTex);
                    glBindTexture(GL_TEXTURE_2D, s_isoPreviewTex);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                                 s_isoInfo.titleImageWidth, s_isoInfo.titleImageHeight,
                                 0, GL_RGBA, GL_UNSIGNED_BYTE, s_isoInfo.titleImageRGBA);
                }
                s_showIsoResult = true;
            } else {
                snprintf(s_isoStatus, sizeof(s_isoStatus), "ERROR: Could not parse ISO; not a valid Xbox disc image");
                s_showIsoResult = true;
            }
        }

        // Result window after parsing
        if (s_showIsoResult) {
            ImGui::SetNextWindowSize(ImVec2(480, 220), ImGuiCond_Always);
            if (ImGui::Begin("Add Xbox ISO", &s_showIsoResult, ImGuiWindowFlags_NoResize)) {
                if (s_isoParsed && s_isoInfo.valid) {
                    if (s_isoPreviewTex) {
                        ImGui::Image((ImTextureID)(intptr_t)s_isoPreviewTex, ImVec2(128, 128));
                        ImGui::SameLine();
                    }
                    ImGui::BeginGroup();
                    ImGui::Text("Title: %s", s_isoInfo.titleName);
                    ImGui::Text("Title ID: %08X", s_isoInfo.titleId);
                    ImGui::Spacing();
                    if (s_isoInfo.titleImageRGBA)
                        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Title image extracted");
                    else
                        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f), "No title image in XBE");
                    ImGui::EndGroup();

                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    if (ImGui::Button("Add to Library", ImVec2(140, 0))) {
                        // Sanitize name
                        char safeName[128];
                        strncpy(safeName, s_isoInfo.titleName, 127); safeName[127] = 0;
                        TM_SanitizeName(safeName, sizeof(safeName));

                        char titleIdHex[16];
                        snprintf(titleIdHex, sizeof(titleIdHex), "%08x", s_isoInfo.titleId);

                        // Build xemu launch command
                        char xemuLaunchCmd[1024] = "";
                        {
                            char xemuBin[512] = "";
                            if (s_xemuPath[0]) {
                                size_t plen = strlen(s_xemuPath);
                                if (plen > 4 && strcmp(s_xemuPath + plen - 4, ".app") == 0)
                                    snprintf(xemuBin, sizeof(xemuBin), "%s/Contents/MacOS/xemu", s_xemuPath);
                                else
                                    strncpy(xemuBin, s_xemuPath, sizeof(xemuBin) - 1);
                            }
                            if (xemuBin[0])
                                snprintf(xemuLaunchCmd, sizeof(xemuLaunchCmd),
                                    "\"%s\" -machine xbox,short-animation=on -dvd_path \"%s\"", xemuBin, s_isoPath);
                            else
                                snprintf(xemuLaunchCmd, sizeof(xemuLaunchCmd),
                                    "xemu -machine xbox,short-animation=on -dvd_path \"%s\"", s_isoPath);
                        }

                        VGames_Add(safeName, titleIdHex, xemuLaunchCmd, "E", "Games");
                        VGames_Save(); UDataSynth_RebuildAll();

                        // Save icon to VGAMES_ICONS/{titleID}.jpg
                        if (s_isoInfo.titleImageRGBA) {
                            TM_EnsureDir(VGAMES_ICONS);
                            char iconPath[512];
                            snprintf(iconPath, sizeof(iconPath), "%s/%s.jpg", VGAMES_ICONS, titleIdHex);
                            XisoSaveTitleImage(&s_isoInfo, iconPath);
                        }

                        snprintf(s_statusMsg, sizeof(s_statusMsg), "Added: %s", safeName);
                        s_statusTime = 3.0f;
                        s_needsScan = true;
                        s_showIsoResult = false;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(100, 0))) {
                        s_showIsoResult = false;
                    }
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", s_isoStatus);
                    ImGui::Spacing();
                    if (ImGui::Button("OK")) s_showIsoResult = false;
                }
            }
            ImGui::End();
        }
    }

    // Transliterate helper for Steam import
    auto TransliterateUTF8 = [](char* dst, const char* src, int maxLen) {
        // Map of 2-byte UTF-8 sequences (Latin-1 Supplement / Latin Extended-A) to ASCII
        // Format: { byte1, byte2, replacement char }
        static const struct { unsigned char b1, b2; char repl; } map[] = {
            {0xC3,0x80,'A'},{0xC3,0x81,'A'},{0xC3,0x82,'A'},{0xC3,0x83,'A'},{0xC3,0x84,'A'},{0xC3,0x85,'A'},
            {0xC3,0x87,'C'},
            {0xC3,0x88,'E'},{0xC3,0x89,'E'},{0xC3,0x8A,'E'},{0xC3,0x8B,'E'},
            {0xC3,0x8C,'I'},{0xC3,0x8D,'I'},{0xC3,0x8E,'I'},{0xC3,0x8F,'I'},
            {0xC3,0x90,'D'},{0xC3,0x91,'N'},
            {0xC3,0x92,'O'},{0xC3,0x93,'O'},{0xC3,0x94,'O'},{0xC3,0x95,'O'},{0xC3,0x96,'O'},{0xC3,0x98,'O'},
            {0xC3,0x99,'U'},{0xC3,0x9A,'U'},{0xC3,0x9B,'U'},{0xC3,0x9C,'U'},
            {0xC3,0x9D,'Y'},{0xC3,0x9F,'s'},
            {0xC3,0xA0,'a'},{0xC3,0xA1,'a'},{0xC3,0xA2,'a'},{0xC3,0xA3,'a'},{0xC3,0xA4,'a'},{0xC3,0xA5,'a'},
            {0xC3,0xA7,'c'},
            {0xC3,0xA8,'e'},{0xC3,0xA9,'e'},{0xC3,0xAA,'e'},{0xC3,0xAB,'e'},
            {0xC3,0xAC,'i'},{0xC3,0xAD,'i'},{0xC3,0xAE,'i'},{0xC3,0xAF,'i'},
            {0xC3,0xB0,'d'},{0xC3,0xB1,'n'},
            {0xC3,0xB2,'o'},{0xC3,0xB3,'o'},{0xC3,0xB4,'o'},{0xC3,0xB5,'o'},{0xC3,0xB6,'o'},{0xC3,0xB8,'o'},
            {0xC3,0xB9,'u'},{0xC3,0xBA,'u'},{0xC3,0xBB,'u'},{0xC3,0xBC,'u'},
            {0xC3,0xBD,'y'},{0xC3,0xBF,'y'},
        };
        int di = 0;
        for (const unsigned char* s = (const unsigned char*)src; *s && di < maxLen - 1; ) {
            if (*s < 0x80) {
                dst[di++] = (char)*s++;
            } else if (s[0] >= 0xC0 && s[0] < 0xE0 && s[1]) {
                // 2-byte UTF-8: check transliteration map.
                bool found = false;
                for (int m = 0; m < (int)(sizeof(map)/sizeof(map[0])); m++) {
                    if (s[0] == map[m].b1 && s[1] == map[m].b2) {
                        dst[di++] = map[m].repl;
                        found = true;
                        break;
                    }
                }
                if (!found) dst[di++] = '?';
                s += 2;
            } else if (s[0] >= 0xE0 && s[0] < 0xF0) {
                dst[di++] = '?'; s += 3; // 3-byte UTF-8 (CJK, etc.)
            } else if (s[0] >= 0xF0) {
                dst[di++] = '?'; s += 4; // 4-byte UTF-8 (emoji, etc.)
            } else {
                s++; // skip invalid continuation bytes
            }
        }
        dst[di] = 0;
    };

    // Import Steam Library
    ImGui::SameLine();
    if (ImGui::Button("Import Steam")) {
        struct SteamGame { int appid; char name[256]; };
        SteamGame games[256];
        int gameCount = 0;

        // Library discovery delegated to the Steam launcher module so
        // path detection (Flatpak, Debian's steam-installer, Steam
        // Deck, Windows registry, custom drives via libraryfolders.vdf)
        // lives in one place. Title Maker just iterates the result.
        char libDirs[16][512];
        int libCount = Steam_DiscoverLibraries(s_steamPath, libDirs, 16);

        auto ParseAppManifest = [&](const char* libDir, const char* fileName) {
            if (gameCount >= 256) return;
            if (strncmp(fileName, "appmanifest_", 12) != 0) return;
            char acfPath[512];
            snprintf(acfPath, sizeof(acfPath), "%s/%s", libDir, fileName);
            FILE* acf = fopen(acfPath, "r");
            if (!acf) return;
            int appid = 0;
            char name[256] = {};
            char acfLine[1024];
            while (fgets(acfLine, sizeof(acfLine), acf)) {
                char* aq = strstr(acfLine, "\"appid\"");
                if (aq) {
                    char* v1 = strchr(aq + 7, '"');
                    if (v1) { char* v2 = strchr(v1 + 1, '"'); if (v2) { *v2 = 0; appid = atoi(v1 + 1); } }
                }
                char* nq = strstr(acfLine, "\"name\"");
                if (nq) {
                    char* v1 = strchr(nq + 6, '"');
                    if (v1) { char* v2 = strchr(v1 + 1, '"'); if (v2) { *v2 = 0; strncpy(name, v1 + 1, 255); } }
                }
            }
            fclose(acf);
            if (appid > 0 && name[0]) {
                games[gameCount].appid = appid;
                TransliterateUTF8(games[gameCount].name, name, 256);
                gameCount++;
            }
        };

        for (int d = 0; d < libCount && gameCount < 256; d++) {
#ifdef _WIN32
            char searchBuf[512];
            snprintf(searchBuf, sizeof(searchBuf), "%s\\appmanifest_*", libDirs[d]);
            struct _finddata_t fd;
            intptr_t hFind = _findfirst(searchBuf, &fd);
            if (hFind != -1) {
                do { ParseAppManifest(libDirs[d], fd.name); } while (_findnext(hFind, &fd) == 0 && gameCount < 256);
                _findclose(hFind);
            }
#else
            DIR* dir = opendir(libDirs[d]);
            if (!dir) continue;
            struct dirent* ent;
            while ((ent = readdir(dir)) != NULL && gameCount < 256) {
                ParseAppManifest(libDirs[d], ent->d_name);
            }
            closedir(dir);
#endif
        }

        if (gameCount == 0) {
            snprintf(s_statusMsg, sizeof(s_statusMsg), "No Steam games found");
            s_statusTime = 3.0f;
        } else {
            int created = 0, skipped = 0, iconsDl = 0;

            TM_EnsureDir(VGAMES_ICONS);

            // Read existing Icons.ini so we can extend it with the names we
            // create here. harddrive.xap reads this file to map displayed
            // title names back to TitleIDs for icon lookup; without it, the
            // imported titles render with no icon even though the .jpg file
            // is sitting in Configs/icons/.
            static char iconKeys[TM_MAX_ICONS][128];
            static char iconVals[TM_MAX_ICONS][128];
            int iconCount = TM_ReadIconsIni(iconKeys, iconVals);

            for (int i = 0; i < gameCount; i++) {
                char safeName[256];
                strncpy(safeName, games[i].name, 255); safeName[255] = 0;
                TM_SanitizeName(safeName, sizeof(safeName));

                char titleID[16];
                snprintf(titleID, sizeof(titleID), "%08x", games[i].appid);

                // Mirror the Name->TitleID mapping into the Icons.ini buffer.
                // Done unconditionally so re-running the import backfills the
                // mapping for entries we'd otherwise skip via FindByName.
                {
                    bool found = false;
                    for (int k = 0; k < iconCount; k++) {
                        if (strcasecmp(iconKeys[k], safeName) == 0) {
                            strncpy(iconVals[k], titleID, 127);
                            iconVals[k][127] = 0;
                            found = true;
                            break;
                        }
                    }
                    if (!found && iconCount < TM_MAX_ICONS) {
                        strncpy(iconKeys[iconCount], safeName, 127);
                        iconKeys[iconCount][127] = 0;
                        strncpy(iconVals[iconCount], titleID, 127);
                        iconVals[iconCount][127] = 0;
                        iconCount++;
                    }
                }

                if (VGames_FindByName(safeName) >= 0) { skipped++; continue; }

                char launchCmd[256];
                snprintf(launchCmd, sizeof(launchCmd), "steam://rungameid/%d", games[i].appid);
                VGames_Add(safeName, titleID, launchCmd, "E", "Games");
                created++;

                // Download icon from Steam CDN
                char iconPath[512];
                snprintf(iconPath, sizeof(iconPath), "%s/%s.jpg", VGAMES_ICONS, titleID);
                struct stat ist;
                if (stat(iconPath, &ist) != 0) {
                    char tmpPath[512];
                    bool gotIcon = false;

                    const char* tryUrls[] = {
                        "logo.png", "icon.jpg", "logo.jpg",
                        "library_icon.jpg", "library_icon.png",
                        "header.jpg",
                        NULL
                    };
                    for (int u = 0; tryUrls[u] && !gotIcon; u++) {
                        bool isPng = strstr(tryUrls[u], ".png") != NULL;
                        char url[512];
                        snprintf(tmpPath, sizeof(tmpPath), "%s/%s_tmp%s", VGAMES_ICONS, titleID, isPng ? ".png" : ".jpg");
                        snprintf(url, sizeof(url),
                            "https://cdn.akamai.steamstatic.com/steam/apps/%d/%s",
                            games[i].appid, tryUrls[u]);
                        if (Http_GetToFile(url, tmpPath) && stat(tmpPath, &ist) == 0 && ist.st_size > 1000) {
#ifdef __APPLE__
                            // sips ships with macOS, resize to 128x128.
                            char cmd[1024];
                            snprintf(cmd, sizeof(cmd),
                                "sips -z 128 128 -s format jpeg \"%s\" --out \"%s\" >/dev/null 2>&1", tmpPath, iconPath);
                            bool resizeOk = (system(cmd) == 0);
#elif defined(_WIN32)
                            // No resize. CDN art is close to 128 and we scale at draw.
                            // stdio copy avoids a cmd.exe console flash.
                            bool resizeOk = TM_CopyFile(tmpPath, iconPath);
#else
                            // convert if installed, else cp.
                            char cmd[1024];
                            snprintf(cmd, sizeof(cmd),
                                "convert \"%s\" -resize 128x128! -quality 90 \"%s\" 2>/dev/null || cp \"%s\" \"%s\"", tmpPath, iconPath, tmpPath, iconPath);
                            bool resizeOk = (system(cmd) == 0);
#endif
                            if (resizeOk && stat(iconPath, &ist) == 0 && ist.st_size > 500)
                                gotIcon = true;
                        }
                        remove(tmpPath);
                    }

                    if (gotIcon) iconsDl++;
                    else remove(iconPath);
                }
            }

            VGames_Save(); UDataSynth_RebuildAll();
            VGames_Reload();
            TM_WriteIconsIni(iconKeys, iconVals, iconCount);

            snprintf(s_statusMsg, sizeof(s_statusMsg), "Imported %d new, %d existing, %d icons", created, skipped, iconsDl);
            s_statusTime = 5.0f;
            s_needsScan = true;
        }
    }

    // Bulk-cleanup button: re-runs Title_SanitizeName over every existing
    // games.ini entry. Useful for libraries imported before the sanitize
    // pipeline existed, or after refining the substitution table -- one
    // click reconciles the dashboard view to what the atlas can actually
    // render.
    ImGui::SameLine();
    if (ImGui::Button("Fix Names")) {
        extern int  Title_SanitizeName(const char*, char*, size_t);
        extern bool Title_NeedsSanitize(const char*);
        int fixed = 0;
        for (int i = 0; i < g_vgames.count; i++) {
            VirtualGame& g = g_vgames.games[i];
            if (!g.valid) continue;
            if (!Title_NeedsSanitize(g.name)) continue;
            char clean[sizeof(g.name)];
            Title_SanitizeName(g.name, clean, sizeof(clean));
            if (clean[0] && strcmp(clean, g.name) != 0) {
                strncpy(g.name, clean, sizeof(g.name) - 1);
                g.name[sizeof(g.name) - 1] = '\0';
                fixed++;
            }
        }
        if (fixed > 0) {
            VGames_Save(); UDataSynth_RebuildAll();
            VGames_Reload();
            s_needsScan = true;
        }
        snprintf(s_statusMsg, sizeof(s_statusMsg),
                 fixed > 0 ? "Sanitized %d title%s" : "All titles already clean",
                 fixed, fixed == 1 ? "" : "s");
        s_statusTime = 4.0f;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Re-run name sanitization across every games.ini entry.\n"
                          "Strips smart quotes, em dashes, ™/®/©, emoji, CJK\n"
                          "down to what the dashboard's text atlas can render.");

    // File browser callbacks
    s_iconBrowser.Display();
    if (s_iconBrowser.HasSelected() && s_selectedIdx >= 0 && s_selectedIdx < s_entryCount) {
        std::string iconSrc = s_iconBrowser.GetSelected().string();
        s_iconBrowser.ClearSelected();
        TmEntry& sel = s_entries[s_selectedIdx];

        TM_EnsureDir(VGAMES_ICONS);

        char dstPath[512];
        snprintf(dstPath, sizeof(dstPath), "%s/%s.jpg", VGAMES_ICONS, sel.titleID);
        TM_CopyFile(iconSrc.c_str(), dstPath);

        s_iconTexIdx = -1; // force icon reload
        snprintf(s_statusMsg, sizeof(s_statusMsg), "Icon saved: %s", dstPath);
        s_statusTime = 3.0f;
    }

    s_appBrowser.Display();
    if (s_appBrowser.HasSelected()) {
        std::string appPath = s_appBrowser.GetSelected().string();
        s_appBrowser.ClearSelected();
        strncpy(s_editLaunch, appPath.c_str(), sizeof(s_editLaunch) - 1);
    }

    // Status message
    if (s_statusTime > 0.0f) {
        s_statusTime -= ImGui::GetIO().DeltaTime;
        float alpha = s_statusTime < 1.0f ? s_statusTime : 1.0f;
        bool isError = strncmp(s_statusMsg, "ERROR", 5) == 0;
        ImVec4 col = isError ? ImVec4(1.0f, 0.3f, 0.3f, alpha) : ImVec4(0.3f, 1.0f, 0.3f, alpha);
        ImGui::TextColored(col, "%s", s_statusMsg);
    }

    ImGui::End();
}
