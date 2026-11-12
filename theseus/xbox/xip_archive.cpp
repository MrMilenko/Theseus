// xip_archive.cpp: XIP archive loader. Reads dashboard asset bundles
// (.xap scripts, .xbx textures, .xm meshes) into memory; supports
// XOR-key-decoded headers and per-entry decompression. Decompiled
// from the 5960 retail XBE; see docs/decomp/XIP.md.
#include "std.h"
#include "theseus.h"
#include "file_util.h"
#include "xip_archive.h"
#include "asset_loader.h"
#include "node.h"
#include "camera.h"

extern CCamera theCamera;
extern UINT g_uMeshRef;
extern D3DXMATRIX g_matProjection;
extern BOOL g_bEdgeAntialiasOverride;
extern "C" void WINAPI D3D_FreeNoncontiguousMemory(void* pMemory);

// =========================================================================
// Constants
// =========================================================================

static const int FILE_BUF_SIZE = 65536;  // 64KB read chunks

// =========================================================================
// File protection: halts on tampered XIP in retail, breaks in debug.
// =========================================================================

static void FileProtectionError()
{
#ifdef _DEBUG
	ALERT(_T("XIP File Protection Error"));
    __asm int 3;
#else
    HalReturnToFirmware(HalFatalErrorRebootRoutine);
#endif
}

// =========================================================================
// CFileBuffer: buffered 64KB block reader.
// =========================================================================

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

void CFileBuffer::SetFile(PCTSTR Name, HANDLE hFile)
{
	ASSERT(m_handle == INVALID_HANDLE_VALUE);
	m_handle = hFile;
}

void CFileBuffer::FreeBuffer()
{
	if (m_buffer != NULL)
	{
		VERIFY(VirtualFree(m_buffer, 0, MEM_RELEASE));
		m_buffer = NULL;
	}
}

// Allocate the read buffer, retrying on failure via NewFailed
static BYTE* AllocReadBuffer()
{
	for (;;)
	{
		BYTE* pb = (BYTE*)VirtualAlloc(NULL, FILE_BUF_SIZE,
			MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
		if (pb) return pb;
		NewFailed(FILE_BUF_SIZE);
	}
}

bool CFileBuffer::Seek(int nPos)
{
	int nBlock = nPos / FILE_BUF_SIZE;

	if (m_buffer == NULL)
	{
		m_buffer = AllocReadBuffer();
		m_currentBlock = -1;
	}

	if (nBlock != m_currentBlock)
	{
		VERIFY(SetFilePointer(m_handle, nBlock * FILE_BUF_SIZE, NULL, FILE_BEGIN) != ~0);
		m_currentBlock = nBlock;

		DWORD dwRead;
		if (!ReadFile(m_handle, m_buffer, FILE_BUF_SIZE, &dwRead, NULL) || dwRead == 0)
			return false;

		++m_currentBlock;
		m_bufferSize = (int)dwRead;
	}

	m_readIndex = nPos % FILE_BUF_SIZE;
	return true;
}

int CFileBuffer::Read(void* pv, int cb)
{
	if (m_buffer == NULL)
		m_buffer = AllocReadBuffer();

	int cbTotal = 0;
	BYTE* pb = (BYTE*)pv;

	while (cb > 0)
	{
		// Copy from current buffer
		if (m_readIndex < m_bufferSize)
		{
			int n = m_bufferSize - m_readIndex;
			if (n > cb) n = cb;

			CopyMemory(pb, m_buffer + m_readIndex, n);
			pb += n;
			cb -= n;
			ASSERT(cb >= 0);
			m_readIndex += n;
			cbTotal += n;
		}

		// Refill buffer when exhausted
		if (m_readIndex == m_bufferSize)
		{
			DWORD dwRead = 0;
			if (!ReadFile(m_handle, m_buffer, FILE_BUF_SIZE, &dwRead, NULL))
			{
				ASSERT(FALSE && "Unable to read from XIP!");
				FileProtectionError();
				return -1;
			}

			if (dwRead == 0)
				return -1;

			++m_currentBlock;
			m_bufferSize = (int)dwRead;
			m_readIndex = 0;
		}
	}

	return cbTotal;
}

// =========================================================================
// XIP file pool: up to 20 simultaneously loaded archives.
// =========================================================================

CXipFile c_rgXipFile[20];
int c_nXipFileCount = 0;

// =========================================================================
// LoadXIP: load or find a cached XIP archive.
// =========================================================================

CXipFile* LoadXIP(const TCHAR* szURL, bool bSync, bool isSkin)
{
	TCHAR szBuf[MAX_PATH];

	// Build full path: relative URLs get Q:/Xips/ prefix
	if (szURL[0] && szURL[1] != ':')
		_tcscpy(szBuf, _T("Q:/Xips/"));
	else
		szBuf[0] = 0;

	_tcscat(szBuf, szURL);

	// Check if already loaded (match by directory path)
	{
		char szDirPath[MAX_PATH];
		CleanFilePath(szDirPath, szBuf);
		char* pch = strrchr(szDirPath, '.');
		ASSERT(pch != NULL);
		*pch = 0;

		for (int i = 0; i < c_nXipFileCount; i++)
		{
			if (_stricmp(szDirPath, c_rgXipFile[i].m_dirPath) == 0)
			{
				c_rgXipFile[i].m_cacheTime = TheseusGetNow();

				// Reload mesh buffers if they were evicted
				if (c_rgXipFile[i].m_loaded &&
					c_rgXipFile[i].m_vertexBufferCount > 0 &&
					c_rgXipFile[i].m_meshBuffers[0].m_vertexBuffer == NULL)
				{
					c_rgXipFile[i].ReloadMeshBuffers();
				}

				return &c_rgXipFile[i];
			}
		}
	}

	if (c_nXipFileCount >= (int)countof(c_rgXipFile))
	{
		TRACE(_T("\001LoadXIP: XIP pool full (%d), cannot load %s\n"),
			(int)countof(c_rgXipFile), szBuf);
		return NULL;
	}

	CXipFile* pXip = &c_rgXipFile[c_nXipFileCount];

	if (!pXip->Open(szBuf))
		return NULL;

	c_nXipFileCount++;

	// Load synchronously or spawn a background thread
	if (bSync)
	{
		// A caller that waited for the load wants to know it failed. The slot
		// stays spent: whoever asked for a sync XIP is about to give up anyway.
		if (!pXip->Load())
		{
			TRACE(_T("\001LoadXIP: %s failed to load\n"), szBuf);
			return NULL;
		}
	}
	else
	{
		DWORD dwThreadID;
		HANDLE hThread = CreateThread(NULL, 0, CXipFile::StartLoadThread, pXip, 0, &dwThreadID);
		if (hThread)
			CloseHandle(hThread);
		else
			CXipFile::StartLoadThread(pXip);
	}

	return pXip;
}

// =========================================================================
// FindInXIPAndDetach: find content and detach ownership from the archive.
// =========================================================================

bool FindInXIPAndDetach(const TCHAR* szURL, BYTE*& pbContent, DWORD& cbContent)
{
	char szFilePath[MAX_PATH];
	CleanFilePath(szFilePath, szURL);

	// Search newest to oldest
	for (int i = c_nXipFileCount - 1; i >= 0; i--)
	{
		if (c_rgXipFile[i].m_locked || !c_rgXipFile[i].m_loaded)
			continue;

		int idx = c_rgXipFile[i].Find(szFilePath);
		if (idx >= 0 && c_rgXipFile[i].m_objects[idx] != NULL)
		{
			pbContent = (BYTE*)c_rgXipFile[i].m_objects[idx];
			cbContent = c_rgXipFile[i].m_filedata[idx].m_size;
			c_rgXipFile[i].m_objects[idx] = NULL;  // Transfer ownership
			return true;
		}
	}

	return false;
}

// =========================================================================
// FindObjectInXIP: find an object by path, with skin override for textures
// =========================================================================

void* FindObjectInXIP(const TCHAR* szURL, const TCHAR* szFilename, int nType)
{
	char szFilePath[MAX_PATH];
	CleanFilePath(szFilePath, szURL);

	// Skin directory override: check for loose texture files first
	if (nType == XIP_TYPE_TEXTURE && TheseusGetSkinDir() != NULL && szFilename != NULL)
	{
		// Skin dir plus an archive-supplied filename, either of which can be
		// long enough on its own.
		TCHAR szSkinPath[MAX_PATH];
		_sntprintf(szSkinPath, countof(szSkinPath) - 1, _T("%s%s"),
			TheseusGetSkinDir(), szFilename);
		szSkinPath[countof(szSkinPath) - 1] = 0;

		char ansiPath[MAX_PATH];
		Ansi(ansiPath, szSkinPath, MAX_PATH);

		LPDIRECT3DTEXTURE8 lpTex = NULL;
		if (D3DXCreateTextureFromFileA(TheseusGetD3DDev(), ansiPath, &lpTex) == D3D_OK)
			return lpTex;
	}

	// Search loaded archives newest to oldest
	for (int i = c_nXipFileCount - 1; i >= 0; i--)
	{
		if (c_rgXipFile[i].m_locked || !c_rgXipFile[i].m_loaded)
			continue;

		// Default XIP (index 0): remap texture extension to .xbx
		if (i == 0 && nType == XIP_TYPE_TEXTURE && szFilename != NULL)
		{
			TCHAR szRemapped[MAX_PATH];
			_tcscpy(szRemapped, szFilename);
			TCHAR* pDot = _tcsrchr(szRemapped, '.');
			if (pDot != NULL)
				_tcscpy(pDot + 1, _T("xbx"));

			TCHAR szFull[MAX_PATH];
			_tcscpy(szFull, TheseusGetXipDir());
			_tcscat(szFull, szRemapped);
			CleanFilePath(szFilePath, szFull);
		}

		void* pObj = c_rgXipFile[i].FindObject(szFilePath, nType);
		if (pObj != NULL)
			return pObj;
	}

	return NULL;
}

// =========================================================================
// CXipFile
// =========================================================================

CXipFile::CXipFile()
{
	m_fileName = NULL;
	m_dirPath = NULL;
	m_loaded = false;
	m_locked = false;
	m_reloading = false;
	m_vertexBufferCount = 0;
	m_indexBufferCount = 0;
	ZeroMemory(m_meshBuffers, sizeof(m_meshBuffers));
}

CXipFile::~CXipFile()
{
	delete[] m_fileName;
	delete[] m_dirPath;
	delete[] m_filedata;
	delete[] m_directory;
	delete[] m_names;
	DeleteMeshBuffers();
}

bool CXipFile::Open(const TCHAR* szXipFileName)
{
	// Store the XIP filename
	m_fileName = new TCHAR[_tcslen(szXipFileName) + 1];
	_tcscpy(m_fileName, szXipFileName);

	// Derive the directory path (used as cache key)
	// Strip extension, and if the base name is "default", use the parent dir
	char szDir[MAX_PATH];
	CleanFilePath(szDir, szXipFileName);

	char* pDot = strrchr(szDir, '.');
	ASSERT(pDot != NULL);
	*pDot = 0;

	char* pSlash = strrchr(szDir, '\\');
	char* pBase = (pSlash != NULL) ? pSlash + 1 : szDir;

	if (_stricmp(pBase, "default") == 0)
	{
		if (pBase > szDir)
			pBase--;
		*pBase = 0;
	}

	m_dirPath = new char[strlen(szDir) + 1];
	strcpy(m_dirPath, szDir);

	return true;
}

DWORD CALLBACK CXipFile::StartLoadThread(LPVOID pvContext)
{
	START_PROFILE();
	CXipFile* pThis = (CXipFile*)pvContext;

	if (pThis->m_reloading)
		pThis->ReloadMeshBuffers();
	else
		VERIFY(pThis->Load());

	END_PROFILE();
	return 0;
}

// Read returns the byte count, or -1 when the file is short or unreadable.
// Callers want "did I get all of it", so ask that question directly.
static bool ReadExact(CFileBuffer& file, void* pv, int cb)
{
	return cb >= 0 && file.Read(pv, cb) == cb;
}

// CreateObjects walks the data section in order with no seeking, so anything
// we decline to store still has to come off the stream or every entry after
// it reads from the wrong place.
static void DiscardBytes(CFileBuffer& file, int cb)
{
	BYTE rgb[512];
	while (cb > 0)
	{
		int n = cb < (int)sizeof(rgb) ? cb : (int)sizeof(rgb);
		if (file.Read(rgb, n) != n)
			return;
		cb -= n;
	}
}

// A XIP that fails to load is never read again, so let go of its handle.
static void CloseXipHandle(CFileBuffer& file)
{
	if (file.m_handle != INVALID_HANDLE_VALUE)
	{
		CloseHandle(file.m_handle);
		file.m_handle = INVALID_HANDLE_VALUE;
	}
}

bool CXipFile::Load()
{
#ifdef _DEBUG
	TCHAR szDirPath [MAX_PATH];
	Unicode(szDirPath, m_dirPath, MAX_PATH);
	DWORD ticks = GetTickCount ();
#endif

	HANDLE hFile = TheseusCreateFile(m_fileName, GENERIC_READ, FILE_SHARE_READ,
		NULL, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_NO_BUFFERING,
		NULL);

	if (hFile == INVALID_HANDLE_VALUE)
	{
		TRACE(_T("\001CXipFile::Open (%s) failed %d\n"), m_fileName, GetLastError());
		return false;
	}

	m_file.SetFile(m_fileName, hFile);

	// Read and validate header
	if (!ReadExact(m_file, &m_header, sizeof(XIPHEADER)))
	{
		CloseXipHandle(m_file);
		return false;
	}

	if (m_header.m_magic != XIP_MAGIC)
	{
#ifdef _DEBUG
		TRACE(_T("\001CXipFile::Open (%s.xip) not a valid XIP file!\n"), szDirPath);
#endif
		CloseXipHandle(m_file);
		return false;
	}

	// The tables sit between the header and the data section, so the name pool
	// is whatever is left over. A corrupt dataStart makes that negative, and
	// every real archive ends exactly where the header says it does.
	const int cbTables = (int)(m_header.m_fileCount * sizeof(FILEDATA))
		+ (int)(m_header.m_nameCount * sizeof(FILENAME));
	const int cbNames = (int)m_header.m_dataStart - (int)sizeof(XIPHEADER) - cbTables;

	if (cbNames <= 0
		|| GetFileSize(m_file.m_handle, NULL) != m_header.m_dataStart + m_header.m_dataSize)
	{
		TRACE(_T("\001CXipFile::Load (%s) header does not describe this file\n"), m_fileName);
		CloseXipHandle(m_file);
		return false;
	}

	// Read file table, directory, and name pool
	m_filedata = new FILEDATA[m_header.m_fileCount];
	if (!ReadExact(m_file, m_filedata, (int)(m_header.m_fileCount * sizeof(FILEDATA))))
	{
		CloseXipHandle(m_file);
		return false;
	}

	m_directory = new FILENAME[m_header.m_nameCount];
	if (!ReadExact(m_file, m_directory, (int)(m_header.m_nameCount * sizeof(FILENAME))))
	{
		CloseXipHandle(m_file);
		return false;
	}

	m_names = new char[cbNames];
	if (!ReadExact(m_file, m_names, cbNames))
	{
		CloseXipHandle(m_file);
		return false;
	}

	CreateObjects();
	m_loaded = true;
	return true;
}

// =========================================================================
// Texture loading: unwrap XPR container and create D3D texture
// =========================================================================

static LPDIRECT3DTEXTURE8 ReadTextureFromXPR(CFileBuffer& file, int nBytes)
{
	// Everything below indexes pbRaw using sizes the archive supplies, so the
	// entry has to be big enough to hold a header and a texture struct before
	// any of them can be trusted.
	if (nBytes < (int)(sizeof(XPR_HEADER) + sizeof(IDirect3DTexture8)))
	{
		TRACE(_T("\001ReadTextureFromXPR: entry too small (%d bytes)\n"), nBytes);
		DiscardBytes(file, nBytes);
		return NULL;
	}

	BYTE* pbRaw = new BYTE[nBytes];
	if (!ReadExact(file, pbRaw, nBytes))
	{
		delete[] pbRaw;
		return NULL;
	}

	const XPR_HEADER* pxpr = (const XPR_HEADER*)pbRaw;
	if (pxpr->dwMagic != XPR_MAGIC_VALUE)
	{
		ALERT(_T("Unable to load XBX file for scene!"));
		delete[] pbRaw;
		return NULL;
	}

	// dwHeaderSize is where the pixels start and dwTotalSize is where they
	// end; both have to land inside the bytes we actually read.
	if (pxpr->dwHeaderSize < sizeof(XPR_HEADER)
		|| pxpr->dwTotalSize < pxpr->dwHeaderSize
		|| pxpr->dwTotalSize > (DWORD)nBytes)
	{
		TRACE(_T("\001ReadTextureFromXPR: header describes %d..%d of %d bytes\n"),
			(int)pxpr->dwHeaderSize, (int)pxpr->dwTotalSize, nBytes);
		delete[] pbRaw;
		return NULL;
	}

	int cbData = pxpr->dwTotalSize - pxpr->dwHeaderSize;
	if (cbData <= 0)
	{
		TRACE(_T("\001ReadTextureFromXPR: no pixel data\n"));
		delete[] pbRaw;
		return NULL;
	}

	// Allocate the D3D texture header in system memory
	IDirect3DTexture8* pTex = (IDirect3DTexture8*)TheseusD3D_AllocNoncontiguousMemory(sizeof(D3DBaseTexture));
	if (pTex == NULL)
	{
		TRACE(_T("Not enough memory to load XBX image file!\n"));
		delete[] pbRaw;
		return NULL;
	}

	CopyMemory(pTex, pbRaw + sizeof(XPR_HEADER), sizeof(IDirect3DTexture8));

	// Allocate contiguous memory for the texture data (GPU-accessible)
	BYTE* pbTexData = (BYTE*)TheseusD3D_AllocContiguousMemory(cbData, D3DTEXTURE_ALIGNMENT);
	if (pbTexData == NULL)
	{
		TRACE(_T("Not enough memory to load XBX image file!\n"));
		D3D_FreeNoncontiguousMemory(pTex);
		delete[] pbRaw;
		return NULL;
	}

	CopyMemory(pbTexData, pbRaw + pxpr->dwHeaderSize, cbData);
	D3D_CopyContiguousMemoryToVideo(pbTexData);

	pTex->Data = NULL;
	pTex->Register(pbTexData);
	pTex->Common |= D3DCOMMON_D3DCREATED;

	delete[] pbRaw;
	return pTex;
}

// =========================================================================
// CreateObjects: load all file entries from the archive
// =========================================================================

void CXipFile::CreateObjects()
{
	m_locked = true;

	m_objects = new void*[m_header.m_fileCount];
	ZeroMemory(m_objects, sizeof(void*) * m_header.m_fileCount);

	for (UINT i = 0; i < m_header.m_fileCount; i++)
	{
		switch (m_filedata[i].m_type)
		{
		case XIP_TYPE_TEXTURE:
			m_objects[i] = ReadTextureFromXPR(m_file, m_filedata[i].m_size);
			break;

		case XIP_TYPE_MESH_REFERENCE:
			{
				// Top byte of the offset, so the archive can name any slot up
				// to 255 whether or not we have that many.
				const UINT iBuffer = m_filedata[i].m_dataOffset >> 24;
				if (iBuffer >= MAX_MESHBUFFER)
				{
					TRACE(_T("\001CreateObjects: mesh ref %d out of range in %s\n"),
						(int)iBuffer, m_fileName);
					break;
				}

				CMeshRef* pRef = new CMeshRef;
				pRef->m_xipFile = this;
				pRef->m_meshBufferIndex = iBuffer;
				pRef->m_firstIndex = m_filedata[i].m_dataOffset & 0x00ffffff;
				pRef->m_primitiveCount = m_filedata[i].m_size;
				m_objects[i] = pRef;
			}
			break;

		case XIP_TYPE_INDEXBUFFER:
			// Written into its vertex buffer's slot, so that count has to fit too.
			if (m_indexBufferCount >= MAX_MESHBUFFER || m_vertexBufferCount >= MAX_MESHBUFFER)
			{
				TRACE(_T("\001CreateObjects: too many index buffers in %s\n"), m_fileName);
				DiscardBytes(m_file, m_filedata[i].m_size);
				break;
			}
			ReadIndexBuffer(i, m_vertexBufferCount);
			m_indexBufferCount++;
			break;

		case XIP_TYPE_VERTEXBUFFER:
			if (m_vertexBufferCount >= MAX_MESHBUFFER)
			{
				TRACE(_T("\001CreateObjects: too many vertex buffers in %s\n"), m_fileName);
				DiscardBytes(m_file, m_filedata[i].m_size);
				break;
			}
			ReadVertexBuffer(i, m_vertexBufferCount);
			m_vertexBufferCount++;
			break;

		case XIP_TYPE_MESH:
			ASSERT(FALSE); // Obsolete...
			break;

		default:
			// Generic data: allocate and read raw bytes
			m_objects[i] = TheseusAllocMemory(m_filedata[i].m_size);
			m_file.Read(m_objects[i], m_filedata[i].m_size);
			break;
		}
	}

	ASSERT(m_vertexBufferCount == m_indexBufferCount);

	m_locked = false;
}

// =========================================================================
// Directory search: binary search by name within the archive
// =========================================================================

struct SEARCHXIP
{
	const char* m_names;
	const char* m_szFind;

	SEARCHXIP(CXipFile* pXip, const char* szFind)
		: m_names(pXip->m_names), m_szFind(szFind) {}
};

static int __cdecl SearchXipCompare(const void* elem1, const void* elem2)
{
	const SEARCHXIP* pSearch = (const SEARCHXIP*)elem1;
	const FILENAME* pEntry = (const FILENAME*)elem2;
	return _stricmp(pSearch->m_szFind, pSearch->m_names + pEntry->m_nameOffset);
}

int CXipFile::Find(const char* szURL)
{
	int cchDir = strlen(m_dirPath);

	if (_strnicmp(szURL, m_dirPath, cchDir) != 0)
		return -1;

	const char* szFile = szURL + cchDir;
	if (*szFile != '\\')
		return -1;
	szFile++;

	SEARCHXIP ctx(this, szFile);
	FILENAME* pHit = (FILENAME*)bsearch(&ctx, m_directory,
		m_header.m_nameCount, sizeof(FILENAME), SearchXipCompare);

	if (pHit == NULL)
		return -1;

	return pHit->m_fileDataIndex;
}

void* CXipFile::FindObject(const char* szURL, int nType)
{
	int idx = Find(szURL);
	if (idx < 0)
		return NULL;

	if (nType != -1 && m_filedata[idx].m_type != (DWORD)nType)
		return NULL;

	if (nType == XIP_TYPE_TEXTURE)
		((LPDIRECT3DTEXTURE8)m_objects[idx])->AddRef();

	return m_objects[idx];
}

// =========================================================================
// Mesh buffer management
// =========================================================================

void CXipFile::ReadIndexBuffer(int nFileIndex, int nBuffer)
{
	CMeshBuffer* pBuf = &m_meshBuffers[nBuffer];

	TheseusCreateIndexBuffer(m_filedata[nFileIndex].m_size,
		D3DUSAGE_DYNAMIC, D3DFMT_INDEX16, D3DPOOL_DEFAULT, &pBuf->m_indexBuffer);

	BYTE* pData;
	const DWORD dwLock = D3DLOCK_DISCARD | D3DLOCK_NOFLUSH;
	VERIFYHR(pBuf->m_indexBuffer->Lock(0, m_filedata[nFileIndex].m_size, (BYTE**)&pData, dwLock));
	m_file.Read(pData, m_filedata[nFileIndex].m_size);
	VERIFYHR(pBuf->m_indexBuffer->Unlock());

	pBuf->m_indexCount = m_filedata[nFileIndex].m_size / sizeof(WORD);
}

void CXipFile::ReadVertexBuffer(int nFileIndex, int nBuffer)
{
	CMeshBuffer* pBuf = &m_meshBuffers[nBuffer];

	// Vertex buffer is prefixed with vertex count and FVF
	int nVerts;
	DWORD fvf;
	m_file.Read(&nVerts, sizeof(int));
	m_file.Read(&fvf, sizeof(DWORD));

	int cbVerts = m_filedata[nFileIndex].m_size - 8;
	if (nVerts <= 0 || cbVerts <= 0)
	{
		TRACE(_T("\001ReadVertexBuffer: bad vertex count %d in %s\n"), nVerts, m_fileName);
		DiscardBytes(m_file, cbVerts > 0 ? cbVerts : 0);
		return;
	}
	pBuf->m_vertexStride = cbVerts / nVerts;

	TheseusCreateVertexBuffer(cbVerts, D3DUSAGE_DYNAMIC, fvf, D3DPOOL_DEFAULT, &pBuf->m_vertexBuffer);

	BYTE* pData;
	const DWORD dwLock = D3DLOCK_DISCARD | D3DLOCK_NOFLUSH;
	VERIFYHR(pBuf->m_vertexBuffer->Lock(0, 0, &pData, dwLock));
	m_file.Read(pData, cbVerts);
	VERIFYHR(pBuf->m_vertexBuffer->Unlock());

	pBuf->m_fvf = fvf;
	pBuf->m_vertexCount = nVerts;
}

void CXipFile::DeleteMeshBuffers()
{
	ASSERT(!m_locked);

	int i;
	for (i = 0; i < MAX_MESHBUFFER; i++)
	{
		if (m_meshBuffers[i].m_vertexBuffer != NULL)
		{
			m_meshBuffers[i].m_vertexBuffer->Release();
			m_meshBuffers[i].m_vertexBuffer = NULL;
		}
		if (m_meshBuffers[i].m_indexBuffer != NULL)
		{
			m_meshBuffers[i].m_indexBuffer->Release();
			m_meshBuffers[i].m_indexBuffer = NULL;
		}
	}
}

void CXipFile::ReloadMeshBuffers()
{
	ASSERT(!m_locked);
	m_locked = true;

	int nIB = 0, nVB = 0;

	UINT i;
	for (i = 0; i < m_header.m_fileCount; i++)
	{
		if (m_filedata[i].m_type == XIP_TYPE_INDEXBUFFER)
		{
			if (!CHECK(nIB < m_indexBufferCount))
				continue;
			m_file.Seek(m_header.m_dataStart + m_filedata[i].m_dataOffset);
			ReadIndexBuffer(i, nIB++);
		}
		else if (m_filedata[i].m_type == XIP_TYPE_VERTEXBUFFER)
		{
			if (!CHECK(nVB < m_vertexBufferCount))
				continue;
			m_file.Seek(m_header.m_dataStart + m_filedata[i].m_dataOffset);
			ReadVertexBuffer(i, nVB++);
		}
	}

	ASSERT(nIB == m_indexBufferCount);
	ASSERT(nVB == m_vertexBufferCount);

	m_locked = false;
	m_reloading = false;
}

// =========================================================================
// Mesh cache eviction: free the oldest unused mesh buffers
// =========================================================================

bool CleanupMeshCache()
{
	CXipFile* pOldest = NULL;

	for (int i = 0; i < c_nXipFileCount; i++)
	{
		CXipFile* pXip = &c_rgXipFile[i];

		if (pXip->m_locked || !pXip->m_loaded)
			continue;
		if (pXip->m_vertexBufferCount == 0)
			continue;
		if (pXip->m_meshBuffers[0].m_vertexBuffer == NULL)
			continue;

		if (pOldest == NULL || pXip->m_cacheTime < pOldest->m_cacheTime)
			pOldest = pXip;
	}

	if (pOldest == NULL)
		return false;

	pOldest->DeleteMeshBuffers();
	return true;
}

// =========================================================================
// Reload / state queries
// =========================================================================

void CXipFile::Reload()
{
	ASSERT(!m_reloading);

	m_reloading = true;

	DWORD dwThreadID;
	HANDLE hThread = CreateThread(NULL, 0, CXipFile::StartLoadThread, this, 0, &dwThreadID);
	if (hThread)
		CloseHandle(hThread);
	else
		StartLoadThread(this);
}

bool CXipFile::IsUnloaded() const
{
	if (!m_loaded || m_reloading)
		return false;
	if (m_vertexBufferCount == 0)
		return false;
	return (m_meshBuffers[0].m_vertexBuffer == NULL);
}

bool CXipFile::IsReloading() const
{
	return m_reloading;
}

// =========================================================================
// CMeshRef: render a mesh reference from the shared buffers
// =========================================================================

void CMeshRef::Render(bool bSetFVF)
{
	if (m_meshBufferIndex >= m_xipFile->m_vertexBufferCount)
		return;
	CMeshBuffer* pBuf = &m_xipFile->m_meshBuffers[m_meshBufferIndex];

	m_xipFile->m_cacheTime = TheseusGetNow();

	if (m_xipFile->IsReloading())
		return;

	// Trigger lazy reload if buffers were evicted
	if (pBuf->m_vertexBuffer == NULL)
	{
		m_xipFile->Reload();
		return;
	}

	if (bSetFVF)
		TheseusSetVertexShader(GetFixedFunctionShader(pBuf->m_fvf));

	// Disable edge AA for large meshes (performance)
	if (m_primitiveCount > 800 && !g_bEdgeAntialiasOverride)
	{
		TheseusSetRenderState(D3DRS_EDGEANTIALIAS, FALSE);
		TheseusSetRenderState(D3DRS_MULTISAMPLEANTIALIAS, TRUE);
	}

	TheseusSetStreamSource(0, pBuf->m_vertexBuffer, pBuf->m_vertexStride);
	TheseusSetIndices(pBuf->m_indexBuffer, 0);
	Frame_Count(FC_DRAW_SCENE);
	TheseusDrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, pBuf->m_vertexCount, m_firstIndex, m_primitiveCount);
}

DWORD CMeshRef::GetFVF() const
{
	if (m_meshBufferIndex >= m_xipFile->m_vertexBufferCount)
		return 0;
	return m_xipFile->m_meshBuffers[m_meshBufferIndex].m_fvf;
}
