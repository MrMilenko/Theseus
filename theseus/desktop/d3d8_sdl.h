// d3d8_sdl.h: D3D8 to SDL / OpenGL stub layer. Provides D3D8 types
// and interfaces backed by OpenGL 3.2 Core; vertex transforms and
// falloff shading run on the GPU via GLSL.

#pragma once

#include "platform_shim.h"

// stb_image declarations (implementation in sdl_main.cpp)
extern "C" unsigned char* stbi_load_from_memory(const unsigned char*, int, int*, int*, int*, int);
extern "C" void stbi_image_free(void*);

// OpenGL 3.2 Core Profile
#ifdef __APPLE__
    #define GL_SILENCE_DEPRECATION
    #include <OpenGL/gl3.h>
#elif defined(_WIN32)
    #include <GL/glew.h>  // Windows needs GLEW for GL 3.2+ function pointers
#else
    #define GL_GLEXT_PROTOTYPES
    #include <GL/gl.h>
    #include <GL/glext.h>
#endif

// Ensure D3D vertex constructors are available (vertex8.h, hud.cpp)
#ifndef DIRECT3D_VERSION
#define DIRECT3D_VERSION 0x0800
#endif
#ifndef D3D_OVERLOADS
#define D3D_OVERLOADS
#endif

// -------------------------------------------------------
// D3D enums and constants
// -------------------------------------------------------
typedef enum _D3DFORMAT {
    D3DFMT_UNKNOWN = 0,
    D3DFMT_A8R8G8B8 = 21,
    D3DFMT_X8R8G8B8 = 22,
    D3DFMT_R5G6B5 = 23,
    D3DFMT_A4R4G4B4 = 26,
    D3DFMT_A1R5G5B5 = 25,
    D3DFMT_DXT1 = 0x31545844,
    D3DFMT_DXT2 = 0x32545844,
    D3DFMT_DXT3 = 0x33545844,
    D3DFMT_DXT4 = 0x34545844,
    D3DFMT_DXT5 = 0x35545844,
    D3DFMT_D16 = 80,
    D3DFMT_D24S8 = 75,
    D3DFMT_INDEX16 = 101,
    D3DFMT_INDEX32 = 102,
    D3DFMT_L8 = 50,
    D3DFMT_A8 = 28,
    D3DFMT_A8L8 = 51,
    D3DFMT_P8 = 41,
    D3DFMT_LIN_A8R8G8B8 = 0x12, // Xbox linear formats
    D3DFMT_LIN_X8R8G8B8 = 0x1E,
    D3DFMT_LIN_R5G6B5 = 0x11,
    D3DFMT_LIN_A4R4G4B4 = 0x1D,
    D3DFMT_LIN_A1R5G5B5 = 0x10,
    D3DFMT_LIN_L8 = 0x13,
    D3DFMT_LIN_A8 = 0x19,
} D3DFORMAT;

typedef enum _D3DPOOL {
    D3DPOOL_DEFAULT = 0,
    D3DPOOL_MANAGED = 1,
    D3DPOOL_SYSTEMMEM = 2,
} D3DPOOL;

typedef enum _D3DPRIMITIVETYPE {
    D3DPT_POINTLIST = 1,
    D3DPT_LINELIST = 2,
    D3DPT_LINESTRIP = 3,
    D3DPT_TRIANGLELIST = 4,
    D3DPT_TRIANGLESTRIP = 5,
    D3DPT_TRIANGLEFAN = 6,
} D3DPRIMITIVETYPE;

typedef enum _D3DTRANSFORMSTATETYPE {
    D3DTS_VIEW = 2,
    D3DTS_PROJECTION = 3,
    D3DTS_WORLD = 256,
    D3DTS_TEXTURE0 = 16,
} D3DTRANSFORMSTATETYPE;

typedef enum _D3DRENDERSTATETYPE {
    D3DRS_ZENABLE = 7,
    D3DRS_FILLMODE = 8,
    D3DRS_ZWRITEENABLE = 14,
    D3DRS_ALPHATESTENABLE = 15,
    D3DRS_SRCBLEND = 19,
    D3DRS_DESTBLEND = 20,
    D3DRS_CULLMODE = 22,
    D3DRS_ZFUNC = 23,
    D3DRS_ALPHAREF = 24,
    D3DRS_ALPHAFUNC = 25,
    D3DRS_ALPHABLENDENABLE = 27,
    D3DRS_FOGENABLE = 28,
    D3DRS_FOGCOLOR = 34,
    D3DRS_SPECULARENABLE = 29,
    D3DRS_LIGHTING = 137,
    D3DRS_AMBIENT = 139,
    D3DRS_COLORVERTEX = 141,
    D3DRS_TEXTUREFACTOR = 60,
    D3DRS_WRAP0 = 128,
    D3DRS_CLIPPING = 136,
    D3DRS_VERTEXBLEND = 151,
    D3DRS_MULTISAMPLEANTIALIAS = 161,
    D3DRS_EDGEANTIALIAS = 40,
    D3DRS_COLORWRITEENABLE = 168,
    D3DRS_SHADEMODE = 9,
    D3DRS_DITHERENABLE = 26,
    D3DRS_STENCILENABLE = 52,
    D3DRS_STENCILFAIL = 53,
    D3DRS_STENCILZFAIL = 54,
    D3DRS_STENCILPASS = 55,
    D3DRS_STENCILFUNC = 56,
    D3DRS_STENCILREF = 57,
    D3DRS_STENCILMASK = 58,
    D3DRS_STENCILWRITEMASK = 59,
    D3DRS_ZBIAS = 47,
    D3DRS_NORMALIZENORMALS = 143,
    D3DRS_DIFFUSEMATERIALSOURCE = 145,
    D3DRS_SPECULARMATERIALSOURCE = 146,
    D3DRS_AMBIENTMATERIALSOURCE = 147,
    D3DRS_EMISSIVEMATERIALSOURCE = 148,
    D3DRS_WRAP1 = 129,
    D3DRS_WRAP2 = 130,
    D3DRS_WRAP3 = 131,
    D3DRS_LOCALVIEWER = 142,
    D3DRS_POINTSIZE = 154,
    D3DRS_POINTSIZE_MIN = 155,
    D3DRS_POINTSPRITEENABLE = 156,
    D3DRS_POINTSCALEENABLE = 157,
    D3DRS_POINTSCALE_A = 158,
    D3DRS_POINTSCALE_B = 159,
    D3DRS_POINTSCALE_C = 160,
    D3DRS_FOGTABLEMODE = 35,
    D3DRS_FOGSTART = 36,
    D3DRS_FOGEND = 37,
    D3DRS_FOGDENSITY = 38,
    D3DRS_RANGEFOGENABLE = 48,
    D3DRS_MULTISAMPLEMASK = 162,
    D3DRS_SWATHWIDTH = 200, // Xbox-specific
} D3DRENDERSTATETYPE;
#define D3DSWATH_OFF 0
#define D3DSWATH_128 1

// Material color source
#define D3DMCS_MATERIAL 0
#define D3DMCS_COLOR1   1
#define D3DMCS_COLOR2   2

// Vertex blend
#define D3DVBF_DISABLE 0

typedef enum _D3DTEXTURESTAGESTATETYPE {
    D3DTSS_COLOROP = 1,
    D3DTSS_COLORARG1 = 2,
    D3DTSS_COLORARG2 = 3,
    D3DTSS_ALPHAOP = 4,
    D3DTSS_ALPHAARG1 = 5,
    D3DTSS_ALPHAARG2 = 6,
    D3DTSS_TEXCOORDINDEX = 11,
    D3DTSS_ADDRESSU = 13,
    D3DTSS_ADDRESSV = 14,
    D3DTSS_MINFILTER = 15, // D3D8 location
    D3DTSS_MAGFILTER = 16,
    D3DTSS_MIPFILTER = 17,
    D3DTSS_RESULTARG = 8,
    D3DTSS_TEXTURETRANSFORMFLAGS = 24,
} D3DTEXTURESTAGESTATETYPE;

// Blend modes, compare funcs, etc.
#define D3DBLEND_ZERO          1
#define D3DBLEND_ONE           2
#define D3DBLEND_SRCALPHA      5
#define D3DBLEND_INVSRCALPHA   6
#define D3DBLEND_DESTALPHA     7
#define D3DBLEND_INVDESTALPHA  8
#define D3DCMP_ALWAYS          8
#define D3DCMP_LESSEQUAL       4
#define D3DCMP_LESS            2
#define D3DCULL_NONE           1
#define D3DCULL_CW             2
#define D3DCULL_CCW            3
#define D3DCLEAR_TARGET        1
#define D3DCLEAR_ZBUFFER       2
#define D3DCLEAR_STENCIL       4
#define D3DTOP_DISABLE         1
#define D3DTOP_SELECTARG1      2
#define D3DTOP_MODULATE        4
#define D3DTA_TEXTURE          2
#define D3DTA_DIFFUSE          0
#define D3DTA_CURRENT          1
#define D3DTA_TFACTOR          3

// Texture address modes
#define D3DTADDRESS_WRAP   1
#define D3DTADDRESS_MIRROR 2
#define D3DTADDRESS_CLAMP  3

// Z-buffer types
#define D3DZB_FALSE 0
#define D3DZB_TRUE  1

// Shade modes
#define D3DSHADE_FLAT    1
#define D3DSHADE_GOURAUD 2

// Stencil operations
#define D3DSTENCILOP_KEEP    1
#define D3DSTENCILOP_ZERO    2
#define D3DSTENCILOP_REPLACE 3

// D3DCOLOR_ARGB
#define D3DCOLOR_ARGB(a, r, g, b) ((D3DCOLOR)(((DWORD)(a) << 24) | ((DWORD)(r) << 16) | ((DWORD)(g) << 8) | (DWORD)(b)))
#define D3DCOLOR_RGBA(r, g, b, a) D3DCOLOR_ARGB(a, r, g, b)
#define D3DCOLOR_XRGB(r, g, b)   D3DCOLOR_ARGB(0xFF, r, g, b)

// Fill modes
#define D3DFILL_POINT     1
#define D3DFILL_WIREFRAME 2
#define D3DFILL_SOLID     3

// Fog modes
#define D3DFOG_NONE   0
#define D3DFOG_EXP    1
#define D3DFOG_EXP2   2
#define D3DFOG_LINEAR 3

// Color write enable flags
#define D3DCOLORWRITEENABLE_RED   1
#define D3DCOLORWRITEENABLE_GREEN 2
#define D3DCOLORWRITEENABLE_BLUE  4
#define D3DCOLORWRITEENABLE_ALPHA 8

// D3D SDK / device creation
#define D3D_SDK_VERSION 120
typedef DWORD D3DDEVTYPE;
#define D3DDEVTYPE_HAL 1
#define D3DPRESENT_RATE_DEFAULT 0
#define D3DPRESENT_INTERVAL_IMMEDIATE 0x80000000
#define D3DSWAPEFFECT_DISCARD 1
#define D3DMULTISAMPLE_2_SAMPLES_SUPERSAMPLE_VERTICAL_LINEAR 0
#define D3DCREATE_HARDWARE_VERTEXPROCESSING 0x00000040
#define D3DADAPTER_DEFAULT 0

// D3DXCreateTextureFromFile alias
#define D3DXCreateTextureFromFile D3DXCreateTextureFromFileA

// Texture transform flags
#define D3DTTFF_DISABLE 0
#define D3DTTFF_COUNT2  2

// Additional texture stage state ops
#define D3DTOP_ADD         12
#define D3DTOP_SELECTARG2  3

// Color helpers
#define D3DCOLOR_COLORVALUE(r,g,b,a) D3DCOLOR_RGBA((DWORD)((r)*255.f),(DWORD)((g)*255.f),(DWORD)((b)*255.f),(DWORD)((a)*255.f))

// D3DXCOLOR
struct D3DXCOLOR {
    float r, g, b, a;
    D3DXCOLOR(){};
    D3DXCOLOR(float _r,float _g,float _b,float _a):r(_r),g(_g),b(_b),a(_a){}
    D3DXCOLOR(D3DCOLOR c) : r(((c>>16)&0xff)/255.f), g(((c>>8)&0xff)/255.f), b((c&0xff)/255.f), a(((c>>24)&0xff)/255.f) {}
};

// Texture filter types
#define D3DTEXF_NONE   0
#define D3DTEXF_POINT  1
#define D3DTEXF_LINEAR 2
#define D3DTEXF_ANISOTROPIC 3
#define D3DTEXF_QUINCUNX 4
#define D3DTEXF_GAUSSIANCUBIC 5

// Extra primitive type (Xbox-specific)
#define D3DPT_QUADLIST 8

// TEXT macro
#ifndef TEXT
#define TEXT(x) _T(x)
#endif

// _stscanf
#define _stscanf sscanf

// D3DX mesh stubs
#define D3DXMESH_MANAGED D3DPOOL_MANAGED
class IDirect3DDevice8; // forward decl for DrawSubset
class IDirect3DVertexBuffer8; // forward decl for GetVertexBuffer
class IDirect3DIndexBuffer8;  // forward decl for GetIndexBuffer
class ID3DXMesh {
public:
    ULONG refCount;
    BYTE* m_vbData;     // vertex buffer storage
    BYTE* m_ibData;     // index buffer storage (WORD indices)
    DWORD m_numFaces;
    DWORD m_numVerts;
    DWORD m_fvf;
    DWORD m_vertStride;
    IDirect3DDevice8* m_dev;

    ID3DXMesh() : refCount(1), m_vbData(NULL), m_ibData(NULL), m_numFaces(0), m_numVerts(0), m_fvf(0), m_vertStride(0), m_dev(NULL) {}
    ~ID3DXMesh() { free(m_vbData); free(m_ibData); }

    void Init(DWORD numFaces, DWORD numVerts, DWORD fvf, IDirect3DDevice8* dev) {
        m_numFaces = numFaces;
        m_numVerts = numVerts;
        m_fvf = fvf;
        m_dev = dev;
        // Calculate stride from FVF
        DWORD stride = 0;
        if (fvf & 0x004) stride += 16; // XYZRHW
        else if (fvf & 0x002) stride += 12; // XYZ
        if (fvf & 0x010) stride += 12; // NORMAL
        else if (fvf & 0x20000000) stride += 4; // NORMPACKED3
        if (fvf & 0x040) stride += 4; // DIFFUSE
        if (fvf & 0x080) stride += 4; // SPECULAR
        int texCount = (fvf >> 8) & 0xF;
        stride += texCount * 8; // each tex coord = 2 floats
        m_vertStride = stride;
        m_vbData = (BYTE*)calloc(numVerts, stride);
        m_ibData = (BYTE*)calloc(numFaces * 3, sizeof(WORD));
    }

    ULONG Release() { if (--refCount == 0) { delete this; return 0; } return refCount; }
    ULONG AddRef() { return ++refCount; }
    HRESULT LockVertexBuffer(DWORD flags, BYTE** ppData) {
        if (m_vbData) { *ppData = m_vbData; }
        else { static BYTE dummy[64]={}; *ppData = dummy; }
        return S_OK;
    }
    HRESULT UnlockVertexBuffer() { return S_OK; }
    HRESULT LockIndexBuffer(DWORD flags, BYTE** ppData) {
        if (m_ibData) { *ppData = m_ibData; }
        else { static BYTE dummy[64]={}; *ppData = dummy; }
        return S_OK;
    }
    HRESULT UnlockIndexBuffer() { return S_OK; }
    // Defined after IDirect3DVertexBuffer8/IDirect3DIndexBuffer8 classes
    HRESULT GetVertexBuffer(IDirect3DVertexBuffer8** ppVB);
    HRESULT GetIndexBuffer(IDirect3DIndexBuffer8** ppIB);
    DWORD GetNumFaces() { return m_numFaces; }
    DWORD GetNumVertices() { return m_numVerts; }
    HRESULT CloneMeshFVF(DWORD opts, DWORD fvf, void* dev, ID3DXMesh** ppOut) {
        ID3DXMesh* mesh = new ID3DXMesh();
        mesh->Init(m_numFaces, m_numVerts, fvf, (IDirect3DDevice8*)dev);
        // Convert vertex data between FVF layouts (different strides)
        if (m_vbData && mesh->m_vbData) {
            DWORD srcStride = m_vertStride;
            DWORD dstStride = mesh->m_vertStride;
            // Helper to get offset of each FVF component
            auto fvfLayout = [](DWORD f, DWORD* oPos, DWORD* oNorm, DWORD* oDiff, DWORD* oSpec, DWORD* oTex0) {
                DWORD off = 0;
                *oPos = *oNorm = *oDiff = *oSpec = *oTex0 = 0xFFFFFFFF;
                if (f & 0x004) { *oPos = off; off += 16; }       // XYZRHW
                else if (f & 0x002) { *oPos = off; off += 12; }  // XYZ
                if (f & 0x010) { *oNorm = off; off += 12; }      // NORMAL
                else if (f & 0x20000000) { *oNorm = off; off += 4; } // NORMPACKED3
                if (f & 0x040) { *oDiff = off; off += 4; }       // DIFFUSE
                if (f & 0x080) { *oSpec = off; off += 4; }       // SPECULAR
                int tc = (f >> 8) & 0xF;
                if (tc > 0) { *oTex0 = off; }
            };
            DWORD sPos, sNorm, sDiff, sSpec, sTex0;
            DWORD dPos, dNorm, dDiff, dSpec, dTex0;
            fvfLayout(m_fvf, &sPos, &sNorm, &sDiff, &sSpec, &sTex0);
            fvfLayout(fvf,   &dPos, &dNorm, &dDiff, &dSpec, &dTex0);
            DWORD posSize = (m_fvf & 0x004) ? 16 : 12;
            for (DWORD v = 0; v < m_numVerts; v++) {
                const BYTE* src = m_vbData + v * srcStride;
                BYTE* dst = mesh->m_vbData + v * dstStride;
                // Copy position
                if (sPos != 0xFFFFFFFF && dPos != 0xFFFFFFFF)
                    memcpy(dst + dPos, src + sPos, posSize);
                // Copy normal
                if (sNorm != 0xFFFFFFFF && dNorm != 0xFFFFFFFF) {
                    DWORD nSz = (m_fvf & 0x010) ? 12 : 4;
                    memcpy(dst + dNorm, src + sNorm, nSz);
                }
                // Copy or default diffuse
                if (dDiff != 0xFFFFFFFF) {
                    if (sDiff != 0xFFFFFFFF)
                        memcpy(dst + dDiff, src + sDiff, 4);
                    else
                        *(DWORD*)(dst + dDiff) = 0xFFFFFFFF; // opaque white default
                }
                // Copy or default specular
                if (dSpec != 0xFFFFFFFF) {
                    if (sSpec != 0xFFFFFFFF)
                        memcpy(dst + dSpec, src + sSpec, 4);
                    else
                        *(DWORD*)(dst + dSpec) = 0x00000000;
                }
                // Copy tex coords
                if (sTex0 != 0xFFFFFFFF && dTex0 != 0xFFFFFFFF) {
                    int stc = (m_fvf >> 8) & 0xF;
                    int dtc = (fvf >> 8) & 0xF;
                    int tc = stc < dtc ? stc : dtc;
                    memcpy(dst + dTex0, src + sTex0, tc * 8);
                }
            }
        }
        if (m_ibData) memcpy(mesh->m_ibData, m_ibData, m_numFaces * 3 * sizeof(WORD));
        *ppOut = mesh;
        return S_OK;
    }
    // DrawSubset renders the mesh through the D3D device
    // Implementation is below IDirect3DDevice8 (needs full device definition)
    HRESULT DrawSubset(DWORD attr);
};
typedef ID3DXMesh* LPD3DXMESH;
inline HRESULT D3DXCreateSphere(void* dev, float r, int slices, int stacks, LPD3DXMESH* m, void* adj) {
    (void)adj;
    // Generate actual sphere geometry (pos + normal + diffuse FVF)
    int numVerts = (slices + 1) * (stacks + 1);
    int numFaces = slices * stacks * 2;
    // Default FVF: XYZ + NORMAL (24 bytes per vertex)
    DWORD fvf = 0x002 | 0x010; // D3DFVF_XYZ | D3DFVF_NORMAL
    ID3DXMesh* mesh = new ID3DXMesh();
    mesh->Init(numFaces, numVerts, fvf, (IDirect3DDevice8*)dev);
    // Fill vertex data: position(float3) + normal(float3)
    float* vb = (float*)mesh->m_vbData;
    for (int j = 0; j <= stacks; j++) {
        float phi = 3.14159265f * (float)j / (float)stacks;
        float sp = sinf(phi), cp = cosf(phi);
        for (int i = 0; i <= slices; i++) {
            float theta = 2.0f * 3.14159265f * (float)i / (float)slices;
            float st2 = sinf(theta), ct = cosf(theta);
            float nx = sp * ct, ny = cp, nz = sp * st2;
            *vb++ = nx * r; *vb++ = ny * r; *vb++ = nz * r; // position
            *vb++ = nx;     *vb++ = ny;     *vb++ = nz;     // normal
        }
    }
    // Fill index data
    WORD* ib = (WORD*)mesh->m_ibData;
    for (int j = 0; j < stacks; j++) {
        for (int i = 0; i < slices; i++) {
            WORD v0 = (WORD)(j * (slices + 1) + i);
            WORD v1 = v0 + 1;
            WORD v2 = (WORD)((j + 1) * (slices + 1) + i);
            WORD v3 = v2 + 1;
            *ib++ = v0; *ib++ = v2; *ib++ = v1;
            *ib++ = v1; *ib++ = v2; *ib++ = v3;
        }
    }
    *m = mesh;
    return S_OK;
}
inline HRESULT D3DXComputeNormals(LPD3DXMESH m, void* adj=NULL) { (void)m;(void)adj; return S_OK; }
// D3DXComputeBoundingBox is defined later, after D3DXVECTOR3

// FVF flags
#define D3DFVF_XYZ           0x002
#define D3DFVF_XYZRHW        0x004
#define D3DFVF_NORMAL        0x010
#define D3DFVF_NORMPACKED3   0x20000000  // Xbox-specific compressed normals
#define D3DFVF_DIFFUSE       0x040
#define D3DFVF_SPECULAR      0x080
#define D3DFVF_TEX0          0x000
#define D3DFVF_TEX1          0x100
#define D3DFVF_TEX2          0x200
#define D3DFVF_TEX3          0x300
#define D3DFVF_TEX4          0x400

// Vertex shader declaration tokens (Xbox D3D8)
#define D3DVSD_STREAM(n) ((DWORD)(1 << 27) | ((n) << 0))
#define D3DVSD_REG(reg, type) ((DWORD)((type) << 16) | (reg))
#define D3DVSD_END() 0xFFFFFFFF
#define D3DVSDT_FLOAT2      0x01
#define D3DVSDT_FLOAT3      0x02
#define D3DVSDT_FLOAT4      0x03
#define D3DVSDT_NORMPACKED3 0x16
#define D3DVSDT_D3DCOLOR    0x04
#define D3DVSDE_POSITION    0
#define D3DVSDE_NORMAL      3
#define D3DVSDE_DIFFUSE     5
#define D3DVSDE_TEXCOORD0   8

// Lock flags
#define D3DLOCK_DISCARD       0x2000
#define D3DLOCK_READONLY      0x10
#define D3DLOCK_NOOVERWRITE   0x1000
#define D3DUSAGE_WRITEONLY    8
#define D3DUSAGE_DYNAMIC      0x200

// -------------------------------------------------------
// D3D8 math types
// -------------------------------------------------------
struct D3DVECTOR { float x, y, z; };
struct D3DMATRIX {
    union {
        float m[4][4];
        struct { float _11,_12,_13,_14, _21,_22,_23,_24, _31,_32,_33,_34, _41,_42,_43,_44; };
    };
};

struct D3DXVECTOR2 {
    float x, y;
    D3DXVECTOR2() : x(0), y(0) {}
    D3DXVECTOR2(float _x, float _y) : x(_x), y(_y) {}
};

struct D3DXVECTOR3 : public D3DVECTOR {
    D3DXVECTOR3() { x = y = z = 0; }
    D3DXVECTOR3(float _x, float _y, float _z) { x = _x; y = _y; z = _z; }
    D3DXVECTOR3(const D3DVECTOR& v) { x = v.x; y = v.y; z = v.z; }
    D3DXVECTOR3 operator+(const D3DXVECTOR3& v) const { return D3DXVECTOR3(x+v.x, y+v.y, z+v.z); }
    D3DXVECTOR3 operator-(const D3DXVECTOR3& v) const { return D3DXVECTOR3(x-v.x, y-v.y, z-v.z); }
    D3DXVECTOR3 operator*(float s) const { return D3DXVECTOR3(x*s, y*s, z*s); }
    D3DXVECTOR3& operator+=(const D3DXVECTOR3& v) { x+=v.x; y+=v.y; z+=v.z; return *this; }
    D3DXVECTOR3& operator-=(const D3DXVECTOR3& v) { x-=v.x; y-=v.y; z-=v.z; return *this; }
    D3DXVECTOR3& operator*=(float s) { x*=s; y*=s; z*=s; return *this; }
    D3DXVECTOR3 operator-() const { return D3DXVECTOR3(-x,-y,-z); }
};

inline DWORD ComputeFVFStride(DWORD fvf) {
    DWORD stride = 0;
    if (fvf & D3DFVF_XYZ)    stride += 12;
    if (fvf & D3DFVF_XYZRHW) stride += 16;
    if (fvf & D3DFVF_NORMAL) stride += 12;
    if (fvf & D3DFVF_NORMPACKED3) stride += 4;
    if (fvf & D3DFVF_DIFFUSE)  stride += 4;
    if (fvf & D3DFVF_SPECULAR) stride += 4;
    DWORD texCount = (fvf >> 8) & 0xF;
    stride += texCount * 8;
    return stride;
}

inline HRESULT D3DXComputeBoundingBox(void* v, DWORD n, DWORD fvf, D3DXVECTOR3* mn, D3DXVECTOR3* mx) {
    if (!v || n == 0) {
        if (mn) { mn->x = mn->y = mn->z = 0; }
        if (mx) { mx->x = mx->y = mx->z = 0; }
        return S_OK;
    }
    DWORD stride = ComputeFVFStride(fvf);
    if (stride == 0) stride = sizeof(float) * 3;
    BYTE* p = (BYTE*)v;
    float minX = ((float*)p)[0], minY = ((float*)p)[1], minZ = ((float*)p)[2];
    float maxX = minX, maxY = minY, maxZ = minZ;
    for (DWORD i = 1; i < n; i++) {
        p += stride;
        float px = ((float*)p)[0], py = ((float*)p)[1], pz = ((float*)p)[2];
        if (px < minX) minX = px; if (px > maxX) maxX = px;
        if (py < minY) minY = py; if (py > maxY) maxY = py;
        if (pz < minZ) minZ = pz; if (pz > maxZ) maxZ = pz;
    }
    if (mn) { mn->x = minX; mn->y = minY; mn->z = minZ; }
    if (mx) { mx->x = maxX; mx->y = maxY; mx->z = maxZ; }
    return S_OK;
}

struct D3DXVECTOR4 {
    float x, y, z, w;
    D3DXVECTOR4() : x(0), y(0), z(0), w(0) {}
    D3DXVECTOR4(float _x, float _y, float _z, float _w) : x(_x), y(_y), z(_z), w(_w) {}
};

struct D3DXQUATERNION {
    float x, y, z, w;
    D3DXQUATERNION() : x(0), y(0), z(0), w(1.0f) {}
    D3DXQUATERNION(float _x, float _y, float _z, float _w) : x(_x), y(_y), z(_z), w(_w) {}
};

struct D3DXMATRIX : public D3DMATRIX {
    D3DXMATRIX() { memset(this, 0, sizeof(*this)); }
    D3DXMATRIX(const D3DMATRIX& o) { memcpy(this, &o, sizeof(D3DMATRIX)); }
    D3DXMATRIX(float _11, float _12, float _13, float _14,
               float _21, float _22, float _23, float _24,
               float _31, float _32, float _33, float _34,
               float _41, float _42, float _43, float _44) {
        m[0][0]=_11; m[0][1]=_12; m[0][2]=_13; m[0][3]=_14;
        m[1][0]=_21; m[1][1]=_22; m[1][2]=_23; m[1][3]=_24;
        m[2][0]=_31; m[2][1]=_32; m[2][2]=_33; m[2][3]=_34;
        m[3][0]=_41; m[3][1]=_42; m[3][2]=_43; m[3][3]=_44;
    }
    float& operator()(int r, int c) { return m[r][c]; }
    float operator()(int r, int c) const { return m[r][c]; }
    D3DXMATRIX operator*(const D3DXMATRIX& o) const {
        D3DXMATRIX r;
        for(int i=0;i<4;i++) for(int j=0;j<4;j++) {
            r.m[i][j]=0; for(int k=0;k<4;k++) r.m[i][j]+=m[i][k]*o.m[k][j];
        }
        return r;
    }
};
typedef D3DXMATRIX* LPD3DXMATRIX;

// D3DX math functions (inline)
inline D3DXMATRIX* D3DXMatrixIdentity(D3DXMATRIX* p) { memset(p,0,sizeof(*p)); p->m[0][0]=p->m[1][1]=p->m[2][2]=p->m[3][3]=1; return p; }
inline D3DXMATRIX* D3DXMatrixTranslation(D3DXMATRIX* p, float x, float y, float z) { D3DXMatrixIdentity(p); p->m[3][0]=x; p->m[3][1]=y; p->m[3][2]=z; return p; }
inline D3DXMATRIX* D3DXMatrixScaling(D3DXMATRIX* p, float sx, float sy, float sz) { memset(p,0,sizeof(*p)); p->m[0][0]=sx; p->m[1][1]=sy; p->m[2][2]=sz; p->m[3][3]=1; return p; }
inline D3DXMATRIX* D3DXMatrixMultiply(D3DXMATRIX* o, const D3DXMATRIX* a, const D3DXMATRIX* b) { D3DXMATRIX t; for(int i=0;i<4;i++) for(int j=0;j<4;j++){t.m[i][j]=0;for(int k=0;k<4;k++)t.m[i][j]+=a->m[i][k]*b->m[k][j];} *o=t; return o; }
inline D3DXMATRIX* D3DXMatrixRotationAxis(D3DXMATRIX* p, const D3DXVECTOR3* v, float a) {
    float c=cosf(a),s=sinf(a),t=1-c,x=v->x,y=v->y,z=v->z;
    float l=sqrtf(x*x+y*y+z*z); if(l>0){x/=l;y/=l;z/=l;}
    D3DXMatrixIdentity(p);
    p->m[0][0]=t*x*x+c;   p->m[0][1]=t*x*y+s*z; p->m[0][2]=t*x*z-s*y;
    p->m[1][0]=t*x*y-s*z; p->m[1][1]=t*y*y+c;   p->m[1][2]=t*y*z+s*x;
    p->m[2][0]=t*x*z+s*y; p->m[2][1]=t*y*z-s*x; p->m[2][2]=t*z*z+c;
    return p;
}
inline D3DXMATRIX* D3DXMatrixLookAtLH(D3DXMATRIX* p, const D3DXVECTOR3* eye, const D3DXVECTOR3* at, const D3DXVECTOR3* up) {
    D3DXVECTOR3 z = *at - *eye; float l=sqrtf(z.x*z.x+z.y*z.y+z.z*z.z); if(l>0){z.x/=l;z.y/=l;z.z/=l;}
    D3DXVECTOR3 x; x.x=up->y*z.z-up->z*z.y; x.y=up->z*z.x-up->x*z.z; x.z=up->x*z.y-up->y*z.x;
    l=sqrtf(x.x*x.x+x.y*x.y+x.z*x.z); if(l>0){x.x/=l;x.y/=l;x.z/=l;}
    D3DXVECTOR3 y; y.x=z.y*x.z-z.z*x.y; y.y=z.z*x.x-z.x*x.z; y.z=z.x*x.y-z.y*x.x;
    D3DXMatrixIdentity(p);
    p->m[0][0]=x.x; p->m[0][1]=y.x; p->m[0][2]=z.x;
    p->m[1][0]=x.y; p->m[1][1]=y.y; p->m[1][2]=z.y;
    p->m[2][0]=x.z; p->m[2][1]=y.z; p->m[2][2]=z.z;
    p->m[3][0]=-(x.x*eye->x+x.y*eye->y+x.z*eye->z);
    p->m[3][1]=-(y.x*eye->x+y.y*eye->y+y.z*eye->z);
    p->m[3][2]=-(z.x*eye->x+z.y*eye->y+z.z*eye->z);
    return p;
}
inline D3DXMATRIX* D3DXMatrixPerspectiveFovLH(D3DXMATRIX* p, float fovy, float aspect, float zn, float zf) {
    float ys=1.0f/tanf(fovy/2); memset(p,0,sizeof(*p));
    p->m[0][0]=ys/aspect; p->m[1][1]=ys; p->m[2][2]=zf/(zf-zn); p->m[2][3]=1; p->m[3][2]=-zn*zf/(zf-zn); return p;
}
inline D3DXMATRIX* D3DXMatrixOrthoLH(D3DXMATRIX* p, float w, float h, float zn, float zf) {
    memset(p,0,sizeof(*p)); p->m[0][0]=2/w; p->m[1][1]=2/h; p->m[2][2]=1/(zf-zn); p->m[3][2]=-zn/(zf-zn); p->m[3][3]=1; return p;
}
inline void D3DXMatrixRotationQuaternion(D3DXMATRIX* p, const D3DXQUATERNION* q) {
    float xx=q->x*q->x,yy=q->y*q->y,zz=q->z*q->z,xy=q->x*q->y,xz=q->x*q->z,yz=q->y*q->z,wx=q->w*q->x,wy=q->w*q->y,wz=q->w*q->z;
    D3DXMatrixIdentity(p);
    p->m[0][0]=1-2*(yy+zz); p->m[0][1]=2*(xy+wz); p->m[0][2]=2*(xz-wy);
    p->m[1][0]=2*(xy-wz); p->m[1][1]=1-2*(xx+zz); p->m[1][2]=2*(yz+wx);
    p->m[2][0]=2*(xz+wy); p->m[2][1]=2*(yz-wx); p->m[2][2]=1-2*(xx+yy);
}
inline D3DXVECTOR3* D3DXVec3Normalize(D3DXVECTOR3* o, const D3DXVECTOR3* v) { float l=sqrtf(v->x*v->x+v->y*v->y+v->z*v->z); if(l>0){o->x=v->x/l;o->y=v->y/l;o->z=v->z/l;}else{o->x=o->y=o->z=0;} return o; }
inline D3DXVECTOR3* D3DXVec3Cross(D3DXVECTOR3* o, const D3DXVECTOR3* a, const D3DXVECTOR3* b) { D3DXVECTOR3 t; t.x=a->y*b->z-a->z*b->y; t.y=a->z*b->x-a->x*b->z; t.z=a->x*b->y-a->y*b->x; *o=t; return o; }
inline float D3DXVec3Dot(const D3DXVECTOR3* a, const D3DXVECTOR3* b) { return a->x*b->x+a->y*b->y+a->z*b->z; }
inline float D3DXVec3Length(const D3DXVECTOR3* v) { return sqrtf(v->x*v->x+v->y*v->y+v->z*v->z); }
inline D3DXVECTOR3* D3DXVec3Lerp(D3DXVECTOR3* o, const D3DXVECTOR3* a, const D3DXVECTOR3* b, float s) { o->x=a->x+s*(b->x-a->x); o->y=a->y+s*(b->y-a->y); o->z=a->z+s*(b->z-a->z); return o; }
inline void D3DXVec3Scale(D3DXVECTOR3* out, const D3DXVECTOR3* v, float s) { out->x=v->x*s; out->y=v->y*s; out->z=v->z*s; }
inline D3DXMATRIX* D3DXMatrixTranspose(D3DXMATRIX* o, const D3DXMATRIX* m) { D3DXMATRIX t; for(int i=0;i<4;i++) for(int j=0;j<4;j++) t.m[i][j]=m->m[j][i]; *o=t; return o; }
inline D3DXVECTOR4* D3DXVec4Normalize(D3DXVECTOR4* o, const D3DXVECTOR4* v) { float l=sqrtf(v->x*v->x+v->y*v->y+v->z*v->z+v->w*v->w); if(l>0){o->x=v->x/l;o->y=v->y/l;o->z=v->z/l;o->w=v->w/l;}else{o->x=o->y=o->z=o->w=0;} return o; }
inline void D3DXVec3TransformNormal(D3DXVECTOR3* out, const D3DXVECTOR3* v, const D3DXMATRIX* m) {
    float x=v->x, y=v->y, z=v->z;
    out->x = x*m->m[0][0] + y*m->m[1][0] + z*m->m[2][0];
    out->y = x*m->m[0][1] + y*m->m[1][1] + z*m->m[2][1];
    out->z = x*m->m[0][2] + y*m->m[1][2] + z*m->m[2][2];
}
inline D3DXMATRIX* D3DXMatrixInverse(D3DXMATRIX* out, float* pDet, const D3DXMATRIX* m) {
    // Gauss-Jordan 4x4 matrix inversion
    float tmp[4][8];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) { tmp[i][j] = m->m[i][j]; tmp[i][j+4] = (i==j) ? 1.0f : 0.0f; }
    }
    for (int i = 0; i < 4; i++) {
        int pivot = i;
        for (int j = i+1; j < 4; j++) if (fabsf(tmp[j][i]) > fabsf(tmp[pivot][i])) pivot = j;
        if (pivot != i) for (int k = 0; k < 8; k++) { float t = tmp[i][k]; tmp[i][k] = tmp[pivot][k]; tmp[pivot][k] = t; }
        float d = tmp[i][i];
        if (fabsf(d) < 1e-10f) { D3DXMatrixIdentity(out); return out; }
        for (int k = 0; k < 8; k++) tmp[i][k] /= d;
        for (int j = 0; j < 4; j++) {
            if (j == i) continue;
            float f = tmp[j][i];
            for (int k = 0; k < 8; k++) tmp[j][k] -= f * tmp[i][k];
        }
    }
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) out->m[i][j] = tmp[i][j+4];
    if (pDet) *pDet = 1.0f; // determinant not computed but non-zero if we got here
    return out;
}

inline D3DXMATRIX* D3DXMatrixAffineTransformation(D3DXMATRIX* out, float s, const D3DXVECTOR3* c, const D3DXQUATERNION* r, const D3DXVECTOR3* t) {
    D3DXMATRIX matR;
    if (r) D3DXMatrixRotationQuaternion(&matR, r);
    else D3DXMatrixIdentity(&matR);
    D3DXMATRIX matS;
    D3DXMatrixScaling(&matS, s, s, s);
    D3DXMATRIX result;
    D3DXMatrixMultiply(&result, &matS, &matR);
    if (c) {
        result.m[3][0] += -c->x * result.m[0][0] - c->y * result.m[1][0] - c->z * result.m[2][0] + c->x;
        result.m[3][1] += -c->x * result.m[0][1] - c->y * result.m[1][1] - c->z * result.m[2][1] + c->y;
        result.m[3][2] += -c->x * result.m[0][2] - c->y * result.m[1][2] - c->z * result.m[2][2] + c->z;
    }
    if (t) { result.m[3][0] += t->x; result.m[3][1] += t->y; result.m[3][2] += t->z; }
    *out = result;
    return out;
}

inline D3DXQUATERNION* D3DXQuaternionIdentity(D3DXQUATERNION* o) { o->x=o->y=o->z=0; o->w=1; return o; }
inline D3DXQUATERNION* D3DXQuaternionRotationAxis(D3DXQUATERNION* o, const D3DXVECTOR3* v, float a) {
    float l=sqrtf(v->x*v->x+v->y*v->y+v->z*v->z),s=sinf(a/2);
    if(l>0){o->x=v->x/l*s;o->y=v->y/l*s;o->z=v->z/l*s;}else{o->x=o->y=o->z=0;} o->w=cosf(a/2); return o;
}
inline D3DXQUATERNION* D3DXQuaternionSlerp(D3DXQUATERNION* o, const D3DXQUATERNION* a, const D3DXQUATERNION* b, float t) {
    float d=a->x*b->x+a->y*b->y+a->z*b->z+a->w*b->w; D3DXQUATERNION q=*b;
    if(d<0){d=-d;q.x=-q.x;q.y=-q.y;q.z=-q.z;q.w=-q.w;}
    if(d>0.9995f){o->x=a->x+t*(q.x-a->x);o->y=a->y+t*(q.y-a->y);o->z=a->z+t*(q.z-a->z);o->w=a->w+t*(q.w-a->w);}
    else{float th=acosf(d),s1=sinf((1-t)*th)/sinf(th),s2=sinf(t*th)/sinf(th);o->x=s1*a->x+s2*q.x;o->y=s1*a->y+s2*q.y;o->z=s1*a->z+s2*q.z;o->w=s1*a->w+s2*q.w;}
    return o;
}
inline D3DXQUATERNION* D3DXQuaternionMultiply(D3DXQUATERNION* o, const D3DXQUATERNION* a, const D3DXQUATERNION* b) {
    D3DXQUATERNION t;
    t.w=a->w*b->w-a->x*b->x-a->y*b->y-a->z*b->z;
    t.x=a->w*b->x+a->x*b->w+a->y*b->z-a->z*b->y;
    t.y=a->w*b->y-a->x*b->z+a->y*b->w+a->z*b->x;
    t.z=a->w*b->z+a->x*b->y-a->y*b->x+a->z*b->w;
    *o=t; return o;
}

inline D3DXQUATERNION* D3DXQuaternionRotationYawPitchRoll(D3DXQUATERNION* o, float yaw, float pitch, float roll) {
    float cy=cosf(yaw/2),sy=sinf(yaw/2),cp=cosf(pitch/2),sp=sinf(pitch/2),cr=cosf(roll/2),sr=sinf(roll/2);
    o->w=cy*cp*cr+sy*sp*sr; o->x=cy*sp*cr+sy*cp*sr; o->y=sy*cp*cr-cy*sp*sr; o->z=cy*cp*sr-sy*sp*cr; return o;
}
inline D3DXQUATERNION* D3DXQuaternionRotationMatrix(D3DXQUATERNION* o, const D3DXMATRIX* m) {
    float tr=m->m[0][0]+m->m[1][1]+m->m[2][2];
    if(tr>0){float s=0.5f/sqrtf(tr+1);o->w=0.25f/s;o->x=(m->m[2][1]-m->m[1][2])*s;o->y=(m->m[0][2]-m->m[2][0])*s;o->z=(m->m[1][0]-m->m[0][1])*s;}
    else if(m->m[0][0]>m->m[1][1]&&m->m[0][0]>m->m[2][2]){float s=2*sqrtf(1+m->m[0][0]-m->m[1][1]-m->m[2][2]);o->w=(m->m[2][1]-m->m[1][2])/s;o->x=0.25f*s;o->y=(m->m[0][1]+m->m[1][0])/s;o->z=(m->m[0][2]+m->m[2][0])/s;}
    else if(m->m[1][1]>m->m[2][2]){float s=2*sqrtf(1+m->m[1][1]-m->m[0][0]-m->m[2][2]);o->w=(m->m[0][2]-m->m[2][0])/s;o->x=(m->m[0][1]+m->m[1][0])/s;o->y=0.25f*s;o->z=(m->m[1][2]+m->m[2][1])/s;}
    else{float s=2*sqrtf(1+m->m[2][2]-m->m[0][0]-m->m[1][1]);o->w=(m->m[1][0]-m->m[0][1])/s;o->x=(m->m[0][2]+m->m[2][0])/s;o->y=(m->m[1][2]+m->m[2][1])/s;o->z=0.25f*s;}
    return o;
}
inline D3DXVECTOR3* D3DXVec3CatmullRom(D3DXVECTOR3* o, const D3DXVECTOR3* p0, const D3DXVECTOR3* p1, const D3DXVECTOR3* p2, const D3DXVECTOR3* p3, float s) {
    float s2=s*s, s3=s2*s;
    o->x=0.5f*(2*p1->x+(-p0->x+p2->x)*s+(2*p0->x-5*p1->x+4*p2->x-p3->x)*s2+(-p0->x+3*p1->x-3*p2->x+p3->x)*s3);
    o->y=0.5f*(2*p1->y+(-p0->y+p2->y)*s+(2*p0->y-5*p1->y+4*p2->y-p3->y)*s2+(-p0->y+3*p1->y-3*p2->y+p3->y)*s3);
    o->z=0.5f*(2*p1->z+(-p0->z+p2->z)*s+(2*p0->z-5*p1->z+4*p2->z-p3->z)*s2+(-p0->z+3*p1->z-3*p2->z+p3->z)*s3);
    return o;
}

#ifndef D3DX_PI
#define D3DX_PI 3.141592654f
#endif

// -------------------------------------------------------
// D3D8 structures
// -------------------------------------------------------
typedef struct { float r, g, b, a; } D3DCOLORVALUE;
typedef struct { LONG x1, y1, x2, y2; } D3DRECT;
typedef struct { D3DCOLORVALUE Diffuse, Specular, Ambient, Emissive; float Power; } D3DMATERIAL8;
typedef struct { DWORD Type; D3DCOLORVALUE Diffuse, Specular, Ambient; D3DVECTOR Direction, Position; float Range, Falloff, Attenuation0, Attenuation1, Attenuation2, Theta, Phi; } D3DLIGHT8;
#define D3DLIGHT_DIRECTIONAL 3
#define D3DLIGHT_SPOT 2
#define D3DLIGHT_POINT 1
typedef struct { UINT Width, Height; D3DFORMAT Format; } D3DSURFACE_DESC;
typedef struct { int Pitch; void* pBits; } D3DLOCKED_RECT;

typedef struct {
    UINT BackBufferWidth, BackBufferHeight;
    D3DFORMAT BackBufferFormat;
    UINT BackBufferCount;
    DWORD MultiSampleType;
    DWORD SwapEffect;
    void* hDeviceWindow;
    BOOL Windowed;
    BOOL EnableAutoDepthStencil;
    D3DFORMAT AutoDepthStencilFormat;
    DWORD Flags;
    UINT FullScreen_RefreshRateInHz;
    UINT FullScreen_PresentationInterval;
} D3DPRESENT_PARAMETERS;
// -------------------------------------------------------
// OpenGL shader program and state (initialized in sdl_main.cpp)
// -------------------------------------------------------
extern SDL_Window* g_pSDLWindow;
extern SDL_GLContext g_pGLContext;

// GL shader program and uniform locations
struct GLState {
    GLuint program;
    GLuint vao;
    GLuint dynamicVBO;  // for DrawPrimitiveUP / DrawIndexedPrimitiveUP
    GLuint dynamicIBO;
    GLuint whiteTex;    // 1x1 white texture (used when no texture bound)
    // Uniform locations
    GLint u_WVP, u_WorldView;
    GLint u_FalloffFront, u_FalloffDelta;
    GLint u_NormalInv;      // WV^-1 matrix for normal transform (from c5-c8)
    GLint u_TFactor, u_MatDiffuse;
    GLint u_VertexMode;     // 0=3D, 1=RHW (pre-transformed)
    GLint u_ColorSource;    // 0=falloff, 1=diffuse_attr, 2=tfactor, 3=white, 4=mat_diffuse
    GLint u_AlphaSource;    // 0=from_color, 1=tfactor, 2=diffuse_attr, 3=opaque
    GLint u_AlphaMul;       // stage 1 multiplier
    GLint u_VertexAlphaMul; // 1 = also multiply final alpha by vertex diffuse alpha (text.cpp soft-fade clip)
    GLint u_NormalType;     // 0=none, 1=float3, 2=packed(uint)
    GLint u_Tex0, u_Tex1;
    GLint u_FragColorOp;    // 0=v_Color, 1=texture, 2=v_Color×texture
    GLint u_FragAlphaOp;    // 0=v_Color.a, 1=tex.a, 2=v_Color.a×tex.a
    GLint u_HasTex1;        // stage 1 texture bound
    GLint u_Tex1AlphaOp;    // 0=none, 1=multiply alpha by tex1.a
    GLint u_Tex1ColorOp;    // 0=none, 1=add tex1.rgb to color
    GLint u_EnvMapMode;     // 0=off, 1=spherical env map (reflection material)
    GLint u_ViewportSize;   // vec2(backbuffer width, backbuffer height) for XYZRHW
    GLint u_AlphaRef;       // alpha test threshold (0=disabled)
    // Attribute locations
    GLint a_Position, a_Normal, a_PackedNrm, a_Diffuse, a_TexCoord;
};
extern GLState g_gl;

// ---------------------------------------------------------------------------
// CRT Post-Process Effect
// ---------------------------------------------------------------------------
struct CRTState {
    bool     enabled;          // master toggle (F4)
    bool     settingsOpen;     // F4 settings panel visible
    // Effect parameters
    float    scanlineIntensity; // 0=off, 1=full black lines
    float    curvature;         // barrel distortion amount
    float    phosphorMask;      // RGB sub-pixel intensity
    float    vignette;          // corner darkening
    float    bloom;             // glow/bleed amount
    float    flickerAmount;     // subtle brightness variation
    float    colorBleed;        // horizontal color smear
    float    brightness;        // overall brightness adjustment
    // GL resources
    GLuint   fbo;
    GLuint   colorTex;
    GLuint   program;          // CRT shader program
    GLuint   quadVAO, quadVBO;
    GLint    u_SceneTex, u_Resolution, u_Time;
    GLint    u_ScanlineIntensity, u_Curvature, u_PhosphorMask;
    GLint    u_Vignette, u_Bloom, u_Flicker, u_ColorBleed, u_Brightness;
    int      texW, texH;       // current FBO dimensions
};
extern CRTState g_crt;

// Initialize CRT post-process FBO and shader
inline bool InitCRTShader(int width, int height) {
    // ---- CRT Vertex Shader (fullscreen triangle) ----
    const char* crtVS = R"(
#version 150
out vec2 v_TexCoord;
void main() {
    // Fullscreen triangle trick: 3 vertices cover the screen
    float x = float((gl_VertexID & 1) << 2) - 1.0;
    float y = float((gl_VertexID & 2) << 1) - 1.0;
    v_TexCoord = vec2((x + 1.0) * 0.5, (y + 1.0) * 0.5);
    gl_Position = vec4(x, y, 0.0, 1.0);
}
)";

    // ---- CRT Fragment Shader ----
    const char* crtFS = R"(
#version 150

uniform sampler2D u_SceneTex;
uniform vec2  u_Resolution;
uniform float u_Time;
uniform float u_ScanlineIntensity;
uniform float u_Curvature;
uniform float u_PhosphorMask;
uniform float u_Vignette;
uniform float u_Bloom;
uniform float u_Flicker;
uniform float u_ColorBleed;
uniform float u_Brightness;

in vec2 v_TexCoord;
out vec4 fragColor;

// Barrel distortion
vec2 distort(vec2 uv, float k) {
    vec2 cc = uv - 0.5;
    float r2 = dot(cc, cc);
    return uv + cc * r2 * k;
}

void main() {
    vec2 uv = v_TexCoord;

    // Barrel distortion (CRT curvature)
    if (u_Curvature > 0.0) {
        uv = distort(uv, u_Curvature * 0.3);
        // Black outside the curved screen area
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
            fragColor = vec4(0.0, 0.0, 0.0, 1.0);
            return;
        }
    }

    // Color bleed: shift R and B channels horizontally
    vec3 color;
    if (u_ColorBleed > 0.0) {
        float offset = u_ColorBleed / u_Resolution.x;
        color.r = texture(u_SceneTex, vec2(uv.x + offset, uv.y)).r;
        color.g = texture(u_SceneTex, uv).g;
        color.b = texture(u_SceneTex, vec2(uv.x - offset, uv.y)).b;
    } else {
        color = texture(u_SceneTex, uv).rgb;
    }

    // Simple bloom: blend with blurred sample
    if (u_Bloom > 0.0) {
        vec3 bloomColor = vec3(0.0);
        float ps = 1.5 / u_Resolution.x;
        float pt = 1.5 / u_Resolution.y;
        bloomColor += texture(u_SceneTex, uv + vec2(-ps, -pt)).rgb;
        bloomColor += texture(u_SceneTex, uv + vec2( ps, -pt)).rgb;
        bloomColor += texture(u_SceneTex, uv + vec2(-ps,  pt)).rgb;
        bloomColor += texture(u_SceneTex, uv + vec2( ps,  pt)).rgb;
        bloomColor *= 0.25;
        color = mix(color, max(color, bloomColor), u_Bloom);
    }

    // Scanlines
    if (u_ScanlineIntensity > 0.0) {
        float scanline = sin(uv.y * u_Resolution.y * 3.14159) * 0.5 + 0.5;
        scanline = pow(scanline, 1.5);
        color *= mix(1.0, scanline, u_ScanlineIntensity * 0.5);
    }

    // Phosphor mask (RGB sub-pixel simulation)
    if (u_PhosphorMask > 0.0) {
        int px = int(gl_FragCoord.x) % 3;
        vec3 mask = vec3(1.0);
        if      (px == 0) mask = vec3(1.0, 1.0 - u_PhosphorMask * 0.5, 1.0 - u_PhosphorMask * 0.5);
        else if (px == 1) mask = vec3(1.0 - u_PhosphorMask * 0.5, 1.0, 1.0 - u_PhosphorMask * 0.5);
        else              mask = vec3(1.0 - u_PhosphorMask * 0.5, 1.0 - u_PhosphorMask * 0.5, 1.0);
        color *= mask;
    }

    // Flicker
    if (u_Flicker > 0.0) {
        float flick = 1.0 - u_Flicker * 0.03 * sin(u_Time * 15.0);
        color *= flick;
    }

    // Vignette (darker corners)
    if (u_Vignette > 0.0) {
        vec2 vig = uv * (1.0 - uv);
        float v = pow(vig.x * vig.y * 16.0, u_Vignette * 0.3);
        color *= v;
    }

    // Brightness
    color *= u_Brightness;

    fragColor = vec4(color, 1.0);
}
)";

    // Compile CRT vertex shader
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &crtVS, NULL);
    glCompileShader(vs);
    GLint ok = 0; glGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024]; glGetShaderInfoLog(vs, sizeof(log), NULL, log);
        fprintf(stderr, "[CRT] Vertex shader error: %s\n", log);
        return false;
    }

    // Compile CRT fragment shader
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &crtFS, NULL);
    glCompileShader(fs);
    glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024]; glGetShaderInfoLog(fs, sizeof(log), NULL, log);
        fprintf(stderr, "[CRT] Fragment shader error: %s\n", log);
        return false;
    }

    // Link CRT program
    g_crt.program = glCreateProgram();
    glAttachShader(g_crt.program, vs);
    glAttachShader(g_crt.program, fs);
    glLinkProgram(g_crt.program);
    glGetProgramiv(g_crt.program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024]; glGetProgramInfoLog(g_crt.program, sizeof(log), NULL, log);
        fprintf(stderr, "[CRT] Link error: %s\n", log);
        return false;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);

    // Get uniform locations
    g_crt.u_SceneTex          = glGetUniformLocation(g_crt.program, "u_SceneTex");
    g_crt.u_Resolution        = glGetUniformLocation(g_crt.program, "u_Resolution");
    g_crt.u_Time              = glGetUniformLocation(g_crt.program, "u_Time");
    g_crt.u_ScanlineIntensity = glGetUniformLocation(g_crt.program, "u_ScanlineIntensity");
    g_crt.u_Curvature         = glGetUniformLocation(g_crt.program, "u_Curvature");
    g_crt.u_PhosphorMask      = glGetUniformLocation(g_crt.program, "u_PhosphorMask");
    g_crt.u_Vignette          = glGetUniformLocation(g_crt.program, "u_Vignette");
    g_crt.u_Bloom             = glGetUniformLocation(g_crt.program, "u_Bloom");
    g_crt.u_Flicker           = glGetUniformLocation(g_crt.program, "u_Flicker");
    g_crt.u_ColorBleed        = glGetUniformLocation(g_crt.program, "u_ColorBleed");
    g_crt.u_Brightness        = glGetUniformLocation(g_crt.program, "u_Brightness");

    // Create empty VAO for fullscreen triangle (no vertex data needed)
    glGenVertexArrays(1, &g_crt.quadVAO);

    // Create FBO
    glGenFramebuffers(1, &g_crt.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, g_crt.fbo);

    // Create color texture
    glGenTextures(1, &g_crt.colorTex);
    glBindTexture(GL_TEXTURE_2D, g_crt.colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_crt.colorTex, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "[CRT] FBO not complete!\n");
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    g_crt.texW = width;
    g_crt.texH = height;

    return true;
}

// Resize CRT FBO if window size changed
inline void CRT_ResizeFBO(int width, int height) {
    if (width == g_crt.texW && height == g_crt.texH) return;
    glBindTexture(GL_TEXTURE_2D, g_crt.colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    g_crt.texW = width;
    g_crt.texH = height;
}

// Begin rendering to the CRT FBO (call before theApp.Draw())
inline void CRT_BeginCapture() {
    glBindFramebuffer(GL_FRAMEBUFFER, g_crt.fbo);
}

// End capture and blit with CRT shader to default framebuffer
inline void CRT_EndAndBlit(float time) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Set viewport to full window for the blit pass
    glViewport(0, 0, g_crt.texW, g_crt.texH);

    // Save GL state that the scene shader may have set
    GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
    GLboolean blendEnabled = glIsEnabled(GL_BLEND);
    GLboolean cullEnabled  = glIsEnabled(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    glUseProgram(g_crt.program);

    // Set uniforms
    glUniform1i(g_crt.u_SceneTex, 0);
    glUniform2f(g_crt.u_Resolution, (float)g_crt.texW, (float)g_crt.texH);
    glUniform1f(g_crt.u_Time, time);
    glUniform1f(g_crt.u_ScanlineIntensity, g_crt.scanlineIntensity);
    glUniform1f(g_crt.u_Curvature, g_crt.curvature);
    glUniform1f(g_crt.u_PhosphorMask, g_crt.phosphorMask);
    glUniform1f(g_crt.u_Vignette, g_crt.vignette);
    glUniform1f(g_crt.u_Bloom, g_crt.bloom);
    glUniform1f(g_crt.u_Flicker, g_crt.flickerAmount);
    glUniform1f(g_crt.u_ColorBleed, g_crt.colorBleed);
    glUniform1f(g_crt.u_Brightness, g_crt.brightness);

    // Bind scene texture
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_crt.colorTex);

    // Draw fullscreen triangle
    glBindVertexArray(g_crt.quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // Restore state
    glBindVertexArray(g_gl.vao);
    glUseProgram(g_gl.program);
    if (depthEnabled) glEnable(GL_DEPTH_TEST);
    if (blendEnabled) glEnable(GL_BLEND);
    if (cullEnabled)  glEnable(GL_CULL_FACE);
    glActiveTexture(GL_TEXTURE0);
}

// Compile and link the GL shader program. Called from sdl_main.cpp after GL context creation.
inline bool InitGLShaders() {
    // ---- Vertex Shader ----
    const char* vsSrc = R"(
#version 150

uniform mat4 u_WVP;
uniform mat4 u_WorldView;
uniform vec4 u_FalloffFront;
uniform vec4 u_FalloffDelta;
uniform mat4 u_NormalInv;     // WV^-1 for normal transform
uniform vec4 u_TFactor;
uniform vec4 u_MatDiffuse;
uniform int u_VertexMode;     // 0=3D, 1=RHW
uniform int u_ColorSource;    // 0=falloff, 1=diffuse, 2=tfactor, 3=white, 4=matDiffuse
uniform int u_AlphaSource;    // 0=from_color, 1=tfactor, 2=diffuse, 3=opaque
uniform float u_AlphaMul;
uniform int u_VertexAlphaMul; // 1 = also multiply by vertex diffuse alpha (for soft-fade text clipping)
uniform int u_NormalType;     // 0=none, 1=float3, 2=packed
uniform int u_EnvMapMode;    // 0=off, 1=spherical env map
uniform vec2 u_ViewportSize; // backbuffer width/height for XYZRHW conversion

in vec4 a_Position;       // xyz[rhw] — w=1 for XYZ, w=1/z for RHW
in vec3 a_Normal;         // float3 normal (when normalType==1)
in uint a_PackedNrm;      // packed 11:11:10 normal (when normalType==2)
in vec4 a_Diffuse;        // vertex color RGBA [0..1]
in vec2 a_TexCoord;

out vec4 v_Color;
out vec2 v_TexCoord;

vec3 unpackNormal11_11_10(uint p) {
    int ix = int(p & 0x7FFu); if (ix >= 1024) ix -= 2048;
    int iy = int((p >> 11u) & 0x7FFu); if (iy >= 1024) iy -= 2048;
    int iz = int((p >> 22u) & 0x3FFu); if (iz >= 512) iz -= 1024;
    return vec3(float(ix) / 1023.0, float(iy) / 1023.0, float(iz) / 511.0);
}

void main() {
    if (u_VertexMode == 1) {
        // Pre-transformed (XYZRHW): screen coords -> NDC
        gl_Position = vec4(
            a_Position.x / (u_ViewportSize.x * 0.5) - 1.0,
            1.0 - a_Position.y / (u_ViewportSize.y * 0.5),
            0.0, 1.0);
    } else {
        // D3D row-major uploaded without transpose: GL sees WVP^T
        // u_WVP * v gives correct D3D result (pos * WVP)
        gl_Position = u_WVP * vec4(a_Position.xyz, 1.0);
        // D3D maps Z to [0,1], GL clips to [-1,1]. Remap.
        gl_Position.z = 2.0 * gl_Position.z - gl_Position.w;
    }

    // Get normal
    vec3 nrm = vec3(0.0, 0.0, 1.0);
    if (u_NormalType == 1) nrm = a_Normal;
    else if (u_NormalType == 2) nrm = unpackNormal11_11_10(a_PackedNrm);

    // Compute falloff color (matching Xbox effect.vsh)
    // Xbox formula: color = sideColor + (frontColor - sideColor) * abs(viewDot)
    // u_FalloffFront = sideColor (c15), u_FalloffDelta = frontColor - sideColor (c16)
    vec4 falloffColor = vec4(0.5);
    if (u_ColorSource == 0 && u_VertexMode == 0) {
        vec3 viewPos = (u_WorldView * vec4(a_Position.xyz, 1.0)).xyz;
        vec3 viewNrm = (u_NormalInv * vec4(nrm, 0.0)).xyz;
        float plen = length(viewPos);
        float nlen = length(viewNrm);
        if (plen > 1e-6) viewPos /= plen;
        if (nlen > 1e-6) viewNrm /= nlen;
        float viewDot = abs(dot(viewNrm, viewPos));
        falloffColor = clamp(u_FalloffFront + u_FalloffDelta * viewDot, 0.0, 1.0);
    }

    // D3DCOLOR is ARGB (0xAARRGGBB), stored little-endian as [B,G,R,A]
    // GL reads bytes in order so a_Diffuse = (B,G,R,A) — swizzle to RGBA
    vec4 diffuseRGBA = a_Diffuse.bgra;

    // Select vertex color source
    vec4 color;
    if      (u_ColorSource == 0) color = falloffColor;
    else if (u_ColorSource == 1) color = diffuseRGBA;
    else if (u_ColorSource == 2) color = u_TFactor;
    else if (u_ColorSource == 3) color = vec4(1.0);
    else                         color = u_MatDiffuse;

    // Alpha source override
    float alpha = color.a;
    if      (u_AlphaSource == 1) alpha = u_TFactor.a;
    else if (u_AlphaSource == 2) alpha = diffuseRGBA.a;
    else if (u_AlphaSource == 3) alpha = 1.0;

    alpha *= u_AlphaMul;

    // Optional clip-by-vertex-alpha. text.cpp's VerticalFade writes per-vertex
    // alpha into the diffuse channel for the soft-fade marquee clip; falloff
    // lighting overwrites color.a so the per-vertex alpha needs an explicit
    // multiplicative path. Only enabled for that draw, leaving normal lit
    // meshes (which may have undefined diffuse alpha) untouched.
    if (u_VertexAlphaMul != 0) alpha *= diffuseRGBA.a;

    v_Color = vec4(color.rgb, alpha);
    v_TexCoord = a_TexCoord;

    // Spherical environment mapping: compute UVs from view-space normal
    if (u_EnvMapMode == 1 && u_VertexMode == 0) {
        vec3 viewNrm = normalize((u_NormalInv * vec4(nrm, 0.0)).xyz);
        v_TexCoord = viewNrm.xy * 0.5 + 0.5;
    }
}
)";

    // ---- Fragment Shader ----
    const char* fsSrc = R"(
#version 150

uniform sampler2D u_Tex0;
uniform sampler2D u_Tex1;
uniform int u_FragColorOp;   // 0=v_Color, 1=texture, 2=v_Color*texture
uniform int u_FragAlphaOp;   // 0=v_Color.a, 1=tex.a, 2=v_Color.a*tex.a
uniform int u_HasTex1;       // stage 1 texture bound
uniform int u_Tex1AlphaOp;   // 0=none, 1=multiply alpha by tex1.a
uniform int u_Tex1ColorOp;   // 0=none, 1=add tex1.rgb to color
uniform float u_AlphaRef;    // alpha test threshold (0=disabled)

in vec4 v_Color;
in vec2 v_TexCoord;
out vec4 fragColor;

void main() {
    vec4 tex = texture(u_Tex0, v_TexCoord);

    // Color: independently controlled
    vec3 color;
    if      (u_FragColorOp == 0) color = v_Color.rgb;
    else if (u_FragColorOp == 1) color = tex.rgb;
    else if (u_FragColorOp == 2) color = v_Color.rgb * tex.rgb;
    else                         color = v_Color.rgb + tex.rgb; // D3DTOP_ADD

    // Alpha: independently controlled
    float alpha;
    if      (u_FragAlphaOp == 0) alpha = v_Color.a;
    else if (u_FragAlphaOp == 1) alpha = tex.a;
    else                         alpha = v_Color.a * tex.a;

    // Stage 1 texture (radial alpha masks, etc.)
    if (u_HasTex1 != 0) {
        vec4 tex1 = texture(u_Tex1, v_TexCoord);
        if (u_Tex1AlphaOp == 1) alpha *= tex1.a;
        if (u_Tex1ColorOp == 1) color = clamp(color + tex1.rgb, 0.0, 1.0);
    }

    vec4 finalColor = vec4(color, alpha);
    if (u_AlphaRef > 0.0 && finalColor.a < u_AlphaRef) discard;
    fragColor = finalColor;
}
)";

    // Compile vertex shader
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vsSrc, NULL);
    glCompileShader(vs);
    GLint ok = 0; glGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024]; glGetShaderInfoLog(vs, sizeof(log), NULL, log);
        fprintf(stderr, "[GL] Vertex shader error: %s\n", log);
        return false;
    }

    // Compile fragment shader
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fsSrc, NULL);
    glCompileShader(fs);
    glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024]; glGetShaderInfoLog(fs, sizeof(log), NULL, log);
        fprintf(stderr, "[GL] Fragment shader error: %s\n", log);
        return false;
    }

    // Link program
    g_gl.program = glCreateProgram();
    glAttachShader(g_gl.program, vs);
    glAttachShader(g_gl.program, fs);

    // Bind attribute locations before linking
    glBindAttribLocation(g_gl.program, 0, "a_Position");
    glBindAttribLocation(g_gl.program, 1, "a_Normal");
    // a_PackedNrm needs glGetAttribLocation after link (integer attrib)
    glBindAttribLocation(g_gl.program, 3, "a_Diffuse");
    glBindAttribLocation(g_gl.program, 4, "a_TexCoord");

    glLinkProgram(g_gl.program);
    glGetProgramiv(g_gl.program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024]; glGetProgramInfoLog(g_gl.program, sizeof(log), NULL, log);
        fprintf(stderr, "[GL] Link error: %s\n", log);
        return false;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);

    glUseProgram(g_gl.program);

    // Get uniform locations
    g_gl.u_WVP          = glGetUniformLocation(g_gl.program, "u_WVP");
    g_gl.u_WorldView     = glGetUniformLocation(g_gl.program, "u_WorldView");
    g_gl.u_FalloffFront  = glGetUniformLocation(g_gl.program, "u_FalloffFront");
    g_gl.u_FalloffDelta  = glGetUniformLocation(g_gl.program, "u_FalloffDelta");
    g_gl.u_NormalInv     = glGetUniformLocation(g_gl.program, "u_NormalInv");
    g_gl.u_TFactor       = glGetUniformLocation(g_gl.program, "u_TFactor");
    g_gl.u_MatDiffuse    = glGetUniformLocation(g_gl.program, "u_MatDiffuse");
    g_gl.u_VertexMode    = glGetUniformLocation(g_gl.program, "u_VertexMode");
    g_gl.u_ColorSource   = glGetUniformLocation(g_gl.program, "u_ColorSource");
    g_gl.u_AlphaSource   = glGetUniformLocation(g_gl.program, "u_AlphaSource");
    g_gl.u_AlphaMul      = glGetUniformLocation(g_gl.program, "u_AlphaMul");
    g_gl.u_VertexAlphaMul= glGetUniformLocation(g_gl.program, "u_VertexAlphaMul");
    g_gl.u_NormalType    = glGetUniformLocation(g_gl.program, "u_NormalType");
    g_gl.u_Tex0          = glGetUniformLocation(g_gl.program, "u_Tex0");
    g_gl.u_Tex1          = glGetUniformLocation(g_gl.program, "u_Tex1");
    g_gl.u_FragColorOp   = glGetUniformLocation(g_gl.program, "u_FragColorOp");
    g_gl.u_FragAlphaOp   = glGetUniformLocation(g_gl.program, "u_FragAlphaOp");
    g_gl.u_HasTex1       = glGetUniformLocation(g_gl.program, "u_HasTex1");
    g_gl.u_Tex1AlphaOp   = glGetUniformLocation(g_gl.program, "u_Tex1AlphaOp");
    g_gl.u_Tex1ColorOp   = glGetUniformLocation(g_gl.program, "u_Tex1ColorOp");
    g_gl.u_EnvMapMode    = glGetUniformLocation(g_gl.program, "u_EnvMapMode");
    g_gl.u_ViewportSize  = glGetUniformLocation(g_gl.program, "u_ViewportSize");
    g_gl.u_AlphaRef      = glGetUniformLocation(g_gl.program, "u_AlphaRef");

    // Attribute locations
    g_gl.a_Position  = 0;
    g_gl.a_Normal    = 1;
    g_gl.a_PackedNrm = glGetAttribLocation(g_gl.program, "a_PackedNrm");
    g_gl.a_Diffuse   = 3;
    g_gl.a_TexCoord  = 4;

    // Set texture units
    glUniform1i(g_gl.u_Tex0, 0);
    glUniform1i(g_gl.u_Tex1, 1);

    // Create VAO (required for GL 3.2 Core)
    glGenVertexArrays(1, &g_gl.vao);
    glBindVertexArray(g_gl.vao);

    // Create dynamic buffers for UP draw calls
    glGenBuffers(1, &g_gl.dynamicVBO);
    glGenBuffers(1, &g_gl.dynamicIBO);

    // Create 1x1 white texture (used when no texture is bound)
    glGenTextures(1, &g_gl.whiteTex);
    glBindTexture(GL_TEXTURE_2D, g_gl.whiteTex);
    BYTE white[4] = {255, 255, 255, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

    // Default GL state
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);  // D3D defaults to LESSEQUAL, GL defaults to LESS
    glDisable(GL_CULL_FACE);
    // D3D front faces are CW (left-handed); GL default is CCW.
    // Flip GL's convention so D3DCULL_CCW/CW map correctly.
    glFrontFace(GL_CW);

    // Set default uniform values
    glUniform1f(g_gl.u_AlphaMul, 1.0f);
    glUniform1i(g_gl.u_VertexAlphaMul, 0);
    glUniform1i(g_gl.u_VertexMode, 0);
    glUniform1i(g_gl.u_ColorSource, 3); // white
    glUniform1i(g_gl.u_AlphaSource, 3); // opaque
    glUniform1i(g_gl.u_NormalType, 0);
    glUniform1i(g_gl.u_FragColorOp, 0); // vertex color
    glUniform1i(g_gl.u_FragAlphaOp, 0); // vertex alpha
    glUniform1i(g_gl.u_HasTex1, 0);
    glUniform1i(g_gl.u_Tex1AlphaOp, 0);
    glUniform2f(g_gl.u_ViewportSize, 640.0f, 480.0f); // default, updated by CreateDevice

    return true;
}

// -------------------------------------------------------
// D3D8 interface stubs backed by OpenGL
// -------------------------------------------------------

class IDirect3DTexture8 {
    int m_ref;
public:
    GLuint m_glTexture;
    UINT m_width, m_height;
    BYTE* m_pixels;      // RGBA pixel data (for LockRect)
    int m_pitch;
    char m_srcName[128]; // Source filename for inspector

    // Instance tracking for GL context reset
    IDirect3DTexture8* m_nextTex;
    static IDirect3DTexture8* s_firstTex;

    IDirect3DTexture8() : m_ref(1), m_glTexture(0), m_width(0), m_height(0), m_pixels(NULL), m_pitch(0) {
        m_srcName[0] = 0;
        m_nextTex = s_firstTex; s_firstTex = this;
    }
    ~IDirect3DTexture8() {
        if (m_glTexture) glDeleteTextures(1, &m_glTexture);
        free(m_pixels);
        // Remove from linked list
        IDirect3DTexture8** pp = &s_firstTex;
        while (*pp && *pp != this) pp = &(*pp)->m_nextTex;
        if (*pp) *pp = m_nextTex;
    }
    ULONG AddRef() { return ++m_ref; }
    ULONG Release() { if(--m_ref <= 0) { delete this; return 0; } return m_ref; }
    HRESULT GetLevelDesc(UINT level, D3DSURFACE_DESC* desc) {
        desc->Width = m_width ? m_width : 1;
        desc->Height = m_height ? m_height : 1;
        desc->Format = D3DFMT_A8R8G8B8;
        return S_OK;
    }
    HRESULT LockRect(UINT level, D3DLOCKED_RECT* lr, const void* rect, DWORD flags) {
        if (!m_pixels && m_width > 0 && m_height > 0) {
            m_pitch = m_width * 4;
            m_pixels = (BYTE*)calloc(m_width * m_height, 4);
        }
        if (m_pixels) { lr->Pitch = m_pitch; lr->pBits = m_pixels; }
        else { static char dummy[4]={}; lr->Pitch=4; lr->pBits=dummy; }
        return S_OK;
    }
    HRESULT UnlockRect(UINT level) {
        // Sync pixel data to GL texture
        if (m_pixels && m_width > 0 && m_height > 0) {
            if (!m_glTexture) {
                glGenTextures(1, &m_glTexture);
                glBindTexture(GL_TEXTURE_2D, m_glTexture);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, m_pixels);
            } else {
                glBindTexture(GL_TEXTURE_2D, m_glTexture);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_width, m_height, GL_RGBA, GL_UNSIGNED_BYTE, m_pixels);
            }
        }
        return S_OK;
    }

    // Create GL texture from RGBA pixel data
    bool CreateFromRGBA(UINT w, UINT h, const BYTE* rgba) {
        if (!rgba || w == 0 || h == 0) return false;
        m_width = w; m_height = h; m_pitch = w * 4;
        m_pixels = (BYTE*)malloc(w * h * 4);
        if (!m_pixels) return false;
        memcpy(m_pixels, rgba, w * h * 4);
        glGenTextures(1, &m_glTexture);
        glBindTexture(GL_TEXTURE_2D, m_glTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        return true;
    }
};
typedef IDirect3DTexture8* LPDIRECT3DTEXTURE8;

class IDirect3DVertexBuffer8 {
    int m_ref; UINT m_size;
public:
    void* m_data;
    GLuint m_glBuffer;
    bool m_glDirty;
    IDirect3DVertexBuffer8* m_nextVB;
    static IDirect3DVertexBuffer8* s_firstVB;

    IDirect3DVertexBuffer8(UINT size) : m_ref(1), m_size(size), m_glBuffer(0), m_glDirty(true) {
        m_data = calloc(1, size ? size : 1);
        // Defer glGenBuffers to EnsureUploaded — constructor may run on non-GL thread
        m_nextVB = s_firstVB; s_firstVB = this;
    }
    ~IDirect3DVertexBuffer8() {
        free(m_data);
        if (m_glBuffer) glDeleteBuffers(1, &m_glBuffer);
        IDirect3DVertexBuffer8** pp = &s_firstVB;
        while (*pp && *pp != this) pp = &(*pp)->m_nextVB;
        if (*pp) *pp = m_nextVB;
    }
    ULONG AddRef() { return ++m_ref; }
    ULONG Release() { if(--m_ref <= 0) { delete this; return 0; } return m_ref; }
    HRESULT Lock(UINT off, UINT size, BYTE** ppData, DWORD flags) {
        *ppData = (BYTE*)m_data + off;
        m_glDirty = true;
        return S_OK;
    }
    HRESULT Unlock() { return S_OK; }
    UINT GetSize() const { return m_size; }

    void EnsureUploaded() {
        if (m_glBuffer == 0) glGenBuffers(1, &m_glBuffer);
        if (m_glDirty && m_size > 0) {
            glBindBuffer(GL_ARRAY_BUFFER, m_glBuffer);
            glBufferData(GL_ARRAY_BUFFER, m_size, m_data, GL_DYNAMIC_DRAW);
            m_glDirty = false;
        }
    }
};
typedef IDirect3DVertexBuffer8* LPDIRECT3DVERTEXBUFFER8;

class IDirect3DIndexBuffer8 {
    int m_ref; UINT m_size;
public:
    void* m_data;
    GLuint m_glBuffer;
    bool m_glDirty;
    IDirect3DIndexBuffer8* m_nextIB;
    static IDirect3DIndexBuffer8* s_firstIB;

    IDirect3DIndexBuffer8(UINT size) : m_ref(1), m_size(size), m_glBuffer(0), m_glDirty(true) {
        m_data = malloc(size);
        // Defer glGenBuffers to EnsureUploaded — constructor may run on non-GL thread
        m_nextIB = s_firstIB; s_firstIB = this;
    }
    ~IDirect3DIndexBuffer8() {
        free(m_data);
        if (m_glBuffer) glDeleteBuffers(1, &m_glBuffer);
        IDirect3DIndexBuffer8** pp = &s_firstIB;
        while (*pp && *pp != this) pp = &(*pp)->m_nextIB;
        if (*pp) *pp = m_nextIB;
    }
    ULONG AddRef() { return ++m_ref; }
    ULONG Release() { if(--m_ref <= 0) { delete this; return 0; } return m_ref; }
    HRESULT Lock(UINT off, UINT size, BYTE** ppData, DWORD flags) { *ppData = (BYTE*)m_data + off; m_glDirty = true; return S_OK; }
    HRESULT Unlock() { return S_OK; }

    void EnsureUploaded() {
        if (m_glBuffer == 0) glGenBuffers(1, &m_glBuffer);
        if (m_glDirty && m_size > 0) {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_glBuffer);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_size, m_data, GL_DYNAMIC_DRAW);
            m_glDirty = false;
        }
    }
};
typedef IDirect3DIndexBuffer8* LPDIRECT3DINDEXBUFFER8;

// Re-upload all GL resources after context recreation (MSAA change, etc.)
inline void ReuploadAllGLResources() {
    // Re-upload textures from CPU pixel data
    for (IDirect3DTexture8* t = IDirect3DTexture8::s_firstTex; t; t = t->m_nextTex) {
        t->m_glTexture = 0; // old handle is invalid
        if (t->m_pixels && t->m_width > 0 && t->m_height > 0) {
            glGenTextures(1, &t->m_glTexture);
            glBindTexture(GL_TEXTURE_2D, t->m_glTexture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, t->m_width, t->m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, t->m_pixels);
        }
    }
    // Re-create VB GL buffers and mark dirty
    for (IDirect3DVertexBuffer8* vb = IDirect3DVertexBuffer8::s_firstVB; vb; vb = vb->m_nextVB) {
        vb->m_glBuffer = 0;
        glGenBuffers(1, &vb->m_glBuffer);
        vb->m_glDirty = true;
    }
    // Re-create IB GL buffers and mark dirty
    for (IDirect3DIndexBuffer8* ib = IDirect3DIndexBuffer8::s_firstIB; ib; ib = ib->m_nextIB) {
        ib->m_glBuffer = 0;
        glGenBuffers(1, &ib->m_glBuffer);
        ib->m_glDirty = true;
    }
    fprintf(stderr, "[GL] Re-uploaded all resources after context reset\n");
}

// Deferred implementations for ID3DXMesh (needs buffer classes to be defined)
inline HRESULT ID3DXMesh::GetVertexBuffer(IDirect3DVertexBuffer8** ppVB) {
    if (!m_vbData || m_numVerts == 0) { *ppVB = NULL; return E_FAIL; }
    UINT size = m_numVerts * m_vertStride;
    IDirect3DVertexBuffer8* vb = new IDirect3DVertexBuffer8(size);
    memcpy(vb->m_data, m_vbData, size);
    vb->m_glDirty = true;
    *ppVB = vb;
    return S_OK;
}
inline HRESULT ID3DXMesh::GetIndexBuffer(IDirect3DIndexBuffer8** ppIB) {
    if (!m_ibData || m_numFaces == 0) { *ppIB = NULL; return E_FAIL; }
    UINT size = m_numFaces * 3 * sizeof(WORD);
    IDirect3DIndexBuffer8* ib = new IDirect3DIndexBuffer8(size);
    memcpy(ib->m_data, m_ibData, size);
    ib->m_glDirty = true;
    *ppIB = ib;
    return S_OK;
}

class IDirect3DSurface8 {
    int m_ref;
public:
    IDirect3DSurface8() : m_ref(1) {}
    ULONG AddRef() { return ++m_ref; }
    ULONG Release() { if(--m_ref <= 0) { delete this; return 0; } return m_ref; }
    HRESULT GetDesc(D3DSURFACE_DESC* desc) { desc->Width=640; desc->Height=480; desc->Format=D3DFMT_A8R8G8B8; return S_OK; }
    HRESULT LockRect(D3DLOCKED_RECT* lr, const void* rect, DWORD flags) { static char dummy[4]={0}; lr->Pitch=4; lr->pBits=dummy; return S_OK; }
    HRESULT UnlockRect() { return S_OK; }
};
typedef IDirect3DSurface8* LPDIRECT3DSURFACE8;

class IDirect3DDevice8 {
public:
    int m_ref;
    DWORD m_fvf;
    IDirect3DTexture8* m_textures[4];
    D3DXMATRIX m_matWorld, m_matView, m_matProj;
    D3DXMATRIX m_matWVP; // cached world*view*proj
    D3DXMATRIX m_matWV;  // cached world*view (for falloff normals)
    bool m_wvpDirty;
    D3DCOLOR m_matDiffuse;
    D3DCOLOR m_texFactor;
    float m_falloffFront[4]; // c15
    float m_falloffDelta[4]; // c16
    float m_normalInv[16];   // c5-c8: WV^-1 for normal transform
    bool m_alphaTestEnabled = false;
    float m_alphaRef = 0.0f;
    bool m_envMapMode;       // true when reflection shader sets c48
    bool m_effectShaderActive;
    bool m_alphaBlendEnable;
    DWORD m_srcBlend, m_destBlend;
    bool m_zEnable;
    bool m_zWriteEnable;
    DWORD m_cullMode;
    bool m_lighting;
    bool m_colorVertex;
public:
    const char* m_dbgMatName; // debug: last material name

    // ---- Node Inspector ----
    struct DrawRecord {
        const char* matName;
        char tex0Name[128];
        UINT tex0W, tex0H;
        char tex1Name[128];
        float screenMinX, screenMinY, screenMaxX, screenMaxY;
        void* sceneNode; // CNode* that issued this draw call
    };
    static const int MAX_DRAW_RECORDS = 2048;
    DrawRecord m_drawRecords[MAX_DRAW_RECORDS];
    int m_drawRecordCount;
    int m_inspectorHitID;           // last hovered draw record (-1 = none)
    char m_inspectorText[512];      // formatted tooltip text
    int m_mouseX, m_mouseY;         // current mouse position
    bool m_inspectorEnabled;        // toggle with F1
    void* m_inspectorCurrentNode;   // CNode* currently being rendered
    void* m_inspectorSelectedNode;  // CNode* selected in tree or 3D view

    void InspectorBeginFrame() { m_drawRecordCount = 0; }
    const char* InspectorGetText() { return (m_inspectorHitID >= 0) ? m_inspectorText : NULL; }

private:
    // TSS tracking
    DWORD m_tss0ColorOp, m_tss0ColorArg1, m_tss0ColorArg2;
    DWORD m_tss0AlphaOp, m_tss0AlphaArg1, m_tss0AlphaArg2;
    DWORD m_tss1ColorOp, m_tss1ColorArg1, m_tss1ColorArg2;
    DWORD m_tss1AlphaOp, m_tss1AlphaArg1, m_tss1AlphaArg2;

    void UpdateWVP() {
        if (m_wvpDirty) {
            m_matWV = m_matWorld * m_matView;
            m_matWVP = m_matWV * m_matProj;
            m_wvpDirty = false;
        }
    }

public:
    IDirect3DDevice8() : m_ref(1), m_fvf(0), m_streamVB(NULL), m_streamIB(NULL), m_streamStride(0), m_baseVertex(0), m_wvpDirty(true), m_matDiffuse(0xFFFFFFFF), m_texFactor(0xFFFFFFFF), m_falloffFront{1,1,1,1}, m_falloffDelta{0,0,0,0}, m_normalInv{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}, m_envMapMode(false),
        m_effectShaderActive(false),
        m_alphaBlendEnable(true), m_srcBlend(D3DBLEND_SRCALPHA), m_destBlend(D3DBLEND_INVSRCALPHA),
        m_zEnable(false), m_zWriteEnable(false), m_cullMode(D3DCULL_NONE), m_lighting(false), m_colorVertex(false),
        m_dbgMatName("(none)"), m_drawRecordCount(0), m_inspectorHitID(-1), m_mouseX(0), m_mouseY(0), m_inspectorEnabled(false), m_inspectorCurrentNode(NULL), m_inspectorSelectedNode(NULL), m_frameDrawCalls(0), m_frameNumber(0),
        m_tss0ColorOp(D3DTOP_DISABLE), m_tss0ColorArg1(D3DTA_TEXTURE), m_tss0ColorArg2(D3DTA_CURRENT),
        m_tss0AlphaOp(D3DTOP_DISABLE), m_tss0AlphaArg1(D3DTA_TEXTURE), m_tss0AlphaArg2(D3DTA_CURRENT),
        m_tss1ColorOp(D3DTOP_DISABLE), m_tss1ColorArg1(D3DTA_TEXTURE), m_tss1ColorArg2(D3DTA_CURRENT),
        m_tss1AlphaOp(D3DTOP_DISABLE), m_tss1AlphaArg1(D3DTA_TEXTURE), m_tss1AlphaArg2(D3DTA_CURRENT) {
        memset(m_textures, 0, sizeof(m_textures));
        D3DXMatrixIdentity(&m_matWorld); D3DXMatrixIdentity(&m_matView); D3DXMatrixIdentity(&m_matProj);
        D3DXMatrixIdentity(&m_matWVP); D3DXMatrixIdentity(&m_matWV);
    }
    ULONG AddRef() { return ++m_ref; }
    ULONG Release() { if(--m_ref <= 0) { delete this; return 0; } return m_ref; }

    // ---- Inspector: record draw call with screen AABB ----
    void InspectorRecordDraw(UINT numVerts, const void* vertData, UINT stride, bool isRHW) {
        if (!m_inspectorEnabled || m_drawRecordCount >= MAX_DRAW_RECORDS) return;
        if (!vertData || numVerts == 0 || stride == 0) return;

        DrawRecord& r = m_drawRecords[m_drawRecordCount];
        r.matName = m_dbgMatName;
        r.sceneNode = m_inspectorCurrentNode;
        r.tex0Name[0] = 0; r.tex1Name[0] = 0;
        r.tex0W = 0; r.tex0H = 0;
        if (m_textures[0]) {
            strncpy(r.tex0Name, m_textures[0]->m_srcName, 127); r.tex0Name[127] = 0;
            r.tex0W = m_textures[0]->m_width; r.tex0H = m_textures[0]->m_height;
        }
        if (m_textures[1]) {
            strncpy(r.tex1Name, m_textures[1]->m_srcName, 127); r.tex1Name[127] = 0;
        }

        // Compute screen-space AABB
        float minSX = 1e9f, minSY = 1e9f, maxSX = -1e9f, maxSY = -1e9f;
        GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
        float vpW = (float)vp[2], vpH = (float)vp[3];

        const BYTE* vd = (const BYTE*)vertData;
        UpdateWVP();

        for (UINT i = 0; i < numVerts; i++) {
            const float* pos = (const float*)(vd + i * stride);
            float sx, sy;
            if (isRHW) {
                sx = pos[0]; sy = pos[1];
            } else {
                // D3D row-vector: clip = pos * WVP
                float x = pos[0], y = pos[1], z = pos[2];
                float cx = x*m_matWVP.m[0][0] + y*m_matWVP.m[1][0] + z*m_matWVP.m[2][0] + m_matWVP.m[3][0];
                float cy = x*m_matWVP.m[0][1] + y*m_matWVP.m[1][1] + z*m_matWVP.m[2][1] + m_matWVP.m[3][1];
                float cw = x*m_matWVP.m[0][3] + y*m_matWVP.m[1][3] + z*m_matWVP.m[2][3] + m_matWVP.m[3][3];
                if (cw < 0.001f) continue; // behind camera
                float ndcx = cx / cw, ndcy = cy / cw;
                sx = (ndcx * 0.5f + 0.5f) * vpW;
                sy = (1.0f - (ndcy * 0.5f + 0.5f)) * vpH;
            }
            if (sx < minSX) minSX = sx;
            if (sy < minSY) minSY = sy;
            if (sx > maxSX) maxSX = sx;
            if (sy > maxSY) maxSY = sy;
        }
        r.screenMinX = minSX; r.screenMinY = minSY;
        r.screenMaxX = maxSX; r.screenMaxY = maxSY;
        m_drawRecordCount++;
    }

    int InspectorHitTest(int mx, int my) {
        m_inspectorHitID = -1;
        m_inspectorText[0] = 0;
        // Walk back-to-front: last drawn (topmost) wins
        for (int i = m_drawRecordCount - 1; i >= 0; i--) {
            const DrawRecord& r = m_drawRecords[i];
            if (mx >= r.screenMinX && mx <= r.screenMaxX &&
                my >= r.screenMinY && my <= r.screenMaxY) {
                // Skip tiny/degenerate AABBs (< 4px)
                if ((r.screenMaxX - r.screenMinX) < 4 || (r.screenMaxY - r.screenMinY) < 4) continue;
                m_inspectorHitID = i;
                snprintf(m_inspectorText, sizeof(m_inspectorText),
                    "Material: %s | Tex0: %s (%ux%u)%s%s",
                    r.matName ? r.matName : "(none)",
                    r.tex0Name[0] ? r.tex0Name : "(none)", r.tex0W, r.tex0H,
                    r.tex1Name[0] ? " | Tex1: " : "",
                    r.tex1Name[0] ? r.tex1Name : "");
                return i;
            }
        }
        return -1;
    }

    HRESULT SetRenderState(DWORD state, DWORD value) {
        if (state == D3DRS_TEXTUREFACTOR) m_texFactor = (D3DCOLOR)value;
        else if (state == D3DRS_ALPHABLENDENABLE) {
            m_alphaBlendEnable = (value != 0);
            if (value) glEnable(GL_BLEND); else glDisable(GL_BLEND);
        }
        else if (state == D3DRS_SRCBLEND) {
            m_srcBlend = value;
            ApplyBlendFunc();
        }
        else if (state == D3DRS_DESTBLEND) {
            m_destBlend = value;
            ApplyBlendFunc();
        }
        else if (state == D3DRS_ZENABLE) {
            m_zEnable = (value != 0);
            if (value) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        }
        else if (state == D3DRS_ZWRITEENABLE) {
            m_zWriteEnable = (value != 0);
            glDepthMask(value ? GL_TRUE : GL_FALSE);
        }
        else if (state == D3DRS_ZFUNC) {
            GLenum func = GL_LEQUAL;
            if (value == D3DCMP_LESS) func = GL_LESS;
            else if (value == D3DCMP_ALWAYS) func = GL_ALWAYS;
            glDepthFunc(func);
        }
        else if (state == D3DRS_CULLMODE) {
            m_cullMode = value;
            if (value == D3DCULL_NONE) glDisable(GL_CULL_FACE);
            else {
                glEnable(GL_CULL_FACE);
                glCullFace(value == D3DCULL_CW ? GL_FRONT : GL_BACK);
            }
        }
        else if (state == D3DRS_FILLMODE) {
            glPolygonMode(GL_FRONT_AND_BACK, value == D3DFILL_WIREFRAME ? GL_LINE : GL_FILL);
        }
        else if (state == D3DRS_LIGHTING) {
            m_lighting = (value != 0);
        }
        else if (state == D3DRS_COLORVERTEX) {
            m_colorVertex = (value != 0);
        }
        else if (state == D3DRS_ALPHATESTENABLE) {
            m_alphaTestEnabled = (value != 0);
        }
        else if (state == D3DRS_ALPHAREF) {
            m_alphaRef = (float)(value & 0xFF) / 255.0f;
        }
        else if (state == D3DRS_ALPHAFUNC) {
            // Only GREATEREQUAL supported; just accept and return
        }
        return S_OK;
    }

    void ApplyBlendFunc() {
        GLenum src = GL_SRC_ALPHA, dst = GL_ONE_MINUS_SRC_ALPHA;
        switch (m_srcBlend) {
            case D3DBLEND_ZERO: src = GL_ZERO; break;
            case D3DBLEND_ONE: src = GL_ONE; break;
            case D3DBLEND_SRCALPHA: src = GL_SRC_ALPHA; break;
            case D3DBLEND_INVSRCALPHA: src = GL_ONE_MINUS_SRC_ALPHA; break;
            case D3DBLEND_DESTALPHA: src = GL_DST_ALPHA; break;
            case D3DBLEND_INVDESTALPHA: src = GL_ONE_MINUS_DST_ALPHA; break;
        }
        switch (m_destBlend) {
            case D3DBLEND_ZERO: dst = GL_ZERO; break;
            case D3DBLEND_ONE: dst = GL_ONE; break;
            case D3DBLEND_SRCALPHA: dst = GL_SRC_ALPHA; break;
            case D3DBLEND_INVSRCALPHA: dst = GL_ONE_MINUS_SRC_ALPHA; break;
            case D3DBLEND_DESTALPHA: dst = GL_DST_ALPHA; break;
            case D3DBLEND_INVDESTALPHA: dst = GL_ONE_MINUS_DST_ALPHA; break;
        }
        glBlendFunc(src, dst);
    }

    HRESULT GetRenderState(D3DRENDERSTATETYPE state, DWORD* pValue) {
        switch (state) {
            case D3DRS_ZENABLE:          *pValue = m_zEnable ? 1 : 0; break;
            case D3DRS_ZWRITEENABLE:     *pValue = m_zWriteEnable ? 1 : 0; break;
            case D3DRS_ALPHABLENDENABLE: *pValue = m_alphaBlendEnable ? 1 : 0; break;
            case D3DRS_SRCBLEND:         *pValue = m_srcBlend; break;
            case D3DRS_DESTBLEND:        *pValue = m_destBlend; break;
            case D3DRS_CULLMODE:         *pValue = m_cullMode; break;
            case D3DRS_LIGHTING:         *pValue = m_lighting ? 1 : 0; break;
            case D3DRS_COLORVERTEX:      *pValue = m_colorVertex ? 1 : 0; break;
            case D3DRS_TEXTUREFACTOR:    *pValue = m_texFactor; break;
            default: *pValue = 0; break;
        }
        return S_OK;
    }
    static GLenum D3DWrapToGL(DWORD d3dAddr) {
        switch (d3dAddr) {
            case 3: return GL_CLAMP_TO_EDGE;  // D3DTADDRESS_CLAMP
            default: return GL_REPEAT;         // D3DTADDRESS_WRAP
        }
    }
    static GLenum D3DFilterToGL(DWORD d3dFilter, bool isMag) {
        switch (d3dFilter) {
            case 1: return GL_NEAREST;  // D3DTEXF_POINT
            case 2: return GL_LINEAR;   // D3DTEXF_LINEAR
            default: return isMag ? GL_LINEAR : GL_LINEAR; // ANISOTROPIC/QUINCUNX/GAUSSIAN → linear
        }
    }
    HRESULT SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value) {
        if (stage == 0) {
            switch (type) {
                case D3DTSS_COLOROP:   m_tss0ColorOp   = value; break;
                case D3DTSS_COLORARG1: m_tss0ColorArg1  = value; break;
                case D3DTSS_COLORARG2: m_tss0ColorArg2  = value; break;
                case D3DTSS_ALPHAOP:   m_tss0AlphaOp   = value; break;
                case D3DTSS_ALPHAARG1: m_tss0AlphaArg1  = value; break;
                case D3DTSS_ALPHAARG2: m_tss0AlphaArg2  = value; break;
                case D3DTSS_ADDRESSU:
                    glActiveTexture(GL_TEXTURE0);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, D3DWrapToGL(value));
                    break;
                case D3DTSS_ADDRESSV:
                    glActiveTexture(GL_TEXTURE0);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, D3DWrapToGL(value));
                    break;
                case D3DTSS_MINFILTER:
                    glActiveTexture(GL_TEXTURE0);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, D3DFilterToGL(value, false));
                    break;
                case D3DTSS_MAGFILTER:
                    glActiveTexture(GL_TEXTURE0);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, D3DFilterToGL(value, true));
                    break;
                default: break;
            }
        } else if (stage == 1) {
            switch (type) {
                case D3DTSS_COLOROP:   m_tss1ColorOp   = value; break;
                case D3DTSS_COLORARG1: m_tss1ColorArg1  = value; break;
                case D3DTSS_COLORARG2: m_tss1ColorArg2  = value; break;
                case D3DTSS_ALPHAOP:   m_tss1AlphaOp   = value; break;
                case D3DTSS_ALPHAARG1: m_tss1AlphaArg1  = value; break;
                case D3DTSS_ALPHAARG2: m_tss1AlphaArg2  = value; break;
                default: break;
            }
        }
        return S_OK;
    }
    HRESULT GetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD* pValue) { *pValue = 0; return S_OK; }
    HRESULT SetTexture(DWORD stage, IDirect3DTexture8* tex) {
        if (stage < 4) m_textures[stage] = tex;
        return S_OK;
    }
    HRESULT SetMaterial(D3DMATERIAL8* mat) {
        if (mat) {
            BYTE a = (BYTE)(mat->Diffuse.a * 255.0f);
            BYTE r = (BYTE)(mat->Diffuse.r * 255.0f);
            BYTE g = (BYTE)(mat->Diffuse.g * 255.0f);
            BYTE b = (BYTE)(mat->Diffuse.b * 255.0f);
            m_matDiffuse = (a << 24) | (r << 16) | (g << 8) | b;
        }
        return S_OK;
    }
    HRESULT SetTransform(D3DTRANSFORMSTATETYPE type, D3DMATRIX* mat) {
        if (type == D3DTS_WORLD) { m_matWorld = *(D3DXMATRIX*)mat; m_wvpDirty = true; }
        else if (type == D3DTS_VIEW) { m_matView = *(D3DXMATRIX*)mat; m_wvpDirty = true; }
        else if (type == D3DTS_PROJECTION) { m_matProj = *(D3DXMATRIX*)mat; m_wvpDirty = true; }
        return S_OK;
    }
    HRESULT GetTransform(D3DTRANSFORMSTATETYPE type, D3DMATRIX* mat) {
        if (type == D3DTS_WORLD) *mat = m_matWorld;
        else if (type == D3DTS_VIEW) *mat = m_matView;
        else if (type == D3DTS_PROJECTION) *mat = m_matProj;
        else D3DXMatrixIdentity((D3DXMATRIX*)mat);
        return S_OK;
    }
    HRESULT SetLight(DWORD index, D3DLIGHT8* light) { return S_OK; }
    HRESULT LightEnable(DWORD index, BOOL enable) { return S_OK; }

    HRESULT Clear(DWORD count, const void* rects, DWORD flags, D3DCOLOR color, float z, DWORD stencil) {
        GLbitfield mask = 0;
        if (flags & D3DCLEAR_TARGET) {
            float r = ((color >> 16) & 0xFF) / 255.0f;
            float g = ((color >> 8) & 0xFF) / 255.0f;
            float b = (color & 0xFF) / 255.0f;
            float a = ((color >> 24) & 0xFF) / 255.0f;
            glClearColor(r, g, b, a);
            mask |= GL_COLOR_BUFFER_BIT;
        }
        if (flags & D3DCLEAR_ZBUFFER) {
            glClearDepth(z);
            // Ensure depth writes are enabled for the clear
            glDepthMask(GL_TRUE);
            mask |= GL_DEPTH_BUFFER_BIT;
        }
        if (mask) glClear(mask);
        // Restore depth write state
        if (flags & D3DCLEAR_ZBUFFER)
            glDepthMask(m_zWriteEnable ? GL_TRUE : GL_FALSE);
        return S_OK;
    }

    int m_frameDrawCalls;  // per-frame draw call counter
    int m_frameNumber;
    HRESULT BeginScene() { InspectorBeginFrame(); m_frameDrawCalls = 0; return S_OK; }
    HRESULT EndScene() {
        m_frameNumber++;
        return S_OK;
    }

    // Pre-swap callback for overlays (set by main loop)
    typedef void (*PreSwapCallback)(void);
    static PreSwapCallback s_preSwapCB;

    HRESULT Present(const void* src, const void* dst, void* wnd, const void* dirty) {
        // Run inspector hit test before swapping
        if (m_inspectorEnabled && m_drawRecordCount > 0) {
            m_inspectorHitID = InspectorHitTest(m_mouseX, m_mouseY);
        }
        // Main loop handles overlay rendering and swap after Draw()
        return S_OK;
    }

    HRESULT SetVertexShader(DWORD handle) {
        m_envMapMode = false;  // Reset per-material; reflection sets c48 after this
        if (handle & 0x80000000) {
            // Effect shader sentinel from GetEffectShader(): high bit = effect active,
            // low bits = real FVF for vertex attribute layout
            m_fvf = handle & 0x7FFFFFFF;
            m_effectShaderActive = true;
        } else if (handle & (D3DFVF_XYZ | D3DFVF_XYZRHW)) {
            m_fvf = handle;
            m_effectShaderActive = false;
        } else {
            m_effectShaderActive = true;
        }
        return S_OK;
    }
    HRESULT SetVertexShaderConstant(DWORD reg, const void* data, DWORD count) {
        const float* f = (const float*)data;
        for (DWORD i = 0; i < count; i++) {
            DWORD r = reg + i;
            if (r >= 5 && r <= 8) { memcpy(m_normalInv + (r-5)*4, f + i*4, 16); } // c5-c8: WV^-1
            if (r == 15) memcpy(m_falloffFront, f + i*4, 16);
            if (r == 16) memcpy(m_falloffDelta, f + i*4, 16);
            if (r == 48) m_envMapMode = true;
        }
        return S_OK;
    }

    IDirect3DVertexBuffer8* m_streamVB;
    IDirect3DIndexBuffer8* m_streamIB;
    UINT m_streamStride;
    UINT m_baseVertex;

    HRESULT SetStreamSource(UINT stream, IDirect3DVertexBuffer8* vb, UINT stride) {
        if (stream == 0) { m_streamVB = vb; m_streamStride = stride; }
        return S_OK;
    }
    HRESULT SetIndices(IDirect3DIndexBuffer8* ib, UINT baseVertex) {
        m_streamIB = ib; m_baseVertex = baseVertex;
        return S_OK;
    }

    // FVF layout helper
    struct FVFLayout {
        UINT posSize;
        bool hasRHW, hasNormal, hasPackedNormal, hasDiffuse, hasSpecular;
        int texCount;
        UINT normalOff, colorOff, specOff, texOff;
    };
    FVFLayout GetFVFLayout(DWORD fvf) {
        FVFLayout l = {};
        l.hasRHW = (fvf & D3DFVF_XYZRHW) != 0;
        l.posSize = l.hasRHW ? 16 : 12;
        l.hasNormal = (fvf & D3DFVF_NORMAL) != 0;
        l.hasPackedNormal = (fvf & D3DFVF_NORMPACKED3) != 0;
        l.hasDiffuse = (fvf & D3DFVF_DIFFUSE) != 0;
        l.hasSpecular = (fvf & D3DFVF_SPECULAR) != 0;
        l.texCount = (fvf >> 8) & 0xF;
        UINT off = l.posSize;
        l.normalOff = off;
        if (l.hasNormal) off += 12;
        else if (l.hasPackedNormal) off += 4;
        l.colorOff = off; if (l.hasDiffuse) off += 4;
        l.specOff = off; if (l.hasSpecular) off += 4;
        l.texOff = off;
        return l;
    }

    // Set GL uniforms for the current TSS/material/falloff state before a draw call
    void SetupGLUniforms(const FVFLayout& l) {
        // Ensure our shader program and VAO are active (ImGui uses its own)
        glUseProgram(g_gl.program);
        glBindVertexArray(g_gl.vao);

        UpdateWVP();

        // Upload matrices (D3D row-major, uploaded without transpose → GL sees transposed)
        glUniformMatrix4fv(g_gl.u_WVP, 1, GL_FALSE, &m_matWVP.m[0][0]);
        glUniformMatrix4fv(g_gl.u_WorldView, 1, GL_FALSE, &m_matWV.m[0][0]);

        // Falloff constants
        glUniform4fv(g_gl.u_FalloffFront, 1, m_falloffFront);
        glUniform4fv(g_gl.u_FalloffDelta, 1, m_falloffDelta);
        glUniformMatrix4fv(g_gl.u_NormalInv, 1, GL_TRUE, m_normalInv);

        // TFACTOR as float4 (ARGB → RGBA)
        float tf[4] = {
            ((m_texFactor >> 16) & 0xFF) / 255.0f,
            ((m_texFactor >> 8) & 0xFF) / 255.0f,
            (m_texFactor & 0xFF) / 255.0f,
            ((m_texFactor >> 24) & 0xFF) / 255.0f
        };
        glUniform4fv(g_gl.u_TFactor, 1, tf);

        // Material diffuse as float4
        float md[4] = {
            ((m_matDiffuse >> 16) & 0xFF) / 255.0f,
            ((m_matDiffuse >> 8) & 0xFF) / 255.0f,
            (m_matDiffuse & 0xFF) / 255.0f,
            ((m_matDiffuse >> 24) & 0xFF) / 255.0f
        };
        glUniform4fv(g_gl.u_MatDiffuse, 1, md);

        // Vertex mode
        glUniform1i(g_gl.u_VertexMode, l.hasRHW ? 1 : 0);

        // Normal type
        bool hasNrm = l.hasNormal || l.hasPackedNormal;
        int nrmType = l.hasNormal ? 1 : (l.hasPackedNormal ? 2 : 0);
        glUniform1i(g_gl.u_NormalType, nrmType);
        glUniform1i(g_gl.u_EnvMapMode, m_envMapMode ? 1 : 0);


        // Check what textures are available
        bool tex0Valid = m_textures[0] && m_textures[0]->m_glTexture;
        bool tex1Valid = m_textures[1] && m_textures[1]->m_glTexture;

        // Check if TSS stage 0 references a texture in any arg
        bool stageRefsTex = (m_tss0ColorArg1 == D3DTA_TEXTURE || m_tss0ColorArg2 == D3DTA_TEXTURE ||
                             m_tss0AlphaArg1 == D3DTA_TEXTURE || m_tss0AlphaArg2 == D3DTA_TEXTURE);
        bool hasTex0 = stageRefsTex && tex0Valid;


        // Bind stage 0 texture
        glActiveTexture(GL_TEXTURE0);
        if (hasTex0) {
            glBindTexture(GL_TEXTURE_2D, m_textures[0]->m_glTexture);
        } else if (tex0Valid && m_tss0ColorOp == D3DTOP_DISABLE && m_tss0AlphaOp == D3DTOP_DISABLE) {
            // Texture bound but TSS disabled — don't use it (falloff materials)
            glBindTexture(GL_TEXTURE_2D, g_gl.whiteTex);
        } else {
            glBindTexture(GL_TEXTURE_2D, g_gl.whiteTex);
        }

        // Bind stage 1 texture
        bool hasTex1 = false;
        glActiveTexture(GL_TEXTURE1);
        if (tex1Valid && m_tss1AlphaOp != D3DTOP_DISABLE) {
            glBindTexture(GL_TEXTURE_2D, m_textures[1]->m_glTexture);
            hasTex1 = true;
        } else {
            glBindTexture(GL_TEXTURE_2D, g_gl.whiteTex);
        }
        glActiveTexture(GL_TEXTURE0);
        glUniform1i(g_gl.u_HasTex1, hasTex1 ? 1 : 0);
        // Stage 1: if it references TEXTURE in alpha args, multiply
        int tex1AlphaOp = 0;
        if (hasTex1 && m_tss1AlphaOp == D3DTOP_MODULATE &&
            (m_tss1AlphaArg1 == D3DTA_TEXTURE || m_tss1AlphaArg2 == D3DTA_TEXTURE)) {
            tex1AlphaOp = 1; // multiply alpha by tex1.a
        }
        glUniform1i(g_gl.u_Tex1AlphaOp, tex1AlphaOp);
        // Stage 1: D3DTOP_ADD for color (additive glow/lighting)
        int tex1ColorOp = 0;
        if (hasTex1 && m_tss1ColorOp == D3DTOP_ADD &&
            (m_tss1ColorArg1 == D3DTA_TEXTURE || m_tss1ColorArg2 == D3DTA_TEXTURE)) {
            tex1ColorOp = 1; // add tex1.rgb to color
        }
        glUniform1i(g_gl.u_Tex1ColorOp, tex1ColorOp);

        // --- Determine vertex color source (computed in vertex shader) ---
        int colorSource = 3; // default: white
        int alphaSource = 0; // default: from color
        float alphaMul = 1.0f;

        if (m_tss0ColorOp == D3DTOP_DISABLE) {
            // No TSS color processing — use falloff or material diffuse
            colorSource = (hasNrm || m_effectShaderActive) ? 0 : 4;
        } else if (m_tss0ColorOp == D3DTOP_SELECTARG1) {
            if (m_tss0ColorArg1 == D3DTA_TFACTOR) colorSource = 2;
            else if (m_tss0ColorArg1 == D3DTA_TEXTURE) colorSource = 3; // white (texture in fragment)
            else if (m_tss0ColorArg1 == D3DTA_DIFFUSE) colorSource = (hasNrm || m_effectShaderActive) ? 0 : (l.hasDiffuse ? 1 : 4);
            else colorSource = 3;
        } else if (m_tss0ColorOp == D3DTOP_SELECTARG2) {
            if (m_tss0ColorArg2 == D3DTA_TFACTOR) colorSource = 2;
            else if (m_tss0ColorArg2 == D3DTA_TEXTURE) colorSource = 3;
            else if (m_tss0ColorArg2 == D3DTA_DIFFUSE) colorSource = (hasNrm || m_effectShaderActive) ? 0 : (l.hasDiffuse ? 1 : 4);
            else colorSource = 3;
        } else if (m_tss0ColorOp == D3DTOP_MODULATE) {
            // For MODULATE: one arg is in vertex shader, texture modulated in fragment
            DWORD nonTexArg = (m_tss0ColorArg1 == D3DTA_TEXTURE) ? m_tss0ColorArg2 : m_tss0ColorArg1;
            if (nonTexArg == D3DTA_DIFFUSE) colorSource = (hasNrm || m_effectShaderActive) ? 0 : (l.hasDiffuse ? 1 : 4);
            else if (nonTexArg == D3DTA_TFACTOR) colorSource = 2;
            else colorSource = 3;
        }

        // --- Determine vertex alpha source ---
        if (m_tss0AlphaOp == D3DTOP_DISABLE) {
            alphaSource = 0; // from color source
        } else if (m_tss0AlphaOp == D3DTOP_SELECTARG1) {
            if (m_tss0AlphaArg1 == D3DTA_TFACTOR) alphaSource = 1;
            else if (m_tss0AlphaArg1 == D3DTA_TEXTURE) alphaSource = 3; // opaque (texture provides in frag)
            else if (m_tss0AlphaArg1 == D3DTA_DIFFUSE) alphaSource = (hasNrm || m_effectShaderActive) ? 0 : 2;
            else alphaSource = 3;
        } else if (m_tss0AlphaOp == D3DTOP_SELECTARG2) {
            if (m_tss0AlphaArg2 == D3DTA_TFACTOR) alphaSource = 1;
            else if (m_tss0AlphaArg2 == D3DTA_TEXTURE) alphaSource = 3;
            else if (m_tss0AlphaArg2 == D3DTA_DIFFUSE) alphaSource = (hasNrm || m_effectShaderActive) ? 0 : 2;
            else alphaSource = 3;
        } else if (m_tss0AlphaOp == D3DTOP_MODULATE) {
            // MODULATE: pick the non-texture arg for vertex alpha; texture part in fragment
            bool arg1IsTex = (m_tss0AlphaArg1 == D3DTA_TEXTURE);
            bool arg2IsTex = (m_tss0AlphaArg2 == D3DTA_TEXTURE);
            if (arg1IsTex || arg2IsTex) {
                DWORD nonTexAlphaArg = arg1IsTex ? m_tss0AlphaArg2 : m_tss0AlphaArg1;
                if (nonTexAlphaArg == D3DTA_TFACTOR) alphaSource = 1;
                else if (nonTexAlphaArg == D3DTA_DIFFUSE) alphaSource = (hasNrm || m_effectShaderActive) ? 0 : 2;
                else alphaSource = 0;
            } else {
                // Both non-texture args (e.g. DIFFUSE × TFACTOR)
                // Use TFACTOR for vertex alpha, DIFFUSE multiplied as alphaMul
                if ((m_tss0AlphaArg1 == D3DTA_DIFFUSE && m_tss0AlphaArg2 == D3DTA_TFACTOR) ||
                    (m_tss0AlphaArg1 == D3DTA_TFACTOR && m_tss0AlphaArg2 == D3DTA_DIFFUSE)) {
                    alphaSource = 1; // tfactor alpha
                    // For non-shader meshes, diffuse alpha is typically 1.0 (white default), so this is fine
                } else {
                    alphaSource = 0;
                }
            }
        }

        // Stage 1 alpha modulation by TFACTOR (CMaskTextureMatInfo stage 2 pattern)
        if (m_tss1AlphaOp == D3DTOP_MODULATE && !hasTex1) {
            DWORD s1arg = (m_tss1AlphaArg1 == D3DTA_CURRENT) ? m_tss1AlphaArg2 : m_tss1AlphaArg1;
            if (s1arg == D3DTA_TFACTOR) {
                alphaMul = ((m_texFactor >> 24) & 0xFF) / 255.0f;
            }
        }

        glUniform1i(g_gl.u_ColorSource, colorSource);
        glUniform1i(g_gl.u_AlphaSource, alphaSource);
        glUniform1f(g_gl.u_AlphaMul, alphaMul);

        // --- Determine fragment color/alpha operations ---
        // fragColorOp: 0=v_Color, 1=texture, 2=v_Color×texture, 3=v_Color+texture (ADD)
        // fragAlphaOp: 0=v_Color.a, 1=tex.a, 2=v_Color.a×tex.a
        int fragColorOp = 0; // default: vertex color only
        int fragAlphaOp = 0; // default: vertex alpha only

        if (m_tss0ColorOp == D3DTOP_DISABLE) {
            fragColorOp = 0; // pure vertex (falloff/solid)
        } else if (m_tss0ColorOp == D3DTOP_SELECTARG1) {
            if (m_tss0ColorArg1 == D3DTA_TEXTURE && hasTex0) fragColorOp = 1; // texture only
            else fragColorOp = 0; // vertex only (TFACTOR/DIFFUSE already in vertex color)
        } else if (m_tss0ColorOp == D3DTOP_SELECTARG2) {
            if (m_tss0ColorArg2 == D3DTA_TEXTURE && hasTex0) fragColorOp = 1;
            else fragColorOp = 0;
        } else if (m_tss0ColorOp == D3DTOP_MODULATE) {
            if (hasTex0) fragColorOp = 2; // vertex × texture
            else fragColorOp = 0;
        } else if (m_tss0ColorOp == D3DTOP_ADD) {
            if (hasTex0) fragColorOp = 3; // vertex + texture (clamped)
            else fragColorOp = 0;
        }

        if (m_tss0AlphaOp == D3DTOP_DISABLE) {
            fragAlphaOp = 0; // pure vertex alpha
        } else if (m_tss0AlphaOp == D3DTOP_SELECTARG1) {
            if (m_tss0AlphaArg1 == D3DTA_TEXTURE && hasTex0) fragAlphaOp = 1; // texture alpha only
            else fragAlphaOp = 0; // vertex alpha
        } else if (m_tss0AlphaOp == D3DTOP_SELECTARG2) {
            if (m_tss0AlphaArg2 == D3DTA_TEXTURE && hasTex0) fragAlphaOp = 1;
            else fragAlphaOp = 0;
        } else if (m_tss0AlphaOp == D3DTOP_MODULATE) {
            bool alphaRefsTex = (m_tss0AlphaArg1 == D3DTA_TEXTURE || m_tss0AlphaArg2 == D3DTA_TEXTURE);
            if (alphaRefsTex && hasTex0) fragAlphaOp = 2; // vertex × texture
            else fragAlphaOp = 0; // vertex only
        }

        glUniform1i(g_gl.u_FragColorOp, fragColorOp);
        glUniform1i(g_gl.u_FragAlphaOp, fragAlphaOp);

        // Alpha test
        glUniform1f(g_gl.u_AlphaRef, m_alphaTestEnabled ? m_alphaRef : 0.0f);
    }

    // Set up vertex attribute pointers for the given FVF layout
    // bufferOffset = byte offset into the currently bound VBO
    void SetupVertexAttribs(const FVFLayout& l, UINT stride, size_t bufferOffset) {
        // Position (always at offset 0 from vertex start)
        int posComps = l.hasRHW ? 4 : 3;
        glEnableVertexAttribArray(g_gl.a_Position);
        glVertexAttribPointer(g_gl.a_Position, posComps, GL_FLOAT, GL_FALSE, stride, (void*)bufferOffset);

        // Normal (float3)
        if (l.hasNormal) {
            glEnableVertexAttribArray(g_gl.a_Normal);
            glVertexAttribPointer(g_gl.a_Normal, 3, GL_FLOAT, GL_FALSE, stride, (void*)(bufferOffset + l.normalOff));
        } else {
            glDisableVertexAttribArray(g_gl.a_Normal);
            glVertexAttrib3f(g_gl.a_Normal, 0.0f, 0.0f, 1.0f);
        }

        // Packed normal (uint)
        if (l.hasPackedNormal && g_gl.a_PackedNrm >= 0) {
            glEnableVertexAttribArray(g_gl.a_PackedNrm);
            glVertexAttribIPointer(g_gl.a_PackedNrm, 1, GL_UNSIGNED_INT, stride, (void*)(bufferOffset + l.normalOff));
        } else if (g_gl.a_PackedNrm >= 0) {
            glDisableVertexAttribArray(g_gl.a_PackedNrm);
            glVertexAttribI1ui(g_gl.a_PackedNrm, 0);
        }

        // Diffuse color (BGRA byte4 → normalized float4)
        // D3DCOLOR is ARGB in memory: byte order is BGRA on little-endian
        if (l.hasDiffuse) {
            glEnableVertexAttribArray(g_gl.a_Diffuse);
            glVertexAttribPointer(g_gl.a_Diffuse, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)(bufferOffset + l.colorOff));
        } else {
            glDisableVertexAttribArray(g_gl.a_Diffuse);
            glVertexAttrib4f(g_gl.a_Diffuse, 1.0f, 1.0f, 1.0f, 1.0f);
        }

        // Texcoord
        if (l.texCount > 0) {
            glEnableVertexAttribArray(g_gl.a_TexCoord);
            glVertexAttribPointer(g_gl.a_TexCoord, 2, GL_FLOAT, GL_FALSE, stride, (void*)(bufferOffset + l.texOff));
        } else {
            glDisableVertexAttribArray(g_gl.a_TexCoord);
            glVertexAttrib2f(g_gl.a_TexCoord, 0.0f, 0.0f);
        }
    }

    // Expand index buffer for triangle strips/fans into triangle list
    // Returns allocated index array (caller must free) and sets outCount
    GLuint* ExpandIndices(DWORD type, UINT primCount, const WORD* srcIdx, UINT minIdx, UINT& outCount) {
        if (type == D3DPT_TRIANGLELIST) {
            outCount = primCount * 3;
            GLuint* idx = (GLuint*)malloc(outCount * sizeof(GLuint));
            for (UINT i = 0; i < outCount; i++) idx[i] = (GLuint)(srcIdx[i] - minIdx);
            return idx;
        } else if (type == D3DPT_TRIANGLESTRIP) {
            outCount = primCount * 3;
            GLuint* idx = (GLuint*)malloc(outCount * sizeof(GLuint));
            UINT n = 0;
            for (UINT i = 0; i < primCount; i++) {
                GLuint i0 = srcIdx[i] - minIdx, i1 = srcIdx[i+1] - minIdx, i2 = srcIdx[i+2] - minIdx;
                if (i % 2 == 0) { idx[n++]=i0; idx[n++]=i1; idx[n++]=i2; }
                else { idx[n++]=i1; idx[n++]=i0; idx[n++]=i2; }
            }
            outCount = n;
            return idx;
        } else if (type == D3DPT_TRIANGLEFAN) {
            outCount = primCount * 3;
            GLuint* idx = (GLuint*)malloc(outCount * sizeof(GLuint));
            UINT n = 0;
            GLuint i0 = srcIdx[0] - minIdx;
            for (UINT i = 0; i < primCount; i++) {
                idx[n++]=i0; idx[n++]=srcIdx[i+1]-minIdx; idx[n++]=srcIdx[i+2]-minIdx;
            }
            outCount = n;
            return idx;
        }
        outCount = 0;
        return NULL;
    }

    HRESULT DrawIndexedPrimitive(DWORD type, UINT minIdx, UINT numVerts, UINT startIdx, UINT primCount) {
        if (!m_streamVB || !m_streamVB->m_data || !m_streamIB || !m_streamIB->m_data) return S_OK;
        if (primCount == 0 || numVerts == 0) return S_OK;
        // GL buffers are lazily created in EnsureUploaded, no need to check here
        if (m_streamStride == 0) return S_OK;

        FVFLayout l = GetFVFLayout(m_fvf);

        // Upload VBO/IBO data to GL if dirty
        m_streamVB->EnsureUploaded();
        m_streamIB->EnsureUploaded();

        // Set uniforms
        SetupGLUniforms(l);

        // Inspector: record draw call with screen AABB
        InspectorRecordDraw(numVerts, m_streamVB->m_data, m_streamStride, l.hasRHW);

        // Bind VBO and set vertex attributes
        glBindBuffer(GL_ARRAY_BUFFER, m_streamVB->m_glBuffer);
        size_t vbOffset = (size_t)m_baseVertex * m_streamStride;
        SetupVertexAttribs(l, m_streamStride, vbOffset);

        // Build index array (expanding strips/fans to triangles)
        const WORD* srcIdx = (const WORD*)m_streamIB->m_data + startIdx;
        UINT idxCount = 0;
        GLuint* triIdx = ExpandIndices(type, primCount, srcIdx, minIdx, idxCount);
        if (!triIdx || idxCount == 0) { free(triIdx); return S_OK; }

        // Upload indices to dynamic IBO and draw
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_gl.dynamicIBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, idxCount * sizeof(GLuint), triIdx, GL_STREAM_DRAW);
        // Clear any accumulated GL errors before draw
        while (glGetError() != GL_NO_ERROR) {}
        glDrawElements(GL_TRIANGLES, idxCount, GL_UNSIGNED_INT, 0);
        free(triIdx);
        m_frameDrawCalls++;

        return S_OK;
    }

    HRESULT DrawPrimitive(DWORD type, UINT start, UINT count) {
        if (!m_streamVB || !m_streamVB->m_data) return S_OK;
        const void* data = (const BYTE*)m_streamVB->m_data + start * m_streamStride;
        return DrawPrimitiveUP(type, count, data, m_streamStride);
    }

    HRESULT DrawPrimitiveUP(DWORD type, UINT primCount, const void* data, UINT stride) {
        if (!data || primCount == 0) return S_OK;
        FVFLayout l = GetFVFLayout(m_fvf);

        UINT vertCount = 0;
        if (type == D3DPT_TRIANGLELIST) vertCount = primCount * 3;
        else if (type == D3DPT_TRIANGLESTRIP) vertCount = primCount + 2;
        else if (type == D3DPT_TRIANGLEFAN) vertCount = primCount + 2;
        else if (type == D3DPT_QUADLIST) vertCount = primCount * 4;
        else return S_OK;

        SetupGLUniforms(l);
        InspectorRecordDraw(vertCount, data, stride, l.hasRHW);

        // Upload vertex data to dynamic VBO
        glBindBuffer(GL_ARRAY_BUFFER, g_gl.dynamicVBO);
        glBufferData(GL_ARRAY_BUFFER, vertCount * stride, data, GL_STREAM_DRAW);
        SetupVertexAttribs(l, stride, 0);

        if (type == D3DPT_TRIANGLELIST) {
            glDrawArrays(GL_TRIANGLES, 0, vertCount);
        } else if (type == D3DPT_TRIANGLESTRIP) {
            glDrawArrays(GL_TRIANGLE_STRIP, 0, vertCount);
        } else if (type == D3DPT_TRIANGLEFAN) {
            glDrawArrays(GL_TRIANGLE_FAN, 0, vertCount);
        } else if (type == D3DPT_QUADLIST) {
            // Convert quads to triangles
            UINT triCount = primCount * 2;
            GLuint* idx = (GLuint*)malloc(triCount * 3 * sizeof(GLuint));
            UINT n = 0;
            for (UINT q = 0; q < primCount; q++) {
                UINT b = q * 4;
                idx[n++]=b; idx[n++]=b+1; idx[n++]=b+2;
                idx[n++]=b; idx[n++]=b+2; idx[n++]=b+3;
            }
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_gl.dynamicIBO);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, n * sizeof(GLuint), idx, GL_STREAM_DRAW);
            glDrawElements(GL_TRIANGLES, n, GL_UNSIGNED_INT, 0);
            free(idx);
        }
        m_frameDrawCalls++;
        return S_OK;
    }

    HRESULT DrawIndexedPrimitiveUP(DWORD type, UINT minIdx, UINT numVerts, UINT primCount, const void* indexData, DWORD indexFormat, const void* vertexData, UINT vertexStride) {
        if (!indexData || !vertexData || primCount == 0 || numVerts == 0) return S_OK;
        FVFLayout l = GetFVFLayout(m_fvf);

        SetupGLUniforms(l);

        // Upload vertex data
        glBindBuffer(GL_ARRAY_BUFFER, g_gl.dynamicVBO);
        glBufferData(GL_ARRAY_BUFFER, numVerts * vertexStride, vertexData, GL_STREAM_DRAW);
        SetupVertexAttribs(l, vertexStride, 0);

        // Expand indices
        const WORD* srcIdx = (const WORD*)indexData;
        UINT idxCount = 0;
        GLuint* triIdx = ExpandIndices(type, primCount, srcIdx, minIdx, idxCount);
        if (!triIdx || idxCount == 0) { free(triIdx); return S_OK; }

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_gl.dynamicIBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, idxCount * sizeof(GLuint), triIdx, GL_STREAM_DRAW);
        glDrawElements(GL_TRIANGLES, idxCount, GL_UNSIGNED_INT, 0);
        free(triIdx);
        m_frameDrawCalls++;
        return S_OK;
    }

    HRESULT CreateTexture(UINT w, UINT h, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DTexture8** pp) {
        IDirect3DTexture8* tex = new IDirect3DTexture8();
        tex->m_width = w; tex->m_height = h;
        tex->m_pitch = w * 4;
        tex->m_pixels = (BYTE*)calloc(w * h, 4);
        // Create GL texture immediately
        if (w > 0 && h > 0) {
            glActiveTexture(GL_TEXTURE0); // ensure we create on unit 0
            glGenTextures(1, &tex->m_glTexture);
            glBindTexture(GL_TEXTURE_2D, tex->m_glTexture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, tex->m_pixels);
        }
        *pp = tex; return S_OK;
    }
    HRESULT CreateVertexBuffer(UINT length, DWORD usage, DWORD fvf, D3DPOOL pool, IDirect3DVertexBuffer8** pp) { *pp = new IDirect3DVertexBuffer8(length); return S_OK; }
    HRESULT CreateIndexBuffer(UINT length, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DIndexBuffer8** pp) { *pp = new IDirect3DIndexBuffer8(length); return S_OK; }
    HRESULT CreateVertexShader(const DWORD* decl, const DWORD* func, DWORD* handle, DWORD usage) { *handle = 0; return S_OK; }
    HRESULT GetBackBuffer(INT idx, DWORD type, IDirect3DSurface8** pp) { *pp = new IDirect3DSurface8(); return S_OK; }
    HRESULT SetClipPlane(DWORD idx, D3DVALUE* eq) { return S_OK; }
};
typedef IDirect3DDevice8* LPDIRECT3DDEVICE8;

class IDirect3D8 {
    int m_ref;
public:
    IDirect3D8() : m_ref(1) {}
    ULONG AddRef() { return ++m_ref; }
    ULONG Release() { if(--m_ref <= 0) { delete this; return 0; } return m_ref; }
    HRESULT CreateDevice(UINT adapter, DWORD type, void* wnd, DWORD flags, D3DPRESENT_PARAMETERS* pp, IDirect3DDevice8** dev) {
        *dev = new IDirect3DDevice8();
        // Update XYZRHW viewport size uniform to match backbuffer
        if (pp && g_gl.program) {
            glUseProgram(g_gl.program);
            glUniform2f(g_gl.u_ViewportSize, (float)pp->BackBufferWidth, (float)pp->BackBufferHeight);
        }
        return S_OK;
    }
};
typedef IDirect3D8* LPDIRECT3D8;

inline IDirect3D8* Direct3DCreate8(UINT version) { return new IDirect3D8(); }

// ID3DXMatrixStack stub (Xbox D3DX utility)
class ID3DXMatrixStack {
    D3DXMATRIX m_stack[32];
    int m_top;
    int m_ref;
public:
    ID3DXMatrixStack() : m_top(0), m_ref(1) { D3DXMatrixIdentity(&m_stack[0]); }
    ULONG AddRef() { return ++m_ref; }
    ULONG Release() { if(--m_ref<=0){delete this;return 0;} return m_ref; }
    HRESULT LoadIdentity() { D3DXMatrixIdentity(&m_stack[m_top]); return S_OK; }
    HRESULT LoadMatrix(const D3DXMATRIX* m) { m_stack[m_top]=*m; return S_OK; }
    HRESULT MultMatrix(const D3DXMATRIX* m) { m_stack[m_top]=m_stack[m_top]*(*m); return S_OK; }
    HRESULT MultMatrixLocal(const D3DXMATRIX* m) { m_stack[m_top]=(*m)*m_stack[m_top]; return S_OK; }
    HRESULT Push() { if(m_top<31){m_stack[m_top+1]=m_stack[m_top];m_top++;} return S_OK; }
    HRESULT Pop() { if(m_top>0) m_top--; return S_OK; }
    D3DXMATRIX* GetTop() { return &m_stack[m_top]; }
    HRESULT RotateAxis(const D3DXVECTOR3* axis, float angle) { D3DXMATRIX r; D3DXMatrixRotationAxis(&r,axis,angle); m_stack[m_top]=m_stack[m_top]*r; return S_OK; }
    HRESULT RotateAxisLocal(const D3DXVECTOR3* axis, float angle) { D3DXMATRIX r; D3DXMatrixRotationAxis(&r,axis,angle); m_stack[m_top]=r*m_stack[m_top]; return S_OK; }
    HRESULT Scale(float x, float y, float z) { D3DXMATRIX s; D3DXMatrixScaling(&s,x,y,z); m_stack[m_top]=m_stack[m_top]*s; return S_OK; }
    HRESULT TranslateLocal(float x, float y, float z) { D3DXMATRIX t; D3DXMatrixTranslation(&t,x,y,z); m_stack[m_top]=t*m_stack[m_top]; return S_OK; }
    HRESULT Translate(float x, float y, float z) { D3DXMATRIX t; D3DXMatrixTranslation(&t,x,y,z); m_stack[m_top]=m_stack[m_top]*t; return S_OK; }
};
inline HRESULT D3DXCreateMatrixStack(DWORD flags, ID3DXMatrixStack** pp) { *pp = new ID3DXMatrixStack(); return S_OK; }

// D3DX texture creation stubs
#define D3DX_DEFAULT ((UINT)-1)
#define D3DX_DEFAULT_NONPOW2 ((UINT)-2)
#define D3DX_FILTER_NONE 1

// Forward-declare XBX parser (defined in xbx_texture.h, included by Image.cpp/xip.cpp)
IDirect3DTexture8* XBX_ParseTexture(const BYTE* pbContent, int cbContent);

inline HRESULT D3DXCreateTextureFromFileA(IDirect3DDevice8* dev, const char* path, IDirect3DTexture8** ppTex) {
    (void)dev;
    *ppTex = NULL;
    // Try to load the file from the xboxfs-mapped path
    FILE* f = fopen(path, "rb");
    if (!f) {
        // Try xboxfs mapping (Q:\path -> xboxfs/Q/path)
        char mapped[1024];
        if (path[0] && path[1] == ':' && (path[2] == '\\' || path[2] == '/')) {
            snprintf(mapped, sizeof(mapped), "xboxfs/%c/%s", path[0], path + 3);
            for (char* p = mapped; *p; p++) if (*p == '\\') *p = '/';
            f = fopen(mapped, "rb");
        }
    }
    if (!f) return E_FAIL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) { fclose(f); return E_FAIL; }
    BYTE* data = (BYTE*)malloc(size);
    fread(data, 1, size, f);
    fclose(f);
    // Try XBX parse (works for .xbx files)
    IDirect3DTexture8* tex = XBX_ParseTexture(data, (int)size);
    if (tex) { free(data); *ppTex = tex; return S_OK; }
    // Fallback: try stb_image for JPG/PNG/BMP
    {

        int w = 0, h = 0, ch = 0;
        unsigned char* pixels = stbi_load_from_memory(data, (int)size, &w, &h, &ch, 4);
        free(data);
        if (pixels && w > 0 && h > 0) {
            tex = new IDirect3DTexture8();
            glGenTextures(1, &tex->m_glTexture);
            glBindTexture(GL_TEXTURE_2D, tex->m_glTexture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            tex->m_width = w; tex->m_height = h;
            stbi_image_free(pixels);
            *ppTex = tex; return S_OK;
        }
    }
    return E_FAIL;
}

inline HRESULT D3DXCreateTextureFromFileInMemory(IDirect3DDevice8* dev, const void* data, UINT size, IDirect3DTexture8** ppTex) {
    (void)dev;
    *ppTex = NULL;
    if (!data || size == 0) return E_FAIL;
    IDirect3DTexture8* tex = XBX_ParseTexture((const BYTE*)data, (int)size);
    if (tex) { *ppTex = tex; return S_OK; }
    // Fallback: stb_image for JPG/PNG/BMP
    {

        int iw = 0, ih = 0, ch = 0;
        unsigned char* pixels = stbi_load_from_memory((const unsigned char*)data, (int)size, &iw, &ih, &ch, 4);
        if (pixels && iw > 0 && ih > 0) {
            tex = new IDirect3DTexture8();
            glGenTextures(1, &tex->m_glTexture);
            glBindTexture(GL_TEXTURE_2D, tex->m_glTexture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, iw, ih, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            tex->m_width = iw; tex->m_height = ih;
            stbi_image_free(pixels);
            *ppTex = tex; return S_OK;
        }
    }
    return E_FAIL;
}

inline HRESULT D3DXCreateTextureFromFileInMemoryEx(IDirect3DDevice8* dev, const void* data, UINT size,
    UINT w, UINT h, UINT mipLevels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, DWORD filter, DWORD mipFilter,
    D3DCOLOR colorKey, void* srcInfo, void* palette, IDirect3DTexture8** ppTex) {
    (void)dev;(void)w;(void)h;(void)mipLevels;(void)usage;(void)fmt;(void)pool;(void)filter;(void)mipFilter;(void)colorKey;(void)srcInfo;(void)palette;
    return D3DXCreateTextureFromFileInMemory(dev, data, size, ppTex);
}

inline HRESULT D3DXCreateTexture(IDirect3DDevice8* dev, UINT w, UINT h, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DTexture8** ppTex) {
    return dev->CreateTexture(w, h, levels, usage, fmt, pool, ppTex);
}

inline HRESULT D3DXCreateMeshFVF(DWORD numFaces, DWORD numVerts, DWORD opts, DWORD fvf, IDirect3DDevice8* dev, ID3DXMesh** ppMesh) {
    ID3DXMesh* mesh = new ID3DXMesh();
    mesh->Init(numFaces, numVerts, fvf, dev);
    *ppMesh = mesh;
    return S_OK;
}

// ID3DXMesh::DrawSubset - renders mesh through the device
inline HRESULT ID3DXMesh::DrawSubset(DWORD attr) {
    (void)attr;
    if (!m_dev || !m_vbData || !m_ibData || m_numFaces == 0 || m_numVerts == 0)
        return S_OK;
    m_dev->SetVertexShader(m_fvf);
    m_dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, m_numVerts, m_numFaces,
        m_ibData, D3DFMT_INDEX16, m_vbData, m_vertStride);
    return S_OK;
}

// Toggle the per-vertex alpha multiply path in the vertex shader. Used by
// text rendering (text.cpp) to make VerticalFade's per-vertex alpha clip
// survive falloff lighting (which would otherwise overwrite color.a). Off
// by default; callers must restore to 0 after their draw.
inline void TheseusSetVertexAlphaMul(BOOL enable) {
    glUniform1i(g_gl.u_VertexAlphaMul, enable ? 1 : 0);
}
