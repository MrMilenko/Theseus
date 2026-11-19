// asset_loader.h: meshes and textures, read from a level's XIP or straight
// from disk. Bodies in render/asset_loader.cpp, binary notes in
// docs/decomp/AssetLoader.md.

#pragma once

#include "node.h"

class CXipFile;

// Header at the front of a .xm mesh file.
struct MESHFILEHEADER
{
	DWORD dwPrimitiveType;
	DWORD dwFaceCount;
	DWORD dwFVF;
	DWORD dwVertexStride;
	DWORD dwVertexCount;
	DWORD dwIndexCount;
};

// What a mesh node draws: either a mesh with its own buffers or a slice of
// buffers its archive shares.
class CMeshCore
{
public:
	virtual void Render(bool setFVF = true) = 0;
	virtual DWORD GetFVF() const = 0;
	// Mesh nodes delete their CMesh through this type; without it the buffers leaked.
	virtual ~CMeshCore() {}
};

// A mesh that owns its vertex and index buffers.
class CMesh : public CMeshCore
{
	friend class CIndexedFaceSet;
public:
	CMesh();
	virtual ~CMesh();

	bool Load(const TCHAR* path);
	bool Create(HANDLE file);
	void Render(bool setFVF = true);
	DWORD GetFVF() const;

	int m_nFaceCount;
	int m_nVertexCount;
	int m_nIndexCount;
	D3DPRIMITIVETYPE m_primitiveType;
	DWORD m_fvf;
	int m_nVertexStride;
	IDirect3DVertexBuffer8* m_vertexBuffer;
	IDirect3DIndexBuffer8* m_indexBuffer;
};

// A range of primitives inside one of the archive's shared buffer pairs.
class CMeshRef : public CMeshCore
{
public:
	void Render(bool setFVF = true);
	DWORD GetFVF() const;

	CXipFile* m_xipFile;
	int m_meshBufferIndex;
	int m_firstIndex;
	int m_primitiveCount;
};

extern CMesh* LoadMesh(const TCHAR* path);
extern CMesh* MakeSphere(float radius, int slices, int stacks);

extern LPDIRECT3DTEXTURE8 LoadTexture(const TCHAR* url, UINT width, UINT height);
extern LPDIRECT3DTEXTURE8 ParseTexture(const TCHAR* url, const BYTE* data, int size, UINT width = 0, UINT height = 0);

// A texture surface a node can hand to its shape.
class CTexture : public CNode
{
	DECLARE_NODE(CTexture, CNode)
	DECLARE_NODE_PROPS()
public:
	CTexture();
	~CTexture();

	virtual bool Create(int w, int h);
	LPDIRECT3DTEXTURE8 GetTextureSurface();

	bool m_repeatS;
	bool m_repeatT;
	bool m_titleImage;
	D3DFORMAT m_format;
	LPDIRECT3DTEXTURE8 m_surface;
	int m_nImageWidth;
	int m_nImageHeight;
};

// A texture loaded from a url. Every one sits on a list for reloads.
class CImageTexture : public CTexture
{
	DECLARE_NODE(CImageTexture, CTexture)
	DECLARE_NODE_PROPS()
public:
	CImageTexture();
	~CImageTexture();

	bool OnSetProperty(const PRD* prop, const void* value);
	LPDIRECT3DTEXTURE8 GetTextureSurface();
	void Load(const TCHAR* url);

	static CImageTexture* c_pFirstImageTexture;

	TCHAR* m_url;
	bool m_alpha;
	bool m_fromfile;
	bool m_dirty;
	CImageTexture* m_nextImageTexture;
};
