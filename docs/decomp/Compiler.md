# Compiler (xap_compile.cpp)

How the 5960 dashboard turns XAP text into bytecode. The compiler is three
layers: a character scanner, a statement and expression compiler that emits
VM ops for script functions, and a scene compiler that emits node
construction ops for a class body. All text is UTF-16 in the retail XBE.
Opcode names are listed in VM.md.

## Globals

| Address | Meaning |
|---------|---------|
| 0x0017ae90 | current file name (shown in syntax errors) |
| 0x0017ae94 | current line number |
| 0x0017ac80 | parse error flag (byte) |
| 0x0017ac88 | buffer the `#file` directive copies a name into |
| 0x0017ae98 | class namespace list head (`Class_Init` / `Class_Exit`) |
| 0x000201c4, 0x00021b98 | empty strings returned on a hard parse failure |

`CClass::ParseFile` (0x00033d00) sets file = name, line = 1, error = 0,
compiles the body, reports `"Stuff past expected end of file!"` if anything
is left, then clears file and line and returns `error == 0`.

## Scanner

| Function | Address |
|----------|---------|
| SyntaxError (free) | 0x00032cf0 |
| SkipWhite | 0x00032d70 |
| Token | 0x00032f70 |
| ParseBoolean | 0x00033150 |
| ParseInteger | 0x000331d0 |
| ParseNumber | 0x00033250 |
| ParseString | 0x000332d0 |
| ParseVec3 / ParseVec4 | 0x00033340 / 0x00033370 |
| ParseIntArray | 0x000333a0 |
| ParseVecArray | 0x00033490 |
| ExpandCString | 0x00033690 |

**SyntaxError** formats the message into a 512 character buffer, wraps it
with the file name and line into a 1024 character report, and sets the error
flag. The retail build never displays the report.

**SkipWhite(pch, mask)** eats every character up to and including space,
counting newlines. The mask enables `#` (1), `//` (2) and `/* */` (4)
comments; every caller passes 0xffffffff. A `#` line may be a directive:

```c
// after '#' and any spaces/tabs
if (!wcsncmp(p, L"line", 4)) line = _wtol(p + 4 + blanks) - 1;
else if (!wcsncmp(p, L"file", 4)) { copy optional-quoted name to 0x17ac88; file = 0x17ac88; }
// then skip to end of line either way
```

**Token** returns the next token and its length, in one of four shapes:

- quoted (`"` or `'`): the text between the quotes, `\` protects the next
  character, the closing quote is consumed but not part of the token;
- path mode (third argument set, used by `DEF` and `USE`): everything up to
  the next blank;
- identifier: starts with a letter or `_`, continues with letters, digits, `_`;
- otherwise a word of letters, digits, `.`, `-`, `_` if it starts with one of
  those, or an operator run of anything else up to a blank or word character.

**ParseInteger / ParseNumber** copy the token into a 64 character stack
buffer with no length check, then `_wtol` / `wcstod`. **ParseBoolean** uses
`wcsnicmp` against `true` then `false` over the token length. **ParseString**
copies the token unexpanded into a new buffer.

**ParseIntArray / ParseVecArray** require `[`, remember the cursor and line,
and walk the list twice: once to count (entries split by an optional `,`,
each entry `nVecSize` numbers), then allocate `count * 4` bytes, rewind the
cursor and line, and read for real. ParseVecArray inlines the number read.
Neither loop checks for the end of the text, so an unterminated array never
finishes.

**ExpandCString(out, max, tok, cch)** expands `\b \f \n \r \t`, `\xHH`,
`\uHHHH`, octal `\ooo` (and a bare `\0`), and passes any other escaped
character through. Errors: `"string constant is too long"`,
`"backslash at end of string"`, `"Invalid hex character escape sequence"`,
`"Invalid hex character"`, and the two Unicode equivalents.

## Code buffer (CCompiler)

| Function | Address |
|----------|---------|
| Write(pv, cb) | 0x00034440 |
| Write(BYTE) | 0x00034490 |
| WriteInteger | 0x000344c0 |
| WriteString | 0x000344f0 |
| GrowTo | 0x00034590 |
| SyntaxError (member) | 0x000345e0 |
| CreateFunction | 0x00034670 |
| ~CCompiler | 0x000346f0 |

Layout: `+4` op count, `+8` op buffer, `+0xc` buffer size, `+0x10` error
byte. GrowTo rounds the request up to a 4 KB page, copies and frees the old
buffer. The writers are inlined at most call sites. WriteString writes the
character count as a dword, then the characters. CreateFunction allocates
`0x1c + ops` bytes, builds a CFunction (vtable 0x00022c18, type 13) and
copies the ops in after the header. The member SyntaxError also sets the
compiler's own error byte.

## Script compiler (CFunctionCompiler)

| Function | Address |
|----------|---------|
| ParseTerm | 0x00034710 |
| ParseOperator | 0x00034bb0 |
| ParseArray | 0x00034c20 |
| ParseCall | 0x00034c90 |
| ParseExpression(pch, prio) | 0x00034d80 |
| ParseIF | 0x00034ea0 |
| ParseWHILE | 0x00034fe0 |
| ParseDO | 0x00035170 |
| ParseFOR | 0x000352f0 |
| FixupBreaks | 0x00035510 |
| ParseBREAK | 0x00035540 |
| ParseCONTINUE | 0x00035610 |
| ParseRETURN | 0x000356c0 |
| ParseSLEEP | 0x00035760 |
| ParseBlock | 0x00035800 |
| ParseStatement | 0x00035960 |
| ParseLocalVar | 0x00035bf0 |

Layout past the CCompiler base: 100 local slots of `{short len; ptr name}`
at `+0x14`, frame size at `+0x334`, break list at `+0x338`, loop depth at
`+0x33c`, loop top address at `+0x340`, behavior flag at `+0x344`. Nothing
checks the 100 slot limit.

### Operators

The table at 0x000200e8 holds 22 ten-byte entries `{ch1, ch2, ch3, priority,
opcode}`, scanned in order with the first match winning:

| Text | Priority | Op |
|------|----------|----|
| `.` | 1 | 0x0d |
| `*` `/` | 2 | 0x0b 0x0c |
| `+` `-` | 3 | 0x09 0x0a |
| `<<` `>>` | 4 | 0x17 0x18 |
| `<=` `>=` `<` `>` | 5 | 0x14 0x15 0x12 0x13 |
| `==` `!=` | 6 | 0x10 0x11 |
| `&` | 7 | 0x1a |
| `^` | 8 | 0x1c |
| `\|` | 9 | 0x1b |
| `=` `+=` `-=` `*=` `/=` `%=` | 13 | 0x0e 0x20 0x21 0x22 0x23 0x24 |

Because `+`, `-`, `*` and `/` come before their compound forms, `a += 1`
scans as `+` and the compound entries are never reached. There are no `&&`
or `||` entries; scripts use `&` and `|`.

ParseExpression is precedence climbing from 14 down to 0, left associative.
Level 0 is ParseTerm; at level 1 a `(` after the operand is a call
(`opCall` + one byte argument count) and a `[` is an index (`opArray`).

### Terms

ParseTerm folds a run of unary `-`, `+` and `~` (`~` only reports
`"Sorry, the ~ operator has not been implemented yet!"`), then:

- `( expr )`;
- a quoted string: `opStr` + expanded text (1024 character buffer);
- a token starting with `.`, `-` or a digit: copied into a 256 character
  buffer (no length check), `wcstod`, a pending minus folded into the
  constant, `opNum` + float;
- `true` / `false`: `opNum` 1.0 / 0.0; `this`: `opThis`; `null`: `opNull`;
- `new Name(args)`: the args, then `opNew` + count + name;
- a declared local: `opLocal` + slot; anything else: `opVar` + name.

An unfolded minus emits `opNeg` after the term.

### Statements

ParseStatement leaves a `}` alone, sends `{` to ParseBlock, and otherwise
emits `opStatement` + the current line before dispatching on the keyword
(compared by length): `var`, `for`, `if`, `do`, `while`, `break`, `continue`,
`return`, and `sleep` in behaviors only. Anything else is an expression
statement compiled from the token start, then `opDrop` and a `;` that may be
missing at the end of the text.

- **Block**: `opFrame` + local count (patched), statements, `opEndFrame` +
  count; the frame size drops back afterwards.
- **if**: `opCond` + exit, then; `else` adds `opJump` over the else part.
  Without `else` the cursor backs up to the start of the peeked token.
- **while**: top at the condition, `opCond` + exit, body, `opJump` top.
- **do**: steps one character past `do`, top at the body, then
  `while ( cond ) ;` emits `opCond` + exit and `opJump` top.
- **for**: init (`var` is read from three characters past the `(`), top at
  the condition, the step clause compiled once to find `)` and discarded,
  body, step again + `opDrop`, `opJump` top.
- **break** pushes a forward `opJump` on the break list, patched to the loop
  end by FixupBreaks; **continue** jumps to the loop top. Both report
  `"unexpected 'break'"` / `"unexpected 'continue'"` outside a loop.
- **return** / **sleep**: optional value (`opNull` if none), then `opRet` /
  `opSleep`.
- **var a = x, b;**: each name takes the next slot; an initializer emits
  `opLocal` slot, the value, `opAssign`, `opDrop`.

## Scene compiler (CClassCompiler)

| Function | Address |
|----------|---------|
| CClass::ParseClassBody | 0x00033d60 |
| Compile (node list) | 0x00035e50 |
| ParseNode | 0x00035ec0 |
| ParseChildren | 0x000363c0 |
| ParseProps | 0x00036440 |
| ParseMemberVar | 0x00036ad0 |
| ParseFunction | 0x00036c30 |
| ParseClass | 0x00036e50 |
| Class_Init / Class_Exit | 0x00036f00 / 0x00036f30 |

The target class sits at `+0x14`. ParseClassBody compiles a node list until
`}` or the end, appends a 0 byte, and stores the CFunction in the class.

**ParseNode**, by first token:

- `DEF name` (path token): new CMember (vtable 0x00022dd8) in the next
  variable slot (class `+0x68`), `opDefNode` + slot, then the node;
- `USE name`: `opUseNode` + slot of an existing member, else
  `"Undefined USE '%s'"` (name cut to 255 characters);
- `behavior {...}` and `function name(...) {...}`: compiled and set as class
  members, a function named `behavior` is refused;
- `var`, `class`, `import Name, url` (url copied into a stack buffer with no
  length check, the imported class loaded and defined by name);
- otherwise a script class or built-in node class by name (`"Unknown class:
  %s"`): `opNewNode` + name, optional `{ props }` (`"Exected a '}'"`, sic),
  `opEndNode`.

One trailing `,` is consumed after every item.

**ParseProps** reads names until `}`. Inline `behavior` / `function`
compile into the class instance function array (`+0x58`) and emit
`opFunction` + name + index. Other names go through the node class property
table (`"Unknown property: %s"`), and the 12-byte PRD `{offset, type, name}`
drives the encoding:

| Type | Reader | Emitted |
|------|--------|---------|
| children (4), node array (11) | ParseChildren | `opInitArray` offset, nodes, `opEndArray` |
| node (9) | ParseNode | `opNewNodeProp` PRD, node |
| integer (1) | ParseInteger | `opInitProp` PRD, 4, value |
| boolean (2) | ParseBoolean | `opInitProp` PRD, 1, value |
| number (3) | ParseNumber | `opInitProp` PRD, 4, value |
| vec3 (5), color (8) | ParseVec3 | `opInitProp` PRD, 12, values |
| vec4 (6), quaternion (7) | ParseVec4 | `opInitProp` PRD, 16, values |
| string (10) | ParseString | `opInitProp` PRD, length, characters |
| int array (12) | ParseIntArray | `opInitProp` PRD, bytes, values |
| number / vec2 / vec3 / vec4 array (13 to 16) | ParseVecArray(1 to 4) | `opInitProp` PRD, bytes, values |

**ParseChildren** takes one node or a `[ ... ]` list. **ParseMemberVar**
adds members and refuses initializers (`"Sorry, class member initializers
have not been implemented yet!"`). **ParseFunction** builds a
CFunctionCompiler on the stack, reads the parameter list into the local
slots (behaviors have none), requires a `{` body, and appends `opNull opRet`
before creating the function. **ParseClass** compiles a nested class body
into a new CClass and defines it in the class namespace.
