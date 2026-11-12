// screen.cpp: the Screen node. Holds the screen size and title props the
// scenes set, and fades the display brightness through the gamma ramp.
// Rebuilt function by function from the 5960 retail XBE (0x4f990..0x4fb70).

#include "std.h"
#include "theseus.h"
#include "node.h"
#include "screen.h"
#include "lerper.h"

#include "date_node.h"

IMPLEMENT_NODE("Screen", CScreen, CNode)

// PRD for node class 'Screen', same names and order the scenes bind to.
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

// 5960's Screen has no script functions; these five are Theseus's.
#define _FND_CLASS CScreen
START_NODE_FUN(CScreen, CNode)
	NODE_FUN_VV(TakeScreenShot)
	NODE_FUN_VV(WireFrameStart)
	NODE_FUN_VV(WireFrameStop)
	NODE_FUN_VV(MotionStart)
	NODE_FUN_VV(MotionStop)
END_NODE_FUN()
#undef _FND_CLASS

// CScreen::CScreen()
// Reconstructed from 5960 retail XBE @ 0x0004f9c0: 640 x 480, every flag
// off, no strings, brightness 1, and the first Screen made becomes the app's
// screen (0x0017e818).
CScreen::CScreen()
{
	m_width = 640;
	m_height = 480;
	m_fullScreen = false;
	m_showFramerate = false;
	m_title = NULL;
	m_icon = NULL;
	m_border = NULL;
	m_letterbox = false;
	m_wideScreen = false;
	m_brightness = 1.0f;
	m_lastBrightness = 1.0f;
	m_bSizeDirty = false;
	m_bTitleDirty = false;

	if (TheseusGetScreen() == NULL)
		TheseusSetScreen(this);
}

// Reconstructed from 5960 retail XBE @ 0x0004fa50.
CScreen::~CScreen()
{
	delete[] m_title;
	delete[] m_icon;
	delete[] m_border;
	if (TheseusGetScreen() == this)
		TheseusSetScreen(NULL);
}

// CScreen::OnSetProperty(prd, value)
// Reconstructed from 5960 retail XBE @ 0x0004fa90. width or height marks
// the size dirty, title the title. A brightness other than 1 isn't stored
// straight: any running fade is dropped and a 2 second lerp heads there
// instead. Setting it back to 1 just stops the fade and stores it.
bool CScreen::OnSetProperty(const PRD* pprd, const void* pvValue)
{
	int field = PTR2INT(pprd->pbOffset);
	if (field == offsetof(m_width) || field == offsetof(m_height))
	{
		m_bSizeDirty = true;
	}
	else if (field == offsetof(m_title))
	{
		m_bTitleDirty = true;
	}
	else if (field == offsetof(m_brightness))
	{
		float target = *(const float*)pvValue;
		CLerper::RemoveObject(this);
		if (target != 1.0f)
		{
			new CLerper(this, &m_brightness, target, 2.0f);
			return false;
		}
	}
	return CNode::OnSetProperty(pprd, pvValue);
}

// CScreen::Advance(dt)
// Reconstructed from 5960 retail XBE @ 0x0004fb20: clears both dirty flags
// (nothing else reads them) and pushes a changed brightness to the gamma
// ramp.
void CScreen::Advance(float nSeconds)
{
	CNode::Advance(nSeconds);
	m_bSizeDirty = false;
	m_bTitleDirty = false;
	if (m_brightness != m_lastBrightness)
	{
		SetBrightness(m_brightness);
		m_lastBrightness = m_brightness;
	}
}

// CScreen::SetBrightness(level)
// Reconstructed from 5960 retail XBE @ 0x0004fb70: all three channels of
// the gamma ramp get (BYTE)(i * level), set through the device (the binary
// inlines SetGammaRamp). Desktop has no ramp.
void CScreen::SetBrightness(float nBrightness)
{
#ifdef _XBOX
	D3DGAMMARAMP ramp;
	for (int i = 0; i < 256; i++)
	{
		BYTE level = (BYTE)(i * nBrightness);
		ramp.red[i] = level;
		ramp.green[i] = level;
		ramp.blue[i] = level;
	}
	TheseusGetD3DDev()->SetGammaRamp(0, &ramp);
#endif
}

// Theseus: saves the back buffer to Q:\Screenshots, named by the time.
void CScreen::TakeScreenShot()
{
	LPDIRECT3DSURFACE8 frame;
	TheseusGetD3DDev()->GetBackBuffer(-1, D3DBACKBUFFER_TYPE_MONO, &frame);

	CDateObject now;
	char path[MAX_PATH];
	sprintf(path, "Q:\\Screenshots\\ScreenShot%d%d%d.bmp", now.getHours(), now.getMinutes(), now.getSeconds());
	TheseusCreateDirectory(_T("Q:\\Screenshots"), NULL);
	XGWriteSurfaceToFile(frame, path);
}
