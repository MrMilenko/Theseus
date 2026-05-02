#pragma once

// D3DVERTEX layout used by procedural geometry in the dashboard (boxes,
// spheres, billboards). Standard XYZ + normal + 1 texcoord. The Xbox D3D8
// SDK headers do not provide D3DVERTEX directly, so it is defined here.
// The dv* aliases inside each union are kept for legacy field-style
// access used by main.cpp, scene_groups.cpp, and hud_node.cpp.

typedef float D3DVALUE, *LPD3DVALUE;

#define D3DFVF_VERTEX (D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1)

typedef struct _D3DVERTEX
{
    union { D3DVALUE x;  D3DVALUE dvX;  };
    union { D3DVALUE y;  D3DVALUE dvY;  };
    union { D3DVALUE z;  D3DVALUE dvZ;  };
    union { D3DVALUE nx; D3DVALUE dvNX; };
    union { D3DVALUE ny; D3DVALUE dvNY; };
    union { D3DVALUE nz; D3DVALUE dvNZ; };
    union { D3DVALUE tu; D3DVALUE dvTU; };
    union { D3DVALUE tv; D3DVALUE dvTV; };

    _D3DVERTEX() {}
    _D3DVERTEX(const D3DVECTOR& v, const D3DVECTOR& n, float _tu, float _tv)
    {
        x = v.x;  y = v.y;  z = v.z;
        nx = n.x; ny = n.y; nz = n.z;
        tu = _tu; tv = _tv;
    }
} D3DVERTEX, *LPD3DVERTEX;
