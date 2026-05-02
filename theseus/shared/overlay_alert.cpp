// overlay_alert.cpp: COverlayAlert node. Bridges XAP scripts to the
// modal Overlay (Xbox) for system message popups (errors, info,
// confirmations). Theseus-original.

#include "std.h"
#include "theseus.h"
#include "node.h"
#ifndef _DESKTOP
#include "overlay.h"
#endif

class COverlayAlert : public CNode
{
    DECLARE_NODE(COverlayAlert, CNode)
public:
    COverlayAlert() {}
    ~COverlayAlert() {}

    void Show(const TCHAR* szMessage);

protected:
    DECLARE_NODE_PROPS()
    DECLARE_NODE_FUNCTIONS()
};

IMPLEMENT_NODE("OverlayAlert", COverlayAlert, CNode)

#define _FND_CLASS COverlayAlert
START_NODE_FUN(COverlayAlert, CNode)
    NODE_FUN_VS(Show)
END_NODE_FUN()
#undef _FND_CLASS

START_NODE_PROPS(COverlayAlert, CNode)
END_NODE_PROPS()

void COverlayAlert::Show(const TCHAR* szMessage)
{
#ifdef _DESKTOP
    // Overlay UI removed on desktop; ImGui handles alerts.
    if (szMessage && *szMessage)
        fprintf(stderr, "[Alert] %s\n", szMessage);
#else
    OverlayAlert(szMessage);
#endif
}
