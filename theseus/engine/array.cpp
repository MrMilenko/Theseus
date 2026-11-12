// array.cpp: the script's Array object and the element reference that
// array[i] produces. Rebuilt from the 5960 retail XBE (0x30440..0x30e70);
// findings are in docs/decomp/VM.md.

#include "std.h"
#include "theseus.h"
#include "node.h"
#include "runner.h"

extern CObject **g_rgParam;
extern int g_nParam;
extern CObject *Dereference(CObject *pObject);

// ===== class layouts ========================================================
// CArrayObject: 0x20 bytes, vtable 0x00021bd4, object type 15 (objArray).
// length 0x14, allocated 0x18, element pointers 0x1c.
// CArrayElementReference: 0x1c bytes, vtable 0x00021b9c, array 0x14, index 0x18.

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

	int m_nLength;
	int m_nAlloc;
	CObject **m_object;

	DECLARE_NODE_FUNCTIONS()

private:
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

// Shared by concat, reverse and sort: copy one slot, holding a reference.
static CObject *Hold(CObject *pObject)
{
	if (pObject != NULL)
		pObject->AddRef();
	return pObject;
}


// ===== CArrayElementReference ===============================================

// CArrayElementReference(pArray, nIndex) / ~CArrayElementReference()
// Reconstructed from 5960 retail XBE @ 0x00030440 / 0x000304c0:
//     array = pArray; pArray->AddRef(); index = nIndex;   ...   array->Release();
// The reference keeps its array alive until the script is done with it.
CArrayElementReference::CArrayElementReference(CArrayObject *pArray, int nIndex)
{
	m_pArray = pArray;
	m_pArray->AddRef();
	m_nIndex = nIndex;
}

CArrayElementReference::~CArrayElementReference()
{
	m_pArray->Release();
}

// CArrayElementReference::Assign(pObject)
// Reconstructed from 5960 retail XBE @ 0x00030520. Ghidra decompile:
//     if (array->length <= index) array->Grow(index + 1);
//     if (slot) slot->Release();  obj->AddRef();  slot = obj;
// Writing past the end stretches the array, filling the gap with nulls.
void CArrayElementReference::Assign(CObject *pObject)
{
	if (m_pArray->m_nLength <= m_nIndex)
		m_pArray->Grow(m_nIndex + 1);

	CObject *&slot = m_pArray->m_object[m_nIndex];
	if (slot != NULL)
		slot->Release();

	pObject->AddRef();
	slot = pObject;
}

// CArrayElementReference::Deref()
// Reconstructed from 5960 retail XBE @ 0x00030570. Ghidra decompile:
//     if (0 <= index && index < array->length) {
//         obj = slot; if (obj) obj->AddRef(); this->Release(); return obj;
//     }
//     CRunner::Error(L"Array reference out of bounds"); return NULL;
// Reading swaps the reference for the element itself.
CObject *CArrayElementReference::Deref()
{
	if (m_nIndex < 0 || m_nIndex >= m_pArray->m_nLength)
	{
		g_pRunner->Error(_T("Array reference out of bounds"));
		return NULL;
	}

	CObject *pElement = Hold(m_pArray->m_object[m_nIndex]);
	Release();
	return pElement;
}


// ===== CArrayObject =========================================================

IMPLEMENT_NODE("Array", CArrayObject, CObject)

// FND table at 0x0017f5d8: concat (sig 17), join (5), reverse (15),
// slice (0, raw parameter list), sort (15).
#define _FND_CLASS CArrayObject
START_NODE_FUN(CArrayObject, CObject)
NODE_FUN_OO(concat)
NODE_FUN_SS(join)
NODE_FUN_OV(reverse)
NODE_FUN(slice)
NODE_FUN_OV(sort)
END_NODE_FUN()
#undef _FND_CLASS

// CArrayObject::CArrayObject()   [script 'new Array(...)']
// Reconstructed from 5960 retail XBE @ 0x00030630. Ghidra decompile:
//     type = 15;
//     if (g_nParam == 1) { g_rgParam[0] = deref chain of g_rgParam[0];
//                          num = g_rgParam[0]->ToNum(); n = (int)num->value;
//                          num->Release(); }
//     else n = (g_nParam > 1) ? g_nParam : 0;
//     length = alloc = n; elements = n ? zeroed new CObject*[n] : NULL;
//     if (g_nParam > 1) for each i: elements[i] = g_rgParam[i] (AddRef'd);
// So new Array(5) is five nulls and new Array(a, b) holds a and b, like JS.
CArrayObject::CArrayObject()
{
	m_obj = objArray;

	int nLength = 0;
	if (g_nParam == 1)
	{
		g_rgParam[0] = Dereference(g_rgParam[0]);
		CNumObject *pCount = g_rgParam[0]->ToNum();
		nLength = (int)pCount->m_value;
		pCount->Release();
	}
	else if (g_nParam > 1)
	{
		nLength = g_nParam;
	}

	m_nLength = nLength;
	m_nAlloc = nLength;
	m_object = NULL;

	if (nLength != 0)
	{
		m_object = new CObject *[nLength];
		memset(m_object, 0, nLength * sizeof (CObject *));
	}

	if (g_nParam > 1)
	{
		for (int i = 0; i < g_nParam; i++)
			m_object[i] = Hold(g_rgParam[i]);
	}
}

// CArrayObject::CArrayObject(nLength)
// Reconstructed from 5960 retail XBE @ 0x00030770. Ghidra decompile:
//     length = alloc = n; elements = n ? zeroed new CObject*[n] : NULL;
// The native side's constructor, used by concat, reverse, slice and sort.
CArrayObject::CArrayObject(int nLength)
{
	m_nLength = nLength;
	m_nAlloc = nLength;
	m_object = NULL;

	if (nLength != 0)
	{
		m_object = new CObject *[nLength];
		memset(m_object, 0, nLength * sizeof (CObject *));
	}
}

// CArrayObject::~CArrayObject()
// Reconstructed from 5960 retail XBE @ 0x00030800: releases every non-null
// element, frees the pointer block, then the inlined CObject destructor.
CArrayObject::~CArrayObject()
{
	for (int i = 0; i < m_nLength; i++)
	{
		if (m_object[i] != NULL)
			m_object[i]->Release();
	}

	delete [] m_object;
}

// CArrayObject::Grow(nNewLength)
// Reconstructed from 5960 retail XBE @ 0x000308e0. Ghidra decompile:
//     if (n <= length) return;
//     if (alloc < n) { block = new CObject*[n + 16]; copy length slots;
//                      free(old); elements = block; alloc = n + 16; }
//     zero slots length..n-1; length = n;
// Room for 16 more each time it has to move.
void CArrayObject::Grow(int nNewLength)
{
	if (nNewLength <= m_nLength)
		return;

	if (nNewLength > m_nAlloc)
	{
		int nRoom = nNewLength + 16;
		CObject **block = new CObject *[nRoom];
		if (m_nLength > 0)
			memcpy(block, m_object, m_nLength * sizeof (CObject *));
		delete [] m_object;
		m_object = block;
		m_nAlloc = nRoom;
	}

	memset(m_object + m_nLength, 0, (nNewLength - m_nLength) * sizeof (CObject *));
	m_nLength = nNewLength;
}

// CArrayObject::ToStr()
// Reconstructed from 5960 retail XBE @ 0x00030960: return join(L",");
CStrObject *CArrayObject::ToStr()
{
	return join(_T(","));
}

// CArrayObject::Dot(pObject)
// Reconstructed from 5960 retail XBE @ 0x00030970. Ghidra decompile:
//     if (obj->type == 4) { if (wcscmp(name, L"length") == 0)
//                               return new CNumObject((float)length); }
//     else if (obj->type == 2) return new CArrayElementReference(this, (int)num);
//     return CObject::Dot(obj);
CObject *CArrayObject::Dot(CObject *pObject)
{
	if (pObject->m_obj == objNumber)
	{
		int nIndex = (int)((CNumObject *)pObject)->m_value;
		return new CArrayElementReference(this, nIndex);
	}

	if (pObject->m_obj == objVariable &&
		_tcscmp(((CVarObject *)pObject)->m_text, _T("length")) == 0)
	{
		return new CNumObject((float)m_nLength);
	}

	return CObject::Dot(pObject);
}

// CArrayObject::concat(pArray2)   [script fn 'concat', FND 0x0017f5d8, sig 17]
// Reconstructed from 5960 retail XBE @ 0x00030a70. Ghidra decompile:
//     if (arg->type != 15) { CRunner::Error(L"object is not an array"); return NULL; }
//     out = new CArrayObject(length + arg->length);
//     out[0..length) = this (AddRef'd); out[length..) = arg (AddRef'd);
CObject *CArrayObject::concat(CObject *pObject)
{
	if (pObject->m_obj != objArray)
	{
		g_pRunner->Error(_T("object is not an array"));
		return NULL;
	}

	CArrayObject *pTail = (CArrayObject *)pObject;
	CArrayObject *pOut = new CArrayObject(m_nLength + pTail->m_nLength);

	for (int i = 0; i < m_nLength; i++)
		pOut->m_object[i] = Hold(m_object[i]);

	for (int i = 0; i < pTail->m_nLength; i++)
		pOut->m_object[m_nLength + i] = Hold(pTail->m_object[i]);

	return pOut;
}

// CArrayObject::join(szSeparator)   [script fn 'join', FND 0x0017f5f0, sig 5]
// Reconstructed from 5960 retail XBE @ 0x00030b80. Ghidra decompile:
//     out = new CStrObject;
//     for (i = 0; i < length; i++) {
//         if (!e[i]) out->Append(L"[null]");
//         else { s = e[i]->ToStr(); out->Append(s->text ? s->text : L""); s->Release(); }
//         if (i != length - 1) out->Append(sep);
//     }
CStrObject *CArrayObject::join(const TCHAR *szSeparator)
{
	CStrObject *pOut = new CStrObject;

	for (int i = 0; i < m_nLength; i++)
	{
		if (m_object[i] == NULL)
		{
			pOut->Append(_T("[null]"));
		}
		else
		{
			CStrObject *pText = m_object[i]->ToStr();
			pOut->Append(pText->GetSz());
			pText->Release();
		}

		if (i != m_nLength - 1)
			pOut->Append(szSeparator);
	}

	return pOut;
}

// CArrayObject::reverse()   [script fn 'reverse', FND 0x0017f608, sig 15]
// Reconstructed from 5960 retail XBE @ 0x00030c40. Ghidra decompile:
//     out = new CArrayObject(length);
//     for (i = 0; i < length; i++) out[length - 1 - i] = e[i] (AddRef'd);
// Returns a new array; the original is left alone.
CObject *CArrayObject::reverse()
{
	CArrayObject *pOut = new CArrayObject(m_nLength);

	for (int i = 0; i < m_nLength; i++)
		pOut->m_object[m_nLength - 1 - i] = Hold(m_object[i]);

	return pOut;
}

// CArrayObject::slice(rgparam, nParam)   [script fn 'slice', FND 0x0017f620, sig 0]
// Reconstructed from 5960 retail XBE @ 0x00030cd0. Ghidra decompile:
//     if (n < 1 || n > 2) { CRunner::Error(L"invalid number of parameters"); return NULL; }
//     start = (int)p[0]->Deref()->ToNum()->value;
//     end = (n == 2) ? (int)p[1]->Deref()->ToNum()->value : -1;
//     if (end < 0) end += length + 1;
//     count = max(end - start, 0);
//     out = new CArrayObject(count); out[i] = e[start + i] (AddRef'd);
// A negative end counts back from one past the last element, so the default
// -1 means "to the end". Start isn't range checked, same as the binary.
CObject *CArrayObject::slice(CObject **rgparam, int nParam /*int nStart, int nEnd*/)
{
	if (nParam < 1 || nParam > 2)
	{
		g_pRunner->Error(_T("invalid number of parameters"));
		return NULL;
	}

	int nStart = (int)rgparam[0]->Deref()->ToNum()->m_value;
	int nEnd = -1;
	if (nParam == 2)
		nEnd = (int)rgparam[1]->Deref()->ToNum()->m_value;

	if (nEnd < 0)
		nEnd += m_nLength + 1;

	int nCount = nEnd - nStart;
	if (nCount < 0)
		nCount = 0;

	CArrayObject *pOut = new CArrayObject(nCount);

	for (int i = 0; i < nCount; i++)
		pOut->m_object[i] = Hold(m_object[nStart + i]);

	return pOut;
}

// Sort comparator, 5960 retail XBE @ 0x00030e00. Ghidra decompile:
//     if (!a) return b ? -1 : 0;   if (!b) return 1;
//     sa = a->ToStr(); sb = b->ToStr();
//     r = wcscmp(sa->text ? sa->text : L"", sb->text ? sb->text : L"");
//     sa->Release(); sb->Release(); return r;
// Nulls sort first, everything else compares as text, like JS's default sort.
static int __cdecl CompareAsText(const void *pLeft, const void *pRight)
{
	CObject *pA = *(CObject *const *)pLeft;
	CObject *pB = *(CObject *const *)pRight;

	if (pA == NULL)
		return (pB == NULL) ? 0 : -1;
	if (pB == NULL)
		return 1;

	CStrObject *pTextA = pA->ToStr();
	CStrObject *pTextB = pB->ToStr();
	int nOrder = _tcscmp(pTextA->GetSz(), pTextB->GetSz());
	pTextA->Release();
	pTextB->Release();
	return nOrder;
}

// CArrayObject::sort()   [script fn 'sort', FND 0x0017f638, sig 15]
// Reconstructed from 5960 retail XBE @ 0x00030e70. Ghidra decompile:
//     out = new CArrayObject(length); out[i] = e[i] (AddRef'd);
//     qsort(out->elements, out->length, 4, compare);   return out;
CObject *CArrayObject::sort()
{
	CArrayObject *pOut = new CArrayObject(m_nLength);

	for (int i = 0; i < m_nLength; i++)
		pOut->m_object[i] = Hold(m_object[i]);

	qsort(pOut->m_object, pOut->m_nLength, sizeof (CObject *), CompareAsText);
	return pOut;
}
