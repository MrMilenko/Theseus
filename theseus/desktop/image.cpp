// image.cpp: desktop texture loading, image cache, and skin texture
// preloader. Handles XBX (Xbox packed textures), standard image
// formats via stb_image, and XPR0 textures. LRU cache for rendered
// text / image surfaces. Counterpart to render/asset_loader.cpp.

#include "std.h"
#include "dashapp.h"
#include "node.h"
#include "asset_loader.h"
#include "activefile.h"
#include "xip.h"
#include "file_util.h"
#include "xbx_texture.h"
#include "skin_assets.h"

inline void D3D_FreeNoncontiguousMemory(void *pMemory) { free(pMemory); }

bool DecodeRAW(const char *szFileName, CTexture *pTexture);

// ============================================================================
// CImage; raw pixel buffer (used by DecodeRAW)
// ============================================================================

class CImage
{
public:
	CImage();
	~CImage();

	int m_nWidth;
	int m_nHeight;
	uint8_t *m_pels;
	int m_nPitch;
};

CImage::CImage()
{
	m_pels = NULL;
	m_nWidth = 0;
	m_nHeight = 0;
}

CImage::~CImage()
{
}

// ============================================================================
// Texture Parsing and Loading
// ============================================================================

LPDIRECT3DTEXTURE8 ParseTexture(const char *szURL, const uint8_t *pbContent, int cbContent, unsigned int width, unsigned int height)
{
	// Desktop SDL build: parse XBX textures via xbx_texture.h
	const char *pch = strrchr(szURL, '.');
	if (pch != NULL)
	{
		pch += 1;
		if (strcasecmp(pch, "xbx") == 0 || strcasecmp(pch, "xt") == 0)
		{
			IDirect3DTexture8* pTexture = XBX_ParseTexture(pbContent, cbContent);
			if (pTexture != NULL)
				return pTexture;
			fprintf(stderr, "[XBX] ParseTexture failed for '%s'\n", szURL);
		}
		else if (strcasecmp(pch, "jpg") == 0 || strcasecmp(pch, "jpeg") == 0 ||
		         strcasecmp(pch, "png") == 0 || strcasecmp(pch, "bmp") == 0)
		{
			// Use stb_image for standard image formats
			int w = 0, h = 0, ch = 0;
			unsigned char* pixels = stbi_load_from_memory(pbContent, cbContent, &w, &h, &ch, 4);
			if (pixels && w > 0 && h > 0) {
				IDirect3DTexture8* pTexture = new IDirect3DTexture8();
				pTexture->CreateFromRGBA((UINT)w, (UINT)h, pixels);
				stbi_image_free(pixels);
				return pTexture;
			}
		}
	}
	return NULL;
}
LPDIRECT3DTEXTURE8 LoadTexture(const char *szURL, unsigned int width, unsigned int height)
{
	bool bInXip = false;
	char szBuf[MAX_PATH];
	MakeAbsoluteURL(szBuf, szURL);

	CActiveFile file;

	// === Skin override first ===
	// Skin authors expect their custom .xbx files to replace the XIP
	// defaults; if XIP wins (the previous order) skin overrides only
	// fire for assets the XIP doesn't ship, which silently breaks
	// every skin that retextures cellwall, hilites, panels, etc.
	//
	// Gated on IsSkinnableAsset so we only probe the skin folder
	// for files that are actually part of the skinnable surface.
	// Random texture refs (icons, scene-specific art, etc.) skip
	// the skin lookup entirely and go straight to XIP.
	if (g_sSkinDir)
	{
		char OverrideName[MAX_PATH];
		strcpy(OverrideName, szURL);

		char *ext = strrchr(OverrideName, '.');
		if (ext != NULL)
			strcpy(ext + 1, "xbx"); // force .xbx extension for override

		if (IsSkinnableAsset(OverrideName))
		{
			// Probe every member of the equivalence group. A UI.X-era
			// skin that only ships menu_hilite.xbx will satisfy a retail
			// XAP request for GameHilite_01.xbx via SkinCandidatesFor.
			const char *candidates[4];
			int nCandidates = SkinCandidatesFor(OverrideName, candidates, 4);
			for (int i = 0; i < nCandidates; i++)
			{
				char SkinPath[MAX_PATH];
				sprintf(SkinPath, "%s%s", g_sSkinDir, candidates[i]);

				if (!file.Fetch(SkinPath, true))
					continue;

				LPDIRECT3DTEXTURE8 lpTexture = ParseTexture(SkinPath, file.GetContent(), file.GetContentLength(), width, height);
				if (lpTexture != NULL)
					return lpTexture;
			}
		}
	}

	// === XIP lookup using forced .xbx extension ===
	char *pch = strrchr(szBuf, '.');
	if (pch != NULL)
	{
		strcpy(pch + 1, "xbx");
		LPDIRECT3DTEXTURE8 lpTexture = (LPDIRECT3DTEXTURE8)FindObjectInXIP(szBuf, szURL, XIP_TYPE_TEXTURE);
		if (lpTexture != NULL)
			return lpTexture;
	}

	// === Fallback: Try original path with original extension ===
	if (file.Fetch(szURL, true))
	{
		const char *ext = strrchr(szURL, '.');
		if (ext && (strcasecmp(ext, ".tga") == 0 || strcasecmp(ext, ".bmp") == 0 || strcasecmp(ext, ".dds") == 0
			|| strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".png") == 0 || strcasecmp(ext, ".jpeg") == 0))
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

	// No final fallback. Returning menu_hilight.xbx for any missing
	// texture poisoned mesh UVs across the dashboard (skin.xm wrapped
	// with the unresolved background.bmp came out as a giant smear of
	// the menu-hilight pill). Better to fail loudly so missing assets
	// are obvious during skin authoring.
	ALERT("Unable to load texture file (%s)", szURL);
	return NULL;
}

// Desktop: load XPR/XBX textures from file path (with xboxfs drive mapping)
LPDIRECT3DTEXTURE8 LoadTextureFromXPR(const char *xprfile)
{
	// Read file and parse via XBX_ParseTexture (same XPR0 format)
	if (xprfile == NULL)
		return NULL;

	FILE* f = fopen(xprfile, "rb");
	if (!f) {
		// Try drive-letter remap (C: -> Configs, Q: -> Data, E: -> Library).
		char mapped[MAX_PATH];
		if (xprfile[0] && xprfile[1] == ':' && (xprfile[2] == '\\' || xprfile[2] == '/')) {
			const char* prefix = XboxFS_DriveToPrefix(xprfile[0]);
			if (prefix) {
				snprintf(mapped, sizeof(mapped), "%s/%s", prefix, xprfile + 3);
				for (char* p = mapped; *p; p++) if (*p == '\\') *p = '/';
				f = fopen(mapped, "rb");
			}
		}
	}
	if (!f) {
		OutputDebugStringA("Could not open texture ");
		OutputDebugStringA(xprfile);
		OutputDebugStringA(" for reading.\n");
		return NULL;
	}

	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (size <= 0) { fclose(f); return NULL; }

	uint8_t* data = new uint8_t[size];
	fread(data, 1, size, f);
	fclose(f);

	IDirect3DTexture8* pTexture = XBX_ParseTexture(data, (int)size);
	delete [] data;

	if (!pTexture) {
		OutputDebugStringA("Could not read ");
		OutputDebugStringA(xprfile);
		OutputDebugStringA("\n");
	}
	return pTexture;
}

// ============================================================================
// Image Cache; LRU texture cache (100 entries)
// ============================================================================

HIMAGE MyLoadImage(const char *szURL);

struct IMGCACHE
{
	char *m_szURL;
	HIMAGE m_hImage;
	SIZE m_size;
	XTIME m_usage;
};

static IMGCACHE imageCache[100];
static IMGCACHE *pImgLock;

void CleanupImageCache()
{
	for (int i = 0; i < countof(imageCache); i += 1)
	{
		IMGCACHE *pImg = &imageCache[i];
		if (pImg == pImgLock)
			continue;

		delete[] pImg->m_szURL;

		if (pImg->m_hImage != NULL)
			pImg->m_hImage->Release();

		memset(pImg, 0, sizeof(IMGCACHE));
	}
}

IMGCACHE *FindImage(const char *szURL)
{
	IMGCACHE *pFreeOne = NULL;
	IMGCACHE *pOldOne = NULL;
	for (int i = 0; i < countof(imageCache); i += 1)
	{
		if (imageCache[i].m_szURL != NULL && strcasecmp(imageCache[i].m_szURL, szURL) == 0)
		{
			//			TRACE("Found %s in image cache\n", szURL);
			imageCache[i].m_usage = TheseusGetNow();
			return &imageCache[i];
		}

		if (pFreeOne != NULL)
			continue;

		if (imageCache[i].m_szURL == NULL)
			pFreeOne = &imageCache[i];
		else if (pOldOne == NULL || pOldOne->m_usage > imageCache[i].m_usage)
			pOldOne = &imageCache[i];
	}

	if (pFreeOne == NULL && pOldOne != NULL)
	{
		// Evicting oldest texture from cache to make room
		delete[] pOldOne->m_szURL;

		if (pOldOne->m_hImage != NULL)
			pOldOne->m_hImage->Release();

		memset(pOldOne, 0, sizeof(IMGCACHE));
		pFreeOne = pOldOne;
	}

	ASSERT(pFreeOne != NULL);

	ASSERT(pImgLock == NULL);
	pImgLock = pFreeOne;

	pFreeOne->m_szURL = new char[strlen(szURL) + 1];
	strcpy(pFreeOne->m_szURL, szURL);

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

extern "C" HIMAGE FetchImage(const char *szURL, SIZE *pSize)
{
	IMGCACHE *pImgCache = FindImage(szURL);
	if (pImgCache == NULL)
		return NULL;

	if (pSize != NULL)
		*pSize = pImgCache->m_size;

	return pImgCache->m_hImage;
}

HIMAGE MyLoadImage(const char *szURL)
{
	return LoadTexture(szURL, 0, 0);
}

// ============================================================================
// RAW Texture Decoding
// ============================================================================

bool DecodeRAW(const char *szFileName, CTexture *pTexture)
{
	FILE *fp = _tfopen(szFileName, "rb");
	if (fp == NULL)
		return false;

	fseek(fp, 0, SEEK_END);
	uint32_t dwFileSize = ftell(fp);
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
		TRACE("Create texture failed!\n");
		fclose(fp);
		return false;
	}

	D3DLOCKED_RECT lr;
	VERIFYHR(pTexture->m_surface->LockRect(0, &lr, NULL, D3DLOCK_DISCARD));
	void *pvPels = lr.pBits;
	int nPitch = lr.Pitch;

	uint8_t *rgbsrc = new uint8_t[nWidth * (nDepth / 8)];

	for (int y = 0; y < nHeight; y += 1)
	{
		fread(rgbsrc, 1, nWidth * (nDepth / 8), fp);

		uint8_t *pbSrc = rgbsrc;
		uint8_t *pbDest = (uint8_t *)pvPels + y * nPitch;

		for (int x = 0; x < nWidth; x += 1)
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

// ============================================================================
// Image Drawing API (used by RenderHTML)
// ============================================================================

EXTERN_C int GetImageSize(const char *szImgFile, SIZE *pSize)
{
	return FetchImage(szImgFile, pSize) != NULL;
}

EXTERN_C void DrawImage(HDRAW hDC, const char *szImgFile, int x, int y, int align, uint16_t *pcx, uint16_t *pcy)
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

		X_BitBlt(hDC, x, y, size.cx, size.cy, hImage, 0, 0);
	}
	else
	{
		size.cx = 20;
		size.cy = 20;
	}

	if (pcx != NULL)
		*pcx = (uint16_t)size.cx;

	if (pcy != NULL)
		*pcy = (uint16_t)size.cy;
}

static bool Clip(HDRAW hDraw, int &x, int &y, int &cx, int &cy)
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

	if (x + cx > (int)hDraw->Desc.Width)
		cx = (int)hDraw->Desc.Width - x;

	if (y + cy > (int)hDraw->Desc.Height)
		cy = (int)hDraw->Desc.Height - y;

	if (cx <= 0 || cy <= 0)
		return false;

	return true;
}

// ============================================================================
// Skin Texture Preloader
// ============================================================================

void PreloadSkinTextures()
{

	if (!g_sSkinDir || g_sSkinDir[0] == '\0')
	{
		return;
	}

	// Convert skin dir to ANSI path, then map through xboxfs
	// Desktop: map Q:\path to Data/path for directory enumeration
	char ansiSkinDir[MAX_PATH] = {0};
	Ansi(ansiSkinDir, g_sSkinDir, MAX_PATH);

	char mappedDir[MAX_PATH];
	if (ansiSkinDir[0] && ansiSkinDir[1] == ':' && (ansiSkinDir[2] == '\\' || ansiSkinDir[2] == '/')) {
		const char* prefix = XboxFS_DriveToPrefix(ansiSkinDir[0]);
		if (prefix) {
			snprintf(mappedDir, sizeof(mappedDir), "%s/%s", prefix, ansiSkinDir + 3);
			for (char* p = mappedDir; *p; p++) if (*p == '\\') *p = '/';
		} else {
			mappedDir[0] = '\0';
		}
	} else {
		strncpy(mappedDir, ansiSkinDir, MAX_PATH);
	}


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

#ifdef _WIN32
	char searchBuf[512];
	snprintf(searchBuf, sizeof(searchBuf), "%s\\*.xbx", mappedDir);
	struct _finddata_t fd;
	intptr_t hFind = _findfirst(searchBuf, &fd);
	if (hFind == -1) {
		OutputDebugStringA("[Preload] Could not open skin directory\n");
		return;
	}
	do { ProcessXbxFile(fd.name); } while (_findnext(hFind, &fd) == 0);
	_findclose(hFind);
#else
	DIR* dir = opendir(mappedDir);
	if (!dir) {
		OutputDebugStringA("[Preload] Could not open skin directory\n");
		return;
	}
	struct dirent* ent;
	while ((ent = readdir(dir)) != NULL) ProcessXbxFile(ent->d_name);
	closedir(dir);
#endif
}// ============================================================================
// Software Rasterization (FillRect / BitBlt)
// ============================================================================

EXTERN_C HRESULT X_FillRect(HDRAW hDraw, int x, int y, int cx, int cy, D3DCOLOR color)
{
	ASSERT(hDraw != NULL);
	ASSERT(hDraw->Desc.Format == D3DFMT_A8R8G8B8 || hDraw->Desc.Format == D3DFMT_X8R8G8B8);

	if (!Clip(hDraw, x, y, cx, cy))
		return S_OK;

	for (int j = 0; j < cy; j += 1)
	{
		uint32_t *ppel = (uint32_t *)((uint8_t *)hDraw->Lock.pBits + hDraw->Lock.Pitch * (y + j)) + x;
		for (int i = 0; i < cx; i += 1)
			*ppel++ = color;
	}

	return S_OK;
}

EXTERN_C HRESULT X_BitBlt(HDRAW hDraw, int x, int y, int cx, int cy, LPDIRECT3DTEXTURE8 pSrcSurface, int xSrc, int ySrc)
{
	ASSERT(hDraw != NULL);
	ASSERT(pSrcSurface != NULL);

	{
		D3DSURFACE_DESC descSrc;

		VERIFYHR(pSrcSurface->GetLevelDesc(0, &descSrc));

		ASSERT(hDraw->Desc.Format == D3DFMT_A8R8G8B8 || hDraw->Desc.Format == D3DFMT_X8R8G8B8);
		ASSERT(hDraw->Desc.Format == descSrc.Format);
	}

	if (!Clip(hDraw, x, y, cx, cy))
		return S_OK;

	D3DLOCKED_RECT lockSrc;
	HRESULT hr = pSrcSurface->LockRect(0, &lockSrc, NULL, D3DLOCK_READONLY);
	if (FAILED(hr))
		return hr;

	for (int j = 0; j < cy; j += 1)
	{
		uint32_t *ppel = (uint32_t *)((uint8_t *)hDraw->Lock.pBits + hDraw->Lock.Pitch * (y + j)) + x;
		uint32_t *ppelSrc = (uint32_t *)((uint8_t *)lockSrc.pBits + lockSrc.Pitch * (ySrc + j)) + xSrc;

		for (int i = 0; i < cx; i += 1)
			*ppel++ = *ppelSrc++;
	}

	pSrcSurface->UnlockRect(0);

	return S_OK;
}
