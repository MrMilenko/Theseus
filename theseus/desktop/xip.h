// xip.h: desktop XIP archive public API. Companion to
// desktop/xip.cpp.

#pragma once

#define XIP_MAGIC 0x30504958
#define XIP_DIGEST_LENGTH 20


struct XIPHEADER
{
	uint32_t m_dwMagic;
	uint32_t m_dwDataStart;
	uint16_t m_wFileCount;
	uint16_t m_wNameCount;
	uint32_t m_dwDataSize;
};

struct FILEDATA
{
	uint32_t m_dwDataOffset;
	uint32_t m_dwSize;
	uint32_t m_dwType;
	uint32_t m_dwTimestamp;
};

struct FILENAME
{
	uint16_t m_wFileDataIndex;
	uint16_t m_wNameOffset;
};

#define XIP_TYPE_GENERIC			0
#define XIP_TYPE_MESH				1
#define XIP_TYPE_TEXTURE			2
#define XIP_TYPE_WAVE				3
#define XIP_TYPE_MESH_REFERENCE		4 // m_dwDataOffset is MESHID; m_dwSize is nPrimitiveCount
#define XIP_TYPE_INDEXBUFFER		5
#define XIP_TYPE_VERTEXBUFFER		6

#ifndef BUILD_XIPSIGN

#define MAX_MESHBUFFER 512

struct CMeshBuffer
{
	DWORD m_fvf;
	int m_nVertexStride;
	int m_nVertexCount;
	int m_nIndexCount;
	IDirect3DVertexBuffer8* m_pVertexBuffer;
	IDirect3DIndexBuffer8* m_pIndexBuffer;
};

class CFileBuffer
{
public:
	CFileBuffer();
	~CFileBuffer();

	void SetFile(const char* Name, HANDLE hFile);
	int Read(void* pv, int cb);
	bool Seek(int nPos);
	void FreeBuffer();
	void StartSignature (VOID);
	void EndSignature (XCALCSIG_SIGNATURE * Signature);



	HANDLE m_hFile;
	uint8_t* m_pbBuffer;
	int m_cbBuffer;
	int m_ibRead;
    int m_nBlkCur;
private:
};

class CXipFile
{
public:
	CXipFile();
	~CXipFile();

	bool Open(const char* szXipFileName);
	void Reload();
	int Find(const char* szURL);
//	bool Find(const char* szURL, uint8_t*& pbContent, uint32_t& cbContent);
	void* FindObject(const char* szURL, int nType = -1);

	char* m_szDirPath;
	char* m_szXipFileName;

	XIPHEADER m_header;
	FILEDATA* m_filedata;
	FILENAME* m_directory;
	char* m_names;

	void** m_objects;
	bool m_bLoaded;

	CMeshBuffer m_rgMeshBuffer [MAX_MESHBUFFER];
	int m_nVertexBufferCount;
	int m_nIndexBufferCount;

	void DeleteMeshBuffers();

	XTIME m_cacheTime;

	bool m_bLocked;

	bool IsUnloaded() const;
	bool IsReloading() const;
	inline bool IsReady() const
		{ return m_bLoaded && !m_bReloading; }

protected:
	CFileBuffer m_file;
	bool m_bReloading;

	bool Load();
	void CreateObjects();
	void ReloadMeshBuffers();
	void ReadIndexBuffer(int nFileIndex, int nIndexBuffer);
	void ReadVertexBuffer(int nFileIndex, int nVertexBuffer);

	static DWORD CALLBACK StartLoadThread(LPVOID pvContext);
	friend CXipFile* LoadXIP(const char* szURL, bool bSync, bool isSkin);
};

extern CXipFile* LoadXIP(const char* szURL, bool bSync = false, bool isSkin = false );
extern bool FindInXIPAndDetach(const char* szURL, BYTE*& pbContent, DWORD& cbContent);
extern void* FindObjectInXIP(const char* szURL, const char* szFilename, int nType = -1);

#endif // BUILD_XIPSIGN
