// main.cpp: Xbox entry point. Sets up D3D, audio, network, the scene
// graph, and the main loop; defines the dashboard globals declared in
// theseus.h. Owns the boot path, the Advance / Draw cycle, and the
// recovery / panic hook. Decompiled from the 5960 retail XBE.

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

extern "C" void Alert(const TCHAR *szMsg, ...)
{
	va_list args;
	va_start(args, szMsg);

	TCHAR szBuffer[512];
	_vsntprintf(szBuffer, countof(szBuffer), szMsg, args);
	Trace(_T("\007%s\n"), szBuffer);
	va_end(args);
}

#ifdef _UNICODE
void Unicode(TCHAR *wsz, const char *sz, int nMaxChars)
{
	while (nMaxChars-- > 0)
	{
		if ((*wsz++ = (unsigned char)*sz++) == 0)
			return;
	}
}

void Ansi(char *sz, const TCHAR *wsz, int nMaxChars)
{
	while (nMaxChars-- > 0)
	{
		if ((*sz++ = (char)*wsz++) == 0)
			return;
	}
}
#endif // _UNICODE

extern "C" PLAUNCH_DATA_PAGE *LaunchDataPage;

static void Theseus_Init()
{
	g_dwStartTick = g_dwFrameTick = GetTickCount();
	g_now = (float)g_dwFrameTick / 1000.0f;

	g_pClass = new CClass;

	// Retrieve the launch data, if any
	if (*LaunchDataPage && (*LaunchDataPage)->Header.dwLaunchDataType == LDT_LAUNCH_DASHBOARD)
	{
		PLD_LAUNCH_DASHBOARD pLaunchDashboard =
			(PLD_LAUNCH_DASHBOARD)((*LaunchDataPage)->LaunchData);

		g_bHasLaunchData = true;
		g_dwTitleID = (*LaunchDataPage)->Header.dwTitleId;
		g_dwLaunchReason = pLaunchDashboard->dwReason;
		g_dwLaunchContext = pLaunchDashboard->dwContext;
		g_dwLaunchParameter1 = pLaunchDashboard->dwParameter1;
		g_dwLaunchParameter2 = pLaunchDashboard->dwParameter2;

		PLAUNCH_DATA_PAGE pTemp = *LaunchDataPage;
		*LaunchDataPage = NULL;
		MmFreeContiguousMemory(pTemp);
	}

	g_dwMainThreadId = GetCurrentThreadId();
}

extern CNtIoctlCdromService g_cdrom;
extern int GetDiscType();

HRESULT InitAudio()
{
	HRESULT hr = S_OK;
	do
	{
		// retreive the status of DVD/CD Rom
		g_cdrom.Open(1);
		g_nDiscType = GetDiscType();

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

void XboxInitRenderState()
{
	float fZero = 0.0f;
	float fOne = 1.0f;

	TheseusSetRenderState(D3DRS_ZENABLE, TheseusGetZBuffer() ? D3DZB_TRUE : D3DZB_FALSE);
	TheseusSetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
	TheseusSetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
	TheseusSetRenderState(D3DRS_ZWRITEENABLE, TRUE);
	TheseusSetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	TheseusSetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
	TheseusSetRenderState(D3DRS_DESTBLEND, D3DBLEND_ZERO);
	TheseusSetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);
	TheseusSetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	TheseusSetRenderState(D3DRS_ALPHAREF, 0);
	TheseusSetRenderState(D3DRS_ALPHAFUNC, D3DCMP_ALWAYS);
	TheseusSetRenderState(D3DRS_DITHERENABLE, TRUE);
	TheseusSetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	TheseusSetRenderState(D3DRS_FOGENABLE, FALSE);
	TheseusSetRenderState(D3DRS_SPECULARENABLE, FALSE);
	TheseusSetRenderState(D3DRS_FOGCOLOR, 0);
	TheseusSetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_NONE);
	TheseusSetRenderState(D3DRS_FOGDENSITY, *(LPDWORD)&fOne);
	TheseusSetRenderState(D3DRS_EDGEANTIALIAS, FALSE);
	TheseusSetRenderState(D3DRS_ZBIAS, 0);
	TheseusSetRenderState(D3DRS_RANGEFOGENABLE, FALSE);
	TheseusSetRenderState(D3DRS_STENCILENABLE, FALSE);
	TheseusSetRenderState(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP);
	TheseusSetRenderState(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP);
	TheseusSetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP);
	TheseusSetRenderState(D3DRS_STENCILFUNC, D3DCMP_ALWAYS);
	TheseusSetRenderState(D3DRS_STENCILREF, 0);
	TheseusSetRenderState(D3DRS_STENCILMASK, 0xffffffff);
	TheseusSetRenderState(D3DRS_STENCILWRITEMASK, 0xffffffff);
	TheseusSetRenderState(D3DRS_WRAP0, 0);
	TheseusSetRenderState(D3DRS_WRAP1, 0);
	TheseusSetRenderState(D3DRS_WRAP2, 0);
	TheseusSetRenderState(D3DRS_WRAP3, 0);
	TheseusSetRenderState(D3DRS_LIGHTING, TRUE);
	TheseusSetRenderState(D3DRS_AMBIENT, 0);
	TheseusSetRenderState(D3DRS_COLORVERTEX, TRUE);
	TheseusSetRenderState(D3DRS_LOCALVIEWER, TRUE);
	TheseusSetRenderState(D3DRS_NORMALIZENORMALS, FALSE);
	TheseusSetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1);
	TheseusSetRenderState(D3DRS_SPECULARMATERIALSOURCE, D3DMCS_COLOR2);
	TheseusSetRenderState(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_COLOR2);
	TheseusSetRenderState(D3DRS_EMISSIVEMATERIALSOURCE, D3DMCS_MATERIAL);
	TheseusSetRenderState(D3DRS_VERTEXBLEND, D3DVBF_DISABLE);
	TheseusSetRenderState(D3DRS_POINTSIZE, *(LPDWORD)&fOne);
	TheseusSetRenderState(D3DRS_POINTSIZE_MIN, *(LPDWORD)&fOne);
	TheseusSetRenderState(D3DRS_POINTSPRITEENABLE, FALSE);
	TheseusSetRenderState(D3DRS_POINTSCALEENABLE, FALSE);
	TheseusSetRenderState(D3DRS_POINTSCALE_A, *(LPDWORD)&fOne);
	TheseusSetRenderState(D3DRS_POINTSCALE_B, *(LPDWORD)&fZero);
	TheseusSetRenderState(D3DRS_POINTSCALE_C, *(LPDWORD)&fZero);
	TheseusSetRenderState(D3DRS_MULTISAMPLEANTIALIAS, TRUE);
	TheseusSetRenderState(D3DRS_MULTISAMPLEMASK, 0xffffffff);
	TheseusSetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA);

	g_transitionMotionBlur = 1.0f;
}

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

	if (isPAL)
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
			g_pp.BackBufferWidth = (g_nDiscType == DISC_VIDEO) ? 720 : 640;
			g_pp.BackBufferHeight = isPAL ? 576 : 480;
			break;

		case XC_AV_PACK_HDTV:
			g_pp.BackBufferWidth = (g_nDiscType == DISC_VIDEO) ? 720 : 640;
			g_pp.BackBufferHeight = 480;

			// Check for DVD 480p override
			if ((XBOX_480P_MACROVISION_ENABLED & XboxHardwareInfo->Flags) && g_nDiscType == DISC_VIDEO)
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

	if (g_pD3D != NULL)
	{
		g_pD3D->Release();
		g_pD3D = NULL;
	}
}

struct COLORVERTEX
{
	float dvX, dvY, dvZ;
	DWORD color;
};

void Draw()
{
	Frame_PhaseBegin(FP_SCENE_SETUP);

	// Setup projection transform...
	if (g_bProjectionDirty)
	{
		g_bProjectionDirty = false;

		float nNear = 0.1f;
		float nFar = 1000.0f;
		float fieldOfView = D3DX_PI / 2.0f;

		if (g_pNavigationInfo != NULL)
		{
			nNear = g_pNavigationInfo->m_avatarSize.x / 2.0f;
			if (g_pNavigationInfo->m_visibilityLimit != 0.0f)
				nFar = g_pNavigationInfo->m_visibilityLimit;
		}

		if (g_pViewpoint != NULL)
		{
			fieldOfView = g_pViewpoint->m_fieldOfView;
		}
		float aspect = g_nViewWidth / g_nViewHeight;

		D3DXMatrixPerspectiveFovLH(&g_matProjection, fieldOfView, aspect, nNear, nFar);
		TheseusSetTransform(D3DTS_PROJECTION, &g_matProjection);
	}

	g_uMesh = 0;
	g_uMeshRef = 0;

	TheseusBeginScene();

	TheseusSetRenderState(D3DRS_FILLMODE, g_bWireframe ? D3DFILL_WIREFRAME : D3DFILL_SOLID);

	const float blurAlpha = g_transitionMotionBlur; // bigger number -> less blur

	D3DCOLOR color;
	bool bBackdrop = false;
	if (g_pBackground == NULL)
		color = D3DCOLOR_COLORVALUE(0.0f, 0.0f, 0.3f, 1.0f);
	else
		color = D3DCOLOR_COLORVALUE(g_pBackground->m_skyColor.x, g_pBackground->m_skyColor.y, g_pBackground->m_skyColor.z, 1.0f);

	if (g_pBackground != NULL && g_pBackground->m_backdrop != NULL)
		bBackdrop = true;

	static int nBeenHere;
	if (blurAlpha >= 1.0f || nBeenHere++ < 2 || color != D3DCOLOR_XRGB(0, 0, 0) || bBackdrop)
	{
		TheseusClear(color); // NOTE: Clears back and Z buffers (if Z buffer is enabled)

		if (bBackdrop)
			g_pBackground->RenderBackdrop();
	}
	else
	{
		static LPDIRECT3DVERTEXBUFFER8 m_pVB;
		if (m_pVB == NULL)
		{
			VERIFYHR(TheseusGetD3DDev()->CreateVertexBuffer(4 * sizeof(COLORVERTEX), D3DUSAGE_WRITEONLY, D3DFVF_XYZ | D3DFVF_DIFFUSE, D3DPOOL_MANAGED, &m_pVB));

			COLORVERTEX *verts;
			VERIFYHR(m_pVB->Lock(0, 4 * sizeof(COLORVERTEX), (BYTE **)&verts, 0));

			verts[0].dvX = (float)TheseusGetViewWidth() / 2.0f;
			verts[0].dvY = -(float)TheseusGetViewHeight() / 2.0f;
			verts[0].dvZ = 0.0f;
			verts[0].color = D3DCOLOR_COLORVALUE(0.0f, 0.0f, 0.0f, blurAlpha);

			verts[1].dvX = -(float)TheseusGetViewWidth() / 2.0f;
			verts[1].dvY = -(float)TheseusGetViewHeight() / 2.0f;
			verts[1].dvZ = 0.0f;
			verts[1].color = D3DCOLOR_COLORVALUE(0.0f, 0.0f, 0.0f, blurAlpha);

			verts[2].dvX = (float)TheseusGetViewWidth() / 2.0f;
			verts[2].dvY = (float)TheseusGetViewHeight() / 2.0f;
			verts[2].dvZ = 0.0f;
			verts[2].color = D3DCOLOR_COLORVALUE(0.0f, 0.0f, 0.0f, blurAlpha);

			verts[3].dvX = -(float)TheseusGetViewWidth() / 2.0f;
			verts[3].dvY = (float)TheseusGetViewHeight() / 2.0f;
			verts[3].dvZ = 0.0f;
			verts[3].color = D3DCOLOR_COLORVALUE(0.0f, 0.0f, 0.0f, blurAlpha);

			VERIFYHR(m_pVB->Unlock());
		}

		D3DXMATRIX matProjection, matProjectionSave, matWorldSave, matViewSave;

		TheseusGetTransform(D3DTS_PROJECTION, &matProjectionSave);
		TheseusGetTransform(D3DTS_WORLD, &matWorldSave);
		TheseusGetTransform(D3DTS_VIEW, &matViewSave);

		D3DXMatrixOrthoLH(&matProjection, TheseusGetViewWidth(), TheseusGetViewHeight(), -10000.0f, 10000.0f);
		TheseusSetTransform(D3DTS_PROJECTION, &matProjection);
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

		VERIFYHR(TheseusGetD3DDev()->SetStreamSource(0, m_pVB, sizeof(COLORVERTEX)));
		VERIFYHR(TheseusGetD3DDev()->SetVertexShader(D3DFVF_XYZ | D3DFVF_DIFFUSE));
		VERIFYHR(TheseusGetD3DDev()->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2));

		TheseusSetTransform(D3DTS_PROJECTION, &matProjectionSave);
		TheseusSetTransform(D3DTS_WORLD, &matWorldSave);
		TheseusSetTransform(D3DTS_VIEW, &matViewSave);
	}

	// Setup lights...
	{
		static int nLastLight = -1;
		int nLight = 0;
		D3DCOLORVALUE ambient;
		ambient.r = 0.0f;
		ambient.g = 0.0f;
		ambient.b = 0.0f;

		// Headlight...
		if (g_pNavigationInfo == NULL || g_pNavigationInfo->m_headlight)
		{
			TheseusSetTransform(D3DTS_WORLD, &g_matIdentity);

			D3DLIGHT8 d3dLight;
			ZeroMemory(&d3dLight, sizeof(d3dLight));

			d3dLight.Type = D3DLIGHT_DIRECTIONAL;

			d3dLight.Diffuse.r = 0.5f;
			d3dLight.Diffuse.g = 0.5f;
			d3dLight.Diffuse.b = 0.5f;

			d3dLight.Specular.r = 0.75f;
			d3dLight.Specular.g = 0.75f;
			d3dLight.Specular.b = 0.75f;

			d3dLight.Ambient.r = 0.2f;
			d3dLight.Ambient.g = 0.2f;
			d3dLight.Ambient.b = 0.2f;

			ambient.r += d3dLight.Ambient.r;
			ambient.g += d3dLight.Ambient.g;
			ambient.b += d3dLight.Ambient.b;

			D3DXVECTOR3 dir(0.0f, 0.0f, -1.0f);
			D3DXVec3TransformNormal(&dir, &dir, &g_matPosition);
			d3dLight.Direction = dir;

			TheseusSetLight(nLight, &d3dLight);
			TheseusLightEnable(nLight, true);
			nLight += 1;
		}

		g_pObject->SetLight(nLight, ambient);

		if (ambient.r < 0.0f)
			ambient.r = 0.0f;
		else if (ambient.r > 1.0f)
			ambient.r = 1.0f;
		if (ambient.g < 0.0f)
			ambient.g = 0.0f;
		else if (ambient.g > 1.0f)
			ambient.g = 1.0f;
		if (ambient.b < 0.0f)
			ambient.b = 0.0f;
		else if (ambient.b > 1.0f)
			ambient.b = 1.0f;

		TheseusSetRenderState(D3DRS_AMBIENT, D3DCOLOR_COLORVALUE(ambient.r, ambient.g, ambient.b, 1.0f));

		// Turn off lights we are not using...
		for (int i = nLight; i < nLastLight; i += 1)
			TheseusLightEnable(i, false);

		TheseusSetRenderState(D3DRS_LIGHTING, nLight > 0);
		nLastLight = nLight;
	}

	{
		static D3DMATERIAL8 mat;
		if (mat.Diffuse.r == 0.0f)
		{
			mat.Diffuse.r = 0.5f;
			mat.Diffuse.g = 0.5f;
			mat.Diffuse.b = 0.5f;
			mat.Diffuse.a = 1.0f;

			mat.Ambient.r = 0.5f;
			mat.Ambient.g = 0.5f;
			mat.Ambient.b = 0.5f;
			mat.Ambient.a = 1.0f;

			mat.Specular.r = 0.5f;
			mat.Specular.g = 0.5f;
			mat.Specular.b = 0.5f;
			mat.Specular.a = 1.0f;

			mat.Emissive.r = 0.5f;
			mat.Emissive.g = 0.5f;
			mat.Emissive.b = 0.5f;
			mat.Emissive.a = 1.0f;

			mat.Power = 0.0f;
		}

		TheseusSetMaterial(&mat);
	}

	Frame_PhaseEnd(FP_SCENE_SETUP);
	// Draw the world...
	Frame_PhaseBegin(FP_SCENE_TREE);
	{
		TheseusPushWorld();
		TheseusIdentityWorld();
		TheseusUpdateWorld();
		g_pObject->Render();
		TheseusPopWorld();
	}
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

bool XboxFormatMemoryUnit(UINT nDevicePort, UINT nMemoryUnit)
{
	ANSI_STRING st;
	char pszMU[MAX_MUNAME];
	st.Length = 0;
	st.MaximumLength = sizeof(pszMU) - 1;
	st.Buffer = pszMU;

	if (MU_CreateDeviceObject(nDevicePort, nMemoryUnit, &st) != NO_ERROR)
		return false;

	if (!XapiFormatFATVolume(&st))
		TRACE(_T("XApiFormatFATVolume failed: %d\n"), GetLastError());

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
		extern bool g_bParseError;
		TCHAR reason[MAX_PATH + 64];
		_stprintf(reason,
		          g_bParseError ? _T("Parse error in %s")
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

void GetStartupClassFile(TCHAR *szFileToLoad)
{
	TCHAR szAppDir[MAX_PATH];
	TCHAR szFontDir[MAX_PATH];
	TCHAR szXipDir[MAX_PATH];
	_tcscpy(szFileToLoad, _T("Q:\\Xips\\default.xap"));
	_tcscpy(szAppDir, _T("Q:\\"));
	_tcscpy(szFontDir, _T("Q:\\Fonts\\"));
	_tcscpy(szXipDir, _T("Q:\\Xips\\"));
	_tcscpy(g_szCurDir, _T("Q:/Xips/default.xap"));

	g_szAppDir = new TCHAR[_tcslen(szAppDir) + 1];
	_tcscpy(g_szAppDir, szAppDir);

	g_sFontDir = new TCHAR[_tcslen(szFontDir) + 1];
	_tcscpy(g_sFontDir, szFontDir);

	g_sXipDir = new TCHAR[_tcslen(szXipDir) + 1];
	_tcscpy(g_sXipDir, szXipDir);
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

int __cdecl main(int /*argc*/, char * /*argv*/[])
{
#ifndef DEVKIT
	SetUnhandledExceptionFilter((LPTOP_LEVEL_EXCEPTION_FILTER)XdashUnhandledExceptionFilter);
#endif

	Theseus();
}
