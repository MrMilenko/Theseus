# Node System

The node system is the core of the Xbox Dashboard's scene graph engine. Every visual element, audio clip, controller input, and configuration object is a "node": a C++ class registered with the script VM via PRD (property descriptor) and FND (function descriptor) tables. XAP scripts create, configure, and call methods on nodes entirely through these descriptor tables. Backed by `node_system.cpp` and `node.h` in the active source tree.

## How These Structures Were Identified

### FND Tables (Function Descriptors)

FND entries were found by scanning the .bss region of the 4920-5960 XBE for clusters of `C7 05` (MOV [bss], imm32) instructions that write function pointers, signatures, and string pointers in 24-byte groups. The string pointers resolve to UTF-16LE function names in the .rdata section (e.g., "getDate", "SetSystemClock", "GetLanguage").

Each FND entry in the binary:

```
Offset  Size  Field
0x00    4     Member function pointer (thiscall)
0x04    12    Padding / vtable adjustment (zero for single inheritance)
0x10    2     Signature ID (sig_vv, sig_iv, etc.)
0x12    2     Padding
0x14    4     Pointer to UTF-16LE name string
```

The signature IDs (sig_vv=1, sig_iv=2, sig_ii=3, etc.) were mapped by cross-referencing the CObject::Call dispatch switch in the binary. Each case handles a different calling convention, and the signature ID selects which case to execute.

### PRD Tables (Property Descriptors)

PRD entries were found similarly. Each stores a byte offset into the C++ object, a type enum, and a pointer to the property name string. The type enum (pt_integer, pt_boolean, pt_number, pt_string, pt_vec3, pt_children, etc.) was recovered from the CObject::SetProperty switch statement, which handles each type differently.

Each PRD entry in the binary:

```
Offset  Size  Field
0x00    4     Byte offset from `this` pointer to member variable
0x04    4     Property type (PROP_TYPE enum)
0x08    4     Pointer to UTF-16LE name string
```

The first entry in each PRD table is special: its offset field points to the parent class's PRD table (forming the inheritance chain), and its name field holds the class name. This was confirmed by tracing FindProp, which walks this linked list.

### IMPLEMENT_NODE / DECLARE_NODE Macros

The macro patterns were deduced from the init function structure in the binary. Each node class has a static CNodeClass constructor that:

1. Stores the friendly name string ("Config", "SavedGameGrid", etc.)
2. Stores the sizeof the class
3. Stores the CreateNode factory function pointer
4. Links to the base class's CNodeClass
5. Links to the PRD table
6. Inserts itself into a global linked list (c_pFirstClass)

This was confirmed by Ghidra cross-references resolving each registered class to its CNodeClass instance, one per node type, all linked into the same global list.

### Verification via XAP Scripts

The PRD and FND tables were cross-verified against extracted XAP scripts from the dashboard's XIP archives. Every property name and function name used in the scripts (e.g., `theConfig.GetLanguage()`, `theSavedGameGrid.curTitle`) must have a matching FND or PRD entry. This provided:

- Complete list of exposed function names per class
- Parameter counts and types (from script call sites)
- Property types (from how scripts assign / read values)
- Inheritance relationships (which properties are inherited from parent nodes)

### CObject::Call Dispatch

The signature-based dispatch system (sig_vv, sig_iv, etc.) was recovered from the CObject::Call function in the binary. This is a large switch statement (~350 lines) that:

1. Reads the signature ID from the FND entry
2. Dereferences script parameters to native types
3. Calls the member function pointer with the correct calling convention
4. Wraps the return value back into a script object

Each signature ID encodes the return type and parameter types:

- First letter: return type (v=void, i=int, s=string, n=float, o=object)
- Remaining letters: parameter types in order

Example: `sig_si` = returns CStrObject*, takes one int parameter.

## Binary Analysis (4920-5960 retail XBE)

### Key Addresses

- `CNodeClass::c_pFirstClass` at `0x0001a068`: head of registered class list
- `_classCObject` at `0x0001a06c`: CObject's class descriptor
- `_classCTheseusNode`: base node class descriptor
- Individual class descriptors for all 64+ node types

### CObject vtable

Identified at the base of all node objects. Virtual function order:

- destructor, GetPropMap, AddRef, Release, OnSetProperty
- GetFunctionMap, GetNodeClass, ToNum, ToStr, Deref, Assign
- Call, Dot, GetMember, Dump (debug only)

### CNode vtable (extends CObject)

Additional virtuals: OnLoad, Advance, Render, GetBBox, GetRadius, GetPalette, RenderDynamicTexture, GetGroundHeight, GetTextureSurface.

### Object Type IDs

The `m_obj` field encodes runtime type. Values recovered from switch statements throughout the VM (CRunner::Step, CObject::Dot, Dereference, etc.):

```
0  = objUndefined      8  = objFunctionRef
1  = objNull            9  = objMember
2  = objNumber          10 = objMemberVar
3  = objString          11 = objInstance
4  = objVariable        12 = objUse
5  = objNode            13 = objFunction
6  = objClass           14 = objMemberFunction
7  = objNodeArray       15 = objArray
                        16 = objVec3
```

### Typed Array Classes

CIntArray, CNumArray, CVec2Array, CVec3Array, CVec4Array all follow the same pattern: `m_nAlloc`, `m_nSize`, `m_value` pointer. Used by PRD entries with types pt_intarray through pt_vec4array. Confirmed by SetProperty handling for each type.

### CNodeArray

Dynamic array of CNode pointers with reference counting. Used for `children` properties and the `m_vars` array in CInstance. Grows by 16 entries at a time (Allocate pattern).

### CTimeDepNode

Timer node base class with start / stop / loop semantics. The Advance function checks `m_startTime` / `m_stopTime` against `theApp.m_now` to toggle `m_isActive`. Used by AudioClip, TimeSensor, and other time-driven nodes.

## Node.cpp, Object.cpp and Class.cpp in the 5960 retail XBE

The base classes sit in three runs of code, each in source-file order.

| Range | Contents |
|---|---|
| `0x0002e8e0` to `0x0002f420` | Node.cpp: CNodeClass, CNode, PRD lookup, SetProperty, CNodeArray, CTimeDepNode, typed array SetSize |
| `0x0002f420` to `0x00030430` | Object.cpp: CObject construction, refcounting, FND lookup, Call, Dot, members |
| `0x00033970` to `0x00034430` | Class.cpp: CClass and CInstance |

### Object layouts

```
CObject      0x14 bytes   +0x00 vtable  +0x04 m_nRefCount  +0x08 m_obj
                          +0x0c m_parent  +0x10 m_members (CNameSpace*)
CNode        0x1c bytes   +0x14 m_behavior (CRunner*)  +0x18 m_visible
CTimeDepNode 0x30 bytes   +0x1c m_loop  +0x20 m_startTime  +0x24 m_stopTime
                          +0x28 m_isActive  +0x2c m_lastStartTime
CNodeClass                +0x14 m_className  +0x18 m_nObjectSize
                          +0x1c m_pfnCreateNode  +0x20 m_baseClass
                          +0x24 m_rgprd  +0x28 m_nextClass
CClass                    +0x2c m_url  +0x30 m_file  +0x58 m_instanceFunctions
                          +0x64 m_constructor  +0x68 m_nVarCount
CInstance    0x54 bytes   CGroup up to +0x44, +0x44 m_class  +0x48 m_vars
CNodeArray   0x0c bytes   +0x00 m_length  +0x04 m_nAlloc  +0x08 m_rgpNode
```

Vtables: CObject `0x00021b00`, CNodeClass `0x00021958`, CNode `0x000219f8`, CTimeDepNode `0x00021990`, CInstance `0x000226a0`, CClass `0x000226fc`. Slots referenced by the code below: `+0x04` GetPropMap, `+0x08` OnSetProperty, `+0x0c` AddRef, `+0x10` Release, `+0x14` GetFunctionMap, `+0x18` GetNodeClass, `+0x1c` ToNum, `+0x20` ToStr, `+0x24` Deref, `+0x34` GetMember, `+0x38` OnLoad, `+0x5c` CTimeDepNode::OnIsActiveChanged.

Globals: class list head `0x0017e730`, class records `0x0017f580` (Object), `0x0017f528` (Node), `0x0017f554` (TimeDepNode). PRD maps `0x00174418` (Object), `0x001743a8` (Node), `0x001743d0` (TimeDepNode). The current runner and `this` for behavior scripts live at `0x0017aea4` / `0x0017aea8`; the frame clock is the double at `0x0017e808`.

### Function map

| Address | Function | What it does |
|---|---|---|
| `0x0002e900` | CNodeClass::CNodeClass | Stores name, size, factory, base and PRD map, pushes the record on the front of the class list |
| `0x0002e970` | CNodeClass::~CNodeClass | Walks the list by link pointer and unlinks the record if found, then the CObject teardown |
| `0x0002ea20` | CNodeClass::FindByName | `wcsncmp` against each name plus a check that the name ends right there |
| `0x0002ea80` | CNode::~CNode | `ResetFunc` and free the behavior runner, then CObject teardown |
| `0x0002eaf0` | CNode::OnLoad | Looks up `behavior` (FindMemberFunction inlined); if found, builds a 0x1c0 byte CRunner bound to the node and points it at the function |
| `0x0002ebf0` | CNode factory | `new` 0x1c, type 5, visible 1 |
| `0x0002ec40` | CObject::FindProp | PRD walk, `_wcsnicmp` plus full-length check |
| `0x0002ecd0` | CObject::SetProperty | See below |
| `0x0002ee50` | CNode::Render | `m_visible = 0` |
| `0x0002ee60` | CNode::Advance | Runs the behavior script, see below |
| `0x0002eef0` | CNode::GetGroundHeight | Returns 0.0 |
| `0x0002ef00` | CNode::GetBBox | Zeroes the six floats |
| `0x0002ef30` | CNode::GetRadius | Returns 0.0 |
| `0x0002ef40` | CNode::RenderDynamicTexture | Empty |
| `0x0002ef60` | CNodeArray::RemoveNode | Closes up every slot that holds the node, see quirks |
| `0x0002efa0` | CNodeArray::ReleaseAll | Release every non-null slot, then length, capacity and block to zero |
| `0x0002eff0` | CNodeArray::Allocate | Grow-only: new block, copy the old capacity's worth, free the old block |
| `0x0002f050` | CTimeDepNode factory | `new` 0x30, CNode fields, all timing fields zero |
| `0x0002f0d0` | CTimeDepNode::Advance | See below |
| `0x0002f180` | CIntArray::SetSize | Grow-only reallocation then `m_nSize = n`. CNumArray's identical body was folded into this one |
| `0x0002f450` | CObject::~CObject | `CLerper::RemoveObject(this)`, then Release and free every member define and the list |
| `0x0002f4a0` / `0x0002f4b0` | AddRef / Release | `+1`; `-1` and the deleting destructor at zero |
| `0x0002f4d0` | CObject::IsKindOf | Walks `GetNodeClass()` and its `m_baseClass` chain |
| `0x0002f500` | CObject factory | `new` 0x14, type 0, refcount 1 |
| `0x0002f530` | CObject::ToStr | `new CStrObject(L"[object]")`, string at `0x00021b74` |
| `0x0002f590` | CObject::ToNum | `new CNumObject`, value 0 |
| `0x0002f5d0` | CObject::FindFunction | Linear search of the 0x18 byte FND entries until a null function pointer; `wcslen` must equal the length and `wcsncmp` must match; returns the index or -1 |
| `0x0002f640` | CObject::Call | See below |
| `0x000301c0` | CObject::Dot | See below |
| `0x00030310` | CObject::AddMember | Creates the member list on first use, then always pushes a new define at the head |
| `0x00030380` | CObject::SetMember | Creates the list on first use, then find-or-add the define and store the object |
| `0x000303c0` | CObject::GetMember | Null when there is no list, otherwise a lookup by name |
| `0x000303e0` | CObject::FindMemberFunction | Own members first; then the first class up the chain with any member of that name decides (a non-function there means not found) |
| `0x00033970` | CClass::CClass | Registers a `[class]` record with no factory, base or PRD map, clears its own fields, type 6 |
| `0x00033a20` | CClass::~CClass | Frees the URL, Releases the constructor, ReleaseAll on the instance functions, tears down the file and the class record |
| `0x00033ad0` | CClass::CreateNode | `new CInstance(this)`, 0x54 bytes |
| `0x00033b30` | CClass::Load | Saves the current directory into a 2048 byte local, moves it to the file's folder, fetches, parses, and restores it on every exit |
| `0x00033c70` | CClass::LoadAbsURL | The same fetch and parse with no directory change |
| `0x00033d00` | CClass::ParseFile | Sets the parser's file name, line 1 and error flag, parses the class body, reports `Stuff past expected end of file!` if text remains, returns no-error |
| `0x00033d60` | CClass::ParseClassBody | Class compiler over the body, stores the built function as the constructor |
| `0x00033df0` | CInstance::CInstance | CGroup init (bbox size -1.0 from `0x0002bef0`), type 11, AddRef the class, size `m_vars` to the class's variable count with null slots, Construct, then OnLoad |
| `0x00033f00` | CInstance::Construct | See below |
| `0x000342c0` | CInstance::~CInstance | Release the class, ReleaseAll the variables, CGroup teardown |
| `0x00034330` | CInstance::Dot | Class members first, then CObject::Dot |

### SetProperty (`0x0002ecd0`)

The byte count arrives in EAX. The field address is `this + prd->pbOffset`.

1. For a `pt_string` (10) property, if both the current and the new string are non-null and `wcscmp` says they're equal, it returns before anything else, so OnSetProperty never sees a no-op string write.
2. `OnSetProperty` (vtable `+0x08`) can veto the write.
3. By type:
   - `pt_string`: free the old buffer, allocate `count * 2 + 2` bytes, copy `count` characters and terminate.
   - `pt_intarray` (12), `pt_numarray` (13): `SetSize(bytes / 4)`. `pt_vec2array` (14): `bytes / 8`. `pt_vec3array` (15): `bytes / 12`. `pt_vec4array` (16): `bytes / 16`. Then the raw bytes are copied into the array's buffer.
   - Anything else: raw copy of the bytes into the field. For `pt_node` (9) the node already in the field is Released first and the new one AddRef'd after.

### Call (`0x0002f640`, 2818 bytes)

Reads the FND entry at `GetFunctionMap() + index * 0x18`: function pointer at `+0x00`, signature as a 16-bit value at `+0x10`. Every argument is first replaced in place by its fully dereferenced value (Deref at vtable `+0x24`, repeated until the object stops changing or becomes null).

The switch covers signatures 0 to 29, with no case for 6. Case 0 passes the raw `(array, count)` through. Every other case checks the count exactly, converts its arguments, makes the call, wraps the result, and Releases the converted temporaries:

| Case | Shape | Case | Shape | Case | Shape |
|---|---|---|---|---|---|
| 1 | `void ()` | 11 | `float (float, float)` | 21 | `void (object)` |
| 2 | `int ()` | 12 | `void (str)` | 22 | `void (float, float, float)` |
| 3 | `int (int)` | 13 | `void (str, str)` | 23 | `void (float, float, float, float)` |
| 4 | `str (int)` | 14 | `void (int)` | 24 | `int (int, int)` |
| 5 | `str (str)` | 15 | `object ()` | 25 | `int (str)` |
| 7 | `str (int, int)` | 16 | `object (str)` | 26 | `void (int, int, str)` |
| 8 | `str ()` | 17 | `object (object)` | 27 | `float (int)` |
| 9 | `float ()` | 18 | `void (int, int)` | 28 | `void (int, str)` |
| 10 | `float (float)` | 19 | `void (float)` | 29 | `int (int, str)` |
|  |  | 20 | `void (float, float)` |  |  |

- int arguments are `ToNum` (vtable `+0x1c`) truncated to int; float arguments are the CNumObject's value as is.
- string arguments are `ToStr` (vtable `+0x20`); a null text pointer is replaced by the empty string at `0x00021b98`.
- object arguments are passed as is.
- int and float results come back as a new CNumObject (its constructor at `0x00038fa0` takes the value in xmm0).
- Case 6 has no entry and shares the exit with unknown signatures: return null, no message.
- A wrong argument count calls the running script's error routine with `invalid number of parameters` and returns null.

### Dot (`0x000301c0`)

Only an objVariable (4) name is resolved, using its text at `+0x1c` and length at `+0x14`:

1. FindFunction. A hit returns a new 0x1c byte CFunctionObject (type 8) holding the index and `this`, and AddRefs `this`.
2. GetMember (vtable `+0x34`). A hit is AddRef'd and returned as is.
3. FindProp. A hit returns a new CProperty (`0x00038a20`) for `this` and the PRD.
4. Otherwise null.

The compiled code checks the type for objVariable a second time before step 3, which can never fail at that point.

### CNode::Advance (`0x0002ee60`)

The runner's first field is its wake-up time as a double (`0.0`, from `0x0002bef8`, means awake).

```
if (no runner) return;
if (wake != 0) { if (now < wake) return; wake = 0; }
current runner = runner; current this = node;
for (;;) {
    do { if (!Step()) ResetFunc(); } while (wake == 0);
    if (now < wake) break;
    wake = 0;
}
```

A behavior script runs every frame until it sleeps into the future. When the function returns, the runner restarts it from the top within the same frame.

### CTimeDepNode::Advance (`0x0002f0d0`)

```
CNode::Advance();
if (!m_isActive) {
    if (start <= (float)now && start != lastStart && (stop == 0.0 || now < stop))
        { lastStart = start; m_isActive = 1; OnIsActiveChanged(); }
} else if (start <= stop && stop != start && stop <= (float)now) {
    m_isActive = 0; OnIsActiveChanged();
}
```

The start test and the stop test narrow the double clock to float; the `now < stop` test compares at double precision. A node fires once per distinct start time, and a stop time only counts when it is later than the start.

### CInstance::Construct (`0x00033f00`, 915 bytes)

Runs the class constructor's bytecode from `m_constructor + 0x18`. It keeps a 100-entry stack of open nodes, a 100-entry stack of open child arrays (starting at the instance's own children at `+0x1c`), a 2048 byte buffer for string literals, and a pending variable slot (-1 when unset). Opcodes 100 to 108 are handled through a jump table; any other byte ends the loop and the function returns a pointer to it.

| Op | Operands | Effect |
|---|---|---|
| 100 NewNode | `int cch`, `cch` UTF-16 chars | Push the open node, create the class by name (CreateNewObject `0x00037580`), set its parent to the instance, append it to the open array (growing 16 slots at a time). A pending variable slot gets the node (AddRef'd) and is cleared |
| 101 NewNodeProp | 12 byte PRD, then either `103 int n` or `[102 int n] 100 int cch chars` | With 103: store variable `n` into the open node's property. Otherwise create the node as above, store it into the property of the node underneath, Release the creation reference, and fill a pending slot |
| 102 DefNode | `int n` | The next node created is also kept in variable `n` |
| 103 UseNode | `int n` | AddRef variable `n` and append it to the open array |
| 104 EndNode | none | OnLoad (vtable `+0x38`) the open node and pop |
| 105 InitProp | 12 byte PRD, `int cb`, `cb` bytes | SetProperty on the open node. For strings `cb` is a character count: the text is copied into the local buffer and terminated, and the stream skips `cb * 2` bytes |
| 106 InitArray | `int offset` | Push the open array; the new one is the node array at `node + offset` |
| 107 EndArray | none | Pop the open array |
| 108 Function | `int cch`, chars, `int n` | AddRef the class's instance function `n` and SetMember it on the open node under that name |

Variable and instance function reads are bounds checked (out of range reads give null); the node stacks are not.

### Quirks

- **CNodeArray::RemoveNode moves bytes, not pointers.** The close-up copy uses `rep movsd` with a count of `(length - i - 1) >> 2` followed by `(length - i - 1) & 3` single bytes, so it moves `length - i - 1` bytes when it should move that many 4 byte pointers. Removing anything but one of the last few entries leaves stale pointers in the tail.
- **Unknown Call signatures are silent.** Signature 6 and anything above 29 return null without reporting an error.
- **Signature 29** (`int (int, str)`) exists in the dispatcher.
- **The CNumArray and CIntArray SetSize bodies are one function** in the image; both property types call `0x0002f180`.
- **CNode::OnLoad and CNodeClass::FindProp are not separate functions.** OnLoad carries its own inlined copy of FindMemberFunction, and the class compiler (`CClassCompiler::ParseProps`, `0x00036440`) calls CObject::FindProp directly on a class record, whose GetPropMap returns its `m_rgprd`.
