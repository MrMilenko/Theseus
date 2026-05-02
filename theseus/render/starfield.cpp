// starfield.cpp: CStarField, the parallax 3D star backdrop used by some
// dashboard scenes. Theseus-original code (no retail counterpart).

#include "std.h"
#include "theseus.h"
#include "node.h"
#include "tmap_system.h"


class CStarField : public CNode
{
	DECLARE_NODE(CStarField, CNode)
public:
	CStarField();
	~CStarField();

	int m_stars;
	float m_speed;

	void Advance(float nSeconds);
	void RenderDynamicTexture(CSurfx* pSurfx);

protected:
	int m_nAllocStars;
	D3DXVECTOR3* m_rgstar;
	void Update();

	DECLARE_NODE_PROPS()
};

IMPLEMENT_NODE("StarField", CStarField, CNode)

START_NODE_PROPS(CStarField, CNode)
	NODE_PROP(pt_integer, CStarField, stars)
	NODE_PROP(pt_number, CStarField, speed)
END_NODE_PROPS()

CStarField::CStarField() :
	m_stars(20),
	m_speed(1.0f)
{
	m_rgstar = NULL;
	m_nAllocStars = 0;
}

CStarField::~CStarField()
{
	delete [] m_rgstar;
}

void CStarField::Advance(float nSeconds)
{
	CNode::Advance(nSeconds);

	Update();

	int i;
	for (i = 0; i < m_stars; i += 1)
	{
		m_rgstar[i].z -= nSeconds * m_speed;

		if (m_rgstar[i].z <= 0.0f)
		{
			m_rgstar[i].x = rnd(2.0f) - 1.0f;
			m_rgstar[i].y = rnd(2.0f) - 1.0f;
			m_rgstar[i].z = 1.5f + rnd(1.0f);
		}
	}
}

void CStarField::RenderDynamicTexture(CSurfx* pSurfx)
{
	Update();

	float xCenter = (float)pSurfx->m_nWidth / 2.0f;
	float yCenter = (float)pSurfx->m_nHeight / 2.0f;

	for (int i = 0; i < m_stars; i += 1)
	{
		if (m_rgstar[i].z > 0.0f)
		{
			int x = (int)(xCenter + m_rgstar[i].x / m_rgstar[i].z * xCenter);
			int y = (int)(yCenter + m_rgstar[i].y / m_rgstar[i].z * yCenter);

			if (x >= 0 && x < pSurfx->m_nWidth && y >= 0 && y < pSurfx->m_nHeight)
				pSurfx->m_pels[x + y * pSurfx->m_nWidth] = (int)(255.0f * (1.0f - (m_rgstar[i].z - 1.0f)));
			else { /*TRACE("Star error!\n");*/ m_rgstar[i].z = 0.0f; }
		}
	}
}

void CStarField::Update()
{
	if (m_rgstar == NULL || m_stars > m_nAllocStars)
	{
		D3DXVECTOR3* rgstars = new D3DXVECTOR3 [m_stars];
		ZeroMemory(rgstars, m_stars * sizeof (D3DXVECTOR3));

		if (m_rgstar != NULL)
		{
			CopyMemory(rgstars, m_rgstar, m_nAllocStars * sizeof (D3DXVECTOR3));
			delete [] m_rgstar;
		}

		m_rgstar = rgstars;
		m_nAllocStars = m_stars;
	}
}
