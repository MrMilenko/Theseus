// text.cpp: XTF font loading, glyph mesh generation, and text node
// rendering. Reads Xbox .xtf vector font files, builds triangle meshes
// from glyph outlines, handles word wrap, scrolling, cursor blinking,
// and the overlay text API. Decompiled from the 5960 retail XBE; see
// docs/decomp/Text.md.

#include "std.h"
#include "theseus.h"
#include "node.h"
#include "locale_node.h"
#include "xap_compile.h"
#include "runner.h"

#ifndef LF_FACESIZE
#define LF_FACESIZE 32
#endif

TCHAR* g_szText = NULL;

// Oh what a nasty hack...
int g_nTextChar = -1; // index of char where we want a cursor
int g_nTextCharLast = -1;

struct TEXTVERTEX
{
	float x, y, z;
	float nx, ny, nz;
	DWORD color;
};

struct CGlyphVertex
{
	float x, y;
};

struct CGlyphShape
{
	WORD m_nIndexCount;
	WORD m_nVertexCount;
	WORD* m_indices;
	CGlyphVertex* m_vertices;
	int m_nGlyphIndex;    // added to hold the index of the character
};

struct CGlyphMetrics
{
    float gmfBlackBoxX;
    float gmfBlackBoxY;
    CGlyphVertex gmfptGlyphOrigin;
    float gmfCellIncX;
    float gmfCellIncY;
};

struct CGlyphObject
{
	CGlyphMetrics m_metrics;
	CGlyphShape* m_glyphShape; // or DWORD m_dwFileOffsetOfGlyphShape
};

struct CWCRange
{
	WCHAR wcLow;
	USHORT cGlyphs;
};

struct CGlyphSet
{
	DWORD cbThis;
	DWORD flAccel;
	DWORD cGlyphsSupported;
	DWORD cRanges;
	CWCRange ranges [1];
};

class CFont
{
public:
	CFont();
	~CFont();

	bool Open(const TCHAR* szFile);
	void Close();
	bool LoadGlyph(int nGlyphIndex);
	int FindGlyphIndex(WCHAR wch);
	void CreateTextMesh(const TCHAR* pchText, int nChars, LPD3DXMESH* ppMesh, D3DXVECTOR3* pMin, D3DXVECTOR3* pMax, float nFormatWidth, bool bDoNotBreak, float scale);
	void CreateCursorMesh(WORD*& indices, int& nCurIndex, TEXTVERTEX*& verts, int& nCurVertex, float x, float y, bool visible);
    bool IsBreakChar(TCHAR ch);

	HANDLE m_hFile;
	CGlyphSet* m_glyphSet;
	CGlyphObject* m_rgGlyphObjects;
	bool* m_rgGlyphLoaded;
};

CFont::CFont()
{
	m_hFile = INVALID_HANDLE_VALUE;
	m_glyphSet = NULL;
	m_rgGlyphObjects = NULL;
	m_rgGlyphLoaded = NULL;
}

CFont::~CFont()
{
	Close();
}

void CFont::Close()
{
	if (m_hFile != INVALID_HANDLE_VALUE)
		VERIFY(CloseHandle(m_hFile));

	if (m_glyphSet != NULL)
	{
		for (UINT i = 0; i < m_glyphSet->cGlyphsSupported; i += 1)
		{
			if (m_rgGlyphLoaded[i])
			{
				CGlyphShape* pGlyphShape = m_rgGlyphObjects[i].m_glyphShape;
				delete [] pGlyphShape->m_indices;
				delete [] pGlyphShape->m_vertices;
				delete pGlyphShape;
			}
		}
	}

	delete [] (BYTE*)m_glyphSet;
	delete [] m_rgGlyphObjects;
	delete [] m_rgGlyphLoaded;

	m_hFile = INVALID_HANDLE_VALUE;
	m_glyphSet = NULL;
	m_rgGlyphObjects = NULL;
	m_rgGlyphLoaded = NULL;
}

bool CFont::Open(const TCHAR* szFile)
{
	ASSERT(m_hFile == INVALID_HANDLE_VALUE);

	m_hFile = TheseusCreateFile(szFile, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (m_hFile == INVALID_HANDLE_VALUE)
		return false;

	DWORD dwRead;

	DWORD dwMagic;
	ReadFile(m_hFile, &dwMagic, 4, &dwRead, NULL);

	if (dwMagic != 0x30465458) // "XTF0"
	{
		TRACE(_T("\001Invalid font file: %s\n"), szFile);

		VERIFY(CloseHandle(m_hFile));
		m_hFile = INVALID_HANDLE_VALUE;

		return false;
	}

	// Skip the face-name header. It's a fixed-length LF_FACESIZE (32-byte)
	// ASCII block, the actual face string is read out separately by the
	// font registry; the loader here just consumes the bytes to advance.
	DWORD dwHeaderLen;
	VERIFY(ReadFile(m_hFile, &dwHeaderLen, 4, &dwRead, NULL));
	ASSERT(dwHeaderLen == LF_FACESIZE);

	char szFace [LF_FACESIZE];
	VERIFY(ReadFile(m_hFile, szFace, LF_FACESIZE, &dwRead, NULL));

	DWORD cbGlyphSet;
	VERIFY(ReadFile(m_hFile, &cbGlyphSet, 4, &dwRead, NULL));

	m_glyphSet = (CGlyphSet*)new BYTE [cbGlyphSet];
	m_glyphSet->cbThis = cbGlyphSet;
	VERIFY(ReadFile(m_hFile, ((BYTE*)m_glyphSet) + 4, cbGlyphSet - 4, &dwRead, NULL));

	m_rgGlyphObjects = new CGlyphObject [m_glyphSet->cGlyphsSupported];
	VERIFY(ReadFile(m_hFile, m_rgGlyphObjects, sizeof (CGlyphObject) * m_glyphSet->cGlyphsSupported, &dwRead, NULL));

	m_rgGlyphLoaded = new bool [m_glyphSet->cGlyphsSupported];
	ZeroMemory(m_rgGlyphLoaded, sizeof (bool) * m_glyphSet->cGlyphsSupported);

	return true;
}

bool CFont::LoadGlyph(int nGlyphIndex)
{
	ASSERT(m_glyphSet != NULL);
	ASSERT(nGlyphIndex >= 0 && (UINT)nGlyphIndex < m_glyphSet->cGlyphsSupported);

	if (m_rgGlyphLoaded[nGlyphIndex])
		return true;

	VERIFY(SetFilePointer(m_hFile, (DWORD)m_rgGlyphObjects[nGlyphIndex].m_glyphShape, 0, FILE_BEGIN) != ~0);
	CGlyphShape* pGlyphShape = new CGlyphShape;
    ASSERT(pGlyphShape);

	DWORD dwRead;
	
	VERIFY(ReadFile(m_hFile, &pGlyphShape->m_nIndexCount, 2, &dwRead, NULL));
	pGlyphShape->m_indices = new WORD [pGlyphShape->m_nIndexCount];

	VERIFY(ReadFile(m_hFile, &pGlyphShape->m_nVertexCount, 2, &dwRead, NULL));
	pGlyphShape->m_vertices = new CGlyphVertex [pGlyphShape->m_nVertexCount];

	VERIFY(ReadFile(m_hFile, pGlyphShape->m_indices, sizeof (WORD) * pGlyphShape->m_nIndexCount, &dwRead, NULL));
	VERIFY(ReadFile(m_hFile, pGlyphShape->m_vertices, sizeof (CGlyphVertex) * pGlyphShape->m_nVertexCount, &dwRead, NULL));

	m_rgGlyphObjects[nGlyphIndex].m_glyphShape = pGlyphShape;
	m_rgGlyphLoaded[nGlyphIndex] = true;

	return true;
}

int CFont::FindGlyphIndex(WCHAR wch)
{
	int nIndex = 0;

	UINT i;
	for (i = 0; i < m_glyphSet->cRanges; i += 1)
	{
		if (wch >= m_glyphSet->ranges[i].wcLow && wch < m_glyphSet->ranges[i].wcLow + m_glyphSet->ranges[i].cGlyphs)
			return nIndex + wch - m_glyphSet->ranges[i].wcLow;

		nIndex += m_glyphSet->ranges[i].cGlyphs;
	}

	if (wch != 127)
		return FindGlyphIndex(127); // the invalid char box

	return 0; // "invalid char" glyph (it's a space)
}

inline float smoothstep(float a, float b, float x)
{
	if (x < a)
		return 0.0f;

	if (x >= b)
		return 1.0f;

	x = (x - a) / (b - a);

	return (x * x * (3 - 2 * x));
}

// Adjust the alpha value for verts whose x is between nStart and nEnd such that they fade
// from opaque to transparent (bRight==true) or transparent to opaque (bRight==false).
static void HorizontalFade(TEXTVERTEX* verts, int nVertexCount, float nStart, float nEnd, bool bRight)
{
	float nWidth = nEnd - nStart;

	int i;
	for (i = 0; i < nVertexCount; i += 1)
	{
		float a = (verts[i].x - nStart) / nWidth;
		a = smoothstep(0.0f, 1.0f, a);

		if (bRight)
			a = 1.0f - a;

		float a0 = (float)(verts[i].color >> 24);
		verts[i].color = (verts[i].color & 0x00ffffff) | (((DWORD)(a0 * a)) << 24);
	}
}

inline void FadeLeftEdge(TEXTVERTEX* verts, int nVertexCount, float nStart, float nEnd)
{
	HorizontalFade(verts, nVertexCount, nStart, nEnd, false);
}

inline void FadeRightEdge(TEXTVERTEX* verts, int nVertexCount, float nStart, float nEnd)
{
	HorizontalFade(verts, nVertexCount, nStart, nEnd, true);
}

static void VerticalFade(TEXTVERTEX* verts, int nVertexCount, float nTop, float nBottom, float nScroll)
{
	for (int i = 0; i < nVertexCount; i += 1)
	{
		float a = 1.0f;
		float y = verts[i].y + nScroll;

		if (y > nTop || y < nBottom)
		{
			a = 0.0f;
		}
		else if (y > nTop - 1.0f)
		{
			a = 1.0f - (y - (nTop - 1.0f));
		}
		else if (y < nBottom + 1.0f)
		{
			a = 1.0f - ((nBottom + 1.0f) - y);
		}
		else //if (y >= nTop + 1.0f && y <= nBottom - 1.0f)
		{
			a = 1.0f;
		}

		ASSERT(a >= 0.0f && a <= 1.0f);

		a = smoothstep(0.0f, 1.0f, a);
		verts[i].color = (verts[i].color & 0x00ffffff) | (((DWORD)(255.0f * a)) << 24);
	}
}

bool CFont::IsBreakChar(TCHAR ch)
{
    // Basically, we will break after double byte or single byte Kana
    if (ch == ' ' || (ch >= 0x3040 && ch < 0xF000) || ch >= 0xFF66)
    {
        return true;
    }

    return false;
}

extern void TheseusCreateMeshFVF(DWORD NumFaces, DWORD NumVertices, DWORD Options, DWORD FVF, LPD3DXMESH* ppMesh);

void CFont::CreateTextMesh(const TCHAR* pchText, int nChars, LPD3DXMESH* ppMesh, D3DXVECTOR3* pMin, D3DXVECTOR3* pMax, float nFormatWidth, bool bDoNotBreak, float scale)
{
	bool bFade = bDoNotBreak;

	if (nChars == -1)
		nChars = _tcslen(pchText);

	LPD3DXMESH pMesh = NULL;
	int nFaces = 0, nVerts = 0;

	int ich = 0;
	float nMaxCol1Width = 0.0f;
	float nMaxCol2Width = 0.0f;
	bool bSingleLine = true;
	bool bSingleColumn = true;
	bool bAsterisk = false;
	int nLine;
	int ich2;
	for (nLine = 0; ich < nChars; nLine += 1)
	{
		int nColumn = 1;
		float nCol1Width = 0.0f;
		float nCol2Width = 0.0f;

		if (pchText[ich] == '*')
		{
			// See if there's a '*' at the end of the line...
			for (ich2 = ich + 1; ich2 < nChars && pchText[ich2] != '\n' && pchText[ich2] != '\r' && pchText[ich2] != '\t'; ich2 += 1)
				;
			if (ich2 > ich + 1 && pchText[ich2 - 1] == '*')
			{
				// Line starts and ends with a *; don't show the *'s and format as a heading
				bAsterisk = true;
				ich += 1;
			}
		}

		while (ich < nChars)
		{
			TCHAR ch = pchText[ich];
			ich += 1;

			if (ch == '*')
			{
				// Ignore this if it's the last visible char in the column...
				if (bAsterisk && (ich == nChars || pchText[ich] == '\t' || pchText[ich] == '\n' || pchText[ich] == '\r'))
					continue;
			}

			if (bDoNotBreak == false && ch == '\r' && ich < nChars && pchText[ich] == '\n')
            {
				ich += 1;
            }

			if (bDoNotBreak == false && (ch == '\n' || ch == '\r'))
			{
				bSingleLine = false;
				break;
			}

			if (ch == '\t' && bDoNotBreak == false)
			{
				nColumn = 2;
				bSingleColumn = false;
				bAsterisk = false;

				if (ich < nChars && pchText[ich] == '*')
				{
					// See if there's a '*' at the end of the line...
					for (ich2 = ich + 1; ich2 < nChars && pchText[ich2] != '\n' && pchText[ich2] != '\r' && pchText[ich2] != '\t'; ich2 += 1)
						;
					if (ich2 > ich + 1 && pchText[ich2 - 1] == '*')
					{
						// Column starts and ends with a *; don't show the *'s and format as a heading
						ich += 1;
						bAsterisk = true;
					}
				}
				continue;
			}

			int nGlyphIndex = FindGlyphIndex(ch);
			if (nGlyphIndex == -1)
				continue;

			VERIFY(LoadGlyph(nGlyphIndex));

			CGlyphShape* pGlyphShape = m_rgGlyphObjects[nGlyphIndex].m_glyphShape;
			pGlyphShape->m_nGlyphIndex = nGlyphIndex;

			if (nVerts + pGlyphShape->m_nVertexCount > 65535)
			{
				TRACE(_T("CreateTextMesh: too much text!\n"));
				nChars = ich - 1; // truncate string
				break;
			}

			if (nColumn == 1)
				nCol1Width += m_rgGlyphObjects[nGlyphIndex].m_metrics.gmfCellIncX;
			else
				nCol2Width += m_rgGlyphObjects[nGlyphIndex].m_metrics.gmfCellIncX;

			nFaces += pGlyphShape->m_nIndexCount / 3;
			nVerts += pGlyphShape->m_nVertexCount;
		}

		if (nCol1Width > nMaxCol1Width)
			nMaxCol1Width = nCol1Width;

		if (nCol2Width > nMaxCol2Width)
			nMaxCol2Width = nCol2Width;
	}

	if (g_nTextChar >= 0 && g_nTextChar <= nChars)
	{
		nFaces += 2;
		nVerts += 4;
	}

	if (nFaces == 0)
		return;

	if (nFormatWidth > 0.0f)
	{
		if (bSingleColumn)
		{
			nMaxCol1Width = nFormatWidth;
		}
		else if (nMaxCol1Width + nMaxCol2Width > nFormatWidth)
		{
			nMaxCol1Width = nFormatWidth - nMaxCol2Width;
			bFade = true;
		}
	}
	else
	{
		bFade = false;
	}

	TheseusCreateMeshFVF(nFaces, nVerts, D3DXMESH_MANAGED, D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE, &pMesh);
	*ppMesh = pMesh;

	WORD* indices;
	pMesh->LockIndexBuffer(D3DLOCK_DISCARD, (BYTE**)&indices);

	TEXTVERTEX* verts;
	pMesh->LockVertexBuffer(D3DLOCK_DISCARD, (BYTE**)&verts);

	float r = 1.0f;
	float g = 1.0f;
	float b = 1.0f;
	bool bTitle = false;
	bool bStartPara = true;
	int nCurIndex = 0;
	int nCurVertex = 0;
	float x = 0.0f;
	float y = 0.0f;
	ich = 0;
	for (nLine = 0; ich < nChars; nLine += 1)
	{
		int nColumn = 1;
		int nFirstColVertex = nCurVertex;
		float nCol2Width = 0.0f;
		float nMaxColWidth = nMaxCol1Width;

		int nBreakChar = -1;
		int nBreakVertex = nCurVertex;
		int nBreakIndex = nCurIndex;

		if (bStartPara && ich < nChars && pchText[ich] == '*')
		{
			// See if there's a '*' at the end of the line...
			for (ich2 = ich + 1; ich2 < nChars && pchText[ich2] != '\n' && pchText[ich2] != '\r' && pchText[ich2] != '\t'; ich2 += 1)
				;
			if (ich2 > ich + 1 && pchText[ich2 - 1] == '*')
			{
				// Line starts and ends with a *; don't show the *'s and format as a heading
				r = 1.0f;
				g = 1.0f;
				b = 0.5f;
				bTitle = true;

				ich += 1;
			}
		}

		for (;;)
		{
			TCHAR ch = '\r';
			if (ich < nChars)
			{
				ch = pchText[ich];
				ich += 1;

				if (ch == '\r' && ich < nChars && pchText[ich] == '\n')
					ich += 1;
			}

			if (ch == '\n' || ch == '\r')
			{
                if (bDoNotBreak && bFade)
                {
                    FadeRightEdge(verts + nFirstColVertex, nCurVertex - nFirstColVertex, nMaxColWidth - 1, nMaxColWidth);
                    break;
                }

				if (nColumn == 2)
				{
					// Right justify the second column...
					for (int i = nFirstColVertex; i < nCurVertex; i += 1)
						verts[i].x += nMaxCol2Width - nCol2Width;
				}
				else
				{
					if (bFade)
						FadeRightEdge(verts + nFirstColVertex, nCurVertex - nFirstColVertex, nMaxColWidth - 1, nMaxColWidth);
				}

				if (ich != nChars)
				{
					x = 0.0f;
					if (bTitle || ch == '\r' || !bSingleColumn)
						y -= 1.0f;
					else
						y -= 1.5f;
				}
				bTitle = false;
				bStartPara = true;
				break;
			}

			if (ch == '\t' && bDoNotBreak == false)
			{
				if (bFade)
					FadeRightEdge(verts + nFirstColVertex, nCurVertex - nFirstColVertex, nMaxColWidth - 2, nMaxColWidth);

				nColumn = 2;
				x = nFormatWidth - nMaxCol2Width;
				nFirstColVertex = nCurVertex;
				nMaxColWidth = nMaxCol2Width;
				bTitle = false;
				bStartPara = true;

				if (ich < nChars && pchText[ich] == '*')
				{
					// See if there's a '*' at the end of the line...
					for (ich2 = ich + 1; ich2 < nChars && pchText[ich2] != '\n' && pchText[ich2] != '\r' && pchText[ich2] != '\t'; ich2 += 1)
						;
					if (ich2 > ich + 1 && pchText[ich2 - 1] == '*')
					{
						// Line starts and ends with a *; don't show the *'s and format as a heading
						r = 1.0f;
						g = 1.0f;
						b = 0.5f;
						bTitle = true;

						ich += 1;
					}
				}

				continue;
			}

			if (bTitle && ch == '*')
			{
				// Ignore this if it's the last visible char in the column...
				if (ich == nChars || pchText[ich] == '\t' || pchText[ich] == '\n' || pchText[ich] == '\r')
				{

					r = 1.0f;
					g = 1.0f;
					b = 1.0f;
					continue;
				}
			}

			bStartPara = false;

			int nGlyphIndex = FindGlyphIndex(ch);
			if (nGlyphIndex == -1)
				continue;

			ASSERT(m_rgGlyphLoaded[nGlyphIndex]);

			if (nFormatWidth > 0.0f)
			{
				// bDoNotBreak path historically had an unused width
				// clamp here that never made it past the prototype.
				// All formatting work happens in the wrapping branch.
				if (!bDoNotBreak)
				{
					if (ch != ' ' && nColumn == 1 && x + m_rgGlyphObjects[nGlyphIndex].m_metrics.gmfCellIncX > nFormatWidth)
					{

						// break line

						if (nBreakChar > -1)
						{
							x = 0.0f;
							y -= 1.0f;

							ich = nBreakChar /*- 1*/;
							nCurVertex = nBreakVertex;
							nCurIndex = nBreakIndex;

							nBreakChar = -1;
							nBreakVertex = nCurVertex;
							nBreakIndex = nCurIndex;

							continue;
						}
						else
						{
							// one word was too long to fit, fade it!
							bFade = true;
						}
					}
				}
			}

			CGlyphShape* pGlyphShape = m_rgGlyphObjects[nGlyphIndex].m_glyphShape;

			int j;
			for (j = 0; j < pGlyphShape->m_nIndexCount; j += 1)
				indices[nCurIndex++] = nCurVertex + pGlyphShape->m_indices[j];

			float fontAdjust = 1.0f;

			if(pGlyphShape->m_nGlyphIndex > 190)   //character is in the japanese font set
			{
				fontAdjust = 0.9f;
			}

			for (j = 0; j < pGlyphShape->m_nVertexCount; j += 1)
			{
				verts[nCurVertex].x = x + (pGlyphShape->m_vertices[j].x * fontAdjust);
				verts[nCurVertex].y = y + (pGlyphShape->m_vertices[j].y * fontAdjust);
				verts[nCurVertex].z = 0.0f;

				verts[nCurVertex].nx = 0.0f;
				verts[nCurVertex].ny = 0.0f;
				verts[nCurVertex].nz = 1.0f;

				verts[nCurVertex].color = D3DCOLOR_COLORVALUE(r, g, b, 1.0f);

				nCurVertex += 1;
			}

			if (ich - 1 == g_nTextChar)
			{
				CreateCursorMesh(indices, nCurIndex, verts, nCurVertex, x, y, true);
			}

			x += (m_rgGlyphObjects[nGlyphIndex].m_metrics.gmfCellIncX * fontAdjust);

			if (nColumn == 2)
				nCol2Width += m_rgGlyphObjects[nGlyphIndex].m_metrics.gmfCellIncX;

			if (IsBreakChar(ch))
			{
				nBreakChar = ich;
				nBreakVertex = nCurVertex;
				nBreakIndex = nCurIndex;
			}
		}
	}

	if (g_nTextChar == nChars)
	{
		bool visible = false;
		if(x < 16.556152f)  //value to keep the cursor from extending past the keyboard blank
			visible = true;

		CreateCursorMesh(indices, nCurIndex, verts, nCurVertex, x, y, visible);
	}

	ASSERT(nCurVertex == nVerts);
	ASSERT(nCurIndex == nFaces * 3);

	for (int i = 0; i < nVerts; i += 1)
	{
		verts[i].x *= scale;
		verts[i].y *= scale;
		verts[i].z *= scale;
	}

	if (pMin != NULL && pMax != NULL)
		VERIFYHR(D3DXComputeBoundingBox(verts, nCurVertex, D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE, pMin, pMax));
	
	pMesh->UnlockIndexBuffer();
	pMesh->UnlockVertexBuffer();
}

void CFont::CreateCursorMesh(WORD*& indices, int& nCurIndex, TEXTVERTEX*& verts, int& nCurVertex, float x, float y, bool visible)
{
	float t = (float) (TheseusGetNow()) * 2.0f;
	float a = fabsf(sinf(t * D3DX_PI));
	if(!visible)
		a = 0.0f;

	indices[nCurIndex++] = (WORD)nCurVertex;
	indices[nCurIndex++] = (WORD)nCurVertex + 1;
	indices[nCurIndex++] = (WORD)nCurVertex + 2;

	indices[nCurIndex++] = (WORD)nCurVertex;
	indices[nCurIndex++] = (WORD)nCurVertex + 2;
	indices[nCurIndex++] = (WORD)nCurVertex + 3;

	verts[nCurVertex].x = x;
	verts[nCurVertex].y = y - 0.1f;
	verts[nCurVertex].z = 0.0f;

	verts[nCurVertex].nx = 0.0f;
	verts[nCurVertex].ny = 0.0f;
	verts[nCurVertex].nz = 1.0f;

	verts[nCurVertex].color = D3DCOLOR_COLORVALUE(1.0f, 1.0f, 1.0f, a);

	nCurVertex += 1;

	verts[nCurVertex].x = x + 0.1f;
	verts[nCurVertex].y = y - 0.1f;
	verts[nCurVertex].z = 0.0f;

	verts[nCurVertex].nx = 0.0f;
	verts[nCurVertex].ny = 0.0f;
	verts[nCurVertex].nz = 1.0f;

	verts[nCurVertex].color = D3DCOLOR_COLORVALUE(1.0f, 1.0f, 1.0f, a);

	nCurVertex += 1;

	verts[nCurVertex].x = x + 0.1f;
	verts[nCurVertex].y = y + 0.75f;
	verts[nCurVertex].z = 0.0f;

	verts[nCurVertex].nx = 0.0f;
	verts[nCurVertex].ny = 0.0f;
	verts[nCurVertex].nz = 1.0f;

	verts[nCurVertex].color = D3DCOLOR_COLORVALUE(1.0f, 1.0f, 1.0f, a);

	nCurVertex += 1;

	verts[nCurVertex].x = x;
	verts[nCurVertex].y = y + 0.75f;
	verts[nCurVertex].z = 0.0f;

	verts[nCurVertex].nx = 0.0f;
	verts[nCurVertex].ny = 0.0f;
	verts[nCurVertex].nz = 1.0f;

	verts[nCurVertex].color = D3DCOLOR_COLORVALUE(1.0f, 1.0f, 1.0f, a);

	nCurVertex += 1;
}



struct CFontTableEntry
{
	CFontTableEntry()
	{
		m_faceName = NULL;
		m_fileName = NULL;
	}

	CFontTableEntry(const TCHAR* szFaceName, const TCHAR* szFileName)
	{
		m_faceName = szFaceName;
		m_fileName = szFileName;
	}

	const TCHAR* m_faceName;
	const TCHAR* m_fileName;
	CFont m_font;
};

#define MAX_FONT_COUNT 10

CFontTableEntry g_fonts [MAX_FONT_COUNT] =
{
	// NOTE: The first font is used as the default when a specified font is not found!
	CFontTableEntry(_T("HGGothicM"), _T("HGGothicM.xtf")),
};

int g_nFontCount = 1;

void InitFontTable()
{
	TCHAR szWild [MAX_PATH];
	WIN32_FIND_DATA fd;
	HANDLE h;

	int cchAppDir = _tcslen(TheseusGetFontDir());

	_tcscpy(szWild, TheseusGetFontDir());
	_tcscpy(szWild + cchAppDir, _T("*.xtf"));

#if defined(_XBOX)
	char szWildA [MAX_PATH];
	Ansi(szWildA, szWild, MAX_PATH);
	h = FindFirstFile(szWildA, &fd);
#else
	h = FindFirstFile(szWild, &fd);
#endif

	if (h == INVALID_HANDLE_VALUE)
	{
		TRACE(_T("\001InitFontTable: No fonts!\n"));
		return;
	}

	do
	{
		TCHAR szFileName [MAX_PATH];

		_tcscpy(szFileName, TheseusGetFontDir());

#if defined(_XBOX)
		Unicode(szFileName + cchAppDir, fd.cFileName, countof(szFileName));
#else
		_tcscpy(szFileName + cchAppDir, fd.cFileName);
#endif

		if (g_nFontCount >= MAX_FONT_COUNT)
		{
			TRACE(_T("\001InitFontTable: too many fonts!\n"));
			continue;
		}

		HANDLE hFont = TheseusCreateFile(szFileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
		if (hFont != INVALID_HANDLE_VALUE)
		{
			DWORD dwRead;
			DWORD cbGlyphSet;
			ReadFile(hFont, &cbGlyphSet, 4, &dwRead, NULL);

			if (cbGlyphSet == 0x30465458)
			{
				DWORD dwHeaderLen;
				ReadFile(hFont, &dwHeaderLen, 4, &dwRead, NULL);

				ASSERT(dwHeaderLen == LF_FACESIZE);

				char szFaceA [LF_FACESIZE];
				ReadFile(hFont, szFaceA, LF_FACESIZE, &dwRead, NULL);

				TCHAR* szFace = new TCHAR [strlen(szFaceA) + 1];
#ifdef _UNICODE
				Unicode(szFace, szFaceA, LF_FACESIZE);
#else
				_tcscpy(szFace, szFaceA);
#endif

				// Don't add the default font twice!
				if (_tcscmp(szFace, g_fonts[0].m_faceName) == 0)
				{
					delete [] szFace;
				}
				else
				{
					TCHAR* szFile = new TCHAR [_tcslen(szFileName) + 1];
					_tcscpy(szFile, szFileName);

					g_fonts[g_nFontCount].m_faceName = szFace;
					g_fonts[g_nFontCount].m_fileName = szFile;
					g_nFontCount += 1;

					TRACE(_T("Adding font: %s (%s)\n"), szFace, szFile);
				}
			}

			CloseHandle(hFont);
		}
	}
	while (FindNextFile(h, &fd));

	FindClose(h);
}

CFont* GetFont(const TCHAR* szFaceName)
{
	if (g_nFontCount == 1)
		InitFontTable();

	int i;
	for (i = 0; i < g_nFontCount; i += 1)
	{
		if (_tcsicmp(szFaceName, g_fonts[i].m_faceName) == 0)
			break;
	}

	if (i == g_nFontCount)
	{
		TRACE(_T("Substituting font '%s' for '%s'\n"), g_fonts[0].m_faceName, szFaceName);
		i = 0;
	}

	if (g_fonts[i].m_font.m_hFile == INVALID_HANDLE_VALUE)
	{
		TCHAR szFontPath [MAX_PATH];

		if (_tcschr(g_fonts[i].m_fileName, ':') == NULL)
				_stprintf(szFontPath, _T("%s%s"), TheseusGetFontDir(), g_fonts[i].m_fileName);
		else
			_tcscpy(szFontPath, g_fonts[i].m_fileName);

		if (!g_fonts[i].m_font.Open(szFontPath))
		{
			TRACE(_T("\001Cannot load font: %s\n"), szFaceName);

			if (g_fonts[0].m_font.m_hFile == INVALID_HANDLE_VALUE)
			{
				if (!g_fonts[0].m_font.Open(g_fonts[0].m_fileName))
				{
					ASSERT(FALSE);
					return NULL;
				}
			}
		}
	}

	return &g_fonts[i].m_font;
}

void Text_Exit()
{
	for (int i = 0; i < g_nFontCount; i += 1)
	{
		if (i > 0)
		{
			delete [] (TCHAR*)g_fonts[i].m_faceName;
			delete [] (TCHAR*)g_fonts[i].m_fileName;
			g_fonts[i].m_faceName = NULL;
			g_fonts[i].m_fileName = NULL;
		}
		g_fonts[i].m_font.Close();
	}
}


// Overlay text API - renders .xtf font glyphs as screen-space triangles


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
			width += pFont->m_rgGlyphObjects[idx].m_metrics.gmfCellIncX;
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

		if (!pFont->m_rgGlyphLoaded[glyphIdx])
			pFont->LoadGlyph(glyphIdx);

		CGlyphShape* shape = pFont->m_rgGlyphObjects[glyphIdx].m_glyphShape;
		if (!shape)
		{
			curX += pFont->m_rgGlyphObjects[glyphIdx].m_metrics.gmfCellIncX;
			continue;
		}

		// Flush if we'd exceed buffer limits
		if (nVerts + shape->m_nVertexCount > MAX_OVL_VERTS ||
			nIndices + shape->m_nIndexCount > MAX_OVL_INDICES)
		{
			dev->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
			{ extern int g_drawCallsThisFrame, g_drawCallsTextFrame;
		  g_drawCallsThisFrame++; g_drawCallsTextFrame++; }
		dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, nVerts, nIndices / 3,
				s_ovlIndices, D3DFMT_INDEX16, s_ovlVerts, sizeof(OverlayTextVert));
			nVerts = 0;
			nIndices = 0;
		}

		// Add indices (offset by current vertex count)
		for (int i = 0; i < shape->m_nIndexCount; i++)
			s_ovlIndices[nIndices++] = (WORD)(nVerts + shape->m_indices[i]);

		// Transform glyph vertices to screen space
		for (int j = 0; j < shape->m_nVertexCount; j++)
		{
			s_ovlVerts[nVerts].x = screenX + (curX + shape->m_vertices[j].x) * scale;
			s_ovlVerts[nVerts].y = screenY + scale - shape->m_vertices[j].y * scale;
			s_ovlVerts[nVerts].z = 0.0f;
			s_ovlVerts[nVerts].rhw = 1.0f;
			s_ovlVerts[nVerts].color = color;
			nVerts++;
		}

		curX += pFont->m_rgGlyphObjects[glyphIdx].m_metrics.gmfCellIncX;
	}

	// Final flush
	if (nIndices > 0)
	{
		{ extern int g_drawCallsThisFrame, g_drawCallsTextFrame;
		  g_drawCallsThisFrame++; g_drawCallsTextFrame++; }
		dev->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
		dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, nVerts, nIndices / 3,
			s_ovlIndices, D3DFMT_INDEX16, s_ovlVerts, sizeof(OverlayTextVert));
	}

	return curX * scale;
}



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

	DECLARE_NODE_PROPS()

	LPD3DXMESH m_mesh;
	D3DXVECTOR3 m_bboxMin, m_bboxMax;
	int m_nLanguage;
	XTIME m_timeToScroll;
};

static const float nScrollSpace = 1.5f;

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
END_NODE_PROPS()

CTextNode::CTextNode() :
	m_text(NULL),
	m_font(NULL),
	m_justify(NULL),
	m_deviation(0.01f),
	m_extrusion(1.0f),
	m_translate(true),
	m_width(0.0f),
	m_height(0.0f),
	m_scroll(0.0f),
	m_scrollRate(0.0f),
	m_scrollDelay(0.0f)
{
	m_mesh = NULL;
	m_nLanguage = 0;
	m_timeToScroll = 0.0f;
}

CTextNode::~CTextNode()
{
	delete [] m_text;
	delete [] m_font;
	delete [] m_justify;

	if (m_mesh != NULL)
		m_mesh->Release();
}

void CTextNode::Advance(float nSeconds)
{
	CNode::Advance(nSeconds);

	if (m_scrollRate > 0.0f && m_mesh != NULL)
	{
		XTIME now = TheseusGetNow();

		if (m_timeToScroll == 0.0f)
			m_timeToScroll = now + m_scrollDelay;

		if (now >= m_timeToScroll)
		{
			float nContentHeight = m_bboxMax.y - m_bboxMin.y;
			if (nContentHeight > m_height)
			{
				m_scroll += m_height * nSeconds * m_scrollRate;
				if (m_scroll >= nContentHeight + nScrollSpace)
					m_scroll -= nContentHeight + nScrollSpace;
			}
		}
	}
}

const TCHAR* FaceFromFont(const TCHAR* szFont)
{
	const TCHAR* szFace = szFont;
/*
	if (nLanguage == LANGUAGE_JAPANESE)
	{
		if (szFont == NULL || _tcsicmp(szFont, _T("body")) == 0)
			szFace = _T("HGSSoEikakuGothicB");
		else if (_tcsicmp(szFont, _T("heading")) == 0)
			szFace = _T("HGMaroGothicMPRO");
	}
	else
*/
	{
		if (szFont == NULL || _tcsicmp(szFont, _T("body")) == 0)
			szFace = _T("XBox Book");
		else if (_tcsicmp(szFont, _T("heading")) == 0)
			szFace = _T("Xbox");
	}

	return szFace;
}

void CTextNode::Render()
{
	bool bInvalidMesh = false;

	if (g_szText != NULL)
	{
		if (m_text == NULL || _tcscmp(m_text, g_szText) != 0 || g_nTextChar != g_nTextCharLast)
		{
			delete [] m_text;
			m_text = new TCHAR [_tcslen(g_szText) + 1];
			_tcscpy(m_text, g_szText);
			g_nTextCharLast = g_nTextChar;
			bInvalidMesh = true;
		}

		g_szText = NULL;
	}

	const TCHAR* szText = m_text;
	if (szText == NULL || szText[0] == 0)
		return;

	if (_tcsncmp(m_text, _T("<clock>"), 7) == 0)
	{
		SYSTEMTIME st;
		GetLocalTime(&st);

		TCHAR szBuf [32];
		FormatTime(szBuf, countof(szBuf), &st);

		if (_tcscmp(m_text + 7, szBuf) != 0)
		{
			delete [] m_text;
			m_text = new TCHAR [7 + _tcslen(szBuf) + 1];
			_stprintf(m_text, _T("<clock>%s"), szBuf);

			bInvalidMesh = true;
		}

		m_translate = false;
		szText = m_text + 7; // look past the <clock>
	}

	if (m_translate && m_nLanguage != g_nCurLanguage)
		bInvalidMesh = true;

	if (m_mesh != NULL && bInvalidMesh)
	{
		m_mesh->Release();
		m_mesh = NULL;
	}

	if (m_mesh == NULL)
	{
        TCHAR sz[MAX_TRANSLATE_LEN];

		if (m_translate)
			szText = Translate(szText, sz);

		m_nLanguage = g_nCurLanguage;

		CFont* pFont = GetFont(FaceFromFont(m_font));
		pFont->CreateTextMesh(szText, -1, &m_mesh, &m_bboxMin, &m_bboxMax, fabsf(m_width), m_width < 0.0f, g_nCurLanguage == LANGUAGE_JAPANESE ? 1.0f : 1.0f);
		if (m_mesh == NULL)
			return;
	}

	float nContentHeight = m_bboxMax.y - m_bboxMin.y;

	float xOffset = 0.0f;
	if (m_justify != NULL)
	{
		float nWidth = m_bboxMax.x - m_bboxMin.x;
		if (m_width != 0.0f)
		{
			float widthLimit = fabsf(m_width);
			if (nWidth > widthLimit)
				nWidth = widthLimit;
		}

		if (_tcsicmp(m_justify, _T("middle")) == 0)
		{
			// Center justify. The original prototype was going to fade
			// both edges when the text overflowed a negative-signed
			// width clamp; never made it past the wishlist stage.
			xOffset = -nWidth / 2.0f;
		}
		else if (_tcsicmp(m_justify, _T("end")) == 0)
		{
			// End / right justify. Same fade-left-side wish never
			// landed.
			xOffset = -nWidth;
		}
	}

	for (int i = 0; i < 2; i += 1)
	{
		if (m_height != 0.0f && nContentHeight > m_height)
		{
			TEXTVERTEX* verts;
			m_mesh->LockVertexBuffer(0, (BYTE**)&verts);
			float yTop = -m_scroll + 1.0f;
			float yBottom = yTop - m_height - 2.0f;
			if (m_scroll == 0.0f)
				yTop += 1.0f; // don't fade at top when not scrolled...
			VerticalFade(verts, m_mesh->GetNumVertices(), yTop/* + 0.5f*/, yBottom, i == 0 ? 0 : -(nContentHeight + nScrollSpace));
			m_mesh->UnlockVertexBuffer();
		}
		else if (i == 1)
		{
			// Don't need second pass if not scrolling...
			break;
		}

		// Don't need second pass unless part of it is visible...
		if (i == 1 && m_scroll + m_height < nContentHeight)
			break;

		TheseusPushWorld();
		TheseusTranslateWorld(xOffset, (i == 0 ? 0 : -(nContentHeight + nScrollSpace)) + m_scroll, 0.0f);
		TheseusUpdateWorld();

		TheseusSetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
		m_mesh->DrawSubset(0);
		TheseusSetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);

		TheseusPopWorld();
	}
}


bool CTextNode::OnSetProperty(const PRD* pprd, const void* pvValue)
{
	if (PTR2INT(pprd->pbOffset) == offsetof(m_scroll))
		return true; // don't need to invalidate

	if (m_mesh != NULL)
	{
		m_mesh->Release();
		m_mesh = NULL;
	}

	m_scroll = 0;
	m_timeToScroll = 0.0f;

	return true;
}



#define MAX_TEXT_CACHE	10

struct CTextCacheEntry
{
	CTextNode* m_textNode;
	XTIME m_usage;
};

static CTextCacheEntry textCache [MAX_TEXT_CACHE];


static CTextCacheEntry* FindText(const TCHAR* szText, float nWidth)
{
	CTextCacheEntry* pFreeOne = NULL;
	CTextCacheEntry* pOldOne = NULL;
	for (int i = 0; i < countof(textCache); i += 1)
	{
		if (textCache[i].m_textNode != NULL && textCache[i].m_textNode->m_text != NULL && _tcscmp(textCache[i].m_textNode->m_text, szText) == 0 && textCache[i].m_textNode->m_width == nWidth)
		{
			textCache[i].m_usage = TheseusGetNow();
			return &textCache[i];
		}

		if (pFreeOne != NULL)
			continue;

		if (textCache[i].m_textNode == NULL)
			pFreeOne = &textCache[i];
		else if (pOldOne == NULL || pOldOne->m_usage > textCache[i].m_usage)
			pOldOne = &textCache[i];
	}

	if (pFreeOne == NULL && pOldOne != NULL)
	{
		delete pOldOne->m_textNode;
		ZeroMemory(pOldOne, sizeof (CTextCacheEntry));
		pFreeOne = pOldOne;
	}

	ASSERT(pFreeOne != NULL);

	pFreeOne->m_textNode = new CTextNode;
	pFreeOne->m_textNode->m_text = new TCHAR [_tcslen(szText) + 1];
	pFreeOne->m_textNode->m_width = nWidth;
	_tcscpy(pFreeOne->m_textNode->m_text, szText);

	pFreeOne->m_usage = TheseusGetNow();

	return pFreeOne;
}

CNode* GetTextNode(const TCHAR* szText, float nWidth)
{
	CTextCacheEntry* pTextCache = FindText(szText, nWidth);
	pTextCache->m_textNode->m_translate = false;
	return pTextCache->m_textNode;
}
