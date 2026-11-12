// memutil.cpp: the desktop's memory hooks. Allocations go straight to the
// C heap; when operator new fails, NewFailed frees caches and retries like
// the dashboard does on the console. Counterpart to xbox/memutil.cpp.

#include "std.h"
#include "dashapp.h"

extern bool CleanupTextureCache();
extern bool CleanupMeshCache();

// How far the out of memory recovery has got.
int g_nNewFailedState;

// NewFailed(bytes)
// Follows the 5960 retail XBE's out of memory handling (inlined around its
// allocations, see LoadXlate @ 0x0003c370): free the texture cache, then
// the mesh cache, retrying after each; a stage that frees nothing moves on
// to the next. When both are spent it prints "xdash: out of memory" and
// gives up, which on the console returns to the firmware and here exits.
int __cdecl NewFailed(size_t nBytes)
{
	bool freed = false;
	if (g_nNewFailedState == 0)
		freed = CleanupTextureCache();
	else if (g_nNewFailedState == 1)
		freed = CleanupMeshCache();
	else
	{
		fprintf(stderr, "xdash: out of memory (failed to allocate %d bytes)\n", (int)nBytes);
		exit(1);
	}

	if (!freed)
		g_nNewFailedState++;
	return 1;
}

void* TheseusAllocMemory(int nBytes)
{
	return malloc(nBytes);
}

void TheseusFreeMemory(void* pv)
{
	free(pv);
}

void TheseusCreateMeshFVF(DWORD NumFaces, DWORD NumVertices, DWORD Options, DWORD FVF, LPD3DXMESH* ppMesh)
{
	D3DXCreateMeshFVF(NumFaces, NumVertices, Options, FVF, TheseusGetD3DDev(), ppMesh);
}

void Memory_Init()
{
	g_nNewFailedState = 0;
}
