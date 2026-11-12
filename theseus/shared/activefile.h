#pragma once

// CActiveFile: one file pulled into memory by URL. The fetch looks in the
// loaded XIP archives first and then on disk; "string:" URLs carry their
// content inline. Rebuilt from the 5960 retail XBE (0x2e560..0x2e840).

class CActiveFile
{
public:
	CActiveFile();
	~CActiveFile();

	void Reset();

	bool Fetch(const TCHAR *szURL, bool bSearchAppDir = false, bool bTry = false);

	const TCHAR *GetURL() const { return m_url; }
	DWORD GetContentLength() const { return m_contentSize; }
	const BYTE *GetContent() const { return m_content; }

	// Hands the content to the caller, who frees it with TheseusFreeMemory.
	BYTE *DetachContent();

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
};
