#pragma once

#pragma function(memcpy)
void* ccpy(void* dest, const void* src, size_t count);
__forceinline void* ccpy(const void* dest, const void* src, size_t count) { return ccpy((void*)dest, src, count); }
__forceinline void* ccpy(const void* dest, unsigned long long src, size_t count) { return ccpy((void*)dest, (void*)src, count); }
__forceinline void* ccpy(unsigned long long dest, const void* src, size_t count) { return ccpy((void*)dest, (void*)src, count); }
__forceinline void* ccpy(unsigned long long dest, unsigned long long src, size_t count) { return ccpy((void*)dest, (void*)src, count); }

#pragma function(memmove)
int ccmp(const void* s1, const void* s2, size_t count);
__forceinline int ccmp(unsigned long long s1, const void* s2, size_t count) { return ccmp((void*)s1, (void*)s2, count); }
__forceinline int ccmp(const void* s1, unsigned long long s2, size_t count) { return ccmp((void*)s1, (void*)s2, count); }
__forceinline int ccmp(unsigned long long s1, unsigned long long s2, size_t count) { return ccmp((void*)s1, (void*)s2, count); }

__forceinline inline void* ccset(void* dest, int value, size_t cunt) {
    unsigned char* p = (unsigned char*)dest;
    while (cunt > 0)
    {
        *p = value;
        p++;
        cunt--;
    }
    return (void*)dest;
}
#pragma function(memset)
void* ccfset(void* dest, int value, size_t count);
__forceinline void* ccset(const void* dest, int value, size_t count) { return ccset((void*)dest, value, count); }
__forceinline void* ccfset(const void* dest, int value, size_t count) { return ccfset((void*)dest, value, count); }
__forceinline void* ccset(unsigned long long dest, int value, size_t count) { return ccset((void*)dest, value, count); }
__forceinline void* ccfset(unsigned long long dest, int value, size_t count) { return ccfset((void*)dest, value, count); }

void* ccmov(void* dest, const void* src, size_t n);
__forceinline void* ccmov(void* dest, void* src, size_t n) { return ccmov((void*)dest, (const void*)src, n); }
__forceinline void* ccmov(const void* dest, void* src, size_t n) { return ccmov((void*)dest, (const void*)src, n); }
__forceinline void* ccmov(void* dest, unsigned long long src, size_t n) { return ccmov((void*)dest, (const void*)src, n); }
__forceinline void* ccmov(unsigned long long dest, void* src, size_t n) { return ccmov((void*)dest, (const void*)src, n); }
__forceinline void* ccmov(unsigned long long dest, unsigned long long src, size_t n) { return ccmov((void*)dest, (const void*)src, n); }

unsigned int ccrc32(const void* buffer, unsigned int size);
__forceinline unsigned int ccrc32(void* buffer, unsigned int size) { return ccrc32((const void*)buffer, size); }
__forceinline unsigned int ccrc32(unsigned long long buffer, unsigned int size) { return ccrc32((const void*)buffer, size); }

unsigned __int64 ccrc64(const void* buffer, unsigned int size);
__forceinline unsigned __int64 ccrc64(void* buffer, unsigned int size) { return ccrc64((const void*)buffer, size); }
__forceinline unsigned __int64 ccrc64(unsigned long long buffer, unsigned int size) { return ccrc64((const void*)buffer, size); }

void* memalloc(int size);
void* memrealloc(void* ptr, int size);
void memfree(void* ptr);
inline void memfree(const void* ptr) { memfree((void*)ptr); }
inline void memfree(unsigned long long ptr) { memfree((void*)ptr); }