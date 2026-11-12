// dotfield_node.cpp: the DotField node, a grid of dots drawn into a dynamic
// texture that drifts and changes spacing on its own, or scrolls at a set
// speed. Rebuilt from the 5960 retail XBE (0x606d0..0x60930).

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

	void Advance(float nSeconds);
	void RenderDynamicTexture(CSurfx* pSurfx);

	// Props, PRD order.
	float m_spacing;
	float m_hSpeed;
	float m_vSpeed;

protected:
	float m_scrollX, m_scrollY;         // grid offset in spacings, 0..1
	float m_driftX, m_driftY;           // pixels a second
	float m_spacingTo, m_spacingFrom;   // the one second spacing ease
	XTIME m_nextDrift, m_easeStart;

	DECLARE_NODE_PROPS()
};

IMPLEMENT_NODE("DotField", CDotField, CNode)

START_NODE_PROPS(CDotField, CNode)
	NODE_PROP(pt_number, CDotField, spacing)
	NODE_PROP(pt_number, CDotField, hSpeed)
	NODE_PROP(pt_number, CDotField, vSpeed)
END_NODE_PROPS()

// CDotField::CDotField()
// Reconstructed from 5960 retail XBE @ 0x000606d0: spacing 8, everything
// else zero.
CDotField::CDotField()
{
	m_spacing = 8.0f;
	m_hSpeed = 0.0f;
	m_vSpeed = 0.0f;
	m_scrollX = 0.0f;
	m_scrollY = 0.0f;
	m_driftX = 0.0f;
	m_driftY = 0.0f;
	m_spacingTo = 0.0f;
	m_spacingFrom = 0.0f;
	m_nextDrift = 0.0f;
	m_easeStart = 0.0f;
}

CDotField::~CDotField()
{
}

// CDotField::Advance(dt)
// Reconstructed from 5960 retail XBE @ 0x00060790. With no set speed, every
// 10 to 15 seconds it picks a drift of up to 4 spacings a second each way
// and a new spacing of 8 to 32, which it eases to over one second. A set
// hSpeed/vSpeed scrolls at that rate instead. The offsets advance in
// spacing units and wrap to 0..1.
void CDotField::Advance(float nSeconds)
{
	CNode::Advance(nSeconds);

	XTIME now = TheseusGetNow();
	if (m_hSpeed != 0.0f || m_vSpeed != 0.0f)
	{
		m_driftX = m_hSpeed;
		m_driftY = m_vSpeed;
	}
	else if (now >= m_nextDrift)
	{
		m_driftX = (rnd(2.0f) - 1.0f) * m_spacing * 4.0f;
		m_driftY = (rnd(2.0f) - 1.0f) * m_spacing * 4.0f;
		m_spacingTo = 8.0f + rnd(24.0f);
		m_spacingFrom = m_spacing;
		m_easeStart = now;
		m_nextDrift = now + 10.0f + rnd(5.0f);
	}

	if (m_easeStart > 0.0f)
	{
		float t = (float)(now - m_easeStart);
		if (t >= 1.0f)
			m_easeStart = 0.0f;
		else
			m_spacing = m_spacingFrom + (m_spacingTo - m_spacingFrom) * t;
	}

	m_scrollX = wrap(m_scrollX + m_driftX * nSeconds / m_spacing);
	m_scrollY = wrap(m_scrollY + m_driftY * nSeconds / m_spacing);
}

// CDotField::RenderDynamicTexture(surface)
// Reconstructed from 5960 retail XBE @ 0x00060930: sets a 255 pixel every
// (int)spacing pixels across and down, starting at offset * spacing.
// Theseus never steps by less than one pixel.
void CDotField::RenderDynamicTexture(CSurfx* pSurfx)
{
	int step = (int)m_spacing;
	if (step < 1)
		step = 1;
	int width = pSurfx->m_nWidth;
	for (int y = (int)(m_scrollY * m_spacing); y < pSurfx->m_nHeight; y += step)
	{
		for (int x = (int)(m_scrollX * m_spacing); x < width; x += step)
			pSurfx->m_pels[y * width + x] = 255;
	}
}
