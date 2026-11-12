// string_util.cpp: the script String object. A counted buffer with the
// handful of JavaScript string methods the dashboard scripts call. Rebuilt
// function by function from the 5960 retail XBE (0x32040..0x328c0); see
// docs/decomp/StringObject.md.

#include "std.h"
#include "theseus.h"
#include "node.h"
#include "runner.h"

// FND at 0x0017f9c0, same order.
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


// ===== The buffer ==========================================================

// CStrObject::CStrObject()
// Reconstructed from 5960 retail XBE @ 0x00032040: type 3, no buffer.
CStrObject::CStrObject()
{
	m_obj = objString;
	m_text = NULL;
	m_length = 0;
	m_capacity = 0;
}

// CStrObject::CStrObject(pch, cch)
// Reconstructed from 5960 retail XBE @ 0x00032130:
//     length = cch; capacity = cch + 1; text = new WCHAR[cch + 1];
//     copy cch characters; text[cch] = 0;
CStrObject::CStrObject(const TCHAR* pch, int cch)
{
	m_obj = objString;
	m_length = cch;
	m_capacity = cch + 1;
	m_text = new TCHAR[m_capacity];
	CopyChars(m_text, pch, cch);
	m_text[cch] = 0;
}

// CStrObject::CStrObject(sz)
// Reconstructed from 5960 retail XBE @ 0x00032090: the counted constructor
// on wcslen(sz), with NULL read as "".
CStrObject::CStrObject(const TCHAR* sz)
{
	if (sz == NULL)
		sz = _T("");
	m_obj = objString;
	m_length = (int)_tcslen(sz);
	m_capacity = m_length + 1;
	m_text = new TCHAR[m_capacity];
	CopyChars(m_text, sz, m_length + 1);
}

// CStrObject::~CStrObject()
// Reconstructed from 5960 retail XBE @ 0x000321d0: frees the buffer.
CStrObject::~CStrObject()
{
	delete[] m_text;
}

CStrObject* CStrObject::ToStr()
{
	AddRef();
	return this;
}

// CStrObject::ToNum()
// Reconstructed from 5960 retail XBE @ 0x00032200: a number object from
// wcstod of the text (or "").
CNumObject* CStrObject::ToNum()
{
	return new CNumObject(GetSz());
}

// A fresh zeroed buffer of nLength characters plus the null.
TCHAR* CStrObject::SetLength(int nLength)
{
	delete[] m_text;
	m_length = nLength;
	m_capacity = nLength + 1;
	m_text = new TCHAR[m_capacity];
	memset(m_text, 0, m_capacity * sizeof(TCHAR));
	return m_text;
}

// CStrObject::Append(sz)
// Reconstructed from 5960 retail XBE @ 0x00032260. Grows the buffer to
// exactly length + n + 1 when it's too small, then copies sz on the end.
void CStrObject::Append(const TCHAR* szAppend)
{
	int add = (int)_tcslen(szAppend);
	int need = m_length + add + 1;
	if (need > m_capacity)
	{
		TCHAR* grown = new TCHAR[need];
		CopyChars(grown, m_text, m_length);
		delete[] m_text;
		m_text = grown;
		m_capacity = need;
	}
	CopyChars(m_text + m_length, szAppend, add);
	m_length += add;
	m_text[m_length] = 0;
}


// ===== Script methods ======================================================

// Reads a script argument as a number and lets go of the temporary.
static int ArgInt(CObject* arg)
{
	CNumObject* num = arg->ToNum();
	int value = (int)num->m_value;
	num->Release();
	return value;
}

static int Clamp(int value, int low, int high)
{
	if (value < low)
		return low;
	if (value > high)
		return high;
	return value;
}

// CStrObject::length()   [script fn 'length', FND 0x0017f9c0, sig 2]
int CStrObject::length()
{
	return m_length;
}

// CStrObject::charCodeAt(index)   [script fn 'charCodeAt', FND 0x0017f9d8, sig 3]
// Reconstructed from 5960 retail XBE @ 0x00032300:
//     return (index >= 0 && index < length) ? text[index] : -1;
int CStrObject::charCodeAt(int index)
{
	if (index < 0 || index >= m_length)
		return -1;
	return m_text[index];
}

// CStrObject::charAt(index)   [script fn 'charAt', FND 0x0017f9f0, sig 4]
// Reconstructed from 5960 retail XBE @ 0x00032320: a one character string,
// where the binary returns no object at all out of range. Theseus returns an
// empty string there.
CStrObject* CStrObject::charAt(int index)
{
	if (index < 0 || index >= m_length)
		return new CStrObject(_T(""), 0);
	return new CStrObject(m_text + index, 1);
}

// CStrObject::concat(sz)   [script fn 'concat', FND 0x0017fa08, sig 5]
// Reconstructed from 5960 retail XBE @ 0x000323a0: a new string sized
// exactly for both, this one first.
CStrObject* CStrObject::concat(const TCHAR* sz)
{
	int add = (int)_tcslen(sz);
	CStrObject* out = new CStrObject;
	out->m_length = m_length + add;
	out->m_capacity = out->m_length + 1;
	out->m_text = new TCHAR[out->m_capacity];
	CopyChars(out->m_text, m_text, m_length);
	CopyChars(out->m_text + m_length, sz, add);
	out->m_text[out->m_length] = 0;
	return out;
}

// CStrObject::indexOf(find [, from])   [script fn 'indexOf', FND 0x0017fa20, sig 0]
// Reconstructed from 5960 retail XBE @ 0x00032460. One or two arguments or
// "invalid number of parameters". from is clamped to 0..length; when it's
// short of the end, wcsstr from there; the answer as a number, -1 for none.
CObject* CStrObject::indexOf(CObject** rgparam, int nParam)
{
	if (nParam != 1 && nParam != 2)
	{
		g_pRunner->Error(_T("invalid number of parameters"));
		return NULL;
	}

	int from = nParam == 2 ? Clamp(ArgInt(rgparam[1]), 0, m_length) : 0;
	CStrObject* find = rgparam[0]->ToStr();
	int found = -1;
	if (from < m_length)
	{
		const TCHAR* hit = _tcsstr(m_text + from, find->GetSz());
		if (hit != NULL)
			found = (int)(hit - m_text);
	}
	find->Release();
	return new CNumObject((float)found);
}

// The last place find starts with its end at or before text + before.
static const TCHAR* FindLast(const TCHAR* text, const TCHAR* find, int before)
{
	int textLen = (int)_tcslen(text);
	int findLen = (int)_tcslen(find);
	if (textLen < findLen)
		return NULL;
	for (int at = before - findLen; at >= 0; at--)
	{
		if (_tcsncmp(text + at, find, findLen) == 0)
			return text + at;
	}
	return NULL;
}

// CStrObject::lastIndexOf(find [, from])   [script fn 'lastIndexOf', FND 0x0017fa38, sig 0]
// Reconstructed from 5960 retail XBE @ 0x000325b0 (search @ 0x00032550).
// Same argument rules as indexOf, from defaulting to the length. Walks back
// from text + from - len(find) comparing with wcsncmp. When nothing matches
// the binary hands back the pointer it stopped at, which can come out below
// -1; Theseus answers -1.
CObject* CStrObject::lastIndexOf(CObject** rgparam, int nParam)
{
	if (nParam != 1 && nParam != 2)
	{
		g_pRunner->Error(_T("invalid number of parameters"));
		return NULL;
	}

	int from = nParam == 2 ? Clamp(ArgInt(rgparam[1]), 0, m_length) : m_length;
	CStrObject* find = rgparam[0]->ToStr();
	const TCHAR* hit = FindLast(GetSz(), find->GetSz(), from);
	find->Release();
	return new CNumObject(hit != NULL ? (float)(hit - GetSz()) : -1.0f);
}

// CStrObject::substr(start [, count])   [script fn 'substr', FND 0x0017fa50, sig 0]
// Reconstructed from 5960 retail XBE @ 0x00032680. count defaults to
// length - start; start is capped at length; a count of 0 or less gives "",
// one running past the end stops at it.
CObject* CStrObject::substr(CObject** rgparam, int nParam)
{
	if (nParam != 1 && nParam != 2)
	{
		g_pRunner->Error(_T("invalid number of parameters"));
		return NULL;
	}

	int start = ArgInt(rgparam[0]);
	int count = nParam == 2 ? ArgInt(rgparam[1]) : m_length - start;
	if (start > m_length)
		start = m_length;
	if (count <= 0)
		count = 0;
	else if (start + count > m_length)
		count = m_length - start;
	return new CStrObject(m_text + start, count);
}

// CStrObject::substring(start, end)   [script fn 'substring', FND 0x0017fa68, sig 0]
// Reconstructed from 5960 retail XBE @ 0x00032780. Exactly two arguments.
// A negative start is 0, a negative end is start; the two swap when out of
// order, then both are capped at length.
CObject* CStrObject::substring(CObject** rgparam, int nParam)
{
	if (nParam != 2)
	{
		g_pRunner->Error(_T("invalid number of parameters"));
		return NULL;
	}

	int start = ArgInt(rgparam[0]);
	if (start < 0)
		start = 0;
	int end = ArgInt(rgparam[1]);
	if (end < 0)
		end = start;
	if (start > end)
	{
		int swap = start;
		start = end;
		end = swap;
	}
	if (start > m_length)
		start = m_length;
	if (end > m_length)
		end = m_length;
	return new CStrObject(m_text + start, end - start);
}

// CStrObject::toLowerCase() / toUpperCase()   [script fns, FND 0x0017fa80 / 0x0017fa98, sig 8]
// Reconstructed from 5960 retail XBE @ 0x00032850 / 0x000328c0: a copy,
// then wcslwr / wcsupr on it.
CStrObject* CStrObject::toLowerCase()
{
	CStrObject* out = new CStrObject(m_text, m_length);
	_tcslwr(out->m_text);
	return out;
}

CStrObject* CStrObject::toUpperCase()
{
	CStrObject* out = new CStrObject(m_text, m_length);
	_tcsupr(out->m_text);
	return out;
}
