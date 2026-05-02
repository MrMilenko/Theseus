// asset_loader.h: mesh, texture, and image asset types loaded from a
// CXipFile or directly from disk. Mirrors asset_loader.cpp's consolidation:
// the underlying data formats here (MESHFILEHEADER, CTexture surfaces,
// image blits) all share the same archive parsing path.

#pragma once

#include "node.h"

// =========================================================================
// Mesh file format and core mesh types
// =========================================================================

struct MESHFILEHEADER
{
	DWORD dwPrimitiveType;
	DWORD dwFaceCount;
	DWORD dwFVF;
	DWORD dwVertexStride;
	DWORD dwVertexCount;
	DWORD dwIndexCount;
};

// Abstract base used by both standalone CMesh and CMeshRef-style instances
// that share GPU buffers across an archive.
class CMeshCore
{
public:
	virtual void Render(bool bSetFVF = true) = 0;
	virtual DWORD GetFVF() const = 0;
};

class CMesh : public CMeshCore
{
public:
	CMesh();
	virtual ~CMesh();

	bool Load(const TCHAR* szFilePath);
	bool Create(HANDLE hFile);
	bool Create(BYTE* pbContent, DWORD cbContent);
	void Render(bool bSetFVF = true);
	DWORD GetFVF() const;

	int m_nFaceCount;
	int m_nVertexCount;
	int m_nIndexCount;
	D3DPRIMITIVETYPE m_primitiveType;
	DWORD m_fvf;
	int m_nVertexStride;
	IDirect3DVertexBuffer8* m_vertexBuffer;
	IDirect3DIndexBuffer8* m_indexBuffer;

	friend class CIndexedFaceSet;
};

// One renderable mesh that lives inside a CXipFile's shared mesh
// buffer pool. The CMeshRef itself is tiny -- it just holds the index
// of the buffer in the parent archive plus the slice within it; the
// actual GPU vertex/index data is owned by the archive's m_meshBuffers
// array. Many CMeshRefs can share one buffer pair.
class CMeshRef : public CMeshCore
{
public:
	void Render(bool bSetFVF = true);
	DWORD GetFVF() const;

	class CXipFile* m_xipFile;
	int m_meshBufferIndex;

	int m_firstIndex;
	int m_primitiveCount;
};

extern CMesh* LoadMesh(const TCHAR* szFilePath);
extern CMesh* CreateMesh(HANDLE hFile);
extern CMesh* CreateMesh(BYTE* pbContent, DWORD cbContent);
extern CMesh* MakeSphere(float nRadius, int nSlices, int nStacks);

// =========================================================================
// Texture loading and texture-bearing scene nodes
// =========================================================================

extern LPDIRECT3DTEXTURE8 LoadTexture(const TCHAR* szURL, UINT width, UINT height);
extern LPDIRECT3DTEXTURE8 ParseTexture(const TCHAR* szURL, const BYTE* pbContent, int cbContent, UINT width=0, UINT height=0);

class CTexture : public CNode
{
	DECLARE_NODE(CTexture, CNode);
public:
	CTexture();
	~CTexture();

	bool m_repeatS;
	bool m_repeatT;
	bool m_titleImage;

	D3DFORMAT m_format;
	LPDIRECT3DTEXTURE8 m_surface;
	int m_nImageWidth;
	int m_nImageHeight;

	virtual bool Create(int nWidth, int nHeight);

	LPDIRECT3DTEXTURE8 GetTextureSurface();

	DECLARE_NODE_PROPS()
};

class CImageTexture : public CTexture
{
	DECLARE_NODE(CImageTexture, CTexture);
public:
	CImageTexture();
	~CImageTexture();

	TCHAR* m_url;
	bool m_alpha;
	bool m_fromfile;

	LPDIRECT3DTEXTURE8 GetTextureSurface();
	bool OnSetProperty(const PRD* pprd, const void* pvValue);

	void Load(const TCHAR* szURL);
	bool m_dirty;

	static CImageTexture* c_pFirstImageTexture;
	CImageTexture* m_nextImageTexture;

	DECLARE_NODE_PROPS()
};

// =========================================================================
// Image draw helpers (CPU-side blits used by the title-image cache)
// =========================================================================

// GDI-style colour/macro fallbacks for hosts that don't already provide
// them through the platform headers.
#ifndef RGB
#define RGB(r,g,b)          ((COLORREF)(((BYTE)(r)|((WORD)((BYTE)(g))<<8))|(((DWORD)(BYTE)(b))<<16)))
#endif
#ifndef PALETTERGB
#define PALETTERGB(r,g,b)   (0x02000000 | RGB(r,g,b))
#endif
#ifndef PALETTEINDEX
#define PALETTEINDEX(i)     ((COLORREF)(0x01000000 | (DWORD)(WORD)(i)))
#endif
#ifndef GetRValue
#define GetRValue(rgb)      ((BYTE)(rgb))
#define GetGValue(rgb)      ((BYTE)(((WORD)(rgb)) >> 8))
#define GetBValue(rgb)      ((BYTE)((rgb)>>16))
#endif

#ifndef SetRect
#define SetRect(pRect, nLeft, nTop, nRight, nBottom) \
{ \
	(pRect)->left = (nLeft); \
	(pRect)->top = (nTop); \
	(pRect)->right = (nRight); \
	(pRect)->bottom = (nBottom); \
}
#endif

#ifndef TRANSPARENT
#define TRANSPARENT         1
#define OPAQUE              2
#endif

#ifndef TA_LEFT
#define TA_LEFT              0
#define TA_RIGHT             2
#define TA_TOP               0
#define TA_BOTTOM            8
#define TA_BASELINE          24
#endif

#ifdef _XBOX
#define XFONT_TRUETYPE
#include <xfont.h>
#endif

#ifndef __cplusplus
typedef struct IDirect3DSurface8 * LPDIRECT3DSURFACE8;
typedef unsigned long D3DCOLOR;
#endif

#ifdef __cplusplus
struct DRAW
{
	LPDIRECT3DSURFACE8 pSurface;
	D3DSURFACE_DESC Desc;
	D3DLOCKED_RECT Lock;
};
#endif

typedef struct DRAW* HDRAW;
typedef LPDIRECT3DTEXTURE8 HIMAGE;

EXTERN_C HRESULT X_BitBlt(HDRAW hDraw, int x, int y, int cx, int cy, LPDIRECT3DTEXTURE8 pSrcSurface, int xSrc, int ySrc);
EXTERN_C HRESULT X_FillRect(HDRAW hDraw, int x, int y, int cx, int cy, D3DCOLOR color);

EXTERN_C void DrawImage(HDRAW hDC, const TCHAR* szImgFile, int x, int y, int align, WORD* pcx, WORD* pcy);
EXTERN_C BOOL GetImageSize(const TCHAR* szImgFile, SIZE* pSize);
