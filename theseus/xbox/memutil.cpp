// memutil.cpp: Xbox memory management with cache eviction on
// failure. Allocations retry through NewFailed(), which sequentially
// evicts the texture cache and then the mesh cache before
// reboot-on-OOM. Desktop supplies its own variant in
// desktop/memutil.cpp.
#include "std.h"
#include "theseus.h"

extern "C" void* WINAPI D3D_AllocNoncontiguousMemory(DWORD Size);
extern "C" void WINAPI D3D_FreeNoncontiguousMemory(void* pMemory);

extern void CleanupImageCache();
extern bool CleanupTextureCache();
extern bool CleanupMeshCache();

int g_nNewFailedState;

int __cdecl NewFailed(size_t nBytes)
{
	switch (g_nNewFailedState)
	{
	case 0:
		if (CleanupTextureCache()) { g_nNewFailedState = 0; return 1; }
		break;
	case 1:
		if (CleanupMeshCache()) { g_nNewFailedState = 0; return 1; }
		break;
	default:
		DbgPrint("xdash: out of memory (failed to allocate %d bytes)\n", nBytes);
#ifdef DEVKIT
		__asm int 3;
#endif
		HalReturnToFirmware(HalRebootRoutine);
	}

	g_nNewFailedState++;
	return 1;
}

void* TheseusAllocMemory(int nBytes)
{
	for (;;)
	{
		void* pv = (void*)GlobalAlloc(GMEM_FIXED, nBytes);
		if (pv != NULL) return pv;
		if (NewFailed(nBytes) == 0) return NULL;
	}
}

void TheseusFreeMemory(void* pv)
{
	if (pv != NULL)
		GlobalFree((HGLOBAL)pv);
}

void* TheseusD3D_AllocContiguousMemory(DWORD Size, DWORD Alignment)
{
	for (;;)
	{
		void* pv = D3D_AllocContiguousMemory(Size, Alignment);
		if (pv != NULL) return pv;
		if (NewFailed(Size) == 0) return NULL;
	}
}

void* TheseusD3D_AllocNoncontiguousMemory(DWORD Size)
{
	for (;;)
	{
		void* pv = D3D_AllocNoncontiguousMemory(Size);
		if (pv != NULL) return pv;
		if (NewFailed(Size) == 0) return NULL;
	}
}

void TheseusCreateMeshFVF(DWORD NumFaces, DWORD NumVertices, DWORD Options, DWORD FVF, LPD3DXMESH* ppMesh)
{
	for (;;)
	{
		HRESULT hr = D3DXCreateMeshFVF(NumFaces, NumVertices, Options, FVF, TheseusGetD3DDev(), ppMesh);
		if (hr != E_OUTOFMEMORY) return;
		if (NewFailed(NumVertices * 32 + NumFaces * 6) == 0) return;
	}
}

void Memory_Init()
{
	_set_new_handler(NewFailed);
	_set_new_mode(1);
	g_nNewFailedState = 0;
}
