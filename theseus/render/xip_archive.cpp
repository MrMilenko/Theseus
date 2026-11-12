// xip_archive.cpp: the XIP archive reader. A .xip holds a scene's scripts,
// textures and meshes; this reads every entry into memory, owns the mesh
// buffers its CMeshRefs draw from, and finds entries by path. Rebuilt
// function by function from the 5960 retail XBE; the findings are in
// docs/decomp/XIP.md. Both builds compile this file. The platform seams are
// the block buffer, how textures are made, whether loads run on a thread,
// and the Xbox edge AA swap.

#include "std.h"
#include "theseus.h"
#include "file_util.h"
#include "xip_archive.h"
#include "asset_loader.h"
#ifndef _XBOX
#include "xbx_texture.h"
#endif

#ifdef _XBOX
extern BOOL g_bEdgeAntialiasOverride;
extern "C" void WINAPI D3D_FreeNoncontiguousMemory(void* pMemory);
#endif

static const int XIP_BLOCK = 0x10000;

#ifdef _XBOX
static const DWORD XIP_LOCK = D3DLOCK_DISCARD | D3DLOCK_NOFLUSH;
#else
static const DWORD XIP_LOCK = D3DLOCK_DISCARD;
#endif


// ===== CFileBuffer ==========================================================

CFileBuffer::CFileBuffer()
{
	m_handle = INVALID_HANDLE_VALUE;
	m_buffer = NULL;
	m_bufferSize = 0;
	m_readIndex = 0;
	m_currentBlock = 0;
}

CFileBuffer::~CFileBuffer()
{
	FreeBuffer();
}

// The 5960 version also finds this file's entry in the signed XIPS hash
// section; Theseus loads unsigned archives and keeps only the handle.
void CFileBuffer::SetFile(const TCHAR* Name, HANDLE hFile)
{
	m_handle = hFile;
}

void CFileBuffer::FreeBuffer()
{
	if (m_buffer == NULL)
		return;
#ifdef _XBOX
	VirtualFree(m_buffer, 0, MEM_RELEASE);
#else
	free(m_buffer);
#endif
	m_buffer = NULL;
}

// The block buffer, from the VirtualAlloc loop 5960 inlines into Seek and
// Read: on failure it frees the texture cache, then the mesh cache, then
// gives up for good (all of which NewFailed does).
static BYTE* AllocBlockBuffer()
{
	for (;;)
	{
#ifdef _XBOX
		BYTE* pb = (BYTE*)VirtualAlloc(NULL, XIP_BLOCK, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
		BYTE* pb = (BYTE*)calloc(1, XIP_BLOCK);
#endif
		if (pb != NULL)
			return pb;
		NewFailed(XIP_BLOCK);
	}
}

// CFileBuffer::Seek(nPos)
// Reconstructed from 5960 retail XBE @ 0x0003d5e0. Ghidra decompile, trimmed:
//     block = pos / 0x10000;
//     if (!buffer) { buffer = alloc(0x10000); current = -1; }
//     if (block != current) {
//         SetFilePointer(h, block << 16, 0, FILE_BEGIN); current = block;
//         if (!ReadFile(h, buffer, 0x10000, &got) || got == 0) return 0;
//         verify block hash; current++; size = got;
//     }
//     index = pos % 0x10000; return 1;
// current ends up one past the block just read, so seeking back into it reads
// it again. That is the binary's behavior and costs nothing that matters.
// Refusing an offset past the end of the file is Theseus's: Read would spin
// forever on it.
bool CFileBuffer::Seek(int nPos)
{
	if (m_buffer == NULL)
	{
		m_buffer = AllocBlockBuffer();
		m_currentBlock = -1;
	}

	const int nBlock = nPos / XIP_BLOCK;
	if (nBlock != m_currentBlock)
	{
		SetFilePointer(m_handle, nBlock * XIP_BLOCK, NULL, FILE_BEGIN);
		m_currentBlock = nBlock;

		DWORD cbGot = 0;
		if (!ReadFile(m_handle, m_buffer, XIP_BLOCK, &cbGot, NULL) || cbGot == 0)
			return false;

		m_currentBlock++;
		m_bufferSize = (int)cbGot;
	}

	const int nIndex = nPos % XIP_BLOCK;
	if (nIndex > m_bufferSize)
		return false;
	m_readIndex = nIndex;
	return true;
}

// A read the disk refuses. 5960 calls HalReturnToFirmware(4), the fatal error
// reboot, and the Xbox build keeps that; desktop refuses the read instead.
static void XipReadFailed()
{
#ifdef _XBOX
	HalReturnToFirmware(HalFatalErrorRebootRoutine);
#else
	TRACE("\001CFileBuffer::Read: the disk refused a XIP read\n");
#endif
}

// CFileBuffer::Read(pv, cb)
// Reconstructed from 5960 retail XBE @ 0x0003d710. Ghidra decompile, trimmed:
//     if (!buffer) buffer = alloc(0x10000);
//     for (;;) {
//         if (cb < 1) return total;
//         if (index < size) { n = min(size - index, cb); copy n; advance; }
//         if (index != size) continue;
//         if (!ReadFile(h, buffer, 0x10000, &got)) HalReturnToFirmware(4);
//         if (got == 0) return -1;
//         verify block hash; current++; size = got; index = 0;
//     }
// The buffer refills as soon as it runs dry, even when the copy just finished,
// so a read that ends on the last byte of the file reports -1.
int CFileBuffer::Read(void* pv, int cb)
{
	if (m_buffer == NULL)
		m_buffer = AllocBlockBuffer();

	BYTE* pbOut = (BYTE*)pv;
	int cbDone = 0;

	while (cb > 0)
	{
		if (m_readIndex < m_bufferSize)
		{
			int n = m_bufferSize - m_readIndex;
			if (n > cb)
				n = cb;
			memcpy(pbOut, m_buffer + m_readIndex, n);
			pbOut += n;
			cb -= n;
			cbDone += n;
			m_readIndex += n;
		}

		if (m_readIndex == m_bufferSize)
		{
			DWORD cbGot = 0;
			if (!ReadFile(m_handle, m_buffer, XIP_BLOCK, &cbGot, NULL))
			{
				XipReadFailed();
				return -1;
			}
			if (cbGot == 0)
				return -1;

			m_currentBlock++;
			m_bufferSize = (int)cbGot;
			m_readIndex = 0;
		}
	}

	return cbDone;
}


// ===== The archive pool =====================================================
// 5960 keeps twenty archives in a fixed array (0x001804e0, 0x150 bytes a slot,
// count at 0x0017e734) and nothing ever leaves it, since scene nodes and mesh
// refs keep pointing at them. Theseus has more scenes than that, so the pool
// grows twenty slots at a time. Each archive is its own allocation and never
// moves, and an outgrown slot table is kept: the Xbox loader thread can be
// walking it from CleanupMeshCache while LoadXIP grows it.

static const int XIP_POOL_STEP = 20;

static CXipFile** volatile c_rgXipFile = NULL;
static int c_nXipFileSlots = 0;
static volatile int c_nXipFileCount = 0;

static void GrowXipPool()
{
	const int nSlots = c_nXipFileSlots + XIP_POOL_STEP;
	CXipFile** rgNew = new CXipFile*[nSlots];
	for (int i = 0; i < c_nXipFileCount; i++)
		rgNew[i] = c_rgXipFile[i];
	c_rgXipFile = rgNew;
	c_nXipFileSlots = nSlots;
}

// 5960 starts loads on a worker thread and runs them inline if no thread
// starts. Desktop always runs them inline: scripts read the scene as soon as
// LoadXIP returns, and a half-built one corrupts their namespace.
static void RunLoader(LPTHREAD_START_ROUTINE pfnLoad, CXipFile* pXip)
{
#ifdef _XBOX
	DWORD idThread;
	HANDLE hThread = CreateThread(NULL, 0, pfnLoad, pXip, 0, &idThread);
	if (hThread != NULL)
	{
		CloseHandle(hThread);
		return;
	}
#endif
	pfnLoad(pXip);
}

// LoadXIP(szURL, bSync, isSkin)
// Reconstructed from 5960 retail XBE @ 0x0003d900. Ghidra decompile, trimmed:
//     path = (url[0] && url[1] != ':') ? "Q:/Xips/" + url : url;
//     key = CleanFilePath(path); *strrchr(key, '.') = 0;
//     for (i < count) if (!_stricmp(key, pool[i].dirPath)) {
//         pool[i].cacheTime = now;
//         if (loaded && vbCount > 0 && !meshBuffers[0].vb) pool[i].ReloadMeshBuffers();
//         return &pool[i];
//     }
//     xip = &pool[count]; if (!xip->Open(path)) return 0; count++;
//     if (!bSync) { start the loader thread; return xip; }
//     xip->Load(); return xip;
// The key keeps "default" while Open drops it, so default.xip never matches
// itself here; nothing loads it twice. Theseus grows the pool where 5960
// would write past it, a sync caller learns about a failed load,
// and a name too long or with no extension is refused. isSkin is Theseus's
// and unused here.
CXipFile* LoadXIP(const TCHAR* szURL, bool bSync, bool isSkin)
{
	const TCHAR* szPrefix = (szURL[0] != 0 && szURL[1] != ':') ? _T("Q:/Xips/") : _T("");
	if (_tcslen(szPrefix) + _tcslen(szURL) >= MAX_PATH)
		return NULL;
	TCHAR szPath[MAX_PATH];
	_tcscpy(szPath, szPrefix);
	_tcscat(szPath, szURL);

	char szKey[MAX_PATH];
	CleanFilePath(szKey, szPath);
	char* pchExt = strrchr(szKey, '.');
	if (pchExt == NULL)
		return NULL;
	*pchExt = 0;

	for (int i = 0; i < c_nXipFileCount; i++)
	{
		CXipFile* pXip = c_rgXipFile[i];
		if (_stricmp(szKey, pXip->m_dirPath) != 0)
			continue;

		pXip->m_cacheTime = TheseusGetNow();
		if (pXip->m_loaded && pXip->m_vertexBufferCount > 0 && pXip->m_meshBuffers[0].m_vertexBuffer == NULL)
			pXip->ReloadMeshBuffers();
		return pXip;
	}

	if (c_nXipFileCount == c_nXipFileSlots)
		GrowXipPool();

	CXipFile* pXip = new CXipFile;
	if (!pXip->Open(szPath))
	{
		delete pXip;
		return NULL;
	}
	c_rgXipFile[c_nXipFileCount] = pXip;
	c_nXipFileCount++;

	if (!bSync)
	{
		RunLoader(CXipFile::StartLoadThread, pXip);
		return pXip;
	}

	// The slot stays spent: a caller that waited on this is about to give up.
	if (!pXip->Load())
	{
		TRACE(_T("\001LoadXIP: %s failed to load\n"), szPath);
		return NULL;
	}
	return pXip;
}

// FindInXIPAndDetach(szURL, pbContent, cbContent)
// Reconstructed from 5960 retail XBE @ 0x0003dab0. Ghidra decompile, trimmed:
//     path = CleanFilePath(url);
//     for (i = count - 1; i >= 0; i--) {
//         if (pool[i].locked || !pool[i].loaded) continue;
//         n = pool[i].Find(path);
//         if (n >= 0 && objects[n]) {
//             content = objects[n]; cb = filedata[n].size; objects[n] = 0; return 1;
//         }
//     }
//     return 0;
// The caller owns the bytes from here on; newer archives win. Only raw
// entries are handed out (Theseus's check): a texture or mesh ref isn't
// m_size bytes long, and a bad directory can point a name at one.
bool FindInXIPAndDetach(const TCHAR* szURL, BYTE*& pbContent, DWORD& cbContent)
{
	char szPath[MAX_PATH];
	CleanFilePath(szPath, szURL);

	for (int i = c_nXipFileCount - 1; i >= 0; i--)
	{
		CXipFile& xip = *c_rgXipFile[i];
		if (xip.m_locked || !xip.m_loaded)
			continue;

		const int n = xip.Find(szPath);
		if (n < 0 || xip.m_objects[n] == NULL)
			continue;
		const DWORD type = xip.m_filedata[n].m_type;
		if (type == XIP_TYPE_TEXTURE || type == XIP_TYPE_MESH_REFERENCE)
			continue;

		pbContent = (BYTE*)xip.m_objects[n];
		cbContent = xip.m_filedata[n].m_size;
		xip.m_objects[n] = NULL;
		return true;
	}
	return false;
}

// A loose texture in the skin folder beats the archive's copy.
static LPDIRECT3DTEXTURE8 LoadSkinTexture(const TCHAR* szFilename)
{
	TCHAR szSkinPath[MAX_PATH];
	_sntprintf(szSkinPath, countof(szSkinPath) - 1, _T("%s%s"), TheseusGetSkinDir(), szFilename);
	szSkinPath[countof(szSkinPath) - 1] = 0;

	char szAnsi[MAX_PATH];
	Ansi(szAnsi, szSkinPath, MAX_PATH);

	LPDIRECT3DTEXTURE8 pTex = NULL;
#ifdef _XBOX
	if (D3DXCreateTextureFromFileA(TheseusGetD3DDev(), szAnsi, &pTex) != D3D_OK)
		return NULL;
#else
	// The skin folder is an Xbox path, so it opens through the translator.
	FILE* f = TheseusFOpen(szAnsi, "rb");
	if (f == NULL)
		return NULL;
	fseek(f, 0, SEEK_END);
	long cb = ftell(f);
	fseek(f, 0, SEEK_SET);
	BYTE* pb = cb > 0 ? (BYTE*)malloc(cb) : NULL;
	bool bRead = pb != NULL && fread(pb, 1, cb, f) == (size_t)cb;
	fclose(f);
	HRESULT hr = bRead ? D3DXCreateTextureFromFileInMemory(TheseusGetD3DDev(), pb, (UINT)cb, &pTex) : E_FAIL;
	free(pb);
	if (hr != D3D_OK)
		return NULL;
#endif
	return pTex;
}

// FindObjectInXIP(szURL, szFilename, nType)
// Reconstructed from 5960 retail XBE @ 0x0003dba0. Ghidra decompile, trimmed:
//     path = CleanFilePath(url);
//     for (i = count - 1; i >= 0; i--) {
//         if (pool[i].locked || !pool[i].loaded) continue;
//         if (i == 0 && type == 2) {          // the default archive packs textures as .xbx
//             name = filename; strcpy(wcsrchr(name, '.') + 1, "xbx");
//             path = CleanFilePath(xipDir + name);
//         }
//         if (obj = pool[i].FindObject(path, type)) return obj;
//     }
//     return 0;
// The skin folder check in front and the NULL filename guard are Theseus's.
void* FindObjectInXIP(const TCHAR* szURL, const TCHAR* szFilename, int nType)
{
	if (nType == XIP_TYPE_TEXTURE && TheseusGetSkinDir() != NULL && szFilename != NULL)
	{
		LPDIRECT3DTEXTURE8 pTex = LoadSkinTexture(szFilename);
		if (pTex != NULL)
			return pTex;
	}

	char szPath[MAX_PATH];
	CleanFilePath(szPath, szURL);

	for (int i = c_nXipFileCount - 1; i >= 0; i--)
	{
		CXipFile& xip = *c_rgXipFile[i];
		if (xip.m_locked || !xip.m_loaded)
			continue;

		if (i == 0 && nType == XIP_TYPE_TEXTURE && szFilename != NULL)
		{
			TCHAR szName[MAX_PATH];
			_tcscpy(szName, szFilename);
			TCHAR* pchExt = _tcsrchr(szName, '.');
			if (pchExt != NULL)
				_tcscpy(pchExt + 1, _T("xbx"));

			TCHAR szFull[MAX_PATH];
			_tcscpy(szFull, TheseusGetXipDir());
			_tcscat(szFull, szName);
			CleanFilePath(szPath, szFull);
		}

		void* pObject = xip.FindObject(szPath, nType);
		if (pObject != NULL)
			return pObject;
	}
	return NULL;
}


// ===== CXipFile =============================================================

CXipFile::CXipFile()
{
	m_dirPath = NULL;
	m_fileName = NULL;
	m_filedata = NULL;
	m_directory = NULL;
	m_names = NULL;
	m_objects = NULL;
	m_loaded = false;
	m_locked = false;
	m_reloading = false;
	m_cacheTime = 0;
	memset(m_meshBuffers, 0, sizeof(m_meshBuffers));
	m_vertexBufferCount = 0;
	m_indexBufferCount = 0;
}

// CXipFile::~CXipFile()
// Reconstructed from 5960 retail XBE @ 0x0003dd20. Ghidra decompile, trimmed:
//     delete fileName, dirPath, filedata, directory, names (each then NULL);
//     DeleteMeshBuffers(); zero meshBuffers[10], vbCount, ibCount;
//     ~CFileBuffer: VirtualFree(buffer); free the XIPS section if held;
// The objects themselves are never freed; scene nodes still point at them.
CXipFile::~CXipFile()
{
	delete[] m_fileName;
	m_fileName = NULL;
	delete[] m_dirPath;
	m_dirPath = NULL;
	delete[] m_filedata;
	m_filedata = NULL;
	delete[] m_directory;
	m_directory = NULL;
	delete[] m_names;
	m_names = NULL;

	DeleteMeshBuffers();
	memset(m_meshBuffers, 0, sizeof(m_meshBuffers));
	m_vertexBufferCount = 0;
	m_indexBufferCount = 0;
}

// CXipFile::Open(szXipFileName)
// Reconstructed from 5960 retail XBE @ 0x0003ddf0. Ghidra decompile, trimmed:
//     fileName = copy(name);
//     dir = CleanFilePath(name); *strrchr(dir, '.') = 0;
//     base = strrchr(dir, '\\'); base = base ? base + 1 : dir;
//     if (!_stricmp(base, "default")) { if (base > dir) base--; *base = 0; }
//     dirPath = copy(dir); return 1;
// Only names the archive; Load reads it. "Q:\Xips\default.xip" answers for
// "Q:\Xips", so the default archive's entries are found by their folder.
bool CXipFile::Open(const TCHAR* szXipFileName)
{
	m_fileName = new TCHAR[_tcslen(szXipFileName) + 1];
	_tcscpy(m_fileName, szXipFileName);

	char szDir[MAX_PATH];
	CleanFilePath(szDir, szXipFileName);
	char* pchExt = strrchr(szDir, '.');
	if (pchExt == NULL)
		return false;
	*pchExt = 0;

	char* pszBase = strrchr(szDir, '\\');
	pszBase = pszBase != NULL ? pszBase + 1 : szDir;
	if (_stricmp(pszBase, "default") == 0)
	{
		if (pszBase > szDir)
			pszBase--;
		*pszBase = 0;
	}

	m_dirPath = new char[strlen(szDir) + 1];
	strcpy(m_dirPath, szDir);
	return true;
}

// CXipFile::StartLoadThread(pvContext)
// Reconstructed from 5960 retail XBE @ 0x0003dec0:
//     if (xip->reloading) xip->ReloadMeshBuffers(); else xip->Load(); return 0;
DWORD CALLBACK CXipFile::StartLoadThread(LPVOID pvContext)
{
	CXipFile* pXip = (CXipFile*)pvContext;
	if (pXip->m_reloading)
		pXip->ReloadMeshBuffers();
	else
		pXip->Load();
	return 0;
}

// Read gives back the byte count, or -1 for a short or failed read, so "did
// all of it arrive" has to be asked this way.
static bool XipReadExact(CFileBuffer& file, void* pv, int cb)
{
	return cb >= 0 && file.Read(pv, cb) == cb;
}

// CreateObjects reads entries back to back without seeking, so one it
// refuses still has to come off the stream or every entry after it is wrong.
static void XipDiscard(CFileBuffer& file, int cb)
{
	BYTE scratch[4096];
	while (cb > 0)
	{
		const int n = cb < (int)sizeof(scratch) ? cb : (int)sizeof(scratch);
		if (file.Read(scratch, n) != n)
			return;
		cb -= n;
	}
}

// Every real archive ends exactly where dataStart + dataSize says.
static bool XipSizeMatches(CFileBuffer& file, const XIPHEADER& hdr)
{
	const DWORD cbFile = GetFileSize(file.m_handle, NULL);
	return cbFile != (DWORD)-1 && cbFile == hdr.m_dataStart + hdr.m_dataSize;
}

// The tables come from the file, so check them before anything indexes with
// them: every name inside the pool and terminated, every directory entry
// naming a real entry, and every entry that gets read lying inside the data.
// 5960 trusts all of it.
static bool XipTablesValid(const XIPHEADER& hdr, const FILEDATA* pFiles, const FILENAME* pNames,
	const char* pchPool, int cbPool)
{
	if (pchPool[cbPool - 1] != 0)
		return false;

	for (int i = 0; i < hdr.m_nameCount; i++)
	{
		if (pNames[i].m_fileDataIndex >= hdr.m_fileCount || pNames[i].m_nameOffset >= cbPool)
			return false;
	}

	for (int i = 0; i < hdr.m_fileCount; i++)
	{
		const FILEDATA& fd = pFiles[i];
		if (fd.m_type == XIP_TYPE_MESH || fd.m_type == XIP_TYPE_MESH_REFERENCE)
			continue;
		if ((unsigned long long)fd.m_dataOffset + fd.m_size > hdr.m_dataSize)
			return false;
		if (fd.m_type == XIP_TYPE_VERTEXBUFFER && fd.m_size < 8)
			return false;
	}
	return true;
}

// An archive that fails to load is never read again.
static void XipClose(CFileBuffer& file)
{
	if (file.m_handle != INVALID_HANDLE_VALUE)
	{
		CloseHandle(file.m_handle);
		file.m_handle = INVALID_HANDLE_VALUE;
	}
}

// CXipFile::Load()
// Reconstructed from 5960 retail XBE @ 0x0003def0. Ghidra decompile, trimmed:
//     path = CleanFilePath(fileName); if (path starts "y:") path[0] = 'x';
//     h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING,
//                     NO_BUFFERING | SEQUENTIAL_SCAN | NORMAL);
//     if (h == -1) return 0;
//     file.SetFile(fileName, h);
//     if (!read(header, 0x10) || magic != 'XIP0') return 0;
//     filedata = new(fileCount * 16); read;  directory = new(nameCount * 4); read;
//     cbNames = dataStart - 16 - fileCount * 16 - nameCount * 4;
//     names = new(cbNames); read;
//     CreateObjects(); loaded = 1; return 1;
// Theseus opens through its own path layer and leaves the y: to x: swap out.
// Every read must be whole, the header has to describe this exact file, the
// tables have to hold together, and a failed load closes its handle.
bool CXipFile::Load()
{
	HANDLE hFile = TheseusCreateFile(m_fileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_NO_BUFFERING, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		TRACE(_T("\001CXipFile::Load: can't open %s (%d)\n"), m_fileName, (int)GetLastError());
		return false;
	}

	m_file.SetFile(m_fileName, hFile);

	if (!XipReadExact(m_file, &m_header, sizeof(XIPHEADER)) || m_header.m_magic != XIP_MAGIC)
	{
		XipClose(m_file);
		return false;
	}

	// The tables sit between the header and the data, and the name pool is
	// what's left. A bad dataStart makes that zero or less.
	const int cbFiledata = m_header.m_fileCount * (int)sizeof(FILEDATA);
	const int cbDirectory = m_header.m_nameCount * (int)sizeof(FILENAME);
	const int cbNames = (int)m_header.m_dataStart - (int)sizeof(XIPHEADER) - cbFiledata - cbDirectory;
	if (cbNames <= 0 || !XipSizeMatches(m_file, m_header))
	{
		TRACE(_T("\001CXipFile::Load: %s header does not describe this file\n"), m_fileName);
		XipClose(m_file);
		return false;
	}

	m_filedata = new FILEDATA[m_header.m_fileCount];
	m_directory = new FILENAME[m_header.m_nameCount];
	m_names = new char[cbNames];
	if (!XipReadExact(m_file, m_filedata, cbFiledata)
		|| !XipReadExact(m_file, m_directory, cbDirectory)
		|| !XipReadExact(m_file, m_names, cbNames))
	{
		XipClose(m_file);
		return false;
	}

	if (!XipTablesValid(m_header, m_filedata, m_directory, m_names, cbNames))
	{
		TRACE(_T("\001CXipFile::Load: %s has a broken table\n"), m_fileName);
		XipClose(m_file);
		return false;
	}

	CreateObjects();
	m_loaded = true;
	return true;
}

#ifdef _XBOX
// An XPR0 texture as FUN_0003e030 in the 5960 XBE builds it. Ghidra
// decompile, trimmed (xpr is the entry's bytes):
//     if (xpr->magic != 'XPR0') return 0;
//     tex = AllocNoncontiguous(0x14); copy the 0x14 byte resource from xpr + 0xc;
//     bits = AllocContiguous(total - headerSize, 0x80); copy from xpr + headerSize;
//     tex->Data = 0; Register(bits); tex->Common |= D3DCOMMON_D3DCREATED;
// The binary never frees the entry's bytes and leaks the header when the
// pixels don't fit; this frees both. The size checks are Theseus's: every
// offset the header gives has to land inside the entry.
static LPDIRECT3DTEXTURE8 TextureFromXPR(const BYTE* pbEntry, int cbEntry)
{
	const XPR_HEADER* pxpr = (const XPR_HEADER*)pbEntry;
	if (pxpr->dwMagic != XPR_MAGIC_VALUE)
		return NULL;

	if (pxpr->dwHeaderSize < sizeof(XPR_HEADER)
		|| pxpr->dwTotalSize < pxpr->dwHeaderSize
		|| pxpr->dwTotalSize > (DWORD)cbEntry)
	{
		TRACE(_T("\001TextureFromXPR: header describes %d..%d of %d bytes\n"),
			(int)pxpr->dwHeaderSize, (int)pxpr->dwTotalSize, cbEntry);
		return NULL;
	}

	const int cbBits = (int)(pxpr->dwTotalSize - pxpr->dwHeaderSize);
	if (cbBits <= 0)
		return NULL;

	IDirect3DTexture8* pTex = (IDirect3DTexture8*)TheseusD3D_AllocNoncontiguousMemory(sizeof(IDirect3DTexture8));
	if (pTex == NULL)
		return NULL;
	memcpy(pTex, pbEntry + sizeof(XPR_HEADER), sizeof(IDirect3DTexture8));

	BYTE* pbBits = (BYTE*)TheseusD3D_AllocContiguousMemory(cbBits, D3DTEXTURE_ALIGNMENT);
	if (pbBits == NULL)
	{
		D3D_FreeNoncontiguousMemory(pTex);
		return NULL;
	}
	memcpy(pbBits, pbEntry + pxpr->dwHeaderSize, cbBits);

	pTex->Data = 0;
	pTex->Register(pbBits);
	pTex->Common |= D3DCOMMON_D3DCREATED;
	return pTex;
}
#endif

// A texture entry. The Xbox build hands the XPR straight to the GPU; desktop
// decodes it in xbx_texture.h. The minimum size is Theseus's.
static LPDIRECT3DTEXTURE8 ReadTexture(CFileBuffer& file, int cbEntry)
{
#ifdef _XBOX
	if (cbEntry < (int)(sizeof(XPR_HEADER) + sizeof(IDirect3DTexture8)))
	{
		TRACE(_T("\001ReadTexture: entry too small (%d bytes)\n"), cbEntry);
		XipDiscard(file, cbEntry);
		return NULL;
	}
#endif

	BYTE* pbEntry = new BYTE[cbEntry];
#ifdef _XBOX
	if (!XipReadExact(file, pbEntry, cbEntry))
	{
		delete[] pbEntry;
		return NULL;
	}
	LPDIRECT3DTEXTURE8 pTex = TextureFromXPR(pbEntry, cbEntry);
#else
	file.Read(pbEntry, cbEntry);
	LPDIRECT3DTEXTURE8 pTex = XBX_ParseTexture(pbEntry, cbEntry);
#endif
	delete[] pbEntry;

	if (pTex == NULL)
		TRACE(_T("\001ReadTexture: could not use a %d byte texture\n"), cbEntry);
	return pTex;
}

// CXipFile::CreateObjects()
// Reconstructed from 5960 retail XBE @ 0x0003e110. Ghidra decompile, trimmed:
//     locked = 1; objects = new(fileCount * 4) zeroed;
//     for (i < fileCount) switch (filedata[i].type) {
//     case 1: break;
//     case 2: objects[i] = texture from the next size bytes; break;
//     case 4: r = new CMeshRef; r->xip = this; r->buffer = offset >> 24;
//             r->first = offset & 0xffffff; r->count = size; objects[i] = r; break;
//     case 5: ReadIndexBuffer(i, vbCount); ibCount++; break;
//     case 6: ReadVertexBuffer(i, vbCount); vbCount++; break;
//     default: objects[i] = XAppAllocMemory(size); read size bytes; break;
//     }
//     locked = 0;
// Entries sit in file order, so this reads straight through. An index buffer
// goes in the slot of the vertex buffer that follows it. The range checks and
// skips are Theseus's; the extra terminator byte on raw entries is for desktop,
// whose allocator doesn't hand back zeroed pages.
void CXipFile::CreateObjects()
{
	m_locked = true;

	const int nFiles = m_header.m_fileCount;
	m_objects = new void*[nFiles];
	memset(m_objects, 0, nFiles * sizeof(void*));

	for (int i = 0; i < nFiles; i++)
	{
		const FILEDATA& fd = m_filedata[i];
		switch (fd.m_type)
		{
		case XIP_TYPE_MESH:
			break;

		case XIP_TYPE_TEXTURE:
			m_objects[i] = ReadTexture(m_file, fd.m_size);
			break;

		case XIP_TYPE_MESH_REFERENCE:
		{
			// The top byte can name any of 256 slots, more than an archive has.
			const UINT nBuffer = fd.m_dataOffset >> 24;
			if (nBuffer >= MAX_MESHBUFFER)
			{
				TRACE(_T("\001CreateObjects: mesh ref %d out of range in %s\n"), (int)nBuffer, m_fileName);
				break;
			}
			CMeshRef* pRef = new CMeshRef;
			pRef->m_xipFile = this;
			pRef->m_meshBufferIndex = nBuffer;
			pRef->m_firstIndex = fd.m_dataOffset & 0x00ffffff;
			pRef->m_primitiveCount = fd.m_size;
			m_objects[i] = pRef;
			break;
		}

		case XIP_TYPE_INDEXBUFFER:
			// It lands in the vertex buffer's slot, so that count has to fit too.
			if (m_indexBufferCount >= MAX_MESHBUFFER || m_vertexBufferCount >= MAX_MESHBUFFER)
			{
				TRACE(_T("\001CreateObjects: too many index buffers in %s\n"), m_fileName);
				XipDiscard(m_file, fd.m_size);
				break;
			}
			ReadIndexBuffer(i, m_vertexBufferCount);
			m_indexBufferCount++;
			break;

		case XIP_TYPE_VERTEXBUFFER:
			if (m_vertexBufferCount >= MAX_MESHBUFFER)
			{
				TRACE(_T("\001CreateObjects: too many vertex buffers in %s\n"), m_fileName);
				XipDiscard(m_file, fd.m_size);
				break;
			}
			ReadVertexBuffer(i, m_vertexBufferCount);
			m_vertexBufferCount++;
			break;

		default:
		{
#ifdef _XBOX
			BYTE* pb = (BYTE*)TheseusAllocMemory(fd.m_size);
			m_file.Read(pb, fd.m_size);
#else
			BYTE* pb = (BYTE*)TheseusAllocMemory(fd.m_size + 1);
			m_file.Read(pb, fd.m_size);
			pb[fd.m_size] = 0;
#endif
			m_objects[i] = pb;
			break;
		}
		}
	}

	m_locked = false;
}

// bsearch hands the compare the key first and a directory entry second.
struct XipSearchKey
{
	const char* m_names;
	const char* m_name;
};

static int __cdecl XipCompareName(const void* pvKey, const void* pvEntry)
{
	const XipSearchKey* pKey = (const XipSearchKey*)pvKey;
	const FILENAME* pEntry = (const FILENAME*)pvEntry;
	return _stricmp(pKey->m_name, pKey->m_names + pEntry->m_nameOffset);
}

// CXipFile::Find(szURL)
// Reconstructed from 5960 retail XBE @ 0x0003e290 (compare @ 0x0003e270).
// Ghidra decompile, trimmed:
//     n = strlen(dirPath);
//     if (_strnicmp(url, dirPath, n) || url[n] != '\\') return -1;
//     key = { names, url + n + 1 };
//     hit = bsearch(&key, directory, nameCount, 4, compare);   // _stricmp
//     return hit ? hit->fileDataIndex : -1;
int CXipFile::Find(const char* szURL)
{
	const size_t cchDir = strlen(m_dirPath);
	if (_strnicmp(szURL, m_dirPath, cchDir) != 0 || szURL[cchDir] != '\\')
		return -1;

	XipSearchKey key;
	key.m_names = m_names;
	key.m_name = szURL + cchDir + 1;

	const FILENAME* pHit = (const FILENAME*)bsearch(&key, m_directory, m_header.m_nameCount,
		sizeof(FILENAME), XipCompareName);
	return pHit != NULL ? pHit->m_fileDataIndex : -1;
}

// CXipFile::FindObject(szURL, nType)
// Reconstructed from 5960 retail XBE @ 0x0003e320. Ghidra decompile, trimmed:
//     n = Find(url);
//     if (n == -1 || (type != -1 && filedata[n].type != type)) return 0;
//     if (type == 2) objects[n]->AddRef();     // inlined D3DResource_AddRef
//     return objects[n];
// A texture handed out is the caller's reference. One that failed to load is
// NULL, which the binary would AddRef anyway.
void* CXipFile::FindObject(const char* szURL, int nType)
{
	const int n = Find(szURL);
	if (n == -1)
		return NULL;
	if (nType != -1 && m_filedata[n].m_type != (DWORD)nType)
		return NULL;

	if (nType == XIP_TYPE_TEXTURE && m_objects[n] != NULL)
		((LPDIRECT3DTEXTURE8)m_objects[n])->AddRef();
	return m_objects[n];
}


// ===== Mesh buffers =========================================================

// CXipFile::ReadIndexBuffer(nFileIndex, nBuffer)
// Reconstructed from 5960 retail XBE @ 0x0003e5f0. Ghidra decompile, trimmed:
//     buf = &meshBuffers[nBuffer];
//     buf->ib = new index buffer of filedata[n].size bytes (0x0003d390);
//     read filedata[n].size bytes into buf->ib's data;
//     buf->indexCount = filedata[n].size >> 1;
// The binary builds the resource by hand and writes into it; Theseus goes
// through the device so desktop gets a bgfx buffer, locks to write, and
// skips the bytes if the device can't give it a buffer.
void CXipFile::ReadIndexBuffer(int nFileIndex, int nBuffer)
{
	CMeshBuffer& buf = m_meshBuffers[nBuffer];
	const int cb = m_filedata[nFileIndex].m_size;

	buf.m_indexBuffer = NULL;
	TheseusCreateIndexBuffer(cb, D3DUSAGE_DYNAMIC, D3DFMT_INDEX16, D3DPOOL_DEFAULT, &buf.m_indexBuffer);

	BYTE* pb = NULL;
	if (buf.m_indexBuffer == NULL || FAILED(buf.m_indexBuffer->Lock(0, cb, &pb, XIP_LOCK)))
	{
		XipDiscard(m_file, cb);
		return;
	}
	m_file.Read(pb, cb);
	buf.m_indexBuffer->Unlock();

	buf.m_indexCount = cb / (int)sizeof(WORD);
}

// CXipFile::ReadVertexBuffer(nFileIndex, nBuffer)
// Reconstructed from 5960 retail XBE @ 0x0003e650. Ghidra decompile, trimmed:
//     buf = &meshBuffers[nBuffer];
//     read count (4); read fvf (4);
//     buf->stride = (filedata[n].size - 8) / count;
//     buf->vb = new vertex buffer (0x0003d410);
//     wait for the GPU if it still reads buf->vb;
//     read size - 8 bytes into buf->vb's data;
//     buf->fvf = fvf; buf->vertexCount = count;
// Same device route as the index buffer. The zero count and short entry
// refusal is Theseus's: the binary divides by the count unchecked.
void CXipFile::ReadVertexBuffer(int nFileIndex, int nBuffer)
{
	CMeshBuffer& buf = m_meshBuffers[nBuffer];

	int nVerts;
	DWORD fvf;
	m_file.Read(&nVerts, sizeof(nVerts));
	m_file.Read(&fvf, sizeof(fvf));

	const int cbVerts = (int)m_filedata[nFileIndex].m_size - 8;
#ifdef _XBOX
	if (nVerts <= 0 || cbVerts <= 0)
	{
		TRACE(_T("\001ReadVertexBuffer: bad vertex count %d in %s\n"), nVerts, m_fileName);
		XipDiscard(m_file, cbVerts > 0 ? cbVerts : 0);
		return;
	}
#else
	if (nVerts <= 0 || cbVerts < 0)
	{
		TRACE("\001ReadVertexBuffer: bad vertex count %d\n", nVerts);
		return;
	}
#endif
	buf.m_vertexStride = cbVerts / nVerts;

	buf.m_vertexBuffer = NULL;
	TheseusCreateVertexBuffer(cbVerts, D3DUSAGE_DYNAMIC, fvf, D3DPOOL_DEFAULT, &buf.m_vertexBuffer);

	BYTE* pb = NULL;
	if (buf.m_vertexBuffer == NULL || FAILED(buf.m_vertexBuffer->Lock(0, 0, &pb, XIP_LOCK)))
	{
		XipDiscard(m_file, cbVerts);
		return;
	}
	m_file.Read(pb, cbVerts);
	buf.m_vertexBuffer->Unlock();

	buf.m_fvf = fvf;
	buf.m_vertexCount = nVerts;
}

// CXipFile::DeleteMeshBuffers()
// Reconstructed from 5960 retail XBE @ 0x0003e390. Ghidra decompile, trimmed:
//     for (i < 10) {
//         if (meshBuffers[i].vb) { meshBuffers[i].vb->Release(); meshBuffers[i].vb = 0; }
//         if (meshBuffers[i].ib) { meshBuffers[i].ib->Release(); meshBuffers[i].ib = 0; }
//     }
// Release is inlined in the binary. The counts stay, which is how
// IsUnloaded tells an evicted archive from one with no meshes.
void CXipFile::DeleteMeshBuffers()
{
	for (int i = 0; i < MAX_MESHBUFFER; i++)
	{
		CMeshBuffer& buf = m_meshBuffers[i];
		if (buf.m_vertexBuffer != NULL)
		{
			buf.m_vertexBuffer->Release();
			buf.m_vertexBuffer = NULL;
		}
		if (buf.m_indexBuffer != NULL)
		{
			buf.m_indexBuffer->Release();
			buf.m_indexBuffer = NULL;
		}
	}
}

// CleanupMeshCache()
// Reconstructed from 5960 retail XBE @ 0x0003e440. Ghidra decompile, trimmed
// (the binary unrolls the loop four times):
//     oldest = 0;
//     for (i < count) {
//         x = &pool[i];
//         if (x->locked || !x->loaded || !x->vbCount || !x->meshBuffers[0].vb) continue;
//         if (!oldest || x->cacheTime < oldest->cacheTime) oldest = x;
//     }
//     if (!oldest) return 0; oldest->DeleteMeshBuffers(); return 1;
// The out of memory path calls this to free the archive drawn longest ago.
bool CleanupMeshCache()
{
	// Count first: a slot under the count is always in the table.
	const int nXips = c_nXipFileCount;
	CXipFile** rgXips = c_rgXipFile;
	CXipFile* pOldest = NULL;
	for (int i = 0; i < nXips; i++)
	{
		CXipFile* pXip = rgXips[i];
		if (pXip->m_locked || !pXip->m_loaded)
			continue;
		if (pXip->m_vertexBufferCount == 0 || pXip->m_meshBuffers[0].m_vertexBuffer == NULL)
			continue;
		if (pOldest == NULL || pXip->m_cacheTime < pOldest->m_cacheTime)
			pOldest = pXip;
	}

	if (pOldest == NULL)
		return false;
	pOldest->DeleteMeshBuffers();
	return true;
}

// CXipFile::ReloadMeshBuffers()
// Reconstructed from 5960 retail XBE @ 0x0003e710. Ghidra decompile, trimmed:
//     locked = 1; ib = 0; vb = 0;
//     for (i < fileCount) {
//         if (type == 5) { file.Seek(dataStart + offset); ReadIndexBuffer(i, ib++); }
//         else if (type == 6) { file.Seek(dataStart + offset); ReadVertexBuffer(i, vb++); }
//     }
//     locked = 0; reloading = 0;
// Brings back what CleanupMeshCache freed, seeking to each buffer since the
// rest of the archive is still in memory. The CHECKs are Theseus's: an entry
// CreateObjects refused has no slot to come back to. So is skipping a buffer
// whose offset can't be reached.
void CXipFile::ReloadMeshBuffers()
{
	m_locked = true;

	int nIndexBuffer = 0;
	int nVertexBuffer = 0;
	for (int i = 0; i < m_header.m_fileCount; i++)
	{
		const FILEDATA& fd = m_filedata[i];
		if (fd.m_type == XIP_TYPE_INDEXBUFFER)
		{
			if (!CHECK(nIndexBuffer < m_indexBufferCount))
				continue;
			const int nSlot = nIndexBuffer++;
			if (m_file.Seek(m_header.m_dataStart + fd.m_dataOffset))
				ReadIndexBuffer(i, nSlot);
		}
		else if (fd.m_type == XIP_TYPE_VERTEXBUFFER)
		{
			if (!CHECK(nVertexBuffer < m_vertexBufferCount))
				continue;
			const int nSlot = nVertexBuffer++;
			if (m_file.Seek(m_header.m_dataStart + fd.m_dataOffset))
				ReadVertexBuffer(i, nSlot);
		}
	}

	m_locked = false;
	m_reloading = false;
}

// CXipFile::Reload()
// Reconstructed from 5960 retail XBE @ 0x0003e800. Ghidra decompile:
//     reloading = 1;
//     if (h = CreateThread(0, 0, StartLoadThread, this, 0, &id)) { CloseHandle(h); return; }
//     if (reloading) ReloadMeshBuffers(); else Load();
// The fallback is StartLoadThread inlined. Desktop reloads inline.
void CXipFile::Reload()
{
	m_reloading = true;
	RunLoader(StartLoadThread, this);
}

// Both inlined in 5960: CInline_Render @ 0x0004e5d0 tests
//     loaded && !reloading && vbCount != 0 && meshBuffers[0].vb == 0
// to decide its archive was evicted, and CInline_Advance reads reloading.
bool CXipFile::IsUnloaded() const
{
	return m_loaded && !m_reloading && m_vertexBufferCount != 0 && m_meshBuffers[0].m_vertexBuffer == NULL;
}

bool CXipFile::IsReloading() const
{
	return m_reloading;
}


// ===== CMeshRef =============================================================

// CMeshRef::Render(bSetFVF)
// Reconstructed from 5960 retail XBE @ 0x0003e850 (CMeshRef vtable @ 0x00024dbc,
// slot 0). Disassembly, summarized:
//     xip->cacheTime = now; buf = &xip->meshBuffers[index];
//     if (xip->reloading) return;
//     if (!buf->vb) { xip->Reload(); return; }
//     if (bSetFVF) SetVertexShader(GetFixedFunctionShader(buf->fvf));
//     if (count > 800 && !g_bEdgeAntialiasOverride) { EDGEANTIALIAS = 0; MULTISAMPLEANTIALIAS = 1; }
//     if (g_uMeshRef++ == 0) set the effect shader matrices for this frame;
//     SetStreamSource(0, buf->vb, buf->stride); SetIndices(buf->ib, 0);
//     DrawIndexedVertices(TRIANGLELIST, count * 3, indices + first);
// Theseus sets those shader matrices on every world change in shape_render
// instead (SetFalloffShaderFrameValues), and skips the AA swap on desktop
// where the shim has no edge AA. The index guard, the check that the slice
// fits its index buffer, and the draw counter are Theseus's.
void CMeshRef::Render(bool bSetFVF)
{
	if (m_meshBufferIndex >= m_xipFile->m_vertexBufferCount)
		return;

	m_xipFile->m_cacheTime = TheseusGetNow();
	CMeshBuffer& buf = m_xipFile->m_meshBuffers[m_meshBufferIndex];

	if (m_xipFile->IsReloading())
		return;
	if (buf.m_vertexBuffer == NULL)
	{
		m_xipFile->Reload();
		return;
	}

	if (buf.m_indexBuffer == NULL || m_firstIndex < 0 || m_primitiveCount < 0
		|| (long long)m_firstIndex + 3LL * m_primitiveCount > buf.m_indexCount)
		return;

	if (bSetFVF)
		TheseusSetVertexShader(GetFixedFunctionShader(buf.m_fvf));

#ifdef _XBOX
	if (m_primitiveCount > 800 && !g_bEdgeAntialiasOverride)
	{
		TheseusSetRenderState(D3DRS_EDGEANTIALIAS, FALSE);
		TheseusSetRenderState(D3DRS_MULTISAMPLEANTIALIAS, TRUE);
	}
#endif

	TheseusSetStreamSource(0, buf.m_vertexBuffer, buf.m_vertexStride);
	TheseusSetIndices(buf.m_indexBuffer, 0);
	Frame_Count(FC_DRAW_SCENE);
	TheseusDrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, buf.m_vertexCount, m_firstIndex, m_primitiveCount);
}

// CMeshRef::GetFVF()
// Reconstructed from 5960 retail XBE @ 0x0003ead0 (vtable slot 1):
//     return xip->meshBuffers[index].fvf;
// The index guard is Theseus's.
DWORD CMeshRef::GetFVF() const
{
	if (m_meshBufferIndex >= m_xipFile->m_vertexBufferCount)
		return 0;
	return m_xipFile->m_meshBuffers[m_meshBufferIndex].m_fvf;
}
