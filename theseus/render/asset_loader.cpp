// asset_loader.cpp: textures and meshes. The Texture and ImageTexture nodes,
// the hundred slot texture cache with its background file loader, the XPR
// texture parser, the Mesh node and CMesh, the vertex and index buffers it
// reads from a .xm file. Rebuilt from the 5960 retail XBE (textures
// 0x60990..0x61840, meshes 0x5bce0..0x5c6c0); the findings are in
// docs/decomp/AssetLoader.md.
//
// Theseus adds the skin folder probing in front of the XIP, the loose file
// fallbacks, the skin texture preload, and the flushes a skin switch uses.
// Both builds compile this file. The desktop seams are the texture decoders,
// the preload's folder scan, the NV2A states and the inspector's names.

#include "std.h"
#include "theseus.h"
#include "node.h"
#include "shape_render.h"
#include "runner.h"
#include "asset_loader.h"
#include "skin_assets.h"
#include "activefile.h"
#include "xip_archive.h"
#include "file_util.h"
#ifdef _XBOX
extern "C" void WINAPI D3D_FreeNoncontiguousMemory(void* pMemory);
extern BOOL g_bEdgeAntialiasOverride;
#else
#include "xbx_texture.h"
#include "host.h"
extern int g_nVertPerFrame;
extern int g_nTriPerFrame;
#endif


// ===== Textures =============================================================

// CreateTexture(width, height, format)
// Reconstructed from 5960 retail XBE @ 0x00060d40. Ghidra decompile:
//     if (D3DXCreateTexture(dev, *w, *h, 1, 0, fmt, MANAGED, &tex) < 0) return NULL;
//     *w, *h = the size the texture came out at; return tex;
LPDIRECT3DTEXTURE8 CreateTexture(int& nWidth, int& nHeight, D3DFORMAT format)
{
	LPDIRECT3DTEXTURE8 pTex = NULL;
	if (FAILED(D3DXCreateTexture(TheseusGetD3DDev(), nWidth, nHeight, 1, 0, format, D3DPOOL_MANAGED, &pTex)))
		return NULL;

	TheseusGetTextureSize(pTex, nWidth, nHeight);
	return pTex;
}


// CTexture: 0x30 bytes, factory 0x00060db0, vtable 0x0002a518. The factory
// sets repeatS (0x1c) and repeatT (0x1d), format A8R8G8B8 (0x20) and a NULL
// surface (0x24). titleImage is Theseus's.
IMPLEMENT_NODE("Texture", CTexture, CNode)

START_NODE_PROPS(CTexture, CNode)
	NODE_PROP(pt_boolean, CTexture, repeatS)
	NODE_PROP(pt_boolean, CTexture, repeatT)
	NODE_PROP(pt_boolean, CTexture, titleImage)
END_NODE_PROPS()

CTexture::CTexture()
	: m_repeatS(true), m_repeatT(true), m_titleImage(false)
{
	m_format = D3DFMT_A8R8G8B8;
	m_surface = NULL;
}

// Destructor @ 0x00060e30: releases the surface, then the node.
CTexture::~CTexture()
{
	if (m_surface)
		m_surface->Release();
}

LPDIRECT3DTEXTURE8 CTexture::GetTextureSurface()
{
	return m_surface;
}

// CTexture::Create(width, height)   [vtable slot 23]
// Reconstructed from 5960 retail XBE @ 0x00060ec0. Ghidra decompile:
//     this->width = w; this->height = h;
//     this->surface = CreateTexture(&w, &h); return surface != NULL;
bool CTexture::Create(int nWidth, int nHeight)
{
	m_nImageWidth = nWidth;
	m_nImageHeight = nHeight;
	m_surface = ::CreateTexture(nWidth, nHeight, m_format);
	return m_surface != NULL;
}


// CImageTexture: 0x38 bytes, factory 0x00060ef0, vtable 0x0002a4b8. url at
// 0x30, alpha at 0x34, the reload flag at 0x35 (starts set). fromfile and the
// list of live image textures, which FlushTextureCache walks, are Theseus's.
IMPLEMENT_NODE("ImageTexture", CImageTexture, CTexture)

START_NODE_PROPS(CImageTexture, CTexture)
	NODE_PROP(pt_string, CImageTexture, url)
	NODE_PROP(pt_boolean, CImageTexture, alpha)
	NODE_PROP(pt_boolean, CImageTexture, fromfile)
END_NODE_PROPS()

CImageTexture* CImageTexture::c_pFirstImageTexture = NULL;

CImageTexture::CImageTexture()
	: m_url(NULL), m_alpha(false), m_fromfile(false)
{
	m_dirty = true;
	m_surface = NULL;

	m_nextImageTexture = c_pFirstImageTexture;
	c_pFirstImageTexture = this;
}

// Destructor @ 0x00060f70: frees url, then runs the CTexture destructor.
CImageTexture::~CImageTexture()
{
	CImageTexture** pp = &c_pFirstImageTexture;
	while (*pp != NULL && *pp != this)
		pp = &(*pp)->m_nextImageTexture;
	if (*pp != NULL)
		*pp = m_nextImageTexture;

	delete [] m_url;
}

// CImageTexture::GetTextureSurface()   [vtable slot 22]
// Reconstructed from 5960 retail XBE @ 0x00060f90. Ghidra decompile:
//     if (this->dirty && this->url) { this->dirty = 0; Load(this->url); }
//     return this->surface;
LPDIRECT3DTEXTURE8 CImageTexture::GetTextureSurface()
{
	if (m_dirty && m_url != NULL)
	{
		m_dirty = false;
		Load(m_url);
	}
	return m_surface;
}

// CImageTexture::OnSetProperty(prd, value)   [vtable slot 2]
// Reconstructed from 5960 retail XBE @ 0x00061820. Ghidra decompile:
//     if (prd->offset == 0x30) this->dirty = 1;
// A new url reloads on the next GetTextureSurface.
bool CImageTexture::OnSetProperty(const PRD* pprd, const void* pvValue)
{
	if (PTR2INT(pprd->pbOffset) == offsetof(m_url))
		m_dirty = true;
	return true;
}


// ===== Background loader ====================================================

// CBackgroundLoader: 0x2c bytes, vtable 0x0002a578 (slot 0 the deleting
// destructor, slot 1 OnComplete). It reads a whole file with one overlapped
// ReadFile; the frame loop finishes it. Loaders queue at the tail of a list
// at 0x0017e6f0.
class CBackgroundLoader
{
public:
	CBackgroundLoader();
	virtual ~CBackgroundLoader();
	virtual void OnComplete() {}

	bool Fetch(const TCHAR* szURL);

	static CBackgroundLoader* c_pFirst;
	CBackgroundLoader* m_next;	// 0x04
	HANDLE m_file;			// 0x08
	OVERLAPPED m_io;		// 0x0c
	BYTE* m_data;			// 0x20
	int m_dataSize;			// 0x24
	TCHAR* m_url;			// 0x28
};

CBackgroundLoader* CBackgroundLoader::c_pFirst = NULL;

// Constructor, reconstructed from 5960 retail XBE @ 0x00060fc0:
//     overlapped = 0; url = data = size = 0; file = INVALID; next = 0;
//     append this at the end of the loader list;
CBackgroundLoader::CBackgroundLoader()
{
	ZeroMemory(&m_io, sizeof(m_io));
	m_file = INVALID_HANDLE_VALUE;
	m_data = NULL;
	m_dataSize = 0;
	m_url = NULL;
	m_next = NULL;

	CBackgroundLoader** ppTail = &c_pFirst;
	while (*ppTail != NULL)
		ppTail = &(*ppTail)->m_next;
	*ppTail = this;
}

// Destructor, reconstructed from 5960 retail XBE @ 0x00061030:
//     unlink this; if (file != INVALID) CloseHandle(file); free(url); free(data);
CBackgroundLoader::~CBackgroundLoader()
{
	for (CBackgroundLoader** pp = &c_pFirst; *pp != NULL; pp = &(*pp)->m_next)
	{
		if (*pp == this)
		{
			*pp = m_next;
			break;
		}
	}

	if (m_file != INVALID_HANDLE_VALUE)
		CloseHandle(m_file);
	delete [] m_url;
	delete [] m_data;
}

// CBackgroundLoader::Fetch(url)
// Reconstructed from 5960 retail XBE @ 0x00061090. Ghidra decompile:
//     MakeAbsoluteURL(abs, url); this->url = wcsdup(abs);
//     flags = (url is "c:" or "y:", either case) ? NO_BUFFERING : 0;
//     file = CreateFile(abs, GENERIC_READ, SHARE_READ, 0, OPEN_EXISTING,
//                       flags | OVERLAPPED | SEQUENTIAL_SCAN, 0);
//     if (file == INVALID) return 0;
//     size = (GetFileSize(file) + 511) & ~511; data = new BYTE[size]; overlapped = 0;
//     if (!ReadFile(file, data, size, 0, &overlapped)) { if (GetLastError() != IO_PENDING) return 0; }
//     else { OnComplete(); delete this; }
//     return 1;
// Unbuffered overlapped reads want whole sectors, hence the round up. Theseus
// also turns off buffering for the e:, f:, g:, r: and s: hard drive partitions.
bool CBackgroundLoader::Fetch(const TCHAR* szURL)
{
	TCHAR szPath[MAX_PATH];
	MakeAbsoluteURL(szPath, szURL);

	m_url = new TCHAR[_tcslen(szPath) + 1];
	_tcscpy(m_url, szPath);

	DWORD dwFlags = FILE_FLAG_OVERLAPPED | FILE_FLAG_SEQUENTIAL_SCAN;
#ifdef _XBOX
	if (szURL[0] > 0 && szURL[0] < 128 && szURL[1] == ':' && strchr("cyefgrs", (char)_totlower(szURL[0])) != NULL)
		dwFlags |= FILE_FLAG_NO_BUFFERING;
#endif

	m_file = TheseusCreateFile(szPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, dwFlags, NULL);
	if (m_file == INVALID_HANDLE_VALUE)
		return false;

	m_dataSize = GetFileSize(m_file, NULL);
#ifdef _XBOX
	m_dataSize = (m_dataSize + 511) / 512 * 512;
#endif
	m_data = new BYTE[m_dataSize];
	ZeroMemory(&m_io, sizeof(m_io));

	if (ReadFile(m_file, m_data, m_dataSize, NULL, &m_io))
	{
		OnComplete();
		delete this;
		return true;
	}

	return GetLastError() == ERROR_IO_PENDING;
}

// BackgroundLoader_Frame()
// Inlined in the 5960 frame loop (XApp main loop @ 0x0002da00, after the
// lerpers advance and before Draw):
//     for (p = first; p; p = next) { next = p->next;
//         if (p->overlapped.Internal != STATUS_PENDING) { p->OnComplete(); delete p; } }
void BackgroundLoader_Frame()
{
	CBackgroundLoader* pNext;
	for (CBackgroundLoader* p = CBackgroundLoader::c_pFirst; p != NULL; p = pNext)
	{
		pNext = p->m_next;
		if (HasOverlappedIoCompleted(&p->m_io))
		{
			p->OnComplete();
			delete p;
		}
	}
}


// ===== Texture cache ========================================================

// One of the hundred cache slots at 0x0017da70, 0x20 bytes each.
struct CachedTexture
{
	TCHAR* m_url;			// 0x00
	LPDIRECT3DTEXTURE8 m_texture;	// 0x04
	XTIME m_lastUsed;		// 0x08
	bool m_loading;			// 0x10
	XTIME m_timeLoaded;		// 0x18
};

static CachedTexture g_textureCache[100];
static CachedTexture* g_pLockedTexture;		// 0x0017da68, the slot being filled

// Desktop: the inspector shows which file a texture came from.
static void NameTexture(LPDIRECT3DTEXTURE8 pTex, const TCHAR* szURL)
{
#ifndef _XBOX
	if (pTex != NULL && szURL != NULL)
	{
		Ansi(pTex->m_srcName, szURL, sizeof(pTex->m_srcName) - 1);
		pTex->m_srcName[sizeof(pTex->m_srcName) - 1] = 0;
	}
#endif
}

static void EmptySlot(CachedTexture* pSlot)
{
	delete [] pSlot->m_url;
	if (pSlot->m_texture != NULL)
		pSlot->m_texture->Release();
	ZeroMemory(pSlot, sizeof(*pSlot));
}

// CBackgroundTexture: 0x38 bytes, vtable 0x0002a4ac. Built inline by
// FindTexture: the cache slot at 0x2c, the size it must have at 0x30/0x34.
class CBackgroundTexture : public CBackgroundLoader
{
public:
	CBackgroundTexture(CachedTexture* pSlot, UINT width, UINT height)
		: m_slot(pSlot), m_width(width), m_height(height) {}
	~CBackgroundTexture();
	void OnComplete();

	CachedTexture* m_slot;
	UINT m_width;
	UINT m_height;
};

// Destructor, reconstructed from 5960 retail XBE @ 0x000611f0:
//     if (slot) slot->loading = 0;
CBackgroundTexture::~CBackgroundTexture()
{
	if (m_slot != NULL)
		m_slot->m_loading = false;
}

// CBackgroundTexture::OnComplete()   [vtable slot 1]
// Reconstructed from 5960 retail XBE @ 0x00061210. Ghidra decompile:
//     slot->texture = ParseTexture(url, data, size, width, height);
//     if (!slot->texture && width == 64)  slot->texture = LoadTexture(L"xboxlogo64.xbx");
//     else if (!slot->texture && width == 128) slot->texture = LoadTexture(L"xboxlogo128.xbx");
//     slot->loading = 0; slot->timeLoaded = now;
// A title image that's missing or the wrong size shows the Xbox logo instead.
void CBackgroundTexture::OnComplete()
{
	LPDIRECT3DTEXTURE8 pTex = ParseTexture(m_url, m_data, m_dataSize, m_width, m_height);
	if (pTex == NULL && m_width == 64)
		pTex = LoadTexture(_T("xboxlogo64.xbx"), 64, 64);
	else if (pTex == NULL && m_width == 128)
		pTex = LoadTexture(_T("xboxlogo128.xbx"), 128, 128);

	m_slot->m_texture = pTex;
	m_slot->m_loading = false;
	m_slot->m_timeLoaded = TheseusGetNow();
	NameTexture(pTex, m_url);
}

// CleanupTextureCache()
// Reconstructed from 5960 retail XBE @ 0x00061290. Ghidra decompile (the
// loop is unrolled ten slots at a time):
//     for each slot: if (url && slot != locked && !loading && (!best || lastUsed < best->lastUsed)) best = slot;
//     if (!best) return 0;
//     free(best->url); Release(best->texture); zero best; return 1;
// The memory allocators call this first when an allocation fails.
bool CleanupTextureCache()
{
	CachedTexture* pOldest = NULL;
	for (int i = 0; i < countof(g_textureCache); i++)
	{
		CachedTexture* pSlot = &g_textureCache[i];
		if (pSlot->m_url == NULL || pSlot == g_pLockedTexture || pSlot->m_loading)
			continue;
		if (pOldest == NULL || pSlot->m_lastUsed < pOldest->m_lastUsed)
			pOldest = pSlot;
	}

	if (pOldest == NULL)
		return false;

	EmptySlot(pOldest);
	return true;
}

// Theseus: drops every cached texture that isn't mid load and makes each
// ImageTexture fetch its surface again. A skin switch calls this.
void FlushTextureCache()
{
	for (int i = 0; i < countof(g_textureCache); i++)
	{
		CachedTexture* pSlot = &g_textureCache[i];
		if (pSlot->m_url != NULL && !pSlot->m_loading)
			EmptySlot(pSlot);
	}

	for (CImageTexture* pImg = CImageTexture::c_pFirstImageTexture; pImg != NULL; pImg = pImg->m_nextImageTexture)
	{
		if (pImg->m_surface != NULL)
		{
			pImg->m_surface->Release();
			pImg->m_surface = NULL;
		}
		pImg->m_dirty = true;
	}

	OutputDebugString(_T("[FlushTextureCache] All cached textures released\n"));
}

// FindTexture(url, async, width, height, inXIP)
// Reconstructed from 5960 retail XBE @ 0x000615a0. Ghidra decompile:
//     for i in 0..99:
//         if (slot[i].url && !wcsicmp(slot[i].url, url)) { slot[i].lastUsed = now; return &slot[i]; }
//         if (!free && !slot[i].loading)
//             if (!slot[i].url) free = &slot[i];
//             else if (!old || slot[i].lastUsed < old->lastUsed) old = &slot[i];
//     if (!free && old) { free(old->url); Release(old->texture); zero old; free = old; }
//     locked = free; free->url = wcsdup(url); free->lastUsed = now;
//     if (!async || inXIP) { free->texture = LoadTexture(url); free->timeLoaded = now; }
//     else { free->loading = 1; p = new CBackgroundTexture(free, width, height);
//            if (!p->Fetch(url) && p) delete p; }
//     locked = NULL; return free;
// The first free slot wins; failing that the least recently used one that
// isn't loading is thrown out. A texture already in an XIP loads right away.
CachedTexture* FindTexture(const TCHAR* szURL, bool bAsync, UINT width, UINT height, bool binXIP = false)
{
	CachedTexture* pFree = NULL;
	CachedTexture* pOldest = NULL;

	for (int i = 0; i < countof(g_textureCache); i++)
	{
		CachedTexture* pSlot = &g_textureCache[i];
		if (pSlot->m_url != NULL && _tcsicmp(pSlot->m_url, szURL) == 0)
		{
			pSlot->m_lastUsed = TheseusGetNow();
			return pSlot;
		}

		if (pFree != NULL || pSlot->m_loading)
			continue;
		if (pSlot->m_url == NULL)
			pFree = pSlot;
		else if (pOldest == NULL || pSlot->m_lastUsed < pOldest->m_lastUsed)
			pOldest = pSlot;
	}

	if (pFree == NULL && pOldest != NULL)
	{
		EmptySlot(pOldest);
		pFree = pOldest;
	}

	g_pLockedTexture = pFree;
	pFree->m_url = new TCHAR[_tcslen(szURL) + 1];
	_tcscpy(pFree->m_url, szURL);
	pFree->m_lastUsed = TheseusGetNow();

	if (!bAsync || binXIP)
	{
		pFree->m_texture = LoadTexture(szURL, width, height);
		pFree->m_timeLoaded = TheseusGetNow();
		NameTexture(pFree->m_texture, szURL);
	}
	else
	{
		pFree->m_loading = true;
		CBackgroundTexture* pLoader = new CBackgroundTexture(pFree, width, height);
		if (!pLoader->Fetch(szURL))
			delete pLoader;
	}

	g_pLockedTexture = NULL;
	return pFree;
}

// GetTexture(url, &timeLoaded, width, height, inXIP)
// 5960 has no function for this; CIconMatInfo_Setup (0x000572d0) calls
// FindTexture(url, 1, width, height, inXIP) and reads the slot itself.
LPDIRECT3DTEXTURE8 GetTexture(const TCHAR* szURL, XTIME* pTimeLoaded, UINT width, UINT height, bool binXIP = false)
{
	CachedTexture* pSlot = FindTexture(szURL, true, width, height, binXIP);
	if (pSlot->m_texture == NULL)
		return NULL;

	if (pTimeLoaded != NULL)
		*pTimeLoaded = pSlot->m_timeLoaded;
	return pSlot->m_texture;
}

// CImageTexture::Load(url)
// Reconstructed from 5960 retail XBE @ 0x00061760. Ghidra decompile:
//     if (surface) { Release(surface); surface = NULL; }
//     slot = FindTexture(url, 0, 0, 0, 0);
//     if (slot->texture) { surface = slot->texture; AddRef(surface);
//                          width, height = the surface's level 0 size; }
void CImageTexture::Load(const TCHAR* szURL)
{
	if (m_surface != NULL)
	{
		m_surface->Release();
		m_surface = NULL;
	}

	CachedTexture* pSlot = FindTexture(szURL, false, 0, 0);
	if (pSlot->m_texture == NULL)
		return;

	m_surface = pSlot->m_texture;
	m_surface->AddRef();
	TheseusGetTextureSize(m_surface, m_nImageWidth, m_nImageHeight);
}


// ===== Texture files ========================================================

#ifdef _XBOX
// Copies a texture header into its own memory and its pixels into
// contiguous memory, then points the header at them. The tail of both
// branches of ParseTexture below.
static LPDIRECT3DTEXTURE8 MakeTexture(const void* pHeader, const BYTE* pbPixels, int cbPixels)
{
	D3DTexture* pTex = (D3DTexture*)TheseusD3D_AllocNoncontiguousMemory(sizeof(D3DTexture));
	if (pTex == NULL)
		return NULL;
	CopyMemory(pTex, pHeader, sizeof(D3DTexture));

	BYTE* pbData = (BYTE*)TheseusD3D_AllocContiguousMemory(cbPixels, D3DTEXTURE_ALIGNMENT);
	if (pbData == NULL)
	{
		D3D_FreeNoncontiguousMemory(pTex);
		return NULL;
	}
	CopyMemory(pbData, pbPixels, cbPixels);

	pTex->Data = 0;
	pTex->Register(pbData);
	pTex->Common |= D3DCOMMON_D3DCREATED;
	return pTex;
}

// The edge length a format's size field encodes: 1 << n, or 0 past 4096.
static UINT FormatEdge(DWORD dwFormat, DWORD dwMask, DWORD dwShift)
{
	DWORD n = (dwFormat & dwMask) >> dwShift;
	return n <= 12 ? 1u << n : 0;
}
#endif

// ParseTexture(url, data, size, width, height)
// Reconstructed from 5960 retail XBE @ 0x00060990. Ghidra decompile:
//     ext = wcsrchr(url, '.');
//     if (ext == "xt") return header (first 0x14 bytes) + the rest as pixels;
//     if (ext == "xbx" && data->magic == 'XPR0') {
//         pixels = total - headerSize;
//         if (headerSize - 12 < 0x14 || res->type != TEXTURE || pixels <= 0
//             || size < headerSize + pixels) return NULL;
//         if (width && height && (1 << usize(res->format) != width
//                                 || 1 << vsize(res->format) != height)) return NULL;
//         return res (at data + 12) + pixels at data + headerSize;
//     }
//     loop: hr = D3DXCreateTextureFromFileInMemoryEx(dev, data, size, DEFAULT, DEFAULT,
//               1, 0, UNKNOWN, MANAGED, DEFAULT, DEFAULT, 0, 0, 0, &tex);
//           S_OK: return tex; E_OUTOFMEMORY: free a cache and retry; else NULL;
// The memory retry is the same one NewFailed runs. Theseus frees the header
// when the pixel allocation fails; 5960's allocator reboots before that.
LPDIRECT3DTEXTURE8 ParseTexture(const TCHAR* szURL, const BYTE* pbContent, int cbContent, UINT width, UINT height)
{
#ifdef _XBOX
	const TCHAR* pszExt = _tcsrchr(szURL, '.');
	if (pszExt != NULL)
	{
		pszExt++;
		if (_tcsicmp(pszExt, _T("xt")) == 0)
			return MakeTexture(pbContent, pbContent + sizeof(D3DTexture), cbContent - sizeof(D3DTexture));

		const XPR_HEADER* pXpr = (const XPR_HEADER*)pbContent;
		if (_tcsicmp(pszExt, _T("xbx")) == 0 && pXpr->dwMagic == XPR_MAGIC_VALUE)
		{
			const D3DTexture* pRes = (const D3DTexture*)(pXpr + 1);
			int cbPixels = pXpr->dwTotalSize - pXpr->dwHeaderSize;

			if (pXpr->dwHeaderSize - sizeof(XPR_HEADER) < sizeof(D3DTexture))
				return NULL;
			if ((pRes->Common & D3DCOMMON_TYPE_MASK) != D3DCOMMON_TYPE_TEXTURE)
				return NULL;
			if (cbPixels <= 0 || (UINT)cbContent < pXpr->dwHeaderSize + cbPixels)
				return NULL;

			if (width != 0 && height != 0)
			{
				if (FormatEdge(pRes->Format, D3DFORMAT_USIZE_MASK, D3DFORMAT_USIZE_SHIFT) != width ||
					FormatEdge(pRes->Format, D3DFORMAT_VSIZE_MASK, D3DFORMAT_VSIZE_SHIFT) != height)
					return NULL;
			}

			return MakeTexture(pRes, pbContent + pXpr->dwHeaderSize, cbPixels);
		}
	}

	HRESULT hr;
	LPDIRECT3DTEXTURE8 pTex;
	do
	{
		pTex = NULL;
		hr = D3DXCreateTextureFromFileInMemoryEx(TheseusGetD3DDev(), pbContent, cbContent, D3DX_DEFAULT, D3DX_DEFAULT, 1, 0,
			D3DFMT_UNKNOWN, D3DPOOL_MANAGED, D3DX_DEFAULT, D3DX_DEFAULT, 0, NULL, NULL, &pTex);
		if (hr == D3D_OK)
			return pTex;
	}
	while (hr == E_OUTOFMEMORY && NewFailed(cbContent) != 0);

	return NULL;
#else
	// Desktop: XPR textures decode in software (xbx_texture.h), common
	// image files through stb_image, anything else is refused.
	const char* pszExt = strrchr(szURL, '.');
	if (pszExt == NULL)
		return NULL;
	pszExt++;

	if (_stricmp(pszExt, "xbx") == 0 || _stricmp(pszExt, "xt") == 0)
	{
		LPDIRECT3DTEXTURE8 pTex = XBX_ParseTexture(pbContent, cbContent);
		if (pTex == NULL)
			fprintf(stderr, "[XBX] ParseTexture failed for '%s'\n", szURL);
		return pTex;
	}

	if (_stricmp(pszExt, "jpg") == 0 || _stricmp(pszExt, "jpeg") == 0 || _stricmp(pszExt, "png") == 0 || _stricmp(pszExt, "bmp") == 0)
	{
		int w = 0, h = 0, channels = 0;
		unsigned char* pixels = stbi_load_from_memory(pbContent, cbContent, &w, &h, &channels, 4);
		if (pixels == NULL)
			return NULL;
		LPDIRECT3DTEXTURE8 pTex = NULL;
		if (w > 0 && h > 0)
		{
			pTex = new IDirect3DTexture8();
			pTex->CreateFromRGBA((UINT)w, (UINT)h, pixels);
		}
		stbi_image_free(pixels);
		return pTex;
	}

	return NULL;
#endif
}

// Skin folder path for one candidate name: the skin dir with the name on the end.
static void MakeSkinPath(TCHAR* szOut, const char* szName)
{
	_tcscpy(szOut, TheseusGetSkinDir());
	int cch = (int)_tcslen(szOut);
	Unicode(szOut + cch, szName, MAX_PATH - cch);
}

// The file name part of a path, as ANSI, for SkinCandidatesFor.
static void BaseName(char* szOut, const TCHAR* szPath)
{
	const TCHAR* pszBase = szPath;
	for (const TCHAR* p = szPath; *p; p++)
		if (*p == '/' || *p == '\\')
			pszBase = p + 1;
	Ansi(szOut, pszBase, MAX_PATH);
}

// Theseus: a loose image file fetched from disk. Xbox opens the common
// formats with D3DX by name; desktop decodes the bytes already fetched, so the
// virtual game icon redirect still applies.
static LPDIRECT3DTEXTURE8 DecodeLooseImage(const TCHAR* szURL, CActiveFile& file, UINT width, UINT height)
{
	const TCHAR* pszExt = _tcsrchr(szURL, '.');
#ifdef _XBOX
	static const TCHAR* const c_rgszD3DX[] = { _T(".tga"), _T(".bmp"), _T(".dds") };
#else
	static const TCHAR* const c_rgszD3DX[] = { ".tga", ".bmp", ".dds", ".jpg", ".png", ".jpeg" };
#endif
	for (int i = 0; pszExt != NULL && i < countof(c_rgszD3DX); i++)
	{
		if (_tcsicmp(pszExt, c_rgszD3DX[i]) != 0)
			continue;

		LPDIRECT3DTEXTURE8 pTex = NULL;
#ifdef _XBOX
		char szAnsi[MAX_PATH];
		Ansi(szAnsi, szURL, MAX_PATH);
		if (FAILED(D3DXCreateTextureFromFileA(TheseusGetD3DDev(), szAnsi, &pTex)))
			return NULL;
#else
		if (FAILED(D3DXCreateTextureFromFileInMemory(TheseusGetD3DDev(), file.GetContent(), file.GetContentLength(), &pTex)))
			return NULL;
#endif
		return pTex;
	}

	return ParseTexture(szURL, file.GetContent(), file.GetContentLength(), width, height);
}

// LoadTexture(url, width, height)
// Reconstructed from 5960 retail XBE @ 0x00060c90. Ghidra decompile:
//     MakeAbsoluteURL(abs, url);
//     ext = wcsrchr(abs, '.'); if (!ext) return NULL;
//     wcscpy(ext + 1, L"xbx"); return FindObjectInXIP(abs, TEXTURE);
// 5960 only looks in the XIPs. The skin folder in front and the loose file
// after are Theseus's.
LPDIRECT3DTEXTURE8 LoadTexture(const TCHAR* szURL, UINT width, UINT height)
{
	TCHAR szPath[MAX_PATH];
	MakeAbsoluteURL(szPath, szURL);

	CActiveFile file;

	// Theseus: a skin's texture wins over the XIP, under any .xbx name in its
	// equivalence group so UI.X-era skins still cover retail requests. Desktop
	// only probes the files a skin can replace.
	if (TheseusGetSkinDir())
	{
		TCHAR szWanted[MAX_PATH];
		_tcscpy(szWanted, szURL);
		TCHAR* pszDot = _tcsrchr(szWanted, '.');
		if (pszDot != NULL)
			_tcscpy(pszDot + 1, _T("xbx"));

		char szName[MAX_PATH];
		BaseName(szName, szWanted);
#ifndef _XBOX
		if (IsSkinnableAsset(szName))
#endif
		{
			const char* rgszNames[4];
			int cNames = SkinCandidatesFor(szName, rgszNames, 4);
			for (int i = 0; i < cNames; i++)
			{
				TCHAR szSkinPath[MAX_PATH];
				MakeSkinPath(szSkinPath, rgszNames[i]);
				if (!file.Fetch(szSkinPath, true))
					continue;

				LPDIRECT3DTEXTURE8 pTex = ParseTexture(szSkinPath, file.GetContent(), file.GetContentLength(), width, height);
				if (pTex != NULL)
					return pTex;
			}
		}
	}

	TCHAR* pszExt = _tcsrchr(szPath, '.');
	if (pszExt != NULL)
	{
		_tcscpy(pszExt + 1, _T("xbx"));
		LPDIRECT3DTEXTURE8 pTex = (LPDIRECT3DTEXTURE8)FindObjectInXIP(szPath, szURL, XIP_TYPE_TEXTURE);
		if (pTex != NULL)
			return pTex;

#ifndef _XBOX
		// Desktop: the XIP holds one name per group, so shell.xbx finds the
		// cellwall.xbx that's really in there.
		char szName[MAX_PATH];
		BaseName(szName, szPath);
		int cchDir = (int)_tcslen(szPath) - (int)strlen(szName);

		const char* rgszNames[4];
		int cNames = SkinCandidatesFor(szName, rgszNames, 4);
		for (int i = 1; i < cNames; i++)
		{
			TCHAR szAlias[MAX_PATH];
			snprintf(szAlias, sizeof(szAlias), "%.*s%s", cchDir, szPath, rgszNames[i]);
			pTex = (LPDIRECT3DTEXTURE8)FindObjectInXIP(szAlias, szURL, XIP_TYPE_TEXTURE);
			if (pTex != NULL)
				return pTex;
		}
#endif
	}

	// Theseus: the file itself, with its own extension.
	if (file.Fetch(szURL, true))
	{
		LPDIRECT3DTEXTURE8 pTex = DecodeLooseImage(szURL, file, width, height);
		if (pTex != NULL)
			return pTex;
	}

	// No final fallback. Returning menu_hilight.xbx for any missing texture
	// poisoned mesh UVs across the dashboard (e.g. skin.xm wrapped with an
	// unresolved background.bmp came out as a giant smear of the menu-hilight
	// pill). Better to fail loudly so missing assets are obvious during skin
	// authoring.
	ALERT(_T("Unable to load texture file (%s)"), szURL);
	return NULL;
}


// ===== Skin texture preload (Theseus) =======================================

LPDIRECT3DTEXTURE8 MyLoadImage(const TCHAR *szURL);

struct IMGCACHE
{
	TCHAR *m_url;
	LPDIRECT3DTEXTURE8 m_hImage;
	SIZE m_size;
	XTIME m_usage;
};

static IMGCACHE imageCache[100];
static IMGCACHE *pImgLock;

IMGCACHE *FindImage(const TCHAR *szURL)
{
	IMGCACHE *pFreeOne = NULL;
	IMGCACHE *pOldOne = NULL;
	int i;
	for (i = 0; i < countof(imageCache); i += 1)
	{
		if (imageCache[i].m_url != NULL && _tcsicmp(imageCache[i].m_url, szURL) == 0)
		{
			imageCache[i].m_usage = TheseusGetNow();
			return &imageCache[i];
		}

		if (pFreeOne != NULL)
			continue;

		if (imageCache[i].m_url == NULL)
			pFreeOne = &imageCache[i];
		else if (pOldOne == NULL || pOldOne->m_usage > imageCache[i].m_usage)
			pOldOne = &imageCache[i];
	}

	if (pFreeOne == NULL && pOldOne != NULL)
	{
		delete[] pOldOne->m_url;

		if (pOldOne->m_hImage != NULL)
			pOldOne->m_hImage->Release();

		ZeroMemory(pOldOne, sizeof(IMGCACHE));
		pFreeOne = pOldOne;
	}

	pImgLock = pFreeOne;

	pFreeOne->m_url = new TCHAR[_tcslen(szURL) + 1];
	_tcscpy(pFreeOne->m_url, szURL);

	pFreeOne->m_usage = TheseusGetNow();

	pFreeOne->m_hImage = MyLoadImage(szURL);

	if (pFreeOne->m_hImage != NULL)
	{
		D3DSURFACE_DESC sd;
		VERIFYHR(pFreeOne->m_hImage->GetLevelDesc(0, &sd));
		pFreeOne->m_size.cx = (int)sd.Width;
		pFreeOne->m_size.cy = (int)sd.Height;
	}

	pImgLock = NULL;

	return pFreeOne;
}

LPDIRECT3DTEXTURE8 MyLoadImage(const TCHAR *szURL) { return LoadTexture(szURL, 0, 0); }

#ifdef _XBOX
void PreloadSkinTextures()
{
	OutputDebugStringA("[Preload] Entered PreloadSkinTextures()\n");

	if (!TheseusGetSkinDir() || TheseusGetSkinDir()[0] == '\0')
	{
		OutputDebugStringA("[Preload] m_sSkinDir is null or empty\n");
		return;
	}

	// Convert m_sSkinDir to ANSI (char*) string
	char ansiSkinDir[MAX_PATH] = {0};
	WideCharToMultiByte(CP_ACP, 0, TheseusGetSkinDir(), -1, ansiSkinDir, MAX_PATH, NULL, NULL);

	OutputDebugStringA("[Preload] m_sSkinDir = ");
	OutputDebugStringA(ansiSkinDir);
	OutputDebugStringA("\n");

	// Extract folder name from path
	const char* skinDirName = strrchr(ansiSkinDir, '\\');
	if (!skinDirName)
	{
		OutputDebugStringA("[Preload] Failed to extract folder name from m_sSkinDir\n");
		return;
	}
	skinDirName++; // skip past '\'

	char skinConfigName[MAX_PATH];
	_snprintf(skinConfigName, MAX_PATH, "%s.xbx", skinDirName);

	char searchPath[MAX_PATH];
	_snprintf(searchPath, MAX_PATH, "%s*.xbx", ansiSkinDir);

	OutputDebugStringA("[Preload] searchPath = ");
	OutputDebugStringA(searchPath);
	OutputDebugStringA("\n");

	WIN32_FIND_DATAA findData;
	HANDLE hFind = TheseusFindFirstFile(searchPath, &findData);
	if (hFind == INVALID_HANDLE_VALUE)
	{
		OutputDebugStringA("[Preload] No .xbx files found in skin directory.\n");
		return;
	}

	do
	{
		OutputDebugStringA("[Preload] findData.cFileName = ");
		OutputDebugStringA(findData.cFileName);
		OutputDebugStringA("\n");

		if (_stricmp(findData.cFileName, skinConfigName) == 0)
			continue;

		char fullPath[MAX_PATH];
		if (ansiSkinDir[strlen(ansiSkinDir) - 1] == '\\')
			_snprintf(fullPath, MAX_PATH, "%s%s", ansiSkinDir, findData.cFileName);
		else
			_snprintf(fullPath, MAX_PATH, "%s\\%s", ansiSkinDir, findData.cFileName);

		OutputDebugStringA("[Preload] Loading: ");
		OutputDebugStringA(fullPath);
		OutputDebugStringA("\n");

		// Convert just the filename to wide and pass it to FindImage
		wchar_t wideFileName[MAX_PATH];
		MultiByteToWideChar(CP_ACP, 0, findData.cFileName, -1, wideFileName, MAX_PATH);
		FindImage(wideFileName);

	} while (TheseusFindNextFile(hFind, &findData));

	TheseusFindClose(hFind);
	OutputDebugStringA("[Preload] Skin texture preload complete.\n");
}
#else
// Desktop: the skin folder goes through the path translator and the host
// folder scan.
void PreloadSkinTextures()
{

	if (!g_sSkinDir || g_sSkinDir[0] == '\0')
	{
		return;
	}

	char ansiSkinDir[MAX_PATH] = {0};
	Ansi(ansiSkinDir, g_sSkinDir, MAX_PATH);

	// The skin menu saves the name with a capital first letter, so on Linux
	// the folder only turns up through the case blind translator.
	char mappedDir[MAX_PATH];
	if (!XboxPath_Translate(ansiSkinDir, mappedDir, sizeof(mappedDir)))
		mappedDir[0] = '\0';


	// Extract skin folder name to skip the config .xbx file
	const char* skinDirName = strrchr(ansiSkinDir, '\\');
	if (!skinDirName) skinDirName = strrchr(ansiSkinDir, '/');
	if (skinDirName) skinDirName++;
	// Handle trailing slash; back up one more
	if (skinDirName && *skinDirName == '\0') {
		const char* end = skinDirName - 2; // before the slash
		while (end > ansiSkinDir && *end != '\\' && *end != '/') end--;
		if (*end == '\\' || *end == '/') skinDirName = end + 1;
	}
	char skinConfigName[MAX_PATH] = {};
	if (skinDirName) {
		// Config file is "skinname.xbx"; extract just the folder name part
		const char* nameEnd = skinDirName;
		while (*nameEnd && *nameEnd != '\\' && *nameEnd != '/') nameEnd++;
		snprintf(skinConfigName, MAX_PATH, "%.*s.xbx", (int)(nameEnd - skinDirName), skinDirName);
	}

	auto ProcessXbxFile = [&](const char* fileName) {
		const char* dot = strrchr(fileName, '.');
		if (!dot || _stricmp(dot, ".xbx") != 0) return;
		if (skinConfigName[0] && _stricmp(fileName, skinConfigName) == 0) return;


		char wideFileName[MAX_PATH];
		for (int i = 0; i < MAX_PATH - 1 && fileName[i]; i++)
			wideFileName[i] = (char)fileName[i];
		wideFileName[strlen(fileName)] = 0;
		FindImage(wideFileName);
	};

	HostDir* dir = Host_OpenDir(mappedDir);
	if (!dir) {
		OutputDebugStringA("[Preload] Could not open skin directory\n");
		return;
	}
	char name[MAX_PATH];
	while (Host_ReadDir(dir, name, sizeof(name), NULL)) ProcessXbxFile(name);
	Host_CloseDir(dir);
}
#endif

// ===== Meshes ===============================================================

// CMesh: 0x24 bytes, vtable 0x00029d60 (Render, GetFVF, deleting destructor).
// Not a node. Fields: faces 0x04, vertices 0x08, indices 0x0c, primitive
// type 0x10, FVF 0x14, stride 0x18, vertex buffer 0x1c, index buffer 0x20.
// The constructor is inlined at each new: everything zero.
CMesh::CMesh()
{
	m_nFaceCount = 0;
	m_nVertexCount = 0;
	m_nIndexCount = 0;
	m_primitiveType = (D3DPRIMITIVETYPE)0;
	m_fvf = 0;
	m_nVertexStride = 0;
	m_vertexBuffer = NULL;
	m_indexBuffer = NULL;
}

// Destructor, reconstructed from 5960 retail XBE @ 0x0005bd00:
//     if (vb) Release(vb); if (ib) Release(ib);
CMesh::~CMesh()
{
	if (m_vertexBuffer != NULL)
		m_vertexBuffer->Release();
	if (m_indexBuffer != NULL)
		m_indexBuffer->Release();
}

// CMesh::GetFVF()   [vtable slot 1, @ 0x0005bda0: return this->fvf]
DWORD CMesh::GetFVF() const
{
	return m_fvf;
}

// CMesh::Create(file)
// Reconstructed from 5960 retail XBE @ 0x0005bdb0. Ghidra decompile:
//     ReadFile(file, &hdr, 0x18);
//     if (hdr.type == 4) primType = TRIANGLELIST; else if (hdr.type == 5) primType = TRIANGLESTRIP;
//     else return 0;
//     faces, fvf, stride, vertices, indices = hdr...;
//     vb = new vertex buffer (vertices * stride); lock it; ReadFile(file, vb data, vertices * stride);
//     ib = new index buffer (indices * 2); ReadFile(file, ib data, indices * 2);
//     return 1;
// The .xm types are the PC D3D numbers (4 list, 5 strip). None of the reads
// are checked.
bool CMesh::Create(HANDLE hFile)
{
	MESHFILEHEADER hdr;
	DWORD cbRead;
	ReadFile(hFile, &hdr, sizeof(hdr), &cbRead, NULL);

	if (hdr.dwPrimitiveType == 4)
		m_primitiveType = D3DPT_TRIANGLELIST;
	else if (hdr.dwPrimitiveType == 5)
		m_primitiveType = D3DPT_TRIANGLESTRIP;
	else
	{
#ifdef _XBOX
		return false;
#else
		m_primitiveType = (D3DPRIMITIVETYPE)hdr.dwPrimitiveType;	// desktop takes the file's word for it
#endif
	}

	m_nFaceCount = hdr.dwFaceCount;
	m_fvf = hdr.dwFVF;
	m_nVertexStride = hdr.dwVertexStride;
	m_nVertexCount = hdr.dwVertexCount;
	m_nIndexCount = hdr.dwIndexCount;

	int cbVertices = m_nVertexCount * m_nVertexStride;
	int cbIndices = m_nIndexCount * sizeof(WORD);

	BYTE* pb;
	TheseusCreateVertexBuffer(cbVertices, D3DUSAGE_DYNAMIC, m_fvf, D3DPOOL_DEFAULT, &m_vertexBuffer);
#ifdef _XBOX
	m_vertexBuffer->Lock(0, 0, &pb, D3DLOCK_DISCARD | D3DLOCK_NOFLUSH);
#else
	m_vertexBuffer->Lock(0, 0, &pb, D3DLOCK_DISCARD);
#endif
	ReadFile(hFile, pb, cbVertices, &cbRead, NULL);
	m_vertexBuffer->Unlock();

	TheseusCreateIndexBuffer(cbIndices, D3DUSAGE_DYNAMIC, D3DFMT_INDEX16, D3DPOOL_DEFAULT, &m_indexBuffer);
	m_indexBuffer->Lock(0, cbIndices, &pb, D3DLOCK_DISCARD);
	ReadFile(hFile, pb, cbIndices, &cbRead, NULL);
	m_indexBuffer->Unlock();

	return true;
}

// CMesh::Load(path)
// 5960 has this inlined in LoadMesh (0x0005c320):
//     file = CreateFile(path, GENERIC_READ, 0, 0, OPEN_EXISTING, SEQUENTIAL_SCAN, 0);
//     if (file != INVALID) { Create(file); CloseHandle(file); }
// Theseus tries the skin folder first, with the Q:\Xips\ or Q:\ dropped.
bool CMesh::Load(const TCHAR* szFilePath)
{
	HANDLE hFile = INVALID_HANDLE_VALUE;

	if (TheseusGetSkinDir())
	{
		const TCHAR* pszRelative = szFilePath;
		if (_tcsnicmp(szFilePath, _T("Q:/Xips/"), 8) == 0 || _tcsnicmp(szFilePath, _T("Q:\\Xips\\"), 8) == 0)
			pszRelative += 8;
		else if (_tcsnicmp(szFilePath, _T("Q:/"), 3) == 0 || _tcsnicmp(szFilePath, _T("Q:\\"), 3) == 0)
			pszRelative += 3;

		TCHAR szSkinPath[MAX_PATH];
		_stprintf(szSkinPath, _T("%s%s"), TheseusGetSkinDir(), pszRelative);
		hFile = TheseusCreateFile(szSkinPath, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	}

	if (hFile == INVALID_HANDLE_VALUE)
		hFile = TheseusCreateFile(szFilePath, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
		return false;

	bool bOK = Create(hFile);
	CloseHandle(hFile);
	return bOK;
}

// CMesh::Render(bSetFVF)   [vtable slot 0]
// Reconstructed from 5960 retail XBE @ 0x0005bea0. Disassembly, summarized:
//     if (!vb || !ib) return;
//     if (bSetFVF) SetVertexShader(GetFixedFunctionShader(fvf));
//     if (faces > 800 && !g_bEdgeAntialiasOverride) { EDGEANTIALIAS = 0; MULTISAMPLEANTIALIAS = 1; }
//     if (g_uMesh == 0) swap the device view for the camera's own, unwobbled;
//     SetStreamSource(0, vb, stride); SetIndices(ib, 0);
//     DrawIndexedVertices(primType, the index count for faces, indices);
//     if (g_uMesh == 0) put the view back; g_uMesh++;
// Theseus doesn't swap the view; its camera writes the device view every
// frame. The edge AA swap is Xbox only; the draw and vertex counters are Theseus's.
void CMesh::Render(bool bSetFVF)
{
	if (m_vertexBuffer == NULL || m_indexBuffer == NULL)
		return;

	if (bSetFVF)
		TheseusSetVertexShader(GetFixedFunctionShader(m_fvf));

#ifdef _XBOX
	if (m_nFaceCount > 800 && !g_bEdgeAntialiasOverride)
	{
		TheseusSetRenderState(D3DRS_EDGEANTIALIAS, FALSE);
		TheseusSetRenderState(D3DRS_MULTISAMPLEANTIALIAS, TRUE);
	}
#endif

	TheseusSetStreamSource(0, m_vertexBuffer, m_nVertexStride);
	TheseusSetIndices(m_indexBuffer, 0);
	Frame_Count(FC_DRAW_SCENE);
	TheseusDrawIndexedPrimitive(m_primitiveType, 0, m_nVertexCount, 0, m_nFaceCount);

#ifndef _XBOX
	g_nVertPerFrame += m_nVertexCount;
	g_nTriPerFrame += m_nFaceCount;
#endif
}

// LoadMesh(path)
// Reconstructed from 5960 retail XBE @ 0x0005c320:
//     mesh = new CMesh; open path; if it opened, mesh->Create(file); return mesh;
// The mesh comes back even when the file didn't load; callers test its FVF.
CMesh* LoadMesh(const TCHAR* szFilePath)
{
	CMesh* pMesh = new CMesh;
	pMesh->Load(szFilePath);
	return pMesh;
}

// MakeSphere(radius, slices, stacks)
// Reconstructed from 5960 retail XBE @ 0x0005c390. Ghidra decompile:
//     mesh = new CMesh; primType = TRIANGLELIST; fvf = XYZ|NORMAL|DIFFUSE; stride = 0x1c;
//     if (D3DXCreateSphere(dev, r, slices, stacks, &sphere, 0) < 0
//         || sphere->CloneMeshFVF(MANAGED, fvf, dev, &clone) < 0 (sphere released)
//         || D3DXComputeNormals(clone) < 0
//         || clone->GetVertexBuffer(&vb) < 0 || clone->GetIndexBuffer(&ib) < 0)
//         { Release(vb); Release(ib); delete mesh; mesh = NULL; }
//     else { indices = faces * 3; faces = clone faces; vertices = clone vertices; }
//     Release(clone);
//     packed = new vertex buffer (vertices * 0x14); lock both;
//     for each vertex: xyz, CompressNormal(normal), diffuse;
//     Release(vb); vb = packed; fvf = XYZ|NORMPACKED3|DIFFUSE; stride = 0x14;
// The sphere node draws from this. Its normals go down to one packed DWORD.
CMesh* MakeSphere(float nRadius, int nSlices, int nStacks)
{
	CMesh* pMesh = new CMesh;
	pMesh->m_primitiveType = D3DPT_TRIANGLELIST;
	pMesh->m_fvf = D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE;
	pMesh->m_nVertexStride = sizeof(D3DXVECTOR3) * 2 + sizeof(DWORD);

	LPD3DXMESH pSphere = NULL;
	LPD3DXMESH pClone = NULL;
	HRESULT hr = D3DXCreateSphere(TheseusGetD3DDev(), nRadius, nSlices, nStacks, &pSphere, NULL);
	if (SUCCEEDED(hr))
	{
		hr = pSphere->CloneMeshFVF(D3DXMESH_MANAGED, pMesh->m_fvf, TheseusGetD3DDev(), &pClone);
		pSphere->Release();
	}
	if (SUCCEEDED(hr))
		hr = D3DXComputeNormals(pClone);
	if (SUCCEEDED(hr))
		hr = pClone->GetVertexBuffer(&pMesh->m_vertexBuffer);
	if (SUCCEEDED(hr))
		hr = pClone->GetIndexBuffer(&pMesh->m_indexBuffer);

	if (SUCCEEDED(hr))
	{
		pMesh->m_nFaceCount = pClone->GetNumFaces();
		pMesh->m_nIndexCount = pMesh->m_nFaceCount * 3;
		pMesh->m_nVertexCount = pClone->GetNumVertices();
	}

	if (pClone != NULL)
		pClone->Release();

	// Theseus: a failed sphere comes back NULL. 5960 released the buffers,
	// deleted the mesh (releasing them again) and then repacked through NULL.
	if (FAILED(hr))
	{
		delete pMesh;
		return NULL;
	}

	const int cbPacked = sizeof(D3DXVECTOR3) + sizeof(DWORD) * 2;
	LPDIRECT3DVERTEXBUFFER8 pPacked;
	TheseusCreateVertexBuffer(pMesh->m_nVertexCount * cbPacked, D3DUSAGE_DYNAMIC, 0, D3DPOOL_MANAGED, &pPacked);

	BYTE* pbFrom;
	BYTE* pbTo;
	pMesh->m_vertexBuffer->Lock(0, 0, &pbFrom, 0);
	pPacked->Lock(0, 0, &pbTo, 0);
	for (int i = 0; i < pMesh->m_nVertexCount; i++)
	{
		const float* pf = (const float*)pbFrom;
		DWORD* pdw = (DWORD*)pbTo;
		CopyMemory(pbTo, pf, sizeof(D3DXVECTOR3));
		pdw[3] = CompressNormal((float*)(pf + 3));
		pdw[4] = ((const DWORD*)pf)[6];
		pbFrom += pMesh->m_nVertexStride;
		pbTo += cbPacked;
	}
	pPacked->Unlock();
	pMesh->m_vertexBuffer->Unlock();

	pMesh->m_vertexBuffer->Release();
	pMesh->m_vertexBuffer = pPacked;
	pMesh->m_fvf = D3DFVF_XYZ | D3DFVF_NORMPACKED3 | D3DFVF_DIFFUSE;
	pMesh->m_nVertexStride = cbPacked;
	return pMesh;
}


// ===== Mesh node ============================================================

class CMeshNode* g_pRenderMeshNode = NULL;	// 0x0017e758
CMeshNode* CMeshNode::c_pFirst;			// 0x0017c8d4

// Theseus: drops every node's mesh and marks it dirty, so the next Render
// loads it again through whichever skin is active. A skin switch calls this.
// XIP meshes belong to their archive and are only let go of.
void FlushMeshCache()
{
	for (CMeshNode* pNode = CMeshNode::c_pFirst; pNode != NULL; pNode = pNode->m_next)
	{
		if (pNode->m_mesh != NULL)
		{
			if (pNode->m_ownMesh)
				delete pNode->m_mesh;
			pNode->m_mesh = NULL;
			pNode->m_ownMesh = true;
		}
		pNode->m_dirty = true;
	}
}

// CMeshNode: 0x40 bytes, factory 0x0005c050, vtable 0x00029d70. url at 0x1c,
// dirty 0x20, render time 0x28, mesh 0x30, ownMesh 0x34, next 0x38.
IMPLEMENT_NODE("Mesh", CMeshNode, CNode)

START_NODE_PROPS(CMeshNode, CNode)
	NODE_PROP(pt_string, CMeshNode, url)
#ifndef _XBOX
	NODE_PROP(pt_number, CMeshNode, falloff)	// Theseus, desktop only
#endif
END_NODE_PROPS()

#define _FND_CLASS CMeshNode
START_NODE_FUN(CMeshNode, CNode)
	NODE_FUN_VS(load)
END_NODE_FUN()
#undef _FND_CLASS

// Constructor, the factory @ 0x0005c050:
//     url = NULL; dirty = 1; renderTime = 0; mesh = NULL; ownMesh = 1;
//     next = first; first = this;
// m_falloff is Theseus's.
CMeshNode::CMeshNode() : m_url(NULL), m_falloff(0.0f)
{
	m_dirty = true;
	m_renderTime = 0.0;
	m_mesh = NULL;
	m_ownMesh = true;

	m_next = c_pFirst;
	c_pFirst = this;
}

// Destructor, reconstructed from 5960 retail XBE @ 0x0005c0e0:
//     if (ownMesh) delete mesh; free(url); unlink this; node destructor;
CMeshNode::~CMeshNode()
{
	if (m_ownMesh)
		delete m_mesh;
	delete [] m_url;

	CMeshNode** pp = &c_pFirst;
	while (*pp != this)
		pp = &(*pp)->m_next;
	*pp = m_next;
}

// CMeshNode::Init()   [vtable slot 23, @ 0x0005c1f0: this->dirty = 0]
void CMeshNode::Init()
{
	m_dirty = false;
}

// Inlined in 5960's Render below; Theseus's shape renderer calls it too.
bool CMeshNode::Initialize()
{
	if (m_url != NULL && m_url[0] != 0)
		load(m_url);
	Init();
	return m_mesh != NULL;
}

// Inlined at 5960's material setups, which ask the bound mesh for its FVF.
DWORD CMeshNode::GetFVF()
{
	return m_mesh != NULL ? m_mesh->GetFVF() : 0;
}

extern void SetFalloffShaderValues(const D3DXCOLOR& sideColor, const D3DXCOLOR& frontColor);
extern DWORD GetEffectShader(int nEffect, DWORD fvf);

// CMeshNode::Render()   [vtable slot 16]
// Reconstructed from 5960 retail XBE @ 0x0005c200. Ghidra decompile:
//     renderTime = now;
//     if (dirty) { if (url && *url) load(url); Init(); if (!mesh) return; }
//     if (mesh) mesh->Render(g_pRenderMeshNode != this);
// The shape renderer sets the FVF itself for the node it's drawing, so the
// mesh only sets it when drawn some other way. The falloff branch is Theseus's.
void CMeshNode::Render()
{
	m_renderTime = TheseusGetNow();

	if (m_dirty && !Initialize())
		return;
	if (m_mesh == NULL)
		return;

	if (m_falloff > 0.0f && g_pRenderMeshNode != this)
	{
		// Theseus: "Mesh { url "foo.xm" falloff 1 }" in alpha era XAPs draws
		// the view angle fade the retail dash does with a MaxMaterial.
		TheseusSetVertexShader(GetEffectShader(1, m_mesh->GetFVF()));

		TheseusSetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_DISABLE);
		TheseusSetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		TheseusSetRenderState(D3DRS_TEXTUREFACTOR, D3DCOLOR_RGBA(255, 255, 255, 0));

		TheseusSetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
		TheseusSetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		TheseusSetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
		TheseusSetRenderState(D3DRS_ZWRITEENABLE, FALSE);

		SetFalloffShaderValues(D3DXCOLOR(0.25f, 0.80f, 0.15f, 0.35f), D3DXCOLOR(0.05f, 0.20f, 0.03f, 0.00f));
		m_mesh->Render(false);

		TheseusSetRenderState(D3DRS_ZWRITEENABLE, TRUE);
		return;
	}

	m_mesh->Render(g_pRenderMeshNode != this);
}

// CMeshNode::load(file)   [script fn 'load', FND 0x00188418, sig 12]
// Reconstructed from 5960 retail XBE @ 0x0005c250. Ghidra decompile:
//     MakeAbsoluteURL(abs, file);
//     mesh = FindObjectInXIP(abs, any type);
//     if (mesh) { ownMesh = 0; return; }
//     ownMesh = 1; mesh = LoadMesh(abs);
//     if (!mesh->GetFVF()) { delete mesh; MakePath(abs, appDir, file); mesh = LoadMesh(abs); }
// An XIP mesh belongs to its archive; a loose one is the node's to free.
// Theseus looks in the skin folder first.
void CMeshNode::load(const TCHAR* szFile)
{
	TCHAR szPath[MAX_PATH];
	MakeAbsoluteURL(szPath, szFile);
	m_mesh = NULL;

	// Theseus: a skin's mesh wins, under any name in its equivalence group,
	// so a skin shipping only Inner_cell-FACES.xm covers cellwall.xm.
	if (TheseusGetSkinDir() && TheseusGetSkinDir()[0])
	{
		char szName[MAX_PATH];
		BaseName(szName, szFile);

		const char* rgszNames[4];
		int cNames = SkinCandidatesFor(szName, rgszNames, 4);
		for (int i = 0; i < cNames; i++)
		{
			TCHAR szSkinPath[MAX_PATH];
			MakeSkinPath(szSkinPath, rgszNames[i]);
			CMesh* pSkinMesh = new CMesh;
			if (pSkinMesh->Load(szSkinPath))
			{
				m_mesh = pSkinMesh;
				m_ownMesh = true;
#ifdef _XBOX
				TRACE(_T("\002Loaded %s from skin override\n"), szSkinPath);
#endif
				return;
			}
			delete pSkinMesh;
		}
	}

	m_mesh = (CMeshCore*)FindObjectInXIP(szPath, szFile);
	if (m_mesh != NULL)
	{
		m_ownMesh = false;
		return;
	}

	m_ownMesh = true;
	CMesh* pMesh = LoadMesh(szPath);
	if (pMesh->GetFVF() == 0)
	{
		delete pMesh;
		MakePath(szPath, TheseusGetAppDir(), szFile);
		pMesh = LoadMesh(szPath);
	}
	m_mesh = pMesh;
}
