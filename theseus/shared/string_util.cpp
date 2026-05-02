// string_util.cpp: CStrObject method implementations (the JS-style
// String built-in exposed to XAP scripts) plus a handful of script-
// callable utility functions. Decompiled from the 5960 retail XBE; see
// docs/decomp/StringObject.md.

#include "std.h"
#include "theseus.h"
#include "node.h"
#include "runner.h"

#define _FND_CLASS CStrObject
START_NODE_FUN(CStrObject, CObject)
	NODE_FUN_IV(length)
	NODE_FUN_II(charCodeAt)
	NODE_FUN_SI(charAt)
	NODE_FUN_SS(concat)
	NODE_FUN(indexOf)
	NODE_FUN(lastIndexOf)
	NODE_FUN(substr)
	NODE_FUN(substring)
	NODE_FUN_SV(toLowerCase)
	NODE_FUN_SV(toUpperCase)
END_NODE_FUN()
#undef _FND_CLASS


CStrObject::CStrObject()
{
	m_obj = objString;
	m_length = 0;
	m_capacity = 0;
	m_text = NULL;
}

CStrObject::CStrObject(const TCHAR* sz)
{
	if (sz == NULL)
		sz = _T("");

	m_obj = objString;
	m_length = _tcslen(sz);
	m_capacity = m_length + 1;
	m_text = new TCHAR [m_capacity];
	CopyChars(m_text, sz, m_length);
	m_text[m_length] = '\0';
}

CStrObject::CStrObject(const TCHAR* pch, int cch)
{
	m_obj = objString;
	m_length = cch;
	m_capacity = cch + 1;
	m_text = new TCHAR [m_capacity];
	CopyChars(m_text, pch, cch);
	m_text[cch] = '\0';
}

CStrObject::~CStrObject()
{
	delete [] m_text;
}

CStrObject* CStrObject::ToStr()
{
	AddRef();
	return this;
}

CNumObject* CStrObject::ToNum()
{
	return new CNumObject(GetSz());
}

TCHAR* CStrObject::SetLength(int nLength)
{
	delete [] m_text;

	m_length = nLength;
	m_capacity = nLength + 1;
	m_text = new TCHAR [m_capacity];
	ZeroMemory(m_text, m_capacity * sizeof (TCHAR));
	return m_text;
}

void CStrObject::Append(const TCHAR* szAppend)
{
	int cchAppend = _tcslen(szAppend);

	if (m_length + cchAppend + 1 > m_capacity)
	{
		int nAllocNew = m_length + cchAppend + 1;
		TCHAR* szNew = new TCHAR [nAllocNew];
		m_capacity = nAllocNew;
		CopyChars(szNew, m_text, m_length);
		delete [] m_text;
		m_text = szNew;
	}

	CopyChars(m_text + m_length, szAppend, cchAppend);
	m_length += cchAppend;
	m_text[m_length] = 0;
}


// Scriptable Methods...

int CStrObject::length()
{
	return m_length;
}

int CStrObject::charCodeAt(int index)
{
	if (index < 0 || index >= m_length)
		return -1;

	return m_text[index];
}

CStrObject* CStrObject::charAt(int index)
{
	if (index < 0 || index >= m_length)
		return new CStrObject(_T(""), 0);

	return new CStrObject(&m_text[index], 1);
}

CStrObject* CStrObject::concat(const TCHAR* sz)
{
	int cch = _tcslen(sz);
	
	CStrObject* pNewStr = new CStrObject;
	pNewStr->m_length = m_length + cch;
	pNewStr->m_capacity = pNewStr->m_length + 1;
	pNewStr->m_text = new TCHAR [pNewStr->m_capacity];
	CopyChars(pNewStr->m_text, m_text, m_length);
	CopyChars(pNewStr->m_text + m_length, sz, cch);
	pNewStr->m_text[pNewStr->m_length] = '\0';

	return pNewStr;
}

CObject* CStrObject::indexOf(CObject** rgparam, int nParam)
{
	if (nParam < 1 || nParam > 2)
	{
		g_pRunner->Error(_T("invalid number of parameters"));
		return NULL;
	}

	const TCHAR* szSubstring = rgparam[0]->ToStr()->GetSz();

	int nStartIndex = 0;
	if (nParam == 2)
	{
		nStartIndex = (int)rgparam[1]->ToNum()->m_value;

		if (nStartIndex < 0)
			nStartIndex = 0;
		else if (nStartIndex > m_length)
			nStartIndex = m_length;
	}

	int nRet = -1;

	if (nStartIndex < m_length)
	{
		const TCHAR* pch = _tcsstr(m_text + nStartIndex, szSubstring);
		if (pch != NULL)
			nRet = (int)(pch - m_text);
	}

	return new CNumObject((float)nRet);
}

const TCHAR* strrstr(const TCHAR* sz, const TCHAR* szFind, int nStartIndex)
{
	int cchSz = _tcslen(sz);
	int cchFind = _tcslen(szFind);

	if (cchSz < cchFind)
		return NULL;

	const TCHAR* pch = sz + nStartIndex - cchFind;
	while (pch >= sz)
	{
		if (_tcsncmp(pch, szFind, cchFind) == 0)
			return pch;
		pch -= 1;
	}

	return NULL;
}

CObject* CStrObject::lastIndexOf(CObject** rgparam, int nParam)
{
	if (nParam < 1 || nParam > 2)
	{
		g_pRunner->Error(_T("invalid number of parameters"));
		return NULL;
	}

	const TCHAR* szSubstring = rgparam[0]->ToStr()->GetSz();

	int nStartIndex = m_length;
	if (nParam == 2)
	{
		nStartIndex = (int)rgparam[1]->ToNum()->m_value;

		if (nStartIndex < 0)
			nStartIndex = 0;
		else if (nStartIndex > m_length)
			nStartIndex = m_length;
	}

	const TCHAR* pch = strrstr(m_text, szSubstring, nStartIndex);
	int nRet = -1;
	if (pch != NULL)
		nRet = (int)(pch - m_text);

	return new CNumObject((float)nRet);
}

CObject* CStrObject::substr(CObject** rgparam, int nParam)
{
	if (nParam < 1 || nParam > 2)
	{
		g_pRunner->Error(_T("invalid number of parameters"));
		return NULL;
	}

	int nStart = (int)rgparam[0]->ToNum()->m_value;

	int nLength = m_length - nStart;
	if (nParam == 2)
		nLength = (int)rgparam[1]->ToNum()->m_value;

	if (nStart > m_length)
		nStart = m_length;
	if (nLength <= 0)
		nLength = 0;
	else if (nStart + nLength > m_length)
		nLength = m_length - nStart;

	return new CStrObject(m_text + nStart, nLength);
}

CObject* CStrObject::substring(CObject** rgparam, int nParam)
{
	if (nParam != 2)
	{
		g_pRunner->Error(_T("invalid number of parameters"));
		return NULL;
	}

	int nStart = (int)rgparam[0]->ToNum()->m_value;
	if (nStart < 0)
		nStart = 0;

	int nEnd = (int)rgparam[1]->ToNum()->m_value;
	if (nEnd < 0)
		nEnd = nStart;

	if (nStart > nEnd)
	{
		int t = nStart;
		nStart = nEnd;
		nEnd = t;
	}

	if (nStart > m_length)
		nStart = m_length;
	if (nEnd > m_length)
		nEnd = m_length;

	return new CStrObject(m_text + nStart, nEnd - nStart);
}

CStrObject* CStrObject::toLowerCase()
{
	CStrObject* pStrObject = new CStrObject(m_text, m_length);
	_tcslwr(pStrObject->m_text);
	return pStrObject;
}

CStrObject* CStrObject::toUpperCase()
{
	CStrObject* pStrObject = new CStrObject(m_text, m_length);
	_tcsupr(pStrObject->m_text);
	return pStrObject;
}
