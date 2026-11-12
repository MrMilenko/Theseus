// image.cpp: desktop texture loading, image cache, and skin texture
// preloader. Handles XBX (Xbox packed textures), standard image
// formats via stb_image, and XPR0 textures. LRU cache for rendered
// text / image surfaces. Counterpart to render/asset_loader.cpp.

#include "std.h"
#include "dashapp.h"
#include "node.h"
#include "asset_loader.h"
#include "activefile.h"
#include "xip_archive.h"
#include "file_util.h"
#include "xbx_texture.h"
#include "skin_assets.h"
#include "host.h"

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

		// The XIP ships one name per group, so shell.xbx resolves to the
		// cellwall.xbx that is actually in there.
		char *slash = strrchr(szBuf, '/');
		char *back  = strrchr(szBuf, '\\');
		char *baseP = (slash > back ? slash : back);
		baseP = baseP ? baseP + 1 : szBuf;

		const char *candidates[4];
		int nCandidates = SkinCandidatesFor(baseP, candidates, 4);
		for (int i = 1; i < nCandidates; i++)   // 0 is the name we just tried
		{
			char aliasBuf[MAX_PATH];
			size_t dirLen = (size_t)(baseP - szBuf);
			memcpy(aliasBuf, szBuf, dirLen);
			snprintf(aliasBuf + dirLen, sizeof(aliasBuf) - dirLen, "%s", candidates[i]);

			lpTexture = (LPDIRECT3DTEXTURE8)FindObjectInXIP(aliasBuf, szURL, XIP_TYPE_TEXTURE);
			if (lpTexture != NULL)
				return lpTexture;
		}
	}

	// === Fallback: Try original path with original extension ===
	if (file.Fetch(szURL, true))
	{
		const char *ext = strrchr(szURL, '.');
		if (ext && (strcasecmp(ext, ".tga") == 0 || strcasecmp(ext, ".bmp") == 0 || strcasecmp(ext, ".dds") == 0
			|| strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".png") == 0 || strcasecmp(ext, ".jpeg") == 0))
		{
			// Decode the bytes Fetch already has; reopening by name would
			// miss the virtual-game icon redirect.
			LPDIRECT3DTEXTURE8 lpTexture = NULL;
			if (SUCCEEDED(D3DXCreateTextureFromFileInMemory(TheseusGetD3DDev(), file.GetContent(), file.GetContentLength(), &lpTexture)))
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

// ============================================================================
// Image Cache; LRU texture cache (100 entries)
// ============================================================================

LPDIRECT3DTEXTURE8 MyLoadImage(const char *szURL);

struct IMGCACHE
{
	char *m_szURL;
	LPDIRECT3DTEXTURE8 m_hImage;
	SIZE m_size;
	XTIME m_usage;
};

static IMGCACHE imageCache[100];
static IMGCACHE *pImgLock;

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

LPDIRECT3DTEXTURE8 MyLoadImage(const char *szURL)
{
	return LoadTexture(szURL, 0, 0);
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
