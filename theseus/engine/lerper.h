// lerper.h: CLerper, a linear interpolator that drives a single float
// value toward a target over a fixed time interval.
//
// Each instance lives in a global singly-linked list (c_pHead / m_next);
// CLerper::AdvanceAll() is called once per frame to step every active
// interpolation and drop ones that have hit their end value. The owning
// CObject is held only as a back-reference for cancellation via
// RemoveObject; the lerper itself does not retain a CObject reference
// count.

#pragma once
class CLerper
{
public:
	CLerper(CObject* pObject, float* pValue, float nNewValue, float nInterval);

	float m_interval;
	XTIME m_startTime;
	float m_startValue;
	float m_endValue;
	float* m_value;     // points at the float being driven
	CObject* m_owner;   // back-reference, not refcounted

	bool Advance();

	static void RemoveObject(CObject* pObject);
	static void AdvanceAll();
	static CLerper* c_pHead;
	CLerper* m_next;
};
