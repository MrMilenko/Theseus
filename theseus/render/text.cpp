// text.cpp: XTF vector fonts, text meshes and the Text node.
//
// A font is a .xtf file of pretriangulated glyphs. CreateTextMesh lays a
// string out into one D3DX mesh (word wrap, two columns, *heading* lines,
// the keyboard cursor), the Text node draws and scrolls that mesh, and the
// overlay calls at the bottom draw the same glyphs straight to the screen.
// Rebuilt function by function from the 5960 retail XBE; the findings are in
// docs/decomp/Text.md.

#include "std.h"
#include "theseus.h"
#include "node.h"
#include "locale_node.h"
#include "xap_compile.h"
#include "runner.h"

extern void TheseusCreateMeshFVF(DWORD NumFaces, DWORD NumVertices, DWORD Options, DWORD FVF, LPD3DXMESH* ppMesh);

// The keyboard hands its edit string and cursor to the next Text node that
// renders (0x0017e748, 0x00175220, 0x00175224 in the binary).
TCHAR* g_szText = NULL;
int g_nTextChar = -1;
int g_nTextCharLast = -1;

// *heading* formatting, off until a script turns it on through the keyboard's
// SetAsteriskFormattingState (0x0017e74c, a BSS byte in 5960).
bool g_bAsteriskFormatting = false;

// D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE (0x52), 28 bytes a vertex.
struct TEXTVERTEX
{
	float x, y, z;
	float nx, ny, nz;
	DWORD color;
};

#define TEXT_FVF (D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE)

static inline void SetVertexAlpha(TEXTVERTEX& v, float a)
{
	v.color = (v.color & 0x00ffffff) | ((DWORD)a << 24);
}


// ===== XTF files ===========================================================
// "XTF0", a 32 byte face name block, the glyph set (code point ranges), one
// 28 byte record per glyph, then the glyph shapes at the offsets the records
// give. The layout is fixed by the file, so these structs match it exactly.

struct XtfRange
{
	WORD first;
	WORD count;
};

struct XtfGlyphSet
{
	DWORD cbThis;
	DWORD flAccel;
	DWORD cGlyphs;
	DWORD cRanges;
	XtfRange ranges[1];
};

struct XtfGlyph
{
	float blackBoxX, blackBoxY;
	float originX, originY;
	float advanceX, advanceY;
	DWORD shapeOffset;
};

struct XtfPoint
{
	float x, y;
};

struct CGlyphShape
{
	WORD nIndices;
	WORD nPoints;
	WORD* indices;
	XtfPoint* points;
};

#define XTF0_MAGIC 0x30465458
#define XTF_FACE_LEN 32

// The binary refuses any single font read over 20 MB (0x00053840).
static bool ReadExact(HANDLE hFile, void* pv, DWORD cb)
{
	if (cb > 0x1400000)
		return false;
	DWORD cbRead = 0;
	return ReadFile(hFile, pv, cb, &cbRead, NULL) && cbRead == cb;
}

static void FreeShape(CGlyphShape* pShape)
{
	delete [] pShape->indices;
	delete [] pShape->points;
	delete pShape;
}

class CFont
{
public:
	CFont();
	~CFont();

	bool Open(const TCHAR* szFile);
	void Close();
	bool IsOpen() const { return m_pSet != NULL; }

	int FindGlyphIndex(WCHAR wch);
	bool LoadGlyph(int nGlyph);
	float Advance(int nGlyph) const { return m_pGlyphs[nGlyph].advanceX; }
	CGlyphShape* Shape(int nGlyph) const { return m_ppShapes[nGlyph]; }

	void CreateTextMesh(const TCHAR* pchText, int nChars, LPD3DXMESH* ppMesh, D3DXVECTOR3* pMin, D3DXVECTOR3* pMax, float nFormatWidth, bool bDoNotBreak, float scale,
		float indentWidth = 0.0f, float indentHeight = 0.0f);

	HANDLE m_hFile;

private:
	XtfGlyphSet* m_pSet;
	XtfGlyph* m_pGlyphs;
	CGlyphShape** m_ppShapes;
};

CFont::CFont() :
	m_hFile(INVALID_HANDLE_VALUE),
	m_pSet(NULL),
	m_pGlyphs(NULL),
	m_ppShapes(NULL)
{
}

CFont::~CFont()
{
	Close();
}

// CFont::Close()
// Reconstructed from 5960 retail XBE @ 0x00053870:
//     if (hFile != -1) { CloseHandle(hFile); hFile = -1; }
//     for (i < glyphSet->cGlyphs) if (loaded[i] && shape[i]) free indices, points, shape;
//     free glyphSet, glyph records, loaded flags (plus the XTF1 buffers);
void CFont::Close()
{
	if (m_hFile != INVALID_HANDLE_VALUE)
	{
		CloseHandle(m_hFile);
		m_hFile = INVALID_HANDLE_VALUE;
	}

	if (m_ppShapes != NULL)
	{
		for (DWORD i = 0; i < m_pSet->cGlyphs; i++)
		{
			if (m_ppShapes[i] != NULL)
				FreeShape(m_ppShapes[i]);
		}
	}

	delete [] (BYTE*)m_pSet;
	delete [] m_pGlyphs;
	delete [] m_ppShapes;
	m_pSet = NULL;
	m_pGlyphs = NULL;
	m_ppShapes = NULL;
}

// CFont::Open(szFile)
// Reconstructed from 5960 retail XBE @ 0x00053ce0 (open + magic) and
// 0x00053970 (the XTF0 body). Ghidra decompile, trimmed:
//     hFile = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING);
//     read magic; XTF0 -> 0x53970, XTF1 -> LZX setup + 0x53b00, else close and fail;
//     0x53970: read header length (4), face (0x20), cbSet (4); need cbSet > 0x13;
//         set = alloc(cbSet); set->cbThis = cbSet; read cbSet - 4 more;
//         need cRanges * 4 + 0x10 == cbSet;
//         glyphs = alloc(cGlyphs * 0x1c); read them all; loaded = zeroed(cGlyphs);
// Every read must return its full size. The 5960 load also checks the file
// against the dash's signed hash table (0x0003eae0) and reads the compressed
// XTF1 format; Theseus ships XTF0 fonts only and skips both. A failed open
// closes the file here, where the binary leaves it for Close.
bool CFont::Open(const TCHAR* szFile)
{
	Close();

	m_hFile = TheseusCreateFile(szFile, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (m_hFile == INVALID_HANDLE_VALUE)
		return false;

	DWORD dwMagic = 0, cbHeader = 0, cbSet = 0;
	char face[XTF_FACE_LEN];
	bool ok = ReadExact(m_hFile, &dwMagic, 4) && dwMagic == XTF0_MAGIC &&
		ReadExact(m_hFile, &cbHeader, 4) &&
		ReadExact(m_hFile, face, XTF_FACE_LEN) &&
		ReadExact(m_hFile, &cbSet, 4) && cbSet >= 20;

	if (ok)
	{
		m_pSet = (XtfGlyphSet*)new BYTE[cbSet];
		m_pSet->cbThis = cbSet;
		ok = ReadExact(m_hFile, &m_pSet->flAccel, cbSet - 4) &&
			16 + m_pSet->cRanges * sizeof (XtfRange) == cbSet;
	}

	if (ok)
	{
		DWORD nGlyphs = m_pSet->cGlyphs;
		m_pGlyphs = new XtfGlyph[nGlyphs];
		ok = ReadExact(m_hFile, m_pGlyphs, nGlyphs * sizeof (XtfGlyph));

		m_ppShapes = new CGlyphShape*[nGlyphs];
		for (DWORD i = 0; i < nGlyphs; i++)
			m_ppShapes[i] = NULL;
	}

	if (!ok)
		Close();
	return ok;
}

// CFont::LoadGlyph(nGlyph)
// Reconstructed from 5960 retail XBE @ 0x00053ed0 (XTF0). Ghidra decompile:
//     if (loaded[n]) return 1;
//     off = glyphs[n].shape; if (off == 0) return 0;
//     if (SetFilePointer(hFile, off, 0, FILE_BEGIN) == -1) return 0;
//     s = new(0xc); read s->nIndices (2); s->indices = new(nIndices * 2);
//     read s->nPoints (2); s->points = new(nPoints * 8);
//     read indices, read points, each checked against its byte count;
//     glyphs[n].shape = s; loaded[n] = 1; return 1;
// The record's offset and the loaded shape live apart here so the record can
// stay the file's 28 bytes on 64 bit hosts. A failed load frees what it made.
bool CFont::LoadGlyph(int nGlyph)
{
	if (m_ppShapes[nGlyph] != NULL)
		return true;

	DWORD offset = m_pGlyphs[nGlyph].shapeOffset;
	if (offset == 0 || SetFilePointer(m_hFile, offset, NULL, FILE_BEGIN) == (DWORD)-1)
		return false;

	CGlyphShape* pShape = new CGlyphShape;
	pShape->indices = NULL;
	pShape->points = NULL;

	bool ok = ReadExact(m_hFile, &pShape->nIndices, 2);
	if (ok)
	{
		pShape->indices = new WORD[pShape->nIndices];
		ok = ReadExact(m_hFile, &pShape->nPoints, 2);
	}
	if (ok)
	{
		pShape->points = new XtfPoint[pShape->nPoints];
		ok = ReadExact(m_hFile, pShape->indices, pShape->nIndices * sizeof (WORD)) &&
			ReadExact(m_hFile, pShape->points, pShape->nPoints * sizeof (XtfPoint));
	}

	if (!ok)
	{
		FreeShape(pShape);
		return false;
	}

	m_ppShapes[nGlyph] = pShape;
	return true;
}

// CFont::FindGlyphIndex(wch)
// Reconstructed from 5960 retail XBE @ 0x000541c0. Ghidra decompile:
//     for (;;) {
//         base = 0;
//         for (i < set->cRanges) {
//             if (r[i].first <= ch && ch < r[i].first + r[i].count) return ch - r[i].first + base;
//             base += r[i].count;
//         }
//         if (ch == 0x7f) return 0;
//         ch = 0x7f;                      // unknown characters draw as the DEL box
//     }
// The NULL set check is Theseus's, for a font that failed to open.
int CFont::FindGlyphIndex(WCHAR wch)
{
	if (m_pSet == NULL)
		return -1;

	for (;;)
	{
		int base = 0;
		for (DWORD i = 0; i < m_pSet->cRanges; i++)
		{
			const XtfRange& range = m_pSet->ranges[i];
			if (wch >= range.first && wch < range.first + range.count)
				return base + (wch - range.first);
			base += range.count;
		}

		if (wch == 0x7f)
			return 0;
		wch = 0x7f;
	}
}


// ===== Fades ===============================================================

// Hermite ease on [0, 1], clamped; the binary inlines it as (3 - 2a) * a * a.
static inline float Ease(float a)
{
	if (a <= 0.0f)
		return 0.0f;
	if (a >= 1.0f)
		return 1.0f;
	return a * a * (3.0f - 2.0f * a);
}

// FadeEdge(verts, n, nStart, nEnd)
// Reconstructed from 5960 retail XBE @ 0x00054290 (HorizontalFade). Ghidra
// decompile, trimmed (verts in EAX, nEnd in XMM1, fade out flag in BL):
//     inv = 1.0 / (nEnd - nStart);
//     for (i < n) { a = ease((v[i].x - nStart) * inv); if (out) a = 1.0 - a;
//                   v[i].alpha = (int)(v[i].alpha * a); }
// Text only ever fades out toward the right edge of a column, so this takes
// no direction. Divides where the binary multiplies by the reciprocal.
static void FadeEdge(TEXTVERTEX* verts, int nVerts, float nStart, float nEnd)
{
	float width = nEnd - nStart;
	for (int i = 0; i < nVerts; i++)
	{
		float keep = 1.0f - Ease((verts[i].x - nStart) / width);
		SetVertexAlpha(verts[i], (float)(verts[i].color >> 24) * keep);
	}
}

// Window clip for the horizontal marquee (Theseus, no 5960 counterpart):
// alpha 0 outside [nLeft, nRight], eased over the last unit on each side.
static void HorizontalFade(TEXTVERTEX* verts, int nVertexCount, float nLeft, float nRight, float nScroll)
{
	for (int i = 0; i < nVertexCount; i += 1)
	{
		float a = 1.0f;
		float x = verts[i].x + nScroll;

		if (x < nLeft || x > nRight)
		{
			a = 0.0f;
		}
		else if (x < nLeft + 1.0f)
		{
			a = 1.0f - ((nLeft + 1.0f) - x);
		}
		else if (x > nRight - 1.0f)
		{
			a = 1.0f - (x - (nRight - 1.0f));
		}

		if (a < 0.0f) a = 0.0f;
		if (a > 1.0f) a = 1.0f;
		a = Ease(a);
		SetVertexAlpha(verts[i], 255.0f * a);
	}
}

// VerticalFade(verts, n, nTop, nBottom, nScroll)
// Reconstructed from 5960 retail XBE @ 0x00054380. Ghidra decompile, trimmed
// (nTop in XMM4, nBottom in XMM5):
//     y = v.y + scroll;
//     if (y > top || y < bottom)  a = 0;
//     else if (y > top - 1)       a = 1 - (y - (top - 1));
//     else if (y < bottom + 1)    a = 1 - ((bottom + 1) - y);
//     else                        a = 1;
//     byte at v + 0x1b = (int)(ease(a) * 255.0);
static void VerticalFade(TEXTVERTEX* verts, int nVerts, float nTop, float nBottom, float nScroll)
{
	float innerTop = nTop - 1.0f;
	float innerBottom = nBottom + 1.0f;

	for (int i = 0; i < nVerts; i++)
	{
		float y = verts[i].y + nScroll;
		float a = 1.0f;
		if (y > nTop || y < nBottom)
			a = 0.0f;
		else if (y > innerTop)
			a = 1.0f - (y - innerTop);
		else if (y < innerBottom)
			a = 1.0f - (innerBottom - y);

		SetVertexAlpha(verts[i], 255.0f * Ease(a));
	}
}


// ===== Text meshes =========================================================

// Characters come in as UTF-16 units on the Xbox and UTF-8 on desktop.
#ifndef _UNICODE
// Decode one UTF-8 codepoint from s starting at i (n is the byte count).
// Returns the codepoint and sets outLen to the bytes consumed. A stray or
// truncated byte decodes as itself (outLen 1) so ASCII and Latin-1 fallbacks
// still render something. BMP only; astral planes truncate, which the vector
// fonts don't cover anyway.
static WCHAR DecodeUTF8(const char* s, int i, int n, int& outLen)
{
	unsigned char c = (unsigned char)s[i];
	if (c < 0x80) { outLen = 1; return c; }
	unsigned int cp; int len;
	if      ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
	else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
	else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
	else { outLen = 1; return c; }
	if (i + len > n) { outLen = 1; return c; }
	for (int k = 1; k < len; k++) {
		unsigned char cc = (unsigned char)s[i + k];
		if ((cc & 0xC0) != 0x80) { outLen = 1; return c; }
		cp = (cp << 6) | (cc & 0x3F);
	}
	outLen = len;
	return (WCHAR)cp;
}
#endif

static inline WCHAR NextChar(const TCHAR* s, int& i, int n)
{
#ifdef _UNICODE
	(void)n;
	return s[i++];
#else
	int len;
	WCHAR ch = DecodeUTF8(s, i, n, len);
	i += len;
	return ch;
#endif
}

static inline bool IsLineEnd(TCHAR ch)
{
	return ch == '\t' || ch == '\n' || ch == '\r';
}

// True when text[i] is the last character of its column.
static inline bool AtColumnEnd(const TCHAR* text, int i, int n)
{
	return i == n || IsLineEnd(text[i]);
}

// A column that starts and ends with '*' is a heading: the stars are hidden
// and the text is tinted. True when text[i] opens one and headings are on.
static bool OpensHeading(const TCHAR* text, int i, int n)
{
	if (!g_bAsteriskFormatting || i >= n || text[i] != '*')
		return false;
	int end = i + 1;
	while (end < n && !IsLineEnd(text[end]))
		end++;
	return end > i + 1 && text[end - 1] == '*';
}

// Line breaks may follow a space, kana or CJK (0x3040..0xefff), or half
// width kana and up (0xff66).
static inline bool CanBreakAfter(WCHAR ch)
{
	return ch == ' ' || (ch >= 0x3040 && ch < 0xf000) || ch >= 0xff66;
}

// Glyphs past index 190 come from the Japanese set and are drawn 10% smaller.
// Theseus's; the 5960 layout uses every glyph at full size.
static inline float GlyphScale(int nGlyph)
{
	return nGlyph > 190 ? 0.9f : 1.0f;
}

// Appends the blinking cursor quad, 0.1 wide from y - 0.1 to y + 0.75.
// Reconstructed from 5960 retail XBE @ 0x00055030 (CFont::CreateCursorMesh):
//     a = |sin((now + now) * PI)|; if (!visible) a = 0; color = (int)(a * 255) << 24 | 0xffffff;
//     indices += { v, v+1, v+2, v, v+2, v+3 };
//     verts: (x, y-0.1) (x+0.1, y-0.1) (x+0.1, y+0.75) (x, y+0.75), normal (0, 0, 1);
static void AddCursor(WORD* indices, int& nIndex, TEXTVERTEX* verts, int& nVertex, float x, float y, bool visible)
{
	float a = visible ? fabsf(sinf((float)(TheseusGetNow() + TheseusGetNow()) * D3DX_PI)) : 0.0f;
	DWORD color = ((DWORD)(a * 255.0f) << 24) | 0x00ffffff;

	static const int quad[6] = { 0, 1, 2, 0, 2, 3 };
	for (int i = 0; i < 6; i++)
		indices[nIndex++] = (WORD)(nVertex + quad[i]);

	const float left = x, right = x + 0.1f;
	const float bottom = y - 0.1f, top = y + 0.75f;
	const float corners[4][2] = { { left, bottom }, { right, bottom }, { right, top }, { left, top } };
	for (int i = 0; i < 4; i++)
	{
		TEXTVERTEX& v = verts[nVertex++];
		v.x = corners[i][0];
		v.y = corners[i][1];
		v.z = 0.0f;
		v.nx = 0.0f;
		v.ny = 0.0f;
		v.nz = 1.0f;
		v.color = color;
	}
}

// CFont::CreateTextMesh(text, nChars, ppMesh, pMin, pMax, width, bDoNotBreak, scale)
// Reconstructed from 5960 retail XBE @ 0x00054440 (3017 bytes). Two passes.
// The first walks each line to size the mesh: it skips *heading* stars, splits
// columns at a tab (unless bDoNotBreak), loads every glyph it meets (a glyph
// that won't load gets its record cleared and is skipped), stops short of
// 0x10000 vertices, and keeps the widest first and second column. A cursor
// inside or at the end of the string adds one quad. With a format width a
// single column takes the whole width; two columns that don't fit squeeze the
// first and fade it. The second pass writes the mesh: headings tint blue to
// 0.5, a glyph that would cross the width wraps back to the last break point
// (or fades if the word has none), the second column is right justified, and
// lines step down 1.5 after a paragraph or 1.0 inside one. Then every vertex
// is scaled and the bounding box taken. See docs/decomp/Text.md.
// Lines start indentWidth in until they drop below indentHeight. Headings only
// count while the asterisk formatting flag is on. The bounds stop in the second
// pass and the glyph scale are Theseus's.
void CFont::CreateTextMesh(const TCHAR* pchText, int nChars, LPD3DXMESH* ppMesh, D3DXVECTOR3* pMin, D3DXVECTOR3* pMax, float nFormatWidth, bool bDoNotBreak, float scale,
	float indentWidth, float indentHeight)
{
	if (m_pSet == NULL)
		return;

	if (nChars == -1)
		nChars = (int)_tcslen(pchText);

	// Pass 1: size it.
	int nFaces = 0, nVerts = 0;
	float col1Max = 0.0f, col2Max = 0.0f;
	bool bSingleColumn = true;
	bool bSkipStar = false;

	int ich = 0;
	while (ich < nChars)
	{
		int column = 1;
		float colWidth[2] = { 0.0f, 0.0f };

		if (OpensHeading(pchText, ich, nChars))
		{
			bSkipStar = true;
			ich++;
		}

		while (ich < nChars)
		{
			WCHAR ch = NextChar(pchText, ich, nChars);

			if (ch == '*' && bSkipStar && AtColumnEnd(pchText, ich, nChars))
				continue;

			if (!bDoNotBreak)
			{
				if (ch == '\r' && ich < nChars && pchText[ich] == '\n')
					ich++;
				if (ch == '\n' || ch == '\r')
					break;
				if (ch == '\t')
				{
					column = 2;
					bSingleColumn = false;
					bSkipStar = OpensHeading(pchText, ich, nChars);
					if (bSkipStar)
						ich++;
					continue;
				}
			}

			int nGlyph = FindGlyphIndex(ch);
			if (nGlyph == -1)
				continue;
			if (!LoadGlyph(nGlyph))
			{
				m_pGlyphs[nGlyph].shapeOffset = 0;
				continue;
			}

			CGlyphShape* pShape = m_ppShapes[nGlyph];
			if (nVerts + pShape->nPoints > 0xffff)
			{
				nChars = ich - 1;
				break;
			}

			colWidth[column - 1] += Advance(nGlyph);
			nFaces += pShape->nIndices / 3;
			nVerts += pShape->nPoints;
		}

		if (colWidth[0] > col1Max)
			col1Max = colWidth[0];
		if (colWidth[1] > col2Max)
			col2Max = colWidth[1];
	}

	if (g_nTextChar >= 0 && g_nTextChar <= nChars)
	{
		nFaces += 2;
		nVerts += 4;
	}

	if (nFaces == 0)
		return;

	bool bFade = bDoNotBreak;
	if (nFormatWidth <= 0.0f)
	{
		bFade = false;
	}
	else if (bSingleColumn)
	{
		col1Max = nFormatWidth;
	}
	else if (col1Max + col2Max > nFormatWidth)
	{
		col1Max = nFormatWidth - col2Max;
		bFade = true;
	}

	LPD3DXMESH pMesh = NULL;
	TheseusCreateMeshFVF(nFaces, nVerts, D3DXMESH_MANAGED, TEXT_FVF, &pMesh);
	if (pMesh == NULL)
		return;

	// No mesh at all beats one written through a failed lock; the node
	// tries again next frame.
	WORD* indices = NULL;
	TEXTVERTEX* verts = NULL;
	if (FAILED(pMesh->LockIndexBuffer(D3DLOCK_DISCARD, (BYTE**)&indices)))
	{
		pMesh->Release();
		return;
	}
	if (FAILED(pMesh->LockVertexBuffer(D3DLOCK_DISCARD, (BYTE**)&verts)))
	{
		pMesh->UnlockIndexBuffer();
		pMesh->Release();
		return;
	}
	*ppMesh = pMesh;

	// Pass 2: fill it.
	int nIndex = 0, nVertex = 0;
	float x = indentWidth, y = 0.0f;
	float blue = 1.0f;
	bool bHeading = false;
	bool bParaStart = true;

	ich = 0;
	while (ich < nChars)
	{
		int column = 1;
		int colFirst = nVertex;
		float colLimit = col1Max;
		float col2Width = 0.0f;

		int wrapChar = -1;
		int wrapVertex = nVertex;
		int wrapIndex = nIndex;

		if (bParaStart && OpensHeading(pchText, ich, nChars))
		{
			blue = 0.5f;
			bHeading = true;
			ich++;
		}

		for (;;)
		{
			WCHAR ch = '\r';
			if (ich < nChars)
			{
				ch = NextChar(pchText, ich, nChars);
				if (ch == '\r' && ich < nChars && pchText[ich] == '\n')
					ich++;
			}

			if (ch == '\n' || ch == '\r')
			{
				if (bDoNotBreak && bFade)
				{
					FadeEdge(verts + colFirst, nVertex - colFirst, colLimit - 1.0f, colLimit);
					break;
				}

				if (column == 2)
				{
					float shift = col2Max - col2Width;
					for (int i = colFirst; i < nVertex; i++)
						verts[i].x += shift;
				}
				else if (bFade)
				{
					FadeEdge(verts + colFirst, nVertex - colFirst, colLimit - 1.0f, colLimit);
				}

				if (ich != nChars)
				{
					bool bTight = bHeading || ch == '\r' || !bSingleColumn;
					y -= bTight ? 1.0f : 1.5f;
					x = y < -indentHeight ? 0.0f : indentWidth;
				}
				bHeading = false;
				bParaStart = true;
				break;
			}

			if (ch == '\t' && !bDoNotBreak)
			{
				if (bFade)
					FadeEdge(verts + colFirst, nVertex - colFirst, colLimit - 2.0f, colLimit);

				column = 2;
				x = nFormatWidth - col2Max;
				colFirst = nVertex;
				colLimit = col2Max;
				bHeading = false;
				bParaStart = true;

				if (OpensHeading(pchText, ich, nChars))
				{
					blue = 0.5f;
					bHeading = true;
					ich++;
				}
				continue;
			}

			if (bHeading && ch == '*' && AtColumnEnd(pchText, ich, nChars))
			{
				blue = 1.0f;
				continue;
			}

			bParaStart = false;

			int nGlyph = FindGlyphIndex(ch);
			if (nGlyph == -1)
				continue;

			if (nFormatWidth > 0.0f && !bDoNotBreak && ch != ' ' && column == 1 &&
				x + Advance(nGlyph) > nFormatWidth)
			{
				if (wrapChar > -1)
				{
					y -= 1.0f;
					x = y < -indentHeight ? 0.0f : indentWidth;
					ich = wrapChar;
					nVertex = wrapVertex;
					nIndex = wrapIndex;
					wrapChar = -1;
					continue;
				}
				bFade = true;
			}

			CGlyphShape* pShape = m_ppShapes[nGlyph];
			if (pShape == NULL)
				continue;

			// Theseus: pass 1 sized the buffers. If this pass ever walks the
			// string differently, drop the rest rather than write past them.
			if (nVertex + pShape->nPoints > nVerts || nIndex + pShape->nIndices > nFaces * 3)
			{
				nChars = ich - 1;
				break;
			}

			for (int i = 0; i < pShape->nIndices; i++)
				indices[nIndex++] = (WORD)(nVertex + pShape->indices[i]);

			float glyphScale = GlyphScale(nGlyph);
			DWORD color = 0xffffff00 | (DWORD)(blue * 255.0f);
			for (int i = 0; i < pShape->nPoints; i++)
			{
				TEXTVERTEX& v = verts[nVertex++];
				v.x = x + pShape->points[i].x * glyphScale;
				v.y = y + pShape->points[i].y * glyphScale;
				v.z = 0.0f;
				v.nx = 0.0f;
				v.ny = 0.0f;
				v.nz = 1.0f;
				v.color = color;
			}

			if (ich - 1 == g_nTextChar)
				AddCursor(indices, nIndex, verts, nVertex, x, y, true);

			x += Advance(nGlyph) * glyphScale;
			if (column == 2)
				col2Width += Advance(nGlyph);

			if (CanBreakAfter(ch))
			{
				wrapChar = ich;
				wrapVertex = nVertex;
				wrapIndex = nIndex;
			}
		}
	}

	// The cursor past the last character hides once it would leave the
	// keyboard's edit box (16.556152 units, 0x0002c1b4).
	if (g_nTextChar == nChars)
		AddCursor(indices, nIndex, verts, nVertex, x, y, x < 16.556152f);

	for (int i = 0; i < nVerts; i++)
	{
		verts[i].x *= scale;
		verts[i].y *= scale;
		verts[i].z *= scale;
	}

	if (pMin != NULL && pMax != NULL)
		D3DXComputeBoundingBox(verts, nVertex, TEXT_FVF, pMin, pMax);

	pMesh->UnlockIndexBuffer();
	pMesh->UnlockVertexBuffer();
}


// ===== Font table ==========================================================
// Ten slots, filled from the *.xtf files in the font directory. Slot 0 is the
// fallback for a face nobody has.

struct CFontTableEntry
{
	const TCHAR* m_szFace;
	const TCHAR* m_szFile;
	CFont m_font;
};

#define MAX_FONT_COUNT 10

// 5960 seeds slot 0 with HGGothicM, which Theseus doesn't ship; desktop falls
// back to the Xbox face instead.
static CFontTableEntry g_fonts[MAX_FONT_COUNT] =
{
#ifdef _XBOX
	{ _T("HGGothicM"), _T("HGGothicM.xtf") },
#else
	{ _T("Xbox"), _T("xbox.xtf") },
#endif
};

static int g_nFontCount = 1;

static TCHAR* CopyString(const TCHAR* sz)
{
	TCHAR* copy = new TCHAR[_tcslen(sz) + 1];
	_tcscpy(copy, sz);
	return copy;
}

// InitFontTable()
// Reconstructed from 5960 retail XBE @ 0x000552b0. Ghidra decompile, trimmed:
//     FindFirstFileA("<app>fonts\*.xtf"), falling back to "<app>*.xtf";
//     for each file, while count < 10:
//         open; read magic (XTF0 or XTF1), header length (must be 0x20), face (0x20);
//         face = widen(face); if (wcscmp(face, fonts[0].face) == 0) free(face);
//         else { fonts[count].face = face; fonts[count].file = copy(path); count++; }
//         CloseHandle;
// Theseus looks in its one font directory and takes XTF0 only, since that's
// all CFont reads. A bad header still closes the file (5960 leaks it).
static void InitFontTable()
{
	const TCHAR* szDir = TheseusGetFontDir();
	int cchDir = (int)_tcslen(szDir);

	TCHAR szPath[MAX_PATH];
	_tcscpy(szPath, szDir);
	_tcscpy(szPath + cchDir, _T("*.xtf"));

	WIN32_FIND_DATA fd;
#ifdef _UNICODE
	char szPathA[MAX_PATH];
	Ansi(szPathA, szPath, MAX_PATH);
	HANDLE hFind = TheseusFindFirstFile(szPathA, &fd);
#else
	HANDLE hFind = TheseusFindFirstFile(szPath, &fd);
#endif
	if (hFind == INVALID_HANDLE_VALUE)
		return;

	do
	{
		if (g_nFontCount >= MAX_FONT_COUNT)
			continue;

#ifdef _UNICODE
		Unicode(szPath + cchDir, fd.cFileName, MAX_PATH - cchDir);
#else
		_tcscpy(szPath + cchDir, fd.cFileName);
#endif

		HANDLE hFile = TheseusCreateFile(szPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
		if (hFile == INVALID_HANDLE_VALUE)
			continue;

		DWORD dwMagic = 0, cbHeader = 0;
		char faceA[XTF_FACE_LEN + 1];
		if (ReadExact(hFile, &dwMagic, 4) && dwMagic == XTF0_MAGIC &&
			ReadExact(hFile, &cbHeader, 4) && cbHeader == XTF_FACE_LEN &&
			ReadExact(hFile, faceA, XTF_FACE_LEN))
		{
			faceA[XTF_FACE_LEN] = 0;

			TCHAR szFace[XTF_FACE_LEN + 1];
#ifdef _UNICODE
			Unicode(szFace, faceA, XTF_FACE_LEN + 1);
#else
			_tcscpy(szFace, faceA);
#endif
			if (_tcscmp(szFace, g_fonts[0].m_szFace) != 0)
			{
				g_fonts[g_nFontCount].m_szFace = CopyString(szFace);
				g_fonts[g_nFontCount].m_szFile = CopyString(szPath);
				g_nFontCount++;
			}
		}

		CloseHandle(hFile);
	}
	while (TheseusFindNextFile(hFind, &fd));

	TheseusFindClose(hFind);
}

// GetFont(szFaceName)
// Reconstructed from 5960 retail XBE @ 0x00055640. Ghidra decompile, trimmed:
//     if (count == 1) InitFontTable();
//     for (i < count) if (!_wcsicmp(face, fonts[i].face)) break;
//     if (i == count) i = 0;
//     if (fonts[i].font.hFile == -1) {
//         path = wcschr(file, ':') ? file : swprintf("%s%s", appdir, file);
//         if (!Open(path) && fonts[0].hFile == -1 && !Open(fonts[0].file)) return NULL;
//     }
//     return &fonts[i].font;
// Theseus builds the path from its font directory instead of the app dir.
CFont* GetFont(const TCHAR* szFaceName)
{
	if (g_nFontCount == 1)
		InitFontTable();

	int i = 0;
	while (i < g_nFontCount && _tcsicmp(szFaceName, g_fonts[i].m_szFace) != 0)
		i++;
	if (i == g_nFontCount)
		i = 0;

	CFontTableEntry& entry = g_fonts[i];
	if (entry.m_font.m_hFile == INVALID_HANDLE_VALUE)
	{
		TCHAR szPath[MAX_PATH];
		if (_tcschr(entry.m_szFile, ':') != NULL)
			_tcscpy(szPath, entry.m_szFile);
		else
			_stprintf(szPath, _T("%s%s"), TheseusGetFontDir(), entry.m_szFile);

		if (!entry.m_font.Open(szPath) &&
			g_fonts[0].m_font.m_hFile == INVALID_HANDLE_VALUE &&
			!g_fonts[0].m_font.Open(g_fonts[0].m_szFile))
			return NULL;
	}

	return &entry.m_font;
}

// Text_Exit()
// Reconstructed from 5960 retail XBE @ 0x00055750: every slot past the first
// frees its face and file strings, then each font closes.
void Text_Exit()
{
	for (int i = 0; i < g_nFontCount; i++)
	{
		if (i > 0)
		{
			delete [] (TCHAR*)g_fonts[i].m_szFace;
			delete [] (TCHAR*)g_fonts[i].m_szFile;
			g_fonts[i].m_szFace = NULL;
			g_fonts[i].m_szFile = NULL;
		}
		g_fonts[i].m_font.Close();
	}
}


#ifdef _XBOX
// ===== Overlay text (Theseus) ==============================================
// Draws "Xbox" face glyphs as screen space triangles for the panic screen,
// the widgets and the overlay menus. No 5960 counterpart.

struct OverlayTextVert
{
	float x, y, z, rhw;
	D3DCOLOR color;
};

#define MAX_OVL_VERTS   8192
#define MAX_OVL_INDICES 16384

static OverlayTextVert s_ovlVerts[MAX_OVL_VERTS];
static WORD s_ovlIndices[MAX_OVL_INDICES];

float OverlayFontMeasure(const TCHAR* text, float pixelHeight)
{
	if (!text)
		return 0.0f;

	CFont* pFont = GetFont(_T("Xbox"));
	if (!pFont)
		return 0.0f;

	float width = 0.0f;
	while (*text)
	{
		int idx = pFont->FindGlyphIndex(*text);
		if (idx >= 0)
			width += pFont->Advance(idx);
		text++;
	}

	return width * pixelHeight;
}

float OverlayFontDraw(const TCHAR* text, float screenX, float screenY, float pixelHeight, D3DCOLOR color)
{
	IDirect3DDevice8* dev = TheseusGetD3DDev();
	if (!dev || !text || !*text)
		return 0.0f;

	CFont* pFont = GetFont(_T("Xbox"));
	if (!pFont)
		return 0.0f;

	int nVerts = 0, nIndices = 0;
	float curX = 0.0f;
	float scale = pixelHeight;

	while (*text)
	{
		TCHAR ch = *text++;
		int glyphIdx = pFont->FindGlyphIndex(ch);
		if (glyphIdx < 0)
			continue;

		pFont->LoadGlyph(glyphIdx);

		CGlyphShape* shape = pFont->Shape(glyphIdx);
		if (!shape)
		{
			curX += pFont->Advance(glyphIdx);
			continue;
		}

		// Flush if we'd exceed buffer limits
		if (nVerts + shape->nPoints > MAX_OVL_VERTS ||
			nIndices + shape->nIndices > MAX_OVL_INDICES)
		{
			dev->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
			Frame_Count(FC_DRAW_TEXT);
			Frame_PhaseBegin(FP_UP_DRAW);
			dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, nVerts, nIndices / 3,
				s_ovlIndices, D3DFMT_INDEX16, s_ovlVerts, sizeof(OverlayTextVert));
			Frame_PhaseEnd(FP_UP_DRAW);
			nVerts = 0;
			nIndices = 0;
		}

		for (int i = 0; i < shape->nIndices; i++)
			s_ovlIndices[nIndices++] = (WORD)(nVerts + shape->indices[i]);

		for (int j = 0; j < shape->nPoints; j++)
		{
			s_ovlVerts[nVerts].x = screenX + (curX + shape->points[j].x) * scale;
			s_ovlVerts[nVerts].y = screenY + scale - shape->points[j].y * scale;
			s_ovlVerts[nVerts].z = 0.0f;
			s_ovlVerts[nVerts].rhw = 1.0f;
			s_ovlVerts[nVerts].color = color;
			nVerts++;
		}

		curX += pFont->Advance(glyphIdx);
	}

	// Final flush
	if (nIndices > 0)
	{
		Frame_Count(FC_DRAW_TEXT);
		dev->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
		Frame_PhaseBegin(FP_UP_DRAW);
		dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, nVerts, nIndices / 3,
			s_ovlIndices, D3DFMT_INDEX16, s_ovlVerts, sizeof(OverlayTextVert));
		Frame_PhaseEnd(FP_UP_DRAW);
	}

	return curX * scale;
}
#endif // _XBOX


// ===== CTextNode ===========================================================
// 0x78 bytes in 5960: text 0x1c, font 0x20, justify 0x24, deviation 0x28,
// extrusion 0x2c, width 0x30, translate 0x34, height 0x38, scroll 0x3c,
// scrollRate 0x40, scrollDelay 0x44, indentWidth 0x48, indentHeight 0x4c,
// mesh 0x50, bbox 0x54..0x6b, language 0x6c, scroll start time 0x70.
// scrollHorizontal is Theseus's.

class CTextNode : public CNode
{
	DECLARE_NODE(CTextNode, CNode)
public:
	CTextNode();
	~CTextNode();

	void Render();
	void Advance(float nSeconds);
	bool OnSetProperty(const PRD* pprd, const void* pvValue);

	TCHAR* m_text;
	TCHAR* m_font;
	TCHAR* m_justify;
	float m_deviation;
	float m_extrusion;
	float m_width;
	bool m_translate;
	float m_height;
	float m_scroll;
	float m_scrollRate;
	float m_scrollDelay;
	float m_indentWidth;
	float m_indentHeight;
	bool m_scrollHorizontal;

	DECLARE_NODE_PROPS()

	LPD3DXMESH m_pMesh;
	D3DXVECTOR3 m_bboxMin, m_bboxMax;
	int m_nLanguage;
	XTIME m_timeToScroll;

private:
	typedef bool (*StampFormatter)(TCHAR* szBuf, SIZE_T chLen, SYSTEMTIME* pst, int locale);
	int ExpandStamp(const TCHAR* szPrefix, StampFormatter pfnFormat, bool& bRebuild);
	void DropMesh();
};

// Gap between the end of a scrolling text and its repeat.
static const float SCROLL_GAP = 1.5f;

IMPLEMENT_NODE("Text", CTextNode, CNode)

START_NODE_PROPS(CTextNode, CNode)
	NODE_PROP(pt_string, CTextNode, text)
	NODE_PROP(pt_string, CTextNode, font)
	NODE_PROP(pt_string, CTextNode, justify)
	NODE_PROP(pt_number, CTextNode, deviation)
	NODE_PROP(pt_number, CTextNode, extrusion)
	NODE_PROP(pt_number, CTextNode, width)
	NODE_PROP(pt_boolean, CTextNode, translate)
	NODE_PROP(pt_number, CTextNode, height)
	NODE_PROP(pt_number, CTextNode, scroll)
	NODE_PROP(pt_number, CTextNode, scrollRate)
	NODE_PROP(pt_number, CTextNode, scrollDelay)
	NODE_PROP(pt_number, CTextNode, indentWidth)
	NODE_PROP(pt_number, CTextNode, indentHeight)
	NODE_PROP(pt_boolean, CTextNode, scrollHorizontal)
END_NODE_PROPS()

// CTextNode::CTextNode() / ~CTextNode()
// Reconstructed from 5960 retail XBE @ 0x000557e0 and 0x00055880:
//     deviation = 0.01; extrusion = 1.0; translate = 1; everything else 0;
//     ~: free text, font, justify; if (mesh) mesh->Release();
CTextNode::CTextNode() :
	m_text(NULL),
	m_font(NULL),
	m_justify(NULL),
	m_deviation(0.01f),
	m_extrusion(1.0f),
	m_width(0.0f),
	m_translate(true),
	m_height(0.0f),
	m_scroll(0.0f),
	m_scrollRate(0.0f),
	m_scrollDelay(0.0f),
	m_indentWidth(0.0f),
	m_indentHeight(0.0f),
	m_scrollHorizontal(false),
	m_pMesh(NULL),
	m_nLanguage(0),
	m_timeToScroll(0.0)
{
}

CTextNode::~CTextNode()
{
	delete [] m_text;
	delete [] m_font;
	delete [] m_justify;
	DropMesh();
}

void CTextNode::DropMesh()
{
	if (m_pMesh != NULL)
	{
		m_pMesh->Release();
		m_pMesh = NULL;
	}
}

// CTextNode::Advance(nSeconds)
// Reconstructed from 5960 retail XBE @ 0x00055900. Ghidra decompile:
//     CNode::Advance(t);
//     if (scrollRate > 0 && mesh) {
//         if (timeToScroll == 0) timeToScroll = scrollDelay + now;
//         h = bbox.max.y - bbox.min.y;
//         if (timeToScroll <= now && h > height) {
//             scroll = height * t * scrollRate + scroll;
//             if (scroll >= h + 1.5) scroll -= h + 1.5;
//         }
//     }
// The horizontal marquee is Theseus's; it steps by the window width instead.
void CTextNode::Advance(float nSeconds)
{
	CNode::Advance(nSeconds);

	if (m_scrollRate <= 0.0f || m_pMesh == NULL)
		return;

	XTIME now = TheseusGetNow();
	if (m_timeToScroll == 0.0)
		m_timeToScroll = now + m_scrollDelay;
	if (now < m_timeToScroll)
		return;

	float window, content;
	if (m_scrollHorizontal)
	{
		window = fabsf(m_width);
		content = m_bboxMax.x - m_bboxMin.x;
		if (window <= 0.0f)
			return;
	}
	else
	{
		window = m_height;
		content = m_bboxMax.y - m_bboxMin.y;
	}

	if (content > window)
	{
		m_scroll += window * nSeconds * m_scrollRate;
		if (m_scroll >= content + SCROLL_GAP)
			m_scroll -= content + SCROLL_GAP;
	}
}

// FaceFromFont(szFont)
// Reconstructed from 5960 retail XBE @ 0x000559a0:
//     if (!font || !_wcsicmp(font, "body")) return "XBox Book";
//     if (!_wcsicmp(font, "heading")) return "Xbox";
//     return font;
const TCHAR* FaceFromFont(const TCHAR* szFont)
{
	if (szFont == NULL || _tcsicmp(szFont, _T("body")) == 0)
		return _T("XBox Book");
	if (_tcsicmp(szFont, _T("heading")) == 0)
		return _T("Xbox");
	return szFont;
}

// "<clock>" and "<date>" texts draw the local time or date after the prefix.
// Refreshes the stored text when the value changes; returns the prefix length,
// or 0 when the text doesn't start with this prefix.
int CTextNode::ExpandStamp(const TCHAR* szPrefix, StampFormatter pfnFormat, bool& bRebuild)
{
	int cchPrefix = (int)_tcslen(szPrefix);
	if (_tcsncmp(m_text, szPrefix, cchPrefix) != 0)
		return 0;

	SYSTEMTIME st;
	GetLocalTime(&st);
	TCHAR szValue[32];
	szValue[0] = 0;
	pfnFormat(szValue, countof(szValue), &st, LOCALE_CURRENT);

	if (_tcscmp(m_text + cchPrefix, szValue) != 0)
	{
		delete [] m_text;
		m_text = new TCHAR[cchPrefix + _tcslen(szValue) + 1];
		_tcscpy(m_text, szPrefix);
		_tcscpy(m_text + cchPrefix, szValue);
		bRebuild = true;
	}
	return cchPrefix;
}

// CTextNode::Render()
// Reconstructed from 5960 retail XBE @ 0x000559e0 (1331 bytes). Takes the
// keyboard's string if one is waiting (a changed string or cursor rebuilds),
// expands <clock> and <date>, rebuilds on a language change, then builds the mesh with
// the translated text, |width| as the format width and a negative width as
// "don't wrap". Justify middle or end shifts left by half or all of the drawn
// width (capped at |width|). With a height smaller than the text the mesh
// fades top and bottom and draws twice, the second copy one gap below so the
// scroll wraps around. Each draw pushes the world matrix, translates, turns
// culling off for the flat glyphs and back to CCW. See docs/decomp/Text.md.
// 5960 also rebuilds every frame a cursorless keyboard string arrives, which
// draws the same mesh; Theseus skips that. The horizontal marquee, the draw counter and the desktop vertex alpha toggle
// are Theseus's.
void CTextNode::Render()
{
	bool bRebuild = false;

	if (g_szText != NULL)
	{
		if (m_text == NULL || _tcscmp(m_text, g_szText) != 0 || g_nTextChar != g_nTextCharLast)
		{
			delete [] m_text;
			m_text = CopyString(g_szText);
			g_nTextCharLast = g_nTextChar;
			bRebuild = true;
		}
		g_szText = NULL;
	}

	if (m_text == NULL || m_text[0] == 0)
		return;

	const TCHAR* szText = m_text;
	int cchStamp = ExpandStamp(_T("<clock>"), FormatTime, bRebuild);
	if (cchStamp == 0)
		cchStamp = ExpandStamp(_T("<date>"), FormatDate, bRebuild);
	if (cchStamp != 0)
	{
		m_translate = false;
		szText = m_text + cchStamp;
	}

	if (m_translate && m_nLanguage != g_nCurLanguage)
		bRebuild = true;

	if (bRebuild)
		DropMesh();

	if (m_pMesh == NULL)
	{
		TCHAR szTranslated[MAX_TRANSLATE_LEN];
		if (m_translate)
			szText = Translate(szText, szTranslated);
		m_nLanguage = g_nCurLanguage;

		CFont* pFont = GetFont(FaceFromFont(m_font));
		if (pFont == NULL || !pFont->IsOpen())
			return;
		pFont->CreateTextMesh(szText, -1, &m_pMesh, &m_bboxMin, &m_bboxMax, fabsf(m_width), m_width < 0.0f, 1.0f,
			m_indentWidth, m_indentHeight);
		if (m_pMesh == NULL)
			return;
	}

	float contentW = m_bboxMax.x - m_bboxMin.x;
	float contentH = m_bboxMax.y - m_bboxMin.y;
	float window = fabsf(m_width);

	float xOffset = 0.0f;
	if (m_justify != NULL)
	{
		float drawn = contentW;
		if (m_width != 0.0f && drawn > window)
			drawn = window;

		if (_tcsicmp(m_justify, _T("middle")) == 0)
			xOffset = -drawn / 2.0f;
		else if (_tcsicmp(m_justify, _T("end")) == 0)
			xOffset = -drawn;
	}

	bool bMarquee = m_scrollHorizontal && window > 0.0f && contentW > window;
	bool bVScroll = !bMarquee && m_height != 0.0f && contentH > m_height;

	for (int copy = 0; copy < 2; copy++)
	{
		if (bMarquee)
		{
			float wrap = copy == 0 ? 0.0f : contentW + SCROLL_GAP;
			TEXTVERTEX* verts;
			if (SUCCEEDED(m_pMesh->LockVertexBuffer(0, (BYTE**)&verts)))
			{
				float left = m_scroll - 1.0f;
				HorizontalFade(verts, m_pMesh->GetNumVertices(), left, left + window + 2.0f, wrap);
				m_pMesh->UnlockVertexBuffer();
			}

			if (copy == 1 && m_scroll + window < contentW)
				break;
		}
		else if (bVScroll)
		{
			float wrap = copy == 0 ? 0.0f : -(contentH + SCROLL_GAP);
			float top = 1.0f - m_scroll;
			float bottom = top - m_height - 2.0f;
			if (m_scroll == 0.0f)
				top += 1.0f;   // no top fade until it starts moving
			TEXTVERTEX* verts;
			if (SUCCEEDED(m_pMesh->LockVertexBuffer(0, (BYTE**)&verts)))
			{
				VerticalFade(verts, m_pMesh->GetNumVertices(), top, bottom, wrap);
				m_pMesh->UnlockVertexBuffer();
			}

			if (copy == 1 && m_scroll + m_height < contentH)
				break;
		}
		else if (copy == 1)
		{
			break;
		}

		TheseusPushWorld();
		if (bMarquee)
			TheseusTranslateWorld(xOffset + (copy == 0 ? 0.0f : contentW + SCROLL_GAP) - m_scroll, 0.0f, 0.0f);
		else
			TheseusTranslateWorld(xOffset, (copy == 0 ? 0.0f : -(contentH + SCROLL_GAP)) + m_scroll, 0.0f);
		TheseusUpdateWorld();

#ifndef _XBOX
		// Desktop falloff lighting writes color.a, so the fade's vertex alpha
		// only clips if the shader multiplies it back in for this draw.
		TheseusSetVertexAlphaMul(TRUE);
#endif
		TheseusSetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
		Frame_Count(FC_DRAW_SUBSET);
		m_pMesh->DrawSubset(0);
		TheseusSetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);
#ifndef _XBOX
		TheseusSetVertexAlphaMul(FALSE);
#endif

		TheseusPopWorld();
	}
}

// CTextNode::OnSetProperty(pprd, pvValue)
// Reconstructed from 5960 retail XBE @ 0x00055f20:
//     if (prd->offset != 0x3c /* scroll */) { if (mesh) { mesh->Release(); mesh = 0; }
//                                           timeToScroll = 0; scroll = 0; }
//     return 1;
bool CTextNode::OnSetProperty(const PRD* pprd, const void* pvValue)
{
	if (PTR2INT(pprd->pbOffset) != offsetof(m_scroll))
	{
		DropMesh();
		m_scroll = 0.0f;
		m_timeToScroll = 0.0;
	}
	return true;
}


// ===== Text node cache =====================================================
// Ten throwaway Text nodes for code that draws a string directly (the save
// grid and title lists), keyed by string and width.

#define MAX_TEXT_CACHE 10

struct CTextCacheEntry
{
	CTextNode* m_pNode;
	XTIME m_lastUsed;
};

static CTextCacheEntry s_textCache[MAX_TEXT_CACHE];

// FindText(szText, nWidth)
// Reconstructed from 5960 retail XBE @ 0x00055f60. Ghidra decompile, trimmed:
//     for (i < 10) {
//         if (node && node->text && !wcscmp(node->text, s) && node->width == w)
//             { used = now; return entry; }
//         if (!free) { if (!node) free = entry; else if (!oldest || used < oldest->used) oldest = entry; }
//     }
//     if (!free && oldest) { delete oldest->node; zero it; free = oldest; }
//     free->node = new CTextNode; node->text = copy(s); node->width = w; used = now;
static CTextCacheEntry* FindText(const TCHAR* szText, float nWidth)
{
	CTextCacheEntry* pEmpty = NULL;
	CTextCacheEntry* pOldest = NULL;

	for (int i = 0; i < MAX_TEXT_CACHE; i++)
	{
		CTextCacheEntry& entry = s_textCache[i];
		CTextNode* pNode = entry.m_pNode;
		if (pNode != NULL && pNode->m_text != NULL && pNode->m_width == nWidth &&
			_tcscmp(pNode->m_text, szText) == 0)
		{
			entry.m_lastUsed = TheseusGetNow();
			return &entry;
		}

		if (pEmpty != NULL)
			continue;
		if (pNode == NULL)
			pEmpty = &entry;
		else if (pOldest == NULL || entry.m_lastUsed < pOldest->m_lastUsed)
			pOldest = &entry;
	}

	if (pEmpty == NULL)
	{
		delete pOldest->m_pNode;
		pOldest->m_pNode = NULL;
		pOldest->m_lastUsed = 0.0;
		pEmpty = pOldest;
	}

	CTextNode* pNode = new CTextNode;
	pNode->m_text = CopyString(szText);
	pNode->m_width = nWidth;
	pEmpty->m_pNode = pNode;
	pEmpty->m_lastUsed = TheseusGetNow();
	return pEmpty;
}

// The callers inline this in 5960; the node never translates its string.
CNode* GetTextNode(const TCHAR* szText, float nWidth)
{
	CTextNode* pNode = FindText(szText, nWidth)->m_pNode;
	pNode->m_translate = false;
	return pNode;
}
