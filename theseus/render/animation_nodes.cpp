// animation_nodes.cpp: the clock and keyframe nodes (TimeSensor and the two
// interpolators) plus the Viewpoint and NavigationInfo bindings. Rebuilt from
// the 5960 retail XBE; findings are in docs/decomp/AnimationNodes.md.

#include "std.h"
#include "theseus.h"
#include "node.h"
#include "interpolator.h"
#include "camera.h"
#include "scene_groups.h"


// ===== CTimeSensor ==========================================================
// 0x50 bytes, factory 0x00041bf0, vtable 0x00026018. On top of CTimeDepNode:
// cycleInterval 0x30, enabled 0x34, cycleTime 0x38 (double),
// fraction_changed 0x40, time 0x48 (double).

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

// CTimeSensor::CTimeSensor()
// Reconstructed from 5960 retail XBE @ 0x00041bf0 (factory). Ghidra decompile:
//     <CTimeDepNode fields zeroed>; cycleInterval = 1.0; enabled = 1;
CTimeSensor::CTimeSensor()
{
	m_cycleInterval = 1.0f;
	m_enabled = true;
}

// CTimeSensor::OnSetProperty(pprd, pvValue)
// Reconstructed from 5960 retail XBE @ 0x00041c70. Ghidra decompile:
//     if (prd->offset == 0x48) { time = (double)*(float*)value; return 0; }
//     return 1;
// Script hands numbers over as floats; time is a double, so it's widened here.
bool CTimeSensor::OnSetProperty(const PRD* pprd, const void* pvValue)
{
	if (PTR2INT(pprd->pbOffset) != offsetof(m_time))
		return true;

	m_time = *(const float*)pvValue;
	return false;
}

// CTimeSensor::Advance(nSeconds)
// Reconstructed from 5960 retail XBE @ 0x00041c90. Ghidra decompile:
//     now = g_now; CTimeDepNode::Advance(secs);
//     if (!isActive) return;
//     time = now;
//     end = cycleInterval + cycleTime;
//     if (end <= now) { if (loop) cycleTime = end;
//                       else { isActive = 0; this->OnIsActiveChanged(); } }
//     f = (float)((now - startTime) / cycleInterval);
//     fraction_changed = f - (int)f;
//     if (fraction_changed == 0.0 && startTime < now) fraction_changed = 1.0;
//     CallFunction(this, L"fraction_changed", 0, 0);
// The 1.0 case lets a cycle end land on its last frame instead of wrapping.
void CTimeSensor::Advance(float nSeconds)
{
	XTIME now = TheseusGetNow();
	CTimeDepNode::Advance(nSeconds);

	if (!m_isActive)
		return;

	m_time = now;

	XTIME cycleEnd = m_cycleTime + m_cycleInterval;
	if (now >= cycleEnd)
	{
		if (m_loop)
		{
			m_cycleTime = cycleEnd;
		}
		else
		{
			m_isActive = false;
			OnIsActiveChanged();
		}
	}

	float cycles = (float)(now - m_startTime) / m_cycleInterval;
	m_fraction_changed = cycles - (float)(int)cycles;
	if (m_fraction_changed == 0.0f && m_startTime < now)
		m_fraction_changed = 1.0f;

	CallFunction(this, _T("fraction_changed"));
}

// CTimeSensor::OnIsActiveChanged()
// Reconstructed from 5960 retail XBE @ 0x00041d50. Ghidra decompile:
//     if (isActive) { cycleTime = g_now; return; }
//     CallFunction(this, L"OnActiveChanged", 0, 0);
void CTimeSensor::OnIsActiveChanged()
{
	if (!m_isActive)
	{
		CallFunction(this, _T("OnActiveChanged"));
		return;
	}

	m_cycleTime = TheseusGetNow();
}


// ===== Interpolators ========================================================
// Both are 0x34 bytes: key array at 0x1c (CNumArray), keyValue at 0x28
// (count 0x2c, data 0x30). Both look up by spreading the key evenly over the
// keyValue entries; the key array itself is never read.

IMPLEMENT_NODE("PositionInterpolator", CPositionInterpolator, CNode)

START_NODE_PROPS(CPositionInterpolator, CNode)
	NODE_PROP(pt_numarray, CPositionInterpolator, key)
	NODE_PROP(pt_vec3array, CPositionInterpolator, keyValue)
END_NODE_PROPS()

// Factory 0x0005d250 zeroes both arrays; the destructor 0x0005d2d0 frees them,
// which the array members do on their own.
CPositionInterpolator::CPositionInterpolator()
{
}

CPositionInterpolator::~CPositionInterpolator()
{
}

// CPositionInterpolator::Interpolate(key)
// Reconstructed from 5960 retail XBE @ 0x0005d300. Ghidra decompile:
//     k = (count - 1) * key; i = (int)floor(k); frac = k - floor(k);
//     if (i < 0) return v[0];
//     if (count - 1 <= i) return v[count - 1];
//     if (0 < i && i < count - 2 && 3 < count)
//         D3DXVec3CatmullRom(&out, &v[i-1], &v[i], &v[i+1], &v[i+2], frac);
//     else out = v[i] + (v[i+1] - v[i]) * frac;
// Catmull-Rom needs a key on each side, so the end spans fall back to a lerp.
D3DXVECTOR3 CPositionInterpolator::Interpolate(float key)
{
	const D3DXVECTOR3* v = m_keyValue.m_value;
	int last = m_keyValue.m_nSize - 1;

	float span = key * (float)last;
	float whole = floorf(span);
	int i = (int)whole;
	float frac = span - whole;

	if (i < 0)
		return v[0];
	if (i >= last)
		return v[last];

	D3DXVECTOR3 out;
	if (i > 0 && i < last - 1 && m_keyValue.m_nSize > 3)
		D3DXVec3CatmullRom(&out, &v[i - 1], &v[i], &v[i + 1], &v[i + 2], frac);
	else
		D3DXVec3Lerp(&out, &v[i], &v[i + 1], frac);
	return out;
}

IMPLEMENT_NODE("OrientationInterpolator", COrientationInterpolator, CNode)

START_NODE_PROPS(COrientationInterpolator, CNode)
	NODE_PROP(pt_numarray, COrientationInterpolator, key)
	NODE_PROP(pt_vec4array, COrientationInterpolator, keyValue)
END_NODE_PROPS()

// Factory 0x0005d470 / destructor 0x0005d4f0, same as the position one.
COrientationInterpolator::COrientationInterpolator()
{
}

COrientationInterpolator::~COrientationInterpolator()
{
}

// COrientationInterpolator::OnLoad()
// Reconstructed from 5960 retail XBE @ 0x0005d520. Ghidra decompile:
//     CNode::OnLoad();
//     for (i = 0; i < count; i++)
//         D3DXQuaternionRotationAxis(&v[i], (vec3*)&v[i], 0.0 - v[i].w);
// The file stores VRML axis+angle; this turns each into a quaternion once.
void COrientationInterpolator::OnLoad()
{
	CNode::OnLoad();

	for (int i = 0; i < m_keyValue.m_nSize; i++)
	{
		D3DXVECTOR4& axisAngle = m_keyValue.m_value[i];
		D3DXQUATERNION q;
		D3DXQuaternionRotationAxis(&q, (const D3DXVECTOR3*)&axisAngle, 0.0f - axisAngle.w);
		axisAngle = D3DXVECTOR4(q.x, q.y, q.z, q.w);
	}
}

// COrientationInterpolator::Interpolate(key)
// Reconstructed from 5960 retail XBE @ 0x0005d5a0. Ghidra decompile:
//     k = (count - 1) * key; i = (int)floor(k);
//     if (i < 0) return v[0];
//     if (count - 1 <= i) return v[count - 1];
//     D3DXQuaternionSlerp(&out, &v[i], &v[i+1], k - floor(k));
D3DXQUATERNION COrientationInterpolator::Interpolate(float key)
{
	const D3DXQUATERNION* q = (const D3DXQUATERNION*)m_keyValue.m_value;
	int last = m_keyValue.m_nSize - 1;

	float span = key * (float)last;
	float whole = floorf(span);
	int i = (int)whole;

	if (i < 0)
		return q[0];
	if (i >= last)
		return q[last];

	D3DXQUATERNION out;
	D3DXQuaternionSlerp(&out, &q[i], &q[i + 1], span - whole);
	return out;
}


// ===== CViewpoint ===========================================================
// 0x48 bytes, factory 0x0004f770, vtable 0x00027f10. Field offsets:
// isBound 0x1c, fieldOfView 0x20, jump 0x24, orientation 0x28,
// position 0x38, description 0x44. The bound one is the global at 0x0017e81c.

IMPLEMENT_NODE("Viewpoint", CViewpoint, CNode)

START_NODE_PROPS(CViewpoint, CNode)
	NODE_PROP(pt_number, CViewpoint, fieldOfView)
	NODE_PROP(pt_boolean, CViewpoint, jump)
	NODE_PROP(pt_boolean, CViewpoint, isBound)
	NODE_PROP(pt_vec3, CViewpoint, position)
	NODE_PROP(pt_vec4, CViewpoint, orientation)
	NODE_PROP(pt_string, CViewpoint, description)
END_NODE_PROPS()

// CViewpoint::CViewpoint()
// Reconstructed from 5960 retail XBE @ 0x0004f770 (factory). Ghidra decompile:
//     isBound = 0; fieldOfView = 0.785398; jump = 1;
//     orientation = (0, 0, 1.0, 0); position = (0, 0, 10.0); description = NULL;
CViewpoint::CViewpoint()
{
	m_isBound = false;
	m_fieldOfView = 0.785398f;
	m_jump = true;
	m_orientation = D3DXQUATERNION(0.0f, 0.0f, 1.0f, 0.0f);
	m_position = D3DXVECTOR3(0.0f, 0.0f, 10.0f);
	m_description = NULL;
}

// CViewpoint::~CViewpoint()
// Reconstructed from 5960 retail XBE @ 0x0004f830. Ghidra decompile:
//     free(description); if (g_pViewpoint == this) g_pViewpoint = NULL;
CViewpoint::~CViewpoint()
{
	delete [] m_description;

	if (TheseusGetViewpoint() == this)
		TheseusSetViewpoint(NULL);
}

// CViewpoint::OnLoad()
// Reconstructed from 5960 retail XBE @ 0x0004f860. Ghidra decompile:
//     CNode::OnLoad(); if (g_pViewpoint == NULL) Bind();
// The first viewpoint a scene loads takes the camera.
void CViewpoint::OnLoad()
{
	CNode::OnLoad();

	if (TheseusGetViewpoint() == NULL)
		Bind();
}

// BindViewpoint(pNode)
// Reconstructed from 5960 retail XBE @ 0x0004f880. Ghidra decompile:
//     if (node && node->GetNodeClass() is or derives from CViewpoint) Bind();
void BindViewpoint(CNode* pViewpointNode)
{
	if (pViewpointNode != NULL && pViewpointNode->IsKindOf(NODE_CLASS(CViewpoint)))
		((CViewpoint*)pViewpointNode)->Bind();
}

// CViewpoint::Bind()
// Reconstructed from 5960 retail XBE @ 0x0004f8b0. Ghidra decompile:
//     if (g_pViewpoint == this) return;
//     if (g_pViewpoint) g_pViewpoint->isBound = 0;
//     g_pViewpoint = this;
//     D3DXQuaternionRotationAxis(&rot, &orientation.xyz, 0.0 - orientation.w);
//     pos = (position.x, position.y, 0.0 - position.z);
//     theCamera.Set(&pos, &rot, jump ? 0.0 : 1.0);
//     g_bProjectionDirty = 1;
// A non jump viewpoint glides the camera over one second.
void CViewpoint::Bind()
{
	CViewpoint* previous = TheseusGetViewpoint();
	if (previous == this)
		return;

	if (previous != NULL)
		previous->m_isBound = false;

	TheseusSetViewpoint(this);

	D3DXQUATERNION facing;
	D3DXQuaternionRotationAxis(&facing, (const D3DXVECTOR3*)&m_orientation, 0.0f - m_orientation.w);

	D3DXVECTOR3 eye(m_position.x, m_position.y, 0.0f - m_position.z);
	theCamera.Set(&eye, &facing, m_jump ? 0.0f : 1.0f);

	TheseusSetProjectionDirty();
}

// CViewpoint::OnSetProperty(pprd, pvValue)
// Reconstructed from 5960 retail XBE @ 0x0004f940. Ghidra decompile:
//     if (prd->offset == 0x1c) { if (*(bool*)value) Bind(); }
//     else if (prd->offset == 0x20) g_bProjectionDirty = 1;
//     return 1;
bool CViewpoint::OnSetProperty(const PRD* pprd, const void* pvValue)
{
	int field = PTR2INT(pprd->pbOffset);

	if (field == offsetof(m_isBound))
	{
		if (*(const bool*)pvValue)
			Bind();
	}
	else if (field == offsetof(m_fieldOfView))
	{
		TheseusSetProjectionDirty();
	}

	return true;
}


// ===== CNavigationInfo ======================================================
// 0x40 bytes, factory 0x000504d0, vtable 0x000281f0. Field offsets:
// isBound 0x1c, avatarSize 0x20, headlight 0x2c, speed 0x30, type 0x34,
// visibilityLimit 0x38, shape 0x3c. The bound one is the global at 0x0017e820.

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

// CNavigationInfo::CNavigationInfo()
// Reconstructed from 5960 retail XBE @ 0x000504d0 (factory). Ghidra decompile:
//     isBound = 0; avatarSize = (0.25, 1.6, 0.75); headlight = 1; speed = 1.0;
//     type = NULL; visibilityLimit = 0; shape = NULL;
CNavigationInfo::CNavigationInfo()
{
	m_isBound = false;
	m_avatarSize = D3DXVECTOR3(0.25f, 1.6f, 0.75f);
	m_headlight = true;
	m_speed = 1.0f;
	m_type = NULL;
	m_visibilityLimit = 0.0f;
	m_shape = NULL;
}

// CNavigationInfo::~CNavigationInfo()
// Reconstructed from 5960 retail XBE @ 0x00050580. Ghidra decompile:
//     free(type); if (g_pNavInfo == this) g_pNavInfo = NULL;
CNavigationInfo::~CNavigationInfo()
{
	delete [] m_type;

	if (TheseusGetNavigationInfo() == this)
		TheseusSetNavigationInfo(NULL);
}

// CNavigationInfo::OnLoad()
// Reconstructed from 5960 retail XBE @ 0x000505b0. Ghidra decompile:
//     CNode::OnLoad();
//     if (g_pNavInfo == NULL) { g_pNavInfo = this; isBound = 1; g_bProjectionDirty = 1; }
void CNavigationInfo::OnLoad()
{
	CNode::OnLoad();

	if (TheseusGetNavigationInfo() != NULL)
		return;

	TheseusSetNavigationInfo(this);
	Bind();
}

// CNavigationInfo::Bind()
// The binary inlines this pair into OnLoad and OnSetProperty.
void CNavigationInfo::Bind()
{
	m_isBound = true;
	TheseusSetProjectionDirty();
}

// CNavigationInfo::OnSetProperty(pprd, pvValue)
// Reconstructed from 5960 retail XBE @ 0x000505e0. Ghidra decompile:
//     switch (prd->offset) {
//     case 0x1c: if (*(bool*)value) { isBound = 1; g_bProjectionDirty = 1; } break;
//     case 0x20: case 0x38: g_bProjectionDirty = 1; break; }
//     return 1;
// Avatar size and visibility limit feed the near and far planes.
bool CNavigationInfo::OnSetProperty(const PRD* pprd, const void* pvValue)
{
	int field = PTR2INT(pprd->pbOffset);

	if (field == offsetof(m_isBound))
	{
		if (*(const bool*)pvValue)
			Bind();
	}
	else if (field == offsetof(m_avatarSize) || field == offsetof(m_visibilityLimit))
	{
		TheseusSetProjectionDirty();
	}

	return true;
}
