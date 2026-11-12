// hud_node.cpp: the 2D overlay nodes. HUD flattens its children onto the
// screen, Picture draws one textured (or flat colored) quad in pixel space.
// Both rebuilt from the 5960 retail XBE (0x52ae0..0x53760); findings are in
// docs/decomp/HudNode.md.

#include "std.h"
#include "theseus.h"
#include "lerper.h"
#include "node.h"
#include "asset_loader.h"

extern D3DXMATRIX g_matIdentity;
extern D3DXMATRIX g_matView;
extern D3DXMATRIX g_matProjection;


// ===== CHud =================================================================
// Class object 0x00188148, 0x44 bytes, factory 0x00052ae0, vtable 0x00028870.

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

// CHud::CHud() / ~CHud()
// Reconstructed from 5960 retail XBE @ 0x00052ae0 (factory) and 0x00052b80:
// the factory is the CGroup constructor inlined plus the HUD vtable, and the
// destructor only resets the vtable before CGroup's. Nothing of its own.
CHud::CHud()
{
}

CHud::~CHud()
{
}

// CHud::Render()
// Reconstructed from 5960 retail XBE @ 0x00052b90. Ghidra decompile:
//     D3DXMatrixOrthoLH(&ortho, 720.0, 480.0, -10000.0, 10000.0);
//     savedProj = g_matProjection; g_matProjection = ortho; SetTransform(PROJ);
//     savedView = g_matView; g_matView = g_matIdentity; SetTransform(VIEW);
//     lighting = rs[LIGHTING]; rs[LIGHTING] = 0;
//     worldStack->Push(); worldStack->LoadIdentity(); SetTransform(WORLD, top);
//     rs[EDGEANTIALIAS] = 0; rs[MULTISAMPLEANTIALIAS] = 1;
//     CGroup::Render();
//     worldStack->Pop(); SetTransform(WORLD, top);
//     rs[LIGHTING] = lighting; restore projection, then view.
// Theseus sizes the ortho box from the live view instead of a fixed 720x480,
// and the two antialias states are Xbox only.
void CHud::Render()
{
    D3DXMATRIX ortho;
    D3DXMatrixOrthoLH(&ortho, TheseusGetViewWidth(), TheseusGetViewHeight(), -10000.0f, 10000.0f);

    D3DXMATRIX savedProjection = g_matProjection;
    g_matProjection = ortho;
    TheseusSetTransform(D3DTS_PROJECTION, &g_matProjection);

    D3DXMATRIX savedView = g_matView;
    g_matView = g_matIdentity;
    TheseusSetTransform(D3DTS_VIEW, &g_matView);

    DWORD lighting;
    TheseusGetRenderState(D3DRS_LIGHTING, &lighting);
    TheseusSetRenderState(D3DRS_LIGHTING, FALSE);

    TheseusPushWorld();
    TheseusIdentityWorld();
    TheseusUpdateWorld();

#ifdef _XBOX
    // NV2A only, the desktop shim has nothing to switch.
    TheseusSetRenderState(D3DRS_EDGEANTIALIAS, FALSE);
    TheseusSetRenderState(D3DRS_MULTISAMPLEANTIALIAS, TRUE);
#endif

    CGroup::Render();

    TheseusPopWorld();
    TheseusSetRenderState(D3DRS_LIGHTING, lighting);

    g_matProjection = savedProjection;
    TheseusSetTransform(D3DTS_PROJECTION, &g_matProjection);
    g_matView = savedView;
    TheseusSetTransform(D3DTS_VIEW, &g_matView);
}


// ===== CPicture =============================================================
// Class object 0x00188174, 0x78 bytes, factory 0x00052db0, vtable 0x00028810.
// Field offsets: left 0x1c, top 0x20, width 0x24, height 0x28, texture 0x2c,
// transparency 0x30, fade 0x34, color 0x38, alpha 0x44, fade start 0x48
// (double), start/end pairs 0x50..0x6c, vertex buffer 0x70.

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

// One corner of the quad: position, normal, diffuse, one UV. 0x24 bytes,
// FVF 0x152 (XYZ | NORMAL | DIFFUSE | TEX1).
struct PictureVertex
{
    float x, y, z;
    float nx, ny, nz;
    D3DCOLOR diffuse;
    float tu, tv;
};

static const DWORD kPictureFVF = D3DFVF_VERTEX | D3DFVF_DIFFUSE;

// CPicture::CPicture()
// Reconstructed from 5960 retail XBE @ 0x00052db0 (the factory, ctor inlined):
//     left = top = 0; width = height = -1.0 (0x0002bef0); texture = 0;
//     transparency = fade = 0; color = (1,1,1); alpha = 1.0;
//     fadeStart = 0.0; vb = 0;
// The start/end pairs are left alone until OnLoad fills them.
CPicture::CPicture()
{
    m_left = 0.0f;
    m_top = 0.0f;
    m_width = -1.0f;
    m_height = -1.0f;
    m_texture = NULL;
    m_transparency = 0.0f;
    m_fade = 0.0f;
    m_color = D3DXVECTOR3(1.0f, 1.0f, 1.0f);
    m_alpha = 1.0f;
    m_startFadeTime = 0.0;
    m_pVB = NULL;
}

// CPicture::~CPicture()
// Reconstructed from 5960 retail XBE @ 0x00052e70:
//     if (vb) vb->Release(); if (texture) texture->Release();  // vtable slot 4
// Buffer first, then the texture node.
CPicture::~CPicture()
{
    if (m_pVB)
        m_pVB->Release();
    if (m_texture)
        m_texture->Release();
}

// CPicture::Render()
// Reconstructed from 5960 retail XBE @ 0x00052f10. Skips when fully faded or
// when the texture node has no surface yet. Makes its 4 vertex buffer on first
// use. A negative width or height takes the texture's size, and a CTexture
// stretches its UVs so only the loaded image shows. Builds a strip in screen
// pixels, recenters it on the view and flips Y, blends with color and alpha
// through TEXTUREFACTOR, draws, then puts texture, blend and filters back.
// See docs/decomp/HudNode.md. Theseus checks the buffer create and lock; the
// binary assumes both work.
void CPicture::Render()
{
    if (m_alpha <= 0.0f)
        return;

    LPDIRECT3DTEXTURE8 surface = NULL;
    if (m_texture)
    {
        surface = m_texture->GetTextureSurface();
        if (!surface)
            return;
    }

    if (!m_pVB)
    {
        if (FAILED(TheseusGetD3DDev()->CreateVertexBuffer(4 * sizeof(PictureVertex), D3DUSAGE_WRITEONLY, kPictureFVF, D3DPOOL_MANAGED, &m_pVB)))
            m_pVB = NULL;
        if (!m_pVB)
            return;
    }

    CTexture* image = NULL;
    if (m_texture && m_texture->IsKindOf(NODE_CLASS(CTexture)))
        image = (CTexture*)m_texture;

    float w = m_width;
    float h = m_height;
    if (w < 0.0f || h < 0.0f)
    {
        if (!surface)
            return;

        float surfW, surfH;
        TheseusGetTextureSize(surface, surfW, surfH);
        if (w < 0.0f)
            w = surfW;
        if (h < 0.0f)
            h = surfH;

        if (image)
        {
            w = (image->m_nImageWidth / w) * w;
            h = (image->m_nImageHeight / h) * h;
        }
    }

    // Textures get padded up to a hardware size, so trim the UVs to the image.
    float maxU = 1.0f;
    float maxV = 1.0f;
    if (image)
    {
        float surfW, surfH;
        TheseusGetTextureSize(surface, surfW, surfH);
        maxU = image->m_nImageWidth / surfW;
        maxV = image->m_nImageHeight / surfH;
    }

    TheseusSetTexture(0, surface);

    float right = m_left + w;
    float bottom = m_top + h;
    PictureVertex quad[4] =
    {
        { right,  bottom, 0, 0, 0, 0, 0xffffffff, maxU, maxV },
        { m_left, bottom, 0, 0, 0, 0, 0xffffffff, 0.0f, maxV },
        { right,  m_top,  0, 0, 0, 0, 0xffffffff, maxU, 0.0f },
        { m_left, m_top,  0, 0, 0, 0, 0xffffffff, 0.0f, 0.0f },
    };

    float halfW = TheseusGetViewWidth() * 0.5f;
    float halfH = TheseusGetViewHeight() * 0.5f;
    for (int n = 0; n < 4; n++)
    {
        PictureVertex& v = quad[n];
        v.x = v.x - halfW;
        v.y = -(v.y - halfH);
        v.z = 0.0f;
        v.nx = v.x;
        v.ny = v.y;
        v.nz = 1.0f;
    }

    TheseusSetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    TheseusSetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    TheseusSetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    TheseusSetRenderState(D3DRS_TEXTUREFACTOR, D3DCOLOR_COLORVALUE(m_color.x, m_color.y, m_color.z, m_alpha));

    if (surface)
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
        // No texture: the quad is just the factor color.
        TheseusSetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        TheseusSetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TFACTOR);
        TheseusSetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        TheseusSetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TFACTOR);
    }

#ifdef _XBOX
    // NV2A only, the desktop shim has nothing to switch.
    TheseusSetRenderState(D3DRS_EDGEANTIALIAS, FALSE);
    TheseusSetRenderState(D3DRS_MULTISAMPLEANTIALIAS, TRUE);
#endif

    BYTE* dst = NULL;
    if (SUCCEEDED(m_pVB->Lock(0, sizeof(quad), &dst, 0)) && dst)
    {
        memcpy(dst, quad, sizeof(quad));
        m_pVB->Unlock();

        LPDIRECT3DDEVICE8 dev = TheseusGetD3DDev();
        TheseusSetStreamSource(0, m_pVB, sizeof(PictureVertex));
        TheseusSetVertexShader(kPictureFVF);
        dev->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
    }

    TheseusSetTexture(0, NULL);
    TheseusSetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    TheseusSetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
    TheseusSetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
}

// CPicture::Advance(nSeconds)
// Reconstructed from 5960 retail XBE @ 0x000534c0. Ghidra decompile:
//     CNode::Advance(s); if (texture) texture->Advance(s);
//     if (fade != 0 && fadeStart != 0) {
//         t = (float)((now - fadeStart) / fade);
//         if (t > 1.0) { fadeStart = 0; t = 1.0; }
//         left = startLeft*(1-t) + endLeft*t;  ...top, width, height the same }
// The divide happens in double like the binary, then drops to float.
void CPicture::Advance(float nSeconds)
{
    CNode::Advance(nSeconds);

    if (m_texture)
        m_texture->Advance(nSeconds);

    if (m_fade == 0.0f || m_startFadeTime == 0.0)
        return;

    float t = (float)((TheseusGetNow() - m_startFadeTime) / m_fade);
    if (t > 1.0f)
    {
        m_startFadeTime = 0.0;
        t = 1.0f;
    }

    float s = 1.0f - t;
    m_left = m_startLeft * s + m_endLeft * t;
    m_top = m_startTop * s + m_endTop * t;
    m_width = m_startWidth * s + m_endWidth * t;
    m_height = m_startHeight * s + m_endHeight * t;
}

// CPicture::OnLoad()
// Reconstructed from 5960 retail XBE @ 0x000535c0. Ghidra decompile:
//     alpha = 1.0 - transparency;
//     startLeft = endLeft = left; ...top, width, height the same;
//     CNode::OnLoad();
void CPicture::OnLoad()
{
    m_alpha = 1.0f - m_transparency;

    m_startLeft = m_left;
    m_endLeft = m_left;
    m_startTop = m_top;
    m_endTop = m_top;
    m_startWidth = m_width;
    m_endWidth = m_width;
    m_startHeight = m_height;
    m_endHeight = m_height;

    CNode::OnLoad();
}

// CPicture::OnSetProperty(pprd, pvValue)
// Reconstructed from 5960 retail XBE @ 0x00053620. Ghidra decompile:
//     switch (prd->offset) {
//     case 0x30: a = 1.0 - v; if (fade == 0) alpha = a;
//                else new CLerper(this, &alpha, a, fade);  return 1;
//     case 0x34: if (v == 0) fadeStart = 0; CLerper::RemoveObject(this); return 1;
//     case 0x1c: endLeft = v; startLeft = left; break;   // 0x20/0x24/0x28 alike
//     default: return 1; }
//     if (fade != 0) { fadeStart = now; return 0; }  return 1;
// Returning false on a fading move keeps the old value, so Advance glides there.
bool CPicture::OnSetProperty(const PRD *pprd, const void *pvValue)
{
    int field = PTR2INT(pprd->pbOffset);

    if (field == offsetof(m_transparency))
    {
        float target = 1.0f - *(const float*)pvValue;
        if (m_fade == 0.0f)
            m_alpha = target;
        else
            new CLerper(this, &m_alpha, target, m_fade);
        return true;
    }

    if (field == offsetof(m_fade))
    {
        if (*(const float*)pvValue == 0.0f)
            m_startFadeTime = 0.0;
        CLerper::RemoveObject(this);
        return true;
    }

    // A move remembers where it starts from and where it's headed.
    float* from;
    float* to;
    float now;
    if (field == offsetof(m_left))
        { from = &m_startLeft; to = &m_endLeft; now = m_left; }
    else if (field == offsetof(m_top))
        { from = &m_startTop; to = &m_endTop; now = m_top; }
    else if (field == offsetof(m_width))
        { from = &m_startWidth; to = &m_endWidth; now = m_width; }
    else if (field == offsetof(m_height))
        { from = &m_startHeight; to = &m_endHeight; now = m_height; }
    else
        return true;

    *to = *(const float*)pvValue;
    *from = now;
    if (m_fade != 0.0f)
    {
        m_startFadeTime = TheseusGetNow();
        return false;
    }
    return true;
}
