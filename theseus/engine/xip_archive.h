// xip_archive.h: XIP archive format definitions and runtime loader.
//
// Companion to xbox/xip_archive.cpp. See docs/decomp/XIP.md for the
// on-disk format reverse-engineering notes.

#pragma once

// =========================================================================
// XIP file format (on-disk structures)
// =========================================================================

#define XIP_MAGIC 0x30504958  // "XIP0" little-endian
#define XIP_DIGEST_LENGTH 20

struct XIPHEADER
{
	DWORD m_magic;       // must equal XIP_MAGIC
	DWORD m_dataStart;   // byte offset where the file data section begins
	WORD  m_fileCount;   // number of FILEDATA entries
	WORD  m_nameCount;   // number of FILENAME entries
	DWORD m_dataSize;    // total size of all file data
};

struct FILEDATA
{
	DWORD m_dataOffset;  // offset within the data section
	                     // (or packed mesh-ref payload for type=MESH_REFERENCE)
	DWORD m_size;        // size in bytes
	                     // (or primitive count for type=MESH_REFERENCE)
	DWORD m_type;        // one of XIP_TYPE_*
	DWORD m_timestamp;   // file modification time
};

struct FILENAME
{
	WORD m_fileDataIndex; // index into the FILEDATA array
	WORD m_nameOffset;    // offset into the names string pool
};

// File type identifiers stored in FILEDATA::m_type
#define XIP_TYPE_GENERIC        0
#define XIP_TYPE_MESH           1  // obsolete
#define XIP_TYPE_TEXTURE        2  // XPR-wrapped texture (.xbx)
#define XIP_TYPE_WAVE           3  // audio data
#define XIP_TYPE_MESH_REFERENCE 4  // packed: high byte = buffer index, low 24 = first index
#define XIP_TYPE_INDEXBUFFER    5  // raw D3D index buffer
#define XIP_TYPE_VERTEXBUFFER   6  // raw D3D vertex buffer (prefixed with count + FVF)

#ifndef BUILD_XIPSIGN

#define MAX_MESHBUFFER 10

// =========================================================================
// Runtime structures
// =========================================================================

// One vertex/index buffer pair attached to an XIP archive. CXipFile
// holds an array of these and CMeshRef instances point at one of them
// by index, sharing the GPU buffers across many mesh references.
struct CMeshBuffer
{
	DWORD m_fvf;
	int   m_vertexStride;
	int   m_vertexCount;
	int   m_indexCount;
	IDirect3DVertexBuffer8* m_vertexBuffer;
	IDirect3DIndexBuffer8*  m_indexBuffer;
};

// Buffered file reader -- reads in 64KB chunks aligned to 64KB
// boundaries. The dashboard opens XIPs with FILE_FLAG_NO_BUFFERING
// for performance, which requires sector-aligned reads, so this
// wrapper batches small Read() calls into a single large ReadFile().
class CFileBuffer
{
public:
	CFileBuffer();
	~CFileBuffer();

	void SetFile(PCTSTR Name, HANDLE hFile);
	int  Read(void* pv, int cb);
	bool Seek(int nPos);
	void FreeBuffer();
	void StartSignature(VOID);
	void EndSignature(XCALCSIG_SIGNATURE* Signature);

	HANDLE m_handle;
	BYTE*  m_buffer;
	int    m_bufferSize;
	int    m_readIndex;
	int    m_currentBlock;
};

// XIP archive -- loads and manages a .xip file's contents.
class CXipFile
{
public:
	CXipFile();
	~CXipFile();

	bool  Open(const TCHAR* szXipFileName);
	void  Reload();
	int   Find(const char* szURL);
	void* FindObject(const char* szURL, int nType = -1);

	char*   m_dirPath;
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

	XTIME m_cacheTime;
	bool  m_locked;

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

// CMeshRef is defined in Mesh.h -- it references CXipFile's shared buffers

// =========================================================================
// Public API
// =========================================================================

extern CXipFile* LoadXIP(const TCHAR* szURL, bool bSync = false, bool isSkin = false);
extern bool FindInXIPAndDetach(const TCHAR* szURL, BYTE*& pbContent, DWORD& cbContent);
extern void* FindObjectInXIP(const TCHAR* szURL, const TCHAR* szFilename, int nType = -1);

#endif // BUILD_XIPSIGN
