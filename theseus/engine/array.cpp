// array.cpp: the script Array object, a growable list of object
// references, and the element reference `a[i]` evaluates to. Rebuilt
// function by function from the 5960 retail XBE (0x30440..0x30e70).

#include "std.h"
#include "theseus.h"
#include "node.h"
#include "runner.h"

extern CObject **g_rgParam;
extern int g_nParam;
extern CObject *Dereference(CObject *pObject);

class CArrayObject : public CObject
{
	DECLARE_NODE(CArrayObject, CObject)
public:
	CArrayObject();
	CArrayObject(int nLength);
	~CArrayObject();

	CStrObject *ToStr();
	CObject *Dot(CObject *pObject);

	CObject *concat(CObject *pArray2);
	CStrObject *join(const TCHAR *szSeparator);
	CObject *reverse();
	CObject *slice(CObject **rgparam, int nParam /*int nStart, int nEnd*/);
	CObject *sort();

protected:
	friend class CArrayElementReference;

	void Grow(int nNewLength);
	void Allocate(int nLength);

	int m_nLength;
	int m_nAlloc;
	CObject **m_object;

	DECLARE_NODE_FUNCTIONS()

private:
	// Need this to prevent the compiler from using default copy ctor
	CArrayObject(const CArrayObject &);
	CArrayObject &operator=(const CArrayObject &rhs);
};

class CArrayElementReference : public CObject
{
public:
	CArrayElementReference(CArrayObject *pArray, int nIndex);
	~CArrayElementReference();

	void Assign(CObject *pObject);
	CObject *Deref();

	CArrayObject *m_pArray;
	int m_nIndex;
};

// Takes a reference for the copy it stores.
static inline CObject *Share(CObject *obj)
{
	if (obj != NULL)
		obj->AddRef();
	return obj;
}

// ===== a[i] ================================================================

// Reconstructed from 5960 retail XBE @ 0x00030440: holds the array (with a
// reference) and the index.
CArrayElementReference::CArrayElementReference(CArrayObject *pArray, int nIndex)
{
	m_pArray = pArray;
	m_pArray->AddRef();
	m_nIndex = nIndex;
}

// Reconstructed from 5960 retail XBE @ 0x000304c0.
CArrayElementReference::~CArrayElementReference()
{
	m_pArray->Release();
}

// CArrayElementReference::Assign(value)
// Reconstructed from 5960 retail XBE @ 0x00030520: grows the array to reach
// the index, releases what was there and stores the value with a reference.
void CArrayElementReference::Assign(CObject *pObject)
{
	if (m_nIndex >= m_pArray->m_nLength)
		m_pArray->Grow(m_nIndex + 1);

	CObject *&slot = m_pArray->m_object[m_nIndex];
	if (slot != NULL)
		slot->Release();
	slot = Share(pObject);
}

// CArrayElementReference::Deref()
// Reconstructed from 5960 retail XBE @ 0x00030570: out of range is the
// script error "Array reference out of bounds"; otherwise the element (with
// a reference), and the reference object lets go of itself.
CObject *CArrayElementReference::Deref()
{
	if (m_nIndex < 0 || m_nIndex >= m_pArray->m_nLength)
	{
		g_pRunner->Error(_T("Array reference out of bounds"));
		return NULL;
	}
	CObject *value = Share(m_pArray->m_object[m_nIndex]);
	Release();
	return value;
}

IMPLEMENT_NODE("Array", CArrayObject, CObject)

// FND at 0x0017f5d8, same order.
#define _FND_CLASS CArrayObject
START_NODE_FUN(CArrayObject, CObject)
NODE_FUN_OO(concat)
NODE_FUN_SS(join)
NODE_FUN_OV(reverse)
NODE_FUN(slice)
NODE_FUN_OV(sort)
END_NODE_FUN()
#undef _FND_CLASS

// Sets up n empty slots (none at all for 0).
void CArrayObject::Allocate(int nLength)
{
	m_nLength = nLength;
	m_nAlloc = nLength;
	m_object = NULL;
	if (nLength != 0)
	{
		m_object = new CObject *[nLength];
		memset(m_object, 0, nLength * sizeof(CObject *));
	}
}

// CArrayObject::CArrayObject()   [script `new Array(...)`]
// Reconstructed from 5960 retail XBE @ 0x00030630. One argument is the
// length (dereferenced and read as a number); two or more become the
// elements, each with a reference; none makes an empty array.
CArrayObject::CArrayObject()
{
	m_obj = objArray;

	int length = 0;
	if (g_nParam == 1)
	{
		g_rgParam[0] = Dereference(g_rgParam[0]);
		CNumObject *count = g_rgParam[0]->ToNum();
		length = (int)count->m_value;
		count->Release();
	}
	else if (g_nParam > 1)
	{
		length = g_nParam;
	}
	Allocate(length);

	if (g_nParam > 1)
	{
		for (int i = 0; i < g_nParam; i++)
			m_object[i] = Share(g_rgParam[i]);
	}
}

// Reconstructed from 5960 retail XBE @ 0x00030770: n empty slots.
CArrayObject::CArrayObject(int nLength)
{
	Allocate(nLength);
}

// Reconstructed from 5960 retail XBE @ 0x00030800: releases every element.
CArrayObject::~CArrayObject()
{
	for (int i = 0; i < m_nLength; i++)
	{
		if (m_object[i] != NULL)
			m_object[i]->Release();
	}
	delete[] m_object;
}

// CArrayObject::Grow(length)
// Reconstructed from 5960 retail XBE @ 0x000308e0: lengthens the array,
// new slots empty, reallocating when it runs out of room. 5960 makes room
// for 16 more; Theseus makes room for half again plus 16 so long push loops
// don't copy every time.
void CArrayObject::Grow(int nNewLength)
{
	if (nNewLength <= m_nLength)
		return;

	if (nNewLength > m_nAlloc)
	{
		int room = nNewLength + nNewLength / 2 + 16;
		CObject **grown = new CObject *[room];
		if (m_nLength > 0)
			memcpy(grown, m_object, m_nLength * sizeof(CObject *));
		delete[] m_object;
		m_object = grown;
		m_nAlloc = room;
	}
	memset(m_object + m_nLength, 0, (nNewLength - m_nLength) * sizeof(CObject *));
	m_nLength = nNewLength;
}

// Reconstructed from 5960 retail XBE @ 0x00030960: join(",").
CStrObject *CArrayObject::ToStr()
{
	return join(_T(","));
}

// CArrayObject::Dot(member)
// Reconstructed from 5960 retail XBE @ 0x00030970: `length` is the length
// as a number, a number is an element reference, anything else goes to
// CObject::Dot.
CObject *CArrayObject::Dot(CObject *pObject)
{
	if (pObject->m_obj == objNumber)
		return new CArrayElementReference(this, (int)((CNumObject *)pObject)->m_value);
	if (pObject->m_obj == objVariable && _tcscmp(((CVarObject *)pObject)->m_text, _T("length")) == 0)
		return new CNumObject((float)m_nLength);
	return CObject::Dot(pObject);
}

// Copies count elements from src into a new array at offset, each with a
// reference. The script methods below all build their result this way.
static void CopyInto(CObject **dst, CObject *const *src, int count)
{
	for (int i = 0; i < count; i++)
		dst[i] = Share(src[i]);
}

// CArrayObject::concat(other)   [script fn 'concat', FND 0x0017f5d8, sig 17]
// Reconstructed from 5960 retail XBE @ 0x00030a70: a new array of this
// one's elements then other's; "object is not an array" for anything else.
CObject *CArrayObject::concat(CObject *pObject)
{
	if (pObject->m_obj != objArray)
	{
		g_pRunner->Error(_T("object is not an array"));
		return NULL;
	}
	CArrayObject *other = (CArrayObject *)pObject;
	CArrayObject *result = new CArrayObject(m_nLength + other->m_nLength);
	CopyInto(result->m_object, m_object, m_nLength);
	CopyInto(result->m_object + m_nLength, other->m_object, other->m_nLength);
	return result;
}

// CArrayObject::join(separator)   [script fn 'join', FND 0x0017f5f0, sig 5]
// Reconstructed from 5960 retail XBE @ 0x00030b80: each element's string
// ("[null]" for an empty slot), separated but not ended by separator.
CStrObject *CArrayObject::join(const TCHAR *szSeperator)
{
	CStrObject *text = new CStrObject;
	for (int i = 0; i < m_nLength; i++)
	{
		if (i > 0)
			text->Append(szSeperator);
		if (m_object[i] == NULL)
		{
			text->Append(_T("[null]"));
			continue;
		}
		CStrObject *part = m_object[i]->ToStr();
		text->Append(part->GetSz());
		part->Release();
	}
	return text;
}

// CArrayObject::reverse()   [script fn 'reverse', FND 0x0017f608, sig 15]
// Reconstructed from 5960 retail XBE @ 0x00030c40: a new array, back to front.
CObject *CArrayObject::reverse()
{
	CArrayObject *result = new CArrayObject(m_nLength);
	for (int i = 0; i < m_nLength; i++)
		result->m_object[m_nLength - 1 - i] = Share(m_object[i]);
	return result;
}

// CArrayObject::slice(start [, end])   [script fn 'slice', FND 0x0017f620, sig 0]
// Reconstructed from 5960 retail XBE @ 0x00030cd0. end defaults to -1; a
// negative end counts back from length + 1. The result holds end - start
// elements (none if that's negative). 5960 carries on after reporting a
// wrong argument count; Theseus stops there.
CObject *CArrayObject::slice(CObject **rgparam, int nParam /*int nStart, int nEnd*/)
{
	if (nParam != 1 && nParam != 2)
	{
		g_pRunner->Error(_T("invalid number of parameters"));
		return NULL;
	}

	int start = (int)rgparam[0]->Deref()->ToNum()->m_value;
	int end = nParam == 2 ? (int)rgparam[1]->Deref()->ToNum()->m_value : -1;
	if (end < 0)
		end += m_nLength + 1;
	int count = end - start;
	if (count < 0)
		count = 0;

	CArrayObject *result = new CArrayObject(count);
	CopyInto(result->m_object, m_object + start, count);
	return result;
}

// The sort order (@ 0x00030e00): empty slots first, then by wcscmp of the
// elements' strings.
static int __cdecl CompareElements(const void *left, const void *right)
{
	CObject *a = *(CObject *const *)left;
	CObject *b = *(CObject *const *)right;
	if (a == NULL)
		return b == NULL ? 0 : -1;
	if (b == NULL)
		return 1;

	CStrObject *textA = a->ToStr();
	CStrObject *textB = b->ToStr();
	int order = _tcscmp(textA->GetSz(), textB->GetSz());
	textA->Release();
	textB->Release();
	return order;
}

// CArrayObject::sort()   [script fn 'sort', FND 0x0017f638, sig 15]
// Reconstructed from 5960 retail XBE @ 0x00030e70: a sorted copy (qsort).
CObject *CArrayObject::sort()
{
	CArrayObject *result = new CArrayObject(m_nLength);
	CopyInto(result->m_object, m_object, m_nLength);
	qsort(result->m_object, result->m_nLength, sizeof(CObject *), CompareElements);
	return result;
}
