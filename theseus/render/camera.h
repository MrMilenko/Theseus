// camera.h: the one scene camera and the paths that fly it between levels.
// Bodies in render/camera.cpp; field offsets in docs/decomp/Camera.md.

#pragma once

// Set() starts a timed move from where the camera is to a new pose;
// Advance() eases along it and UpdateViewMatrix() loads the result.
class CCamera : public CNode
{
	DECLARE_NODE(CCamera, CNode)
	DECLARE_NODE_PROPS()
public:
	CCamera();
	~CCamera();

	void Advance(float dt);
	void Reset();
	void Set(const D3DXVECTOR3* pos, const D3DXQUATERNION* rot, float dt = 0.0f);
	void UpdateViewMatrix();

	char* m_mode;
	CNode* m_lookat;
	bool m_bNoisy;

	D3DXVECTOR3 m_position;
	D3DXQUATERNION m_orientation;
	D3DXVECTOR3 m_oldPosition;		// where the move started
	D3DXQUATERNION m_oldOrientation;
	D3DXVECTOR3 m_newPosition;		// where it ends
	D3DXQUATERNION m_newOrientation;
	XTIME m_nStartTime;
	float m_nDeltaTime;
	D3DXVECTOR3 m_actualPosition;
};

// Drives theCamera from a position and an orientation interpolator over
// m_interval seconds, either way along the path.
class CCameraPath : public CNode
{
	DECLARE_NODE(CCameraPath, CNode)
	DECLARE_NODE_PROPS()
public:
	CCameraPath();
	~CCameraPath();

	void Advance(float dt);
	bool OnSetProperty(const PRD* prop, const void* value);
	void Activate(bool backwards);

	float m_interval;
	bool m_isActive;
	bool m_backward;
	CNode* m_position;
	CNode* m_orientation;

protected:
	XTIME m_startTime;
};

extern CCamera theCamera;
