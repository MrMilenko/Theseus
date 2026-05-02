// locale.cpp: desktop CLocale node. Per-locale string lookups,
// language enumeration, ISO639 code mapping. Counterpart to
// engine/locale_node.cpp.

#include "std.h"
#include "dashapp.h"
#include "node.h"
#include "runner.h"
#include "dashlocale.h"
#include "xap_compile.h"
#include "titlecollection.h"

#define MAX_XLATE 1000 // This is the maximum number of terms in a translation file

int g_nCurLanguage;
int g_nCurRegion;

static const char* rgszTranslateSection[] =
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
    char* m_szKey;
    char* m_szValue;
};

static HANDLE g_hXlate;
static char* rgchXlateData;
static XLATE rgxlate [MAX_XLATE];

static void FreeXlate()
{
    ASSERT(g_nCurLanguage >= 0 && g_nCurLanguage < countof(rgszTranslateSection));

    if (g_hXlate != NULL && g_hXlate != INVALID_HANDLE_VALUE)
    {
		CloseHandle( g_hXlate );
		g_hXlate = NULL;
    }
    free(rgchXlateData);
    rgchXlateData = NULL;
    memset(rgxlate, 0, sizeof(rgxlate));
}

static void LoadXlate(const char* szXlateSection)
{

	//Block: load language file from hd
	{
		uint32_t datOut = NULL;
		char * tempPath;
		tempPath = (char*)malloc(MAX_PATH);
		sprintf( tempPath, "Q:\\Language\\%s.dat", szXlateSection );
		g_hXlate = CreateFile( tempPath, GENERIC_ALL, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_SYSTEM, NULL );
	    if (g_hXlate != INVALID_HANDLE_VALUE)
        {
			uint32_t datSize = GetFileSize( g_hXlate, NULL );
			rgchXlateData = (char*)malloc(datSize);
			ReadFile( g_hXlate, rgchXlateData, datSize, LPDW(&datOut), NULL);
        }
		free(tempPath);
	}

	if (rgchXlateData == NULL)
	{
		TRACE("\001LoadXlate: failed to load %hs.dat\n", szXlateSection);
		return;
	}

	// Language .dat files are UTF-16LE (Xbox char=wchar_t).
	// On desktop char=char, so convert in-place: extract low byte of each wchar.
	{
		unsigned char* src = (unsigned char*)rgchXlateData;
		// Check for UTF-16LE BOM (FF FE)
		int srcOff = 0;
		if (src[0] == 0xFF && src[1] == 0xFE)
			srcOff = 2; // skip BOM

		uint32_t datSize = GetFileSize(g_hXlate, NULL);
		int nChars = ((int)datSize - srcOff) / 2;
		char* converted = (char*)malloc(nChars + 1);
		for (int i = 0; i < nChars; i++)
			converted[i] = (char)src[srcOff + i * 2]; // low byte of UTF-16LE
		converted[nChars] = '\0';
		free(rgchXlateData);
		rgchXlateData = converted;
	}

    {
        char* pch = rgchXlateData; // Already converted, BOM stripped
        int nXlate = 0;
        int nLine = 1;
        while (*pch != 0)
        {
            const char* pchKey;
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
                TRACE("\001%hs(%d): invalid translation data: expected an '='\n", szXlateSection, nLine);
                return;
            }

            pch += 1;

            const char* pchValue;
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
                TRACE("\001%hs(%d): expected end of line!\n", szXlateSection, nLine);
                return;
            }

            if (cchKey == 0 || cchValue == 0)
            {
                TRACE("\001%hs(%d): missing key or value\n", szXlateSection, nLine);
                continue;
            }

            cchValue = ExpandCString((char*)pchValue, cchValue, pchValue, cchValue);
            if (cchValue < 0)
                return;

            ((char*)pchKey)[cchKey] = 0;
            ((char*)pchValue)[cchValue] = 0;

    //      TRACE("XLATE: \"%s\" = \"%s\"\n", pchKey, pchValue);

            ASSERT(nXlate < MAX_XLATE); // need to increase MAX_XLATE!
            rgxlate[nXlate].m_szKey = (char*)pchKey;
            rgxlate[nXlate].m_szValue = (char*)pchValue;
            nXlate += 1;
        }
    }
}

const char* Translate(const char* szString, char* szTranslate, int nLanguage/*=LANGUAGE_CURRENT*/)
{
    if (nLanguage == LANGUAGE_CURRENT)
        nLanguage = g_nCurLanguage;

    strcpy(szTranslate, szString);
    char* pch = strchr(szTranslate, '@');
    if (pch != NULL)
        *pch = '@';

    bool bFound = false;
    for (int i = 0; i < MAX_XLATE; i += 1)
    {
        if (rgxlate[i].m_szKey == NULL)
            break;

        if (strcmp(rgxlate[i].m_szKey, szTranslate) == 0)
        {
            strcpy(szTranslate, rgxlate[i].m_szValue);
            bFound = true;
            break;
        }
    }

    // Strings not found in English.dat may be handled by xlate.ini overlay.
    // This is normal operation, not an error.

    return szTranslate;
}



class CTranslator : public CNode
{
    DECLARE_NODE(CTranslator, CNode)
public:
    CTranslator();
    ~CTranslator();

    void SetLanguage(int nNewLanguage);
    CStrObject* Translate(const char* szString);
    CStrObject* TranslateStripColon(const char* szString);
    CStrObject* GetLanguageCode();
    CStrObject* GetDateSeparator();
    CStrObject* FormatNumber(int nNumber);

    int GetTimeZoneCount();
    CStrObject* GetTimeZoneText(int nTimeZone);

    DECLARE_NODE_FUNCTIONS()
};

IMPLEMENT_NODE("Translator", CTranslator, CNode)

#undef _FND_CLASS
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

void CycleLanguage()
{
    FreeXlate();

    g_nCurLanguage += 1;
    if (g_nCurLanguage >= countof(rgszTranslateSection))
        g_nCurLanguage = 0;

    LoadXlate(rgszTranslateSection[g_nCurLanguage]);
}

void CTranslator::SetLanguage(int nNewLanguage)
{
    if (nNewLanguage == g_nCurLanguage && rgxlate[0].m_szKey != NULL)
        return;

    if (nNewLanguage < 0 || nNewLanguage >= LANGUAGE_COUNT)
    {
        TRACE("CTranslator::SetLanguage: invalid language %d\n", nNewLanguage);
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

CStrObject* CTranslator::Translate(const char* szString)
{
    char sz[MAX_TRANSLATE_LEN];
    return new CStrObject(::Translate(szString, sz));
}

CStrObject* CTranslator::TranslateStripColon(const char* szString)
{
    char sz[MAX_TRANSLATE_LEN];
    ::Translate(szString, sz);

    int cch = strlen(sz);
    if (cch > 0 && sz[cch - 1] == ':')
        sz[cch - 1] = '\0';

    return new CStrObject(sz);
}

static const char rgszLangCodes [] = { "ENJADEFRESIT" };

const char* GetLanguageCode(char* sz)
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

char GetDateSeparator()
{
    char chSep;

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
    char chSep = ::GetDateSeparator();
    return new CStrObject(&chSep, 1);
}

CStrObject* CTranslator::FormatNumber(int nNumber)
{
    char szBuf [64];
    FormatInteger(szBuf, nNumber);
    return new CStrObject(szBuf);
}



void FormatInteger(char* szBuf, int nNumber, int locale/*=LOCALE_CURRENT*/)
{
    // Bug 7784: remove all number separators from the dash
    sprintf(szBuf, "%d", nNumber);
}

void FormatBlocks (char* szBuf, int nBlocks, int locale/*=LOCALE_CURRENT*/)
{
    if (nBlocks > MAX_BLOCKS_TO_SHOW) {
        FormatInteger(szBuf, MAX_BLOCKS_TO_SHOW, locale);
        strcat (szBuf, TEXT("+"));
    }
    else {
        FormatInteger(szBuf, nBlocks, locale);
    }
}

bool FormatTime(char* szBuf, SIZE_T chLen, SYSTEMTIME* pst, int locale/*=LOCALE_CURRENT*/)
{
    bool bPM;
    int nHour;
    char szDate[32], szTime[32];

    if (locale == LOCALE_CURRENT)
        locale = LOCALE_FROM_REGION_LANGUAGE(g_nCurRegion, g_nCurLanguage);

    // Format date
    char chSep = GetDateSeparator();

    switch (locale)
    {
    case LOCALE_NA_ENGLISH: // US: mm/dd/yy
        snprintf(szDate, countof(szDate), "%02d%c%02d%c%d ", pst->wMonth, chSep, pst->wDay, chSep, pst->wYear);
        break;

    case LOCALE_NA_JAPANESE: // Japanese: yy/mm/dd
    case LOCALE_JAPAN_JAPANESE:
    case LOCALE_RESTOFWORLD_JAPANESE:
        snprintf(szDate, countof(szDate), "%d%c%02d%c%02d ", pst->wYear, chSep, pst->wMonth, chSep, pst->wDay);
        break;

    default: // Rest: dd?mm?yy
        snprintf(szDate, countof(szDate), "%02d%c%02d%c%d ", pst->wDay, chSep, pst->wMonth, chSep, pst->wYear);
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

        snprintf(szTime, countof(szTime), "%d:%02d %s", nHour, pst->wMinute, bPM ? "PM" : "AM");
        break;

    default: // the rest of the world
        snprintf(szTime, countof(szTime), "%d:%02d", pst->wHour, pst->wMinute);
    }

    if (chLen < strlen(szDate) + strlen(szTime) + sizeof('\0'))
    {
        return false;
    }

    strcpy(szBuf, szDate);
    strcat(szBuf, szTime);
    return true;
}

void Locale_Exit()
{
    FreeXlate();
}
