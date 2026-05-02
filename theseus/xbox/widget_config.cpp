// widget_config.cpp: widget config persistence. Reads / writes
// Q:\System\Config.ini, one [Widget <name>] section per registered
// widget. Theseus-original.

#include "std.h"
#include "settingsfile.h"
#include "widget_layer.h"

// Format is human-editable so users can pre-seed a config without
// booting:
//
//     [Widget ftp]
//     Enabled=Yes
//     Anchor=TR
//     Opacity=80
//     Tint=#FFFFFF
//
// LoadWidgetConfig is called once at boot AFTER all RegisterWidget calls.
// SaveWidgetConfig is called from the overlay's Widgets page on every
// value cycle so changes survive a reboot.

#define WIDGET_CONFIG_PATH _T("Q:\\System\\Config.ini")

static void GetSectionName(const Widget* w, TCHAR* buf, int bufChars)
{
    _sntprintf(buf, bufChars, _T("Widget %hs"), w->name ? w->name : "unknown");
    buf[bufChars - 1] = 0;
}

static const TCHAR* AnchorToString(WidgetAnchor a)
{
    switch (a)
    {
    case WIDGET_ANCHOR_TL: return _T("TL");
    case WIDGET_ANCHOR_BL: return _T("BL");
    case WIDGET_ANCHOR_BR: return _T("BR");
    case WIDGET_ANCHOR_TR:
    default:               return _T("TR");
    }
}

static WidgetAnchor StringToAnchor(const TCHAR* s)
{
    if (!s) return WIDGET_ANCHOR_TR;
    if (_tcsicmp(s, _T("TL")) == 0) return WIDGET_ANCHOR_TL;
    if (_tcsicmp(s, _T("BL")) == 0) return WIDGET_ANCHOR_BL;
    if (_tcsicmp(s, _T("BR")) == 0) return WIDGET_ANCHOR_BR;
    return WIDGET_ANCHOR_TR;
}

static DWORD ParseHexRGB(const TCHAR* s, DWORD fallback)
{
    if (!s || !*s) return fallback;
    const TCHAR* p = s;
    if (*p == _T('#')) p++;
    else if (*p == _T('0') && (p[1] == _T('x') || p[1] == _T('X'))) p += 2;

    DWORD v = 0;
    int digits = 0;
    while (*p && digits < 8)
    {
        TCHAR c = *p++;
        int d;
        if      (c >= _T('0') && c <= _T('9')) d = c - _T('0');
        else if (c >= _T('a') && c <= _T('f')) d = 10 + (c - _T('a'));
        else if (c >= _T('A') && c <= _T('F')) d = 10 + (c - _T('A'));
        else break;
        v = (v << 4) | d;
        digits++;
    }
    if (digits == 0) return fallback;
    return v & 0x00FFFFFF;
}

void LoadWidgetConfig()
{
    int count = GetWidgetCount();
    if (count == 0) return;

    CSettingsFile cfg;
    if (!cfg.Open(WIDGET_CONFIG_PATH))
        return; // No file yet; defaults stand.

    TCHAR section[64];
    TCHAR value[64];

    for (int i = 0; i < count; i++)
    {
        Widget* w = GetWidgetAt(i);
        if (!w) continue;

        GetSectionName(w, section, 64);

        if (cfg.GetValue(section, _T("Enabled"), value, 64))
        {
            w->enabled = (_tcsicmp(value, _T("Yes")) == 0)
                      || (_tcsicmp(value, _T("On"))  == 0)
                      || (_tcsicmp(value, _T("1"))   == 0);
        }

        if (cfg.GetValue(section, _T("Anchor"), value, 64))
            w->anchor = StringToAnchor(value);

        if (cfg.GetValue(section, _T("Opacity"), value, 64))
        {
            int pct = _ttoi(value);
            if (pct < 0)   pct = 0;
            if (pct > 100) pct = 100;
            w->opacity = (pct * 255) / 100;
        }

        if (cfg.GetValue(section, _T("Tint"), value, 64))
            w->tintRGB = ParseHexRGB(value, w->tintRGB);
    }

    cfg.Close();
}

void SaveWidgetConfig()
{
    int count = GetWidgetCount();
    if (count == 0) return;

    CSettingsFile cfg;
    cfg.Open(WIDGET_CONFIG_PATH);

    TCHAR section[64];
    TCHAR value[32];

    for (int i = 0; i < count; i++)
    {
        Widget* w = GetWidgetAt(i);
        if (!w) continue;

        GetSectionName(w, section, 64);

        cfg.SetValue(section, _T("Enabled"), w->enabled ? _T("Yes") : _T("No"));
        cfg.SetValue(section, _T("Anchor"),  AnchorToString(w->anchor));

        int pct = ((int)w->opacity * 100 + 127) / 255;
        if (pct < 0)   pct = 0;
        if (pct > 100) pct = 100;
        _sntprintf(value, 32, _T("%d"), pct);
        value[31] = 0;
        cfg.SetValue(section, _T("Opacity"), value);

        _sntprintf(value, 32, _T("#%06X"), (unsigned int)(w->tintRGB & 0x00FFFFFF));
        value[31] = 0;
        cfg.SetValue(section, _T("Tint"), value);
    }

    cfg.Save();
    cfg.Close();
}
