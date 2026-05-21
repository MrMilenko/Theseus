// skin_editor.cpp: see skin_editor.h.

#include "std.h"
#include "dashapp.h"
#include "theseus.h"
#include "settingsfile.h"
#include "skin_assets.h"
#include "skin_editor.h"
#include "imgui.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

extern char g_currentSkin[64];
extern void SaveDesktopSettings();
extern void ReloadSkin();

// Forward declaration; definition lives after the helpers below.
static bool IsStockReadOnly();

// Native OS file picker. Returns empty string on cancel / unsupported.
#if defined(_WIN32)
#  include <windows.h>
#  include <commdlg.h>
#endif

static std::string PickFile() {
#if defined(__APPLE__)
    FILE* p = popen(
        "osascript -e 'POSIX path of (choose file with prompt \"Select a replacement asset\")' 2>/dev/null",
        "r");
    if (!p) return "";
    char buf[2048]; std::string out;
    while (fgets(buf, sizeof(buf), p)) out += buf;
    pclose(p);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
        out.pop_back();
    return out;
#elif defined(_WIN32)
    OPENFILENAMEA ofn = {};
    char path[MAX_PATH] = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = "Select a replacement asset";
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&ofn)) return std::string(path);
    return "";
#elif defined(__linux__)
    FILE* p = popen("zenity --file-selection --title=\"Select a replacement asset\" 2>/dev/null", "r");
    if (!p) return "";
    char buf[2048]; std::string out;
    while (fgets(buf, sizeof(buf), p)) out += buf;
    pclose(p);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
        out.pop_back();
    return out;
#else
    return "";
#endif
}

// Copy the chosen file into the current skin folder as `slotName`. Backs
// up any existing file at that path to <name>.bak. Returns "" on success
// or an error string.
static std::string ApplyAssetPick(const std::string& srcPath, const char* slotName) {
    if (IsStockReadOnly()) return "Stock is read-only. Use 'New from Stock' first.";
    if (srcPath.empty() || !slotName || !*slotName) return "Nothing picked.";
    fs::path skinDir = fs::path("Data/Skins") / g_currentSkin;
    std::error_code ec;
    if (!fs::is_directory(skinDir, ec))
        return "Active skin folder is missing.";
    fs::path target = skinDir / slotName;
    if (fs::exists(target, ec)) {
        fs::path backup = skinDir / (std::string(slotName) + ".bak");
        fs::remove(backup, ec); // overwrite any previous backup
        fs::rename(target, backup, ec);
        if (ec) return std::string("Backup failed: ") + ec.message();
    }
    fs::copy_file(srcPath, target, fs::copy_options::overwrite_existing, ec);
    if (ec) return std::string("Copy failed: ") + ec.message();
    return "";
}

bool g_skinEditorOpen = false;

static const char* kSkinsRoot = "Data/Skins";

// Stock is the canonical reference skin. Refuse to overwrite it so users
// don't trash the fallback by accident. They have to "New from Stock"
// first to get a writable copy.
static bool IsStockReadOnly() {
    return strcasecmp(g_currentSkin, "Stock") == 0;
}

static std::vector<std::string> ListSkinDirs() {
    std::vector<std::string> out;
    std::error_code ec;
    if (!fs::is_directory(kSkinsRoot, ec)) return out;
    for (const auto& ent : fs::directory_iterator(kSkinsRoot, ec)) {
        if (ec) break;
        if (!ent.is_directory()) continue;
        out.push_back(ent.path().filename().string());
    }
    std::sort(out.begin(), out.end(),
        [](const std::string& a, const std::string& b) {
            // Stock always first; otherwise alphabetical.
            if (a == "Stock") return b != "Stock";
            if (b == "Stock") return false;
            return strcasecmp(a.c_str(), b.c_str()) < 0;
        });
    return out;
}

struct SkinnableEntry {
    const char* name;
    CMatInfo* info;
    int colorCount; // 1 or 2
};

struct SaveResult { bool ok; std::string msg; };

// Write the current in-memory material colors back to <Skin>.xbx.
// Single-color types land under [Name] Color=R,G,B,A; two-color types
// land under ColorA/ColorB.
static SaveResult SaveSkinColors() {
    if (IsStockReadOnly())
        return { false, "Stock is read-only. Use 'New from Stock' first." };
    CSettingsFile* skin = TheseusGetSkinSettings();
    if (!skin) return { false, "Skin settings not loaded." };

    auto fmt = [](DWORD c, char* buf, size_t n) {
        snprintf(buf, n, "%u,%u,%u,%u",
            (unsigned)((c >> 16) & 0xFF),
            (unsigned)((c >>  8) & 0xFF),
            (unsigned) (c        & 0xFF),
            (unsigned)((c >> 24) & 0xFF));
    };

    char buf[64];
    int written = 0;
    for (int i = 0; i < g_nMatInfoCount; i++) {
        CMatInfo* p = g_rgMatInfo[i];
        if (!p || !p->m_name) continue;
        int n = MatInfo_ColorCount(p);
        if (n == 1) {
            fmt(MatInfo_GetColor(p, 0), buf, sizeof(buf));
            skin->SetValue(p->m_name, _T("Color"), buf);
            written++;
        } else if (n == 2) {
            fmt(MatInfo_GetColor(p, 0), buf, sizeof(buf));
            skin->SetValue(p->m_name, _T("ColorA"), buf);
            fmt(MatInfo_GetColor(p, 1), buf, sizeof(buf));
            skin->SetValue(p->m_name, _T("ColorB"), buf);
            written++;
        }
    }
    if (!skin->Save()) return { false, "Save() failed writing skin file." };
    char done[128];
    snprintf(done, sizeof(done), "Saved %d materials to %s.xbx.",
             written, g_currentSkin);
    return { true, std::string(done) };
}

static std::vector<SkinnableEntry> CollectSkinnable() {
    std::vector<SkinnableEntry> out;
    for (int i = 0; i < g_nMatInfoCount; i++) {
        CMatInfo* p = g_rgMatInfo[i];
        if (!p) continue;
        int n = MatInfo_ColorCount(p);
        if (n <= 0) continue;
        out.push_back({ p->m_name, p, n });
    }
    std::sort(out.begin(), out.end(),
        [](const SkinnableEntry& a, const SkinnableEntry& b) {
            return strcasecmp(a.name, b.name) < 0;
        });
    return out;
}

// Switch to the given skin, persist the choice, reload assets.
static void ApplySkin(const char* name) {
    if (!name || !*name) return;
    strncpy(g_currentSkin, name, sizeof(g_currentSkin) - 1);
    g_currentSkin[sizeof(g_currentSkin) - 1] = 0;
    SaveDesktopSettings();
    ReloadSkin();
}

// Copy Stock to a new folder named `newName`, rename the Stock.xbx config
// inside so the loader can find it. Returns empty string on success, an
// error message otherwise.
static std::string CloneStockTo(const std::string& newName) {
    if (newName.empty()) return "Name cannot be empty.";
    for (char c : newName) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            return "Name contains an illegal character.";
    }
    fs::path dst = fs::path(kSkinsRoot) / newName;
    std::error_code ec;
    if (fs::exists(dst, ec)) return "A skin with that name already exists.";
    fs::path src = fs::path(kSkinsRoot) / "Stock";
    if (!fs::is_directory(src, ec)) return "Stock skin folder is missing.";

    fs::copy(src, dst, fs::copy_options::recursive, ec);
    if (ec) return std::string("Copy failed: ") + ec.message();

    // Rename the config file Stock.xbx -> <newName>.xbx so InitSkin's
    // "{skin}/{skin}.xbx" lookup finds it.
    fs::path oldCfg = dst / "Stock.xbx";
    fs::path newCfg = dst / (newName + ".xbx");
    if (fs::exists(oldCfg, ec)) {
        fs::rename(oldCfg, newCfg, ec);
        if (ec) return std::string("Config rename failed: ") + ec.message();
    }
    return "";
}

void RenderSkinEditor() {
    if (!g_skinEditorOpen) return;

    ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(360, 0), ImVec2(640, FLT_MAX));
    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize;
    if (!ImGui::Begin("Skin Editor", &g_skinEditorOpen, flags)) {
        ImGui::End();
        return;
    }

    // Rescan skin folders on demand so creating one in Finder shows up.
    static std::vector<std::string> s_skins;
    static double s_lastScan = 0.0;
    double now = ImGui::GetTime();
    if (s_skins.empty() || (now - s_lastScan) > 2.0) {
        s_skins = ListSkinDirs();
        s_lastScan = now;
    }

    ImGui::Spacing();
    ImGui::Text("Active skin");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.55f, 1.0f, 0.55f, 1.0f), "%s", g_currentSkin);
    if (IsStockReadOnly()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f),
                           "(read-only - create a copy below to edit)");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Skin picker
    int curIdx = 0;
    for (size_t i = 0; i < s_skins.size(); i++) {
        if (s_skins[i] == g_currentSkin) { curIdx = (int)i; break; }
    }
    ImGui::Text("Switch to:");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##skinpicker",
        s_skins.empty() ? "(no skins found)" : s_skins[curIdx].c_str())) {
        for (size_t i = 0; i < s_skins.size(); i++) {
            bool selected = ((int)i == curIdx);
            if (ImGui::Selectable(s_skins[i].c_str(), selected)) {
                if (s_skins[i] != g_currentSkin) ApplySkin(s_skins[i].c_str());
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    if (ImGui::Button("Reload current"))
        ReloadSkin();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // New from Stock
    static char s_newName[64] = "";
    static std::string s_msg;
    static bool s_msgIsError = false;

    ImGui::Text("New from Stock");
    ImGui::SetNextItemWidth(-90);
    ImGui::InputTextWithHint("##newname", "skin name", s_newName, sizeof(s_newName));
    ImGui::SameLine();
    if (ImGui::Button("Create", ImVec2(80, 0))) {
        s_msg = CloneStockTo(s_newName);
        s_msgIsError = !s_msg.empty();
        if (!s_msgIsError) {
            ApplySkin(s_newName);
            s_msg = std::string("Created and switched to '") + s_newName + "'.";
            s_newName[0] = 0;
            s_skins = ListSkinDirs();
        }
    }

    if (!s_msg.empty()) {
        ImGui::Spacing();
        ImVec4 c = s_msgIsError ? ImVec4(1.0f, 0.5f, 0.5f, 1.0f)
                                : ImVec4(0.55f, 1.0f, 0.55f, 1.0f);
        ImGui::TextColored(c, "%s", s_msg.c_str());
    }

    ImGui::Spacing();

    static std::vector<SkinnableEntry> s_entries;
    static double s_matLastScan = 0.0;
    static char s_matFilter[64] = "";

    if (s_entries.empty() || (now - s_matLastScan) > 1.0) {
        s_entries = CollectSkinnable();
        s_matLastScan = now;
    }

    bool matsOpen = ImGui::CollapsingHeader("Materials",
                                            ImGuiTreeNodeFlags_DefaultOpen);
    if (!matsOpen) {
        ImGui::SameLine();
        ImGui::TextDisabled(" (%d)", (int)s_entries.size());
    }
    if (matsOpen) {
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##matfilter", "Filter materials...",
                             s_matFilter, sizeof(s_matFilter));

    ImGui::BeginChild("MaterialList", ImVec2(0, 280), true);
    int shown = 0;
    auto pickerForIdx = [](const SkinnableEntry& e, int idx) {
        DWORD c = MatInfo_GetColor(e.info, idx);
        float rgba[4];
        rgba[0] = ((c >> 16) & 0xFF) / 255.0f;
        rgba[1] = ((c >>  8) & 0xFF) / 255.0f;
        rgba[2] = ( c        & 0xFF) / 255.0f;
        rgba[3] = ((c >> 24) & 0xFF) / 255.0f;
        char label[32];
        snprintf(label, sizeof(label), "##c%d_%p", idx, (void*)e.info);
        ImGui::SetNextItemWidth(60.0f);
        if (ImGui::ColorEdit4(label, rgba,
                ImGuiColorEditFlags_NoInputs |
                ImGuiColorEditFlags_AlphaPreviewHalf)) {
            DWORD nc =
                ((DWORD)(rgba[3] * 255.0f + 0.5f) << 24) |
                ((DWORD)(rgba[0] * 255.0f + 0.5f) << 16) |
                ((DWORD)(rgba[1] * 255.0f + 0.5f) <<  8) |
                ((DWORD)(rgba[2] * 255.0f + 0.5f));
            MatInfo_SetColor(e.info, idx, nc);
        }
    };

    // A material is "active" if its m_lastUsedFrame is within a short window
    // of the current frame. Some materials only render on certain scenes.
    int curFrame = (g_pD3DDev ? g_pD3DDev->m_frameNumber : 0);

    for (auto& e : s_entries) {
        if (s_matFilter[0]) {
            const char* hay = e.name ? e.name : "";
            const char* nd = s_matFilter;
            bool match = false;
            for (const char* p = hay; *p; p++) {
                const char* a = p; const char* b = nd;
                while (*a && *b && (tolower(*a) == tolower(*b))) { a++; b++; }
                if (!*b) { match = true; break; }
            }
            if (!match) continue;
        }
        ImGui::PushID(e.info);

        bool active = (e.info->m_lastUsedFrame >= 0) &&
                      (curFrame - e.info->m_lastUsedFrame) < 30;
        ImVec4 dotCol = active ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f)
                               : ImVec4(0.35f, 0.35f, 0.35f, 1.0f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(dotCol, "*");
        ImGui::SameLine();
        ImVec4 nameCol = active ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f)
                                : ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
        ImGui::TextColored(nameCol, "%s", e.name ? e.name : "(unnamed)");
        ImGui::SameLine(200);
        pickerForIdx(e, 0);
        if (e.colorCount > 1) {
            ImGui::SameLine();
            pickerForIdx(e, 1);
        }
        ImGui::PopID();
        shown++;
    }
    if (s_entries.empty()) {
        ImGui::TextDisabled("(materials not registered yet)");
    } else if (shown == 0) {
        ImGui::TextDisabled("(no matches)");
    }
    ImGui::EndChild();

    static std::string s_saveMsg;
    static bool        s_saveMsgIsError = false;

    ImGui::Spacing();
    if (ImGui::Button("Save Colors to Skin")) {
        SaveResult r = SaveSkinColors();
        s_saveMsg = r.msg;
        s_saveMsgIsError = !r.ok;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%d skinnable materials.", (int)s_entries.size());
    if (!s_saveMsg.empty()) {
        ImVec4 c = s_saveMsgIsError ? ImVec4(1.0f, 0.5f, 0.5f, 1.0f)
                                    : ImVec4(0.55f, 1.0f, 0.55f, 1.0f);
        ImGui::TextColored(c, "%s", s_saveMsg.c_str());
    }
    } // matsOpen

    // ----- Assets section -----
    ImGui::Spacing();

    static char s_assetFilter[64] = "";

    int totalRegistered = 0;
    for (int i = 0; kSkinnableAssets[i]; i++) totalRegistered++;

    bool assetsOpen = ImGui::CollapsingHeader("Assets");
    if (!assetsOpen) {
        ImGui::SameLine();
        ImGui::TextDisabled(" (%d)", totalRegistered);
    }
    if (assetsOpen) {
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##assetfilter", "Filter assets...",
                             s_assetFilter, sizeof(s_assetFilter));

    ImGui::BeginChild("AssetList", ImVec2(0, 220), true);
    int shownA = 0;
    int totalA = 0;
    static std::string s_assetMsg;
    static bool        s_assetMsgIsError = false;
    fs::path skinDir = fs::path(kSkinsRoot) / g_currentSkin;
    for (int i = 0; kSkinnableAssets[i]; i++) {
        totalA++;
        const char* name = kSkinnableAssets[i];
        if (s_assetFilter[0]) {
            const char* hay = name;
            const char* nd = s_assetFilter;
            bool match = false;
            for (const char* p = hay; *p; p++) {
                const char* a = p; const char* b = nd;
                while (*a && *b && (tolower(*a) == tolower(*b))) { a++; b++; }
                if (!*b) { match = true; break; }
            }
            if (!match) continue;
        }
        std::error_code ec;
        bool present = fs::exists(skinDir / name, ec);

        ImGui::PushID(i);
        char selLabel[8];
        snprintf(selLabel, sizeof(selLabel), "##sel");
        bool clicked = ImGui::Selectable(selLabel, false,
            ImGuiSelectableFlags_AllowItemOverlap, ImVec2(0, 0));
        ImGui::SameLine(0, 0);
        ImVec4 dotCol = present ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f)
                                : ImVec4(0.35f, 0.35f, 0.35f, 1.0f);
        ImGui::TextColored(dotCol, "*");
        ImGui::SameLine();
        ImVec4 nameCol = present ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f)
                                 : ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
        ImGui::TextColored(nameCol, "%s", name);
        ImGui::SameLine(280);
        ImGui::TextDisabled(present ? "(in skin)" : "(not in skin)");
        if (clicked) {
            std::string picked = PickFile();
            if (!picked.empty()) {
                std::string err = ApplyAssetPick(picked, name);
                if (err.empty()) {
                    s_assetMsg = std::string("Replaced ") + name;
                    s_assetMsgIsError = false;
                    ReloadSkin();
                } else {
                    s_assetMsg = err;
                    s_assetMsgIsError = true;
                }
            }
        }
        ImGui::PopID();
        shownA++;
    }
    if (totalA == 0) {
        ImGui::TextDisabled("(no skinnable assets registered)");
    } else if (shownA == 0) {
        ImGui::TextDisabled("(no matches)");
    }
    ImGui::EndChild();

    ImGui::TextDisabled("Click a row to replace the asset in this skin.");
    if (!s_assetMsg.empty()) {
        ImVec4 c = s_assetMsgIsError ? ImVec4(1.0f, 0.5f, 0.5f, 1.0f)
                                     : ImVec4(0.55f, 1.0f, 0.55f, 1.0f);
        ImGui::TextColored(c, "%s", s_assetMsg.c_str());
    }
    } // assetsOpen

    ImGui::End();
}
