// sdl_main.cpp: desktop entry point. Creates an SDL window with an
// OpenGL 3.2 Core context, hands off to dashinit / dashapp, and
// runs the main loop. Counterpart to xbox/main.cpp.

#include "std.h"
#include "dashapp.h"
#include "node.h"
#include "runner.h"
#include "shape_render.h"
#include "asset_loader.h"
#include "audio_sdl.h"
#include "xiso.h"
#include "hdd_browser.h"
#include "title_maker.h"
#include "menu_bar.h"
#include "media_player.h"
#include "media_db.h"
#include "xap_editor.h"
#include "inspector.h"
#include "preloader.h"
#include "boot_anim.h"
#include "panel_shared.h"
#include "launch.h"
#include <signal.h>

#ifdef THESEUS_USE_BGFX
#include <SDL_syswm.h>
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>

// Loads Data/shaders/<backend>/<name>.bin. Picks subdir from the active renderer.
bgfx::ShaderHandle theseus_bgfx_load_shader(const char* name)
{
	const char* sub = "metal";
	switch (bgfx::getRendererType()) {
		case bgfx::RendererType::Direct3D11: sub = "dx11";   break;
		case bgfx::RendererType::Metal:      sub = "metal";  break;
		case bgfx::RendererType::Vulkan:     sub = "spirv";  break;
		case bgfx::RendererType::OpenGL:     sub = "glsl";   break;
		default: break;
	}
	char path[512];
	snprintf(path, sizeof(path), "Data/shaders/%s/%s.bin", sub, name);
	// fopen is macro-routed through xboxfs.h's path translator; the
	// relative path passes through unchanged, no Xbox-style remap.
	FILE* f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "bgfx: shader file not found: %s\n", path);
		return BGFX_INVALID_HANDLE;
	}
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	const bgfx::Memory* mem = bgfx::alloc((uint32_t)sz + 1);
	fread(mem->data, 1, sz, f);
	mem->data[sz] = '\0';
	fclose(f);
	bgfx::ShaderHandle sh = bgfx::createShader(mem);
	bgfx::setName(sh, name);
	return sh;
}
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>  // _NSGetExecutablePath
#endif
#ifdef __linux__
#include <sys/wait.h>
#endif

#ifdef _WIN32
#include <io.h>
#include <direct.h>
#include <process.h>
#include <dbghelp.h>
#include <avrt.h>
#include <dwmapi.h>
#pragma comment(lib, "dbghelp.lib")

// Watchdog: dumps main thread stack to hang_dump.txt after 10s of detected hang
static DWORD WINAPI HangWatchdog(LPVOID param) {
    DWORD mainThread = (DWORD)(uintptr_t)param;
    while (true) {
        Sleep(2000);
        extern volatile bool g_hangCheck;
        extern volatile DWORD g_hangStartTime;
        if (!g_hangCheck) continue;
        if (GetTickCount() - g_hangStartTime < 10000) continue;

        FILE* f = fopen("hang_dump.txt", "w");
        if (!f) continue;
        fprintf(f, "=== HANG DETECTED (>10s) ===\n");
        fprintf(f, "Main thread ID: %lu\n\n", mainThread);

        HANDLE hThread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, mainThread);
        if (hThread) {
            SuspendThread(hThread);
            CONTEXT ctx = {};
            ctx.ContextFlags = CONTEXT_FULL;
            GetThreadContext(hThread, &ctx);

            SymInitialize(GetCurrentProcess(), NULL, TRUE);
            STACKFRAME64 sf = {};
            sf.AddrPC.Offset = ctx.Rip; sf.AddrPC.Mode = AddrModeFlat;
            sf.AddrFrame.Offset = ctx.Rbp; sf.AddrFrame.Mode = AddrModeFlat;
            sf.AddrStack.Offset = ctx.Rsp; sf.AddrStack.Mode = AddrModeFlat;

            fprintf(f, "Call stack:\n");
            for (int i = 0; i < 50; i++) {
                if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, GetCurrentProcess(), hThread, &sf, &ctx, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL))
                    break;
                char symBuf[sizeof(SYMBOL_INFO) + 256];
                SYMBOL_INFO* sym = (SYMBOL_INFO*)symBuf;
                sym->SizeOfStruct = sizeof(SYMBOL_INFO);
                sym->MaxNameLen = 255;
                DWORD64 disp = 0;
                if (SymFromAddr(GetCurrentProcess(), sf.AddrPC.Offset, &disp, sym))
                    fprintf(f, "  [%d] %s + 0x%llx\n", i, sym->Name, (unsigned long long)disp);
                else
                    fprintf(f, "  [%d] 0x%llx\n", i, (unsigned long long)sf.AddrPC.Offset);
            }
            SymCleanup(GetCurrentProcess());
            ResumeThread(hThread);
            CloseHandle(hThread);
        }
        fclose(f);
        fprintf(stderr, "[Watchdog] Hang dump written to hang_dump.txt\n");
        g_hangCheck = false;
    }
    return 0;
}
volatile bool g_hangCheck = false;
volatile DWORD g_hangStartTime = 0;
#define strcasecmp _stricmp
#define rmdir _rmdir
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif
#else
#include <unistd.h>
#include <dirent.h>
#if defined(__APPLE__) || defined(__GLIBC__)
#include <execinfo.h>
#define HAS_BACKTRACE 1
#endif
#endif

// Dear ImGui
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include "imfilebrowser.h"
#include "embedded_assets.h"

// stb_image for Title Maker icon preview (implementation compiled here)
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#include "stb_image.h"

// Platform code uses native file I/O; undo Xbox filesystem macros
// Dashboard engine files (config.cpp, Runner.cpp, xip.cpp, etc.) keep the macros
// so they think they're on an Xbox. We don't.
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

// Virtual games database
VirtualGameDB g_vgames = {};

// Global SDL/GL state; used by D3D8 stubs to render
SDL_Window*   g_pSDLWindow = NULL;
SDL_GLContext  g_pGLContext = NULL;
GLState        g_gl = {};

#ifdef THESEUS_USE_BGFX
// Phase 2 / chunk 5a: linked FF emulator program, used by the shim's
// shadow-submit path. Stays BGFX_INVALID_HANDLE if the shader binaries
// were missing at init time; the shadow path no-ops in that case.
bgfx::ProgramHandle g_bgfxProgFF = BGFX_INVALID_HANDLE;

// Chunk 5b: uniform handles for the FF emulator program. Mirror
// SetupGLUniforms / vs_ff / fs_ff. Created at bgfx init time and lived
// for the run; bgfx destroys all handles on shutdown.
BgfxFFUniforms g_bgfxFF = {};

// Chunk 5b: 1x1 white texture bound when the shim has no real texture
// at a stage. Real-texture upload via IDirect3DTexture8::m_bgfxTex is
// chunk 5c.
bgfx::TextureHandle g_bgfxWhiteTex = BGFX_INVALID_HANDLE;

// Chunk 5d-3+: blit program for fullscreen textured quads (boot anim,
// CRT post). vs_blit/fs_blit; sampler bound at slot 0 named "s_blit".
bgfx::ProgramHandle g_bgfxProgBlit = BGFX_INVALID_HANDLE;
bgfx::UniformHandle g_bgfxSamplerBlit = BGFX_INVALID_HANDLE;
#endif

// CRT post-process state. Effect params get sensible defaults; backend
// handles are zeroed and lazy-allocated by Init / Resize on the first
// frame the toggle goes hot.
CRTState g_crt = {};
struct CRTDefaults {
    CRTDefaults() {
        g_crt.scanlineIntensity = 0.5f;
        g_crt.curvature         = 0.4f;
        g_crt.phosphorMask      = 0.3f;
        g_crt.vignette          = 0.3f;
        g_crt.bloom             = 0.15f;
        g_crt.flickerAmount     = 0.2f;
        g_crt.colorBleed        = 1.0f;
        g_crt.brightness        = 1.05f;
#ifdef THESEUS_USE_BGFX
        g_crt.fb.idx       = bgfx::kInvalidHandle;
        g_crt.colorTex.idx = bgfx::kInvalidHandle;
        g_crt.program.idx  = bgfx::kInvalidHandle;
        g_crt.s_scene.idx  = bgfx::kInvalidHandle;
        g_crt.u_p1.idx     = bgfx::kInvalidHandle;
        g_crt.u_p2.idx     = bgfx::kInvalidHandle;
        g_crt.u_p3.idx     = bgfx::kInvalidHandle;
#endif
    }
} g_crtDefaults;
static float s_crtTime = 0.0f;

#ifdef _WIN32
// Force discrete GPU on Optimus / switchable laptops. bgfx exports its own
// pair, so skip under BGFX to avoid the dup-symbol link error.
#ifndef THESEUS_USE_BGFX
extern "C" {
    __declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif
#endif

// `--graphics-debug` dumper. Writes GPU / driver / vsync / MSAA / display info
// to stderr and theseus_graphics.log next to the binary.
static void DumpGraphicsInfo()
{
    FILE* logFp = fopen("theseus_graphics.log", "w");

    auto write2 = [&logFp](const char* line) {
        fprintf(stderr, "%s", line);
        if (logFp) fprintf(logFp, "%s", line);
    };

    char buf[1024];

    write2("=== Theseus graphics debug ===\n");

#ifndef THESEUS_USE_BGFX
    snprintf(buf, sizeof(buf), "GL_VENDOR:   %s\n", (const char*)glGetString(GL_VENDOR));
    write2(buf);
    snprintf(buf, sizeof(buf), "GL_RENDERER: %s\n", (const char*)glGetString(GL_RENDERER));
    write2(buf);
    snprintf(buf, sizeof(buf), "GL_VERSION:  %s\n", (const char*)glGetString(GL_VERSION));
    write2(buf);
    const GLubyte* glsl = glGetString(GL_SHADING_LANGUAGE_VERSION);
    snprintf(buf, sizeof(buf), "GLSL:        %s\n", glsl ? (const char*)glsl : "(unknown)");
    write2(buf);
#else
    {
        const bgfx::Caps* c = bgfx::getCaps();
        snprintf(buf, sizeof(buf), "bgfx renderer: %s\n",
                 bgfx::getRendererName(c->rendererType));
        write2(buf);
    }
#endif

    int swap = SDL_GL_GetSwapInterval();
    snprintf(buf, sizeof(buf), "Swap interval: %d (vsync %s)\n",
        swap, (swap == 0) ? "off" : (swap < 0 ? "adaptive" : "on"));
    write2(buf);

    int msaaBuffers = 0, msaaSamples = 0;
    SDL_GL_GetAttribute(SDL_GL_MULTISAMPLEBUFFERS, &msaaBuffers);
    SDL_GL_GetAttribute(SDL_GL_MULTISAMPLESAMPLES, &msaaSamples);
    snprintf(buf, sizeof(buf), "MSAA: buffers=%d samples=%d\n", msaaBuffers, msaaSamples);
    write2(buf);

    if (g_pSDLWindow) {
        SDL_DisplayMode dm;
        if (SDL_GetWindowDisplayMode(g_pSDLWindow, &dm) == 0) {
            snprintf(buf, sizeof(buf), "Display mode: %dx%d @ %dHz\n",
                dm.w, dm.h, dm.refresh_rate);
            write2(buf);
        }
        int winW = 0, winH = 0;
        SDL_GetWindowSize(g_pSDLWindow, &winW, &winH);
        snprintf(buf, sizeof(buf), "Window size: %dx%d\n", winW, winH);
        write2(buf);
    }

#if defined(_WIN32) && !defined(THESEUS_USE_BGFX)
    snprintf(buf, sizeof(buf), "NvOptimusEnablement=%lu, AmdPowerXpressRequestHighPerformance=%d\n",
        (unsigned long)NvOptimusEnablement, AmdPowerXpressRequestHighPerformance);
    write2(buf);
#endif

    write2("==============================\n");

    if (logFp) fclose(logFp);
    fflush(stderr);
}

// Desktop restart flag (set by launch() in runner.cpp, handled by main loop)
bool g_desktopRestartRequested = false;
bool g_desktopRestartMuted = false;
static char* s_execPath = NULL;

// Saved argv/argc for execv restart
int g_argc = 0;
char** g_argv = NULL;

// Game process tracking; hide dashboard while game runs, soft restart on exit
#ifdef _WIN32
static DWORD  s_gamePID = 0;        // PID of launched game (0 = none)
#else
#include <signal.h>
#include <spawn.h>
extern char **environ;
static pid_t  s_gamePID = 0;        // PID of launched game (0 = none)
#endif
static bool   s_gameRunning = false; // true while waiting for game to exit
static bool   s_softRestartPending = false; // reinit after game exits

// Audio mute state (Ctrl+M toggle, auto-muted during game launch)
bool g_audioMuted = false;       // user choice (Ctrl+M)
bool g_windowFocused = true;     // SDL focus state
// Mute ambient (SDL_mixer) when: user pressed Ctrl+M, window unfocused, or
// media is playing. mpv runs through its own audio out, unaffected by this.
static void ApplyEffectiveMute()
{
    extern bool g_mediaFullscreen;
    bool shouldMute = g_audioMuted || !g_windowFocused || g_mediaFullscreen;
    if (shouldMute) DashAudio_MuteAll();
    else            DashAudio_UnmuteAll();
}
void ApplyEffectiveMute_Public() { ApplyEffectiveMute(); }
bool g_startMinimized = false;
bool g_graphicsDebug = false;
extern double g_perfDrawMs, g_perfImguiMs, g_perfSwapMs, g_perfFrameMs, g_perfFps;
float g_muteOverlayTimer = 0.0f; // seconds remaining to show the overlay toast

// Static instance tracking for GL context reset
IDirect3DTexture8* IDirect3DTexture8::s_firstTex = NULL;
IDirect3DVertexBuffer8* IDirect3DVertexBuffer8::s_firstVB = NULL;
IDirect3DIndexBuffer8* IDirect3DIndexBuffer8::s_firstIB = NULL;
IDirect3DDevice8::PreSwapCallback IDirect3DDevice8::s_preSwapCB = NULL;


// Load/save desktop settings (xemu path, CRT effect, etc.)
char s_xemuPath[512] = "";
char g_qcowPath[512] = "";  // global, used by desktop_nodes.cpp for SavedGameGrid
char s_steamPath[512] = ""; // Steam install root (parent of steamapps/)
char s_retroarchPath[512] = ""; // RetroArch install root (contains retroarch.exe + cores/)
bool g_showRetroArchTab = true; // hides the Title Maker RetroArch tab; games stay in games.ini
bool g_showSteamTab     = true; // hides the Title Maker Steam tab; games stay in games.ini

// Library roots from desktop.ini [Library]. Consumed by audio_sdl.cpp + media nodes.
char g_musicRoot[512]  = "";
char g_moviesRoot[512] = "";
char g_tvRoot[512]     = "";
char g_tmdbKey[128]    = "";  // TMDB v3 API key, optional
char g_romsDir[512]    = "";  // Default ROMs/ISOs root, expanded as $ROMS_DIR in launch templates
int g_startupMode = 0;      // 0 = ask, 1 = dashboard, 2 = development
bool g_bUseOnScreenKeyboard = false;  // when true, ignore physical keyboard during keyboard popups
bool g_bShowBootAnimation   = true;   // play xbox_boot.mp4 once at startup before the dashboard

// Legacy [Progressive] / [Dashboard Settings] sections, round-tripped through
// desktop.ini so the truncate-rewrite save doesn't drop them.
char g_use720p[8]        = "Yes";
char g_useProgressive[8] = "Yes";
char g_currentSkin[64]   = "Stock";

class CKeyboard;
extern CKeyboard* g_pActiveKeyboard;
extern void Keyboard_InsertText(CKeyboard* pKb, const char* sz);
extern void Keyboard_HandleKey(CKeyboard* pKb, int sdlKey);

void LoadDesktopSettings() {
    FILE* fp = fopen("Configs/desktop.ini", "r");
    if (!fp) return;
    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        char* nl = strchr(line, '\n'); if (nl) *nl = 0;
        char* cr = strchr(line, '\r'); if (cr) *cr = 0;
        if (strncmp(line, "XemuPath=", 9) == 0)
            strncpy(s_xemuPath, line + 9, sizeof(s_xemuPath) - 1);
        else if (strncmp(line, "QcowPath=", 9) == 0)
            strncpy(g_qcowPath, line + 9, sizeof(g_qcowPath) - 1);
        else if (strncmp(line, "SteamPath=", 10) == 0)
            strncpy(s_steamPath, line + 10, sizeof(s_steamPath) - 1);
        else if (strncmp(line, "RetroArchPath=", 14) == 0)
            strncpy(s_retroarchPath, line + 14, sizeof(s_retroarchPath) - 1);
        else if (strncmp(line, "ShowRetroArchTab=", 17) == 0)
            g_showRetroArchTab = atoi(line + 17) != 0;
        else if (strncmp(line, "ShowSteamTab=", 13) == 0)
            g_showSteamTab = atoi(line + 13) != 0;
        else if (strncmp(line, "CRT_Enabled=", 12) == 0)
            g_crt.enabled = atoi(line + 12) != 0;
        else if (strncmp(line, "CRT_Scanlines=", 14) == 0)
            g_crt.scanlineIntensity = (float)atof(line + 14);
        else if (strncmp(line, "CRT_Curvature=", 14) == 0)
            g_crt.curvature = (float)atof(line + 14);
        else if (strncmp(line, "CRT_Phosphor=", 13) == 0)
            g_crt.phosphorMask = (float)atof(line + 13);
        else if (strncmp(line, "CRT_Vignette=", 13) == 0)
            g_crt.vignette = (float)atof(line + 13);
        else if (strncmp(line, "CRT_Bloom=", 10) == 0)
            g_crt.bloom = (float)atof(line + 10);
        else if (strncmp(line, "CRT_Flicker=", 12) == 0)
            g_crt.flickerAmount = (float)atof(line + 12);
        else if (strncmp(line, "CRT_ColorBleed=", 15) == 0)
            g_crt.colorBleed = (float)atof(line + 15);
        else if (strncmp(line, "CRT_Brightness=", 15) == 0)
            g_crt.brightness = (float)atof(line + 15);
        else if (strncmp(line, "UseOnScreenKeyboard=", 20) == 0)
            g_bUseOnScreenKeyboard = atoi(line + 20) != 0;
        else if (strncmp(line, "ShowBootAnimation=", 18) == 0)
            g_bShowBootAnimation = atoi(line + 18) != 0;
        else if (strncmp(line, "Use 720p=", 9) == 0) {
            strncpy(g_use720p, line + 9, sizeof(g_use720p) - 1);
            g_use720p[sizeof(g_use720p) - 1] = 0;
        }
        else if (strncmp(line, "Use Progressive=", 16) == 0) {
            strncpy(g_useProgressive, line + 16, sizeof(g_useProgressive) - 1);
            g_useProgressive[sizeof(g_useProgressive) - 1] = 0;
        }
        else if (strncmp(line, "Current Skin=", 13) == 0) {
            strncpy(g_currentSkin, line + 13, sizeof(g_currentSkin) - 1);
            g_currentSkin[sizeof(g_currentSkin) - 1] = 0;
        }
        else if (strncmp(line, "StartupMode=", 12) == 0) {
            if (strncmp(line + 12, "dashboard", 9) == 0) g_startupMode = 1;
            else if (strncmp(line + 12, "development", 11) == 0) g_startupMode = 2;
            else g_startupMode = 0;
        }
        else if (strncmp(line, "MusicRoot=", 10) == 0)
            strncpy(g_musicRoot, line + 10, sizeof(g_musicRoot) - 1);
        else if (strncmp(line, "MoviesRoot=", 11) == 0)
            strncpy(g_moviesRoot, line + 11, sizeof(g_moviesRoot) - 1);
        else if (strncmp(line, "TvRoot=", 7) == 0)
            strncpy(g_tvRoot, line + 7, sizeof(g_tvRoot) - 1);
        else if (strncmp(line, "TMDBKey=", 8) == 0)
            strncpy(g_tmdbKey, line + 8, sizeof(g_tmdbKey) - 1);
        else if (strncmp(line, "RomsDir=", 8) == 0)
            strncpy(g_romsDir, line + 8, sizeof(g_romsDir) - 1);
        else if (strncmp(line, "MSAA=", 5) == 0) {
            int n = atoi(line + 5);
            if (n == 0 || n == 2 || n == 4 || n == 8)
                g_msaaSamples = n;
        }
        else if (strncmp(line, "Vsync=", 6) == 0) {
            int n = atoi(line + 6);
            if (n >= 0 && n <= 2) g_vsyncMode = n;
        }
        else if (strncmp(line, "FpsCap=", 7) == 0) {
            int n = atoi(line + 7);
            if (n >= 0) g_fpsCap = n;
        }
        else if (strncmp(line, "Hwdec=", 6) == 0)
            g_hwdec = atoi(line + 6) != 0;
        else if (strncmp(line, "Renderer=", 9) == 0) {
            const char* v = line + 9;
            if      (strncmp(v, "auto",   4) == 0) g_rendererPref = 0;
            else if (strncmp(v, "d3d11",  5) == 0) g_rendererPref = 1;
            else if (strncmp(v, "vulkan", 6) == 0) g_rendererPref = 2;
            else if (strncmp(v, "opengl", 6) == 0) g_rendererPref = 3;
        }
        else if (strncmp(line, "Resolution=", 11) == 0) {
            int n = atoi(line + 11);
            if (n == 0 || n == 720 || n == 1080 || n == 1440 || n == 2160)
                g_windowResolution = n;
        }
        else if (strncmp(line, "DisplayMode=", 12) == 0) {
            int n = atoi(line + 12);
            if (n >= 0 && n <= 2) g_windowMode = n;
        }
    }
    fclose(fp);
}

// Refresh the legacy sections from disk so a Save doesn't overwrite values
// the dashboard wrote between Load and Save (e.g. skin change via XAP).
static void RereadLegacyFromDisk() {
    FILE* fp = fopen("Configs/desktop.ini", "r");
    if (!fp) return;
    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        char* nl = strchr(line, '\n'); if (nl) *nl = 0;
        char* cr = strchr(line, '\r'); if (cr) *cr = 0;
        if (strncmp(line, "Use 720p=", 9) == 0) {
            strncpy(g_use720p, line + 9, sizeof(g_use720p) - 1);
            g_use720p[sizeof(g_use720p) - 1] = 0;
        } else if (strncmp(line, "Use Progressive=", 16) == 0) {
            strncpy(g_useProgressive, line + 16, sizeof(g_useProgressive) - 1);
            g_useProgressive[sizeof(g_useProgressive) - 1] = 0;
        } else if (strncmp(line, "Current Skin=", 13) == 0) {
            strncpy(g_currentSkin, line + 13, sizeof(g_currentSkin) - 1);
            g_currentSkin[sizeof(g_currentSkin) - 1] = 0;
        }
    }
    fclose(fp);
}

void SaveDesktopSettings() {
    // Ensure directory exists
    struct stat st;
    if (stat("Configs", &st) != 0) {
        system("mkdir -p \"Configs\"");
    }
    RereadLegacyFromDisk();
    FILE* fp = fopen("Configs/desktop.ini", "w");
    if (!fp) return;
    fprintf(fp, "[Desktop]\n");
    fprintf(fp, "XemuPath=%s\n", s_xemuPath);
    fprintf(fp, "QcowPath=%s\n", g_qcowPath);
    fprintf(fp, "SteamPath=%s\n", s_steamPath);
    fprintf(fp, "RetroArchPath=%s\n", s_retroarchPath);
    fprintf(fp, "ShowRetroArchTab=%d\n", g_showRetroArchTab ? 1 : 0);
    fprintf(fp, "ShowSteamTab=%d\n",     g_showSteamTab     ? 1 : 0);
    fprintf(fp, "StartupMode=%s\n", g_startupMode == 2 ? "development" : g_startupMode == 1 ? "dashboard" : "");
    fprintf(fp, "UseOnScreenKeyboard=%d\n", g_bUseOnScreenKeyboard ? 1 : 0);
    fprintf(fp, "ShowBootAnimation=%d\n",   g_bShowBootAnimation   ? 1 : 0);
    fprintf(fp, "MSAA=%d\n",                g_msaaSamples);
    fprintf(fp, "Vsync=%d\n",               g_vsyncMode);
    fprintf(fp, "FpsCap=%d\n",              g_fpsCap);
    fprintf(fp, "Hwdec=%d\n",               g_hwdec ? 1 : 0);
    {
        const char* rname = (g_rendererPref == 1) ? "d3d11"
                          : (g_rendererPref == 2) ? "vulkan"
                          : (g_rendererPref == 3) ? "opengl"
                          : "auto";
        fprintf(fp, "Renderer=%s\n", rname);
    }
    fprintf(fp, "Resolution=%d\n",          g_windowResolution);
    fprintf(fp, "DisplayMode=%d\n",         g_windowMode);
    fprintf(fp, "\n[CRT]\n");
    fprintf(fp, "CRT_Enabled=%d\n", g_crt.enabled ? 1 : 0);
    fprintf(fp, "CRT_Scanlines=%.3f\n", g_crt.scanlineIntensity);
    fprintf(fp, "CRT_Curvature=%.3f\n", g_crt.curvature);
    fprintf(fp, "CRT_Phosphor=%.3f\n", g_crt.phosphorMask);
    fprintf(fp, "CRT_Vignette=%.3f\n", g_crt.vignette);
    fprintf(fp, "CRT_Bloom=%.3f\n", g_crt.bloom);
    fprintf(fp, "CRT_Flicker=%.3f\n", g_crt.flickerAmount);
    fprintf(fp, "CRT_ColorBleed=%.3f\n", g_crt.colorBleed);
    fprintf(fp, "CRT_Brightness=%.3f\n", g_crt.brightness);
    fprintf(fp, "\n[Library]\n");
    fprintf(fp, "MusicRoot=%s\n", g_musicRoot);
    fprintf(fp, "MoviesRoot=%s\n", g_moviesRoot);
    fprintf(fp, "TvRoot=%s\n", g_tvRoot);
    fprintf(fp, "TMDBKey=%s\n", g_tmdbKey);
    fprintf(fp, "RomsDir=%s\n", g_romsDir);
    // Legacy Q:\System\config.ini sections (aliased to this file by xboxfs.h).
    fprintf(fp, "\n[Progressive]\n");
    fprintf(fp, "Use 720p=%s\n",        g_use720p);
    fprintf(fp, "Use Progressive=%s\n", g_useProgressive);
    fprintf(fp, "\n[Dashboard Settings]\n");
    fprintf(fp, "Current Skin=%s\n",    g_currentSkin);
    fclose(fp);
}

// Helper: returns true when ImGui has an active text input widget (Title Maker, inspector search, etc.)
static bool s_imguiHasRendered = false;
bool ImGui_WantsKeyboard() {
    if (!s_imguiHasRendered) return false;
    return ImGui::GetIO().WantCaptureKeyboard;
}

// Stores a GLuint or bgfx::TextureHandle depending on backend.
struct GuiTexture {
#ifndef THESEUS_USE_BGFX
    GLuint glHandle;
#else
    bgfx::TextureHandle bgfxHandle;
#endif
    int w, h;
};

GuiTexture* GuiTextureCreate(int w, int h, const void* rgbaPixels) {
    if (w <= 0 || h <= 0 || rgbaPixels == NULL) return NULL;
    GuiTexture* t = new GuiTexture();
    t->w = w;
    t->h = h;
#ifndef THESEUS_USE_BGFX
    glGenTextures(1, &t->glHandle);
    glBindTexture(GL_TEXTURE_2D, t->glHandle);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgbaPixels);
#else
    t->bgfxHandle = bgfx::createTexture2D(
        (uint16_t)w, (uint16_t)h, false, 1,
        bgfx::TextureFormat::RGBA8,
        BGFX_TEXTURE_NONE | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
        bgfx::copy(rgbaPixels, (uint32_t)(w * h * 4)));
    if (!bgfx::isValid(t->bgfxHandle)) {
        delete t;
        return NULL;
    }
#endif
    return t;
}

void GuiTextureDestroy(GuiTexture** ptex) {
    if (!ptex || !*ptex) return;
    GuiTexture* t = *ptex;
#ifndef THESEUS_USE_BGFX
    if (t->glHandle) glDeleteTextures(1, &t->glHandle);
#else
    if (bgfx::isValid(t->bgfxHandle)) bgfx::destroy(t->bgfxHandle);
#endif
    delete t;
    *ptex = NULL;
}

unsigned long long GuiTextureImId(const GuiTexture* tex) {
    if (!tex) return 0;
#ifndef THESEUS_USE_BGFX
    return (unsigned long long)tex->glHandle;
#else
    return (unsigned long long)tex->bgfxHandle.idx;
#endif
}

// Forward declarations for PreSwapOverlays
bool g_debugMode = false;
bool g_showMenuBar = true;   // toggled by F10 / View > Hide Menu Bar / --no-toolbar

// Pre-swap callback: renders ImGui overlays on top of the 3D scene before Present() swaps.
// This runs in a single ImGui frame so all overlays (mute toast, Title Maker, selection highlight) share input.
// Forward decls (g_mediaFullscreen lives lower; PreSwapOverlays uses it).
extern bool g_mediaFullscreen;
void MediaPlayer_Update();
void MediaUI_NoteActivity();
bool MediaUI_OsdVisible();

static void PreSwapOverlays() {
    bool needMute = (g_muteOverlayTimer > 0.0f || g_audioMuted);
    bool needHighlight = (g_debugMode && g_pD3DDev &&
                          g_pD3DDev->m_inspectorSelectedNode &&
                          g_pD3DDev->m_drawRecordCount > 0);
    // Always render ImGui frame for the menu bar
    // Video renders inside CDVDPlayer::Render() during the scene graph pass

#ifndef THESEUS_USE_BGFX
    ImGui_ImplOpenGL3_NewFrame();
#else
    extern void ImGui_ImplBgfx_NewFrame();
    ImGui_ImplBgfx_NewFrame();
#endif
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    // Pump mpv events every frame regardless of fullscreen state. If we
    // only pump while fullscreen, stale END_FILE events from the previous
    // playback queue up between videos and stall the render context.
    MediaPlayer_Update();

    if (g_mediaFullscreen) {
        extern void MediaUI_RenderOSD();
        MediaUI_RenderOSD();
    }

    // Menu bar (toggle with F10). In fullscreen video, also auto-hide
    // alongside the OSD chrome.
    if (g_showMenuBar && (!g_mediaFullscreen || MediaUI_OsdVisible()))
        RenderMainMenuBar();

    // Settings, About, and Shortcuts windows
    RenderSettingsWindow();
    RenderAboutWindow();
    RenderShortcutsWindow();

    // Modal scan progress (blocks input while MediaDB_ScanAndCache runs).
    extern void RenderScanProgressModal();
    RenderScanProgressModal();

    // Mute overlay toast
    if (needMute) {
        float alpha = 1.0f;
        if (!g_audioMuted && g_muteOverlayTimer > 0.0f) {
            alpha = (g_muteOverlayTimer < 1.0f) ? g_muteOverlayTimer : 1.0f;
        } else if (g_audioMuted && g_muteOverlayTimer <= 0.0f) {
            alpha = 0.5f;
        } else if (g_audioMuted) {
            alpha = (g_muteOverlayTimer < 1.0f && g_muteOverlayTimer > 0.0f) ? 1.0f : 0.5f;
        }
        int winW, winH;
        SDL_GetWindowSize(g_pSDLWindow, &winW, &winH);
        ImU32 bgCol = IM_COL32(0, 0, 0, (int)(180 * alpha));
        ImU32 txtCol = g_audioMuted
            ? IM_COL32(255, 100, 100, (int)(255 * alpha))
            : IM_COL32(100, 255, 100, (int)(255 * alpha));
        const char* label = g_audioMuted ? "MUTED" : "UNMUTED";
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        ImVec2 textSize = ImGui::CalcTextSize(label);
        float px = 10.0f;
        float py = winH - textSize.y - 14.0f;
        dl->AddRectFilled(ImVec2(px - 8, py - 4), ImVec2(px + textSize.x + 8, py + textSize.y + 4), bgCol, 6.0f);
        dl->AddText(ImVec2(px, py), txtCol, label);
    }

    // Title Maker floating window
    RenderTitleMaker();

    // Playlist Maker floating window (F6)
    extern void RenderPlaylistMaker();
    RenderPlaylistMaker();

    // Selection highlight overlay (pulsing AABB + hover tooltip)
    if (needHighlight)
        DrawSelectionHighlight(g_pD3DDev);

    // Xbox HDD Browser (F5)
    RenderHDDBrowser();

    // Inspector (F1); floating ImGui window
    RenderInspectorPanel(g_pD3DDev);

    // XAP Editor (F2); floating ImGui window
    RenderXAPEditor();

    // Launch overlay (fade-to-black + Xbox logo). Drawn last so it sits
    // above every other overlay.
    LaunchOverlay_Tick();
    if (LaunchOverlay_IsActive()) {
        float a = LaunchOverlay_Alpha();
        int winW, winH;
        SDL_GetWindowSize(g_pSDLWindow, &winW, &winH);
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        dl->AddRectFilled(ImVec2(0, 0), ImVec2((float)winW, (float)winH),
                          IM_COL32(0, 0, 0, (int)(255.0f * a)));

        int texW = 0, texH = 0;
        unsigned long long tex = LaunchOverlay_LogoGLTex(&texW, &texH);
        if (tex && texW > 0 && texH > 0 && a > 0.05f) {
            // 60% of window width, clamped to 70% height for ultra-wide.
            float drawW = (float)winW * 0.60f;
            float drawH = drawW * ((float)texH / (float)texW);
            float maxH = (float)winH * 0.70f;
            if (drawH > maxH) {
                drawH = maxH;
                drawW = drawH * ((float)texW / (float)texH);
            }
            ImVec2 p0((winW - drawW) * 0.5f, (winH - drawH) * 0.5f);
            ImVec2 p1(p0.x + drawW, p0.y + drawH);
            ImU32 tint = IM_COL32(255, 255, 255, (int)(255.0f * a));
            dl->AddImage((ImTextureID)(intptr_t)tex, p0, p1,
                         ImVec2(0, 0), ImVec2(1, 1), tint);
        }
    }

    if (g_graphicsDebug) {
        ImGuiWindowFlags fl = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                              ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
        ImGui::SetNextWindowBgAlpha(0.6f);
        ImGui::SetNextWindowPos(ImVec2(8, 30), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("##perf", nullptr, fl)) {
            ImGui::Text("FPS:    %.1f", g_perfFps);
            ImGui::Text("Frame:  %.2f ms", g_perfFrameMs);
            ImGui::Separator();
            ImGui::Text("Draw:   %.2f ms", g_perfDrawMs);
            ImGui::Text("ImGui:  %.2f ms", g_perfImguiMs);
            ImGui::Text("Swap:   %.2f ms", g_perfSwapMs);
        }
        ImGui::End();
    }

    ImGui::Render();
#ifndef THESEUS_USE_BGFX
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#else
    extern void ImGui_ImplBgfx_RenderDrawData(ImDrawData*);
    ImGui_ImplBgfx_RenderDrawData(ImGui::GetDrawData());
#endif
    s_imguiHasRendered = true;

    // Video rendering moved before ImGui frame
}

// g_inspectorOpen is now in inspector.cpp; inspector renders as floating ImGui panel
SDL_Window* g_pXapEditorWindow = NULL;
int  g_msaaSamples = 4;       // 0=off, 2/4/8x MSAA (default 4x)
bool g_msaaChangeRequested = false;
// 0=adaptive (-1, fall back to 1), 1=on (1), 2=off (0). Default adaptive.
int  g_vsyncMode = 0;
// 0 = auto, 1 = d3d11 (windows only), 2 = vulkan, 3 = opengl. Takes effect
// on next launch; bgfx::init cannot switch backends mid-session.
int  g_rendererPref = 0;
bool g_vsyncChangeRequested = false;

// Manual FPS cap (0 = unlimited). Applied via SDL_Delay before SwapWindow.
int  g_fpsCap = 0;

// mpv hwdec. false = "no" (software, GL core safe on macOS).
// true = "auto" (faster on AMD/NVIDIA discrete, may corrupt on macOS).
// Takes effect on next media open; existing mpv handle won't pick it up.
bool g_hwdec = false;

// Window resolution. 720 / 1080 / 1440 / 2160 = those many vertical pixels at
// 16:9. 0 = native (use the display's current mode in fullscreen).
int  g_windowResolution = 720;
// Display mode. 0=windowed, 1=borderless desktop, 2=exclusive fullscreen.
int  g_windowMode = 0;
bool g_displayChangeRequested = false;

// Per-pass timings populated each frame in the main loop. Read by the
// --graphics-debug overlay. Single-frame ms; FPS is EMA-smoothed.
double g_perfDrawMs  = 0.0;
double g_perfImguiMs = 0.0;
double g_perfSwapMs  = 0.0;
double g_perfFrameMs = 0.0;
double g_perfFps     = 0.0;

void ApplyVsyncMode() {
    int interval = (g_vsyncMode == 2) ? 0 : (g_vsyncMode == 1) ? 1 : -1;
    if (SDL_GL_SetSwapInterval(interval) != 0 && interval == -1)
        SDL_GL_SetSwapInterval(1);
}

#ifdef THESEUS_USE_BGFX
// bgfx reset flags from g_vsyncMode + g_msaaSamples. Adaptive vsync (mode 2)
// falls through to off; bgfx has no equivalent.
static uint32_t BgfxResetFlags() {
    uint32_t flags = 0;
    if (g_vsyncMode == 1) flags |= BGFX_RESET_VSYNC;
    switch (g_msaaSamples) {
        case 2:  flags |= BGFX_RESET_MSAA_X2;  break;
        case 4:  flags |= BGFX_RESET_MSAA_X4;  break;
        case 8:  flags |= BGFX_RESET_MSAA_X8;  break;
        case 16: flags |= BGFX_RESET_MSAA_X16; break;
    }
    return flags;
}

static void BgfxApplyReset() {
    if (!g_pSDLWindow) return;
    int w = 0, h = 0;
    SDL_GetWindowSize(g_pSDLWindow, &w, &h);
    if (w <= 0 || h <= 0) return;
    bgfx::reset((uint32_t)w, (uint32_t)h, BgfxResetFlags());
    bgfx::setViewRect(0, 0, 0, (uint16_t)w, (uint16_t)h);
    // ImGui lives on view 2 (sharp on top of any CRT pass on view 1).
    // bgfx skips views with no rect set, so size it to the backbuffer.
    bgfx::setViewRect(2, 0, 0, (uint16_t)w, (uint16_t)h);
}
#endif

// Pushes g_windowResolution + g_windowMode at the SDL window. Mode change
// drops fullscreen first (if set), resizes, then re-applies the new mode.
void ApplyDisplayMode() {
    if (!g_pSDLWindow) return;

    SDL_SetWindowFullscreen(g_pSDLWindow, 0);

    if (g_windowResolution > 0) {
        // 16:9 width derived from vertical pixel count.
        int h = g_windowResolution;
        int w = (h * 16) / 9;
        SDL_SetWindowSize(g_pSDLWindow, w, h);
        SDL_SetWindowPosition(g_pSDLWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }

    Uint32 flags = 0;
    if (g_windowMode == 1) flags = SDL_WINDOW_FULLSCREEN_DESKTOP;
    else if (g_windowMode == 2) flags = SDL_WINDOW_FULLSCREEN;
    if (flags) SDL_SetWindowFullscreen(g_pSDLWindow, flags);
}
bool g_scrollToSelected = false; // set true when 3D click selects a node
// g_bWireframe lives in dashapp.cpp; converted to D3DRS_FILLMODE per frame.
// Don't poke glPolygonMode here, the per-frame reset clobbers it.
extern bool g_bWireframe;

// True while libmpv is rendering fullscreen video. Cleared by Esc/B.
bool g_mediaFullscreen = false;
char g_mediaFullscreenTitle[256] = "";
char g_mediaFullscreenSubtitle[256] = "";
// Latched pulse: media_ui::MediaUI_StopFullscreen sets this when user leaves
// playback. CMediaCollection::ConsumePlaybackExited reads-and-clears it.
int g_mediaPlaybackExited = 0;

// g_xapEditorOpen is now in xap_editor.cpp

// XAP Editor; now a floating ImGui window, no separate SDL window needed
void CreateXapEditorWindow() { /* no-op: editor is an ImGui panel now */ }
void DestroyXapEditorWindow() { /* no-op */ }

// g_extractedMode and inline editing state are now in xap_editor.cpp

// XapEditor_LoadFile, XapEditor_SaveFile, XapEditor_ScanDir,
// XapEditor_GetFileList are now in xap_editor.cpp

// CountNodes, SetVisibilityRecursive, FindNodeDefName*, MatchesFilter,
// SubtreeMatchesFilter, RenderNodeTree, RenderInspectorPanel, DrawSelectionHighlight
// are now in inspector.cpp

// ReloadSceneFromEditor is now in xap_editor.cpp

static void crash_handler(int sig) {
    fprintf(stderr, "\n[CRASH] Signal %d (%s)\n", sig, sig == SIGSEGV ? "SIGSEGV" : sig == SIGABRT ? "SIGABRT" : "OTHER");
    fflush(stderr);
#ifdef HAS_BACKTRACE
    void* bt[64];
    int n = backtrace(bt, 64);
    backtrace_symbols_fd(bt, n, 2);
#endif
#ifndef _WIN32
    _exit(128 + sig);
#else
    exit(128 + sig);
#endif
}

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
static LONG WINAPI WindowsCrashHandler(EXCEPTION_POINTERS* ep) {
    fprintf(stderr, "\n[CRASH] Exception 0x%08lX at address 0x%p\n",
            ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2) {
        fprintf(stderr, "[CRASH] %s address 0x%p\n",
                ep->ExceptionRecord->ExceptionInformation[0] ? "Writing" : "Reading",
                (void*)ep->ExceptionRecord->ExceptionInformation[1]);
    }
    // Walk the stack
    HANDLE process = GetCurrentProcess();
    SymInitialize(process, NULL, TRUE);
    STACKFRAME64 frame = {};
    CONTEXT* ctx = ep->ContextRecord;
    frame.AddrPC.Offset = ctx->Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = ctx->Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = ctx->Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
    fprintf(stderr, "[CRASH] Stack trace:\n");
    for (int i = 0; i < 32; i++) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(), &frame, ctx, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL))
            break;
        char symBuf[sizeof(SYMBOL_INFO) + 256];
        SYMBOL_INFO* sym = (SYMBOL_INFO*)symBuf;
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 255;
        DWORD64 disp = 0;
        if (SymFromAddr(process, frame.AddrPC.Offset, &disp, sym))
            fprintf(stderr, "  [%d] %s + 0x%llx\n", i, sym->Name, (unsigned long long)disp);
        else
            fprintf(stderr, "  [%d] 0x%p\n", i, (void*)frame.AddrPC.Offset);
    }
    fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif



// Inspector panel (DrawSelectionHighlight, RenderNodeTree, RenderInspectorPanel,
// etc.) is now in inspector.cpp
// RenderXAPEditor is now in xap_editor.cpp

// Preloader UI and XIP extraction are now in preloader.cpp

int main(int argc, char* argv[]) {
    g_argc = argc;
    g_argv = argv;
    s_execPath = argv[0];

#ifdef _WIN32
    // /SUBSYSTEM:WINDOWS so explorer launches don't pop a console, but
    // attach to a parent shell when one exists so stdout/stderr show up.
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
        freopen("CONIN$", "r", stdin);
    }

    //Milenko-Testing: I don't run windows, i googled this. don't know if it will work as intended.
    SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
    DwmEnableMMCSS(TRUE);
    {
        DWORD taskIdx = 0;
        AvSetMmThreadCharacteristicsW(L"Games", &taskIdx);
    }
    typedef BOOL (WINAPI *SetProcessInfoFn)(HANDLE, PROCESS_INFORMATION_CLASS, LPVOID, DWORD);
    HMODULE hKernel = GetModuleHandleA("kernel32.dll");
    if (hKernel) {
        SetProcessInfoFn pSetProcessInformation =
            (SetProcessInfoFn)GetProcAddress(hKernel, "SetProcessInformation");
        if (pSetProcessInformation) {
            PROCESS_POWER_THROTTLING_STATE pt = {0};
            pt.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
            pt.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
            pt.StateMask = 0; // 0 == opt out of throttling
            pSetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &pt, sizeof(pt));
        }
    }
#endif

    // Change working directory to the executable's directory so that
    // relative paths (xboxfs/, etc.) work when launched from Finder/Explorer.
    {
        char exeDir[1024];
#ifdef _WIN32
        // GetModuleFileName + strip filename
        DWORD len = GetModuleFileNameA(NULL, exeDir, sizeof(exeDir));
        if (len > 0) {
            char* last = strrchr(exeDir, '\\');
            if (last) *last = '\0';
            _chdir(exeDir);
        }
#elif defined(__APPLE__)
        uint32_t sz = sizeof(exeDir);
        if (_NSGetExecutablePath(exeDir, &sz) == 0) {
            char* last = strrchr(exeDir, '/');
            if (last) *last = '\0';
            chdir(exeDir);
        }
#else
        ssize_t len = readlink("/proc/self/exe", exeDir, sizeof(exeDir) - 1);
        if (len > 0) {
            exeDir[len] = '\0';
            char* last = strrchr(exeDir, '/');
            if (last) *last = '\0';
            chdir(exeDir);
        }
#endif
    }


    // This fixes compilation so it works for both GCC and clang
#if defined(__SANITIZE_ADDRESS__)
    #define ASAN_ACTIVE 1
#elif defined(__has_feature)
    #if __has_feature(address_sanitizer)
        #define ASAN_ACTIVE 1
    #endif
#endif

#if !defined(ASAN_ACTIVE)
    // ASan installs its own SIGSEGV/SIGBUS handlers; ours would eat them.
    // Skip our handler entirely on asan builds.
#  ifdef _WIN32
    SetUnhandledExceptionFilter(WindowsCrashHandler);
#  else
    signal(SIGSEGV, crash_handler);
    signal(SIGBUS, crash_handler);
#  endif
    signal(SIGABRT, crash_handler);
#endif

#undef ASAN_ACTIVE // Clean up

    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    // Load before window create so g_msaaSamples is honored at boot.
    LoadDesktopSettings();
    // Materialize the ini on first run / after adding new keys.
    SaveDesktopSettings();
    extern void Playlist_LoadAll();
    Playlist_LoadAll();
    MediaDB_LoadCache();

    // GL build wraps an OpenGL drawable into the window. BGFX leaves it
    // plain so the backend layer (CAMetalLayer / Vulkan) can attach.
    Uint32 windowFlags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
#ifndef THESEUS_USE_BGFX
    windowFlags |= SDL_WINDOW_OPENGL;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, g_msaaSamples > 0 ? 1 : 0);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, g_msaaSamples);
#endif

    g_pSDLWindow = SDL_CreateWindow(
        "UIX Desktop - Preview",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 720,
        windowFlags
    );

#ifndef THESEUS_USE_BGFX
    if (!g_pSDLWindow) {
        // Fallback 1: drop MSAA but keep GL 3.2 core + double buffer
        fprintf(stderr, "SDL_CreateWindow failed (%s), retrying without MSAA...\n", SDL_GetError());
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
        g_pSDLWindow = SDL_CreateWindow(
            "UIX Desktop - Preview",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            1280, 720,
            windowFlags
        );
    }

    if (!g_pSDLWindow) {
        // Fallback 2: drop to GL 3.0 compatibility profile (Xvfb / software renderers)
        fprintf(stderr, "SDL_CreateWindow failed (%s), retrying with compatibility profile...\n", SDL_GetError());
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
        g_pSDLWindow = SDL_CreateWindow(
            "UIX Desktop - Preview",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            1280, 720,
            windowFlags
        );
    }
#endif

    if (!g_pSDLWindow) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // Window icon (TeamUIX logo, embedded).
    {
        int iw, ih, ich;
        unsigned char *px = stbi_load_from_memory(teamuix_png, (int)teamuix_png_len, &iw, &ih, &ich, 4);
        if (px) {
            SDL_Surface *ico = SDL_CreateRGBSurfaceFrom(px, iw, ih, 32, iw * 4,
                0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);
            if (ico) {
                SDL_SetWindowIcon(g_pSDLWindow, ico);
                SDL_FreeSurface(ico);
            }
            stbi_image_free(px);
        }
    }

#ifndef THESEUS_USE_BGFX
    // Create OpenGL context
    g_pGLContext = SDL_GL_CreateContext(g_pSDLWindow);
    if (!g_pGLContext) {
        fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(g_pSDLWindow);
        SDL_Quit();
        return 1;
    }

    ApplyVsyncMode();
#endif

#ifdef THESEUS_USE_BGFX
    // bgfx::init. nwh = NSWindow* on macOS, HWND on Win32, Window on X11.
    {
        SDL_SysWMinfo wmi;
        SDL_VERSION(&wmi.version);
        if (!SDL_GetWindowWMInfo(g_pSDLWindow, &wmi)) {
            fprintf(stderr, "SDL_GetWindowWMInfo failed: %s\n", SDL_GetError());
        } else {
            bgfx::PlatformData pd;
            std::memset(&pd, 0, sizeof(pd));
#  if defined(__APPLE__)
            pd.nwh = wmi.info.cocoa.window;
#  elif defined(_WIN32)
            pd.nwh = wmi.info.win.window;
#  else
            // Branch on SDL's actual subsystem. Feeding x11 lanes to
            // bgfx on a Wayland session hands RADV a NULL Display and
            // it crashes in vkGetPhysicalDeviceSurfaceSupportKHR.
            if (wmi.subsystem == SDL_SYSWM_WAYLAND) {
                pd.ndt  = wmi.info.wl.display;
                pd.nwh  = wmi.info.wl.surface;
                pd.type = bgfx::NativeWindowHandleType::Wayland;
            } else {
                pd.ndt  = wmi.info.x11.display;
                pd.nwh  = (void*)(uintptr_t)wmi.info.x11.window;
                pd.type = bgfx::NativeWindowHandleType::Default; // X11
            }
#  endif
            bgfx::Init bgfxInit;
            bgfxInit.platformData       = pd;
            bgfxInit.resolution.width   = 1280;
            bgfxInit.resolution.height  = 720;
            bgfxInit.resolution.reset   = BgfxResetFlags();
            // Renderer pick. User override via Config.ini Renderer key
            // (auto / d3d11 / vulkan / opengl). Default is auto, which
            // lets bgfx pick the best backend for the host (Metal on
            // macOS, D3D11 on Windows, Vulkan on Linux).
            switch (g_rendererPref) {
                case 1: bgfxInit.type = bgfx::RendererType::Direct3D11; break;
                case 2: bgfxInit.type = bgfx::RendererType::Vulkan;     break;
                case 3: bgfxInit.type = bgfx::RendererType::OpenGL;     break;
                default: bgfxInit.type = bgfx::RendererType::Count;     break;
            }
            if (!bgfx::init(bgfxInit)) {
                fprintf(stderr, "bgfx::init failed\n");
            } else {
                const bgfx::Caps* caps = bgfx::getCaps();
                fprintf(stdout, "bgfx initialized: %s\n",
                        bgfx::getRendererName(caps->rendererType));

                // Link the shader programs we use across the run.
                {
                    bgfx::ShaderHandle vsh = theseus_bgfx_load_shader("vs_simple");
                    bgfx::ShaderHandle fsh = theseus_bgfx_load_shader("fs_simple");
                    if (bgfx::isValid(vsh) && bgfx::isValid(fsh)) {
                        bgfx::ProgramHandle prog = bgfx::createProgram(vsh, fsh, true);
                        if (bgfx::isValid(prog))
                            fprintf(stdout, "bgfx: vs_simple/fs_simple program linked\n");
                        else
                            fprintf(stderr, "bgfx: simple program link failed\n");
                    }
                }
                {
                    bgfx::ShaderHandle vsh = theseus_bgfx_load_shader("vs_ff");
                    bgfx::ShaderHandle fsh = theseus_bgfx_load_shader("fs_ff");
                    if (bgfx::isValid(vsh) && bgfx::isValid(fsh)) {
                        bgfx::ProgramHandle prog = bgfx::createProgram(vsh, fsh, true);
                        if (bgfx::isValid(prog)) {
                            g_bgfxProgFF = prog;
                            fprintf(stdout, "bgfx: vs_ff/fs_ff program linked (FF emulator)\n");
                        } else {
                            fprintf(stderr, "bgfx: ff program link failed\n");
                        }
                    }
                }
                {
                    bgfx::ShaderHandle vsh = theseus_bgfx_load_shader("vs_blit");
                    bgfx::ShaderHandle fsh = theseus_bgfx_load_shader("fs_blit");
                    if (bgfx::isValid(vsh) && bgfx::isValid(fsh)) {
                        bgfx::ProgramHandle prog = bgfx::createProgram(vsh, fsh, true);
                        if (bgfx::isValid(prog)) {
                            g_bgfxProgBlit = prog;
                            g_bgfxSamplerBlit = bgfx::createUniform("s_blit", bgfx::UniformType::Sampler);
                            fprintf(stdout, "bgfx: vs_blit/fs_blit program linked\n");
                        } else {
                            fprintf(stderr, "bgfx: blit program link failed\n");
                        }
                    }
                }

                // View 0 viewport. Required for shadow-submit (chunk 5a)
                // to be a valid draw; bgfx skips submits to a view with
                // no rect. ApplyBgfxResize bumps these to window size as
                // soon as we have one; placeholders here for the first
                // frame.
                bgfx::setViewRect(0, 0, 0, 1280, 720);
                bgfx::setViewRect(2, 0, 0, 1280, 720); // ImGui

                // The dashboard renders D3D-style: it expects every draw
                // to land on the framebuffer in the order it was issued,
                // because most of the UI is alpha-blended over the scene
                // with depth-test off (so painter's-algorithm order is
                // what determines what's on top). bgfx's default view
                // mode reorders submits by state for batching, which
                // shuffles the painter order and corrupts everything
                // that overlays. Force Sequential to match GL.
                bgfx::setViewMode(0, bgfx::ViewMode::Sequential);

                // Chunk 5b: create FF uniform + sampler handles. Names
                // must match the .sc shader sources exactly.
                g_bgfxFF.u_FfWVP          = bgfx::createUniform("u_FfWVP",          bgfx::UniformType::Mat4);
                g_bgfxFF.u_FfWorldView    = bgfx::createUniform("u_FfWorldView",    bgfx::UniformType::Mat4);
                g_bgfxFF.u_FfNormalInv    = bgfx::createUniform("u_FfNormalInv",    bgfx::UniformType::Mat4);
                g_bgfxFF.u_FfFalloffFront = bgfx::createUniform("u_FfFalloffFront", bgfx::UniformType::Vec4);
                g_bgfxFF.u_FfFalloffDelta = bgfx::createUniform("u_FfFalloffDelta", bgfx::UniformType::Vec4);
                g_bgfxFF.u_FfTFactor      = bgfx::createUniform("u_FfTFactor",      bgfx::UniformType::Vec4);
                g_bgfxFF.u_FfMatDiffuse   = bgfx::createUniform("u_FfMatDiffuse",   bgfx::UniformType::Vec4);
                g_bgfxFF.u_FfFlags1       = bgfx::createUniform("u_FfFlags1",       bgfx::UniformType::Vec4);
                g_bgfxFF.u_FfFlags2       = bgfx::createUniform("u_FfFlags2",       bgfx::UniformType::Vec4);
                g_bgfxFF.u_FfViewportSize = bgfx::createUniform("u_FfViewportSize", bgfx::UniformType::Vec4);
                g_bgfxFF.u_FfFragFlags1   = bgfx::createUniform("u_FfFragFlags1",   bgfx::UniformType::Vec4);
                g_bgfxFF.u_FfFragFlags2   = bgfx::createUniform("u_FfFragFlags2",   bgfx::UniformType::Vec4);
                g_bgfxFF.s_tex0           = bgfx::createUniform("s_tex0",           bgfx::UniformType::Sampler);
                g_bgfxFF.s_tex1           = bgfx::createUniform("s_tex1",           bgfx::UniformType::Sampler);

                // 1x1 white placeholder for stages without a real texture.
                {
                    static const uint8_t white[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
                    g_bgfxWhiteTex = bgfx::createTexture2D(
                        1, 1, false, 1,
                        bgfx::TextureFormat::RGBA8,
                        BGFX_TEXTURE_NONE | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
                        bgfx::copy(white, 4));
                }

                // CRT post-process program + uniforms. The offscreen
                // framebuffer itself gets allocated lazily on the first
                // frame the user enables the toggle.
                if (!InitCRTShader_Bgfx(1280, 720))
                    fprintf(stderr, "[CRT] bgfx init failed; effect will stay disabled\n");
            }
        }
    }
#endif

    fprintf(stdout, "UIX Desktop - SDL/OpenGL Preview Tool\n");
#ifndef THESEUS_USE_BGFX
    fprintf(stdout, "OpenGL: %s\n", glGetString(GL_VERSION));
    fprintf(stdout, "Renderer: %s\n", glGetString(GL_RENDERER));
#endif
    fprintf(stdout, "F1: Toggle Debug Tools | F2: XAP Editor | F10: Toggle Menu Bar\n");
    fflush(stdout); fflush(stderr);

#if defined(_WIN32) && !defined(THESEUS_USE_BGFX)
    // Initialize GLEW (must be called after GL context creation, before any GL calls)
    glewExperimental = GL_TRUE;
    GLenum glewErr = glewInit();
    if (glewErr != GLEW_OK) {
        fprintf(stderr, "glewInit() failed: %s\n", glewGetErrorString(glewErr));
        SDL_GL_DeleteContext(g_pGLContext);
        SDL_DestroyWindow(g_pSDLWindow);
        SDL_Quit();
        return 1;
    }
    // Clear any GL error set by glewInit (common with core profile)
    glGetError();
#endif

#ifndef THESEUS_USE_BGFX
    // Enable MSAA
    glEnable(GL_MULTISAMPLE);

    // Set viewport for 3D rendering (left portion only)
    glViewport(0, 0, 1280, 720);

    // Initialize shaders and GL state
    if (!InitGLShaders()) {
        fprintf(stderr, "InitGLShaders() failed!\n");
        SDL_GL_DeleteContext(g_pGLContext);
        SDL_DestroyWindow(g_pSDLWindow);
        SDL_Quit();
        return 1;
    }

    // Initialize CRT post-process shader
    InitCRTShader(1280, 720);
#endif

    // Initialize Dear ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Xbox-green ImGui theme. High-contrast green text, subtle green tint on
    // chrome, brighter green on active states. Tight spacing, square corners.
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* c = style.Colors;

    const ImVec4 kBgDeep   = ImVec4(0.02f, 0.05f, 0.02f, 0.96f);
    const ImVec4 kBgChrome = ImVec4(0.04f, 0.12f, 0.05f, 0.96f);
    const ImVec4 kBgFrame  = ImVec4(0.05f, 0.10f, 0.05f, 0.85f);
    const ImVec4 kAccent   = ImVec4(0.20f, 0.55f, 0.22f, 1.00f);
    const ImVec4 kAccentHi = ImVec4(0.30f, 0.75f, 0.30f, 1.00f);
    const ImVec4 kAccentLo = ImVec4(0.10f, 0.30f, 0.12f, 0.80f);
    const ImVec4 kText     = ImVec4(0.85f, 1.00f, 0.85f, 1.00f);
    const ImVec4 kTextDim  = ImVec4(0.55f, 0.78f, 0.55f, 0.85f);
    const ImVec4 kBorder   = ImVec4(0.15f, 0.35f, 0.18f, 0.80f);

    c[ImGuiCol_Text]                 = kText;
    c[ImGuiCol_TextDisabled]         = kTextDim;
    c[ImGuiCol_WindowBg]             = kBgDeep;
    c[ImGuiCol_PopupBg]              = kBgDeep;
    c[ImGuiCol_Border]               = kBorder;
    c[ImGuiCol_FrameBg]              = kBgFrame;
    c[ImGuiCol_FrameBgHovered]       = ImVec4(0.10f, 0.20f, 0.10f, 0.95f);
    c[ImGuiCol_FrameBgActive]        = ImVec4(0.12f, 0.28f, 0.14f, 1.00f);
    c[ImGuiCol_TitleBg]              = kBgChrome;
    c[ImGuiCol_TitleBgActive]        = ImVec4(0.06f, 0.22f, 0.08f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.04f, 0.10f, 0.05f, 0.85f);
    c[ImGuiCol_MenuBarBg]            = kBgChrome;
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0.02f, 0.05f, 0.02f, 0.60f);
    c[ImGuiCol_ScrollbarGrab]        = kAccentLo;
    c[ImGuiCol_ScrollbarGrabHovered] = kAccent;
    c[ImGuiCol_ScrollbarGrabActive]  = kAccentHi;
    c[ImGuiCol_CheckMark]            = ImVec4(0.40f, 1.00f, 0.40f, 1.00f);
    c[ImGuiCol_SliderGrab]           = kAccent;
    c[ImGuiCol_SliderGrabActive]     = kAccentHi;
    c[ImGuiCol_Button]               = kAccentLo;
    c[ImGuiCol_ButtonHovered]        = kAccent;
    c[ImGuiCol_ButtonActive]         = kAccentHi;
    c[ImGuiCol_Header]               = kAccentLo;
    c[ImGuiCol_HeaderHovered]        = kAccent;
    c[ImGuiCol_HeaderActive]         = kAccentHi;
    c[ImGuiCol_Separator]            = kBorder;
    c[ImGuiCol_SeparatorHovered]     = kAccent;
    c[ImGuiCol_SeparatorActive]      = kAccentHi;
    c[ImGuiCol_Tab]                  = ImVec4(0.05f, 0.15f, 0.07f, 0.85f);
    c[ImGuiCol_TabHovered]           = kAccent;
    c[ImGuiCol_TabActive]            = ImVec4(0.15f, 0.40f, 0.18f, 1.00f);
    c[ImGuiCol_TabUnfocused]         = ImVec4(0.05f, 0.10f, 0.05f, 0.80f);
    c[ImGuiCol_TabUnfocusedActive]   = ImVec4(0.10f, 0.25f, 0.10f, 0.90f);
    c[ImGuiCol_PlotHistogram]        = kAccent;
    c[ImGuiCol_PlotHistogramHovered] = kAccentHi;
    c[ImGuiCol_DragDropTarget]       = kAccentHi;
    c[ImGuiCol_NavHighlight]         = kAccentHi;

    style.WindowRounding    = 0.0f;
    style.FrameRounding     = 2.0f;
    style.GrabRounding      = 2.0f;
    style.PopupRounding     = 0.0f;
    style.TabRounding       = 2.0f;
    style.ScrollbarRounding = 2.0f;
    style.WindowBorderSize  = 1.0f;
    style.FrameBorderSize   = 0.0f;
    style.ItemSpacing       = ImVec2(8, 6);
    style.FramePadding      = ImVec2(8, 4);
    style.WindowPadding     = ImVec2(12, 10);
    style.IndentSpacing     = 18.0f;

#ifndef THESEUS_USE_BGFX
    ImGui_ImplSDL2_InitForOpenGL(g_pSDLWindow, g_pGLContext);
    ImGui_ImplOpenGL3_Init("#version 150");
#else
    // SDL2 feeds events / DPI; no GL context under BGFX.
    ImGui_ImplSDL2_InitForOther(g_pSDLWindow);
    {
        // View layout under bgfx:
        //   view 0 = dashboard 3D scene (FF emulator submits)
        //   view 1 = CRT post-process blit (only touched when g_crt.enabled)
        //   view 2 = ImGui (menu bar, panels, libmpv via ImGui::AddImage)
        // ImGui always lives on view 2 so it draws sharp on top of the
        // post-processed scene, matching the GL backend's behavior.
        extern bool ImGui_ImplBgfx_Init(unsigned char);
        if (!ImGui_ImplBgfx_Init(2))
            fprintf(stderr, "ImGui_ImplBgfx_Init failed!\n");
    }
#endif

    // Check for CLI flags
    float cliUiScale = 0.0f; // 0 = unset, default 1.0 if not specified
    bool cliFullscreen = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dashboard") == 0) g_startupMode = 1;
        else if (strcmp(argv[i], "--development") == 0) g_startupMode = 2;
        else if (strcmp(argv[i], "--preview") == 0) g_startupMode = 1; // legacy compat
        else if (strcmp(argv[i], "--muted") == 0) g_audioMuted = true;
        else if (strcmp(argv[i], "--minimized") == 0) g_startMinimized = true;
        else if (strcmp(argv[i], "--graphics-debug") == 0) g_graphicsDebug = true;
        else if (strcmp(argv[i], "--fullscreen") == 0) cliFullscreen = true;
        else if (strcmp(argv[i], "--no-toolbar") == 0) g_showMenuBar = false;
        else if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc) {
            cliUiScale = (float)atof(argv[++i]);
        }
        else if (strcmp(argv[i], "--4k") == 0) {
            // 4K mode: fullscreen + 2x UI scale. Native 4K backbuffer +
            // ImGui at 2x feels right on a 27"+ 3840x2160 panel.
            cliFullscreen = true;
            if (cliUiScale == 0.0f) cliUiScale = 2.0f;
        }
    }

    // Apply CLI overrides. --fullscreen forces exclusive mode (DWM bypass for
    // AMD Adrenaline / NVIDIA overlay recognition); persisted state wins
    // otherwise. ApplyDisplayMode handles either path.
    if (cliFullscreen) g_windowMode = 2;
    if (g_pSDLWindow) ApplyDisplayMode();
    if (cliUiScale > 0.0f) {
        ImGui::GetIO().FontGlobalScale = cliUiScale;
        fprintf(stderr, "[ui] FontGlobalScale = %.2f\n", cliUiScale);
    }
    if (g_startMinimized && g_pSDLWindow) {
        SDL_MinimizeWindow(g_pSDLWindow);
    }
    if (g_graphicsDebug) {
        DumpGraphicsInfo();
    }

    // Show startup mode selector (skipped if preference saved or CLI override)
    g_extractedMode = RunPreloader(g_pSDLWindow);

    // Update window title
    SDL_SetWindowTitle(g_pSDLWindow, g_extractedMode
        ? "UIX Desktop - Development Mode"
        : "UIX Desktop");

    // Cold-boot animation. Plays once before dashboard init mirrors what the
    // original Xbox does on power-on. Skipped in dev mode (the dashboard
    // isn't the focus there) and toggleable via Settings > Show Boot Animation.
    if (g_bShowBootAnimation && !g_extractedMode) {
        BootAnim_PlayAndWait(g_pSDLWindow,
            "Configs/xbox_boot.mp4");
    }

    // Initialize app globals
    g_dwMainThreadId = GetCurrentThreadId();
    g_dwStartTick = g_dwFrameTick = GetTickCount();
    g_now = 0.0;

    if (!InitApp()) {
        fprintf(stderr, "InitApp() failed!\n");
        CleanupApp();
#ifndef THESEUS_USE_BGFX
        ImGui_ImplOpenGL3_Shutdown();
#else
        extern void ImGui_ImplBgfx_Shutdown();
        ImGui_ImplBgfx_Shutdown();
#endif
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
#ifndef THESEUS_USE_BGFX
        SDL_GL_DeleteContext(g_pGLContext);
#endif
        SDL_DestroyWindow(g_pSDLWindow);
        SDL_Quit();
        return 1;
    }

    fprintf(stdout, "InitApp() succeeded - entering main loop\n");

    // Register pluggable launcher modules (shell, url, future:
    // xemu, retroarch, steam, ...). Done before the dispatcher
    // is exercised so any subsequent DesktopLaunch* call goes
    // through the registry.
    {
        extern void Launchers_RegisterAll();
        Launchers_RegisterAll();
    }

    // Synthesize TitleMeta.xbx for every Title Maker entry so the Memory
    // section renders pods. User-supplied files are left alone.
    {
        extern int UDataSynth_RebuildAll();
        UDataSynth_RebuildAll();
    }

    // Register pre-swap overlay callback for mute toast
    IDirect3DDevice8::s_preSwapCB = PreSwapOverlays;

    // Apply muted state if restarting after a game launch
    if (g_audioMuted) {
        DashAudio_MuteAll();
        g_muteOverlayTimer = 5.0f; // show mute toast on restart
    }

    // In development mode: open editor and load extracted XAPs
    if (g_extractedMode) {
        g_xapEditorOpen = true;
        XapEditor_LoadFile("Data/Xips/default/default.xap");
        if (XapEditor_HasBuffer())
            ReloadSceneFromEditor();
    }

    bool running = true;
    Uint64 fpsCapPrev = SDL_GetPerformanceCounter();
    Uint64 perfFreq = SDL_GetPerformanceFrequency();
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            // Let ImGui handle events first
            ImGui_ImplSDL2_ProcessEvent(&event);

            switch (event.type) {
                case SDL_QUIT:
                    running = false;
                    break;
                case SDL_WINDOWEVENT:
                    if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                        Uint32 mainWinID = SDL_GetWindowID(g_pSDLWindow);
                        if (event.window.windowID == mainWinID) {
                            int w = event.window.data1, h = event.window.data2;
#ifndef THESEUS_USE_BGFX
                            glViewport(0, 0, w, h);
#else
                            bgfx::reset((uint32_t)w, (uint32_t)h, BgfxResetFlags());
                            bgfx::setViewRect(0, 0, 0, (uint16_t)w, (uint16_t)h);
                            bgfx::setViewRect(2, 0, 0, (uint16_t)w, (uint16_t)h);
#endif
                            g_pp.BackBufferWidth = w;
                            g_pp.BackBufferHeight = h;
                            g_nViewWidth = (float)w;
                            g_nViewHeight = (float)h;
                            g_bProjectionDirty = true;
                        }
                    }
                    if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
                        Uint32 evWinID = event.window.windowID;
                        Uint32 mainWinID = SDL_GetWindowID(g_pSDLWindow);
                        if (evWinID == mainWinID) {
                            running = false;
                        }
                    }
                    if (event.window.windowID == SDL_GetWindowID(g_pSDLWindow)) {
                        // Tie audio to focus: silent in background / minimized,
                        // audible when the window is active. ApplyEffectiveMute
                        // honors the manual g_audioMuted on top, so a user
                        // Ctrl+M still wins.
                        switch (event.window.event) {
                        case SDL_WINDOWEVENT_FOCUS_LOST:
                        case SDL_WINDOWEVENT_MINIMIZED:
                        case SDL_WINDOWEVENT_HIDDEN:
                            g_windowFocused = false;
                            ApplyEffectiveMute();
                            break;
                        case SDL_WINDOWEVENT_FOCUS_GAINED:
                        case SDL_WINDOWEVENT_RESTORED:
                        case SDL_WINDOWEVENT_SHOWN:
                            g_windowFocused = true;
                            ApplyEffectiveMute();
                            // If the launch overlay is still up when focus
                            // returns (e.g. user clicked back to the dashboard
                            // before the fade window expired), clear it so
                            // the user sees the live scene immediately.
                            LaunchOverlay_Reset();
                            break;
                        default: break;
                        }
                    }
                    break;
                case SDL_MOUSEMOTION:
                    if (g_pD3DDev) {
                        g_pD3DDev->m_mouseX = event.motion.x;
                        g_pD3DDev->m_mouseY = event.motion.y;
                    }
                    if (g_mediaFullscreen) MediaUI_NoteActivity();
                    break;
                case SDL_MOUSEWHEEL:
                    if (g_mediaFullscreen) MediaUI_NoteActivity();
                    break;
                case SDL_MOUSEBUTTONDOWN:
                    if (g_mediaFullscreen) MediaUI_NoteActivity();
                    // Click in 3D viewport to select node (debug mode only)
                    // Only process on the main window, not inspector/editor windows
                    if (event.button.button == SDL_BUTTON_LEFT && g_debugMode && g_pD3DDev &&
                        event.button.windowID == SDL_GetWindowID(g_pSDLWindow)) {
                        ImGuiIO& mio = ImGui::GetIO();
                        if (!mio.WantCaptureMouse) {
                            int mx = event.button.x, my = event.button.y;
                            // Find the draw record under cursor (back-to-front)
                            for (int i = g_pD3DDev->m_drawRecordCount - 1; i >= 0; i--) {
                                const auto& r = g_pD3DDev->m_drawRecords[i];
                                if (mx >= r.screenMinX && mx <= r.screenMaxX &&
                                    my >= r.screenMinY && my <= r.screenMaxY &&
                                    (r.screenMaxX - r.screenMinX) >= 4 &&
                                    (r.screenMaxY - r.screenMinY) >= 4) {
                                    g_pD3DDev->m_inspectorSelectedNode = r.sceneNode;
                                    g_scrollToSelected = true;
                                    break;
                                }
                            }
                        }
                    }
                    break;
                case SDL_TEXTINPUT:
                    if (g_pActiveKeyboard && !ImGui_WantsKeyboard() && !g_bUseOnScreenKeyboard)
                        Keyboard_InsertText(g_pActiveKeyboard, event.text.text);
                    break;
                case SDL_KEYDOWN:
                    if (g_mediaFullscreen) MediaUI_NoteActivity();
                    // Esc / Q / Backspace exits fullscreen video, before any
                    // other key handling (so it works even when keyboard is
                    // captured by an XAP keyboard widget).
                    if (g_mediaFullscreen) {
                        SDL_Keycode k = event.key.keysym.sym;
                        if (k == SDLK_ESCAPE || k == SDLK_q || k == SDLK_BACKSPACE) {
                            extern void MediaUI_StopFullscreen();
                            MediaUI_StopFullscreen();
                            break;
                        }
                        if (k == SDLK_SPACE) { MediaPlayer_TogglePause(); break; }
                        if (k == SDLK_LEFT)  { MediaPlayer_SeekRelative(-5.0); break; }
                        if (k == SDLK_RIGHT) { MediaPlayer_SeekRelative( 5.0); break; }
                        if (k == SDLK_t) {
                            extern void MediaUI_ToggleTrackMenu();
                            MediaUI_ToggleTrackMenu();
                            break;
                        }
                        if (k == SDLK_LEFTBRACKET) {
                            extern void MediaUI_PlaylistPrev();
                            MediaUI_PlaylistPrev();
                            break;
                        }
                        if (k == SDLK_RIGHTBRACKET) {
                            extern void MediaUI_PlaylistNext();
                            MediaUI_PlaylistNext();
                            break;
                        }
                    }
                    if (event.key.keysym.sym == SDLK_ESCAPE && g_debugMode && g_pD3DDev) {
                        g_pD3DDev->m_inspectorSelectedNode = NULL;
                    }
                    if (g_pActiveKeyboard && !ImGui_WantsKeyboard() && !g_bUseOnScreenKeyboard)
                        Keyboard_HandleKey(g_pActiveKeyboard, event.key.keysym.sym);
                    if (event.key.keysym.sym == SDLK_F2) {
                        g_xapEditorOpen = !g_xapEditorOpen;
                        if (g_xapEditorOpen && !XapEditor_HasBuffer())
                            XapEditor_LoadFile("Data/Xips/default/default.xap");
                    }
                    if (event.key.keysym.sym == SDLK_F3) {
                        g_titleMakerOpen = !g_titleMakerOpen;
                    }
                    if (event.key.keysym.sym == SDLK_F4) {
                        ToggleSettingsWindow();
                    }
                    if (event.key.keysym.sym == SDLK_F5) {
                        g_hddBrowserOpen = !g_hddBrowserOpen;
                    }
                    if (event.key.keysym.sym == SDLK_F6) {
                        extern void TogglePlaylistMaker();
                        TogglePlaylistMaker();
                    }
                    if (event.key.keysym.sym == SDLK_F8) {
                        // D3D call-trace dump (project_d3d_call_audit.md
                        // Sweep 2). Shift+F8 resets the counters.
                        if (event.key.keysym.mod & KMOD_SHIFT)
                            Theseus_D3D_ResetTrace();
                        else
                            Theseus_D3D_DumpTrace();
                    }
                    if (event.key.keysym.sym == SDLK_F10) {
                        g_showMenuBar = !g_showMenuBar;
                    }
                    if (event.key.keysym.sym == SDLK_F11) {
                        // F11: toggle between windowed and exclusive fullscreen
                        // through the unified display state so the value persists
                        // and the in-dashboard Settings reflects it.
                        g_windowMode = (g_windowMode == 2) ? 0 : 2;
                        g_displayChangeRequested = true;
                        SaveDesktopSettings();
                    }
                    if (event.key.keysym.sym == SDLK_F12) {
                        // F12: toggle borderless windowed (no title bar /
                        // resize handles, but keeps the current window
                        // size and position). Independent from fullscreen.
                        Uint32 flags = SDL_GetWindowFlags(g_pSDLWindow);
                        bool isBorderless = (flags & SDL_WINDOW_BORDERLESS) != 0;
                        SDL_SetWindowBordered(g_pSDLWindow, isBorderless ? SDL_TRUE : SDL_FALSE);
                    }
                    if (event.key.keysym.sym == SDLK_F1) {
                        g_debugMode = !g_debugMode;
                        g_inspectorOpen = g_debugMode;
                        if (!g_debugMode && g_bWireframe) {
                            g_bWireframe = false;
#ifndef THESEUS_USE_BGFX
                            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
#endif
                        }
                        if (g_pD3DDev) {
                            g_pD3DDev->m_inspectorEnabled = g_debugMode;
                            if (!g_debugMode) {
                                g_pD3DDev->m_inspectorSelectedNode = NULL;
                                g_pD3DDev->m_inspectorHitID = -1;
                            }
                        }
                    }
                    // Ctrl+R (or Cmd+R on Mac): restart dashboard
                    if (event.key.keysym.sym == SDLK_r && (event.key.keysym.mod & (KMOD_CTRL | KMOD_GUI))) {
                        g_desktopRestartRequested = true;
                        g_desktopRestartMuted = g_audioMuted;
                    }
                    // Ctrl+M (or Cmd+M on Mac): toggle manual mute. Layered
                    // on top of the focus-based auto mute via ApplyEffectiveMute.
                    if (event.key.keysym.sym == SDLK_m && (event.key.keysym.mod & (KMOD_CTRL | KMOD_GUI))) {
                        g_audioMuted = !g_audioMuted;
                        ApplyEffectiveMute();
                        g_muteOverlayTimer = 3.0f;
                    }
                    break;
            }
        }

        // All panels are floating ImGui windows in the main window now.
        {
            SDL_Window* focusedWin = SDL_GetKeyboardFocus();
            if (focusedWin == g_pSDLWindow)
                ImGui::GetIO().AddFocusEvent(true);
        }


        // Game launch is now blocking in DesktopLaunchGame() + execv restart,
        // so no polling needed here. Handle any pending restart requests
        // (e.g. from Title Maker "Save & Restart") that still use the old flag.
        if (g_desktopRestartRequested) {
            bool muted = g_desktopRestartMuted;
            g_desktopRestartRequested = false;
            g_desktopRestartMuted = false;
            fprintf(stderr, "[Desktop] Restarting preview...%s\n", muted ? " (muted)" : "");

            // Build argv with --preview appended (if not already present)
            bool hasPreview = false;
            for (int i = 0; i < g_argc; i++)
                if (strcmp(g_argv[i], "--preview") == 0) { hasPreview = true; break; }

            char** newArgv;
            if (hasPreview) {
                newArgv = g_argv;
            } else {
                newArgv = (char**)malloc(sizeof(char*) * (g_argc + 2));
                for (int i = 0; i < g_argc; i++) newArgv[i] = g_argv[i];
                newArgv[g_argc] = (char*)"--preview";
                newArgv[g_argc + 1] = NULL;
            }

#ifdef _WIN32
            // Show window again if it was hidden (game launch)
            if (g_pSDLWindow) SDL_ShowWindow(g_pSDLWindow);
            _execv(g_argv[0], newArgv);
#else
            execv(g_argv[0], newArgv);
#endif
            // fallthrough if execv fails
            fprintf(stderr, "[Desktop] execv failed: %s\n", strerror(errno));
        }

        // While fullscreen video is up, freeze the dashboard scene VM so
        // controller input doesn't drift around the menu (joystick polling
        // still runs globally), and so animations/timers don't fast-forward
        // when the user comes back. media_ui handles its own input.
        if (!g_mediaFullscreen) Advance();

#ifndef THESEUS_USE_BGFX
        // CRT: render to FBO if enabled
        if (g_crt.enabled && g_crt.fbo) {
            int ww, wh;
            SDL_GL_GetDrawableSize(g_pSDLWindow, &ww, &wh);
            CRT_ResizeFBO(ww, wh);
            CRT_BeginCapture();
            glViewport(0, 0, ww, wh);
        }
#else
        // Same idea under bgfx: allocate / resize the offscreen FB to
        // window size, point view 0 at it so the scene draws there
        // instead of straight to the backbuffer. Always set view 0's
        // framebuffer here every frame, otherwise toggling CRT off
        // leaves view 0 stuck on the FBO from the previous frame.
        if (g_crt.enabled && bgfx::isValid(g_crt.program)) {
            int ww, wh;
            SDL_GetWindowSize(g_pSDLWindow, &ww, &wh);
            CRT_ResizeFBO_Bgfx(ww, wh);
            CRT_BeginCapture_Bgfx();
        } else {
            CRT_PointViewToBackbuffer_Bgfx();
        }
#endif

        // Render 3D scene (viewport set to left portion; Present() skips swap when inspector is open)
        Uint64 tDraw0 = SDL_GetPerformanceCounter();
        if (g_mediaFullscreen) {
            extern void MediaUI_DrawFullscreenVideo();
            MediaUI_DrawFullscreenVideo();
        } else {
            Draw();
        }
        Uint64 tDraw1 = SDL_GetPerformanceCounter();

        // Force fill before ImGui so overlays render solid. Must go through
        // the wrapper, otherwise the wrapper cache stays at WIREFRAME and
        // the next frame's set is short-circuited.
        if (g_bWireframe && g_pD3DDev)
            TheseusSetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);

#ifndef THESEUS_USE_BGFX
        // CRT: blit with post-process shader
        if (g_crt.enabled && g_crt.fbo) {
            s_crtTime += 0.016f;
            CRT_EndAndBlit(s_crtTime);
        }
#else
        if (g_crt.enabled && bgfx::isValid(g_crt.program) && bgfx::isValid(g_crt.fb)) {
            s_crtTime += 0.016f;
            CRT_EndAndBlit_Bgfx(s_crtTime, g_crt.texW, g_crt.texH);
        }
#endif

        // Overlays (menu bar, inspector, Title Maker, etc.); always render
        Uint64 tImgui0 = SDL_GetPerformanceCounter();
        if (IDirect3DDevice8::s_preSwapCB) IDirect3DDevice8::s_preSwapCB();
        Uint64 tImgui1 = SDL_GetPerformanceCounter();

        // FPS cap: sleep until budget elapsed. 0 = unlimited.
        if (g_fpsCap > 0) {
            Uint64 budget = perfFreq / (Uint64)g_fpsCap;
            Uint64 now = SDL_GetPerformanceCounter();
            Uint64 elapsed = now - fpsCapPrev;
            if (elapsed < budget) {
                Uint32 ms = (Uint32)((budget - elapsed) * 1000 / perfFreq);
                if (ms > 0) SDL_Delay(ms);
            }
            fpsCapPrev = SDL_GetPerformanceCounter();
        }

        // Swap
        Uint64 tSwap0 = SDL_GetPerformanceCounter();
#ifndef THESEUS_USE_BGFX
        if (g_pSDLWindow) SDL_GL_SwapWindow(g_pSDLWindow);
#else
        // Chunk 5d-2: bgfx now owns presentation. There is no GL
        // drawable on the window so SDL_GL_SwapWindow is gone; bgfx's
        // Metal layer becomes the visible contentView.
        bgfx::frame();
#endif
        Uint64 tSwap1 = SDL_GetPerformanceCounter();

        if (g_graphicsDebug) {
            double freq = (double)perfFreq;
            g_perfDrawMs  = (tDraw1 - tDraw0)   * 1000.0 / freq;
            g_perfImguiMs = (tImgui1 - tImgui0) * 1000.0 / freq;
            g_perfSwapMs  = (tSwap1 - tSwap0)   * 1000.0 / freq;
            g_perfFrameMs = (tSwap1 - tDraw0)   * 1000.0 / freq;
            double instFps = (g_perfFrameMs > 0.0) ? 1000.0 / g_perfFrameMs : 0.0;
            g_perfFps = (g_perfFps == 0.0) ? instFps : 0.9 * g_perfFps + 0.1 * instFps;
        }

        // (Old code re-applied Mix_Volume(-1, 0) every frame while muted.
        //  Removed: rapid per-frame volume writes on playing channels were
        //  creating audible discontinuities. New sounds started while muted
        //  are silenced inside DashAudio_PlaySound's s_muted check, so the
        //  per-frame hammer wasn't actually needed.)
        if (g_muteOverlayTimer > 0.0f) g_muteOverlayTimer -= 0.016f;

        // Inspector, XAP editor, Title Maker all render via PreSwapOverlays callback

        // Handle XAP scene reload (must happen outside rendering)
        if (XapEditor_ConsumeReloadRequest()) {
            ReloadSceneFromEditor();
        }

        if (g_vsyncChangeRequested) {
            g_vsyncChangeRequested = false;
#ifndef THESEUS_USE_BGFX
            ApplyVsyncMode();
#else
            BgfxApplyReset();
#endif
        }

        if (g_displayChangeRequested) {
            g_displayChangeRequested = false;
            ApplyDisplayMode();
        }

        // Handle MSAA change. GL path tears down + recreates the GL
        // context with new sample count (heavy: reloads shaders, re-
        // uploads textures). bgfx path is a single bgfx::reset call;
        // bgfx keeps all GPU resources alive across the reset.
#ifndef THESEUS_USE_BGFX
        if (g_msaaChangeRequested) {
            g_msaaChangeRequested = false;

            // Shutdown ImGui GL backend (uses old context)
            ImGui_ImplOpenGL3_Shutdown();

            // Destroy old GL context
            SDL_GL_DeleteContext(g_pGLContext);

            // Set new MSAA attributes
            if (g_msaaSamples > 0) {
                SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
                SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, g_msaaSamples);
            } else {
                SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
                SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
            }

            // Create new GL context on the same window
            g_pGLContext = SDL_GL_CreateContext(g_pSDLWindow);
            if (!g_pGLContext) {
                fprintf(stderr, "[MSAA] Failed to create new GL context! Falling back...\n");
                SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
                SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
                g_pGLContext = SDL_GL_CreateContext(g_pSDLWindow);
                g_msaaSamples = 0;
            }
            ApplyVsyncMode();

            // Enable/disable MSAA
            if (g_msaaSamples > 0) glEnable(GL_MULTISAMPLE);
            else glDisable(GL_MULTISAMPLE);

            // Reinit shaders and GL state
            memset(&g_gl, 0, sizeof(g_gl));
            InitGLShaders();
            InitCRTShader(1280, 720);

            // Re-upload all textures and buffers from CPU memory
            ReuploadAllGLResources();

            // Reinit ImGui GL backend
            ImGui_ImplOpenGL3_Init("#version 150");

            // Restore viewport (full main window; inspector is separate)
            int winW, winH;
            SDL_GetWindowSize(g_pSDLWindow, &winW, &winH);
            glViewport(0, 0, winW, winH);

        }
#else
        // bgfx::reset keeps all resources alive; MSAA toggle is one call.
        if (g_msaaChangeRequested) {
            g_msaaChangeRequested = false;
            BgfxApplyReset();
        }
#endif
    }

    // Cleanup
    DestroyXapEditorWindow();
    XapEditor_Cleanup();
#ifndef THESEUS_USE_BGFX
    ImGui_ImplOpenGL3_Shutdown();
#else
    {
        extern void ImGui_ImplBgfx_Shutdown();
        ImGui_ImplBgfx_Shutdown();
    }
#endif
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    CleanupApp();
#ifndef THESEUS_USE_BGFX
    SDL_GL_DeleteContext(g_pGLContext);
#else
    // Intentionally NOT calling bgfx::shutdown() here. Several static
    // CXipFile / CMesh / CTexture instances have destructors that run
    // AFTER main returns and call bgfx::destroy on their handles; if
    // shutdown ran first those destroys segfault on a dead context.
    // The OS reclaims everything at process exit.
#endif
    SDL_DestroyWindow(g_pSDLWindow);
    SDL_Quit();

    return 0;
}
