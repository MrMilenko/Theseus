# Util

Utility classes used by the node and VM system. No XAP-visible nodes; this is internal infrastructure.

## CNameSpace

Symbol table mapping names to `CNode*` pointers. Implemented as a singly-linked list of `CDefine` entries. The VM uses this for variable lookup during script execution: every scope level (global, local, function) maintains its own `CNameSpace`.

Lookup walks the chain linearly; insertions prepend to the head. No hash table: the dashboard's script scopes are small enough that linear search is adequate.

## CDefine

A single name/node pair in the namespace chain. Each entry holds:
- `TCHAR* m_szName`: the identifier string
- `CNode* m_pNode`: the bound node
- `CDefine* m_pNext`: next entry in the chain

## CLerper

Linear interpolation helper that drives smooth property transitions (e.g. CLayer transparency fades, color shifts). Each CLerper instance tracks a start value, end value, duration, and elapsed time.

Active lerpers are maintained in a global linked list. The main loop ticks all active lerpers each frame, advancing interpolation and removing completed ones. This is how the dashboard achieves its smooth UI transitions without per-node timer logic.

## NewFailed()

Global `new` handler registered at startup. When a heap allocation fails, this function attempts to recover by purging texture and mesh caches. If memory is reclaimed, the allocation is retried. This is the dashboard's only defense against OOM on the Xbox's fixed 64MB RAM.

## Typed Array Classes

Fixed-type dynamic arrays used primarily by interpolator keyframes:

- **CFloatArray**: array of `float` values
- **CVec3Array**: array of 3-component vectors (x, y, z)
- **CVec4Array**: array of 4-component vectors (x, y, z, w)

These provide indexed access and dynamic resizing. The interpolator nodes (CPositionInterpolator, COrientationInterpolator, etc.) store their key/keyValue data in these arrays.

## Addresses

| Address | Name | What it does |
|---------|------|--------------|
| 0x0002f180 | CIntArray::SetSize | 4 byte elements; floats share the same body. Grows the buffer to exactly the new size when it is too small, copying the old values, and always stores the size |
| 0x0002f1e0 / 0x0002f2c0 / 0x0002f370 | CVec2/3/4Array::SetSize | The same for 8, 12 and 16 byte vectors (constructed through the vector constructor iterator) |
| 0x00032bb0 | CNameSpace::Define | Fails when the name is bound already; otherwise allocates `cch * 2 + 0xc` bytes (next, node, name), copies and terminates the name and pushes it on the front |
| 0x00032c20 | CNameSpace::Get | Returns the entry whose name has that exact length and matches with `wcsncmp`, or pushes a new one with no node |
| 0x00032ca0 | CNameSpace::Lookup | The node bound to the name (checks the first character, then length, then `wcsncmp`), or NULL |
| 0x00039be0 | CLerper::CLerper | Interval, start time from the frame clock (`0x0017e808`), start value read through the pointer, end value, the pointer, the owner; pushed on the list at `0x0017aeac` |
| 0x00039c20 | CLerper::AdvanceAll | For each lerper `t = (now - start) / interval`; below 1 writes `(1 - t) * start + end * t`, otherwise writes the end value, unlinks and frees it |
| 0x00039ca0 | CLerper::RemoveObject | Unlinks and frees every lerper with that owner |

A namespace's entries are released inline where it is freed (the class destructor walks the list,
calls `Release` on each node and frees the entry).

The out of memory path (see LoadXlate, `0x0003c370`) frees the texture cache, then the mesh cache,
and when neither frees anything prints "xdash: out of memory (failed to allocate %d bytes)" and
returns to the firmware.
