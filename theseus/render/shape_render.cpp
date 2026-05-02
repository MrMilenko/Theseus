// shape_render.cpp: shape nodes, appearance / material rendering, vertex
// shader setup. CShape, CAppearance, CMaterial, CBox, CSphere geometry
// primitives plus falloff / reflection shader configuration and effect
// shader lookup. Decompiled from the 5960 retail XBE; see
// docs/decomp/ShapeRender.md.

#include "std.h"
#include "theseus.h"
#include "node.h"
#include "shape_render.h"
#include "runner.h"
#include "asset_loader.h"
#include "scene_groups.h"
#include "camera.h"

extern class CMeshNode* g_pRenderMeshNode;
extern D3DXMATRIX g_matPosition;
extern D3DXMATRIX g_matView;
extern D3DXMATRIX g_matProjection;

// ===== CShape =====

IMPLEMENT_NODE("Shape", CShape, CNode)

START_NODE_PROPS(CShape, CNode)
	NODE_PROP(pt_node, CShape, appearance)
	NODE_PROP(pt_node, CShape, geometry)
END_NODE_PROPS()

CShape::CShape()
{
	m_appearance = NULL;
	m_geometry = NULL;
}

CShape::~CShape()
{
	if (m_appearance != NULL)
		m_appearance->Release();

	if (m_geometry != NULL)
		m_geometry->Release();
}

void CShape::Render()
{
	if (m_geometry == NULL)
		return;

	CMeshNode* pMeshNode = NULL;
	if (m_geometry->IsKindOf(NODE_CLASS(CMeshNode)))
	{
		pMeshNode = (CMeshNode*)m_geometry;

		if (pMeshNode->m_dirty && !pMeshNode->Initialize())
			return;

		DWORD fvf = pMeshNode->GetFVF();
		if (fvf == 0)
			return;

		g_pRenderMeshNode = pMeshNode;

		TheseusSetVertexShader(GetFixedFunctionShader(fvf));
	}

	if (m_appearance != NULL)
		m_appearance->Render();

	m_geometry->Render();

	g_pRenderMeshNode = NULL;

	{
		D3DXMATRIX mat;
		TheseusGetTransform(D3DTS_WORLD, &mat);

		m_position.x = mat.m[3][0];
		m_position.y = mat.m[3][1];
		m_position.z = mat.m[3][2];
	}
}

void CShape::GetBBox(BBox* pBBox)
{
	if (m_geometry != NULL)
		m_geometry->GetBBox(pBBox);
	else
		CNode::GetBBox(pBBox);
}

float CShape::GetRadius()
{
	if (m_geometry == NULL)
		return 0.0f;

	return m_geometry->GetRadius();
}

void CShape::Advance(float nSeconds)
{
	CNode::Advance(nSeconds);

	if (m_geometry != NULL)
		m_geometry->Advance(nSeconds);

	if (m_appearance != NULL)
		m_appearance->Advance(nSeconds);
}

// ===== CAppearance =====

IMPLEMENT_NODE("Appearance", CAppearance, CNode)

START_NODE_PROPS(CAppearance, CNode)
	NODE_PROP(pt_node, CAppearance, material)
	NODE_PROP(pt_node, CAppearance, texture)
END_NODE_PROPS()

CAppearance::CAppearance()
{
	m_material = NULL;
	m_texture = NULL;
}

CAppearance::~CAppearance()
{
	if (m_material != NULL)
		m_material->Release();

	if (m_texture != NULL)
		m_texture->Release();
}

void CAppearance::Render()
{
	TheseusSetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);

	TheseusSetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	TheseusSetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	TheseusSetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	TheseusSetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

	TheseusSetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_DISABLE);
	TheseusSetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	TheseusSetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
	TheseusSetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	TheseusSetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	TheseusSetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

	TheseusSetTexture(0, NULL);
	TheseusSetTexture(1, NULL);

	if (m_texture != NULL)
	{
		m_texture->Render();

		LPDIRECT3DTEXTURE8 pSurface = m_texture->GetTextureSurface();
		if (pSurface != NULL)
		{
			TheseusSetTexture(0, pSurface);

			TheseusSetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
			TheseusSetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
			TheseusSetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
			TheseusSetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		}
		else
		{
			TheseusSetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
			TheseusSetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TFACTOR);
			TheseusSetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
			TheseusSetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TFACTOR);
			TheseusSetRenderState(D3DRS_TEXTUREFACTOR, D3DCOLOR_COLORVALUE(0.0f, 0.0f, 0.0f, 0.0f));
		}
	}

	if (m_material != NULL)
		m_material->Render();
}

void CAppearance::Advance(float nSeconds)
{
	CNode::Advance(nSeconds);

	if (m_material != NULL)
		m_material->Advance(nSeconds);

	if (m_texture != NULL)
		m_texture->Advance(nSeconds);
}

// ===== CMaterial =====

IMPLEMENT_NODE("Material", CMaterial, CNode)

START_NODE_PROPS(CMaterial, CNode)
	NODE_PROP(pt_number, CMaterial, ambientIntensity)
	NODE_PROP(pt_color, CMaterial, diffuseColor)
	NODE_PROP(pt_color, CMaterial, emissiveColor)
	NODE_PROP(pt_number, CMaterial, shininess)
	NODE_PROP(pt_color, CMaterial, specularColor)
	NODE_PROP(pt_number, CMaterial, transparency)
END_NODE_PROPS()

CMaterial::CMaterial() :
	m_ambientIntensity(0.2f),
	m_diffuseColor(0.8f, 0.8f, 0.8f),
	m_emissiveColor(0.0f, 0.0f, 0.0f),
	m_shininess(0.2f),
	m_specularColor(0.0f, 0.0f, 0.0f),
	m_transparency(0.0f)
{
}

void CMaterial::Render()
{
	m_material.Diffuse.r = m_diffuseColor.x;
	m_material.Diffuse.g = m_diffuseColor.y;
	m_material.Diffuse.b = m_diffuseColor.z;
	m_material.Diffuse.a = 1.0f - m_transparency;

	m_material.Ambient.r = m_ambientIntensity;
	m_material.Ambient.g = m_ambientIntensity;
	m_material.Ambient.b = m_ambientIntensity;
	m_material.Ambient.a = 1.0f - m_transparency;

	m_material.Specular.r = m_specularColor.x;
	m_material.Specular.g = m_specularColor.y;
	m_material.Specular.b = m_specularColor.z;
	m_material.Specular.a = 1.0f - m_transparency;

	m_material.Emissive.r = m_emissiveColor.x;
	m_material.Emissive.g = m_emissiveColor.y;
	m_material.Emissive.b = m_emissiveColor.z;
	m_material.Emissive.a = 1.0f - m_transparency;

	m_material.Power = m_shininess;

	TheseusSetMaterial(&m_material);
}

// ===== CreateCube =====

void CreateCube(D3DVERTEX* pVertices, WORD* pIndices, D3DXVECTOR3 size, bool bInside = false)
{
    D3DXVECTOR3 n0( 0.0f, 0.0f,-1.0f );
    D3DXVECTOR3 n1( 0.0f, 0.0f, 1.0f );
    D3DXVECTOR3 n2( 0.0f, 1.0f, 0.0f );
    D3DXVECTOR3 n3( 0.0f,-1.0f, 0.0f );
    D3DXVECTOR3 n4( 1.0f, 0.0f, 0.0f );
    D3DXVECTOR3 n5(-1.0f, 0.0f, 0.0f );

	if (bInside)
	{
		*pVertices++ = D3DVERTEX(D3DXVECTOR3(-0.5f * size.x, 0.5f * size.y,-0.5f * size.z), n0, 0.01f, 0.99f);
		*pVertices++ = D3DVERTEX(D3DXVECTOR3( 0.5f * size.x, 0.5f * size.y,-0.5f * size.z), n0, 0.99f, 0.99f);
		*pVertices++ = D3DVERTEX(D3DXVECTOR3( 0.5f * size.x,-0.5f * size.y,-0.5f * size.z), n0, 0.99f, 0.01f);
		*pVertices++ = D3DVERTEX(D3DXVECTOR3(-0.5f * size.x,-0.5f * size.y,-0.5f * size.z), n0, 0.01f, 0.01f);

		*pVertices++ = D3DVERTEX(D3DXVECTOR3(-0.5f * size.x, 0.5f * size.y, 0.5f * size.z), n1, 0.99f, 0.99f);
		*pVertices++ = D3DVERTEX(D3DXVECTOR3(-0.5f * size.x,-0.5f * size.y, 0.5f * size.z), n1, 0.99f, 0.01f);
		*pVertices++ = D3DVERTEX(D3DXVECTOR3( 0.5f * size.x,-0.5f * size.y, 0.5f * size.z), n1, 0.01f, 0.01f);
		*pVertices++ = D3DVERTEX(D3DXVECTOR3( 0.5f * size.x, 0.5f * size.y, 0.5f * size.z), n1, 0.01f, 0.99f);

		*pVertices++ = D3DVERTEX(D3DXVECTOR3(-0.5f * size.x, 0.5f * size.y, 0.5f * size.z), n2, 0.01f, 0.99f);
		*pVertices++ = D3DVERTEX(D3DXVECTOR3( 0.5f * size.x, 0.5f * size.y, 0.5f * size.z), n2, 0.99f, 0.99f);
		*pVertices++ = D3DVERTEX(D3DXVECTOR3( 0.5f * size.x, 0.5f * size.y,-0.5f * size.z), n2, 0.99f, 0.01f);
		*pVertices++ = D3DVERTEX(D3DXVECTOR3(-0.5f * size.x, 0.5f * size.y,-0.5f * size.z), n2, 0.01f, 0.01f);

		*pVertices++ = D3DVERTEX(D3DXVECTOR3(-0.5f * size.x,-0.5f * size.y, 0.5f * size.z), n3, 0.01f, 0.99f);
		*pVertices++ = D3DVERTEX(D3DXVECTOR3(-0.5f * size.x,-0.5f * size.y,-0.5f * size.z), n3, 0.01f, 0.01f);
		*pVertices++ = D3DVERTEX(D3DXVECTOR3( 0.5f * size.x,-0.5f * size.y,-0.5f * size.z), n3, 0.99f, 0.01f);
		*pVertices++ = D3DVERTEX(D3DXVECTOR3( 0.5f * size.x,-0.5f * size.y, 0.5f * size.z), n3, 0.99f, 0.99f);

		*pVertices++ = D3DVERTEX(D3DXVECTOR3( 0.5f * size.x, 0.5f * size.y,-0.5f * size.z), n4, 0.01f, 0.99f);
		*pVertices++ = D3DVERTEX(D3DXVECTOR3( 0.5f * size.x, 0.5f * size.y, 0.5f * size.z), n4, 0.99f, 0.99f);
		*pVertices++ = D3DVERTEX(D3DXVECTOR3( 0.5f * size.x,-0.5f * size.y, 0.5f * size.z), n4, 0.99f, 0.01f);
		*pVertices++ = D3DVERTEX(D3DXVECTOR3( 0.5f * size.x,-0.5f * size.y,-0.5f * size.z), n4, 0.01f, 0.01f);

		*pVertices++ = D3DVERTEX(D3DXVECTOR3(-0.5f * size.x, 0.5f * size.y,-0.5f * size.z), n5, 0.99f, 0.99f);
		*pVertices++ = D3DVERTEX(D3DXVECTOR3(-0.5f * size.x,-0.5f * size.y,-0.5f * size.z), n5, 0.99f, 0.01f);
		*pVertices++ = D3DVERTEX(D3DXVECTOR3(-0.5f * size.x,-0.5f * size.y, 0.5f * size.z), n5, 0.01f, 0.01f);
		*pVertices++ = D3DVERTEX(D3DXVECTOR3(-0.5f * size.x, 0.5f * size.y, 0.5f * size.z), n5, 0.01f, 0.99f);
	}
	else
	{
		*pVertices++ = D3DVERTEX(D3DXVECTOR3(-0.5f * size.x,-0.5f * size.y,-0.5f * size.z), n0, 0.01f, 0.01f);
		*pVertices++ = D3DVERTEX(D3DXVECTOR3( 0.5f * size.x,-0.5f * size.y,-0.5f * size.z), n0, 0.99f, 0.01f);
		*pVertices++ = D3DVERTEX(D3DXVECTOR3( 0.5f * size.x, 0.5f * size.y,-0.5f * size.z), n0, 0.99f, 0.99f);
		*pVertices++ = D3DVERTEX(D3DXVECTOR3(-0.5f * size.x, 0.5f * size.y,-0.5f * size.z), n0, 0.01f, 0.99f);

		*pVertices++ = D3DVERTEX(D3DXVECTOR3( 0.5f * size.x, 0.5f * size.y, 0.5f * size.z), n1, 0.01f, 0.99f);
		*pVertices++ = D3DVERTEX(D3DXVECTOR3( 0.5f * size.x,-0.5f * size.y, 0.5f * size.z), n1, 0.01f, 0.01f);
		*pVertices++ = D3DVERTEX(D3DXVECTOR3(-0.5f * size.x,-0.5f * size.y, 0.5f * size.z), n1, 0.99f, 0.01f);
		*pVertices++ = D3DVERTEX(D3DXVECTOR3(-0.5f * size.x, 0.5f * size.y, 0.5f * size.z), n1, 0.99f, 0.99f);

		*pVertices++ = D3DVERTEX(D3DXVECTOR3(-0.5f * size.x, 0.5f * size.y,-0.5f * size.z), n2, 0.01f, 0.01f);
		*pVertices++ = D3DVERTEX(D3DXVECTOR3( 0.5f * size.x, 0.5f * size.y,-0.5f * size.z), n2, 0.99f, 0.01f);
		*pVertices++ = D3DVERTEX(D3DXVECTOR3( 0.5f * size.x, 0.5f * size.y, 0.5f * size.z), n2, 0.99f, 0.99f);
		*pVertices++ = D3DVERTEX(D3DXVECTOR3(-0.5f * size.x, 0.5f * size.y, 0.5f * size.z), n2, 0.01f, 0.99f);

		*pVertices++ = D3DVERTEX(D3DXVECTOR3( 0.5f * size.x,-0.5f * size.y, 0.5f * size.z), n3, 0.99f, 0.99f);
		*pVertices++ = D3DVERTEX(D3DXVECTOR3( 0.5f * size.x,-0.5f * size.y,-0.5f * size.z), n3, 0.99f, 0.01f);
		*pVertices++ = D3DVERTEX(D3DXVECTOR3(-0.5f * size.x,-0.5f * size.y,-0.5f * size.z), n3, 0.01f, 0.01f);
		*pVertices++ = D3DVERTEX(D3DXVECTOR3(-0.5f * size.x,-0.5f * size.y, 0.5f * size.z), n3, 0.01f, 0.99f);

		*pVertices++ = D3DVERTEX(D3DXVECTOR3( 0.5f * size.x,-0.5f * size.y,-0.5f * size.z), n4, 0.01f, 0.01f);
		*pVertices++ = D3DVERTEX(D3DXVECTOR3( 0.5f * size.x,-0.5f * size.y, 0.5f * size.z), n4, 0.99f, 0.01f);
		*pVertices++ = D3DVERTEX(D3DXVECTOR3( 0.5f * size.x, 0.5f * size.y, 0.5f * size.z), n4, 0.99f, 0.99f);
		*pVertices++ = D3DVERTEX(D3DXVECTOR3( 0.5f * size.x, 0.5f * size.y,-0.5f * size.z), n4, 0.01f, 0.99f);

		*pVertices++ = D3DVERTEX(D3DXVECTOR3(-0.5f * size.x, 0.5f * size.y, 0.5f * size.z), n5, 0.01f, 0.99f);
		*pVertices++ = D3DVERTEX(D3DXVECTOR3(-0.5f * size.x,-0.5f * size.y, 0.5f * size.z), n5, 0.01f, 0.01f);
		*pVertices++ = D3DVERTEX(D3DXVECTOR3(-0.5f * size.x,-0.5f * size.y,-0.5f * size.z), n5, 0.99f, 0.01f);
		*pVertices++ = D3DVERTEX(D3DXVECTOR3(-0.5f * size.x, 0.5f * size.y,-0.5f * size.z), n5, 0.99f, 0.99f);
	}

    *pIndices++ =  0+0;   *pIndices++ =  0+1;   *pIndices++ =  0+2;
    *pIndices++ =  0+2;   *pIndices++ =  0+3;   *pIndices++ =  0+0;
    *pIndices++ =  4+0;   *pIndices++ =  4+1;   *pIndices++ =  4+2;
    *pIndices++ =  4+2;   *pIndices++ =  4+3;   *pIndices++ =  4+0;
    *pIndices++ =  8+0;   *pIndices++ =  8+1;   *pIndices++ =  8+2;
    *pIndices++ =  8+2;   *pIndices++ =  8+3;   *pIndices++ =  8+0;
    *pIndices++ = 12+0;   *pIndices++ = 12+1;   *pIndices++ = 12+2;
    *pIndices++ = 12+2;   *pIndices++ = 12+3;   *pIndices++ = 12+0;
    *pIndices++ = 16+0;   *pIndices++ = 16+1;   *pIndices++ = 16+2;
    *pIndices++ = 16+2;   *pIndices++ = 16+3;   *pIndices++ = 16+0;
    *pIndices++ = 20+0;   *pIndices++ = 20+1;   *pIndices++ = 20+2;
    *pIndices++ = 20+2;   *pIndices++ = 20+3;   *pIndices++ = 20+0;
}

// ===== CBox =====

class CBox : public CNode
{
	DECLARE_NODE(CBox, CNode)
public:
	CBox();
	~CBox();

	void Render();
	void GetBBox(BBox* pBBox);
	float GetRadius();

	D3DXVECTOR3 m_size;

	IDirect3DVertexBuffer8* m_pVB;
	IDirect3DIndexBuffer8* m_pIB;

#define NUM_CUBE_VERTICES (4*6)
#define NUM_CUBE_INDICES  (6*6)

	D3DVERTEX m_pCubeVertices [NUM_CUBE_VERTICES];
	WORD m_pCubeIndices [NUM_CUBE_INDICES];

	bool m_dirty;

	DECLARE_NODE_PROPS()
};

IMPLEMENT_NODE("Box", CBox, CNode)

START_NODE_PROPS(CBox, CNode)
	NODE_PROP(pt_vec3, CBox, size)
END_NODE_PROPS()

CBox::CBox() :
	m_size(1.0f, 1.0f, 1.0f)
{
	m_dirty = true;

	m_pVB = NULL;
	m_pIB = NULL;
}

CBox::~CBox()
{
	if (m_pVB != NULL)
		m_pVB->Release();

	if (m_pIB != NULL)
		m_pIB->Release();
}

void CBox::Render()
{
	if (m_dirty)
	{
		TRACE(_T("Creating a cube...\n"));
		CreateCube(m_pCubeVertices, m_pCubeIndices, m_size);
		m_dirty = false;
	}

	if (m_pVB == NULL)
	{
		void* pVerts;
		TheseusCreateVertexBuffer(NUM_CUBE_VERTICES * sizeof (D3DVERTEX), D3DUSAGE_DYNAMIC, D3DFVF_VERTEX, D3DPOOL_MANAGED, &m_pVB);

		VERIFYHR(m_pVB->Lock(0, NUM_CUBE_VERTICES * sizeof (D3DVERTEX), (BYTE**)&pVerts, 0));
		CopyMemory(pVerts, m_pCubeVertices, NUM_CUBE_VERTICES * sizeof (D3DVERTEX));
		VERIFYHR(m_pVB->Unlock());
	}

	if (m_pIB == NULL)
	{
		void* pIndices;
		TheseusCreateIndexBuffer(NUM_CUBE_INDICES * sizeof (WORD), D3DUSAGE_DYNAMIC, D3DFMT_INDEX16, D3DPOOL_MANAGED, &m_pIB);
		VERIFYHR(m_pIB->Lock(0, NUM_CUBE_INDICES * sizeof (WORD), (BYTE**)&pIndices, 0));
		CopyMemory(pIndices, m_pCubeIndices, NUM_CUBE_INDICES * sizeof (WORD));
		VERIFYHR(m_pIB->Unlock());
	}

	if (m_pVB != NULL && m_pIB != NULL)
	{
		TheseusSetStreamSource(0, m_pVB, sizeof (D3DVERTEX));
		TheseusSetIndices(m_pIB, 0);
		TheseusSetVertexShader(D3DFVF_VERTEX);
	    TheseusDrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, NUM_CUBE_VERTICES, 0, 12);
	}
}

void CBox::GetBBox(BBox* pBBox)
{
	pBBox->center.x = 0.0f;
	pBBox->center.y = 0.0f;
	pBBox->center.z = 0.0f;
	pBBox->size = m_size;
}

float CBox::GetRadius()
{
	return D3DXVec3Length(&m_size) / 2.0f;
}

// ===== CSphere =====

extern class CMesh* MakeSphere(float nRadius, int nSlices, int nStacks);

class CSphere : public CMeshNode
{
	DECLARE_NODE(CSphere, CMeshNode)
public:
	CSphere();
	~CSphere();

	float m_radius;
	int m_slices;
	int m_stacks;

	void Init();

	DECLARE_NODE_PROPS()
};

IMPLEMENT_NODE("Sphere", CSphere, CMeshNode)

START_NODE_PROPS(CSphere, CMeshNode)
	NODE_PROP(pt_number, CSphere, radius)
	NODE_PROP(pt_integer, CSphere, slices)
	NODE_PROP(pt_integer, CSphere, stacks)
END_NODE_PROPS()

CSphere::CSphere() :
	m_radius(1.0f),
	m_slices(32),
	m_stacks(32)
{
	m_dirty = true;
}

CSphere::~CSphere()
{
}

void CSphere::Init()
{
	m_mesh = MakeSphere(m_radius, m_slices, m_stacks);

    if (m_mesh)
    {
        m_dirty = false;
    }
}

// ===== Shader Descriptors =====
// Xbox-only: real NV2A vertex shader bytecode + the GetEffectShader/
// GetFixedFunctionShader/CompressNormal/SetFalloffShader/etc helpers
// that drive it. Desktop has GLSL-sentinel equivalents in falloff.cpp
// because the desktop GL emulator can't run NV2A microcode.
#ifdef _XBOX

DWORD dwEffectVertexShader[] = {
	0x00152078,
	0x00000000, 0x00ec001b, 0x0836186c, 0x20708800,
	0x00000000, 0x00ed401b, 0x0836186c, 0x28200ff8,
	0x00000000, 0x00aca61b, 0x0836186c, 0x28300ff8,
	0x00000000, 0x00ec201b, 0x0836186c, 0x20704800,
	0x00000000, 0x00ed601b, 0x0836186c, 0x24200ff8,
	0x00000000, 0x00acc61b, 0x0836186c, 0x24300ff8,
	0x00000000, 0x00ec401b, 0x0836186c, 0x20702800,
	0x00000000, 0x00ed801b, 0x0836186c, 0x22200ff8,
	0x00000000, 0x00ace61b, 0x0836186c, 0x22300ff8,
	0x00000000, 0x00ec601b, 0x0836186c, 0x20701800,
	0x00000000, 0x00a0001b, 0x3436686c, 0x21400ff8,
	0x00000000, 0x002e001b, 0x0c36106c, 0x2fa00ff8,
	0x00000000, 0x08a0001b, 0x24364bfd, 0x11510ff8,
	0x00000000, 0x0647401b, 0xc4361bff, 0x1078e800,
	0x00000000, 0x0800001b, 0x083613fd, 0x50610ff8,
	0x00000000, 0x0040001b, 0x35fe286c, 0x2e700ff8,
	0x00000000, 0x0040001b, 0x25fec86c, 0x2e800ff8,
	0x00000000, 0x00a0001b, 0x7437086c, 0x21900ff8,
	0x00000000, 0x0087601b, 0xc400286c, 0x3070e800,
	0x00000000, 0x014000ff, 0x97ff286c, 0x21b00ff8,
	0x00000000, 0x008de01b, 0xa5ff686c, 0x3070f819
};

DWORD dwEffect2VertexShader[] = {
	0x00152078,
	0x00000000, 0x00ec001b, 0x0836186c, 0x20708800,
	0x00000000, 0x00ed401b, 0x0836186c, 0x28200ff8,
	0x00000000, 0x00aca61b, 0x0836186c, 0x28300ff8,
	0x00000000, 0x00ec201b, 0x0836186c, 0x20704800,
	0x00000000, 0x00ed601b, 0x0836186c, 0x24200ff8,
	0x00000000, 0x00acc61b, 0x0836186c, 0x24300ff8,
	0x00000000, 0x00ec401b, 0x0836186c, 0x20702800,
	0x00000000, 0x00ed801b, 0x0836186c, 0x22200ff8,
	0x00000000, 0x00ace61b, 0x0836186c, 0x22300ff8,
	0x00000000, 0x00ec601b, 0x0836186c, 0x20701800,
	0x00000000, 0x02a00e18, 0x3430686c, 0x2140f84c,
	0x00000000, 0x002e001b, 0x0c36106c, 0x2fa00ff8,
	0x00000000, 0x08a00018, 0x24304bfd, 0x11510ff8,
	0x00000000, 0x0647401b, 0xc4361bff, 0x1078e800,
	0x00000000, 0x0800001b, 0x083613fd, 0x50610ff8,
	0x00000000, 0x00400018, 0x35fe286c, 0x2e700ff8,
	0x00000000, 0x00400018, 0x25fec86c, 0x2e800ff8,
	0x00000000, 0x00a00018, 0x7431086c, 0x21900ff8,
	0x00000000, 0x0087601b, 0xc400286c, 0x3070e800,
	0x00000000, 0x014000ff, 0x97ff286c, 0x21b00ff8,
	0x00000000, 0x008de01b, 0xa5ff686c, 0x3070f819
};

DWORD dwEffect3VertexShader[] = {
	0x00152078,
	0x00000000, 0x00ec001b, 0x0836186c, 0x20708800,
	0x00000000, 0x00ed401b, 0x0836186c, 0x28200ff8,
	0x00000000, 0x00aca61b, 0x0836186c, 0x28300ff8,
	0x00000000, 0x00ec201b, 0x0836186c, 0x20704800,
	0x00000000, 0x00ed601b, 0x0836186c, 0x24200ff8,
	0x00000000, 0x00acc61b, 0x0836186c, 0x24300ff8,
	0x00000000, 0x00ec401b, 0x0836186c, 0x20702800,
	0x00000000, 0x00ed801b, 0x0836186c, 0x22200ff8,
	0x00000000, 0x00ace61b, 0x0836186c, 0x22300ff8,
	0x00000000, 0x00ec601b, 0x0836186c, 0x20701800,
	0x00000000, 0x02a00c1b, 0x3436686c, 0x2140f84c,
	0x00000000, 0x002e001b, 0x0c36106c, 0x2fa00ff8,
	0x00000000, 0x08a0001b, 0x24364bfd, 0x11510ff8,
	0x00000000, 0x0647401b, 0xc4361bff, 0x1078e800,
	0x00000000, 0x0800001b, 0x083613fd, 0x50610ff8,
	0x00000000, 0x0040001b, 0x35fe286c, 0x2e700ff8,
	0x00000000, 0x0040001b, 0x25fec86c, 0x2e800ff8,
	0x00000000, 0x00a0001b, 0x7437086c, 0x21900ff8,
	0x00000000, 0x0087601b, 0xc400286c, 0x3070e800,
	0x00000000, 0x014000ff, 0x97ff286c, 0x21b00ff8,
	0x00000000, 0x008de01b, 0xa5ff686c, 0x3070f819
};

DWORD dwEffect4VertexShader[] = {
	0x001d2078,
	0x00000000, 0x00f1001b, 0x0836186c, 0x28200ff8,
	0x00000000, 0x00f1201b, 0x0836186c, 0x24200ff8,
	0x00000000, 0x00f1401b, 0x0836186c, 0x22200ff8,
	0x00000000, 0x00f1601b, 0x0836186c, 0x21200ff8,
	0x00000000, 0x00b1061b, 0x0836186c, 0x28300ff8,
	0x00000000, 0x00b1261b, 0x0836186c, 0x24300ff8,
	0x00000000, 0x00b1461b, 0x0836186c, 0x22300ff8,
	0x00000000, 0x00f1801b, 0x2436186c, 0x20708800,
	0x00000000, 0x00f1a01b, 0x2436186c, 0x20704800,
	0x00000000, 0x00f1c01b, 0x2436186c, 0x20702800,
	0x00000000, 0x00f1e01b, 0x2436186c, 0x20701800,
	0x00000000, 0x00a0001b, 0x3436686c, 0x21400ff8,
	0x00000000, 0x00b2261b, 0x0836186c, 0x21800ff8,
	0x00000000, 0x08a0001b, 0x24364bfd, 0x11610ff8,
	0x00000000, 0x002e001b, 0x0c36106c, 0x2f900ff8,
	0x00000000, 0x0040001b, 0x35fe286c, 0x2e500ff8,
	0x00000000, 0x0940001b, 0x86370bfd, 0x91a10ff8,
	0x00000000, 0x008de01b, 0x95ff486c, 0x3070f818,
	0x00000000, 0x0040001b, 0x25fe286c, 0x2e700ff8,
	0x00000000, 0x00a0001b, 0x7436a86c, 0x21b00ff8,
	0x00000000, 0x0647401b, 0xc4361bff, 0x1078e800,
	0x00000000, 0x006000ff, 0xb43613fe, 0xd1000ff8,
	0x00000000, 0x0087601b, 0xc400286c, 0x3070e800,
	0x00000000, 0x008000ff, 0x0434ac69, 0xde200ff8,
	0x00000000, 0x0072001a, 0x0c361068, 0x9e300ff8,
	0x00000000, 0x00a0001b, 0x3436686c, 0x21400ff8,
	0x00000000, 0x0800001b, 0x083613fd, 0x10110ff8,
	0x00000000, 0x005200ff, 0x15fe186c, 0x21500ff8,
	0x00000000, 0x0092001a, 0x25feabfc, 0x3070e849
};

DWORD dwAnisoVertexShader[] = {
	0x000a2078,
	0x00000000, 0x00ec001b, 0x0836186c, 0x20708800,
	0x00000000, 0x00ec201b, 0x0836186c, 0x20704800,
	0x00000000, 0x00ec401b, 0x0836186c, 0x20702800,
	0x00000000, 0x00ec601b, 0x0836186c, 0x20701800,
	0x00000000, 0x00ac861b, 0x0836186c, 0x21200ff8,
	0x00000000, 0x002e001b, 0x0c36106c, 0x2f300ff8,
	0x00000000, 0x014000ff, 0x27fe486c, 0x21400ff8,
	0x00000000, 0x008de01b, 0x35fe886c, 0x3070f818,
	0x00000000, 0x0647401b, 0xc4361bff, 0x1078e800,
	0x00000000, 0x0087601b, 0xc400286c, 0x3070e801
};

struct SHADEDESC
{
	int m_nEffect;
	DWORD* m_rgdwMicrocode;
	DWORD m_fvf;
	DWORD m_dwShader;
	bool m_bReportedError;
};

#define SHADERNAME(name) dw##name##VertexShader

SHADEDESC fixed_shaders [] =
{
	{
		0,
		SHADERNAME(Effect),
		D3DFVF_XYZ | D3DFVF_NORMPACKED3 | D3DFVF_DIFFUSE
	},
	{
		1,
		SHADERNAME(Effect),
		D3DFVF_XYZ | D3DFVF_NORMPACKED3
	},
	{
		2,
		SHADERNAME(Effect),
		D3DFVF_XYZ | D3DFVF_NORMPACKED3 | D3DFVF_TEX1
	},
};

SHADEDESC shaders [] =
{
	{
		1,
		SHADERNAME(Effect),
		D3DFVF_XYZ | D3DFVF_NORMPACKED3 | D3DFVF_DIFFUSE
	},
	{
		1,
		SHADERNAME(Effect),
		D3DFVF_XYZ | D3DFVF_NORMPACKED3
	},
	{
		1,
		SHADERNAME(Effect2),
		D3DFVF_XYZ | D3DFVF_NORMPACKED3 | D3DFVF_TEX1
	},
	{
		2,
		SHADERNAME(Aniso),
		D3DFVF_XYZ | D3DFVF_NORMPACKED3
	},
	{
		3,
		SHADERNAME(Effect3),
		D3DFVF_XYZ | D3DFVF_NORMPACKED3 | D3DFVF_TEX1
	},
	{
		4,
		SHADERNAME(Effect4),
		D3DFVF_XYZ | D3DFVF_NORMPACKED3
	},
	{
		1,
		SHADERNAME(Effect),
		D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE
	},
	{
		1,
		SHADERNAME(Effect),
		D3DFVF_XYZ | D3DFVF_NORMAL
	},
	{
		1,
		SHADERNAME(Effect2),
		D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1
	},
	{
		2,
		SHADERNAME(Aniso),
		D3DFVF_XYZ | D3DFVF_NORMAL
	},
	{
		3,
		SHADERNAME(Effect3),
		D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1
	},
	{
		4,
		SHADERNAME(Effect4),
		D3DFVF_XYZ | D3DFVF_NORMAL
	},
};

// ===== Effect Shader Lookup =====

DWORD GetEffectShader(int nEffect, DWORD fvf)
{
	if (nEffect == 0)
		return fvf;

	SHADEDESC* pShadeDesc = shaders;
	for (int i = 0; pShadeDesc->m_nEffect != nEffect || pShadeDesc->m_fvf != fvf; i += 1, pShadeDesc += 1)
	{
		if (i >= countof (shaders) - 1)
			return fvf;
	}

	if (pShadeDesc->m_dwShader == 0)
	{
		const DWORD* rgdwFunction = NULL;

		rgdwFunction = pShadeDesc->m_rgdwMicrocode;

		static DWORD decl1 [] =
		{
			D3DVSD_STREAM( 0 ),
			D3DVSD_REG( 0, D3DVSDT_FLOAT3 ),
			D3DVSD_REG( 3, D3DVSDT_NORMPACKED3 ),
			D3DVSD_END()
		};

		static DWORD decl2 [] =
		{
			D3DVSD_STREAM( 0 ),
			D3DVSD_REG( 0, D3DVSDT_FLOAT3 ),
			D3DVSD_REG( 3, D3DVSDT_NORMPACKED3 ),
			D3DVSD_REG( 6, D3DVSDT_FLOAT2 ),
			D3DVSD_END()
		};

		static DWORD decl3 [] =
		{
			D3DVSD_STREAM( 0 ),
			D3DVSD_REG( 0, D3DVSDT_FLOAT3 ),
			D3DVSD_REG( 3, D3DVSDT_FLOAT3 ),
			D3DVSD_END()
		};

		static DWORD decl4 [] =
		{
			D3DVSD_STREAM( 0 ),
			D3DVSD_REG( 0, D3DVSDT_FLOAT3 ),
			D3DVSD_REG( 3, D3DVSDT_FLOAT3 ),
			D3DVSD_REG( 6, D3DVSDT_FLOAT2 ),
			D3DVSD_END()
		};

		if (fvf & D3DFVF_NORMPACKED3)
		{
			TheseusCreateVertexShader((fvf & D3DFVF_TEX1) ? decl2 : decl1, rgdwFunction, &pShadeDesc->m_dwShader, 0);
		}
		else
		{
			TheseusCreateVertexShader((fvf & D3DFVF_TEX1) ? decl4 : decl3, rgdwFunction, &pShadeDesc->m_dwShader, 0);
		}
	}

	return pShadeDesc->m_dwShader;
}

// ===== Fixed Function Shader Lookup =====

DWORD GetFixedFunctionShader(DWORD fvf)
{
	SHADEDESC* pShadeDesc = fixed_shaders;
	int i;
	for (i = 0; pShadeDesc->m_fvf != fvf; i += 1, pShadeDesc += 1)
	{
		if (i >= countof (fixed_shaders) - 1)
			return fvf;
	}

	if (pShadeDesc->m_dwShader == 0)
	{
		static DWORD decl[3][5] =
		{
			{
				D3DVSD_STREAM( 0 ),
				D3DVSD_REG( D3DVSDE_POSITION, D3DVSDT_FLOAT3 ),
				D3DVSD_REG( D3DVSDE_NORMAL, D3DVSDT_NORMPACKED3 ),
				D3DVSD_REG( D3DVSDE_DIFFUSE, D3DVSDT_D3DCOLOR ),
				D3DVSD_END()
			},
			{
				D3DVSD_STREAM( 0 ),
				D3DVSD_REG( D3DVSDE_POSITION, D3DVSDT_FLOAT3 ),
				D3DVSD_REG( D3DVSDE_NORMAL, D3DVSDT_NORMPACKED3 ),
				D3DVSD_END()
			},
			{
				D3DVSD_STREAM( 0 ),
				D3DVSD_REG( D3DVSDE_POSITION, D3DVSDT_FLOAT3 ),
				D3DVSD_REG( D3DVSDE_NORMAL, D3DVSDT_NORMPACKED3 ),
				D3DVSD_REG( D3DVSDE_TEXCOORD0, D3DVSDT_FLOAT2 ),
				D3DVSD_END()
			},
		};

		TheseusCreateVertexShader(&decl[i][0], NULL, &pShadeDesc->m_dwShader, 0);
	}

	return pShadeDesc->m_dwShader;
}

#else // !_XBOX, desktop GLSL-sentinel versions

// Desktop OpenGL has no NV2A vertex shaders. GetEffectShader returns a
// sentinel (high bit set + real FVF) that tells the d3d8_sdl shim's
// SetVertexShader to flip m_effectShaderActive while keeping the FVF
// for vertex attribute layout. GetFixedFunctionShader passes through.
DWORD GetEffectShader(int nEffect, DWORD fvf)
{
	if (nEffect == 0)
		return fvf;
	return 0x80000000 | fvf;
}

DWORD GetFixedFunctionShader(DWORD fvf)
{
	return fvf;
}

#endif // _XBOX

// ===== Normal Compression =====
// Below this point is portable D3DX math, with one wrinkle: the float-to-DWORD
// conversion of negative normal components is undefined behavior in C, so the
// two platforms cast differently. Xbox MSVC i386 happens to do the right thing
// with a direct (DWORD)(float) cast; clang on desktop produces 0 for negatives,
// which corrupts compressed normals and breaks the falloff edge-glow shader.
// Route through (int) first on desktop so the truncation is well-defined.

DWORD CompressNormal(float* pvNormal) {

    float vNormal[3];
    float fLength;

    fLength = (float)sqrt(pvNormal[0] * pvNormal[0] + pvNormal[1] * pvNormal[1] + pvNormal[2] * pvNormal[2]);

    vNormal[0] = pvNormal[0] / fLength;
    vNormal[1] = pvNormal[1] / fLength;
    vNormal[2] = pvNormal[2] / fLength;

#ifdef _XBOX
    return ((((DWORD)(vNormal[0] * 1023.0f) & 0x7FF) << 0)  |
            (((DWORD)(vNormal[1] * 1023.0f) & 0x7FF) << 11) |
            (((DWORD)(vNormal[2] *  511.0f) & 0x3FF) << 22));
#else
    return ((((uint32_t)(int)(vNormal[0] * 1023.0f) & 0x7FF) << 0)  |
            (((uint32_t)(int)(vNormal[1] * 1023.0f) & 0x7FF) << 11) |
            (((uint32_t)(int)(vNormal[2] *  511.0f) & 0x3FF) << 22));
#endif
}

// ===== Reflection Shader Setup =====

void SetReflectShaderFrameValues()
{
    D3DXMATRIX mat, worldView;

    D3DXMatrixMultiply(&worldView, TheseusGetWorld(), &g_matView);

    D3DXMatrixTranspose(&mat, &worldView);
    TheseusSetVertexShaderConstant(40, &mat(0,0), 4);

    D3DXMatrixTranspose(&mat, &g_matProjection);
    TheseusSetVertexShaderConstant(44, &mat(0,0), 4);

		D3DXVECTOR4 reflectConst(0.0f, 0.0f, 1.0f, 0.5f);
    TheseusSetVertexShaderConstant(48, &reflectConst, 1);

	D3DXVECTOR4 lightDir(1.0f, 1.0f, -1.0f, 0.0f);
	D3DXVec4Normalize(&lightDir, &lightDir);
	D3DXMatrixTranspose(&mat, &worldView);
	D3DXVec3TransformNormal((D3DXVECTOR3*)&lightDir, (D3DXVECTOR3*)&lightDir, &mat);
	D3DXVec4Normalize(&lightDir, &lightDir);
	TheseusSetVertexShaderConstant(49, &lightDir, 1);

    TheseusSetRenderState(D3DRS_LIGHTING, FALSE);
}

// ===== Falloff Shader Setup =====

void SetFalloffShaderFrameValues()
{
	D3DXMATRIX mat, worldView;

	D3DXMatrixMultiply(&worldView, TheseusGetWorld(), &g_matView);

	D3DXMatrixMultiply(&mat, &worldView, &g_matProjection);
	D3DXMatrixTranspose(&mat, &mat);
	TheseusSetVertexShaderConstant(0, &mat(0,0), 4);

	D3DXMatrixTranspose(&mat, &worldView);
	TheseusSetVertexShaderConstant(10, &mat(0,0), 4);

	D3DXMatrixInverse(&mat, NULL, &worldView);
	TheseusSetVertexShaderConstant(5, &mat(0,0), 4);

	D3DXVECTOR4 v(0.0f, 0.5f, 1.0f, -1.0f);
	TheseusSetVertexShaderConstant(9, &v, 1);

	D3DXVECTOR4 lightDir(1.0f, 1.0f, -1.0f, 0.0f);
	D3DXVec4Normalize(&lightDir, &lightDir);
	D3DXMatrixTranspose(&mat, &worldView);
	D3DXVec3TransformNormal((D3DXVECTOR3*)&lightDir, (D3DXVECTOR3*)&lightDir, &mat);
	D3DXVec4Normalize(&lightDir, &lightDir);
	TheseusSetVertexShaderConstant(4, &lightDir, 1);

	TheseusSetTexture(0, NULL);
	TheseusSetRenderState(D3DRS_LIGHTING, FALSE);
}

// ===== Falloff Shader Values =====

float g_nEffectAlpha = 1.0f;

void SetFalloffShaderValues(const D3DXCOLOR& sideColor, const D3DXCOLOR& frontColor)
{
	D3DXVECTOR4 v;

	v.x = sideColor.r;
	v.y = sideColor.g;
	v.z = sideColor.b;
	v.w = sideColor.a * g_nEffectAlpha;
	TheseusSetVertexShaderConstant(15, &v, 1);

	v.x = frontColor.r - sideColor.r;
	v.y = frontColor.g - sideColor.g;
	v.z = frontColor.b - sideColor.b;
	v.w = (frontColor.a - sideColor.a) * g_nEffectAlpha;
	TheseusSetVertexShaderConstant(16, &v, 1);
}


// ============================================================================
// VRML compatibility stub nodes: parsed but not rendered. Present so the
// alpha dashboard's IndexedLineSet/Coordinate nodes still parse without
// "unknown class" errors. No Render() override; the runtime just registers
// the property surface and walks past at draw time.
// ============================================================================

class CIndexedLineSet : public CNode
{
	DECLARE_NODE(CIndexedLineSet, CNode)
public:
	CNode* m_coord;
	CIntArray m_coordIndex;
	CIndexedLineSet() : m_coord(NULL) {}
	DECLARE_NODE_PROPS()
};

IMPLEMENT_NODE("IndexedLineSet", CIndexedLineSet, CNode)

START_NODE_PROPS(CIndexedLineSet, CNode)
	NODE_PROP(pt_node, CIndexedLineSet, coord)
	NODE_PROP(pt_intarray, CIndexedLineSet, coordIndex)
END_NODE_PROPS()

class CCoordinate : public CNode
{
	DECLARE_NODE(CCoordinate, CNode)
public:
	CNumArray m_point;  // Array of floats (vec3 packed sequentially)
	DECLARE_NODE_PROPS()
};

IMPLEMENT_NODE("Coordinate", CCoordinate, CNode)

START_NODE_PROPS(CCoordinate, CNode)
	NODE_PROP(pt_numarray, CCoordinate, point)
END_NODE_PROPS()
