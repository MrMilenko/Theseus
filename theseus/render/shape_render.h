// shape_render.h: the drawable scene nodes. Shape pairs an Appearance
// (Material plus texture) with geometry; Mesh loads geometry from a file;
// Transform places, scales, spins and fades its children. Bodies in
// render/shape_render.cpp, binary notes in docs/decomp/ShapeRender.md.

#pragma once

class CMeshCore;

// Geometry from a .xm file. Every mesh node sits on one list so the loader
// can find nodes still waiting for their file.
class CMeshNode : public CNode
{
	DECLARE_NODE(CMeshNode, CNode)
	DECLARE_NODE_PROPS()
	DECLARE_NODE_FUNCTIONS()
public:
	CMeshNode();
	~CMeshNode();

	void Render();
	virtual void Init();
	bool Initialize();
	DWORD GetFVF();

	void load(const TCHAR* szFile);	// script: swap the mesh file

	static CMeshNode* c_pFirst;

	TCHAR* m_url;
	float m_falloff;
	bool m_dirty;
	XTIME m_renderTime;
	CMeshCore* m_mesh;
	bool m_ownMesh;
	CMeshNode* m_next;
};

// Material and texture that apply to a shape's geometry.
class CAppearance : public CNode
{
	DECLARE_NODE(CAppearance, CNode)
	DECLARE_NODE_PROPS()
public:
	CAppearance();
	~CAppearance();

	void Advance(float dt);
	void Render();

	CNode* m_material;
	CNode* m_texture;
};

class CShape : public CNode
{
	DECLARE_NODE(CShape, CNode)
	DECLARE_NODE_PROPS()
public:
	CShape();
	~CShape();

	void Advance(float dt);
	void Render();
	void GetBBox(BBox* box);
	float GetRadius();

	CNode* m_appearance;
	CNode* m_geometry;
	D3DXVECTOR3 m_position;
};

// VRML style material fields, folded into a D3DMATERIAL8 for drawing.
class CMaterial : public CNode
{
	DECLARE_NODE(CMaterial, CNode)
	DECLARE_NODE_PROPS()
public:
	CMaterial();

	void Render();

	float m_ambientIntensity;
	D3DXVECTOR3 m_diffuseColor;
	D3DXVECTOR3 m_emissiveColor;
	float m_shininess;
	D3DXVECTOR3 m_specularColor;
	float m_transparency;
	D3DMATERIAL8 m_material;
};

// Each animated field keeps a start and end value and the time its move
// began; Advance blends them and CalcMatrix rebuilds m_xform.
class CTransform : public CGroup
{
	DECLARE_NODE(CTransform, CGroup)
	DECLARE_NODE_PROPS()
	DECLARE_NODE_FUNCTIONS()
public:
	CTransform();

	void OnLoad();
	bool OnSetProperty(const PRD* prop, const void* value);
	void Advance(float dt);
	void Render();
	void CalcMatrix();

	void SetTranslation(float x, float y, float z);
	void SetRotation(float x, float y, float z, float a);
	void SetScale(float x, float y, float z);
	void SetScaleOrientation(float x, float y, float z, float a);
	void SetCenter(float x, float y, float z);
	void SetAlpha(float a);
	void DisappearAfter(float secs);

	D3DXQUATERNION m_rotation;	// the script's axis + angle, not a real quaternion

	D3DXVECTOR3 m_center;
	D3DXQUATERNION m_scaleOrientation;
	D3DXVECTOR3 m_scale;
	D3DXQUATERNION m_orientQuat;
	D3DXVECTOR3 m_translation;

	D3DXVECTOR3 m_centerStart;
	D3DXQUATERNION m_scaleOrientationStart;
	D3DXVECTOR3 m_scaleFrom;
	D3DXQUATERNION m_turnFrom;
	D3DXVECTOR3 m_moveFrom;

	D3DXVECTOR3 m_centerEnd;
	D3DXQUATERNION m_scaleOrientationEnd;
	D3DXVECTOR3 m_scaleTo;
	D3DXQUATERNION m_turnTo;
	D3DXVECTOR3 m_moveTo;

	float m_alpha;
	float m_fadeFrom;
	float m_fadeTo;

	bool m_moving;
	BOOL m_dirty;
	D3DXMATRIX m_xform;
	float m_fade;

	float m_timeCenterStart;
	float m_timeScaleOrientationStart;
	XTIME m_timeScaleStart;
	XTIME m_timeRotationStart;
	XTIME m_timeTranslationStart;
	XTIME m_fadeStartTime;
	XTIME m_hideTime;
};
