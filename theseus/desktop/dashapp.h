// dashapp.h: desktop dashboard declarations. Counterpart to
// shared/theseus.h on Xbox; pulls the Theseus-style globals plus
// desktop-only helpers used by sdl_main / dashinit.

#include "settingsfile.h"
// Pull in the Theseus-style globals (g_pD3DDev, g_now, g_pScreen, etc.)
// up front. The DashApp* inlines below reference them and need the externs
// in scope at parse time. Once dashapp.h is removed entirely the include
// goes with it.
#include "theseus.h"
// Time values are stored as double for fractional precision over long uptimes.
typedef double XTIME;

#define MAX_BLOCKS_TO_SHOW 50000

#define CNode CTheseusNode
#define classCNode classCTheseusNode

#ifdef _MSC_VER
class __single_inheritance CObject;
class __single_inheritance CNode;
#else
class CObject;
class CNode;
#endif
class CClass;
class CInstance;
class CNodeArray;
class CScreen;
class CViewpoint;
class CNavigationInfo;
class CBackground;


// Theseus app interface (free functions + globals from shared/theseus.h).
// CDashApp class is gone. Desktop-only forward declarations below.

// Desktop-only globals
extern int g_nVertPerFrame;
extern int g_nTriPerFrame;
extern char* g_szAppTitle;

// Desktop-only free functions
int TheseusMessageBox(const char* szText, unsigned int uType = MB_OK);
void MakeAbsoluteURL(char* szBuf, const char* szBase, const char* szURL);
void MakeAbsoluteURL(char* szBuf, const char* szURL);
void UpdateCurDirFromFile(const char* szURL);
extern int __cdecl NewFailed(size_t nBytes);
extern void SetFalloffShaderFrameValues();
extern void CleanFilePath(char* szPath, const char* szSrcPath);

// App lifecycle (implementations in dashapp.cpp)
bool InitApp();
void CleanupApp();
void Advance();
void Draw();
bool InitD3D();
void ReleaseD3D();
HRESULT InitAudio();
void GetStartupClassFile(char* szFileToLoad);

#undef D3DLOCK_DISCARD
#define D3DLOCK_DISCARD 0



extern bool ResetScreenSaver();



#define D3DFVF_NORMPACKED3		0x20000000

extern DWORD CompressNormal(float* pvNormal);



extern DWORD GetFixedFunctionShader(DWORD fvf);
