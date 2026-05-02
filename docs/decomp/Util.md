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
