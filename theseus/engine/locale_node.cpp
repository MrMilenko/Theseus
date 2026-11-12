// locale_node.cpp: the Translator node and the dashboard's locale helpers.
//
// One table of "key = value" pairs for the current language, looked up by
// every Text node and script string; plus the date, time and number formats
// that change with region and language, and the time zone list. Rebuilt
// function by function from the 5960 retail XBE; what the binary does is
// written up in docs/decomp/Locale.md. Both builds compile this file. The
// seam is the language file: 5960 loads it from an XBE section, Theseus from
// Q:\Language\<name>.dat, and desktop narrows its UTF-16 to one byte a char.

#include "std.h"
#include "theseus.h"
#ifdef _XBOX
#include "title_collection.h"
#else
#include "titlecollection.h"
#endif
#include "node.h"
#include "runner.h"
#include "locale_node.h"
#include "xap_compile.h"
#include "timezone.h"

// 5960 holds 550 pairs (0x0017aee8); the Theseus files can be longer.
#define MAX_XLATE 1000

int g_nCurLanguage;     // 0x0017c024
int g_nCurRegion;       // 0x0017c020

// 5960 names an XBE section per language (pointer table at 0x001745b0, nine
// of them); Theseus ships a .dat file for the first six.
static const char* const s_languageFiles[LANGUAGE_COUNT] =
{
	"English", "Japanese", "German", "French", "Spanish", "Italian",
};

// Two letters per language, ending with Korean, Chinese and Portuguese.
static const TCHAR s_languageCodes[] = _T("ENJADEFRESITKOTWBR");
#define LANGUAGE_CODE_COUNT 9

struct XlatePair
{
	TCHAR* key;
	TCHAR* value;
};

static XlatePair s_xlate[MAX_XLATE];
static TCHAR* s_xlateText;      // the loaded file; keys and values point into it


// ===== Loading a language ==================================================

// Reads Q:\Language\<name>.dat (UTF-16LE behind a BOM) into s_xlateText,
// null terminated and without the BOM. Desktop keeps the low byte of each
// character since its TCHAR is char.
static bool ReadLanguageFile(const char* name)
{
	char path[MAX_PATH];
	sprintf(path, "Q:\\Language\\%s.dat", name);
	HANDLE file = TheseusCreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_SYSTEM, NULL);
	if (file == INVALID_HANDLE_VALUE)
		return false;

	DWORD units = GetFileSize(file, NULL) / 2;
	WORD* raw = (WORD*)malloc((units + 1) * sizeof(WORD));
	DWORD got = 0;
	if (raw != NULL)
		ReadFile(file, raw, units * 2, &got, NULL);
	CloseHandle(file);
	if (raw == NULL)
		return false;

	units = got / 2;
	raw[units] = 0;
	WORD* start = (units > 0 && raw[0] == 0xfeff) ? raw + 1 : raw;
	int count = (int)(raw + units - start);

	s_xlateText = (TCHAR*)malloc((count + 1) * sizeof(TCHAR));
	if (s_xlateText != NULL)
	{
		for (int i = 0; i <= count; i++)
			s_xlateText[i] = (TCHAR)start[i];
	}
	free(raw);
	return s_xlateText != NULL;
}

static void FreeXlate()
{
	free(s_xlateText);
	s_xlateText = NULL;
	memset(s_xlate, 0, sizeof(s_xlate));
}

static inline unsigned CharCode(TCHAR ch)
{
#ifdef _XBOX
	return (WORD)ch;
#else
	return (unsigned char)ch;
#endif
}

static inline bool IsBlank(TCHAR ch)
{
	return ch == ' ' || ch == '\t';
}

static inline TCHAR* SkipBlanks(TCHAR* p)
{
	while (IsBlank(*p))
		p++;
	return p;
}

// A quoted field, or a value, runs to a quote or the end of the line.
static inline TCHAR* SkipField(TCHAR* p)
{
	while (*p != 0 && *p != '\r' && *p != '\n' && *p != '"')
		p++;
	return p;
}

// Steps over CR, and LF after it.
static inline TCHAR* NextLine(TCHAR* p)
{
	p++;
	if (*p == '\n')
		p++;
	return p;
}

// LoadXlate(section)
// Reconstructed from 5960 retail XBE @ 0x0003c370. The binary loads the
// language's XBE section (retrying after freeing the texture then mesh cache,
// rebooting when out of memory), checks the FEFF mark and then reads one pair
// per CR ended line:
//     [blanks] ['#' comment] CR                 skipped
//     key | "key"  [blanks] = [blanks]  value | "value"  [blanks] ['#' comment] CR
// An unquoted key ends at a blank or '='; inside it '_' becomes ':' before
// '=' or a space and ' ' anywhere else. The value goes through
// ExpandCString. A missing '=' or CR stops the whole file; an empty key or
// value skips the line. See docs/decomp/Locale.md.
static void LoadXlate(const char* name)
{
	if (!ReadLanguageFile(name))
	{
		TRACE(_T("\001LoadXlate: failed to load %hs.dat\n"), name);
		return;
	}

	TCHAR* p = s_xlateText;
	int count = 0;
	while (*p != 0)
	{
		p = SkipBlanks(p);

		// A whole line comment runs to CR or LF.
		if (*p == '#')
		{
			while (*p != 0 && *p != '\r' && *p != '\n')
				p++;
		}
		if (*p == '\r')
		{
			p = NextLine(p);
			continue;
		}

		TCHAR* key;
		if (*p == '"')
		{
			key = ++p;
			p = SkipField(p);
		}
		else
		{
			key = p;
			for (; CharCode(*p) > ' ' && *p != '='; p++)
			{
				if (*p == '_')
					*p = (p[1] == '=' || p[1] == ' ') ? ':' : ' ';
			}
		}
		int keyLen = (int)(p - key);
		if (*p == '"')
			p++;

		p = SkipBlanks(p);
		if (*p != '=')
			return;
		p = SkipBlanks(p + 1);
		if (*p == '"')
			p++;

		TCHAR* value = p;
		p = SkipField(p);
		int valueLen = (int)(p - value);
		if (*p == '"')
			p++;

		// A trailing comment runs to CR only.
		p = SkipBlanks(p);
		if (*p == '#')
		{
			while (*p != 0 && *p != '\r')
				p++;
		}
		if (*p != '\r')
			return;
		p = NextLine(p);

		if (keyLen == 0 || valueLen == 0)
			continue;

		valueLen = ExpandCString(value, valueLen, value, valueLen);
		if (valueLen < 0)
			return;
		key[keyLen] = 0;
		value[valueLen] = 0;

		// 5960 has no bound here.
		if (!CHECK(count < MAX_XLATE))
			return;
		s_xlate[count].key = key;
		s_xlate[count].value = value;
		count++;
	}
}

// Translate(string, out, language)
// Reconstructed from 5960 retail XBE @ 0x0003c620:
//     wcscpy(out, string); p = wcschr(out, '@'); if (p) *p = '@';
//     for (i = 0; i < 550 && table[i].key; i++)
//         if (!wcscmp(table[i].key, out)) { wcscpy(out, table[i].value); break; }
//     return out;
// The '@' write puts back the character it found, so it's left out. The
// language argument is unused, as in 5960 (it reads the current table).
const TCHAR* Translate(const TCHAR* szString, TCHAR* szTranslate, int nLanguage/*=LANGUAGE_CURRENT*/)
{
	_tcscpy(szTranslate, szString);
	for (int i = 0; i < MAX_XLATE && s_xlate[i].key != NULL; i++)
	{
		if (_tcscmp(s_xlate[i].key, szTranslate) == 0)
		{
			_tcscpy(szTranslate, s_xlate[i].value);
			break;
		}
	}
	return szTranslate;
}

void Locale_Exit()
{
	FreeXlate();
}


// ===== Translator node =====================================================

class CTranslator : public CNode
{
	DECLARE_NODE(CTranslator, CNode)
public:
	CTranslator();
	~CTranslator();

	void SetLanguage(int nNewLanguage);
	CStrObject* Translate(const TCHAR* szString);
	CStrObject* TranslateStripColon(const TCHAR* szString);
	CStrObject* GetLanguageCode();
	CStrObject* GetDateSeparator();
	CStrObject* FormatNumber(int nNumber);

	int GetTimeZoneCount();
	CStrObject* GetTimeZoneText(int nTimeZone);

	DECLARE_NODE_FUNCTIONS()
};

IMPLEMENT_NODE("Translator", CTranslator, CNode)

// FND at 0x00180408, same order.
#define _FND_CLASS CTranslator
START_NODE_FUN(CTranslator, CNode)
	NODE_FUN_VI(SetLanguage)
	NODE_FUN_SS(Translate)
	NODE_FUN_SV(GetLanguageCode)
	NODE_FUN_SV(GetDateSeparator)
	NODE_FUN_SI(FormatNumber)
	NODE_FUN_IV(GetTimeZoneCount)
	NODE_FUN_SI(GetTimeZoneText)
	NODE_FUN_SS(TranslateStripColon)
END_NODE_FUN()
#undef _FND_CLASS

// CTranslator::CTranslator() / ~CTranslator()
// Reconstructed from 5960 retail XBE @ 0x0003c690 (the factory builds a bare
// CNode, 0x1c bytes) and @ 0x0003c700 (straight to CNode's destructor).
// The node has no state of its own.
CTranslator::CTranslator()
{
}

CTranslator::~CTranslator()
{
}

// CTranslator::SetLanguage(language)   [script fn 'SetLanguage', FND 0x00180408, sig 14]
// Reconstructed from 5960 retail XBE @ 0x0003c8f0:
//     if ((lang != cur || !table[0].key) && lang >= 0 && lang < 9) {
//         cur = lang; XFreeSectionByHandle(section); zero table;
//         LoadXlate(sections[lang]);
//         for each of the 9 title arrays: if (loaded) SetRoot(0);
//     }
// Theseus takes the six languages it has files for, and empties the title
// lists through DeleteAll so their names come back in the new language.
void CTranslator::SetLanguage(int nNewLanguage)
{
	if (nNewLanguage == g_nCurLanguage && s_xlate[0].key != NULL)
		return;
	if (nNewLanguage < 0 || nNewLanguage >= LANGUAGE_COUNT)
		return;

	g_nCurLanguage = nNewLanguage;
	FreeXlate();
	LoadXlate(s_languageFiles[nNewLanguage]);

	for (int i = 0; i < countof(g_titles); i++)
	{
		if (g_titles[i].IsValid())
			g_titles[i].DeleteAll(false);
	}
}

// CTranslator::Translate(string)   [script fn 'Translate', FND 0x00180420, sig 5]
// Reconstructed from 5960 retail XBE @ 0x0003c980:
//     WCHAR buf[1024]; return new CStrObject(Translate(string, buf));
CStrObject* CTranslator::Translate(const TCHAR* szString)
{
	TCHAR buf[MAX_TRANSLATE_LEN];
	return new CStrObject(::Translate(szString, buf));
}

// CTranslator::TranslateStripColon(string)   [script fn 'TranslateStripColon', FND 0x001804b0, sig 5]
// Reconstructed from 5960 retail XBE @ 0x0003ca10:
//     Translate(string, buf); n = wcslen(buf);
//     if (n > 0 && buf[n-1] == ':') buf[n-1] = 0; return new CStrObject(buf);
CStrObject* CTranslator::TranslateStripColon(const TCHAR* szString)
{
	TCHAR buf[MAX_TRANSLATE_LEN];
	::Translate(szString, buf);
	int len = (int)_tcslen(buf);
	if (len > 0 && buf[len - 1] == ':')
		buf[len - 1] = 0;
	return new CStrObject(buf);
}

// GetLanguageCode(out)
// Reconstructed from 5960 retail XBE @ 0x0003cac0:
//     if (cur >= 0 && cur < 10) { out[0..1] = L"ENJADEFRESITKOTWBR"[cur*2..]; out[2] = 0; }
//     else out[0] = 0;
// The binary lets 9 through, one past "BR"; this stops at the last code.
const TCHAR* GetLanguageCode(TCHAR* sz)
{
	if (g_nCurLanguage < 0 || g_nCurLanguage >= LANGUAGE_CODE_COUNT)
	{
		sz[0] = 0;
		return sz;
	}
	sz[0] = s_languageCodes[g_nCurLanguage * 2];
	sz[1] = s_languageCodes[g_nCurLanguage * 2 + 1];
	sz[2] = 0;
	return sz;
}

// CTranslator::GetLanguageCode()   [script fn 'GetLanguageCode', FND 0x00180438, sig 8]
// Reconstructed from 5960 retail XBE @ 0x0003cb00: the same two letters as a
// string object, or an empty one out of range.
CStrObject* CTranslator::GetLanguageCode()
{
	if (g_nCurLanguage < 0 || g_nCurLanguage >= LANGUAGE_CODE_COUNT)
		return new CStrObject;
	return new CStrObject(s_languageCodes + g_nCurLanguage * 2, 2);
}

// GetDateSeparator()
// Reconstructed from 5960 retail XBE @ 0x0003cbc0. The binary switches on
// region << 16 | language: 0x10002, 0x10005, 0x20002, 0x20003, 0x20005,
// 0x40002, 0x40003, 0x40005 give '.'; 0x10006, 0x20006, 0x40006 give '-';
// everything else '/'. So German and Italian use dots everywhere, French
// outside North America, and Korean dashes.
TCHAR GetDateSeparator()
{
	int region = g_nCurRegion;
	if (region != XC_GAME_REGION_NA && region != XC_GAME_REGION_JAPAN && region != XC_GAME_REGION_RESTOFWORLD)
		return '/';

	switch (g_nCurLanguage)
	{
	case LANGUAGE_GERMAN:
	case LANGUAGE_ITALIAN:
		return '.';
	case LANGUAGE_FRENCH:
		return region == XC_GAME_REGION_NA ? '/' : '.';
	case 6:     // Korean
		return '-';
	}
	return '/';
}

// CTranslator::GetDateSeparator()   [script fn 'GetDateSeparator', FND 0x00180450, sig 8]
// Reconstructed from 5960 retail XBE @ 0x0003cc40: the one character as a string.
CStrObject* CTranslator::GetDateSeparator()
{
	TCHAR sep = ::GetDateSeparator();
	return new CStrObject(&sep, 1);
}

// FormatInteger(out, number, locale)
// 5960 has no separate copy; the script call (@ 0x0003ccb0) and the block
// counts (@ 0x0004ca90) print with swprintf(out, L"%d", n) in place.
void FormatInteger(TCHAR* szBuf, int nNumber, int locale/*=LOCALE_CURRENT*/)
{
	_stprintf(szBuf, _T("%d"), nNumber);
}

// FormatBlocks(out, blocks, locale)
// Reconstructed from the memory monitor's block text, 5960 retail XBE
// @ 0x0004ca90: past 50000 it prints "50000+".
void FormatBlocks(TCHAR* szBuf, int nBlocks, int locale/*=LOCALE_CURRENT*/)
{
	if (nBlocks <= MAX_BLOCKS_TO_SHOW)
	{
		FormatInteger(szBuf, nBlocks, locale);
		return;
	}
	FormatInteger(szBuf, MAX_BLOCKS_TO_SHOW, locale);
	_tcscat(szBuf, _T("+"));
}

// CTranslator::FormatNumber(number)   [script fn 'FormatNumber', FND 0x00180468, sig 4]
// Reconstructed from 5960 retail XBE @ 0x0003ccb0:
//     WCHAR buf[64]; swprintf(buf, L"%d", n); return new CStrObject(buf);
CStrObject* CTranslator::FormatNumber(int nNumber)
{
	TCHAR buf[64];
	FormatInteger(buf, nNumber);
	return new CStrObject(buf);
}


// ===== Dates and times =====================================================

static inline bool KnownRegion(int region)
{
	return region == XC_GAME_REGION_NA || region == XC_GAME_REGION_JAPAN || region == XC_GAME_REGION_RESTOFWORLD;
}

// Japanese, Korean and Chinese (languages 1, 6, 7).
static inline bool EastAsianLanguage(int language)
{
	return language == 1 || language == 6 || language == 7;
}

// The date half of FormatDate and FormatTime, with its trailing space:
// North American English writes month first, East Asian languages year
// first, everyone else day first.
static void PrintDate(TCHAR* out, int size, const SYSTEMTIME* st, int locale)
{
	TCHAR sep = GetDateSeparator();
	if (locale == LOCALE_NA_ENGLISH)
		_sntprintf(out, size, _T("%02d%c%02d%c%d "), st->wMonth, sep, st->wDay, sep, st->wYear);
	else if (KnownRegion(REGION_FROM_LOCALE(locale)) && EastAsianLanguage(LANGUAGE_FROM_LOCALE(locale)))
		_sntprintf(out, size, _T("%d%c%02d%c%02d "), st->wYear, sep, st->wMonth, sep, st->wDay);
	else
		_sntprintf(out, size, _T("%02d%c%02d%c%d "), st->wDay, sep, st->wMonth, sep, st->wYear);
	out[size - 1] = 0;
}

// FormatTime(out, size, time, locale)
// Reconstructed from 5960 retail XBE @ 0x0003ce90. Prints the date as
// FormatDate does, then the time: English, Japanese, Korean and Chinese in
// the three regions get "%d:%02d %s" on a 12 hour clock (0 shows as 12, AM
// below 12); everyone else "%d:%02d" on 24. Both parts plus 2 must fit in
// size or it returns false without writing.
bool FormatTime(TCHAR* szBuf, SIZE_T chLen, SYSTEMTIME* pst, int locale/*=LOCALE_CURRENT*/)
{
	if (locale == LOCALE_CURRENT)
		locale = LOCALE_FROM_REGION_LANGUAGE(g_nCurRegion, g_nCurLanguage);

	TCHAR date[32];
	PrintDate(date, countof(date), pst, locale);

	TCHAR time[32];
	int language = LANGUAGE_FROM_LOCALE(locale);
	bool twelveHour = KnownRegion(REGION_FROM_LOCALE(locale)) && (language == LANGUAGE_ENGLISH || EastAsianLanguage(language));
	if (twelveHour)
	{
		int hour = pst->wHour >= 12 ? pst->wHour - 12 : pst->wHour;
		if (hour == 0)
			hour = 12;
		_sntprintf(time, countof(time), _T("%d:%02d %s"), hour, pst->wMinute, pst->wHour < 12 ? _T("AM") : _T("PM"));
	}
	else
	{
		_sntprintf(time, countof(time), _T("%d:%02d"), pst->wHour, pst->wMinute);
	}
	time[countof(time) - 1] = 0;

	if (chLen < _tcslen(date) + _tcslen(time) + 2)
		return false;
	_tcscpy(szBuf, date);
	_tcscat(szBuf, time);
	return true;
}

// FormatDate(out, size, time, locale)
// Reconstructed from 5960 retail XBE @ 0x0003cd40. Ghidra decompile, trimmed:
//     if (locale == -1) locale = region << 16 | language;
//     sep = GetDateSeparator();
//     0x10000 (NA English):                         "%02d%c%02d%c%d " month, day, year
//     regions 1, 2, 4 with language 1, 6 or 7:      "%d%c%02d%c%02d " year, month, day
//     anything else:                                "%02d%c%02d%c%d " day, month, year
//     if (chLen < len + 2) return false; copy; return true;
// The Text node's <date> prefix draws this.
bool FormatDate(TCHAR* szBuf, SIZE_T chLen, SYSTEMTIME* pst, int locale/*=LOCALE_CURRENT*/)
{
	if (locale == LOCALE_CURRENT)
		locale = LOCALE_FROM_REGION_LANGUAGE(g_nCurRegion, g_nCurLanguage);

	TCHAR date[32];
	PrintDate(date, countof(date), pst, locale);
	if (chLen < _tcslen(date) + 2)
		return false;
	_tcscpy(szBuf, date);
	return true;
}


// ===== Time zones ==========================================================

// CTranslator::GetTimeZoneCount()   [script fn 'GetTimeZoneCount', FND 0x00180480, sig 2]
// Reconstructed from 5960 retail XBE @ 0x0003c710: return 75;
int CTranslator::GetTimeZoneCount()
{
	return TIMEZONECOUNT;
}

// CTranslator::GetTimeZoneText(index)   [script fn 'GetTimeZoneText', FND 0x00180498, sig 4]
// Reconstructed from 5960 retail XBE @ 0x0003c720:
//     if (index >= 0 && index < 75) return Translate(zones[index].name);
//     return new CStrObject;
CStrObject* CTranslator::GetTimeZoneText(int nTimeZone)
{
	if (nTimeZone < 0 || nTimeZone >= TIMEZONECOUNT)
		return new CStrObject;
	return Translate(g_timezoneinfo[nTimeZone].dispname);
}

// GetTimeZoneIndex(info)
// Reconstructed from 5960 retail XBE @ 0x0003c780. Walks the 75 zones for
// the best match on the bias: same bias scores 1, same standard and daylight
// bias scores 2, and when the first 4 characters of the standard name match
// too that zone wins outright (the EEPROM keeps only 4). The first zone with
// the top score wins; -1 when no bias matches.
int GetTimeZoneIndex(const TIME_ZONE_INFORMATION* tzinfo)
{
	int best = -1;
	int bestScore = -1;
	for (int i = 0; i < TIMEZONECOUNT; i++)
	{
		const TZINFO& zone = g_timezoneinfo[i];
		if (zone.bias != tzinfo->Bias)
			continue;

		int score = 1;
		if (zone.stdbias == tzinfo->StandardBias && zone.dltbias == tzinfo->DaylightBias)
		{
			score = 2;
			if (wcsncmp(zone.stdname, tzinfo->StandardName, 4) == 0)
				return i;
		}
		if (score > bestScore)
		{
			bestScore = score;
			best = i;
		}
	}
	return best;
}

static void CopyChangeDate(SYSTEMTIME* out, const TZDATE& in)
{
	out->wMonth = in.month;
	out->wDay = in.day;
	out->wDayOfWeek = in.dayofweek;
	out->wHour = in.hour;
}

// GetTimeZoneInfo(index, info)
// Reconstructed from 5960 retail XBE @ 0x0003c820. Out of range returns
// false. Otherwise zeroes info, copies the three biases, the standard name
// and the daylight name (the standard one when a zone has none, both capped
// at 32 characters), and the month, day, weekday and hour of each change.
bool GetTimeZoneInfo(int index, TIME_ZONE_INFORMATION* tzinfo)
{
	if (index < 0 || index >= TIMEZONECOUNT)
		return false;

	const TZINFO& zone = g_timezoneinfo[index];
	memset(tzinfo, 0, sizeof(*tzinfo));
	tzinfo->Bias = zone.bias;
	tzinfo->StandardBias = zone.stdbias;
	tzinfo->DaylightBias = zone.dltbias;
	wcsncpy(tzinfo->StandardName, zone.stdname, 32);
	wcsncpy(tzinfo->DaylightName, zone.dltname != NULL ? zone.dltname : zone.stdname, 32);
	CopyChangeDate(&tzinfo->StandardDate, zone.stddate);
	CopyChangeDate(&tzinfo->DaylightDate, zone.dltdate);
	return true;
}
