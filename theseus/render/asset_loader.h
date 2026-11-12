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
	// Mesh nodes delete their CMesh through this type; without it the buffers leaked.
	virtual ~CMeshCore() {}
};

class CMesh : public CMeshCore
{
public:
	CMesh();
	virtual ~CMesh();

	bool Load(const TCHAR* szFilePath);
	bool Create(HANDLE hFile);
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
