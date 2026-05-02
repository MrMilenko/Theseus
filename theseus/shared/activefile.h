#pragma once

// CActiveFile: a file fetched into memory plus enough state to
// re-fetch it on demand and to read through it like a stream.
//
// Used by the dashboard's text/data loaders to pull a resource by URL,
// keep its bytes around for as long as the consumer needs them, and
// notice when the underlying file has been modified on disk so it can
// be re-read transparently. The fetch path checks any loaded XIP
// archives first, then falls back to the filesystem; bytes from a XIP
// are flagged so the update-poll loop knows not to stat them.

class CActiveFile
{
public:
	CActiveFile();
	~CActiveFile();

	void Reset();

	bool Fetch(const TCHAR *szURL, bool bSearchAppDir = false, bool bTry = false);
	bool Update();

	const TCHAR *GetURL() const { return m_url; }
	DWORD GetContentLength() const { return m_contentSize; }
	const BYTE *GetContent() const { return m_content; }

	BYTE *DetachContent();

	// File-like read API. The fetched bytes are treated as a flat
	// stream and m_readPos tracks the consumer's position in it.

	inline void Rewind()
	{
		Seek(0);
	}

	inline void Seek(int nNewBytePos)
	{
		ASSERT(nNewBytePos >= 0 && (DWORD)nNewBytePos <= m_contentSize);
		m_readPos = nNewBytePos;
	}

	inline void Skip(int nSkipBytes)
	{
		ASSERT((int)m_readPos + nSkipBytes >= 0 && (DWORD)(m_readPos + nSkipBytes) <= m_contentSize);
		m_readPos += nSkipBytes;
	}

	inline int Tell()
	{
		return (int)m_readPos;
	}

	inline bool Read(void *pv, int cb)
	{
		ASSERT(cb >= 0);

		if (m_readPos + (DWORD)cb > m_contentSize)
			return false;

		CopyMemory(pv, m_content + m_readPos, cb);
		m_readPos += cb;

		return true;
	}

	DWORD m_readPos;

#ifdef _UNICODE
	bool IsUnicode();
	void MakeUnicode();
#endif

protected:
	bool FetchFile(bool bTry = false);

	TCHAR *m_url;
	BYTE *m_content;
	DWORD m_contentSize;
	FILETIME m_modifiedTime;

	bool m_inXIP;

	float m_updatePeriod;
	XTIME m_nextUpdateTime;
};
