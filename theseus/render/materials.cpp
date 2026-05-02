// materials.cpp: CMaxMaterial (3ds Max-exported material) + the family of
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



void GenerateRadialAlphaMask(BYTE *pbPels, int nPitch, int nWidth, int nHeight, float nFactor, float nMax, float nScale = 1.0f)
{
	LPDWORD pdwPels = (LPDWORD)pbPels;
	int nRadialWidth = (int)((float)nWidth * nScale);
	int nRadialHeight = (int)((float)nHeight * nScale);
	int x, y;

	for (y = 0; y < nRadialHeight; y += 1)
	{
		for (x = 0; x < nRadialWidth; x += 1)
		{
			float cx = (float)(x - nRadialWidth / 2) / (nRadialWidth / 2);
			float cy = (float)(y - nRadialHeight / 2) / (nRadialHeight / 2);
			float d = sqrtf(cx * cx + cy * cy);

			if (d < 0.0f)
				d = 0.0f;
			else if (d > 1.0f)
				d = 1.0f;

			float a = 1.0f - d;

			a = a * nFactor;

			if (a < 0.0f)
				a = 0.0f;
			else if (a > nMax)
				a = nMax;

			pdwPels[x] = ((BYTE)(a * 255.0f)) << 24;
		}

		for (; x < nWidth; x += 1)
		{
			pdwPels[x] = 0;
		}

		pdwPels += (nPitch >> 2);
	}

	for (; y < nHeight; y += 1)
	{
		memset(pdwPels, 0, nWidth << 2);
		pdwPels += (nPitch >> 2);
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

	// Edge Aliasing mode enable
	TheseusSetRenderState(D3DRS_EDGEANTIALIAS, TRUE);
	TheseusSetRenderState(D3DRS_MULTISAMPLEANTIALIAS, FALSE);

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
			m_colorSide = D3DCOLOR_RGBA(Color2.r, Color2.g, Color2.b, (Color2.a - 3));
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

	TheseusSetRenderState(D3DRS_EDGEANTIALIAS, TRUE);
	TheseusSetRenderState(D3DRS_MULTISAMPLEANTIALIAS, FALSE);

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

	// Edge Aliasing mode enable
	TheseusSetRenderState(D3DRS_EDGEANTIALIAS, TRUE);
	TheseusSetRenderState(D3DRS_MULTISAMPLEANTIALIAS, FALSE);

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

	// Edge Aliasing mode enable
	TheseusSetRenderState(D3DRS_EDGEANTIALIAS, TRUE);
	TheseusSetRenderState(D3DRS_MULTISAMPLEANTIALIAS, FALSE);

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

void GetRGBAValues(TCHAR *dataIn, CustomColor *color)
{
	int tempR, tempG, tempB, tempA = 0;

	_stscanf(dataIn, _T("%d, %d, %d, %d"), &tempR, &tempG, &tempB, &tempA);

	color->r = tempR;
	color->g = tempG;
	color->b = tempB;
	color->a = tempA;
}

// Helpers for updating existing material colors on skin reload
static void UpdateSolidColor(CMatInfo *p, BYTE r, BYTE g, BYTE b, BYTE a)
{
	CSolidMatInfo *pMat = static_cast<CSolidMatInfo *>(p);
	pMat->m_r = r; pMat->m_g = g; pMat->m_b = b; pMat->m_a = a;
}

static void UpdateBackingColor(CMatInfo *p, BYTE r, BYTE g, BYTE b, BYTE a)
{
	CBackingMatInfo *pMat = static_cast<CBackingMatInfo *>(p);
	pMat->m_r = r; pMat->m_g = g; pMat->m_b = b; pMat->m_a = a;
}

static void UpdateModulateColor(CMatInfo *p, BYTE r, BYTE g, BYTE b, BYTE a)
{
	CModulateTextureMatInfo *pMat = static_cast<CModulateTextureMatInfo *>(p);
	pMat->m_r = r; pMat->m_g = g; pMat->m_b = b; pMat->m_a = a;
}

static void UpdateFalloffColor(CMatInfo *p, D3DCOLOR colorSide, D3DCOLOR colorFront)
{
	CFalloffMatInfo *pMat = static_cast<CFalloffMatInfo *>(p);
	pMat->m_colorSide = colorSide;
	pMat->m_colorFront = colorFront;
}

// Reads a single-color material from the skin file and updates in place
static void ReloadSolidMat(CSettingsFile &SkinXBX, const TCHAR *szSection, CustomColor *tmp)
{
	CMatInfo *p = LookupMatInfo(szSection);
	if (!p) return;
	TCHAR buf[MAX_PATH];
	SkinXBX.GetValue(szSection, _T("Color"), buf, MAX_PATH);
	GetRGBAValues(buf, tmp);
	UpdateSolidColor(p, (BYTE)tmp->r, (BYTE)tmp->g, (BYTE)tmp->b, (BYTE)tmp->a);
}

static void ReloadBackingMat(CSettingsFile &SkinXBX, const TCHAR *szSection, CustomColor *tmp)
{
	CMatInfo *p = LookupMatInfo(szSection);
	if (!p) return;
	TCHAR buf[MAX_PATH];
	SkinXBX.GetValue(szSection, _T("Color"), buf, MAX_PATH);
	GetRGBAValues(buf, tmp);
	UpdateBackingColor(p, (BYTE)tmp->r, (BYTE)tmp->g, (BYTE)tmp->b, (BYTE)tmp->a);
}

static void ReloadModulateMat(CSettingsFile &SkinXBX, const TCHAR *szSection, CustomColor *tmp)
{
	CMatInfo *p = LookupMatInfo(szSection);
	if (!p) return;
	TCHAR buf[MAX_PATH];
	SkinXBX.GetValue(szSection, _T("Color"), buf, MAX_PATH);
	GetRGBAValues(buf, tmp);
	UpdateModulateColor(p, (BYTE)tmp->r, (BYTE)tmp->g, (BYTE)tmp->b, (BYTE)tmp->a);
}

static void ReloadFalloffMat(CSettingsFile &SkinXBX, const TCHAR *szSection, CustomColor *tmpA, CustomColor *tmpB)
{
	CMatInfo *p = LookupMatInfo(szSection);
	if (!p) return;
	TCHAR bufA[MAX_PATH], bufB[MAX_PATH];
	SkinXBX.GetValue(szSection, _T("ColorA"), bufA, MAX_PATH);
	SkinXBX.GetValue(szSection, _T("ColorB"), bufB, MAX_PATH);
	GetRGBAValues(bufA, tmpA);
	GetRGBAValues(bufB, tmpB);
	UpdateFalloffColor(p,
		D3DCOLOR_RGBA(tmpA->r, tmpA->g, tmpA->b, tmpA->a),
		D3DCOLOR_RGBA(tmpB->r, tmpB->g, tmpB->b, tmpB->a));
}

// Reload path: same as init but updates existing materials in place
static void Material_Reload()
{
	if (!TheseusGetSkinSettings())
		return;

	CSettingsFile &SkinXBX = *TheseusGetSkinSettings();
	CustomColor tmpA, tmpB, tmpC, tmpD;

	// InnerWall_01 (CInnerWallMatInfo has same color layout as CFalloffMatInfo)
	{
		CMatInfo *p = LookupMatInfo(_T("InnerWall_01"));
		if (p)
		{
			TCHAR bufA[MAX_PATH], bufB[MAX_PATH];
			SkinXBX.GetValue(_T("InnerWall_01"), _T("ColorA"), bufA, MAX_PATH);
			SkinXBX.GetValue(_T("InnerWall_01"), _T("ColorB"), bufB, MAX_PATH);
			GetRGBAValues(bufA, &tmpA);
			GetRGBAValues(bufB, &tmpB);
			CInnerWallMatInfo *pIW = static_cast<CInnerWallMatInfo *>(p);
			pIW->m_colorSide = D3DCOLOR_RGBA(tmpA.r, tmpA.g, tmpA.b, tmpA.a);
			pIW->m_colorFront = D3DCOLOR_RGBA(tmpB.r, tmpB.g, tmpB.b, tmpB.a);
		}
	}

	// InnerWall_02
	ReloadFalloffMat(SkinXBX, _T("InnerWall_02"), &tmpA, &tmpB);

	// Single-color solids
	ReloadSolidMat(SkinXBX, _T("Material #1334"), &tmpA);
	ReloadSolidMat(SkinXBX, _T("XBOXgreendark"), &tmpA);
	ReloadSolidMat(SkinXBX, _T("XBOXgreen"), &tmpA);
	ReloadSolidMat(SkinXBX, _T("XBoxGreen2"), &tmpA);
	ReloadSolidMat(SkinXBX, _T("GameHilite33"), &tmpA);
	ReloadSolidMat(SkinXBX, _T("Nothing"), &tmpA);
	ReloadSolidMat(SkinXBX, _T("NavType"), &tmpA);
	ReloadSolidMat(SkinXBX, _T("RedType"), &tmpA);
	ReloadSolidMat(SkinXBX, _T("XBoxGreen"), &tmpA);
	ReloadSolidMat(SkinXBX, _T("Type"), &tmpA);
	ReloadSolidMat(SkinXBX, _T("Typesdsafsda"), &tmpA);
	ReloadSolidMat(SkinXBX, _T("Material #133"), &tmpA);
	ReloadSolidMat(SkinXBX, _T("Material #1335"), &tmpA);
	ReloadSolidMat(SkinXBX, _T("Material #133511"), &tmpA);
	ReloadSolidMat(SkinXBX, _T("Material #1336"), &tmpA);
	ReloadSolidMat(SkinXBX, _T("HilightedType"), &tmpA);
	ReloadSolidMat(SkinXBX, _T("XBoxGreenq"), &tmpA);
	ReloadSolidMat(SkinXBX, _T("Black80"), &tmpA);
	ReloadSolidMat(SkinXBX, _T("CellEgg/Partsw"), &tmpA);
	ReloadSolidMat(SkinXBX, _T("CellEgg/Partsz"), &tmpA);
	ReloadSolidMat(SkinXBX, _T("Material #108"), &tmpA);
	ReloadSolidMat(SkinXBX, _T("ItemsType"), &tmpA);
	ReloadSolidMat(SkinXBX, _T("GameHiliteMemory"), &tmpA);
	ReloadSolidMat(SkinXBX, _T("red"), &tmpA);

	// Falloff materials (ColorA/ColorB)
	ReloadFalloffMat(SkinXBX, _T("FlatSurfaces"), &tmpA, &tmpB);
	ReloadFalloffMat(SkinXBX, _T("FlatSurfacesSelected"), &tmpA, &tmpB);
	ReloadFalloffMat(SkinXBX, _T("FlatSurfacesMemory"), &tmpA, &tmpB);
	ReloadFalloffMat(SkinXBX, _T("DarkSurfaces"), &tmpA, &tmpB);
	ReloadFalloffMat(SkinXBX, _T("DarkSurfaces2"), &tmpA, &tmpB);
	ReloadFalloffMat(SkinXBX, _T("FlatSurfaces2sided"), &tmpA, &tmpB);
	ReloadFalloffMat(SkinXBX, _T("DetailLegSkin_Inner"), &tmpA, &tmpB);
	ReloadFalloffMat(SkinXBX, _T("Screen"), &tmpA, &tmpB);
	ReloadFalloffMat(SkinXBX, _T("Spout"), &tmpA, &tmpB);
	ReloadFalloffMat(SkinXBX, _T("NavType34"), &tmpA, &tmpB);
	ReloadFalloffMat(SkinXBX, _T("MetaFlatSurfaces"), &tmpA, &tmpB);
	ReloadFalloffMat(SkinXBX, _T("SC_SavedGame_Row01"), &tmpA, &tmpB);
	ReloadFalloffMat(SkinXBX, _T("HK_SavedGame_Row01"), &tmpA, &tmpB);
	ReloadFalloffMat(SkinXBX, _T("SavedEgg_Selected"), &tmpA, &tmpB);
	ReloadFalloffMat(SkinXBX, _T("FlatUnselected"), &tmpA, &tmpB);
	ReloadFalloffMat(SkinXBX, _T("Mem_InnerWall_Outer"), &tmpA, &tmpB);
	ReloadFalloffMat(SkinXBX, _T("SavedGameEgg"), &tmpA, &tmpB);
	ReloadFalloffMat(SkinXBX, _T("GameMenuEgg"), &tmpA, &tmpB);
	ReloadFalloffMat(SkinXBX, _T("Shell"), &tmpA, &tmpB);
	ReloadFalloffMat(SkinXBX, _T("Material #133sdsfdsf"), &tmpA, &tmpB);
	ReloadFalloffMat(SkinXBX, _T("IconParts"), &tmpA, &tmpB);
	ReloadFalloffMat(SkinXBX, _T("IconParts1"), &tmpA, &tmpB);
	ReloadFalloffMat(SkinXBX, _T("MU1Pod_HL"), &tmpA, &tmpB);
	ReloadFalloffMat(SkinXBX, _T("MU1Pod"), &tmpA, &tmpB);
	ReloadFalloffMat(SkinXBX, _T("GamePodb"), &tmpA, &tmpB);
	ReloadFalloffMat(SkinXBX, _T("GamePod"), &tmpA, &tmpB);
	ReloadFalloffMat(SkinXBX, _T("JwlSrfc01/InfoPnls"), &tmpA, &tmpB);
	ReloadFalloffMat(SkinXBX, _T("MenuCell"), &tmpA, &tmpB);
	ReloadFalloffMat(SkinXBX, _T("Tubes"), &tmpA, &tmpB);
	ReloadFalloffMat(SkinXBX, _T("JewelSurface02/PodMesh"), &tmpA, &tmpB);
	ReloadFalloffMat(SkinXBX, _T("TubesFade"), &tmpA, &tmpB);
	ReloadFalloffMat(SkinXBX, _T("TubesQ"), &tmpA, &tmpB);
	ReloadFalloffMat(SkinXBX, _T("EmptyMU"), &tmpA, &tmpB);
	ReloadFalloffMat(SkinXBX, _T("Tube"), &tmpA, &tmpB);
	ReloadFalloffMat(SkinXBX, _T("MemoryHeader"), &tmpA, &tmpB);
	ReloadFalloffMat(SkinXBX, _T("ButtonGlow"), &tmpA, &tmpB);
	ReloadFalloffMat(SkinXBX, _T("gradient"), &tmpA, &tmpB);
	ReloadFalloffMat(SkinXBX, _T("CellWallStructure"), &tmpA, &tmpB);
	ReloadFalloffMat(SkinXBX, _T("FlatSrfc/PodParts"), &tmpA, &tmpB);
	ReloadFalloffMat(SkinXBX, _T("Cell_Light"), &tmpA, &tmpB);
	ReloadFalloffMat(SkinXBX, _T("Cell_Light/LegSkin"), &tmpA, &tmpB);
	ReloadFalloffMat(SkinXBX, _T("CellEgg/Parts"), &tmpA, &tmpB);
	ReloadFalloffMat(SkinXBX, _T("GameEgg"), &tmpA, &tmpB);
	ReloadFalloffMat(SkinXBX, _T("FlatSurfaces2sided3"), &tmpA, &tmpB);
	ReloadFalloffMat(SkinXBX, _T("console_hilite"), &tmpA, &tmpB);

	// Wireframe (CFalloffTexInfo — same color fields as CFalloffMatInfo)
	ReloadFalloffMat(SkinXBX, _T("Wireframe"), &tmpA, &tmpB);

	// Aniso materials (same color fields as CFalloffMatInfo)
	ReloadFalloffMat(SkinXBX, _T("Metal_Chrome"), &tmpA, &tmpB);
	ReloadFalloffMat(SkinXBX, _T("Tvbox"), &tmpA, &tmpB);
	ReloadFalloffMat(SkinXBX, _T("AudioCD"), &tmpA, &tmpB);

	// Backing materials
	ReloadBackingMat(SkinXBX, _T("PanelBacking_01"), &tmpA);
	ReloadBackingMat(SkinXBX, _T("PanelBacking_02"), &tmpA);
	ReloadBackingMat(SkinXBX, _T("PanelBacking_03"), &tmpA);
	ReloadBackingMat(SkinXBX, _T("PanelBacking_04"), &tmpA);
	ReloadBackingMat(SkinXBX, _T("NameBacking"), &tmpA);
	ReloadBackingMat(SkinXBX, _T("ModeBacking"), &tmpA);
	ReloadBackingMat(SkinXBX, _T("SavedGameBacking"), &tmpA);
	ReloadBackingMat(SkinXBX, _T("MemManMetaBacking"), &tmpA);
	ReloadBackingMat(SkinXBX, _T("DarkenBacking"), &tmpA);
	ReloadBackingMat(SkinXBX, _T("TextBacking"), &tmpA);
	ReloadBackingMat(SkinXBX, _T("MetaBacking"), &tmpA);
	ReloadBackingMat(SkinXBX, _T("DarkenBackingDark"), &tmpA);

	// Modulate texture materials
	ReloadModulateMat(SkinXBX, _T("GameHilite"), &tmpA);
	ReloadModulateMat(SkinXBX, _T("PanelBacking"), &tmpA);

	// EggGlow (CEggGlowFadeMatInfo)
	{
		CMatInfo *p = LookupMatInfo(_T("EggGlow"));
		if (p)
		{
			TCHAR bufA[MAX_PATH], bufB[MAX_PATH];
			SkinXBX.GetValue(_T("EggGlow"), _T("ColorA"), bufA, MAX_PATH);
			SkinXBX.GetValue(_T("EggGlow"), _T("ColorB"), bufB, MAX_PATH);
			GetRGBAValues(bufA, &tmpA);
			GetRGBAValues(bufB, &tmpB);
			CEggGlowFadeMatInfo *pEgg = static_cast<CEggGlowFadeMatInfo *>(p);
			pEgg->m_colorSide = D3DCOLOR_RGBA(tmpA.r, tmpA.g, tmpA.b, tmpA.a);
			pEgg->Color1 = tmpB;
		}
	}

	// EggGlowPulse (CEggGlowPulseMatInfo)
	{
		CMatInfo *p = LookupMatInfo(_T("EggGlowPulse"));
		if (p)
		{
			TCHAR bufA[MAX_PATH], bufB[MAX_PATH];
			SkinXBX.GetValue(_T("EggGlowPulse"), _T("ColorA"), bufA, MAX_PATH);
			SkinXBX.GetValue(_T("EggGlowPulse"), _T("ColorB"), bufB, MAX_PATH);
			GetRGBAValues(bufA, &tmpA);
			GetRGBAValues(bufB, &tmpB);
			CEggGlowPulseMatInfo *pPulse = static_cast<CEggGlowPulseMatInfo *>(p);
			pPulse->Color1 = tmpA;
			pPulse->Color2 = tmpB;
		}
	}

	// Keyboard -> Key, BrightKey, KeyText (CKeyMatInfo)
	{
		TCHAR bufA[MAX_PATH], bufB[MAX_PATH];
		SkinXBX.GetValue(_T("Keyboard"), _T("ColorA"), bufA, MAX_PATH);
		SkinXBX.GetValue(_T("Keyboard"), _T("ColorB"), bufB, MAX_PATH);
		GetRGBAValues(bufA, &tmpA);
		GetRGBAValues(bufB, &tmpB);
		SkinXBX.GetValue(_T("Keyboard"), _T("ColorC"), bufA, MAX_PATH);
		SkinXBX.GetValue(_T("Keyboard"), _T("ColorD"), bufB, MAX_PATH);
		GetRGBAValues(bufA, &tmpC);
		GetRGBAValues(bufB, &tmpD);

		const TCHAR *keyNames[] = { _T("Key"), _T("BrightKey"), _T("KeyText") };
		for (int i = 0; i < 3; i++)
		{
			CMatInfo *p = LookupMatInfo(keyNames[i]);
			if (p)
			{
				CKeyMatInfo *pKey = static_cast<CKeyMatInfo *>(p);
				pKey->Color1 = tmpA;
				pKey->Color2 = tmpB;
				pKey->Color3 = tmpC;
				pKey->Color4 = tmpD;
			}
		}
	}

	OutputDebugString(_T("[Material_Reload] All material colors updated\n"));
}

void Material_Init(bool bReloadSkinXBX)
{
	if (bReloadSkinXBX)
	{
		Material_Reload();
		return;
	}

	CustomColor *CustomColorA = new CustomColor;
	CustomColor *CustomColorB = new CustomColor;
	CustomColor *CustomColorC = new CustomColor;
	CustomColor *CustomColorD = new CustomColor;

	TCHAR CurrentSkinFile[MAX_PATH];
	TCHAR WorkerString[MAX_PATH];
	TCHAR SkinString[MAX_PATH];
	TCHAR ColorA[MAX_PATH];
	TCHAR ColorB[MAX_PATH];

	CSettingsFile &SkinXBX = *TheseusGetSkinSettings();

	SkinXBX.GetValue(_T("InnerWall_01"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("InnerWall_01"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CInnerWallMatInfo(_T("InnerWall_01"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a),
						  D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a));

	SkinXBX.GetValue(_T("InnerWall_02"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("InnerWall_02"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CFalloffMatInfo(_T("InnerWall_02"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a), MATINFO_CULL_NONE);

	new CMatInfo(_T("MetaInfo_Text"), MATINFO_STANDARD_MATERIAL);
	new CMatInfo(_T("NamePanel_Text"), MATINFO_STANDARD_MATERIAL);
	new CMatInfo(_T("GameNameText_01"), MATINFO_STANDARD_MATERIAL);
	new CMatInfo(_T("GameNameText_02"), MATINFO_STANDARD_MATERIAL);
	new CMatInfo(_T("GameNameText_03"), MATINFO_STANDARD_MATERIAL);
	new CMatInfo(_T("GameIcon_01"), MATINFO_STANDARD_MATERIAL);
	new CMatInfo(_T("GameIcon_03"), MATINFO_STANDARD_MATERIAL);
	new CMatInfo(_T("Material #132"), MATINFO_STANDARD_MATERIAL);
	new CMatInfo(_T("u2 info"), MATINFO_STANDARD_MATERIAL);

	SkinXBX.GetValue(_T("Material #1334"), _T("Color"), ColorA, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	new CSolidMatInfo(_T("Material #1334"), (BYTE)CustomColorA->r, (BYTE)CustomColorA->g, (BYTE)CustomColorA->b, (BYTE)CustomColorA->a);

	SkinXBX.GetValue(_T("XBOXgreendark"), _T("Color"), ColorA, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	new CSolidMatInfo(_T("XBOXgreendark"), (BYTE)CustomColorA->r, (BYTE)CustomColorA->g, (BYTE)CustomColorA->b, (BYTE)CustomColorA->a);

	SkinXBX.GetValue(_T("XBOXgreen"), _T("Color"), ColorA, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	new CSolidMatInfo(_T("XBOXgreen"), (BYTE)CustomColorA->r, (BYTE)CustomColorA->g, (BYTE)CustomColorA->b, (BYTE)CustomColorA->a);

	SkinXBX.GetValue(_T("XBoxGreen2"), _T("Color"), ColorA, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	new CSolidMatInfo(_T("XBoxGreen2"), (BYTE)CustomColorA->r, (BYTE)CustomColorA->g, (BYTE)CustomColorA->b, (BYTE)CustomColorA->a);

	SkinXBX.GetValue(_T("GameHilite33"), _T("Color"), ColorA, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	new CSolidMatInfo(_T("GameHilite33"), (BYTE)CustomColorA->r, (BYTE)CustomColorA->g, (BYTE)CustomColorA->b, (BYTE)CustomColorA->a);

	SkinXBX.GetValue(_T("Nothing"), _T("Color"), ColorA, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	new CSolidMatInfo(_T("Nothing"), (BYTE)CustomColorA->r, (BYTE)CustomColorA->g, (BYTE)CustomColorA->b, (BYTE)CustomColorA->a);

	SkinXBX.GetValue(_T("NavType"), _T("Color"), ColorA, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	new CSolidMatInfo(_T("NavType"), (BYTE)CustomColorA->r, (BYTE)CustomColorA->g, (BYTE)CustomColorA->b, (BYTE)CustomColorA->a);

	SkinXBX.GetValue(_T("RedType"), _T("Color"), ColorA, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	new CSolidMatInfo(_T("RedType"), (BYTE)CustomColorA->r, (BYTE)CustomColorA->g, (BYTE)CustomColorA->b, (BYTE)CustomColorA->a);

	SkinXBX.GetValue(_T("XBoxGreen"), _T("Color"), ColorA, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	new CSolidMatInfo(_T("XBoxGreen"), (BYTE)CustomColorA->r, (BYTE)CustomColorA->g, (BYTE)CustomColorA->b, (BYTE)CustomColorA->a);

	SkinXBX.GetValue(_T("Type"), _T("Color"), ColorA, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	new CSolidMatInfo(_T("Type"), (BYTE)CustomColorA->r, (BYTE)CustomColorA->g, (BYTE)CustomColorA->b, (BYTE)CustomColorA->a);

	SkinXBX.GetValue(_T("Typesdsafsda"), _T("Color"), ColorA, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	new CSolidMatInfo(_T("Typesdsafsda"), (BYTE)CustomColorA->r, (BYTE)CustomColorA->g, (BYTE)CustomColorA->b, (BYTE)CustomColorA->a);

	SkinXBX.GetValue(_T("Material #133"), _T("Color"), ColorA, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	new CSolidMatInfo(_T("Material #133"), (BYTE)CustomColorA->r, (BYTE)CustomColorA->g, (BYTE)CustomColorA->b, (BYTE)CustomColorA->a);

	SkinXBX.GetValue(_T("Material #1335"), _T("Color"), ColorA, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	new CSolidMatInfo(_T("Material #1335"), (BYTE)CustomColorA->r, (BYTE)CustomColorA->g, (BYTE)CustomColorA->b, (BYTE)CustomColorA->a);

	SkinXBX.GetValue(_T("Material #133511"), _T("Color"), ColorA, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	new CSolidMatInfo(_T("Material #133511"), (BYTE)CustomColorA->r, (BYTE)CustomColorA->g, (BYTE)CustomColorA->b, (BYTE)CustomColorA->a);

	SkinXBX.GetValue(_T("Material #1336"), _T("Color"), ColorA, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	new CSolidMatInfo(_T("Material #1336"), (BYTE)CustomColorA->r, (BYTE)CustomColorA->g, (BYTE)CustomColorA->b, (BYTE)CustomColorA->a);

	SkinXBX.GetValue(_T("HilightedType"), _T("Color"), ColorA, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	new CSolidMatInfo(_T("HilightedType"), (BYTE)CustomColorA->r, (BYTE)CustomColorA->g, (BYTE)CustomColorA->b, (BYTE)CustomColorA->a);

	SkinXBX.GetValue(_T("XBoxGreenq"), _T("Color"), ColorA, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	new CSolidMatInfo(_T("XBoxGreenq"), (BYTE)CustomColorA->r, (BYTE)CustomColorA->g, (BYTE)CustomColorA->b, (BYTE)CustomColorA->a);

	SkinXBX.GetValue(_T("Black80"), _T("Color"), ColorA, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	new CSolidMatInfo(_T("Black80"), (BYTE)CustomColorA->r, (BYTE)CustomColorA->g, (BYTE)CustomColorA->b, (BYTE)CustomColorA->a);

	SkinXBX.GetValue(_T("CellEgg/Partsw"), _T("Color"), ColorA, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	new CSolidMatInfo(_T("CellEgg/Partsw"), (BYTE)CustomColorA->r, (BYTE)CustomColorA->g, (BYTE)CustomColorA->b, (BYTE)CustomColorA->a);

	SkinXBX.GetValue(_T("CellEgg/Partsz"), _T("Color"), ColorA, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	new CSolidMatInfo(_T("CellEgg/Partsz"), (BYTE)CustomColorA->r, (BYTE)CustomColorA->g, (BYTE)CustomColorA->b, (BYTE)CustomColorA->a);

	SkinXBX.GetValue(_T("Material #108"), _T("Color"), ColorA, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	new CSolidMatInfo(_T("Material #108"), (BYTE)CustomColorA->r, (BYTE)CustomColorA->g, (BYTE)CustomColorA->b, (BYTE)CustomColorA->a);

	SkinXBX.GetValue(_T("ItemsType"), _T("Color"), ColorA, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	new CSolidMatInfo(_T("ItemsType"), (BYTE)CustomColorA->r, (BYTE)CustomColorA->g, (BYTE)CustomColorA->b, (BYTE)CustomColorA->a);

	SkinXBX.GetValue(_T("GameHiliteMemory"), _T("Color"), ColorA, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	new CSolidMatInfo(_T("GameHiliteMemory"), (BYTE)CustomColorA->r, (BYTE)CustomColorA->g, (BYTE)CustomColorA->b, (BYTE)CustomColorA->a);

	SkinXBX.GetValue(_T("red"), _T("Color"), ColorA, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	new CSolidMatInfo(_T("red"), (BYTE)CustomColorA->r, (BYTE)CustomColorA->g, (BYTE)CustomColorA->b, (BYTE)CustomColorA->a);

	SkinXBX.GetValue(_T("FlatSurfaces"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("FlatSurfaces"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CFalloffMatInfo(_T("FlatSurfaces"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a));

	SkinXBX.GetValue(_T("FlatSurfacesSelected"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("FlatSurfacesSelected"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CFalloffMatInfo(_T("FlatSurfacesSelected"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a));

	SkinXBX.GetValue(_T("FlatSurfacesMemory"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("FlatSurfacesMemory"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CFalloffMatInfo(_T("FlatSurfacesMemory"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a));

	SkinXBX.GetValue(_T("DarkSurfaces"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("DarkSurfaces"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CFalloffMatInfo(_T("DarkSurfaces"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a));

	SkinXBX.GetValue(_T("DarkSurfaces2"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("DarkSurfaces2"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CFalloffMatInfo(_T("DarkSurfaces2"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a));

	SkinXBX.GetValue(_T("FlatSurfaces2sided"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("FlatSurfaces2sided"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CFalloffMatInfo(_T("FlatSurfaces2sided"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a), MATINFO_CULL_NONE);

	SkinXBX.GetValue(_T("DetailLegSkin_Inner"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("DetailLegSkin_Inner"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CFalloffMatInfo(_T("DetailLegSkin_Inner"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a));

	SkinXBX.GetValue(_T("Screen"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("Screen"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CFalloffMatInfo(_T("Screen"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a), MATINFO_CULL_NONE);

	SkinXBX.GetValue(_T("Spout"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("Spout"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CFalloffMatInfo(_T("Spout"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a));

	SkinXBX.GetValue(_T("NavType34"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("NavType34"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CFalloffMatInfo(_T("NavType34"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a));

	SkinXBX.GetValue(_T("MetaFlatSurfaces"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("MetaFlatSurfaces"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CFalloffMatInfo(_T("MetaFlatSurfaces"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a));

	SkinXBX.GetValue(_T("SC_SavedGame_Row01"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("SC_SavedGame_Row01"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CFalloffMatInfo(_T("SC_SavedGame_Row01"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a));

	SkinXBX.GetValue(_T("HK_SavedGame_Row01"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("HK_SavedGame_Row01"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CFalloffMatInfo(_T("HK_SavedGame_Row01"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a));

	SkinXBX.GetValue(_T("SavedEgg_Selected"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("SavedEgg_Selected"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CFalloffMatInfo(_T("SavedEgg_Selected"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a));

	SkinXBX.GetValue(_T("FlatUnselected"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("FlatUnselected"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CFalloffMatInfo(_T("FlatUnselected"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a));

	SkinXBX.GetValue(_T("Mem_InnerWall_Outer"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("Mem_InnerWall_Outer"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CFalloffMatInfo(_T("Mem_InnerWall_Outer"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a));

	SkinXBX.GetValue(_T("SavedGameEgg"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("SavedGameEgg"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CFalloffMatInfo(_T("SavedGameEgg"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a));

	SkinXBX.GetValue(_T("GameMenuEgg"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("GameMenuEgg"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CFalloffMatInfo(_T("GameMenuEgg"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a));

	SkinXBX.GetValue(_T("Shell"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("Shell"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CFalloffMatInfo(_T("Shell"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a));

	SkinXBX.GetValue(_T("Material #133sdsfdsf"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("Material #133sdsfdsf"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CFalloffMatInfo(_T("Material #133sdsfdsf"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a));

	SkinXBX.GetValue(_T("IconParts"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("IconParts"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CFalloffMatInfo(_T("IconParts"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a), MATINFO_CULL_NONE);

	SkinXBX.GetValue(_T("IconParts1"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("IconParts1"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CFalloffMatInfo(_T("IconParts1"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a));

	SkinXBX.GetValue(_T("MU1Pod_HL"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("MU1Pod_HL"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CFalloffMatInfo(_T("MU1Pod_HL"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a));

	SkinXBX.GetValue(_T("MU1Pod"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("MU1Pod"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CFalloffMatInfo(_T("MU1Pod"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a));

	SkinXBX.GetValue(_T("GamePodb"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("GamePodb"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CFalloffMatInfo(_T("GamePodb"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a));

	SkinXBX.GetValue(_T("GamePod"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("GamePod"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CFalloffMatInfo(_T("GamePod"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a));

	SkinXBX.GetValue(_T("JwlSrfc01/InfoPnls"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("JwlSrfc01/InfoPnls"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CFalloffMatInfo(_T("JwlSrfc01/InfoPnls"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a));

	SkinXBX.GetValue(_T("MenuCell"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("MenuCell"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CFalloffMatInfo(_T("MenuCell"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a));

	SkinXBX.GetValue(_T("Wireframe"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("Wireframe"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CFalloffTexInfo(_T("Wireframe"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a));

	SkinXBX.GetValue(_T("Tubes"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("Tubes"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CFalloffMatInfo(_T("Tubes"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a), MATINFO_CULL_NONE);

	SkinXBX.GetValue(_T("JewelSurface02/PodMesh"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("JewelSurface02/PodMesh"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CFalloffMatInfo(_T("JewelSurface02/PodMesh"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a));

	SkinXBX.GetValue(_T("TubesFade"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("TubesFade"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CFalloffMatInfo(_T("TubesFade"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a), MATINFO_CULL_NONE);

	SkinXBX.GetValue(_T("TubesQ"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("TubesQ"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CFalloffMatInfo(_T("TubesQ"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a));

	SkinXBX.GetValue(_T("EmptyMU"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("EmptyMU"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CFalloffMatInfo(_T("EmptyMU"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a));

	SkinXBX.GetValue(_T("Tube"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("Tube"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CFalloffMatInfo(_T("Tube"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a));

	SkinXBX.GetValue(_T("MemoryHeader"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("MemoryHeader"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CFalloffMatInfo(_T("MemoryHeader"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a), MATINFO_CULL_NONE);

	SkinXBX.GetValue(_T("EggGlow"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("EggGlow"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CEggGlowFadeMatInfo(_T("EggGlow"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), *CustomColorB);

	SkinXBX.GetValue(_T("ButtonGlow"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("ButtonGlow"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CFalloffMatInfo(_T("ButtonGlow"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a));

	SkinXBX.GetValue(_T("gradient"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("gradient"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CFalloffMatInfo(_T("gradient"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a));

	SkinXBX.GetValue(_T("CellWallStructure"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("CellWallStructure"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CFalloffMatInfo(_T("CellWallStructure"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a));

	SkinXBX.GetValue(_T("FlatSrfc/PodParts"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("FlatSrfc/PodParts"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CFalloffMatInfo(_T("FlatSrfc/PodParts"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a));

	SkinXBX.GetValue(_T("Cell_Light"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("Cell_Light"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CFalloffMatInfo(_T("Cell_Light"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a));

	SkinXBX.GetValue(_T("Cell_Light/LegSkin"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("Cell_Light/LegSkin"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CFalloffMatInfo(_T("Cell_Light/LegSkin"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a));

	SkinXBX.GetValue(_T("CellEgg/Parts"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("CellEgg/Parts"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CFalloffMatInfo(_T("CellEgg/Parts"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a));

	SkinXBX.GetValue(_T("GameEgg"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("GameEgg"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CFalloffMatInfo(_T("GameEgg"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a));

	SkinXBX.GetValue(_T("FlatSurfaces2sided3"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("FlatSurfaces2sided3"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CFalloffMatInfo(_T("FlatSurfaces2sided3"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a));

	SkinXBX.GetValue(_T("console_hilite"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("console_hilite"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CFalloffMatInfo(_T("console_hilite"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a));

	SkinXBX.GetValue(_T("Metal_Chrome"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("Metal_Chrome"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CAnisoMatInfo(_T("Metal_Chrome"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a));

	SkinXBX.GetValue(_T("Tvbox"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("Tvbox"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CAnisoMatInfo(_T("Tvbox"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a));

	SkinXBX.GetValue(_T("AudioCD"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("AudioCD"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CAnisoMatInfo(_T("AudioCD"), D3DCOLOR_RGBA(CustomColorA->r, CustomColorA->g, CustomColorA->b, CustomColorA->a), D3DCOLOR_RGBA(CustomColorB->r, CustomColorB->g, CustomColorB->b, CustomColorB->a));

	SkinXBX.GetValue(_T("PanelBacking_01"), _T("Color"), ColorA, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	new CBackingMatInfo(_T("PanelBacking_01"), (BYTE)CustomColorA->r, (BYTE)CustomColorA->g, (BYTE)CustomColorA->b, (BYTE)CustomColorA->a);

	SkinXBX.GetValue(_T("PanelBacking_02"), _T("Color"), ColorA, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	new CBackingMatInfo(_T("PanelBacking_02"), (BYTE)CustomColorA->r, (BYTE)CustomColorA->g, (BYTE)CustomColorA->b, (BYTE)CustomColorA->a);

	SkinXBX.GetValue(_T("PanelBacking_03"), _T("Color"), ColorA, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	new CBackingMatInfo(_T("PanelBacking_03"), (BYTE)CustomColorA->r, (BYTE)CustomColorA->g, (BYTE)CustomColorA->b, (BYTE)CustomColorA->a);

	SkinXBX.GetValue(_T("PanelBacking_04"), _T("Color"), ColorA, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	new CBackingMatInfo(_T("PanelBacking_04"), (BYTE)CustomColorA->r, (BYTE)CustomColorA->g, (BYTE)CustomColorA->b, (BYTE)CustomColorA->a);

	SkinXBX.GetValue(_T("NameBacking"), _T("Color"), ColorA, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	new CBackingMatInfo(_T("NameBacking"), (BYTE)CustomColorA->r, (BYTE)CustomColorA->g, (BYTE)CustomColorA->b, (BYTE)CustomColorA->a);

	SkinXBX.GetValue(_T("ModeBacking"), _T("Color"), ColorA, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	new CBackingMatInfo(_T("ModeBacking"), (BYTE)CustomColorA->r, (BYTE)CustomColorA->g, (BYTE)CustomColorA->b, (BYTE)CustomColorA->a);

	SkinXBX.GetValue(_T("SavedGameBacking"), _T("Color"), ColorA, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	new CBackingMatInfo(_T("SavedGameBacking"), (BYTE)CustomColorA->r, (BYTE)CustomColorA->g, (BYTE)CustomColorA->b, (BYTE)CustomColorA->a);

	SkinXBX.GetValue(_T("MemManMetaBacking"), _T("Color"), ColorA, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	new CBackingMatInfo(_T("MemManMetaBacking"), (BYTE)CustomColorA->r, (BYTE)CustomColorA->g, (BYTE)CustomColorA->b, (BYTE)CustomColorA->a);

	SkinXBX.GetValue(_T("DarkenBacking"), _T("Color"), ColorA, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	new CBackingMatInfo(_T("DarkenBacking"), (BYTE)CustomColorA->r, (BYTE)CustomColorA->g, (BYTE)CustomColorA->b, (BYTE)CustomColorA->a, MATINFO_CULL_NONE | MATINFO_RADIAL_EDGE_ALPHA);

	SkinXBX.GetValue(_T("TextBacking"), _T("Color"), ColorA, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	new CBackingMatInfo(_T("TextBacking"), (BYTE)CustomColorA->r, (BYTE)CustomColorA->g, (BYTE)CustomColorA->b, (BYTE)CustomColorA->a, MATINFO_CULL_NONE | MATINFO_RADIAL_ALPHA);

	SkinXBX.GetValue(_T("MetaBacking"), _T("Color"), ColorA, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	new CBackingMatInfo(_T("MetaBacking"), (BYTE)CustomColorA->r, (BYTE)CustomColorA->g, (BYTE)CustomColorA->b, (BYTE)CustomColorA->a, MATINFO_CULL_NONE | MATINFO_RADIAL_ALPHA);

	SkinXBX.GetValue(_T("DarkenBackingDark"), _T("Color"), ColorA, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	new CBackingMatInfo(_T("DarkenBackingDark"), (BYTE)CustomColorA->r, (BYTE)CustomColorA->g, (BYTE)CustomColorA->b, (BYTE)CustomColorA->a, MATINFO_CULL_NONE | MATINFO_RADIAL_ALPHA);

	SkinXBX.GetValue(_T("GameHilite"), _T("Color"), ColorA, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	new CModulateTextureMatInfo(_T("GameHilite"), (BYTE)CustomColorA->r, (BYTE)CustomColorA->g, (BYTE)CustomColorA->b, (BYTE)CustomColorA->a, MATINFO_CULL_NONE);

	new CMaskTextureMatInfo(_T("equalizer"), MATINFO_RADIAL_AV_ALPHA);
	new CMaskTextureMatInfo(_T("MainMenuOrb"), MATINFO_RADIAL_EDGE_MAIN_ALPHA);

	SkinXBX.GetValue(_T("PanelBacking"), _T("Color"), ColorA, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	new CModulateTextureMatInfo(_T("PanelBacking"), (BYTE)CustomColorA->r, (BYTE)CustomColorA->g, (BYTE)CustomColorA->b, (BYTE)CustomColorA->a, MATINFO_CULL_NONE);

	SkinXBX.GetValue(_T("EggGlowPulse"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("EggGlowPulse"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	new CEggGlowPulseMatInfo(_T("EggGlowPulse"), *CustomColorA, *CustomColorB);

	SkinXBX.GetValue(_T("Keyboard"), _T("ColorA"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("Keyboard"), _T("ColorB"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorA);
	GetRGBAValues(ColorB, CustomColorB);
	SkinXBX.GetValue(_T("Keyboard"), _T("ColorC"), ColorA, MAX_PATH);
	SkinXBX.GetValue(_T("Keyboard"), _T("ColorD"), ColorB, MAX_PATH);
	GetRGBAValues(ColorA, CustomColorC);
	GetRGBAValues(ColorB, CustomColorD);
	new CKeyMatInfo(_T("Key"), false, false, *CustomColorA, *CustomColorB, *CustomColorC, *CustomColorD);
	new CKeyMatInfo(_T("BrightKey"), true, false, *CustomColorA, *CustomColorB, *CustomColorC, *CustomColorD);
	new CKeyMatInfo(_T("KeyText"), false, true, *CustomColorA, *CustomColorB, *CustomColorC, *CustomColorD);

	new CReflectMatInfo(_T("ReflectSurface"));
	new CFreeSpaceMatInfo(_T("Material #10822"));
	new CIconMatInfo(_T("TitleIcon"), 128, 128);
	new CIconMatInfo(_T("TitleSoundtrackIcon"), 128, 128, false, true);
	new CIconMatInfo(_T("SavedGameIcon"), 64, 64, true);
	new CIconMatInfo(_T("SoundtrackIcon"), 64, 64, true, true);
	new CIconMatInfo(_T("SelectedIcon"), 128, 128, false, false, true);
	new CIconMatInfo(_T("TitleMenuIcon"), 128, 128, false, false, true);
	// OrangeNavType - Solid (5960 binary: A=0xB2=178, not 255)
	new CSolidMatInfo(_T("OrangeNavType"), 0xF9, 0x98, 0x19, 0xB2);

	// XboxLiveGameTitleOrbIcon - Same as OrangeNavType
	new CSolidMatInfo(_T("XboxLiveGameTitleOrbIcon"), 0xF9, 0x98, 0x19, 0xFF); // same as OrangeNavType

	// MemoryHeaderHilite - Falloff (5960 binary: side=0xF0C7E800 front=0x82617200)
	// Was incorrectly Solid with R/G swapped. Actual: RGBA(199,232,0,240) / RGBA(97,114,0,130)
	new CFalloffMatInfo(_T("MemoryHeaderHilite"), D3DCOLOR_RGBA(199, 232, 0, 240), D3DCOLOR_RGBA(97, 114, 0, 130));

	// grill grey - Falloff (5960 binary: side=0x00202020 front=0xFF404040)
	new CFalloffMatInfo(_T("grill grey"), D3DCOLOR_RGBA(32, 32, 32, 0), D3DCOLOR_RGBA(64, 64, 64, 255));

	// LiveChrome - Solid (9D6DC2)
	new CSolidMatInfo(_T("LiveChrome"), 0x9D, 0x6D, 0xC2, 0xFF); // from 'livechrome'

	// grey - Falloff (5960 binary: side=0x00475345 front=0xFF566452)
	new CFalloffMatInfo(_T("grey"), D3DCOLOR_RGBA(71, 83, 69, 0), D3DCOLOR_RGBA(86, 100, 82, 255));

	// dark green panels - Solid
	new CSolidMatInfo(_T("dark green panels"), 0x09, 0x29, 0x00, 0xFF); // from 'dark_green_panels'

	// dark green panels falloff - Falloff (5960 binary: side=0x00000000 front=0xFF052305)
	new CFalloffMatInfo(_T("dark green panels falloff"), D3DCOLOR_RGBA(0, 0, 0, 0), D3DCOLOR_RGBA(5, 35, 5, 255));

	// footer - Solid
	new CSolidMatInfo(_T("footer"), 0x9D, 0x6D, 0xC2, 0xFF); // from 'footer'

	// live header - Solid
	new CSolidMatInfo(_T("live header"), 0x04, 0x14, 0x00, 0xFF); // from 'live_header'

	// orangeEggGlow - Solid
	new CSolidMatInfo(_T("orangeEggGlow"), 0xFE, 0xFF, 0xBC, 0xFF); // derived from 'eggglow_1' (E4FEFFBC flipped = FFBCFFFE)

	// MotdIcon - Solid (default white)
	new CSolidMatInfo(_T("MotdIcon"), 0xFF, 0xFF, 0xFF, 0xFF); // fallback or default

	// highlight - Solid
	new CSolidMatInfo(_T("highlight"), 0x9D, 0x6D, 0xC2, 0xFF); // from 'highlight'

	// button - Solid
	new CSolidMatInfo(_T("button"), 0x9D, 0x6D, 0xC2, 0xFF); // from 'button'

	// white - Solid
	new CSolidMatInfo(_T("white"), 0xFF, 0xFF, 0xFF, 0xFF); // from 'white'

	// solid green 1 - Standard
	new CSolidMatInfo(_T("solid green 1"), 0x11, 0xFF, 0x22, 0xFF); // from 'solid_green_1'

	// solid green2 - Standard
	new CSolidMatInfo(_T("solid green2"), 0x12, 0xC1, 0x0A, 0xFF); // from 'solid_green_2'

	// solid green 3 - Standard
	new CSolidMatInfo(_T("solid green 3"), 0x0A, 0x75, 0x1C, 0xFF); // from 'solid_green_3'

	// solid green 4 - Standard
	new CSolidMatInfo(_T("solid green 4"), 0x12, 0x37, 0x00, 0xFF); // from 'solid_green_4'

	// Black
	new CSolidMatInfo(_T("Black"), 0x00, 0x00, 0x00, 0xFF);

	qsort(g_rgMatInfo, g_nMatInfoCount, sizeof(CMatInfo *), SortMatInfoCompare);

	delete CustomColorA;
	delete CustomColorB;
	delete CustomColorC;
	delete CustomColorD;
}

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

	TheseusSetRenderState(D3DRS_EDGEANTIALIAS, FALSE);
	TheseusSetRenderState(D3DRS_MULTISAMPLEANTIALIAS, TRUE);

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
}
