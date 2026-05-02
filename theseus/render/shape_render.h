// shape_render.h: shape, mesh, and transform node declarations.
// CMeshNode, CTransform, CShape, CAppearance, CMaterial, CBox, CSphere.
// Companion to render/shape_render.cpp.

#pragma once

class CMeshNode : public CNode
{
	DECLARE_NODE(CMeshNode, CNode)
public:
	CMeshNode();
	~CMeshNode();

	void Render();

	TCHAR* m_url;
	float m_falloff;

	void load(const TCHAR* szFile);

	bool Initialize();
	virtual void Init();

	bool m_dirty;
	XTIME m_renderTime;

	DWORD GetFVF();

	class CMeshCore* m_mesh;
	bool m_ownMesh;

	static CMeshNode* c_pFirst;
	CMeshNode* m_next;

	DECLARE_NODE_PROPS()
	DECLARE_NODE_FUNCTIONS()
};

class CAppearance : public CNode
{
	DECLARE_NODE(CAppearance, CNode)
public:
	CAppearance();
	~CAppearance();

	CNode* m_material;
	CNode* m_texture;

	void Render();
	void Advance(float nSeconds);

	DECLARE_NODE_PROPS()
};

class CShape : public CNode
{
	DECLARE_NODE(CShape, CNode)
public:
	CShape();
	~CShape();

	CNode* m_appearance;
	CNode* m_geometry;

	void Render();
	void GetBBox(BBox* pBBox);
	float GetRadius();
	void Advance(float nSeconds);

	D3DXVECTOR3 m_position;

	DECLARE_NODE_PROPS()
};

class CMaterial : public CNode
{
	DECLARE_NODE(CMaterial, CNode)
public:
	CMaterial();

	float m_ambientIntensity;
	D3DXVECTOR3 m_diffuseColor;
	D3DXVECTOR3 m_emissiveColor;
	float m_shininess;
	D3DXVECTOR3 m_specularColor;
	float m_transparency;

	D3DMATERIAL8 m_material;

	void Render();

	DECLARE_NODE_PROPS()
};




class CTransform : public CGroup
{
	DECLARE_NODE(CTransform, CGroup)
public:
	CTransform();

	D3DXQUATERNION m_rotation; // axis/angle stored in a D3DXVECTOR4 layout

	D3DXVECTOR3 m_center;
	D3DXQUATERNION m_scaleOrientation;
	D3DXVECTOR3 m_scale;
	D3DXQUATERNION m_rotationQuat;
	D3DXVECTOR3 m_translation;

	D3DXVECTOR3 m_centerStart;
	D3DXQUATERNION m_scaleOrientationStart;
	D3DXVECTOR3 m_scaleStart;
	D3DXQUATERNION m_rotationStart;
	D3DXVECTOR3 m_translationStart;

	D3DXVECTOR3 m_centerEnd;
	D3DXQUATERNION m_scaleOrientationEnd;
	D3DXVECTOR3 m_scaleEnd;
	D3DXQUATERNION m_rotationEnd;
	D3DXVECTOR3 m_translationEnd;

	float m_alpha;
	float m_alphaStart;
	float m_alphaEnd;

	bool m_moving;

	BOOL m_dirty;
	D3DXMATRIX m_matrix;

	float m_fade;

	void CalcMatrix();
	void Render();
	void Advance(float nSeconds);
	bool OnSetProperty(const PRD* pprd, const void* pvValue);
	void OnLoad();

	void SetScale(float sx, float sy, float sz);
	void SetScaleOrientation(float x, float y, float z, float a);
	void SetTranslation(float x, float y, float z);
	void SetCenter(float x, float y, float z);
	void SetRotation(float x, float y, float z, float a);
	void SetAlpha(float a);
    void DisappearAfter(float t);

	float m_timeCenterStart;
	float m_timeScaleOrientationStart;
	XTIME m_timeScaleStart;
	XTIME m_timeRotationStart;
	XTIME m_timeTranslationStart;
	XTIME m_timeAlphaStart;
	XTIME m_timeToDisappear;

	DECLARE_NODE_PROPS()
	DECLARE_NODE_FUNCTIONS()
};

