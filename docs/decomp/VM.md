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
