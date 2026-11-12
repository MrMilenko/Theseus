// math_node.cpp: CMathClass, the script's Math object. The 5960 part is
// rebuilt from the retail XBE (0x32950..0x32ba0); findings are in
// docs/decomp/Math.md.

#include "std.h"
#include "theseus.h"
#include "node.h"
#include "runner.h"

// The one Math object; the binary's lives in .data and is never freed.
CMathClass g_Math;

// FND table at 0x0017fb18: the 18 names and signatures script binds to.
// projectMEnabled and itoa are Theseus additions.
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
// Reconstructed from 5960 retail XBE @ 0x00032950. Ghidra decompile:
//     <CNodeClass "Math", no factory, no base, no props, linked into the list>
//     E = 2.7182817; LN2 = 0.6931472; LN10 = 2.3025851; LOG2E = 1.442;
//     LOG10E = 0.434; PI = 3.1415927; SQRT1_2 = 0.70710677; SQRT2 = 1.4142135;
// These are the float constants read out of 0x2c034..0x2c04c. LOG2E and
// LOG10E really are stored to three places in the binary.
CMathClass::CMathClass() : CNodeClass(_T("Math"), 0, NULL, NULL, NULL)
{
	m_E = 2.7182817f;
	m_LN2 = 0.6931472f;
	m_LN10 = 2.3025851f;
	m_LOG2E = 1.442f;
	m_LOG10E = 0.434f;
	m_PI = 3.1415927f;
	m_SQRT1_2 = 0.70710677f;
	m_SQRT2 = 1.4142135f;
}

// The static object can't be released; AddRef/Release do nothing.
void CMathClass::AddRef()
{
}

void CMathClass::Release()
{
}

// One argument wrappers, 0x32a20..0x32ba0 (sig 10, float in, float out).
// Each is a single x87 instruction or CRT call in the binary:
//     abs  fabs        acos  0x32a30 -> CRT acos   asin  0x32a40 -> CRT asin
//     atan 0x32a50     ceil  __ceil(double)        cos   fcos
//     exp  0x32aa0     floor __floor(double)       log   0x32ae0
//     sin  fsin        sqrt  fsqrt                 tan   fptan
// A float wrapper around each library function is the whole behavior.
float CMathClass::abs(float number)   { return fabsf(number); }
float CMathClass::acos(float number)  { return acosf(number); }
float CMathClass::asin(float number)  { return asinf(number); }
float CMathClass::atan(float number)  { return atanf(number); }
float CMathClass::ceil(float number)  { return ceilf(number); }
float CMathClass::cos(float number)   { return cosf(number); }
float CMathClass::exp(float number)   { return expf(number); }
float CMathClass::floor(float number) { return floorf(number); }
float CMathClass::log(float number)   { return logf(number); }
float CMathClass::sin(float number)   { return sinf(number); }
float CMathClass::sqrt(float number)  { return sqrtf(number); }
float CMathClass::tan(float number)   { return tanf(number); }

// CMathClass::atan2(y, x)   [script fn 'atan2', FND 0x0017fb78, sig 11]
// Reconstructed from 5960 retail XBE @ 0x00032a60. Ghidra decompile:
//     return fpatan(arg2, arg1);      // atan(arg2 / arg1)
// The binary feeds the second argument as the numerator, so script's
// atan2(a, b) is atan2f(b, a). Scripts are written against that.
float CMathClass::atan2(float y, float x)
{
	return atan2f(x, y);
}

// CMathClass::pow(base, exponent)   [script fn 'pow', FND 0x0017fc38, sig 11]
// Reconstructed from 5960 retail XBE @ 0x00032b30: a jump into the CRT pow.
float CMathClass::pow(float base, float exponent)
{
	return powf(base, exponent);
}

// CMathClass::max / min   [script fns 'max' 0x0017fc08, 'min' 0x0017fc20, sig 11]
// Reconstructed from 5960 retail XBE @ 0x00032af0 / 0x00032b10. Ghidra decompile:
//     max: if (b < a) return a; return b;
//     min: if (a < b) return a; return b;
float CMathClass::max(float number1, float number2)
{
	if (number2 < number1)
		return number1;
	return number2;
}

float CMathClass::min(float number1, float number2)
{
	if (number1 < number2)
		return number1;
	return number2;
}

// CMathClass::random()   [script fn 'random', FND 0x0017fc50, sig 9]
// Reconstructed from 5960 retail XBE @ 0x00032b40. Ghidra decompile:
//     return rand() * (1.0 / 32767);
// 32767 is the Xbox CRT's RAND_MAX; Theseus divides by the host's RAND_MAX
// so desktop stays in 0..1.
float CMathClass::random()
{
	return (float)rand() / (float)RAND_MAX;
}

// CMathClass::round(n)   [script fn 'round', FND 0x0017fc68, sig 10]
// Reconstructed from 5960 retail XBE @ 0x00032b60. Ghidra decompile:
//     return (float)(int)(n + 0.5);
// Adds a half and truncates toward zero, so negative halves round up.
float CMathClass::round(float number)
{
	int whole = (int)(number + 0.5f);
	return (float)whole;
}

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
