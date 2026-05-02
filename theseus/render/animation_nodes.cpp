// animation_nodes.cpp: time-driven nodes, interpolators, and camera /
// navigation-info binding. Decompiled from the 5960 retail XBE; see
// docs/decomp/AnimationNodes.md.

#include "std.h"
#include "theseus.h"
#include "node.h"
#include "interpolator.h"
#include "camera.h"
#include "scene_groups.h"

// =========================================================================
// CTimeSensor: fires fraction_changed callbacks at regular intervals.
// =========================================================================

class CTimeSensor : public CTimeDepNode
{
	DECLARE_NODE(CTimeSensor, CTimeDepNode)
public:
	CTimeSensor();

	float m_cycleInterval;
	bool  m_enabled;
	XTIME m_cycleTime;
	float m_fraction_changed;
	XTIME m_time;

	void Advance(float nSeconds);
	void OnIsActiveChanged();
	bool OnSetProperty(const PRD* pprd, const void* pvValue);

	DECLARE_NODE_PROPS()
};

IMPLEMENT_NODE("TimeSensor", CTimeSensor, CTimeDepNode)

START_NODE_PROPS(CTimeSensor, CTimeDepNode)
	NODE_PROP(pt_number, CTimeSensor, cycleInterval)
	NODE_PROP(pt_boolean, CTimeSensor, enabled)
	NODE_PROP(pt_number, CTimeSensor, cycleTime)
	NODE_PROP(pt_number, CTimeSensor, fraction_changed)
	NODE_PROP(pt_number, CTimeSensor, time)
END_NODE_PROPS()

CTimeSensor::CTimeSensor()
	: m_cycleInterval(1.0f), m_enabled(true)
{
}

bool CTimeSensor::OnSetProperty(const PRD* pprd, const void* pvValue)
{
	if (PTR2INT(pprd->pbOffset) == offsetof(m_time))
	{
		m_time = (XTIME)(*(float*)pvValue);
		return false;
	}
	return true;
}

void CTimeSensor::Advance(float nSeconds)
{
	XTIME now = TheseusGetNow();
	CTimeDepNode::Advance(nSeconds);

	if (m_isActive)
	{
		m_time = now;

		if (now >= m_cycleTime + m_cycleInterval)
		{
			if (m_loop)
				m_cycleTime += m_cycleInterval;
			else
			{
				m_isActive = false;
				OnIsActiveChanged();
			}
		}

		float temp = (float)(now - m_startTime) / m_cycleInterval;
		m_fraction_changed = temp - (int)temp;
		if (m_fraction_changed == 0.0f && now > m_startTime)
			m_fraction_changed = 1.0f;
		CallFunction(this, _T("fraction_changed"));
	}
}

void CTimeSensor::OnIsActiveChanged()
{
	if (m_isActive)
		m_cycleTime = TheseusGetNow();
	else
		CallFunction(this, _T("OnActiveChanged"));
}

// =========================================================================
// CPositionInterpolator: Catmull-Rom / lerp interpolation on Vec3 keys.
// =========================================================================

IMPLEMENT_NODE("PositionInterpolator", CPositionInterpolator, CNode)

START_NODE_PROPS(CPositionInterpolator, CNode)
	NODE_PROP(pt_numarray, CPositionInterpolator, key)
	NODE_PROP(pt_vec3array, CPositionInterpolator, keyValue)
END_NODE_PROPS()

CPositionInterpolator::CPositionInterpolator() {}
CPositionInterpolator::~CPositionInterpolator() {}

D3DXVECTOR3 CPositionInterpolator::Interpolate(float key)
{
	float k = key * (m_keyValue.m_nSize - 1);
	int idx = (int)floorf(k);
	float frac = k - floorf(k);

	if (idx < 0)
		return m_keyValue.m_value[0];
	if (idx >= m_keyValue.m_nSize - 1)
		return m_keyValue.m_value[m_keyValue.m_nSize - 1];

	D3DXVECTOR3 result;
	if (idx > 0 && idx < m_keyValue.m_nSize - 2 && m_keyValue.m_nSize >= 4)
	{
		D3DXVec3CatmullRom(&result,
			&m_keyValue.m_value[idx - 1], &m_keyValue.m_value[idx],
			&m_keyValue.m_value[idx + 1], &m_keyValue.m_value[idx + 2], frac);
	}
	else
	{
		D3DXVec3Lerp(&result, &m_keyValue.m_value[idx], &m_keyValue.m_value[idx + 1], frac);
	}
	return result;
}

// =========================================================================
// COrientationInterpolator: quaternion slerp on Vec4 keys.
// =========================================================================

IMPLEMENT_NODE("OrientationInterpolator", COrientationInterpolator, CNode)

START_NODE_PROPS(COrientationInterpolator, CNode)
	NODE_PROP(pt_numarray, COrientationInterpolator, key)
	NODE_PROP(pt_vec4array, COrientationInterpolator, keyValue)
END_NODE_PROPS()

COrientationInterpolator::COrientationInterpolator() {}
COrientationInterpolator::~COrientationInterpolator() {}

void COrientationInterpolator::OnLoad()
{
	CNode::OnLoad();
	for (int i = 0; i < m_keyValue.m_nSize; i++)
	{
		D3DXQUATERNION q;
		D3DXQuaternionRotationAxis(&q, (D3DXVECTOR3*)&m_keyValue.m_value[i], -m_keyValue.m_value[i].w);
		*((D3DXQUATERNION*)&m_keyValue.m_value[i]) = q;
	}
}

D3DXQUATERNION COrientationInterpolator::Interpolate(float key)
{
	float k = key * (m_keyValue.m_nSize - 1);
	int idx = (int)floorf(k);
	float frac = k - floorf(k);

	if (idx < 0)
		return *((D3DXQUATERNION*)&m_keyValue.m_value[0]);
	if (idx >= m_keyValue.m_nSize - 1)
		return *((D3DXQUATERNION*)&m_keyValue.m_value[m_keyValue.m_nSize - 1]);

	D3DXQUATERNION result;
	D3DXQuaternionSlerp(&result,
		(D3DXQUATERNION*)&m_keyValue.m_value[idx],
		(D3DXQUATERNION*)&m_keyValue.m_value[idx + 1], frac);
	return result;
}

// =========================================================================
// CViewpoint: camera position / orientation binding.
// =========================================================================

IMPLEMENT_NODE("Viewpoint", CViewpoint, CNode)

START_NODE_PROPS(CViewpoint, CNode)
	NODE_PROP(pt_number, CViewpoint, fieldOfView)
	NODE_PROP(pt_boolean, CViewpoint, jump)
	NODE_PROP(pt_boolean, CViewpoint, isBound)
	NODE_PROP(pt_vec3, CViewpoint, position)
	NODE_PROP(pt_vec4, CViewpoint, orientation)
	NODE_PROP(pt_string, CViewpoint, description)
END_NODE_PROPS()

CViewpoint::CViewpoint()
	: m_isBound(false), m_fieldOfView(0.785398f), m_jump(true),
	  m_orientation(0.0f, 0.0f, 1.0f, 0.0f), m_position(0.0f, 0.0f, 10.0f),
	  m_description(NULL)
{
}

CViewpoint::~CViewpoint()
{
	delete[] m_description;
	if (TheseusGetViewpoint() == this)
		TheseusSetViewpoint(NULL);
}

void CViewpoint::OnLoad()
{
	CNode::OnLoad();
	if (TheseusGetViewpoint() == NULL)
		Bind();
}

void BindViewpoint(CNode* pViewpointNode)
{
	if (pViewpointNode == NULL || !pViewpointNode->IsKindOf(NODE_CLASS(CViewpoint)))
		return;
	((CViewpoint*)pViewpointNode)->Bind();
}

void CViewpoint::Bind()
{
	if (TheseusGetViewpoint() == this)
		return;

	if (TheseusGetViewpoint() != NULL)
		TheseusGetViewpoint()->m_isBound = false;

	TheseusSetViewpoint(this);

	D3DXQUATERNION q;
	D3DXQuaternionRotationAxis(&q, (D3DXVECTOR3*)&m_orientation, -m_orientation.w);

	D3DXVECTOR3 position = m_position;
	position.z = -position.z;

	theCamera.Set(&position, &q, m_jump ? 0.0f : 1.0f);

	TheseusSetProjectionDirty();
}

bool CViewpoint::OnSetProperty(const PRD* pprd, const void* pvValue)
{
	if (PTR2INT(pprd->pbOffset) == offsetof(m_isBound))
	{
		if (*(bool*)pvValue)
			Bind();
	}
	else if (PTR2INT(pprd->pbOffset) == offsetof(m_fieldOfView))
	{
		TheseusSetProjectionDirty();
	}
	return true;
}

// =========================================================================
// CNavigationInfo: avatar size, speed, navigation-type binding.
// =========================================================================

IMPLEMENT_NODE("NavigationInfo", CNavigationInfo, CNode)

START_NODE_PROPS(CNavigationInfo, CNode)
	NODE_PROP(pt_boolean, CNavigationInfo, isBound)
	NODE_PROP(pt_vec3, CNavigationInfo, avatarSize)
	NODE_PROP(pt_boolean, CNavigationInfo, headlight)
	NODE_PROP(pt_number, CNavigationInfo, speed)
	NODE_PROP(pt_string, CNavigationInfo, type)
	NODE_PROP(pt_number, CNavigationInfo, visibilityLimit)
	NODE_PROP(pt_node, CNavigationInfo, shape)
END_NODE_PROPS()

CNavigationInfo::CNavigationInfo()
	: m_isBound(false), m_avatarSize(0.25f, 1.6f, 0.75f),
	  m_headlight(true), m_speed(1.0f), m_type(NULL),
	  m_visibilityLimit(0.0f), m_shape(NULL)
{
}

CNavigationInfo::~CNavigationInfo()
{
	delete[] m_type;
	if (TheseusGetNavigationInfo() == this)
		TheseusSetNavigationInfo(NULL);
}

void CNavigationInfo::OnLoad()
{
	CNode::OnLoad();
	if (TheseusGetNavigationInfo() == NULL)
	{
		TheseusSetNavigationInfo(this);
		Bind();
	}
}

void CNavigationInfo::Bind()
{
	m_isBound = true;

	TheseusSetProjectionDirty();
}

bool CNavigationInfo::OnSetProperty(const PRD* pprd, const void* pvValue)
{
	if (PTR2INT(pprd->pbOffset) == offsetof(m_isBound))
	{
		if (*(bool*)pvValue)
			Bind();
	}
	else if (PTR2INT(pprd->pbOffset) == offsetof(m_avatarSize) ||
			 PTR2INT(pprd->pbOffset) == offsetof(m_visibilityLimit))
	{
		TheseusSetProjectionDirty();
	}
	return true;
}
