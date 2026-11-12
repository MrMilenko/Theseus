// camera.cpp: the one scene camera and the CameraPath node that flies it.
// Both rebuilt from the 5960 retail XBE (0x50640..0x50c90); findings are in
// docs/decomp/Camera.md.

#include "std.h"
#include "theseus.h"
#include "node.h"
#include "camera.h"
#include "shape_render.h"
#include "interpolator.h"

extern D3DXMATRIX g_matView;
extern D3DXMATRIX g_matPosition;
extern bool g_bMovingScreen;


// ===== CCamera ==============================================================
// The global camera lives at 0x00187fb0, 0x90 bytes, factory 0x00050640,
// vtable 0x00028350. Field offsets: mode 0x1c, lookat 0x20, position 0x24,
// orientation 0x30, old pair 0x40/0x4c, new pair 0x5c/0x68, start time 0x78
// (double), duration 0x80, position actually used 0x84.

IMPLEMENT_NODE("Camera", CCamera, CNode)

START_NODE_PROPS(CCamera, CNode)
	NODE_PROP(pt_string, CCamera, mode)
	NODE_PROP(pt_node, CCamera, lookat)
END_NODE_PROPS()

CCamera theCamera;

// CCamera::CCamera()
// Reconstructed from 5960 retail XBE @ 0x00050640 (factory). Ghidra decompile:
//     duration = 0; position = (0, 0, 20.0); orientation = (0, 0, 0, 1.0);
//     mode = NULL; g_bNoisyCamera = 0;
// Theseus leaves the noisy flag where the config loader put it.
CCamera::CCamera()
{
	m_mode = NULL;
	Reset();
}

// CCamera::~CCamera()
// Reconstructed from 5960 retail XBE @ 0x00050760: frees mode, then CNode.
CCamera::~CCamera()
{
	delete [] m_mode;
}

// CCamera::Reset()
// The factory's field setup above; Theseus also calls it on its own.
void CCamera::Reset()
{
	m_position = D3DXVECTOR3(0.0f, 0.0f, 20.0f);
	m_orientation = D3DXQUATERNION(0.0f, 0.0f, 0.0f, 1.0f);
	m_nDeltaTime = 0.0f;
}

// CCamera::UpdateViewMatrix()
// Reconstructed from 5960 retail XBE @ 0x00050780. Ghidra decompile:
//     pos = this->position; rot = this->orientation;
//     if (g_bNoisyCamera) {
//         t = now * 0.25;
//         YawPitchRoll(&wobble, sin(t * PI/2) * DEG, sin(t * PI) * DEG, 0);
//         QuaternionMultiply(&rot, &wobble, &rot);
//     }
//     this->actualPos = pos;
//     MatrixAffineTransformation(&g_matPosition, 1.0, NULL, &rot, &pos);
//     MatrixInverse(&g_matView, NULL, &g_matPosition);
//     g_matView._31.._34 = 0.0 - each;   then copied into the device view.
// The wobble is at most one degree each way. Flipping the third row turns
// the right handed scene camera into D3D's left handed view.
void CCamera::UpdateViewMatrix()
{
	D3DXVECTOR3 eye = m_position;
	D3DXQUATERNION facing = m_orientation;

	m_bNoisy = g_bMovingScreen;
	if (m_bNoisy)
	{
		float t = (float)(TheseusGetNow() * 0.25);
		float yaw = sinf(t * (D3DX_PI / 2.0f)) * (D3DX_PI / 180.0f);
		float pitch = sinf(t * D3DX_PI) * (D3DX_PI / 180.0f);

		D3DXQUATERNION wobble;
		D3DXQuaternionRotationYawPitchRoll(&wobble, yaw, pitch, 0.0f);
		D3DXQuaternionMultiply(&facing, &wobble, &facing);
	}

	m_actualPosition = eye;

	D3DXMatrixAffineTransformation(&g_matPosition, 1.0f, NULL, &facing, &eye);
	D3DXMatrixInverse(&g_matView, NULL, &g_matPosition);
	g_matView._31 = 0.0f - g_matView._31;
	g_matView._32 = 0.0f - g_matView._32;
	g_matView._33 = 0.0f - g_matView._33;
	g_matView._34 = 0.0f - g_matView._34;

	TheseusSetTransform(D3DTS_VIEW, &g_matView);
}

// CCamera::Set(pPosition, pOrientation, nSeconds)
// Reconstructed from 5960 retail XBE @ 0x000508e0. Ghidra decompile:
//     newPos = *p; newRot = *q; oldPos = position; oldRot = orientation;
//     start = now; duration = secs;
//     if (secs <= 0.0) { position = newPos; orientation = newRot; }
void CCamera::Set(const D3DXVECTOR3* pPosition, const D3DXQUATERNION* pOrientation, float nSeconds/*=0.0f*/)
{
	m_newPosition = *pPosition;
	m_newOrientation = *pOrientation;
	m_oldPosition = m_position;
	m_oldOrientation = m_orientation;
	m_nStartTime = TheseusGetNow();
	m_nDeltaTime = nSeconds;

	if (nSeconds > 0.0f)
		return;

	m_position = m_newPosition;
	m_orientation = m_newOrientation;
}

// CCamera::Advance(nSeconds)
// Reconstructed from 5960 retail XBE @ 0x000509a0. Ghidra decompile:
//     if (0.0 < duration) {
//         f = (now - start) / duration;
//         if (1.0 <= f) { duration = 0; f = 1.0; }
//         position = old + (new - old) * f;
//         QuaternionSlerp(&orientation, &oldRot, &newRot, f);
//     }
//     UpdateViewMatrix();
// It never calls CNode::Advance.
void CCamera::Advance(float nSeconds)
{
	if (m_nDeltaTime > 0.0f)
	{
		float f = (float)(TheseusGetNow() - m_nStartTime) / m_nDeltaTime;
		if (f >= 1.0f)
		{
			f = 1.0f;
			m_nDeltaTime = 0.0f;
		}

		D3DXVec3Lerp(&m_position, &m_oldPosition, &m_newPosition, f);
		D3DXQuaternionSlerp(&m_orientation, &m_oldOrientation, &m_newOrientation, f);
	}

	UpdateViewMatrix();
}


// ===== CCameraPath ==========================================================
// 0x38 bytes, factory 0x00050a70, vtable 0x000282f0. Field offsets:
// interval 0x1c, isActive 0x20, backward 0x21, position 0x24,
// orientation 0x28, start time 0x30 (double).

IMPLEMENT_NODE("CameraPath", CCameraPath, CNode)

START_NODE_PROPS(CCameraPath, CNode)
	NODE_PROP(pt_boolean, CCameraPath, isActive)
	NODE_PROP(pt_boolean, CCameraPath, backward)
	NODE_PROP(pt_number, CCameraPath, interval)
	NODE_PROP(pt_node, CCameraPath, position)
	NODE_PROP(pt_node, CCameraPath, orientation)
END_NODE_PROPS()

// CCameraPath::CCameraPath()
// Reconstructed from 5960 retail XBE @ 0x00050a70 (factory). Ghidra decompile:
//     interval = 1.0; isActive = 0; backward = 0; position = orientation = NULL;
CCameraPath::CCameraPath()
{
	m_interval = 1.0f;
	m_isActive = false;
	m_backward = false;
	m_position = NULL;
	m_orientation = NULL;
}

// CCameraPath::~CCameraPath()
// Reconstructed from 5960 retail XBE @ 0x00050af0. Ghidra decompile:
//     if (position) position->Release();   // vtable slot 4
//     if (orientation) orientation = NULL;
// The binary drops the orientation pointer without a Release; kept that way.
CCameraPath::~CCameraPath()
{
	if (m_position != NULL)
		m_position->Release();

	m_orientation = NULL;
}

// CCameraPath::Advance(nSeconds)
// Reconstructed from 5960 retail XBE @ 0x00050b60. Ghidra decompile:
//     CNode::Advance(secs);
//     if (!isActive) return;
//     t = (now - start) / interval;
//     if (1.0 <= t) { t = 1.0; isActive = 0; }
//     if (backward) t = 1.0 - t;
//     pos = theCamera.position; rot = theCamera.orientation;
//     if (position) { pos = position->Interpolate(t); pos.z = 0.0 - pos.z; }
//     if (orientation) rot = orientation->Interpolate(t);
//     theCamera.Set(&pos, &rot, 0);
// Paths are authored in the right handed scene, hence the z flip.
void CCameraPath::Advance(float nSeconds)
{
	CNode::Advance(nSeconds);

	if (!m_isActive)
		return;

	float t = (float)(TheseusGetNow() - m_startTime) / m_interval;
	if (t >= 1.0f)
	{
		t = 1.0f;
		m_isActive = false;
	}

	if (m_backward)
		t = 1.0f - t;

	D3DXVECTOR3 eye = theCamera.m_position;
	D3DXQUATERNION facing = theCamera.m_orientation;

	if (m_position != NULL)
	{
		eye = ((CPositionInterpolator*)m_position)->Interpolate(t);
		eye.z = 0.0f - eye.z;
	}

	if (m_orientation != NULL)
		facing = ((COrientationInterpolator*)m_orientation)->Interpolate(t);

	theCamera.Set(&eye, &facing, 0.0f);
}

// CCameraPath::OnSetProperty(pprd, pvValue)
// Reconstructed from 5960 retail XBE @ 0x00050c90. Ghidra decompile:
//     if (prd->offset == 0x20) start = now;   return 1;
// Writing isActive either way restarts the clock.
bool CCameraPath::OnSetProperty(const PRD* pprd, const void* pvValue)
{
	if (PTR2INT(pprd->pbOffset) == offsetof(m_isActive))
		m_startTime = TheseusGetNow();

	return true;
}

// CCameraPath::Activate(bBackwards)
// The binary inlines this into CLevel's arrival code (see SceneGroups.md):
//     isActive = 1; start = now; backward = bBackwards;
void CCameraPath::Activate(bool bBackwards)
{
	m_isActive = true;
	m_startTime = TheseusGetNow();
	m_backward = bBackwards;
}
