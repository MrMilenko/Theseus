// shape_render.cpp: the nodes that put geometry on screen.
//
// Shape pairs a geometry node with an Appearance (a Material plus a texture),
// Box and Sphere are the two built in geometries, and the rest sets up the
// effect vertex shaders the falloff and reflection materials draw with.
// Rebuilt function by function from the 5960 retail XBE; the findings are in
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


// ===== CShape ===============================================================
// 0x30 bytes: appearance at 0x1c, geometry at 0x20, last world position at 0x24.

IMPLEMENT_NODE("Shape", CShape, CNode)

START_NODE_PROPS(CShape, CNode)
	NODE_PROP(pt_node, CShape, appearance)
	NODE_PROP(pt_node, CShape, geometry)
END_NODE_PROPS()

// CShape::CShape() / ~CShape()
// Reconstructed from 5960 retail XBE, CShape_CreateNode @ 0x00050cf0 and
// CShape_dtor @ 0x00050d60:
//     this[7] = 0; this[8] = 0;                       // appearance, geometry
//     ~: if (this[7]) this[7]->Release(); if (this[8]) this[8]->Release();
CShape::CShape() :
	m_appearance(NULL),
	m_geometry(NULL)
{
}

CShape::~CShape()
{
	if (m_appearance)
		m_appearance->Release();
	if (m_geometry)
		m_geometry->Release();
}

// CShape::Render()
// Reconstructed from 5960 retail XBE @ 0x00050dd0. Ghidra decompile, trimmed:
//     if (!geometry) return;
//     if (class chain of geometry reaches MeshNode) {
//         if (mesh->dirty) { if (url && *url) load(url); mesh->Init(); if (!mesh->core) return; }
//         if (!mesh->core || !mesh->core->GetFVF()) return;
//         g_pRenderMeshNode = mesh; SetVertexShader(GetFixedFunctionShader(fvf));
//     }
//     if (appearance) appearance->Render();
//     geometry->Render();
//     position = world matrix row 3 (copied from the device); g_pRenderMeshNode = NULL;
// Initialize() is the binary's inlined load/Init/core test. The profiler
// phases and the per mesh triangle tally are Theseus additions.
void CShape::Render()
{
	if (m_geometry == NULL)
		return;

	CMeshNode* pMesh = NULL;
	if (m_geometry->IsKindOf(NODE_CLASS(CMeshNode)))
	{
		pMesh = (CMeshNode*)m_geometry;
		if (pMesh->m_dirty && !pMesh->Initialize())
			return;

		DWORD meshFVF = pMesh->GetFVF();
		if (meshFVF == 0)
			return;

		g_pRenderMeshNode = pMesh;
		TheseusSetVertexShader(GetFixedFunctionShader(meshFVF));
	}

	if (m_appearance)
	{
		FramePhaseScope material(FP_TREE_MATERIAL);
		m_appearance->Render();
	}

	{
		FramePhaseScope draw(FP_TREE_DRAW);
		int trisBefore = g_frameCount[FC_TRIANGLES];
		m_geometry->Render();
		if (pMesh)
			Frame_TallyMesh(this, pMesh->m_url, g_frameCount[FC_TRIANGLES] - trisBefore);
	}

	D3DXMATRIX world;
	TheseusGetTransform(D3DTS_WORLD, &world);
	m_position = D3DXVECTOR3(world._41, world._42, world._43);
	g_pRenderMeshNode = NULL;
}

// CShape::GetBBox(pBBox) / GetRadius() / Advance(nSeconds)
// Reconstructed from 5960 retail XBE @ 0x00050eb0, 0x00050ef0, 0x00050f10:
//     GetBBox:   if (geometry) tail call geometry->GetBBox(p); else zero all six floats;
//     GetRadius: return geometry ? geometry->GetRadius() : 0.0f;
//     Advance:   CNode::Advance(t); geometry->Advance(t); appearance->Advance(t);  (each if set)
// The zero fill is the CNode default, so it goes through CNode::GetBBox.
void CShape::GetBBox(BBox* pBBox)
{
	if (m_geometry)
		m_geometry->GetBBox(pBBox);
	else
		CNode::GetBBox(pBBox);
}

float CShape::GetRadius()
{
	return m_geometry ? m_geometry->GetRadius() : 0.0f;
}

void CShape::Advance(float nSeconds)
{
	CNode::Advance(nSeconds);
	if (m_geometry)
		m_geometry->Advance(nSeconds);
	if (m_appearance)
		m_appearance->Advance(nSeconds);
}


// ===== CAppearance ==========================================================
// 0x24 bytes: material at 0x1c, texture at 0x20.

IMPLEMENT_NODE("Appearance", CAppearance, CNode)

START_NODE_PROPS(CAppearance, CNode)
	NODE_PROP(pt_node, CAppearance, material)
	NODE_PROP(pt_node, CAppearance, texture)
END_NODE_PROPS()

// CAppearance::CAppearance() / ~CAppearance()
// Reconstructed from 5960 retail XBE, CAppearance_CreateNode @ 0x00050f50 and
// CAppearance_dtor @ 0x00050fc0: both pointers start NULL and are released
// material first, then texture.
CAppearance::CAppearance() :
	m_material(NULL),
	m_texture(NULL)
{
}

CAppearance::~CAppearance()
{
	if (m_material)
		m_material->Release();
	if (m_texture)
		m_texture->Release();
}

// CAppearance::Render()
// Reconstructed from 5960 retail XBE @ 0x00051030. The device calls are
// inlined into push buffer writes; decoded they are:
//     CULLMODE = CCW; ZWRITEENABLE = 0; ALPHABLENDENABLE = 1;
//     SRCBLEND = SRCALPHA (0x302); DESTBLEND = INVSRCALPHA (0x303);
//     stage 0: COLOROP = ALPHAOP = DISABLE, TEXCOORDINDEX = 0, TEXTURETRANSFORMFLAGS = 0;
//     stage 1: COLOROP = ALPHAOP = DISABLE; SetTexture(0, NULL); SetTexture(1, NULL);
//     if (texture) {
//         texture->Render(); surf = texture->GetTextureSurface();
//         if (surf) { SetTexture(0, surf); COLOROP/ALPHAOP = SELECTARG1 of TEXTURE; }
//         else { ALPHAOP/COLOROP = SELECTARG1 of TFACTOR; TEXTUREFACTOR = 0; }
//     }
//     if (material) material->Render();
// Every shape starts from this blend and stage state, so the order here is the
// order the binary writes it.
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

	for (DWORD stage = 0; stage < 2; stage++)
		TheseusSetTexture(stage, NULL);

	if (m_texture)
	{
		m_texture->Render();

		LPDIRECT3DTEXTURE8 pTex = m_texture->GetTextureSurface();
		if (pTex == NULL)
		{
			// No image yet: draw with a clear texture factor instead.
			TheseusSetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
			TheseusSetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TFACTOR);
			TheseusSetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
			TheseusSetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TFACTOR);
			TheseusSetRenderState(D3DRS_TEXTUREFACTOR, 0);
		}
		else
		{
			TheseusSetTexture(0, pTex);
			TheseusSetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
			TheseusSetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
			TheseusSetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
			TheseusSetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		}
	}

	if (m_material)
		m_material->Render();
}

// CAppearance::Advance(nSeconds)
// Reconstructed from 5960 retail XBE @ 0x000512c0:
//     CNode::Advance(t); if (material) material->Advance(t); if (texture) texture->Advance(t);
void CAppearance::Advance(float nSeconds)
{
	CNode::Advance(nSeconds);
	if (m_material)
		m_material->Advance(nSeconds);
	if (m_texture)
		m_texture->Advance(nSeconds);
}


// ===== CMaterial ============================================================
// 0x90 bytes: ambientIntensity 0x1c, diffuseColor 0x20, emissiveColor 0x2c,
// shininess 0x38, specularColor 0x3c, transparency 0x48, D3DMATERIAL8 at 0x4c.

IMPLEMENT_NODE("Material", CMaterial, CNode)

START_NODE_PROPS(CMaterial, CNode)
	NODE_PROP(pt_number, CMaterial, ambientIntensity)
	NODE_PROP(pt_color, CMaterial, diffuseColor)
	NODE_PROP(pt_color, CMaterial, emissiveColor)
	NODE_PROP(pt_number, CMaterial, shininess)
	NODE_PROP(pt_color, CMaterial, specularColor)
	NODE_PROP(pt_number, CMaterial, transparency)
END_NODE_PROPS()

// CMaterial::CMaterial()
// Reconstructed from 5960 retail XBE, CMaterial_CreateNode @ 0x00051300:
//     this[7] = 0.2 (0x0002c01c); this[8..10] = 0.8 (0x0002bfd8); this[11..13] = 0;
//     this[14] = 0.2; this[15..17] = 0; this[18] = 0;
// The VRML material defaults, which is all these numbers could ever be.
CMaterial::CMaterial() :
	m_ambientIntensity(0.2f),
	m_diffuseColor(0.8f, 0.8f, 0.8f),
	m_emissiveColor(0.0f, 0.0f, 0.0f),
	m_shininess(0.2f),
	m_specularColor(0.0f, 0.0f, 0.0f),
	m_transparency(0.0f)
{
}

static D3DCOLORVALUE MaterialColor(float r, float g, float b, float a)
{
	D3DCOLORVALUE c;
	c.r = r;
	c.g = g;
	c.b = b;
	c.a = a;
	return c;
}

// CMaterial::Render()
// Reconstructed from 5960 retail XBE @ 0x000513a0. Ghidra decompile, trimmed:
//     a = 1.0 - transparency;
//     mat.Diffuse = (diffuse, a); mat.Ambient = (ambientIntensity x3, a);
//     mat.Specular = (specular, a); mat.Emissive = (emissive, a); mat.Power = shininess;
//     copy 0x11 dwords into the device material, mark it dirty   // SetMaterial, inlined
void CMaterial::Render()
{
	float alpha = 1.0f - m_transparency;

	m_material.Diffuse = MaterialColor(m_diffuseColor.x, m_diffuseColor.y, m_diffuseColor.z, alpha);
	m_material.Ambient = MaterialColor(m_ambientIntensity, m_ambientIntensity, m_ambientIntensity, alpha);
	m_material.Specular = MaterialColor(m_specularColor.x, m_specularColor.y, m_specularColor.z, alpha);
	m_material.Emissive = MaterialColor(m_emissiveColor.x, m_emissiveColor.y, m_emissiveColor.z, alpha);
	m_material.Power = m_shininess;

	TheseusSetMaterial(&m_material);
}


// ===== CreateCube ===========================================================

// Corner of a unit cube: which way each axis goes, and which end of the
// texture it gets (0 maps to 0.01, 1 to 0.99).
struct CubeCorner
{
	signed char x, y, z;
	BYTE u, v;
};

// The binary's six faces in order -z, +z, +y, -y, +x, -x, four corners each.
static const CubeCorner c_cubeCorners[24] =
{
	{ -1, -1, -1, 0, 0 }, {  1, -1, -1, 1, 0 }, {  1,  1, -1, 1, 1 }, { -1,  1, -1, 0, 1 },
	{  1,  1,  1, 0, 1 }, {  1, -1,  1, 0, 0 }, { -1, -1,  1, 1, 0 }, { -1,  1,  1, 1, 1 },
	{ -1,  1, -1, 0, 0 }, {  1,  1, -1, 1, 0 }, {  1,  1,  1, 1, 1 }, { -1,  1,  1, 0, 1 },
	{  1, -1,  1, 1, 1 }, {  1, -1, -1, 1, 0 }, { -1, -1, -1, 0, 0 }, { -1, -1,  1, 0, 1 },
	{  1, -1, -1, 0, 0 }, {  1, -1,  1, 1, 0 }, {  1,  1,  1, 1, 1 }, {  1,  1, -1, 0, 1 },
	{ -1,  1,  1, 0, 1 }, { -1, -1,  1, 0, 0 }, { -1, -1, -1, 1, 0 }, { -1,  1, -1, 1, 1 },
};

static const float c_cubeNormals[6][3] =
{
	{ 0.0f, 0.0f, -1.0f }, { 0.0f, 0.0f, 1.0f },
	{ 0.0f, 1.0f, 0.0f }, { 0.0f, -1.0f, 0.0f },
	{ 1.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f },
};

// CreateCube(pVertices, pIndices, size, bInside)
// Reconstructed from 5960 retail XBE @ 0x00051450 (bInside in CL, indices in
// EAX). Unrolled in the binary: 24 vertex stores of +-0.5 * size, a face normal
// and texture coordinates inset to 0.01..0.99, then the indices 0,1,2, 2,3,0
// for each face. The inside variant writes each face's four corners in reverse
// order with the same normals. Written here as a corner table walked in a loop.
void CreateCube(D3DVERTEX* pVertices, WORD* pIndices, D3DXVECTOR3 size, bool bInside = false)
{
	for (int face = 0; face < 6; face++)
	{
		D3DXVECTOR3 normal(c_cubeNormals[face][0], c_cubeNormals[face][1], c_cubeNormals[face][2]);

		for (int n = 0; n < 4; n++)
		{
			const CubeCorner& c = c_cubeCorners[face * 4 + (bInside ? 3 - n : n)];
			D3DXVECTOR3 pos(c.x * 0.5f * size.x, c.y * 0.5f * size.y, c.z * 0.5f * size.z);
			*pVertices++ = D3DVERTEX(pos, normal, c.u ? 0.99f : 0.01f, c.v ? 0.99f : 0.01f);
		}

		WORD first = (WORD)(face * 4);
		static const WORD quad[6] = { 0, 1, 2, 2, 3, 0 };
		for (int n = 0; n < 6; n++)
			*pIndices++ = first + quad[n];
	}
}


// ===== CBox =================================================================
// 0x37c bytes: size 0x1c, vertex buffer 0x28, index buffer 0x2c, 24 vertices
// at 0x30, 36 indices at 0x330, dirty flag 0x378.

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

	D3DVERTEX m_vertices [24];
	WORD m_indices [36];

	bool m_dirty;

	DECLARE_NODE_PROPS()
};

IMPLEMENT_NODE("Box", CBox, CNode)

START_NODE_PROPS(CBox, CNode)
	NODE_PROP(pt_vec3, CBox, size)
END_NODE_PROPS()

// CBox::CBox() / ~CBox()
// Reconstructed from 5960 retail XBE, factory @ 0x000526c0 and dtor @ 0x00052750:
//     size = (1.0, 1.0, 1.0); dirty = 1; vb = ib = NULL;
//     ~: release vb, then ib (D3D resource release inlined)
CBox::CBox() :
	m_size(1.0f, 1.0f, 1.0f),
	m_pVB(NULL),
	m_pIB(NULL),
	m_dirty(true)
{
}

CBox::~CBox()
{
	if (m_pVB)
		m_pVB->Release();
	if (m_pIB)
		m_pIB->Release();
}

// CBox::Render()
// Reconstructed from 5960 retail XBE @ 0x00052820 (not a defined function in
// the Ghidra project, read from the disassembly):
//     if (dirty) { CreateCube(verts, indices, size, false); dirty = 0; }
//     if (!vb) { create 0x300 byte vb; copy 0xc0 dwords of verts into it; }
//     if (!ib) { create 0x48 byte ib; copy 0x12 dwords of indices into it; }
//     if (vb && ib) { SetStreamSource(0, vb, 0x20); SetIndices(ib, 0);
//                     SetVertexShader(0x112); draw 36 indices as a triangle list; }
// The binary's create helper retries until it gets memory. Theseus's can fail,
// so a buffer that won't create or lock is dropped and retried next frame.
void CBox::Render()
{
	if (m_dirty)
	{
		CreateCube(m_vertices, m_indices, m_size);
		m_dirty = false;
	}

	BYTE* pDst;

	if (m_pVB == NULL)
	{
		TheseusCreateVertexBuffer(sizeof (m_vertices), D3DUSAGE_DYNAMIC, D3DFVF_VERTEX, D3DPOOL_MANAGED, &m_pVB);
		if (m_pVB != NULL)
		{
			if (FAILED(m_pVB->Lock(0, sizeof (m_vertices), &pDst, 0)))
			{
				m_pVB->Release();
				m_pVB = NULL;
			}
			else
			{
				memcpy(pDst, m_vertices, sizeof (m_vertices));
				m_pVB->Unlock();
			}
		}
	}

	if (m_pIB == NULL)
	{
		TheseusCreateIndexBuffer(sizeof (m_indices), D3DUSAGE_DYNAMIC, D3DFMT_INDEX16, D3DPOOL_MANAGED, &m_pIB);
		if (m_pIB != NULL)
		{
			if (FAILED(m_pIB->Lock(0, sizeof (m_indices), &pDst, 0)))
			{
				m_pIB->Release();
				m_pIB = NULL;
			}
			else
			{
				memcpy(pDst, m_indices, sizeof (m_indices));
				m_pIB->Unlock();
			}
		}
	}

	if (m_pVB == NULL || m_pIB == NULL)
		return;

	TheseusSetStreamSource(0, m_pVB, sizeof (D3DVERTEX));
	TheseusSetIndices(m_pIB, 0);
	TheseusSetVertexShader(D3DFVF_VERTEX);
	TheseusDrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 24, 0, 12);
}

// CBox::GetBBox(pBBox) / GetRadius()
// Reconstructed from 5960 retail XBE @ 0x00052960 and 0x00052990:
//     GetBBox:   center = (0, 0, 0); size = this->size;
//     GetRadius: return sqrt(x*x + y*y + z*z) * 0.5;   // x87, half the diagonal
void CBox::GetBBox(BBox* pBBox)
{
	pBBox->center = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
	pBBox->size = m_size;
}

float CBox::GetRadius()
{
	return D3DXVec3Length(&m_size) * 0.5f;
}


// ===== CSphere ==============================================================
// A MeshNode whose mesh comes from MakeSphere: radius 0x40, slices 0x44, stacks 0x48.

extern class CMesh* MakeSphere(float nRadius, int nSlices, int nStacks);

class CSphere : public CMeshNode
{
	DECLARE_NODE(CSphere, CMeshNode)
public:
	CSphere();

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

// CSphere::CSphere()
// Reconstructed from 5960 retail XBE @ 0x00052a00:
//     (MeshNode ctor inlined) radius = 1.0; slices = 0x20; stacks = 0x20; dirty = 1;
// The destructor @ 0x00052a90 is only the MeshNode one.
CSphere::CSphere() :
	m_radius(1.0f),
	m_slices(32),
	m_stacks(32)
{
	m_dirty = true;
}

// CSphere::Init()   [MeshNode vtable slot 23]
// Reconstructed from 5960 retail XBE @ 0x00052aa0. Ghidra decompile:
//     mesh = MakeSphere(radius, slices, stacks); this[0xc] = mesh;
//     if (mesh != 0) dirty = 0;
void CSphere::Init()
{
	m_mesh = MakeSphere(m_radius, m_slices, m_stacks);
	if (m_mesh != NULL)
		m_dirty = false;
}


// ===== Effect vertex shaders ================================================
// Xbox only: the NV2A microcode and its lookup tables. Desktop can't run
// NV2A microcode, so it has its own versions below.

#ifdef _XBOX

// The five NV2A programs, read word for word out of the XBE data: a header
// word with the instruction count in the high half, then four words per
// instruction. Binary data, so any faithful copy is identical.

// @ 0x001754f0, 21 instructions
static DWORD c_EffectMicrocode[] =
{
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

// @ 0x00175648, 21 instructions
static DWORD c_Effect2Microcode[] =
{
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

// @ 0x001757a0, 21 instructions
static DWORD c_Effect3Microcode[] =
{
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

// @ 0x001758f8, 29 instructions
static DWORD c_Effect4Microcode[] =
{
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

// @ 0x00175ad0, 10 instructions
static DWORD c_AnisoMicrocode[] =
{
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

// One lookup entry, 0x14 bytes in the binary: effect number, microcode, the
// FVF it serves, the shader handle once created, and a spare flag.
struct EffectShader
{
	int nEffect;
	DWORD* pMicrocode;
	DWORD fvf;
	DWORD hShader;
	bool bReported;
};

#define FVF_PACKED   (D3DFVF_XYZ | D3DFVF_NORMPACKED3)
#define FVF_FLOAT    (D3DFVF_XYZ | D3DFVF_NORMAL)

// Table @ 0x00175bb0, 12 entries. Effect 1 is the falloff shader (its own
// variant when there are texture coordinates), 2 anisotropic, 3 the textured
// falloff, 4 reflection. Each comes in packed and float normal versions.
static EffectShader c_effectShaders[] =
{
	{ 1, c_EffectMicrocode,  FVF_PACKED | D3DFVF_DIFFUSE },
	{ 1, c_EffectMicrocode,  FVF_PACKED },
	{ 1, c_Effect2Microcode, FVF_PACKED | D3DFVF_TEX1 },
	{ 2, c_AnisoMicrocode,   FVF_PACKED },
	{ 3, c_Effect3Microcode, FVF_PACKED | D3DFVF_TEX1 },
	{ 4, c_Effect4Microcode, FVF_PACKED },
	{ 1, c_EffectMicrocode,  FVF_FLOAT | D3DFVF_DIFFUSE },
	{ 1, c_EffectMicrocode,  FVF_FLOAT },
	{ 1, c_Effect2Microcode, FVF_FLOAT | D3DFVF_TEX1 },
	{ 2, c_AnisoMicrocode,   FVF_FLOAT },
	{ 3, c_Effect3Microcode, FVF_FLOAT | D3DFVF_TEX1 },
	{ 4, c_Effect4Microcode, FVF_FLOAT },
};

// Table @ 0x00175b74, 3 entries: the packed normal FVFs that need a
// declaration to draw fixed function. The microcode pointer is never used.
static EffectShader c_fixedShaders[] =
{
	{ 0, c_EffectMicrocode, FVF_PACKED | D3DFVF_DIFFUSE },
	{ 1, c_EffectMicrocode, FVF_PACKED },
	{ 2, c_EffectMicrocode, FVF_PACKED | D3DFVF_TEX1 },
};

// Effect shader declarations @ 0x0017669c..0x001766e0: position in v0,
// normal in v3, texture coordinates in v6.
static DWORD c_declFloat [] = { D3DVSD_STREAM(0), D3DVSD_REG(0, D3DVSDT_FLOAT3), D3DVSD_REG(3, D3DVSDT_FLOAT3), D3DVSD_END() };
static DWORD c_declFloatTex [] = { D3DVSD_STREAM(0), D3DVSD_REG(0, D3DVSDT_FLOAT3), D3DVSD_REG(3, D3DVSDT_FLOAT3), D3DVSD_REG(6, D3DVSDT_FLOAT2), D3DVSD_END() };
static DWORD c_declPacked [] = { D3DVSD_STREAM(0), D3DVSD_REG(0, D3DVSDT_FLOAT3), D3DVSD_REG(3, D3DVSDT_NORMPACKED3), D3DVSD_END() };
static DWORD c_declPackedTex [] = { D3DVSD_STREAM(0), D3DVSD_REG(0, D3DVSDT_FLOAT3), D3DVSD_REG(3, D3DVSDT_NORMPACKED3), D3DVSD_REG(6, D3DVSDT_FLOAT2), D3DVSD_END() };

// Fixed function declarations @ 0x00176660, five dwords each, one per c_fixedShaders entry.
static DWORD c_fixedDecls[3][5] =
{
	{ D3DVSD_STREAM(0), D3DVSD_REG(D3DVSDE_POSITION, D3DVSDT_FLOAT3), D3DVSD_REG(D3DVSDE_NORMAL, D3DVSDT_NORMPACKED3), D3DVSD_REG(D3DVSDE_DIFFUSE, D3DVSDT_D3DCOLOR), D3DVSD_END() },
	{ D3DVSD_STREAM(0), D3DVSD_REG(D3DVSDE_POSITION, D3DVSDT_FLOAT3), D3DVSD_REG(D3DVSDE_NORMAL, D3DVSDT_NORMPACKED3), D3DVSD_END() },
	{ D3DVSD_STREAM(0), D3DVSD_REG(D3DVSDE_POSITION, D3DVSDT_FLOAT3), D3DVSD_REG(D3DVSDE_NORMAL, D3DVSDT_NORMPACKED3), D3DVSD_REG(D3DVSDE_TEXCOORD0, D3DVSDT_FLOAT2), D3DVSD_END() },
};

// GetEffectShader(nEffect, fvf)
// Reconstructed from 5960 retail XBE @ 0x0005c6c0 (effect in ECX, fvf in EAX).
// Ghidra decompile, trimmed:
//     if (effect == 0) return fvf;
//     for (i = 0; e[i].effect != effect || e[i].fvf != fvf; i++) if (i > 10) return fvf;
//     if (e[i].shader == 0)
//         CreateVertexShader(packed ? (tex ? 0x1766c0 : 0x1766d4) : (tex ? 0x17669c : 0x1766b0),
//                            e[i].microcode, &e[i].shader, 0);
//     return e[i].shader;
// Shaders are created the first time a material asks for them and kept.
DWORD GetEffectShader(int nEffect, DWORD fvf)
{
	if (nEffect == 0)
		return fvf;

	for (int n = 0; n < (int)countof (c_effectShaders); n++)
	{
		EffectShader& e = c_effectShaders[n];
		if (e.nEffect != nEffect || e.fvf != fvf)
			continue;

		if (e.hShader == 0)
		{
			bool bTex = (fvf & D3DFVF_TEX1) != 0;
			DWORD* pDecl;
			if (fvf & D3DFVF_NORMPACKED3)
				pDecl = bTex ? c_declPackedTex : c_declPacked;
			else
				pDecl = bTex ? c_declFloatTex : c_declFloat;
			TheseusCreateVertexShader(pDecl, e.pMicrocode, &e.hShader, 0);
		}
		return e.hShader;
	}

	return fvf;
}

// GetFixedFunctionShader(fvf)
// Reconstructed from 5960 retail XBE @ 0x0005c740 (fvf in EAX). Finds fvf in
// the three entry table, else returns fvf unchanged. On first use it builds a
// shader from the matching declaration with no microcode (CreateVertexShader
// inlined: XMemAlloc, parse the declaration, handle | 1) and keeps it.
DWORD GetFixedFunctionShader(DWORD fvf)
{
	for (int n = 0; n < (int)countof (c_fixedShaders); n++)
	{
		EffectShader& e = c_fixedShaders[n];
		if (e.fvf != fvf)
			continue;

		if (e.hShader == 0)
			TheseusCreateVertexShader(c_fixedDecls[n], NULL, &e.hShader, 0);
		return e.hShader;
	}

	return fvf;
}

#else

// Desktop versions, Theseus's own. GetEffectShader hands the bgfx shim a
// sentinel (high bit plus the real FVF) so its SetVertexShader turns on the
// effect path and still knows the vertex layout. Fixed function passes through.
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


// ===== Normals ==============================================================

// CompressNormal(pvNormal)
// Reconstructed from 5960 retail XBE @ 0x0005c820 (vector in EAX):
//     len = sqrt(x*x + y*y + z*z); x, y, z scaled to unit length;
//     return (ftol(x * 1023) & 0x7ff) | (ftol(y * 1023) & 0x7ff) << 11 | ftol(z * 511) << 22;
// ftol truncates through a signed integer, which is what lets negative
// components wrap into the packed fields. Going through int keeps that on
// every compiler; a straight float to DWORD cast gives 0 for them on clang.
// The binary multiplies by 1 / len; dividing keeps the normals Theseus has
// always packed (the two differ in about 1 vector in 40,000).
DWORD CompressNormal(float* pvNormal)
{
	float len = sqrtf(pvNormal[0] * pvNormal[0] + pvNormal[1] * pvNormal[1] + pvNormal[2] * pvNormal[2]);
	float x = pvNormal[0] / len;
	float y = pvNormal[1] / len;
	float z = pvNormal[2] / len;

	DWORD packed = (DWORD)(int)(x * 1023.0f) & 0x7ff;
	packed |= ((DWORD)(int)(y * 1023.0f) & 0x7ff) << 11;
	packed |= ((DWORD)(int)(z * 511.0f) & 0x3ff) << 22;
	return packed;
}


// ===== Effect shader constants ==============================================

// Light from the upper right front, (1, 1, -1) normalized, taken into the
// object's space. Both frame value setters below end on this.
static void SetLightDirection(DWORD reg, const D3DXMATRIX& worldView)
{
	D3DXVECTOR4 dir(1.0f, 1.0f, -1.0f, 0.0f);
	D3DXVec4Normalize(&dir, &dir);

	D3DXMATRIX t;
	D3DXMatrixTranspose(&t, &worldView);
	D3DXVec3TransformNormal((D3DXVECTOR3*)&dir, (D3DXVECTOR3*)&dir, &t);
	D3DXVec4Normalize(&dir, &dir);

	TheseusSetVertexShaderConstant(reg, &dir, 1);
}

// SetReflectShaderFrameValues()
// Reconstructed from 5960 retail XBE @ 0x0005c8d0 (constant numbers are the
// binary's biased by 96):
//     wv = world (matrix stack top) * view;
//     c40..43 = transpose(wv); c44..47 = transpose(projection); c48 = (0, 0, 1, 0.5);
//     c49 = light direction (see above); LIGHTING = FALSE;
// Desktop skips the LIGHTING write: the bgfx shim never lights fixed function.
void SetReflectShaderFrameValues()
{
	D3DXMATRIX worldView, t;
	D3DXMatrixMultiply(&worldView, TheseusGetWorld(), &g_matView);

	D3DXMatrixTranspose(&t, &worldView);
	TheseusSetVertexShaderConstant(40, &t, 4);

	D3DXMatrixTranspose(&t, &g_matProjection);
	TheseusSetVertexShaderConstant(44, &t, 4);

	D3DXVECTOR4 c48(0.0f, 0.0f, 1.0f, 0.5f);
	TheseusSetVertexShaderConstant(48, &c48, 1);

	SetLightDirection(49, worldView);

#ifdef _XBOX
	TheseusSetRenderState(D3DRS_LIGHTING, FALSE);
#endif
}

// SetFalloffShaderFrameValues()
// Reconstructed from 5960 retail XBE @ 0x0005ca00:
//     wv = world * view;
//     c0..3 = transpose(wv * projection); c10..13 = transpose(wv); c5..8 = inverse(wv);
//     c9 = (0, 0.5, 1, -1); c4 = light direction;
//     SetTexture(0, NULL); LIGHTING = FALSE;      // both inlined
// Runs on every world matrix change. The profiler phase is a Theseus
// addition, and desktop skips LIGHTING as in the reflection setter.
void SetFalloffShaderFrameValues()
{
	FramePhaseScope world(FP_TREE_WORLD);

	D3DXMATRIX worldView, t;
	D3DXMatrixMultiply(&worldView, TheseusGetWorld(), &g_matView);

	D3DXMatrixMultiply(&t, &worldView, &g_matProjection);
	D3DXMatrixTranspose(&t, &t);
	TheseusSetVertexShaderConstant(0, &t, 4);

	D3DXMatrixTranspose(&t, &worldView);
	TheseusSetVertexShaderConstant(10, &t, 4);

	D3DXMatrixInverse(&t, NULL, &worldView);
	TheseusSetVertexShaderConstant(5, &t, 4);

	D3DXVECTOR4 c9(0.0f, 0.5f, 1.0f, -1.0f);
	TheseusSetVertexShaderConstant(9, &c9, 1);

	SetLightDirection(4, worldView);

	TheseusSetTexture(0, NULL);
#ifdef _XBOX
	TheseusSetRenderState(D3DRS_LIGHTING, FALSE);
#endif
}

// Scene wide fade for the effect materials, @ 0x00175ca0, starts at 1.0.
float g_nEffectAlpha = 1.0f;

// SetFalloffShaderValues(sideColor, frontColor)
// Inlined in the 5960 retail XBE into each falloff material's Setup, e.g.
// CFalloffMatInfo_Setup @ 0x00057790:
//     c15 = (side.rgb, side.a * g_nEffectAlpha);
//     c16 = (front.rgb - side.rgb, (front.a - side.a) * g_nEffectAlpha);
// The shader blends from the side color at the silhouette to the front color.
void SetFalloffShaderValues(const D3DXCOLOR& sideColor, const D3DXCOLOR& frontColor)
{
	D3DXVECTOR4 side(sideColor.r, sideColor.g, sideColor.b, sideColor.a * g_nEffectAlpha);
	TheseusSetVertexShaderConstant(15, &side, 1);

	D3DXVECTOR4 delta(frontColor.r - sideColor.r, frontColor.g - sideColor.g, frontColor.b - sideColor.b,
		(frontColor.a - sideColor.a) * g_nEffectAlpha);
	TheseusSetVertexShaderConstant(16, &delta, 1);
}


// ===== VRML stub nodes ======================================================
// Theseus's own, not in 5960: parsed but never drawn, so the alpha dashboards'
// IndexedLineSet and Coordinate nodes load without "unknown class" errors.

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
