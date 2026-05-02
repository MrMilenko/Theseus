// locale_node.cpp: CLocale, the dashboard's translation table for UI
// strings. Reads xlate.ini at boot, exposes XAP-callable lookups, and
// drives section switching for language changes. Decompiled from the
// 5960 retail XBE.

#include "std.h"
#include "theseus.h"
#include "node.h"
#include "runner.h"
#include "locale_node.h"
#include "xap_compile.h"
#include "title_collection.h"

#define MAX_XLATE 1000 // This is the maximum number of terms in a translation file

int g_nCurLanguage;
int g_nCurRegion;

static const CHAR* rgszTranslateSection[] =
{
    "English",
    "Japanese",
    "German",
    "French",
    "Spanish",
    "Italian"
};

struct XLATE
{
    TCHAR* m_szKey;
    TCHAR* m_szValue;
};

static HANDLE g_hXlate;
static TCHAR* rgchXlateData;
static XLATE rgxlate [MAX_XLATE];

static void FreeXlate()
{
    ASSERT(g_nCurLanguage >= 0 && g_nCurLanguage < countof(rgszTranslateSection));

    if (g_hXlate != NULL && g_hXlate != INVALID_HANDLE_VALUE)
    {
		CloseHandle( g_hXlate );
    }
    rgchXlateData = NULL;
    ZeroMemory(rgxlate, sizeof(rgxlate));
}

static void LoadXlate(const CHAR* szXlateSection)
{

	// Load language file from hard drive
	{
		DWORD datOut = 0;
		char tempPath[MAX_PATH];
		sprintf(tempPath, "Q:\\Language\\%s.dat", szXlateSection);
		g_hXlate = CreateFile(tempPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_SYSTEM, NULL);
		if (g_hXlate != INVALID_HANDLE_VALUE)
		{
			DWORD datSize = GetFileSize(g_hXlate, NULL);
			rgchXlateData = (TCHAR*)malloc(datSize);
			ReadFile(g_hXlate, rgchXlateData, datSize, &datOut, NULL);
		}
	}

    {
        TCHAR* pch = rgchXlateData + 1; // Skip Unicode BOM
        int nXlate = 0;
        int nLine = 1;
        while (*pch != 0)
        {
            const TCHAR* pchKey;
            int cchKey;

            while (*pch == ' ' || *pch == '\t')
                pch += 1;

            if (*pch == '#')
            {
                pch += 1;
                while (*pch != 0 && *pch != '\r' && *pch != '\n')
                    pch += 1;
            }

            if (*pch == '\r')
            {
                pch += 1;
                if (*pch == '\n')
                    pch += 1;

                nLine += 1;
                continue;
            }

            if (*pch == '"')
            {
                pch += 1;
                pchKey = pch;
                while (*pch != 0 && *pch != '\r' && *pch != '\n' && *pch != '"')
                    pch += 1;
            }
            else
            {
                pchKey = pch;
                while ((unsigned)*pch > ' ' && *pch != '=')
                {
                    if (*pch == '_')
                    {
                        if (*(pch + 1) == '=' || *(pch + 1) == ' ')
                            *pch = ':';
                        else
                            *pch = ' ';
                    }

                    pch += 1;
                }
            }
            cchKey = (int)(pch - pchKey);

            if (*pch == '"')
                pch += 1;

            while (*pch == ' ' || *pch == '\t')
                pch += 1;

            if (*pch != '=')
            {
                TRACE(_T("\001%hs(%d): invalid translation data: expected an '='\n"), szXlateSection, nLine);
                return;
            }

            pch += 1;

            const TCHAR* pchValue;
            int cchValue;

            while (*pch == ' ' || *pch == '\t')
                pch += 1;

            if (*pch == '"')
                pch += 1;

            pchValue = pch;
            while (*pch != 0 && *pch != '\r' && *pch != '\n' && *pch != '"')
                pch += 1;
            cchValue = (int)(pch - pchValue);

            if (*pch == '"')
                pch += 1;

            while (*pch == ' ' || *pch == '\t')
                pch += 1;

            if (*pch == '#')
            {
                pch += 1;
                while (*pch != 0 && *pch != '\r')
                    pch += 1;
            }

            if (*pch == '\r')
            {
                pch += 1;
                if (*pch == '\n')
                    pch += 1;

                nLine += 1;
            }
            else
            {
                TRACE(_T("\001%hs(%d): expected end of line!\n"), szXlateSection, nLine);
                return;
            }

            if (cchKey == 0 || cchValue == 0)
            {
                TRACE(_T("\001%hs(%d): missing key or value\n"), szXlateSection, nLine);
                continue;
            }

            cchValue = ExpandCString((TCHAR*)pchValue, cchValue, pchValue, cchValue);
            if (cchValue < 0)
                return;

            ((TCHAR*)pchKey)[cchKey] = 0;
            ((TCHAR*)pchValue)[cchValue] = 0;

    //      TRACE(_T("XLATE: \"%s\" = \"%s\"\n"), pchKey, pchValue);

            ASSERT(nXlate < MAX_XLATE); // need to increase MAX_XLATE!
            rgxlate[nXlate].m_szKey = (TCHAR*)pchKey;
            rgxlate[nXlate].m_szValue = (TCHAR*)pchValue;
            nXlate += 1;
        }
    }
}

const TCHAR* Translate(const TCHAR* szString, TCHAR* szTranslate, int nLanguage/*=LANGUAGE_CURRENT*/)
{
    if (nLanguage == LANGUAGE_CURRENT)
        nLanguage = g_nCurLanguage;

    _tcscpy(szTranslate, szString);
    TCHAR* pch = _tcschr(szTranslate, '@');
    if (pch != NULL)
        *pch = '@';

    bool bFound = false;
    for (int i = 0; i < MAX_XLATE; i += 1)
    {
        if (rgxlate[i].m_szKey == NULL)
            break;

        if (_tcscmp(rgxlate[i].m_szKey, szTranslate) == 0)
        {
            _tcscpy(szTranslate, rgxlate[i].m_szValue);
            bFound = true;
            break;
        }
    }

#ifdef _DEBUG
    if (!bFound)
    {
        for (const TCHAR* pch = szTranslate; *pch != '\0'; pch += 1)
        {
            if (*pch != ' ')
            {
                // TODO: Start ASSERTing when this warning stop happening...
                TRACE(_T("Translate: '%s' not found in %hs\n"), szString, rgszTranslateSection[nLanguage]);
                break;
            }
        }
    }
#endif

    return szTranslate;
}



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

CTranslator::CTranslator()
{
}

CTranslator::~CTranslator()
{
}

#include "timezone.h"

int CTranslator::GetTimeZoneCount()
{
    return TIMEZONECOUNT;
}

CStrObject* CTranslator::GetTimeZoneText(int nTimeZone)
{
    if (nTimeZone < 0 || nTimeZone >= TIMEZONECOUNT)
        return new CStrObject;

    return Translate(g_timezoneinfo[nTimeZone].dispname);
}

int GetTimeZoneIndex(const TIME_ZONE_INFORMATION* tzinfo)
{
    ASSERT(tzinfo->StandardDate.wYear == 0 &&
           tzinfo->DaylightDate.wYear == 0);

    int index, bestmatch, bestresult;
    bestmatch = bestresult = -1;
    for (index=0; index < TIMEZONECOUNT; index++) {
        const struct TZINFO* srcinfo = &g_timezoneinfo[index];
        if (srcinfo->bias == tzinfo->Bias) {
            int match = 1;
            if (srcinfo->stdbias == tzinfo->StandardBias &&
                srcinfo->dltbias == tzinfo->DaylightBias) {
                match++;

                // Compare only the first 4 characters of the time zone name
                // because that's what we store in the EEPROM.
                // If everything matches, no need to continue searching.
                if (wcsncmp(srcinfo->stdname, tzinfo->StandardName, 4) == 0)
                    return index;
            }

            if (match > bestmatch) {
                bestmatch = match;
                bestresult = index;
            }
        }
    }

    return bestresult;
}

inline void TZDateToSysTime(const struct TZDATE* tzdate, SYSTEMTIME* systime)
{
    systime->wMonth = tzdate->month;
    systime->wDay = tzdate->day;
    systime->wDayOfWeek = tzdate->dayofweek;
    systime->wHour = tzdate->hour;
}

bool GetTimeZoneInfo(int index, TIME_ZONE_INFORMATION* tzinfo)
{
    if (index < 0 || index >= TIMEZONECOUNT)
        return false;

    const struct TZINFO* srcinfo = &g_timezoneinfo[index];
    memset(tzinfo, 0, sizeof(*tzinfo));

    tzinfo->Bias = srcinfo->bias;
    tzinfo->StandardBias = srcinfo->stdbias;
    tzinfo->DaylightBias = srcinfo->dltbias;

    wcsncpy(tzinfo->StandardName, srcinfo->stdname, countof(tzinfo->StandardName));
    wcsncpy(tzinfo->DaylightName,
            srcinfo->dltname ? srcinfo->dltname : srcinfo->stdname,
            countof(tzinfo->DaylightName));

    TZDateToSysTime(&srcinfo->stddate, &tzinfo->StandardDate);
    TZDateToSysTime(&srcinfo->dltdate, &tzinfo->DaylightDate);
    return true;
}

#ifdef _DEBUG
void CycleLanguage()
{
    FreeXlate();

    g_nCurLanguage += 1;
    if (g_nCurLanguage >= countof(rgszTranslateSection))
        g_nCurLanguage = 0;

    LoadXlate(rgszTranslateSection[g_nCurLanguage]);
}
#endif

void CTranslator::SetLanguage(int nNewLanguage)
{
    if (nNewLanguage == g_nCurLanguage && rgxlate[0].m_szKey != NULL)
        return;

    if (nNewLanguage < 0 || nNewLanguage >= LANGUAGE_COUNT)
    {
        TRACE(_T("CTranslator::SetLanguage: invalid language %d\n"), nNewLanguage);
        return;
    }

    g_nCurLanguage = nNewLanguage;
    FreeXlate();

    LoadXlate(rgszTranslateSection[g_nCurLanguage]);

    for (int i=0; i<countof(g_titles); i++)
    {
        if (g_titles[i].IsValid())
            g_titles[i].DeleteAll(false);
    }
}

CStrObject* CTranslator::Translate(const TCHAR* szString)
{
    TCHAR sz[MAX_TRANSLATE_LEN];
    return new CStrObject(::Translate(szString, sz));
}

CStrObject* CTranslator::TranslateStripColon(const TCHAR* szString)
{
    TCHAR sz[MAX_TRANSLATE_LEN];
    ::Translate(szString, sz);

    int cch = _tcslen(sz);
    if (cch > 0 && sz[cch - 1] == _T(':'))
        sz[cch - 1] = _T('\0');

    return new CStrObject(sz);
}

static const TCHAR rgszLangCodes [] = { _T("ENJADEFRESIT") };

const TCHAR* GetLanguageCode(TCHAR* sz)
{
    if (g_nCurLanguage < 0 || g_nCurLanguage > countof(rgszLangCodes) / 2)
    {
        sz[0] = 0;
        return sz;
    }

    sz[0] = rgszLangCodes[g_nCurLanguage * 2];
    sz[1] = rgszLangCodes[g_nCurLanguage * 2 + 1];
    sz[2] = 0;

    return sz;
}

CStrObject* CTranslator::GetLanguageCode()
{
    if (g_nCurLanguage < 0 || g_nCurLanguage > countof(rgszLangCodes) / 2)
        return new CStrObject; // empty string

    return new CStrObject(rgszLangCodes + g_nCurLanguage * 2, 2);
}

TCHAR GetDateSeparator()
{
    TCHAR chSep;

    switch (LOCALE_FROM_REGION_LANGUAGE(g_nCurRegion, g_nCurLanguage))
    {
    case LOCALE_NA_GERMAN: // Germany: d.m.y h:m
    case LOCALE_JAPAN_GERMAN: // Germany: d.m.y h:m
    case LOCALE_RESTOFWORLD_GERMAN: // Germany: d.m.y h:m
    case LOCALE_NA_ITALIAN: // Italian: d.m.y h:m
    case LOCALE_JAPAN_ITALIAN: // Italian: d.m.y h:m
    case LOCALE_RESTOFWORLD_ITALIAN: // Italian: d.m.y h:m
    case LOCALE_JAPAN_FRENCH: // French: d.m.y h:m
    case LOCALE_RESTOFWORLD_FRENCH: // French: d.m.y h:m
        chSep = '.';
        break;

    default: // the rest of the world
        chSep = '/';
    }

    return chSep;
}

CStrObject* CTranslator::GetDateSeparator()
{
    TCHAR chSep = ::GetDateSeparator();
    return new CStrObject(&chSep, 1);
}

CStrObject* CTranslator::FormatNumber(int nNumber)
{
    TCHAR szBuf [64];
    FormatInteger(szBuf, nNumber);
    return new CStrObject(szBuf);
}



void FormatInteger(TCHAR* szBuf, int nNumber, int locale/*=LOCALE_CURRENT*/)
{
    // Bug 7784: remove all number separator from the dash
    _stprintf(szBuf, _T("%d"), nNumber);

#if 0
    if (locale == LOCALE_CURRENT)
        locale = LOCALE_FROM_REGION_LANGUAGE(g_nCurRegion, g_nCurLanguage);

    TCHAR chSeperator;
    switch (LANGUAGE_FROM_LOCALE(locale))
    {
    default:
        chSeperator = ',';
        break;

    case LANGUAGE_FRENCH:
        chSeperator = ' ';
        break;

    case LANGUAGE_ITALIAN:
    case LANGUAGE_GERMAN:
        chSeperator = '.';
        break;
    }

    TCHAR* pch = szBuf;

    if (nNumber < 0)
    {
        *pch++ = '-';
        nNumber = -nNumber;
    }

    if (nNumber == 0)
    {
        *pch++ = '0';
        *pch = '\0';
        return;
    }

    bool bNonZero = false;
    for (int nDiv = 1000000000; nDiv > 0; nDiv /= 10)
    {
        int nDigit = nNumber / nDiv;
        if (nDigit != 0 || bNonZero)
        {
            bNonZero = true;
            *pch++ = nDigit + '0';
            nNumber -= nDigit * nDiv;

            if (nDiv == 1000000000 || nDiv == 1000000 || nDiv == 1000)
                *pch++ = chSeperator;
        }
    }

    *pch = '\0';
#endif
}

void FormatBlocks (TCHAR* szBuf, int nBlocks, int locale/*=LOCALE_CURRENT*/)
{
    if (nBlocks > MAX_BLOCKS_TO_SHOW) {
        FormatInteger(szBuf, MAX_BLOCKS_TO_SHOW, locale);
        _tcscat (szBuf, TEXT("+"));
    }
    else {
        FormatInteger(szBuf, nBlocks, locale);
    }
}

bool FormatTime(TCHAR* szBuf, SIZE_T chLen, SYSTEMTIME* pst, int locale/*=LOCALE_CURRENT*/)
{
    bool bPM;
    int nHour;
    TCHAR szDate[32], szTime[32];

    if (locale == LOCALE_CURRENT)
        locale = LOCALE_FROM_REGION_LANGUAGE(g_nCurRegion, g_nCurLanguage);

    // Format date
    TCHAR chSep = GetDateSeparator();

    switch (locale)
    {
    case LOCALE_NA_ENGLISH: // US: mm/dd/yy
        _sntprintf(szDate, countof(szDate), _T("%02d%c%02d%c%d "), pst->wMonth, chSep, pst->wDay, chSep, pst->wYear);
        break;

    case LOCALE_NA_JAPANESE: // Japanese: yy/mm/dd
    case LOCALE_JAPAN_JAPANESE:
    case LOCALE_RESTOFWORLD_JAPANESE:
        _sntprintf(szDate, countof(szDate), _T("%d%c%02d%c%02d "), pst->wYear, chSep, pst->wMonth, chSep, pst->wDay);
        break;

    default: // Rest: dd?mm?yy
        _sntprintf(szDate, countof(szDate), _T("%02d%c%02d%c%d "), pst->wDay, chSep, pst->wMonth, chSep, pst->wYear);
    }

    // Format time
    switch (locale)
    {
    case LOCALE_NA_ENGLISH: // US: h:m ampm
    case LOCALE_JAPAN_ENGLISH:
    case LOCALE_RESTOFWORLD_ENGLISH:
    case LOCALE_NA_JAPANESE: // Japanese: h:m ampm
    case LOCALE_JAPAN_JAPANESE:
    case LOCALE_RESTOFWORLD_JAPANESE:
        bPM = pst->wHour >= 12;
        nHour = pst->wHour;

        if (nHour >= 12)
            nHour -= 12;
        if (nHour == 0)
            nHour = 12;

        _sntprintf(szTime, countof(szTime), _T("%d:%02d %s"), nHour, pst->wMinute, bPM ? _T("PM") : _T("AM"));
        break;

    default: // the rest of the world
        _sntprintf(szTime, countof(szTime), _T("%d:%02d"), pst->wHour, pst->wMinute);
    }

    if (chLen < lstrlen(szDate) + lstrlen(szTime) + sizeof(_T('\0')))
    {
        return false;
    }

    lstrcpy(szBuf, szDate);
    lstrcat(szBuf, szTime);
    return true;
}

void Locale_Exit()
{
    FreeXlate();
}
