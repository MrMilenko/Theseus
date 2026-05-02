// ftp_widget.cpp: FTP status HUD widget. Shows IP, port, and active
// session count when the FTP server is running. Theseus-original.

#include "std.h"
#include "theseus.h"
#include "widget_layer.h"
#include "widget_draw.h"
#include "network.h"
#include "toolbox/ftpServer.h"

namespace {

const int FTP_WIDGET_W = 196;
const int FTP_WIDGET_H = 92;

uint32_t s_prevRX   = 0;
uint32_t s_prevTX   = 0;
DWORD    s_prevTick = 0;

// Most recently computed instantaneous rates (KiB/s, refreshed once per second)
float    s_dispRX = 0.0f;
float    s_dispTX = 0.0f;

// Cached IP string, refreshed on the same 1Hz tick as the rates.
TCHAR    s_ipStr[20] = { 0 };

static void RefreshIpString()
{
    DWORD addr = net::getAddress();
    if (addr == 0)
    {
        _sntprintf(s_ipStr, 20, _T("..."));
    }
    else
    {
        _sntprintf(s_ipStr, 20, _T("%d.%d.%d.%d"),
                   (int)(addr & 0xFF),
                   (int)((addr >> 8) & 0xFF),
                   (int)((addr >> 16) & 0xFF),
                   (int)((addr >> 24) & 0xFF));
    }
    s_ipStr[19] = 0;
}

void ftpWidgetTick()
{
    if (!ftpServer::isRunning())
    {
        s_dispRX = 0.0f;
        s_dispTX = 0.0f;
        s_prevRX = 0;
        s_prevTX = 0;
        s_prevTick = GetTickCount();
        s_ipStr[0] = 0;
        return;
    }

    DWORD now = GetTickCount();
    if (s_prevTick == 0)
    {
        s_prevTick = now;
        s_prevRX = ftpServer::getTotalBytesReceived();
        s_prevTX = ftpServer::getTotalBytesSent();
        RefreshIpString();
        return;
    }

    DWORD elapsed = now - s_prevTick;
    if (elapsed < 1000) return;

    uint32_t curRX = ftpServer::getTotalBytesReceived();
    uint32_t curTX = ftpServer::getTotalBytesSent();

    // Delta, with a rollover guard. Counters are 32-bit; if they wrap mid-
    // sample we just show 0 for that tick rather than a giant garbage rate.
    uint32_t dRX = (curRX >= s_prevRX) ? (curRX - s_prevRX) : 0;
    uint32_t dTX = (curTX >= s_prevTX) ? (curTX - s_prevTX) : 0;

    float secs = (float)elapsed / 1000.0f;
    s_dispRX = ((float)dRX / 1024.0f) / secs;
    s_dispTX = ((float)dTX / 1024.0f) / secs;

    s_prevRX = curRX;
    s_prevTX = curTX;
    s_prevTick = now;

    RefreshIpString();
}

static void FormatRate(float kbps, TCHAR* buf, int bufChars)
{
    if (kbps < 1.0f)
        _sntprintf(buf, bufChars, _T("idle"));
    else if (kbps < 1024.0f)
        _sntprintf(buf, bufChars, _T("%.1f KB/s"), kbps);
    else
        _sntprintf(buf, bufChars, _T("%.2f MB/s"), kbps / 1024.0f);
    buf[bufChars - 1] = 0;
}

void ftpWidgetDraw(int x, int y, DWORD argb)
{
    if (!ftpServer::isRunning()) return;

    DWORD opacity = (argb >> 24) & 0xFF;
    DWORD bgAlpha = opacity / 2;
    DWORD bgColor = (bgAlpha << 24); // black with scaled alpha

    // Backdrop
    DrawSolidRect(x, y, FTP_WIDGET_W, FTP_WIDGET_H, bgColor);
    // Subtle 1px border
    DWORD borderColor = ((opacity / 3) << 24) | (argb & 0x00FFFFFF);
    DrawSolidRect(x, y, FTP_WIDGET_W, 1, borderColor);
    DrawSolidRect(x, y + FTP_WIDGET_H - 1, FTP_WIDGET_W, 1, borderColor);
    DrawSolidRect(x, y, 1, FTP_WIDGET_H, borderColor);
    DrawSolidRect(x + FTP_WIDGET_W - 1, y, 1, FTP_WIDGET_H, borderColor);

    int conns = ftpServer::getActiveConnections();

    TCHAR header[32];
    if (conns > 0)
        _sntprintf(header, 32, _T("FTP  %d client%s"), conns, conns == 1 ? _T("") : _T("s"));
    else
        _sntprintf(header, 32, _T("FTP  listening"));
    header[31] = 0;

    OverlayFontDraw(header, (float)(x + 8), (float)(y + 6), 16.0f, argb);

    OverlayFontDraw(s_ipStr[0] ? s_ipStr : _T("..."),
                    (float)(x + 8), (float)(y + 26), 14.0f, argb);

    TCHAR up[24], down[24];
    FormatRate(s_dispTX, up,   24);
    FormatRate(s_dispRX, down, 24);

    TCHAR line[48];
    _sntprintf(line, 48, _T("UP  %s"), up);   line[47] = 0;
    OverlayFontDraw(line, (float)(x + 8), (float)(y + 46), 14.0f, argb);
    _sntprintf(line, 48, _T("DN  %s"), down); line[47] = 0;
    OverlayFontDraw(line, (float)(x + 8), (float)(y + 66), 14.0f, argb);
}

} // namespace

void RegisterFtpWidget()
{
    Widget w;
    w.name      = "ftp";
    w.anchor    = WIDGET_ANCHOR_TR;
    w.enabled   = true;
    w.opacity   = 200;          // ~78%
    w.tintRGB   = 0x00FFFFFF;   // white
    w.reservedW = FTP_WIDGET_W;
    w.reservedH = FTP_WIDGET_H;
    w.tick      = ftpWidgetTick;
    w.draw      = ftpWidgetDraw;
    RegisterWidget(w);
}
