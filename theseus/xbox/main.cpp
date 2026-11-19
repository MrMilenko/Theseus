// main.cpp: the Xbox entry point, boot path and frame loop, plus the
// dashboard globals theseus.h declares. Launch data, render states, Draw,
// Advance, cleanup and the memory unit format are rebuilt from the 5960
// retail XBE (0x2c520..0x2dd20); see docs/decomp/AppShell.md. Boot timing,
// skins, Config.ini video modes, widgets, FTP, Discord and the panic
// screen are Theseus's.

#include "std.h"
#include <xlaunch.h>
#include "theseus.h"
#include "file_util.h"
#include "xip_archive.h"
#include "node.h"
#include "screen.h"
#include "runner.h"
#include "lerper.h"
#include "camera.h"
#include "scene_groups.h"
#include "settingsfile.h"
#include "xbox_live.h"
#include "ntiosvc.h"
#include "dsound_manager.h"
#include "overlay.h"
#include "widget_layer.h"
#include "network.h"
#include "discord.h"
#include "locale_node.h"
#include "panic_screen.h"
extern void PreloadSkinTextures();
extern void RegisterFtpWidget();
extern void RegisterFpsWidget();
extern void RegisterPerfWidget();
extern void RegisterIsoWidget();
extern void LoadWidgetConfig();

extern void Memory_Init();

extern void Class_Init();
extern void Class_Exit();

extern void DiscDrive_Init();

extern void BackgroundLoader_Frame();

extern void Text_Exit();

extern void Locale_Exit();

#ifdef _DEBUG
extern void Debug_Init();
extern void Debug_Exit();
extern void Debug_Frame();
#endif

extern void TitleArray_Init();

extern void Material_Init(bool bReloadSkinXBX);
extern float g_nEffectAlpha;

bool g_bWireframe = false;
bool g_bMovingScreen = true;
bool g_useMilkdropViz = false; // desktop-only; constant false on Xbox
bool g_showAlbumCover = false; // desktop-only; constant false on Xbox

// DVD Closed Captioning stubs (not used).
extern "C" void __stdcall D3DDevice_EnableCC(int) {}
extern "C" int __stdcall D3DDevice_GetCCStatus(int, int *) { return 0; }
extern "C" void __stdcall D3DDevice_SendCC(int, int, int) {}


// Globals

// D3D
LPDIRECT3DDEVICE8 g_pD3DDev = NULL;
LPDIRECT3D8 g_pD3D = NULL;
D3DPRESENT_PARAMETERS g_pp;

// View
float g_nViewWidth = 0.0f;
float g_nViewHeight = 0.0f;
bool g_bStretchWidescreen = false;
bool g_bEmulateXboxRes = true; // console is always 720x480

// Render state
bool g_bZBuffer = false;
bool g_bProjectionDirty = true;
ID3DXMatrixStack* g_worldStack = NULL;

// Timing
DWORD g_dwStartTick = 0;
DWORD g_dwFrameTick = 0;
XTIME g_now = 0.0;

// Boot timing only. Frame timing lives in frame_profiler.
static LONGLONG s_qpcFreq = 0;

static inline LONGLONG QpcNow()
{
	LARGE_INTEGER li;
	QueryPerformanceCounter(&li);
	return li.QuadPart;
}

static inline float QpcToMs(LONGLONG delta)
{
	if (s_qpcFreq == 0)
	{
		LARGE_INTEGER li;
		QueryPerformanceFrequency(&li);
		s_qpcFreq = li.QuadPart;
		if (s_qpcFreq == 0) s_qpcFreq = 1; // avoid div0
	}
	return (float)((double)delta * 1000.0 / (double)s_qpcFreq);
}

// Boot-phase timing: each call logs the time since the previous mark
// plus cumulative since the first mark. Lets us see in the debug
// output which init step is actually expensive.
static LONGLONG s_bootStartQpc = 0;
static LONGLONG s_bootLastQpc  = 0;

static void BootMark(const char* label)
{
	LONGLONG now = QpcNow();
	if (s_bootStartQpc == 0)
	{
		s_bootStartQpc = now;
		s_bootLastQpc  = now;
	}
	float dt    = QpcToMs(now - s_bootLastQpc);
	float total = QpcToMs(now - s_bootStartQpc);

	char buf[160];
	_snprintf(buf, sizeof(buf), "[Boot] +%7.1f ms  (%7.1f total)  %s\n", dt, total, label);
	OutputDebugStringA(buf);
	s_bootLastQpc = now;
}

// Printed once at the very top of boot so the debug console shows which
// build is running the moment it attaches. Bump THESEUS_BUILD_TAG (or pass
// -DTHESEUS_BUILD_TAG=\"...\") to give a build a name.
#ifndef THESEUS_BUILD_TAG
#define THESEUS_BUILD_TAG "dev"
#endif

static void BootBanner()
{
#ifdef _DEBUG
	const char* cfg = "DEBUG";
#else
	const char* cfg = "RETAIL";
#endif
	char buf[160];
	OutputDebugStringA("\n========================================\n");
	_snprintf(buf, sizeof(buf), "  Theseus  [%s]  %s\n", THESEUS_BUILD_TAG, cfg);
	OutputDebugStringA(buf);
	OutputDebugStringA("  built " __DATE__ " " __TIME__ "\n");
	OutputDebugStringA("========================================\n");
}

// Scene root
CClass* g_pClass = NULL;
CInstance* g_pObject = NULL;

// Bound nodes
CScreen* g_pScreen = NULL;
CViewpoint* g_pViewpoint = NULL;
CNavigationInfo* g_pNavigationInfo = NULL;
CBackground* g_pBackground = NULL;

// Paths
TCHAR* g_szAppDir = NULL;
TCHAR* g_sFontDir = NULL;
TCHAR* g_sXipDir = NULL;
TCHAR* g_sSkinDir = NULL;
CSettingsFile* g_pSkinSettings = NULL;

// Partition flags
bool g_fExists = false;
bool g_gExists = false;
bool g_hExists = false;
bool g_iExists = false;
bool g_jExists = false;
bool g_kExists = false;
bool g_lExists = false;
bool g_mExists = false;
bool g_nExists = false;
bool g_rExists = false;
bool g_sExists = false;

// Launch data
bool g_bHasLaunchData = false;
DWORD g_dwTitleID = 0;
DWORD g_dwLaunchReason = 0;
DWORD g_dwLaunchContext = 0;
DWORD g_dwLaunchParameter1 = 0;
DWORD g_dwLaunchParameter2 = 0;

// Thread
DWORD g_dwMainThreadId = 0;



D3DXMATRIX g_matView;
D3DXMATRIX g_matPosition;
D3DXMATRIX g_matProjection;
D3DRECT g_scissorRect;
D3DRECT g_scissorRectx2;

D3DXMATRIX g_matIdentity(
	1.0f, 0.0f, 0.0f, 0.0f,
	0.0f, 1.0f, 0.0f, 0.0f,
	0.0f, 0.0f, 1.0f, 0.0f,
	0.0f, 0.0f, 0.0f, 1.0f);

UINT g_uMesh;
UINT g_uMeshRef;

// Alert(format, ...)
// 5960 @ 0x0002c520 formats into a 512 character buffer and drops it
// (retail has no debug output). Theseus sends the text to Trace.
extern "C" void Alert(const TCHAR *szMsg, ...)
{
	TCHAR szText[512];
	va_list args;
	va_start(args, szMsg);
	_vsntprintf(szText, countof(szText), szMsg, args);
	va_end(args);
	Trace(_T("\007%s\n"), szText);
}

#ifdef _UNICODE
// Widen or narrow at most nMaxChars characters, stopping after the
// terminator. The narrow side keeps only each character's low byte.
void Unicode(TCHAR *wsz, const char *sz, int nMaxChars)
{
	for (int i = 0; i < nMaxChars; i++)
	{
		wsz[i] = (unsigned char)sz[i];
		if (sz[i] == 0)
			return;
	}
}

void Ansi(char *sz, const TCHAR *wsz, int nMaxChars)
{
	for (int i = 0; i < nMaxChars; i++)
	{
		sz[i] = (char)wsz[i];
		if (sz[i] == 0)
			return;
	}
}
#endif // _UNICODE

extern "C" PLAUNCH_DATA_PAGE *LaunchDataPage;

// Theseus_Init()
// Reconstructed from the 5960 app constructor @ 0x0002c570: starts the
// frame clock from GetTickCount and makes the root class. When the kernel
// handed over launch data meant for the dash (LDT_LAUNCH_DASHBOARD), the
// title id, reason, context and both parameters are kept and the page is
// freed so nothing reads it twice. Then the main thread id is noted.
static void Theseus_Init()
{
	g_dwStartTick = g_dwFrameTick = GetTickCount();
	g_now = (float)g_dwFrameTick / 1000.0f;

	g_pClass = new CClass;

	PLAUNCH_DATA_PAGE pPage = *LaunchDataPage;
	if (pPage != NULL && pPage->Header.dwLaunchDataType == LDT_LAUNCH_DASHBOARD)
	{
		PLD_LAUNCH_DASHBOARD pDash = (PLD_LAUNCH_DASHBOARD)pPage->LaunchData;
		g_bHasLaunchData = true;
		g_dwTitleID = pPage->Header.dwTitleId;
		g_dwLaunchReason = pDash->dwReason;
		g_dwLaunchContext = pDash->dwContext;
		g_dwLaunchParameter1 = pDash->dwParameter1;
		g_dwLaunchParameter2 = pDash->dwParameter2;

		*LaunchDataPage = NULL;
		MmFreeContiguousMemory(pPage);
	}

	g_dwMainThreadId = GetCurrentThreadId();
}

extern CNtIoctlCdromService g_discIoctl;
extern int GetDiscType();

HRESULT InitAudio()
{
	HRESULT hr = S_OK;
	do
	{
		// retreive the status of DVD/CD Rom
		g_discIoctl.Open(1);
		g_discKind = GetDiscType();

		// initialize DSound
		if (!DSoundManager::Instance())
		{
			hr = E_OUTOFMEMORY;
			BREAKONFAIL(hr, "Failed to create DSoundManager");
		}

		hr = DSoundManager::Instance()->Initialize();
		BREAKONFAIL(hr, "Failed to init DSound");

	} while (0);

	return hr;
}
DWORD theseus_rgdwRenderStateCache[256];
bool theseus_rgbRenderStateCache[256];
float g_transitionMotionBlur;

// XboxInitRenderState()
// Reconstructed from 5960 retail XBE @ 0x0002c660: sets the startup render
// states one at a time (z from the app's z-buffer flag, solid Gouraud fill,
// z writes, no alpha test, one/zero blend, CCW culling, LESSEQUAL), then
// the motion blur factor to 1. Theseus sets the full list below, in order.
void XboxInitRenderState()
{
	const float fZero = 0.0f;
	const float fOne = 1.0f;
	const DWORD dwZero = *(const DWORD *)&fZero;
	const DWORD dwOne = *(const DWORD *)&fOne;

	const struct { D3DRENDERSTATETYPE state; DWORD value; } states[] =
	{
		{ D3DRS_ZENABLE,                  TheseusGetZBuffer() ? D3DZB_TRUE : D3DZB_FALSE },
		{ D3DRS_FILLMODE,                 D3DFILL_SOLID },
		{ D3DRS_SHADEMODE,                D3DSHADE_GOURAUD },
		{ D3DRS_ZWRITEENABLE,             TRUE },
		{ D3DRS_ALPHATESTENABLE,          FALSE },
		{ D3DRS_SRCBLEND,                 D3DBLEND_ONE },
		{ D3DRS_DESTBLEND,                D3DBLEND_ZERO },
		{ D3DRS_CULLMODE,                 D3DCULL_CCW },
		{ D3DRS_ZFUNC,                    D3DCMP_LESSEQUAL },
		{ D3DRS_ALPHAREF,                 0 },
		{ D3DRS_ALPHAFUNC,                D3DCMP_ALWAYS },
		{ D3DRS_DITHERENABLE,             TRUE },
		{ D3DRS_ALPHABLENDENABLE,         TRUE },
		{ D3DRS_FOGENABLE,                FALSE },
		{ D3DRS_SPECULARENABLE,           FALSE },
		{ D3DRS_FOGCOLOR,                 0 },
		{ D3DRS_FOGTABLEMODE,             D3DFOG_NONE },
		{ D3DRS_FOGDENSITY,               dwOne },
		{ D3DRS_EDGEANTIALIAS,            FALSE },
		{ D3DRS_ZBIAS,                    0 },
		{ D3DRS_RANGEFOGENABLE,           FALSE },
		{ D3DRS_STENCILENABLE,            FALSE },
		{ D3DRS_STENCILFAIL,              D3DSTENCILOP_KEEP },
		{ D3DRS_STENCILZFAIL,             D3DSTENCILOP_KEEP },
		{ D3DRS_STENCILPASS,              D3DSTENCILOP_KEEP },
		{ D3DRS_STENCILFUNC,              D3DCMP_ALWAYS },
		{ D3DRS_STENCILREF,               0 },
		{ D3DRS_STENCILMASK,              0xffffffff },
		{ D3DRS_STENCILWRITEMASK,         0xffffffff },
		{ D3DRS_WRAP0,                    0 },
		{ D3DRS_WRAP1,                    0 },
		{ D3DRS_WRAP2,                    0 },
		{ D3DRS_WRAP3,                    0 },
		{ D3DRS_LIGHTING,                 TRUE },
		{ D3DRS_AMBIENT,                  0 },
		{ D3DRS_COLORVERTEX,              TRUE },
		{ D3DRS_LOCALVIEWER,              TRUE },
		{ D3DRS_NORMALIZENORMALS,         FALSE },
		{ D3DRS_DIFFUSEMATERIALSOURCE,    D3DMCS_COLOR1 },
		{ D3DRS_SPECULARMATERIALSOURCE,   D3DMCS_COLOR2 },
		{ D3DRS_AMBIENTMATERIALSOURCE,    D3DMCS_COLOR2 },
		{ D3DRS_EMISSIVEMATERIALSOURCE,   D3DMCS_MATERIAL },
		{ D3DRS_VERTEXBLEND,              D3DVBF_DISABLE },
		{ D3DRS_POINTSIZE,                dwOne },
		{ D3DRS_POINTSIZE_MIN,            dwOne },
		{ D3DRS_POINTSPRITEENABLE,        FALSE },
		{ D3DRS_POINTSCALEENABLE,         FALSE },
		{ D3DRS_POINTSCALE_A,             dwOne },
		{ D3DRS_POINTSCALE_B,             dwZero },
		{ D3DRS_POINTSCALE_C,             dwZero },
		{ D3DRS_MULTISAMPLEANTIALIAS,     TRUE },
		{ D3DRS_MULTISAMPLEMASK,          0xffffffff },
		{ D3DRS_COLORWRITEENABLE,         D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA },
	};

	for (int i = 0; i < (int)countof(states); i++)
		TheseusSetRenderState(states[i].state, states[i].value);

	g_transitionMotionBlur = 1.0f;
}

// InitD3D()
// 5960 @ 0x0002ccf0 sizes the back buffer by AV pack (640 or 720 wide, 576
// high on PAL-I), 4x gaussian supersampling, vsync, interlaced unless an
// HDTV pack allows 480p, and creates the device. Theseus reads its modes
// from Q:\System\Config.ini, defaults to no multisampling and immediate
// present, and adds its widgets and overlay once the device exists.
bool InitD3D()
{
#ifdef THESEUS_PUSHBUFFER_KB
	// Experiment: a bigger push buffer so the per-frame index stream doesn't
	// stall the CPU waiting for the GPU to free space. Must precede CreateDevice.
	Direct3D_SetPushBufferSize(THESEUS_PUSHBUFFER_KB * 1024, 64 * 1024);
#endif
	g_pD3D = Direct3DCreate8(D3D_SDK_VERSION);
	if (g_pD3D == NULL)
	{
		Alert(_T("Cannot initialize Direct3D! (D3D_SDK_VERSION=%d)"), D3D_SDK_VERSION);
		return false;
	}

	D3DDEVTYPE devtype = D3DDEVTYPE_HAL;

	ZeroMemory(&g_pp, sizeof(g_pp));
	g_pp.BackBufferCount = 1;
	g_pp.FullScreen_RefreshRateInHz = D3DPRESENT_RATE_DEFAULT;

	g_pp.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
	g_pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	g_pp.MultiSampleType = D3DMULTISAMPLE_NONE; // overridden below if [Performance] AntiAlias=Yes
	g_pp.EnableAutoDepthStencil = g_bZBuffer;
	if (g_bZBuffer)
		g_pp.AutoDepthStencilFormat = D3DFMT_D24S8;

	DWORD dwBehavior = D3DCREATE_HARDWARE_VERTEXPROCESSING;

	// LOAD INI PREFERENCES
	CSettingsFile VideoSettings;
	VideoSettings.Open(_T("Q:\\System\\Config.ini"));

	TCHAR progBuffer[MAX_PATH] = _T("");
	TCHAR mode720Buffer[MAX_PATH] = _T("");

	VideoSettings.GetValue(_T("Progressive"), _T("Use Progressive"), progBuffer, MAX_PATH);
	VideoSettings.GetValue(_T("Progressive"), _T("Use 720p"), mode720Buffer, MAX_PATH);

	// 2x vertical supersample AA costs ~10ms per frame at 720p. Off by
	// default; HDTV users wanting smoother text edges can opt in via
	// [Performance] AntiAlias=Yes.
	TCHAR aaBuffer[MAX_PATH] = _T("");
	VideoSettings.GetValue(_T("Performance"), _T("AntiAlias"), aaBuffer, MAX_PATH);
	if (_tcsicmp(aaBuffer, _T("Yes")) == 0)
		g_pp.MultiSampleType = D3DMULTISAMPLE_2_SAMPLES_SUPERSAMPLE_VERTICAL_LINEAR;

	bool bEnable480p = (_tcsicmp(progBuffer, _T("Yes")) == 0);
	bool bEnable720p = (_tcsicmp(mode720Buffer, _T("Yes")) == 0);
	bool isPAL = (XGetVideoStandard() == XC_VIDEO_STANDARD_PAL_I);
	bool bForceProgressive = false;
	bool bForceWidescreen = false;

	g_pp.Flags = 0;

	// Dump and override AV flags for dev
	DWORD videoFlags = 0, videoType = 0;
	if (XQueryValue(XC_VIDEO_FLAGS, &videoType, &videoFlags, sizeof(videoFlags), NULL) == ERROR_SUCCESS)
	{
		videoFlags |= AV_FLAGS_HDTV_480p | AV_FLAGS_HDTV_720p;
		XSetValue(XC_VIDEO_FLAGS, REG_DWORD, &videoFlags, sizeof(videoFlags));
	}

	// Composite, S-Video, RF and SCART can't carry a progressive signal, so
	// Config.ini's 480p and 720p only count on an HDTV or VGA pack. 5960 only
	// goes progressive on the HDTV pack (0x0002ccf0).
	DWORD dwPack = XGetAVPack();
	if (isPAL || (dwPack != XC_AV_PACK_HDTV && dwPack != XC_AV_PACK_VGA))
	{
		bEnable480p = false;
		bEnable720p = false;
	}

	// IF 720p REQUESTED
	if (bEnable720p)
	{
		g_pp.BackBufferWidth = 1280;
		g_pp.BackBufferHeight = 720;
		bForceProgressive = true;
		bForceWidescreen = true;
	}
	else
	{
		switch (XGetAVPack())
		{
		default:
			g_pp.BackBufferWidth = 640;
			g_pp.BackBufferHeight = 480;
			break;

		case XC_AV_PACK_STANDARD:
		case XC_AV_PACK_SVIDEO:
		case XC_AV_PACK_RFU:
		case XC_AV_PACK_SCART:
			g_pp.BackBufferWidth = (g_discKind == DISC_DVD) ? 720 : 640;
			g_pp.BackBufferHeight = isPAL ? 576 : 480;
			break;

		case XC_AV_PACK_HDTV:
			g_pp.BackBufferWidth = (g_discKind == DISC_DVD) ? 720 : 640;
			g_pp.BackBufferHeight = 480;

			// Check for DVD 480p override
			if ((XBOX_480P_MACROVISION_ENABLED & XboxHardwareInfo->Flags) && g_discKind == DISC_DVD)
			{
				DWORD dvdFlags = 0, dvdType = 0;
				if (XQueryValue(XC_VIDEO_FLAGS, &dvdType, &dvdFlags, sizeof(dvdFlags), NULL) == ERROR_SUCCESS)
				{
					if (dvdFlags & AV_FLAGS_HDTV_480p)
					{
						bForceProgressive = true;
						bForceWidescreen = true;
					}
				}
			}
			break;
		}

		if (bEnable480p)
		{
			bForceProgressive = true;
			bForceWidescreen = true;
		}
	}

	// FINALIZE FLAGS
	if (bForceProgressive)
		g_pp.Flags |= D3DPRESENTFLAG_PROGRESSIVE;
	else
		g_pp.Flags |= D3DPRESENTFLAG_INTERLACED;

	if (bForceWidescreen || (XGetVideoFlags() & XC_VIDEO_FLAGS_WIDESCREEN))
		g_pp.Flags |= D3DPRESENTFLAG_WIDESCREEN;
	g_bStretchWidescreen = (g_pp.Flags & D3DPRESENTFLAG_WIDESCREEN) != 0;

	g_pp.BackBufferFormat = D3DFMT_X8R8G8B8;

	g_pp.hDeviceWindow = NULL;
	g_pp.Windowed = FALSE;

	g_nViewWidth = (float)g_pp.BackBufferWidth;
	g_nViewHeight = (float)g_pp.BackBufferHeight;

	HRESULT hr = g_pD3D->CreateDevice(D3DADAPTER_DEFAULT, devtype, g_pp.hDeviceWindow, dwBehavior, &g_pp, &g_pD3DDev);
	if (FAILED(hr))
	{
		return false;
	}
	InitCubeOverlay();
	RegisterFtpWidget();
	RegisterFpsWidget();
	RegisterPerfWidget();
	RegisterIsoWidget();
	LoadWidgetConfig();
	LPDIRECT3DSURFACE8 pFrontBuffer;
	D3DSURFACE_DESC d3dsd;
	g_pD3DDev->GetBackBuffer(-1, D3DBACKBUFFER_TYPE_MONO, &pFrontBuffer);
	pFrontBuffer->GetDesc(&d3dsd);
	pFrontBuffer->Release();

	g_scissorRect.x1 = 0;
	g_scissorRect.y1 = 0;
	g_scissorRect.x2 = d3dsd.Width;
	g_scissorRect.y2 = d3dsd.Height;

	g_pD3DDev->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &pFrontBuffer);
	pFrontBuffer->GetDesc(&d3dsd);
	pFrontBuffer->Release();

	g_scissorRectx2.x1 = 0;
	g_scissorRectx2.y1 = 0;
	g_scissorRectx2.x2 = d3dsd.Width;
	g_scissorRectx2.y2 = d3dsd.Height;

	XboxInitRenderState();

	TheseusSetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	TheseusSetRenderState(D3DRS_AMBIENT, 0xffffffff);

	TheseusSetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_ANISOTROPIC);
	TheseusSetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_ANISOTROPIC);
	TheseusSetTextureStageState(0, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);

	TheseusSetTextureStageState(1, D3DTSS_MINFILTER, D3DTEXF_ANISOTROPIC);
	TheseusSetTextureStageState(1, D3DTSS_MAGFILTER, D3DTEXF_ANISOTROPIC);

	return true;
}

void ReleaseD3D()
{
	RELEASENULL(g_worldStack);
	RELEASENULL(g_pD3D);
}

struct COLORVERTEX
{
	float dvX, dvY, dvZ;
	DWORD color;
};

// Rebuild the projection when something marked it dirty: the field of
// view from the bound viewpoint (90 degrees without one), near at half the
// avatar size and far at the visibility limit (0.1 and 1000 without them).
static void UpdateProjection()
{
	if (!g_bProjectionDirty)
		return;
	g_bProjectionDirty = false;

	float fieldOfView = D3DX_PI / 2.0f;
	float nNear = 0.1f;
	float nFar = 1000.0f;

	if (g_pNavigationInfo != NULL)
	{
		nNear = g_pNavigationInfo->m_avatarSize.x / 2.0f;
		if (g_pNavigationInfo->m_visibilityLimit != 0.0f)
			nFar = g_pNavigationInfo->m_visibilityLimit;
	}
	if (g_pViewpoint != NULL)
		fieldOfView = g_pViewpoint->m_fieldOfView;

	D3DXMatrixPerspectiveFovLH(&g_matProjection, fieldOfView, g_nViewWidth / g_nViewHeight, nNear, nFar);
	TheseusSetTransform(D3DTS_PROJECTION, &g_matProjection);
}

// Motion blur: instead of clearing, lay a black quad of alpha blurAlpha
// over the last frame so it fades out. The quad is built once.
static void FadeLastFrame(float blurAlpha)
{
	static LPDIRECT3DVERTEXBUFFER8 s_pQuad;
	if (s_pQuad == NULL)
	{
		VERIFYHR(TheseusGetD3DDev()->CreateVertexBuffer(4 * sizeof(COLORVERTEX), D3DUSAGE_WRITEONLY, D3DFVF_XYZ | D3DFVF_DIFFUSE, D3DPOOL_MANAGED, &s_pQuad));

		COLORVERTEX *verts;
		VERIFYHR(s_pQuad->Lock(0, 4 * sizeof(COLORVERTEX), (BYTE **)&verts, 0));
		float halfW = (float)TheseusGetViewWidth() / 2.0f;
		float halfH = (float)TheseusGetViewHeight() / 2.0f;
		const float corners[4][2] = { { halfW, -halfH }, { -halfW, -halfH }, { halfW, halfH }, { -halfW, halfH } };
		for (int i = 0; i < 4; i++)
		{
			verts[i].dvX = corners[i][0];
			verts[i].dvY = corners[i][1];
			verts[i].dvZ = 0.0f;
			verts[i].color = D3DCOLOR_COLORVALUE(0.0f, 0.0f, 0.0f, blurAlpha);
		}
		VERIFYHR(s_pQuad->Unlock());
	}

	D3DXMATRIX matOrtho, matSavedProjection, matSavedWorld, matSavedView;
	TheseusGetTransform(D3DTS_PROJECTION, &matSavedProjection);
	TheseusGetTransform(D3DTS_WORLD, &matSavedWorld);
	TheseusGetTransform(D3DTS_VIEW, &matSavedView);

	D3DXMatrixOrthoLH(&matOrtho, TheseusGetViewWidth(), TheseusGetViewHeight(), -10000.0f, 10000.0f);
	TheseusSetTransform(D3DTS_PROJECTION, &matOrtho);
	TheseusSetTransform(D3DTS_WORLD, &g_matIdentity);
	TheseusSetTransform(D3DTS_VIEW, &g_matIdentity);

	TheseusSetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	TheseusSetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
	TheseusSetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	TheseusSetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
	TheseusSetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	TheseusSetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
	TheseusSetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	TheseusSetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	TheseusSetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	TheseusSetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

	TheseusSetStreamSource(0, s_pQuad, sizeof(COLORVERTEX));
	TheseusSetVertexShader(D3DFVF_XYZ | D3DFVF_DIFFUSE);
	VERIFYHR(TheseusGetD3DDev()->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2));

	TheseusSetTransform(D3DTS_PROJECTION, &matSavedProjection);
	TheseusSetTransform(D3DTS_WORLD, &matSavedWorld);
	TheseusSetTransform(D3DTS_VIEW, &matSavedView);
}

static float Clamp01(float v)
{
	return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

// Theseus: the headlight and the scene's lights. A navigation info with
// its headlight on (or none at all) gets a dim white directional light
// down the view; the scene adds its own and the summed ambient is clamped.
static void SetupLights()
{
	static int s_nLastLights = -1;
	int nLights = 0;
	D3DCOLORVALUE ambient = { 0.0f, 0.0f, 0.0f, 0.0f };

	if (g_pNavigationInfo == NULL || g_pNavigationInfo->m_headlight)
	{
		TheseusSetTransform(D3DTS_WORLD, &g_matIdentity);

		D3DLIGHT8 light;
		ZeroMemory(&light, sizeof(light));
		light.Type = D3DLIGHT_DIRECTIONAL;
		light.Diffuse.r = light.Diffuse.g = light.Diffuse.b = 0.5f;
		light.Specular.r = light.Specular.g = light.Specular.b = 0.75f;
		light.Ambient.r = light.Ambient.g = light.Ambient.b = 0.2f;

		ambient.r += light.Ambient.r;
		ambient.g += light.Ambient.g;
		ambient.b += light.Ambient.b;

		D3DXVECTOR3 dir(0.0f, 0.0f, -1.0f);
		D3DXVec3TransformNormal(&dir, &dir, &g_matPosition);
		light.Direction = dir;

		TheseusSetLight(nLights, &light);
		TheseusLightEnable(nLights, true);
		nLights++;
	}

	g_pObject->SetLight(nLights, ambient);

	TheseusSetRenderState(D3DRS_AMBIENT, D3DCOLOR_COLORVALUE(Clamp01(ambient.r), Clamp01(ambient.g), Clamp01(ambient.b), 1.0f));

	for (int i = nLights; i < s_nLastLights; i++)
		TheseusLightEnable(i, false);

	TheseusSetRenderState(D3DRS_LIGHTING, nLights > 0);
	s_nLastLights = nLights;
}

// Draw()
// Reconstructed from 5960 retail XBE @ 0x0002d0e0. Projection if dirty;
// then the background: a clear to the sky color (dark blue without a
// Background node) plus its backdrop, or, while a transition blurs (factor
// under 1, past the first two frames, black sky, no backdrop), a fade of
// the last frame instead. Then a default material (grey, created once),
// the scene drawn from an identity world, and Present. Theseus adds the
// lights, wireframe, its frame profiler, widgets and the overlay.
void Draw()
{
	Frame_PhaseBegin(FP_SCENE_SETUP);

	UpdateProjection();

	g_uMesh = 0;
	g_uMeshRef = 0;

	TheseusBeginScene();
	TheseusSetRenderState(D3DRS_FILLMODE, g_bWireframe ? D3DFILL_WIREFRAME : D3DFILL_SOLID);

	// A bigger factor means less blur.
	const float blurAlpha = g_transitionMotionBlur;

	D3DCOLOR sky = D3DCOLOR_COLORVALUE(0.0f, 0.0f, 0.3f, 1.0f);
	bool bBackdrop = false;
	if (g_pBackground != NULL)
	{
		sky = D3DCOLOR_COLORVALUE(g_pBackground->m_skyColor.x, g_pBackground->m_skyColor.y, g_pBackground->m_skyColor.z, 1.0f);
		bBackdrop = g_pBackground->m_backdrop != NULL;
	}

	static int s_nFrames;
	if (blurAlpha >= 1.0f || s_nFrames++ < 2 || sky != D3DCOLOR_XRGB(0, 0, 0) || bBackdrop)
	{
		TheseusClear(sky);
		if (bBackdrop)
			g_pBackground->RenderBackdrop();
	}
	else
	{
		FadeLastFrame(blurAlpha);
	}

	SetupLights();

	static D3DMATERIAL8 s_material;
	if (s_material.Diffuse.r == 0.0f)
	{
		D3DCOLORVALUE grey = { 0.5f, 0.5f, 0.5f, 1.0f };
		s_material.Diffuse = s_material.Ambient = s_material.Specular = s_material.Emissive = grey;
		s_material.Power = 0.0f;
	}
	TheseusSetMaterial(&s_material);

	Frame_PhaseEnd(FP_SCENE_SETUP);

	Frame_PhaseBegin(FP_SCENE_TREE);
	TheseusPushWorld();
	TheseusIdentityWorld();
	TheseusUpdateWorld();
	g_pObject->Render();
	TheseusPopWorld();
	Frame_PhaseEnd(FP_SCENE_TREE);

	Frame_PhaseBegin(FP_SCENE_END);
	TheseusEndScene();
	Frame_PhaseEnd(FP_SCENE_END);

	Frame_PhaseBegin(FP_WIDGETS);
	TheseusSetRenderState(D3DRS_SWATHWIDTH, D3DSWATH_OFF);
	Frame_PhaseBegin(FP_WIDGET_TICK);
	TickWidgets();
	Frame_PhaseEnd(FP_WIDGET_TICK);
	DrawWidgets();
	Frame_PhaseEnd(FP_WIDGETS);

	Frame_PhaseBegin(FP_OVERLAY);
	UpdateCubeOverlay();
	DrawCubeOverlay();
	Frame_PhaseEnd(FP_OVERLAY);

	Frame_PhaseBegin(FP_PRESENT);
	TheseusPresent();
	Frame_PhaseEnd(FP_PRESENT);
	TheseusSetRenderState(D3DRS_SWATHWIDTH, D3DSWATH_128);

	g_dwFrameTick = GetTickCount();
}


// Xbox platform init

static char szSystemPath[255];
static char szConfigPath[255];

#ifndef ARRAYSIZE
#define ARRAYSIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

// XboxFormatMemoryUnit(port, slot)
// Reconstructed from 5960 retail XBE @ 0x0002dcc0: create the unit's device
// object (its name in a 32 byte ANSI buffer), format it as FAT, close the
// object. Fails only if the device object can't be made.
bool XboxFormatMemoryUnit(UINT nDevicePort, UINT nMemoryUnit)
{
	char szDevice[MAX_MUNAME];
	ANSI_STRING device;
	device.Length = 0;
	device.MaximumLength = sizeof(szDevice) - 1;
	device.Buffer = szDevice;

	if (MU_CreateDeviceObject(nDevicePort, nMemoryUnit, &device) != NO_ERROR)
		return false;

	XapiFormatFATVolume(&device);
	MU_CloseDeviceObject(nDevicePort, nMemoryUnit);
	return true;
}

static void InitSkin()
{
	TCHAR CurrentSkinFile[MAX_PATH] = {0};
	TCHAR WorkerString[MAX_PATH] = {0};
	TCHAR SkinString[MAX_PATH] = {0};

	CSettingsFile* pSkinXBX = new CSettingsFile;
	bool bSkinLoaded = false;

	if (pSkinXBX->Open(_T("Q:\\System\\Config.ini")))
	{
		if (pSkinXBX->GetValue(_T("Dashboard Settings"), _T("Current Skin"), CurrentSkinFile, MAX_PATH))
		{
			pSkinXBX->Close();

			// CurrentSkinFile comes straight out of Config.ini and can be as
			// long as the buffer itself, so both of these have to be bounded.
			// _sntprintf leaves the string unterminated when it truncates.
			_sntprintf(SkinString, countof(SkinString) - 1, _T("Q:\\Skins\\%s\\"), CurrentSkinFile);
			SkinString[countof(SkinString) - 1] = 0;
			_sntprintf(WorkerString, countof(WorkerString) - 1, _T("%s%s.xbx"), SkinString, CurrentSkinFile);
			WorkerString[countof(WorkerString) - 1] = 0;

			if (pSkinXBX->Open(WorkerString))
			{
				bSkinLoaded = true;
			}
			else
			{
				OutputDebugString(_T("[InitSkin] Failed to load custom skin, attempting fallback...\n"));
			}
		}
		else
		{
			OutputDebugString(_T("[InitSkin] Config.ini missing 'Current Skin' entry\n"));
			pSkinXBX->Close();
		}
	}
	else
	{
		OutputDebugString(_T("[InitSkin] Could not open Config.ini\n"));
	}

	if (!bSkinLoaded)
	{
		_tcscpy(CurrentSkinFile, _T("Stock"));
		_stprintf(SkinString, _T("Q:\\Skins\\Stock\\"));
		_stprintf(WorkerString, _T("%sStock.xbx"), SkinString);

		pSkinXBX->Close();
		if (!pSkinXBX->Open(WorkerString))
		{
			OutputDebugString(_T("[InitSkin] FATAL: Failed to load fallback Stock skin\n"));
			delete pSkinXBX;
			g_pSkinSettings = NULL;
			g_sSkinDir = NULL;
			return;
		}
	}

	g_sSkinDir = new TCHAR[_tcslen(SkinString) + 1];
	_tcscpy(g_sSkinDir, SkinString);
	g_pSkinSettings = pSkinXBX;

	OutputDebugString(_T("[InitSkin] Skin directory set to: "));
	OutputDebugString(g_sSkinDir);
	OutputDebugString(_T("\n"));

	TCHAR szNoisy[16];
	if (g_pSkinSettings->GetValue(_T("Camera"), _T("Noisy"), szNoisy, countof(szNoisy)))
		g_bMovingScreen = (_tcsicmp(szNoisy, _T("false")) != 0);
}

extern void FlushTextureCache();
extern void FlushMeshCache();

void ReloadSkin()
{
	if (g_pSkinSettings)
	{
		g_pSkinSettings->Close();
		delete g_pSkinSettings;
		g_pSkinSettings = NULL;
	}
	if (g_sSkinDir)
	{
		delete [] g_sSkinDir;
		g_sSkinDir = NULL;
	}

	InitSkin();

	if (!g_pSkinSettings)
	{
		OutputDebugString(_T("[ReloadSkin] InitSkin failed, skin not reloaded\n"));
		return;
	}

	FlushTextureCache();
	FlushMeshCache();
	Material_Init(true);

	OutputDebugString(_T("[ReloadSkin] Skin reloaded successfully\n"));
}

static void InitDiscord()
{
	g_DiscordEnabled = false;
	InitDiscordConfig();

	if (!IsDiscordRelayEnabled())
	{
		OutputDebugString(_T("[Discord] Relay is disabled in config.\n"));
		return;
	}

	OutputDebugString(_T("[Discord] Relay is enabled.\n"));
	g_DiscordEnabled = true;

	const char* titleIdHex = "0ffeeff0";
	SendDiscordRelayFromConfig(titleIdHex);
}

// Xbox_Init()
// 5960 @ 0x0002dd20 clears 480p on PAL, sets a 1 MB file cache, links its
// drives, makes C:\TDATA\fffe0000\music, waits for device enumeration and
// clears the temp audio files. Theseus links its own drive set (Q: is its
// data dir, F: to N: the extra partitions), uses a 16 MB cache, and brings
// up the skin, the network and Discord here.
static void Xbox_Init()
{
	XI_GetProgramPath(szSystemPath);
	sprintf(szConfigPath, "%s\\uixdata", szSystemPath);
	OBJECT_STRING uixsystem;
	RtlInitObjectString(&uixsystem, szConfigPath);

	OBJECT_STRING ddd = CONSTANT_OBJECT_STRING("\\??\\d:");
	IoDeleteSymbolicLink(&ddd);

	struct {
		OBJECT_STRING drive;
		OBJECT_STRING path;
		bool* existsFlag;
	} driveLinks[] = {
		{CONSTANT_OBJECT_STRING("\\??\\Q:"), uixsystem, NULL},
		{CONSTANT_OBJECT_STRING("\\??\\C:"), CONSTANT_OBJECT_STRING("\\Device\\Harddisk0\\partition2"), NULL},
		{CONSTANT_OBJECT_STRING("\\??\\D:"), CONSTANT_OBJECT_STRING("\\Device\\CdRom0"), NULL},
		{CONSTANT_OBJECT_STRING("\\??\\E:"), CONSTANT_OBJECT_STRING("\\Device\\Harddisk0\\partition1"), NULL},
		{CONSTANT_OBJECT_STRING("\\??\\F:"), CONSTANT_OBJECT_STRING("\\Device\\Harddisk0\\partition6"), &g_fExists},
		{CONSTANT_OBJECT_STRING("\\??\\G:"), CONSTANT_OBJECT_STRING("\\Device\\Harddisk0\\partition7"), &g_gExists},
		{CONSTANT_OBJECT_STRING("\\??\\X:"), CONSTANT_OBJECT_STRING("\\Device\\Harddisk0\\partition5"), NULL},
		{CONSTANT_OBJECT_STRING("\\??\\Y:"), CONSTANT_OBJECT_STRING("\\Device\\Harddisk0\\partition4"), NULL},
		{CONSTANT_OBJECT_STRING("\\??\\Z:"), CONSTANT_OBJECT_STRING("\\Device\\Harddisk0\\partition3"), NULL},
		{CONSTANT_OBJECT_STRING("\\??\\MUSIC:"), CONSTANT_OBJECT_STRING("\\Device\\Harddisk0\\partition1\\TDATA\\fffe0000"), NULL},
		{CONSTANT_OBJECT_STRING("\\??\\H:"), CONSTANT_OBJECT_STRING("\\Device\\Harddisk0\\partition8"), &g_hExists},
		{CONSTANT_OBJECT_STRING("\\??\\I:"), CONSTANT_OBJECT_STRING("\\Device\\Harddisk0\\partition9"), &g_iExists},
		{CONSTANT_OBJECT_STRING("\\??\\J:"), CONSTANT_OBJECT_STRING("\\Device\\Harddisk0\\partition10"), &g_jExists},
		{CONSTANT_OBJECT_STRING("\\??\\K:"), CONSTANT_OBJECT_STRING("\\Device\\Harddisk0\\partition11"), &g_kExists},
		{CONSTANT_OBJECT_STRING("\\??\\L:"), CONSTANT_OBJECT_STRING("\\Device\\Harddisk0\\partition12"), &g_lExists},
		{CONSTANT_OBJECT_STRING("\\??\\M:"), CONSTANT_OBJECT_STRING("\\Device\\Harddisk0\\partition13"), &g_mExists},
		{CONSTANT_OBJECT_STRING("\\??\\N:"), CONSTANT_OBJECT_STRING("\\Device\\Harddisk0\\partition14"), &g_nExists},
	};

	for (int i = 0; i < ARRAYSIZE(driveLinks); ++i)
	{
		NTSTATUS status = IoCreateSymbolicLink(&driveLinks[i].drive, &driveLinks[i].path);
		if (driveLinks[i].existsFlag)
			*driveLinks[i].existsFlag = NT_SUCCESS(status);
		else
			VERIFY(NT_SUCCESS(status));
	}
	BootMark("  Xbox_Init: drive symlinks");

	CreateDirectory("e:\\TDATA", NULL);
	CreateDirectory("e:\\TDATA\\fffe0000", NULL);
	CreateDirectory("e:\\TDATA\\fffe0000\\music", NULL);
	BootMark("  Xbox_Init: CreateDirectory(TDATA)");

	XSetFileCacheSize(1024 * 1024 * 16);
	BootMark("  Xbox_Init: XSetFileCacheSize");

	XInitDevices(0, NULL);
	BootMark("  Xbox_Init: XInitDevices");

	if (SetFileAttributesA(TheseusTempPcmFileA, FILE_ATTRIBUTE_NORMAL))
		DeleteFileA(TheseusTempPcmFileA);
	if (SetFileAttributesA(TheseusTempWmaFileA, FILE_ATTRIBUTE_NORMAL))
		DeleteFileA(TheseusTempWmaFileA);
	BootMark("  Xbox_Init: temp file cleanup");

	g_nCurRegion = XGetGameRegion();
	BootMark("  Xbox_Init: XGetGameRegion");

	InitSkin();
	BootMark("  Xbox_Init: InitSkin");

	if (net::init())
	{
		IN_ADDR addr = {net::getAddress()};
		char ipStr[16];
		XNetInAddrToString(addr, ipStr, sizeof(ipStr));
		TRACE(_T("Network Up. IP: %S\n"), ipStr);
	}
	else
	{
		TRACE(_T("Network failed to initialize.\n"));
	}
	BootMark("  Xbox_Init: net::init");

	InitDiscord();
	BootMark("  Xbox_Init: InitDiscord");
}



// InitApp()
// 5960 @ 0x0002d7e0: CD-ROM and disc type, Xbox_Init, version lock and the
// online redirect, InitD3D, LoadXIP, Class_Init, materials, then load and
// create the startup scene and call its initialize. Theseus keeps that
// order without the online steps, opens DirectSound up front, and shows
// the panic screen instead of stopping when a step fails.
bool InitApp()
{
	BootBanner();
	BootMark("start");
	Memory_Init();                               BootMark("Memory_Init");
	Xbox_Init();                                 BootMark("Xbox_Init");

#ifdef _DEBUG
	Debug_Init();                                BootMark("Debug_Init");
#endif

	DiscDrive_Init();                            BootMark("DiscDrive_Init");

	if (!InitD3D())
		return false;
	BootMark("InitD3D");

	InitAudio();                                 BootMark("InitAudio");

	// Prime path globals (g_sFontDir, g_sXipDir, ...) before LoadXIP so
	// the panic screen has a font path to render from if XIP load fails.
	TCHAR szFileToLoad[MAX_PATH];
	szFileToLoad[0] = 0;
	GetStartupClassFile(szFileToLoad);           BootMark("GetStartupClassFile");

	if (!LoadXIP(_T("Q:\\Xips\\default.xip"), true))
		TheseusPanic(_T("Q:\\Xips\\default.xip is missing or unreadable."),
		             _T("Copy default.xip back into Q:\\Xips over FTP, then restart."));
	BootMark("LoadXIP default.xip (sync)");

	Class_Init();                                BootMark("Class_Init");
	TitleArray_Init();                           BootMark("TitleArray_Init");
	Material_Init(false);                        BootMark("Material_Init");

	if (!g_pClass->Load(szFileToLoad))
	{
		extern bool g_bXapSyntaxError;
		TCHAR reason[MAX_PATH + 64];
		_stprintf(reason,
		          g_bXapSyntaxError ? _T("Parse error in %s")
		                        : _T("Cannot open %s"),
		          szFileToLoad);
		TheseusPanic(reason,
		             _T("Replace the file named above over FTP, then restart."));
	}
	BootMark("g_pClass->Load(startup XAP)");

	g_pObject = (CInstance *)g_pClass->CreateNode();
	if (g_pObject == NULL)
		TheseusPanic(_T("The startup scene could not be created."),
		             _T("Replace Q:\\Xips over FTP, then restart."));
	BootMark("CreateNode");

	// VERIFYHR still runs the call in retail but throws the result away, and a
	// null matrix stack takes the first frame down with no explanation.
	if (FAILED(D3DXCreateMatrixStack(0, &g_worldStack)) || g_worldStack == NULL)
		TheseusPanic(_T("Out of memory setting up the renderer."),
		             _T("Restart the console. If it repeats, reinstall Theseus."));
	BootMark("D3DXCreateMatrixStack");

	CallFunction(g_pObject, _T("initialize"));
	BootMark("CallFunction initialize");

	PreloadSkinTextures();
	BootMark("PreloadSkinTextures");

	OutputDebugStringA("[Boot] InitApp DONE\n");
	return true;
}

// CleanupApp()
// 5960 @ 0x0002d930 shuts XOnline down, frees the matrix stack, the scene
// and its class, then Class_Exit, the XIP sections, Text_Exit, DirectSound
// and the app dir. Theseus releases D3D first, drops Locale too, has no
// XOnline session to end and lets DSoundManager close the sound side.
void CleanupApp()
{
	ReleaseD3D();

	delete g_pObject;
	g_pObject = NULL;
	delete g_pClass;
	g_pClass = NULL;

	Class_Exit();
	Locale_Exit();
	Text_Exit();

	DSoundManager::Instance()->Cleanup();

	delete[] g_szAppDir;
	g_szAppDir = NULL;

#ifdef _DEBUG
	Debug_Exit();
#endif
}

// Advance()
// Reconstructed from the frame loop in 5960 AppShell_Run @ 0x0002da00: the clock is
// GetTickCount in seconds, the step is the change since last frame (never
// zero: 5960 uses one millisecond's worth), then the camera, every lerper
// and the scene advance. Theseus also feeds its background loader; 5960
// polls the keyboard and runs its deferred deletes here.
void Advance()
{
	XTIME now = (float)GetTickCount() / 1000.0f;
	float nDelta = (float)(now - g_now);
	if (nDelta == 0.0f)
		nDelta = 0.001f;
	g_now = now;

	theCamera.Advance(nDelta);
	CLerper::AdvanceAll();
	g_pObject->Advance(nDelta);

	BackgroundLoader_Frame();

#ifdef _DEBUG
	Debug_Frame();
#endif
}

void Theseus()
{
	Theseus_Init();

	if (!InitApp())
	{
		CleanupApp();
		return;
	}

	bool firstFrame = true;
	for (;;)
	{
		Frame_Begin();
		Frame_PhaseBegin(FP_ADVANCE);
		Advance();
		Frame_PhaseEnd(FP_ADVANCE);
		Draw();
		Frame_End();

		if (firstFrame)
		{
			BootMark("first frame complete");
			firstFrame = false;
		}
	}

	CleanupApp();
}

// GetStartupClassFile(out)
// 5960 @ 0x0002dbc0 starts from y:\default.xap with y:/default.xap as the
// current dir and keeps the app dir. Theseus runs from Q:, so the scene is
// Q:\Xips\default.xap, and it also records its font and XIP dirs.
void GetStartupClassFile(TCHAR *szFileToLoad)
{
	_tcscpy(szFileToLoad, _T("Q:\\Xips\\default.xap"));
	_tcscpy(g_szCurDir, _T("Q:/Xips/default.xap"));

	g_szAppDir = new TCHAR[_tcslen(_T("Q:\\")) + 1];
	_tcscpy(g_szAppDir, _T("Q:\\"));

	g_sFontDir = new TCHAR[_tcslen(_T("Q:\\Fonts\\")) + 1];
	_tcscpy(g_sFontDir, _T("Q:\\Fonts\\"));

	g_sXipDir = new TCHAR[_tcslen(_T("Q:\\Xips\\")) + 1];
	_tcscpy(g_sXipDir, _T("Q:\\Xips\\"));
}

LONG XdashUnhandledExceptionFilter(LPEXCEPTION_POINTERS pEx)
{
	// Drop into panic mode so the user gets a recovery screen with FTP
	// info and a crash log on disk, instead of a silent reboot.
	TheseusPanic(_T("Unhandled exception in dashboard."),
	             _T("Restart the console. If it repeats, send Q:\\Logs\\panic-*.txt to us."),
	             pEx);

	// TheseusPanic doesn't return, but if D3D was unavailable it will
	// have rebooted on its own. This line is the unreachable safety net.
	HalReturnToFirmware(HalRebootRoutine);
	__asm { hlt }
}

// main()
// 5960 @ 0x0002dc80 installs its crash handler unless the hardware says
// it's a debug kit, then runs the app. Theseus decides that at build time
// (DEVKIT) and its handler is the panic screen above.
int __cdecl main(int /*argc*/, char * /*argv*/[])
{
#ifndef DEVKIT
	SetUnhandledExceptionFilter((LPTOP_LEVEL_EXCEPTION_FILTER)XdashUnhandledExceptionFilter);
#endif

	Theseus();
}
