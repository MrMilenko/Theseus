// util.cpp: small engine pieces. The name spaces the compiler and runner
// keep DEF names in, the lerper list that fades floats over time, and the
// growable arrays behind array properties. Rebuilt from the 5960 retail XBE
// (0x2f180..0x2f370, 0x32bb0..0x32ca0, 0x39be0..0x39ca0); see
// docs/decomp/Util.md.

#include "std.h"
#include "theseus.h"
#include "node.h"
#include "lerper.h"


// ===== CNameSpace ==========================================================
// A list of CDefine entries, newest first, each one block: next, node and
// the name inline after them.

CNameSpace::CNameSpace()
{
	m_firstDefine = NULL;
}

// Theseus skips entries that never got a node.
CNameSpace::~CNameSpace()
{
	while (m_firstDefine != NULL)
	{
		CDefine* next = m_firstDefine->m_next;
		if (m_firstDefine->m_node != NULL)
			m_firstDefine->m_node->Release();
		delete m_firstDefine;
		m_firstDefine = next;
	}
}

static bool SameName(const CDefine* def, const TCHAR* pchName, int cchName)
{
	return (int)_tcslen(def->m_name) == cchName && _tcsncmp(pchName, def->m_name, cchName) == 0;
}

// CNameSpace::Add(name, cch)
// The entry allocation shared by Define and Get (inlined into both in
// 5960): cch * 2 + 0xc bytes, the name copied in and terminated, pushed on
// the front.
CDefine* CNameSpace::Add(const TCHAR* pchName, int cchName)
{
#pragma push_macro("new")
#undef new
	CDefine* def = new(cchName) CDefine;
#pragma pop_macro("new")
	CopyChars(def->m_name, pchName, cchName);
	def->m_name[cchName] = 0;
	def->m_node = NULL;
	def->m_next = m_firstDefine;
	m_firstDefine = def;
	return def;
}

// CNameSpace::Define(name, cch, node)
// Reconstructed from 5960 retail XBE @ 0x00032bb0: false when the name is
// already bound, otherwise a new entry for it holding node.
bool CNameSpace::Define(const TCHAR* pchName, int cchName, CNode* pNode)
{
	if (Lookup(pchName, cchName) != NULL)
		return false;
	Add(pchName, cchName)->m_node = pNode;
	return true;
}

// CNameSpace::Get(name, cch)
// Reconstructed from 5960 retail XBE @ 0x00032c20: the entry with that
// exact name (same length, wcsncmp), or a new empty one.
CDefine* CNameSpace::Get(const TCHAR* pchName, int cchName)
{
	for (CDefine* def = m_firstDefine; def != NULL; def = def->m_next)
	{
		if (SameName(def, pchName, cchName))
			return def;
	}
	return Add(pchName, cchName);
}

// CNameSpace::Lookup(name, cch)
// Reconstructed from 5960 retail XBE @ 0x00032ca0: the node bound to that
// exact name, or NULL. (The binary checks the first character before the
// length as a shortcut.)
CNode* CNameSpace::Lookup(const TCHAR* pchName, int cchName)
{
	for (CDefine* def = m_firstDefine; def != NULL; def = def->m_next)
	{
		if (SameName(def, pchName, cchName))
			return def->m_node;
	}
	return NULL;
}

// The entry holding pNode, or NULL.
CDefine* CNameSpace::Lookup(CNode* pNode)
{
	CDefine* def = m_firstDefine;
	while (def != NULL && def->m_node != pNode)
		def = def->m_next;
	return def;
}


// ===== CLerper =============================================================
// The list head is 0x0017aeac. An entry is interval, start time (double),
// start value, end value, the float it drives, its owner and next.

CLerper* CLerper::c_pHead;

// CLerper::CLerper(owner, value, to, seconds)
// Reconstructed from 5960 retail XBE @ 0x00039be0: records where *value
// starts, the target, the clock now, and links itself at the head.
CLerper::CLerper(CObject* pObject, float* pValue, float nNewValue, float nInterval)
{
	m_interval = nInterval;
	m_owner = pObject;
	m_startTime = TheseusGetNow();
	m_startValue = *pValue;
	m_endValue = nNewValue;
	m_value = pValue;
	m_next = c_pHead;
	c_pHead = this;
}

// One step: false once the time is up, after storing the end value.
bool CLerper::Advance()
{
	float t = (float)(TheseusGetNow() - m_startTime) / m_interval;
	if (t < 1.0f)
	{
		*m_value = (1.0f - t) * m_startValue + t * m_endValue;
		return true;
	}
	*m_value = m_endValue;
	return false;
}

// CLerper::AdvanceAll()
// Reconstructed from 5960 retail XBE @ 0x00039c20: every lerper writes
// (1 - t) * start + t * end for t = elapsed / interval, and one that
// reached t >= 1 writes the end value, unlinks and frees itself.
void CLerper::AdvanceAll()
{
	CLerper** link = &c_pHead;
	while (*link != NULL)
	{
		CLerper* lerper = *link;
		if (lerper->Advance())
		{
			link = &lerper->m_next;
			continue;
		}
		*link = lerper->m_next;
		delete lerper;
	}
}

// CLerper::RemoveObject(owner)
// Reconstructed from 5960 retail XBE @ 0x00039ca0: unlinks and frees every
// lerper that owner started, leaving the values where they are.
void CLerper::RemoveObject(CObject* pObject)
{
	CLerper** link = &c_pHead;
	while (*link != NULL)
	{
		CLerper* lerper = *link;
		if (lerper->m_owner != pObject)
		{
			link = &lerper->m_next;
			continue;
		}
		*link = lerper->m_next;
		delete lerper;
	}
}


// ===== Array properties ====================================================
// Each array is { allocated, size, values }. SetSize grows the buffer to
// exactly the new size when it's too small, copying the old values over,
// and always sets the size; it never shrinks the buffer. Reconstructed from
// 5960 retail XBE @ 0x0002f180 (4 byte elements, ints and floats share it),
// 0x0002f1e0 (8), 0x0002f2c0 (12) and 0x0002f370 (16).

template <typename T>
static void GrowArray(int& alloc, int& size, T*& values, int newSize)
{
	if (newSize > alloc)
	{
		T* grown = new T[newSize];
		if (values != NULL)
		{
			for (int i = 0; i < size; i++)
				grown[i] = values[i];
			delete[] values;
		}
		values = grown;
		alloc = newSize;
	}
	size = newSize;
}

CIntArray::CIntArray()   { m_nAlloc = 0; m_nSize = 0; m_value = NULL; }
CIntArray::~CIntArray()  { delete[] m_value; }
void CIntArray::SetSize(int n) { GrowArray(m_nAlloc, m_nSize, m_value, n); }

CNumArray::CNumArray()   { m_nAlloc = 0; m_nSize = 0; m_value = NULL; }
CNumArray::~CNumArray()  { delete[] m_value; }
void CNumArray::SetSize(int n) { GrowArray(m_nAlloc, m_nSize, m_value, n); }

CVec2Array::CVec2Array()  { m_nAlloc = 0; m_nSize = 0; m_value = NULL; }
CVec2Array::~CVec2Array() { delete[] m_value; }
void CVec2Array::SetSize(int n) { GrowArray(m_nAlloc, m_nSize, m_value, n); }

CVec3Array::CVec3Array()  { m_nAlloc = 0; m_nSize = 0; m_value = NULL; }
CVec3Array::~CVec3Array() { delete[] m_value; }
void CVec3Array::SetSize(int n) { GrowArray(m_nAlloc, m_nSize, m_value, n); }

CVec4Array::CVec4Array()  { m_nAlloc = 0; m_nSize = 0; m_value = NULL; }
CVec4Array::~CVec4Array() { delete[] m_value; }
void CVec4Array::SetSize(int n) { GrowArray(m_nAlloc, m_nSize, m_value, n); }
