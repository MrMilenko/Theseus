// hdd_browser.cpp: F5 Xbox HDD image browser (qcow2 / FATX). Read-
// only browser for xemu qcow2 HDD images; lets the user navigate
// the FATX filesystem, view partitions, browse directories, and
// export files to host.

#include "xbox_hdd.h"
#include "hdd_browser.h"
#include "imgui.h"
#include "imfilebrowser.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <string>
#include <algorithm>

#ifndef _WIN32
#include <dirent.h>
#include <sys/stat.h>
#include <pwd.h>
#include <unistd.h>
#else
#include <io.h>
#include <direct.h>
#include <sys/stat.h>
#include <windows.h>
#include <shlobj.h>
// windows.h #defines CreateFile/CreateDirectory as A/W macros, which
// collides with FATXReader::CreateFile / FATXReader::CreateDirectory.
// Drop the macros so the class methods compile under their real names.
#ifdef CreateFile
#undef CreateFile
#endif
#ifdef CreateDirectory
#undef CreateDirectory
#endif
#endif

bool g_hddBrowserOpen = false;

// ============================================================================
// Browser state
// ============================================================================

static XboxHDD s_hdd;
static char s_qcow2Path[512] = "";
static bool s_imageOpen = false;
static char s_statusMsg[256] = "";
static float s_statusTime = 0.0f;

// Partition info for display
struct PartitionDisplay {
    int index;              // index into s_hdd partitions
    uint64_t offset;
    uint64_t size;
    char label[16];         // "E:", "C:", "P3", etc.
    bool hasUDATA;
    bool hasTDATA;
    bool hasXboxDash;
};
static std::vector<PartitionDisplay> s_partitions;
static int s_selectedPartition = -1;

// Navigation state
struct DirLevel {
    int partitionIndex;
    uint32_t cluster;
    char name[64];
};
static std::vector<DirLevel> s_navStack;
static std::vector<FATXDirEntry> s_currentEntries;
static int s_selectedEntry = -1;

// Export state
static char s_exportPath[512] = "";
static bool s_exportPopupOpen = false;

// File browser for qcow2 selection
static ImGui::FileBrowser s_fileBrowser(ImGuiFileBrowserFlags_CloseOnEsc);
static bool s_fileBrowserInit = false;

// File browser for importing files into FATX
static ImGui::FileBrowser s_importBrowser(ImGuiFileBrowserFlags_CloseOnEsc);
static bool s_importBrowserInit = false;

// New directory name input
static char s_newDirName[43] = "";

// Export folder picker
static ImGui::FileBrowser s_exportBrowser(ImGuiFileBrowserFlags_SelectDirectory | ImGuiFileBrowserFlags_CloseOnEsc);
static bool s_exportBrowserInit = false;

// Import folder picker
static ImGui::FileBrowser s_importFolderBrowser(ImGuiFileBrowserFlags_SelectDirectory | ImGuiFileBrowserFlags_CloseOnEsc);
static bool s_importFolderBrowserInit = false;

// ============================================================================
// Helpers
// ============================================================================

static const char* GetDefaultQcow2Path() {
    static char path[512] = "";
    if (path[0]) return path;

#ifdef __APPLE__
    const char* home = getenv("HOME");
    if (home)
        snprintf(path, sizeof(path), "%s/Library/Application Support/xemu/xemu/xbox_hdd.qcow2", home);
#elif defined(__linux__)
    const char* home = getenv("HOME");
    if (home)
        snprintf(path, sizeof(path), "%s/.local/share/xemu/xemu/xbox_hdd.qcow2", home);
#elif defined(_WIN32)
    char* appdata = getenv("LOCALAPPDATA");
    if (appdata)
        snprintf(path, sizeof(path), "%s\\xemu\\xemu\\xbox_hdd.qcow2", appdata);
#endif
    return path;
}

static void SetStatus(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(s_statusMsg, sizeof(s_statusMsg), fmt, args);
    va_end(args);
    s_statusTime = 4.0f;
}

static const char* FormatSize(uint64_t bytes) {
    static char buf[4][32];
    static int idx = 0;
    char* out = buf[idx++ & 3];
    if (bytes >= (uint64_t)1024 * 1024 * 1024)
        snprintf(out, 32, "%.1f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    else if (bytes >= 1024 * 1024)
        snprintf(out, 32, "%.1f MB", bytes / (1024.0 * 1024.0));
    else if (bytes >= 1024)
        snprintf(out, 32, "%.1f KB", bytes / 1024.0);
    else
        snprintf(out, 32, "%llu B", (unsigned long long)bytes);
    return out;
}

// Match partitions to Xbox partition table by offset to get drive letters
static void AssignPartitionLabels() {
    for (auto& pd : s_partitions) {
        pd.label[0] = '\0';
        for (int i = 0; i < XBOX_PARTITION_COUNT; i++) {
            if (pd.offset == XBOX_PARTITION_TABLE[i].offset) {
                snprintf(pd.label, sizeof(pd.label), "%c:", XBOX_PARTITION_TABLE[i].letter);
                break;
            }
        }
        if (pd.label[0] == '\0')
            strncpy(pd.label, "??", sizeof(pd.label));
    }
}

// ============================================================================
// Open / scan / navigate
// ============================================================================

static void CloseImage() {
    s_hdd.Close();
    s_imageOpen = false;
    s_partitions.clear();
    s_navStack.clear();
    s_currentEntries.clear();
    s_selectedPartition = -1;
    s_selectedEntry = -1;
}

static void OpenImage() {
    CloseImage();

    if (!s_qcow2Path[0]) {
        SetStatus("No image path specified");
        return;
    }

    fprintf(stderr, "[HDD Browser] Opening: '%s'\n", s_qcow2Path);
    if (!s_hdd.OpenReadWrite(s_qcow2Path)) {
        SetStatus("Failed to open: %s", s_qcow2Path);
        return;
    }

    s_imageOpen = true;

    // Save path to global setting so SavedGameGrid can use it
    // Save path and reset SavedGameGrid so it re-enumerates from FATX
    extern char g_qcowPath[512];
    extern void SaveDesktopSettings();
    strncpy(g_qcowPath, s_qcow2Path, sizeof(g_qcowPath) - 1);
    SaveDesktopSettings();
    // Reset enumeration flags so SavedGameGrid picks up the new source
    extern bool s_titlesEnumerated;
    extern bool s_xboxHDDTried;
    s_titlesEnumerated = false;
    s_xboxHDDTried = false;

    // Build partition display list
    int count = s_hdd.GetPartitionCount();
    for (int i = 0; i < count; i++) {
        FATXReader* part = s_hdd.GetPartition(i);
        if (!part) continue;

        PartitionDisplay pd;
        memset(&pd, 0, sizeof(pd));
        pd.index = i;
        pd.offset = part->GetPartition().offset;
        pd.size = part->GetPartition().size;
        pd.label[0] = '\0';

        // Check root directory for recognizable content
        auto root = part->ReadRootDirectory();
        for (const auto& e : root) {
            if (e.IsDirectory()) {
                if (strcasecmp(e.name, "UDATA") == 0) pd.hasUDATA = true;
                if (strcasecmp(e.name, "TDATA") == 0) pd.hasTDATA = true;
            }
            if (e.IsFile() && strcasecmp(e.name, "xboxdash.xbe") == 0)
                pd.hasXboxDash = true;
        }

        s_partitions.push_back(pd);
    }

    // Assign drive letters: C:/E: by content, X:/Y:/Z: for the rest in order
    AssignPartitionLabels();

    // Auto-select the data partition
    int dataPart = s_hdd.FindDataPartition();
    if (dataPart >= 0) {
        // Find which display index corresponds to this partition
        for (int i = 0; i < (int)s_partitions.size(); i++) {
            if (s_partitions[i].index == dataPart) {
                s_selectedPartition = i;
                break;
            }
        }
    }

    SetStatus("Opened: %d partitions, %s virtual disk",
              count, FormatSize(s_hdd.GetQcow2DiskSize()));
}

static void NavigateTo(int partIdx, uint32_t cluster, const char* name) {
    DirLevel level;
    level.partitionIndex = partIdx;
    level.cluster = cluster;
    strncpy(level.name, name, sizeof(level.name) - 1);
    level.name[sizeof(level.name) - 1] = '\0';
    s_navStack.push_back(level);

    FATXReader* part = s_hdd.GetPartition(partIdx);
    if (part) {
        s_currentEntries = part->ReadDirectory(cluster);
        // Sort: directories first, then alphabetical
        std::sort(s_currentEntries.begin(), s_currentEntries.end(),
            [](const FATXDirEntry& a, const FATXDirEntry& b) {
                if (a.IsDirectory() != b.IsDirectory())
                    return a.IsDirectory();
                return strcasecmp(a.name, b.name) < 0;
            });
    } else {
        s_currentEntries.clear();
    }
    s_selectedEntry = -1;
}

static void NavigateToRoot(int displayIdx) {
    s_navStack.clear();
    s_currentEntries.clear();
    s_selectedEntry = -1;
    s_selectedPartition = displayIdx;

    if (displayIdx < 0 || displayIdx >= (int)s_partitions.size())
        return;

    int partIdx = s_partitions[displayIdx].index;
    NavigateTo(partIdx, 1, s_partitions[displayIdx].label);
}

static void NavigateUp() {
    if (s_navStack.size() <= 1) return;
    s_navStack.pop_back();

    auto& top = s_navStack.back();
    FATXReader* part = s_hdd.GetPartition(top.partitionIndex);
    if (part) {
        s_currentEntries = part->ReadDirectory(top.cluster);
        std::sort(s_currentEntries.begin(), s_currentEntries.end(),
            [](const FATXDirEntry& a, const FATXDirEntry& b) {
                if (a.IsDirectory() != b.IsDirectory())
                    return a.IsDirectory();
                return strcasecmp(a.name, b.name) < 0;
            });
    }
    s_selectedEntry = -1;
}

// ============================================================================
// Export file to host
// ============================================================================

static void ExportSelectedFile() {
    if (s_selectedEntry < 0 || s_selectedEntry >= (int)s_currentEntries.size())
        return;

    const FATXDirEntry& entry = s_currentEntries[s_selectedEntry];
    if (entry.IsDirectory()) {
        SetStatus("Cannot export a directory (yet)");
        return;
    }

    if (s_navStack.empty()) return;
    int partIdx = s_navStack.back().partitionIndex;

    auto data = s_hdd.ReadFile(partIdx, entry.firstCluster, entry.fileSize);
    if (data.empty() && entry.fileSize > 0) {
        SetStatus("Failed to read file data");
        return;
    }

    // Build export path
    char outPath[1024];
    if (s_exportPath[0]) {
        snprintf(outPath, sizeof(outPath), "%s/%s", s_exportPath, entry.name);
    } else {
        snprintf(outPath, sizeof(outPath), "%s", entry.name);
    }

    FILE* fp = fopen(outPath, "wb");
    if (!fp) {
        SetStatus("Failed to write: %s", outPath);
        return;
    }
    if (entry.fileSize > 0)
        fwrite(data.data(), 1, data.size(), fp);
    fclose(fp);

    SetStatus("Exported: %s (%s) -> %s", entry.name, FormatSize(entry.fileSize), outPath);
}

// ============================================================================
// Render
// ============================================================================

// Recursively import a host directory into FATX
static int ImportFolderRecursive(FATXReader* part, uint32_t fatxDirCluster,
                                  const char* hostPath, int depth = 0) {
    if (!part || depth > 10) return 0;  // safety limit

    int imported = 0;
    fprintf(stderr, "[Import] Scanning: '%s' (depth %d)\n", hostPath, depth);

    // Helper lambda: process a single directory entry
    auto ProcessEntry = [&](const char* entName, bool isDir, bool isFile) {
        if (entName[0] == '.') return;

        // Validate FATX name length (42 chars max)
        size_t nameLen = strlen(entName);
        if (nameLen > 42) {
            fprintf(stderr, "[Import] Skipping '%s' (name too long for FATX)\n", entName);
            return;
        }

        char fullPath[1024];
        snprintf(fullPath, sizeof(fullPath), "%s/%s", hostPath, entName);

        if (isDir) {
            if (part->CreateDirectory(fatxDirCluster, entName)) {
                auto entries = part->ReadDirectory(fatxDirCluster);
                for (const auto& e : entries) {
                    if (e.IsDirectory() && strcasecmp(e.name, entName) == 0) {
                        imported += ImportFolderRecursive(part, e.firstCluster, fullPath, depth + 1);
                        break;
                    }
                }
                imported++;
            } else {
                fprintf(stderr, "[Import] Failed to create dir: %s\n", entName);
            }
        } else if (isFile) {
            FILE* fp = fopen(fullPath, "rb");
            if (fp) {
                struct stat fst;
                stat(fullPath, &fst);
                uint32_t fileSize = (uint32_t)fst.st_size;
                std::vector<uint8_t> data(fileSize);
                if (fileSize > 0)
                    fread(data.data(), 1, fileSize, fp);
                fclose(fp);
                if (part->CreateFile(fatxDirCluster, entName, data.data(), fileSize)) {
                    imported++;
                } else {
                    fprintf(stderr, "[Import] Failed to write: %s\n", entName);
                }
            }
        }
    };

#ifdef _WIN32
    char searchBuf[1024];
    snprintf(searchBuf, sizeof(searchBuf), "%s\\*", hostPath);
    struct _finddata_t fd;
    intptr_t hFind = _findfirst(searchBuf, &fd);
    if (hFind == -1) {
        fprintf(stderr, "[Import] _findfirst failed: '%s'\n", hostPath);
        return 0;
    }
    do {
        bool isDir = (fd.attrib & _A_SUBDIR) != 0;
        bool isFile = !isDir;
        ProcessEntry(fd.name, isDir, isFile);
    } while (_findnext(hFind, &fd) == 0);
    _findclose(hFind);
#else
    DIR* dir = opendir(hostPath);
    if (!dir) {
        fprintf(stderr, "[Import] opendir failed: '%s' (errno %d)\n", hostPath, errno);
        return 0;
    }
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        char fullPath[1024];
        snprintf(fullPath, sizeof(fullPath), "%s/%s", hostPath, ent->d_name);
        struct stat st;
        if (stat(fullPath, &st) != 0) continue;
        ProcessEntry(ent->d_name, S_ISDIR(st.st_mode), S_ISREG(st.st_mode));
    }
    closedir(dir);
#endif
    return imported;
}

void RenderHDDBrowser() {
    if (!g_hddBrowserOpen) return;

    // Tick status timer
    if (s_statusTime > 0.0f)
        s_statusTime -= 1.0f / 60.0f;

    ImGui::SetNextWindowPos(ImVec2(40, 40), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(720, 560), ImGuiCond_FirstUseEver);

    ImGui::Begin("Xbox HDD Browser", &g_hddBrowserOpen);

    // ---- File browser init ----
    if (!s_fileBrowserInit) {
        s_fileBrowser.SetTitle("Select Xbox HDD Image");
        s_fileBrowser.SetTypeFilters({ ".qcow2", ".img", ".bin" });
        s_fileBrowserInit = true;
    }

    // ---- Image path + open ----
    ImGui::Text("Image:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 370);
    ImGui::InputText("##qcow2path", s_qcow2Path, sizeof(s_qcow2Path));
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) {
        s_fileBrowser.Open();
    }
    ImGui::SameLine();
    if (ImGui::Button(s_imageOpen ? "Reload" : "Open")) {
        OpenImage();
        if (s_imageOpen && s_selectedPartition >= 0)
            NavigateToRoot(s_selectedPartition);
    }
    if (s_imageOpen) {
        ImGui::SameLine();
        if (ImGui::Button("Close")) {
            CloseImage();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Create New")) {
        ImGui::OpenPopup("CreateNewHDD");
    }

    // Create new HDD popup
    if (ImGui::BeginPopup("CreateNewHDD")) {
        static char s_createPath[512] = "";
        if (!s_createPath[0]) {
            const char* home = getenv("HOME");
            if (home) snprintf(s_createPath, sizeof(s_createPath), "%s/xbox_hdd.qcow2", home);
        }
        ImGui::Text("Create a new Xbox HDD image with formatted FATX partitions.");
        ImGui::Spacing();
        ImGui::Text("Save as:");
        ImGui::SetNextItemWidth(400);
        ImGui::InputText("##createpath", s_createPath, sizeof(s_createPath));
        ImGui::Spacing();
        if (ImGui::Button("Create 8 GB Image")) {
            if (XboxHDD::Create(s_createPath)) {
                strncpy(s_qcow2Path, s_createPath, sizeof(s_qcow2Path) - 1);
                SetStatus("Created new Xbox HDD image");
                OpenImage();
                if (s_imageOpen && s_selectedPartition >= 0)
                    NavigateToRoot(s_selectedPartition);
            } else {
                SetStatus("Failed to create image");
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // File browser display + selection handling
    s_fileBrowser.Display();
    if (s_fileBrowser.HasSelected()) {
        std::string selected = s_fileBrowser.GetSelected().string();
        strncpy(s_qcow2Path, selected.c_str(), sizeof(s_qcow2Path) - 1);
        s_fileBrowser.ClearSelected();
        // Auto-open on selection
        OpenImage();
        if (s_imageOpen && s_selectedPartition >= 0)
            NavigateToRoot(s_selectedPartition);
    }

    // ---- Status bar ----
    if (s_statusTime > 0.0f) {
        float alpha = (s_statusTime < 1.0f) ? s_statusTime : 1.0f;
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, alpha), "%s", s_statusMsg);
    } else if (s_imageOpen) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
            "%d partitions | %s virtual disk | %s",
            s_hdd.GetPartitionCount(), FormatSize(s_hdd.GetQcow2DiskSize()),
            s_hdd.IsWritable() ? "Read-Write" : "Read-only");
    } else {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No image loaded");
    }

    if (!s_imageOpen) {
        ImGui::End();
        return;
    }

    ImGui::Separator();

    // ---- Split: partitions on left, directory listing on right ----
    float partitionWidth = 200.0f;

    // Left panel: partitions
    ImGui::BeginChild("Partitions", ImVec2(partitionWidth, 0), true);
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Partitions");
    ImGui::Separator();

    for (int i = 0; i < (int)s_partitions.size(); i++) {
        auto& pd = s_partitions[i];
        char label[128];
        const char* desc = "";
        if (pd.hasUDATA || pd.hasTDATA) desc = " (Saves)";
        else if (pd.hasXboxDash) desc = " (System)";
        snprintf(label, sizeof(label), "%s %s%s", pd.label, FormatSize(pd.size), desc);

        if (ImGui::Selectable(label, s_selectedPartition == i)) {
            NavigateToRoot(i);
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Right panel: directory listing
    ImGui::BeginChild("FileList", ImVec2(0, 0), true);

    // Breadcrumb path bar
    if (!s_navStack.empty()) {
        for (int i = 0; i < (int)s_navStack.size(); i++) {
            if (i > 0) ImGui::SameLine(0, 2);
            ImGui::PushID(i);

            char crumb[72];
            snprintf(crumb, sizeof(crumb), "%s >", s_navStack[i].name);

            if (ImGui::SmallButton(crumb)) {
                // Navigate back to this level
                while ((int)s_navStack.size() > i + 1)
                    s_navStack.pop_back();
                auto& top = s_navStack.back();
                FATXReader* part = s_hdd.GetPartition(top.partitionIndex);
                if (part) {
                    s_currentEntries = part->ReadDirectory(top.cluster);
                    std::sort(s_currentEntries.begin(), s_currentEntries.end(),
                        [](const FATXDirEntry& a, const FATXDirEntry& b) {
                            if (a.IsDirectory() != b.IsDirectory())
                                return a.IsDirectory();
                            return strcasecmp(a.name, b.name) < 0;
                        });
                }
                s_selectedEntry = -1;
            }
            ImGui::PopID();
        }
    }

    // ---- Toolbar ----
    {
        bool hasSelection = (s_selectedEntry >= 0 && s_selectedEntry < (int)s_currentEntries.size());
        bool selIsFile = hasSelection && s_currentEntries[s_selectedEntry].IsFile();
        bool writable = s_hdd.IsWritable();
        bool inDir = !s_navStack.empty();

        if (!selIsFile) ImGui::BeginDisabled();
        if (ImGui::SmallButton("Export")) {
            if (!s_exportBrowserInit) {
                s_exportBrowser.SetTitle("Select Export Folder");
                s_exportBrowserInit = true;
            }
            // If no export path set yet, open the folder picker
            if (!s_exportPath[0]) {
                s_exportBrowser.Open();
            } else {
                ExportSelectedFile();
            }
        }
        if (ImGui::IsItemHovered() && s_exportPath[0]) {
            ImGui::SetTooltip("Export to: %s\nRight-click to change", s_exportPath);
        }
        if (selIsFile && ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            s_exportBrowser.Open();
        }
        if (!selIsFile) ImGui::EndDisabled();

        ImGui::SameLine();
        if (!writable || !inDir) ImGui::BeginDisabled();
        if (ImGui::SmallButton("Import File")) {
            if (!s_importBrowserInit) {
                s_importBrowser.SetTitle("Select File to Import");
                s_importBrowserInit = true;
            }
            s_importBrowser.Open();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Import Folder")) {
            if (!s_importFolderBrowserInit) {
                s_importFolderBrowser.SetTitle("Select Folder to Import");
                s_importFolderBrowserInit = true;
            }
            s_importFolderBrowser.Open();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("New Dir")) {
            ImGui::OpenPopup("NewDirPopup");
        }
        if (!writable || !inDir) ImGui::EndDisabled();

        ImGui::SameLine();
        if (!writable || !hasSelection) ImGui::BeginDisabled();
        if (ImGui::SmallButton("Rename")) {
            ImGui::OpenPopup("RenamePopup");
            // Pre-fill with current name
            static char s_renameBuf[43];
            strncpy(s_renameBuf, s_currentEntries[s_selectedEntry].name, 42);
            s_renameBuf[42] = '\0';
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete")) {
            ImGui::OpenPopup("ConfirmDelete");
        }
        if (!writable || !hasSelection) ImGui::EndDisabled();

        // Show selected file info on same line
        if (hasSelection) {
            ImGui::SameLine();
            const auto& sel = s_currentEntries[s_selectedEntry];
            if (sel.IsFile()) {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                    "  %s | Cluster %u", FormatSize(sel.fileSize), sel.firstCluster);
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                    "  Directory | Cluster %u", sel.firstCluster);
            }
        }
    }
    ImGui::Separator();

    // ".." entry if we're deeper than root
    if (s_navStack.size() > 1) {
        if (ImGui::Selectable(".. (parent)", false, ImGuiSelectableFlags_AllowDoubleClick)) {
            if (ImGui::IsMouseDoubleClicked(0))
                NavigateUp();
        }
    }

    // Directory entries table
    if (ImGui::BeginTable("DirEntries", 3,
        ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_None, 3.0f);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_None, 1.0f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_None, 0.6f);
        ImGui::TableHeadersRow();

        for (int i = 0; i < (int)s_currentEntries.size(); i++) {
            const auto& e = s_currentEntries[i];
            ImGui::TableNextRow();
            ImGui::PushID(i);

            // Name column
            ImGui::TableSetColumnIndex(0);
            bool isDir = e.IsDirectory();
            char displayName[64];
            if (isDir)
                snprintf(displayName, sizeof(displayName), "[D] %s", e.name);
            else
                snprintf(displayName, sizeof(displayName), "    %s", e.name);

            ImGuiSelectableFlags flags = ImGuiSelectableFlags_SpanAllColumns |
                                          ImGuiSelectableFlags_AllowDoubleClick;
            if (ImGui::Selectable(displayName, s_selectedEntry == i, flags)) {
                s_selectedEntry = i;
                if (ImGui::IsMouseDoubleClicked(0) && isDir) {
                    int partIdx = s_navStack.back().partitionIndex;
                    NavigateTo(partIdx, e.firstCluster, e.name);
                }
            }

            // Size column
            ImGui::TableSetColumnIndex(1);
            if (isDir)
                ImGui::TextColored(ImVec4(0.5f, 0.7f, 1.0f, 1.0f), "<DIR>");
            else
                ImGui::Text("%s", FormatSize(e.fileSize));

            // Type column
            ImGui::TableSetColumnIndex(2);
            const char* ext = strrchr(e.name, '.');
            if (ext)
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s", ext + 1);

            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    // ---- Popups and file browser (must render every frame) ----
    {
    bool inDir = !s_navStack.empty();
    bool hasSelection = (s_selectedEntry >= 0 && s_selectedEntry < (int)s_currentEntries.size());

    // Export folder picker
    s_exportBrowser.Display();
    if (s_exportBrowser.HasSelected()) {
        std::string sel = s_exportBrowser.GetSelected().string();
        strncpy(s_exportPath, sel.c_str(), sizeof(s_exportPath) - 1);
        s_exportBrowser.ClearSelected();
        SetStatus("Export folder: %s", s_exportPath);
        // Now do the export if a file was selected
        if (s_selectedEntry >= 0 && s_selectedEntry < (int)s_currentEntries.size()
            && s_currentEntries[s_selectedEntry].IsFile()) {
            ExportSelectedFile();
        }
    }

    // Import folder browser
    s_importFolderBrowser.Display();
    if (s_importFolderBrowser.HasSelected()) {
        std::string folderPath = s_importFolderBrowser.GetSelected().string();
        s_importFolderBrowser.ClearSelected();
        fprintf(stderr, "[Import] Folder browser selected: '%s'\n", folderPath.c_str());

        if (inDir) {
            int partIdx = s_navStack.back().partitionIndex;
            uint32_t dirCluster = s_navStack.back().cluster;
            FATXReader* part = s_hdd.GetPartition(partIdx);
            if (part) {
                // Create the folder itself first, then import contents into it
                const char* folderName = strrchr(folderPath.c_str(), '/');
                if (!folderName) folderName = strrchr(folderPath.c_str(), '\\');
                folderName = folderName ? folderName + 1 : folderPath.c_str();

                uint32_t targetCluster = dirCluster;
                if (folderName[0] && strlen(folderName) <= 42) {
                    part->CreateDirectory(dirCluster, folderName);
                    // Find the new directory's cluster
                    auto entries = part->ReadDirectory(dirCluster);
                    for (const auto& e : entries) {
                        if (e.IsDirectory() && strcasecmp(e.name, folderName) == 0) {
                            targetCluster = e.firstCluster;
                            break;
                        }
                    }
                }

                int count = ImportFolderRecursive(part, targetCluster, folderPath.c_str());
                SetStatus("Imported %s/ (%d items)", folderName, count);
                // Refresh
                s_currentEntries = part->ReadDirectory(dirCluster);
                std::sort(s_currentEntries.begin(), s_currentEntries.end(),
                    [](const FATXDirEntry& a, const FATXDirEntry& b) {
                        if (a.IsDirectory() != b.IsDirectory()) return a.IsDirectory();
                        return strcasecmp(a.name, b.name) < 0;
                    });
                s_selectedEntry = -1;
            }
        }
    }

    s_importBrowser.Display();
    if (s_importBrowser.HasSelected()) {
        std::string hostPath = s_importBrowser.GetSelected().string();
        s_importBrowser.ClearSelected();

        // Read host file
        FILE* fp = fopen(hostPath.c_str(), "rb");
        if (fp && inDir) {
            fseek(fp, 0, SEEK_END);
            uint32_t fileSize = (uint32_t)ftell(fp);
            fseek(fp, 0, SEEK_SET);
            std::vector<uint8_t> fileData(fileSize);
            if (fileSize > 0)
                fread(fileData.data(), 1, fileSize, fp);
            fclose(fp);

            // Extract filename from path
            const char* fname = strrchr(hostPath.c_str(), '/');
            if (!fname) fname = strrchr(hostPath.c_str(), '\\');
            fname = fname ? fname + 1 : hostPath.c_str();

            int partIdx = s_navStack.back().partitionIndex;
            uint32_t dirCluster = s_navStack.back().cluster;
            FATXReader* part = s_hdd.GetPartition(partIdx);
            if (part && part->CreateFile(dirCluster, fname, fileData.data(), fileSize)) {
                SetStatus("Imported: %s (%s)", fname, FormatSize(fileSize));
                // Refresh directory listing
                s_currentEntries = part->ReadDirectory(dirCluster);
                std::sort(s_currentEntries.begin(), s_currentEntries.end(),
                    [](const FATXDirEntry& a, const FATXDirEntry& b) {
                        if (a.IsDirectory() != b.IsDirectory()) return a.IsDirectory();
                        return strcasecmp(a.name, b.name) < 0;
                    });
                s_selectedEntry = -1;
            } else {
                SetStatus("Failed to import: %s", fname);
            }
        } else if (fp) {
            fclose(fp);
        }
    }

    // New directory popup
    if (ImGui::BeginPopup("NewDirPopup")) {
        ImGui::Text("Directory name:");
        ImGui::SetNextItemWidth(200);
        ImGui::InputText("##newdir", s_newDirName, sizeof(s_newDirName));
        if (ImGui::Button("Create") && s_newDirName[0] && inDir) {
            int partIdx = s_navStack.back().partitionIndex;
            uint32_t dirCluster = s_navStack.back().cluster;
            FATXReader* part = s_hdd.GetPartition(partIdx);
            if (part && part->CreateDirectory(dirCluster, s_newDirName)) {
                SetStatus("Created directory: %s", s_newDirName);
                s_currentEntries = part->ReadDirectory(dirCluster);
                std::sort(s_currentEntries.begin(), s_currentEntries.end(),
                    [](const FATXDirEntry& a, const FATXDirEntry& b) {
                        if (a.IsDirectory() != b.IsDirectory()) return a.IsDirectory();
                        return strcasecmp(a.name, b.name) < 0;
                    });
                s_selectedEntry = -1;
                s_newDirName[0] = '\0';
            } else {
                SetStatus("Failed to create directory");
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            s_newDirName[0] = '\0';
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Rename popup
    if (ImGui::BeginPopup("RenamePopup")) {
        static char s_renameBuf[43] = "";
        if (hasSelection && s_renameBuf[0] == '\0')
            strncpy(s_renameBuf, s_currentEntries[s_selectedEntry].name, 42);
        ImGui::Text("New name:");
        ImGui::SetNextItemWidth(250);
        ImGui::InputText("##rename", s_renameBuf, sizeof(s_renameBuf));
        if (ImGui::Button("Rename") && s_renameBuf[0] && hasSelection && inDir) {
            int partIdx = s_navStack.back().partitionIndex;
            uint32_t dirCluster = s_navStack.back().cluster;
            FATXReader* part = s_hdd.GetPartition(partIdx);
            const char* oldName = s_currentEntries[s_selectedEntry].name;
            if (part && part->RenameEntry(dirCluster, oldName, s_renameBuf)) {
                SetStatus("Renamed: %s -> %s", oldName, s_renameBuf);
                s_currentEntries = part->ReadDirectory(dirCluster);
                std::sort(s_currentEntries.begin(), s_currentEntries.end(),
                    [](const FATXDirEntry& a, const FATXDirEntry& b) {
                        if (a.IsDirectory() != b.IsDirectory()) return a.IsDirectory();
                        return strcasecmp(a.name, b.name) < 0;
                    });
                s_selectedEntry = -1;
            } else {
                SetStatus("Failed to rename");
            }
            s_renameBuf[0] = '\0';
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            s_renameBuf[0] = '\0';
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Delete confirmation popup
    if (ImGui::BeginPopup("ConfirmDelete")) {
        if (hasSelection) {
            const auto& sel = s_currentEntries[s_selectedEntry];
            ImGui::Text("Delete \"%s\"?", sel.name);
            if (sel.IsDirectory())
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f), "Warning: directory contents will be lost");
            if (ImGui::Button("Yes, Delete")) {
                int partIdx = s_navStack.back().partitionIndex;
                uint32_t dirCluster = s_navStack.back().cluster;
                FATXReader* part = s_hdd.GetPartition(partIdx);
                if (part && part->DeleteEntry(dirCluster, sel.name)) {
                    SetStatus("Deleted: %s", sel.name);
                    s_currentEntries = part->ReadDirectory(dirCluster);
                    std::sort(s_currentEntries.begin(), s_currentEntries.end(),
                        [](const FATXDirEntry& a, const FATXDirEntry& b) {
                            if (a.IsDirectory() != b.IsDirectory()) return a.IsDirectory();
                            return strcasecmp(a.name, b.name) < 0;
                        });
                    s_selectedEntry = -1;
                } else {
                    SetStatus("Failed to delete");
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
                ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    } // end popups scope

    ImGui::EndChild();

    ImGui::End();
}
