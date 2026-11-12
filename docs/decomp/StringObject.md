# String Object

`CStrObject` is the VM's built-in string type. Not an XAP node, but registered as a script-accessible object so XAP scripts can call methods on string values.

## Script Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `length` | `() -> int` | Returns the string length |
| `charCodeAt` | `(index) -> int` | Returns the character code at the given index |
| `charAt` | `(index) -> string` | Returns a single-character string at the given index |
| `concat` | `(str) -> string` | Returns a new string with `str` appended |
| `indexOf` | `(substr [, start]) -> number` | Returns the first index of `substr`, or -1 |
| `lastIndexOf` | `(substr [, start]) -> number` | Returns the last index of `substr`, or -1 |
| `substr` | `(start [, length]) -> string` | Returns a substring starting at `start` for `length` chars |
| `substring` | `(start, end) -> string` | Returns chars between `start` and `end` indices |
| `toLowerCase` | `() -> string` | Returns a lowercased copy (uses `_tcslwr` on Xbox) |
| `toUpperCase` | `() -> string` | Returns an uppercased copy (uses `_tcsupr` on Xbox) |

## Immutability

The string type is effectively immutable for script purposes. All methods that produce modified text return new `CStrObject` instances rather than mutating the original. This matches the ECMAScript string semantics that the XAP scripting language borrows from.

## Implementation Notes

`_tcslwr` and `_tcsupr` are the Xbox CRT's locale-aware case conversion functions (TCHAR variants). These operate in-place on the internal buffer of the newly-created return string, not on the caller's string.

## Binary layout and functions

The object is 0x20 bytes: the CObject header (type 3 at +0x08), then length at +0x14, capacity at
+0x18 and the UTF-16 buffer at +0x1c.

| Address | Name | What it does |
|---------|------|--------------|
| 0x00032040 | CStrObject() | Type 3, no buffer |
| 0x00032090 | CStrObject(sz) | `wcslen`, a buffer of length + 1, copy; NULL reads as `""` |
| 0x00032130 | CStrObject(pch, cch) | A buffer of cch + 1, copy cch characters, terminate |
| 0x000321d0 | ~CStrObject | Frees the buffer |
| 0x00032200 | ToNum | `wcstod` of the text into a new number object |
| 0x00032260 | Append | Grows to exactly length + n + 1 when needed, copies onto the end |
| 0x00032300 | charCodeAt | The UTF-16 unit, -1 out of range |
| 0x00032320 | charAt | A one character string; no object at all out of range |
| 0x000323a0 | concat | A new string sized for both |
| 0x00032460 | indexOf | 1 or 2 arguments; start clamped to 0..length; `wcsstr` from there when start is short of the end |
| 0x00032550 | (reverse search) | From text + start - len(find) backwards with `wcsncmp`; returns where it stopped, so a miss can give an index below -1 |
| 0x000325b0 | lastIndexOf | Same argument rules, start defaults to the length |
| 0x00032680 | substr | count defaults to length - start; start capped at length; count of 0 or less is empty; count past the end stops there. A negative start is not clamped |
| 0x00032780 | substring | Exactly 2 arguments; negative start is 0, negative end is start; swapped when out of order; both capped at length |
| 0x00032850 / 0x000328c0 | toLowerCase / toUpperCase | A copy, then `wcslwr` / `wcsupr` |

A wrong argument count raises "invalid number of parameters" (string at `0x00021b38`) and returns
nothing. Theseus answers `""` from an out of range `charAt` and -1 from every missed `lastIndexOf`.
