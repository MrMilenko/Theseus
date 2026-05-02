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
