// libc math under its usual names, so codec libraries (the bundled opus)
// link without the CRT. Everything lands on the customcrt implementations
// in customcrt_math.cpp.
//
// Compiled with /Oi- from build.bat, on purpose: with intrinsics enabled the
// compiler reserves these names and refuses to let anyone define them
// (C2169), #pragma function or not. The main build compiles with /Oi, so
// this file is excluded from the main loop and built on its own line.

#include "customcrt_math.h"

extern "C" {

float  sqrtf(float x) { return csqrtf(x); }
double sqrt(double x) { return csqrt(x); }
float  expf(float x) { return cexf(x); }
float  logf(float x) { return clogf(x); }
float  cosf(float x) { return ccosf(x); }
float  sinf(float x) { return csinf(x); }
float  tanf(float x) { return ctanf(x); }
float  powf(float x, float y) { return cpowf(x, y); }
double pow(double x, double y) { return cpow(x, y); }
float  floorf(float x) { return cfloorf(x); }
double floor(double x) { return cfloor(x); }
float  ceilf(float x) { return cceilf(x); }
double ceil(double x) { return cceil(x); }
float  roundf(float x) { return croundf(x); }
double round(double x) { return cround(x); }
float  fabsf(float x) { return cfabs(x); }
double fabs(double x) { return cfabs(x); }
float  atanf(float x) { return catanf(x); }
double atan(double x) { return catan(x); }
float  atan2f(float y, float x) { return catan2f(y, x); }
double atan2(double y, double x) { return catan2(y, x); }
float  asinf(float x) { return casinf(x); }
double asin(double x) { return casin(x); }
float  acosf(float x) { return cacosf(x); }
double acos(double x) { return cacos(x); }
float  fmodf(float x, float y) { return cfmodf(x, y); }
double fmod(double x, double y) { return cfmod(x, y); }
float  ldexpf(float x, int e) { return ccldexpf(x, e); }
double ldexp(double x, int e) { return ccldexp(x, e); }

} // extern "C"
