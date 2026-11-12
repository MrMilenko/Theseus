// math_node.cpp: the script Math object, a class with the usual constants
// and functions. Rebuilt from the 5960 retail XBE (0x32950..0x32ba0); see
// docs/decomp/Math.md.

#include "std.h"
#include "theseus.h"
#include "node.h"
#include "runner.h"

// One for the whole run; it ignores reference counting.
CMathClass g_Math;

// FND at 0x0017fb18; projectMEnabled and itoa are Theseus's.

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

// CMathClass::CMathClass()
// Reconstructed from 5960 retail XBE @ 0x00032950: registers the "Math"
// class and loads the eight constants from 0x0002c034..0x0002c04c. LOG2E
// and LOG10E are stored as 1.442 and 0.434 there, so they are here.
CMathClass::CMathClass() : CNodeClass(_T("Math"), 0, NULL, NULL, NULL)
{
	m_E       = 2.7182818f;
	m_LN2     = 0.6931472f;
	m_LN10    = 2.3025851f;
	m_LOG2E   = 1.442f;
	m_LOG10E  = 0.434f;
	m_PI      = 3.1415927f;
	m_SQRT1_2 = 0.70710677f;
	m_SQRT2   = 1.4142135f;
}

void CMathClass::AddRef()  {}
void CMathClass::Release() {}

// The functions, 5960 retail XBE @ 0x00032a20..0x00032ba0: each is the
// matching x87 instruction or CRT call on a float. Three details are the
// binary's: atan2 hands its arguments to the arctangent swapped, round is
// (int)(n + 0.5), and random is rand() over the CRT's top value.
float CMathClass::abs(float n)   { return fabsf(n); }
float CMathClass::acos(float n)  { return acosf(n); }
float CMathClass::asin(float n)  { return asinf(n); }
float CMathClass::atan(float n)  { return atanf(n); }
float CMathClass::ceil(float n)  { return ceilf(n); }
float CMathClass::cos(float n)   { return cosf(n); }
float CMathClass::exp(float n)   { return expf(n); }
float CMathClass::floor(float n) { return floorf(n); }
float CMathClass::log(float n)   { return logf(n); }
float CMathClass::sin(float n)   { return sinf(n); }
float CMathClass::sqrt(float n)  { return sqrtf(n); }
float CMathClass::tan(float n)   { return tanf(n); }

float CMathClass::atan2(float y, float x)         { return atan2f(x, y); }
float CMathClass::pow(float base, float exponent) { return powf(base, exponent); }

// @ 0x00032af0 / 0x00032b10: the second argument wins ties.
float CMathClass::max(float a, float b) { return b < a ? a : b; }
float CMathClass::min(float a, float b) { return a < b ? a : b; }

float CMathClass::random()       { return (float)rand() / (float)RAND_MAX; }
float CMathClass::round(float n) { return (float)(int)(n + 0.5f); }

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
