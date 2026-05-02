// camera.h: CCamera node declaration. Camera position, orientation, and
// view matrix construction. Companion to render/camera.cpp.

#pragma once

class CCamera : public CNode
{
	DECLARE_NODE(CCamera, CNode)
public:
	CCamera();
	~CCamera();

	char* m_mode;
	CNode* m_lookat;

	bool m_bNoisy;

	DECLARE_NODE_PROPS()


	void Reset();

	void Set(const D3DXVECTOR3* pPosition, const D3DXQUATERNION* pOrientation, float nSeconds = 0.0f);

	void UpdateViewMatrix();

	void Advance(float nSeconds);

	D3DXVECTOR3 m_position;
	D3DXQUATERNION m_orientation;

	D3DXVECTOR3 m_oldPosition;
	D3DXQUATERNION m_oldOrientation;

	D3DXVECTOR3 m_newPosition;
	D3DXQUATERNION m_newOrientation;

	XTIME m_nStartTime;
	float m_nDeltaTime;

	D3DXVECTOR3 m_actualPosition;
};

class CCameraPath : public CNode
{
	DECLARE_NODE(CCameraPath, CNode)
public:
	CCameraPath();
	~CCameraPath();

	void Advance(float nSeconds);
	bool OnSetProperty(const PRD* pprd, const void* pvValue);

	float m_interval;
	bool m_isActive;
	bool m_backward;
	CNode* m_position;
	CNode* m_orientation;

	void Activate(bool bBackwards);

protected:
	XTIME m_startTime;

	DECLARE_NODE_PROPS()
};

extern CCamera theCamera;
