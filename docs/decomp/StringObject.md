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
