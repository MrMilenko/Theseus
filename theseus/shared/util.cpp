// util.cpp: core utilities consolidated into one TU. CDefine /
// CNameSpace lookup primitives, mem-tracking helpers, the linear lerper
// (header-only declared in lerper.h), and a few small array helpers.
// Decompiled from the 5960 retail XBE; see docs/decomp/Util.md.
#include "std.h"
#include "theseus.h"
#include "node.h"
#include "lerper.h"

// =========================================================================
// CNameSpace: linked list symbol table for node DEF/USE and class members
// =========================================================================

CNameSpace::CNameSpace()
{
	m_firstDefine = NULL;
}

CNameSpace::~CNameSpace()
{
	CDefine* pNext;
	for (CDefine* p = m_firstDefine; p != NULL; p = pNext)
	{
		p->m_node->Release();
		pNext = p->m_next;
		delete p;
	}
}

bool CNameSpace::Define(const TCHAR* pchName, int cchName, CNode* pNode)
{
	if (Lookup(pchName, cchName) != NULL)
		return FALSE;

#pragma push_macro("new")
#undef new
	CDefine* pDef = new(cchName) CDefine;
#pragma pop_macro("new")

	CopyChars(pDef->m_name, pchName, cchName);
	pDef->m_name[cchName] = '\0';
	pDef->m_next = m_firstDefine;
	m_firstDefine = pDef;
	pDef->m_node = pNode;

	return true;
}

CDefine* CNameSpace::Add(const TCHAR* pchName, int cchName)
{
#pragma push_macro("new")
#undef new
	CDefine* pDef = new(cchName) CDefine;
#pragma pop_macro("new")

	CopyChars(pDef->m_name, pchName, cchName);
	pDef->m_name[cchName] = '\0';
	pDef->m_next = m_firstDefine;
	m_firstDefine = pDef;
	pDef->m_node = NULL;

	return pDef;
}

CDefine* CNameSpace::Get(const TCHAR* pchName, int cchName)
{
	for (CDefine* p = m_firstDefine; p != NULL; p = p->m_next)
	{
		if ((int)_tcslen(p->m_name) == cchName && _tcsncmp(pchName, p->m_name, cchName) == 0)
			return p;
	}
	return Add(pchName, cchName);
}

CNode* CNameSpace::Lookup(const TCHAR* pchName, int cchName)
{
	for (CDefine* p = m_firstDefine; p != NULL; p = p->m_next)
	{
		if ((int)_tcslen(p->m_name) == cchName && _tcsncmp(pchName, p->m_name, cchName) == 0)
			return p->m_node;
	}
	return NULL;
}

CDefine* CNameSpace::Lookup(CNode* pNode)
{
	for (CDefine* p = m_firstDefine; p != NULL; p = p->m_next)
	{
		if (pNode == p->m_node)
			return p;
	}
	return NULL;
}

void CNameSpace::Dump() const
{
	for (CDefine* pDefine = m_firstDefine; pDefine != NULL; pDefine = pDefine->m_next)
		TRACE(_T("%s -> 0x%08x\n"), pDefine->m_name, pDefine->m_node);
}

// Memory management lives in xbox/memutil.cpp (Xbox) and
// desktop/memutil.cpp (desktop). The two backends share nothing
// concrete; declarations are in shared/theseus.h.

// =========================================================================
// CLerper: smooth value interpolation over time
// =========================================================================

CLerper* CLerper::c_pHead;

CLerper::CLerper(CObject* pObject, float* pValue, float nNewValue, float nInterval)
{
	m_owner = pObject;
	m_interval = nInterval;
	m_startTime = TheseusGetNow();
	m_startValue = *pValue;
	m_endValue = nNewValue;
	m_value = pValue;

	m_next = c_pHead;
	c_pHead = this;
}

bool CLerper::Advance()
{
	float t = (float)(TheseusGetNow() - m_startTime) / m_interval;
	if (t >= 1.0f)
	{
		*m_value = m_endValue;
		return false;
	}
	*m_value = (1.0f - t) * m_startValue + t * m_endValue;
	return true;
}

void CLerper::AdvanceAll()
{
	for (CLerper** pp = &c_pHead; *pp != NULL; )
	{
		CLerper* p = *pp;
		if (!p->Advance())
		{
			*pp = p->m_next;
			delete p;
		}
		else
		{
			pp = &p->m_next;
		}
	}
}

void CLerper::RemoveObject(CObject* pObject)
{
	for (CLerper** pp = &c_pHead; *pp != NULL; )
	{
		CLerper* p = *pp;
		if (p->m_owner == pObject)
		{
			*pp = p->m_next;
			delete p;
		}
		else
		{
			pp = &p->m_next;
		}
	}
}

// =========================================================================
// Typed array implementations (used by PRD property system)
// =========================================================================

CIntArray::CIntArray()   { m_nAlloc = 0; m_nSize = 0; m_value = NULL; }
CIntArray::~CIntArray()  { delete[] m_value; }
void CIntArray::SetSize(int n)
{
	if (n > m_nAlloc)
	{
		int* p = new int[n];
		if (m_value) { CopyMemory(p, m_value, sizeof(int) * m_nSize); delete[] m_value; }
		m_value = p; m_nAlloc = n;
	}
	m_nSize = n;
}

CNumArray::CNumArray()   { m_nAlloc = 0; m_nSize = 0; m_value = NULL; }
CNumArray::~CNumArray()  { delete[] m_value; }
void CNumArray::SetSize(int n)
{
	if (n > m_nAlloc)
	{
		float* p = new float[n];
		if (m_value) { CopyMemory(p, m_value, sizeof(float) * m_nSize); delete[] m_value; }
		m_value = p; m_nAlloc = n;
	}
	m_nSize = n;
}

CVec2Array::CVec2Array()  { m_nAlloc = 0; m_nSize = 0; m_value = NULL; }
CVec2Array::~CVec2Array() { delete[] m_value; }
void CVec2Array::SetSize(int n)
{
	if (n > m_nAlloc)
	{
		D3DXVECTOR2* p = new D3DXVECTOR2[n];
		if (m_value) { CopyMemory(p, m_value, sizeof(D3DXVECTOR2) * m_nSize); delete[] m_value; }
		m_value = p; m_nAlloc = n;
	}
	m_nSize = n;
}

CVec3Array::CVec3Array()  { m_nAlloc = 0; m_nSize = 0; m_value = NULL; }
CVec3Array::~CVec3Array() { delete[] m_value; }
void CVec3Array::SetSize(int n)
{
	if (n > m_nAlloc)
	{
		D3DXVECTOR3* p = new D3DXVECTOR3[n];
		if (m_value) { CopyMemory(p, m_value, sizeof(D3DXVECTOR3) * m_nSize); delete[] m_value; }
		m_value = p; m_nAlloc = n;
	}
	m_nSize = n;
}

CVec4Array::CVec4Array()  { m_nAlloc = 0; m_nSize = 0; m_value = NULL; }
CVec4Array::~CVec4Array() { delete[] m_value; }
void CVec4Array::SetSize(int n)
{
	if (n > m_nAlloc)
	{
		D3DXVECTOR4* p = new D3DXVECTOR4[n];
		if (m_value) { CopyMemory(p, m_value, sizeof(D3DXVECTOR4) * m_nSize); delete[] m_value; }
		m_value = p; m_nAlloc = n;
	}
	m_nSize = n;
}
