// interpolator.h: keyframe-driven interpolators following VRML97 semantics.
//
// Each instance stores a parallel pair of arrays: m_key holds the parameter
// values (typically 0..1) at which a sample is defined, and m_keyValue
// holds the sampled output. Interpolate(t) finds the bracketing key pair
// and returns a linear blend (vec3) or shortest-arc slerp (quaternion) of
// the values. Both classes are scene-graph nodes so they can be DEF'd in
// .xap content and wired up as the value source for a TimeSensor->Transform
// animation.

#pragma once

class CPositionInterpolator : public CNode
{
	DECLARE_NODE(CPositionInterpolator, CNode)
public:
	CPositionInterpolator();
	~CPositionInterpolator();

	D3DXVECTOR3 Interpolate(float key);

	CNumArray m_key;
	CVec3Array m_keyValue;

	DECLARE_NODE_PROPS()
};

class COrientationInterpolator : public CNode
{
	DECLARE_NODE(COrientationInterpolator, CNode)
public:
	COrientationInterpolator();
	~COrientationInterpolator();

	void OnLoad();
	D3DXQUATERNION Interpolate(float key);

	CNumArray m_key;
	CVec4Array m_keyValue;

	DECLARE_NODE_PROPS()
};
