// settingsfile.h: CSettingsFile, the in-memory model of an .xbx /
// .ini settings file (sections + key=value entries, parse on Open,
// lazy save in destructor). Companion to shared/settingsfile.cpp.

#pragma once

// Returns the directory the running XBE was launched from, with the
// trailing filename component stripped. Used by the settings code to
// locate sibling .xbx files relative to the dashboard image.
void XI_GetProgramPath(char* szBuffer);

// One key=value entry inside an .xbx settings section.
class CSettingsFileValue
{
public:
	CSettingsFileValue();
	~CSettingsFileValue();

	CSettingsFileValue* m_next;
	TCHAR* m_name;
	TCHAR* m_value;
};

// One [section] block inside an .xbx settings file. Each section owns
// a singly-linked list of CSettingsFileValue entries.
class CSettingsFileSection
{
public:
	CSettingsFileSection();
	~CSettingsFileSection();

	CSettingsFileValue* FindValue(const TCHAR* szName);
	bool SetValue(const TCHAR* szName, const TCHAR* szValue);

	CSettingsFileSection* m_next;
	TCHAR* m_name;
	CSettingsFileValue* m_values;
};

// In-memory model of an .xbx settings file. Parses on Open, mutates
// in memory via SetValue, lazily writes back on Save (which the
// destructor calls automatically). Top-level entries that have no
// preceding [section] header land in a synthetic "default" section.
class CSettingsFile
{
public:
	CSettingsFile();
	~CSettingsFile();

	bool OpenDir(const TCHAR* szDir);
	bool Open(const TCHAR* szFile);
	bool Close();
	void Cancel();
	bool Save();

	inline const TCHAR* GetFileName() const
	{
		return m_filePath;
	}

	bool GetValue(const TCHAR* szSection, const TCHAR* szName, TCHAR* szValueBuf, int cchValueBuf);
	void SetValue(const TCHAR* szSection, const TCHAR* szName, const TCHAR* szValue);
	CSettingsFileSection* FindSection(const TCHAR* szSection, bool bCreate = false);

	// SetValue writes the whole file every time it changes a key, which is
	// fine for a setting the user just toggled and ruinous for a loop. Bracket
	// a run of writes with these and call Save yourself at the end.
	void BeginBatch() { m_batching = true; }
	void EndBatch()   { m_batching = false; }

	TCHAR* m_filePath;
	CSettingsFileSection* m_sections;
	bool m_dirty;
	bool m_batching;

protected:

#ifdef _UNICODE
	bool m_unicode;
#endif
};
