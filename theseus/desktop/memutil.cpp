// memutil.cpp: desktop allocator backend. Plain new / delete pass-
// through with no Xbox-style cache eviction (the host OS handles OOM).
// Counterpart to xbox/memutil.cpp.

#include "std.h"
#include "dashapp.h"

// Desktop: simple memory management - no Xbox D3D memory pools

extern void CleanupImageCache();
extern bool CleanupTextureCache();
extern bool CleanupMeshCache();

int g_nNewFailedState;

int __cdecl NewFailed(size_t nBytes)
{
	TRACE("\001new failed to allocate %d bytes!\n", nBytes);

	switch (g_nNewFailedState)
	{
	case 0:
		TRACE("\002Cleaning the texture cache!\n");
		if (CleanupTextureCache())
			return 1;
		break;

	case 1:
		TRACE("\002Cleaning the mesh cache!\n");
		if (CleanupMeshCache())
			return 1;
		break;

	default:
		// We've run out of things to try!
		fprintf(stderr, "xdash: out of memory (failed to allocate %d bytes)\n", nBytes);
		exit(1);
	}

	g_nNewFailedState += 1;
	return 1; // keep trying!
}

void *TheseusAllocMemory(int nBytes)
{
	return malloc(nBytes);
}

void TheseusFreeMemory(void *pv)
{
	free(pv);
}

void TheseusCreateMeshFVF(DWORD NumFaces, DWORD NumVertices, DWORD Options, DWORD FVF, LPD3DXMESH *ppMesh)
{
	D3DXCreateMeshFVF(NumFaces, NumVertices, Options, FVF, TheseusGetD3DDev(), ppMesh);
}

void Memory_Init()
{
	g_nNewFailedState = 0;
}
