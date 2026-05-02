// screen.cpp: CScreen, the dashboard's screen-level node (resolution,
// brightness, overscan, screenshot, wireframe / motion debug toggles).
// Decompiled from the 5960 retail XBE.

#include "std.h"
#include "theseus.h"
#include "node.h"
#include "screen.h"
#include "lerper.h"

#include "date_node.h"

IMPLEMENT_NODE("Screen", CScreen, CNode)

START_NODE_PROPS(CScreen, CNode)
	NODE_PROP(pt_integer, CScreen, width)
	NODE_PROP(pt_integer, CScreen, height)
	NODE_PROP(pt_boolean, CScreen, fullScreen)
	NODE_PROP(pt_boolean, CScreen, showFramerate)
	NODE_PROP(pt_string, CScreen, title)
	NODE_PROP(pt_string, CScreen, icon)
	NODE_PROP(pt_string, CScreen, border)
	NODE_PROP(pt_boolean, CScreen, letterbox)
	NODE_PROP(pt_boolean, CScreen, wideScreen)
	NODE_PROP(pt_number, CScreen, brightness)
END_NODE_PROPS()

#define _FND_CLASS CScreen
START_NODE_FUN(CScreen, CNode)
    NODE_FUN_VV(TakeScreenShot)
    NODE_FUN_VV(WireFrameStart)
    NODE_FUN_VV(WireFrameStop)
    NODE_FUN_VV(MotionStart)
    NODE_FUN_VV(MotionStop)
END_NODE_FUN()
#undef _FND_CLASS

CScreen::CScreen()
{
	m_width = 640;
	m_height = 480;
	m_fullScreen = false;
	m_title = NULL;
	m_icon = NULL;
	m_border = NULL;
	m_showFramerate = false;
	m_letterbox = false;
	m_wideScreen = false;
	m_brightness = 1.0f;
	m_lastBrightness = 1.0f;

	if (TheseusGetScreen() == NULL)
		TheseusSetScreen(this);

	m_bSizeDirty = false;
	m_bTitleDirty = false;
}

CScreen::~CScreen()
{
	delete [] m_title;
	delete [] m_icon;
	delete [] m_border;

	if (TheseusGetScreen() == this)
		TheseusSetScreen(NULL);
}

bool CScreen::OnSetProperty(const PRD* pprd, const void* pvValue)
{
	if (PTR2INT(pprd->pbOffset) == offsetof(m_width) ||
		PTR2INT(pprd->pbOffset) == offsetof(m_height))
	{
		m_bSizeDirty = true;
	}
	else if (PTR2INT(pprd->pbOffset) == offsetof(m_title))
	{
		m_bTitleDirty = true;
	}
	else if (PTR2INT(pprd->pbOffset) == offsetof(m_brightness))
	{
		float nBrightness = *(float*)pvValue;
//		SetBrightness(*(float*)pvValue);
		CLerper::RemoveObject(this);

		if (nBrightness != 1.0f)
		{
			new CLerper(this, &m_brightness, nBrightness, 2.0f);
			return false;
		}
	}

	return CNode::OnSetProperty(pprd, pvValue);
}

void CScreen::Advance(float nSeconds)
{
	CNode::Advance(nSeconds);

	if (m_bSizeDirty)
	{
		m_bSizeDirty = false;
	}

	if (m_bTitleDirty)
	{
		m_bTitleDirty = false;
	}

	if (m_brightness != m_lastBrightness)
	{
		SetBrightness(m_brightness);
		m_lastBrightness = m_brightness;
	}
}

void CScreen::SetBrightness(float nBrightness)
{
#ifdef _XBOX
    D3DGAMMARAMP ramp;

	for (int i = 0; i < 256; i += 1)
	{
        ramp.red[i] = (BYTE)((float)i * nBrightness);
        ramp.green[i] = (BYTE)((float)i * nBrightness);
        ramp.blue[i] = (BYTE)((float)i * nBrightness);
	}

    TheseusGetD3DDev()->SetGammaRamp(0, &ramp);
#endif
}

void CScreen::TakeScreenShot()
{
	LPDIRECT3DSURFACE8 pFrontBuffer;
	D3DSURFACE_DESC d3dsd;
	TheseusGetD3DDev()->GetBackBuffer( -1, D3DBACKBUFFER_TYPE_MONO, &pFrontBuffer );
	
	char tempPath[MAX_PATH];

	CDateObject theDate;
						
	sprintf( tempPath, "Q:\\Screenshots\\ScreenShot%d%d%d.bmp",
			 theDate.getHours(), theDate.getMinutes(), theDate.getSeconds() );
						
	CreateDirectory( "Q:\\Screenshots", NULL );
	XGWriteSurfaceToFile( pFrontBuffer, tempPath );
}
