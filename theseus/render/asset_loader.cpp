// asset_loader.cpp: mesh, texture, and image loaders. Reads MESHFILEHEADER
// blobs, XPR-wrapped textures, and free-standing image files out of XIP
// archives or the loose skin / extracted directories. Decompiled from
// the 5960 retail XBE.

#include "std.h"
#include "theseus.h"
#include "node.h"
#include "shape_render.h"
#include "runner.h"
#include "asset_loader.h"
#include "activefile.h"
#include "xip_archive.h"
#include "file_util.h"
#include "camera.h"

// =========================================================================
// Texture: Base texture classes, cache, and background loader
// =========================================================================

extern "C" void WINAPI D3D_FreeNoncontiguousMemory(void *pMemory);

bool DecodeRAW(const TCHAR *szFileName, CTexture *pTexture);

// Allocate a managed-pool D3D texture at the requested size and format.
// nWidth / nHeight are in/out parameters because D3DXCreateTexture rounds
// up to the nearest supported size and the caller wants the rounded
// dimensions back.
LPDIRECT3DTEXTURE8 CreateTexture(int& nWidth, int& nHeight, D3DFORMAT format)
{
	LPDIRECT3DTEXTURE8 pTexture;
	if (FAILED(D3DXCreateTexture(TheseusGetD3DDev(), nWidth, nHeight, 1, 0, format, D3DPOOL_MANAGED, &pTexture)))
		return NULL;

	TheseusGetTextureSize(pTexture, nWidth, nHeight);
	return pTexture;
}



IMPLEMENT_NODE("Texture", CTexture, CNode)

START_NODE_PROPS(CTexture, CNode)
	NODE_PROP(pt_boolean, CTexture, repeatS)
	NODE_PROP(pt_boolean, CTexture, repeatT)
	NODE_PROP(pt_boolean, CTexture, titleImage)
END_NODE_PROPS()

CTexture::CTexture() :
	m_repeatS(true),
	m_repeatT(true),
	m_titleImage(false)
{
	m_surface = NULL;
	m_format = D3DFMT_A8R8G8B8;
}

CTexture::~CTexture()
{
	if (m_surface != NULL)
		m_surface->Release();
}

LPDIRECT3DTEXTURE8 CTexture::GetTextureSurface() { return m_surface; }

bool CTexture::Create(int nWidth, int nHeight)
{
	m_nImageWidth = nWidth;
	m_nImageHeight = nHeight;
	m_surface = ::CreateTexture(nWidth, nHeight, m_format);
	if (m_surface == NULL)
	{
		TRACE(_T("\001CreateTexture(%d,%d) failed!\n"), nWidth, nHeight);
		return false;
	}

	return true;
}



IMPLEMENT_NODE("ImageTexture", CImageTexture, CTexture)

START_NODE_PROPS(CImageTexture, CTexture)
	NODE_PROP(pt_string, CImageTexture, url)
	NODE_PROP(pt_boolean, CImageTexture, alpha)
	NODE_PROP(pt_boolean, CImageTexture, fromfile)
END_NODE_PROPS()

CImageTexture* CImageTexture::c_pFirstImageTexture = NULL;

CImageTexture::CImageTexture() :
	m_url(NULL),
	m_alpha(false),
	m_fromfile(false)
{
	m_dirty = true;
	m_surface = NULL;

	m_nextImageTexture = c_pFirstImageTexture;
	c_pFirstImageTexture = this;
}

CImageTexture::~CImageTexture()
{
	for (CImageTexture** pp = &c_pFirstImageTexture; *pp != NULL; pp = &(*pp)->m_nextImageTexture)
	{
		if (*pp == this)
		{
			*pp = m_nextImageTexture;
			break;
		}
	}

	delete [] m_url;
}

LPDIRECT3DTEXTURE8 CImageTexture::GetTextureSurface()
{
	if (m_dirty && m_url != NULL)
	{
		m_dirty = false;
		Load(m_url);
	}

	return CTexture::GetTextureSurface();
}

class CBackgroundLoader
{
public:
	CBackgroundLoader();
	virtual ~CBackgroundLoader();

	bool Fetch(const TCHAR* szURL);

	virtual void OnComplete();

	static CBackgroundLoader* c_pFirstLoader;
	CBackgroundLoader* m_nextLoader;

	HANDLE m_file;
    OVERLAPPED m_overlapped;
	BYTE* m_content;
	int m_contentSize;
	TCHAR* m_url;
};

CBackgroundLoader::CBackgroundLoader()
{
	ZeroMemory(&m_overlapped, sizeof (m_overlapped));

	m_url = NULL;
	m_file = INVALID_HANDLE_VALUE;
	m_content = NULL;
	m_contentSize = 0;

	// Stick this one at the end of the list...
	m_nextLoader = NULL;
	CBackgroundLoader** ppLoader;
	for (ppLoader = &c_pFirstLoader; *ppLoader != NULL; ppLoader = &(*ppLoader)->m_nextLoader)
		;
	*ppLoader = this;
}

CBackgroundLoader::~CBackgroundLoader()
{
	for (CBackgroundLoader** ppLoader = &c_pFirstLoader; *ppLoader != NULL; ppLoader = &(*ppLoader)->m_nextLoader)
	{
		if (*ppLoader == this)
		{
			*ppLoader = m_nextLoader;
			break;
		}
	}

	if (m_file != INVALID_HANDLE_VALUE)
		CloseHandle(m_file);

	delete [] m_url;
	delete [] m_content;
}

CBackgroundLoader* CBackgroundLoader::c_pFirstLoader;

bool CBackgroundLoader::Fetch(const TCHAR* szURL)
{
	ASSERT(m_url == NULL);

	TCHAR szBuf [MAX_PATH];
	MakeAbsoluteURL(szBuf, szURL);

	m_url = new TCHAR [_tcslen(szBuf) + 1];
	_tcscpy(m_url, szBuf);

	DWORD dwFlags = 0;
#ifdef _XBOX
	// Overlapped needs FILE_FLAG_NO_BUFFERING for the hard drive, but can't have it on memory units...
	if ((szURL[0] == 'c' || szURL[0] == 'C' || szURL[0] == 'y' || szURL[0] == 'Y'|| szURL[0] == 'e' || szURL[0] == 'E'|| szURL[0] == 'f' || szURL[0] == 'F'|| szURL[0] == 'g' || szURL[0] == 'G'|| szURL[0] == 'r' || szURL[0] == 'R'|| szURL[0] == 's' || szURL[0] == 'S') && szURL[1] == ':')
		dwFlags = FILE_FLAG_NO_BUFFERING;
#endif
	m_file = TheseusCreateFile(szBuf, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED | FILE_FLAG_SEQUENTIAL_SCAN | dwFlags, NULL);
	if (m_file == INVALID_HANDLE_VALUE)
	{
		TRACE(_T("\001CBackgroundLoader::Fetch(%s) failed %d\n"), szURL, GetLastError());
		return false;
	}

	m_contentSize = GetFileSize(m_file, NULL);
#ifdef _XBOX
	// Round up to next 512 bytes (sector) or overlapped will fail on xbox...
	m_contentSize = (m_contentSize + 511) & ~511;
#endif
	m_content = new BYTE [m_contentSize];

	ZeroMemory(&m_overlapped, sizeof (m_overlapped));

	if (!ReadFile(m_file, m_content, m_contentSize, NULL, &m_overlapped))
	{
		DWORD dwError = GetLastError();
		if (dwError != ERROR_IO_PENDING)
		{
			TRACE(_T("\001CBackgroundLoader::Fetch ReadFile (%s) failed %d\n"), m_url, dwError);
			return false;
		}
	}
	else
	{
		OnComplete();
		delete this;
	}

	return true;
}

void CBackgroundLoader::OnComplete()
{
	// override this to find out when the load is done
}

void BackgroundLoader_Frame()
{
	CBackgroundLoader* pNextLoader;
	for (CBackgroundLoader* pLoader = CBackgroundLoader::c_pFirstLoader; pLoader != NULL; pLoader = pNextLoader)
	{
		pNextLoader = pLoader->m_nextLoader; // in case pLoader is deleted and removed from the list...

		ASSERT(pLoader->m_file != INVALID_HANDLE_VALUE);

		if (HasOverlappedIoCompleted(&pLoader->m_overlapped))
		{
			pLoader->OnComplete();
			delete pLoader;
		}
	}
}

struct TXTCACHE
{
	TCHAR* m_url;
	LPDIRECT3DTEXTURE8 m_texture;
	XTIME m_usage;
	bool m_loading;
	XTIME m_timeLoaded;
};

class CBackgroundTexture : public CBackgroundLoader
{
public:
	CBackgroundTexture(TXTCACHE* pTxt, UINT width=0, UINT height=0);
	~CBackgroundTexture();

	virtual void OnComplete();
	TXTCACHE* m_textureCache;

private:
    UINT m_width, m_height;
};

CBackgroundTexture::CBackgroundTexture(TXTCACHE* pTxt, UINT width, UINT height)
{
	ASSERT(pTxt != NULL);
	m_textureCache = pTxt;
    m_width = width;
    m_height = height;
}

CBackgroundTexture::~CBackgroundTexture()
{
	if (m_textureCache != NULL)
		m_textureCache->m_loading = false;
}
//Milenko Check
void CBackgroundTexture::OnComplete()
{
	m_textureCache->m_texture = ParseTexture(m_url, m_content, m_contentSize, m_width, m_height);
	if((m_textureCache->m_texture == NULL) && (m_width == 64))
	{
		m_textureCache->m_texture = LoadTexture(_T("xboxlogo64.xbx"), 64, 64);
    }
	else if((m_textureCache->m_texture == NULL) && (m_width == 128))
	{
		m_textureCache->m_texture = LoadTexture(_T("xboxlogo128.xbx"), 128, 128);
	}
	m_textureCache->m_loading = false;
	m_textureCache->m_timeLoaded = TheseusGetNow();
}

static TXTCACHE TextureCache [100];
static TXTCACHE* pTxtLock;

bool CleanupTextureCache()
{
	TRACE(_T("Looking for a texture to free...\n"));

	TXTCACHE* pOldTxt = NULL;
	int i;
	for (i = 0; i < countof(TextureCache); i += 1)
	{
		TXTCACHE* pTxt = &TextureCache[i];
		if (pTxt->m_url == NULL || pTxt == pTxtLock || pTxt->m_loading)
			continue;

		if (pOldTxt == NULL || pOldTxt->m_usage > pTxt->m_usage)
			pOldTxt = pTxt;
	}

	if (pOldTxt == NULL)
	{
		TRACE(_T("    none left!\n"));
		return false;
	}

	// Free this one up...

	TRACE(_T("    freeing %s\n"), pOldTxt->m_url);

	delete [] pOldTxt->m_url;

#ifdef _XBOX
	if (pOldTxt->m_texture != NULL)
		pOldTxt->m_texture->Release();
#endif

	ZeroMemory(pOldTxt, sizeof (TXTCACHE));

	return true;
}

void FlushTextureCache()
{
	int i;
	for (i = 0; i < countof(TextureCache); i += 1)
	{
		TXTCACHE* pTxt = &TextureCache[i];
		if (pTxt->m_url == NULL || pTxt->m_loading)
			continue;

		delete [] pTxt->m_url;

#ifdef _XBOX
		if (pTxt->m_texture != NULL)
			pTxt->m_texture->Release();
#endif

		ZeroMemory(pTxt, sizeof (TXTCACHE));
	}

	// Dirty all live CImageTexture nodes so they reload on next render
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

TXTCACHE* FindTexture(const TCHAR* szURL, bool bAsync, UINT width, UINT height, bool binXIP=false)
{
	TXTCACHE* pFreeOne = NULL;
	TXTCACHE* pOldOne = NULL;

	for (int i = 0; i < countof(TextureCache); i += 1)
	{
		if (TextureCache[i].m_url != NULL && _tcsicmp(TextureCache[i].m_url, szURL) == 0)
		{
			TextureCache[i].m_usage = TheseusGetNow();
			return &TextureCache[i];
		}

		if (pFreeOne != NULL)
			continue;

		if (TextureCache[i].m_loading)
			continue;

		if (TextureCache[i].m_url == NULL)
			pFreeOne = &TextureCache[i];
		else if (pOldOne == NULL || pOldOne->m_usage > TextureCache[i].m_usage)
			pOldOne = &TextureCache[i];
	}

	if (pFreeOne == NULL && pOldOne != NULL)
	{
		delete [] pOldOne->m_url;

#ifdef _XBOX
		if (pOldOne->m_texture != NULL)
			pOldOne->m_texture->Release();
#endif

		ZeroMemory(pOldOne, sizeof (TXTCACHE));
		pFreeOne = pOldOne;
	}

	ASSERT(pFreeOne != NULL);

	ASSERT(pTxtLock == NULL);
	pTxtLock = pFreeOne;

	pFreeOne->m_url = new TCHAR [_tcslen(szURL) + 1];
	_tcscpy(pFreeOne->m_url, szURL);

	pFreeOne->m_usage = TheseusGetNow();

	if (bAsync && !binXIP)
	{
		pFreeOne->m_loading = true;
		CBackgroundTexture* pLoader = new CBackgroundTexture(pFreeOne, width, height);
		if (!pLoader->Fetch(szURL))
			delete pLoader;
	}
	else
	{
		pFreeOne->m_texture = LoadTexture(szURL, width, height);
		pFreeOne->m_timeLoaded = TheseusGetNow();
	}

	pTxtLock = NULL;

	return pFreeOne;
}

LPDIRECT3DTEXTURE8 GetTexture(const TCHAR* szURL, XTIME* pTimeLoaded, UINT width, UINT height, bool binXIP = false)
{
	TXTCACHE* pTxt = FindTexture(szURL, true, width, height, binXIP);
	ASSERT(pTxt != NULL);
	if (pTxt->m_texture == NULL)
		return NULL;

	if (pTimeLoaded != NULL)
		*pTimeLoaded = pTxt->m_timeLoaded;

	return pTxt->m_texture;
}



extern bool CreateTextureFromFile(const TCHAR* szFileName, CTexture* pTexture);

void CImageTexture::Load(const TCHAR* szURL)
{
	if (m_surface != NULL)
	{
		m_surface->Release();
		m_surface = NULL;
	}

	TXTCACHE* pTxt = FindTexture(szURL, false, 0, 0);
	ASSERT(pTxt != NULL);
	if (pTxt->m_texture == NULL)
		return;

	m_surface = pTxt->m_texture;
	m_surface->AddRef();

	TheseusGetTextureSize(m_surface, m_nImageWidth, m_nImageHeight);
}

bool CImageTexture::OnSetProperty(const PRD* pprd, const void* pvValue)
{
	if (PTR2INT(pprd->pbOffset) == offsetof(m_url))
		m_dirty = true;

	return true;
}

// =========================================================================
// Image: Texture parsing, image cache, XPR loader, and drawing primitives
// =========================================================================

/*
TODO:
	Load images from arbitrary URL
	Perform load/decode in background thread
	Deal with incremental display (as an option)
	Deal with animated GIF's
	Handle the same image formats for both BITMAP and SURFACE cases
	Optimize case where CreateTexture actually returns the right size/format
*/

class CImage
{
public:
	CImage();
	~CImage();

	int m_nWidth;
	int m_nHeight;
	BYTE *m_pels;
	int m_nPitch;
};

CImage::CImage() { m_pels = NULL; m_nWidth = 0; m_nHeight = 0; }
CImage::~CImage() { }



LPDIRECT3DTEXTURE8 ParseTexture(const TCHAR *szURL, const BYTE *pbContent, int cbContent, UINT width, UINT height)
{
#ifdef _XBOX
	const TCHAR *pch = _tcsrchr(szURL, '.');
	if (pch != NULL)
	{
		pch += 1;
		if (_tcsicmp(pch, _T("xt")) == 0)
		{
			IDirect3DTexture8 *pTexture = (IDirect3DTexture8 *)TheseusD3D_AllocNoncontiguousMemory(sizeof(D3DBaseTexture));
			CopyMemory(pTexture, pbContent, sizeof(IDirect3DTexture8));

			int cbData = cbContent - sizeof(IDirect3DTexture8);

			BYTE *pbData = (BYTE *)TheseusD3D_AllocContiguousMemory(cbData, D3DTEXTURE_ALIGNMENT);
			CopyMemory(pbData, pbContent + sizeof(IDirect3DTexture8), cbData);
			D3D_CopyContiguousMemoryToVideo(pbData);

			pTexture->Data = NULL;
			pTexture->Register(pbData);
			pTexture->Common |= D3DCOMMON_D3DCREATED;

			return pTexture;
		}
		else if (_tcsicmp(pch, _T("xbx")) == 0)
		{
			const XPR_HEADER *pxprh = (const XPR_HEADER *)pbContent;
			if (pxprh->dwMagic == XPR_MAGIC_VALUE)
			{
				int cbHeaders = pxprh->dwHeaderSize - sizeof(XPR_HEADER);
				int cbData = pxprh->dwTotalSize - pxprh->dwHeaderSize;

				// Validate the XPR header before we touch the payload.
				// All four checks must pass: enough room for the texture
				// resource header, the resource has to be a texture, the
				// data section has to be non-empty and within bounds, and
				// the texture dimensions (decoded from the format dword)
				// must match the caller's expected size if one was given.
				if (cbHeaders < sizeof(IDirect3DTexture8))
				{
					TRACE(_T("Invalid XBX image file (wrong header size; is %d should be %d)!\n"), cbHeaders, sizeof(IDirect3DTexture8));
					return NULL;
				}

				D3DResource *pResource = (D3DResource *)(pbContent + sizeof(XPR_HEADER));
				if ((pResource->Common & D3DCOMMON_TYPE_MASK) != D3DCOMMON_TYPE_TEXTURE)
				{
					TRACE(_T("Invalid XBX image file (not a texture)!\n"));
					return NULL;
				}

				if (cbData <= 0 || (UINT)cbContent < pxprh->dwHeaderSize + cbData)
				{
					TRACE(_T("Invalid XBX image file! (wrong data size)\n"));
					return NULL;
				}

				if (width != 0 && height != 0)
				{
					DWORD dwInfo = *((DWORD *)pbContent + 6);
					const DWORD exptbl[] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 0, 0, 0, 0, 0, 0};

					DWORD dwU = exptbl[(dwInfo & D3DFORMAT_USIZE_MASK) >> D3DFORMAT_USIZE_SHIFT];
					DWORD dwV = exptbl[(dwInfo & D3DFORMAT_VSIZE_MASK) >> D3DFORMAT_VSIZE_SHIFT];

					if (dwU != width || dwV != height)
					{
						TRACE(_T("\001Invalid XBX image size! (not %dx%d)\n"), width, height);
						return NULL;
					}
				}

				IDirect3DTexture8 *pTexture = (IDirect3DTexture8 *)TheseusD3D_AllocNoncontiguousMemory(sizeof(D3DBaseTexture));
				if (pTexture == NULL)
				{
					TRACE(_T("Not enough memory to load XBX image file!\n"));
					return NULL;
				}

				CopyMemory(pTexture, pbContent + sizeof(XPR_HEADER), sizeof(IDirect3DTexture8));

				BYTE *pbData = (BYTE *)TheseusD3D_AllocContiguousMemory(cbData, D3DTEXTURE_ALIGNMENT);
				if (pbData == NULL)
				{
					D3D_FreeNoncontiguousMemory(pTexture);
					TRACE(_T("Not enough memory to load XBX image file!\n"));
					return NULL;
				}

				CopyMemory(pbData, pbContent + pxprh->dwHeaderSize, cbData);
				D3D_CopyContiguousMemoryToVideo(pbData);

				pTexture->Data = NULL;
				pTexture->Register(pbData);
				pTexture->Common |= D3DCOMMON_D3DCREATED;

				return pTexture;
			}
		}
	}

	TRACE(_T("\002Texture '%s' needs to be converted to an Xbox friendly format!\n"), szURL);
#endif

	for (;;)
	{
		LPDIRECT3DTEXTURE8 lpTexture = NULL;
		HRESULT hr = D3DXCreateTextureFromFileInMemoryEx(TheseusGetD3DDev(), pbContent, cbContent, D3DX_DEFAULT, D3DX_DEFAULT, 1, 0, D3DFMT_UNKNOWN, D3DPOOL_MANAGED, D3DX_DEFAULT, D3DX_DEFAULT, 0, NULL, NULL, &lpTexture);
		if (hr == D3D_OK)
			return lpTexture;

		if (hr != E_OUTOFMEMORY || NewFailed(cbContent) == 0)
			break;
	}

	return NULL;
}

LPDIRECT3DTEXTURE8 LoadTexture(const TCHAR *szURL, UINT width, UINT height)
{
	bool bInXip = false;
	TCHAR szBuf[MAX_PATH];
	MakeAbsoluteURL(szBuf, szURL);

	// === XIP lookup using forced .xbx extension ===
	TCHAR *pch = _tcsrchr(szBuf, '.');
	if (pch != NULL)
	{
		_tcscpy(pch + 1, _T("xbx"));
		LPDIRECT3DTEXTURE8 lpTexture = (LPDIRECT3DTEXTURE8)FindObjectInXIP(szBuf, szURL, XIP_TYPE_TEXTURE);
		if (lpTexture != NULL)
			return lpTexture;
	}

	CActiveFile file;

	// === Try override from skin directory using .xbx ===
	if (TheseusGetSkinDir())
	{
		TCHAR SkinPath[MAX_PATH];
		TCHAR OverrideName[MAX_PATH];
		_tcscpy(OverrideName, szURL);

		TCHAR *ext = _tcsrchr(OverrideName, '.');
		if (ext != NULL)
			_tcscpy(ext + 1, _T("xbx")); // force .xbx extension for override

		_stprintf(SkinPath, _T("%s%s"), TheseusGetSkinDir(), OverrideName);

		if (file.Fetch(SkinPath, true))
		{
			TCHAR *skinExt = _tcsrchr(SkinPath, '.');
			if (skinExt && (_tcsicmp(skinExt, _T(".tga")) == 0 || _tcsicmp(skinExt, _T(".bmp")) == 0 || _tcsicmp(skinExt, _T(".dds")) == 0))
			{
				// Use D3DXCreateTextureFromFileA for standard formats
				char ansiPath[MAX_PATH];
				Ansi(ansiPath, SkinPath, MAX_PATH);
				LPDIRECT3DTEXTURE8 lpTexture = NULL;
				if (SUCCEEDED(D3DXCreateTextureFromFileA(TheseusGetD3DDev(), ansiPath, &lpTexture)))
				{
					OutputDebugString(_T("[Texture] Loaded standard image from skin override: "));
					OutputDebugString(SkinPath);
					OutputDebugString(_T("\n"));
					ALERT(_T("Texture loaded: (%s)"), szURL);
					return lpTexture;
				}
			}
			else
			{
				// Use ParseTexture for .xbx or custom formats
				LPDIRECT3DTEXTURE8 lpTexture = ParseTexture(SkinPath, file.GetContent(), file.GetContentLength(), width, height);
				if (lpTexture != NULL)
				{
					OutputDebugString(_T("[Texture] Loaded from skin override: "));
					OutputDebugString(SkinPath);
					OutputDebugString(_T("\n"));
					ALERT(_T("Texture loaded: (%s)"), szURL);
					return lpTexture;
				}
			}
		}
	}

	// === Fallback: Try original path with original extension ===
	if (file.Fetch(szURL, true))
	{
		TCHAR *ext = _tcsrchr(szURL, '.');
		if (ext && (_tcsicmp(ext, _T(".tga")) == 0 || _tcsicmp(ext, _T(".bmp")) == 0 || _tcsicmp(ext, _T(".dds")) == 0))
		{
			// D3DX for standard formats
			char ansiPath[MAX_PATH];
			Ansi(ansiPath, szURL, MAX_PATH);
			LPDIRECT3DTEXTURE8 lpTexture = NULL;
			if (SUCCEEDED(D3DXCreateTextureFromFileA(TheseusGetD3DDev(), ansiPath, &lpTexture)))
				return lpTexture;
		}
		else
		{
			LPDIRECT3DTEXTURE8 lpTexture = ParseTexture(szURL, file.GetContent(), file.GetContentLength(), width, height);
			if (lpTexture != NULL)
				return lpTexture;
		}
	}

	// === FINAL fallback: Load "menu_hilite.xbx" from skin directory ===
	if (TheseusGetSkinDir())
	{
		OutputDebugString(_T("[Texture] Attempting final fallback: menu_hilite.xbx\n"));

		TCHAR FallbackPath[MAX_PATH];
		_stprintf(FallbackPath, _T("%smenu_hilight.xbx"), TheseusGetSkinDir());

		if (file.Fetch(FallbackPath, true))
		{
			LPDIRECT3DTEXTURE8 lpTexture = ParseTexture(FallbackPath, file.GetContent(), file.GetContentLength(), width, height);
			if (lpTexture != NULL)
			{
				ALERT(_T("Fallback texture loaded: menu_hilite.xbx"));
				return lpTexture;
			}
		}
	}

	ALERT(_T("Unable to load texture file (%s) and fallback failed"), szURL);
	return NULL;
}

// Load a texture from a standalone XPR / XBX file on disk.
//
// XPR is the Xbox Resource format the .xbx asset files use: a small
// header (XPR_HEADER) followed by one or more D3DResource header
// records, followed by the contiguous payload bytes those resources
// point at. The dashboard's regular texture path goes through
// ParseTexture() against an in-memory buffer; this function exists
// for the few sites that need to read straight from a file handle
// without staging into memory first.
LPDIRECT3DTEXTURE8 LoadTextureFromXPR(const char *xprfile)
{
	if (xprfile == NULL)
		return NULL;

	HANDLE hFile = CreateFile(xprfile, GENERIC_READ, FILE_SHARE_READ, NULL,
							  OPEN_EXISTING, 0, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		OutputDebugStringA("Could not open texture ");
		OutputDebugStringA(xprfile);
		OutputDebugStringA(" for reading.\n");
		return NULL;
	}

	XPR_HEADER xprh;
	DWORD cb;
	BYTE *headers = NULL;
	BYTE *texData = NULL;

	if (!ReadFile(hFile, &xprh, sizeof(XPR_HEADER), &cb, NULL) ||
	    xprh.dwMagic != XPR_MAGIC_VALUE)
	{
		goto fail;
	}

	// Resource headers come immediately after XPR_HEADER. Subtract the
	// three DWORDs that XPR_HEADER stores past the trailing magic word
	// to get the actual header section size.
	{
		DWORD headersSize = xprh.dwHeaderSize - 3 * sizeof(DWORD);
		headers = new BYTE[headersSize];
		if (!headers)
			goto fail;
		if (!ReadFile(hFile, headers, headersSize, &cb, NULL))
			goto fail;

		// Texture payload bytes go into contiguous video-accessible
		// memory so the GPU can sample directly out of them.
		DWORD texDataSize = xprh.dwTotalSize - xprh.dwHeaderSize;
		texData = (BYTE *)D3D_AllocContiguousMemory(texDataSize, D3DTEXTURE_ALIGNMENT);
		if (!texData)
			goto fail;

		SetFilePointer(hFile, xprh.dwHeaderSize, NULL, FILE_BEGIN);
		if (!ReadFile(hFile, texData, texDataSize, &cb, NULL))
			goto fail;

		// First resource record in the header section determines what
		// kind of D3D object this XPR contains. We only honor textures
		// and vertex buffers; index buffers exist as a recognized type
		// but D3D won't accept Register() on them.
		DWORD type = *((DWORD *)headers) & D3DCOMMON_TYPE_MASK;
		if (type != D3DCOMMON_TYPE_VERTEXBUFFER &&
		    type != D3DCOMMON_TYPE_TEXTURE &&
		    type != D3DCOMMON_TYPE_INDEXBUFFER)
		{
			goto fail;
		}

		LPDIRECT3DRESOURCE8 ppResource = (LPDIRECT3DRESOURCE8)headers;
		if (type != D3DCOMMON_TYPE_INDEXBUFFER)
			ppResource->Register(texData);

		CloseHandle(hFile);
		return (LPDIRECT3DTEXTURE8)ppResource;
	}

fail:
	OutputDebugStringA("Could not read ");
	OutputDebugStringA(xprfile);
	OutputDebugStringA("\n");
	delete [] headers;
	if (hFile != INVALID_HANDLE_VALUE)
		CloseHandle(hFile);
	return NULL;
}



HIMAGE MyLoadImage(const TCHAR *szURL);

struct IMGCACHE
{
	TCHAR *m_url;
	HIMAGE m_hImage;
	SIZE m_size;
	XTIME m_usage;
};

static IMGCACHE imageCache[100];
static IMGCACHE *pImgLock;

void CleanupImageCache()
{
	int i;
	for (i = 0; i < countof(imageCache); i += 1)
	{
		IMGCACHE *pImg = &imageCache[i];
		if (pImg == pImgLock)
			continue;

		delete[] pImg->m_url;

#ifdef _XBOX
		if (pImg->m_hImage != NULL)
			pImg->m_hImage->Release();
#endif

		ZeroMemory(pImg, sizeof(IMGCACHE));
	}
}

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
		TRACE(_T("Unloading %s from image cache\n"), pOldOne->m_url);
		delete[] pOldOne->m_url;

#ifdef _XBOX
		if (pOldOne->m_hImage != NULL)
			pOldOne->m_hImage->Release();
#endif

		ZeroMemory(pOldOne, sizeof(IMGCACHE));
		pFreeOne = pOldOne;
	}

	ASSERT(pFreeOne != NULL);

	TRACE(_T("Loading %s into image cache...\n"), szURL);

	ASSERT(pImgLock == NULL);
	pImgLock = pFreeOne;

	pFreeOne->m_url = new TCHAR[_tcslen(szURL) + 1];
	_tcscpy(pFreeOne->m_url, szURL);

	pFreeOne->m_usage = TheseusGetNow();

	pFreeOne->m_hImage = MyLoadImage(szURL);

	if (pFreeOne->m_hImage != NULL)
	{
#ifdef _XBOX
		D3DSURFACE_DESC sd;
		VERIFYHR(pFreeOne->m_hImage->GetLevelDesc(0, &sd));
		pFreeOne->m_size.cx = (int)sd.Width;
		pFreeOne->m_size.cy = (int)sd.Height;
#endif
	}

	pImgLock = NULL;

	return pFreeOne;
}

extern "C" // this one is used by the RenderHTML C-code...
	HIMAGE
	FetchImage(const TCHAR *szURL, SIZE *pSize)
{
	IMGCACHE *pImgCache = FindImage(szURL);
	if (pImgCache == NULL)
		return NULL;

	if (pSize != NULL)
		*pSize = pImgCache->m_size;

	return pImgCache->m_hImage;
}

HIMAGE MyLoadImage(const TCHAR *szURL) { return LoadTexture(szURL, 0, 0); }



bool DecodeRAW(const TCHAR *szFileName, CTexture *pTexture)
{
	FILE *fp = _tfopen(szFileName, _T("rb"));
	if (fp == NULL)
		return false;

	fseek(fp, 0, SEEK_END);
	DWORD dwFileSize = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	int nWidth = 0;
	int nHeight = 0;
	int nDepth = 0;

	if (dwFileSize == 1024 * 1024 * 3)
	{
		nWidth = 1024;
		nHeight = 1024;
		nDepth = 24;
	}

	if (nWidth == 0 || nHeight == 0 || nDepth == 0)
	{
		fclose(fp);
		return false;
	}

	if (!pTexture->Create(nWidth, nHeight))
	{
		TRACE(_T("Create texture failed!\n"));
		fclose(fp);
		return false;
	}

	D3DLOCKED_RECT lr;
	VERIFYHR(pTexture->m_surface->LockRect(0, &lr, NULL, D3DLOCK_DISCARD));
	void *pvPels = lr.pBits;
	int nPitch = lr.Pitch;

	BYTE *rgbsrc = new BYTE[nWidth * (nDepth / 8)];

	int y;
	for (y = 0; y < nHeight; y += 1)
	{
		fread(rgbsrc, 1, nWidth * (nDepth / 8), fp);

		BYTE *pbSrc = rgbsrc;
		BYTE *pbDest = (BYTE *)pvPels + y * nPitch;

		int x;
		for (x = 0; x < nWidth; x += 1)
		{
			pbDest[0] = pbSrc[2];
			pbDest[1] = pbSrc[1];
			pbDest[2] = pbSrc[0];
			pbDest[3] = 255;

			pbDest += 4;
			pbSrc += 3;
		}
	}

	delete[] rgbsrc;

	pTexture->m_surface->UnlockRect(0);

	fclose(fp);

	return true;
}



EXTERN_C BOOL GetImageSize(const TCHAR *szImgFile, SIZE *pSize)
{
	return FetchImage(szImgFile, pSize) != NULL;
}

EXTERN_C void DrawImage(HDRAW hDC, const TCHAR *szImgFile, int x, int y, int align, WORD *pcx, WORD *pcy)
{
	HIMAGE hImage;
	SIZE size;

	hImage = FetchImage(szImgFile, &size);

	if (hImage != NULL)
	{
		if ((align & TA_RIGHT) != 0)
			x -= size.cx;

		if ((align & TA_BOTTOM) != 0)
			y -= size.cy;

#ifdef _XBOX
		X_BitBlt(hDC, x, y, size.cx, size.cy, hImage, 0, 0);
#endif
	}
	else
	{
		size.cx = 20;
		size.cy = 20;
	}

	if (pcx != NULL)
		*pcx = (WORD)size.cx;

	if (pcy != NULL)
		*pcy = (WORD)size.cy;
}



static bool Clip(HDRAW hDraw /*LPDIRECT3DSURFACE8 pSurface*/, int &x, int &y, int &cx, int &cy)
{
	ASSERT(hDraw != NULL);

	if (x < 0)
	{
		cx += x;
		x = 0;
	}

	if (y < 0)
	{
		cy += y;
		y = 0;
	}

	if (x + cx > (int)hDraw->Desc /*desc*/.Width)
		cx = (int)hDraw->Desc /*desc*/.Width - x;

	if (y + cy > (int)hDraw->Desc /*desc*/.Height)
		cy = (int)hDraw->Desc /*desc*/.Height - y;

	if (cx <= 0 || cy <= 0)
		return false;

	return true;
}
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
	HANDLE hFind = FindFirstFileA(searchPath, &findData);
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

	} while (FindNextFileA(hFind, &findData));

	FindClose(hFind);
	OutputDebugStringA("[Preload] Skin texture preload complete.\n");
}



EXTERN_C HRESULT X_FillRect(HDRAW hDraw, int x, int y, int cx, int cy, D3DCOLOR color)
{
	ASSERT(hDraw != NULL);

#ifdef _DEBUG
	{
		ASSERT(hDraw->Desc /*desc*/.Format == D3DFMT_A8R8G8B8 || hDraw->Desc /*desc*/.Format == D3DFMT_X8R8G8B8);
	}
#endif

	if (!Clip(hDraw /*pSurface*/, x, y, cx, cy))
		return S_OK;

	for (int j = 0; j < cy; j += 1)
	{
		DWORD *ppel = (DWORD *)((BYTE *)hDraw->Lock /*lock*/.pBits + hDraw->Lock /*lock*/.Pitch * (y + j)) + x;
		for (int i = 0; i < cx; i += 1)
			*ppel++ = color;
	}

	return S_OK;
}

EXTERN_C HRESULT X_BitBlt(HDRAW hDraw, int x, int y, int cx, int cy, LPDIRECT3DTEXTURE8 pSrcSurface, int xSrc, int ySrc)
{
	ASSERT(hDraw != NULL);
	ASSERT(pSrcSurface != NULL);

#ifdef _DEBUG
	{
		D3DSURFACE_DESC descSrc;

		VERIFYHR(pSrcSurface->GetLevelDesc(0, &descSrc));

		ASSERT(hDraw->Desc.Format == D3DFMT_A8R8G8B8 || hDraw->Desc.Format == D3DFMT_X8R8G8B8);
		ASSERT(hDraw->Desc.Format == descSrc.Format);
	}
#endif

	// Clipping is against the destination only -- callers are
	// responsible for keeping (xSrc + cx) and (ySrc + cy) inside
	// the source surface.
	if (!Clip(hDraw, x, y, cx, cy))
		return S_OK;

	D3DLOCKED_RECT lockSrc;
	HRESULT hr = pSrcSurface->LockRect(0, &lockSrc, NULL, D3DLOCK_READONLY);
	if (FAILED(hr))
		return hr;

	for (int j = 0; j < cy; j += 1)
	{
		DWORD *ppel = (DWORD *)((BYTE *)hDraw->Lock.pBits + hDraw->Lock.Pitch * (y + j)) + x;
		DWORD *ppelSrc = (DWORD *)((BYTE *)lockSrc.pBits + lockSrc.Pitch * (ySrc + j)) + xSrc;

		for (int i = 0; i < cx; i += 1)
			*ppel++ = *ppelSrc++;
	}

	pSrcSurface->UnlockRect(0);

	return S_OK;
}

// =========================================================================
// Mesh: Mesh loading, rendering, and node system
// =========================================================================

extern CCamera theCamera;
extern UINT g_uMesh;
extern BOOL g_bEdgeAntialiasOverride;

CMesh::CMesh()
{
	m_vertexBuffer = NULL;
	m_indexBuffer = NULL;
	m_nVertexStride = 0;
	m_nFaceCount = 0;
	m_nVertexCount = 0;
	m_nIndexCount = 0;
	m_fvf = 0;
	m_primitiveType = (D3DPRIMITIVETYPE)0;
}

CMesh::~CMesh()
{
	{
		if (m_vertexBuffer != NULL)
			m_vertexBuffer->Release();

		if (m_indexBuffer != NULL)
			m_indexBuffer->Release();
	}
}

DWORD CMesh::GetFVF() const
{
	ASSERT(m_fvf != D3DFVF_RESERVED0);
	return m_fvf;
}

bool CMesh::Create(BYTE *pbData, DWORD dwData)
{
	MESHFILEHEADER *pHeader = (MESHFILEHEADER *)pbData;
	pbData += sizeof(MESHFILEHEADER);

#ifdef _XBOX
	switch (pHeader->dwPrimitiveType)
	{
	default:
		ASSERT(FALSE); // unsupported primitive type
		return false;

	case 4:
		m_primitiveType = D3DPT_TRIANGLELIST;
		break;

	case 5:
		m_primitiveType = D3DPT_TRIANGLESTRIP;
		break;
	}
#else
	m_primitiveType = (D3DPRIMITIVETYPE)pHeader->dwPrimitiveType;
#endif

	m_nFaceCount = pHeader->dwFaceCount;
	m_fvf = pHeader->dwFVF;
	m_nVertexStride = pHeader->dwVertexStride;
	m_nVertexCount = pHeader->dwVertexCount;
	m_nIndexCount = pHeader->dwIndexCount;

	TheseusCreateVertexBuffer(m_nVertexCount * m_nVertexStride, D3DUSAGE_DYNAMIC, m_fvf, D3DPOOL_DEFAULT, &m_vertexBuffer);

	BYTE *verts;
#ifdef _XBOX
	const DWORD dwLockFlags = D3DLOCK_DISCARD | D3DLOCK_NOFLUSH;
#else
	const DWORD dwLockFlags = D3DLOCK_DISCARD;
#endif
	VERIFYHR(m_vertexBuffer->Lock(0, 0, &verts, dwLockFlags));
	CopyMemory(verts, pbData, m_nVertexCount * m_nVertexStride);
	pbData += m_nVertexCount * m_nVertexStride;
	VERIFYHR(m_vertexBuffer->Unlock());

	TheseusCreateIndexBuffer(m_nIndexCount * sizeof(WORD), D3DUSAGE_DYNAMIC, D3DFMT_INDEX16, D3DPOOL_DEFAULT, &m_indexBuffer);

	BYTE *indices;
	VERIFYHR(m_indexBuffer->Lock(0, m_nIndexCount * sizeof(WORD), (BYTE **)&indices, dwLockFlags));
	CopyMemory(indices, pbData, m_nIndexCount * sizeof(WORD));
	VERIFYHR(m_indexBuffer->Unlock());

	return true;
}

bool CMesh::Create(HANDLE hFile)
{
	DWORD dwRead;
	MESHFILEHEADER header;

	VERIFY(ReadFile(hFile, &header, sizeof(header), &dwRead, NULL) && dwRead == sizeof(header));

#ifdef _XBOX
	switch (header.dwPrimitiveType)
	{
	default:
		ASSERT(FALSE); // unsupported primitive type
		return false;

	case 4:
		m_primitiveType = D3DPT_TRIANGLELIST;
		break;

	case 5:
		m_primitiveType = D3DPT_TRIANGLESTRIP;
		break;
	}
#else
	m_primitiveType = (D3DPRIMITIVETYPE)header.dwPrimitiveType;
#endif

	m_nFaceCount = header.dwFaceCount;
	m_fvf = header.dwFVF;
	ASSERT(m_fvf != D3DFVF_RESERVED0);
	m_nVertexStride = header.dwVertexStride;
	m_nVertexCount = header.dwVertexCount;
	m_nIndexCount = header.dwIndexCount;

	TheseusCreateVertexBuffer(m_nVertexCount * m_nVertexStride, D3DUSAGE_DYNAMIC, m_fvf, D3DPOOL_DEFAULT, &m_vertexBuffer);

	BYTE *verts;
#ifdef _XBOX
	const DWORD dwLockFlags = D3DLOCK_DISCARD | D3DLOCK_NOFLUSH;
#else
	const DWORD dwLockFlags = D3DLOCK_DISCARD;
#endif
	VERIFYHR(m_vertexBuffer->Lock(0, 0, &verts, dwLockFlags));
	VERIFY(ReadFile(hFile, verts, m_nVertexCount * m_nVertexStride, &dwRead, NULL) && dwRead == (DWORD)(m_nVertexCount * m_nVertexStride));
	VERIFYHR(m_vertexBuffer->Unlock());

	TheseusCreateIndexBuffer(m_nIndexCount * sizeof(WORD), D3DUSAGE_DYNAMIC, D3DFMT_INDEX16, D3DPOOL_DEFAULT, &m_indexBuffer);

	BYTE *indices;
	VERIFYHR(m_indexBuffer->Lock(0, m_nIndexCount * sizeof(WORD), (BYTE **)&indices, D3DLOCK_DISCARD));
	VERIFY(ReadFile(hFile, indices, m_nIndexCount * sizeof(WORD), &dwRead, NULL) && dwRead == m_nIndexCount * sizeof(WORD));
	VERIFYHR(m_indexBuffer->Unlock());

	return true;
}

bool CMesh::Load(const TCHAR *szFilePath)
{
    HANDLE hFile = INVALID_HANDLE_VALUE;
    const TCHAR* relativePath = szFilePath;

    // Strip known absolute prefixes to get a relative path
    if (_tcsnicmp(szFilePath, _T("Q:/Xips/"), 8) == 0 || _tcsnicmp(szFilePath, _T("Q:\\Xips\\"), 8) == 0)
    {
        relativePath = szFilePath + 8;  // skip "Q:/Xips/"
    }
    else if (_tcsnicmp(szFilePath, _T("Q:/"), 3) == 0 || _tcsnicmp(szFilePath, _T("Q:\\"), 3) == 0)
    {
        relativePath = szFilePath + 3;  // generic fallback if someone did Q:\file directly
    }

    // === Attempt skin override ===
    if (TheseusGetSkinDir())
    {
        TCHAR SkinPath[MAX_PATH];
        _stprintf(SkinPath, _T("%s%s"), TheseusGetSkinDir(), relativePath);

        hFile = TheseusCreateFile(SkinPath, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
        if (hFile != INVALID_HANDLE_VALUE)
        {
            OutputDebugString(_T("[Mesh] Loaded from skin override: "));
            OutputDebugString(SkinPath);
            OutputDebugString(_T("\n"));
        }
    }

    // === Fallback to original path ===
    if (hFile == INVALID_HANDLE_VALUE)
    {
        hFile = TheseusCreateFile(szFilePath, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
        if (hFile == INVALID_HANDLE_VALUE)
        {
            TRACE(_T("\001[Mesh] Cannot load MeshNode: %s\n"), szFilePath);
            return false;
        }

        OutputDebugString(_T("[Mesh] Loaded from original path: "));
        OutputDebugString(szFilePath);
        OutputDebugString(_T("\n"));
    }

    bool b = Create(hFile);
    CloseHandle(hFile);
    return b;
}

void CMesh::Render(bool bSetFVF)
{
	if (m_vertexBuffer == NULL || m_indexBuffer == NULL)
		return;

	ASSERT(m_primitiveType != 0); // forget to set this?

	if (bSetFVF)
		TheseusSetVertexShader(GetFixedFunctionShader(m_fvf));

	if (m_nFaceCount > 800 && !g_bEdgeAntialiasOverride)
	{
		TheseusSetRenderState(D3DRS_EDGEANTIALIAS, FALSE);
		TheseusSetRenderState(D3DRS_MULTISAMPLEANTIALIAS, TRUE);
	}

	TheseusSetStreamSource(0, m_vertexBuffer, m_nVertexStride);
	TheseusSetIndices(m_indexBuffer, 0);
	extern int g_drawCallsThisFrame;
	extern int g_drawCallsSceneFrame;
	g_drawCallsThisFrame++;
	g_drawCallsSceneFrame++;
	TheseusDrawIndexedPrimitive(m_primitiveType, 0, m_nVertexCount, 0, m_nFaceCount);
}



class CMeshNode *g_pRenderMeshNode = NULL;

CMeshNode *CMeshNode::c_pFirst;

IMPLEMENT_NODE("Mesh", CMeshNode, CNode)

START_NODE_PROPS(CMeshNode, CNode)
NODE_PROP(pt_string, CMeshNode, url)
END_NODE_PROPS()

#define _FND_CLASS CMeshNode
START_NODE_FUN(CMeshNode, CNode)
NODE_FUN_VS(load)
END_NODE_FUN()
#undef _FND_CLASS

CMeshNode::CMeshNode() : m_url(NULL), m_falloff(0.0f)
{
	m_next = c_pFirst;
	c_pFirst = this;
	m_renderTime = 0.0f;

	m_dirty = true;
	m_mesh = NULL;
	m_ownMesh = true;
}

CMeshNode::~CMeshNode()
{
	if (m_ownMesh)
		delete m_mesh;

	delete[] m_url;

	CMeshNode **ppMeshNode;
	for (ppMeshNode = &c_pFirst; *ppMeshNode != this; ppMeshNode = &(*ppMeshNode)->m_next)
		ASSERT(*ppMeshNode != NULL);
	*ppMeshNode = m_next;
}

bool CMeshNode::Initialize()
{
	ASSERT(m_dirty);

	if (m_url != NULL && m_url[0] != 0)
		load(m_url);

	Init();

	return m_mesh != NULL;
}

void CMeshNode::Init() { m_dirty = false; }

extern void SetFalloffShaderValues(const D3DXCOLOR &sideColor, const D3DXCOLOR &frontColor);
extern DWORD GetEffectShader(int nEffect, DWORD fvf);

void CMeshNode::Render()
{
	m_renderTime = TheseusGetNow();

	if (m_dirty && !Initialize())
		return;

	if (m_mesh != NULL)
	{
		// Script-side falloff: alpha XAP scripts use "Mesh { url "foo.xm" falloff 1 }"
		// to trigger the viewing-angle transparency effect on individual meshes.
		// Retail dashboards handle this through MaxMaterial instead.
		if (m_falloff > 0.0f && g_pRenderMeshNode != this)
		{
			DWORD fvf = m_mesh->GetFVF();

			TheseusSetVertexShader(GetEffectShader(1, fvf));

			TheseusSetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_DISABLE);
			TheseusSetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
			TheseusSetRenderState(D3DRS_TEXTUREFACTOR, D3DCOLOR_RGBA(255, 255, 255, 0));

			TheseusSetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
			TheseusSetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
			TheseusSetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
			TheseusSetRenderState(D3DRS_ZWRITEENABLE, FALSE);

			SetFalloffShaderValues(
				D3DXCOLOR(0.25f, 0.80f, 0.15f, 0.35f),
				D3DXCOLOR(0.05f, 0.20f, 0.03f, 0.00f)
			);
			m_mesh->Render(false);

			TheseusSetRenderState(D3DRS_ZWRITEENABLE, TRUE);
		}
		else
		{
			// The bSetFVF flag is suppressed when shape_render has already
			// programmed the FVF for this node in the same render pass --
			// happens when the same mesh is drawn back-to-back at different
			// transforms inside one Group.
			m_mesh->Render(g_pRenderMeshNode != this);
		}
	}
}

void CMeshNode::load(const TCHAR *szFile)
{
	ASSERT(m_mesh == NULL);

	TCHAR szFilePath[MAX_PATH];
	MakeAbsoluteURL(szFilePath, szFile);

	m_mesh = (CMeshCore *)FindObjectInXIP(szFilePath, szFile);

	if (m_mesh == NULL)
	{
		m_ownMesh = true;
		CMesh *pMesh = LoadMesh(szFilePath);
		m_mesh = pMesh;
		if (pMesh->GetFVF() == 0)
		{
			delete pMesh;
			MakePath(szFilePath, TheseusGetAppDir(), szFile);
			pMesh = LoadMesh(szFilePath);
			m_mesh = pMesh;
			if (pMesh->GetFVF() == 0)
				return;
		}
		TRACE(_T("\002Loaded %s from file\n"), szFilePath);
	}
	else
	{
		m_ownMesh = false;
	}
}

DWORD CMeshNode::GetFVF()
{
	if (m_mesh == NULL)
		return 0;

	return m_mesh->GetFVF();
}



CMesh *LoadMesh(const TCHAR *szFilePath)
{
	CMesh *pMesh = new CMesh;
	pMesh->Load(szFilePath);
	return pMesh;
}

CMesh *CreateMesh(HANDLE hFile)
{
	CMesh *pMesh = new CMesh;
	pMesh->Create(hFile);
	return pMesh;
}

CMesh *CreateMesh(BYTE *pbContent, DWORD cbContent)
{
	CMesh *pMesh = new CMesh;
	pMesh->Create(pbContent, cbContent);
	return pMesh;
}

CMesh *MakeSphere(float nRadius, int nSlices, int nStacks)
{
	HRESULT hr;

	CMesh *pMesh = new CMesh;

	pMesh->m_primitiveType = D3DPT_TRIANGLELIST;

	pMesh->m_fvf = D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE;
	pMesh->m_nVertexStride = 3 * sizeof(float) + 3 * sizeof(float) + sizeof(DWORD);

	// Build the sphere via D3DX's tessellator and clone it into a
	// vertex buffer with the FVF this node renders against. Two-stage
	// because D3DXCreateSphere only emits position+normal; the clone
	// is what gives us the diffuse-color slot.
	LPD3DXMESH pSphere = NULL;
	LPD3DXMESH pClone = NULL;

	hr = D3DXCreateSphere(TheseusGetD3DDev(), nRadius, nSlices, nStacks, &pSphere, NULL);

	if (SUCCEEDED(hr))
	{
		ASSERT(pSphere);
		hr = pSphere->CloneMeshFVF(D3DXMESH_MANAGED, pMesh->m_fvf, TheseusGetD3DDev(), &pClone);
		pSphere->Release();
	}

	if (SUCCEEDED(hr))
	{
		ASSERT(pClone);
		hr = D3DXComputeNormals(pClone);
	}

	if (SUCCEEDED(hr))
	{
		hr = pClone->GetVertexBuffer(&pMesh->m_vertexBuffer);
	}

	if (SUCCEEDED(hr))
	{
		hr = pClone->GetIndexBuffer(&pMesh->m_indexBuffer);
	}

	if (SUCCEEDED(hr))
	{
		pMesh->m_nIndexCount = pClone->GetNumFaces() * 3;
		pMesh->m_nFaceCount = pClone->GetNumFaces();
		pMesh->m_nVertexCount = pClone->GetNumVertices();
	}
	else
	{
		if (pMesh->m_vertexBuffer)
		{
			pMesh->m_vertexBuffer->Release();
		}
		if (pMesh->m_indexBuffer)
		{
			pMesh->m_indexBuffer->Release();
		}
		delete pMesh;
		pMesh = NULL;
	}

	if (pClone)
	{
		pClone->Release();
	}

	// Compress the vertices
	LPDIRECT3DVERTEXBUFFER8 pCompressedVertexBuffer;
	BYTE *pSrc, *pDst;
	DWORD dwNormal;

	TheseusCreateVertexBuffer(pMesh->m_nVertexCount * (3 * sizeof(float) + 2 * sizeof(DWORD)), D3DUSAGE_DYNAMIC, 0, D3DPOOL_MANAGED, &pCompressedVertexBuffer);

	pMesh->m_vertexBuffer->Lock(0, 0, &pSrc, 0);
	pCompressedVertexBuffer->Lock(0, 0, &pDst, 0);

	for (int i = 0; i < pMesh->m_nVertexCount; i++)
	{
		memcpy(pDst, pSrc, 3 * sizeof(float));
		pSrc += 3 * sizeof(float);
		pDst += 3 * sizeof(float);
		dwNormal = CompressNormal((float *)pSrc);
		memcpy(pDst, &dwNormal, sizeof(DWORD));
		pSrc += 3 * sizeof(float);
		pDst += sizeof(DWORD);
		memcpy(pDst, pSrc, sizeof(DWORD));
		pSrc += sizeof(DWORD);
		pDst += sizeof(DWORD);
	}

	pCompressedVertexBuffer->Unlock();
	pMesh->m_vertexBuffer->Unlock();

	pMesh->m_vertexBuffer->Release();
	pMesh->m_vertexBuffer = pCompressedVertexBuffer;
	pMesh->m_fvf = D3DFVF_XYZ | D3DFVF_NORMPACKED3 | D3DFVF_DIFFUSE /*| D3DFVF_TEX1*/;
	pMesh->m_nVertexStride = 3 * sizeof(float) + sizeof(DWORD) + sizeof(DWORD) /*+ 2 * sizeof (float)*/;

	return pMesh;
}
