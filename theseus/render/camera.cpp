// camera.cpp: CCamera node implementation. View matrix construction,
// position / orientation lerping, viewpoint binding hooks. Decompiled
// from the 5960 retail XBE.

#include "std.h"
#include "theseus.h"
#include "node.h"
#include "camera.h"
#include "shape_render.h"
#include "interpolator.h"

extern D3DXMATRIX g_matView;
extern D3DXMATRIX g_matPosition;
extern bool g_bMovingScreen;

IMPLEMENT_NODE("Camera", CCamera, CNode)

START_NODE_PROPS(CCamera, CNode)
	NODE_PROP(pt_string, CCamera, mode)
	NODE_PROP(pt_node, CCamera, lookat)
END_NODE_PROPS()

CCamera theCamera;

CCamera::CCamera()
{
	Reset();
	m_mode = NULL;
}

CCamera::~CCamera()
{
	delete [] m_mode;
}

void CCamera::Reset()
{
	m_nDeltaTime = 0.0f;
	m_position.x = 0.0f;
	m_position.y = 0.0f;
	m_position.z = 20.0f;

	D3DXQuaternionIdentity(&m_orientation);
}

void CCamera::UpdateViewMatrix()
{
	D3DXVECTOR3 position = m_position;
	D3DXQUATERNION orientation = m_orientation;

	m_bNoisy = g_bMovingScreen;

	if (m_bNoisy)
	{
		float t = (float) (TheseusGetNow() / 4.0f);
		float x = sinf(t * D3DX_PI / 2.0f);
		float y = sinf(t * D3DX_PI);

		// Plus or minus 1 degree...
		x *= D3DX_PI / 180.0f;
		y *= D3DX_PI / 180.0f;

		D3DXQUATERNION q;
		D3DXQuaternionRotationYawPitchRoll(&q, x, y, 0.0f);
		D3DXQuaternionMultiply(&orientation, &q, &orientation);
	}

	m_actualPosition = position;
	D3DXMatrixAffineTransformation(&g_matPosition, 1.0f, NULL, &orientation, &position);
	D3DXMatrixInverse(&g_matView, NULL, &g_matPosition);
	g_matView._31 = -g_matView._31;
	g_matView._32 = -g_matView._32;
	g_matView._33 = -g_matView._33;
	g_matView._34 = -g_matView._34;
	TheseusSetTransform(D3DTS_VIEW, &g_matView);
}

void CCamera::Set(const D3DXVECTOR3* pPosition, const D3DXQUATERNION* pOrientation, float nSeconds/*=0.0f*/)
{
	m_newPosition = *pPosition;
	m_newOrientation = *pOrientation;

	m_oldPosition = m_position;
	m_oldOrientation = m_orientation;

	m_nStartTime = TheseusGetNow();
	m_nDeltaTime = nSeconds;

	if (m_nDeltaTime <= 0.0f)
	{
		m_position = m_newPosition;
		m_orientation = m_newOrientation;
	}
}

void CCamera::Advance(float nSeconds)
{
	if (m_nDeltaTime > 0.0f)
	{
		float f = (float) (TheseusGetNow() - m_nStartTime) / m_nDeltaTime;
		if (f >= 1.0f)
		{
			m_nDeltaTime = 0.0f;
			f = 1.0f;
		}

		D3DXVec3Lerp(&m_position, &m_oldPosition, &m_newPosition, f);
		D3DXQuaternionSlerp(&m_orientation, &m_oldOrientation, &m_newOrientation, f);

	}

	UpdateViewMatrix();
}




IMPLEMENT_NODE("CameraPath", CCameraPath, CNode)

START_NODE_PROPS(CCameraPath, CNode)
	NODE_PROP(pt_boolean, CCameraPath, isActive)
	NODE_PROP(pt_boolean, CCameraPath, backward)
	NODE_PROP(pt_number, CCameraPath, interval)
	NODE_PROP(pt_node, CCameraPath, position)
	NODE_PROP(pt_node, CCameraPath, orientation)
END_NODE_PROPS()

CCameraPath::CCameraPath() :
	m_isActive(false),
	m_backward(false),
	m_interval(1.0f),
	m_position(NULL),
	m_orientation(NULL)
{
}

CCameraPath::~CCameraPath()
{
	if (m_position != NULL)
		m_position->Release();

	if (m_orientation != NULL)
		m_orientation = NULL;
}

void CCameraPath::Advance(float nSeconds)
{
	CNode::Advance(nSeconds);

	if (!m_isActive)
		return;

	float t = (float) (TheseusGetNow() - m_startTime) / m_interval;
	if (t >= 1.0f)
	{
		t = 1.0f;
		m_isActive = false;

		TRACE(_T("End of camera path\n"));
	}

	if (m_backward)
		t = 1.0f - t;

	D3DXVECTOR3 position = theCamera.m_position;
	D3DXQUATERNION orientation = theCamera.m_orientation;

	if (m_position != NULL)
	{
		position = ((CPositionInterpolator*)m_position)->Interpolate(t);
		position.z = -position.z;
	}

	if (m_orientation != NULL)
		orientation = ((COrientationInterpolator*)m_orientation)->Interpolate(t);

	theCamera.Set(&position, &orientation);
}

bool CCameraPath::OnSetProperty(const PRD* pprd, const void* pvValue)
{
	if (PTR2INT(pprd->pbOffset) == offsetof(m_isActive))
		m_startTime = TheseusGetNow();

	return true;
}

void CCameraPath::Activate(bool bBackwards)
{
	m_isActive = true;
	m_startTime = TheseusGetNow();
	m_backward = bBackwards;
}
