// scene_groups.cpp: the scene graph's container nodes. Group, Transform,
// Inline, Spinner, Waver, Layout, Switch, Billboard, Layer, Background and
// Level, rebuilt function by function from the 5960 retail XBE (Group.cpp at
// 0x4d3b0..0x4f760, Background at 0x4fc40..0x504c0, Layer at 0x5a290..0x5ab30,
// Level at 0x5cbd0..0x5d230). Findings are in docs/decomp/SceneGroups.md.

#include "std.h"
#include "theseus.h"
#include "node.h"
#include "xip_archive.h"
#include "camera.h"
#include "shape_render.h"
#include "lerper.h"
#include "file_util.h"
#include "scene_groups.h"

extern D3DXMATRIX g_matView;
extern D3DXMATRIX g_matPosition;
extern D3DXMATRIX g_matProjection;
extern D3DXMATRIX g_matIdentity;
extern float g_nEffectAlpha;
extern float g_transitionMotionBlur;
extern void BindJoystick(CNode* pJoystickNode);


// ===== CGroup ===============================================================
// A node with children. Everything else in this file is built on it.

IMPLEMENT_NODE("Group", CGroup, CNode)

START_NODE_PROPS(CGroup, CNode)
    NODE_PROP(pt_children, CGroup, children)
END_NODE_PROPS()

// CGroup::CGroup()
// Reconstructed from 5960 retail XBE @ 0x0004d3d0 (the factory, ctor inlined):
//     children = {0,0,0}; bboxCenter = 0; bboxSize = -1.0 (0x0002bef0) x3;
//     bboxSpecified = 1; bboxDirty = 1;
// A size of -1 on every axis means "nobody gave us a box, work it out".
CGroup::CGroup()
{
    m_bboxCenter = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
    m_bboxSize = D3DXVECTOR3(-1.0f, -1.0f, -1.0f);
    m_bboxSpecified = true;
    m_bboxDirty = true;
}

// CGroup::~CGroup()
// Reconstructed from 5960 retail XBE @ 0x0004d470: ReleaseAll on the
// children, then the CNode teardown. The CNodeArray member does that release.
CGroup::~CGroup()
{
}

// CGroup::GetBBox(pBBox)
// Reconstructed from 5960 retail XBE @ 0x0004d4d0. Ghidra decompile:
//     if (size == (-1,-1,-1)) specified = 0;
//     if (dirty && !specified) {
//         if (count == 0) center = size = 0;
//         else { child[0]->GetBBox(&b); lo = b.c - b.s; hi = b.c + b.s;
//                for (i = 1..) { GetBBox(&b);
//                    if (b.c - b.s < lo) lo = b.c - b.s;
//                    if (b.c + b.s < hi) hi = b.c + b.s; }
//                center = (hi + lo) * 0.5; size = hi - lo; } }
//     *pBBox = { center, size };
// The far corner keeps the smaller value too, so it isn't a true union. That's
// the binary's behavior and we keep it. Dirty is never cleared here.
void CGroup::GetBBox(BBox* pBBox)
{
    if (m_bboxSize.x == -1.0f && m_bboxSize.y == -1.0f && m_bboxSize.z == -1.0f)
        m_bboxSpecified = false;

    if (m_bboxDirty && !m_bboxSpecified)
    {
        int count = m_children.GetLength();
        if (count == 0)
        {
            m_bboxCenter = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
            m_bboxSize = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
        }
        else
        {
            BBox box;
            m_children.GetNode(0)->GetBBox(&box);
            D3DXVECTOR3 lo = box.center - box.size;
            D3DXVECTOR3 hi = box.center + box.size;

            for (int n = 1; n < count; n++)
            {
                m_children.GetNode(n)->GetBBox(&box);
                D3DXVECTOR3 near_ = box.center - box.size;
                D3DXVECTOR3 far_ = box.center + box.size;

                if (near_.x < lo.x) lo.x = near_.x;
                if (near_.y < lo.y) lo.y = near_.y;
                if (near_.z < lo.z) lo.z = near_.z;
                if (far_.x < hi.x) hi.x = far_.x;
                if (far_.y < hi.y) hi.y = far_.y;
                if (far_.z < hi.z) hi.z = far_.z;
            }

            m_bboxCenter.x = (hi.x + lo.x) * 0.5f;
            m_bboxCenter.y = (hi.y + lo.y) * 0.5f;
            m_bboxCenter.z = (hi.z + lo.z) * 0.5f;
            m_bboxSize = hi - lo;
        }
    }

    pBBox->center = m_bboxCenter;
    pBBox->size = m_bboxSize;
}

// CGroup::GetRadius()
// Reconstructed from 5960 retail XBE @ 0x0004d770: the largest child radius,
// starting from 0.
float CGroup::GetRadius()
{
    float best = 0.0f;
    for (int n = 0; n < m_children.GetLength(); n++)
    {
        float r = m_children.GetNode(n)->GetRadius();
        if (r > best)
            best = r;
    }
    return best;
}

// CGroup::Render()
// Reconstructed from 5960 retail XBE @ 0x0004d7e0:
//     for (i = 0; i < count; i++) if (child[i]->visible) child[i]->Render();
// The visit/skip counters feed Theseus's frame profiler.
void CGroup::Render()
{
    for (int n = 0; n < m_children.GetLength(); n++)
    {
        CNode* child = m_children.GetNode(n);
        Frame_Count(FC_NODE_VISITS);
        if (!child->m_visible)
        {
            Frame_Count(FC_NODE_SKIPS);
            continue;
        }
        child->Render();
    }
}

// CGroup::Advance(nSeconds)
// Reconstructed from 5960 retail XBE @ 0x0004d820: CNode::Advance, then
// every child.
void CGroup::Advance(float nSeconds)
{
    CNode::Advance(nSeconds);
    for (int n = 0; n < m_children.GetLength(); n++)
        m_children.GetNode(n)->Advance(nSeconds);
}

// CGroup::SetLight
// Not in 5960, whose node vtable has no light slot. Theseus's light pass in
// the app walks the tree with it, so groups hand it on to their children.
void CGroup::SetLight(int& nLight, D3DCOLORVALUE& ambient)
{
    for (int n = 0; n < m_children.GetLength(); n++)
    {
        CNode* child = m_children.GetNode(n);
        if (child != NULL)
            child->SetLight(nLight, ambient);
    }
}

// CGroup::RenderDynamicTexture(pSurfx)
// Reconstructed from 5960 retail XBE @ 0x0004d860: pass the surface to every
// non-null child.
void CGroup::RenderDynamicTexture(CSurfx* pSurfx)
{
    for (int n = 0; n < m_children.GetLength(); n++)
    {
        CNode* child = m_children.GetNode(n);
        if (child != NULL)
            child->RenderDynamicTexture(pSurfx);
    }
}

// CGroup::GetTextureSurface()
// Reconstructed from 5960 retail XBE @ 0x0004d8a0: the first non-null child
// texture, else NULL.
LPDIRECT3DTEXTURE8 CGroup::GetTextureSurface()
{
    for (int n = 0; n < m_children.GetLength(); n++)
    {
        CNode* child = m_children.GetNode(n);
        if (child == NULL)
            continue;
        LPDIRECT3DTEXTURE8 tex = child->GetTextureSurface();
        if (tex != NULL)
            return tex;
    }
    return NULL;
}


// ===== CTransform ===========================================================
// Scale, rotation and translation for its children, plus an alpha. With fade
// set, the script setters glide to the new value over fade seconds.

IMPLEMENT_NODE("Transform", CTransform, CGroup)

START_NODE_PROPS(CTransform, CGroup)
    NODE_PROP(pt_vec3, CTransform, center)
    NODE_PROP(pt_vec4, CTransform, scaleOrientation)
    NODE_PROP(pt_vec3, CTransform, scale)
    NODE_PROP(pt_vec4, CTransform, rotation)
    NODE_PROP(pt_vec3, CTransform, translation)
    NODE_PROP(pt_number, CTransform, fade)
    NODE_PROP(pt_boolean, CTransform, moving)
    NODE_PROP(pt_number, CTransform, alpha)
END_NODE_PROPS()

#define _FND_CLASS CTransform
START_NODE_FUN(CTransform, CGroup)
    NODE_FUN_VNNN(SetScale)
    NODE_FUN_VNNNN(SetScaleOrientation)
    NODE_FUN_VNNN(SetTranslation)
    NODE_FUN_VNNN(SetCenter)
    NODE_FUN_VNNNN(SetRotation)
    NODE_FUN_VN(SetAlpha)
    NODE_FUN_VN(DisappearAfter)
END_NODE_FUN()
#undef _FND_CLASS

// CTransform::CTransform()
// Reconstructed from 5960 retail XBE @ 0x0004d910. Ghidra decompile:
//     rotation = (0,0,1,0); center = 0; scaleOrientation = (0,0,1,0);
//     scale = (1,1,1); translation = 0; moving = 0; dirty = 1; fade = 0;
//     alpha = 1.0; +0x178 = +0x17c = 0; the five fade clocks (doubles) = 0.0
// The rotation quaternion is left for OnLoad to build.
CTransform::CTransform() :
    m_rotation(0.0f, 0.0f, 1.0f, 0.0f),
    m_center(0.0f, 0.0f, 0.0f),
    m_scaleOrientation(0.0f, 0.0f, 1.0f, 0.0f),
    m_scale(1.0f, 1.0f, 1.0f),
    m_translation(0.0f, 0.0f, 0.0f),
    m_alpha(1.0f),
    m_moving(false),
    m_dirty(TRUE),
    m_fade(0.0f),
    m_timeCenterStart(0.0f),
    m_timeScaleOrientationStart(0.0f),
    m_timeScaleStart(0.0),
    m_timeRotationStart(0.0),
    m_timeTranslationStart(0.0),
    m_timeAlphaStart(0.0),
    m_timeToDisappear(0.0)
{
}

// CTransform::OnLoad()
// Reconstructed from 5960 retail XBE @ 0x0004da40:
//     CNode::OnLoad(); D3DXQuaternionRotationAxis(&rotationQuat, &rotation.xyz, rotation.w);
void CTransform::OnLoad()
{
    CGroup::OnLoad();
    D3DXQuaternionRotationAxis(&m_rotationQuat, (D3DXVECTOR3*)&m_rotation, m_rotation.w);
}

// CTransform::OnSetProperty(pprd, pvValue)
// Reconstructed from 5960 retail XBE @ 0x0004da60. Ghidra decompile:
//     if (prd->offset == 0x44) QuaternionRotationAxis(&quat, value, value[3]);
//     else if (prd->offset == 0x120) { SetAlpha inlined; return 0; }
//     return 1;
// A rotation write also rebuilds the quaternion; an alpha write goes through
// SetAlpha (so it can fade) and the raw store is vetoed.
bool CTransform::OnSetProperty(const PRD* pprd, const void* pvValue)
{
    int offset = PTR2INT(pprd->pbOffset);
    if (offset == offsetof(m_rotation))
    {
        const float* axisAngle = (const float*)pvValue;
        D3DXQuaternionRotationAxis(&m_rotationQuat, (const D3DXVECTOR3*)axisAngle, axisAngle[3]);
    }
    else if (offset == offsetof(m_alpha))
    {
        SetAlpha(*(const float*)pvValue);
        return false;
    }
    return true;
}

// Fraction of a fade that started at `start`, clamped to 1. When it reaches
// 1 the clock is stopped (set back to 0). The binary inlines this four times.
static float FadeStep(XTIME& start, float fade)
{
    float t = (float)(TheseusGetNow() - start) / fade;
    if (t >= 1.0f)
    {
        start = 0.0;
        t = 1.0f;
    }
    return t;
}

// CTransform::Advance(nSeconds)
// Reconstructed from 5960 retail XBE @ 0x0004dae0. Ghidra decompile:
//     CGroup::Advance(dt);
//     if (timeAlphaStart > 0) { t = (now - timeAlphaStart) / fade;
//         if (t >= 1) { timeAlphaStart = 0; t = 1; }
//         alpha = (alphaEnd - alphaStart) * t + alphaStart; }
//     if (timeToDisappear > 0 && timeToDisappear <= now) { timeToDisappear = 0; alpha = 0; }
void CTransform::Advance(float nSeconds)
{
    CGroup::Advance(nSeconds);

    if (m_timeAlphaStart > 0.0)
    {
        float t = FadeStep(m_timeAlphaStart, m_fade);
        m_alpha = m_alphaStart + (m_alphaEnd - m_alphaStart) * t;
    }

    if (m_timeToDisappear > 0.0 && m_timeToDisappear <= TheseusGetNow())
    {
        m_timeToDisappear = 0.0;
        m_alpha = 0.0f;
    }
}

// CTransform::CalcMatrix()
// Reconstructed from 5960 retail XBE @ 0x0004dbb0. Starts from identity, steps
// any running scale and translation fades (vector lerp) and the rotation fade
// (D3DXQuaternionSlerp), each one setting moving. Then multiplies in scale if
// it isn't (1,1,1), the quaternion if its w isn't 0, and the translation if it
// isn't zero, in that order. Dirty stays set while any of the three fades is
// still running (sum of their clocks > 0).
void CTransform::CalcMatrix()
{
    D3DXMatrixIdentity(&m_matrix);

    if (m_timeScaleStart > 0.0)
    {
        m_moving = true;
        float t = FadeStep(m_timeScaleStart, m_fade);
        D3DXVec3Lerp(&m_scale, &m_scaleStart, &m_scaleEnd, t);
    }

    if (m_timeTranslationStart > 0.0)
    {
        m_moving = true;
        float t = FadeStep(m_timeTranslationStart, m_fade);
        D3DXVec3Lerp(&m_translation, &m_translationStart, &m_translationEnd, t);
    }

    if (m_timeRotationStart > 0.0)
    {
        m_moving = true;
        float t = FadeStep(m_timeRotationStart, m_fade);
        D3DXQuaternionSlerp(&m_rotationQuat, &m_rotationStart, &m_rotationEnd, t);
    }

    D3DXMATRIX step;

    if (m_scale.x != 1.0f || m_scale.y != 1.0f || m_scale.z != 1.0f)
    {
        D3DXMatrixScaling(&step, m_scale.x, m_scale.y, m_scale.z);
        D3DXMatrixMultiply(&m_matrix, &m_matrix, &step);
    }

    if (m_rotationQuat.w != 0.0f)
    {
        D3DXMatrixRotationQuaternion(&step, &m_rotationQuat);
        D3DXMatrixMultiply(&m_matrix, &m_matrix, &step);
    }

    if (m_translation.x != 0.0f || m_translation.y != 0.0f || m_translation.z != 0.0f)
    {
        D3DXMatrixTranslation(&step, m_translation.x, m_translation.y, m_translation.z);
        D3DXMatrixMultiply(&m_matrix, &m_matrix, &step);
    }

    m_dirty = (m_timeScaleStart + m_timeTranslationStart + m_timeRotationStart) > 0.0;
}

// CTransform::SetScale(x, y, z)   [script fn 'SetScale', FND 0x00187dc8, sig 22]
// Reconstructed from 5960 retail XBE @ 0x0004df30. Ghidra decompile:
//     if (fade > 0) { timeScaleStart = now; scaleStart = scale;
//                     scaleEnd = (x,y,z); moving = 1; }
//     else scale = (x,y,z);
//     dirty = 1;
void CTransform::SetScale(float sx, float sy, float sz)
{
    D3DXVECTOR3 target(sx, sy, sz);
    if (m_fade > 0.0f)
    {
        m_timeScaleStart = TheseusGetNow();
        m_scaleStart = m_scale;
        m_scaleEnd = target;
        m_moving = true;
    }
    else
    {
        m_scale = target;
    }
    m_dirty = TRUE;
}

// CTransform::SetScaleOrientation(x, y, z, a)   [script fn 'SetScaleOrientation', FND 0x00187de0, sig 23]
// Reconstructed from 5960 retail XBE @ 0x0004dfd0:
//     scaleOrientation = (x,y,z,a); dirty = 1;
// Stored only; CalcMatrix never reads it.
void CTransform::SetScaleOrientation(float x, float y, float z, float a)
{
    m_scaleOrientation = D3DXQUATERNION(x, y, z, a);
    m_dirty = TRUE;
}

// CTransform::SetTranslation(x, y, z)   [script fn 'SetTranslation', FND 0x00187df8, sig 22]
// Reconstructed from 5960 retail XBE @ 0x0004e010: same shape as SetScale,
// on the translation and its fade clock.
void CTransform::SetTranslation(float x, float y, float z)
{
    D3DXVECTOR3 target(x, y, z);
    if (m_fade > 0.0f)
    {
        m_timeTranslationStart = TheseusGetNow();
        m_translationStart = m_translation;
        m_translationEnd = target;
        m_moving = true;
    }
    else
    {
        m_translation = target;
    }
    m_dirty = TRUE;
}

// CTransform::SetCenter(x, y, z)   [script fn 'SetCenter', FND 0x00187e10, sig 22]
// Reconstructed from 5960 retail XBE @ 0x0004e0c0:
//     center = (x,y,z); dirty = 1;
void CTransform::SetCenter(float x, float y, float z)
{
    m_center = D3DXVECTOR3(x, y, z);
    m_dirty = TRUE;
}

// CTransform::SetRotation(x, y, z, a)   [script fn 'SetRotation', FND 0x00187e28, sig 23]
// Reconstructed from 5960 retail XBE @ 0x0004e0f0. Ghidra decompile:
//     QuaternionRotationAxis(&q, &(x,y,z), a);
//     if (fade > 0) { timeRotationStart = now; rotationStart = rotationQuat;
//                     rotationEnd = q; moving = 1; }
//     else { rotation = (x,y,z,a); rotationQuat = q; }
//     dirty = 1;
// A fading rotation leaves the axis/angle property alone.
void CTransform::SetRotation(float x, float y, float z, float a)
{
    D3DXVECTOR3 axis(x, y, z);
    D3DXQUATERNION q;
    D3DXQuaternionRotationAxis(&q, &axis, a);

    if (m_fade > 0.0f)
    {
        m_timeRotationStart = TheseusGetNow();
        m_rotationStart = m_rotationQuat;
        m_rotationEnd = q;
        m_moving = true;
    }
    else
    {
        m_rotation = D3DXQUATERNION(x, y, z, a);
        m_rotationQuat = q;
    }
    m_dirty = TRUE;
}

// CTransform::SetAlpha(a)   [script fn 'SetAlpha', FND 0x00187e40, sig 19]
// Reconstructed from 5960 retail XBE @ 0x0004e200:
//     if (fade > 0) { timeAlphaStart = now; alphaStart = alpha; alphaEnd = a; }
//     else alpha = a;
void CTransform::SetAlpha(float a)
{
    if (m_fade > 0.0f)
    {
        m_timeAlphaStart = TheseusGetNow();
        m_alphaStart = m_alpha;
        m_alphaEnd = a;
    }
    else
    {
        m_alpha = a;
    }
}

// CTransform::DisappearAfter(t)   [script fn 'DisappearAfter', FND 0x00187e58, sig 19]
// Reconstructed from 5960 retail XBE @ 0x0004e250:
//     timeToDisappear = (double)(t + (float)now);
// The binary adds in single precision; Theseus keeps the double clock.
void CTransform::DisappearAfter(float t)
{
    m_timeToDisappear = t + TheseusGetNow();
}

// CTransform::Render()
// Reconstructed from 5960 retail XBE @ 0x0004e270. Ghidra decompile:
//     moving = 0; if (dirty) CalcMatrix();
//     world->Push(); world->MultMatrixLocal(&matrix); <upload world>
//     if (alpha > 0) { saved = effectAlpha; effectAlpha *= alpha;
//                      CGroup::Render(); effectAlpha = saved; }
//     world->Pop(); <upload world>
// moving reads true only on frames where a fade stepped the matrix. A fully
// transparent subtree counts as one skip for Theseus's frame profiler.
void CTransform::Render()
{
    m_moving = false;
    if (m_dirty)
        CalcMatrix();

    TheseusPushWorld();
    TheseusMultWorld(&m_matrix);
    TheseusUpdateWorld();

    if (m_alpha > 0.0f)
    {
        float outerAlpha = g_nEffectAlpha;
        g_nEffectAlpha = outerAlpha * m_alpha;
        CGroup::Render();
        g_nEffectAlpha = outerAlpha;
    }
    else
    {
        Frame_Count(FC_NODE_SKIPS);
    }

    TheseusPopWorld();
}


// ===== CInline ==============================================================
// Pulls another XAP in as a child, out of its own XIP when there is one, and
// fades it in once it arrives.

class CInline : public CGroup
{
    DECLARE_NODE(CInline, CGroup)
public:
    CInline();
    ~CInline();

    TCHAR* m_url;
    bool m_preload;
    bool m_bClassLoaded;
    bool m_fadeInDelayLoad;

    void Render();
    void Advance(float nSeconds);

protected:
    void Init();
    void FinishLoad();
    bool m_dirty;

    bool m_bLoading;
    bool m_bFirstLoad;
    CXipFile* m_xipFile;
    XTIME m_timeOfLoad;

    CClass m_class;

    DECLARE_NODE_PROPS()

#ifdef THESEUS_DESKTOP
    // Hot reload from new XAP text (the XAP editor's save loop)
    bool ReloadFromText(const char* xapText);
#endif

private:
    HANDLE m_hClassLoaderThread;
    TCHAR m_szAbsURL[MAX_PATH];
};

IMPLEMENT_NODE("Inline", CInline, CGroup)

START_NODE_PROPS(CInline, CGroup)
    NODE_PROP(pt_string, CInline, url)
    NODE_PROP(pt_boolean, CInline, preload)
    NODE_PROP(pt_boolean, CInline, fadeInDelayLoad)
END_NODE_PROPS()

// CInline::CInline()
// Reconstructed from 5960 retail XBE @ 0x0004e3b0. Ghidra decompile:
//     url = 0; preload = 0; classLoaded = 0; fadeInDelayLoad = 1; CClass();
//     thread = 0; timeOfLoad = 0.0; loading = 0; firstLoad = 0; xip = 0; dirty = 1;
CInline::CInline() :
    m_url(NULL),
    m_preload(false),
    m_bClassLoaded(false),
    m_fadeInDelayLoad(true),
    m_dirty(true),
    m_bLoading(false),
    m_bFirstLoad(false),
    m_xipFile(NULL),
    m_timeOfLoad(0.0),
    m_hClassLoaderThread(NULL)
{
}

// CInline::~CInline()
// Reconstructed from 5960 retail XBE @ 0x0004e470: free the url, then the
// class and group teardown.
CInline::~CInline()
{
    delete [] m_url;
}

// CInline::Init()
// Reconstructed from 5960 retail XBE @ 0x0004e4d0. Ghidra decompile:
//     dirty = 0; children.ReleaseAll(); if (!url) return;
//     MakeAbsoluteURL(path, url); p = wcsrchr(path, '/');
//     if (p) { wcscpy(p, ".xip"); loading = 1; firstLoad = 1;
//              xip = LoadXIP(path); if (xip) return; loading = 0; }
//     FinishLoad();
// The archive is named after the folder holding the XAP. With one, the load
// finishes later in Advance; without, the class loads right here. The binary
// never fills in the absolute url on that path; we do, so it can't parse garbage.
void CInline::Init()
{
    m_dirty = false;
    m_children.ReleaseAll();

    if (m_url == NULL)
        return;

    TCHAR xipPath[MAX_PATH];
    MakeAbsoluteURL(xipPath, m_url);
    TCHAR* slash = _tcsrchr(xipPath, '/');
    if (slash != NULL)
    {
        _tcscpy(slash, _T(".xip"));
        m_bLoading = true;
        m_bFirstLoad = true;
        m_xipFile = LoadXIP(xipPath);
        if (m_xipFile != NULL)
            return;
        m_bLoading = false;
    }

    MakeAbsoluteURL(m_szAbsURL, m_url);
    FinishLoad();
}

// CInline::FinishLoad()
// Reconstructed from 5960 retail XBE @ 0x0004e560. Ghidra decompile:
//     if (m_class.LoadAbsURL(absURL)) { n = m_class.CreateNode();
//         CallFunction(n, "initialize"); children.AddNode(n); n->parent = this;
//         CallFunction(this, "onLoad"); }
//     firstLoad = 0; classLoaded = 1;
// The load timer around it is Theseus's (frame profiler hitch report).
void CInline::FinishLoad()
{
    long long started = Frame_Now();

    if (m_class.LoadAbsURL(m_szAbsURL))
    {
        CNode* root = m_class.CreateNode();
        CallFunction(root, _T("initialize"));
        m_children.AddNode(root);
        root->m_parent = this;
        CallFunction(this, _T("onLoad"));
    }

    m_bFirstLoad = false;
    m_bClassLoaded = true;

    Frame_NoteLoad(Frame_Ms(Frame_Now() - started));
}

// CInline::Render()
// Reconstructed from 5960 retail XBE @ 0x0004e5d0. Nothing without a url or
// while loading. If our XIP had its mesh buffers dropped (loaded, not
// reloading, has vertex buffers, first one gone) it starts a reload and skips
// the frame. Otherwise Init when dirty, and once the class is in, render the
// children with the effect alpha scaled by the fade in, (now - timeOfLoad) / 2
// capped at 1 (or 1 straight away without fadeInDelayLoad), inside the XAP's
// directory. See docs/decomp/SceneGroups.md.
void CInline::Render()
{
    if (m_url == NULL || m_bLoading)
        return;

    if (m_xipFile != NULL && m_xipFile->IsUnloaded())
    {
        m_bLoading = true;
        m_xipFile->Reload();
        return;
    }

    if (m_dirty)
        Init();

    if (m_bLoading || !m_bClassLoaded)
        return;

    float fadeIn = 1.0f;
    if (m_fadeInDelayLoad)
    {
        fadeIn = (float)(TheseusGetNow() - m_timeOfLoad) * 0.5f;
        if (fadeIn > 1.0f)
            fadeIn = 1.0f;
    }

    float outerAlpha = g_nEffectAlpha;
    g_nEffectAlpha = outerAlpha * fadeIn;
    {
        CDirPush inXapDir(m_url);
        CGroup::Render();
    }
    g_nEffectAlpha = outerAlpha;
}

// CInline::Advance(nSeconds)
// Reconstructed from 5960 retail XBE @ 0x0004e740. Ghidra decompile:
//     if (dirty && preload) Init();
//     if (xip && (!xip->loaded || xip->reloading)) { loading = 1; return; }
//     if (loading) { loading = 0;
//         if (firstLoad) { MakeAbsoluteURL(absURL, url); if (!thread) FinishLoad(); }
//         timeOfLoad = now; }
//     if (thread && WaitForSingleObject(thread, 0) == 0) { CloseHandle(thread); thread = 0; }
//     CGroup::Advance(dt);
// The class always loads on this thread (the script engine isn't thread
// safe), so the handle is never set; the check is kept as the binary has it.
void CInline::Advance(float nSeconds)
{
    if (m_dirty && m_preload)
        Init();

    if (m_xipFile != NULL && !m_xipFile->IsReady())
    {
        m_bLoading = true;
        return;
    }

    if (m_bLoading)
    {
        m_bLoading = false;
        if (m_bFirstLoad)
        {
            MakeAbsoluteURL(m_szAbsURL, m_url);
            if (m_hClassLoaderThread == NULL)
                FinishLoad();
        }
        m_timeOfLoad = TheseusGetNow();
    }

    if (m_hClassLoaderThread != NULL && WaitForSingleObject(m_hClassLoaderThread, 0) == WAIT_OBJECT_0)
    {
        CloseHandle(m_hClassLoaderThread);
        m_hClassLoaderThread = NULL;
    }

    CGroup::Advance(nSeconds);
}


// ===== CSpinner =============================================================
// Turns its children around an axis, rpm turns a minute.

class CSpinner : public CGroup
{
    DECLARE_NODE(CSpinner, CGroup)
public:
    CSpinner();

    float m_rpm;
    D3DXVECTOR3 m_axis;
    float m_angle;

    void Render();
    void Advance(float nSeconds);

    DECLARE_NODE_PROPS()
};

IMPLEMENT_NODE("Spinner", CSpinner, CGroup)

START_NODE_PROPS(CSpinner, CGroup)
    NODE_PROP(pt_number, CSpinner, rpm)
    NODE_PROP(pt_vec3, CSpinner, axis)
    NODE_PROP(pt_number, CSpinner, angle)
END_NODE_PROPS()

// CSpinner::CSpinner()
// Reconstructed from 5960 retail XBE @ 0x0004e820 (factory, ctor inlined):
//     rpm = 1.0; axis = (0, 1, 0); angle = 0;
CSpinner::CSpinner() :
    m_rpm(1.0f),
    m_axis(0.0f, 1.0f, 0.0f),
    m_angle(0.0f)
{
}

// CSpinner::Render()   (shared with CWaver)
// Reconstructed from 5960 retail XBE @ 0x0004e8c0:
//     world->Push(); D3DXMatrixRotationAxis(&m, &axis, angle);
//     world->MultMatrixLocal(&m); <upload>; CGroup::Render(); world->Pop(); <upload>
void CSpinner::Render()
{
    D3DXMATRIX spin;
    TheseusPushWorld();
    D3DXMatrixRotationAxis(&spin, &m_axis, m_angle);
    TheseusMultWorld(&spin);
    TheseusUpdateWorld();

    CGroup::Render();

    TheseusPopWorld();
}

// CSpinner::Advance(nSeconds)
// Reconstructed from 5960 retail XBE @ 0x0004e980. Ghidra decompile:
//     CGroup::Advance(dt);
//     angle += rpm * (1/60) * dt * 2pi;
//     while (angle > 2pi) angle -= 2pi;  while (angle < -2pi) angle += 2pi;
// The binary multiplies by a stored 1/60; written as the division it stands for.
void CSpinner::Advance(float nSeconds)
{
    CGroup::Advance(nSeconds);

    const float turn = 2.0f * D3DX_PI;
    m_angle += m_rpm / 60.0f * nSeconds * turn;

    while (m_angle > turn)
        m_angle -= turn;
    while (m_angle < -turn)
        m_angle += turn;
}


// ===== CWaver ===============================================================
// A Spinner that rocks back and forth instead of going round.

class CWaver : public CSpinner
{
    DECLARE_NODE(CWaver, CSpinner)
public:
    CWaver();

    float m_field;

    void Advance(float nSeconds);

    DECLARE_NODE_PROPS()
};

IMPLEMENT_NODE("Waver", CWaver, CGroup)

START_NODE_PROPS(CWaver, CSpinner)
    NODE_PROP(pt_number, CWaver, field)
END_NODE_PROPS()

// CWaver::CWaver()
// Reconstructed from 5960 retail XBE @ 0x0004ea20 (factory, ctor inlined):
// the Spinner defaults plus field = pi/4 (0x0002c150).
CWaver::CWaver() :
    m_field(D3DX_PI / 4.0f)
{
}

// CWaver::Advance(nSeconds)
// Reconstructed from 5960 retail XBE @ 0x0004eaf0. Disassembly:
//     CGroup::Advance(dt);   // not CSpinner's
//     angle = sin(rpm * now * (pi/60)) * field * 0.5;
// The angle is a function of the clock alone, swinging field wide.
void CWaver::Advance(float nSeconds)
{
    CGroup::Advance(nSeconds);
    float phase = (float)(D3DX_PI * TheseusGetNow() * m_rpm / 60.0f);
    m_angle = sinf(phase) * m_field * 0.5f;
}


// ===== CLayout ==============================================================
// Lines its children up along direction, or rings them round the Y axis when
// direction is zero.

class CLayout : public CGroup
{
    DECLARE_NODE(CLayout, CGroup)
public:
    CLayout();

    D3DXVECTOR3 m_direction;
    float m_spacing;

    void Render();
    void GetBBox(BBox* pBBox);
    float GetRadius();

    DECLARE_NODE_PROPS()
};

IMPLEMENT_NODE("Layout", CLayout, CGroup)

START_NODE_PROPS(CLayout, CGroup)
    NODE_PROP(pt_vec3, CLayout, direction)
    NODE_PROP(pt_number, CLayout, spacing)
END_NODE_PROPS()

// CLayout::CLayout()
// Reconstructed from 5960 retail XBE @ 0x0004eb30 (factory, ctor inlined):
//     direction = (1, 0, 0); spacing = 1.0;
CLayout::CLayout() :
    m_direction(1.0f, 0.0f, 0.0f),
    m_spacing(1.0f)
{
}

// CLayout::Render()
// Reconstructed from 5960 retail XBE @ 0x0004ebd0. Ring (direction zero):
// radius (count / 4) * spacing, integer divide. Each child i gets a Push, then
// if visible: translate (cos(-a), 0, sin(-a)) * radius with a = 2pi * i / count,
// rotate a + pi/2 about (0, 0.1, 0), upload, render, Pop. A hidden child's Push
// is never popped; the binary does that too. Line: Push, step back
// (count + spacing) / 2 along direction, then per visible child step half a
// unit, upload, render, and step spacing + 0.5 more unless it's the last; Pop
// at the end.
void CLayout::Render()
{
    int count = m_children.GetLength();
    if (count == 0)
        return;

    if (m_direction.x == 0.0f && m_direction.y == 0.0f && m_direction.z == 0.0f)
    {
        float radius = (float)(count / 4) * m_spacing;
        D3DXVECTOR3 up(0.0f, 0.1f, 0.0f);

        for (int n = 0; n < count; n++)
        {
            TheseusPushWorld();

            CNode* child = m_children.GetNode(n);
            Frame_Count(FC_NODE_VISITS);
            if (!child->m_visible)
            {
                Frame_Count(FC_NODE_SKIPS);
                continue;
            }

            float a = 2.0f * D3DX_PI * n / count;
            TheseusTranslateWorld(radius * cosf(-a), 0.0f, radius * sinf(-a));
            TheseusRotateWorld(&up, a + D3DX_PI / 2.0f);
            TheseusUpdateWorld();

            child->Render();

            TheseusPopWorld();
        }
        return;
    }

    float spacing = m_spacing;
    D3DXVECTOR3 step;

    TheseusPushWorld();

    D3DXVec3Scale(&step, &m_direction, (count + spacing) * -0.5f);
    TheseusTranslateWorld(step.x, step.y, step.z);

    for (int n = 0; n < count; n++)
    {
        CNode* child = m_children.GetNode(n);
        Frame_Count(FC_NODE_VISITS);
        if (!child->m_visible)
        {
            Frame_Count(FC_NODE_SKIPS);
            continue;
        }

        D3DXVec3Scale(&step, &m_direction, 0.5f);
        TheseusTranslateWorld(step.x, step.y, step.z);
        TheseusUpdateWorld();

        child->Render();

        if (n < count - 1)
        {
            D3DXVec3Scale(&step, &m_direction, spacing + 0.5f);
            TheseusTranslateWorld(step.x, step.y, step.z);
        }
    }

    TheseusPopWorld();
}

// CLayout::GetBBox(pBBox)
// Reconstructed from 5960 retail XBE @ 0x0004ef00. Ghidra decompile:
//     if (count == 0) { *pBBox = 0; return; }
//     child[0]->GetBBox(pBBox);
//     for (i = 1..) { child[i]->GetBBox(&b); size += b.size;
//                     if (b.size < size) size = b.size;   // per axis }
// Center is the first child's; for positive sizes each pass ends on the
// newest child's size. Kept as the binary has it.
void CLayout::GetBBox(BBox* pBBox)
{
    int count = m_children.GetLength();
    if (count == 0)
    {
        CNode::GetBBox(pBBox);
        return;
    }

    m_children.GetNode(0)->GetBBox(pBBox);

    for (int n = 1; n < count; n++)
    {
        BBox box;
        m_children.GetNode(n)->GetBBox(&box);

        D3DXVECTOR3& size = pBBox->size;
        size += box.size;
        if (box.size.x < size.x) size.x = box.size.x;
        if (box.size.y < size.y) size.y = box.size.y;
        if (box.size.z < size.z) size.z = box.size.z;
    }
}

// CLayout::GetRadius()
// Reconstructed from 5960 retail XBE @ 0x0004f000:
//     r = sum(child radius); if (count > 1) r += (count - 1) * spacing * 0.5;
float CLayout::GetRadius()
{
    int count = m_children.GetLength();
    float total = 0.0f;

    for (int n = 0; n < count; n++)
        total += m_children.GetNode(n)->GetRadius();

    if (count > 1)
        total += (float)(count - 1) * m_spacing * 0.5f;

    return total;
}


// ===== CSwitch ==============================================================
// Shows the one choice whichChoice picks (none when out of range), but keeps
// all of them advancing.

class CSwitch : public CNode
{
    DECLARE_NODE(CSwitch, CNode)
public:
    CSwitch();
    ~CSwitch();

    int m_whichChoice;
    CNodeArray m_choice;

    void Render();
    void Advance(float nSeconds);
    void GetBBox(BBox* pBBox);
    float GetRadius();
    void SetLight(int& nLight, D3DCOLORVALUE& ambient);

    DECLARE_NODE_PROPS()

private:
    CNode* Chosen();
};

IMPLEMENT_NODE("Switch", CSwitch, CNode)

START_NODE_PROPS(CSwitch, CNode)
    NODE_PROP(pt_integer, CSwitch, whichChoice)
    NODE_PROP(pt_nodearray, CSwitch, choice)
END_NODE_PROPS()

// CSwitch::CSwitch()
// Reconstructed from 5960 retail XBE @ 0x0004f080 (factory, ctor inlined):
//     whichChoice = -1; choice = {0,0,0};
CSwitch::CSwitch() :
    m_whichChoice(-1)
{
}

// CSwitch::~CSwitch()
// Reconstructed from 5960 retail XBE @ 0x0004f100: ReleaseAll on the
// choices (the CNodeArray member does it), then the CNode teardown.
CSwitch::~CSwitch()
{
}

// The picked choice, or NULL when whichChoice is out of range. Every Switch
// method in the binary opens with this bounds test.
CNode* CSwitch::Chosen()
{
    if (m_whichChoice < 0 || m_whichChoice >= m_choice.GetLength())
        return NULL;
    return m_choice.GetNode(m_whichChoice);
}

// CSwitch::Render()
// Reconstructed from 5960 retail XBE @ 0x0004f160: render the choice if it's
// in range and visible. Visit/skip counts are Theseus's profiler.
void CSwitch::Render()
{
    CNode* chosen = Chosen();
    if (chosen == NULL)
        return;

    Frame_Count(FC_NODE_VISITS);
    if (!chosen->m_visible)
    {
        Frame_Count(FC_NODE_SKIPS);
        return;
    }
    chosen->Render();
}

// CSwitch::GetBBox(pBBox)
// Reconstructed from 5960 retail XBE @ 0x0004f190: the choice's box, else
// all zeros.
void CSwitch::GetBBox(BBox* pBBox)
{
    CNode* chosen = Chosen();
    if (chosen == NULL)
    {
        CNode::GetBBox(pBBox);
        return;
    }
    chosen->GetBBox(pBBox);
}

// CSwitch::GetRadius()
// Reconstructed from 5960 retail XBE @ 0x0004f1e0: the choice's radius, else 0.
float CSwitch::GetRadius()
{
    CNode* chosen = Chosen();
    return chosen != NULL ? chosen->GetRadius() : 0.0f;
}

// CSwitch::Advance(nSeconds)
// Reconstructed from 5960 retail XBE @ 0x0004f210: CNode::Advance, then
// every choice, picked or not.
void CSwitch::Advance(float nSeconds)
{
    CNode::Advance(nSeconds);
    for (int n = 0; n < m_choice.GetLength(); n++)
        m_choice.GetNode(n)->Advance(nSeconds);
}

// CSwitch::SetLight
// Theseus only, like CGroup::SetLight: hand it to the picked choice.
void CSwitch::SetLight(int& nLight, D3DCOLORVALUE& ambient)
{
    CNode* chosen = Chosen();
    if (chosen != NULL)
        chosen->SetLight(nLight, ambient);
}


// ===== CBillboard ===========================================================
// Keeps its children turned toward the camera, either fully or only about
// axisOfRotation.

class CBillboard : public CGroup
{
    DECLARE_NODE(CBillboard, CGroup)
public:
    CBillboard();
    ~CBillboard();

    D3DXVECTOR3 m_axisOfRotation;

    void Render();

    D3DXMATRIX m_matrix;
    void CalcMatrix();

    DECLARE_NODE_PROPS()
};

IMPLEMENT_NODE("Billboard", CBillboard, CGroup)

START_NODE_PROPS(CBillboard, CGroup)
    NODE_PROP(pt_vec3, CBillboard, axisOfRotation)
END_NODE_PROPS()

// CBillboard::CBillboard()
// Reconstructed from 5960 retail XBE @ 0x0004f260 (factory, ctor inlined):
//     axisOfRotation = (0, 1, 0);
CBillboard::CBillboard() :
    m_axisOfRotation(0.0f, 1.0f, 0.0f)
{
}

// CBillboard::~CBillboard()
// Reconstructed from 5960 retail XBE @ 0x0004f320: just the group teardown.
CBillboard::~CBillboard()
{
}

// CBillboard::Render()
// Reconstructed from 5960 retail XBE @ 0x0004f330:
//     CalcMatrix(); world->Push(); world->MultMatrixLocal(&matrix); <upload>;
//     CGroup::Render(); world->Pop(); <upload>
void CBillboard::Render()
{
    CalcMatrix();

    TheseusPushWorld();
    TheseusMultWorld(&m_matrix);
    TheseusUpdateWorld();

    CGroup::Render();

    TheseusPopWorld();
}

// CBillboard::CalcMatrix()
// Reconstructed from 5960 retail XBE @ 0x0004f3d0 (unnamed helper, only
// called from Render). Inverts the current world matrix and brings the camera
// position into local space as a normalized "eye". With an axis no longer than
// 0.001 it builds a facing frame: up is (inv._14, inv._22, inv._23) normalized,
// x = normalize(up x eye), rows x, up, eye. Otherwise it normalizes the axis,
// takes x = normalize(axis x eye), z = x x axis, and rotates about the axis by
// acos(z.z), negated when x.z > 0. The odd up vector is the binary's; kept.
void CBillboard::CalcMatrix()
{
    D3DXMATRIX world, toLocal;
    TheseusGetTransform(D3DTS_WORLD, &world);
    D3DXMatrixInverse(&toLocal, NULL, &world);

    D3DXVECTOR3 eye = theCamera.m_position;
    D3DXVec3TransformNormal(&eye, &eye, &toLocal);
    D3DXVec3Normalize(&eye, &eye);

    D3DXVECTOR3 axis = m_axisOfRotation;
    float length = D3DXVec3Length(&axis);

    if (length <= 0.001f)
    {
        D3DXVECTOR3 up(toLocal._14, toLocal._22, toLocal._23);
        D3DXVec3Normalize(&up, &up);

        D3DXVECTOR3 side;
        D3DXVec3Cross(&side, &up, &eye);
        D3DXVec3Normalize(&side, &side);

        m_matrix = D3DXMATRIX(
            side.x, side.y, side.z, 0.0f,
            up.x,   up.y,   up.z,   0.0f,
            eye.x,  eye.y,  eye.z,  0.0f,
            0.0f,   0.0f,   0.0f,   1.0f);
        return;
    }

    axis *= 1.0f / length;

    D3DXVECTOR3 side, facing;
    D3DXVec3Cross(&side, &axis, &eye);
    D3DXVec3Normalize(&side, &side);
    D3DXVec3Cross(&facing, &side, &axis);

    float angle = acosf(facing.z);
    if (side.z > 0.0f)
        angle = -angle;

    D3DXMatrixRotationAxis(&m_matrix, &axis, angle);
}


// ===== CLayer ===============================================================
// A scene drawn through its own viewpoint and projection, over a black wash
// whose strength is 1 - transparency.

class CLayer : public CGroup
{
    DECLARE_NODE(CLayer, CGroup)
public:
    CLayer();
    ~CLayer();

    void Render();
    void Advance(float nSeconds);
    bool OnSetProperty(const PRD* pprd, const void* pvValue);

    CNode* m_viewpoint;
    CNode* m_navigationInfo;

    float m_fade;
    float m_transparency;

protected:
    LPDIRECT3DVERTEXBUFFER8 m_vb;
    float m_alpha;

    DECLARE_NODE_PROPS()
};

IMPLEMENT_NODE("Layer", CLayer, CGroup)

START_NODE_PROPS(CLayer, CGroup)
    NODE_PROP(pt_node, CLayer, viewpoint)
    NODE_PROP(pt_node, CLayer, navigationInfo)
    NODE_PROP(pt_number, CLayer, fade)
    NODE_PROP(pt_number, CLayer, transparency)
END_NODE_PROPS()

// CLayer::CLayer()
// Reconstructed from 5960 retail XBE @ 0x0005a2a0 (factory, ctor inlined):
//     viewpoint = navigationInfo = 0; fade = transparency = 0; vb = 0;
// alpha (+0x58) isn't set until transparency is written; same here.
CLayer::CLayer() :
    m_viewpoint(NULL),
    m_navigationInfo(NULL),
    m_fade(0.0f),
    m_transparency(0.0f),
    m_vb(NULL)
{
}

// CLayer::~CLayer()
// Reconstructed from 5960 retail XBE @ 0x0005a350: release the viewpoint,
// the navigation info and the wash vertex buffer.
CLayer::~CLayer()
{
    if (m_viewpoint != NULL)
        m_viewpoint->Release();
    if (m_navigationInfo != NULL)
        m_navigationInfo->Release();
    if (m_vb != NULL)
        m_vb->Release();
}

// CLayer::OnSetProperty(pprd, pvValue)
// Reconstructed from 5960 retail XBE @ 0x0005a400. Ghidra decompile:
//     if (prd->offset == 0x50) { CLerper::RemoveObject(this);
//         if (fade <= 0) alpha = 1 - value;
//         else new CLerper(this, &alpha, 1 - value, fade); }
//     return 1;
bool CLayer::OnSetProperty(const PRD* pprd, const void* pvValue)
{
    if (PTR2INT(pprd->pbOffset) == offsetof(m_transparency))
    {
        float target = 1.0f - *(const float*)pvValue;
        CLerper::RemoveObject(this);
        if (m_fade <= 0.0f)
            m_alpha = target;
        else
            new CLerper(this, &m_alpha, target, m_fade);
    }
    return true;
}

struct WashVertex
{
    float x, y, z;
    DWORD color;
};

// CLayer::Render()
// Reconstructed from 5960 retail XBE @ 0x0005a4a0. Nothing without a
// viewpoint or with alpha 0. Otherwise it draws a screen sized black strip
// (vertex buffer built on first use, colors 0xff000000) under an ortho
// projection with identity world and view, alpha = diffuse * texture factor
// (alpha * 255), blended, no culling, stage 1 off. Then it saves the
// matrices, sets a perspective projection (fov from the viewpoint, near
// avatarSize.x / 2 or 0.1, far visibilityLimit or 1000) and a view built from
// the viewpoint's position and orientation with z flipped, renders the
// children from an identity world, and puts everything back. See
// docs/decomp/SceneGroups.md. Theseus differences, one each: the aspect
// follows the window unless Xbox resolution is emulated; a navigationInfo
// that isn't a NavigationInfo is ignored (the binary tests and goes on).
void CLayer::Render()
{
    if (m_viewpoint == NULL || m_alpha == 0.0f)
        return;

    float viewW = TheseusGetViewWidth();
    float viewH = TheseusGetViewHeight();

    if (m_vb == NULL)
    {
        if (FAILED(TheseusGetD3DDev()->CreateVertexBuffer(4 * sizeof(WashVertex), D3DUSAGE_WRITEONLY,
                D3DFVF_XYZ | D3DFVF_DIFFUSE, D3DPOOL_MANAGED, &m_vb)))
            return;

        WashVertex* v;
        if (SUCCEEDED(m_vb->Lock(0, 4 * sizeof(WashVertex), (BYTE**)&v, 0)))
        {
            const float corner[4][2] = { { 0.5f, -0.5f }, { -0.5f, -0.5f }, { 0.5f, 0.5f }, { -0.5f, 0.5f } };
            for (int n = 0; n < 4; n++)
            {
                v[n].x = viewW * corner[n][0];
                v[n].y = viewH * corner[n][1];
                v[n].z = 0.0f;
                v[n].color = D3DCOLOR_ARGB(255, 0, 0, 0);
            }
            m_vb->Unlock();
        }
    }

    D3DXMATRIX outerProjection, outerWorld, outerView;
    TheseusGetTransform(D3DTS_PROJECTION, &outerProjection);
    TheseusGetTransform(D3DTS_WORLD, &outerWorld);
    TheseusGetTransform(D3DTS_VIEW, &outerView);

    D3DXMATRIX ortho;
    D3DXMatrixOrthoLH(&ortho, viewW, viewH, -10000.0f, 10000.0f);
    TheseusSetTransform(D3DTS_PROJECTION, &ortho);
    TheseusSetTransform(D3DTS_WORLD, &g_matIdentity);
    TheseusSetTransform(D3DTS_VIEW, &g_matIdentity);

    TheseusSetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    TheseusSetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    TheseusSetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);
    TheseusSetRenderState(D3DRS_TEXTUREFACTOR, (DWORD)(m_alpha * 255.0f) << 24);
    TheseusSetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    TheseusSetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    TheseusSetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    TheseusSetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    TheseusSetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    TheseusSetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    TheseusSetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    TheseusSetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

    TheseusGetD3DDev()->SetStreamSource(0, m_vb, sizeof(WashVertex));
    TheseusGetD3DDev()->SetVertexShader(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    TheseusGetD3DDev()->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);

    TheseusSetTransform(D3DTS_PROJECTION, &outerProjection);
    TheseusSetTransform(D3DTS_WORLD, &outerWorld);
    TheseusSetTransform(D3DTS_VIEW, &outerView);

    CViewpoint* viewpoint = (CViewpoint*)m_viewpoint;
    CNavigationInfo* nav = (CNavigationInfo*)m_navigationInfo;
    if (nav != NULL && !nav->IsKindOf(NODE_CLASS(CNavigationInfo)))
        nav = NULL;

    D3DXMATRIX savedProjection, savedWorld, savedView;
    TheseusGetTransform(D3DTS_PROJECTION, &savedProjection);
    TheseusGetTransform(D3DTS_WORLD, &savedWorld);
    TheseusGetTransform(D3DTS_VIEW, &savedView);

    float zNear = 0.1f;
    float zFar = 1000.0f;
    if (nav != NULL)
    {
        zNear = nav->m_avatarSize.x * 0.5f;
        if (nav->m_visibilityLimit != 0.0f)
            zFar = nav->m_visibilityLimit;
    }

    // 1.5, or 1.875 stretched, is the binary's 720x480; off emulation Theseus
    // uses the real window shape.
    float aspect;
    if (TheseusGetEmulateXboxRes())
        aspect = TheseusGetStretchWidescreen() ? 1.5f * 1.25f : 1.5f;
    else
        aspect = viewH > 0.0f ? viewW / viewH : 1.5f;

    D3DXMatrixPerspectiveFovLH(&g_matProjection, viewpoint->m_fieldOfView, aspect, zNear, zFar);
    TheseusSetTransform(D3DTS_PROJECTION, &g_matProjection);

    D3DXVECTOR3 eyeAt(viewpoint->m_position.x, viewpoint->m_position.y, -viewpoint->m_position.z);
    D3DXQUATERNION facing;
    D3DXQuaternionRotationAxis(&facing, (const D3DXVECTOR3*)&viewpoint->m_orientation, -viewpoint->m_orientation.w);
    D3DXMatrixAffineTransformation(&g_matView, 1.0f, NULL, &facing, &eyeAt);
    D3DXMatrixInverse(&g_matView, NULL, &g_matView);
    g_matView._31 = -g_matView._31;
    g_matView._32 = -g_matView._32;
    g_matView._33 = -g_matView._33;
    g_matView._34 = -g_matView._34;
    TheseusSetTransform(D3DTS_VIEW, &g_matView);

    TheseusPushWorld();
    TheseusIdentityWorld();
    TheseusUpdateWorld();
    CGroup::Render();
    TheseusPopWorld();

    g_matProjection = savedProjection;
    g_matView = savedView;
    TheseusSetTransform(D3DTS_PROJECTION, &savedProjection);
    TheseusSetTransform(D3DTS_WORLD, &savedWorld);
    TheseusSetTransform(D3DTS_VIEW, &savedView);
}

// CLayer::Advance(nSeconds)
// Reconstructed from 5960 retail XBE @ 0x0005aaf0: CGroup::Advance, then the
// navigation info, then the viewpoint.
void CLayer::Advance(float nSeconds)
{
    CGroup::Advance(nSeconds);
    if (m_navigationInfo != NULL)
        m_navigationInfo->Advance(nSeconds);
    if (m_viewpoint != NULL)
        m_viewpoint->Advance(nSeconds);
}


// ===== CBackground ==========================================================
// The backdrop behind everything. The first one made becomes the bound one.

IMPLEMENT_NODE("Background", CBackground, CNode)

START_NODE_PROPS(CBackground, CNode)
    NODE_PROP(pt_vec3, CBackground, skyColor)
    NODE_PROP(pt_node, CBackground, backdrop)
    NODE_PROP(pt_boolean, CBackground, isBound)
END_NODE_PROPS()

// CBackground::CBackground()
// Reconstructed from 5960 retail XBE @ 0x0004fc90. Ghidra decompile:
//     skyColor = 0; backdrop = 0; isBound = 0; vb = 0; (+ the fade fields below)
//     if (g_background == 0) { g_background = this; isBound = 1; }
// 5960's Background also has fade, alpha, SetAlpha and DisappearAfter; Theseus
// doesn't carry them.
CBackground::CBackground() :
    m_backdrop(NULL),
    m_skyColor(0.0f, 0.0f, 0.0f),
    m_isBound(false),
    m_vb(NULL)
{
    if (TheseusGetBackground() == NULL)
    {
        TheseusSetBackground(this);
        m_isBound = true;
    }
}

// CBackground::~CBackground()
// Reconstructed from 5960 retail XBE @ 0x0004fd30: release the vertex buffer
// and the backdrop, and stop being the bound background.
CBackground::~CBackground()
{
    if (m_vb != NULL)
        m_vb->Release();
    if (m_backdrop != NULL)
        m_backdrop->Release();
    if (TheseusGetBackground() == this)
        TheseusSetBackground(NULL);
}

// CBackground::Advance(nSeconds)
// Reconstructed from 5960 retail XBE @ 0x0004fe30. CNode::Advance, the
// backdrop's Advance, then on first use a 4 vertex D3DVERTEX strip at
// z = -10000 covering the screen, uv (1,1) (0,1) (1,0) (0,0). The normals
// hold copies of the corner positions as the binary writes them (nz = 1).
// The binary steps its alpha fade first; Theseus has none.
void CBackground::Advance(float nSeconds)
{
    CNode::Advance(nSeconds);

    if (m_backdrop != NULL)
        m_backdrop->Advance(nSeconds);

    if (m_vb != NULL)
        return;

    if (FAILED(TheseusGetD3DDev()->CreateVertexBuffer(4 * sizeof(D3DVERTEX), D3DUSAGE_WRITEONLY,
            D3DFVF_VERTEX, D3DPOOL_MANAGED, &m_vb)))
        return;

    D3DVERTEX* v;
    if (FAILED(m_vb->Lock(0, 4 * sizeof(D3DVERTEX), (BYTE**)&v, 0)))
        return;

    float halfW = TheseusGetViewWidth() * 0.5f;
    float halfH = TheseusGetViewHeight() * 0.5f;

    // position x, y; normal x, y; uv
    const float corner[4][6] = {
        {  halfW, -halfH,  halfW, -halfH, 1.0f, 1.0f },
        { -halfW, -halfH,  halfW,  halfH, 0.0f, 1.0f },
        {  halfW,  halfH,  halfW,  halfH, 1.0f, 0.0f },
        { -halfW,  halfH, -halfW,  halfH, 0.0f, 0.0f },
    };
    for (int n = 0; n < 4; n++)
    {
        v[n].dvX = corner[n][0];
        v[n].dvY = corner[n][1];
        v[n].dvZ = -10000.0f;
        v[n].dvNX = corner[n][2];
        v[n].dvNY = corner[n][3];
        v[n].dvNZ = 1.0f;
        v[n].dvTU = corner[n][4];
        v[n].dvTV = corner[n][5];
    }

    m_vb->Unlock();
}

// CBackground::RenderBackdrop()
// Reconstructed from 5960 retail XBE @ 0x00050110. Takes the backdrop's
// texture (nothing to do without one), binds it to stage 0 and clears stage
// 1, saves projection, world and view, sets the ortho screen projection with
// identity world and view, and draws the strip from Advance with lighting,
// z writes and backface culling (CCW) saved, set and restored around it.
// Color comes from the texture. The binary's alpha also comes from a texture
// factor driven by Background's fade, with blending on; Theseus's Background
// has no fade, so it takes alpha from the texture and draws with blending off.
// The app only calls this when there's a backdrop.
void CBackground::RenderBackdrop()
{
    LPDIRECT3DTEXTURE8 tex = m_backdrop->GetTextureSurface();
    if (tex == NULL)
        return;

    TheseusSetTexture(0, tex);
    TheseusSetTexture(1, NULL);

    D3DXMATRIX outerProjection, outerWorld, outerView;
    TheseusGetTransform(D3DTS_PROJECTION, &outerProjection);
    TheseusGetTransform(D3DTS_WORLD, &outerWorld);
    TheseusGetTransform(D3DTS_VIEW, &outerView);

    D3DXMATRIX ortho;
    D3DXMatrixOrthoLH(&ortho, TheseusGetViewWidth(), TheseusGetViewHeight(), -10000.0f, 10000.0f);
    TheseusSetTransform(D3DTS_PROJECTION, &ortho);
    TheseusSetTransform(D3DTS_WORLD, &g_matIdentity);
    TheseusSetTransform(D3DTS_VIEW, &g_matIdentity);

    DWORD lighting, blend, cull, zwrite;
    TheseusGetRenderState(D3DRS_LIGHTING, &lighting);
    TheseusGetRenderState(D3DRS_ALPHABLENDENABLE, &blend);
    TheseusGetRenderState(D3DRS_CULLMODE, &cull);
    TheseusGetRenderState(D3DRS_ZWRITEENABLE, &zwrite);

    TheseusSetRenderState(D3DRS_LIGHTING, FALSE);
    TheseusSetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    TheseusSetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);
    TheseusSetRenderState(D3DRS_ZWRITEENABLE, FALSE);

    TheseusSetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    TheseusSetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    TheseusSetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    TheseusSetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    TheseusSetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    TheseusSetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);

    TheseusGetD3DDev()->SetStreamSource(0, m_vb, sizeof(D3DVERTEX));
    TheseusGetD3DDev()->SetVertexShader(D3DFVF_VERTEX);
    TheseusGetD3DDev()->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);

    TheseusSetRenderState(D3DRS_LIGHTING, lighting);
    TheseusSetRenderState(D3DRS_ALPHABLENDENABLE, blend);
    TheseusSetRenderState(D3DRS_CULLMODE, cull);
    TheseusSetRenderState(D3DRS_ZWRITEENABLE, zwrite);

    TheseusSetTransform(D3DTS_PROJECTION, &outerProjection);
    TheseusSetTransform(D3DTS_WORLD, &outerWorld);
    TheseusSetTransform(D3DTS_VIEW, &outerView);
}

// CBackground::Bind()
// Inlined into the ctor and OnSetProperty in the binary: unbind whoever is
// bound, then bind this one.
void CBackground::Bind()
{
    CBackground* bound = TheseusGetBackground();
    if (bound != NULL)
        bound->m_isBound = false;
    TheseusSetBackground(this);
    m_isBound = true;
}

// CBackground::OnSetProperty(pprd, pvValue)
// Reconstructed from 5960 retail XBE @ 0x00050450. Ghidra decompile:
//     if (prd->offset == 0x2c) { if (*value) { Bind inlined } }
//     else if (prd->offset == 0x48) { SetAlpha inlined; return 0; }
//     return 1;
// Setting isBound true binds; false does nothing. Theseus has no alpha.
bool CBackground::OnSetProperty(const PRD* pprd, const void* pvValue)
{
    if (PTR2INT(pprd->pbOffset) == offsetof(m_isBound) && *(const bool*)pvValue)
        Bind();
    return true;
}


// ===== CLevel ===============================================================
// One place in the dashboard (main menu, memory, music, settings...). Going
// to a level cross fades from the current one over a second, through the
// tunnel and camera path between them.

CLevel* g_pCurLevel;
CLevel* g_pFromLevel;
CNode* g_pCurTunnel;
XTIME g_timeToNextLevel;
bool g_bLevelTransition;

IMPLEMENT_NODE("Level", CLevel, CGroup)

START_NODE_PROPS(CLevel, CGroup)
    NODE_PROP(pt_node, CLevel, control)
    NODE_PROP(pt_node, CLevel, tunnel)
    NODE_PROP(pt_node, CLevel, path)
    NODE_PROP(pt_node, CLevel, shell)
    NODE_PROP(pt_string, CLevel, archive)
    NODE_PROP(pt_boolean, CLevel, unloadable)
    NODE_PROP(pt_boolean, CLevel, fade)
END_NODE_PROPS()

#define _FND_CLASS CLevel
START_NODE_FUN(CLevel, CGroup)
    NODE_FUN_VV(GoTo)
    NODE_FUN_VV(GoBackTo)
END_NODE_FUN()
#undef _FND_CLASS

// CLevel::CLevel()
// Reconstructed from 5960 retail XBE @ 0x0005cc20. Ghidra decompile:
//     g_timeToNextLevel = 0.0;
//     control = tunnel = path = shell = archive = 0; unloadable = 1; fade = 1;
//     visible = 0; xip = 0; arrive = 0;
// Every new level resets the shared transition clock.
CLevel::CLevel() :
    m_control(NULL),
    m_tunnel(NULL),
    m_path(NULL),
    m_shell(NULL),
    m_archive(NULL),
    m_unloadable(true),
    m_fade(true),
    m_xipFile(NULL),
    m_bArrive(false)
{
    g_timeToNextLevel = 0.0;
    m_visible = false;
}

// CLevel::~CLevel()
// Reconstructed from 5960 retail XBE @ 0x0005ccd0: drop out of the current,
// from and tunnel globals, release tunnel, control, path and shell, free the
// archive name.
CLevel::~CLevel()
{
    if (g_pCurLevel == this)
        g_pCurLevel = NULL;
    if (g_pFromLevel == this)
        g_pFromLevel = NULL;
    if (g_pCurTunnel == m_tunnel)
        g_pCurTunnel = NULL;

    if (m_tunnel != NULL)
        m_tunnel->Release();
    if (m_control != NULL)
        m_control->Release();
    if (m_path != NULL)
        m_path->Release();
    if (m_shell != NULL)
        m_shell->Release();

    delete [] m_archive;
}

// CLevel::Render()
// Reconstructed from 5960 retail XBE @ 0x0005cd80. Ghidra decompile:
//     a = 1;
//     if (fade) { t = clamp((g_timeToNextLevel - now) * 1.3333334, 0, 1);
//         if (this == g_pCurLevel) a = 1 - t; else if (this == g_pFromLevel) a = t;
//         if (a == 0) return; }    // only reached for cur or from
//     effectAlpha *= a; if (shell) shell->Render();
//     if (this == g_pCurLevel) g_motionBlur = min(a * 1.3, 1);
//     CGroup::Render(); effectAlpha restored;
// The 1.3333334 is 1 / 0.75: the cross fade runs over the last 0.75 s.
void CLevel::Render()
{
    float a = 1.0f;

    if (m_fade)
    {
        float t = (float)(g_timeToNextLevel - TheseusGetNow()) / 0.75f;
        if (t > 1.0f)
            t = 1.0f;
        else if (t < 0.0f)
            t = 0.0f;

        if (this == g_pCurLevel)
            a = 1.0f - t;
        else if (this == g_pFromLevel)
            a = t;

        if (a == 0.0f)
            return;
    }

    float outerAlpha = g_nEffectAlpha;
    g_nEffectAlpha = outerAlpha * a;

    if (m_shell != NULL)
        m_shell->Render();

    if (this == g_pCurLevel)
        g_transitionMotionBlur = a * 1.3f > 1.0f ? 1.0f : a * 1.3f;

    CGroup::Render();

    g_nEffectAlpha = outerAlpha;
}

// CLevel::Activate()
// Reconstructed from 5960 retail XBE @ 0x0005ce90. Ghidra decompile:
//     g_bLevelTransition = 1;
//     if (archive && !xip) xip = LoadXIP(archive);
//     if (g_pFromLevel) { from->Deactivate() inlined; g_timeToNextLevel = 0; g_pFromLevel = 0; }
//     else if (!g_pCurLevel || g_timeToNextLevel == 0) goto start;
//     if (g_pCurLevel) { cur->visible = 0;
//         if (cur->xip && cur->unloadable && !cur->xip->locked) cur->xip->DeleteMeshBuffers(); }
//     g_pCurLevel = 0;
//     if (g_pCurTunnel) { g_pCurTunnel->visible = 0; g_pCurTunnel = 0; }
//   start:
//     BindJoystick(0) inlined; g_timeToNextLevel = now + 1.0;
//     g_pFromLevel = g_pCurLevel; visible = 1; g_pCurLevel = this;
// A transition still running is cut short: both its ends are hidden and the
// new one fades in from nothing.
void CLevel::Activate()
{
    g_bLevelTransition = true;

    if (m_archive != NULL && m_xipFile == NULL)
        m_xipFile = LoadXIP(m_archive);

    bool midTransition = g_pFromLevel != NULL || (g_pCurLevel != NULL && g_timeToNextLevel != 0.0);
    if (midTransition)
    {
        if (g_pFromLevel != NULL)
        {
            g_pFromLevel->Deactivate();
            g_pFromLevel = NULL;
        }

        if (g_pCurLevel != NULL)
        {
            CLevel* cur = g_pCurLevel;
            cur->m_visible = false;
            if (cur->m_xipFile != NULL && cur->m_unloadable && !cur->m_xipFile->m_locked)
                cur->m_xipFile->DeleteMeshBuffers();
            g_pCurLevel = NULL;
        }

        if (g_pCurTunnel != NULL)
        {
            g_pCurTunnel->m_visible = false;
            g_pCurTunnel = NULL;
        }
    }

    BindJoystick(NULL);
    g_timeToNextLevel = TheseusGetNow() + 1.0;
    g_pFromLevel = g_pCurLevel;
    m_visible = true;
    g_pCurLevel = this;
}

// CLevel::GoTo()   [script fn 'GoTo', FND 0x00188478, sig 1]
// Reconstructed from 5960 retail XBE @ 0x0005cf90. Ghidra decompile:
//     if (this == g_pCurLevel) return;
//     Activate(); g_pCurTunnel = tunnel; if (tunnel) tunnel->visible = 1;
//     if (path is a Viewpoint) BindViewpoint(path);
//     else if (path is a CameraPath) { active = 1; start = now; backward = 0; }
//     CallFunction(this, "OnActivate");
// The CameraPath start is its Activate(false), inlined.
void CLevel::GoTo()
{
    if (this == g_pCurLevel)
        return;

    Activate();

    g_pCurTunnel = m_tunnel;
    if (g_pCurTunnel != NULL)
        g_pCurTunnel->m_visible = true;

    if (m_path != NULL)
    {
        if (m_path->IsKindOf(NODE_CLASS(CViewpoint)))
            BindViewpoint(m_path);
        else if (m_path->IsKindOf(NODE_CLASS(CCameraPath)))
            ((CCameraPath*)m_path)->Activate(false);
    }

    CallFunction(this, _T("OnActivate"));
}

// CLevel::GoBackTo()   [script fn 'GoBackTo', FND 0x00188490, sig 1]
// Reconstructed from 5960 retail XBE @ 0x0005d040. Ghidra decompile:
//     if (this == g_pCurLevel) return;
//     Activate(); g_pCurTunnel = from ? from->tunnel : 0;
//     if (g_pCurTunnel) g_pCurTunnel->visible = 1;
//     if (from && from->path is a CameraPath) { path: active = 1; start = now; backward = 1; }
//     else if (path is a Viewpoint) BindViewpoint(path);
//     CallFunction(this, "OnActivate");
// Going back runs the level we're leaving's tunnel and path in reverse.
void CLevel::GoBackTo()
{
    if (this == g_pCurLevel)
        return;

    Activate();

    CLevel* from = g_pFromLevel;
    g_pCurTunnel = from != NULL ? from->m_tunnel : NULL;
    if (g_pCurTunnel != NULL)
        g_pCurTunnel->m_visible = true;

    if (from != NULL && from->m_path != NULL && from->m_path->IsKindOf(NODE_CLASS(CCameraPath)))
        ((CCameraPath*)from->m_path)->Activate(true);
    else if (m_path != NULL && m_path->IsKindOf(NODE_CLASS(CViewpoint)))
        BindViewpoint(m_path);

    CallFunction(this, _T("OnActivate"));
}

// CLevel::Advance(nSeconds)
// Reconstructed from 5960 retail XBE @ 0x0005d120. Ghidra decompile:
//     CGroup::Advance(dt); tunnel, path, control, shell ->Advance(dt) when set;
//     if (this == g_pCurLevel && g_timeToNextLevel != 0 && g_timeToNextLevel <= now) {
//         g_timeToNextLevel = 0; if (g_pCurTunnel) g_pCurTunnel->visible = 0;
//         if (g_pFromLevel) { g_pFromLevel->Deactivate(); g_pFromLevel = 0; }
//         arrive = 1; }
//     if (arrive && (!xip || xip->loaded)) { arrive = 0; BindJoystick(control);
//         CallFunction(this, "OnArrival"); g_bLevelTransition = 0; }
// Arrival waits for the level's XIP before handing over the controller.
void CLevel::Advance(float nSeconds)
{
    CGroup::Advance(nSeconds);

    if (m_tunnel != NULL)
        m_tunnel->Advance(nSeconds);
    if (m_path != NULL)
        m_path->Advance(nSeconds);
    if (m_control != NULL)
        m_control->Advance(nSeconds);
    if (m_shell != NULL)
        m_shell->Advance(nSeconds);

    if (this == g_pCurLevel && g_timeToNextLevel != 0.0 && g_timeToNextLevel <= TheseusGetNow())
    {
        g_timeToNextLevel = 0.0;
        if (g_pCurTunnel != NULL)
            g_pCurTunnel->m_visible = false;
        if (g_pFromLevel != NULL)
        {
            g_pFromLevel->Deactivate();
            g_pFromLevel = NULL;
        }
        m_bArrive = true;
    }

    if (m_bArrive && (m_xipFile == NULL || m_xipFile->m_loaded))
    {
        m_bArrive = false;
        BindJoystick(m_control);
        CallFunction(this, _T("OnArrival"));
        g_bLevelTransition = false;
    }
}

// CLevel::Deactivate()
// Reconstructed from 5960 retail XBE @ 0x0005d200:
//     visible = 0; CallFunction(this, "OnDeactivate");
//     if (unloadable && !xip->locked) xip->DeleteMeshBuffers();
// The binary reads the XIP without checking for one; we skip levels with no archive.
void CLevel::Deactivate()
{
    m_visible = false;
    CallFunction(this, _T("OnDeactivate"));

    if (m_unloadable && m_xipFile != NULL && !m_xipFile->m_locked)
        m_xipFile->DeleteMeshBuffers();
}


#ifdef THESEUS_DESKTOP
// ===== XAP live-editor helpers (desktop only) ===============================
// CInline reload + Inline-node enumeration for the desktop XAP editor's
// "save -> hot reload" loop. Touch CInline's privates so they live in this
// TU. Xbox build doesn't ship the editor.

#include "runner.h"      // CMember, CDefine
#include "xap_editor.h"  // InlineInfo struct

static void CollectInlinesRecursive(CNode* pNode, CInstance* pRoot, InlineInfo* out, int* count, int max, int depth = 0) {
    if (!pNode || depth > 16 || *count >= max) return;

    CNodeClass* nc = pNode->GetNodeClass();
    if (nc && nc->m_className && strcmp(nc->m_className, "Inline") == 0) {
        CInline* inl = (CInline*)pNode;
        if (inl->m_url && inl->m_url[0]) {
            out[*count].node = pNode;
            out[*count].url = inl->m_url;

            out[*count].defName = NULL;
            if (pRoot && pRoot->m_class && pRoot->m_class->m_members) {
                for (CDefine* d = pRoot->m_class->m_members->m_firstDefine; d; d = d->m_next) {
                    if (d->m_node && d->m_node->m_obj == objMember) {
                        CMember* mem = (CMember*)d->m_node;
                        int idx = mem->m_memberIndex;
                        if (idx >= 0 && idx < pRoot->m_vars.GetLength()) {
                            if (pRoot->m_vars.GetNode(idx) == pNode)
                                out[*count].defName = d->m_name;
                        }
                    }
                }
            }
            (*count)++;
        }
    }

    CGroup* grp = dynamic_cast<CGroup*>(pNode);
    if (grp) {
        for (int i = 0; i < grp->m_children.GetLength(); i++) {
            CNode* child = grp->m_children.GetNode(i);
            if (child) CollectInlinesRecursive(child, pRoot, out, count, max, depth + 1);
        }
    }

    CInstance* inst = dynamic_cast<CInstance*>(pNode);
    if (inst) {
        for (int i = 0; i < inst->m_vars.GetLength(); i++) {
            CNode* varNode = inst->m_vars.GetNode(i);
            if (varNode && varNode != pNode)
                CollectInlinesRecursive(varNode, pRoot, out, count, max, depth + 1);
        }
    }
}

extern "C" int CollectInlineNodes(CNode* pRoot, CInstance* pRootInst, void* outBuf, int maxEntries) {
    int count = 0;
    CollectInlinesRecursive(pRoot, pRootInst, (InlineInfo*)outBuf, &count, maxEntries);
    return count;
}

bool CInline::ReloadFromText(const char* xapText) {
    if (!m_url)
        return false;

    m_children.ReleaseAll();

    m_class.~CClass();
    new (&m_class) CClass;

    char absURL[MAX_PATH];
    MakeAbsoluteURL(absURL, m_url);

    int cch = (int)strlen(absURL) + 1;
    m_class.m_url = new char[cch];
    memcpy(m_class.m_url, absURL, cch * sizeof(char));

    extern bool g_bParseError;
    g_bParseError = false;

    size_t len = strlen(xapText);
    char* parseBuf = (char*)malloc(len + 1);
    memcpy(parseBuf, xapText, len + 1);

    bool ok = m_class.ParseFile(absURL, (const char*)parseBuf);
    free(parseBuf);

    if (!ok || g_bParseError)
        return false;

    CNode* pNode = m_class.CreateNode();
    if (!pNode)
        return false;

    CallFunction(pNode, "initialize");
    m_children.AddNode(pNode);
    pNode->m_parent = this;
    CallFunction(this, "onLoad");

    m_bClassLoaded = true;
    m_bLoading = false;
    m_timeOfLoad = TheseusGetNow();

    return true;
}

extern "C" bool ReloadInlineNode(CNode* pInlineNode, const char* xapText) {
    CNodeClass* nc = pInlineNode->GetNodeClass();
    if (!nc || !nc->m_className || strcmp(nc->m_className, "Inline") != 0)
        return false;
    return ((CInline*)pInlineNode)->ReloadFromText(xapText);
}
#endif // THESEUS_DESKTOP
