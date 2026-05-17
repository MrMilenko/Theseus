// hud_node.cpp: CHud overlay node. Always-visible 2D layer above the
// scene graph (mute toast, system messages, transient overlays).
// Decompiled from the 5960 retail XBE.

#include "std.h"
#include "theseus.h"
#include "lerper.h"
#include "node.h"
#include "asset_loader.h"
#include "lerper.h"

extern D3DXMATRIX g_matIdentity;

class CHud : public CGroup
{
	DECLARE_NODE(CHud, CGroup)
public:
	CHud();
	~CHud();

	void Render();

	DECLARE_NODE_PROPS()
};

IMPLEMENT_NODE("HUD", CHud, CGroup)

START_NODE_PROPS(CHud, CGroup)
END_NODE_PROPS()

CHud::CHud()
{
}

CHud::~CHud()
{
}

extern D3DXMATRIX g_matView;
extern D3DXMATRIX g_matProjection;

void CHud::Render()
{
	D3DXMATRIX matProjection, matProjectionSave, matViewSave;
	D3DXMatrixOrthoLH(&matProjection, TheseusGetViewWidth(), TheseusGetViewHeight(), -10000.0f, 10000.0f);

	matProjectionSave = g_matProjection;
	g_matProjection = matProjection;
	TheseusSetTransform(D3DTS_PROJECTION, &g_matProjection);

	matViewSave = g_matView;
	g_matView = g_matIdentity;
	TheseusSetTransform(D3DTS_VIEW, &g_matView);

	DWORD dwLighting;
	TheseusGetRenderState(D3DRS_LIGHTING, &dwLighting);
	TheseusSetRenderState(D3DRS_LIGHTING, FALSE);

	TheseusPushWorld();
	TheseusIdentityWorld();
	TheseusUpdateWorld();

#ifdef _XBOX
	// Disable Edge Antialiasing. Xbox NV2A hardware feature; shim no-op
	// on desktop GL.
	TheseusSetRenderState(D3DRS_EDGEANTIALIAS, FALSE);
	TheseusSetRenderState(D3DRS_MULTISAMPLEANTIALIAS, TRUE);
#endif

	CGroup::Render();

	TheseusPopWorld();

	TheseusSetRenderState(D3DRS_LIGHTING, dwLighting);

	g_matProjection = matProjectionSave;
	TheseusSetTransform(D3DTS_PROJECTION, &g_matProjection);
	g_matView = matViewSave;
	TheseusSetTransform(D3DTS_VIEW, &g_matView);
}



class CPicture : public CNode
{
	DECLARE_NODE(CPicture, CNode)
public:
	CPicture();
	~CPicture();

	float m_left;
	float m_top;
	float m_width;
	float m_height;
	CNode *m_texture;
	float m_transparency;
	float m_fade;
	D3DXVECTOR3 m_color;

	void OnLoad();
	void Render();
	void Advance(float nSeconds);
	bool OnSetProperty(const PRD *pprd, const void *pvValue);

	float m_alpha;
	//	float m_startAlpha;
	//	float m_endAlpha;
	XTIME m_startFadeTime;

	float m_startLeft;
	float m_endLeft;
	float m_startTop;
	float m_endTop;
	float m_startWidth;
	float m_endWidth;
	float m_startHeight;
	float m_endHeight;

	LPDIRECT3DVERTEXBUFFER8 m_pVB;

	DECLARE_NODE_PROPS()
};

IMPLEMENT_NODE("Picture", CPicture, CNode)

START_NODE_PROPS(CPicture, CNode)
NODE_PROP(pt_number, CPicture, top)
NODE_PROP(pt_number, CPicture, left)
NODE_PROP(pt_number, CPicture, width)
NODE_PROP(pt_number, CPicture, height)
NODE_PROP(pt_node, CPicture, texture)
NODE_PROP(pt_number, CPicture, transparency)
NODE_PROP(pt_number, CPicture, fade)
NODE_PROP(pt_color, CPicture, color)
END_NODE_PROPS()

CPicture::CPicture() : m_texture(NULL),
					   m_left(0.0f),
					   m_top(0.0f),
					   m_width(-1.0f),
					   m_height(-1.0f),
					   m_transparency(0.0f),
					   m_fade(0.0f),
					   m_color(1.0f, 1.0f, 1.0f)
{
	m_alpha = 1.0f;
	//	m_startAlpha = 1.0f;
	//	m_endAlpha = 1.0f;
	m_startFadeTime = 0.0f;

	m_pVB = NULL;
}

CPicture::~CPicture()
{
	if (m_pVB != NULL)
		m_pVB->Release();

	if (m_texture != NULL)
		m_texture->Release();
}

typedef struct _LITVERTEX
{
	union
	{
		D3DVALUE x; /* Homogeneous coordinates */
		D3DVALUE dvX;
	};
	union
	{
		D3DVALUE y;
		D3DVALUE dvY;
	};
	union
	{
		D3DVALUE z;
		D3DVALUE dvZ;
	};
	union
	{
		D3DVALUE nx; /* Normal */
		D3DVALUE dvNX;
	};
	union
	{
		D3DVALUE ny;
		D3DVALUE dvNY;
	};
	union
	{
		D3DVALUE nz;
		D3DVALUE dvNZ;
	};
	union
	{
		D3DCOLOR color; /* Vertex color */
		D3DCOLOR dcColor;
	};
	union
	{
		D3DVALUE tu; /* Texture coordinates */
		D3DVALUE dvTU;
	};
	union
	{
		D3DVALUE tv;
		D3DVALUE dvTV;
	};
#if (DIRECT3D_VERSION >= 0x0500)
#if (defined __cplusplus) && (defined D3D_OVERLOADS)
	_LITVERTEX()
	{
	}
	_LITVERTEX(const D3DVECTOR &v, const D3DVECTOR &n, float _tu, float _tv)
	{
		x = v.x;
		y = v.y;
		z = v.z;
		nx = n.x;
		ny = n.y;
		nz = n.z;
		tu = _tu;
		tv = _tv;
	}
#endif
#endif /* DIRECT3D_VERSION >= 0x0500 */
} LITVERTEX, *LPLITVERTEX;

void CPicture::Render()
{
	if (m_alpha <= 0.0f)
		return;

	//	if (m_texture == NULL)
	//		return;
	LPDIRECT3DTEXTURE8 pTexture = NULL;
	if (m_texture != NULL)
	{
		pTexture = m_texture->GetTextureSurface();
		if (pTexture == NULL)
			return;
	}

	if (m_pVB == NULL)
	{
		VERIFYHR(TheseusGetD3DDev()->CreateVertexBuffer(4 * sizeof(LITVERTEX), D3DUSAGE_WRITEONLY, D3DFVF_VERTEX | D3DFVF_DIFFUSE, D3DPOOL_MANAGED, &m_pVB));
	}

	float nWidth = m_width;
	float nHeight = m_height;

	if (nWidth < 0.0f || nHeight < 0.0f)
	{
		if (pTexture == NULL)
			return;

		float nTexWidth, nTexHeight;
		TheseusGetTextureSize(pTexture, nTexWidth, nTexHeight);

		if (nWidth < 0.0f)
			nWidth = nTexWidth;
		if (nHeight < 0.0f)
			nHeight = nTexHeight;

		if (m_texture->IsKindOf(NODE_CLASS(CTexture)))
		{
			CTexture *pTex = (CTexture *)m_texture;
			nWidth *= pTex->m_nImageWidth / nWidth;
			nHeight *= pTex->m_nImageHeight / nHeight;
		}
	}

	float tu = 1.0f;
	float tv = 1.0f;

	if (m_texture != NULL && m_texture->IsKindOf(NODE_CLASS(CTexture)))
	{
		// The texture image may have been stretched during creation on order to fill the texture surface.
		// Here we calculate a correction factor to the image looks right...

		float width, height;
		TheseusGetTextureSize(pTexture, width, height);

		CTexture *pTex = (CTexture *)m_texture;

		tu = pTex->m_nImageWidth / width;
		tv = pTex->m_nImageHeight / height;
	}

	TheseusSetTexture(0, pTexture);
	LITVERTEX verts[4];

	verts[0].dvX = m_left + nWidth;
	verts[0].dvY = m_top + nHeight;
	verts[0].dvTU = tu;
	verts[0].dvTV = tv;
	verts[0].dcColor = D3DCOLOR_RGBA(255, 255, 255, 255);

	verts[1].dvX = m_left;
	verts[1].dvY = m_top + nHeight;
	verts[1].dvTU = 0.0f;
	verts[1].dvTV = tv;
	verts[1].dcColor = D3DCOLOR_RGBA(255, 255, 255, 255);

	verts[2].dvX = m_left + nWidth;
	verts[2].dvY = m_top;
	verts[2].dvTU = tu;
	verts[2].dvTV = 0.0f;
	verts[2].dcColor = D3DCOLOR_RGBA(255, 255, 255, 255);

	verts[3].dvX = m_left;
	verts[3].dvY = m_top;
	verts[3].dvTU = 0.0f;
	verts[3].dvTV = 0.0f;
	verts[3].dcColor = D3DCOLOR_RGBA(255, 255, 255, 255);

	for (int i = 0; i < 4; i += 1)
	{
		verts[i].dvX -= TheseusGetViewWidth() / 2.0f;
		verts[i].dvY -= TheseusGetViewHeight() / 2.0f;
		verts[i].dvZ = 0.0f;
		verts[i].dvY = -verts[i].dvY;

		verts[i].dvNX = verts[i].dvX;
		verts[i].dvNY = verts[i].dvY;
		verts[i].dvNZ = 1.0f;
	}

	TheseusSetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	TheseusSetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	TheseusSetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

	TheseusSetRenderState(D3DRS_TEXTUREFACTOR, D3DCOLOR_COLORVALUE(m_color.x, m_color.y, m_color.z, m_alpha));

	if (pTexture != NULL)
	{
		TheseusSetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
		TheseusSetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		TheseusSetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_TFACTOR);

		TheseusSetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
		TheseusSetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		TheseusSetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);

		TheseusSetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_POINT);
		TheseusSetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_POINT);
	}
	else
	{
		TheseusSetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
		TheseusSetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TFACTOR);

		TheseusSetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
		TheseusSetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TFACTOR);
	}

#ifdef _XBOX
	// Disable Edge Antialiasing. Xbox NV2A hardware feature; shim no-op
	// on desktop GL.
	TheseusSetRenderState(D3DRS_EDGEANTIALIAS, FALSE);
	TheseusSetRenderState(D3DRS_MULTISAMPLEANTIALIAS, TRUE);
#endif

	D3DVERTEX *v;
	VERIFYHR(m_pVB->Lock(0, 4 * sizeof(LITVERTEX), (BYTE **)&v, 0));
	CopyMemory(v, verts, 4 * sizeof(LITVERTEX));
	VERIFYHR(m_pVB->Unlock());

	VERIFYHR(TheseusGetD3DDev()->SetStreamSource(0, m_pVB, sizeof(LITVERTEX)));
	VERIFYHR(TheseusGetD3DDev()->SetVertexShader(D3DFVF_VERTEX | D3DFVF_DIFFUSE));
	VERIFYHR(TheseusGetD3DDev()->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2));

	TheseusSetTexture(0, NULL);
	TheseusSetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);

	TheseusSetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
	TheseusSetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
}

void CPicture::Advance(float nSeconds)
{
	CNode::Advance(nSeconds);

	if (m_texture != NULL)
		m_texture->Advance(nSeconds);
	if (m_fade != 0.0f && m_startFadeTime != 0.0f)
	{
		float t = (float)(TheseusGetNow() - m_startFadeTime) / m_fade;

		if (t > 1.0f)
		{
			t = 1.0f;
			m_startFadeTime = 0.0f;
		}

		float t1 = 1.0f - t;

		//		m_alpha = t1 * m_startAlpha + t * m_endAlpha;

		m_left = t1 * m_startLeft + t * m_endLeft;
		m_top = t1 * m_startTop + t * m_endTop;
		m_width = t1 * m_startWidth + t * m_endWidth;
		m_height = t1 * m_startHeight + t * m_endHeight;

		//		TRACE("start %f end %f t %f a %f\n", m_startAlpha, m_endAlpha, t, m_alpha);
	}
}

void CPicture::OnLoad()
{
	m_alpha = 1.0f - m_transparency;

	m_startLeft = m_endLeft = m_left;
	m_startTop = m_endTop = m_top;
	m_startWidth = m_endWidth = m_width;
	m_startHeight = m_endHeight = m_height;

	CNode::OnLoad();
}

bool CPicture::OnSetProperty(const PRD *pprd, const void *pvValue)
{
	if (PTR2INT(pprd->pbOffset) == offsetof(m_transparency))
	{
		float value = *(float *)pvValue;

		//		TRACE("Set transparency to %f\n", value);
		value = 1.0f - value;

		if (m_fade == 0.0f)
			m_alpha = value;
		else
			new CLerper(this, &m_alpha, value, m_fade);
		/*
				m_endAlpha = 1.0f - value;

				if (m_fade == 0.0f)
				{
					m_startAlpha = m_endAlpha;
					m_alpha = m_endAlpha;
					m_startFadeTime = 0.0f;
		//			TRACE("\timmediate; alpha = %f\n", m_alpha);
				}
				else
				{
					m_startAlpha = m_alpha;
					m_startFadeTime = TheseusGetNow();
		//			TRACE("\tfade over %f seconds; alpha = %f\n", m_fade, m_alpha);
				}
		*/
	}
	else if (PTR2INT(pprd->pbOffset) == offsetof(m_fade))
	{
		float value = *(float *)pvValue;
		//		TRACE("Set fade to %f\n", value);

		if (value == 0.0f)
		{
			//			m_alpha = m_endAlpha;
			m_startFadeTime = 0.0f;
			//			TRACE("\tadjusting alpha to %f\n", m_alpha);
		}

		CLerper::RemoveObject(this);
	}
	else if (PTR2INT(pprd->pbOffset) == offsetof(m_left))
	{
		float value = *(float *)pvValue;

		m_endLeft = value;
		m_startLeft = m_left;
		if (m_fade != 0.0f)
		{
			m_startFadeTime = TheseusGetNow();
			return false;
		}
	}
	else if (PTR2INT(pprd->pbOffset) == offsetof(m_top))
	{
		float value = *(float *)pvValue;
		m_endTop = value;
		m_startTop = m_top;
		if (m_fade != 0.0f)
		{
			m_startFadeTime = TheseusGetNow();
			return false;
		}
	}
	else if (PTR2INT(pprd->pbOffset) == offsetof(m_width))
	{
		float value = *(float *)pvValue;
		m_endWidth = value;
		m_startWidth = m_width;
		if (m_fade != 0.0f)
		{
			m_startFadeTime = TheseusGetNow();
			return false;
		}
	}
	else if (PTR2INT(pprd->pbOffset) == offsetof(m_height))
	{
		float value = *(float *)pvValue;
		m_endHeight = value;
		m_startHeight = m_height;
		if (m_fade != 0.0f)
		{
			m_startFadeTime = TheseusGetNow();
			return false;
		}
	}

	return true;
}


