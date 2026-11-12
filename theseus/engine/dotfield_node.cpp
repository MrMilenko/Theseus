// dotfield_node.cpp: CDotField, a drifting grid of dots drawn into a dynamic
// texture. Rebuilt from the 5960 retail XBE (0x606d0..0x60930); findings are
// in docs/decomp/TmapSystem.md.

#include "std.h"
#include "theseus.h"
#include "node.h"
#include "tmap_system.h"


// ===== CDotField ============================================================
// 0x50 bytes, factory 0x000606d0, vtable 0x0002a400. Field offsets:
// spacing 0x1c, hSpeed 0x20, vSpeed 0x24, offset 0x28/0x2c, velocity
// 0x30/0x34, target and previous spacing 0x38/0x3c, next change 0x40 and
// last change 0x48 (doubles).

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

// CDotField::CDotField()
// Reconstructed from 5960 retail XBE @ 0x000606d0 (factory). Ghidra decompile:
//     spacing = 8.0; hSpeed = vSpeed = 0; offsets = 0; velocities = 0;
//     nextChange = lastChange = 0.0;
// The binary leaves the two spacing slots unset; Theseus zeroes them.
CDotField::CDotField()
{
	m_spacing = 8.0f;
	m_hSpeed = 0.0f;
	m_vSpeed = 0.0f;
	m_xOff = 0.0f;
	m_yOff = 0.0f;
	m_xVel = 0.0f;
	m_yVel = 0.0f;
	m_targetSpacing = 0.0f;
	m_prevSpacing = 0.0f;
	m_nextChange = 0.0;
	m_lastChange = 0.0;
}

CDotField::~CDotField()
{
}

// CDotField::Advance(nSeconds)
// Reconstructed from 5960 retail XBE @ 0x00060790. Ghidra decompile:
//     CNode::Advance(secs);
//     if (hSpeed == 0 && vSpeed == 0) {
//         if (nextChange <= now) {
//             xVel = (rand() * (2.0/32767) - 1.0) * spacing * 4.0;   // yVel alike
//             target = rand() * (24.0/32767) + 8.0; prev = spacing;
//             lastChange = now; nextChange = rand() * (5.0/32767) + now + 10.0;
//         }
//     } else { xVel = hSpeed; yVel = vSpeed; }
//     if (0.0 < lastChange) {
//         r = now - lastChange;
//         if (1.0 <= r) lastChange = 0.0; else spacing = (target - prev) * r + prev;
//     }
//     xOff += (xVel / spacing) * secs; xOff -= floor(xOff);   // yOff alike
// With no speeds set it wanders: a new heading and grid size every 10 to 15
// seconds, easing into the new size over one second.
void CDotField::Advance(float nSeconds)
{
	CNode::Advance(nSeconds);

	XTIME now = TheseusGetNow();

	if (m_hSpeed != 0.0f || m_vSpeed != 0.0f)
	{
		m_xVel = m_hSpeed;
		m_yVel = m_vSpeed;
	}
	else if (now >= m_nextChange)
	{
		m_xVel = (rnd(2.0f) - 1.0f) * m_spacing * 4.0f;
		m_yVel = (rnd(2.0f) - 1.0f) * m_spacing * 4.0f;
		m_targetSpacing = rnd(24.0f) + 8.0f;
		m_prevSpacing = m_spacing;
		m_lastChange = now;
		m_nextChange = rnd(5.0f) + now + 10.0;
	}

	if (m_lastChange > 0.0)
	{
		float r = (float)(now - m_lastChange);
		if (r >= 1.0f)
			m_lastChange = 0.0;
		else
			m_spacing = (m_targetSpacing - m_prevSpacing) * r + m_prevSpacing;
	}

	m_xOff = wrap((m_xVel / m_spacing) * nSeconds + m_xOff);
	m_yOff = wrap((m_yVel / m_spacing) * nSeconds + m_yOff);
}

// CDotField::RenderDynamicTexture(pSurfx)
// Reconstructed from 5960 retail XBE @ 0x00060930. Ghidra decompile:
//     step = (int)spacing;
//     for (y = (int)(yOff * spacing); y < height; y += step)
//         for (x = (int)(xOff * spacing); x < width; x += step)
//             pels[y * width + x] = 0xff;
// Theseus keeps the step at one pixel or more so a tiny spacing can't hang.
void CDotField::RenderDynamicTexture(CSurfx* pSurfx)
{
	int step = (int)m_spacing;
	if (step < 1)
		step = 1;

	int width = pSurfx->m_nWidth;
	int x0 = (int)(m_xOff * m_spacing);

	for (int y = (int)(m_yOff * m_spacing); y < pSurfx->m_nHeight; y += step)
	{
		for (int x = x0; x < width; x += step)
			pSurfx->m_pels[y * width + x] = 0xff;
	}
}
