// xap_editor.cpp: XAP script editor with syntax highlighting. Uses
// ImGuiColorTextEdit for a proper code editing experience; supports
// VRML97 / XAP syntax highlighting, undo / redo, find, line numbers.
// Desktop-only.

#include "std.h"
#include "xap_editor.h"
#include "dashapp.h"
#include "node.h"
#include "runner.h"
#include "panel_shared.h"

#include "imgui.h"
#include "TextEditor.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <io.h>
#include <direct.h>
#define strcasecmp _stricmp
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#else
#include <dirent.h>
#endif

#undef fopen
#undef _tfopen

// Implemented in group.cpp
extern "C" int CollectInlineNodes(CNode* pRoot, CInstance* pRootInst, void* outBuf, int maxEntries);
extern "C" bool ReloadInlineNode(CNode* pInlineNode, const char* xapText);

// ============================================================================
// State
// ============================================================================

bool g_xapEditorOpen = false;
bool g_extractedMode = false;

static TextEditor s_editor;
static bool s_editorInit = false;
static char g_xapEditorPath[512] = "";
static char g_xapEditorStatus[256] = "";
static float g_xapEditorStatusTime = 0;

static CNode* g_editingInlineNode = NULL;
static char   g_editingInlineUrl[256] = "";
static bool   g_reloadRequested = false;

// ============================================================================
// XAP/VRML Language Definition
// ============================================================================

static TextEditor::LanguageDefinition CreateXAPLanguageDef() {
    TextEditor::LanguageDefinition lang;
    lang.mName = "XAP";

    // VRML/XAP keywords
    static const char* const keywords[] = {
        "DEF", "USE", "PROTO", "EXTERNPROTO", "Script", "function", "var",
        "if", "else", "for", "while", "do", "return", "break", "continue",
        "new", "true", "false", "null", "sleep", "eval",
        "eventIn", "eventOut", "field", "exposedField",
        "IS", "ROUTE", "TO", "TRUE", "FALSE"
    };
    for (auto& k : keywords)
        lang.mKeywords.insert(k);

    // Node type identifiers (highlighted differently)
    static const char* const identifiers[] = {
        "Transform", "Shape", "Appearance", "Material", "MaxMaterial",
        "ImageTexture", "Group", "Inline", "Switch", "Billboard",
        "TimeSensor", "Viewpoint", "NavigationInfo", "Background",
        "AudioClip", "Sound", "PeriodicAudioGroup",
        "Text", "FontStyle", "Box", "Cone", "Cylinder", "Sphere",
        "Layer", "Screen", "Level", "Mesh", "MeshNode",
        "Joystick", "Config", "Translator", "Settings",
        "SavedGameGrid", "MemoryMonitor", "CopyDestination",
        "MusicCollection", "ScreenSaver", "Keyboard",
        "DVDPlayer", "DiscDrive", "LiveAccounts", "OverlayAlert",
        "Folder", "File", "Recovery",
        "IndexedLineSet", "Coordinate", "IndexedFaceSet",
        "TextureCoordinate", "Normal",
        "Panel", "HUD", "DotField", "StarField", "DeltaField"
    };
    for (auto& id : identifiers) {
        TextEditor::Identifier ident;
        ident.mDeclaration = "XAP Node Type";
        lang.mIdentifiers.insert(std::make_pair(std::string(id), ident));
    }

    // Single line comment
    lang.mCommentStart = "/*";
    lang.mCommentEnd = "*/";
    lang.mSingleLineComment = "#";

    lang.mCaseSensitive = true;
    lang.mAutoIndentation = true;

    // Token regex patterns
    lang.mTokenRegexStrings.push_back(std::make_pair<std::string, TextEditor::PaletteIndex>(
        "[ \\t]*#.*", TextEditor::PaletteIndex::Comment));
    lang.mTokenRegexStrings.push_back(std::make_pair<std::string, TextEditor::PaletteIndex>(
        "L?\\\"(\\\\.|[^\\\"])*\\\"", TextEditor::PaletteIndex::String));
    lang.mTokenRegexStrings.push_back(std::make_pair<std::string, TextEditor::PaletteIndex>(
        "[+-]?([0-9]+([.][0-9]*)?|[.][0-9]+)([eE][+-]?[0-9]+)?", TextEditor::PaletteIndex::Number));
    lang.mTokenRegexStrings.push_back(std::make_pair<std::string, TextEditor::PaletteIndex>(
        "[a-zA-Z_][a-zA-Z0-9_]*", TextEditor::PaletteIndex::Identifier));
    lang.mTokenRegexStrings.push_back(std::make_pair<std::string, TextEditor::PaletteIndex>(
        "[\\[\\]\\{\\}\\(\\)\\<\\>]", TextEditor::PaletteIndex::Punctuation));

    return lang;
}

// ============================================================================
// File I/O
// ============================================================================

void XapEditor_LoadFile(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        snprintf(g_xapEditorStatus, sizeof(g_xapEditorStatus), "Failed to open: %s", path);
        g_xapEditorStatusTime = (float)SDL_GetTicks() / 1000.0f;
        return;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    std::string text(size, '\0');
    fread(&text[0], 1, size, f);
    fclose(f);

    s_editor.SetText(text);

    strncpy(g_xapEditorPath, path, sizeof(g_xapEditorPath) - 1);
    snprintf(g_xapEditorStatus, sizeof(g_xapEditorStatus), "Loaded %ld bytes", size);
    g_xapEditorStatusTime = (float)SDL_GetTicks() / 1000.0f;
}

static void XapEditor_SaveFile() {
    if (!g_xapEditorPath[0]) return;
    std::string text = s_editor.GetText();
    FILE* f = fopen(g_xapEditorPath, "wb");
    if (!f) {
        snprintf(g_xapEditorStatus, sizeof(g_xapEditorStatus), "Failed to save!");
        g_xapEditorStatusTime = (float)SDL_GetTicks() / 1000.0f;
        return;
    }
    fwrite(text.c_str(), 1, text.size(), f);
    fclose(f);
    snprintf(g_xapEditorStatus, sizeof(g_xapEditorStatus), "Saved %zu bytes", text.size());
    g_xapEditorStatusTime = (float)SDL_GetTicks() / 1000.0f;
}

bool XapEditor_HasBuffer() {
    return g_xapEditorPath[0] != '\0';
}

bool XapEditor_ConsumeReloadRequest() {
    if (g_reloadRequested) { g_reloadRequested = false; return true; }
    return false;
}

void XapEditor_Cleanup() {}

// ============================================================================
// File Scanning
// ============================================================================

static void XapEditor_ScanDir(const char* dir, char fileList[][512], int* count, int maxFiles) {
#ifdef _WIN32
    char searchPath[512];
    snprintf(searchPath, sizeof(searchPath), "%s\\*", dir);
    struct _finddata_t fd;
    intptr_t hFind = _findfirst(searchPath, &fd);
    if (hFind == -1) return;
    do {
        if (fd.name[0] == '.') continue;
        if (*count >= maxFiles) break;
        char fullpath[512];
        snprintf(fullpath, sizeof(fullpath), "%s\\%s", dir, fd.name);
        if (fd.attrib & _A_SUBDIR)
            XapEditor_ScanDir(fullpath, fileList, count, maxFiles);
        else if (strlen(fd.name) > 4 && _stricmp(fd.name + strlen(fd.name) - 4, ".xap") == 0) {
            strncpy(fileList[*count], fullpath, 511);
            (*count)++;
        }
    } while (_findnext(hFind, &fd) == 0);
    _findclose(hFind);
#else
    DIR* d = opendir(dir);
    if (!d) return;
    struct dirent* entry;
    while ((entry = readdir(d)) != NULL && *count < maxFiles) {
        if (entry->d_name[0] == '.') continue;
        char fullpath[512];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", dir, entry->d_name);
        struct stat fst;
        if (stat(fullpath, &fst) == 0 && S_ISDIR(fst.st_mode))
            XapEditor_ScanDir(fullpath, fileList, count, maxFiles);
        else if (strlen(entry->d_name) > 4 && strcasecmp(entry->d_name + strlen(entry->d_name) - 4, ".xap") == 0) {
            strncpy(fileList[*count], fullpath, 511);
            (*count)++;
        }
    }
    closedir(d);
#endif
}

// ============================================================================
// Scene Reload
// ============================================================================

void ReloadSceneFromEditor() {
    if (!g_xapEditorPath[0]) return;

    XapEditor_SaveFile();

    std::string text = s_editor.GetText();

    if (g_editingInlineNode) {
        if (g_pD3DDev) {
            g_pD3DDev->m_inspectorSelectedNode = NULL;
            g_pD3DDev->m_inspectorHitID = -1;
        }
        bool ok = ReloadInlineNode(g_editingInlineNode, text.c_str());
        snprintf(g_xapEditorStatus, sizeof(g_xapEditorStatus),
                 ok ? "Inline reloaded: %s" : "Reload FAILED: %s", g_editingInlineUrl);
        g_xapEditorStatusTime = (float)SDL_GetTicks() / 1000.0f;
        return;
    }

    // Full scene rebuild
    if (g_pD3DDev) {
        g_pD3DDev->m_inspectorSelectedNode = NULL;
        g_pD3DDev->m_inspectorHitID = -1;
    }

    delete g_pObject;
    g_pObject = NULL;
    delete g_pClass;
    g_pClass = new CClass;

    extern bool g_bParseError;
    g_bParseError = false;

    char* parseBuf = (char*)malloc(text.size() + 1);
    memcpy(parseBuf, text.c_str(), text.size() + 1);

    const char* url = "Q:\\Xips\\default.xap";
    int cch = (int)strlen(url) + 1;
    g_pClass->m_url = new TCHAR[cch];
    memcpy(g_pClass->m_url, url, cch);

    extern TCHAR g_szCurDir[];
    _tcscpy(g_szCurDir, _T("Q:/Xips/default.xap"));

    bool ok = g_pClass->ParseFile(url, (const TCHAR*)parseBuf);
    free(parseBuf);

    if (!ok || g_bParseError) {
        snprintf(g_xapEditorStatus, sizeof(g_xapEditorStatus), "Parse error! Check console.");
        g_xapEditorStatusTime = (float)SDL_GetTicks() / 1000.0f;
        g_pObject = (CInstance*)g_pClass->CreateNode();

        // Mark error line in editor if we can find it
        // TODO: capture parse error line number and set error markers
        return;
    }

    g_pObject = (CInstance*)g_pClass->CreateNode();
    if (g_pObject)
        CallFunction(g_pObject, _T("initialize"));

    g_editingInlineNode = NULL;
    g_editingInlineUrl[0] = '\0';

    snprintf(g_xapEditorStatus, sizeof(g_xapEditorStatus), "Scene reloaded!");
    g_xapEditorStatusTime = (float)SDL_GetTicks() / 1000.0f;
}

// ============================================================================
// Editor Window (floating ImGui panel on main SDL window)
// ============================================================================

void RenderXAPEditor() {
    if (!g_xapEditorOpen) return;

    // Initialize editor on first use
    if (!s_editorInit) {
        auto lang = CreateXAPLanguageDef();
        s_editor.SetLanguageDefinition(lang);
        s_editor.SetPalette(TextEditor::GetDarkPalette());
        s_editor.SetShowWhitespaces(false);
        s_editor.SetTabSize(4);
        s_editorInit = true;
    }

    ImGui::SetNextWindowPos(ImVec2(300, 50), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);

    ImGuiWindowFlags flags = ImGuiWindowFlags_MenuBar;

    if (ImGui::Begin("XAP Editor", &g_xapEditorOpen, flags)) {

        // Cached file list
        static char xapFiles[256][512];
        static int xapFileCount = 0;
        static bool scanned = false;
        if (!scanned) {
            xapFileCount = 0;
            XapEditor_ScanDir("xboxfs/Q/Xips", xapFiles, &xapFileCount, 256);
            scanned = true;
        }

        // Menu bar
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Save", "Ctrl+S", false, g_xapEditorPath[0] != '\0'))
                    XapEditor_SaveFile();
                if (ImGui::MenuItem("Reload from Disk", NULL, false, g_xapEditorPath[0] != '\0'))
                    XapEditor_LoadFile(g_xapEditorPath);
                ImGui::Separator();
                if (ImGui::MenuItem("Reload Scene", "Ctrl+R", false, g_xapEditorPath[0] != '\0'))
                    g_reloadRequested = true;
                ImGui::Separator();
                if (ImGui::MenuItem("Rescan Files"))
                    scanned = false;
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Edit")) {
                if (ImGui::MenuItem("Undo", "Ctrl+Z", false, s_editor.CanUndo()))
                    s_editor.Undo();
                if (ImGui::MenuItem("Redo", "Ctrl+Y", false, s_editor.CanRedo()))
                    s_editor.Redo();
                ImGui::Separator();
                if (ImGui::MenuItem("Select All", "Ctrl+A"))
                    s_editor.SetSelection(TextEditor::Coordinates(), TextEditor::Coordinates(s_editor.GetTotalLines(), 0));
                ImGui::EndMenu();
            }

            // Scene menu: cache inline list so it doesn't rebuild every frame.
            {
                static InlineInfo s_cachedInlines[32];
                static int s_cachedInlineCount = 0;
                static bool s_sceneMenuWasOpen = false;

                bool sceneMenuOpen = ImGui::BeginMenu("Scene");
                if (sceneMenuOpen && !s_sceneMenuWasOpen) {
                    // Just opened: refresh the inline list.
                    if (g_pObject)
                        s_cachedInlineCount = CollectInlineNodes((CNode*)g_pObject,
                            (CInstance*)g_pObject, s_cachedInlines, 32);
                    else
                        s_cachedInlineCount = 0;
                }
                s_sceneMenuWasOpen = sceneMenuOpen;

                if (sceneMenuOpen) {
                    bool isCurrent = (g_editingInlineNode == NULL &&
                                     strstr(g_xapEditorPath, "default/default.xap") != NULL);
                    if (ImGui::MenuItem("Main Dashboard (root)", NULL, isCurrent)) {
                        XapEditor_LoadFile("xboxfs/Q/Xips/default/default.xap");
                        g_editingInlineNode = NULL;
                        g_editingInlineUrl[0] = '\0';
                    }
                    ImGui::Separator();

                    for (int i = 0; i < s_cachedInlineCount; i++) {
                        const char* name = s_cachedInlines[i].defName ? s_cachedInlines[i].defName : s_cachedInlines[i].url;
                        char label[256];
                        snprintf(label, sizeof(label), "%s  (%s)##scene_%d", name, s_cachedInlines[i].url, i);
                        bool cur = (g_editingInlineNode == (CNode*)s_cachedInlines[i].node);
                        if (ImGui::MenuItem(label, NULL, cur)) {
                            char xapPath[512];
                            snprintf(xapPath, sizeof(xapPath), "xboxfs/Q/Xips/default/%s", s_cachedInlines[i].url);
                            XapEditor_LoadFile(xapPath);
                            g_editingInlineNode = (CNode*)s_cachedInlines[i].node;
                            strncpy(g_editingInlineUrl, s_cachedInlines[i].url, sizeof(g_editingInlineUrl) - 1);
                        }
                    }
                    if (s_cachedInlineCount == 0)
                        ImGui::MenuItem("(no Inline nodes found)", NULL, false, false);

                    ImGui::EndMenu();
                }
            }

            // Browse menu
            if (ImGui::BeginMenu("Browse")) {
                char lastDir[512] = "";
                bool inSubmenu = false;
                for (int i = 0; i < xapFileCount; i++) {
                    const char* path = xapFiles[i];
                    const char* rel = path;
                    if (strncmp(rel, "xboxfs/Q/Xips/", 14) == 0) rel += 14;
                    char dir[512];
                    strncpy(dir, rel, sizeof(dir) - 1);
                    dir[sizeof(dir) - 1] = '\0';
                    char* lastSlash = strrchr(dir, '/');
                    if (lastSlash) *lastSlash = '\0';
                    else strcpy(dir, ".");
                    if (strcmp(dir, lastDir) != 0) {
                        if (inSubmenu) ImGui::EndMenu();
                        inSubmenu = ImGui::BeginMenu(dir);
                        strncpy(lastDir, dir, sizeof(lastDir) - 1);
                    }
                    if (inSubmenu) {
                        const char* fname = strrchr(rel, '/');
                        fname = fname ? fname + 1 : rel;
                        char browseLabel[256];
                        snprintf(browseLabel, sizeof(browseLabel), "%s##browse_%d", fname, i);
                        bool cur = (strcmp(g_xapEditorPath, path) == 0);
                        if (ImGui::MenuItem(browseLabel, NULL, cur))
                            XapEditor_LoadFile(path);
                    }
                }
                if (inSubmenu) ImGui::EndMenu();
                if (xapFileCount == 0)
                    ImGui::MenuItem("(no .xap files found)", NULL, false, false);
                ImGui::EndMenu();
            }

            ImGui::EndMenuBar();
        }

        // Status bar
        {
            const char* displayPath = g_xapEditorPath;
            if (strncmp(displayPath, "xboxfs/Q/Xips/", 14) == 0) displayPath += 14;

            if (g_xapEditorPath[0]) {
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", displayPath);
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No file loaded");
            }

            ImGui::SameLine();
            if (g_editingInlineNode)
                ImGui::TextColored(ImVec4(0.3f, 0.7f, 0.9f, 0.8f), "[Inline: %s]", g_editingInlineUrl);
            else if (g_xapEditorPath[0])
                ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 0.8f), "[Root Scene]");

            // Cursor position
            auto cpos = s_editor.GetCursorPosition();
            ImGui::SameLine(ImGui::GetWindowWidth() - 180);
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                "Ln %d, Col %d  |  %d lines", cpos.mLine + 1, cpos.mColumn + 1, s_editor.GetTotalLines());

            // Fading status message
            if (g_xapEditorStatus[0]) {
                float elapsed = (float)SDL_GetTicks() / 1000.0f - g_xapEditorStatusTime;
                if (elapsed < 3.0f) {
                    float alpha = elapsed < 2.0f ? 1.0f : (3.0f - elapsed);
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.4f, alpha), "  %s", g_xapEditorStatus);
                }
            }
        }

        ImGui::Separator();

        // The editor
        s_editor.Render("##xapcode");
    }
    ImGui::End();

    // Keyboard shortcuts
    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S) && g_xapEditorPath[0])
        XapEditor_SaveFile();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_R) && g_xapEditorPath[0])
        g_reloadRequested = true;
}
