#include "pch.h"
#include "customcrt_memory.h"
#include "customcrt_text.h"
#include "customcrt_fstream.h"

// Provide libc symbols referenced by ImGui / Windows SDK when linking with /NODEFAULTLIB.
// Only the functions declared as compiler intrinsics need #pragma function.
#pragma function(strlen, strcmp, strcpy, strncpy, memcmp, memcpy, memmove, memset, memchr)

extern "C" {

size_t strlen(const char* s) { return ccslenf(s); }
int    memcmp(const void* a, const void* b, size_t count) { return ccmp(a, b, count); }
void*  memcpy(void* dest, const void* src, size_t count) { return ccpy(dest, src, count); }
void*  memmove(void* dest, const void* src, size_t count) { return ccmov(dest, src, count); }
void*  memset(void* dest, int value, size_t count) { return ccfset(dest, value, count); }

int    strcmp(const char* a, const char* b) { return ccscmp(a, b); }
int    strncmp(const char* a, const char* b, size_t n) { return ccsncmp(a, b, n); }
char*  strcpy(char* d, const char* s) {
    char* o = d;
    while ((*d++ = *s++) != 0) {}
    return o;
}
char*  strncpy(char* dest, const char* src, size_t count) {
    char* d = dest;
    while (count > 0 && *src) {
        *d++ = *src++;
        count--;
    }
    while (count > 0) {
        *d++ = 0;
        count--;
    }
    return dest;
}

void const* memchr(void const* ptr, int value, size_t count) {
    const unsigned char* p = (const unsigned char*)ptr;
    unsigned char v = (unsigned char)value;
    while (count--) {
        if (*p == v) return (void const*)p;
        p++;
    }
    return nullptr;
}

char const* strchr(char const* s, int c) {
    char ch = (char)c;
    while (*s) {
        if (*s == ch) return s;
        s++;
    }
    return (ch == 0) ? s : nullptr;
}

char const* strstr(char const* haystack, char const* needle) { return ccstrstr(haystack, needle); }

double atof(const char* s) { return (double)ccstrtf(s); }

void qsort(void* base, size_t num, size_t width, int(__cdecl* compare)(const void*, const void*)) {
    custom_qsort(base, (int)num, (int)width, compare);
}

} // extern "C"

// stdio/format stubs referenced by imgui.cpp
extern "C" int vsnprintf(char* buffer, size_t count, const char* format, va_list args) {
    return cvnprint(buffer, count, format, args);
}

extern "C" int __stdio_common_vsscanf(unsigned __int64 /*options*/, const char* buffer, size_t /*buffer_count*/, const char* format, void* /*locale*/, va_list args) {
    return ccscan(buffer, format, args);
}

extern "C" int fflush(void* /*file*/) { return 0; }
extern "C" int printf(const char* /*format*/, ...) { return 0; }
// fprintf is deliberately absent: <stdio.h> emits it as a COMDAT inline in
// every translation unit that includes it (opus does), and a strong definition
// here would collide at link time.

// ImGui helper implementations
int ImFormatString(char* buf, size_t buf_size, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int r = cvnprint(buf, buf_size, fmt, args);
    va_end(args);
    return r;
}
int ImFormatStringV(char* buf, size_t buf_size, const char* fmt, va_list args) {
    return cvnprint(buf, buf_size, fmt, args);
}

// ---- bundled opus (src/libs/opus/source) -----------------------------------
// opus_build_config.h routes every opus_alloc/opus_free here.
extern "C" void* opus_heap_alloc(size_t size) { return memalloc((int)size); }
extern "C" void  opus_heap_free(void* ptr) { memfree(ptr); }

// The libc math aliases live in customcrt_mathalias.cpp, which is compiled
// with /Oi-: under /Oi the compiler keeps these names as intrinsics and
// refuses to let anyone define them (C2169), #pragma function or not.
