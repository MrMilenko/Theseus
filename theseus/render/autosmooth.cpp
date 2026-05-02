// autosmooth.cpp: mesh normal auto-smoothing by crease angle. Standalone
// helper called during mesh load; not a script-callable node. Decompiled
// from the 5960 retail XBE; see docs/decomp/ShapeRender.md.

#include "std.h"

static inline bool VecsEqual(D3DXVECTOR3* a, D3DXVECTOR3* b)
{
	return (a->x == b->x && a->y == b->y && a->z == b->z);
}

static void CalcSmoothNormal(D3DXVECTOR3* faceNormals, int faceIdx,
	int* adjFaces, int nAdj, float threshold, D3DXVECTOR3* outNormal)
{
	*outNormal = faceNormals[faceIdx];

	int i;
	for (i = 0; i < nAdj; i++)
	{
		if (adjFaces[i] != faceIdx)
		{
			float dot = D3DXVec3Dot(&faceNormals[adjFaces[i]], &faceNormals[faceIdx]);
			if (dot > threshold)
				*outNormal += faceNormals[adjFaces[i]];
		}
	}
}

int autosmooth(
	int* coordindex, int numvertinds,
	D3DXVECTOR3* facenormals, int numfaces,
	float creaseAngle,
	D3DXVECTOR3* normallist, int* normalindex)
{
	float threshold = cosf(creaseAngle);

	// Find max vertex index
	int maxvert = 0;
	int i;
	for (i = 0; i < numvertinds; i++)
	{
		if (coordindex[i] > maxvert)
			maxvert = coordindex[i];
	}
	maxvert++;

	// Build per-vertex face adjacency lists
	int* facesPerVert = new int[numfaces * maxvert];
	int* numFacesPerVert = new int[maxvert];
	ZeroMemory(numFacesPerVert, sizeof(int) * maxvert);

	int faceNum = 0;
	for (i = 0; i < numvertinds; i++)
	{
		if (coordindex[i] >= 0)
		{
			facesPerVert[coordindex[i] * numfaces + numFacesPerVert[coordindex[i]]] = faceNum;
			numFacesPerVert[coordindex[i]]++;
		}
		else
		{
			faceNum++;
		}
	}

	// Generate smoothed normals, deduplicating identical results per vertex
	int* normalsPerVert = new int[numfaces * maxvert];
	int* numNormalsPerVert = new int[maxvert];
	ZeroMemory(numNormalsPerVert, sizeof(int) * maxvert);

	int normalCount = 0;
	faceNum = 0;

	for (i = 0; i < numvertinds; i++)
	{
		int vi = coordindex[i];
		if (vi >= 0)
		{
			CalcSmoothNormal(facenormals, faceNum,
				facesPerVert + (vi * numfaces), numFacesPerVert[vi],
				threshold, &normallist[normalCount]);
			D3DXVec3Normalize(&normallist[normalCount], &normallist[normalCount]);

			// Check if we already generated this normal for this vertex
			int found = 0;
			int sameIdx = 0;
			for (int j = 0; j < numNormalsPerVert[vi] && !found; j++)
			{
				sameIdx = normalsPerVert[vi * numfaces + j];
				found = VecsEqual(&normallist[sameIdx], &normallist[normalCount]);
			}

			if (found)
			{
				normalindex[i] = sameIdx;
			}
			else if (normalCount > 0 && VecsEqual(&normallist[normalCount], &normallist[normalCount - 1]))
			{
				normalindex[i] = normalCount - 1;
			}
			else
			{
				normalindex[i] = normalCount;
				normalsPerVert[vi * numfaces + numNormalsPerVert[vi]] = normalCount;
				numNormalsPerVert[vi]++;
				normalCount++;
			}
		}
		else
		{
			faceNum++;
			normalindex[i] = -1;
		}
	}

	delete[] facesPerVert;
	delete[] numFacesPerVert;
	delete[] normalsPerVert;
	delete[] numNormalsPerVert;

	return normalCount;
}
