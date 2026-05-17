// preloader.cpp: desktop preloader UI and XIP extraction. Shows the
// boot splash and unpacks bundled XIP archives into xboxfs/ on first
// run. Desktop-only.

#include "preloader.h"
#include "std.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#ifndef THESEUS_USE_BGFX
#include "imgui_impl_opengl3.h"
#endif
#include <SDL.h>
#include <sys/stat.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cmath>

#ifdef _WIN32
#include <io.h>
#include <direct.h>
#else
#include <dirent.h>
#endif

#ifdef THESEUS_USE_BGFX
#include <bgfx/bgfx.h>
#elif defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

// ============================================================================
// XIP Extraction (ported from xiptool.c)
// ============================================================================

#pragma pack(push, 1)
#define XTOOL_XIP_MAGIC 0x30504958  /* "XIP0" */

typedef struct { DWORD magic; DWORD dataStart; WORD fileCount; WORD nameCount; DWORD dataSize; } XTOOL_XIPHEADER;
typedef struct { DWORD offset; DWORD size; DWORD type; DWORD timestamp; } XTOOL_FILEDATA;
typedef struct { WORD fileDataIndex; WORD nameOffset; } XTOOL_FILENAME;
typedef struct { DWORD primitiveType; DWORD faceCount; DWORD fvf; DWORD vertexStride; DWORD vertexCount; DWORD indexCount; } XTOOL_MESHFILEHEADER;

#pragma pack(pop)

#define XTOOL_TYPE_MESH_REFERENCE 4
#define XTOOL_TYPE_INDEXBUFFER    5
#define XTOOL_TYPE_VERTEXBUFFER   6
#define XTOOL_MAX_MESHBUF 10

static void Preloader_Mkdirp(const char* path) {
    char tmp[1024];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
#ifdef _WIN32
            _mkdir(tmp);
#else
            mkdir(tmp, 0755);
#endif
            *p = '/';
        }
    }
#ifdef _WIN32
    _mkdir(tmp);
#else
    mkdir(tmp, 0755);
#endif
}

static const char* Preloader_FindName(WORD idx, XTOOL_FILENAME* filenames, int nameCount,
                                       const char* names, long namesSize) {
    for (int i = 0; i < nameCount; i++) {
        if (filenames[i].fileDataIndex == idx && filenames[i].nameOffset < namesSize)
            return &names[filenames[i].nameOffset];
    }
    return NULL;
}

static bool Preloader_EndsWith(const char* str, const char* suffix) {
    size_t slen = strlen(str), xlen = strlen(suffix);
    if (xlen > slen) return false;
    return strcasecmp(str + slen - xlen, suffix) == 0;
}

// Each mesh reference becomes one standalone .xm. We slice the shared IB by
// [first_index, first_index + prim_count*3), find the contiguous vertex range
// it touches, copy those vertices, and rebase the indices to start at zero.
// Mirrors the meshref-keyed extraction in tools/xiptool.c.
int Preloader_ExtractXIP(const char* xipPath, const char* outDir) {
    FILE* f = fopen(xipPath, "rb");
    if (!f) return -1;

    XTOOL_XIPHEADER hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1 || hdr.magic != XTOOL_XIP_MAGIC) {
        fclose(f); return -1;
    }

    XTOOL_FILEDATA* filedata = (XTOOL_FILEDATA*)calloc(hdr.fileCount, sizeof(XTOOL_FILEDATA));
    fread(filedata, sizeof(XTOOL_FILEDATA), hdr.fileCount, f);

    XTOOL_FILENAME* filenames = (XTOOL_FILENAME*)calloc(hdr.nameCount, sizeof(XTOOL_FILENAME));
    fread(filenames, sizeof(XTOOL_FILENAME), hdr.nameCount, f);

    long namesStart = (long)ftell(f);
    long namesSize = (long)hdr.dataStart - namesStart;
    if (namesSize < 0) namesSize = 0;
    char* names = (char*)calloc(1, namesSize + 1);
    if (namesSize > 0) fread(names, 1, namesSize, f);

    Preloader_Mkdirp(outDir);

    struct LoadedBuf {
        BYTE* ib;
        DWORD ibSize;
        BYTE* vb;
        DWORD vbSize;
        DWORD vertexCount;
        DWORD fvf;
        DWORD vertexStride;
    };
    LoadedBuf bufs[XTOOL_MAX_MESHBUF];
    memset(bufs, 0, sizeof(bufs));
    int nIBs = 0, nVBs = 0;

    for (int i = 0; i < hdr.fileCount; i++) {
        long dataPos = (long)hdr.dataStart + (long)filedata[i].offset;
        if (filedata[i].type == XTOOL_TYPE_INDEXBUFFER && nIBs < XTOOL_MAX_MESHBUF) {
            fseek(f, dataPos, SEEK_SET);
            bufs[nIBs].ib = (BYTE*)malloc(filedata[i].size);
            bufs[nIBs].ibSize = filedata[i].size;
            fread(bufs[nIBs].ib, 1, filedata[i].size, f);
            nIBs++;
        } else if (filedata[i].type == XTOOL_TYPE_VERTEXBUFFER && nVBs < XTOOL_MAX_MESHBUF) {
            fseek(f, dataPos, SEEK_SET);
            bufs[nVBs].vb = (BYTE*)malloc(filedata[i].size);
            bufs[nVBs].vbSize = filedata[i].size;
            fread(bufs[nVBs].vb, 1, filedata[i].size, f);
            if (filedata[i].size >= 8) {
                memcpy(&bufs[nVBs].vertexCount, bufs[nVBs].vb,     sizeof(DWORD));
                memcpy(&bufs[nVBs].fvf,         bufs[nVBs].vb + 4, sizeof(DWORD));
                if (bufs[nVBs].vertexCount > 0)
                    bufs[nVBs].vertexStride = (filedata[i].size - 8) / bufs[nVBs].vertexCount;
            }
            nVBs++;
        }
    }

    int nBufs = (nIBs < nVBs) ? nIBs : nVBs;

    int xmCount = 0;
    for (int i = 0; i < hdr.fileCount; i++) {
        if (filedata[i].type != XTOOL_TYPE_MESH_REFERENCE) continue;

        DWORD bufIdx   = filedata[i].offset >> 24;
        DWORD firstIdx = filedata[i].offset & 0x00FFFFFF;
        DWORD primCount = filedata[i].size;
        DWORD indexCount = primCount * 3;

        const char* name = Preloader_FindName((WORD)i, filenames, hdr.nameCount, names, namesSize);
        if (!name) continue;
        if ((int)bufIdx >= nBufs) continue;

        LoadedBuf* mb = &bufs[bufIdx];
        if (mb->vertexStride == 0 || mb->ibSize == 0) continue;

        DWORD totalIndices = mb->ibSize / sizeof(WORD);
        if (firstIdx + indexCount > totalIndices) continue;

        WORD* sliceIndices = ((WORD*)mb->ib) + firstIdx;

        WORD minIdx = 0xFFFF, maxIdx = 0;
        if (indexCount == 0) { minIdx = 0; maxIdx = 0; }
        else {
            for (DWORD k = 0; k < indexCount; k++) {
                WORD v = sliceIndices[k];
                if (v < minIdx) minIdx = v;
                if (v > maxIdx) maxIdx = v;
            }
        }
        if (maxIdx >= mb->vertexCount) continue;

        DWORD vertCount = (DWORD)(maxIdx - minIdx + 1);

        char xmPath[1024];
        snprintf(xmPath, sizeof(xmPath), "%s/%s", outDir, name);

        FILE* xmf = fopen(xmPath, "wb");
        if (!xmf) continue;

        XTOOL_MESHFILEHEADER mfh = { 4, primCount, mb->fvf, mb->vertexStride, vertCount, indexCount };
        fwrite(&mfh, sizeof(mfh), 1, xmf);

        BYTE* vertexData = mb->vb + 8 + (DWORD)minIdx * mb->vertexStride;
        fwrite(vertexData, 1, vertCount * mb->vertexStride, xmf);

        if (minIdx == 0) {
            fwrite(sliceIndices, sizeof(WORD), indexCount, xmf);
        } else {
            WORD* remapped = (WORD*)malloc(indexCount * sizeof(WORD));
            for (DWORD k = 0; k < indexCount; k++)
                remapped[k] = (WORD)(sliceIndices[k] - minIdx);
            fwrite(remapped, sizeof(WORD), indexCount, xmf);
            free(remapped);
        }

        fclose(xmf);
        xmCount++;
    }

    int extracted = 0;
    for (int i = 0; i < hdr.nameCount; i++) {
        WORD idx = filenames[i].fileDataIndex;
        if (idx >= hdr.fileCount) continue;
        XTOOL_FILEDATA* fd = &filedata[idx];
        if (fd->type == XTOOL_TYPE_MESH_REFERENCE || fd->type == XTOOL_TYPE_INDEXBUFFER || fd->type == XTOOL_TYPE_VERTEXBUFFER)
            continue;
        const char* name = (filenames[i].nameOffset < namesSize) ? &names[filenames[i].nameOffset] : "unknown";
        if (name[0] == '~') continue;
        fseek(f, (long)hdr.dataStart + (long)fd->offset, SEEK_SET);
        BYTE* data = (BYTE*)malloc(fd->size);
        fread(data, 1, fd->size, f);
        char outPath[1024];
        snprintf(outPath, sizeof(outPath), "%s/%s", outDir, name);
        FILE* out = fopen(outPath, "wb");
        if (out) { fwrite(data, 1, fd->size, out); fclose(out); extracted++; }
        free(data);
    }

    for (int b = 0; b < XTOOL_MAX_MESHBUF; b++) { free(bufs[b].ib); free(bufs[b].vb); }
    free(names); free(filenames); free(filedata);
    fclose(f);
    return extracted + xmCount;
}

static int Preloader_ExtractAllXIPs(const char* xipsDir, char statusBuf[], int statusBufSize) {
    int totalXips = 0, count = 0;

#ifdef _WIN32
    // Windows: use _findfirst/_findnext
    char searchPath[1024];
    snprintf(searchPath, sizeof(searchPath), "%s\\*.xip", xipsDir);
    struct _finddata_t fd;
    intptr_t hFind;

    // Count pass
    hFind = _findfirst(searchPath, &fd);
    if (hFind == -1) {
        snprintf(statusBuf, statusBufSize, "Cannot open: %s", xipsDir);
        return -1;
    }
    do { totalXips++; } while (_findnext(hFind, &fd) == 0);
    _findclose(hFind);

    // Extract pass
    hFind = _findfirst(searchPath, &fd);
    if (hFind != -1) {
        do {
            char xipPath[1024], outDir[1024];
            snprintf(xipPath, sizeof(xipPath), "%s\\%s", xipsDir, fd.name);
            snprintf(outDir, sizeof(outDir), "%s\\%s", xipsDir, fd.name);
            char* dot = strrchr(outDir + strlen(xipsDir) + 1, '.');
            if (dot) *dot = '\0';

            snprintf(statusBuf, statusBufSize, "Extracting %s... (%d/%d)", fd.name, count + 1, totalXips);
            fprintf(stdout, "[Extract] %s\n", fd.name);
            Preloader_ExtractXIP(xipPath, outDir);
            count++;
        } while (_findnext(hFind, &fd) == 0);
        _findclose(hFind);
    }
#else
    // POSIX: use opendir/readdir
    DIR* d = opendir(xipsDir);
    if (!d) { snprintf(statusBuf, statusBufSize, "Cannot open: %s", xipsDir); return -1; }

    struct dirent* ent;
    while ((ent = readdir(d)) != NULL) {
        if (Preloader_EndsWith(ent->d_name, ".xip")) totalXips++;
    }
    rewinddir(d);

    while ((ent = readdir(d)) != NULL) {
        if (!Preloader_EndsWith(ent->d_name, ".xip")) continue;

        char xipPath[1024], outDir[1024];
        snprintf(xipPath, sizeof(xipPath), "%s/%s", xipsDir, ent->d_name);
        snprintf(outDir, sizeof(outDir), "%s/%s", xipsDir, ent->d_name);
        char* dot = strrchr(outDir + strlen(xipsDir) + 1, '.');
        if (dot) *dot = '\0';

        snprintf(statusBuf, statusBufSize, "Extracting %s... (%d/%d)", ent->d_name, count + 1, totalXips);
        fprintf(stdout, "[Extract] %s\n", ent->d_name);
        Preloader_ExtractXIP(xipPath, outDir);
        count++;
    }
    closedir(d);
#endif

    snprintf(statusBuf, statusBufSize, "Extracted %d XIP file(s)", count);
    return count;
}

// Check if extracted XIPs already exist (look for default/default.xap)
static bool Preloader_HasExtractedXIPs() {
    struct stat st;
    return (stat("Data/Xips/default/default.xap", &st) == 0 && S_ISREG(st.st_mode));
}

// StartupMode is stored in desktop.ini via g_startupMode (loaded by sdl_main.cpp)
extern int g_startupMode;
extern void SaveDesktopSettings();

// ============================================================================
// Preloader UI
// ============================================================================

// Helper: draw a large styled button
static bool PreloaderButton(const char* label, const char* desc, float width, const ImVec4& color) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(color.x * 0.6f, color.y * 0.6f, color.z * 0.6f, 0.9f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(color.x * 0.8f, color.y * 0.8f, color.z * 0.8f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, color);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16, 12));

    bool clicked = ImGui::Button(label, ImVec2(width, 0));

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);

    if (desc && desc[0]) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.6f, 0.5f, 1.0f));
        float textW = ImGui::CalcTextSize(desc).x;
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - textW) * 0.5f);
        ImGui::TextUnformatted(desc);
        ImGui::PopStyleColor();
    }

    return clicked;
}

bool RunPreloader(SDL_Window* window) {
    // If user saved a preference, skip the UI entirely
    if (g_startupMode == 1) return false;  // dashboard mode
    if (g_startupMode == 2) return true;   // development mode

    enum { PAGE_MAIN, PAGE_EXTRACTED, PAGE_EXTRACTING, PAGE_EXTRACT_DONE } page = PAGE_MAIN;
    bool done = false;
    bool extractedMode = false;
    bool rememberChoice = false;
    char extractStatus[256] = "";
    bool hasExtracted = Preloader_HasExtractedXIPs();
    float animTime = 0.0f;

    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) exit(0);
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                if (page == PAGE_EXTRACTED || page == PAGE_EXTRACT_DONE)
                    page = PAGE_MAIN;
            }
        }

        animTime += 0.016f;

#ifndef THESEUS_USE_BGFX
        ImGui_ImplOpenGL3_NewFrame();
#else
        extern void ImGui_ImplBgfx_NewFrame();
        ImGui_ImplBgfx_NewFrame();
#endif
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        int winW, winH;
        SDL_GetWindowSize(window, &winW, &winH);

        // Background glow effect
        ImDrawList* bgDL = ImGui::GetBackgroundDrawList();
        // Subtle green radial glow
        float pulse = 0.5f + 0.15f * sinf(animTime * 0.8f);
        ImVec2 center(winW * 0.5f, winH * 0.45f);
        for (int r = 300; r > 0; r -= 4) {
            float t = (float)r / 300.0f;
            float a = (1.0f - t) * 0.08f * pulse;
            bgDL->AddCircleFilled(center, (float)r, IM_COL32((int)(20 * a * 255), (int)(80 * a * 255), (int)(20 * a * 255), (int)(a * 255)), 64);
        }

        // Centered panel
        float panelW = 460, panelH = (page == PAGE_MAIN) ? 380 : 420;
        ImGui::SetNextWindowPos(ImVec2(winW * 0.5f, winH * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(panelW, panelH), ImGuiCond_Always);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                                 ImGuiWindowFlags_NoScrollbar;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(36, 28));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.07f, 0.04f, 0.96f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.15f, 0.4f, 0.15f, 0.5f));

        ImGui::Begin("##preloader", NULL, flags);

        // Title
        {
            ImGui::PushFont(NULL); // default font, just make it colored
            const char* title = "UIX Desktop";
            float titleW = ImGui::CalcTextSize(title).x;
            ImGui::SetCursorPosX((ImGui::GetWindowWidth() - titleW) * 0.5f);
            float glow = 0.8f + 0.2f * sinf(animTime * 1.2f);
            ImGui::TextColored(ImVec4(0.3f * glow, 1.0f * glow, 0.3f * glow, 1.0f), "%s", title);
            ImGui::PopFont();
        }

        // Subtitle
        {
            const char* sub = "Select Startup Mode";
            float subW = ImGui::CalcTextSize(sub).x;
            ImGui::SetCursorPosX((ImGui::GetWindowWidth() - subW) * 0.5f);
            ImGui::TextColored(ImVec4(0.4f, 0.5f, 0.4f, 0.8f), "%s", sub);
        }

        // Decorative line
        ImGui::Spacing();
        {
            ImVec2 p = ImGui::GetCursorScreenPos();
            float lineW = ImGui::GetContentRegionAvail().x;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            float cx = p.x + lineW * 0.5f;
            // Gradient line from center outward
            dl->AddRectFilledMultiColor(
                ImVec2(cx - lineW * 0.4f, p.y), ImVec2(cx, p.y + 2),
                IM_COL32(0, 80, 0, 0), IM_COL32(60, 200, 60, 200),
                IM_COL32(60, 200, 60, 200), IM_COL32(0, 80, 0, 0));
            dl->AddRectFilledMultiColor(
                ImVec2(cx, p.y), ImVec2(cx + lineW * 0.4f, p.y + 2),
                IM_COL32(60, 200, 60, 200), IM_COL32(0, 80, 0, 0),
                IM_COL32(0, 80, 0, 0), IM_COL32(60, 200, 60, 200));
            ImGui::Dummy(ImVec2(0, 8));
        }

        float btnWidth = ImGui::GetContentRegionAvail().x;

        if (page == PAGE_MAIN) {
            ImGui::Spacing();
            ImGui::Spacing();

            // Dashboard Mode button
            if (PreloaderButton("Dashboard Mode", "Load the dashboard from XIP archives", btnWidth,
                               ImVec4(0.2f, 0.6f, 0.2f, 1.0f))) {
                extractedMode = false;
                if (rememberChoice) { g_startupMode = 1; SaveDesktopSettings(); }
                done = true;
            }

            ImGui::Spacing();
            ImGui::Spacing();
            ImGui::Spacing();

            // Development Mode button
            if (PreloaderButton("Development Mode", "Edit XAP scripts and preview changes live", btnWidth,
                               ImVec4(0.15f, 0.45f, 0.55f, 1.0f))) {
                page = PAGE_EXTRACTED;
            }

            // Remember choice checkbox
            ImGui::Spacing();
            ImGui::Spacing();
            ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 200) * 0.5f);
            ImGui::Checkbox("Remember my choice", &rememberChoice);

            // Version info at bottom
            ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 44);
            {
                ImGui::Separator();
                ImGui::Spacing();
                const char* ver = "Change later in Settings";
                float verW = ImGui::CalcTextSize(ver).x;
                ImGui::SetCursorPosX((ImGui::GetWindowWidth() - verW) * 0.5f);
                ImGui::TextColored(ImVec4(0.3f, 0.4f, 0.3f, 0.6f), "%s", ver);
            }

        } else if (page == PAGE_EXTRACTED) {
            ImGui::Spacing();

            // Back button
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.15f, 0.8f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
            if (ImGui::SmallButton("<< Back")) page = PAGE_MAIN;
            ImGui::PopStyleColor(2);

            ImGui::Spacing();
            ImGui::Spacing();

            // Extract XIPs button
            if (PreloaderButton("Extract XIPs from Archives", "Unpack all .xip files into editable folders", btnWidth,
                               ImVec4(0.5f, 0.4f, 0.15f, 1.0f))) {
                page = PAGE_EXTRACTING;
                // Run extraction synchronously (it's fast, just file I/O)
                Preloader_ExtractAllXIPs("Data/Xips", extractStatus, sizeof(extractStatus));
                hasExtracted = Preloader_HasExtractedXIPs();
                page = PAGE_EXTRACT_DONE;
            }

            ImGui::Spacing();
            ImGui::Spacing();
            ImGui::Spacing();

            // Load from extracted folder
            {
                bool canLoad = hasExtracted;
                if (!canLoad) {
                    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.4f);
                }

                if (PreloaderButton("Start Development Mode",
                                   canLoad ? "Load scene from extracted XAP files"
                                           : "No extracted XIPs found - extract first",
                                   btnWidth, ImVec4(0.15f, 0.45f, 0.55f, 1.0f)) && canLoad) {
                    extractedMode = true;
                    if (rememberChoice) { g_startupMode = 2; SaveDesktopSettings(); }
                    done = true;
                }

                if (!canLoad) {
                    ImGui::PopStyleVar();
                }
            }

            // Status
            if (hasExtracted) {
                ImGui::Spacing();
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 0.7f), "  Extracted XIPs detected");
            }

        } else if (page == PAGE_EXTRACT_DONE) {
            ImGui::Spacing();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.15f, 0.8f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
            if (ImGui::SmallButton("<< Back")) page = PAGE_MAIN;
            ImGui::PopStyleColor(2);

            ImGui::Spacing();
            ImGui::Spacing();

            // Show extraction result
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Extraction Complete!");
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.6f, 0.7f, 0.6f, 0.9f), "%s", extractStatus);

            ImGui::Spacing();
            ImGui::Spacing();
            ImGui::Spacing();

            // Now they can load from the extracted folder
            if (PreloaderButton("Load from Extracted Folder",
                               "Open XAP editor with live scene reload",
                               btnWidth, ImVec4(0.15f, 0.45f, 0.55f, 1.0f))) {
                extractedMode = true;
                done = true;
            }

            ImGui::Spacing();
            ImGui::Spacing();

            // Or go back and use compiled
            if (PreloaderButton("Load Compiled XIPs Instead",
                               "Use original .xip archives",
                               btnWidth, ImVec4(0.2f, 0.6f, 0.2f, 1.0f))) {
                extractedMode = false;
                done = true;
            }
        }

        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);

        // Render
        ImGui::Render();
#ifndef THESEUS_USE_BGFX
        glViewport(0, 0, winW, winH);
        glClearColor(0.02f, 0.03f, 0.02f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
#else
        bgfx::setViewRect(0, 0, 0, (uint16_t)winW, (uint16_t)winH);
        bgfx::setViewClear(0, BGFX_CLEAR_COLOR,
                           (uint32_t)((5<<24) | (8<<16) | (5<<8) | 0xFF),
                           1.0f, 0);
        bgfx::touch(0);
        extern void ImGui_ImplBgfx_RenderDrawData(ImDrawData*);
        ImGui_ImplBgfx_RenderDrawData(ImGui::GetDrawData());
        bgfx::frame();
#endif
    }

    return extractedMode;
}
