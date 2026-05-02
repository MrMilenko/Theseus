// dotfield_node.cpp: CDotField, the scrolling dot-grid particle effect
// fed into the dynamic texture pipeline. Decompiled from the 5960 retail
// XBE; see docs/decomp/TmapSystem.md for the surrounding architecture.

#include "std.h"
#include "theseus.h"
#include "node.h"
#include "tmap_system.h"

class CDotField : public CNode
{
	DECLARE_NODE(CDotField, CNode)
public:
	CDotField();
	~CDotField();

	float m_spacing;
	float m_hSpeed;
	float m_vSpeed;

	void Advance(float nSeconds);
	void RenderDynamicTexture(CSurfx* pSurfx);

protected:
	float m_xOff, m_yOff;
	float m_xVel, m_yVel;
	float m_targetSpacing, m_prevSpacing;
	XTIME m_nextChange, m_lastChange;

	DECLARE_NODE_PROPS()
};

IMPLEMENT_NODE("DotField", CDotField, CNode)

START_NODE_PROPS(CDotField, CNode)
	NODE_PROP(pt_number, CDotField, spacing)
	NODE_PROP(pt_number, CDotField, hSpeed)
	NODE_PROP(pt_number, CDotField, vSpeed)
END_NODE_PROPS()

CDotField::CDotField()
	: m_spacing(8.0f), m_hSpeed(0.0f), m_vSpeed(0.0f),
	  m_xOff(0.0f), m_yOff(0.0f), m_xVel(0.0f), m_yVel(0.0f),
	  m_targetSpacing(0.0f), m_prevSpacing(0.0f),
	  m_nextChange(0.0f), m_lastChange(0.0f)
{
}

CDotField::~CDotField() {}

void CDotField::Advance(float nSeconds)
{
	CNode::Advance(nSeconds);

	if (m_hSpeed == 0.0f && m_vSpeed == 0.0f)
	{
		// Random drift mode
		if (TheseusGetNow() >= m_nextChange)
		{
			m_xVel = (rnd(2.0f) - 1.0f) * m_spacing * 4.0f;
			m_yVel = (rnd(2.0f) - 1.0f) * m_spacing * 4.0f;
			m_targetSpacing = 8.0f + rnd(24.0f);
			m_prevSpacing = m_spacing;
			m_lastChange = TheseusGetNow();
			m_nextChange = TheseusGetNow() + 10.0f + rnd(5.0f);
		}
	}
	else
	{
		m_xVel = m_hSpeed;
		m_yVel = m_vSpeed;
	}

	// Smooth spacing transition
	if (m_lastChange > 0.0f)
	{
		float r = (float)(TheseusGetNow() - m_lastChange);
		if (r < 1.0f)
			m_spacing = m_prevSpacing + (m_targetSpacing - m_prevSpacing) * r;
		else
			m_lastChange = 0.0f;
	}

	m_xOff = wrap(m_xOff + m_xVel * nSeconds / m_spacing);
	m_yOff = wrap(m_yOff + m_yVel * nSeconds / m_spacing);
}

void CDotField::RenderDynamicTexture(CSurfx* pSurfx)
{
	int step = (int)m_spacing;
	if (step < 1) step = 1;

	for (int y = (int)(m_yOff * m_spacing); y < pSurfx->m_nHeight; y += step)
		for (int x = (int)(m_xOff * m_spacing); x < pSurfx->m_nWidth; x += step)
			pSurfx->m_pels[x + y * pSurfx->m_nWidth] = 255;
}
