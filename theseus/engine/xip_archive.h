// xip_archive.h: the XIP archive, one file holding a scene's scripts,
// textures and meshes. The reader is render/xip_archive.cpp; the format and
// what the 5960 dash does with it are in docs/decomp/XIP.md.

#pragma once

// ===== On disk ==============================================================
// [XIPHEADER][FILEDATA x fileCount][FILENAME x nameCount][names][data]

#define XIP_MAGIC 0x30504958  // "XIP0"
#define XIP_DIGEST_LENGTH 20

struct XIPHEADER
{
	DWORD m_magic;
	DWORD m_dataStart;   // file offset of the data section
	WORD  m_fileCount;
	WORD  m_nameCount;
	DWORD m_dataSize;    // dataStart + dataSize is the file size
};

struct FILEDATA
{
	DWORD m_dataOffset;  // from dataStart; a mesh reference packs (buffer << 24) | first index
	DWORD m_size;        // bytes; a mesh reference keeps its primitive count here
	DWORD m_type;        // XIP_TYPE_*
	DWORD m_timestamp;
};

// Sorted by name, so lookups are a bsearch.
struct FILENAME
{
	WORD m_fileDataIndex;
	WORD m_nameOffset;   // into the name pool
};

#define XIP_TYPE_GENERIC        0
#define XIP_TYPE_MESH           1  // nothing writes these anymore
#define XIP_TYPE_TEXTURE        2  // an XPR0 texture (.xbx)
#define XIP_TYPE_WAVE           3
#define XIP_TYPE_MESH_REFERENCE 4  // a slice of one of the archive's mesh buffers
#define XIP_TYPE_INDEXBUFFER    5  // WORD indices
#define XIP_TYPE_VERTEXBUFFER   6  // vertex count, FVF, then the vertices

#ifndef BUILD_XIPSIGN

// 5960 gives every archive ten buffer pairs. Desktop allows more for archives
// built outside the dash; the mesh reference byte still caps it at 256.
#ifdef _XBOX
#define MAX_MESHBUFFER 10
#else
#define MAX_MESHBUFFER 512
#endif

// One vertex buffer and the index buffer that goes with it. Every CMeshRef in
// the archive draws a slice of one of these.
struct CMeshBuffer
{
	DWORD m_fvf;
	int   m_vertexStride;
	int   m_vertexCount;
	int   m_indexCount;
	IDirect3DVertexBuffer8* m_vertexBuffer;
	IDirect3DIndexBuffer8*  m_indexBuffer;
};

// Reads an archive 64 KB at a time. The dash opens XIPs unbuffered, which
// only allows whole aligned blocks, so small reads come out of this buffer.
class CFileBuffer
{
public:
	CFileBuffer();
	~CFileBuffer();

	void SetFile(const TCHAR* Name, HANDLE hFile);
	int  Read(void* pv, int cb);
	bool Seek(int nPos);
	void FreeBuffer();

	HANDLE m_handle;
	BYTE*  m_buffer;
	int    m_bufferSize;    // what the last block read gave
	int    m_readIndex;
	int    m_currentBlock;  // one past the block in the buffer
};

class CXipFile
{
public:
	CXipFile();
	~CXipFile();

	bool  Open(const TCHAR* szXipFileName);
	void  Reload();
	int   Find(const char* szURL);
	void* FindObject(const char* szURL, int nType = -1);

	char*   m_dirPath;   // lookup key: the path minus ".xip"; default.xip answers for its folder
	TCHAR*  m_fileName;

	XIPHEADER m_header;
	FILEDATA* m_filedata;
	FILENAME* m_directory;
	char*     m_names;

	void**    m_objects;
	bool      m_loaded;

	CMeshBuffer m_meshBuffers[MAX_MESHBUFFER];
	int m_vertexBufferCount;
	int m_indexBufferCount;

	void DeleteMeshBuffers();

	XTIME m_cacheTime;   // last use, so CleanupMeshCache can pick the oldest
	bool  m_locked;      // set while a load or reload fills the buffers

	bool IsUnloaded() const;
	bool IsReloading() const;
	inline bool IsReady() const { return m_loaded && !m_reloading; }

protected:
	CFileBuffer m_file;
	bool m_reloading;

	bool Load();
	void CreateObjects();
	void ReloadMeshBuffers();
	void ReadIndexBuffer(int nFileIndex, int nBuffer);
	void ReadVertexBuffer(int nFileIndex, int nBuffer);

	static DWORD CALLBACK StartLoadThread(LPVOID pvContext);
	friend CXipFile* LoadXIP(const TCHAR* szURL, bool bSync, bool isSkin);
};

// CMeshRef lives in asset_loader.h and points back into m_meshBuffers.

extern CXipFile* LoadXIP(const TCHAR* szURL, bool bSync = false, bool isSkin = false);
extern bool FindInXIPAndDetach(const TCHAR* szURL, BYTE*& pbContent, DWORD& cbContent);
extern void* FindObjectInXIP(const TCHAR* szURL, const TCHAR* szFilename, int nType = -1);
extern bool CleanupMeshCache();

#endif // BUILD_XIPSIGN
