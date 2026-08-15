#include "pch.h"
#include <emmintrin.h>
#include <smmintrin.h>
#include "core/log.h"

// Shims for the handful of libc symbols that opus.lib (built by CMake/MSVC)
// still references. The project links with /NODEFAULTLIB, so nothing else
// provides them. Function-style imports arrive as `__imp_<name>` indirections,
// therefore every stub is exposed both as a plain symbol and as a data pointer
// under the `__imp_` name.

extern "C" {

// ---- allocation ---------------------------------------------------------
void* cc_malloc(size_t size)
{
    return memalloc((int)size);
}

void cc_free(void* p)
{
    if (p) memfree(p);
}

void* cc_calloc(size_t n, size_t size)
{
    void* p = memalloc((int)(n * size));
    if (p) ccfset(p, 0, n * size);
    return p;
}

void* cc_realloc(void* p, size_t size)
{
    return memrealloc(p, (int)size);
}

// minimp4 copies metadata strings out of a file with this. The caller frees
// them with free(), so the allocation has to come from the same place as
// every other one here.
char* cc_strdup(const char* s)
{
    if (!s) return 0;

    int len = (int)ccslenf(s) + 1;
    char* copy = (char*)memalloc(len);
    if (copy) ccpy(copy, s, (size_t)len);
    return copy;
}

// ---- process ------------------------------------------------------------
void cc_abort()
{
    TerminateProcess(GetCurrentProcess(), 3);
}

unsigned int cc_set_abort_behavior(unsigned int, unsigned int)
{
    return 0;
}

void* cc_acrt_iob_func(unsigned int)
{
    return 0;
}

// Normally nothing prints and this throws it away. Built with
// TLS_DEBUG_TO_LOG it forwards to the client's log instead, which is the only
// way to see what a third party library objects to when all it says is
// "broken".
int cc_stdio_common_vfprintf(unsigned __int64, void*, const char* format, void*, va_list args)
{
#ifdef TLS_DEBUG_TO_LOG
    char line[1024];
    int n = cvnprint(line, sizeof(line), format, args);

    // It prints in fragments without newlines, so they are gathered up and
    // flushed on one.
    static char pending[2048];
    static int filled = 0;

    for (int i = 0; i < n && line[i]; i++)
    {
        if (line[i] == '\n' || filled >= (int)sizeof(pending) - 2)
        {
            pending[filled] = 0;
            if (filled) log_line("tlse: %s", pending);
            filled = 0;
            continue;
        }
        if (line[i] != '\r') pending[filled++] = line[i];
    }
    return n;
#else
    (void)format;
    (void)args;
    return 0;
#endif
}

int cc_stdio_common_vsprintf(unsigned __int64, char* buffer, size_t count, const char* format, void*, va_list args)
{
    return cvnprint(buffer, count, format, args);
}

int cc_stdio_common_vsscanf(unsigned __int64, const char* buffer, size_t, const char* format, void*, va_list args)
{
    return ccscan(buffer, format, args);
}

// MSVC emits __isa_available lookups for its own SSE/AVX dispatch paths.
// 1 == __ISA_AVAILABLE_SSE2, the x64 baseline, which is always correct.
int __isa_available = 1;
int __isa_enabled = 1;
int __favor = 0;

// Static objects with destructors register through atexit. The process image
// is torn down by ExitProcess, so running them is pointless.
int atexit(void(__cdecl*)(void)) { return 0; }
int _purecall() { cc_abort(); return 0; }

// ---- math referenced by opus -------------------------------------------
// These names are compiler intrinsics, so they cannot be defined directly.
// /alternatename installs each stub as the weak fallback the linker picks when
// the real symbol stays unresolved.
double cc_m_sqrt(double x)          { return csqrt(x); }
double cc_m_floor(double x)         { return cfloor(x); }
double cc_m_ceil(double x)          { return cceil(x); }
double cc_m_log(double x)           { return (double)clogf((float)x); }
double cc_m_log10(double x)         { return (double)clogf((float)x) * 0.43429448190325176; }
double cc_m_pow(double x, double y) { return cpow(x, y); }
double cc_m_exp(double x)           { return (double)cexf((float)x); }
double cc_m_fabs(double x)          { return cfabs(x); }
double cc_m_ldexp(double x, int e)  { return ccldexp(x, e); }

double cc_m_sin(double x)           { return (double)csinf((float)x); }
double cc_m_cos(double x)           { return (double)ccosf((float)x); }
double cc_m_tan(double x)           { return (double)ctanf((float)x); }
double cc_m_atan(double x)          { return catan(x); }
double cc_m_atan2(double y, double x) { return catan2(y, x); }
double cc_m_asin(double x)          { return casin(x); }
double cc_m_acos(double x)          { return cacos(x); }
double cc_m_fmod(double x, double y) { return cfmod(x, y); }

float cc_m_sqrtf(float x)           { return csqrtf(x); }
float cc_m_floorf(float x)          { return cfloorf(x); }
float cc_m_ceilf(float x)           { return cceilf(x); }
float cc_m_powf(float x, float y)   { return cpowf(x, y); }
float cc_m_logf(float x)            { return clogf(x); }
float cc_m_expf(float x)            { return cexf(x); }
float cc_m_fabsf(float x)           { return cfabs(x); }
float cc_m_log10f(float x)          { return clogf(x) * 0.43429448f; }

void cc_exit(int code)              { TerminateProcess(GetCurrentProcess(), (UINT)code); }

// rnnoise.lib was compiled with /GS, so it expects the stack cookie machinery.
// Nothing here writes past a buffer on its behalf, and the check is a no-op
// rather than a crash on mismatch.
unsigned __int64 __security_cookie = 0x00002B992DDFA232ull;
void __fastcall __security_check_cookie(unsigned __int64) {}

// Referenced from the unwind data of those same functions. The image is linked
// without exception handling, so unwinding never reaches it; returning
// "continue search" is the neutral answer if it somehow did.
int __GSHandlerCheck(void*, void*, void*, void*) { return 1; }

// It also links the file API for loading a model from disk. The built-in model
// is always used (rnnoise_create(0)), so these are never reached.
void* cc_fopen(const char*, const char*) { return 0; }
int cc_fseek(void*, long, int)           { return -1; }
long cc_ftell(void*)                     { return -1; }
size_t cc_fread(void*, size_t, size_t, void*) { return 0; }
int cc_fclose(void*)                     { return 0; }
size_t cc_fwrite(const void*, size_t, size_t, void*) { return 0; }

// tlse asks for these. Nothing security relevant depends on rand() there -
// the key material comes from the system generator - but it is called, so it
// has to exist and it may as well not be a constant.
static unsigned int g_rand_state = 0;

int cc_rand()
{
    if (!g_rand_state)
    {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        g_rand_state = (unsigned int)(now.QuadPart ^ (now.QuadPart >> 32)) | 1u;
    }

    // xorshift32, which is plenty for something whose output only ever
    // decides padding lengths and retry jitter.
    g_rand_state ^= g_rand_state << 13;
    g_rand_state ^= g_rand_state >> 17;
    g_rand_state ^= g_rand_state << 5;
    return (int)(g_rand_state & 0x7FFFFFFF);
}

void cc_srand(unsigned int seed) { g_rand_state = seed | 1u; }

// Certificate validity is checked against the clock, so this one has to be a
// real answer rather than a stub: a wrong year rejects every certificate.
long long cc_time(long long* out)
{
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);

    unsigned long long ticks = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    // 100 ns ticks since 1601 to seconds since 1970.
    long long seconds = (long long)((ticks - 116444736000000000ULL) / 10000000ULL);

    if (out) *out = seconds;
    return seconds;
}

// The rest of what tlse reaches for. Its file handling only ever loads
// certificates from disk, which this client never asks it to do - the trust
// store is compiled in - so those stay stubs.
int cc_fgetc(void*)                          { return -1; }
int cc_fputc(int c, void*)                   { return c; }
int cc_setvbuf(void*, char*, int, size_t)    { return 0; }

int cc_stricmp(const char* a, const char* b)
{
    if (!a || !b) return a == b ? 0 : (a ? 1 : -1);

    while (*a && *b)
    {
        int ca = cctolower(*a);
        int cb = cctolower(*b);
        if (ca != cb) return ca - cb;
        a++;
        b++;
    }
    return cctolower(*a) - cctolower(*b);
}

long cc_clock()
{
    // CLOCKS_PER_SEC is a thousand on windows, so milliseconds are the answer.
    return (long)GetTickCount64();
}

// Certificate dates are compared through this, so it has to be right rather
// than merely present: an answer off by a year rejects every certificate that
// exists.
struct cc_tm
{
    int tm_sec, tm_min, tm_hour;
    int tm_mday, tm_mon, tm_year;
    int tm_wday, tm_yday, tm_isdst;
};

static cc_tm g_gmtime_result;

void* cc_gmtime64(const long long* when)
{
    if (!when) return 0;

    long long seconds = *when;
    if (seconds < 0) return 0;

    long long days = seconds / 86400;
    int rest = (int)(seconds % 86400);

    cc_tm* out = &g_gmtime_result;
    out->tm_hour = rest / 3600;
    out->tm_min = (rest % 3600) / 60;
    out->tm_sec = rest % 60;
    out->tm_isdst = 0;

    // 1 January 1970 was a Thursday.
    out->tm_wday = (int)((days + 4) % 7);

    int year = 1970;
    for (;;)
    {
        bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        int length = leap ? 366 : 365;
        if (days < length) break;
        days -= length;
        year++;
    }

    out->tm_year = year - 1900;
    out->tm_yday = (int)days;

    bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    static const int LENGTHS[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

    int month = 0;
    for (; month < 12; month++)
    {
        int length = LENGTHS[month] + ((month == 1 && leap) ? 1 : 0);
        if (days < length) break;
        days -= length;
    }

    out->tm_mon = month;
    out->tm_mday = (int)days + 1;
    return out;
}

extern "C" int vsnprintf(char* buffer, size_t count, const char* format, va_list args);

int cc_toupper(int c) { return (c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c; }

// Nothing here has a signal handler, and tlse only raises on an assertion it
// never reaches. Returning zero is "delivered".
int cc_raise(int) { return 0; }

// cnprint is C++ and its name is mangled; C code cannot reach it by the name
// the linker would look for.
int cc_snprintf(char* buffer, size_t count, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    int written = vsnprintf(buffer, count, format, args);
    va_end(args);
    return written;
}

// ---- __imp_ indirections ------------------------------------------------
void* __imp_malloc                  = (void*)&cc_malloc;
void* __imp_free                    = (void*)&cc_free;
void* __imp_calloc                  = (void*)&cc_calloc;
void* __imp_realloc                 = (void*)&cc_realloc;
void* __imp_abort                   = (void*)&cc_abort;
void* __imp__set_abort_behavior     = (void*)&cc_set_abort_behavior;
void* __imp___acrt_iob_func         = (void*)&cc_acrt_iob_func;
void* __imp___stdio_common_vfprintf = (void*)&cc_stdio_common_vfprintf;
void* __imp___stdio_common_vsprintf = (void*)&cc_stdio_common_vsprintf;
void* __imp___stdio_common_vsscanf  = (void*)&cc_stdio_common_vsscanf;
void* __imp_fopen                   = (void*)&cc_fopen;
void* __imp_fseek                   = (void*)&cc_fseek;
void* __imp_ftell                   = (void*)&cc_ftell;
void* __imp_fread                   = (void*)&cc_fread;
void* __imp_fclose                  = (void*)&cc_fclose;
void* __imp_exit                    = (void*)&cc_exit;

} // extern "C"

// The short vector math library lives in the CRT, and the prebuilt rnnoise.lib
// calls into it. Its entry points pass __m128d in XMM registers and return in
// XMM0, which is the __vectorcall convention - a plain __cdecl definition would
// receive the argument by pointer and dereference garbage. __vectorcall names
// are decorated, so the undecorated symbols the library asks for are aliased
// onto these below.
// Scalar rather than roundpd: the rest of the image only assumes SSE2, and
// this is not on a hot path.
extern "C" __m128d __vectorcall imd_vdecl_floor2(__m128d x)
{
    double xs[2], r[2];
    _mm_storeu_pd(xs, x);
    r[0] = cfloor(xs[0]);
    r[1] = cfloor(xs[1]);
    return _mm_loadu_pd(r);
}

extern "C" __m128d __vectorcall imd_vdecl_pow2(__m128d x, __m128d y)
{
    double xs[2], ys[2], r[2];
    _mm_storeu_pd(xs, x);
    _mm_storeu_pd(ys, y);
    r[0] = cpow(xs[0], ys[0]);
    r[1] = cpow(xs[1], ys[1]);
    return _mm_loadu_pd(r);
}

#pragma comment(linker, "/alternatename:sqrt=cc_m_sqrt")
#pragma comment(linker, "/alternatename:floor=cc_m_floor")
#pragma comment(linker, "/alternatename:ceil=cc_m_ceil")
#pragma comment(linker, "/alternatename:log=cc_m_log")
#pragma comment(linker, "/alternatename:log10=cc_m_log10")
#pragma comment(linker, "/alternatename:pow=cc_m_pow")
#pragma comment(linker, "/alternatename:exp=cc_m_exp")
#pragma comment(linker, "/alternatename:fabs=cc_m_fabs")
#pragma comment(linker, "/alternatename:ldexp=cc_m_ldexp")
#pragma comment(linker, "/alternatename:sin=cc_m_sin")
#pragma comment(linker, "/alternatename:cos=cc_m_cos")
#pragma comment(linker, "/alternatename:tan=cc_m_tan")
#pragma comment(linker, "/alternatename:atan=cc_m_atan")
#pragma comment(linker, "/alternatename:atan2=cc_m_atan2")
#pragma comment(linker, "/alternatename:asin=cc_m_asin")
#pragma comment(linker, "/alternatename:acos=cc_m_acos")
#pragma comment(linker, "/alternatename:fmod=cc_m_fmod")
#pragma comment(linker, "/alternatename:sqrtf=cc_m_sqrtf")
#pragma comment(linker, "/alternatename:floorf=cc_m_floorf")
#pragma comment(linker, "/alternatename:ceilf=cc_m_ceilf")
#pragma comment(linker, "/alternatename:powf=cc_m_powf")
#pragma comment(linker, "/alternatename:logf=cc_m_logf")
#pragma comment(linker, "/alternatename:expf=cc_m_expf")
#pragma comment(linker, "/alternatename:fabsf=cc_m_fabsf")
#pragma comment(linker, "/alternatename:__vdecl_floor2=imd_vdecl_floor2@@16")
#pragma comment(linker, "/alternatename:__vdecl_pow2=imd_vdecl_pow2@@32")
#pragma comment(linker, "/alternatename:log10f=cc_m_log10f")
#pragma comment(linker, "/alternatename:exit=cc_exit")
#pragma comment(linker, "/alternatename:malloc=cc_malloc")
#pragma comment(linker, "/alternatename:free=cc_free")
#pragma comment(linker, "/alternatename:calloc=cc_calloc")
#pragma comment(linker, "/alternatename:realloc=cc_realloc")
#pragma comment(linker, "/alternatename:strdup=cc_strdup")
#pragma comment(linker, "/alternatename:_strdup=cc_strdup")
#pragma comment(linker, "/alternatename:abort=cc_abort")
#pragma comment(linker, "/alternatename:rand=cc_rand")
#pragma comment(linker, "/alternatename:srand=cc_srand")
#pragma comment(linker, "/alternatename:time=cc_time")
#pragma comment(linker, "/alternatename:__acrt_iob_func=cc_acrt_iob_func")
#pragma comment(linker, "/alternatename:fopen=cc_fopen")
#pragma comment(linker, "/alternatename:fclose=cc_fclose")
#pragma comment(linker, "/alternatename:fwrite=cc_fwrite")
#pragma comment(linker, "/alternatename:fread=cc_fread")
#pragma comment(linker, "/alternatename:fseek=cc_fseek")
#pragma comment(linker, "/alternatename:ftell=cc_ftell")
#pragma comment(linker, "/alternatename:fgetc=cc_fgetc")
#pragma comment(linker, "/alternatename:fputc=cc_fputc")
#pragma comment(linker, "/alternatename:setvbuf=cc_setvbuf")
#pragma comment(linker, "/alternatename:stricmp=cc_stricmp")
#pragma comment(linker, "/alternatename:_stricmp=cc_stricmp")
#pragma comment(linker, "/alternatename:clock=cc_clock")
#pragma comment(linker, "/alternatename:_gmtime64=cc_gmtime64")
#pragma comment(linker, "/alternatename:snprintf=cc_snprintf")
#pragma comment(linker, "/alternatename:_snprintf=cc_snprintf")
#pragma comment(linker, "/alternatename:_time64=cc_time")
#pragma comment(linker, "/alternatename:toupper=cc_toupper")
#pragma comment(linker, "/alternatename:raise=cc_raise")
