# Math

CMathClass is a global singleton implementing the JavaScript Math object for XAP scripts. Exposes standard math functions (sin, cos, floor, etc.) and constants (PI, E, etc.). Backed by `math_node.cpp` in the active source tree.

## Binary Analysis (4920-5960 retail XBE)

### FND Table Strings

Function names found in the binary string table:

- `abs`, `acos`, `asin`, `atan`, `atan2`
- `ceil`, `cos`, `exp`, `floor`, `log`
- `max`, `min`, `pow`, `random`, `round`
- `sin`, `sqrt`, `tan`, `itoa`

### Labeled Addresses

- `CMathClass_abs` at `0x00032af0`
- `CMathClass_floor` at `0x000223e4`
- `CMathClass_ceil` at `0x000223c4`
- `CMathClass_round` at `0x00022418`
- `CMathClass_random` at `0x00022410`
- `CMathClass_sqrt` at `0x00022434`
- `CMathClass_pow` at `0x00022408`
- `CMathClass_atan2` at `0x000223b8`

### Global Singleton

`g_Math` at `0x0001a508`. Constructor populates JS Math constants. Not reference counted.

### Observations

- All math wrappers are thin CRT calls (fabsf, sinf, cosf, etc.)
- `atan2(y, x)` passes args reversed to match JS convention
- `itoa` converts int to Unicode string via CRT `_itoa` + `Unicode()`
