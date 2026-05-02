// scene_groups.cpp: scene-graph container nodes (CGroup, CTransform,
// CInline, CSpinner, CWaver, CLayout, CSwitch, CBillboard, CLayer,
// CBackground, CLevel). All confirmed present in the 5960 retail XBE via
// class-registration strings and FND table entries; see
// docs/decomp/SceneGroups.md.

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
// Base container node. Holds an array of children, renders them in order.
// Computes bounding box as union of child boxes.

IMPLEMENT_NODE("Group", CGroup, CNode)

START_NODE_PROPS(CGroup, CNode)
    NODE_PROP(pt_children, CGroup, children)
END_NODE_PROPS()

CGroup::CGroup()
{
    m_bboxCenter.x = 0.0f;
    m_bboxCenter.y = 0.0f;
    m_bboxCenter.z = 0.0f;
    m_bboxSize.x = -1.0f;
    m_bboxSize.y = -1.0f;
    m_bboxSize.z = -1.0f;
    m_bboxSpecified = true;
    m_bboxDirty = true;
}

CGroup::~CGroup()
{
}

void CGroup::GetBBox(BBox* pBBox)
{
    if (m_bboxSize.x == -1.0f && m_bboxSize.y == -1.0f && m_bboxSize.z == -1.0f)
        m_bboxSpecified = false;

    if (m_bboxDirty && !m_bboxSpecified)
    {
        int nChildCount = m_children.GetLength();
        if (nChildCount == 0)
        {
            m_bboxCenter.x = 0.0f;
            m_bboxCenter.y = 0.0f;
            m_bboxCenter.z = 0.0f;
            m_bboxSize.x = 0.0f;
            m_bboxSize.y = 0.0f;
            m_bboxSize.z = 0.0f;
        }
        else
        {
            BBox bbox;
            m_children.GetNode(0)->GetBBox(&bbox);

            float xMin = bbox.center.x - bbox.size.x;
            float yMin = bbox.center.y - bbox.size.y;
            float zMin = bbox.center.z - bbox.size.z;
            float xMax = bbox.center.x + bbox.size.x;
            float yMax = bbox.center.y + bbox.size.y;
            float zMax = bbox.center.z + bbox.size.z;

            int i;
            for (i = 1; i < nChildCount; i += 1)
            {
                m_children.GetNode(i)->GetBBox(&bbox);

                if (xMin > bbox.center.x - bbox.size.x)
                    xMin = bbox.center.x - bbox.size.x;

                if (yMin > bbox.center.y - bbox.size.y)
                    yMin = bbox.center.y - bbox.size.y;

                if (zMin > bbox.center.z - bbox.size.z)
                    zMin = bbox.center.z - bbox.size.z;

                if (xMax > bbox.center.x + bbox.size.x)
                    xMax = bbox.center.x + bbox.size.x;

                if (yMax > bbox.center.y + bbox.size.y)
                    yMax = bbox.center.y + bbox.size.y;

                if (zMax > bbox.center.z + bbox.size.z)
                    zMax = bbox.center.z + bbox.size.z;
            }

            m_bboxCenter.x = (xMax + xMin) / 2.0f;
            m_bboxCenter.y = (yMax + yMin) / 2.0f;
            m_bboxCenter.z = (zMax + zMin) / 2.0f;

            m_bboxSize.x = xMax - xMin;
            m_bboxSize.y = yMax - yMin;
            m_bboxSize.z = zMax - zMin;
        }
    }

    pBBox->center = m_bboxCenter;
    pBBox->size = m_bboxSize;
}

float CGroup::GetRadius()
{
    float radius = 0.0f;
    int nChildCount = m_children.GetLength();

    int i;
    for (i = 0; i < nChildCount; i += 1)
    {
        float r = m_children.GetNode(i)->GetRadius();
        if (radius < r)
            radius = r;
    }

    return radius;
}

void CGroup::Render()
{
    extern int g_nodeVisitsThisFrame;
    extern int g_nodeSkipsThisFrame;

    int nChildCount = m_children.GetLength();

    int i;
    for (i = 0; i < nChildCount; i += 1)
    {
        CNode* pNode = m_children.GetNode(i);
        g_nodeVisitsThisFrame++;
        if (pNode->m_visible)
            pNode->Render();
        else
            g_nodeSkipsThisFrame++;
    }
}

void CGroup::Advance(float nSeconds)
{
    CNode::Advance(nSeconds);
    int nChildCount = m_children.GetLength();

    int i;
    for (i = 0; i < nChildCount; i += 1)
        m_children.GetNode(i)->Advance(nSeconds);
}

void CGroup::SetLight(int& nLight, D3DCOLORVALUE& ambient)
{
    int nChildCount = m_children.GetLength();

    int i;
    for (i = 0; i < nChildCount; i += 1)
    {
        CNode* pNode = m_children.GetNode(i);
        if (pNode != NULL)
            pNode->SetLight(nLight, ambient);
    }
}

void CGroup::RenderDynamicTexture(CSurfx* pSurfx)
{
    int nChildCount = m_children.GetLength();

    int i;
    for (i = 0; i < nChildCount; i += 1)
    {
        CNode* pNode = m_children.GetNode(i);
        if (pNode != NULL)
            pNode->RenderDynamicTexture(pSurfx);
    }
}

LPDIRECT3DTEXTURE8 CGroup::GetTextureSurface()
{
    int nChildCount = m_children.GetLength();

    for (int i = 0; i < nChildCount; i += 1)
    {
        CNode* pNode = m_children.GetNode(i);
        if (pNode != NULL)
        {
            LPDIRECT3DTEXTURE8 pSurface = pNode->GetTextureSurface();
            if (pSurface != NULL)
                return pSurface;
        }
    }

    return NULL;
}


// ===== CTransform ===========================================================
// Applies translation, rotation, scale to children with optional fade
// interpolation. Supports animated property transitions via SetScale,
// SetTranslation, SetRotation, SetAlpha.

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

CTransform::CTransform() :
    m_center(0.0f, 0.0f, 0.0f),
    m_scaleOrientation(0.0f, 0.0f, 1.0f, 0.0f),
    m_scale(1.0f, 1.0f, 1.0f),
    m_rotation(0.0f, 0.0f, 1.0f, 0.0f),
    m_translation(0.0f, 0.0f, 0.0f),
    m_fade(0.0f),
    m_moving(false)
{
    m_dirty = true;
    m_alpha = 1.0f;
    m_timeCenterStart = 0.0f;
    m_timeScaleStart = 0.0f;
    m_timeScaleOrientationStart = 0.0f;
    m_timeRotationStart = 0.0f;
    m_timeTranslationStart = 0.0f;
    m_timeAlphaStart = 0.0f;
    m_timeToDisappear = 0.0;
}

void CTransform::OnLoad()
{
    CGroup::OnLoad();
    D3DXQuaternionRotationAxis(&m_rotationQuat, (D3DXVECTOR3*)&m_rotation, m_rotation.w);
}

bool CTransform::OnSetProperty(const PRD* pprd, const void* pvValue)
{
    if (PTR2INT(pprd->pbOffset) == offsetof(m_rotation))
    {
        const float* p = (const float*)pvValue;
        D3DXQuaternionRotationAxis(&m_rotationQuat, (D3DXVECTOR3*)p, p[3]);
    }
    else if (PTR2INT(pprd->pbOffset) == offsetof(m_alpha))
    {
        SetAlpha(*(float*)pvValue);
        return false;
    }

    return CGroup::OnSetProperty(pprd, pvValue);
}

void CTransform::Advance(float nSeconds)
{
    CGroup::Advance(nSeconds);

    if (m_timeAlphaStart > 0.0f)
    {
        float t = (float) (TheseusGetNow() - m_timeAlphaStart) / m_fade;
        if (t >= 1.0f)
        {
            t = 1.0f;
            m_timeAlphaStart = 0.0f;
        }

        m_alpha = m_alphaStart + (m_alphaEnd - m_alphaStart) * t;
    }

    if (m_timeToDisappear > 0.0f && TheseusGetNow() >= m_timeToDisappear)
    {
        m_alpha = 0.0f;
        m_timeToDisappear = 0.0f;
    }
}

void CTransform::CalcMatrix()
{
    D3DXMatrixIdentity(&m_matrix);

    if (m_timeScaleStart > 0.0f)
    {
        m_moving = true;

        float t = (float) (TheseusGetNow() - m_timeScaleStart) / m_fade;
        if (t >= 1.0f)
        {
            t = 1.0f;
            m_timeScaleStart = 0.0f;
        }

        D3DXVec3Lerp(&m_scale, &m_scaleStart, &m_scaleEnd, t);
    }

    if (m_timeTranslationStart > 0.0f)
    {
        m_moving = true;

        float t = (float) (TheseusGetNow() - m_timeTranslationStart) / m_fade;
        if (t >= 1.0f)
        {
            t = 1.0f;
            m_timeTranslationStart = 0.0f;
        }

        D3DXVec3Lerp(&m_translation, &m_translationStart, &m_translationEnd, t);
    }

    if (m_timeRotationStart > 0.0f)
    {
        m_moving = true;

        float t = (float) (TheseusGetNow() - m_timeRotationStart) / m_fade;
        if (t >= 1.0f)
        {
            t = 1.0f;
            m_timeRotationStart = 0.0f;
        }

        D3DXQuaternionSlerp(&m_rotationQuat, &m_rotationStart, &m_rotationEnd, t);
    }

    if (m_scale.x != 1.0f || m_scale.y != 1.0f || m_scale.z != 1.0f)
    {
        D3DXMATRIX mat;
        D3DXMatrixScaling(&mat, m_scale.x, m_scale.y, m_scale.z);
        D3DXMatrixMultiply(&m_matrix, &m_matrix, &mat);
    }

    if (m_rotationQuat.w != 0.0f)
    {
        D3DXMATRIX mat;
        D3DXMatrixRotationQuaternion(&mat, &m_rotationQuat);
        D3DXMatrixMultiply(&m_matrix, &m_matrix, &mat);
    }

    if (m_translation.x != 0.0f || m_translation.y != 0.0f || m_translation.z != 0.0f)
    {
        D3DXMATRIX mat;
        D3DXMatrixTranslation(&mat, m_translation.x, m_translation.y, m_translation.z);
        D3DXMatrixMultiply(&m_matrix, &m_matrix, &mat);
    }

    m_dirty = (m_timeScaleStart + m_timeTranslationStart + m_timeRotationStart) > 0.0f;
}

void CTransform::SetScale(float sx, float sy, float sz)
{
    if (m_fade > 0.0f)
    {
        m_timeScaleStart = TheseusGetNow();
        m_scaleStart = m_scale;
        m_scaleEnd.x = sx;
        m_scaleEnd.y = sy;
        m_scaleEnd.z = sz;
        m_moving = true;
    }
    else
    {
        m_scale.x = sx;
        m_scale.y = sy;
        m_scale.z = sz;
    }

    m_dirty = true;
}

void CTransform::SetScaleOrientation(float x, float y, float z, float a)
{
    m_scaleOrientation.x = x;
    m_scaleOrientation.y = y;
    m_scaleOrientation.z = z;
    m_scaleOrientation.w = a;
    m_dirty = true;
}

void CTransform::SetTranslation(float x, float y, float z)
{
    if (m_fade > 0.0f)
    {
        m_timeTranslationStart = TheseusGetNow();
        m_translationStart = m_translation;
        m_translationEnd.x = x;
        m_translationEnd.y = y;
        m_translationEnd.z = z;
        m_moving = true;
    }
    else
    {
        m_translation.x = x;
        m_translation.y = y;
        m_translation.z = z;
    }

    m_dirty = true;
}

void CTransform::SetCenter(float x, float y, float z)
{
    m_center.x = x;
    m_center.y = y;
    m_center.z = z;
    m_dirty = true;
}

void CTransform::SetRotation(float x, float y, float z, float a)
{
    D3DXVECTOR3 v(x, y, z);
    D3DXQUATERNION q;
    D3DXQuaternionRotationAxis(&q, &v, a);

    if (m_fade > 0.0f)
    {
        m_timeRotationStart = TheseusGetNow();
        m_rotationStart = m_rotationQuat;
        m_rotationEnd = q;
        m_moving = true;
    }
    else
    {
        m_rotation.x = x;
        m_rotation.y = y;
        m_rotation.z = z;
        m_rotation.w = a;
        m_rotationQuat = q;
    }

    m_dirty = true;
}

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

void CTransform::DisappearAfter(float t)
{
    m_timeToDisappear = TheseusGetNow() + t;
}

void CTransform::Render()
{
    m_moving = false;

    if (m_dirty)
        CalcMatrix();

    TheseusPushWorld();
    TheseusMultWorld(&m_matrix);
    TheseusUpdateWorld();

    float nEffectAlphaSave = g_nEffectAlpha;
    g_nEffectAlpha *= m_alpha;

    if (m_alpha > 0.0f)
    {
        CGroup::Render();
    }
    else
    {
        // Whole subtree skipped due to zero alpha. Counts as one
        // skip event regardless of subtree size since we don't
        // descend to know how big it is.
        extern int g_nodeSkipsThisFrame;
        g_nodeSkipsThisFrame++;
    }

    g_nEffectAlpha = nEffectAlphaSave;

    TheseusPopWorld();
}


// ===== CInline ==============================================================
// Loads a child scene from a URL (with optional XIP archive).
// Supports background loading and fade-in on delay-loaded content.

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

#ifdef UIX_DESKTOP
    // Hot-reload from new XAP source text (XAP editor "save" loop)
    bool ReloadFromText(const char* xapText);
#endif

private:
    HANDLE m_hClassLoaderThread;
    TCHAR m_szAbsURL[MAX_PATH];
    static void WINAPI ClassLoaderThread(CInline* p);
};

IMPLEMENT_NODE("Inline", CInline, CGroup)

START_NODE_PROPS(CInline, CGroup)
    NODE_PROP(pt_string, CInline, url)
    NODE_PROP(pt_boolean, CInline, preload)
    NODE_PROP(pt_boolean, CInline, fadeInDelayLoad)
END_NODE_PROPS()

CInline::CInline() :
    m_url(NULL),
    m_hClassLoaderThread(NULL),
    m_bClassLoaded(false),
    m_fadeInDelayLoad(true),
    m_preload(false)
{
    m_dirty = true;
    m_bLoading = false;
    m_bFirstLoad = false;
    m_xipFile = NULL;
    m_timeOfLoad = 0.0f;
}

CInline::~CInline()
{
    delete [] m_url;
}

void CInline::Init()
{
    ASSERT(m_dirty);
    m_dirty = false;
    m_children.ReleaseAll();

    if (m_url == NULL)
        return;

    ASSERT(!m_bLoading);

    // Start background XIP load if there is an archive for this URL
    {
        TCHAR szURL [MAX_PATH];

        MakeAbsoluteURL(szURL, m_url);
        TCHAR* pch = _tcsrchr(szURL, '/');
        if (pch != NULL)
        {
            _tcscpy(pch, _T(".xip"));

            m_bLoading = true;
            m_bFirstLoad = true;
            m_xipFile = LoadXIP(szURL);
            if (m_xipFile != NULL)
                return;

            TRACE(_T("\001LoadXIP: %s failed!\n"));

            m_bLoading = false;
        }
    }

    FinishLoad();
}

void CInline::FinishLoad()
{
    TRACE(_T("FinishLoad: (0x%08x) %s\n"), this, m_url);
    ASSERT(!m_bClassLoaded);

    if (m_class.LoadAbsURL(m_szAbsURL))
    {
        CNode* pNode = m_class.CreateNode();
        CallFunction(pNode, _T("initialize"));

        m_children.AddNode(pNode);
        pNode->m_parent = this;
        CallFunction(this, _T("onLoad"));
    }
    m_bFirstLoad = false;
    m_bClassLoaded = true;
}

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

    float a = (float) (TheseusGetNow() - m_timeOfLoad) / 2.0f;

    if (!m_fadeInDelayLoad)
    {
        a = 1.0f;
    }
    else if (a > 1.0f)
    {
        a = 1.0f;
    }

    float nEffectAlphaSave = g_nEffectAlpha;
    g_nEffectAlpha *= a;

    CDirPush dirPush(m_url);

    CGroup::Render();

    g_nEffectAlpha = nEffectAlphaSave;
}

void WINAPI CInline::ClassLoaderThread(CInline* p)
{
    ASSERT(p->m_xipFile);
    ASSERT(p->m_bFirstLoad);
    ASSERT(!p->m_bLoading);
    ASSERT(!p->m_bClassLoaded);
    ASSERT(p->m_xipFile->IsReady());
    p->FinishLoad();
}

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
        ASSERT(m_xipFile != NULL);
        if (!m_xipFile->IsReady())
            return;

        m_bLoading = false;

        if (m_bFirstLoad)
        {
            ASSERT(m_hClassLoaderThread == NULL);

            MakeAbsoluteURL(m_szAbsURL, m_url);

            // Class loader can't run in a separate thread; the scripting
            // engine is not thread-safe.
            if (!m_hClassLoaderThread)
            {
                FinishLoad();
            }
        }

        m_timeOfLoad = TheseusGetNow();
    }

    if (m_hClassLoaderThread)
    {
        if (WaitForSingleObject(m_hClassLoaderThread, 0) == WAIT_OBJECT_0)
        {
            ASSERT(m_bClassLoaded);
            CloseHandle(m_hClassLoaderThread);
            m_hClassLoaderThread = NULL;
        }
    }

    CGroup::Advance(nSeconds);
}


// ===== CSpinner =============================================================
// Continuously rotates children around an axis at a given RPM.

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

CSpinner::CSpinner()
{
    m_rpm = 1.0f;
    m_axis.x = 0.0f;
    m_axis.y = 1.0f;
    m_axis.z = 0.0f;
    m_angle = 0.0f;
}

void CSpinner::Render()
{
    TheseusPushWorld();

    D3DXMATRIX matrix;
    D3DXMatrixRotationAxis(&matrix, &m_axis, m_angle);

    TheseusMultWorld(&matrix);
    TheseusUpdateWorld();

    CGroup::Render();

    TheseusPopWorld();
}

void CSpinner::Advance(float nSeconds)
{
    CGroup::Advance(nSeconds);

    m_angle += ((m_rpm / 60.0f) * nSeconds) * (2.0f * D3DX_PI);

    while (m_angle > 2.0f * D3DX_PI)
        m_angle -= 2.0f * D3DX_PI;
    while (m_angle < -2.0f * D3DX_PI)
        m_angle += 2.0f * D3DX_PI;
}


// ===== CWaver ===============================================================
// Oscillates children sinusoidally around an axis. Extends CSpinner but
// overrides Advance to use sin() instead of linear accumulation.

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

CWaver::CWaver()
{
    m_field = D3DX_PI / 4.0f;
}

void CWaver::Advance(float nSeconds)
{
    CGroup::Advance(nSeconds); // Intentionally skips CSpinner::Advance
    m_angle = sinf((float) (TheseusGetNow() * D3DX_PI * m_rpm / 60.0f)) * m_field / 2.0f;
}


// ===== CLayout ==============================================================
// Arranges children in a line along a direction vector, or in a circle
// if direction is zero.

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

CLayout::CLayout() :
    m_direction(1.0f, 0.0f, 0.0f),
    m_spacing(1.0f)
{
}

void CLayout::Render()
{
    extern int g_nodeVisitsThisFrame;
    extern int g_nodeSkipsThisFrame;

    int nChildren = m_children.GetLength();
    if (nChildren == 0)
        return;

    if (m_direction.x == 0 && m_direction.y == 0 && m_direction.z == 0)
    {
        // Circular layout
        D3DXMATRIX mat = *TheseusGetWorld();

        float r = (nChildren / 4) * m_spacing;

        int i;
        for (i = 0; i < nChildren; i += 1)
        {
            TheseusPushWorld();

            CNode* pNode = m_children.GetNode(i);
            g_nodeVisitsThisFrame++;
            if (!pNode->m_visible)
            {
                g_nodeSkipsThisFrame++;
                continue;
            }

            float a = D3DX_PI * 2.0f * i / nChildren;
            float x = r * cosf(-a);
            float z = r * sinf(-a);

            D3DXVECTOR3 axis(0.0f, 0.1f, 0.0f);
            TheseusTranslateWorld(x, 0.0f, z);
            TheseusRotateWorld(&axis, a + D3DX_PI / 2.0f);
            TheseusUpdateWorld();

            pNode->Render();

            TheseusPopWorld();
        }

        return;
    }

    // Linear layout
    float spacing = m_spacing;

    TheseusPushWorld();

    D3DXVECTOR3 v;
    D3DXVec3Scale(&v, &m_direction, -((float)nChildren + spacing) / 2);
    TheseusTranslateWorld(v.x, v.y, v.z);

    int i;
    for (i = 0; i < nChildren; i += 1)
    {
        CNode* pNode = m_children.GetNode(i);
        g_nodeVisitsThisFrame++;
        if (!pNode->m_visible)
        {
            g_nodeSkipsThisFrame++;
            continue;
        }

        D3DXVec3Scale(&v, &m_direction, 0.5f);

        TheseusTranslateWorld(v.x, v.y, v.z);

        TheseusUpdateWorld();
        pNode->Render();

        if (i < nChildren - 1)
        {
            D3DXVec3Scale(&v, &m_direction, 0.5f + spacing);
            TheseusTranslateWorld(v.x, v.y, v.z);
        }
    }

    TheseusPopWorld();
}

void CLayout::GetBBox(BBox* pBBox)
{
    if (m_children.GetLength() == 0)
    {
        CNode::GetBBox(pBBox);
        return;
    }

    float spacing = m_spacing + 1.0f;
    m_children.GetNode(0)->GetBBox(pBBox);
    int i;
    for (i = 1; i < m_children.GetLength(); i += 1)
    {
        CNode* pNode = m_children.GetNode(i);
        BBox bbox;
        pNode->GetBBox(&bbox);

        D3DXVECTOR3 v;
        D3DXVec3Scale(&v, &m_direction, spacing);
        v.x *= bbox.size.x;
        v.y *= bbox.size.y;
        v.z *= bbox.size.z;
        pBBox->size += bbox.size;

        if (pBBox->size.x > bbox.size.x)
            pBBox->size.x = bbox.size.x;
        if (pBBox->size.y > bbox.size.y)
            pBBox->size.y = bbox.size.y;
        if (pBBox->size.z > bbox.size.z)
            pBBox->size.z = bbox.size.z;
    }
}

float CLayout::GetRadius()
{
    float radius = 0.0f;

    for (int i = 0; i < m_children.GetLength(); i += 1)
        radius += m_children.GetNode(i)->GetRadius();

    if (m_children.GetLength() > 1)
        radius += (m_children.GetLength() - 1) * m_spacing / 2.0f;

    return radius;
}


// ===== CSwitch ==============================================================
// Shows one child selected by index (whichChoice). All children still
// get Advance() calls regardless of selection.

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
};

IMPLEMENT_NODE("Switch", CSwitch, CNode)

START_NODE_PROPS(CSwitch, CNode)
    NODE_PROP(pt_integer, CSwitch, whichChoice)
    NODE_PROP(pt_nodearray, CSwitch, choice)
END_NODE_PROPS()

CSwitch::CSwitch() :
    m_whichChoice(-1)
{
}

CSwitch::~CSwitch()
{
}

void CSwitch::Render()
{
    extern int g_nodeVisitsThisFrame;
    extern int g_nodeSkipsThisFrame;

    if (m_whichChoice >= 0 && m_whichChoice < m_choice.GetLength())
    {
        CNode* pNode = m_choice.GetNode(m_whichChoice);
        g_nodeVisitsThisFrame++;
        if (pNode->m_visible)
            pNode->Render();
        else
            g_nodeSkipsThisFrame++;
    }
}

void CSwitch::GetBBox(BBox* pBBox)
{
    if (m_whichChoice >= 0 && m_whichChoice < m_choice.GetLength())
        m_choice.GetNode(m_whichChoice)->GetBBox(pBBox);
    else
        CNode::GetBBox(pBBox);
}

float CSwitch::GetRadius()
{
    if (m_whichChoice >= 0 && m_whichChoice < m_choice.GetLength())
        return m_choice.GetNode(m_whichChoice)->GetRadius();

    return 0.0f;
}

void CSwitch::Advance(float nSeconds)
{
    CNode::Advance(nSeconds);

    for (int i = 0; i < m_choice.GetLength(); i += 1)
        m_choice.GetNode(i)->Advance(nSeconds);
}

void CSwitch::SetLight(int& nLight, D3DCOLORVALUE& ambient)
{
    if (m_whichChoice >= 0 && m_whichChoice < m_choice.GetLength())
    {
        CNode* pNode = m_choice.GetNode(m_whichChoice);

        if (pNode != NULL)
            pNode->SetLight(nLight, ambient);
    }
}


// ===== CBillboard ===========================================================
// Orients children to face the camera. If axisOfRotation is zero-length,
// does full viewer alignment. Otherwise rotates around the given axis.

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

CBillboard::CBillboard() :
    m_axisOfRotation(0.0f, 1.0f, 0.0f)
{
}

CBillboard::~CBillboard()
{
}

void CBillboard::Render()
{
    CalcMatrix();

    TheseusPushWorld();
    TheseusMultWorld(&m_matrix);
    TheseusUpdateWorld();

    CGroup::Render();

    TheseusPopWorld();
}

void CBillboard::CalcMatrix()
{
    D3DXMATRIX world;
    D3DXMATRIX world2local;

    TheseusGetTransform(D3DTS_WORLD, &world);
    D3DXMatrixInverse(&world2local, NULL, &world);

    // Get eye vector in local coordinate space
    D3DXVECTOR3 eye = theCamera.m_position;
    D3DXVec3TransformNormal(&eye, &eye, &world2local);
    D3DXVec3Normalize(&eye, &eye);

    D3DXVECTOR3 axis = m_axisOfRotation;
    float alen = D3DXVec3Length(&axis);

    if (alen <= 0.001f)
    {
        // Full viewer alignment
        D3DXVECTOR3 up(world2local.m[0][3], world2local.m[1][1], world2local.m[1][2]);
        D3DXVec3Normalize(&up, &up);

        D3DXVECTOR3 x;
        D3DXVec3Cross(&x, &up, &eye);
        D3DXVec3Normalize(&x, &x);

        m_matrix.m[0][0] = x.x;
        m_matrix.m[0][1] = x.y;
        m_matrix.m[0][2] = x.z;
        m_matrix.m[0][3] = 0.0f;

        m_matrix.m[1][0] = up.x;
        m_matrix.m[1][1] = up.y;
        m_matrix.m[1][2] = up.z;
        m_matrix.m[1][3] = 0.0f;

        m_matrix.m[2][0] = eye.x;
        m_matrix.m[2][1] = eye.y;
        m_matrix.m[2][2] = eye.z;
        m_matrix.m[2][3] = 0.0f;

        m_matrix.m[3][0] = 0.0f;
        m_matrix.m[3][1] = 0.0f;
        m_matrix.m[3][2] = 0.0f;
        m_matrix.m[3][3] = 1.0f;
    }
    else
    {
        // Axis-constrained rotation
        axis *= 1.0f / alen;

        D3DXVECTOR3 x;
        D3DXVec3Cross(&x, &axis, &eye);
        D3DXVec3Normalize(&x, &x);

        D3DXVECTOR3 z;
        D3DXVec3Cross(&z, &x, &axis);

        float angle = acosf(z.z);
        if (x.z > 0)
          angle = -angle;

        D3DXMatrixRotationAxis(&m_matrix, &axis, angle);
    }
}


// ===== CLayer ===============================================================
// A self-contained rendering layer with its own viewpoint and projection.
// Draws a black fade quad at m_alpha transparency before rendering children.

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

CLayer::CLayer() :
    m_viewpoint(NULL),
    m_navigationInfo(NULL),
    m_fade(0.0f),
    m_transparency(0.0f)
{
    m_vb = NULL;
}

CLayer::~CLayer()
{
    if (m_viewpoint != NULL)
        m_viewpoint->Release();

    if (m_navigationInfo != NULL)
        m_navigationInfo->Release();

    if (m_vb != NULL)
        m_vb->Release();
}

struct COLORVERTEX
{
    float dvX, dvY, dvZ;
    DWORD color;
};

bool CLayer::OnSetProperty(const PRD* pprd, const void* pvValue)
{
    if (PTR2INT(pprd->pbOffset) == offsetof(m_transparency))
    {
        CLerper::RemoveObject(this);
        if (m_fade > 0.0f)
            new CLerper(this, &m_alpha, 1.0f - *(float*)pvValue, m_fade);
        else
            m_alpha = 1.0f - *(float*)pvValue;
    }

    return CGroup::OnSetProperty(pprd, pvValue);
}

void CLayer::Render()
{
    if (m_viewpoint == NULL)
    {
        TRACE(_T("\001Layer is missing a viewpoint!\n"));
        return;
    }

    if (m_alpha == 0.0f)
        return;

    // Draw the fade quad
    {
        if (m_vb == NULL)
        {
            VERIFYHR(TheseusGetD3DDev()->CreateVertexBuffer(4 * sizeof(COLORVERTEX), D3DUSAGE_WRITEONLY, D3DFVF_XYZ | D3DFVF_DIFFUSE, D3DPOOL_MANAGED, &m_vb));

            COLORVERTEX* verts;
            VERIFYHR(m_vb->Lock(0, 4 * sizeof (COLORVERTEX), (BYTE**)&verts, 0));

            verts[0].dvX = (float)TheseusGetViewWidth() / 2.0f;
            verts[0].dvY = -(float)TheseusGetViewHeight() / 2.0f;
            verts[0].dvZ = 0.0f;
            verts[0].color = D3DCOLOR_COLORVALUE(0.0f, 0.0f, 0.0f, 1.0f);

            verts[1].dvX = -(float)TheseusGetViewWidth() / 2.0f;
            verts[1].dvY = -(float)TheseusGetViewHeight() / 2.0f;
            verts[1].dvZ = 0.0f;
            verts[1].color = D3DCOLOR_COLORVALUE(0.0f, 0.0f, 0.0f, 1.0f);

            verts[2].dvX = (float)TheseusGetViewWidth() / 2.0f;
            verts[2].dvY = (float)TheseusGetViewHeight() / 2.0f;
            verts[2].dvZ = 0.0f;
            verts[2].color = D3DCOLOR_COLORVALUE(0.0f, 0.0f, 0.0f, 1.0f);

            verts[3].dvX = -(float)TheseusGetViewWidth() / 2.0f;
            verts[3].dvY = (float)TheseusGetViewHeight() / 2.0f;
            verts[3].dvZ = 0.0f;
            verts[3].color = D3DCOLOR_COLORVALUE(0.0f, 0.0f, 0.0f, 1.0f);

            VERIFYHR(m_vb->Unlock());
        }

        D3DXMATRIX matProjection, matProjectionSave, matWorldSave, matViewSave;

        TheseusGetTransform(D3DTS_PROJECTION, &matProjectionSave);
        TheseusGetTransform(D3DTS_WORLD, &matWorldSave);
        TheseusGetTransform(D3DTS_VIEW, &matViewSave);

        D3DXMatrixOrthoLH(&matProjection, TheseusGetViewWidth(), TheseusGetViewHeight(), -10000.0f, 10000.0f);
        TheseusSetTransform(D3DTS_PROJECTION, &matProjection);
        TheseusSetTransform(D3DTS_WORLD, &g_matIdentity);
        TheseusSetTransform(D3DTS_VIEW, &g_matIdentity);

        TheseusSetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
        TheseusSetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
        TheseusSetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);
        TheseusSetRenderState(D3DRS_TEXTUREFACTOR, D3DCOLOR_RGBA(0, 0, 0, (BYTE)(m_alpha * 255.0f)));
        TheseusSetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        TheseusSetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        TheseusSetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
        TheseusSetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
        TheseusSetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        TheseusSetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        TheseusSetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        TheseusSetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

        VERIFYHR(TheseusGetD3DDev()->SetStreamSource(0, m_vb, sizeof (COLORVERTEX)));
        VERIFYHR(TheseusGetD3DDev()->SetVertexShader(D3DFVF_XYZ | D3DFVF_DIFFUSE));
        VERIFYHR(TheseusGetD3DDev()->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2));

        TheseusSetTransform(D3DTS_PROJECTION, &matProjectionSave);
        TheseusSetTransform(D3DTS_WORLD, &matWorldSave);
        TheseusSetTransform(D3DTS_VIEW, &matViewSave);
    }

    CViewpoint* pViewpoint = (CViewpoint*)m_viewpoint;
    CNavigationInfo* pNavigationInfo = (CNavigationInfo*)m_navigationInfo;

    if (pNavigationInfo != NULL && !pNavigationInfo->IsKindOf(NODE_CLASS(CNavigationInfo)))
    {
        TRACE(_T("\002Layer with bogus NavigationInfo!\n"));
        pNavigationInfo = NULL;
    }

    D3DXMATRIX matProjectionSave, matWorldSave, matViewSave;
    TheseusGetTransform(D3DTS_PROJECTION, &matProjectionSave);
    TheseusGetTransform(D3DTS_WORLD, &matWorldSave);
    TheseusGetTransform(D3DTS_VIEW, &matViewSave);

    // Calculate projection matrix
    {
        float nNear = 0.1f;
        float nFar = 1000.0f;
        float fieldOfView = D3DX_PI / 2.0f;

        if (pNavigationInfo != NULL)
        {
            nNear = pNavigationInfo->m_avatarSize.x / 2.0f;
            if (pNavigationInfo->m_visibilityLimit != 0.0f)
                nFar = pNavigationInfo->m_visibilityLimit;
        }

        fieldOfView = pViewpoint->m_fieldOfView;

        float aspect = 720.0f/480.0f;

        if (TheseusGetStretchWidescreen())
        {
            aspect *= 1.25f;
        }

        D3DXMatrixPerspectiveFovLH(&g_matProjection, fieldOfView, aspect, nNear, nFar);
        TheseusSetTransform(D3DTS_PROJECTION, &g_matProjection);
    }

    // Calculate view matrix
    if (pViewpoint != NULL)
    {
        D3DXVECTOR3 position = pViewpoint->m_position;
        position.z = -position.z;

        D3DXQUATERNION orientation;
        D3DXQuaternionRotationAxis(&orientation, (D3DXVECTOR3*)&pViewpoint->m_orientation, -pViewpoint->m_orientation.w);

        D3DXMatrixAffineTransformation(&g_matView, 1.0f, NULL, &orientation, &position);
        D3DXMatrixInverse(&g_matView, NULL, &g_matView);

        g_matView._31 = -g_matView._31;
        g_matView._32 = -g_matView._32;
        g_matView._33 = -g_matView._33;
        g_matView._34 = -g_matView._34;
        TheseusSetTransform(D3DTS_VIEW, &g_matView);
    }

    // Render children
    {
        TheseusPushWorld();
        TheseusIdentityWorld();
        TheseusUpdateWorld();
        CGroup::Render();
        TheseusPopWorld();
    }

    // Restore transforms
    {
        g_matProjection = matProjectionSave;
        g_matView = matViewSave;

        TheseusSetTransform(D3DTS_PROJECTION, &matProjectionSave);
        TheseusSetTransform(D3DTS_WORLD, &matWorldSave);
        TheseusSetTransform(D3DTS_VIEW, &matViewSave);
    }
}

void CLayer::Advance(float nSeconds)
{
    CGroup::Advance(nSeconds);

    if (m_navigationInfo != NULL)
        m_navigationInfo->Advance(nSeconds);

    if (m_viewpoint != NULL)
        m_viewpoint->Advance(nSeconds);
}


// ===== CBackground ==========================================================
// Renders a backdrop texture behind the scene. Binds itself as the active
// background on creation if none exists.

IMPLEMENT_NODE("Background", CBackground, CNode)

START_NODE_PROPS(CBackground, CNode)
    NODE_PROP(pt_vec3, CBackground, skyColor)
    NODE_PROP(pt_node, CBackground, backdrop)
    NODE_PROP(pt_boolean, CBackground, isBound)
END_NODE_PROPS()

CBackground::CBackground() :
    m_skyColor(0.0f, 0.0f, 0.0f),
    m_backdrop(NULL),
    m_isBound(false)
{
    m_vb = NULL;

    if (TheseusGetBackground() == NULL)
        Bind();
}

CBackground::~CBackground()
{
    if (m_vb != NULL)
        m_vb->Release();

    if (m_backdrop != NULL)
        m_backdrop->Release();

    if (TheseusGetBackground() == this)
        TheseusSetBackground(NULL);
}

void CBackground::Advance(float nSeconds)
{
    CNode::Advance(nSeconds);

    if (m_backdrop != NULL)
        m_backdrop->Advance(nSeconds);

    if (m_vb == NULL)
    {
        VERIFYHR(TheseusGetD3DDev()->CreateVertexBuffer(4 * sizeof(D3DVERTEX), D3DUSAGE_WRITEONLY, D3DFVF_VERTEX, D3DPOOL_MANAGED, &m_vb));

        D3DVERTEX* verts;
        VERIFYHR(m_vb->Lock(0, 4 * sizeof (D3DVERTEX), (BYTE**)&verts, 0));

        verts[0].dvX = (float)TheseusGetViewWidth() / 2.0f;
        verts[0].dvY = -(float)TheseusGetViewHeight() / 2.0f;
        verts[0].dvZ = -10000.0f;
        verts[0].dvNX = (float)TheseusGetViewWidth() / 2.0f;
        verts[0].dvNY = -(float)TheseusGetViewHeight() / 2.0f;
        verts[0].dvNZ = 1.0f;
        verts[0].dvTU = 1.0f;
        verts[0].dvTV = 1.0f;

        verts[1].dvX = -(float)TheseusGetViewWidth() / 2.0f;
        verts[1].dvY = -(float)TheseusGetViewHeight() / 2.0f;
        verts[1].dvZ = -10000.0f;
        verts[1].dvNX = (float)TheseusGetViewWidth() / 2.0f;
        verts[1].dvNY = (float)TheseusGetViewHeight() / 2.0f;
        verts[1].dvNZ = 1.0f;
        verts[1].dvTU = 0.0f;
        verts[1].dvTV = 1.0f;

        verts[2].dvX = (float)TheseusGetViewWidth() / 2.0f;
        verts[2].dvY = (float)TheseusGetViewHeight() / 2.0f;
        verts[2].dvZ = -10000.0f;
        verts[2].dvNX = (float)TheseusGetViewWidth() / 2.0f;
        verts[2].dvNY = (float)TheseusGetViewHeight() / 2.0f;
        verts[2].dvNZ = 1.0f;
        verts[2].dvTU = 1.0f;
        verts[2].dvTV = 0.0f;

        verts[3].dvX = -(float)TheseusGetViewWidth() / 2.0f;
        verts[3].dvY = (float)TheseusGetViewHeight() / 2.0f;
        verts[3].dvZ = -10000.0f;
        verts[3].dvNX = -(float)TheseusGetViewWidth() / 2.0f;
        verts[3].dvNY = (float)TheseusGetViewHeight() / 2.0f;
        verts[3].dvNZ = 1.0f;
        verts[3].dvTU = 0.0f;
        verts[3].dvTV = 0.0f;

        VERIFYHR(m_vb->Unlock());
    }
}

void CBackground::RenderBackdrop()
{
    ASSERT(m_backdrop != NULL);

    LPDIRECT3DTEXTURE8 pTexture = m_backdrop->GetTextureSurface();
    if (pTexture == NULL)
        return;

    TheseusSetTexture(0, pTexture);
    TheseusSetTexture(1, NULL);

    D3DXMATRIX matProjection, matProjectionSave, matWorldSave, matViewSave;

    TheseusGetTransform(D3DTS_PROJECTION, &matProjectionSave);
    TheseusGetTransform(D3DTS_WORLD, &matWorldSave);
    TheseusGetTransform(D3DTS_VIEW, &matViewSave);

    D3DXMatrixOrthoLH(&matProjection, TheseusGetViewWidth(), TheseusGetViewHeight(), -10000.0f, 10000.0f);
    TheseusSetTransform(D3DTS_PROJECTION, &matProjection);
    TheseusSetTransform(D3DTS_WORLD, &g_matIdentity);
    TheseusSetTransform(D3DTS_VIEW, &g_matIdentity);

    DWORD dwLighting, dwAlphaBlendEnable, dwCullMode, dwZWriteEnable;
    TheseusGetRenderState(D3DRS_LIGHTING, &dwLighting);
    TheseusGetRenderState(D3DRS_ALPHABLENDENABLE, &dwAlphaBlendEnable);
    TheseusGetRenderState(D3DRS_CULLMODE, &dwCullMode);
    TheseusGetRenderState(D3DRS_ZWRITEENABLE, &dwZWriteEnable);

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

    VERIFYHR(TheseusGetD3DDev()->SetStreamSource(0, m_vb, sizeof (D3DVERTEX)));
    VERIFYHR(TheseusGetD3DDev()->SetVertexShader(D3DFVF_VERTEX));
    VERIFYHR(TheseusGetD3DDev()->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2));

    TheseusSetRenderState(D3DRS_LIGHTING, dwLighting);
    TheseusSetRenderState(D3DRS_ALPHABLENDENABLE, dwAlphaBlendEnable);
    TheseusSetRenderState(D3DRS_CULLMODE, dwCullMode);
    TheseusSetRenderState(D3DRS_ZWRITEENABLE, dwZWriteEnable);

    TheseusSetTransform(D3DTS_PROJECTION, &matProjectionSave);
    TheseusSetTransform(D3DTS_WORLD, &matWorldSave);
    TheseusSetTransform(D3DTS_VIEW, &matViewSave);
}

void CBackground::Bind()
{
    if (TheseusGetBackground() != NULL)
        TheseusGetBackground()->m_isBound = false;

    TheseusSetBackground(this);
    m_isBound = true;
}

bool CBackground::OnSetProperty(const PRD* pprd, const void* pvValue)
{
    if (PTR2INT(pprd->pbOffset) == offsetof(m_isBound))
    {
        if (*(bool*)pvValue)
            Bind();
    }

    return true;
}


// ===== CLevel ===============================================================
// A top-level scene area. Manages transitions between levels with fade,
// XIP archive loading, tunnel camera paths, and joystick rebinding.

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

CLevel::CLevel() :
    m_tunnel(NULL),
    m_control(NULL),
    m_path(NULL),
    m_shell(NULL),
    m_archive(NULL),
    m_fade(true),
    m_unloadable(true)
{
    m_visible = false;
    m_xipFile = NULL;
    g_timeToNextLevel = 0.0f;
    m_bArrive = false;
}

CLevel::~CLevel()
{
    if (this == g_pCurLevel)
        g_pCurLevel = NULL;

    if (this == g_pFromLevel)
        g_pFromLevel = NULL;

    if (m_tunnel == g_pCurTunnel)
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

void CLevel::Render()
{
    float alpha = 1.0f;

    if (m_fade)
    {
        float t = (float) (g_timeToNextLevel - TheseusGetNow()) / 0.75f;
        if (t > 1.0f)
            t = 1.0f;
        else if (t < 0.0f)
            t = 0.0f;

        if (g_pCurLevel == this)
            alpha = 1.0f - t;
        else if (g_pFromLevel == this)
            alpha = t;

        if (alpha == 0.0f)
            return;
    }

    float nEffectAlphaSave = g_nEffectAlpha;
    g_nEffectAlpha *= alpha;

    if (m_shell != NULL)
        m_shell->Render();

    if (g_pCurLevel == this)
    {
        float blurAlpha = alpha * 1.3f;
        if (blurAlpha > 1.0f)
            blurAlpha = 1.0f;
        g_transitionMotionBlur = blurAlpha;
    }

    CGroup::Render();

    g_nEffectAlpha = nEffectAlphaSave;
}

void CLevel::Activate()
{
    g_bLevelTransition = true;

    TRACE(_T("Leaving level  0x%08x\n"), g_pCurLevel);
    TRACE(_T("Going to level 0x%08x\n"), this);

    if (m_archive != NULL && m_xipFile == NULL)
    {
        m_xipFile = LoadXIP(m_archive);
    }

    if (g_pFromLevel != NULL || g_pCurLevel != NULL && g_timeToNextLevel != 0.0f)
    {
        TRACE(_T("\taborting goto level 0x%08x\n"), g_pFromLevel);

        if (g_pFromLevel != NULL)
        {
            g_pFromLevel->Deactivate();
            g_pFromLevel = NULL;
            g_timeToNextLevel = 0.0f;
        }

        if (g_pCurLevel != NULL)
        {
            g_pCurLevel->m_visible = false;
            if (g_pCurLevel->m_xipFile && g_pCurLevel->m_unloadable && !g_pCurLevel->m_xipFile->m_locked)
                g_pCurLevel->m_xipFile->DeleteMeshBuffers();
            g_pCurLevel = NULL;
            g_timeToNextLevel = 0.0f;
        }

        if (g_pCurTunnel != NULL)
        {
            g_pCurTunnel->m_visible = false;
            g_pCurTunnel = NULL;
        }
    }

    ASSERT(g_pFromLevel == NULL);

    g_timeToNextLevel = TheseusGetNow() + 1.0f;
    g_pFromLevel = g_pCurLevel;

    m_visible = true;

    BindJoystick(NULL);

    g_pCurLevel = this;
}

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

void CLevel::GoBackTo()
{
    if (this == g_pCurLevel)
        return;

    Activate();

    g_pCurTunnel = (g_pFromLevel == NULL) ? NULL : g_pFromLevel->m_tunnel;

    if (g_pCurTunnel != NULL)
        g_pCurTunnel->m_visible = true;

    if (g_pFromLevel != NULL && g_pFromLevel->m_path != NULL && g_pFromLevel->m_path->IsKindOf(NODE_CLASS(CCameraPath)))
        ((CCameraPath*)g_pFromLevel->m_path)->Activate(true);
    else if (m_path != NULL && m_path->IsKindOf(NODE_CLASS(CViewpoint)))
        BindViewpoint(m_path);

    CallFunction(this, _T("OnActivate"));
}

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

    if (g_pCurLevel == this && g_timeToNextLevel != 0.0f && TheseusGetNow() >= g_timeToNextLevel)
    {
        TRACE(_T("Arrived at level 0x%08x\n"), this);

        ASSERT(g_pCurLevel == this);

        g_timeToNextLevel = 0.0f;

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

void CLevel::Deactivate()
{
    ASSERT(this == g_pFromLevel);

    m_visible = false;
    CallFunction(this, _T("OnDeactivate"));

    if (m_unloadable && !m_xipFile->m_locked)
        m_xipFile->DeleteMeshBuffers();
}


#ifdef UIX_DESKTOP
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
#endif // UIX_DESKTOP
