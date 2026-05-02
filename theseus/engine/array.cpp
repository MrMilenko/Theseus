// array.cpp: typed-array containers exposed to XAP scripts (CIntArray,
// CNumArray, CVec2Array, CVec3Array, CVec4Array, CNodeArray). PRD entries
// of pt_intarray / pt_numarray / pt_vec*array / pt_children point at
// fields of these classes. Decompiled from the 5960 retail XBE.

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

CArrayElementReference::CArrayElementReference(CArrayObject *pArray, int nIndex)
{
	ASSERT(pArray != NULL);
	m_pArray = pArray;
	m_pArray->AddRef();
	m_nIndex = nIndex;
}

CArrayElementReference::~CArrayElementReference()
{
	m_pArray->Release();
}

void CArrayElementReference::Assign(CObject *pObject)
{
	if (m_nIndex >= m_pArray->m_nLength)
		m_pArray->Grow(m_nIndex + 1);

	CObject *pOldObject = m_pArray->m_object[m_nIndex];
	if (pOldObject != NULL)
		pOldObject->Release();

	pObject->AddRef();
	m_pArray->m_object[m_nIndex] = pObject;
}

CObject *CArrayElementReference::Deref()
{
	if (m_nIndex < 0 || m_nIndex >= m_pArray->m_nLength)
	{
		g_pRunner->Error(_T("Array reference out of bounds"));
		return NULL;
	}

	CObject *pObject = m_pArray->m_object[m_nIndex];
	if (pObject != NULL)
		pObject->AddRef();
	Release();
	return pObject;
}

IMPLEMENT_NODE("Array", CArrayObject, CObject)

#define _FND_CLASS CArrayObject
START_NODE_FUN(CArrayObject, CObject)
NODE_FUN_OO(concat)
NODE_FUN_SS(join)
NODE_FUN_OV(reverse)
NODE_FUN(slice)
NODE_FUN_OV(sort)
END_NODE_FUN()
#undef _FND_CLASS

CArrayObject::CArrayObject()
{
	m_obj = objArray;

	int nLength = 0;

	if (g_nParam == 1)
	{
		g_rgParam[0] = Dereference(g_rgParam[0]);
		CNumObject *pNum = g_rgParam[0]->ToNum();
		nLength = (int)pNum->m_value;
		pNum->Release();
	}
	else if (g_nParam > 1)
	{
		nLength = g_nParam;
	}

	if (nLength == 0)
	{
		m_nLength = 0;
		m_nAlloc = 0;
		m_object = NULL;
	}
	else
	{
		m_nLength = nLength;
		m_nAlloc = nLength;
		m_object = new CObject *[nLength];
		ZeroMemory(m_object, nLength * sizeof(CObject *));
	}

	if (g_nParam > 1)
	{
		int i;
		for (i = 0; i < g_nParam; i += 1)
		{
			if (g_rgParam[i] != NULL)
				g_rgParam[i]->AddRef();
			m_object[i] = g_rgParam[i];
		}
	}
}

CArrayObject::CArrayObject(int nLength)
{
	m_nLength = nLength;
	m_nAlloc = nLength;

	if (nLength == 0)
	{
		m_object = NULL;
	}
	else
	{
		m_object = new CObject *[nLength];
		ZeroMemory(m_object, nLength * sizeof(CObject *));
	}
}

CArrayObject::~CArrayObject()
{
	for (int i = 0; i < m_nLength; i += 1)
	{
		if (m_object[i] != NULL)
			m_object[i]->Release();
	}

	delete[] m_object;
}

void CArrayObject::Grow(int nNewLength)
{
	if (nNewLength <= m_nLength)
		return;

	if (nNewLength > m_nAlloc)
	{
		int nNewAlloc = nNewLength + (nNewLength / 2) + 16;
		CObject **object = new CObject *[nNewAlloc];
		CopyMemory(object, m_object, m_nLength * sizeof(CObject *));
		delete[] m_object;
		m_object = object;
		m_nAlloc = nNewAlloc;
	}

	ZeroMemory(m_object + m_nLength, (nNewLength - m_nLength) * sizeof(CObject *));
	m_nLength = nNewLength;
}

CStrObject *CArrayObject::ToStr()
{
	CStrObject *pStr = join(_T(","));
	return pStr;
}

CObject *CArrayObject::Dot(CObject *pObject)
{
	if (pObject->m_obj == objVariable)
	{
		if (_tcscmp(((CVarObject *)pObject)->m_text, _T("length")) == 0)
		{
			return new CNumObject((float)m_nLength);
		}
	}
	else if (pObject->m_obj == objNumber)
	{
		int nIndex = (int)((CNumObject *)pObject)->m_value;
		return new CArrayElementReference(this, nIndex);
	}

	return CObject::Dot(pObject);
}

CObject *CArrayObject::concat(CObject *pObject)
{
	if (pObject->m_obj != objArray)
	{
		g_pRunner->Error(_T("object is not an array"));
		return NULL;
	}

	CArrayObject *pArray2 = (CArrayObject *)pObject;

	CArrayObject *pNewArray = new CArrayObject(m_nLength + pArray2->m_nLength);

	int i;
	for (i = 0; i < m_nLength; i += 1)
	{
		CObject *pObject = m_object[i];
		if (pObject != NULL)
			pObject->AddRef();
		pNewArray->m_object[i] = pObject;
	}

	for (i = 0; i < pArray2->m_nLength; i += 1)
	{
		CObject *pObject = pArray2->m_object[i];
		if (pObject != NULL)
			pObject->AddRef();
		pNewArray->m_object[m_nLength + i] = pObject;
	}

	return pNewArray;
}

CStrObject *CArrayObject::join(const TCHAR *szSeperator)
{
	CStrObject *pNewString = new CStrObject;

	int i;
	for (i = 0; i < m_nLength; i += 1)
	{
		CObject *pObject = m_object[i];
		if (pObject != NULL)
		{
			CStrObject *pStr = pObject->ToStr();
			pNewString->Append(pStr->GetSz());
			pStr->Release();
		}
		else
		{
			pNewString->Append(_T("[null]"));
		}

		if (i != m_nLength - 1)
			pNewString->Append(szSeperator);
	}

	return pNewString;
}

CObject *CArrayObject::reverse()
{
	CArrayObject *pNewArray = new CArrayObject(m_nLength);

	int i;
	for (i = 0; i < m_nLength; i += 1)
	{
		CObject *pObject = m_object[i];
		if (pObject != NULL)
			pObject->AddRef();
		pNewArray->m_object[m_nLength - i - 1] = pObject;
	}

	return pNewArray;
}

CObject *CArrayObject::slice(CObject **rgparam, int nParam /*int nStart, int nEnd*/)
{
	int nStart = 0;
	int nEnd = -1;

	if (nParam < 1 || nParam > 2)
	{
		g_pRunner->Error(_T("invalid number of parameters"));
		return NULL;
	}

	nStart = (int)rgparam[0]->Deref()->ToNum()->m_value;
	if (nParam == 2)
		nEnd = (int)rgparam[1]->Deref()->ToNum()->m_value;

	if (nEnd < 0)
		nEnd = m_nLength + 1 + nEnd;

	int nLength = nEnd - nStart;
	if (nLength < 0)
		nLength = 0;

	CArrayObject *pNewArray = new CArrayObject(nLength);

	for (int i = 0; i < nLength; i += 1)
	{
		CObject *pObject = m_object[nStart + i];
		if (pObject != NULL)
			pObject->AddRef();
		pNewArray->m_object[i] = pObject;
	}

	return pNewArray;
}

static int __cdecl SortCompare(const void *elem1, const void *elem2)
{
	CObject *pObj1 = *(CObject **)elem1;
	CObject *pObj2 = *(CObject **)elem2;

	if (pObj1 == NULL)
	{
		if (pObj2 == NULL)
			return 0;

		return -1;
	}

	if (pObj2 == NULL)
		return 1;

	CStrObject *pStr1 = pObj1->ToStr();
	CStrObject *pStr2 = pObj2->ToStr();

	int nRet = _tcscmp(pStr1->GetSz(), pStr2->GetSz());

	pStr1->Release();
	pStr2->Release();

	return nRet;
}

CObject *CArrayObject::sort()
{
	CArrayObject *pNewArray = new CArrayObject(m_nLength);

	for (int i = 0; i < m_nLength; i += 1)
	{
		CObject *pObject = m_object[i];
		if (pObject != NULL)
			pObject->AddRef();
		pNewArray->m_object[i] = pObject;
	}

	qsort(pNewArray->m_object, pNewArray->m_nLength, sizeof(CObject *), SortCompare);

	return pNewArray;
}
