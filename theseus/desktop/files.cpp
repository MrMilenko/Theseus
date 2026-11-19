// files.cpp: the File and Folder script nodes on the desktop. A Folder
// whose path is set lists that directory into files and subFolders; a File
// carries its name, extension, path, size and date. Rebuilt from the 5960
// retail XBE's Files.cpp code (0x317c0..0x32030), on the desktop's char
// paths and find wrappers.

#include "std.h"
#include "dashapp.h"
#include "file_util.h"
#include "node.h"
#include "date_node.h"
#include "runner.h"

// 5960 layout, 0x30 bytes: name 0x1c, type 0x20, path 0x24, length 0x28, date 0x2c.
class CFile : public CNode
{
	DECLARE_NODE(CFile, CNode)
	DECLARE_NODE_PROPS()
	DECLARE_NODE_FUNCTIONS()
public:
	CFile();
	~CFile();

	CStrObject* readText();

	char* m_name;		// without the extension
	char* m_type;		// extension, lower case, no dot
	char* m_path;
	int m_length;
	CDateObject* m_date;
};

// 5960 layout, 0x3c bytes: path 0x1c, name 0x20, files 0x24, subFolders 0x30.
class CFolder : public CNode
{
	DECLARE_NODE(CFolder, CNode)
	DECLARE_NODE_PROPS()
	DECLARE_NODE_FUNCTIONS()
public:
	CFolder();
	~CFolder();

	bool OnSetProperty(const PRD* prop, const void* value);
	void Refresh(const char* szPath);

	void sortByName();
	void sortByType();
	void sortByDate();
	void sortByLength();

	char* m_path;
	char* m_name;
	CNodeArray m_files;
	CNodeArray m_subFolders;
};

IMPLEMENT_NODE("File", CFile, CNode)

START_NODE_PROPS(CFile, CNode)
	NODE_PROP(pt_string, CFile, name)
	NODE_PROP(pt_string, CFile, type)
	NODE_PROP(pt_string, CFile, path)
	NODE_PROP(pt_integer, CFile, length)
	NODE_PROP(pt_node, CFile, date)
END_NODE_PROPS()

#undef _FND_CLASS
#define _FND_CLASS CFile
START_NODE_FUN(CFile, CNode)
	NODE_FUN_SV(readText)
END_NODE_FUN()

IMPLEMENT_NODE("Folder", CFolder, CNode)

START_NODE_PROPS(CFolder, CNode)
	NODE_PROP(pt_string, CFolder, path)
	NODE_PROP(pt_string, CFolder, name)
	NODE_PROP(pt_nodearray, CFolder, files)
	NODE_PROP(pt_nodearray, CFolder, subFolders)
END_NODE_PROPS()

#undef _FND_CLASS
#define _FND_CLASS CFolder
START_NODE_FUN(CFolder, CNode)
	NODE_FUN_VV(sortByName)
	NODE_FUN_VV(sortByType)
	NODE_FUN_VV(sortByDate)
	NODE_FUN_VV(sortByLength)
END_NODE_FUN()

// CFile::CFile()
// Reconstructed from 5960 retail XBE @ 0x00031800 (inlined in CreateNode):
//     name = type = path = 0; length = -1; date = 0;
CFile::CFile()
{
	m_name = NULL;
	m_type = NULL;
	m_path = NULL;
	m_length = -1;
	m_date = NULL;
}

// CFile::~CFile()
// Reconstructed from 5960 retail XBE @ 0x000318e0: frees the three strings
// and deletes the date through its virtual destructor.
CFile::~CFile()
{
	delete [] m_name;
	delete [] m_type;
	delete [] m_path;
	delete m_date;
}

// CFile::readText()   [script fn 'readText', FND 0x0017f918, sig 8]
// Reconstructed from 5960 retail XBE @ 0x00031960: a new empty string; if
// there's a path, fetch the file and append its contents. 5960 widens the
// text first (MakeUnicode); desktop strings are already narrow.
CStrObject* CFile::readText()
{
	CStrObject* pText = new CStrObject;
	if (m_path == NULL)
		return pText;

	CActiveFile file;
	if (file.Fetch(m_path))
		pText->Append((char*)file.GetContent());
	return pText;
}

// CFolder::CFolder()
// Reconstructed from 5960 retail XBE @ 0x00031a50: both strings and both
// arrays start empty.
CFolder::CFolder()
{
	m_path = NULL;
	m_name = NULL;
}

// CFolder::~CFolder()
// Reconstructed from 5960 retail XBE @ 0x00031ab0: frees path and name;
// the two arrays release their nodes as they go.
CFolder::~CFolder()
{
	delete [] m_path;
	delete [] m_name;
}

// CFolder::OnSetProperty(prop, value)
// Reconstructed from 5960 retail XBE @ 0x00031b30:
//     if (prop->offset == 0x1c) Refresh(*value); return true;
// Setting path is what lists the folder.
bool CFolder::OnSetProperty(const PRD* prop, const void* value)
{
	if ((intptr_t)prop->pbOffset == MEMBER_OFFSET(m_path))
		Refresh(*(const char* const*)value);
	return true;
}

static char* NewCopy(const char* sz, size_t cch)
{
	char* copy = new char [cch + 1];
	memcpy(copy, sz, cch);
	copy[cch] = 0;
	return copy;
}

// "folder\name", the way 5960 joins them.
static char* NewJoin(const char* szFolder, const char* szName)
{
	char* path = new char [strlen(szFolder) + 1 + strlen(szName) + 1];
	sprintf(path, "%s\\%s", szFolder, szName);
	return path;
}

// CFolder::Refresh(path)
// Reconstructed from 5960 retail XBE @ 0x00031b50 (971 bytes). Lists
// path\*.*; when the folder can't be read it leaves everything alone. The
// folder's name becomes whatever follows the last backslash. Hidden, system
// and temporary entries are skipped. A directory becomes a Folder (path
// joined, name as is) on subFolders; a file becomes a File on files with
// its date from the last write time, the name up to the last dot, the type
// after it in lower case, the joined path and the low 32 bits of the size.
// Entries are added to what's already there. Theseus reports a folder it
// can't read through TRACE.
void CFolder::Refresh(const char* szPath)
{
	char szPattern [MAX_PATH];
	lstrcpyn(szPattern, szPath, MAX_PATH - sizeof ("\\*.*"));
	strcat(szPattern, "\\*.*");

	char szPatternA [MAX_PATH];
	Ansi(szPatternA, szPattern, MAX_PATH);

	WIN32_FIND_DATA fd;
	HANDLE hFind = TheseusFindFirstFile(szPatternA, &fd);
	if (hFind == INVALID_HANDLE_VALUE)
	{
		TRACE("\001Cannot read directory: \"%s\" (%d)\n", szPath, GetLastError());
		return;
	}

	const char* pchLeaf = strrchr(szPath, '\\');
	pchLeaf = (pchLeaf != NULL) ? pchLeaf + 1 : "";
	delete [] m_name;
	m_name = NewCopy(pchLeaf, strlen(pchLeaf));

	const DWORD dwSkip = FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_TEMPORARY;

	do
	{
		if (fd.dwFileAttributes & dwSkip)
			continue;

		char szEntry [MAX_PATH];
		Unicode(szEntry, fd.cFileName, countof(szEntry));

		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			CFolder* pFolder = new CFolder;
			m_subFolders.AddNode(pFolder);
			pFolder->m_path = NewJoin(szPath, szEntry);
			pFolder->m_name = NewCopy(szEntry, strlen(szEntry));
			continue;
		}

		CFile* pFile = new CFile;
		m_files.AddNode(pFile);

		pFile->m_date = new CDateObject;
		pFile->m_date->m_time = fd.ftLastWriteTime;

		const char* pchDot = strrchr(szEntry, '.');
		size_t cchBase = strlen(szEntry) - (pchDot != NULL ? strlen(pchDot) : 0);
		pFile->m_name = NewCopy(szEntry, cchBase);

		const char* pchExt = (pchDot != NULL) ? pchDot + 1 : "";
		pFile->m_type = NewCopy(pchExt, strlen(pchExt));
		for (char* pch = pFile->m_type; *pch != 0; pch++)
			*pch = (char)tolower((unsigned char)*pch);

		pFile->m_path = NewJoin(szPath, szEntry);
		pFile->m_length = fd.nFileSizeLow;
	}
	while (TheseusFindNextFile(hFind, &fd));

	TheseusFindClose(hFind);
}

// The sorts: qsort over files with these compares (0x31f30..0x32010).
// Name and type ignore case, date is CompareFileTime, length subtracts.
static inline const CFile* FileAt(const void* pv)
{
	return *(const CFile* const*)pv;
}

static int __cdecl CompareNames(const void* a, const void* b)
{
	return strcasecmp(FileAt(a)->m_name, FileAt(b)->m_name);
}

static int __cdecl CompareTypes(const void* a, const void* b)
{
	return strcasecmp(FileAt(a)->m_type, FileAt(b)->m_type);
}

static int __cdecl CompareDates(const void* a, const void* b)
{
	return CompareFileTime(&FileAt(a)->m_date->m_time, &FileAt(b)->m_date->m_time);
}

static int __cdecl CompareLengths(const void* a, const void* b)
{
	return FileAt(a)->m_length - FileAt(b)->m_length;
}

void CFolder::sortByName()		{ m_files.Sort(CompareNames); }
void CFolder::sortByType()		{ m_files.Sort(CompareTypes); }
void CFolder::sortByDate()		{ m_files.Sort(CompareDates); }
void CFolder::sortByLength()	{ m_files.Sort(CompareLengths); }
