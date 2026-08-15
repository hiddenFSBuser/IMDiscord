//-----------------------------------------------------------------
// DEAR IMGUI COMPILE-TIME OPTIONS
//-----------------------------------------------------------------

#pragma once
#include "pch.h"

#define IMGUI_DISABLE_OBSOLUTE_FUNCTIONS
#define IMGUI_DISABLE_DEFAULT_ALLOCATORS
#define IMGUI_DISABLE_DEFAULT_MATH_FUNCTIONS
#define IMGUI_DISABLE_FILE_FUNCTIONS
#define IMGUI_DISABLE_WIN32_DEFAULT_CLIPBOARD_FUNCTIONS
#define IMGUI_DISABLE_DEFAULT_FORMAT_FUNCTIONS
#define IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS

#define IM_ASSERT(_EXPR)    ((void)(_EXPR))

// Allocation is wired up at runtime through ImGui::SetAllocatorFunctions in
// app_main; imgui has no compile-time allocator hook.

#define ImDrawIdx           unsigned int

// Stub stdio FILE handles so imgui's LogToTTY/stdio references don't pull CRT.
struct custom_file { int dummy; };
static custom_file g_custom_stdout = {0};
static custom_file g_custom_stderr = {0};
static custom_file g_custom_stdin = {0};
#undef stdin
#undef stdout
#undef stderr
#define stdout ((custom_file*)&g_custom_stdout)
#define stderr ((custom_file*)&g_custom_stderr)
#define stdin  ((custom_file*)&g_custom_stdin)

// Math helpers used by imgui (default math functions disabled).
static inline float  ImFabs(float x) { return cabs(x); }
static inline float  ImSqrt(float x) { return csqrtf(x); }
static inline float  ImFmod(float x, float y) { return cfmodf(x, y); }
static inline float  ImCos(float x) { return ccosf(x); }
static inline float  ImSin(float x) { return csinf(x); }
static inline float  ImAcos(float x) { return cacosf(x); }
static inline float  ImAtan2(float y, float x) { return catan2f(y, x); }
static inline float  ImPow(float x, float y) { return cpowf(x, y); }
static inline double ImPow(double x, double y) { return cpow(x, y); }
static inline float  ImLog(float x) { return clogf(x); }
static inline double ImLog(double x) { return clogf((float)x); }
static inline float  ImCeil(float x) { return cceilf(x); }
static inline float  ImAtof(const char* s) { return ccstrtf(s); }
static inline int    ImAbs(int x) { return x < 0 ? -x : x; }
static inline float  ImAbs(float x) { return cabs(x); }
static inline double ImAbs(double x) { return cfabs(x); }
static inline float  ImSign(float x) { return (x < 0.0f) ? -1.0f : (x > 0.0f) ? 1.0f : 0.0f; }
static inline double ImSign(double x) { return (x < 0.0) ? -1.0 : (x > 0.0) ? 1.0 : 0.0; }
static inline float  ImRsqrt(float x) { return 1.0f / csqrtf(x); }
static inline double ImRsqrt(double x) { return 1.0 / csqrt(x); }

#define IMGUI_DISABLE_STB_SPRINTF_IMPLEMENTATION
