// texture.cpp: desktop CTexture wrapper. Loads .xbx packed textures
// (DXT1 / DXT3 / DXT5 / linear) and exposes them as GL texture
// objects to the dashboard's render pipeline.

#include "std.h"
#include "dashapp.h"
#include "node.h"
#include "runner.h"
#include "asset_loader.h"



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

LPDIRECT3DTEXTURE8 CTexture::GetTextureSurface()
{	
	return m_surface;
}

bool CTexture::Create(int nWidth, int nHeight)
{
	m_nImageWidth = nWidth;
	m_nImageHeight = nHeight;
	m_surface = ::CreateTexture(nWidth, nHeight, m_format);
	if (m_surface == NULL)
	{
		TRACE("\001CreateTexture(%d,%d) failed!\n", nWidth, nHeight);
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

	bool Fetch(const char* szURL);

	virtual void OnComplete();

	static CBackgroundLoader* c_pFirstLoader;
	CBackgroundLoader* m_pNextLoader;

	HANDLE m_hFile;
    OVERLAPPED m_overlapped;
	uint8_t* m_pbContent;
	int m_cbContent;
	char* m_szURL;
};

CBackgroundLoader::CBackgroundLoader()
{
	memset(&m_overlapped, 0, sizeof (m_overlapped));

	m_szURL = NULL;
	m_hFile = INVALID_HANDLE_VALUE;
	m_pbContent = NULL;
	m_cbContent = 0;

	// Stick this one at the end of the list...
	m_pNextLoader = NULL;
	CBackgroundLoader** ppLoader;
	for (ppLoader = &c_pFirstLoader; *ppLoader != NULL; ppLoader = &(*ppLoader)->m_pNextLoader)
		;
	*ppLoader = this;
}

CBackgroundLoader::~CBackgroundLoader()
{
	for (CBackgroundLoader** ppLoader = &c_pFirstLoader; *ppLoader != NULL; ppLoader = &(*ppLoader)->m_pNextLoader)
	{
		if (*ppLoader == this)
		{
			*ppLoader = m_pNextLoader;
			break;
		}
	}

	if (m_hFile != INVALID_HANDLE_VALUE)
		CloseHandle(m_hFile);

	delete [] m_szURL;
	delete [] m_pbContent;
}

CBackgroundLoader* CBackgroundLoader::c_pFirstLoader;

bool CBackgroundLoader::Fetch(const char* szURL)
{
	ASSERT(m_szURL == NULL);

	char szBuf [MAX_PATH];
	MakeAbsoluteURL(szBuf, szURL);

	m_szURL = new char [strlen(szBuf) + 1];
	strcpy(m_szURL, szBuf);

	uint32_t dwFlags = 0;
	m_hFile = TheseusCreateFile(szBuf, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED | FILE_FLAG_SEQUENTIAL_SCAN | dwFlags, NULL);
	if (m_hFile == INVALID_HANDLE_VALUE)
	{
		TRACE("\001CBackgroundLoader::Fetch(%s) failed %d\n", szURL, GetLastError());
		return false;
	}

	m_cbContent = GetFileSize(m_hFile, NULL);
	m_pbContent = new uint8_t [m_cbContent];

	memset(&m_overlapped, 0, sizeof (m_overlapped));

	if (!ReadFile(m_hFile, m_pbContent, m_cbContent, NULL, &m_overlapped))
	{
		uint32_t dwError = GetLastError();
		if (dwError != ERROR_IO_PENDING)
		{
			TRACE("\001CBackgroundLoader::Fetch ReadFile (%s) failed %d\n", m_szURL, dwError);
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
		pNextLoader = pLoader->m_pNextLoader; // in case pLoader is deleted and removed from the list...

		ASSERT(pLoader->m_hFile != INVALID_HANDLE_VALUE);

		if (HasOverlappedIoCompleted(&pLoader->m_overlapped))
		{
			pLoader->OnComplete();
			delete pLoader;
		}
	}
}

struct TXTCACHE
{
	char* m_szURL;
	LPDIRECT3DTEXTURE8 m_pTexture;
	XTIME m_usage;
	bool m_bLoading;
	XTIME m_timeLoaded;
};


class CBackgroundTexture : public CBackgroundLoader
{
public:
	CBackgroundTexture(TXTCACHE* pTxt, unsigned int width=0, unsigned int height=0);
	~CBackgroundTexture();

	virtual void OnComplete();
	TXTCACHE* m_pTxt;

private:
    unsigned int m_width, m_height;
};

CBackgroundTexture::CBackgroundTexture(TXTCACHE* pTxt, unsigned int width, unsigned int height)
{
	ASSERT(pTxt != NULL);
	m_pTxt = pTxt;
    m_width = width;
    m_height = height;
}

CBackgroundTexture::~CBackgroundTexture()
{
	if (m_pTxt != NULL)
		m_pTxt->m_bLoading = false;
}
// Texture format verification
void CBackgroundTexture::OnComplete()
{
	m_pTxt->m_pTexture = ParseTexture(m_szURL, m_pbContent, m_cbContent, m_width, m_height);
	if((m_pTxt->m_pTexture == NULL) && (m_width == 64))
	{
		m_pTxt->m_pTexture = LoadTexture("xboxlogo64.xbx", 64, 64);
    }
	else if((m_pTxt->m_pTexture == NULL) && (m_width == 128))
	{
		m_pTxt->m_pTexture = LoadTexture("xboxlogo128.xbx", 128, 128);
	}
	m_pTxt->m_bLoading = false;
	m_pTxt->m_timeLoaded = TheseusGetNow();
	if (m_pTxt->m_pTexture && m_szURL) {
		char ansi[128] = {};
		Ansi(ansi, m_szURL, 127);
		strncpy(m_pTxt->m_pTexture->m_srcName, ansi, 127);
	}
}




static TXTCACHE TextureCache [100];
static TXTCACHE* pTxtLock;

bool CleanupTextureCache()
{
	TRACE("Looking for a texture to free...\n");

	TXTCACHE* pOldTxt = NULL;
	for (int i = 0; i < countof(TextureCache); i += 1)
	{
		TXTCACHE* pTxt = &TextureCache[i];
		if (pTxt->m_szURL == NULL || pTxt == pTxtLock || pTxt->m_bLoading)
			continue;

		if (pOldTxt == NULL || pOldTxt->m_usage > pTxt->m_usage)
			pOldTxt = pTxt;
	}

	if (pOldTxt == NULL)
	{
		TRACE("    none left!\n");
		return false;
	}

	// Free this one up...

	TRACE("    freeing %s\n", pOldTxt->m_szURL);

	delete [] pOldTxt->m_szURL;

	if (pOldTxt->m_pTexture != NULL)
		pOldTxt->m_pTexture->Release();

	memset(pOldTxt, 0, sizeof (TXTCACHE));

	return true;
}

void FlushTextureCache()
{
	for (int i = 0; i < countof(TextureCache); i += 1)
	{
		TXTCACHE* pTxt = &TextureCache[i];
		if (pTxt->m_szURL == NULL || pTxt->m_bLoading)
			continue;

		delete [] pTxt->m_szURL;

		if (pTxt->m_pTexture != NULL)
			pTxt->m_pTexture->Release();

		memset(pTxt, 0, sizeof (TXTCACHE));
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

	OutputDebugString("[FlushTextureCache] All cached textures released\n");
}

TXTCACHE* FindTexture(const char* szURL, bool bAsync, unsigned int width, unsigned int height, bool binXIP=false)
{
	TXTCACHE* pFreeOne = NULL;
	TXTCACHE* pOldOne = NULL;
	
	for (int i = 0; i < countof(TextureCache); i += 1)
	{
		if (TextureCache[i].m_szURL != NULL && strcasecmp(TextureCache[i].m_szURL, szURL) == 0)
		{
			TextureCache[i].m_usage = TheseusGetNow();
			return &TextureCache[i];
		}

		if (pFreeOne != NULL)
			continue;

		if (TextureCache[i].m_bLoading)
			continue;

		if (TextureCache[i].m_szURL == NULL)
			pFreeOne = &TextureCache[i];
		else if (pOldOne == NULL || pOldOne->m_usage > TextureCache[i].m_usage)
			pOldOne = &TextureCache[i];
	}

	if (pFreeOne == NULL && pOldOne != NULL)
	{
		delete [] pOldOne->m_szURL;

		if (pOldOne->m_pTexture != NULL)
			pOldOne->m_pTexture->Release();

		memset(pOldOne, 0, sizeof (TXTCACHE));
		pFreeOne = pOldOne;
	}

	ASSERT(pFreeOne != NULL);

	ASSERT(pTxtLock == NULL);
	pTxtLock = pFreeOne;

	pFreeOne->m_szURL = new char [strlen(szURL) + 1];
	strcpy(pFreeOne->m_szURL, szURL);

	pFreeOne->m_usage = TheseusGetNow();

	if (bAsync && !binXIP)
	{
		pFreeOne->m_bLoading = true;
		CBackgroundTexture* pLoader = new CBackgroundTexture(pFreeOne, width, height);
		if (!pLoader->Fetch(szURL))
			delete pLoader;
	}
	else
	{
		pFreeOne->m_pTexture = LoadTexture(szURL, width, height);
		pFreeOne->m_timeLoaded = TheseusGetNow();
		// Tag texture with source name for inspector
		if (pFreeOne->m_pTexture && szURL) {
			char ansi[128] = {};
			Ansi(ansi, szURL, 127);
			strncpy(pFreeOne->m_pTexture->m_srcName, ansi, 127);
		}
	}
	
	pTxtLock = NULL;

	return pFreeOne;
}

LPDIRECT3DTEXTURE8 GetTexture(const char* szURL, XTIME* pTimeLoaded, unsigned int width, unsigned int height, bool binXIP = false)
{
	TXTCACHE* pTxt = FindTexture(szURL, true, width, height, binXIP);
	ASSERT(pTxt != NULL);
	if (pTxt->m_pTexture == NULL)
		return NULL;

	if (pTimeLoaded != NULL)
		*pTimeLoaded = pTxt->m_timeLoaded;

	return pTxt->m_pTexture;
}




extern bool CreateTextureFromFile(const char* szFileName, CTexture* pTexture);

void CImageTexture::Load(const char* szURL)
{
	if (m_surface != NULL)
	{
		m_surface->Release();
		m_surface = NULL;
	}

	TXTCACHE* pTxt = FindTexture(szURL, false, 0, 0);
	ASSERT(pTxt != NULL);
	if (pTxt->m_pTexture == NULL)
		return;

	m_surface = pTxt->m_pTexture;
	m_surface->AddRef();

	TheseusGetTextureSize(m_surface, m_nImageWidth, m_nImageHeight);
}

bool CImageTexture::OnSetProperty(const PRD* pprd, const void* pvValue)
{
	if ((intptr_t)pprd->pbOffset == MEMBER_OFFSET(m_url))
		m_dirty = true;

	return true;
}
