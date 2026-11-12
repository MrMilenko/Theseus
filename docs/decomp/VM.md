# Script VM

The XAP scripting system is a custom bytecode VM embedded in the Xbox Dashboard. XAP files contain VRML97-like scene graph declarations mixed with JavaScript-like scripting. The compilation pipeline tokenizes and parses XAP source into bytecode, which the VM interprets at runtime. Backed by `xap_compile.cpp` (lexer, parser, compiler) and `xap_vm.cpp` (runner) in the active source tree.

## Architecture

### Compilation Pipeline

1. **Tokenizer**: character-level scanning, whitespace and comment handling, string escape processing.
2. **Scene Parser (CClassCompiler)**: parses VRML97 node declarations (DEF, USE, children, properties).
3. **Expression Compiler (CFunctionCompiler)**: recursive descent parser for JS expressions and statements.
4. **Bytecode Emitter (CCompiler)**: outputs opcode stream with fixups for jumps and branches.

### Runtime

1. **CRunner**: stack-based bytecode interpreter with context stack for nested calls.
2. **Property System (CProperty)**: bridges script property access to C++ node member offsets via the PRD table.
3. **Object System**: reference-counted runtime types (CNumObject, CStrObject, CVarObject, etc.).

## Binary Analysis (4920-5960 retail XBE)

### Opcode Set

Recovered from disassembly of CRunner::Step. The VM uses a single-byte opcode format; operands follow inline in the bytecode stream.

Identified opcodes:

- `opNull`, `opThis`: push constants
- `opNum`, `opStr`: push literal values (float / string follow inline)
- `opVar`: push late-bound variable reference (name follows inline)
- `opLocal`: push stack frame local by index
- `opDot`, `opArray`: member access and indexing
- `opCall`: function call (arg count follows as byte)
- `opNew`: object construction (param count and class name follow)
- `opAssign`: assignment to l-value
- `opAdd`, `opSub`, `opMul`, `opDiv`, `opMod`: arithmetic
- `opEQ`, `opNE`, `opLT`, `opLE`, `opGT`, `opGE`: comparison
- `opAnd`, `opOr`, `opXor`, `opSHL`, `opSHR`: bitwise
- `opNeg`: unary negation
- `opCond`: conditional branch (jump target follows as UINT)
- `opJump`: unconditional branch
- `opRet`: return from function
- `opSleep`: yield execution for N seconds (behavior-only)
- `opDrop`: pop and discard top of stack
- `opFrame`, `opEndFrame`: manage local variable scope
- `opStatement`: line number tracking (line number follows as int)
- `opDefNode`, `opUseNode`: scene graph DEF / USE
- `opNewNode`, `opEndNode`: node instantiation
- `opInitProp`, `opInitArray`, `opEndArray`: property initialization
- `opNewNodeProp`, `opFunction`: inline node property and function binding

### Operator Precedence Table

Found in .rdata as an array of DOPER structs mapping character pairs to opcodes with priority levels:

- Priority 1: `.` (member access)
- Priority 2: `*`, `/`
- Priority 3: `+`, `-`
- Priority 4: `<<`, `>>`
- Priority 5: `<`, `<=`, `>`, `>=`
- Priority 6: `==`, `!=`
- Priority 7-9: `&`, `^`, `|`
- Priority 13: `=`, `+=`, `-=`, `*=`, `/=`, `%=`

### Built-in Functions

Identified by string comparison in CRunner::ExecuteBuiltIn:

- `EnableInput`: toggle joystick input
- `eval`: parse and execute a string as script
- `launch`: call `XWriteTitleInfoAndReboot` to launch a title
- `alert`: display message
- `log`: trace output
- `DebugBreak`: debug-only breakpoint

### Variable Resolution

CRunner::LookupVariable / LookupMember performs late-bound lookup in this order:

1. Self object properties (PRD table match)
2. Instance members (scripted class variables)
3. Class hierarchy (walk base classes)
4. Parent chain (walk scene graph parents)

Special globals resolved by name string comparison: `Math`, `camera`.

### CFunction Layout

Compiled functions are allocated with trailing bytecode:

```
[CFunction header][m_rgop bytecode bytes...]
```

Created via placement new: `new(m_nop) CFunction` allocates `sizeof(CFunction) + m_nop` extra bytes.

### Key Globals

- `g_pRunner`: current active VM instance
- `g_pThis`: current `this` object for property resolution
- `g_classes`: CNameSpace for scripted class definitions
- `g_rgParam` / `g_nParam`: constructor parameter passing

### Comment / Whitespace Handling

The tokenizer supports three comment styles:

- `#` line comments
- `//` line comments
- `/* */` block comments

## Runner Internals (5960 retail XBE)

The runner code sits together at 0x36f70 to 0x39b90. Everything below is read from the retail binary.

### CRunner Layout

The runner is never heap allocated. `CallFunction` (0x371a0), `ExecuteScript` (0x36fb0) and `CMemberFunctionObject::Deref` (0x39810) each build one on the stack and tear it down with `ResetFunc`.

| Offset | Field | Notes |
|--------|-------|-------|
| 0x000 | wakeup | double, sleep deadline |
| 0x008 | nop | byte offset into the op stream |
| 0x00c | ops | current op stream |
| 0x010 | sp | stack top index |
| 0x014 | stack | 100 object pointers |
| 0x1a4 | spFrame | first argument of the current call |
| 0x1a8 | spBase | top of the locals |
| 0x1ac | self | owner of the running function |
| 0x1b0 | this | object the script was started on |
| 0x1b4 | error | set by the first error, stops `Run` |
| 0x1b8 | line | last `opStatement` line |
| 0x1bc | context | saved frames, newest first |

A saved frame (`PushContext` 0x37690, 0x1c bytes) holds the next link, ops, nop, spFrame, self, this, and the stack index under the callee and its arguments. `PopContext` (0x376e0) resolves the return value, restores the five fields (and `g_pThis`), releases every slot above the saved index, frees the frame and pushes the value.

### Globals

| Address | Use |
|---------|-----|
| 0x17ae9c | constructor argument count |
| 0x17aea0 | constructor argument array |
| 0x17aea4 | runner currently stepping |
| 0x17aea8 | `this` for member lookups |
| 0x17c058 | input enable flag (`EnableInput`) |
| 0x17fac8 | the `Math` object |
| 0x187fb0 | the camera |

### Step (0x37800)

One switch on the opcode byte. Operands follow inline: ints and floats are 4 bytes, a string is a 4 byte length followed by that many 16-bit characters, `opCall` takes a single byte.

| Op | Operands | Behavior |
|----|----------|----------|
| opNull | | push null |
| opNew | int n, string class | `CreateNewObject` with the n values on the stack, release them, push the result. Unknown class: `Failed to create new object: "%s"` (name cut to 255) |
| opVar | string | push a new unresolved name object |
| opJump | uint | jump |
| opCond | uint | pop, resolve, `ToNum`; nonzero skips the operand, zero or null jumps |
| opCall | byte n | see below |
| opRet | | with a saved frame, `PopContext`; at the top, pop the result into the out pointer and stop |
| opStr / opNum | string / float | push a string / number |
| opAdd .. opXor | | `BinaryOperator` |
| opDot / opArray | | pop key and base, both must be non-null; `opArray` resolves the key fully; the base is resolved one step and asked `Dot(key)`. A null answer reports `Unknown member: %s` but still pushes |
| opAssign | | pop value (resolved) and target; null target is `cannot assign to null`; else `target->Assign(value)`, push value |
| opLocal | int i | slot `spFrame + i`; creates a `CLocalVariable` there on first use, pushes it with a new reference |
| opThis | | push self |
| opFrame | int n | `sp += n`, `spBase = sp`; no size check |
| opStatement | int line | store the line |
| opNeg | | pop and resolve; not a number is `expected a number`; pushes `0.0 - v` |
| opDrop | | pop and release |
| opEndFrame | int n | release n slots off the top, `spBase = sp` |
| opSleep | | pop and resolve; null is `illegal null reference`, not a number `expected a number`; wakeup = now + v |

Any other byte stops the runner.

`opCall` looks at the object under the n arguments:

- type 8 (native function ref): call it, release it and the arguments, push the result.
- type 14 (bound method): AddRef it, `PushContext(n)`, `spFrame = sp - n`, self = its owner, ops = its code.
- otherwise it is a name. `ExecuteBuiltIn` gets the first try; if it handled the call the argument slots are cleared without a release, the name is released and the result pushed. Then `LookupFunction`; a hit pushes a frame and sets self and `g_pThis` to the owner. A miss is `unknown function: %s` with the name cut to 31 characters.

There is no null check on the callee.

### BinaryOperator (0x37fe0)

Pops right then left, each resolved fully.

1. Either null, or both type 5 (node): `==` and `!=` compare the pointers, anything else is `illegal null reference`.
2. Both strings and the op is not `+`: `wcscmp` (null text reads as empty), then `== != < > <= >=` push 1 or 0. Anything else is `type mismatch`.
3. Both numbers: `+ - * /`, the six comparisons, and `& | ^ << >>`. The bit ops convert both sides with `_ftol`, mask the shift count to 5 bits, and turn the result back into a float as unsigned (a negative int gets 4294967296.0 added, the constant at 0x2bed0). Any other op, including `%`, falls out of the switch and pushes the right operand unchanged.
4. Otherwise `+` concatenates the two `ToStr` forms and anything else is `type mismatch error`.

The compiler never emits `%`, so the fallthrough is unreachable from script.

### Errors (0x375e0)

Only the first error of a run counts. The message is formatted into 512 characters, wrapped with or without the line number, and the error flag is set. Retail never displays it; the run just stops.

### Built-ins (0x38620)

Matched by length, then by text: `EnableInput` (1 arg, else `Invalid parameter`), `eval` (1 arg, else `Bad launch`), `launch` (1 or 2 args, else `Bad launch`), `DebugBreak` (does nothing), `alert` (1 arg, debug print, else `Bad alert`), `log` (1 arg, does nothing with it in retail, else `Bad log!`).

`launch` with one argument copies the path, turns its last `/` into `\`, then cuts at the last `\` to get the directory. There is no check that a `\` was found. Both paths go through `CleanFilePath`, a lock at 0x17aeb0 is taken exclusive, and the title is launched through `XWriteTitleInfoAndRebootA` with launch data type -1.

### Lookup

`LookupMember` (0x399e0) sets `g_pThis`, then tries in order: a node property (type 5 objects only, wrapped in a `CProperty` whose ref count is set to 0), the object's own members, each class up the base chain, and then the same lookup on each parent.

`LookupFunction` (0x389b0) tries `FindMemberFunction` on self, then `LookupMember` on each parent. A hit on a parent makes that parent the owner. A hit that is not a function (type 13) is returned as a raw pointer anyway.

`CVarObject::Deref` (0x394e0) special-cases the names `Math` and `camera`, then looks the name up on self and, if self is not `this`, on `this`. A miss is `Unknown object: %s`; `Assign` (0x395b0) reports `Unknown variable: %s`.

### Value Objects

| Type | Class | Vtable | Notes |
|------|-------|--------|-------|
| 2 | CNumObject | 0x22c50 | `ToStr` (0x38fd0) prints `%f` into 20 characters, then trims trailing zeros and a trailing point |
| 4 | CVarObject | 0x22cf8 | owns a copy of the name |
| 7 | CNodeArrayObject | 0x22d30 | `length`, `add`, `remove` become function refs 0, 1, 2; a number indexes, out of range is `Array reference out of bounds` |
| 8 | CFunctionObject | 0x22cc0 | index plus a referenced owner |
| 9 | CMember | 0x22dd8 | variable slot on whatever `g_pThis` is |
| 10 | CMemberVarObject | 0x22da0 | variable slot on a referenced instance |
| 13 | CFunction | 0x22c18 | code size at +0x14, ops from +0x18 |
| 14 | CMemberFunctionObject | 0x22d68 | reading one without calling runs it on a fresh runner |
| 16 | CVec3Object | 0x22c88 | node class `Vector3`; `ToStr` is `%f %f %f` |
| | CLocalVariable | 0x22e48 | one reference to its value; `Deref` swaps the slot reference for a value reference |
| | CProperty | 0x22e10 | node plus PRD |

`CProperty::Deref` (0x38b00) wraps integer, boolean and number fields as numbers, vec3 as a `Vector3`, node fields as the node itself, strings as a new string, and children / node array fields as a `CNodeArrayObject` over the field. Anything else is `Unknown property type`.

`CProperty::Assign` (0x38d70) refuses null for everything but node fields (`Illegal null assignement`, spelled that way in the binary). Integers truncate, booleans test against 0.0, a vec3 field only takes a `Vector3`, and other types are ignored.

## Array (0x30440..0x30e70)

`CArrayObject` is the script's `Array`: object type 15, 0x20 bytes, length at 0x14,
allocated slots at 0x18, element pointers at 0x1c. Its function table at `0x0017f5d8`
holds `concat` (sig 17), `join` (5), `reverse` (15), `slice` (0, raw parameter list) and
`sort` (15).

- **Script constructor (`0x00030630`).** With one argument the argument is dereferenced
  until it stops changing, written back into the parameter slot, converted with `ToNum`
  and truncated: `new Array(5)` is five nulls. With two or more, the length is the
  argument count and each argument is stored with an AddRef. No arguments, no block.
- **Native constructor (`0x00030770`).** `n` zeroed slots, or none for 0.
- **Destructor (`0x00030800`).** Releases each non-null element, frees the block.
- **Grow (`0x000308e0`).** Only grows. When the new length passes the allocation it
  allocates `n + 16` slots, copies the old length over and frees the old block. The new
  tail is zeroed.
- **Dot (`0x00030970`).** A variable named `length` gives a new number; a number gives a
  `CArrayElementReference` for `(int)n`; anything else goes to the base Dot.
- **Element reference.** Holds an AddRef on the array (`0x00030440`, released at
  `0x000304c0`). `Assign` (`0x00030520`) grows to `index + 1` when `length <= index`,
  releases the old slot and stores the new object with an AddRef. `Deref` (`0x00030570`)
  reports `Array reference out of bounds` for a negative or too large index; otherwise it
  AddRefs the element, releases the reference itself and returns the element.
- **concat (`0x00030a70`).** Errors with `object is not an array` unless the argument is
  type 15; otherwise a new array of both arrays' elements.
- **join (`0x00030b80`).** Builds a new string: each element's `ToStr` text (an empty
  string when it has none), `[null]` for an empty slot, and the separator between
  elements. `ToStr` (`0x00030960`) is `join(",")`.
- **reverse (`0x00030c40`)** and **sort (`0x00030e70`)** return new arrays. Sort is the
  CRT `qsort` with the comparator at `0x00030e00`: two nulls are equal, a null sorts
  first, otherwise `wcscmp` of the two `ToStr` texts.
- **slice (`0x00030cd0`).** Takes one or two parameters, else `invalid number of
  parameters`. Start is `(int)` of the first; end is the second or -1. A negative end
  becomes `end + length + 1`, so the default means "through the last element". The count
  is `end - start`, floored at 0. Start is never range checked.
