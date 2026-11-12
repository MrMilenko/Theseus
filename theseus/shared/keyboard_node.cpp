// keyboard_node.cpp: the Keyboard node, the on-screen keyboard behind every
// name and passcode prompt.
//
// A grid of keys (5 x 11 western, 9 x 11 Japanese) with a highlighted key the
// script moves around, a 31 character edit string with a cursor, and OnDone /
// OnError callbacks. Rebuilt function by function from the 5960 retail XBE;
// what the binary does is written up in docs/decomp/Keyboard.md.

#include "std.h"
#include "theseus.h"
#include "node.h"
#include "runner.h"

// Shared with the Text node: the keyboard lends it the edit string and cursor
// (0x0017e748, 0x00175220), flags the highlighted key (0x0017e74d) and owns the
// *heading* switch (0x0017e74c).
extern TCHAR* g_szText;
extern int g_nTextChar;
extern bool g_bActiveKey;
extern bool g_bAsteriskFormatting;
extern int g_nCurLanguage;


// ===== Layouts =============================================================
// Six 16 bit key tables at 0x000211b8..0x00021498, reached through the
// pointer list at 0x00175430 (three western, then three Japanese). Values
// below 0x0f are function keys, everything else types itself.

enum
{
	kcShift = 1,
	kcCaps,
	kcAlphabet,
	kcSymbols,
	kcAccents,
	kcLeft,
	kcRight,
	kcBackspace,
	kcDone,
	kcSpace,
	kcHiragana,
	kcKatakana,
	kcEnglish,
	kcBlank,        // a hole in the Japanese grid, never drawn or landed on
};

#define EDIT_MAX        31
#define GRID_COLUMNS    11
#define WESTERN_KEYS    (5 * GRID_COLUMNS)
#define JAPANESE_KEYS   (9 * GRID_COLUMNS)
#define WESTERN_NODES   45      // merged keys share one node
#define JAPANESE_NODES  99

static const WORD s_westernAlpha[WESTERN_KEYS] =
{
	kcDone,     '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
	kcShift,    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j',
	kcCaps,     'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't',
	kcAccents,  'u', 'v', 'w', 'x', 'y', 'z',
	            kcBackspace, kcBackspace, kcBackspace, kcBackspace,
	kcSymbols,  kcSpace, kcSpace, kcSpace, kcSpace, kcSpace, kcSpace,
	            kcLeft, kcLeft, kcRight, kcRight,
};

static const WORD s_westernSymbols[WESTERN_KEYS] =
{
	kcDone,     '(', ')', '&', '_', '^', '%', '\\', '/', '@', '#',
	kcShift,    '[', ']', '$', 0xa3, 0x20ac, 0xa5, ';', ':', '\'', '"',
	kcCaps,     '<', '>', '?', '!', 0xbf, 0xa1, '-', '*', '+', '=',
	kcAccents,  '{', '}', 0xab, 0xbb, ',', '.',
	            kcBackspace, kcBackspace, kcBackspace, kcBackspace,
	kcAlphabet, kcSpace, kcSpace, kcSpace, kcSpace, kcSpace, kcSpace,
	            kcLeft, kcLeft, kcRight, kcRight,
};

static const WORD s_westernAccents[WESTERN_KEYS] =
{
	kcDone,     '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
	kcShift,    0xe0, 0xe1, 0xe2, 0xe4, 0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed,
	kcCaps,     0xee, 0xef, 0xf2, 0xf3, 0xf4, 0xf5, 0xf9, 0xfa, 0xfb, 0xfc,
	kcAlphabet, 0xfd, 0xff, 0xe7, 0xdf, 0xf1, 0xe6,
	            kcBackspace, kcBackspace, kcBackspace, kcBackspace,
	kcSymbols,  kcSpace, kcSpace, kcSpace, kcSpace, kcSpace, kcSpace,
	            kcLeft, kcLeft, kcRight, kcRight,
};

static const WORD s_hiragana[JAPANESE_KEYS] =
{
	0x3042, 0x3044, 0x3046, 0x3048, 0x304a, 0x308f, 0x3092, 0x3093, kcBlank, kcBlank, kcHiragana,
	0x304b, 0x304d, 0x304f, 0x3051, 0x3053, 0x3041, 0x3043, 0x3045, 0x3047, 0x3049, kcKatakana,
	0x3055, 0x3057, 0x3059, 0x305b, 0x305d, 0x3063, 0x3083, 0x3085, 0x3087, 0x308e, kcEnglish,
	0x305f, 0x3061, 0x3064, 0x3066, 0x3068, 0x304c, 0x304e, 0x3050, 0x3052, 0x3054, kcSpace,
	0x306a, 0x306b, 0x306c, 0x306d, 0x306e, 0x3056, 0x3058, 0x305a, 0x305c, 0x305e, kcBackspace,
	0x306f, 0x3072, 0x3075, 0x3078, 0x307b, 0x3060, 0x3062, 0x3065, 0x3067, 0x3069, kcLeft,
	0x307e, 0x307f, 0x3080, 0x3081, 0x3082, 0x3070, 0x3073, 0x3076, 0x3079, 0x307c, kcRight,
	0x3084, kcBlank, 0x3086, kcBlank, 0x3088, 0x3071, 0x3074, 0x3077, 0x307a, 0x307d, kcBlank,
	0x3089, 0x308a, 0x308b, 0x308c, 0x308d, 0x30fc, 0x3001, 0x3002, 0x300c, 0x300d, kcDone,
};

static const WORD s_katakana[JAPANESE_KEYS] =
{
	0x30a2, 0x30a4, 0x30a6, 0x30a8, 0x30aa, 0x30ef, 0x30f2, 0x30f3, 0x30f4, kcBlank, kcHiragana,
	0x30ab, 0x30ad, 0x30af, 0x30b1, 0x30b3, 0x30a1, 0x30a3, 0x30a5, 0x30a7, 0x30a9, kcKatakana,
	0x30b5, 0x30b7, 0x30b9, 0x30bb, 0x30bd, 0x30c3, 0x30e3, 0x30e5, 0x30e7, 0x30ee, kcEnglish,
	0x30bf, 0x30c1, 0x30c4, 0x30c6, 0x30c8, 0x30ac, 0x30ae, 0x30b0, 0x30b2, 0x30b4, kcSpace,
	0x30ca, 0x30cb, 0x30cc, 0x30cd, 0x30ce, 0x30b6, 0x30b8, 0x30ba, 0x30bc, 0x30be, kcBackspace,
	0x30cf, 0x30d2, 0x30d5, 0x30d8, 0x30db, 0x30c0, 0x30c2, 0x30c5, 0x30c7, 0x30c9, kcLeft,
	0x30de, 0x30df, 0x30e0, 0x30e1, 0x30e2, 0x30d0, 0x30d3, 0x30d6, 0x30d9, 0x30dc, kcRight,
	0x30e4, kcBlank, 0x30e6, kcBlank, 0x30e8, 0x30d1, 0x30d4, 0x30d7, 0x30da, 0x30dd, kcBlank,
	0x30e9, 0x30ea, 0x30eb, 0x30ec, 0x30ed, 0x30fc, 0x3001, 0x3002, 0x300c, 0x300d, kcDone,
};

// The Japanese "English" page: capitals and lowercase side by side, then
// digits and punctuation.
static const WORD s_japaneseLatin[JAPANESE_KEYS] =
{
	'A', 'B', 'C', 'D', 'E', 'a', 'b', 'c', 'd', 'e',   kcHiragana,
	'F', 'G', 'H', 'I', 'J', 'f', 'g', 'h', 'i', 'j',   kcKatakana,
	'K', 'L', 'M', 'N', 'O', 'k', 'l', 'm', 'n', 'o',   kcEnglish,
	'P', 'Q', 'R', 'S', 'T', 'p', 'q', 'r', 's', 't',   kcSpace,
	'U', 'V', 'W', 'X', 'Y', 'u', 'v', 'w', 'x', 'y',   kcBackspace,
	'Z', '"', '\'', '@', '#', 'z', '(', ')', '{', '}',  kcLeft,
	'&', '^', '$', 0xa5, '%', '-', '+', '=', '*', '/',  kcRight,
	'0', '1', '2', '3', '4', '?', '!', ':', ';', '\\',  kcBlank,
	'5', '6', '7', '8', '9', '<', '>', ',', '.', '_',   kcDone,
};

static const WORD* const s_layouts[2][3] =
{
	{ s_westernAlpha, s_westernSymbols, s_westernAccents },
	{ s_hiragana, s_katakana, s_japaneseLatin },
};

static inline bool JapaneseGrid()
{
	return g_nCurLanguage == 1;
}

static inline int BottomRow()
{
	return JapaneseGrid() ? 8 : 4;
}

// Western grids key off mode, the Japanese grid off jmode.
static inline const WORD* GridFor(int mode, int jmode)
{
	return JapaneseGrid() ? s_layouts[1][jmode] : s_layouts[0][mode];
}

// 5960 calls CharUpperW for shifted keys; Theseus only maps a to z.
static TCHAR KeyUpper(TCHAR ch)
{
	if (ch >= 'a' && ch <= 'z')
		return (TCHAR)(ch - ('a' - 'A'));
	return ch;
}


// ===== CKeyboard ===========================================================
// 0x9c bytes in 5960: the nodearray at +0x1c, frame/text/control +0x28..+0x30,
// string +0x34, shift +0x38, caps +0x39, mode +0x40, jmode +0x44, the edit
// buffer +0x48 (32 WCHARs), its length +0x88, cursor +0x8c, row +0x90 and
// column +0x94.

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
	bool GetAsteriskFormattingState();
	void SetAsteriskFormattingState(bool bOn);

	void Advance(float nSeconds);
	void Render();
	bool OnSetProperty(const PRD* pprd, const void* pvValue);

protected:
	TCHAR m_edit[EDIT_MAX + 1];
	int m_editLen;
	int m_cursor;       // -1 means the whole string is selected
	int m_row;
	int m_col;

	const WORD* Grid() const { return GridFor(m_mode, m_jmode); }
	WORD KeyAt(int row, int col) const { return Grid()[row * GRID_COLUMNS + col]; }
	void TakeSelection();
	void OpenGap(int at, int count);

	DECLARE_NODE_PROPS()
	DECLARE_NODE_FUNCTIONS()
};

IMPLEMENT_NODE("Keyboard", CKeyboard, CNode)

// PRD at 0x00175448, same order.
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

// FND at 0x00188250 (the binary lists Set before Get).
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

// CKeyboard::CKeyboard()
// Reconstructed from 5960 retail XBE @ 0x0005ab80: every field zero except
// the cursor, which starts at -1 so the first key replaces the whole string.
CKeyboard::CKeyboard()
{
	m_frame = NULL;
	m_text = NULL;
	m_control = NULL;
	m_string = NULL;
	m_shift = false;
	m_caps = false;
	m_mode = 0;
	m_jmode = 0;
	m_editLen = 0;
	m_cursor = -1;
	m_row = 0;
	m_col = 0;
}

// CKeyboard::~CKeyboard()
// Reconstructed from 5960 retail XBE @ 0x0005ac20:
//     if (text) text->Release(); if (frame) frame->Release();
//     if (control) control->Release(); free(string); keys.ReleaseAll();
CKeyboard::~CKeyboard()
{
	CNode* owned[] = { m_text, m_frame, m_control };
	for (int i = 0; i < 3; i++)
	{
		if (owned[i] != NULL)
			owned[i]->Release();
	}
	delete [] m_string;
}

// Every edit starts here: with the whole string selected (cursor -1) the edit
// replaces it, so the string empties and the cursor goes to 0. The binary
// repeats this block at the top of each editing function.
void CKeyboard::TakeSelection()
{
	if (m_cursor != -1)
		return;
	m_editLen = 0;
	m_cursor = 0;
}

// Shifts the tail from `at` right by `count` characters.
void CKeyboard::OpenGap(int at, int count)
{
	MoveMemory(m_edit + at + count, m_edit + at, (m_editLen - at) * sizeof(TCHAR));
}


// ===== Moving the highlight ================================================

// CKeyboard::selectKey(row, col)   [script fn 'selectKey', FND 0x00188250, sig 18]
// Reconstructed from 5960 retail XBE @ 0x0005ace0:
//     row = (row < 0 || row > (lang==1 ? 8 : 4)) ? 0 : row;
//     col = (col < 0 || col > 10) ? 0 : col;
void CKeyboard::selectKey(int nRow, int nColumn)
{
	bool rowOk = nRow >= 0 && nRow <= BottomRow();
	bool colOk = nColumn >= 0 && nColumn < GRID_COLUMNS;
	m_row = rowOk ? nRow : 0;
	m_col = colOk ? nColumn : 0;
}

// CKeyboard::selectUp()   [script fn 'selectUp', FND 0x00188268, sig 1]
// Reconstructed from 5960 retail XBE @ 0x0005ad40:
//     do { row = row ? row - 1 : bottom; } while (grid[row*11 + col] == 0xe);
void CKeyboard::selectUp()
{
	do
	{
		m_row = (m_row == 0) ? BottomRow() : m_row - 1;
	}
	while (KeyAt(m_row, m_col) == kcBlank);
}

// CKeyboard::selectDown()   [script fn 'selectDown', FND 0x00188280, sig 1]
// Reconstructed from 5960 retail XBE @ 0x0005adb0:
//     do { row = (row == bottom) ? 0 : row + 1; } while (grid[row*11 + col] == 0xe);
void CKeyboard::selectDown()
{
	do
	{
		m_row = (m_row == BottomRow()) ? 0 : m_row + 1;
	}
	while (KeyAt(m_row, m_col) == kcBlank);
}

// CKeyboard::selectLeft()   [script fn 'selectLeft', FND 0x00188298, sig 1]
// Reconstructed from 5960 retail XBE @ 0x0005ae60. Column 0 wraps to 10
// (again if that's a blank). Japanese steps one column, skipping blanks.
// Western rows 3 and 4 hop over the wide keys: row 3 from 7..10 lands on 6;
// row 4 from 9..10 lands on 8, from 7..8 on 6, from 1..6 on 0.
void CKeyboard::selectLeft()
{
	for (;;)
	{
		if (m_col == 0)
		{
			m_col = GRID_COLUMNS - 1;
			if (KeyAt(m_row, m_col) != kcBlank)
				return;
			continue;
		}

		if (JapaneseGrid())
		{
			m_col--;
			if (KeyAt(m_row, m_col) != kcBlank)
				return;
			continue;
		}

		if (m_row == 3 && m_col > 6)
			m_col = 6;
		else if (m_row == 4)
			m_col = (m_col > 8) ? 8 : (m_col > 6) ? 6 : 0;
		else
			m_col--;
		return;
	}
}

// CKeyboard::selectRight()   [script fn 'selectRight', FND 0x001882b0, sig 1]
// Reconstructed from 5960 retail XBE @ 0x0005af50. The mirror of selectLeft:
// column 10 wraps to 0, Japanese skips blanks, western row 3 wraps from 7 on
// and row 4 goes 0 to 1, 1..6 to 7, 7..8 to 9, 9 on to 0.
void CKeyboard::selectRight()
{
	for (;;)
	{
		if (m_col == GRID_COLUMNS - 1)
		{
			m_col = 0;
			if (KeyAt(m_row, m_col) != kcBlank)
				return;
			continue;
		}

		if (JapaneseGrid())
		{
			m_col++;
			if (KeyAt(m_row, m_col) != kcBlank)
				return;
			continue;
		}

		if (m_row == 3)
			m_col = (m_col < 7) ? m_col + 1 : 0;
		else if (m_row == 4)
		{
			if (m_col >= 9)
				m_col = 0;
			else if (m_col == 0)
				m_col = 1;
			else
				m_col = (m_col > 6) ? 9 : 7;
		}
		else
			m_col++;
		return;
	}
}


// ===== Pressing a key ======================================================

// CKeyboard::activate()   [script fn 'activate', FND 0x001882c8, sig 1]
// Reconstructed from 5960 retail XBE @ 0x0005b340. Looks up the highlighted
// key and acts on it: function keys switch case or page, move the cursor or
// erase; DONE trims the string, copies it to `string` and calls OnDone (an
// empty or all blank string calls OnError instead); SPACE inserts quietly
// unless full; anything else inserts the character (uppercased when shift
// and caps disagree), drops shift, or calls OnError when full.
// See docs/decomp/Keyboard.md.
void CKeyboard::activate()
{
	WORD key = KeyAt(m_row, m_col);

	switch (key)
	{
	case kcShift:
		Shift();
		return;

	case kcCaps:
		if (m_mode != 0 && m_mode != 2)
			break;
		m_caps = !m_caps;
		return;

	case kcAlphabet: m_mode = 0; return;
	case kcSymbols:  m_mode = 1; return;
	case kcAccents:  m_mode = 2; return;
	case kcHiragana: m_jmode = 0; return;
	case kcKatakana: m_jmode = 1; return;
	case kcEnglish:  m_jmode = 2; return;

	case kcLeft:      CursorLeft(); return;
	case kcRight:     CursorRight(); return;
	case kcBackspace: Backspace(); return;

	case kcDone:
	{
		int lead = 0;
		while (lead < m_editLen && m_edit[lead] == ' ')
			lead++;
		if (lead == m_editLen)
			break;

		// Blank out trailing spaces, then slide the text over the leading ones.
		for (int end = m_editLen - 1; m_edit[end] == ' '; end--)
			m_edit[end] = 0;
		if (lead > 0)
			MoveMemory(m_edit, m_edit + lead, (_tcslen(m_edit + lead) + 1) * sizeof(TCHAR));

		// The copy keeps the untrimmed length, so trimmed spaces come along as nulls.
		delete [] m_string;
		m_string = new TCHAR[m_editLen + 1];
		CopyChars(m_string, m_edit, m_editLen);
		m_string[m_editLen] = 0;
		CallFunction(this, _T("OnDone"));
		return;
	}

	case kcSpace:
		TakeSelection();
		if (m_editLen >= EDIT_MAX)
			return;
		OpenGap(m_cursor, 1);
		m_edit[m_cursor++] = ' ';
		m_editLen++;
		return;

	default:
		TakeSelection();
		if (m_editLen >= EDIT_MAX)
			break;
		OpenGap(m_cursor, 1);
		m_edit[m_cursor++] = (m_caps != m_shift) ? KeyUpper((TCHAR)key) : (TCHAR)key;
		m_editLen++;
		m_shift = false;
		return;
	}

	CallFunction(this, _T("OnError"));
}


// ===== Editing =============================================================

// CKeyboard::Backspace()   [script fn 'Backspace', FND 0x001882f8, sig 1]
// Reconstructed from 5960 retail XBE @ 0x0005b630:
//     if (cursor == -1) { len = 0; cursor = 0; }
//     if (cursor > 0) { memmove(&buf[cursor-1], &buf[cursor], (len-cursor)*2); len--; cursor--; }
//     else CallFunction(this, L"OnError");
void CKeyboard::Backspace()
{
	TakeSelection();
	if (m_cursor <= 0)
	{
		CallFunction(this, _T("OnError"));
		return;
	}
	MoveMemory(m_edit + m_cursor - 1, m_edit + m_cursor, (m_editLen - m_cursor) * sizeof(TCHAR));
	m_cursor--;
	m_editLen--;
}

// CKeyboard::Delete()   [script fn 'Delete', FND 0x00188310, sig 1]
// Reconstructed from 5960 retail XBE @ 0x0005b6a0: takes the selection, then
// if there is a character under the cursor steps past it and backspaces over
// it, otherwise OnError.
void CKeyboard::Delete()
{
	TakeSelection();
	if (m_cursor >= m_editLen)
	{
		CallFunction(this, _T("OnError"));
		return;
	}
	m_cursor++;
	Backspace();
}

// CKeyboard::CursorLeft()   [script fn 'CursorLeft', FND 0x00188328, sig 1]
// Reconstructed from 5960 retail XBE @ 0x0005b700:
//     if (cursor == -1) cursor = 0; else if (cursor > 0) cursor--; else OnError;
void CKeyboard::CursorLeft()
{
	if (m_cursor > 0)
		m_cursor--;
	else if (m_cursor == -1)
		m_cursor = 0;
	else
		CallFunction(this, _T("OnError"));
}

// CKeyboard::CursorRight()   [script fn 'CursorRight', FND 0x00188340, sig 1]
// Reconstructed from 5960 retail XBE @ 0x0005b740:
//     if (cursor == -1) cursor = len; else if (cursor < len) cursor++; else OnError;
void CKeyboard::CursorRight()
{
	if (m_cursor == -1)
		m_cursor = m_editLen;
	else if (m_cursor < m_editLen)
		m_cursor++;
	else
		CallFunction(this, _T("OnError"));
}

// CKeyboard::Shift()   [script fn 'Shift', FND 0x00188358, sig 1]
// Reconstructed from 5960 retail XBE @ 0x0005b780:
//     if (mode != 0 && mode != 2) OnError; else shift = !shift;
// Only the letter and accent pages have a case.
void CKeyboard::Shift()
{
	if (m_mode == 0 || m_mode == 2)
		m_shift = !m_shift;
	else
		CallFunction(this, _T("OnError"));
}

// CKeyboard::CycleMode()   [script fn 'CycleMode', FND 0x00188370, sig 1]
// Reconstructed from 5960 retail XBE @ 0x0005b7b0: bumps mode and jmode
// together, each going back to 0 once past 2.
void CKeyboard::CycleMode()
{
	m_mode++;
	m_jmode++;
	if (m_mode > 2)
		m_mode = 0;
	if (m_jmode > 2)
		m_jmode = 0;
}

// CKeyboard::Insert(text)   [script fn 'Insert', FND 0x00188388, sig 12]
// Reconstructed from 5960 retail XBE @ 0x0005b7e0:
//     take selection; n = wcslen(s); if (len + n > 31) { OnError; return; }
//     memmove(&buf[cursor+n], &buf[cursor], (len-cursor)*2); copy s in;
//     cursor += n; len += n; shift = 0;
void CKeyboard::Insert(const TCHAR* szInsert)
{
	TakeSelection();
	int count = (int)_tcslen(szInsert);
	if (m_editLen + count > EDIT_MAX)
	{
		CallFunction(this, _T("OnError"));
		return;
	}
	OpenGap(m_cursor, count);
	CopyChars(m_edit + m_cursor, szInsert, count);
	m_cursor += count;
	m_editLen += count;
	m_shift = false;
}

// CKeyboard::GetAsteriskFormattingState / SetAsteriskFormattingState
//   [script fns, FND 0x001883b8 sig 2 / 0x001883a0 sig 14]
// Reconstructed from 5960 retail XBE @ 0x0005acb0 / 0x0005acc0: read and
// write the one global byte at 0x0017e74c that turns *heading* text on.
bool CKeyboard::GetAsteriskFormattingState()
{
	return g_bAsteriskFormatting;
}

void CKeyboard::SetAsteriskFormattingState(bool bOn)
{
	g_bAsteriskFormatting = bOn;
}


// ===== Node hooks ==========================================================

// CKeyboard::OnSetProperty(pprd, pvValue)
// Reconstructed from 5960 retail XBE @ 0x0005b970:
//     if (prd->offset == 0x34 /* string */) {
//         s = *value ? *value : L""; n = min(wcslen(s), 31);
//         copy n chars into buf; len = n; cursor = -1; }
//     return 1;
// A script setting `string` loads the edit buffer with the whole thing selected.
bool CKeyboard::OnSetProperty(const PRD* pprd, const void* pvValue)
{
	if (PTR2INT(pprd->pbOffset) != offsetof(m_string))
		return true;

	const TCHAR* szValue = *(const TCHAR* const*)pvValue;
	if (szValue == NULL)
		szValue = _T("");
	int count = (int)_tcslen(szValue);
	if (count > EDIT_MAX)
		count = EDIT_MAX;
	CopyChars(m_edit, szValue, count);
	m_editLen = count;
	m_cursor = -1;
	return true;
}

// CKeyboard::Advance(nSeconds)
// Reconstructed from 5960 retail XBE @ 0x0005bca0:
//     CNode::Advance(t); if (control) control->Advance(t);
void CKeyboard::Advance(float nSeconds)
{
	CNode::Advance(nSeconds);
	if (m_control != NULL)
		m_control->Advance(nSeconds);
}

// Western rows 3 and 4 have keys several columns wide; any column of one
// highlights its first cell.
static int MergedCell(int cell)
{
	if (cell >= 41 && cell <= 43)
		return 40;      // BACKSPACE
	if (cell >= 46 && cell <= 50)
		return 45;      // SPACE
	if (cell == 52)
		return 51;      // <
	if (cell == 54)
		return 53;      // >
	return cell;
}

static const TCHAR* FunctionKeyLabel(WORD key)
{
	switch (key)
	{
	case kcShift:     return _T("SHIFT");
	case kcCaps:      return _T("CAPS LOCK");
	case kcAlphabet:  return _T("ALPHABET");
	case kcSymbols:   return _T("SYMBOLS");
	case kcAccents:   return _T("ACCENTS");
	case kcLeft:      return _T("<");
	case kcRight:     return _T(">");
	case kcBackspace: return _T("BACKSPACE");
	case kcDone:      return _T("DONE");
	case kcSpace:     return _T("SPACE");
	case kcHiragana:  return _T("HIRAGANA");
	case kcKatakana:  return _T("KATAKANA");
	case kcEnglish:   return _T("ENGLISH");
	case kcBlank:     return _T(" ");
	}
	return NULL;
}

// CKeyboard::Render()
// Reconstructed from 5960 retail XBE @ 0x0005b9d0. Draws the frame, then the
// text node with the edit string and cursor handed over through g_szText /
// g_nTextChar. The key nodes only draw when the nodearray has exactly 45
// (western) or 99 (Japanese) entries. Each grid cell that starts a new key
// gets the next node: its label goes in g_szText (the function key name, or
// the character, uppercased on western pages when shift and caps disagree),
// g_bActiveKey says whether it is highlighted, and blanks are skipped.
void CKeyboard::Render()
{
	const WORD* grid = Grid();

	if (m_frame != NULL)
		m_frame->Render();

	if (m_text != NULL)
	{
		m_edit[m_editLen] = 0;
		g_szText = m_edit;
		g_nTextChar = m_cursor;
		m_text->Render();
		g_nTextChar = -1;
	}

	bool japanese = JapaneseGrid();
	if (m_keys.GetLength() != (japanese ? JAPANESE_NODES : WESTERN_NODES))
		return;

	int cells = japanese ? JAPANESE_KEYS : WESTERN_KEYS;
	int highlighted = m_row * GRID_COLUMNS + m_col;
	if (!japanese)
		highlighted = MergedCell(highlighted);

	TCHAR label[2];
	int node = 0;
	for (int cell = 0; cell < cells; cell++)
	{
		WORD key = grid[cell];
		if (!japanese && cell > 0 && key == grid[cell - 1])
			continue;

		CNode* pKey = m_keys.GetNode(node++);

		const TCHAR* name = FunctionKeyLabel(key);
		if (name != NULL)
		{
			g_szText = (TCHAR*)name;
		}
		else
		{
			label[0] = (TCHAR)key;
			if (!japanese && m_caps != m_shift)
				label[0] = KeyUpper(label[0]);
			label[1] = 0;
			g_szText = label;
		}

		g_bActiveKey = (cell == highlighted);
		if (key != kcBlank)
			pKey->Render();
	}
}
