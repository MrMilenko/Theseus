// dashapp.cpp: desktop dashboard globals and main loop. Owns the
// D3D8 stub device, the Advance / Draw cycle, audio init, the scene
// graph root, and startup class file loading. Desktop counterpart
// to xbox/main.cpp.

#include "std.h"
#include "dashapp.h"
#include "file_util.h"
#include "xip_archive.h"
#include "node.h"
#include "screen.h"
#include "runner.h"
#include "lerper.h"
#include "camera.h"
#include "scene_groups.h"
#include "audio_sdl.h"
#include "cdaudio.h"
#include "settingsfile.h"
#include "display.h"

#include <algorithm>
extern void PreloadSkinTextures();

extern void Memory_Init();

extern void Debug_Init();
extern void Debug_Exit();

extern void Class_Init();
extern void Class_Exit();

extern void Debug_Frame();

extern void DashInit();

extern void DiscDrive_Init();

extern void AssetLoader_Pump();

extern void Text_Exit();

extern void Locale_Exit();

extern void TitleArray_Init();

extern void Material_Init(bool bReloadSkinXBX);

bool g_bWireframe = false;
bool g_bMovingScreen = true;

// ============================================================================
// DVD Closed Captioning Stubs (not used on desktop)
// ============================================================================

extern "C" void __stdcall D3DDevice_EnableCC(int)
{
}

extern "C" int __stdcall D3DDevice_GetCCStatus(int, int *)
{
	return 0;
}

extern "C" void __stdcall D3DDevice_SendCC(int, int, int)
{
}

// ============================================================================
// Globals
// ============================================================================

// Theseus-style globals. CDashApp is gone; these are the sole storage.
LPDIRECT3DDEVICE8 g_pD3DDev = NULL;
LPDIRECT3D8       g_pD3D = NULL;
D3DPRESENT_PARAMETERS g_pp = {};
float             g_nViewWidth = 0;
float             g_nViewHeight = 0;
bool              g_bStretchWidescreen = false;
bool              g_bEmulateXboxRes = true; // default on so existing XIPs keep working
bool              g_bZBuffer = false;
bool              g_bProjectionDirty = true;
ID3DXMatrixStack* g_worldStack = NULL;
DWORD             g_dwStartTick = 0;
DWORD             g_dwFrameTick = 0;
XTIME             g_now = 0.0;
CClass*           g_pClass = NULL;
CInstance*        g_pObject = NULL;
CScreen*          g_pScreen = NULL;
CViewpoint*       g_pViewpoint = NULL;
CNavigationInfo*  g_pNavigationInfo = NULL;
CBackground*      g_pBackground = NULL;
TCHAR*            g_szAppDir = NULL;
TCHAR*            g_sFontDir = NULL;
TCHAR*            g_sXipDir = NULL;
TCHAR*            g_sSkinDir = NULL;
CSettingsFile*    g_pSkinSettings = NULL;
bool              g_bHasLaunchData = false;
DWORD             g_dwTitleID = 0;
DWORD             g_dwLaunchContext = 0;
DWORD             g_dwLaunchParameter1 = 0;
DWORD             g_dwLaunchParameter2 = 0;
DWORD             g_dwMainThreadId = 0;

// Desktop-only state (was in CDashApp class, now standalone globals).
int               g_nVertPerFrame = 0;
int               g_nTriPerFrame = 0;
char*             g_szAppTitle = NULL;

D3DXMATRIX g_viewMatrix;
D3DXMATRIX g_cameraWorld;
D3DXMATRIX g_matProjection;
D3DRECT g_clipRect;
D3DRECT g_clipRect2x;

D3DXMATRIX g_matIdentity(
	1.0f, 0.0f, 0.0f, 0.0f,
	0.0f, 1.0f, 0.0f, 0.0f,
	0.0f, 0.0f, 1.0f, 0.0f,
	0.0f, 0.0f, 0.0f, 1.0f);

unsigned int g_uMesh;
unsigned int g_uMeshRef;

// Alert(format, ...)
// 5960 @ 0x0002c520 formats into a 512 character buffer and drops it, since
// retail has no debug output. The desktop sends the text to Trace.
extern "C" void Alert(const char *szMsg, ...)
{
	char szText[512];
	va_list args;
	va_start(args, szMsg);
	_vsntprintf(szText, countof(szText), szMsg, args);
	va_end(args);
	Trace("\007%s\n", szText);
}

extern int GetDiscType();

// ============================================================================
// Audio
// ============================================================================

HRESULT InitAudio()
{
	// Desktop: SDL_mixer audio engine
	if (DashAudio_Init() < 0)
		return E_FAIL;

	CdAudio_Init();

	return S_OK;
}

// ============================================================================
// D3D Initialization and Render State
// ============================================================================

float g_levelBlurAmount;

// =========================================================================
// D3D call-trace storage + dump (Sweep 2 of project_d3d_call_audit.md)
// =========================================================================

D3DStateStats g_rsStats[256] = {};
D3DStateStats g_tssStats[8 * 32] = {};

// Wrapper-level state caches. See TheseusSet* / TheseusGet* in
// theseus/shared/theseus.h.
// BSS-zeroed: valid[] all false on first call.
DWORD theseus_desktop_rs_cache[256];
bool  theseus_desktop_rs_valid[256];
DWORD theseus_desktop_tss_cache[8 * 32];
bool  theseus_desktop_tss_valid[8 * 32];
LPDIRECT3DTEXTURE8 theseus_desktop_tex_cache[8];
bool theseus_desktop_tex_valid[8];
D3DMATERIAL8 theseus_desktop_mat_cache;
bool theseus_desktop_mat_valid;
D3DMATRIX theseus_desktop_xform_cache[256];
bool theseus_desktop_xform_valid[256];
DWORD theseus_desktop_vs_cache;
bool theseus_desktop_vs_valid;
IDirect3DVertexBuffer8* theseus_desktop_vb_cache[4];
UINT theseus_desktop_vb_stride_cache[4];
bool theseus_desktop_vb_valid[4];
IDirect3DIndexBuffer8* theseus_desktop_ib_cache;
UINT theseus_desktop_ib_base_cache;
bool theseus_desktop_ib_valid;

// Trace counters for the new wrappers (currently storage-only; the
// inline wrappers in theseus.h don't increment these yet since the
// extra branch would cost more than the caches save. If we want to
// measure cache hit-rate later we can add them in like the
// SetRenderState path).
D3DStateStats g_texStats[8] = {};
D3DStateStats g_matStats = {};
D3DStateStats g_xformStats[256] = {};

// Pretty-print map for the common state IDs. Keeps the dump readable
// without having to cross-reference numeric constants in d3d8_sdl.h.
static const char* RsName(DWORD s)
{
    switch (s) {
        case 7:   return "ZENABLE";
        case 8:   return "FILLMODE";
        case 9:   return "SHADEMODE";
        case 14:  return "ZWRITEENABLE";
        case 15:  return "ALPHATESTENABLE";
        case 19:  return "SRCBLEND";
        case 20:  return "DESTBLEND";
        case 22:  return "CULLMODE";
        case 23:  return "ZFUNC";
        case 24:  return "ALPHAREF";
        case 25:  return "ALPHAFUNC";
        case 26:  return "DITHERENABLE";
        case 27:  return "ALPHABLENDENABLE";
        case 28:  return "FOGENABLE";
        case 29:  return "SPECULARENABLE";
        case 40:  return "EDGEANTIALIAS";
        case 47:  return "ZBIAS";
        case 60:  return "TEXTUREFACTOR";
        case 136: return "CLIPPING";
        case 137: return "LIGHTING";
        case 139: return "AMBIENT";
        case 141: return "COLORVERTEX";
        case 145: return "DIFFUSEMATSRC";
        case 146: return "SPECULARMATSRC";
        case 147: return "AMBIENTMATSRC";
        case 148: return "EMISSIVEMATSRC";
        case 151: return "VERTEXBLEND";
        case 161: return "MSAA";
        case 162: return "MSAAMASK";
        case 168: return "COLORWRITEENABLE";
        case 200: return "SWATHWIDTH";
        default:  return NULL;
    }
}

static const char* TssName(DWORD t)
{
    // D3DTSS_* values from theseus/desktop/d3d8_sdl.h
    switch (t) {
        case 1:  return "COLOROP";
        case 2:  return "COLORARG1";
        case 3:  return "COLORARG2";
        case 4:  return "ALPHAOP";
        case 5:  return "ALPHAARG1";
        case 6:  return "ALPHAARG2";
        case 8:  return "RESULTARG";
        case 11: return "TEXCOORDINDEX";
        case 13: return "ADDRESSU";
        case 14: return "ADDRESSV";
        case 15: return "MINFILTER";
        case 16: return "MAGFILTER";
        case 17: return "MIPFILTER";
        case 24: return "TEXTURETRANSFORMFLAGS";
        default: return NULL;
    }
}

void Theseus_D3D_DumpTrace()
{
    fprintf(stderr, "\n[D3DTrace] ============================================\n");
    fprintf(stderr, "[D3DTrace] RenderState call counts (top 25 by redundant)\n");
    fprintf(stderr, "[D3DTrace] ============================================\n");

    int order[256];
    for (int i = 0; i < 256; i++) order[i] = i;
    std::sort(order, order + 256, [](int a, int b) {
        return g_rsStats[a].redundantCalls > g_rsStats[b].redundantCalls;
    });
    fprintf(stderr, "  %-22s  %8s  %8s  %8s  %6s\n",
            "state", "total", "redund", "filtered", "red%");
    int shown = 0;
    for (int i = 0; i < 256 && shown < 25; i++) {
        int s = order[i];
        if (g_rsStats[s].totalCalls == 0) continue;
        const char* nm = RsName((DWORD)s);
        char nmbuf[32];
        if (!nm) { snprintf(nmbuf, sizeof(nmbuf), "(%d)", s); nm = nmbuf; }
        float pct = 100.0f * g_rsStats[s].redundantCalls / g_rsStats[s].totalCalls;
        fprintf(stderr, "  %-22s  %8d  %8d  %8d  %5.1f%%\n",
                nm,
                g_rsStats[s].totalCalls,
                g_rsStats[s].redundantCalls,
                g_rsStats[s].filteredCalls,
                pct);
        shown++;
    }

    fprintf(stderr, "\n[D3DTrace] ============================================\n");
    fprintf(stderr, "[D3DTrace] TextureStageState call counts (top 25 by redundant)\n");
    fprintf(stderr, "[D3DTrace] ============================================\n");

    int tssOrder[8 * 32];
    for (int i = 0; i < 8 * 32; i++) tssOrder[i] = i;
    std::sort(tssOrder, tssOrder + (8 * 32), [](int a, int b) {
        return g_tssStats[a].redundantCalls > g_tssStats[b].redundantCalls;
    });
    fprintf(stderr, "  %-30s  %8s  %8s  %6s\n",
            "stage/type", "total", "redund", "red%");
    shown = 0;
    for (int i = 0; i < 8 * 32 && shown < 25; i++) {
        int idx = tssOrder[i];
        if (g_tssStats[idx].totalCalls == 0) continue;
        int stage = idx / 32;
        int type  = idx % 32;
        const char* nm = TssName((DWORD)type);
        char nmbuf[32];
        if (!nm) { snprintf(nmbuf, sizeof(nmbuf), "type %d", type); nm = nmbuf; }
        char full[64];
        snprintf(full, sizeof(full), "stage %d / %s", stage, nm);
        float pct = 100.0f * g_tssStats[idx].redundantCalls / g_tssStats[idx].totalCalls;
        fprintf(stderr, "  %-30s  %8d  %8d  %5.1f%%\n",
                full,
                g_tssStats[idx].totalCalls,
                g_tssStats[idx].redundantCalls,
                pct);
        shown++;
    }

    fprintf(stderr, "[D3DTrace] ============================================\n\n");
    fflush(stderr);
}

void Theseus_D3D_ResetTrace()
{
    for (int i = 0; i < 256; i++) g_rsStats[i] = D3DStateStats{};
    for (int i = 0; i < 8 * 32; i++) g_tssStats[i] = D3DStateStats{};
    fprintf(stderr, "[D3DTrace] counters reset\n");
    fflush(stderr);
}

// XboxInitRenderState()
// Reconstructed from 5960 retail XBE @ 0x0002c660, which sets the startup
// states one at a time: z test from the z-buffer flag, solid Gouraud fill,
// z writes on, alpha test off, one/zero blending, CCW culling, LESSEQUAL,
// and so on through the list, then the motion blur factor to 1. The desktop
// leaves out EDGEANTIALIAS and MULTISAMPLEANTIALIAS: the first is an NV2A
// feature, the second is set when the GL context is made.
void XboxInitRenderState()
{
	const float fZero = 0.0f;
	const float fOne = 1.0f;
	const DWORD dwZero = *(const uint32_t*)&fZero;
	const DWORD dwOne = *(const uint32_t*)&fOne;

	const struct { D3DRENDERSTATETYPE state; DWORD value; } states[] =
	{
		{ D3DRS_ZENABLE,                g_bZBuffer ? D3DZB_TRUE : D3DZB_FALSE },
		{ D3DRS_FILLMODE,               D3DFILL_SOLID },
		{ D3DRS_SHADEMODE,              D3DSHADE_GOURAUD },
		{ D3DRS_ZWRITEENABLE,           TRUE },
		{ D3DRS_ALPHATESTENABLE,        FALSE },
		{ D3DRS_SRCBLEND,               D3DBLEND_ONE },
		{ D3DRS_DESTBLEND,              D3DBLEND_ZERO },
		{ D3DRS_CULLMODE,               D3DCULL_CCW },
		{ D3DRS_ZFUNC,                  D3DCMP_LESSEQUAL },
		{ D3DRS_ALPHAREF,               0 },
		{ D3DRS_ALPHAFUNC,              D3DCMP_ALWAYS },
		{ D3DRS_DITHERENABLE,           TRUE },
		{ D3DRS_ALPHABLENDENABLE,       TRUE },
		{ D3DRS_FOGENABLE,              FALSE },
		{ D3DRS_SPECULARENABLE,         FALSE },
		{ D3DRS_FOGCOLOR,               0 },
		{ D3DRS_FOGTABLEMODE,           D3DFOG_NONE },
		{ D3DRS_FOGDENSITY,             dwOne },
		{ D3DRS_ZBIAS,                  0 },
		{ D3DRS_RANGEFOGENABLE,         FALSE },
		{ D3DRS_STENCILENABLE,          FALSE },
		{ D3DRS_STENCILFAIL,            D3DSTENCILOP_KEEP },
		{ D3DRS_STENCILZFAIL,           D3DSTENCILOP_KEEP },
		{ D3DRS_STENCILPASS,            D3DSTENCILOP_KEEP },
		{ D3DRS_STENCILFUNC,            D3DCMP_ALWAYS },
		{ D3DRS_STENCILREF,             0 },
		{ D3DRS_STENCILMASK,            0xffffffff },
		{ D3DRS_STENCILWRITEMASK,       0xffffffff },
		{ D3DRS_WRAP0,                  0 },
		{ D3DRS_WRAP1,                  0 },
		{ D3DRS_WRAP2,                  0 },
		{ D3DRS_WRAP3,                  0 },
		{ D3DRS_LIGHTING,               TRUE },
		{ D3DRS_AMBIENT,                0 },
		{ D3DRS_COLORVERTEX,            TRUE },
		{ D3DRS_LOCALVIEWER,            TRUE },
		{ D3DRS_NORMALIZENORMALS,       FALSE },
		{ D3DRS_DIFFUSEMATERIALSOURCE,  D3DMCS_COLOR1 },
		{ D3DRS_SPECULARMATERIALSOURCE, D3DMCS_COLOR2 },
		{ D3DRS_AMBIENTMATERIALSOURCE,  D3DMCS_COLOR2 },
		{ D3DRS_EMISSIVEMATERIALSOURCE, D3DMCS_MATERIAL },
		{ D3DRS_VERTEXBLEND,            D3DVBF_DISABLE },
		{ D3DRS_POINTSIZE,              dwOne },
		{ D3DRS_POINTSIZE_MIN,          dwOne },
		{ D3DRS_POINTSPRITEENABLE,      FALSE },
		{ D3DRS_POINTSCALEENABLE,       FALSE },
		{ D3DRS_POINTSCALE_A,           dwOne },
		{ D3DRS_POINTSCALE_B,           dwZero },
		{ D3DRS_POINTSCALE_C,           dwZero },
		{ D3DRS_MULTISAMPLEMASK,        0xffffffff },
		{ D3DRS_COLORWRITEENABLE,       D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA },
	};

	for (int i = 0; i < (int)countof(states); i++)
		TheseusSetRenderState(states[i].state, states[i].value);

	g_levelBlurAmount = 1.0f;
}

// Scissor rectangle covering back buffer iBuffer (-1 is the front buffer).
static void FullBufferRect(int iBuffer, D3DRECT* pRect)
{
	LPDIRECT3DSURFACE8 pSurface;
	D3DSURFACE_DESC desc;
	g_pD3DDev->GetBackBuffer(iBuffer, D3DBACKBUFFER_TYPE_MONO, &pSurface);
	pSurface->GetDesc(&desc);
	pSurface->Release();

	pRect->x1 = 0;
	pRect->y1 = 0;
	pRect->x2 = desc.Width;
	pRect->y2 = desc.Height;
}

// InitD3D()
// 5960 @ 0x0002ccf0 sizes the back buffer by AV pack, asks for 4x gaussian
// supersampling and vsync, creates the device, records the front and back
// buffer rectangles and sets the startup states. The desktop sizes the back
// buffer to the window (1280x720 if it can't tell), presents immediately,
// asks for the 2x vertical supersample mode, and switches the first two
// texture stages to anisotropic filtering.
bool InitD3D()
{
	g_pD3D = Direct3DCreate8(D3D_SDK_VERSION);
	if (g_pD3D == NULL)
	{
		Alert("Cannot initialize Direct3D! (D3D_SDK_VERSION=%d)", D3D_SDK_VERSION);
		return false;
	}

	int drawW = 0, drawH = 0;
	Plat_GetDrawableSize(&drawW, &drawH);

	memset(&g_pp, 0, sizeof (g_pp));
	g_pp.BackBufferWidth = drawW > 0 ? drawW : 1280;
	g_pp.BackBufferHeight = drawH > 0 ? drawH : 720;
	g_pp.BackBufferFormat = D3DFMT_X8R8G8B8;
	g_pp.BackBufferCount = 1;
	g_pp.MultiSampleType = D3DMULTISAMPLE_2_SAMPLES_SUPERSAMPLE_VERTICAL_LINEAR;
	g_pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	g_pp.hDeviceWindow = NULL;
	g_pp.Windowed = FALSE;
	g_pp.EnableAutoDepthStencil = g_bZBuffer;
	if (g_bZBuffer)
		g_pp.AutoDepthStencilFormat = D3DFMT_D24S8;
	g_pp.Flags = D3DPRESENTFLAG_PROGRESSIVE | D3DPRESENTFLAG_WIDESCREEN;
	g_pp.FullScreen_RefreshRateInHz = D3DPRESENT_RATE_DEFAULT;
	g_pp.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

	g_bStretchWidescreen = false;
	g_nViewWidth = (float)g_pp.BackBufferWidth;
	g_nViewHeight = (float)g_pp.BackBufferHeight;

	HRESULT hr = g_pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, g_pp.hDeviceWindow,
		D3DCREATE_HARDWARE_VERTEXPROCESSING, &g_pp, &g_pD3DDev);
	if (FAILED(hr))
	{
		LogComError(hr, "InitD3D");
		return false;
	}

	FullBufferRect(-1, &g_clipRect);
	FullBufferRect(0, &g_clipRect2x);

	XboxInitRenderState();

	TheseusSetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	TheseusSetRenderState(D3DRS_AMBIENT, 0xffffffff);

	for (DWORD stage = 0; stage < 2; stage++)
	{
		TheseusSetTextureStageState(stage, D3DTSS_MINFILTER, D3DTEXF_ANISOTROPIC);
		TheseusSetTextureStageState(stage, D3DTSS_MAGFILTER, D3DTEXF_ANISOTROPIC);
		if (stage == 0)
			TheseusSetTextureStageState(stage, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
	}

	return true;
}

// ReleaseD3D()
// Frees the world matrix stack and the Direct3D object.
void ReleaseD3D()
{
	RELEASENULL(g_worldStack);
	RELEASENULL(g_pD3D);
}

// ============================================================================
// Drawing
// ============================================================================

struct COLORVERTEX
{
	float dvX, dvY, dvZ;
	uint32_t color;
};

// Rebuild the projection when something marked it dirty: field of view
// from the bound viewpoint (90 degrees without one), near at half the
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
		VERIFYHR(TheseusGetD3DDev()->CreateVertexBuffer(4 * sizeof (COLORVERTEX), D3DUSAGE_WRITEONLY, D3DFVF_XYZ | D3DFVF_DIFFUSE, D3DPOOL_MANAGED, &s_pQuad));

		COLORVERTEX* verts;
		VERIFYHR(s_pQuad->Lock(0, 4 * sizeof (COLORVERTEX), (uint8_t**)&verts, 0));
		const float halfW = g_nViewWidth / 2.0f;
		const float halfH = g_nViewHeight / 2.0f;
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

	D3DXMatrixOrthoLH(&matOrtho, g_nViewWidth, g_nViewHeight, -10000.0f, 10000.0f);
	TheseusSetTransform(D3DTS_PROJECTION, &matOrtho);
	TheseusSetTransform(D3DTS_WORLD, &g_matIdentity);
	TheseusSetTransform(D3DTS_VIEW, &g_matIdentity);

	// Stage 0 passes the vertex color and alpha straight through, stage 1 is off.
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

	TheseusSetStreamSource(0, s_pQuad, sizeof (COLORVERTEX));
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
	D3DCOLORVALUE ambient;
	ambient.r = ambient.g = ambient.b = 0.0f;

	if (g_pNavigationInfo == NULL || g_pNavigationInfo->m_headlight)
	{
		TheseusSetTransform(D3DTS_WORLD, &g_matIdentity);

		D3DLIGHT8 light;
		memset(&light, 0, sizeof (light));
		light.Type = D3DLIGHT_DIRECTIONAL;
		light.Diffuse.r = light.Diffuse.g = light.Diffuse.b = 0.5f;
		light.Specular.r = light.Specular.g = light.Specular.b = 0.75f;
		light.Ambient.r = light.Ambient.g = light.Ambient.b = 0.2f;

		ambient.r += light.Ambient.r;
		ambient.g += light.Ambient.g;
		ambient.b += light.Ambient.b;

		D3DXVECTOR3 dir(0.0f, 0.0f, -1.0f);
		D3DXVec3TransformNormal(&dir, &dir, &g_cameraWorld);
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
// the last frame instead. Then a default material (grey, made once), the
// scene drawn from an identity world, and Present. Theseus adds the
// lights, wireframe, its frame profiler and the active keyboard reset.
void Draw()
{
	Frame_PhaseBegin(FP_SCENE_SETUP);

	UpdateProjection();

	g_uMesh = 0;
	g_uMeshRef = 0;

	TheseusBeginScene();
	TheseusSetRenderState(D3DRS_FILLMODE, g_bWireframe ? D3DFILL_WIREFRAME : D3DFILL_SOLID);

	// A bigger factor means less blur.
	const float blurAlpha = g_levelBlurAmount;

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
		D3DCOLORVALUE grey;
		grey.r = grey.g = grey.b = 0.5f;
		grey.a = 1.0f;
		s_material.Diffuse = s_material.Ambient = s_material.Specular = s_material.Emissive = grey;
		s_material.Power = 0.0f;
	}
	TheseusSetMaterial(&s_material);

	Frame_PhaseEnd(FP_SCENE_SETUP);

	Frame_PhaseBegin(FP_SCENE_TREE);
	// Visible keyboards claim this again while they render.
	extern void Keyboard_ClearActive();
	Keyboard_ClearActive();
	TheseusPushWorld();
	TheseusIdentityWorld();
	TheseusUpdateWorld();
	g_pObject->Render();
	TheseusPopWorld();
	Frame_PhaseEnd(FP_SCENE_TREE);

	Frame_PhaseBegin(FP_SCENE_END);
	TheseusEndScene();
	Frame_PhaseEnd(FP_SCENE_END);

	TheseusSetRenderState(D3DRS_SWATHWIDTH, D3DSWATH_OFF);
	TheseusPresent();
	TheseusSetRenderState(D3DRS_SWATHWIDTH, D3DSWATH_128);

	g_dwFrameTick = GetTickCount();
}

// ============================================================================
// Application Lifecycle
// ============================================================================

// InitApp()
// 5960 @ 0x0002d7e0: CD-ROM and disc type, platform init, version lock and
// the online redirect, InitD3D, the default XIP, Class_Init, materials, then
// load and create the startup scene and call its initialize. The desktop
// keeps that order without the online steps, starts SDL audio after the
// device and preloads the skin textures at the end.
bool InitApp()
{
	Memory_Init();
	DashInit();
	Debug_Init();
	DiscDrive_Init();

	if (!InitD3D())
		return false;
	InitAudio();
	LoadXIP("Q:\\Xips\\default.xip", true);

	Class_Init();
	TitleArray_Init();
	Material_Init(false);

	if (g_pClass == NULL)
		g_pClass = new CClass;

	char szStartup[MAX_PATH];
	szStartup[0] = 0;
	GetStartupClassFile(szStartup);
	if (!g_pClass->Load(szStartup))
	{
		// A parse error has already said what went wrong.
		extern bool g_bXapSyntaxError;
		if (!g_bXapSyntaxError)
			Alert("%s\n\nCannot open file.", szStartup);
		return false;
	}

	g_pObject = (CInstance*)g_pClass->CreateNode();
	VERIFYHR(D3DXCreateMatrixStack(0, &g_worldStack));

	CallFunction(g_pObject, "initialize");
	PreloadSkinTextures();

	fprintf(stdout, "[InitApp] Done!\n");
	fflush(stdout);
	return true;
}

// CleanupApp()
// 5960 @ 0x0002d930 ends the XOnline session, frees the matrix stack, the
// scene and its class, then Class_Exit, the XIP sections, Text_Exit, sound
// and the app dir. The desktop releases D3D first, drops Locale too, and
// shuts down CD audio, SDL audio, Plex and Jellyfin.
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

	extern void Plex_Shutdown();
	extern void Jellyfin_Shutdown();
	CdAudio_Shutdown();
	DashAudio_Shutdown();
	Plex_Shutdown();
	Jellyfin_Shutdown();

	delete[] g_szAppDir;
	g_szAppDir = NULL;

	Debug_Exit();
}

// Advance()
// From the 5960 frame loop @ 0x0002da00: the step is the change in
// the clock since last frame (one millisecond's worth if it didn't move),
// then the camera, every lerper and the scene advance. Theseus clocks from
// launch on a high resolution counter (scripts time things like the screen
// saver against 0, so an uptime base would fire them on the first frame),
// and feeds its background loader.
void Advance()
{
	static const Uint64 s_clockStart = SDL_GetPerformanceCounter();
	XTIME now = (double)(SDL_GetPerformanceCounter() - s_clockStart) / (double)SDL_GetPerformanceFrequency();
	float nDelta = (float)(now - g_now);
	if (nDelta == 0.0f)
		nDelta = 0.001f;
	g_now = now;
	g_dwFrameTick = GetTickCount();

	theCamera.Advance(nDelta);
	CLerper::AdvanceAll();
	g_pObject->Advance(nDelta);

	AssetLoader_Pump();
	Debug_Frame();
}

// ============================================================================
// Startup
// ============================================================================

static char* NewCopy(const char* sz)
{
	char* copy = new char[strlen(sz) + 1];
	strcpy(copy, sz);
	return copy;
}

// GetStartupClassFile(out)
// 5960 @ 0x0002dbc0 starts from y:\default.xap with y:/default.xap as the
// current dir and keeps the app dir. Theseus runs from Q:, so the scene is
// Q:\Xips\default.xap, and it also records its font and XIP dirs.
void GetStartupClassFile(char *szFileToLoad)
{
	strcpy(szFileToLoad, "Q:\\Xips\\default.xap");
	strcpy(g_szCurDir, "Q:/Xips/default.xap");

	g_szAppDir = NewCopy("Q:\\");
	g_sFontDir = NewCopy("Q:\\Fonts\\");
	g_sXipDir = NewCopy("Q:\\Xips\\");
}
