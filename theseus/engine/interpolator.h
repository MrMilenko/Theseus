// interpolator.h: VRML style key/keyValue interpolators. Interpolate(t)
// finds the two keys around t and blends their values (lerp for positions,
// slerp for orientations). Camera paths and scripts both drive them.
// Bodies in render/animation_nodes.cpp, see docs/decomp/AnimationNodes.md.

#pragma once

class CPositionInterpolator : public CNode
{
	DECLARE_NODE(CPositionInterpolator, CNode)
	DECLARE_NODE_PROPS()
public:
	CPositionInterpolator();
	~CPositionInterpolator();

	D3DXVECTOR3 Interpolate(float t);

	CNumArray m_key;
	CVec3Array m_keyValue;
};

// Key values come in as axis + angle; OnLoad turns them into quaternions.
class COrientationInterpolator : public CNode
{
	DECLARE_NODE(COrientationInterpolator, CNode)
	DECLARE_NODE_PROPS()
public:
	COrientationInterpolator();
	~COrientationInterpolator();

	void OnLoad();
	D3DXQUATERNION Interpolate(float t);

	CNumArray m_key;
	CVec4Array m_keyValue;
};
