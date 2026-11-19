// scene_groups.h: the scene-level nodes other files reach into: Background,
// Level, Viewpoint and NavigationInfo. The rest of scene_groups.cpp's nodes
// (Layer, Inline, Spinner, Waver, Layout, Switch, Billboard) are private to
// it. Binary notes in docs/decomp/SceneGroups.md.

#pragma once

class CXipFile;

// Backdrop behind the scene. The fade fields are Theseus's SetAlpha.
class CBackground : public CNode
{
	DECLARE_NODE(CBackground, CNode)
	DECLARE_NODE_PROPS()
	DECLARE_NODE_FUNCTIONS()
public:
	CBackground();
	~CBackground();

	void Advance(float dt);
	bool OnSetProperty(const PRD* prop, const void* value);
	void Bind();
	void RenderBackdrop();
	void SetAlpha(float a);
	void DisappearAfter(float secs);

	CNode* m_backdrop;
	D3DXVECTOR3 m_skyColor;
	bool m_isBound;
	float m_fade;			// seconds SetAlpha takes to get there
	XTIME m_fadeStartTime;
	float m_fadeFrom;
	float m_fadeTo;
	float m_alpha;
	XTIME m_hideTime;
	LPDIRECT3DVERTEXBUFFER8 m_vb;
};

// One dashboard area, loaded from its own XIP while you're in it.
class CLevel : public CGroup
{
	DECLARE_NODE(CLevel, CGroup)
	DECLARE_NODE_PROPS()
	DECLARE_NODE_FUNCTIONS()
public:
	CLevel();
	~CLevel();

	void Advance(float dt);
	void Render();
	void GoTo();
	void GoBackTo();

	CNode* m_control;		// joystick for this level
	CNode* m_tunnel;		// geometry on the way in
	CNode* m_path;			// camera path through it
	CNode* m_shell;
	TCHAR* m_archive;
	bool m_unloadable;
	bool m_fade;
	float m_timeToNextLevel;

protected:
	void Activate();
	void Deactivate();

	CXipFile* m_xipFile;
	bool m_bArrive;
};

class CViewpoint : public CNode
{
	DECLARE_NODE(CViewpoint, CNode)
	DECLARE_NODE_PROPS()
public:
	CViewpoint();
	~CViewpoint();

	void OnLoad();
	bool OnSetProperty(const PRD* prop, const void* value);
	void Bind();

	bool m_isBound;
	float m_fieldOfView;
	bool m_jump;
	D3DXQUATERNION m_orientation;
	D3DXVECTOR3 m_position;
	TCHAR* m_description;
};

extern void BindViewpoint(CNode* viewpoint);

class CNavigationInfo : public CNode
{
	DECLARE_NODE(CNavigationInfo, CNode)
	DECLARE_NODE_PROPS()
public:
	CNavigationInfo();
	~CNavigationInfo();

	void OnLoad();
	bool OnSetProperty(const PRD* prop, const void* value);
	void Bind();

	bool m_isBound;
	D3DXVECTOR3 m_avatarSize;
	bool m_headlight;
	float m_speed;
	TCHAR* m_type;
	float m_visibilityLimit;
	CNode* m_shape;
};
