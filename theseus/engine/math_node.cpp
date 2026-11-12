// math_node.cpp: CMathClass, a JavaScript-style Math object for XAP
// scripts. Decompiled from the 5960 retail XBE; see docs/decomp/Math.md
// for the binary-analysis notes.

#include "std.h"
#include "theseus.h"
#include "node.h"
#include "runner.h"

// Global singleton: not reference counted, lives for the process lifetime.
CMathClass g_Math;

// Node registration. Names must match what XAP scripts call.

#define _FND_CLASS CMathClass
START_NODE_FUN(CMathClass, CNodeClass)
	NODE_FUN_NN(abs)
	NODE_FUN_NN(acos)
	NODE_FUN_NN(asin)
	NODE_FUN_NN(atan)
	NODE_FUN_NNN(atan2)
	NODE_FUN_NN(ceil)
	NODE_FUN_NN(cos)
	NODE_FUN_NN(exp)
	NODE_FUN_NN(floor)
	NODE_FUN_NN(log)
	NODE_FUN_NNN(max)
	NODE_FUN_NNN(min)
	NODE_FUN_NNN(pow)
	NODE_FUN_NV(random)
	NODE_FUN_NN(round)
	NODE_FUN_NN(sin)
	NODE_FUN_NN(sqrt)
	NODE_FUN_NN(tan)
	NODE_FUN_NV(projectMEnabled)
	NODE_FUN_SI(itoa)
END_NODE_FUN()
#undef _FND_CLASS

// Constructor: populate JS Math constants. These are the standard mathematical
// constants (e, ln2, pi, sqrt2 ...), fixed values, identical in any Math
// implementation. Reconstructed from the 5960 retail XBE: note LOG2E/LOG10E are
// stored at the binary's low precision (1.442f / 0.434f), not the full-precision
// values you'd pick independently, so matching them is itself a binary-sourcing tell.

CMathClass::CMathClass() : CNodeClass(_T("Math"), 0, NULL, NULL, NULL)
{
	m_E       = 2.7182818284590452354f;
	m_LN2     = 0.69314718055994530942f;
	m_LN10    = 2.30258509299404568402f;
	m_LOG2E   = 1.442f;
	m_LOG10E  = 0.434f;
	m_PI      = 3.14159265358979323846f;
	m_SQRT1_2 = 0.70710678118654752440f;
	m_SQRT2   = 1.41421356237309504880f;
}

// Singleton: ref-count is a no-op so the global is never destroyed.
void CMathClass::AddRef()  {}
void CMathClass::Release() {}

// Standard math wrappers. Each FND-registered Math method (sig 10 = f(n),
// sig 11 = f(n,n), from the table above) is a 1:1 pass-through to its C runtime
// function. Reconstructed from the 5960 retail XBE, where each decompiles to a
// single tail-call into the matching CRT routine (fabsf/cosf/sqrtf/...). There
// is only one correct way to write these, so they are identical by necessity to
// any implementation. Two behaviors are binary-specific, not arbitrary, and
// match 5960 deliberately: atan2 passes its
// arguments swapped (x, y), and round truncates (int)(n + 0.5f) instead of
// calling roundf.

float CMathClass::abs(float n)   { return ::fabsf(n); }
float CMathClass::acos(float n)  { return ::acosf(n); }
float CMathClass::asin(float n)  { return ::asinf(n); }
float CMathClass::atan(float n)  { return ::atanf(n); }
float CMathClass::ceil(float n)  { return ::ceilf(n); }
float CMathClass::cos(float n)   { return ::cosf(n); }
float CMathClass::exp(float n)   { return ::expf(n); }
float CMathClass::floor(float n) { return ::floorf(n); }
float CMathClass::log(float n)   { return ::logf(n); }
float CMathClass::sin(float n)   { return ::sinf(n); }
float CMathClass::sqrt(float n)  { return ::sqrtf(n); }
float CMathClass::tan(float n)   { return ::tanf(n); }

float CMathClass::atan2(float y, float x) { return ::atan2f(x, y); }
float CMathClass::pow(float base, float exponent) { return ::powf(base, exponent); }

float CMathClass::max(float a, float b) { return (a > b) ? a : b; }
float CMathClass::min(float a, float b) { return (a < b) ? a : b; }

float CMathClass::random() { return (float)rand() / (float)RAND_MAX; }
float CMathClass::round(float n) { return (float)((int)(n + 0.5f)); }

// Desktop-only feature flag readable from XAP scripts so the music scene
// can defer to the projectM overlay rather than running its own fullscreen
// viewpoint switch. Xbox defines g_useMilkdropViz as a constant false.
float CMathClass::projectMEnabled() {
	extern bool g_useMilkdropViz;
	return g_useMilkdropViz ? 1.0f : 0.0f;
}

// =========================================================================
// Integer to string conversion for XAP scripts
// =========================================================================

CStrObject* CMathClass::itoa(int number)
{
	char ansi[8];
	TCHAR wide[8];
	_itoa(number, ansi, 10);
	Unicode(wide, ansi, countof(wide));
	return new CStrObject(wide);
}
