// materials.cpp: MaxMaterial, the node a mesh names its material with, and
// the CMatInfo family that turns each material name into texture stage and
// render state setup. Rebuilt from the 5960 retail XBE (0x56110..0x5a130);
// findings are in docs/decomp/Materials.md. The retail material table and
// the skin overrides live in materials_retail.inl.
//
// XIPs bind materials by name, so the names, the CMatInfo classes and the
// MATINFO_* flags are fixed. Both builds compile this file; the only
// desktop differences are the two NV2A antialias states, which the desktop
// renderer has no use for, and the skin editor hooks.

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

// Globals the binary keeps for the materials: the Tubes edge AA override
// (0x0017c8d0), the current and selected title images (0x0017c8cc /
// 0x0017c8c8), the key palette switch (0x0017e74d), and the shared pulse
// clock (0x0017c8c0) with the material that last restarted it (0x0017e750).
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

// One skin color, kept as four ints so the skin loader can fill it.
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
	r = 0;
	g = 0;
	b = 0;
	a = 0;
}

CustomColor::~CustomColor()
{
}

// ===== CMaxMaterial =========================================================
// 0xa8 bytes, factory 0x00059ff0 (the Material constructor plus these three),
// vtable 0x00028b70. name 0x90, param 0x98 (double), bound CMatInfo 0xa0.

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

// The four mask textures. Each one is built on first use and kept for good.
// Reconstructed from 5960 retail XBE @ 0x000562d0, 0x000563a0, 0x00056470 and
// 0x00056540, which are the same body with different numbers:
//     if (!cached) { w = h = 256; cached = CreateTexture(&w, &h, A8R8G8B8);
//                    LockRect; buf = new BYTE[w * h * 4];
//                    GenerateRadialAlphaMask(buf, w * 4, w, h, factor, max, scale);
//                    XGSwizzleRect(buf, 0, NULL, bits, w, h, NULL, 4); free(buf); }
//     return cached;
// Desktop textures are linear, so the mask goes straight into the locked bits.
static LPDIRECT3DTEXTURE8 BuildRadialMask(LPDIRECT3DTEXTURE8 *ppCache, float nFactor, float nMax, float nScale)
{
	if (*ppCache != NULL)
		return *ppCache;

	int cx = 256;
	int cy = 256;
	LPDIRECT3DTEXTURE8 pMask = CreateTexture(cx, cy, D3DFMT_A8R8G8B8);
	*ppCache = pMask;
	if (pMask == NULL)
		return NULL;

	D3DLOCKED_RECT locked;
	pMask->LockRect(0, &locked, NULL, D3DLOCK_DISCARD);

#ifdef _XBOX
	BYTE *pbLinear = new BYTE [cx * cy * 4];
	GenerateRadialAlphaMask(pbLinear, cx * 4, cx, cy, nFactor, nMax, nScale);
	XGSwizzleRect(pbLinear, 0, NULL, locked.pBits, cx, cy, NULL, 4);
	delete [] pbLinear;
#else
	GenerateRadialAlphaMask((BYTE *)locked.pBits, locked.Pitch, cx, cy, nFactor, nMax, nScale);
#endif

	pMask->UnlockRect(0);
	return pMask;
}

// Plain round falloff, factor 1, max 1.
LPDIRECT3DTEXTURE8 GetRadialAlphaMask()
{
	static LPDIRECT3DTEXTURE8 s_pMask = NULL;
	return BuildRadialMask(&s_pMask, 1.0f, 1.0f, 1.0f);
}

// The same circle squeezed into the top left 0.6875 of the texture. Desktop
// has always drawn it full size; kept that way.
LPDIRECT3DTEXTURE8 GetRadialAVAlphaMask()
{
	static LPDIRECT3DTEXTURE8 s_pMask = NULL;
#ifdef _XBOX
	return BuildRadialMask(&s_pMask, 1.0f, 1.0f, 0.6875f);
#else
	return BuildRadialMask(&s_pMask, 1.0f, 1.0f, 1.0f);
#endif
}

// Steep edge: factor 10 capped at 0.8, so only the rim fades. Icons use it.
LPDIRECT3DTEXTURE8 GetRadialEdgeAlphaMask()
{
	static LPDIRECT3DTEXTURE8 s_pMask = NULL;
	return BuildRadialMask(&s_pMask, 10.0f, 0.8f, 1.0f);
}

// Softer edge for the main panels: factor 3, max 1.
LPDIRECT3DTEXTURE8 GetRadialEdgeAlphaMainMask()
{
	static LPDIRECT3DTEXTURE8 s_pMask = NULL;
	return BuildRadialMask(&s_pMask, 3.0f, 1.0f, 1.0f);
}


// ===== CMatInfo family ======================================================
// Every class has one virtual, Setup. Shared layout: name 0x04, flags 0x08.
// The flag bits are the binary's: 1 the mesh's own material, 2 no culling,
// 4..0x20 pick one of the four masks above, 0x40 turns edge AA off for a
// solid color.

#define MATINFO_STANDARD_MATERIAL 0x00000001
#define MATINFO_CULL_NONE 0x00000002
#define MATINFO_RADIAL_ALPHA 0x00000004
#define MATINFO_RADIAL_EDGE_ALPHA 0x00000008
#define MATINFO_RADIAL_EDGE_MAIN_ALPHA 0x00000010
#define MATINFO_RADIAL_AV_ALPHA 0x00000020
#define MATINFO_NO_EDGE_AA 0x00000040

// Solid, vtable 0x00028bf4: R, G, B, A bytes at 0x0c.
class CSolidMatInfo : public CMatInfo
{
public:
	CSolidMatInfo(const TCHAR *szName, BYTE r, BYTE g, BYTE b, BYTE a, DWORD dwFlags = 0);

	bool Setup(CMaxMaterial *pMaxMat);

	BYTE m_r, m_g, m_b, m_a;
};

// Falloff, vtable 0x00028bf0: side 0x0c, front 0x10, shader effect 0x14,
// a copy of the name at 0x18.
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

// FalloffTex, vtable 0x00028be8: falloff modulated by the mesh's texture.
class CFalloffTexInfo : public CMatInfo
{
public:
	CFalloffTexInfo(const TCHAR *szName, D3DCOLOR colorSide, D3DCOLOR colorFront, DWORD dwFlags = 0);

	bool Setup(CMaxMaterial *pMaxMat);

	D3DCOLOR m_colorSide;
	D3DCOLOR m_colorFront;
	int m_nShaderEffect;
};

// Aniso, vtable 0x00028be0: falloff through the anisotropic shader.
class CAnisoMatInfo : public CMatInfo
{
public:
	CAnisoMatInfo(const TCHAR *szName, D3DCOLOR colorSide, D3DCOLOR colorFront, DWORD dwFlags = 0);

	bool Setup(CMaxMaterial *pMaxMat);

	D3DCOLOR m_colorSide;
	D3DCOLOR m_colorFront;
	int m_nShaderEffect;
};

// Icon, vtable 0x00028bd8: the current or selected title image.
class CIconMatInfo : public CMatInfo
{
public:
	CIconMatInfo(const TCHAR *szName, UINT width, UINT height, bool bFadeIn = false, bool binXIP = false, bool bSelImg = false, bool bEdgeMask = true);

	bool Setup(CMaxMaterial *pMaxMat);

	bool m_bFadeIn;
	bool m_binXIP;
	bool m_bSelImg;
	bool m_bEdgeMask;

private:
	UINT m_width, m_height;
};

// Key, vtable 0x00028bcc: bright 0x220, text 0x221. The four colors are
// Theseus's skin hook; 5960 has the palettes as constants.
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

// EggGlowPulse, vtable 0x00028b68.
class CEggGlowPulseMatInfo : public CFalloffMatInfo
{
public:
	CustomColor Color1;
	CustomColor Color2;

	CEggGlowPulseMatInfo(const TCHAR *szName, CustomColor color_a, CustomColor color_b);

	bool Setup(CMaxMaterial *pMaxMat);
};

// EggGlow, vtable 0x00028b60.
class CEggGlowFadeMatInfo : public CFalloffMatInfo
{
public:
	CustomColor Color1;
	CEggGlowFadeMatInfo(const TCHAR *szName, D3DCOLOR colorSide, CustomColor ColorA, DWORD dwFlags = 0);

	bool Setup(CMaxMaterial *pMaxMat);
};

// Backing, vtable 0x00028bf8: a mask texture tinted by R, G, B, A at 0x0c.
class CBackingMatInfo : public CMatInfo
{
public:
	CBackingMatInfo(const TCHAR *szName, BYTE r, BYTE g, BYTE b, BYTE a, DWORD dwFlags = 0);

	bool Setup(CMaxMaterial *pMaxMat);

	BYTE m_r, m_g, m_b, m_a;
};

// ModulateTexture, vtable 0x00028bec: the mesh's texture, pulsing alpha.
class CModulateTextureMatInfo : public CMatInfo
{
public:
	CModulateTextureMatInfo(const TCHAR *szName, BYTE r, BYTE g, BYTE b, BYTE a, DWORD dwFlags /*=0*/);

	bool Setup(CMaxMaterial *pMaxMat);

	BYTE m_r, m_g, m_b, m_a;
};

// Reflect, vtable 0x00028be4: the environment reflection shader.
class CReflectMatInfo : public CMatInfo
{
public:
	CReflectMatInfo(const TCHAR *szName);

	bool Setup(CMaxMaterial *pMaxMat);

	int m_nShaderEffect;
};

// MaskTexture, vtable 0x00028bdc: one of the masks on stage 1.
class CMaskTextureMatInfo : public CMatInfo
{
public:
	CMaskTextureMatInfo(const TCHAR *szName, DWORD dwFlags);

	bool Setup(CMaxMaterial *pMaxMat);
};

// FreeSpace, vtable 0x00028bd0: the memory unit fill gauge.
class CFreeSpaceMatInfo : public CMatInfo
{
public:
	CFreeSpaceMatInfo(const TCHAR *szName);

	bool Setup(CMaxMaterial *pMaxMat);
};

// InnerWall, vtable 0x00028b6c: textured falloff, both sides drawn.
class CInnerWallMatInfo : public CMatInfo
{
public:
	CInnerWallMatInfo(const TCHAR *szName, D3DCOLOR colorSide, D3DCOLOR colorFront, DWORD dwFlags = 0);

	bool Setup(CMaxMaterial *pMaxMat);

	D3DCOLOR m_colorSide;
	D3DCOLOR m_colorFront;
	int m_nShaderEffect;
};

// The material list Material_Init fills, 0x0017c6b8, count at 0x0017e754.
CMatInfo *g_rgMatInfo[240];

// The vertex shader every falloff style material starts with.
static void SetEffectShader(int nEffect)
{
	TheseusSetVertexShader(GetEffectShader(nEffect, g_pRenderMeshNode->GetFVF()));
}

// Texture factor alpha scaled by the scene's fade (0x00175ca0).
static BYTE FadedAlpha(float nAlpha)
{
	return (BYTE)(int)(nAlpha * g_nEffectAlpha);
}

// Edge AA on, multisample off: what most Setups end with. These two are
// NV2A states; desktop sets antialiasing up once with the window.
static void UseEdgeAntialias()
{
#ifdef _XBOX
	TheseusSetRenderState(D3DRS_EDGEANTIALIAS, TRUE);
	TheseusSetRenderState(D3DRS_MULTISAMPLEANTIALIAS, FALSE);
#endif
}

// CMatInfo::CMatInfo(szName, dwFlags)
// Material_Init inlines this into every material (see 0x000570c0):
//     g_rgMatInfo[g_nMatInfoCount++] = this; name = szName; flags = dwFlags;
// Theseus keeps the list from overflowing.
CMatInfo::CMatInfo(const TCHAR *szName, DWORD dwFlags /*=0*/)
{
	if (g_nMatInfoCount < (int)countof(g_rgMatInfo))
		g_rgMatInfo[g_nMatInfoCount++] = this;

	m_name = szName;
	m_flags = dwFlags;
}

// CMatInfo::Setup(pMaxMat)
// Reconstructed from 5960 retail XBE @ 0x00057650. Ghidra decompile:
//     tss[0].TEXCOORDINDEX = 0; tss[0].TEXTURETRANSFORMFLAGS = 0;
//     if (flags & 1) return 0;
//     if (flags & 2) rs.CULLMODE = NONE;
//     return 1;
// Returning false hands the mesh back to its own material.
bool CMatInfo::Setup(CMaxMaterial *pMaxMat)
{
	TheseusSetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
	TheseusSetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);

	if (m_flags & MATINFO_STANDARD_MATERIAL)
		return false;

	if (m_flags & MATINFO_CULL_NONE)
		TheseusSetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);

	return true;
}

// CSolidMatInfo
// Reconstructed from 5960 retail XBE @ 0x000576b0. Ghidra decompile:
//     tss[0]: ADDRESSV WRAP, COLOROP SELECTARG1 (TFACTOR),
//             ALPHAOP MODULATE (DIFFUSE, TFACTOR);
//     rs.TEXTUREFACTOR = ARGB((int)(a * fade), r, g, b);
//     rs.EDGEANTIALIAS = !(flags & 0x40); rs.MULTISAMPLEANTIALIAS = 0;
//     return CMatInfo::Setup();
// A flat color with the vertex alpha kept.
CSolidMatInfo::CSolidMatInfo(const TCHAR *szName, BYTE r, BYTE g, BYTE b, BYTE a, DWORD dwFlags /*=0*/) : CMatInfo(szName, dwFlags)
{
	m_r = r;
	m_g = g;
	m_b = b;
	m_a = a;
}

bool CSolidMatInfo::Setup(CMaxMaterial *pMaxMat)
{
	TheseusSetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TFACTOR);
	TheseusSetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);
	TheseusSetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
	TheseusSetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	TheseusSetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
	TheseusSetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);

	TheseusSetRenderState(D3DRS_TEXTUREFACTOR, D3DCOLOR_ARGB(FadedAlpha(m_a), m_r, m_g, m_b));
#ifdef _XBOX
	TheseusSetRenderState(D3DRS_EDGEANTIALIAS, (m_flags & MATINFO_NO_EDGE_AA) ? FALSE : TRUE);
	TheseusSetRenderState(D3DRS_MULTISAMPLEANTIALIAS, FALSE);
#endif

	return CMatInfo::Setup(pMaxMat);
}

// CFalloffMatInfo
// Material_Init builds these inline (see the Key constructor at 0x000570c0):
//     side, front, shader effect 1, wcscpy(matName, name).
// Setup reconstructed from 5960 retail XBE @ 0x00057790. Ghidra decompile:
//     if (!g_pRenderMeshNode) return 0;
//     tss[0].ADDRESSV = WRAP; SetVertexShader(GetEffectShader(effect, fvf));
//     tss[0].COLOROP = tss[0].ALPHAOP = DISABLE;
//     rs.EDGEANTIALIAS = 1; rs.MULTISAMPLEANTIALIAS = 0;
//     SetVertexShaderConstant(side), SetVertexShaderConstant(front);
//     return CMatInfo::Setup();
// The shader blends side to front by how directly a face looks at the camera.
CFalloffMatInfo::CFalloffMatInfo(const TCHAR *szName, D3DCOLOR colorSide, D3DCOLOR colorFront, DWORD dwFlags /*=0*/) : CMatInfo(szName, dwFlags)
{
	m_colorSide = colorSide;
	m_colorFront = colorFront;
	m_nShaderEffect = 1;
	_tcscpy(m_matName, szName);
}

bool CFalloffMatInfo::Setup(CMaxMaterial *pMaxMat)
{
	if (g_pRenderMeshNode == NULL)
		return false;

	TheseusSetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
	SetEffectShader(m_nShaderEffect);
	TheseusSetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_DISABLE);
	TheseusSetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	UseEdgeAntialias();
	SetFalloffShaderValues(m_colorSide, m_colorFront);

	return CMatInfo::Setup(pMaxMat);
}

// CFalloffTexInfo
// Setup reconstructed from 5960 retail XBE @ 0x00057940. Ghidra decompile:
//     if (!g_pRenderMeshNode) return 0;
//     SetVertexShader(GetEffectShader(effect 3, fvf)); falloff constants;
//     tss[0]: ADDRESSV WRAP, COLOROP and ALPHAOP MODULATE (DIFFUSE, TEXTURE);
//     tss[1]: COLOROP, ALPHAOP DISABLE;
//     return CMatInfo::Setup();
// Theseus also pins stage 0 to texture coordinate set 0.
CFalloffTexInfo::CFalloffTexInfo(const TCHAR *szName, D3DCOLOR colorSide, D3DCOLOR colorFront, DWORD dwFlags /*=0*/) : CMatInfo(szName, dwFlags)
{
	m_colorSide = colorSide;
	m_colorFront = colorFront;
	m_nShaderEffect = 3;
}

bool CFalloffTexInfo::Setup(CMaxMaterial *pMaxMat)
{
	if (g_pRenderMeshNode == NULL)
		return false;

	SetEffectShader(m_nShaderEffect);
	SetFalloffShaderValues(m_colorSide, m_colorFront);

	TheseusSetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
	TheseusSetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	TheseusSetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
	TheseusSetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	TheseusSetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	TheseusSetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_TEXTURE);
	TheseusSetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_TEXTURE);
	TheseusSetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
	TheseusSetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
	TheseusSetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);

	return CMatInfo::Setup(pMaxMat);
}

// CAnisoMatInfo
// Setup reconstructed from 5960 retail XBE @ 0x00057af0. Ghidra decompile:
//     the Falloff Setup with shader effect 2 and no edge AA writes.
CAnisoMatInfo::CAnisoMatInfo(const TCHAR *szName, D3DCOLOR colorSide, D3DCOLOR colorFront, DWORD dwFlags /*=0*/) : CMatInfo(szName, dwFlags)
{
	m_colorSide = colorSide;
	m_colorFront = colorFront;
	m_nShaderEffect = 2;
}

bool CAnisoMatInfo::Setup(CMaxMaterial *pMaxMat)
{
	if (g_pRenderMeshNode == NULL)
		return false;

	TheseusSetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
	SetEffectShader(m_nShaderEffect);
	TheseusSetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_DISABLE);
	TheseusSetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	SetFalloffShaderValues(m_colorSide, m_colorFront);

	return CMatInfo::Setup(pMaxMat);
}

// CInnerWallMatInfo
// Setup reconstructed from 5960 retail XBE @ 0x00056610. Ghidra decompile:
//     the FalloffTex Setup (effect 3), then tss[0].ADDRESSU = ADDRESSV = WRAP
//     and rs.CULLMODE = NONE before CMatInfo::Setup().
CInnerWallMatInfo::CInnerWallMatInfo(const TCHAR *szName, D3DCOLOR colorSide, D3DCOLOR colorFront, DWORD dwFlags /*=0*/) : CMatInfo(szName, dwFlags)
{
	m_colorSide = colorSide;
	m_colorFront = colorFront;
	m_nShaderEffect = 3;
}

bool CInnerWallMatInfo::Setup(CMaxMaterial *pMaxMat)
{
	if (g_pRenderMeshNode == NULL)
		return false;

	SetEffectShader(m_nShaderEffect);
	SetFalloffShaderValues(m_colorSide, m_colorFront);

	TheseusSetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
	TheseusSetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	TheseusSetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_TEXTURE);
	TheseusSetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_TEXTURE);
	TheseusSetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
	TheseusSetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
	TheseusSetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
	TheseusSetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	TheseusSetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	TheseusSetTextureStageState(0, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
	TheseusSetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
	TheseusSetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);

	return CMatInfo::Setup(pMaxMat);
}

// CFreeSpaceMatInfo
// Setup reconstructed from 5960 retail XBE @ 0x000567d0. Ghidra decompile:
//     tss[0]: COLOROP SELECTARG1 (TEXTURE), ALPHAOP MODULATE (TEXTURE, TFACTOR);
//     rs.TEXTUREFACTOR = ((int)(fade * 70) << 24) | 0xffffff;
//     tss[1].COLOROP = DISABLE; tss[1].TEXCOORDINDEX = 0;
//     m = identity; m._32 = ((1.0 - param) * 2 - 1.0) * 0.3 + 0.1;
//     device texture0 transform = m;
//     tss[0].TEXTURETRANSFORMFLAGS = COUNT2; tss[0].ADDRESSV = CLAMP;
//     return 1;
// param is how full the unit is; the transform slides the gauge texture up
// the bar. It never calls CMatInfo::Setup.
CFreeSpaceMatInfo::CFreeSpaceMatInfo(const TCHAR *szName) : CMatInfo(szName)
{
}

bool CFreeSpaceMatInfo::Setup(CMaxMaterial *pMaxMat)
{
	TheseusSetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	TheseusSetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	TheseusSetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
	TheseusSetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	TheseusSetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);
	TheseusSetRenderState(D3DRS_TEXTUREFACTOR, D3DCOLOR_ARGB(FadedAlpha(70.0f), 255, 255, 255));
	TheseusSetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	TheseusSetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 0);

	float nShift = (float)((1.0 - pMaxMat->m_param) * 2.0 - 1.0);
	D3DXMATRIX slide;
	D3DXMatrixIdentity(&slide);
	slide._32 = nShift * 0.3f + 0.1f;
	TheseusSetTransform(D3DTS_TEXTURE0, &slide);

	TheseusSetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
	TheseusSetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);

	return true;
}

// Mask choice shared by MaskTexture and Backing: the first of the four mask
// flags that's set, or no change to the stage.
static LPDIRECT3DTEXTURE8 MaskForFlags(DWORD dwFlags)
{
	if (dwFlags & MATINFO_RADIAL_ALPHA)
		return GetRadialAlphaMask();
	if (dwFlags & MATINFO_RADIAL_EDGE_ALPHA)
		return GetRadialEdgeAlphaMask();
	if (dwFlags & MATINFO_RADIAL_EDGE_MAIN_ALPHA)
		return GetRadialEdgeAlphaMainMask();
	if (dwFlags & MATINFO_RADIAL_AV_ALPHA)
		return GetRadialAVAlphaMask();
	return NULL;
}

// CMaskTextureMatInfo
// Setup reconstructed from 5960 retail XBE @ 0x00056940. Ghidra decompile:
//     if (mask for flags) SetTexture(1, mask);
//     tss[0].ADDRESSV = WRAP;
//     tss[1]: ALPHAOP MODULATE (CURRENT, TEXTURE), COLOROP ADD (CURRENT, TEXTURE),
//             TEXCOORDINDEX 0;
//     tss[2]: COLOROP SELECTARG1 (CURRENT), ALPHAOP MODULATE (CURRENT, TFACTOR);
//     rs.TEXTUREFACTOR = (int)(fade * 255) << 24;
//     CMatInfo::Setup();
// The mesh texture on stage 0, cut by the round mask, faded on stage 2.
CMaskTextureMatInfo::CMaskTextureMatInfo(const TCHAR *szName, DWORD dwFlags) : CMatInfo(szName, dwFlags)
{
}

bool CMaskTextureMatInfo::Setup(CMaxMaterial *pMaxMat)
{
	LPDIRECT3DTEXTURE8 pMask = MaskForFlags(m_flags);
	if (pMask != NULL)
		TheseusSetTexture(1, pMask);

	TheseusSetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
	TheseusSetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
	TheseusSetTextureStageState(1, D3DTSS_ALPHAARG1, D3DTA_CURRENT);
	TheseusSetTextureStageState(1, D3DTSS_ALPHAARG2, D3DTA_TEXTURE);
	TheseusSetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_ADD);
	TheseusSetTextureStageState(1, D3DTSS_COLORARG1, D3DTA_CURRENT);
	TheseusSetTextureStageState(1, D3DTSS_COLORARG2, D3DTA_TEXTURE);
	TheseusSetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 0);
	TheseusSetTextureStageState(2, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	TheseusSetTextureStageState(2, D3DTSS_COLORARG2, D3DTA_TEXTURE);
	TheseusSetTextureStageState(2, D3DTSS_COLORARG1, D3DTA_CURRENT);
	TheseusSetTextureStageState(2, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
	TheseusSetTextureStageState(2, D3DTSS_ALPHAARG1, D3DTA_CURRENT);
	TheseusSetTextureStageState(2, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);
	TheseusSetRenderState(D3DRS_TEXTUREFACTOR, D3DCOLOR_ARGB(FadedAlpha(255.0f), 0, 0, 0));

	return CMatInfo::Setup(pMaxMat);
}

// One pulse: 1 at the start of each two second cycle, dipping to 0 halfway.
// Both pulsing materials use it: 1 - |sin((now - start) * 0.5 * PI)|.
static float PulseLevel()
{
	float nCycle = (float)((TheseusGetNow() - g_pulseStartTime) * 0.5);
	return 1.0f - fabsf(sinf(nCycle * D3DX_PI));
}

// CModulateTextureMatInfo
// Setup reconstructed from 5960 retail XBE @ 0x00056c70. Ghidra decompile:
//     tss[0]: ADDRESSV WRAP, COLOROP SELECTARG1 (TEXTURE), ALPHAOP SELECTARG1 (TFACTOR);
//     if (g_pLastPulse != mat && fade == 1.0) { g_pLastPulse = mat; start = now; }
//     a = ((1 - |sin((now - start) * 0.5 * PI)|) * 0.25 + 0.75) * fade * alpha;
//     rs.TEXTUREFACTOR = ARGB((int)a, r, g, b);
//     rs.EDGEANTIALIAS = 1; rs.MULTISAMPLEANTIALIAS = 0;
//     CMatInfo::Setup();
// The pulse restarts each time a different material starts using it, once
// the scene has finished fading in.
CModulateTextureMatInfo::CModulateTextureMatInfo(const TCHAR *szName, BYTE r, BYTE g, BYTE b, BYTE a, DWORD dwFlags /*=0*/) : CMatInfo(szName, dwFlags)
{
	m_r = r;
	m_g = g;
	m_b = b;
	m_a = a;
}

bool CModulateTextureMatInfo::Setup(CMaxMaterial *pMaxMat)
{
	TheseusSetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	TheseusSetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	TheseusSetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	TheseusSetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
	TheseusSetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TFACTOR);

	if (pMaxMat != g_pLastPulseMaxMat && g_nEffectAlpha == 1.0f)
	{
		g_pLastPulseMaxMat = pMaxMat;
		g_pulseStartTime = TheseusGetNow();
	}

	float nAlpha = (float)m_a * (PulseLevel() * 0.25f + 0.75f) * g_nEffectAlpha;
	TheseusSetRenderState(D3DRS_TEXTUREFACTOR, D3DCOLOR_ARGB((BYTE)(int)nAlpha, m_r, m_g, m_b));
	UseEdgeAntialias();

	return CMatInfo::Setup(pMaxMat);
}

// CReflectMatInfo
// Setup reconstructed from 5960 retail XBE @ 0x00056d90. Ghidra decompile:
//     if (!g_pRenderMeshNode) return 0;
//     SetVertexShader(GetEffectShader(effect 4, fvf));
//     falloff constants side (229, 229, 229, 0), front (229, 229, 229, 255);
//     SetReflectShaderFrameValues();
//     tss[0]: ADDRESSU, ADDRESSV WRAP, COLOROP MODULATE (TEXTURE, DIFFUSE),
//             ALPHAOP SELECTARG2 (DIFFUSE);
//     return CMatInfo::Setup();
CReflectMatInfo::CReflectMatInfo(const TCHAR *szName) : CMatInfo(szName, 0)
{
	m_nShaderEffect = 4;
}

bool CReflectMatInfo::Setup(CMaxMaterial *pMaxMat)
{
	if (g_pRenderMeshNode == NULL)
		return false;

	SetEffectShader(m_nShaderEffect);
	SetFalloffShaderValues(D3DCOLOR_ARGB(0, 229, 229, 229), D3DCOLOR_ARGB(255, 229, 229, 229));
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

// CBackingMatInfo
// Setup reconstructed from 5960 retail XBE @ 0x00056ea0. Ghidra decompile:
//     if (mask for flags) SetTexture(0, mask);
//     tss[0]: ADDRESSV WRAP, COLOROP SELECTARG1 (TFACTOR),
//             ALPHAOP MODULATE (TEXTURE, TFACTOR);
//     rs.TEXTUREFACTOR = ARGB((int)(a * fade), r, g, b);
//     CMatInfo::Setup();
// A colored panel whose shape comes from the mask's alpha.
CBackingMatInfo::CBackingMatInfo(const TCHAR *szName, BYTE r, BYTE g, BYTE b, BYTE a, DWORD dwFlags /*=0*/) : CMatInfo(szName, dwFlags)
{
	m_r = r;
	m_g = g;
	m_b = b;
	m_a = a;
}

bool CBackingMatInfo::Setup(CMaxMaterial *pMaxMat)
{
	LPDIRECT3DTEXTURE8 pMask = MaskForFlags(m_flags);
	if (pMask != NULL)
		TheseusSetTexture(0, pMask);

	TheseusSetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	TheseusSetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	TheseusSetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);
	TheseusSetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TFACTOR);
	TheseusSetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
	TheseusSetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
	TheseusSetRenderState(D3DRS_TEXTUREFACTOR, D3DCOLOR_ARGB(FadedAlpha(m_a), m_r, m_g, m_b));

	return CMatInfo::Setup(pMaxMat);
}

// CEggGlowPulseMatInfo
// Setup reconstructed from 5960 retail XBE @ 0x00056f80. Ghidra decompile:
//     if (g_pLastPulse != mat) { g_pLastPulse = mat; start = 2.0; }
//     side = 0x00b2d000;
//     front = ((int)((1 - |sin((now - start) * 0.5 * PI)|) * 128 + 64) << 24) | 0xffff80;
//     CFalloffMatInfo::Setup();
// Front alpha swings 64..192 every two seconds. Theseus takes the side and
// front RGB (and side alpha) from Color1 / Color2 so skins can recolor it;
// retail values are (178, 208, 0, 0) and (255, 255, 128).
CEggGlowPulseMatInfo::CEggGlowPulseMatInfo(const TCHAR *szName, CustomColor color_a, CustomColor color_b) : CFalloffMatInfo(szName, 0, D3DCOLOR_RGBA(0, 0, 0, 0), D3DCOLOR_RGBA(0, 0, 0, 0))
{
	Color1 = color_a;
	Color2 = color_b;
}

bool CEggGlowPulseMatInfo::Setup(CMaxMaterial *pMaxMat)
{
	if (pMaxMat != g_pLastPulseMaxMat)
	{
		g_pLastPulseMaxMat = pMaxMat;
		g_pulseStartTime = 2.0;
	}

	int nFrontAlpha = (int)(PulseLevel() * 128.0f + 64.0f);
	m_colorSide = D3DCOLOR_RGBA(Color1.r, Color1.g, Color1.b, Color1.a);
	m_colorFront = D3DCOLOR_RGBA(Color2.r, Color2.g, Color2.b, nFrontAlpha);

	return CFalloffMatInfo::Setup(pMaxMat);
}

// CEggGlowFadeMatInfo
// Setup reconstructed from 5960 retail XBE @ 0x00057000. Ghidra decompile:
//     if (mat->param == 1.0) mat->param = now;          // compared bit for bit
//     a = 0;
//     if (0.0 < mat->param) {
//         t = (now - mat->param) * 0.8;
//         if (1.0 <= t) { t = 1.0; mat->param = 0.0; }
//         a = 1.0 - t;
//     }
//     front = ((int)(a * 255) << 24) | 0xfeffbc;
//     CFalloffMatInfo::Setup();
// Script sets param to 1 to fire the glow; it fades out over 1.25 seconds.
// Theseus takes the front RGB from Color1 (retail 254, 255, 188).
CEggGlowFadeMatInfo::CEggGlowFadeMatInfo(const TCHAR *szName, D3DCOLOR colorSide, CustomColor ColorA, DWORD dwFlags) : CFalloffMatInfo(szName, dwFlags, colorSide, D3DCOLOR_RGBA(ColorA.r, ColorA.g, ColorA.b, ColorA.a))
{
	Color1 = ColorA;
}

bool CEggGlowFadeMatInfo::Setup(CMaxMaterial *pMaxMat)
{
	if (pMaxMat->m_param == 1.0)
		pMaxMat->m_param = TheseusGetNow();

	float nLeft = 0.0f;
	if (pMaxMat->m_param > 0.0)
	{
		float t = (float)((TheseusGetNow() - pMaxMat->m_param) * 0.8);
		if (t >= 1.0f)
		{
			t = 1.0f;
			pMaxMat->m_param = 0.0;
		}
		nLeft = 1.0f - t;
	}

	m_colorFront = D3DCOLOR_RGBA(Color1.r, Color1.g, Color1.b, (int)(nLeft * 255.0f));

	return CFalloffMatInfo::Setup(pMaxMat);
}

// CKeyMatInfo
// Constructor reconstructed from 5960 retail XBE @ 0x000570c0: the Falloff
// fields (effect 1, both colors 0, name copied), then bright and text.
// Setup reconstructed from 5960 retail XBE @ 0x00057120. Ghidra decompile:
//     if (!g_bActiveKey) {
//         bright: side 0xfff3ff6b, front 0x2219e100;
//         else:   side 0xdef3ff6b, front 0x0f14c000;
//         if (text) { stage 0 = SELECTARG1 (TFACTOR) color, MODULATE
//                     (DIFFUSE, TFACTOR) alpha, ADDRESSV WRAP;
//                     rs.TEXTUREFACTOR = ((int)(fade * 178) << 24) | 0xbefa5e; }
//     } else {
//         if (g_pLastPulse != mat && 0 < fade) { g_pLastPulse = mat; start = now; }
//         side 0xffb2d000; front 0xbeb2d000;
//         if (text) { same stage 0; rs.TEXTUREFACTOR = (int)(fade * 192) << 24; }
//     }
//     rs.EDGEANTIALIAS = 1; rs.MULTISAMPLEANTIALIAS = 0;
//     CFalloffMatInfo::Setup();
// g_bActiveKey picks the Live (darker) palette. Theseus builds the same
// colors from the skin's four Keyboard colors (retail values give exactly
// the constants above).
CKeyMatInfo::CKeyMatInfo(const TCHAR *szName, bool bBright, bool bText, CustomColor ColorA, CustomColor ColorB, CustomColor ColorC, CustomColor ColorD) : CFalloffMatInfo(szName, D3DCOLOR_RGBA(0, 0, 0, 0), D3DCOLOR_RGBA(0, 0, 0, 0), 0)
{
	Color1 = ColorA;
	Color2 = ColorB;
	Color3 = ColorC;
	Color4 = ColorD;
	m_bBright = bBright;
	m_bText = bText;
}

// Text keys draw the glyph from the factor color, faded by the vertex alpha.
static void KeyTextStage(D3DCOLOR factor)
{
	TheseusSetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
	TheseusSetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	TheseusSetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TFACTOR);
	TheseusSetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
	TheseusSetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
	TheseusSetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);
	TheseusSetRenderState(D3DRS_TEXTUREFACTOR, factor);
}

bool CKeyMatInfo::Setup(CMaxMaterial *pMaxMat)
{
	if (g_bActiveKey)
	{
		if (pMaxMat != g_pLastPulseMaxMat && g_nEffectAlpha > 0.0f)
		{
			g_pLastPulseMaxMat = pMaxMat;
			g_pulseStartTime = TheseusGetNow();
		}

		m_colorSide = D3DCOLOR_RGBA(Color1.r, Color1.g, Color1.b, Color1.a);
		m_colorFront = D3DCOLOR_RGBA(Color1.r, Color1.g, Color1.b, Color1.a - 65);

		if (m_bText)
			KeyTextStage(D3DCOLOR_ARGB(FadedAlpha(192.0f), 0, 0, 0));
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
			m_colorSide = D3DCOLOR_RGBA(Color2.r, Color2.g, Color2.b, Color2.a - 33);
			m_colorFront = D3DCOLOR_RGBA(Color3.r - 5, Color3.g - 33, Color3.b, Color3.a - 19);
		}

		if (m_bText)
			KeyTextStage(D3DCOLOR_ARGB(FadedAlpha(178.0f), Color4.r, Color4.g, Color4.b));
	}

	UseEdgeAntialias();

	return CFalloffMatInfo::Setup(pMaxMat);
}

// CIconMatInfo
// Setup reconstructed from 5960 retail XBE @ 0x000572d0. Ghidra decompile:
//     inXIP = this->inXIP;
//     if (g_szCurTitleImage && !selImg) {
//         if (name is xboxlogo64.xbx / xboxlogo128.xbx / xboxlogo64a.xbx) inXIP = 1;
//         tex = FindTexture(name, async, w, h, inXIP);
//     } else if (g_szSelTitleImage && selImg) {
//         if (name is xboxlogo64.xbx / xboxlogo128.xbx /
//             XboxLiveAccount64.tga / XboxLiveAccount128.tga) inXIP = 1;
//         tex = FindTexture(...);
//     }
//     a = 1; if (tex && fadeIn) a = min((now - loaded) * 4.0, 1.0);
//     if (tex) { rs: CULLMODE NONE, ZWRITE 0, ALPHABLEND 1, SRCALPHA / INVSRCALPHA;
//                SetTexture(0, tex); tss[0]: TEXCOORDINDEX 0, TTFF 0,
//                COLOROP SELECTARG1 (TEXTURE), ALPHAOP MODULATE (TEXTURE, TFACTOR);
//                rs.TEXTUREFACTOR = (int)(a * fade * 255) << 24; }
//     else { tss[0] COLOROP and ALPHAOP SELECTARG1 (TFACTOR); rs.TEXTUREFACTOR = 0; }
//     if (this->edgeMask) { SetTexture(1, GetRadialEdgeAlphaMask()); tss[1]:
//         TEXCOORDINDEX 0, ALPHAOP MODULATE (CURRENT, TEXTURE), COLOROP ADD (CURRENT, TEXTURE); }
//     return 1;
// Without an image the quad draws clear. The edge mask byte (0x0f) is set
// on every retail icon but MotdIcon.
CIconMatInfo::CIconMatInfo(const TCHAR *szName, UINT width, UINT height, bool bFadeIn /*=false*/, bool binXIP /*=false*/, bool bSelImg, bool bEdgeMask) : CMatInfo(szName)
{
	m_bFadeIn = bFadeIn;
	m_binXIP = binXIP;
	m_bSelImg = bSelImg;
	m_bEdgeMask = bEdgeMask;
	m_width = width;
	m_height = height;
}

// The stock logos ship inside the dashboard's XIPs rather than on disk; the
// two lists differ by which icon asks.
static bool IsStockLogo(const TCHAR *szImage, bool bSelected)
{
	if (_tcsicmp(szImage, _T("xboxlogo64.xbx")) == 0 ||
		_tcsicmp(szImage, _T("xboxlogo128.xbx")) == 0)
		return true;

	if (!bSelected)
		return _tcsicmp(szImage, _T("xboxlogo64a.xbx")) == 0;

	return _tcsicmp(szImage, _T("XboxLiveAccount64.tga")) == 0 ||
		_tcsicmp(szImage, _T("XboxLiveAccount128.tga")) == 0;
}

bool CIconMatInfo::Setup(CMaxMaterial *pMaxMat)
{
	const TCHAR *szImage = NULL;
	if (!m_bSelImg)
		szImage = g_szCurTitleImage;
	else
		szImage = g_szSelTitleImage;

	LPDIRECT3DTEXTURE8 pImage = NULL;
	float nAlpha = 1.0f;

	if (szImage != NULL)
	{
		bool bInXIP = m_binXIP || IsStockLogo(szImage, m_bSelImg);

		XTIME timeLoaded = 0.0;
		pImage = GetTexture(szImage, &timeLoaded, m_width, m_height, bInXIP);
		if (pImage != NULL && m_bFadeIn)
		{
			nAlpha = (float)(TheseusGetNow() - timeLoaded) * 4.0f;
			if (nAlpha > 1.0f)
				nAlpha = 1.0f;
		}
	}

	if (pImage != NULL)
	{
		TheseusSetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
		TheseusSetRenderState(D3DRS_ZWRITEENABLE, FALSE);
		TheseusSetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
		TheseusSetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		TheseusSetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

		TheseusSetTexture(0, pImage);
		TheseusSetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
		TheseusSetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
		TheseusSetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
		TheseusSetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		TheseusSetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
		TheseusSetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		TheseusSetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);
		TheseusSetRenderState(D3DRS_TEXTUREFACTOR, D3DCOLOR_COLORVALUE(0.0f, 0.0f, 0.0f, nAlpha * g_nEffectAlpha));
	}
	else
	{
		TheseusSetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
		TheseusSetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TFACTOR);
		TheseusSetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
		TheseusSetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TFACTOR);
		TheseusSetRenderState(D3DRS_TEXTUREFACTOR, 0);
	}

	if (!m_bEdgeMask)
		return true;

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

// Material lookup. The list is sorted by name once Material_Init has built
// it (qsort with the comparator at 0x00057c70) and CMaxMaterial::Render
// bsearches it (comparator 0x00057c90): plain case sensitive name order.
static int __cdecl SortMatInfoCompare(const void *elem1, const void *elem2)
{
	const CMatInfo *pLeft = *(const CMatInfo *const *)elem1;
	const CMatInfo *pRight = *(const CMatInfo *const *)elem2;
	return _tcscmp(pLeft->m_name, pRight->m_name);
}

static int __cdecl SearchMatInfoCompare(const void *elem1, const void *elem2)
{
	const CMatInfo *pEntry = *(const CMatInfo *const *)elem2;
	return _tcscmp((const TCHAR *)elem1, pEntry->m_name);
}

CMatInfo *LookupMatInfo(const TCHAR *szName)
{
	CMatInfo **ppFound = (CMatInfo **)bsearch(szName, g_rgMatInfo, g_nMatInfoCount, sizeof (CMatInfo *), SearchMatInfoCompare);
	return (ppFound != NULL) ? *ppFound : NULL;
}

// Public introspection / live edit API used by the Skin Editor. Desktop only:
// the dashboard never edits materials at runtime, and dynamic_cast here is the
// only thing that would force RTTI into the Xbox build.
#ifndef _XBOX
int MatInfo_ColorCount(CMatInfo* p)
{
	if (!p) return 0;
	// Two-color types (falloff + everything that inherits it) first; some
	// of these are also CInnerWall etc., which are independent two-color
	// types, so check those separately.
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
	// The egg glows rebuild their falloff colors every frame from these, so
	// that's where the skin's ColorA / ColorB actually live.
	if (CEggGlowPulseMatInfo* m = dynamic_cast<CEggGlowPulseMatInfo*>(p)) {
		const CustomColor& c = (idx == 0) ? m->Color1 : m->Color2;
		return D3DCOLOR_RGBA(c.r, c.g, c.b, c.a);
	}
	if (CEggGlowFadeMatInfo* m = dynamic_cast<CEggGlowFadeMatInfo*>(p))
		return (idx == 0) ? m->m_colorSide : D3DCOLOR_RGBA(m->Color1.r, m->Color1.g, m->Color1.b, m->Color1.a);
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
	CustomColor cc;
	cc.r = r; cc.g = g; cc.b = b; cc.a = a;
	if (CEggGlowPulseMatInfo* m = dynamic_cast<CEggGlowPulseMatInfo*>(p)) {
		if (idx == 0) m->Color1 = cc; else m->Color2 = cc;
		return;
	}
	if (CEggGlowFadeMatInfo* m = dynamic_cast<CEggGlowFadeMatInfo*>(p)) {
		if (idx == 0) m->m_colorSide = c; else m->Color1 = cc;
		return;
	}
	if (CInnerWallMatInfo* m = dynamic_cast<CInnerWallMatInfo*>(p)) {
		if (idx == 0) m->m_colorSide = c; else m->m_colorFront = c;
		return;
	}
	if (CFalloffMatInfo* m = dynamic_cast<CFalloffMatInfo*>(p)) {
		if (idx == 0) m->m_colorSide = c; else m->m_colorFront = c;
		return;
	}
}
#endif // !_XBOX

#include "materials_retail.inl"

IMPLEMENT_NODE("MaxMaterial", CMaxMaterial, CMaterial)

START_NODE_PROPS(CMaxMaterial, CMaterial)
NODE_PROP(pt_string, CMaxMaterial, name)
NODE_PROP(pt_number, CMaxMaterial, param)
END_NODE_PROPS()

// CMaxMaterial::CMaxMaterial() / ~CMaxMaterial()
// Reconstructed from 5960 retail XBE @ 0x0005a020 and 0x0005a0d0:
//     <Material fields>; name = NULL; param = 0.0; matInfo = NULL;
//     ...   free(name); then the node destructor.
CMaxMaterial::CMaxMaterial()
{
	m_name = NULL;
	m_param = 0.0;
	m_pMatInfo = NULL;
}

CMaxMaterial::~CMaxMaterial()
{
	delete [] m_name;
}

// CMaxMaterial::OnSetProperty(pprd, pvValue)
// Reconstructed from 5960 retail XBE @ 0x0005a0f0. Ghidra decompile:
//     if (prd->offset == 0x90) matInfo = NULL;               // looked up again
//     else if (prd->offset == 0x98) { param = (double)*(float*)v; return 0; }
//     return 1;
bool CMaxMaterial::OnSetProperty(const PRD *pprd, const void *pvValue)
{
	int field = PTR2INT(pprd->pbOffset);

	if (field == offsetof(m_param))
	{
		m_param = *(const float *)pvValue;
		return false;
	}

	if (field == offsetof(m_name))
		m_pMatInfo = NULL;

	return true;
}

// CMaxMaterial::Render()
// Reconstructed from 5960 retail XBE @ 0x0005a130. Ghidra decompile:
//     if (!matInfo) { if (name) matInfo = bsearch(name, g_rgMatInfo, ...);
//                     if (!matInfo) return; }
//     rs.EDGEANTIALIAS = 0; rs.MULTISAMPLEANTIALIAS = 1;
//     g_bEdgeAntialiasOverride = name is "Tubes", "TubesFade", "TubesQ" or "Tube";
//     matInfo->Setup(this);
// Multisample is the default; the Setups that want hard edges switch to edge
// AA. The tube meshes get the override so their outlines keep edge AA.
void CMaxMaterial::Render()
{
	if (m_pMatInfo == NULL)
	{
		if (m_name != NULL)
			m_pMatInfo = LookupMatInfo(m_name);
		if (m_pMatInfo == NULL)
			return;
	}

#ifdef _XBOX
	TheseusSetRenderState(D3DRS_EDGEANTIALIAS, FALSE);
	TheseusSetRenderState(D3DRS_MULTISAMPLEANTIALIAS, TRUE);
#endif

	g_bEdgeAntialiasOverride =
		_tcscmp(m_name, _T("Tubes")) == 0 ||
		_tcscmp(m_name, _T("TubesFade")) == 0 ||
		_tcscmp(m_name, _T("TubesQ")) == 0 ||
		_tcscmp(m_name, _T("Tube")) == 0;

	m_pMatInfo->Setup(this);

#ifdef THESEUS_DESKTOP
	// The skin editor highlights the materials drawn this frame.
	extern IDirect3DDevice8* g_pD3DDev;
	if (g_pD3DDev != NULL)
		m_pMatInfo->m_lastUsedFrame = g_pD3DDev->m_frameNumber;
#endif
}
