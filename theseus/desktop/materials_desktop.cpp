// materials_desktop.cpp [DESKTOP FORK]
// CMaxMaterial + CMatInfo binders. Forked from
// theseus/render/materials.cpp so the D3D8 shim's dead and
// redundant SetRenderState calls can come out without touching
// the Xbox build. Keep this and the Xbox file in sync per the
// contract below or scenes break on the other build.
//
// PARITY CONTRACT
//
// XIPs reference materials by name (the string the .max exporter
// wrote into the mesh's user data block). Both this file and
// theseus/render/materials.cpp resolve those names to a CMatInfo
// subclass. Out of sync means the same XIP looks different across
// platforms, or worst case a scene fails to bind a material on one.
//
// 1. Adding a new variant (new CMatInfo). Same name and same
//    MATINFO_* flag semantics in both files. Visual output CAN
//    differ on purpose (Xbox might use a hardware feature desktop
//    can't replicate) but the binding has to succeed on both.
//
// 2. Renaming a variant. Rename in both files in the same commit.
//    Then grep xips-source/ and Data/Xips/ for the old name across
//    default-xbox and default-desktop sets, update those too.
//
// 3. Removing a variant. Confirm zero XIP references first, then
//    remove from both files in the same commit.
//
// 4. Changing render state behavior. Desktop changes go here.
//    Xbox changes go in theseus/render/materials.cpp. If the
//    visual diverges noticeably, document it in
//    docs/decomp/Materials.md.
//
// 5. Why the fork exists. Desktop runs OpenGL via the D3D8 shim.
//    Xbox runs real D3D8. A lot of SetRenderState calls on the
//    Xbox path are dead, redundant, or counterproductive on GL
//    drivers (Windows AMD and Intel iGPU especially). The fork
//    lets us strip them on desktop without risking the Xbox
//    binary. See project_d3d_call_audit.md.
//
// Original decomp header preserved:
// CMaxMaterial (3ds Max-exported material) + the family of
// concrete CMatInfo material binders (solid colour, falloff, modulate-
// texture, alpha modes, etc.) selected per Shape via the .max exporter's
// user-data block. Decompiled from the 5960 retail XBE.

#include "std.h"
#include "theseus.h"
#include "node.h"
#include "shape_render.h"
#include "settingsfile.h"
#include "xip_archive.h"

extern class CMeshNode *g_pRenderMeshNode;
extern void SetFalloffShaderValues(const D3DXCOLOR &sideColor, const D3DXCOLOR &frontColor);
extern DWORD GetEffectShader(int nEffect, DWORD fvf);
extern LPDIRECT3DTEXTURE8 CreateTexture(int &nWidth, int &nHeight, D3DFORMAT format);
extern LPDIRECT3DTEXTURE8 GetTexture(const TCHAR *szURL, XTIME *pTimeLoaded = NULL, UINT width = 0, UINT height = 0, bool binXIP = false);
extern void SetReflectShaderFrameValues();

BOOL g_bEdgeAntialiasOverride;
const TCHAR *g_szCurTitleImage;
const TCHAR *g_szSelTitleImage;
bool g_bActiveKey = false;
extern float g_nEffectAlpha;
extern D3DRECT g_scissorRect;
extern D3DRECT g_scissorRectx2;
XTIME g_pulseStartTime;
static class CMaxMaterial *g_pLastPulseMaxMat = NULL;
int g_nMatInfoCount = 0;
TCHAR g_szLastLoadedSkin[MAX_PATH] = _T("");

class CustomColor
{
public:
	CustomColor();
	~CustomColor();

	int r;
	int g;
	int b;
	int a;
};

CustomColor::CustomColor()
{
	r = g = b = a = 0;
}

CustomColor::~CustomColor()
{
}

class CMaxMaterial : public CMaterial
{
	DECLARE_NODE(CMaxMaterial, CMaterial)
public:
	CMaxMaterial();
	~CMaxMaterial();

	TCHAR *m_name;
	XTIME m_param;

	bool OnSetProperty(const PRD *pprd, const void *pvValue);

	void Render();

	class CMatInfo *m_pMatInfo;

	DECLARE_NODE_PROPS()
};



// GenerateRadialAlphaMask   [no script binding; the four Get*AlphaMask build on it]
// Reconstructed from 5960 retail XBE @ 0x00056110 (pixels in EAX, scale in
// XMM0, pitch/width/height/factor/max on the stack, RET 0x14). Per pixel:
//     d = sqrt(dx*dx + dy*dy)          dx, dy run -1..1 across the drawn block
//     a = clamp((1 - clamp(d, 0, 1)) * factor, 0, max)
//     pixel = (BYTE)(int)(a * 255) << 24     alpha only, color stays 0
// Only the top-left width*scale by height*scale block is drawn; the rest of
// each row and every row under it is cleared. 1.0 / 0.0 / 255.0 are the
// binary's constants at 0x2bed8 / 0x2bee4 / 0x2bf38. Callers pass (factor,
// max, scale): 0x562d0 (1, 1, 1), 0x563a0 (1, 1, 0.6875), 0x56470 (10, 0.8, 1),
// 0x56540 (3, 1, 1). A round falloff has one sensible shape; this follows the
// binary's clamps and integer halving rather than any other version of it.
void GenerateRadialAlphaMask(BYTE *pbPels, int nPitch, int nWidth, int nHeight, float nFactor, float nMax, float nScale = 1.0f)
{
	const int cxDraw = (int)(nWidth * nScale);
	const int cyDraw = (int)(nHeight * nScale);
	const int halfX = cxDraw / 2;
	const int halfY = cyDraw / 2;
	const int rowBytes = nPitch & ~3;

	for (int y = 0; y < nHeight; y++)
	{
		DWORD *pRow = (DWORD *)(pbPels + y * rowBytes);
		int x = 0;

		if (y < cyDraw)
		{
			float dy = (float)(y - halfY) / (float)halfY;
			for (; x < cxDraw; x++)
			{
				float dx = (float)(x - halfX) / (float)halfX;
				float d = sqrtf(dx * dx + dy * dy);
				if (d < 0.0f)
					d = 0.0f;
				else if (d > 1.0f)
					d = 1.0f;

				float a = (1.0f - d) * nFactor;
				if (a < 0.0f)
					a = 0.0f;
				else if (a > nMax)
					a = nMax;

				pRow[x] = (DWORD)(BYTE)(int)(a * 255.0f) << 24;
			}
		}

		for (; x < nWidth; x++)
			pRow[x] = 0;
	}
}

LPDIRECT3DTEXTURE8 GetRadialAlphaMask()
{
	static LPDIRECT3DTEXTURE8 pTexture = NULL;

	if (pTexture != NULL)
		return pTexture;

	int nWidth = 256;
	int nHeight = 256;
	pTexture = CreateTexture(nWidth, nHeight, D3DFMT_A8R8G8B8);
	ASSERT(pTexture != NULL);

	D3DLOCKED_RECT lr;
	VERIFYHR(pTexture->LockRect(0, &lr, NULL, D3DLOCK_DISCARD));

#ifdef _XBOX
	BYTE *pbBuf = new BYTE[4 * nWidth * nHeight];
	GenerateRadialAlphaMask(pbBuf, nWidth * 4, nWidth, nHeight, 1.0f, 1.0f);
	XGSwizzleRect(pbBuf, 0, NULL, lr.pBits, nWidth, nHeight, NULL, 4);
	delete[] pbBuf;
#else
	GenerateRadialAlphaMask((BYTE *)lr.pBits, lr.Pitch, nWidth, nHeight, 1.0f, 1.0f);
#endif

	VERIFYHR(pTexture->UnlockRect(0));

	return pTexture;
}

LPDIRECT3DTEXTURE8 GetRadialAVAlphaMask()
{
	static LPDIRECT3DTEXTURE8 pTexture = NULL;

	if (pTexture != NULL)
		return pTexture;

	int nWidth = 256;
	int nHeight = 256;
	pTexture = CreateTexture(nWidth, nHeight, D3DFMT_A8R8G8B8);
	ASSERT(pTexture != NULL);

	D3DLOCKED_RECT lr;
	VERIFYHR(pTexture->LockRect(0, &lr, NULL, D3DLOCK_DISCARD));

#ifdef _XBOX
	BYTE *pbBuf = new BYTE[4 * nWidth * nHeight];
	GenerateRadialAlphaMask(pbBuf, nWidth * 4, nWidth, nHeight, 1.0f, 1.0f, 0.6875f);
	XGSwizzleRect(pbBuf, 0, NULL, lr.pBits, nWidth, nHeight, NULL, 4);
	delete[] pbBuf;
#else
	GenerateRadialAlphaMask((BYTE *)lr.pBits, lr.Pitch, nWidth, nHeight, 1.0f, 1.0f);
#endif

	VERIFYHR(pTexture->UnlockRect(0));

	return pTexture;
}

LPDIRECT3DTEXTURE8 GetRadialEdgeAlphaMask()
{
	static LPDIRECT3DTEXTURE8 pTexture = NULL;

	if (pTexture != NULL)
		return pTexture;

	int nWidth = 256;
	int nHeight = 256;
	pTexture = CreateTexture(nWidth, nHeight, D3DFMT_A8R8G8B8);
	ASSERT(pTexture != NULL);

	D3DLOCKED_RECT lr;
	VERIFYHR(pTexture->LockRect(0, &lr, NULL, D3DLOCK_DISCARD));

#ifdef _XBOX
	BYTE *pbBuf = new BYTE[4 * nWidth * nHeight];
	GenerateRadialAlphaMask(pbBuf, nWidth * 4, nWidth, nHeight, 10.0f, 0.8f);
	XGSwizzleRect(pbBuf, 0, NULL, lr.pBits, nWidth, nHeight, NULL, 4);
	delete[] pbBuf;
#else
	GenerateRadialAlphaMask((BYTE *)lr.pBits, lr.Pitch, nWidth, nHeight, 10.0f, 0.8f);
#endif

	VERIFYHR(pTexture->UnlockRect(0));

	return pTexture;
}

LPDIRECT3DTEXTURE8 GetRadialEdgeAlphaMainMask()
{
	static LPDIRECT3DTEXTURE8 pTexture = NULL;

	if (pTexture != NULL)
		return pTexture;

	int nWidth = 256;
	int nHeight = 256;
	pTexture = CreateTexture(nWidth, nHeight, D3DFMT_A8R8G8B8);
	ASSERT(pTexture != NULL);

	D3DLOCKED_RECT lr;
	VERIFYHR(pTexture->LockRect(0, &lr, NULL, D3DLOCK_DISCARD));

#ifdef _XBOX
	BYTE *pbBuf = new BYTE[4 * nWidth * nHeight];
	GenerateRadialAlphaMask(pbBuf, nWidth * 4, nWidth, nHeight, 3.0f, 1.0f);
	XGSwizzleRect(pbBuf, 0, NULL, lr.pBits, nWidth, nHeight, NULL, 4);
	delete[] pbBuf;
#else
	GenerateRadialAlphaMask((BYTE *)lr.pBits, lr.Pitch, nWidth, nHeight, 3.0f, 1.0f);
#endif

	VERIFYHR(pTexture->UnlockRect(0));

	return pTexture;
}



#define MATINFO_STANDARD_MATERIAL 0x00000001
#define MATINFO_CULL_NONE 0x00000002
#define MATINFO_RADIAL_ALPHA 0x00000004
#define MATINFO_RADIAL_EDGE_ALPHA 0x00000008
#define MATINFO_RADIAL_EDGE_MAIN_ALPHA 0x00000010
#define MATINFO_RADIAL_AV_ALPHA 0x00000020

class CSolidMatInfo : public CMatInfo
{
public:
	CSolidMatInfo(const TCHAR *szName, BYTE r, BYTE g, BYTE b, BYTE a, DWORD dwFlags = 0);

	bool Setup(CMaxMaterial *pMaxMat);

	BYTE m_r, m_g, m_b, m_a;
};

class CFalloffMatInfo : public CMatInfo
{
public:
	CFalloffMatInfo(const TCHAR *szName, D3DCOLOR colorSide, D3DCOLOR colorFront, DWORD dwFlags = 0);

	bool Setup(CMaxMaterial *pMaxMat);

	D3DCOLOR m_colorSide;
	D3DCOLOR m_colorFront;
	int m_nShaderEffect;
	TCHAR m_matName[MAX_PATH];
};

class CFalloffTexInfo : public CMatInfo
{
public:
	CFalloffTexInfo(const TCHAR *szName, D3DCOLOR colorSide, D3DCOLOR colorFront, DWORD dwFlags = 0);

	bool Setup(CMaxMaterial *pMaxMat);

	D3DCOLOR m_colorSide;
	D3DCOLOR m_colorFront;
	int m_nShaderEffect;
};

class CAnisoMatInfo : public CMatInfo
{
public:
	CAnisoMatInfo(const TCHAR *szName, D3DCOLOR colorSide, D3DCOLOR colorFront, DWORD dwFlags = 0);

	bool Setup(CMaxMaterial *pMaxMat);

	D3DCOLOR m_colorSide;
	D3DCOLOR m_colorFront;
	int m_nShaderEffect;
};

class CIconMatInfo : public CMatInfo
{
public:
	CIconMatInfo(const TCHAR *szName, UINT width, UINT height, bool bFadeIn = false, bool binXIP = false, bool bSelImg = false);

	bool Setup(CMaxMaterial *pMaxMat);

	bool m_bFadeIn;
	bool m_binXIP;
	bool m_bSelImg;

private:
	UINT m_width, m_height;
};

class CKeyMatInfo : public CFalloffMatInfo
{
public:
	CustomColor Color1;
	CustomColor Color2;
	CustomColor Color3;
	CustomColor Color4;
	CKeyMatInfo(const TCHAR *szName, bool bBright, bool bText, CustomColor ColorA, CustomColor ColorB, CustomColor ColorC, CustomColor ColorD);

	bool Setup(CMaxMaterial *pMaxMat);

	bool m_bBright;
	bool m_bText;
};

class CEggGlowPulseMatInfo : public CFalloffMatInfo
{
public:
	CustomColor Color1;
	CustomColor Color2;

	CEggGlowPulseMatInfo(const TCHAR *szName, CustomColor color_a, CustomColor color_b);

	bool Setup(CMaxMaterial *pMaxMat);
};

class CEggGlowFadeMatInfo : public CFalloffMatInfo
{
public:
	CustomColor Color1;
	CEggGlowFadeMatInfo(const TCHAR *szName, D3DCOLOR colorSide, CustomColor ColorA, DWORD dwFlags = 0);

	bool Setup(CMaxMaterial *pMaxMat);
};

class CBackingMatInfo : public CMatInfo
{
public:
	CBackingMatInfo(const TCHAR *szName, BYTE r, BYTE g, BYTE b, BYTE a, DWORD dwFlags = 0);

	bool Setup(CMaxMaterial *pMaxMat);

	BYTE m_r, m_g, m_b, m_a;
};

class CModulateTextureMatInfo : public CMatInfo
{
public:
	CModulateTextureMatInfo(const TCHAR *szName, BYTE r, BYTE g, BYTE b, BYTE a, DWORD dwFlags /*=0*/);

	bool Setup(CMaxMaterial *pMaxMat);

	BYTE m_r, m_g, m_b, m_a;
};

class CReflectMatInfo : public CMatInfo
{
public:
	CReflectMatInfo(const TCHAR *szName);

	bool Setup(CMaxMaterial *pMaxMat);

	int m_nShaderEffect;
};

class CMaskTextureMatInfo : public CMatInfo
{
public:
	CMaskTextureMatInfo(const TCHAR *szName, DWORD dwFlags);

	bool Setup(CMaxMaterial *pMaxMat);
};

class CFreeSpaceMatInfo : public CMatInfo
{
public:
	CFreeSpaceMatInfo(const TCHAR *szName);

	bool Setup(CMaxMaterial *pMaxMat);
};

class CInnerWallMatInfo : public CMatInfo
{
public:
	CInnerWallMatInfo(const TCHAR *szName, D3DCOLOR colorSide, D3DCOLOR colorFront, DWORD dwFlags = 0);

	bool Setup(CMaxMaterial *pMaxMat);

	D3DCOLOR m_colorSide;
	D3DCOLOR m_colorFront;
	int m_nShaderEffect;
};

CMatInfo *g_rgMatInfo[240];

CInnerWallMatInfo::CInnerWallMatInfo(const TCHAR *szName, D3DCOLOR colorSide, D3DCOLOR colorFront, DWORD dwFlags /*=0*/) : CMatInfo(szName, dwFlags)
{
	m_nShaderEffect = 3;
	m_colorSide = colorSide;
	m_colorFront = colorFront;
}
bool CInnerWallMatInfo::Setup(CMaxMaterial *pMaxMat)
{
	if (g_pRenderMeshNode == NULL)
		return false;

	TheseusSetVertexShader(GetEffectShader(m_nShaderEffect, g_pRenderMeshNode->GetFVF()));
	SetFalloffShaderValues(m_colorSide, m_colorFront);

	TheseusSetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
	TheseusSetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
	TheseusSetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
	TheseusSetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_TEXTURE);
	TheseusSetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	TheseusSetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
	TheseusSetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_TEXTURE);
	TheseusSetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);

	TheseusSetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	TheseusSetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);

	TheseusSetTextureStageState(0, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
	TheseusSetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);

	TheseusSetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);

	return CMatInfo::Setup(pMaxMat);
}

CFreeSpaceMatInfo::CFreeSpaceMatInfo(const TCHAR *szName) : CMatInfo(szName)
{
}

bool CFreeSpaceMatInfo::Setup(CMaxMaterial *pMaxMat)
{
	// This is the free/used space guage texture for memory units...
	TheseusSetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	TheseusSetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	TheseusSetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
	TheseusSetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	TheseusSetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);
	TheseusSetRenderState(D3DRS_TEXTUREFACTOR, D3DCOLOR_RGBA(255, 255, 255, (BYTE)(70 * g_nEffectAlpha)));
	TheseusSetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);

	D3DXMATRIX mat;
	D3DXMatrixIdentity(&mat);
	mat._32 = (float)((1.0f - pMaxMat->m_param) * 2.0f - 1.0f) * 0.3f + 0.1f;
	TheseusSetTransform(D3DTS_TEXTURE0, &mat);
	TheseusSetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
	TheseusSetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);

	return true;
}

CMaskTextureMatInfo::CMaskTextureMatInfo(const TCHAR *szName, DWORD dwFlags) : CMatInfo(szName, dwFlags)
{
}

bool CMaskTextureMatInfo::Setup(CMaxMaterial *pMaxMat)
{
	if ((m_flags & MATINFO_RADIAL_ALPHA) != 0)
		TheseusSetTexture(1, GetRadialAlphaMask());
	else if ((m_flags & MATINFO_RADIAL_EDGE_ALPHA) != 0)
		TheseusSetTexture(1, GetRadialEdgeAlphaMask());
	else if ((m_flags & MATINFO_RADIAL_EDGE_MAIN_ALPHA) != 0)
		TheseusSetTexture(1, GetRadialEdgeAlphaMainMask());
	else if ((m_flags & MATINFO_RADIAL_AV_ALPHA) != 0)
		TheseusSetTexture(1, GetRadialAVAlphaMask());

	TheseusSetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
	TheseusSetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
	TheseusSetTextureStageState(1, D3DTSS_ALPHAARG1, D3DTA_CURRENT);
	TheseusSetTextureStageState(1, D3DTSS_ALPHAARG2, D3DTA_TEXTURE);
	TheseusSetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_ADD);
	TheseusSetTextureStageState(1, D3DTSS_COLORARG1, D3DTA_CURRENT);
	TheseusSetTextureStageState(1, D3DTSS_COLORARG2, D3DTA_TEXTURE);
	TheseusSetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 0);

	TheseusSetTextureStageState(2, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	TheseusSetTextureStageState(2, D3DTSS_COLORARG1, D3DTA_CURRENT);
	TheseusSetTextureStageState(2, D3DTSS_COLORARG2, D3DTA_TEXTURE);
	TheseusSetTextureStageState(2, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
	TheseusSetTextureStageState(2, D3DTSS_ALPHAARG1, D3DTA_CURRENT);
	TheseusSetTextureStageState(2, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);

	TheseusSetRenderState(D3DRS_TEXTUREFACTOR, D3DCOLOR_RGBA(0, 0, 0, (BYTE)(255.0f * g_nEffectAlpha)));

	return CMatInfo::Setup(pMaxMat);
}

CModulateTextureMatInfo::CModulateTextureMatInfo(const TCHAR *szName, BYTE r, BYTE g, BYTE b, BYTE a, DWORD dwFlags /*=0*/) : CMatInfo(szName, dwFlags)
{
	m_r = r;
	m_g = g;
	m_b = b;
	m_a = a;
}

bool CModulateTextureMatInfo::Setup(CMaxMaterial *pMaxMat)
{
	TheseusSetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
	TheseusSetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	TheseusSetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	TheseusSetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	TheseusSetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TFACTOR);

	if (g_pLastPulseMaxMat != pMaxMat && g_nEffectAlpha == 1.0f)
	{
		g_pLastPulseMaxMat = pMaxMat;
		g_pulseStartTime = TheseusGetNow();
	}

	float t = (float)(TheseusGetNow() - g_pulseStartTime) / 2.0f;
	float a = 1.0f - fabsf(sinf(t * D3DX_PI));

	TheseusSetRenderState(D3DRS_TEXTUREFACTOR, D3DCOLOR_RGBA(m_r, m_g, m_b, (BYTE)(m_a * (a * 0.25f + 0.75f) * g_nEffectAlpha)));

	// Edge AA + per-material MSAA toggle stripped (shim no-op on GL).
	// On Xbox these flip the NV2A's hardware edge AA; on desktop MSAA
	// is configured at GL context creation and EDGEANTIALIAS has no
	// GL equivalent.

	return CMatInfo::Setup(pMaxMat);
}

CReflectMatInfo::CReflectMatInfo(const TCHAR *szName) : CMatInfo(szName, 0)
{
	m_nShaderEffect = 4;
}

bool CReflectMatInfo::Setup(CMaxMaterial *pMaxMat)
{
	if (g_pRenderMeshNode == NULL)
		return false;

	TheseusSetVertexShader(GetEffectShader(m_nShaderEffect, g_pRenderMeshNode->GetFVF()));
	SetFalloffShaderValues(D3DCOLOR_RGBA(229, 229, 229, 0), D3DCOLOR_RGBA(229, 229, 229, 255));

	SetReflectShaderFrameValues();

	TheseusSetTextureStageState(0, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
	TheseusSetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
	TheseusSetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	TheseusSetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	TheseusSetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	TheseusSetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
	TheseusSetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

	return CMatInfo::Setup(pMaxMat);
}

CBackingMatInfo::CBackingMatInfo(const TCHAR *szName, BYTE r, BYTE g, BYTE b, BYTE a, DWORD dwFlags /*=0*/) : CMatInfo(szName, dwFlags)
{
	m_r = r;
	m_g = g;
	m_b = b;
	m_a = a;
}

bool CBackingMatInfo::Setup(CMaxMaterial *pMaxMat)
{
	if ((m_flags & MATINFO_RADIAL_ALPHA) != 0)
		TheseusSetTexture(0, GetRadialAlphaMask());
	else if ((m_flags & MATINFO_RADIAL_EDGE_ALPHA) != 0)
		TheseusSetTexture(0, GetRadialEdgeAlphaMask());
	else if ((m_flags & MATINFO_RADIAL_EDGE_MAIN_ALPHA) != 0)
		TheseusSetTexture(0, GetRadialEdgeAlphaMainMask());
	else if ((m_flags & MATINFO_RADIAL_AV_ALPHA) != 0)
		TheseusSetTexture(0, GetRadialAVAlphaMask());

	TheseusSetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
	TheseusSetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
	TheseusSetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	TheseusSetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);
	TheseusSetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	TheseusSetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TFACTOR);
	TheseusSetRenderState(D3DRS_TEXTUREFACTOR, D3DCOLOR_RGBA(m_r, m_g, m_b, (BYTE)(m_a * g_nEffectAlpha)));

	return CMatInfo::Setup(pMaxMat);
}

CEggGlowPulseMatInfo::CEggGlowPulseMatInfo(const TCHAR *szName, CustomColor color_a, CustomColor color_b) : CFalloffMatInfo(szName, 0, D3DCOLOR_RGBA(0, 0, 0, 0), D3DCOLOR_RGBA(0, 0, 0, 0))
{
	Color1 = color_a;
	Color2 = color_b;
}

bool CEggGlowPulseMatInfo::Setup(CMaxMaterial *pMaxMat)
{
	if (g_pLastPulseMaxMat != pMaxMat)
	{
		g_pLastPulseMaxMat = pMaxMat;
		g_pulseStartTime = 2.0f;
	}

	float t = (float)(TheseusGetNow() - g_pulseStartTime) / 2.0f;
	float a = 1.0f - fabsf(sinf(t * D3DX_PI));

	m_colorSide = D3DCOLOR_RGBA(Color1.r, Color1.g, Color1.b, Color1.a);
	m_colorFront = D3DCOLOR_RGBA(Color2.r, Color2.g, Color2.b, (int)(64.0f + 128.0f * a));

	return CFalloffMatInfo::Setup(pMaxMat);
}

CEggGlowFadeMatInfo::CEggGlowFadeMatInfo(const TCHAR *szName, D3DCOLOR colorSide, CustomColor ColorA, DWORD dwFlags) : CFalloffMatInfo(szName, dwFlags, colorSide, D3DCOLOR_RGBA(ColorA.r, ColorA.g, ColorA.b, ColorA.a))
{
	Color1 = ColorA;
}

bool CEggGlowFadeMatInfo::Setup(CMaxMaterial *pMaxMat)
{
	float a = 0.0f;

	if (pMaxMat->m_param == 1.0f)
	{
		pMaxMat->m_param = TheseusGetNow();
		ASSERT(pMaxMat->m_param > 1.0f);
	}

	if (pMaxMat->m_param > 0.0f)
	{
		float t = (float)(TheseusGetNow() - pMaxMat->m_param) / 1.25f;
		if (t >= 1.0f)
		{
			t = 1.0f;
			pMaxMat->m_param = 0.0f;
		}

		a = 1.0f - t;
	}

	// m_colorFront = D3DCOLOR_RGBA(254, 255, 188, (int)(255.0f * a));
	m_colorFront = D3DCOLOR_RGBA(Color1.r, Color1.g, Color1.b, (int)(255.0f * a));

	return CFalloffMatInfo::Setup(pMaxMat);
}

CKeyMatInfo::CKeyMatInfo(const TCHAR *szName, bool bBright, bool bText, CustomColor ColorA, CustomColor ColorB, CustomColor ColorC, CustomColor ColorD) : CFalloffMatInfo(szName, D3DCOLOR_RGBA(0, 0, 0, 0), D3DCOLOR_RGBA(0, 0, 0, 0), 0)
{
	Color1 = ColorA;
	Color2 = ColorB;
	Color3 = ColorC;
	Color4 = ColorD;
	m_bBright = bBright;
	m_bText = bText;
}

bool CKeyMatInfo::Setup(CMaxMaterial *pMaxMat)
{
	if (g_bActiveKey)
	{

		if (m_bBright)
		{
			m_colorSide = D3DCOLOR_RGBA(Color1.r, Color1.g, Color1.b, Color1.a);
			m_colorFront = D3DCOLOR_RGBA(Color1.r, Color1.g, Color1.b, (Color1.a - 65));
		}
		else
		{
			m_colorSide = D3DCOLOR_RGBA(Color1.r, Color1.g, Color1.b, Color1.a);
			m_colorFront = D3DCOLOR_RGBA(Color1.r, Color1.g, Color1.b, (Color1.a - 65));
		}

		if (m_bText)
		{
			TheseusSetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
			TheseusSetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
			TheseusSetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TFACTOR);
			TheseusSetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
			TheseusSetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
			TheseusSetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);
			TheseusSetRenderState(D3DRS_TEXTUREFACTOR, D3DCOLOR_RGBA(0, 0, 0, (BYTE)(192 * g_nEffectAlpha)));
		}
	}
	else
	{
		if (m_bBright)
		{
			m_colorSide = D3DCOLOR_RGBA(Color2.r, Color2.g, Color2.b, Color2.a);
			m_colorFront = D3DCOLOR_RGBA(Color3.r, Color3.g, Color3.b, Color3.a);
		}
		else
		{
			m_colorSide = D3DCOLOR_RGBA(Color2.r, Color2.g, Color2.b, (Color2.a - 33));
			m_colorFront = D3DCOLOR_RGBA((Color3.r - 5), (Color3.g - 33), Color3.b, (Color3.a - 19));
		}

		if (m_bText)
		{
			TheseusSetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
			TheseusSetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
			TheseusSetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TFACTOR);
			TheseusSetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
			TheseusSetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
			TheseusSetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);

			TheseusSetRenderState(D3DRS_TEXTUREFACTOR, D3DCOLOR_RGBA(Color4.r, Color4.g, Color4.b, (BYTE)(178 * g_nEffectAlpha)));
		}
	}

	// Edge AA + MSAA toggle stripped (shim no-op on GL).

	return CFalloffMatInfo::Setup(pMaxMat);
}

CIconMatInfo::CIconMatInfo(const TCHAR *szName, UINT width, UINT height, bool bFadeIn /*=false*/, bool binXIP /*=false*/, bool bSelImg) : CMatInfo(szName)
{
	m_bFadeIn = bFadeIn;
	m_binXIP = binXIP;
	m_bSelImg = bSelImg;
	m_width = width;
	m_height = height;
}

bool CIconMatInfo::Setup(CMaxMaterial *pMaxMat)
{
	LPDIRECT3DTEXTURE8 pTexture = NULL;
	float alpha = 1.0f;
	bool bInXip = m_binXIP;

	if ((g_szCurTitleImage != NULL) && (!m_bSelImg))
	{

		if ((_tcsicmp(g_szCurTitleImage, _T("xboxlogo64.xbx")) == 0) ||
			(_tcsicmp(g_szCurTitleImage, _T("xboxlogo128.xbx")) == 0))
		{
			bInXip = true;
		}

		XTIME timeLoaded = 0.0f;
		pTexture = GetTexture(g_szCurTitleImage, &timeLoaded, m_width, m_height, bInXip);
		if (pTexture != NULL && m_bFadeIn)
		{
			alpha = (float)(TheseusGetNow() - timeLoaded) / 0.25f;
			if (alpha > 1.0f)
				alpha = 1.0f;
		}
	}
	else if ((g_szSelTitleImage != NULL) && (m_bSelImg)) // for the selected image orb
	{

		if ((_tcsicmp(g_szSelTitleImage, _T("xboxlogo64.xbx")) == 0) ||
			(_tcsicmp(g_szSelTitleImage, _T("xboxlogo128.xbx")) == 0))
		{
			bInXip = true;
		}

		XTIME timeLoaded = 0.0f;
		pTexture = GetTexture(g_szSelTitleImage, &timeLoaded, m_width, m_height, bInXip);
		if (pTexture != NULL && m_bFadeIn)
		{
			alpha = (float)(TheseusGetNow() - timeLoaded) / 0.25f;
			if (alpha > 1.0f)
				alpha = 1.0f;
		}
	}

	if (pTexture != NULL)
	{
		TheseusSetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
		TheseusSetRenderState(D3DRS_ZWRITEENABLE, FALSE);
		TheseusSetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
		TheseusSetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		TheseusSetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

		TheseusSetTexture(0, pTexture);
		TheseusSetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
		TheseusSetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
		TheseusSetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		TheseusSetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
		TheseusSetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		TheseusSetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);
		TheseusSetRenderState(D3DRS_TEXTUREFACTOR, D3DCOLOR_COLORVALUE(0.0f, 0.0f, 0.0f, (alpha * g_nEffectAlpha)));
	}
	else
	{
		TheseusSetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
		TheseusSetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TFACTOR);
		TheseusSetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
		TheseusSetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TFACTOR);
		TheseusSetRenderState(D3DRS_TEXTUREFACTOR, D3DCOLOR_COLORVALUE(0.0f, 0.0f, 0.0f, 0.0f));
	}

	TheseusSetTexture(1, GetRadialEdgeAlphaMask());
	TheseusSetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 0);
	TheseusSetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
	TheseusSetTextureStageState(1, D3DTSS_ALPHAARG1, D3DTA_CURRENT);
	TheseusSetTextureStageState(1, D3DTSS_ALPHAARG2, D3DTA_TEXTURE);
	TheseusSetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_ADD);
	TheseusSetTextureStageState(1, D3DTSS_COLORARG1, D3DTA_CURRENT);
	TheseusSetTextureStageState(1, D3DTSS_COLORARG2, D3DTA_TEXTURE);

	return true;
}

CMatInfo::CMatInfo(const TCHAR *szName, DWORD dwFlags /*=0*/)
{
	ASSERT(g_nMatInfoCount < countof(g_rgMatInfo));
	g_rgMatInfo[g_nMatInfoCount] = this;
	g_nMatInfoCount += 1;

	m_name = szName;
	m_flags = dwFlags;
}

CSolidMatInfo::CSolidMatInfo(const TCHAR *szName, BYTE r, BYTE g, BYTE b, BYTE a, DWORD dwFlags /*=0*/) : CMatInfo(szName, dwFlags)
{
	m_r = r;
	m_g = g;
	m_b = b;
	m_a = a;
}

bool CMatInfo::Setup(CMaxMaterial *pMaxMat)
{
	if ((m_flags & MATINFO_STANDARD_MATERIAL) != 0)
		return false;

	if ((m_flags & MATINFO_CULL_NONE) != 0)
		TheseusSetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);

	return true;
}

bool CSolidMatInfo::Setup(CMaxMaterial *pMaxMat)
{
	TheseusSetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
	TheseusSetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	TheseusSetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TFACTOR);
	TheseusSetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
	TheseusSetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
	TheseusSetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);
	TheseusSetRenderState(D3DRS_TEXTUREFACTOR, D3DCOLOR_RGBA(m_r, m_g, m_b, (BYTE)(m_a * g_nEffectAlpha)));

	// Edge AA + per-material MSAA toggle stripped (shim no-op on GL).
	// On Xbox these flip the NV2A's hardware edge AA; on desktop MSAA
	// is configured at GL context creation and EDGEANTIALIAS has no
	// GL equivalent.

	return CMatInfo::Setup(pMaxMat);
}

CFalloffMatInfo::CFalloffMatInfo(const TCHAR *szName, D3DCOLOR colorSide, D3DCOLOR colorFront, DWORD dwFlags /*=0*/) : CMatInfo(szName, dwFlags)
{
	m_nShaderEffect = 1;
	m_colorSide = colorSide;
	m_colorFront = colorFront;
	_tcscpy(m_matName, szName);
}

bool CFalloffMatInfo::Setup(CMaxMaterial *pMaxMat)
{
	if (g_pRenderMeshNode == NULL)
		return false;

	TheseusSetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
	TheseusSetVertexShader(GetEffectShader(m_nShaderEffect, g_pRenderMeshNode->GetFVF()));
	TheseusSetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_DISABLE);
	TheseusSetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

	// Edge AA + per-material MSAA toggle stripped (shim no-op on GL).
	// On Xbox these flip the NV2A's hardware edge AA; on desktop MSAA
	// is configured at GL context creation and EDGEANTIALIAS has no
	// GL equivalent.

	SetFalloffShaderValues(m_colorSide, m_colorFront);

	return CMatInfo::Setup(pMaxMat);
}

CFalloffTexInfo::CFalloffTexInfo(const TCHAR *szName, D3DCOLOR colorSide, D3DCOLOR colorFront, DWORD dwFlags /*=0*/) : CMatInfo(szName, dwFlags)
{
	m_nShaderEffect = 3;
	m_colorSide = colorSide;
	m_colorFront = colorFront;
}

bool CFalloffTexInfo::Setup(CMaxMaterial *pMaxMat)
{
	if (g_pRenderMeshNode == NULL)
		return false;

	TheseusSetVertexShader(GetEffectShader(m_nShaderEffect, g_pRenderMeshNode->GetFVF()));
	SetFalloffShaderValues(m_colorSide, m_colorFront);

	TheseusSetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
	TheseusSetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
	TheseusSetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
	TheseusSetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_TEXTURE);
	TheseusSetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	TheseusSetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
	TheseusSetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_TEXTURE);
	TheseusSetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);

	TheseusSetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	TheseusSetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);

	return CMatInfo::Setup(pMaxMat);
}

CAnisoMatInfo::CAnisoMatInfo(const TCHAR *szName, D3DCOLOR colorSide, D3DCOLOR colorFront, DWORD dwFlags /*=0*/) : CMatInfo(szName, dwFlags)
{
	m_nShaderEffect = 2;
	m_colorSide = colorSide;
	m_colorFront = colorFront;
}

bool CAnisoMatInfo::Setup(CMaxMaterial *pMaxMat)
{
	if (g_pRenderMeshNode == NULL)
		return false;

	TheseusSetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
	TheseusSetVertexShader(GetEffectShader(m_nShaderEffect, g_pRenderMeshNode->GetFVF()));
	TheseusSetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_DISABLE);
	TheseusSetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

	SetFalloffShaderValues(m_colorSide, m_colorFront);

	return CMatInfo::Setup(pMaxMat);
}

static int __cdecl SortMatInfoCompare(const void *elem1, const void *elem2)
{
	const CMatInfo *pMatInfo1 = *(const CMatInfo **)elem1;
	const CMatInfo *pMatInfo2 = *(const CMatInfo **)elem2;
	return _tcscmp(pMatInfo1->m_name, pMatInfo2->m_name);
}

static int __cdecl SearchMatInfoCompare(const void *elem1, const void *elem2)
{
	const TCHAR *szName = (const TCHAR *)elem1;
	const CMatInfo *pMatInfo = *(const CMatInfo **)elem2;
	return _tcscmp(szName, pMatInfo->m_name);
}

CMatInfo *LookupMatInfo(const TCHAR *szName)
{
	CMatInfo **ppMatInfo = (CMatInfo **)bsearch(szName, g_rgMatInfo, g_nMatInfoCount, sizeof(CMatInfo *), SearchMatInfoCompare);
	if (ppMatInfo == NULL)
		return NULL;
	return *ppMatInfo;
}

// Public introspection / live edit API used by the Skin Editor.
int MatInfo_ColorCount(CMatInfo* p)
{
	if (!p) return 0;
	if (dynamic_cast<CInnerWallMatInfo*>(p)) return 2;
	if (dynamic_cast<CFalloffMatInfo*>(p))   return 2;
	if (dynamic_cast<CSolidMatInfo*>(p))     return 1;
	if (dynamic_cast<CBackingMatInfo*>(p))   return 1;
	if (dynamic_cast<CModulateTextureMatInfo*>(p)) return 1;
	return 0;
}

DWORD MatInfo_GetColor(CMatInfo* p, int idx)
{
	if (!p) return 0;
	if (CSolidMatInfo* m = dynamic_cast<CSolidMatInfo*>(p))
		return D3DCOLOR_RGBA(m->m_r, m->m_g, m->m_b, m->m_a);
	if (CBackingMatInfo* m = dynamic_cast<CBackingMatInfo*>(p))
		return D3DCOLOR_RGBA(m->m_r, m->m_g, m->m_b, m->m_a);
	if (CModulateTextureMatInfo* m = dynamic_cast<CModulateTextureMatInfo*>(p))
		return D3DCOLOR_RGBA(m->m_r, m->m_g, m->m_b, m->m_a);
	if (CInnerWallMatInfo* m = dynamic_cast<CInnerWallMatInfo*>(p))
		return (idx == 0) ? m->m_colorSide : m->m_colorFront;
	if (CFalloffMatInfo* m = dynamic_cast<CFalloffMatInfo*>(p))
		return (idx == 0) ? m->m_colorSide : m->m_colorFront;
	return 0;
}

void MatInfo_SetColor(CMatInfo* p, int idx, DWORD c)
{
	if (!p) return;
	BYTE a = (BYTE)((c >> 24) & 0xFF);
	BYTE r = (BYTE)((c >> 16) & 0xFF);
	BYTE g = (BYTE)((c >>  8) & 0xFF);
	BYTE b = (BYTE)( c        & 0xFF);
	if (CSolidMatInfo* m = dynamic_cast<CSolidMatInfo*>(p))
		{ m->m_r=r; m->m_g=g; m->m_b=b; m->m_a=a; return; }
	if (CBackingMatInfo* m = dynamic_cast<CBackingMatInfo*>(p))
		{ m->m_r=r; m->m_g=g; m->m_b=b; m->m_a=a; return; }
	if (CModulateTextureMatInfo* m = dynamic_cast<CModulateTextureMatInfo*>(p))
		{ m->m_r=r; m->m_g=g; m->m_b=b; m->m_a=a; return; }
	if (CInnerWallMatInfo* m = dynamic_cast<CInnerWallMatInfo*>(p)) {
		if (idx == 0) m->m_colorSide = c; else m->m_colorFront = c;
		return;
	}
	if (CFalloffMatInfo* m = dynamic_cast<CFalloffMatInfo*>(p)) {
		if (idx == 0) m->m_colorSide = c; else m->m_colorFront = c;
		return;
	}
}

#include "materials_retail.inl"

IMPLEMENT_NODE("MaxMaterial", CMaxMaterial, CMaterial)

START_NODE_PROPS(CMaxMaterial, CMaterial)
NODE_PROP(pt_string, CMaxMaterial, name)
NODE_PROP(pt_number, CMaxMaterial, param)
END_NODE_PROPS()

CMaxMaterial::CMaxMaterial() : m_name(NULL),
							   m_param(0.0f)
{
	m_pMatInfo = NULL;
}

CMaxMaterial::~CMaxMaterial()
{
	delete[] m_name;
}

bool CMaxMaterial::OnSetProperty(const PRD *pprd, const void *pvValue)
{
	if (PTR2INT(pprd->pbOffset) == offsetof(m_name))
		m_pMatInfo = NULL;
	else if (PTR2INT(pprd->pbOffset) == offsetof(m_param))
	{
		m_param = (XTIME)(*(float *)pvValue);
		return false;
	}

	return true;
}

void CMaxMaterial::Render()
{
	if (m_pMatInfo == NULL && m_name != NULL)
		m_pMatInfo = LookupMatInfo(m_name);

	if (m_pMatInfo == NULL)
	{
		TRACE(_T("\001Referencing undefined MaxMaterial '%s'\n"), m_name);
		return;
	}

	// Edge AA + MSAA reset stripped (shim no-op on GL). On Xbox this
	// is the default-state preamble before per-material Setup() runs;
	// on desktop both states have no shim handler.

	if (_tcscmp(m_name, TEXT("Tubes")) == 0 ||
		_tcscmp(m_name, TEXT("TubesFade")) == 0 ||
		_tcscmp(m_name, TEXT("TubesQ")) == 0 ||
		_tcscmp(m_name, TEXT("Tube")) == 0)
	{
		g_bEdgeAntialiasOverride = TRUE;
	}
	else
	{
		g_bEdgeAntialiasOverride = FALSE;
	}

	m_pMatInfo->Setup(this);

#ifdef THESEUS_DESKTOP
	extern IDirect3DDevice8* g_pD3DDev;
	if (g_pD3DDev) m_pMatInfo->m_lastUsedFrame = g_pD3DDev->m_frameNumber;
#endif
}
