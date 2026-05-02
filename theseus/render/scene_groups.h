// scene_groups.h: CBackground, CLevel, CViewpoint, CNavigationInfo
// declarations. CGroup is declared in node.h, CTransform in shape_render.h.
// CLayer, CInline, CSpinner, CWaver, CLayout, CSwitch, CBillboard are
// defined entirely within scene_groups.cpp.

#pragma once

class CBackground : public CNode
{
	DECLARE_NODE(CBackground, CNode)
public:
	CBackground();
	~CBackground();

	void Advance(float nSeconds);
	bool OnSetProperty(const PRD* pprd, const void* pvValue);

	void Bind();
	void RenderBackdrop();

	CNode* m_backdrop;
	D3DXVECTOR3 m_skyColor;
	bool m_isBound;

	LPDIRECT3DVERTEXBUFFER8 m_vb;

	DECLARE_NODE_PROPS()
};

class CLevel : public CGroup
{
	DECLARE_NODE(CLevel, CGroup)
public:
	CLevel();
	~CLevel();

	CNode* m_control;	// joystick bound to this level
	CNode* m_tunnel;	// tunnel geometry leading here
	CNode* m_path;		// camera path through the tunnel
	CNode* m_shell;		// sphere around the level
	TCHAR* m_archive;
	bool m_unloadable;
	bool m_fade;

	float m_timeToNextLevel;

	void GoTo();
	void GoBackTo();

	void Advance(float nSeconds);
	void Render();

protected:
	void Activate();
	void Deactivate();

	class CXipFile* m_xipFile;
	bool m_bArrive;

	DECLARE_NODE_PROPS()
	DECLARE_NODE_FUNCTIONS()
};

class CViewpoint : public CNode
{
	DECLARE_NODE(CViewpoint, CNode)
public:
	CViewpoint();
	~CViewpoint();

	void OnLoad();
	void Bind();

	bool m_isBound;
	float m_fieldOfView;
	bool m_jump;
	D3DXQUATERNION m_orientation;
	D3DXVECTOR3 m_position;
	TCHAR* m_description;

	bool OnSetProperty(const PRD* pprd, const void* pvValue);

	DECLARE_NODE_PROPS()
};

extern void BindViewpoint(CNode* pViewpontNode);

class CNavigationInfo : public CNode
{
	DECLARE_NODE(CNavigationInfo, CNode)
public:
	CNavigationInfo();
	~CNavigationInfo();

	bool OnSetProperty(const PRD* pprd, const void* pvValue);
	void OnLoad();

	bool m_isBound;
	D3DXVECTOR3 m_avatarSize;
	bool m_headlight;
	float m_speed;
	TCHAR* m_type;
	float m_visibilityLimit;
	CNode* m_shape;

	void Bind();

	DECLARE_NODE_PROPS()
};
