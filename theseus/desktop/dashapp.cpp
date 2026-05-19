// dashapp.cpp: desktop dashboard globals and main loop. Owns the
// D3D8 stub device, the Advance / Draw cycle, audio init, the scene
// graph root, and startup class file loading. Desktop counterpart
// to xbox/main.cpp.

#include "std.h"
#include "dashapp.h"
#include "file_util.h"
#include "xip.h"
#include "node.h"
#include "screen.h"
#include "runner.h"
#include "lerper.h"
#include "camera.h"
#include "scene_groups.h"
#include "audio_sdl.h"
#include "cdaudio.h"
#include "settingsfile.h"

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

extern void BackgroundLoader_Frame();

extern void Text_Exit();

extern void Locale_Exit();

extern void TitleArray_Init();

extern void Material_Init(bool bReloadSkinXBX);
extern float g_nEffectAlpha;

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
int               g_nodeVisitsThisFrame = 0;
int               g_nodeSkipsThisFrame = 0;
char*             g_szAppTitle = NULL;

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

unsigned int g_uMesh;
unsigned int g_uMeshRef;

extern "C" void Alert(const char *szMsg, ...)
{
	va_list args;
	va_start(args, szMsg);

	char szBuffer[512];
	_vsntprintf(szBuffer, countof(szBuffer), szMsg, args);
	Trace("\007%s\n", szBuffer);
	va_end(args);
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

float g_transitionMotionBlur;

// =========================================================================
// D3D call-trace storage + dump (Sweep 2 of project_d3d_call_audit.md)
// =========================================================================

D3DStateStats g_rsStats[256] = {};
D3DStateStats g_tssStats[8 * 32] = {};

// Wrapper-level state caches (Phase 1 outcome of the Sweep 2 trace).
// See TheseusSet* / TheseusGet* in theseus/shared/theseus.h.
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

void XboxInitRenderState()
{
	float fZero = 0.0f;
	float fOne = 1.0f;

	TheseusSetRenderState(D3DRS_ZENABLE, g_bZBuffer ? D3DZB_TRUE : D3DZB_FALSE);
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
	TheseusSetRenderState(D3DRS_FOGDENSITY, *(uint32_t*)&fOne);
	// EDGEANTIALIAS stripped: Xbox NV2A feature, shim no-op on GL.
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
	TheseusSetRenderState(D3DRS_POINTSIZE, *(uint32_t*)&fOne);
	TheseusSetRenderState(D3DRS_POINTSIZE_MIN, *(uint32_t*)&fOne);
	TheseusSetRenderState(D3DRS_POINTSPRITEENABLE, FALSE);
	TheseusSetRenderState(D3DRS_POINTSCALEENABLE, FALSE);
	TheseusSetRenderState(D3DRS_POINTSCALE_A, *(uint32_t*)&fOne);
	TheseusSetRenderState(D3DRS_POINTSCALE_B, *(uint32_t*)&fZero);
	TheseusSetRenderState(D3DRS_POINTSCALE_C, *(uint32_t*)&fZero);
	// MULTISAMPLEANTIALIAS stripped: MSAA on desktop is set at GL
	// context creation, not via render-state at init.
	TheseusSetRenderState(D3DRS_MULTISAMPLEMASK, 0xffffffff);
	TheseusSetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA);

	g_transitionMotionBlur = 1.0f;
}

bool InitD3D()
{
	g_pD3D = Direct3DCreate8(D3D_SDK_VERSION);
	if (g_pD3D == NULL)
	{
		Alert("Cannot initialize Direct3D! (D3D_SDK_VERSION=%d)", D3D_SDK_VERSION);
		return false;
	}

	D3DDEVTYPE devtype = D3DDEVTYPE_HAL;

	memset(&g_pp, 0, sizeof(g_pp));
	g_pp.BackBufferCount = 1;
	g_pp.FullScreen_RefreshRateInHz = D3DPRESENT_RATE_DEFAULT;

	g_pp.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
	g_pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	g_pp.MultiSampleType = D3DMULTISAMPLE_2_SAMPLES_SUPERSAMPLE_VERTICAL_LINEAR;
	g_pp.EnableAutoDepthStencil = g_bZBuffer;
	if (g_bZBuffer)
		g_pp.AutoDepthStencilFormat = D3DFMT_D24S8;

	uint32_t dwBehavior = D3DCREATE_HARDWARE_VERTEXPROCESSING;

	// Desktop: resolution follows SDL window size, updated on resize
	g_pp.BackBufferWidth = 1280;
	g_pp.BackBufferHeight = 720;
	g_pp.Flags = D3DPRESENTFLAG_PROGRESSIVE | D3DPRESENTFLAG_WIDESCREEN;
	g_bStretchWidescreen = false;

	g_pp.BackBufferFormat = D3DFMT_X8R8G8B8;

	g_pp.hDeviceWindow = NULL;
	g_pp.Windowed = FALSE;

	g_nViewWidth = (float)g_pp.BackBufferWidth;
	g_nViewHeight = (float)g_pp.BackBufferHeight;

	HRESULT hr = g_pD3D->CreateDevice(D3DADAPTER_DEFAULT, devtype, g_pp.hDeviceWindow, dwBehavior, &g_pp, &g_pD3DDev);
	if (FAILED(hr))
	{
		LogComError(hr, "InitD3D");
		return false;
	}
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

// ============================================================================
// Drawing
// ============================================================================

struct COLORVERTEX
{
	float dvX, dvY, dvZ;
	uint32_t color;
};

void Draw()
{
	// Setup projection transform
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

	const float blurAlpha = g_transitionMotionBlur;

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
		TheseusClear(color);

		if (bBackdrop)
			g_pBackground->RenderBackdrop();
	}
	else
	{
		// Motion blur overlay quad
		static LPDIRECT3DVERTEXBUFFER8 m_pVB;
		if (m_pVB == NULL)
		{
			VERIFYHR(TheseusGetD3DDev()->CreateVertexBuffer(4 * sizeof(COLORVERTEX), D3DUSAGE_WRITEONLY, D3DFVF_XYZ | D3DFVF_DIFFUSE, D3DPOOL_MANAGED, &m_pVB));

			COLORVERTEX *verts;
			VERIFYHR(m_pVB->Lock(0, 4 * sizeof(COLORVERTEX), (uint8_t **)&verts, 0));

			verts[0].dvX = (float)g_nViewWidth / 2.0f;
			verts[0].dvY = -(float)g_nViewHeight / 2.0f;
			verts[0].dvZ = 0.0f;
			verts[0].color = D3DCOLOR_COLORVALUE(0.0f, 0.0f, 0.0f, blurAlpha);

			verts[1].dvX = -(float)g_nViewWidth / 2.0f;
			verts[1].dvY = -(float)g_nViewHeight / 2.0f;
			verts[1].dvZ = 0.0f;
			verts[1].color = D3DCOLOR_COLORVALUE(0.0f, 0.0f, 0.0f, blurAlpha);

			verts[2].dvX = (float)g_nViewWidth / 2.0f;
			verts[2].dvY = (float)g_nViewHeight / 2.0f;
			verts[2].dvZ = 0.0f;
			verts[2].color = D3DCOLOR_COLORVALUE(0.0f, 0.0f, 0.0f, blurAlpha);

			verts[3].dvX = -(float)g_nViewWidth / 2.0f;
			verts[3].dvY = (float)g_nViewHeight / 2.0f;
			verts[3].dvZ = 0.0f;
			verts[3].color = D3DCOLOR_COLORVALUE(0.0f, 0.0f, 0.0f, blurAlpha);

			VERIFYHR(m_pVB->Unlock());
		}

		D3DXMATRIX matProjection, matProjectionSave, matWorldSave, matViewSave;

		TheseusGetTransform(D3DTS_PROJECTION, &matProjectionSave);
		TheseusGetTransform(D3DTS_WORLD, &matWorldSave);
		TheseusGetTransform(D3DTS_VIEW, &matViewSave);

		D3DXMatrixOrthoLH(&matProjection, g_nViewWidth, g_nViewHeight, -10000.0f, 10000.0f);
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

	// Setup lights
	{
		static int nLastLight = -1;
		int nLight = 0;
		D3DCOLORVALUE ambient;
		ambient.r = 0.0f;
		ambient.g = 0.0f;
		ambient.b = 0.0f;

		// Headlight
		if (g_pNavigationInfo == NULL || g_pNavigationInfo->m_headlight)
		{
			TheseusSetTransform(D3DTS_WORLD, &g_matIdentity);

			D3DLIGHT8 d3dLight;
			memset(&d3dLight, 0, sizeof(d3dLight));

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

		// Turn off lights we are not using
		for (int i = nLight; i < nLastLight; i += 1)
			TheseusLightEnable(i, false);

		TheseusSetRenderState(D3DRS_LIGHTING, nLight > 0);
		nLastLight = nLight;
	}

	// Default material
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

	// Draw the world
	{
		// Clear active-keyboard tracker before scene render; visible CKeyboards
		// will re-set it via their Render() override. Hidden ones never run, so
		// the global ends up NULL when no keyboard is on screen.
		extern void Keyboard_ClearActive();
		Keyboard_ClearActive();

		TheseusPushWorld();
		TheseusIdentityWorld();
		TheseusUpdateWorld();
		g_pObject->Render();
		TheseusPopWorld();
	}

	TheseusEndScene();
	TheseusSetRenderState(D3DRS_SWATHWIDTH, D3DSWATH_OFF);

	TheseusPresent();
	TheseusSetRenderState(D3DRS_SWATHWIDTH, D3DSWATH_128);

	g_dwFrameTick = GetTickCount();
}

// ============================================================================
// Application Lifecycle
// ============================================================================

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

	// Load the startup class file (default.xap)
	{
		char szFileToLoad[MAX_PATH];
		szFileToLoad[0] = 0;

		if (!g_pClass)
			g_pClass = new CClass;

		GetStartupClassFile(szFileToLoad);
		ASSERT(szFileToLoad[0] != 0);

		if (!g_pClass->Load(szFileToLoad))
		{
			extern bool g_bParseError;
			if (!g_bParseError)
				Alert("%s\n\nCannot open file.", szFileToLoad);
			return false;
		}
	}

	g_pObject = (CInstance *)g_pClass->CreateNode();
	ASSERT(g_pObject != NULL);

	VERIFYHR(D3DXCreateMatrixStack(0, &g_worldStack));
	ASSERT(g_worldStack != NULL);

	CallFunction(g_pObject, "initialize");

	PreloadSkinTextures();
	fprintf(stdout, "[InitApp] Done!\n"); fflush(stdout);
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

	CdAudio_Shutdown();
	DashAudio_Shutdown();

	extern void Plex_Shutdown();
	Plex_Shutdown();

	delete[] g_szAppDir;
	g_szAppDir = NULL;

	Debug_Exit();
}

void Advance()
{
	XTIME now = (float)GetTickCount() / 1000.0f;
	float nDelta = (float)(now - g_now);
	if (nDelta == 0.0f)
		nDelta = 0.001f;
	g_now = now;
	g_dwFrameTick = GetTickCount();

	ASSERT(g_nEffectAlpha == 1.0f);

	theCamera.Advance(nDelta);

	CLerper::AdvanceAll();
	g_pObject->Advance(nDelta);

	BackgroundLoader_Frame();

	Debug_Frame();
}

// ============================================================================
// Startup
// ============================================================================

void GetStartupClassFile(char *szFileToLoad)
{
	char szAppDir[MAX_PATH];
	char szFontDir[MAX_PATH];
	char szXipDir[MAX_PATH];
	strcpy(szFileToLoad, "Q:\\Xips\\default.xap");
	strcpy(szAppDir, "Q:\\");
	strcpy(szFontDir, "Q:\\Fonts\\");
	strcpy(szXipDir, "Q:\\Xips\\");
	strcpy(g_szCurDir, "Q:/Xips/default.xap");

	g_szAppDir = new char[strlen(szAppDir) + 1];
	strcpy(g_szAppDir, szAppDir);

	g_sFontDir = new char[strlen(szFontDir) + 1];
	strcpy(g_sFontDir, szFontDir);

	g_sXipDir = new char[strlen(szXipDir) + 1];
	strcpy(g_sXipDir, szXipDir);
}
