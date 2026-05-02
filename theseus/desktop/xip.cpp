// xip.cpp: desktop XIP archive loading, mesh buffer management, and
// file lookup. XIP is the dashboard's packed archive format
// containing XAP scripts, XBX textures, XM meshes, and vertex /
// index buffers. Counterpart to xbox/xip_archive.cpp.

#include "std.h"
#include "dashapp.h"
#include "theseus.h"
#include "file_util.h"
#include "xip.h"
#include "asset_loader.h"
#include "node.h"
#include "camera.h"
#include "xbx_texture.h"

extern CCamera theCamera;
extern unsigned int g_uMeshRef;
extern D3DXMATRIX g_matProjection;

extern int g_bEdgeAntialiasOverride;
inline void D3D_FreeNoncontiguousMemory(void *pMemory) { free(pMemory); }

// ============================================================================
// CFileBuffer -- buffered file I/O for XIP reading
// ============================================================================

#define FILE_BUFFER_SIZE 65536

void FileProtectionError()
{
	ALERT("XIP File Protection Error");
    __debugbreak();
}

CFileBuffer::CFileBuffer()
{
	m_hFile = INVALID_HANDLE_VALUE;
	m_pbBuffer = NULL;
	m_cbBuffer = 0;
	m_ibRead = 0;
    m_nBlkCur = 0;
}

void CFileBuffer::SetFile(const char* Name, HANDLE hFile)
{
	ASSERT(m_hFile == INVALID_HANDLE_VALUE);
	m_hFile = hFile;
	
}

CFileBuffer::~CFileBuffer()
{
	FreeBuffer();
}

void CFileBuffer::FreeBuffer()
{
	if (m_pbBuffer != NULL)
	{
		free(m_pbBuffer);
		m_pbBuffer = NULL;
	}
}

bool CFileBuffer::Seek(int nPos)
{
#if DBG
    static int nLastPos;
    nLastPos = nPos;
#endif

	int nBlock = nPos / FILE_BUFFER_SIZE;

	if (m_pbBuffer == NULL)
	{
        for (;;)
        {
            m_pbBuffer = (uint8_t*)calloc(1, FILE_BUFFER_SIZE);
            if (m_pbBuffer)
            {
                break;
            }
            NewFailed(FILE_BUFFER_SIZE);
        }
		m_nBlkCur = -1;
	}

	if (nBlock != m_nBlkCur)
	{
		VERIFY(SetFilePointer(m_hFile, nBlock * FILE_BUFFER_SIZE, NULL, FILE_BEGIN) != ~0);
        m_nBlkCur = nBlock;

		uint32_t dwRead;
		if (!ReadFile(m_hFile, m_pbBuffer, FILE_BUFFER_SIZE, LPDW(&dwRead), NULL) || dwRead == 0)
        {
			return false;
        }

        ++m_nBlkCur;

		m_cbBuffer = (int)dwRead;
	}

	m_ibRead = nPos % FILE_BUFFER_SIZE;

	return true;
}

int CFileBuffer::Read(void* pv, int cb)
{
	if (m_pbBuffer == NULL)
	{
        for (;;)
        {
    		m_pbBuffer = (uint8_t*)calloc(1, FILE_BUFFER_SIZE);
            if (m_pbBuffer)
            {
                break;
            }
            NewFailed(FILE_BUFFER_SIZE);
        }
	}

	int cbTotalRead = 0;
	int cbRead = 0;
	uint8_t* pb = (uint8_t*)pv;

	while (cb > 0)
	{
		if (m_ibRead < m_cbBuffer)
		{
			cbRead = m_cbBuffer - m_ibRead;
			if (cbRead > cb)
				cbRead = cb;

			memcpy(pb, m_pbBuffer + m_ibRead, cbRead);

			pb += cbRead;
			cb -= cbRead;
			ASSERT(cb >= 0);

			m_ibRead += cbRead;
			cbTotalRead += cbRead;
		}

		if (m_ibRead == m_cbBuffer)
		{
			uint32_t dwRead = 0;
			if (!ReadFile(m_hFile, m_pbBuffer, FILE_BUFFER_SIZE, LPDW(&dwRead), NULL))
            {
                ASSERT(FALSE && "Unable to read from XIP!");
                FileProtectionError();
                return -1;
            }

            if (dwRead == 0)
            {
                return -1;
            }
			
            ++m_nBlkCur;

			m_cbBuffer = (int)dwRead;
			m_ibRead = 0;
		}
	}

	return cbTotalRead;
}

// ============================================================================
// XIP File Management -- load, search, cache
// ============================================================================

CXipFile c_rgXipFile[20];
int c_nXipFileCount = 0;

CXipFile* LoadXIP(const char* szURL, bool bSync, bool isSkin )
{
	char szBuf [MAX_PATH];

    if (szURL[0] && szURL[1] != ':')
    {
		strcpy(szBuf, "Q:/Xips/");
    }
    else
    {
        szBuf[0] = 0;
    }

    strcat(szBuf, szURL);

	{
		char szDirPath [MAX_PATH];
		CleanFilePath(szDirPath, szBuf);
		char* pch = strrchr(szDirPath, '.');
		ASSERT(pch != NULL);
		*pch = 0;

		for (int i = 0; i < c_nXipFileCount; i += 1)
		{
			if (_stricmp(szDirPath, c_rgXipFile[i].m_szDirPath) == 0)
			{
				c_rgXipFile[i].m_cacheTime = TheseusGetNow();
				if (c_rgXipFile[i].m_bLoaded && c_rgXipFile[i].m_nVertexBufferCount > 0 && c_rgXipFile[i].m_rgMeshBuffer[0].m_pVertexBuffer == NULL)
					c_rgXipFile[i].ReloadMeshBuffers();

				return &c_rgXipFile[i];
			}
		}
	}

	ASSERT(c_nXipFileCount < countof(c_rgXipFile));
	CXipFile* pXipFile = &c_rgXipFile[c_nXipFileCount];

	if (!pXipFile->Open(szBuf))
    {
		return NULL;
    }

	c_nXipFileCount += 1;

	// Always load synchronously — async loading causes race conditions
	// with the script VM (namespace corruption when scripts run against partial scene state)
	pXipFile->Load();

	return pXipFile;
}

bool FindInXIPAndDetach(const char* szURL, BYTE*& pbContent, DWORD& cbContent)
{
	char szFilePath [MAX_PATH];
	CleanFilePath(szFilePath, szURL);

	for (int i = c_nXipFileCount - 1; i >= 0; i -= 1)
	{
		if (c_rgXipFile[i].m_bLocked || !c_rgXipFile[i].m_bLoaded)
			continue;

		int nObject = c_rgXipFile[i].Find(szFilePath);
		if (nObject >= 0 && c_rgXipFile[i].m_objects[nObject] != NULL)
		{
			pbContent = (uint8_t*)c_rgXipFile[i].m_objects[nObject];
			cbContent = c_rgXipFile[i].m_filedata[nObject].m_dwSize;
			c_rgXipFile[i].m_objects[nObject] = NULL;
			return true;
		}
	}

	return false;
}
void* FindObjectInXIP(const char* szURL, const char* szFilename, int nType/*=-1*/)
{
	char szFilePath [MAX_PATH];
	CleanFilePath(szFilePath, szURL);

	// For textures, check the skin directory first so skins can override XIP textures
	if (nType == XIP_TYPE_TEXTURE && g_sSkinDir != NULL && szFilename != NULL)
	{
		char szBuf [MAX_PATH];
		strcpy(szBuf, g_sSkinDir);
		strcat(szBuf, szFilename);

		char thePath[MAX_PATH];
		Ansi(thePath, szBuf, MAX_PATH);

		LPDIRECT3DTEXTURE8 lpTexture = NULL;
		HRESULT hr = D3DXCreateTextureFromFileA(TheseusGetD3DDev(), thePath, &lpTexture);
		if (hr == D3D_OK)
			return lpTexture;
	}

	// Search loaded XIPs (newest to oldest)
	for (int i = c_nXipFileCount - 1; i >= 0; i -= 1)
	{
		if (c_rgXipFile[i].m_bLocked || !c_rgXipFile[i].m_bLoaded)
			continue;

		// For XIP 0 (default), remap texture extension to .xbx
		if (i == 0 && nType == XIP_TYPE_TEXTURE && szFilename != NULL)
		{
			char szBuf [MAX_PATH];
			char szBufName [MAX_PATH];
			strcpy(szBufName, szFilename);
			char *pDot = strrchr(szBufName, '.');
			if (pDot != NULL)
				strcpy(pDot + 1, "xbx");
			strcpy(szBuf, g_sXipDir);
			strcat(szBuf, szBufName);
			CleanFilePath(szFilePath, szBuf);
		}

		void* pObject = c_rgXipFile[i].FindObject(szFilePath, nType);
		if (pObject != NULL)
			return pObject;
	}

	return NULL;
}

CXipFile::CXipFile()
{
	m_szXipFileName = NULL;
	m_szDirPath = NULL;
	m_bLoaded = false;
	m_bLocked = false;
	m_bReloading = false;

	memset(m_rgMeshBuffer, 0, sizeof (m_rgMeshBuffer));
	m_nVertexBufferCount = 0;
	m_nIndexBufferCount = 0;
}

CXipFile::~CXipFile()
{
	delete [] m_szXipFileName;
	m_szXipFileName = NULL;

	delete [] m_szDirPath;
	m_szDirPath = NULL;

	delete [] m_filedata;
	m_filedata = NULL;

	delete [] m_directory;
	m_directory = NULL;

	delete [] m_names;
	m_names = NULL;

	DeleteMeshBuffers();

	memset(m_rgMeshBuffer, 0, sizeof (m_rgMeshBuffer));
	m_nVertexBufferCount = 0;
	m_nIndexBufferCount = 0;
}

bool CXipFile::Open(const char* szXipFileName)
{
	m_szXipFileName = new char [strlen(szXipFileName) + 1];
	strcpy(m_szXipFileName, szXipFileName);

	char szDirPath [MAX_PATH];
	CleanFilePath(szDirPath, szXipFileName);

	char* pch = strrchr(szDirPath, '.');
	ASSERT(pch != NULL);
	*pch = 0;

	pch = strrchr(szDirPath, '\\');
	if (pch == NULL)
		pch = szDirPath;
	else
		pch += 1;

	if (_stricmp(pch, "default") == 0)
	{
		if (pch > szDirPath)
			pch -= 1;
		*pch = 0;
	}

	m_szDirPath = new char [strlen(szDirPath) + 1];
	strcpy(m_szDirPath, szDirPath);

	return true;
}

DWORD CALLBACK CXipFile::StartLoadThread(LPVOID pvContext)
{
	START_PROFILE();
	CXipFile *pThis = (CXipFile*)pvContext;

	if (pThis->m_bReloading)
		pThis->ReloadMeshBuffers();
	else
		VERIFY(pThis->Load());

	END_PROFILE();
	
	return 0;
}

bool CXipFile::Load()
{
	char szDirPath [MAX_PATH];
	Unicode(szDirPath, m_szDirPath, MAX_PATH);
	uint32_t ticks = GetTickCount ();

	HANDLE hFile;
	XCALCSIG_SIGNATURE sig;
	

	if ((hFile = TheseusCreateFile(m_szXipFileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_NO_BUFFERING, NULL)) == INVALID_HANDLE_VALUE)
	{
		TRACE("\001CXipFile::Open (%s) failed %d\n", m_szXipFileName, GetLastError());
		return false;
	}

	m_file.SetFile(m_szXipFileName, hFile);

	if (!m_file.Read(&m_header, sizeof (XIPHEADER)))
		return false;

	if (m_header.m_dwMagic != XIP_MAGIC)
		return false;

	m_filedata = new FILEDATA [m_header.m_wFileCount];
	if (!m_file.Read(m_filedata, m_header.m_wFileCount * sizeof (FILEDATA)))
		return false;

	m_directory = new FILENAME [m_header.m_wNameCount];
	if (!m_file.Read(m_directory, m_header.m_wNameCount * sizeof (FILENAME)))
		return false;

	{
		int cbNames = m_header.m_dwDataStart - (sizeof (XIPHEADER) + m_header.m_wFileCount * sizeof (FILEDATA) + m_header.m_wNameCount * sizeof (FILENAME));
		m_names = new char [cbNames];
		if (!m_file.Read(m_names, cbNames))
			return false;
	}

	CreateObjects();

	m_bLoaded = true;

	return true;
}

LPDIRECT3DTEXTURE8 ReadTexture(CFileBuffer& file, int nBytes)
{
	// Desktop: parse XBX texture data via SDL/OpenGL path
	uint8_t* pbContent = new uint8_t[nBytes];
	file.Read(pbContent, nBytes);
	IDirect3DTexture8* pResult = XBX_ParseTexture(pbContent, nBytes);
	delete [] pbContent;
	if (!pResult)
		TRACE("\001ReadTexture: XBX parse failed (%d bytes)\n", nBytes);
	return pResult;
}

void CXipFile::CreateObjects()
{
	m_bLocked = true;

	m_objects = new void* [m_header.m_wFileCount];
	memset(m_objects, 0, sizeof (void*) * m_header.m_wFileCount);

	for (unsigned int i = 0; i < m_header.m_wFileCount; i += 1)
	{
		switch (m_filedata[i].m_dwType)
		{
		default:
			// Desktop: null-terminate explicitly (malloc doesn't zero-fill like VirtualAlloc)
			m_objects[i] = TheseusAllocMemory(m_filedata[i].m_dwSize + sizeof(char));
			m_file.Read(m_objects[i], m_filedata[i].m_dwSize);
			((uint8_t*)m_objects[i])[m_filedata[i].m_dwSize] = 0;
			break;

		case XIP_TYPE_MESH:
			ASSERT(FALSE); // Obsolete...
//			m_objects[i] = CreateMesh(m_hFile);
			break;

		case XIP_TYPE_MESH_REFERENCE:
			{
				CMeshRef* pMeshRef = new CMeshRef;
				pMeshRef->m_xipFile = this;
				pMeshRef->m_meshBufferIndex = m_filedata[i].m_dwDataOffset >> 24;
				ASSERT(pMeshRef->m_meshBufferIndex < MAX_MESHBUFFER);
				pMeshRef->m_firstIndex = m_filedata[i].m_dwDataOffset & 0x00ffffff;
				pMeshRef->m_primitiveCount = m_filedata[i].m_dwSize;
				m_objects[i] = pMeshRef;
			}
			break;

		case XIP_TYPE_TEXTURE:
			m_objects[i] = ReadTexture(m_file, m_filedata[i].m_dwSize);

			break;

		case XIP_TYPE_INDEXBUFFER:
			ASSERT(m_nIndexBufferCount < MAX_MESHBUFFER);
			ReadIndexBuffer(i, m_nVertexBufferCount);
			m_nIndexBufferCount += 1;
			break;

		case XIP_TYPE_VERTEXBUFFER:
			ASSERT(m_nVertexBufferCount < MAX_MESHBUFFER);
			ReadVertexBuffer(i, m_nVertexBufferCount);
			m_nVertexBufferCount += 1;
			break;
		}
	}

	ASSERT(m_nVertexBufferCount == m_nIndexBufferCount);

	m_bLocked = false;
}

struct SEARCHXIP
{
	SEARCHXIP(CXipFile* pXipFile, const char* szFind)
	{
		m_names = pXipFile->m_names;
		m_szFind = szFind;
	}

	const char* m_names;
	const char* m_szFind;
};

static int __cdecl SearchXipCompare(const void *elem1, const void *elem2)
{
	const SEARCHXIP* pSearch = (const SEARCHXIP*)elem1;
	const FILENAME* pName = (const FILENAME*)elem2;
	return _stricmp(pSearch->m_szFind, pSearch->m_names + pName->m_wNameOffset);
}

int CXipFile::Find(const char* szURL)
{
	int cchDirPath = strlen(m_szDirPath);

    if (_strnicmp(szURL, m_szDirPath, cchDirPath) != 0)
        return -1;

    const char* szFile = szURL + cchDirPath;

    if (*szFile != '\\')
        return -1;

    szFile += 1;

	SEARCHXIP searchxip(this, szFile);
	FILENAME* pFileName = (FILENAME*)bsearch(&searchxip, m_directory, m_header.m_wNameCount, sizeof (FILENAME), SearchXipCompare);

	if (pFileName == NULL)
		return -1;

	return m_directory[(int)((uint8_t*)pFileName - (uint8_t*)m_directory) / sizeof (FILENAME)].m_wFileDataIndex;
}

void* CXipFile::FindObject(const char* szURL, int nType/*=-1*/)
{
	int nObject = Find(szURL);
    if (nObject == -1)
	{
        return NULL;
	}

    if (nType != -1 && m_filedata[nObject].m_dwType != (uint32_t)nType)
	{
        return NULL;
	}

    if(XIP_TYPE_TEXTURE == nType)
    {
        ((LPDIRECT3DTEXTURE8)m_objects[nObject])->AddRef();
    }

    return m_objects[nObject];
}

void CXipFile::DeleteMeshBuffers()
{
	ASSERT(!m_bLocked); 

	for (int i = 0; i < MAX_MESHBUFFER; i += 1)
	{
		if (m_rgMeshBuffer[i].m_pVertexBuffer != NULL)
		{
            m_rgMeshBuffer[i].m_pVertexBuffer->Release();
            m_rgMeshBuffer[i].m_pVertexBuffer = NULL;
		}

		if (m_rgMeshBuffer[i].m_pIndexBuffer != NULL)
		{
            m_rgMeshBuffer[i].m_pIndexBuffer->Release();
            m_rgMeshBuffer[i].m_pIndexBuffer = NULL;
		}
	}
}

bool CleanupMeshCache()
{
	CXipFile* pOldOne = NULL;

	for (int i = 0; i < c_nXipFileCount; i += 1)
	{
		if (c_rgXipFile[i].m_bLocked)
		{
			continue;
		}

		if (!c_rgXipFile[i].m_bLoaded)
		{
			continue;
		}

		if (c_rgXipFile[i].m_nVertexBufferCount == 0)
		{
			continue;
		}

		if (c_rgXipFile[i].m_rgMeshBuffer[0].m_pVertexBuffer == NULL)
		{
			continue;
		}

		if (pOldOne == NULL || c_rgXipFile[i].m_cacheTime < pOldOne->m_cacheTime)
			pOldOne = &c_rgXipFile[i];
	}

	if (pOldOne == NULL)
		return false;

	pOldOne->DeleteMeshBuffers();

	return true;
}

void CXipFile::ReadIndexBuffer(int nFileIndex, int nIndexBuffer)
{
	CMeshBuffer* pMeshBuffer = &m_rgMeshBuffer[nIndexBuffer];

	TheseusCreateIndexBuffer(m_filedata[nFileIndex].m_dwSize, D3DUSAGE_DYNAMIC, D3DFMT_INDEX16, D3DPOOL_DEFAULT, &pMeshBuffer->m_pIndexBuffer);

	uint8_t* indices;
	const DWORD dwLockFlags = D3DLOCK_DISCARD;
	VERIFYHR(pMeshBuffer->m_pIndexBuffer->Lock(0, m_filedata[nFileIndex].m_dwSize, (uint8_t**)&indices, dwLockFlags));
	m_file.Read(indices, m_filedata[nFileIndex].m_dwSize);
	VERIFYHR(pMeshBuffer->m_pIndexBuffer->Unlock());

	pMeshBuffer->m_nIndexCount = m_filedata[nFileIndex].m_dwSize /  sizeof (uint16_t);
}

void CXipFile::ReadVertexBuffer(int nFileIndex, int nVertexBuffer)
{
	CMeshBuffer* pMeshBuffer = &m_rgMeshBuffer[nVertexBuffer];

	int nVertexCount;
	DWORD fvf;

	m_file.Read(&nVertexCount, sizeof (int));
	m_file.Read(&fvf, sizeof (DWORD));
	
	pMeshBuffer->m_nVertexStride = (m_filedata[nFileIndex].m_dwSize - 8) / nVertexCount;

	TheseusCreateVertexBuffer(m_filedata[nFileIndex].m_dwSize - 8, D3DUSAGE_DYNAMIC, fvf, D3DPOOL_DEFAULT, &pMeshBuffer->m_pVertexBuffer);

	
	uint8_t* verts;
	const DWORD dwLockFlags = D3DLOCK_DISCARD;
	VERIFYHR(pMeshBuffer->m_pVertexBuffer->Lock(0, 0, &verts, dwLockFlags));
	m_file.Read(verts, m_filedata[nFileIndex].m_dwSize - 8);
	VERIFYHR(pMeshBuffer->m_pVertexBuffer->Unlock());

	pMeshBuffer->m_fvf = fvf;
	pMeshBuffer->m_nVertexCount = nVertexCount;
}

void CXipFile::ReloadMeshBuffers()
{
	ASSERT(!m_bLocked);
	m_bLocked = true;

	int nIndexBuffer = 0;
	int nVertexBuffer = 0;

	for (unsigned int i = 0; i < m_header.m_wFileCount; i += 1)
	{
		switch (m_filedata[i].m_dwType)
		{
		case XIP_TYPE_INDEXBUFFER:
			ASSERT(nIndexBuffer < m_nIndexBufferCount);
			m_file.Seek(m_header.m_dwDataStart + m_filedata[i].m_dwDataOffset);
			ReadIndexBuffer(i, nIndexBuffer);
			nIndexBuffer += 1;
			break;

		case XIP_TYPE_VERTEXBUFFER:
			ASSERT(nVertexBuffer < m_nVertexBufferCount);
			m_file.Seek(m_header.m_dwDataStart + m_filedata[i].m_dwDataOffset);
			ReadVertexBuffer(i, nVertexBuffer);
			nVertexBuffer += 1;
			break;
		}
	}

	ASSERT(nIndexBuffer == m_nIndexBufferCount);
	ASSERT(nVertexBuffer == m_nVertexBufferCount);

	m_bLocked = false;
	m_bReloading = false;
}

void CXipFile::Reload()
{
	ASSERT(!m_bReloading);

	m_bReloading = true;
	// Synchronous reload
	StartLoadThread(this);
}

bool CXipFile::IsUnloaded() const
{
	if (!m_bLoaded)
		return false;

	if (m_bReloading)
		return false;

	if (m_nVertexBufferCount == 0)
		return false;

	if (m_rgMeshBuffer[0].m_pVertexBuffer != NULL)
		return false;

	return true;
}

bool CXipFile::IsReloading() const
{
	return m_bReloading;
}

void CMeshRef::Render(bool bSetFVF/*=true*/)
{
	ASSERT(m_meshBufferIndex < m_xipFile->m_nVertexBufferCount);
	CMeshBuffer* pMeshBuffer = &m_xipFile->m_rgMeshBuffer[m_meshBufferIndex];

	m_xipFile->m_cacheTime = TheseusGetNow();

	if (m_xipFile->IsReloading())
		return;

	if (pMeshBuffer->m_pVertexBuffer == NULL)
	{
		m_xipFile->Reload();
		return;
	}

	if (bSetFVF)
		TheseusSetVertexShader(GetFixedFunctionShader(pMeshBuffer->m_fvf));

	if (m_primitiveCount > 800 && !g_bEdgeAntialiasOverride) {
		TheseusSetRenderState(D3DRS_EDGEANTIALIAS, FALSE);
		TheseusSetRenderState(D3DRS_MULTISAMPLEANTIALIAS, TRUE);
	}

	TheseusSetStreamSource(0, pMeshBuffer->m_pVertexBuffer, pMeshBuffer->m_nVertexStride);
	TheseusSetIndices(pMeshBuffer->m_pIndexBuffer, 0);

	TheseusDrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, pMeshBuffer->m_nVertexCount, m_firstIndex, m_primitiveCount);
}

DWORD CMeshRef::GetFVF() const
{
	ASSERT(m_meshBufferIndex < m_xipFile->m_nVertexBufferCount);
	return m_xipFile->m_rgMeshBuffer[m_meshBufferIndex].m_fvf;
}
