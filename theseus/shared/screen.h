// screen.h: CScreen XAP node, the dashboard's screen-level state
// (size, brightness, framerate / wireframe toggles, screenshot).
// Companion to shared/screen.cpp.

#pragma once

extern bool g_bWireframe;
extern bool g_bMovingScreen;

class CScreen : public CNode
{
	DECLARE_NODE(CScreen, CNode)
public:
	CScreen();
	~CScreen();

	int m_width;
	int m_height;
	bool m_fullScreen;
	TCHAR* m_title;
	TCHAR* m_icon;
	TCHAR* m_border;
	bool m_showFramerate;
	bool m_letterbox;
	bool m_wideScreen;
	float m_brightness;

	void	TakeScreenShot();
	void	WireFrameStart()	{ g_bWireframe = true; }
	void	WireFrameStop()		{ g_bWireframe = false; }
	void	MotionStart()		{ g_bMovingScreen = true; }
	void	MotionStop()		{ g_bMovingScreen = false; }


	bool OnSetProperty(const PRD* pprd, const void* pvValue);
	void Advance(float nSeconds);
	bool m_bSizeDirty;
	bool m_bTitleDirty;

	void SetBrightness(float nBrightness);
	float m_lastBrightness;

	DECLARE_NODE_PROPS()
	DECLARE_NODE_FUNCTIONS()
};
