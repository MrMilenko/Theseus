// mesh.cpp: desktop CMesh vertex / index buffer management,
// CMeshNode scene graph node, and sphere generation. Loads .xm mesh
// files with skin-directory override. Counterpart to
// render/asset_loader.cpp on Xbox.

#include "std.h"
#include "dashapp.h"
#include "node.h"
#include "shape_render.h"
#include "runner.h"
#include "asset_loader.h"
#include "xip.h"
#include "file_util.h"
#include "camera.h"
#include "skin_assets.h"

extern CCamera theCamera;
extern unsigned int g_uMesh;
extern int g_bEdgeAntialiasOverride;

// ============================================================================
// CMesh; vertex/index buffer container
// ============================================================================

CMesh::CMesh()
{
	m_vertexBuffer = NULL;
	m_indexBuffer = NULL;
	m_nVertexStride = 0;
	m_nFaceCount = 0;
	m_nVertexCount = 0;
	m_nIndexCount = 0;
	m_fvf = 0;
	m_primitiveType = (D3DPRIMITIVETYPE)0;
}

CMesh::~CMesh()
{
	if (m_vertexBuffer != NULL)
		m_vertexBuffer->Release();

	if (m_indexBuffer != NULL)
		m_indexBuffer->Release();
}

DWORD CMesh::GetFVF() const
{
	ASSERT(m_fvf != 0);
	return m_fvf;
}

bool CMesh::Create(BYTE *pbData, DWORD dwData)
{
	MESHFILEHEADER *pHeader = (MESHFILEHEADER *)pbData;
	pbData += sizeof(MESHFILEHEADER);

	m_primitiveType = (D3DPRIMITIVETYPE)pHeader->dwPrimitiveType;

	m_nFaceCount = pHeader->dwFaceCount;
	m_fvf = pHeader->dwFVF;
	m_nVertexStride = pHeader->dwVertexStride;
	m_nVertexCount = pHeader->dwVertexCount;
	m_nIndexCount = pHeader->dwIndexCount;

	TheseusCreateVertexBuffer(m_nVertexCount * m_nVertexStride, D3DUSAGE_DYNAMIC, m_fvf, D3DPOOL_DEFAULT, &m_vertexBuffer);

	uint8_t *verts;
	const uint32_t dwLockFlags = D3DLOCK_DISCARD;
	VERIFYHR(m_vertexBuffer->Lock(0, 0, &verts, dwLockFlags));
	memcpy(verts, pbData, m_nVertexCount * m_nVertexStride);
	pbData += m_nVertexCount * m_nVertexStride;
	VERIFYHR(m_vertexBuffer->Unlock());

	TheseusCreateIndexBuffer(m_nIndexCount * sizeof(uint16_t), D3DUSAGE_DYNAMIC, D3DFMT_INDEX16, D3DPOOL_DEFAULT, &m_indexBuffer);

	uint8_t *indices;
	VERIFYHR(m_indexBuffer->Lock(0, m_nIndexCount * sizeof(uint16_t), (uint8_t **)&indices, dwLockFlags));
	memcpy(indices, pbData, m_nIndexCount * sizeof(uint16_t));
	VERIFYHR(m_indexBuffer->Unlock());

	return true;
}

bool CMesh::Create(HANDLE hFile)
{
	uint32_t dwRead;
	MESHFILEHEADER header;

	VERIFY(ReadFile(hFile, &header, sizeof(header), LPDW(&dwRead), NULL) && dwRead == sizeof(header));

	m_primitiveType = (D3DPRIMITIVETYPE)header.dwPrimitiveType;

	m_nFaceCount = header.dwFaceCount;
	m_fvf = header.dwFVF;
	ASSERT(m_fvf != 0);
	m_nVertexStride = header.dwVertexStride;
	m_nVertexCount = header.dwVertexCount;
	m_nIndexCount = header.dwIndexCount;

	TheseusCreateVertexBuffer(m_nVertexCount * m_nVertexStride, D3DUSAGE_DYNAMIC, m_fvf, D3DPOOL_DEFAULT, &m_vertexBuffer);

	uint8_t *verts;
	const uint32_t dwLockFlags = D3DLOCK_DISCARD;
	VERIFYHR(m_vertexBuffer->Lock(0, 0, &verts, dwLockFlags));
	VERIFY(ReadFile(hFile, verts, m_nVertexCount * m_nVertexStride, LPDW(&dwRead), NULL) && dwRead == (uint32_t)(m_nVertexCount * m_nVertexStride));
	VERIFYHR(m_vertexBuffer->Unlock());

	TheseusCreateIndexBuffer(m_nIndexCount * sizeof(uint16_t), D3DUSAGE_DYNAMIC, D3DFMT_INDEX16, D3DPOOL_DEFAULT, &m_indexBuffer);

	uint8_t *indices;
	VERIFYHR(m_indexBuffer->Lock(0, m_nIndexCount * sizeof(uint16_t), (uint8_t **)&indices, D3DLOCK_DISCARD));
	VERIFY(ReadFile(hFile, indices, m_nIndexCount * sizeof(uint16_t), LPDW(&dwRead), NULL) && dwRead == m_nIndexCount * sizeof(uint16_t));
	VERIFYHR(m_indexBuffer->Unlock());

	return true;
}

bool CMesh::Load(const TCHAR *szFilePath)
{
    HANDLE hFile = INVALID_HANDLE_VALUE;
    const char* relativePath = szFilePath;

    // Strip known absolute prefixes to get a relative path
    if (strncasecmp(szFilePath, "Q:/Xips/", 8) == 0 || strncasecmp(szFilePath, "Q:\\Xips\\", 8) == 0)
    {
        relativePath = szFilePath + 8;  // skip "Q:/Xips/"
    }
    else if (strncasecmp(szFilePath, "Q:/", 3) == 0 || strncasecmp(szFilePath, "Q:\\", 3) == 0)
    {
        relativePath = szFilePath + 3;  // generic fallback if someone did Q:\file directly
    }

    // === Attempt skin override ===
    if (g_sSkinDir)
    {
        char SkinPath[MAX_PATH];
        sprintf(SkinPath, "%s%s", g_sSkinDir, relativePath);

        hFile = TheseusCreateFile(SkinPath, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
        if (hFile != INVALID_HANDLE_VALUE)
        {
        }
    }

    // === Fallback to original path ===
    if (hFile == INVALID_HANDLE_VALUE)
    {
        hFile = TheseusCreateFile(szFilePath, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
        if (hFile == INVALID_HANDLE_VALUE)
        {
            TRACE("\001[Mesh] Cannot load MeshNode: %s\n", szFilePath);
            return false;
        }

    }

    bool b = Create(hFile);
    CloseHandle(hFile);
    return b;
}

void CMesh::Render(bool bSetFVF)
{
	if (m_vertexBuffer == NULL || m_indexBuffer == NULL)
		return;

	ASSERT(m_primitiveType != 0); // forget to set this?

	if (bSetFVF)
		TheseusSetVertexShader(GetFixedFunctionShader(m_fvf));

	if (m_nFaceCount > 800 && !g_bEdgeAntialiasOverride)
	{
		TheseusSetRenderState(D3DRS_EDGEANTIALIAS, FALSE);
		TheseusSetRenderState(D3DRS_MULTISAMPLEANTIALIAS, TRUE);
	}

	TheseusSetStreamSource(0, m_vertexBuffer, m_nVertexStride);
	TheseusSetIndices(m_indexBuffer, 0);
	TheseusDrawIndexedPrimitive(m_primitiveType, 0, m_nVertexCount, 0, m_nFaceCount);

	g_nVertPerFrame += m_nVertexCount;
	g_nTriPerFrame += m_nFaceCount;
}

// ============================================================================
// CMeshNode; scene graph node that loads/renders a mesh
// ============================================================================

class CMeshNode *g_pRenderMeshNode = NULL;

CMeshNode *CMeshNode::c_pFirst;

IMPLEMENT_NODE("Mesh", CMeshNode, CNode)

START_NODE_PROPS(CMeshNode, CNode)
NODE_PROP(pt_string, CMeshNode, url)
NODE_PROP(pt_number, CMeshNode, falloff)
END_NODE_PROPS()

#undef _FND_CLASS
#define _FND_CLASS CMeshNode
START_NODE_FUN(CMeshNode, CNode)
NODE_FUN_VS(load)
END_NODE_FUN()

CMeshNode::CMeshNode() : m_url(NULL), m_falloff(0.0f)
{
	m_next = c_pFirst;
	c_pFirst = this;
	m_renderTime = 0.0f;

	m_dirty = true;
	m_mesh = NULL;
	m_ownMesh = true;
}

CMeshNode::~CMeshNode()
{
	if (m_ownMesh)
		delete m_mesh;

	delete[] m_url;

	CMeshNode **ppMeshNode;
	for (ppMeshNode = &c_pFirst; *ppMeshNode != this; ppMeshNode = &(*ppMeshNode)->m_next)
		ASSERT(*ppMeshNode != NULL);
	*ppMeshNode = m_next;
}

bool CMeshNode::Initialize()
{
	ASSERT(m_dirty);

	if (m_url != NULL && m_url[0] != 0)
		load(m_url);

	Init();

	return m_mesh != NULL;
}

void CMeshNode::Init()
{
	m_dirty = false;
}

extern void SetFalloffShaderValues(const D3DXCOLOR &sideColor, const D3DXCOLOR &frontColor);
extern DWORD GetEffectShader(int nEffect, DWORD fvf);

void CMeshNode::Render()
{
	m_renderTime = TheseusGetNow();

	if (m_dirty && !Initialize())
		return;

	if (m_mesh != NULL)
	{
		// Script-side falloff: alpha XAP scripts use "Mesh { url "foo.xm" falloff 1 }"
		// to trigger the viewing-angle transparency effect on individual meshes.
		// Retail dashboards handle this through MaxMaterial instead.
		if (m_falloff > 0.0f && g_pRenderMeshNode != this)
		{
			DWORD fvf = m_mesh->GetFVF();

			// GetEffectShader returns sentinel (0x80000000 | fvf) that sets
			// m_effectShaderActive=true while preserving real FVF for stride
			TheseusSetVertexShader(GetEffectShader(1, fvf));

			// Tell the fragment shader to use falloff color (not texture/diffuse)
			TheseusSetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_DISABLE);
			TheseusSetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
			TheseusSetRenderState(D3DRS_TEXTUREFACTOR, D3DCOLOR_RGBA(255, 255, 255, 0));

			// Alpha blend instead of additive; prevents wash-out from overlapping layers
			TheseusSetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
			TheseusSetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
			TheseusSetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
			TheseusSetRenderState(D3DRS_ZWRITEENABLE, FALSE);

			// Match retail stock green colors
			// Side = bright green edge glow, Front = transparent center
			SetFalloffShaderValues(
				D3DXCOLOR(0.25f, 0.80f, 0.15f, 0.35f),  // side: green edge, lower alpha
				D3DXCOLOR(0.05f, 0.20f, 0.03f, 0.00f)   // front: very dim, fully transparent
			);
			m_mesh->Render(false);

			// Restore
			TheseusSetRenderState(D3DRS_ZWRITEENABLE, TRUE);
		}
		else
		{
			m_mesh->Render(g_pRenderMeshNode != this);
		}
	}
}

void CMeshNode::load(const TCHAR *szFile)
{
	ASSERT(m_mesh == NULL);

	char szFilePath[MAX_PATH];
	MakeAbsoluteURL(szFilePath, szFile);

	m_mesh = NULL;

	// Skin override first: a skin's mesh wins over the XIP default.
	// No allowlist gate -- UIX-era skins drop entire main-menu mesh
	// packs (70+ files) into the skin folder and expect them to win
	// over the bundled XIP.
	if (g_sSkinDir && g_sSkinDir[0])
	{
		// Probe every member of the equivalence group so a skin
		// shipping just cellwall.xm satisfies a request for
		// Inner_cell-FACES.xm, etc.
		const char *candidates[4];
		int nCandidates = SkinCandidatesFor(szFile, candidates, 4);
		for (int i = 0; i < nCandidates && m_mesh == NULL; i++)
		{
			char SkinMeshPath[MAX_PATH];
			sprintf(SkinMeshPath, "%s%s", g_sSkinDir, candidates[i]);
			CMesh *pSkinMesh = new CMesh;
			if (pSkinMesh->Load(SkinMeshPath))
			{
				m_ownMesh = true;
				m_mesh = pSkinMesh;
			}
			else
			{
				delete pSkinMesh;
			}
		}
	}

	// XIP archive next -- the bundled default for any mesh the
	// active skin doesn't override.
	if (m_mesh == NULL)
	{
		m_mesh = (CMeshCore *)FindObjectInXIP(szFilePath, szFile);
		if (m_mesh != NULL)
			m_ownMesh = false;
	}

	if (m_mesh == NULL)
	{
		m_ownMesh = true;
		CMesh *pMesh = new CMesh;
		if (pMesh->Load(szFilePath))
		{
			m_mesh = pMesh;
		}
		else
		{
			delete pMesh;
			char altPath[MAX_PATH];
			MakePath(altPath, g_szAppDir, szFile);
			pMesh = new CMesh;
			if (pMesh->Load(altPath))
				m_mesh = pMesh;
			else
				delete pMesh;
		}
	}

}

DWORD CMeshNode::GetFVF()
{
	if (m_mesh == NULL)
		return 0;

	return m_mesh->GetFVF();
}

// Drop every loaded mesh and mark every CMeshNode dirty so the next
// Render() pass re-runs load() against the current g_sSkinDir. Called
// from ReloadSkin alongside FlushTextureCache: skin switches change
// what cellwall.xm resolves to, but without this, scenes already on
// screen keep the meshes that were loaded under the previous skin.
//
// XIP-owned meshes (m_ownMesh==false) are not deleted -- the XIP owns
// the buffer and would crash on free. We just unhook the pointer; the
// reload will route back through the normal skin/XIP/disk fallback.
void FlushMeshCache()
{
	for (CMeshNode *pNode = CMeshNode::c_pFirst; pNode != NULL; pNode = pNode->m_next)
	{
		if (pNode->m_mesh != NULL)
		{
			if (pNode->m_ownMesh)
				delete pNode->m_mesh;
			pNode->m_mesh = NULL;
			pNode->m_ownMesh = true;
		}
		// Dirty every node, including ones whose previous load
		// failed (m_mesh stayed NULL). Otherwise a skin missing
		// cellwall.xm leaves those nodes permanently NULL even
		// after the user switches back to a skin that ships it,
		// because FlushMeshCache wouldn't re-dirty them.
		pNode->m_dirty = true;
	}
}

// ============================================================================
// Mesh Factory Functions
// ============================================================================

CMesh *LoadMesh(const char *szFilePath)
{
	CMesh *pMesh = new CMesh;
	pMesh->Load(szFilePath);
	return pMesh;
}

CMesh *CreateMesh(HANDLE hFile)
{
	CMesh *pMesh = new CMesh;
	pMesh->Create(hFile);
	return pMesh;
}

CMesh *CreateMesh(uint8_t *pbContent, uint32_t cbContent)
{
	CMesh *pMesh = new CMesh;
	pMesh->Create(pbContent, cbContent);
	return pMesh;
}

CMesh *MakeSphere(float nRadius, int nSlices, int nStacks)
{
	HRESULT hr;

	CMesh *pMesh = new CMesh;

	pMesh->m_primitiveType = D3DPT_TRIANGLELIST;

	pMesh->m_fvf = D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE;
	pMesh->m_nVertexStride = 3 * sizeof(float) + 3 * sizeof(float) + sizeof(uint32_t);

	LPD3DXMESH pSphere = NULL;
	LPD3DXMESH pClone = NULL;

	hr = D3DXCreateSphere(TheseusGetD3DDev(), nRadius, nSlices, nStacks, &pSphere, NULL);

	if (SUCCEEDED(hr))
	{
		ASSERT(pSphere);
		hr = pSphere->CloneMeshFVF(D3DXMESH_MANAGED, pMesh->m_fvf, TheseusGetD3DDev(), &pClone);
		pSphere->Release();
	}

	if (SUCCEEDED(hr))
	{
		ASSERT(pClone);
		hr = D3DXComputeNormals(pClone);
	}

	if (SUCCEEDED(hr))
	{
		hr = pClone->GetVertexBuffer(&pMesh->m_vertexBuffer);
	}

	if (SUCCEEDED(hr))
	{
		hr = pClone->GetIndexBuffer(&pMesh->m_indexBuffer);
	}

	if (SUCCEEDED(hr))
	{
		pMesh->m_nIndexCount = pClone->GetNumFaces() * 3;
		pMesh->m_nFaceCount = pClone->GetNumFaces();
		pMesh->m_nVertexCount = pClone->GetNumVertices();
	}
	else
	{
		if (pMesh->m_vertexBuffer)
		{
			pMesh->m_vertexBuffer->Release();
		}
		if (pMesh->m_indexBuffer)
		{
			pMesh->m_indexBuffer->Release();
		}
		delete pMesh;
		pMesh = NULL;
	}

	if (pClone)
	{
		pClone->Release();
	}

	// Desktop: D3DX mesh stubs return empty data, skip vertex compression
	if (!pMesh || !pMesh->m_vertexBuffer || pMesh->m_nVertexCount == 0)
	{
		if (pMesh)
		{
			pMesh->m_fvf = D3DFVF_XYZ | D3DFVF_NORMPACKED3 | D3DFVF_DIFFUSE;
			pMesh->m_nVertexStride = 3 * sizeof(float) + sizeof(uint32_t) + sizeof(uint32_t);
		}
		return pMesh;
	}
	LPDIRECT3DVERTEXBUFFER8 pCompressedVertexBuffer;
	uint8_t *pSrc, *pDst;
	uint32_t dwNormal;

	TheseusCreateVertexBuffer(pMesh->m_nVertexCount * (3 * sizeof(float) + 2 * sizeof(uint32_t)), D3DUSAGE_DYNAMIC, 0, D3DPOOL_MANAGED, &pCompressedVertexBuffer);

	pMesh->m_vertexBuffer->Lock(0, 0, &pSrc, 0);
	pCompressedVertexBuffer->Lock(0, 0, &pDst, 0);

	for (int i = 0; i < pMesh->m_nVertexCount; i++)
	{
		memcpy(pDst, pSrc, 3 * sizeof(float));
		pSrc += 3 * sizeof(float);
		pDst += 3 * sizeof(float);
		dwNormal = CompressNormal((float *)pSrc);
		memcpy(pDst, &dwNormal, sizeof(uint32_t));
		pSrc += 3 * sizeof(float);
		pDst += sizeof(uint32_t);
		memcpy(pDst, pSrc, sizeof(uint32_t));
		pSrc += sizeof(uint32_t);
		pDst += sizeof(uint32_t);
	}

	pCompressedVertexBuffer->Unlock();
	pMesh->m_vertexBuffer->Unlock();

	pMesh->m_vertexBuffer->Release();
	pMesh->m_vertexBuffer = pCompressedVertexBuffer;
	pMesh->m_fvf = D3DFVF_XYZ | D3DFVF_NORMPACKED3 | D3DFVF_DIFFUSE;
	pMesh->m_nVertexStride = 3 * sizeof(float) + sizeof(uint32_t) + sizeof(uint32_t);

	return pMesh;
}
