// settingsfile.h: .xbx / .ini style settings files held in memory as
// [section] lists of name=value pairs. Bodies in shared/settingsfile.cpp.

#pragma once

// Folder the running program was started from, without the file name.
void XI_GetProgramPath(char* out);

// name=value, one link in its section's list.
class CSettingsFileValue
{
public:
	CSettingsFileValue();
	~CSettingsFileValue();

	CSettingsFileValue* m_next;
	TCHAR* m_name;
	TCHAR* m_value;
};

class CSettingsFileSection
{
public:
	CSettingsFileSection();
	~CSettingsFileSection();

	CSettingsFileValue* FindValue(const TCHAR* name);
	bool SetValue(const TCHAR* name, const TCHAR* value);

	CSettingsFileSection* m_next;
	TCHAR* m_name;
	CSettingsFileValue* m_values;
};

// Open parses the whole file into memory. Pairs before the first [section]
// go to "default".
class CSettingsFile
{
public:
	CSettingsFile();
	~CSettingsFile();

	bool Open(const TCHAR* path);
	bool OpenDir(const TCHAR* dir);
	bool Save();
	bool Close();
	void Cancel();

	inline const TCHAR* GetFileName() const { return m_filePath; }

	CSettingsFileSection* FindSection(const TCHAR* section, bool create = false);
	bool GetValue(const TCHAR* section, const TCHAR* name, TCHAR* buf, int bufLen);
	void SetValue(const TCHAR* section, const TCHAR* name, const TCHAR* value);

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
	int WriteTo(const TCHAR* path);

#ifdef _UNICODE
	bool m_unicode;
#endif
};
