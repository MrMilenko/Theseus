// theseus.h: top-level dashboard declarations. Globals (D3D device,
// view dims, render-state cache, scene root, bound nodes), the
// Theseus* D3D wrappers, world matrix stack helpers, and a few math
// inlines. Effectively the dashboard's "everywhere" header.

#ifndef THESEUS_H
#define THESEUS_H

#ifndef _XBOX
#include <errno.h>
#include <sys/stat.h>
#endif

// On Xbox, settingsfile.h and file_util.h provide CSettingsFile, CleanFilePath,
// and FSCHAR. On desktop those live in dashapp.h/fileutil.h and the Win32 types
// come in via std.h->sdl_platform.h, so we only pull shared/ versions for Xbox.
#ifdef _XBOX
#include "settingsfile.h"
#include "file_util.h"
#else
class CSettingsFile;
extern void CleanFilePath(char* szPath, const char* szSrcPath);
extern void UpdateCurDirFromFile(const char* szURL);
#endif

// Desktop's node.h NODE_FUN_* macros reference _NFC, while Theseus source
// files use _FND_CLASS. Alias them so files that set _FND_CLASS compile
// under desktop's node.h. Temporary until desktop/node.h adopts the same
// convention or the files drop into engine/shared/render wholesale.
#ifdef _DESKTOP
#define _NFC _FND_CLASS
#endif
// Time values are stored as double for fractional precision over long uptimes.
typedef double XTIME;

#define MAX_BLOCKS_TO_SHOW 50000

#define CNode CTheseusNode // avoid name collision with D3DX::CNode
#define classCNode classCTheseusNode // avoid name collision with D3DX::classCNode
class CMaxMaterial;  // Forward declare since it's referenced in Setup()

// Material descriptor table entry. Each subclass in render/materials.cpp
// represents one of the dashboard's hand-rolled material binders
// (solid colour, falloff, modulate-texture, etc); CMatInfo holds the
// name string the .max exporter wrote into the material's user-data
// block, plus a bitfield of MATINFO_* flags that select between
// alpha modes and similar variants.
class CMatInfo
{
public:
    CMatInfo(const TCHAR *szName, DWORD dwFlags = 0);

    virtual bool Setup(CMaxMaterial *pMaxMat);

    const TCHAR *m_name;
    DWORD m_flags;

    // Frame number of the most recent CMaxMaterial::Render() that bound this
    // CMatInfo. -1 = never used. Read by the Skin Editor to highlight which
    // materials are currently on screen.
    int m_lastUsedFrame = -1;
};

extern int g_nMatInfoCount;
extern CMatInfo* g_rgMatInfo[240];
extern TCHAR g_szLastLoadedSkin[MAX_PATH];

// Color introspection / live edit for the Skin Editor. Returns 1 or 2 colors
// per material (or 0 for types without editable colors). D3DCOLOR is ARGB.
int      MatInfo_ColorCount(CMatInfo* p);
DWORD    MatInfo_GetColor(CMatInfo* p, int idx);
void     MatInfo_SetColor(CMatInfo* p, int idx, DWORD c);

class CObject;
class CClass;
class CInstance;
class CNode;
class CNodeArray;
class CScreen;
class CViewpoint;
class CNavigationInfo;
class CNavigator;
class CBackground;


// Globals
//
// Declared on both platforms. Xbox defines them in xbox/main.cpp.
// Desktop defines them in desktop/dashapp.cpp alongside the CDashApp
// singleton during the port; the class members and globals are kept
// in sync during init. Once CDashApp is gone the sync disappears and
// the globals stand alone.

// D3D
extern LPDIRECT3DDEVICE8 g_pD3DDev;
extern LPDIRECT3D8 g_pD3D;
extern D3DPRESENT_PARAMETERS g_pp;

// View
extern float g_nViewWidth;
extern float g_nViewHeight;
extern bool g_bStretchWidescreen;

// Render state
extern bool g_bZBuffer;
extern bool g_bProjectionDirty;
extern ID3DXMatrixStack* g_worldStack;

// Timing
extern DWORD g_dwStartTick;
extern DWORD g_dwFrameTick;
extern XTIME g_now;

// Scene root
extern CClass* g_pClass;
extern CInstance* g_pObject;

// Bound nodes
extern CScreen* g_pScreen;
extern CViewpoint* g_pViewpoint;
extern CNavigationInfo* g_pNavigationInfo;
extern CBackground* g_pBackground;

// Paths
extern TCHAR* g_szAppDir;
extern TCHAR* g_sFontDir;
extern TCHAR* g_sXipDir;
extern TCHAR* g_sSkinDir;
extern CSettingsFile* g_pSkinSettings;

// Partition flags
extern bool g_fExists;
extern bool g_gExists;
extern bool g_hExists;
extern bool g_iExists;
extern bool g_jExists;
extern bool g_kExists;
extern bool g_lExists;
extern bool g_mExists;
extern bool g_nExists;
extern bool g_rExists;
extern bool g_sExists;

// Launch data
extern bool g_bHasLaunchData;
extern DWORD g_dwTitleID;
extern DWORD g_dwLaunchReason;
extern DWORD g_dwLaunchContext;
extern DWORD g_dwLaunchParameter1;
extern DWORD g_dwLaunchParameter2;

// Thread
extern DWORD g_dwMainThreadId;

// Core functions
bool InitApp();
void CleanupApp();
void Advance();
void Draw();
bool InitD3D();
void ReleaseD3D();
HRESULT InitAudio();
void GetStartupClassFile(TCHAR* szFileToLoad);



inline XTIME TheseusGetNow() { return g_now; }
inline float TheseusGetViewWidth() { return g_nViewWidth; }
inline float TheseusGetViewHeight() { return g_nViewHeight; }
inline bool TheseusGetStretchWidescreen() { return g_bStretchWidescreen; }
inline void TheseusSetProjectionDirty() { g_bProjectionDirty = true; }
inline CBackground* TheseusGetBackground() { return g_pBackground; }
inline void TheseusSetBackground(CBackground* p) { g_pBackground = p; }
inline CViewpoint* TheseusGetViewpoint() { return g_pViewpoint; }
inline void TheseusSetViewpoint(CViewpoint* p) { g_pViewpoint = p; }
inline CNavigationInfo* TheseusGetNavigationInfo() { return g_pNavigationInfo; }
inline void TheseusSetNavigationInfo(CNavigationInfo* p) { g_pNavigationInfo = p; }
inline const TCHAR* TheseusGetAppDir() { return g_szAppDir; }
inline const TCHAR* TheseusGetXipDir() { return g_sXipDir; }
inline const TCHAR* TheseusGetSkinDir() { return g_sSkinDir; }
inline const TCHAR* TheseusGetFontDir() { return g_sFontDir; }
inline bool TheseusGetZBuffer() { return g_bZBuffer; }
inline bool TheseusHasLaunchData() { return g_bHasLaunchData; }
inline DWORD TheseusGetTitleID() { return g_dwTitleID; }
inline DWORD TheseusGetLaunchReason() { return g_dwLaunchReason; }
inline DWORD TheseusGetLaunchParameter1() { return g_dwLaunchParameter1; }
inline CSettingsFile* TheseusGetSkinSettings() { return g_pSkinSettings; }
inline CScreen* TheseusGetScreen() { return g_pScreen; }
inline void TheseusSetScreen(CScreen* p) { g_pScreen = p; }


// Desktop always provides real implementations in desktop/debug.cpp.
// Xbox debug build has them in engine/debug.cpp; Xbox retail gets the
// inline no-op stubs so unused error logging links cleanly.
#if defined(_DESKTOP) || defined(_DEBUG)
void TheseusGetErrorString(HRESULT hr, TCHAR* szBuf, int cchBuf);
const TCHAR* TheseusGetErrorString(HRESULT hr);
void LogComError(HRESULT hr, const char* szFunc = NULL);
void LogError(const char* szFunc);
#else
inline void LogComError(HRESULT hr, const char* szFunc = NULL) {}
inline void LogError(const char* szFunc) {}
#endif


void* TheseusAllocMemory(int nBytes);
void TheseusFreeMemory(void* pv);

#ifdef _XBOX
void* TheseusD3D_AllocContiguousMemory(DWORD Size, DWORD Alignment);
void* TheseusD3D_AllocNoncontiguousMemory(DWORD Size);
#endif


// Theseus Direct 3D Device Interfaces

#ifdef _XBOX // cache eliminates GetRenderState round-trips on Xbox
extern DWORD theseus_rgdwRenderStateCache [256];
extern bool theseus_rgbRenderStateCache [256];
#endif

inline LPDIRECT3DDEVICE8 TheseusGetD3DDev()
{
	return g_pD3DDev;
}

#ifndef _XBOX
// Desktop-side wrapper caches. Sweep 2 trace in
// project_d3d_call_audit.md showed ~2.5M shim dispatches per session
// where the value was identical to the previous call. Short-circuit
// those at the wrapper layer, before the virtual call into the shim.
//
// 256 covers all D3DRS_* in use; valid[] BSS-zeroed so first call for
// any state always reaches the shim.
extern DWORD theseus_desktop_rs_cache[256];
extern bool  theseus_desktop_rs_valid[256];
// TSS cache: 8 stages * 32 state types.
extern DWORD theseus_desktop_tss_cache[8 * 32];
extern bool  theseus_desktop_tss_valid[8 * 32];
// Texture cache: 8 stages of pointer compare.
extern LPDIRECT3DTEXTURE8 theseus_desktop_tex_cache[8];
extern bool theseus_desktop_tex_valid[8];
// Material cache: single struct (D3DMATERIAL8 is 68 bytes).
extern D3DMATERIAL8 theseus_desktop_mat_cache;
extern bool theseus_desktop_mat_valid;
// Transform cache: 4 transform types (WORLD, VIEW, PROJECTION, TEXTURE0)
// indexed by D3DTS_WORLD etc. 256 is the practical upper bound.
extern D3DMATRIX theseus_desktop_xform_cache[256];
extern bool theseus_desktop_xform_valid[256];
// Per-draw wrappers: vertex shader handle, stream source per stream, indices.
extern DWORD theseus_desktop_vs_cache;
extern bool theseus_desktop_vs_valid;
extern IDirect3DVertexBuffer8* theseus_desktop_vb_cache[4];
extern UINT theseus_desktop_vb_stride_cache[4];
extern bool theseus_desktop_vb_valid[4];
extern IDirect3DIndexBuffer8* theseus_desktop_ib_cache;
extern UINT theseus_desktop_ib_base_cache;
extern bool theseus_desktop_ib_valid;

// Sweep 2 trace counters for the additional wrappers, sharing the
// D3DStateStats struct from d3d8_sdl.h.
struct D3DStateStats;
extern D3DStateStats g_texStats[8];
extern D3DStateStats g_matStats;
extern D3DStateStats g_xformStats[256];

// Reset every wrapper-level cache's validity flag. Call when foreign
// code (libmpv, MSAA reset, GL context recreate) has invalidated
// downstream state. otherwise the wrappers above short-circuit
// before reaching the shim and the next "set" sees no actual call,
// leaving the shim with stale state. This is the pair to the shim's
// InvalidateStateCache() (which clears the SHIM cache); both layers
// must be invalidated together.
inline void TheseusInvalidateWrapperCaches()
{
	memset(theseus_desktop_rs_valid,    0, sizeof(theseus_desktop_rs_valid));
	memset(theseus_desktop_tss_valid,   0, sizeof(theseus_desktop_tss_valid));
	memset(theseus_desktop_tex_valid,   0, sizeof(theseus_desktop_tex_valid));
	theseus_desktop_mat_valid = false;
	memset(theseus_desktop_xform_valid, 0, sizeof(theseus_desktop_xform_valid));
	theseus_desktop_vs_valid = false;
	memset(theseus_desktop_vb_valid,    0, sizeof(theseus_desktop_vb_valid));
	theseus_desktop_ib_valid = false;
}
#endif

inline void TheseusSetRenderState(D3DRENDERSTATETYPE dwRenderStateType, DWORD dwRenderState)
{
#ifdef _XBOX // cache eliminates GetRenderState round-trips on Xbox
	ASSERT((UINT)dwRenderStateType < countof (theseus_rgdwRenderStateCache));
	theseus_rgdwRenderStateCache[(UINT)dwRenderStateType] = dwRenderState;
	theseus_rgbRenderStateCache[(UINT)dwRenderStateType] = true;
#else
	// Wrapper-level dedup. Skip the virtual call if value unchanged.
	if ((UINT)dwRenderStateType < 256) {
		if (theseus_desktop_rs_valid[dwRenderStateType] &&
		    theseus_desktop_rs_cache[dwRenderStateType] == dwRenderState)
			return;
		theseus_desktop_rs_cache[dwRenderStateType] = dwRenderState;
		theseus_desktop_rs_valid[dwRenderStateType] = true;
	}
#endif

	VERIFYHR(TheseusGetD3DDev()->SetRenderState(dwRenderStateType, dwRenderState));
}

inline void TheseusGetRenderState(D3DRENDERSTATETYPE dwRenderStateType, LPDWORD lpdwRenderState)
{
#ifdef _XBOX // cache eliminates GetRenderState round-trips on Xbox
	ASSERT((UINT)dwRenderStateType < countof (theseus_rgdwRenderStateCache));
	ASSERT(theseus_rgbRenderStateCache[(UINT)dwRenderStateType]); // state must be written before reading
	*lpdwRenderState = theseus_rgdwRenderStateCache[(UINT)dwRenderStateType];
#else
	// Serve from the desktop wrapper cache when we've seen this state
	// before. Saves the virtual call into the shim's switch.
	if ((UINT)dwRenderStateType < 256 && theseus_desktop_rs_valid[dwRenderStateType]) {
		*lpdwRenderState = theseus_desktop_rs_cache[dwRenderStateType];
		return;
	}
	VERIFYHR(TheseusGetD3DDev()->GetRenderState(dwRenderStateType, lpdwRenderState));
#endif
}

inline void TheseusSetTextureStageState(DWORD dwStage, D3DTEXTURESTAGESTATETYPE dwState, DWORD dwValue)
{
#ifndef _XBOX
	// Wrapper-level dedup for the per-stage texture state cluster.
	// Sweep 2 trace showed this is where most of the redundancy lives
	// (>388K wasted calls per session on COLOROP / ALPHAOP alone).
	if (dwStage < 8 && (DWORD)dwState < 32) {
		int idx = (int)dwStage * 32 + (int)dwState;
		if (theseus_desktop_tss_valid[idx] &&
		    theseus_desktop_tss_cache[idx] == dwValue)
			return;
		theseus_desktop_tss_cache[idx] = dwValue;
		theseus_desktop_tss_valid[idx] = true;
	}
#endif
	VERIFYHR(TheseusGetD3DDev()->SetTextureStageState(dwStage, dwState, dwValue));
}

inline void TheseusGetTextureStageState(DWORD dwStage, D3DTEXTURESTAGESTATETYPE dwState, LPDWORD lpdwValue)
{
#ifndef _XBOX
	if (dwStage < 8 && (DWORD)dwState < 32) {
		int idx = (int)dwStage * 32 + (int)dwState;
		if (theseus_desktop_tss_valid[idx]) {
			*lpdwValue = theseus_desktop_tss_cache[idx];
			return;
		}
	}
#endif
	VERIFYHR(TheseusGetD3DDev()->GetTextureStageState(dwStage, dwState, lpdwValue));
}

inline void TheseusSetTexture(DWORD dwStage, LPDIRECT3DTEXTURE8 lpTexture)
{
#ifndef _XBOX
	if (dwStage < 8) {
		if (theseus_desktop_tex_valid[dwStage] &&
		    theseus_desktop_tex_cache[dwStage] == lpTexture)
			return;
		theseus_desktop_tex_cache[dwStage] = lpTexture;
		theseus_desktop_tex_valid[dwStage] = true;
	}
#endif
	VERIFYHR(TheseusGetD3DDev()->SetTexture(dwStage, lpTexture));
}

inline void TheseusSetMaterial(D3DMATERIAL8* lpMaterial)
{
#ifndef _XBOX
	// Materials are 68 bytes; memcmp is faster than the virtual call.
	if (lpMaterial && theseus_desktop_mat_valid &&
	    memcmp(&theseus_desktop_mat_cache, lpMaterial, sizeof(D3DMATERIAL8)) == 0)
		return;
	if (lpMaterial) {
		theseus_desktop_mat_cache = *lpMaterial;
		theseus_desktop_mat_valid = true;
	}
#endif
	VERIFYHR(TheseusGetD3DDev()->SetMaterial(lpMaterial));
}

inline void TheseusSetTransform(D3DTRANSFORMSTATETYPE dtstTransformStateType, D3DMATRIX* lpD3DMatrix)
{
#ifndef _XBOX
	// 4x4 matrix compare. ~16 ns cost on miss, saves the virtual call
	// + GL matrix upload on hit. WORLD updates are usually unique per
	// draw so we expect lower hit rate here than for state setters,
	// but PROJECTION / VIEW often stay constant across many draws.
	if (lpD3DMatrix && (UINT)dtstTransformStateType < 256) {
		if (theseus_desktop_xform_valid[dtstTransformStateType] &&
		    memcmp(&theseus_desktop_xform_cache[dtstTransformStateType],
		           lpD3DMatrix, sizeof(D3DMATRIX)) == 0)
			return;
		theseus_desktop_xform_cache[dtstTransformStateType] = *lpD3DMatrix;
		theseus_desktop_xform_valid[dtstTransformStateType] = true;
	}
#endif
	VERIFYHR(TheseusGetD3DDev()->SetTransform(dtstTransformStateType, lpD3DMatrix));
}

inline void TheseusGetTransform(D3DTRANSFORMSTATETYPE dtstTransformStateType, D3DMATRIX* lpD3DMatrix)
{
	VERIFYHR(TheseusGetD3DDev()->GetTransform(dtstTransformStateType, lpD3DMatrix));
}

#ifndef _XBOX
inline void TheseusSetClipPlane(DWORD dwIndex, D3DVALUE* pPlaneEquation)
{
#ifndef _XBOX
	VERIFYHR(TheseusGetD3DDev()->SetClipPlane(dwIndex, pPlaneEquation));
#endif
}
#endif // _XBOX

#ifdef _LIGHTS
inline void TheseusSetLight(DWORD dwLightIndex, D3DLIGHT8* lpLight)
{
	VERIFYHR(TheseusGetD3DDev()->SetLight(dwLightIndex, lpLight));
}

inline void TheseusLightEnable(DWORD dwLightIndex, bool bEnable)
{
	VERIFYHR(TheseusGetD3DDev()->LightEnable(dwLightIndex, bEnable));
}
#endif

inline void TheseusClear(D3DCOLOR color)
{
	DWORD dwFlags = D3DCLEAR_TARGET;

	if (g_bZBuffer)
		dwFlags |= D3DCLEAR_ZBUFFER;

	VERIFYHR(TheseusGetD3DDev()->Clear(0, NULL, dwFlags, color, 1.0f, 0));
}

inline void TheseusBeginScene()
{
	VERIFYHR(TheseusGetD3DDev()->BeginScene());
}

inline void TheseusEndScene()
{
	VERIFYHR(TheseusGetD3DDev()->EndScene());
}

inline void TheseusPresent()
{
	VERIFYHR(TheseusGetD3DDev()->Present(NULL, NULL, NULL, NULL));
}

inline void TheseusGetTextureSize(LPDIRECT3DTEXTURE8 pTexture, int& nWidth, int& nHeight)
{
	D3DSURFACE_DESC sd;
	VERIFYHR(pTexture->GetLevelDesc(0, &sd));
	nWidth = (int)sd.Width;
	nHeight = (int)sd.Height;
}

inline void TheseusGetTextureSize(LPDIRECT3DTEXTURE8 pTexture, float& nWidth, float& nHeight)
{
	int iWidth, iHeight;
	TheseusGetTextureSize(pTexture, iWidth, iHeight);
	nWidth = (float)iWidth;
	nHeight = (float)iHeight;
}

inline void TheseusSetVertexShader(DWORD Handle)
{
#ifndef _XBOX
	if (theseus_desktop_vs_valid && theseus_desktop_vs_cache == Handle)
		return;
	theseus_desktop_vs_cache = Handle;
	theseus_desktop_vs_valid = true;
#endif
	VERIFYHR(TheseusGetD3DDev()->SetVertexShader(Handle));
}

inline void TheseusSetVertexShaderConstant(DWORD Register, const void* pConstantData, DWORD ConstantCount)
{
	VERIFYHR(TheseusGetD3DDev()->SetVertexShaderConstant(Register, pConstantData, ConstantCount));
}

inline void TheseusSetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer8* pStreamData, UINT Stride)
{
#ifndef _XBOX
	if (StreamNumber < 4) {
		if (theseus_desktop_vb_valid[StreamNumber] &&
		    theseus_desktop_vb_cache[StreamNumber] == pStreamData &&
		    theseus_desktop_vb_stride_cache[StreamNumber] == Stride)
			return;
		theseus_desktop_vb_cache[StreamNumber] = pStreamData;
		theseus_desktop_vb_stride_cache[StreamNumber] = Stride;
		theseus_desktop_vb_valid[StreamNumber] = true;
	}
#endif
	VERIFYHR(TheseusGetD3DDev()->SetStreamSource(StreamNumber, pStreamData, Stride));
}

inline void TheseusSetIndices(IDirect3DIndexBuffer8* pIndexData, UINT BaseVertexIndex)
{
#ifndef _XBOX
	if (theseus_desktop_ib_valid &&
	    theseus_desktop_ib_cache == pIndexData &&
	    theseus_desktop_ib_base_cache == BaseVertexIndex)
		return;
	theseus_desktop_ib_cache = pIndexData;
	theseus_desktop_ib_base_cache = BaseVertexIndex;
	theseus_desktop_ib_valid = true;
#endif
	VERIFYHR(TheseusGetD3DDev()->SetIndices(pIndexData, BaseVertexIndex));
}

inline void TheseusDrawIndexedPrimitive(D3DPRIMITIVETYPE Type, UINT MinIndex, UINT NumVertices, UINT StartIndex, UINT PrimitiveCount)
{
	VERIFYHR(TheseusGetD3DDev()->DrawIndexedPrimitive(Type, MinIndex, NumVertices, StartIndex, PrimitiveCount));
}

extern int __cdecl NewFailed(size_t nBytes);

inline void TheseusCreateIndexBuffer(UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DIndexBuffer8** ppIndexBuffer)
{
	HRESULT hr;

	do
	{
		hr = TheseusGetD3DDev()->CreateIndexBuffer(Length, Usage, Format, Pool, ppIndexBuffer);
		if (hr != E_OUTOFMEMORY)
			break;
	}
	while (NewFailed(Length) != 0);

	VERIFYHR(hr);
}

inline void TheseusCreateVertexBuffer(UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool, IDirect3DVertexBuffer8** ppVertexBuffer)
{
	HRESULT hr;

	do
	{
		hr = TheseusGetD3DDev()->CreateVertexBuffer(Length, Usage, FVF, Pool, ppVertexBuffer);
		if (hr != E_OUTOFMEMORY)
			break;
	}
	while (NewFailed(Length) != 0);

	VERIFYHR(hr);
}

inline void TheseusCreateVertexShader(CONST DWORD* pDeclaration, CONST DWORD* pFunction, DWORD* pHandle, DWORD Usage)
{
	VERIFYHR(TheseusGetD3DDev()->CreateVertexShader(pDeclaration, pFunction, pHandle, Usage));
}


// World Matrix Stack

inline D3DXMATRIX* TheseusGetWorld()
{
	ASSERT(g_worldStack != NULL);
	return g_worldStack->GetTop();
}

extern void SetFalloffShaderFrameValues();

inline void TheseusUpdateWorld()
{
	ASSERT(g_worldStack != NULL);
	TheseusSetTransform(D3DTS_WORLD, TheseusGetWorld());
	SetFalloffShaderFrameValues();
}

inline void TheseusPushWorld()
{
	ASSERT(g_worldStack != NULL);
	VERIFYHR(g_worldStack->Push());
}

inline void TheseusPopWorld()
{
	ASSERT(g_worldStack != NULL);
	VERIFYHR(g_worldStack->Pop());
	TheseusUpdateWorld();
}

inline void TheseusMultWorld(const D3DXMATRIX* pMat)
{
	ASSERT(g_worldStack != NULL);
	VERIFYHR(g_worldStack->MultMatrixLocal(pMat));
}

inline void TheseusTranslateWorld(float x, float y, float z)
{
	ASSERT(g_worldStack != NULL);
	VERIFYHR(g_worldStack->TranslateLocal(x, y, z));
}

inline void TheseusRotateWorld(const D3DXVECTOR3* pV, float angle)
{
	ASSERT(g_worldStack != NULL);
	VERIFYHR(g_worldStack->RotateAxisLocal(pV, angle));
}

inline void TheseusIdentityWorld()
{
	ASSERT(g_worldStack != NULL);
	VERIFYHR(g_worldStack->LoadIdentity());
}

#ifndef _XBOX
#undef D3DLOCK_DISCARD
#define D3DLOCK_DISCARD 0
#endif



inline float pos(float n)
{
	if (n > 0.0f)
		return n;

	return 0.0f;
}

inline float rnd(float n)
{
	return (float)rand() * n / 32767.0f;
}

inline float wrap(float n)
{
	return n - floorf(n);
}

#define trunc(n) ((int)(n))


inline int clamp(int x, int a, int b)
{
	return (x < a ? a : (x > b ? b : x));
}

inline float clampf(float x, float a, float b)
{
	return (x < a ? a : (x > b ? b : x));
}



extern bool CallFunction(CObject* pObject, const TCHAR* szFunc, int nParam = 0, CObject** rgParam = NULL);



extern TCHAR g_szCurDir [];

// Scoped guard that saves and restores the global current-directory
// path during a nested file fetch. Used by the asset loader to make
// relative URLs resolve against the directory of the file currently
// being parsed; the destructor restores the prior cwd on scope exit.
class CDirPush
{
public:
	CDirPush(const TCHAR* szFile = NULL)
	{
        ASSERT(g_dwMainThreadId == GetCurrentThreadId());
		_tcscpy(m_savedCurDir, g_szCurDir);

		if (szFile != NULL)
		{
			UpdateCurDirFromFile(szFile);
		}
	}

	~CDirPush()
	{
        ASSERT(g_dwMainThreadId == GetCurrentThreadId());
		_tcscpy(g_szCurDir, m_savedCurDir);
	}

	TCHAR m_savedCurDir [1024];
};



inline HANDLE TheseusCreateFile(const TCHAR* szFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes = 0, HANDLE hTemplateFile = NULL)
{
	// null path would crash inside CleanFilePath. Bail before we get there.
	if (!szFileName)
		return INVALID_HANDLE_VALUE;
	FSCHAR sszFileName [MAX_PATH];
	CleanFilePath(sszFileName, szFileName);
#ifdef _XBOX
	return CreateFile(sszFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
#elif defined(_WIN32)
	// Native Windows: use real Win32 CreateFileA so subsequent ReadFile/CloseHandle
	// calls hit real kernel handles. The fopen path below would return a FILE*
	// that the real Win32 APIs can't interpret.
	return CreateFileA(sszFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
#else
	const char* mode = "rb";
	if (dwDesiredAccess & GENERIC_WRITE) mode = (dwCreationDisposition == CREATE_ALWAYS) ? "wb" : "r+b";
	FILE* f = fopen(sszFileName, mode);
	if (!f && (dwCreationDisposition == CREATE_ALWAYS || dwCreationDisposition == OPEN_ALWAYS))
		f = fopen(sszFileName, "wb");
	return f ? (HANDLE)f : INVALID_HANDLE_VALUE;
#endif
}

inline bool TheseusCreateDirectory(LPCTSTR lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes = NULL)
{
	FSCHAR sszFileName [MAX_PATH];
	CleanFilePath(sszFileName, lpPathName);
#ifdef _XBOX
	return CreateDirectory(sszFileName, lpSecurityAttributes) != FALSE;
#else
	(void)lpSecurityAttributes;
#ifdef _WIN32
	return CreateDirectoryA(sszFileName, NULL) != FALSE;
#else
	return mkdir(sszFileName, 0755) == 0 || errno == EEXIST;
#endif
#endif
}



const CHAR TheseusTempPcmFileA[]  =  "MUSIC:\\tempcda.cda";
const WCHAR TheseusTempPcmFileW[] = L"MUSIC:\\tempcda.cda";
const CHAR TheseusTempWmaFileA[]  =  "MUSIC:\\tempwma.wma";
const WCHAR TheseusTempWmaFileW[] = L"MUSIC:\\tempwma.wma";

#ifdef _UNICODE
#define TheseusTempPcmFile TheseusTempPcmFileW
#else  // _UNICODE
#define TheseusTempPcmFile TheseusTempPcmFileA
#endif // _UNICODE



extern int g_nDiscType;

#define DISC_NONE   0
#define DISC_BAD    1
#define DISC_TITLE  2
#define DISC_AUDIO  3
#define DISC_VIDEO  4



extern bool ResetScreenSaver();



#define D3DFVF_NORMPACKED3		0x20000000

extern DWORD CompressNormal(float* pvNormal);



extern DWORD GetFixedFunctionShader(DWORD fvf);

#endif // THESEUS_H
