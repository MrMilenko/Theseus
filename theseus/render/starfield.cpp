// starfield.cpp: CStarField, the flying star dots a dynamic texture draws
// behind the music visualizer. Rebuilt from the 5960 retail XBE
// (0x60390..0x60640); findings are in docs/decomp/TmapSystem.md.

#include "std.h"
#include "theseus.h"
#include "node.h"
#include "tmap_system.h"


// ===== CStarField ===========================================================
// 0x2c bytes, factory 0x00060390, vtable 0x0002a368. Field offsets:
// stars 0x1c, speed 0x20, allocated count 0x24, star array 0x28 (x, y, z).

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

// CStarField::CStarField()
// Reconstructed from 5960 retail XBE @ 0x00060390 (factory). Ghidra decompile:
//     stars = 20; speed = 1.0; allocated = 0; array = NULL;
CStarField::CStarField()
{
	m_stars = 20;
	m_speed = 1.0f;
	m_nAllocStars = 0;
	m_rgstar = NULL;
}

// CStarField::~CStarField()
// Reconstructed from 5960 retail XBE @ 0x00060410: frees the array, then CNode.
CStarField::~CStarField()
{
	delete [] m_rgstar;
}

// CStarField::Update()
// Reconstructed from 5960 retail XBE @ 0x00060640. Ghidra decompile:
//     if (array == NULL || allocated < stars) {
//         fresh = new vec3[stars]; zero it;
//         if (array) { copy allocated stars over; free(array); }
//         array = fresh; allocated = stars;
//     }
// New stars start at z = 0, so the next Advance scatters them.
void CStarField::Update()
{
	if (m_rgstar != NULL && m_stars <= m_nAllocStars)
		return;

	D3DXVECTOR3* grown = new D3DXVECTOR3 [m_stars];
	memset(grown, 0, m_stars * sizeof (D3DXVECTOR3));

	if (m_rgstar != NULL)
	{
		memcpy(grown, m_rgstar, m_nAllocStars * sizeof (D3DXVECTOR3));
		delete [] m_rgstar;
	}

	m_rgstar = grown;
	m_nAllocStars = m_stars;
}

// CStarField::Advance(nSeconds)
// Reconstructed from 5960 retail XBE @ 0x00060430. Ghidra decompile:
//     CNode::Advance(secs); Update();
//     for (i = 0; i < stars; i++) {
//         s[i].z -= speed * secs;
//         if (s[i].z <= 0.0) {
//             s[i].x = rand() * (2.0/32767) - 1.0;
//             s[i].y = rand() * (2.0/32767) - 1.0;
//             s[i].z = rand() * (1.0/32767) + 1.5;
//         }
//     }
// A star that reaches the viewer respawns somewhere 1.5 to 2.5 deep.
void CStarField::Advance(float nSeconds)
{
	CNode::Advance(nSeconds);
	Update();

	for (int i = 0; i < m_stars; i++)
	{
		D3DXVECTOR3& star = m_rgstar[i];
		star.z -= m_speed * nSeconds;
		if (star.z > 0.0f)
			continue;

		star.x = rnd(2.0f) - 1.0f;
		star.y = rnd(2.0f) - 1.0f;
		star.z = rnd(1.0f) + 1.5f;
	}
}

// CStarField::RenderDynamicTexture(pSurfx)
// Reconstructed from 5960 retail XBE @ 0x00060510. Ghidra decompile:
//     Update();
//     halfW = width * 0.5; halfH = height * 0.5;
//     for (i = 0; i < stars; i++) {
//         if (!(0.0 < s[i].z)) continue;
//         inv = 1.0 / s[i].z;
//         x = (int)((s[i].x * inv + 1.0) * halfW);
//         y = (int)((s[i].y * inv + 1.0) * halfH);
//         if (x < 0 || x >= width || y < 0 || y >= height) s[i].z = 0;
//         else pels[y * width + x] = (int)((1.0 - (s[i].z - 1.0)) * 255.0);
//     }
// Perspective divide by depth; nearer stars are brighter. One that leaves
// the frame is parked at z = 0 so Advance respawns it.
void CStarField::RenderDynamicTexture(CSurfx* pSurfx)
{
	Update();

	int width = pSurfx->m_nWidth;
	int height = pSurfx->m_nHeight;
	float halfW = (float)width * 0.5f;
	float halfH = (float)height * 0.5f;

	for (int i = 0; i < m_stars; i++)
	{
		D3DXVECTOR3& star = m_rgstar[i];
		if (!(star.z > 0.0f))
			continue;

		float inv = 1.0f / star.z;
		int x = (int)((star.x * inv + 1.0f) * halfW);
		int y = (int)((star.y * inv + 1.0f) * halfH);

		if (x < 0 || x >= width || y < 0 || y >= height)
		{
			star.z = 0.0f;
			continue;
		}

		pSurfx->m_pels[y * width + x] = (BYTE)(int)((1.0f - (star.z - 1.0f)) * 255.0f);
	}
}
