// panic_screen.cpp: D3D8-direct recovery UI. Triggered when init
// fails: bypasses the scene graph, the XAP VM, and any XIP-resident
// asset; keeps FTP up and dumps a panic log to Q:\Logs\panic-*.txt
// so the user can recover. Theseus-original.

#include "std.h"
#include "panic_screen.h"
#include "theseus.h"
#include "network.h"
#include "toolbox/ftpServer.h"
#include <stdio.h>
#include <time.h>
#include <math.h>

extern float OverlayFontDraw(const TCHAR* text, float x, float y, float pixelHeight, D3DCOLOR color);
extern float OverlayFontMeasure(const TCHAR* text, float pixelHeight);

namespace {

// XIPs the dashboard expects to find on disk. Used for the file-integrity
// readout so the user can tell at a glance which archive is missing.
const char* kExpectedXips[] = {
    "default.xip",
    "skin.xip",
    "mainmenu5.xip",
    "settings3.xip",
    "music_playedit2.xip",
    "memory2.xip",
};

void IpToString(DWORD ip, char buf[16])
{
    BYTE* b = (BYTE*)&ip;
    sprintf(buf, "%d.%d.%d.%d", b[0], b[1], b[2], b[3]);
}

bool FileExistsA(const char* szPath)
{
    DWORD attr = GetFileAttributesA(szPath);
    return attr != 0xFFFFFFFF;
}

void EnsureLogsDir()
{
    CreateDirectoryA("Q:\\Logs", NULL);
}

void TimestampedLogPath(char buf[64])
{
    time_t t = time(NULL);
    struct tm* lt = localtime(&t);
    if (lt) {
        sprintf(buf, "Q:\\Logs\\panic-%04d%02d%02d-%02d%02d%02d.txt",
                lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
                lt->tm_hour, lt->tm_min, lt->tm_sec);
    } else {
        sprintf(buf, "Q:\\Logs\\panic-unknown.txt");
    }
}

void WritePanicLog(const TCHAR* reason, LPEXCEPTION_POINTERS pEx)
{
    EnsureLogsDir();

    char path[64];
    TimestampedLogPath(path);

    FILE* f = fopen(path, "w");
    if (!f)
        return;

    fprintf(f, "Theseus dashboard panic\n");
    fprintf(f, "=======================\n\n");

    fprintf(f, "Build: %s %s\n", __DATE__, __TIME__);
    fprintf(f, "Kernel build: %u\n", XboxKrnlVersion ? XboxKrnlVersion->Build : 0);

    char ipStr[16] = "0.0.0.0";
    if (net::isReady())
        IpToString(net::getAddress(), ipStr);
    fprintf(f, "IP: %s\n", ipStr);
    fprintf(f, "FTP: %s\n", ftpServer::isRunning() ? "running on port 21" : "not running");

    fprintf(f, "\nReason:\n");
#ifdef UNICODE
    char ansiReason[512];
    WideCharToMultiByte(CP_ACP, 0, reason, -1, ansiReason, sizeof(ansiReason), NULL, NULL);
    fprintf(f, "  %s\n", ansiReason);
#else
    fprintf(f, "  %s\n", reason);
#endif

    if (pEx && pEx->ExceptionRecord) {
        EXCEPTION_RECORD* er = pEx->ExceptionRecord;
        fprintf(f, "\nException 0x%08X at 0x%p\n", er->ExceptionCode, er->ExceptionAddress);
        for (DWORD i = 0; i < er->NumberParameters && i < EXCEPTION_MAXIMUM_PARAMETERS; i++)
            fprintf(f, "  param[%u] = 0x%08X\n", i, er->ExceptionInformation[i]);

        if (pEx->ContextRecord) {
            CONTEXT* c = pEx->ContextRecord;
            fprintf(f, "\nRegisters:\n");
            fprintf(f, "  EAX=%08lX EBX=%08lX ECX=%08lX EDX=%08lX\n",
                    (unsigned long)c->Eax, (unsigned long)c->Ebx,
                    (unsigned long)c->Ecx, (unsigned long)c->Edx);
            fprintf(f, "  ESI=%08lX EDI=%08lX EBP=%08lX ESP=%08lX\n",
                    (unsigned long)c->Esi, (unsigned long)c->Edi,
                    (unsigned long)c->Ebp, (unsigned long)c->Esp);
            fprintf(f, "  EIP=%08lX EFL=%08lX CS =%04lX     SS =%04lX\n",
                    (unsigned long)c->Eip, (unsigned long)c->EFlags,
                    (unsigned long)c->SegCs, (unsigned long)c->SegSs);

            // Best-effort EBP-chain stack walk. No PDB on Xbox so we just
            // dump raw return addresses; pair with map file in post-mortem.
            fprintf(f, "\nStack walk (EBP chain, raw addresses):\n");
            DWORD ebp = c->Ebp;
            for (int depth = 0; depth < 16 && ebp != 0; depth++) {
                DWORD* frame = (DWORD*)ebp;
                // Cheap sanity: must be in user RAM range and 4-byte aligned.
                if ((ebp & 3) != 0 || ebp < 0x10000 || ebp > 0x7FFFFFFF)
                    break;
                DWORD ret = frame[1];
                fprintf(f, "  #%d  ret=0x%08lX\n", depth, (unsigned long)ret);
                ebp = frame[0];
            }
        }
    }

    fprintf(f, "\nExpected XIPs:\n");
    char fullPath[64];
    for (size_t i = 0; i < sizeof(kExpectedXips) / sizeof(kExpectedXips[0]); i++) {
        sprintf(fullPath, "Q:\\Xips\\%s", kExpectedXips[i]);
        fprintf(f, "  %s %s\n", FileExistsA(fullPath) ? "OK    " : "MISSING", kExpectedXips[i]);
    }

    fprintf(f, "\nDrive mounts:\n");
    static const char drives[] = { 'C', 'E', 'F', 'G', 'X', 'Y', 'Z', 'Q' };
    for (size_t i = 0; i < sizeof(drives); i++) {
        char root[4] = { drives[i], ':', '\\', 0 };
        fprintf(f, "  %c: %s\n", drives[i], FileExistsA(root) ? "mounted" : "absent");
    }

    fclose(f);

    OutputDebugStringA("[Panic] log written to ");
    OutputDebugStringA(path);
    OutputDebugStringA("\n");
}

// Match overlay.cpp's DrawCubeOverlay state setup so the same glyph draw
// path that powers the in-dashboard overlay works here. Anything less and
// the texture stages stay in some leftover MODULATE-with-NULL-texture mode
// and the diffuse-coloured glyphs render invisible.
void Set2DOverlayState(IDirect3DDevice8* dev)
{
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_COLORVERTEX, TRUE);
    dev->SetTexture(0, NULL);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    dev->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
}

// Procedural background. Two stacked quads draw a vertical gradient from
// near-black at the top to a faint Theseus-green near the bottom.
void DrawBackground(IDirect3DDevice8* dev, float W, float H)
{
    struct V { float x, y, z, rhw; D3DCOLOR c; };
    const D3DCOLOR top = D3DCOLOR_ARGB(255, 4, 8, 4);
    const D3DCOLOR mid = D3DCOLOR_ARGB(255, 8, 18, 10);
    const D3DCOLOR bot = D3DCOLOR_ARGB(255, 12, 30, 14);

    V quad[8] = {
        { 0,    0,    0, 1, top }, { W,    0,    0, 1, top },
        { 0,    H/2,  0, 1, mid }, { W,    H/2,  0, 1, mid },
        { 0,    H/2,  0, 1, mid }, { W,    H/2,  0, 1, mid },
        { 0,    H,    0, 1, bot }, { W,    H,    0, 1, bot },
    };

    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, &quad[0], sizeof(V));
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, &quad[4], sizeof(V));
}

void DrawCenteredText(const TCHAR* s, float screenW, float y, float h, D3DCOLOR c)
{
    float w = OverlayFontMeasure(s, h);
    OverlayFontDraw(s, (screenW - w) * 0.5f, y, h, c);
}

bool ButtonPressedEdge(WORD curr, WORD prev, WORD mask)
{
    return (curr & mask) && !(prev & mask);
}

void RebootCold()
{
    HalReturnToFirmware(HalRebootRoutine);
}

}  // namespace

void TheseusPanic(const TCHAR* reason, LPEXCEPTION_POINTERS pEx)
{
    if (!reason) reason = _T("(unknown)");

    OutputDebugString(_T("[Panic] entering panic mode: "));
    OutputDebugString(reason);
    OutputDebugString(_T("\n"));

    WritePanicLog(reason, pEx);

    IDirect3DDevice8* dev = TheseusGetD3DDev();
    if (!dev) {
        // No display surface available; nothing to draw, just reboot
        // so the user is not stranded looking at a frozen frame.
        OutputDebugStringA("[Panic] no D3D device; rebooting\n");
        RebootCold();
        return;
    }

    // Open all four controller ports for input polling.
    HANDLE hPads[4] = { NULL, NULL, NULL, NULL };
    for (int i = 0; i < 4; i++) {
        XINPUT_POLLING_PARAMETERS pp = {};
        pp.fAutoPoll = TRUE;
        pp.fInterruptOut = TRUE;
        pp.bInputInterval = 8;
        pp.bOutputInterval = 8;
        hPads[i] = XInputOpen(XDEVICE_TYPE_GAMEPAD, i, XDEVICE_NO_SLOT, &pp);
    }

    bool stayMode = false;
    WORD prevButtons = 0;
    DWORD startTick = GetTickCount();

    // Force the font table to load now so the very first frame has glyphs.
    // OverlayFontDraw lazy-inits but only triggers when g_nFontCount==1, and
    // we want any failure to be visible in the panic log via the TRACE inside.
    (void)OverlayFontMeasure(_T("Theseus"), 16.0f);

    while (true) {
        // Input: A reboots into normal init, B/Back reboots, X = stay.
        XINPUT_STATE state = {};
        WORD currButtons = 0;
        for (int i = 0; i < 4; i++) {
            if (hPads[i] && XInputGetState(hPads[i], &state) == ERROR_SUCCESS)
                currButtons |= state.Gamepad.wButtons;
        }

        if (ButtonPressedEdge(currButtons, prevButtons, XINPUT_GAMEPAD_A))
            RebootCold();
        if (ButtonPressedEdge(currButtons, prevButtons, XINPUT_GAMEPAD_B) ||
            ButtonPressedEdge(currButtons, prevButtons, XINPUT_GAMEPAD_BACK))
            RebootCold();
        if (ButtonPressedEdge(currButtons, prevButtons, XINPUT_GAMEPAD_X))
            stayMode = true;

        prevButtons = currButtons;

        // Resolve the viewport every frame. A panic from main loop may
        // have a different aspect than a panic from cold init.
        const float screenW = (g_nViewWidth  > 0.0f) ? g_nViewWidth  : 640.0f;
        const float screenH = (g_nViewHeight > 0.0f) ? g_nViewHeight : 480.0f;
        const float marginX = screenW * 0.08f;

        TheseusBeginScene();
        TheseusClear(D3DCOLOR_ARGB(255, 0, 0, 0));
        Set2DOverlayState(dev);
        DrawBackground(dev, screenW, screenH);

        const DWORD nowMs = GetTickCount() - startTick;
        const float t = (float)nowMs * 0.001f;
        const float pulse = 0.7f + 0.3f * (float)sin(t * 1.6f);

        const D3DCOLOR titleColor = D3DCOLOR_ARGB(
            255,
            (BYTE)(60 * pulse),
            (BYTE)(220 * pulse),
            (BYTE)(60 * pulse));

        DrawCenteredText(_T("THESEUS  PANIC  MODE"), screenW,
                         screenH * 0.075f, screenH * 0.058f, titleColor);
        DrawCenteredText(_T("Dashboard init failed -- recovery only"), screenW,
                         screenH * 0.146f, screenH * 0.029f,
                         D3DCOLOR_ARGB(255, 120, 160, 120));

        const float labelH = screenH * 0.029f; // ~14 at 480
        const float bodyH  = screenH * 0.033f; // ~16 at 480
        const float lineH  = screenH * 0.029f;

        // Reason panel.
        float y = screenH * 0.229f;
        OverlayFontDraw(_T("Reason:"), marginX, y, labelH, D3DCOLOR_ARGB(255, 100, 200, 100));
        OverlayFontDraw(reason, marginX, y + lineH * 1.3f, bodyH, D3DCOLOR_ARGB(255, 240, 240, 240));

        // Network panel.
        y = screenH * 0.354f;
        char ipStr[16] = "0.0.0.0";
        bool netUp = net::isReady();
        if (netUp) IpToString(net::getAddress(), ipStr);

        TCHAR line[128];
        OverlayFontDraw(_T("Network:"), marginX, y, labelH, D3DCOLOR_ARGB(255, 100, 200, 100));

        _stprintf(line, _T("  IP        %S"), ipStr);
        OverlayFontDraw(line, marginX, y + lineH * 1.3f, labelH,
                        netUp ? D3DCOLOR_ARGB(255, 230, 230, 230)
                              : D3DCOLOR_ARGB(255, 230, 120, 120));

        _stprintf(line, _T("  FTP       %s"),
                  ftpServer::isRunning() ? _T("running on port 21") : _T("NOT RUNNING"));
        OverlayFontDraw(line, marginX, y + lineH * 2.6f, labelH,
                        ftpServer::isRunning()
                            ? D3DCOLOR_ARGB(255, 230, 230, 230)
                            : D3DCOLOR_ARGB(255, 230, 120, 120));

        _stprintf(line, _T("  clients   %d active"), ftpServer::getActiveConnections());
        OverlayFontDraw(line, marginX, y + lineH * 3.9f, labelH,
                        D3DCOLOR_ARGB(255, 200, 200, 200));

        // Expected XIPs panel.
        y = screenH * 0.521f;
        OverlayFontDraw(_T("Expected XIPs:"), marginX, y, labelH, D3DCOLOR_ARGB(255, 100, 200, 100));
        char fullPath[64];
        for (size_t i = 0; i < sizeof(kExpectedXips) / sizeof(kExpectedXips[0]); i++) {
            sprintf(fullPath, "Q:\\Xips\\%s", kExpectedXips[i]);
            bool present = FileExistsA(fullPath);

            TCHAR namebuf[40];
#ifdef UNICODE
            MultiByteToWideChar(CP_ACP, 0, kExpectedXips[i], -1, namebuf, 40);
#else
            strcpy(namebuf, kExpectedXips[i]);
#endif
            _stprintf(line, _T("  %s  %s"), present ? _T("ok    ") : _T("MISSING"), namebuf);
            OverlayFontDraw(line, marginX, y + lineH * (1.3f + i * 1.0f),
                            labelH * 0.85f,
                            present ? D3DCOLOR_ARGB(255, 200, 220, 200)
                                    : D3DCOLOR_ARGB(255, 230, 120, 120));
        }

        // Heartbeat dot top-right so a frozen renderer is obvious.
        const float beat = 0.5f + 0.5f * (float)sin(t * 4.0f);
        const D3DCOLOR beatC = D3DCOLOR_ARGB(255, (BYTE)(60 + 100 * beat),
                                                  (BYTE)(220 - 60 * beat),
                                                  (BYTE)(60 + 100 * beat));
        OverlayFontDraw(_T("*"), screenW - marginX, screenH * 0.025f,
                        bodyH, beatC);

        // Footer with action hints.
        if (stayMode) {
            DrawCenteredText(_T("Staying in panic mode -- push fixes via FTP, then power-cycle."),
                             screenW, screenH * 0.896f, screenH * 0.025f,
                             D3DCOLOR_ARGB(255, 200, 220, 120));
        } else {
            DrawCenteredText(_T("[A] retry boot     [B] reboot     [X] stay (FTP only)"),
                             screenW, screenH * 0.896f, labelH,
                             D3DCOLOR_ARGB(255, 180, 200, 180));
        }

        TheseusEndScene();
        TheseusPresent();
    }
    // unreachable
}
