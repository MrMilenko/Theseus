// keyboard_node.cpp: CKeyboard, the on-screen keyboard node.
//
// Provides text input via a grid of selectable keys. Three Western modes
// (alpha, symbol, accent) and three Japanese modes (hiragana, katakana,
// English). XAP scripts drive navigation and receive callbacks (OnDone,
// OnError).
//
// CKeyboard confirmed in the 5960 retail XBE: class descriptor
// "CKeyboard" at 0x00029d38, node-type string "Keyboard" at 0x00029d4c.
// See docs/decomp/Keyboard.md.
//   FND table at 0x00013da4
//   Class registration at 0x0001f368

#include "std.h"
#include "theseus.h"
#include "node.h"
#include "runner.h"

extern CNode* GetTextNode(const TCHAR* szText, float nWidth);
extern bool g_bActiveKey;
extern TCHAR* g_szText;
extern int g_nTextChar;
extern int g_nCurLanguage;


// ===== Constants ============================================================

#define KB_MAX_LENGTH   31
#define KB_COLS         11

// Western mode indices
enum { MODE_ALPHA = 0, MODE_SYMBOL, MODE_ACCENT };

// Japanese mode indices
enum { JMODE_HIRAGANA = 0, JMODE_KATAKANA, JMODE_ENGLISH };

// Special key codes
enum {
	KEY_SHIFT = 1, KEY_CAPS, KEY_ALPHA, KEY_SYM, KEY_ACC,
	KEY_LT, KEY_RT, KEY_BS, KEY_DONE, KEY_SPACE,
	KEY_HIRAGANA, KEY_KATAKANA, KEY_ENGLISH, KEY_VOID
};


// ===== Unicode character defines ============================================

#define _nbsp     0x00a0
#define _iexcl    0x00a1
#define _pound    0x00a3
#define _yen      0x00a5
#define _laquo    0x00ab
#define _raquo    0x00bb
#define _iquest   0x00bf
#define _agrave   0x00e0
#define _aacute   0x00e1
#define _acirc    0x00e2
#define _auml     0x00e4
#define _aelig    0x00e6
#define _ccedil   0x00e7
#define _egrave   0x00e8
#define _eacute   0x00e9
#define _ecirc    0x00ea
#define _euml     0x00eb
#define _igrave   0x00ec
#define _iacute   0x00ed
#define _icirc    0x00ee
#define _iuml     0x00ef
#define _ntilde   0x00f1
#define _ograve   0x00f2
#define _oacute   0x00f3
#define _ocirc    0x00f4
#define _otilde   0x00f5
#define _ugrave   0x00f9
#define _uacute   0x00fa
#define _ucirc    0x00fb
#define _uuml     0x00fc
#define _yacute   0x00fd
#define _yuml     0x00ff
#define _szlig    0x00df
#define _euro     0x20ac


// ===== Key layout tables ====================================================
// Each table is a flat array of WORD, KB_COLS entries per row.
// Special keys use the KEY_* constants above.

static const WORD s_alphaKeys[] =
{
	KEY_DONE,   L'1', L'2', L'3', L'4', L'5', L'6', L'7', L'8', L'9', L'0',
	KEY_SHIFT,  L'a', L'b', L'c', L'd', L'e', L'f', L'g', L'h', L'i', L'j',
	KEY_CAPS,   L'k', L'l', L'm', L'n', L'o', L'p', L'q', L'r', L's', L't',
	KEY_ACC,    L'u', L'v', L'w', L'x', L'y', L'z', KEY_BS, KEY_BS, KEY_BS, KEY_BS,
	KEY_SYM,    KEY_SPACE, KEY_SPACE, KEY_SPACE, KEY_SPACE, KEY_SPACE, KEY_SPACE, KEY_LT, KEY_LT, KEY_RT, KEY_RT,
};

static const WORD s_symbolKeys[] =
{
	KEY_DONE,   L'(',  L')',  L'&',  L'_',  L'^',    L'%',  L'\\', L'/',  L'@',  L'#',
	KEY_SHIFT,  L'[',  L']',  L'$',  _pound, _euro,  _yen,  L';',  L':',  L'\'', L'"',
	KEY_CAPS,   L'<',  L'>',  L'?',  L'!',  _iquest, _iexcl, L'-', L'*',  L'+',  L'=',
	KEY_ACC,    L'{',  L'}',  _laquo, _raquo, L',',  L'.',  KEY_BS, KEY_BS, KEY_BS, KEY_BS,
	KEY_ALPHA,  KEY_SPACE, KEY_SPACE, KEY_SPACE, KEY_SPACE, KEY_SPACE, KEY_SPACE, KEY_LT, KEY_LT, KEY_RT, KEY_RT,
};

static const WORD s_accentKeys[] =
{
	KEY_DONE,   L'1',     L'2',     L'3',     L'4',     L'5',     L'6',     L'7',     L'8',     L'9',     L'0',
	KEY_SHIFT,  _agrave,  _aacute,  _acirc,   _auml,    _egrave,  _eacute,  _ecirc,   _euml,    _igrave,  _iacute,
	KEY_CAPS,   _icirc,   _iuml,    _ograve,  _oacute,  _ocirc,   _otilde,  _ugrave,  _uacute,  _ucirc,   _uuml,
	KEY_ALPHA,  _yacute,  _yuml,    _ccedil,  _szlig,   _ntilde,  _aelig,   KEY_BS,   KEY_BS,   KEY_BS,   KEY_BS,
	KEY_SYM,    KEY_SPACE, KEY_SPACE, KEY_SPACE, KEY_SPACE, KEY_SPACE, KEY_SPACE, KEY_LT, KEY_LT, KEY_RT, KEY_RT,
};

static const WORD* s_westernModes[] = { s_alphaKeys, s_symbolKeys, s_accentKeys };

static const WORD s_hiraganaKeys[] =
{
	0x3042, 0x3044, 0x3046, 0x3048, 0x304A, 0x308F, 0x3092, 0x3093, KEY_VOID, KEY_VOID, KEY_HIRAGANA,
	0x304B, 0x304D, 0x304F, 0x3051, 0x3053, 0x3041, 0x3043, 0x3045, 0x3047, 0x3049, KEY_KATAKANA,
	0x3055, 0x3057, 0x3059, 0x305B, 0x305D, 0x3063, 0x3083, 0x3085, 0x3087, 0x308E, KEY_ENGLISH,
	0x305F, 0x3061, 0x3064, 0x3066, 0x3068, 0x304C, 0x304E, 0x3050, 0x3052, 0x3054, KEY_SPACE,
	0x306A, 0x306B, 0x306C, 0x306D, 0x306E, 0x3056, 0x3058, 0x305A, 0x305C, 0x305E, KEY_BS,
	0x306F, 0x3072, 0x3075, 0x3078, 0x307B, 0x3060, 0x3062, 0x3065, 0x3067, 0x3069, KEY_LT,
	0x307E, 0x307F, 0x3080, 0x3081, 0x3082, 0x3070, 0x3073, 0x3076, 0x3079, 0x307C, KEY_RT,
	0x3084, KEY_VOID, 0x3086, KEY_VOID, 0x3088, 0x3071, 0x3074, 0x3077, 0x307A, 0x307D, KEY_VOID,
	0x3089, 0x308A, 0x308B, 0x308C, 0x308D, 0x30FC, 0x3001, 0x3002, 0x300C, 0x300D, KEY_DONE,
};

static const WORD s_katakanaKeys[] =
{
	0x30A2, 0x30A4, 0x30A6, 0x30A8, 0x30AA, 0x30EF, 0x30F2, 0x30F3, 0x30F4, KEY_VOID, KEY_HIRAGANA,
	0x30AB, 0x30AD, 0x30AF, 0x30B1, 0x30B3, 0x30A1, 0x30A3, 0x30A5, 0x30A7, 0x30A9, KEY_KATAKANA,
	0x30B5, 0x30B7, 0x30B9, 0x30BB, 0x30BD, 0x30C3, 0x30E3, 0x30E5, 0x30E7, 0x30EE, KEY_ENGLISH,
	0x30BF, 0x30C1, 0x30C4, 0x30C6, 0x30C8, 0x30AC, 0x30AE, 0x30B0, 0x30B2, 0x30B4, KEY_SPACE,
	0x30CA, 0x30CB, 0x30CC, 0x30CD, 0x30CE, 0x30B6, 0x30B8, 0x30BA, 0x30BC, 0x30BE, KEY_BS,
	0x30CF, 0x30D2, 0x30D5, 0x30D8, 0x30DB, 0x30C0, 0x30C2, 0x30C5, 0x30C7, 0x30C9, KEY_LT,
	0x30DE, 0x30DF, 0x30E0, 0x30E1, 0x30E2, 0x30D0, 0x30D3, 0x30D6, 0x30D9, 0x30DC, KEY_RT,
	0x30E4, KEY_VOID, 0x30E6, KEY_VOID, 0x30E8, 0x30D1, 0x30D4, 0x30D7, 0x30DA, 0x30DD, KEY_VOID,
	0x30E9, 0x30EA, 0x30EB, 0x30EC, 0x30ED, 0x30FC, 0x3001, 0x3002, 0x300C, 0x300D, KEY_DONE,
};

static const WORD s_englishKeys[] =
{
	0x41, 0x42, 0x43, 0x44, 0x45, 0x61, 0x62, 0x63, 0x64, 0x65, KEY_HIRAGANA,
	0x46, 0x47, 0x48, 0x49, 0x4A, 0x66, 0x67, 0x68, 0x69, 0x6A, KEY_KATAKANA,
	0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, KEY_ENGLISH,
	0x50, 0x51, 0x52, 0x53, 0x54, 0x70, 0x71, 0x72, 0x73, 0x74, KEY_SPACE,
	0x55, 0x56, 0x57, 0x58, 0x59, 0x75, 0x76, 0x77, 0x78, 0x79, KEY_BS,
	0x5A, 0x22, 0x27, 0x40, 0x23, 0x7A, 0x28, 0x29, 0x7B, 0x7D, KEY_LT,
	0x26, 0x5E, 0x24, 0xA5, 0x25, 0x2D, 0x2B, 0x3D, 0x2A, 0x2F, KEY_RT,
	0x30, 0x31, 0x32, 0x33, 0x34, 0x3F, 0x21, 0x3A, 0x3B, 0x5C, KEY_VOID,
	0x35, 0x36, 0x37, 0x38, 0x39, 0x3C, 0x3E, 0x2C, 0x2E, 0x5F, KEY_DONE,
};

static const WORD* s_japaneseModes[] = { s_hiraganaKeys, s_katakanaKeys, s_englishKeys };


// ===== Key label lookup =====================================================

static const TCHAR* GetKeyLabel(WORD key)
{
	switch (key)
	{
	case KEY_SHIFT:    return _T("SHIFT");
	case KEY_CAPS:     return _T("CAPS LOCK");
	case KEY_ALPHA:    return _T("ALPHABET");
	case KEY_SYM:      return _T("SYMBOLS");
	case KEY_ACC:      return _T("ACCENTS");
	case KEY_ENGLISH:  return _T("ENGLISH");
	case KEY_HIRAGANA: return _T("HIRAGANA");
	case KEY_KATAKANA: return _T("KATAKANA");
	case KEY_LT:       return _T("<");
	case KEY_RT:       return _T(">");
	case KEY_BS:       return _T("BACKSPACE");
	case KEY_DONE:     return _T("DONE");
	case KEY_SPACE:    return _T("SPACE");
	case KEY_VOID:     return _T(" ");
	default:           return NULL;
	}
}


// ===== Helpers ==============================================================

static bool IsJapanese() { return g_nCurLanguage == 1; }

static int GetRowCount() { return IsJapanese() ? 8 : 4; }

static const WORD* GetActiveLayout(int westernMode, int japaneseMode)
{
	return IsJapanese() ? s_japaneseModes[japaneseMode] : s_westernModes[westernMode];
}

static WORD KeyAt(const WORD* layout, int row, int col)
{
	return layout[row * KB_COLS + col];
}

static bool IsValidKey(const WORD* layout, int row, int col)
{
	return KeyAt(layout, row, col) != KEY_VOID;
}


// ===== CKeyboard ============================================================

class CKeyboard : public CNode
{
	DECLARE_NODE(CKeyboard, CNode)
public:
	CKeyboard();
	~CKeyboard();

	CNodeArray m_keys;
	CNode* m_frame;
	CNode* m_text;
	CNode* m_control;

	TCHAR* m_string;

	bool m_shift;
	bool m_caps;
	int m_mode;
	int m_jmode;
	bool m_asteriskFormatting;

	void selectKey(int nRow, int nColumn);
	void selectUp();
	void selectDown();
	void selectLeft();
	void selectRight();
	void activate();

	void Backspace();
	void Delete();
	void CursorLeft();
	void CursorRight();
	void Shift();
	void CycleMode();
	void Insert(const TCHAR* szInsert);

	bool GetAsteriskFormattingState() { return m_asteriskFormatting; }
	void SetAsteriskFormattingState(bool b) { m_asteriskFormatting = b; }

	void Advance(float nSeconds);
	void Render();
	bool OnSetProperty(const PRD* pprd, const void* pvValue);

protected:
	TCHAR m_rgch[KB_MAX_LENGTH + 1];
	int m_nLength;
	int m_nCursor;
	int m_nRow;
	int m_nColumn;

	const WORD* ActiveLayout() { return GetActiveLayout(m_mode, m_jmode); }
	TCHAR ApplyCase(TCHAR ch);
	void ClearSelection();
	void InsertChar(TCHAR ch);

	DECLARE_NODE_PROPS()
	DECLARE_NODE_FUNCTIONS();
};

IMPLEMENT_NODE("Keyboard", CKeyboard, CNode)

START_NODE_PROPS(CKeyboard, CNode)
	NODE_PROP(pt_nodearray, CKeyboard, keys)
	NODE_PROP(pt_node, CKeyboard, frame)
	NODE_PROP(pt_node, CKeyboard, text)
	NODE_PROP(pt_node, CKeyboard, control)
	NODE_PROP(pt_integer, CKeyboard, mode)
	NODE_PROP(pt_integer, CKeyboard, jmode)
	NODE_PROP(pt_boolean, CKeyboard, shift)
	NODE_PROP(pt_boolean, CKeyboard, caps)
	NODE_PROP(pt_string, CKeyboard, string)
END_NODE_PROPS()

#define _FND_CLASS CKeyboard
START_NODE_FUN(CKeyboard, CNode)
	NODE_FUN_VII(selectKey)
	NODE_FUN_VV(selectUp)
	NODE_FUN_VV(selectDown)
	NODE_FUN_VV(selectLeft)
	NODE_FUN_VV(selectRight)
	NODE_FUN_VV(activate)
	NODE_FUN_VV(Backspace)
	NODE_FUN_VV(Delete)
	NODE_FUN_VV(CursorLeft)
	NODE_FUN_VV(CursorRight)
	NODE_FUN_VV(Shift)
	NODE_FUN_VV(CycleMode)
	NODE_FUN_VS(Insert)
	NODE_FUN_IV(GetAsteriskFormattingState)
	NODE_FUN_VI(SetAsteriskFormattingState)
END_NODE_FUN()
#undef _FND_CLASS

CKeyboard::CKeyboard() :
	m_control(NULL),
	m_frame(NULL),
	m_text(NULL),
	m_mode(MODE_ALPHA),
	m_jmode(JMODE_HIRAGANA),
	m_shift(false),
	m_caps(false),
	m_asteriskFormatting(false),
	m_string(NULL)
{
	m_nLength = 0;
	m_nCursor = -1;
	m_nRow = 0;
	m_nColumn = 0;
}

CKeyboard::~CKeyboard()
{
	if (m_text != NULL)
		m_text->Release();

	if (m_frame != NULL)
		m_frame->Release();

	if (m_control != NULL)
		m_control->Release();

	delete [] m_string;
}

void CKeyboard::ClearSelection()
{
	if (m_nCursor == -1)
	{
		m_nLength = 0;
		m_nCursor = 0;
	}
}

TCHAR CKeyboard::ApplyCase(TCHAR ch)
{
	if (m_caps ^ m_shift)
		ch = (TCHAR)toupper((unsigned char)ch);
	return ch;
}

void CKeyboard::InsertChar(TCHAR ch)
{
	ClearSelection();

	if (m_nLength >= KB_MAX_LENGTH)
	{
		CallFunction(this, _T("OnError"));
		return;
	}

	MoveMemory(&m_rgch[m_nCursor + 1], &m_rgch[m_nCursor], (m_nLength - m_nCursor) * sizeof(TCHAR));
	m_rgch[m_nCursor] = ch;
	m_nCursor += 1;
	m_nLength += 1;
}


// ===== Navigation ===========================================================

void CKeyboard::selectKey(int nRow, int nColumn)
{
	int rows = GetRowCount();

	m_nRow = (nRow < 0 || nRow > rows) ? 0 : nRow;
	m_nColumn = (nColumn < 0 || nColumn > 10) ? 0 : nColumn;
}

void CKeyboard::selectUp()
{
	const WORD* layout = ActiveLayout();
	int rows = GetRowCount();
	int attempts = rows + 1;

	while (attempts-- > 0)
	{
		m_nRow = (m_nRow == 0) ? rows : m_nRow - 1;
		if (IsValidKey(layout, m_nRow, m_nColumn))
			return;
	}
}

void CKeyboard::selectDown()
{
	const WORD* layout = ActiveLayout();
	int rows = GetRowCount();
	int attempts = rows + 1;

	while (attempts-- > 0)
	{
		m_nRow = (m_nRow == rows) ? 0 : m_nRow + 1;
		if (IsValidKey(layout, m_nRow, m_nColumn))
			return;
	}
}

void CKeyboard::selectLeft()
{
	const WORD* layout = ActiveLayout();

	if (IsJapanese())
	{
		int attempts = KB_COLS;
		while (attempts-- > 0)
		{
			m_nColumn = (m_nColumn == 0) ? 10 : m_nColumn - 1;
			if (IsValidKey(layout, m_nRow, m_nColumn))
				return;
		}
	}
	else
	{
		if (m_nColumn == 0)
		{
			m_nColumn = 10;
			return;
		}

		if (m_nRow == 3)
		{
			m_nColumn = (m_nColumn > 6) ? 6 : m_nColumn - 1;
		}
		else if (m_nRow == 4)
		{
			if (m_nColumn > 8)
				m_nColumn = 8;
			else if (m_nColumn > 6)
				m_nColumn = 6;
			else
				m_nColumn = 0;
		}
		else
		{
			m_nColumn -= 1;
		}
	}
}

void CKeyboard::selectRight()
{
	const WORD* layout = ActiveLayout();

	if (IsJapanese())
	{
		int attempts = KB_COLS;
		while (attempts-- > 0)
		{
			m_nColumn = (m_nColumn == 10) ? 0 : m_nColumn + 1;
			if (IsValidKey(layout, m_nRow, m_nColumn))
				return;
		}
	}
	else
	{
		if (m_nRow == 3)
		{
			if (m_nColumn >= 7)
			{
				m_nColumn = 0;
				return;
			}
			m_nColumn += 1;
		}
		else if (m_nRow == 4)
		{
			if (m_nColumn >= 9)
			{
				m_nColumn = 0;
				return;
			}

			if (m_nColumn == 0)
				m_nColumn = 1;
			else if (m_nColumn < 7)
				m_nColumn = 7;
			else if (m_nColumn < 9)
				m_nColumn = 9;
		}
		else
		{
			if (m_nColumn == 10)
			{
				m_nColumn = 0;
				return;
			}
			m_nColumn += 1;
		}
	}
}


// ===== Key activation =======================================================

void CKeyboard::activate()
{
	const WORD* layout = ActiveLayout();
	WORD key = KeyAt(layout, m_nRow, m_nColumn);

	switch (key)
	{
	case KEY_DONE:
		{
			// Reject all-whitespace strings
			int i;
			for (i = 0; i < m_nLength; i += 1)
			{
				if (m_rgch[i] != ' ')
					break;
			}

			if (i == m_nLength)
			{
				CallFunction(this, _T("OnError"));
				break;
			}

			// Trim trailing spaces
			TCHAR* pEnd = m_rgch + (m_nLength - 1);
			while (*pEnd == _T(' '))
			{
				*pEnd = _T('\0');
				pEnd--;
			}

			// Trim leading spaces
			TCHAR* pStart = m_rgch;
			while (*pStart == _T(' '))
				pStart++;

			if (pStart != m_rgch)
				MoveMemory(m_rgch, pStart, sizeof(TCHAR) * (_tcslen(pStart) + 1));

			delete [] m_string;
			m_string = new TCHAR [m_nLength + 1];
			CopyChars(m_string, m_rgch, m_nLength);
			m_string[m_nLength] = 0;

			CallFunction(this, _T("OnDone"));
		}
		break;

	case KEY_SHIFT:    Shift(); break;
	case KEY_CAPS:
		if (m_mode != MODE_ALPHA && m_mode != MODE_ACCENT)
			CallFunction(this, _T("OnError"));
		else
			m_caps = !m_caps;
		break;

	case KEY_ALPHA:    m_mode = MODE_ALPHA; break;
	case KEY_SYM:      m_mode = MODE_SYMBOL; break;
	case KEY_ACC:      m_mode = MODE_ACCENT; break;
	case KEY_HIRAGANA: m_jmode = JMODE_HIRAGANA; break;
	case KEY_KATAKANA: m_jmode = JMODE_KATAKANA; break;
	case KEY_ENGLISH:  m_jmode = JMODE_ENGLISH; break;
	case KEY_LT:       CursorLeft(); break;
	case KEY_RT:       CursorRight(); break;
	case KEY_BS:       Backspace(); break;

	case KEY_SPACE:
		InsertChar(_T(' '));
		break;

	default:
		InsertChar(ApplyCase((TCHAR)key));
		m_shift = false;
		break;
	}
}


// ===== Text editing =========================================================

void CKeyboard::Backspace()
{
	ClearSelection();

	if (m_nCursor > 0)
	{
		MoveMemory(&m_rgch[m_nCursor - 1], &m_rgch[m_nCursor], (m_nLength - m_nCursor) * sizeof(TCHAR));
		m_nLength -= 1;
		m_nCursor -= 1;
	}
	else
	{
		CallFunction(this, _T("OnError"));
	}
}

void CKeyboard::Delete()
{
	ClearSelection();

	if (m_nCursor < m_nLength)
	{
		CursorRight();
		Backspace();
	}
	else
	{
		CallFunction(this, _T("OnError"));
	}
}

void CKeyboard::CursorLeft()
{
	if (m_nCursor == -1)
		m_nCursor = 0;
	else if (m_nCursor > 0)
		m_nCursor -= 1;
	else
		CallFunction(this, _T("OnError"));
}

void CKeyboard::CursorRight()
{
	if (m_nCursor == -1)
		m_nCursor = m_nLength;
	else if (m_nCursor < m_nLength)
		m_nCursor += 1;
	else
		CallFunction(this, _T("OnError"));
}

void CKeyboard::Shift()
{
	if (m_mode != MODE_ALPHA && m_mode != MODE_ACCENT)
		CallFunction(this, _T("OnError"));
	else
		m_shift = !m_shift;
}

void CKeyboard::CycleMode()
{
	m_mode = (m_mode + 1 > 2) ? 0 : m_mode + 1;
	m_jmode = (m_jmode + 1 > 2) ? 0 : m_jmode + 1;
}

void CKeyboard::Insert(const TCHAR* szInsert)
{
	ClearSelection();

	int cch = _tcslen(szInsert);

	if (m_nLength + cch > KB_MAX_LENGTH)
	{
		CallFunction(this, _T("OnError"));
		return;
	}

	MoveMemory(&m_rgch[m_nCursor + cch], &m_rgch[m_nCursor], (m_nLength - m_nCursor) * sizeof(TCHAR));
	CopyChars(&m_rgch[m_nCursor], szInsert, cch);
	m_nCursor += cch;
	m_nLength += cch;
	m_shift = false;
}


// ===== Properties ===========================================================

bool CKeyboard::OnSetProperty(const PRD* pprd, const void* pvValue)
{
	if (PTR2INT(pprd->pbOffset) == offsetof(m_string))
	{
		TCHAR* szNew = *(TCHAR**)pvValue;
		if (szNew == NULL)
			szNew = _T("");
		int cch = _tcslen(szNew);
		if (cch > KB_MAX_LENGTH)
			cch = KB_MAX_LENGTH;
		CopyChars(m_rgch, szNew, cch);
		m_nLength = cch;
		m_nCursor = -1;
	}

	return true;
}


// ===== Rendering ============================================================

void CKeyboard::Render()
{
	const WORD* layout = ActiveLayout();

	if (m_frame != NULL)
		m_frame->Render();

	// Render text input field
	if (m_text != NULL)
	{
		m_rgch[m_nLength] = 0;
		g_szText = m_rgch;
		g_nTextChar = m_nCursor;
		m_text->Render();
		g_nTextChar = -1;
	}

	// Determine grid size
	int totalKeys;
	if (IsJapanese())
	{
		if (m_keys.GetLength() != 99)
			return;
		totalKeys = 99;
	}
	else
	{
		if (m_keys.GetLength() != 45)
			return;
		totalKeys = 55;
	}

	// Map selected position to a flat key index, accounting for merged keys
	int selectedIdx = m_nRow * KB_COLS + m_nColumn;
	if (!IsJapanese())
	{
		if (selectedIdx > 40 && selectedIdx <= 43)
			selectedIdx = 40;
		else if (selectedIdx > 45 && selectedIdx <= 50)
			selectedIdx = 45;
		else if (selectedIdx == 52)
			selectedIdx = 51;
		else if (selectedIdx == 54)
			selectedIdx = 53;
	}

	TCHAR charBuf[2];
	int nodeIdx = 0;

	for (int i = 0; i < totalKeys; i += 1)
	{
		// Western layout: skip duplicate entries for merged keys (BS, Space, LT, RT)
		if (!IsJapanese() && i > 0 && layout[i] == layout[i - 1])
			continue;

		CNode* pNode = m_keys.GetNode(nodeIdx);
		WORD key = layout[i];

		// Set the label text for this key
		const TCHAR* label = GetKeyLabel(key);
		if (label != NULL)
		{
			g_szText = (TCHAR*)label;
		}
		else
		{
			charBuf[0] = (TCHAR)key;
			charBuf[1] = 0;

			if (!IsJapanese())
				charBuf[0] = ApplyCase(charBuf[0]);

			g_szText = charBuf;
		}

		g_bActiveKey = (i == selectedIdx);
		if (key != KEY_VOID)
			pNode->Render();

		nodeIdx += 1;
	}
}

void CKeyboard::Advance(float nSeconds)
{
	CNode::Advance(nSeconds);

	if (m_control != NULL)
		m_control->Advance(nSeconds);
}
