# Math

The script's `Math` object in the 5960 retail binary is a `CNodeClass` named "Math"
built at `0x00032950`. It has no factory and no props of its own; it carries eight
float constants and an 18 entry function table at `0x0017fb18`.

## Constants

Stored right after the class fields, read from `0x0002c034..0x0002c04c`:

| Field | Value |
|-------|-------|
| E | 2.7182817 |
| LN2 | 0.6931472 |
| LN10 | 2.3025851 |
| LOG2E | 1.442 |
| LOG10E | 0.434 |
| PI | 3.1415927 |
| SQRT1_2 | 0.70710677 |
| SQRT2 | 1.4142135 |

LOG2E and LOG10E are stored to three places. Nothing in the binary exposes these
fields to script by name, so `Math.PI` in a XAP is an unknown member.

## Functions

| Address | Name | Sig | Body |
|---------|------|-----|------|
| `0x00032a20` | abs | 10 | `fabs` |
| `0x00032a30` | acos | 10 | CRT acos |
| `0x00032a40` | asin | 10 | CRT asin |
| `0x00032a50` | atan | 10 | CRT atan |
| `0x00032a60` | atan2 | 11 | `fpatan(arg2, arg1)` |
| `0x00032a70` | ceil | 10 | CRT ceil on a double |
| `0x00032a90` | cos | 10 | `fcos` |
| `0x00032aa0` | exp | 10 | CRT exp |
| `0x00032ac0` | floor | 10 | CRT floor on a double |
| `0x00032ae0` | log | 10 | CRT log |
| `0x00032af0` | max | 11 | `if (b < a) return a; return b;` |
| `0x00032b10` | min | 11 | `if (a < b) return a; return b;` |
| `0x00032b30` | pow | 11 | jump into CRT pow |
| `0x00032b40` | random | 9 | `rand() * (1.0 / 32767)` |
| `0x00032b60` | round | 10 | `(float)(int)(n + 0.5)` |
| `0x00032b80` | sin | 10 | `fsin` |
| `0x00032b90` | sqrt | 10 | `fsqrt` |
| `0x00032ba0` | tan | 10 | `fptan` |

Two of these aren't what the name suggests:

- `atan2(a, b)` computes the arc tangent of `b / a`, the opposite argument order from
  C. Dashboard scripts are written against that.
- `round` truncates after adding a half, so negative halves round toward zero
  (`round(-2.5)` is -2).

## Theseus notes

- `random` divides by the host's `RAND_MAX`, so desktop stays in 0..1.
- `projectMEnabled` and `itoa` are Theseus additions; the binary has neither.
