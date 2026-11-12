# Locale and the Translator Node

The 5960 retail binary keeps one translation table for the current language, a `Translator` node
that scripts call into, and the region and language aware date, time and number formats. This is
what the binary does; Theseus's version lives in `theseus/engine/locale_node.cpp` and both builds
compile it.

## Functions

| Address | Name | What it does |
|---------|------|--------------|
| 0x0003c370 | LoadXlate | Loads a language section and parses it into the table |
| 0x0003c620 | Translate | Copies a string, then swaps it for its translation if the table has it |
| 0x0003c680 | CTranslator::GetFunctionMap | Returns the FND at `0x00180408` |
| 0x0003c690 | CTranslator::CreateNode | Factory for `Translator`, a bare 0x1c byte CNode |
| 0x0003c6d0 | CTranslator::GetNodeClass | Returns the node class at `0x001803d8` |
| 0x0003c700 | CTranslator::~CTranslator | Straight to CNode's destructor |
| 0x0003c710 | GetTimeZoneCount | Returns 75 |
| 0x0003c720 | GetTimeZoneText | Translated display name of a zone, empty string out of range |
| 0x0003c780 | GetTimeZoneIndex | Best zone for a `TIME_ZONE_INFORMATION` |
| 0x0003c820 | GetTimeZoneInfo | Fills a `TIME_ZONE_INFORMATION` from the zone table |
| 0x0003c8f0 | SetLanguage | Switches the table to another language |
| 0x0003c980 | Translate (script) | `Translate` into a 1024 character buffer, as a string object |
| 0x0003ca10 | TranslateStripColon | The same, minus one trailing `:` |
| 0x0003cac0 | GetLanguageCode | Two letter code for the current language |
| 0x0003cb00 | GetLanguageCode (script) | The same as a string object |
| 0x0003cbc0 | GetDateSeparator | `.`, `-` or `/` by region and language |
| 0x0003cc40 | GetDateSeparator (script) | The same as a one character string |
| 0x0003ccb0 | FormatNumber | `swprintf(buf, L"%d", n)` as a string object |
| 0x0003cd40 | FormatDate | The date half of FormatTime |
| 0x0003ce90 | FormatTime | Date then time, laid out for the locale |

## State

| Address | What |
|---------|------|
| 0x0017aee8 | The table: 550 (key, value) pointer pairs, 0x44c dwords, zeroed on every language change |
| 0x0017c018 | The loaded section's data |
| 0x0017c01c | The section handle |
| 0x0017c020 | Current region |
| 0x0017c024 | Current language |
| 0x001745b0 | Section name per language, nine pointers (`EnglishXlate` first) |

A locale is `region << 16 | language`. The regions the binary tests are 1 (North America), 2 (Japan)
and 4 (rest of world). Languages run English 0, Japanese 1, German 2, French 3, Spanish 4,
Italian 5, Korean 6, Chinese 7, Portuguese 8.

## Loading a language

`SetLanguage` (0x0003c8f0) does nothing when the language is already loaded (same number and a
non-empty first key) or out of `0..8`. Otherwise it stores the language, frees the old section with
`XFreeSectionByHandle`, zeroes the table and calls `LoadXlate` with the section name. Afterwards
it walks the nine title arrays at `0x00182088` (stride 0x944) and resets each loaded one with
`CTitleArray::SetRoot(0)`, so title names are fetched again in the new language.

`LoadXlate` (0x0003c370) calls `XGetSectionHandleA` and `XLoadSectionByHandle`. A failed load
frees the texture cache, then the mesh cache, and retries; when neither frees anything it prints
"xdash: out of memory" and returns to the firmware. The data must start with `FEFF`, otherwise it
also returns to the firmware (code 4).

The text is UTF-16, one entry per CR ended line:

```
[blanks] [# comment] CR                                   blank or comment line, skipped
[blanks] key [blanks] = [blanks] value [blanks] [# comment] CR
```

- A key in quotes runs to the closing quote or the line end. A bare key runs to the first
  character at or below space, or `=`; inside a bare key `_` is rewritten to `:` when the next
  character is `=` or space, and to a space otherwise.
- The value may be quoted the same way and runs to a quote or the line end. It goes through
  `ExpandCString` in place, so `\n`, `\x`, `\u` escapes work.
- A comment on a line of its own ends at CR or LF; a comment after a value ends at CR only.
- When the `=` or the final CR is missing the binary stops reading the file. An empty key or
  value skips the line. There is no bound check on the 550 slots.

## Translate

```
wcscpy(out, in);
p = wcschr(out, '@'); if (p) *p = '@';      // writes back the same character
for (i = 0; i < 550 && table[i].key; i++)
    if (!wcscmp(table[i].key, out)) { wcscpy(out, table[i].value); break; }
return out;
```

## Separators and formats

`GetDateSeparator` (0x0003cbc0) switches on the locale value:

| Locale values | Separator |
|---------------|-----------|
| 0x10002, 0x10005, 0x20002, 0x20003, 0x20005, 0x40002, 0x40003, 0x40005 | `.` |
| 0x10006, 0x20006, 0x40006 | `-` |
| anything else | `/` |

German and Italian use dots in every region, French outside North America, Korean dashes.

`FormatDate` and `FormatTime` take a locale (`-1` means the current one). The date part:

| Locale | Format | Fields |
|--------|--------|--------|
| 0x10000 | `%02d%c%02d%c%d ` | month, day, year |
| regions 1, 2, 4 with language 1, 6 or 7 | `%d%c%02d%c%02d ` | year, month, day |
| anything else | `%02d%c%02d%c%d ` | day, month, year |

The time part in `FormatTime` is `%d:%02d %s` on a 12 hour clock for languages 0, 1, 6 and 7 in
regions 1, 2 and 4 (hours above 11 drop 12, 0 becomes 12, `AM` at `0x0002425c` below 12, `PM` at
`0x00024264` otherwise); everything else gets `%d:%02d` on 24 hours. Both parts go into 32
character buffers. If the caller's size is less than their lengths plus 2 the call returns false
and writes nothing.

Numbers have no separators: `FormatNumber` prints `%d` (format string at `0x000242b0`). The memory
monitor's block counts (0x0004ca90) print the same way and cap at `50000+`.

`GetLanguageCode` reads two characters from `L"ENJADEFRESITKOTWBR"` for languages 0 to 9 (9 runs
past the last code) and gives an empty string otherwise.

## Time zones

The zone table at `0x000201f0` has 75 records of 0x1c bytes: display name, DST flag, bias, standard
bias, daylight bias, standard name, standard change date (month, day, weekday, hour as bytes),
daylight name (may be null) and daylight change date.

`GetTimeZoneIndex` (0x0003c780) scores each zone whose bias matches: 1 for the bias alone, 2 when
the standard and daylight biases match too, and returns at once when the first 4 characters of the
standard name also match (the EEPROM keeps only 4). Otherwise the first zone with the best score
wins, or -1.

`GetTimeZoneInfo` (0x0003c820) zeroes all 0xac bytes, copies the three biases, the standard name
and the daylight name (the standard name when there is none) with `wcsncpy(..., 32)`, and the month,
day, weekday and hour of each change date.

## Theseus differences

- The language text comes from `Q:\Language\<name>.dat` instead of an XBE section, for the first
  six languages, with up to 1000 pairs and a bound check. A missing file leaves the table empty.
- Desktop strings are narrow, so desktop keeps one byte per UTF-16 character.
- The title arrays are emptied with `DeleteAll(false)` rather than `SetRoot(0)`.
