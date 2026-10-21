#include "pch.h"
#include "customcrt_memory.h"

// customcrt_memory.h switches memcpy/memset to real calls (#pragma function)
// so that customcrt_crtalias can provide them for the rest of the program.
// Inside their own implementations a call would land right back here, so the
// bulk work is done with the string intrinsics (__movsb/__movsq/__stosb/__stosq):
// they always compile to rep movs/stos and can never become a runtime call.
// Plain byte/word loops are off-limits here: at /O2 the compiler rewrites them
// into memset/memcpy calls, and #pragma intrinsic does not stop that - the
// resulting call aliases straight back into these very functions (this is what
// used to crash the client with a stack overflow).
#include <intrin.h>

void* ccpy(void* dest, const void* src, size_t count)
{
    unsigned char* cdest = static_cast<unsigned char*>(dest);
    const unsigned char* csrc = static_cast<const unsigned char*>(src);
    constexpr size_t word_size = sizeof(size_t);

    if (count >= word_size) {
        size_t align_offset = word_size - (reinterpret_cast<uintptr_t>(cdest) % word_size);
        if (align_offset != word_size) {
            __movsb(cdest, csrc, align_offset);
            cdest += align_offset;
            csrc += align_offset;
            count -= align_offset;
        }

        size_t num_words = count / word_size;
        if (num_words) {
            __movsq(reinterpret_cast<unsigned long long*>(cdest),
                    reinterpret_cast<const unsigned long long*>(csrc), num_words);
            cdest += num_words * word_size;
            csrc += num_words * word_size;
            count -= num_words * word_size;
        }
    }

    if (count) {
        __movsb(cdest, csrc, count);
    }

    return dest;
}

int ccmp(const void* s1, const void* s2, size_t count)
{
    const unsigned char* p1 = static_cast<const unsigned char*>(s1);
    const unsigned char* p2 = static_cast<const unsigned char*>(s2);
    constexpr size_t word_size = sizeof(size_t);

    while (count >= word_size)
    {
        if (*(reinterpret_cast<const size_t*>(p1)) != *(reinterpret_cast<const size_t*>(p2))) {
            break;
        }
        p1 += word_size;
        p2 += word_size;
        count -= word_size;
    }

    while (count--)
    {
        if (*p1 != *p2) {
            return *p1 - *p2;
        }
        p1++;
        p2++;
    }

    return 0;
}

void* ccfset(void* dest, int value, size_t count) {
    unsigned char* ptr = (unsigned char*)dest;
    unsigned char val = (unsigned char)value;

    size_t align_offset = sizeof(uint64) - (uintptr)ptr % sizeof(uint64);
    if (align_offset == sizeof(uint64)) align_offset = 0;
    if (align_offset > count) align_offset = count;
    if (align_offset) {
        __stosb(ptr, val, align_offset);
        ptr += align_offset;
        count -= align_offset;
    }

    if (count >= sizeof(uint64)) {
        uint64 word_val = 0x0101010101010101ULL * val;
        size_t num_words = count / sizeof(uint64);
        __stosq((unsigned long long*)ptr, word_val, num_words);
        ptr += num_words * sizeof(uint64);
        count -= num_words * sizeof(uint64);
    }

    if (count) {
        __stosb(ptr, val, count);
    }

    return dest;
}

void* ccmov(void* dest, const void* src, size_t n) {
    if (n == 0 || dest == src) return dest;

    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;

    // Forward copy is safe for d < s and for non-overlapping regions. For an
    // overlapping move with d > s the copy has to run from the end backwards,
    // one word at a time: a single rep movsq would overwrite source bytes that
    // have not been read yet.
    if (d < s || (size_t)(d - s) >= n) {
        size_t num_words = n / sizeof(uint64);
        if (num_words) {
            __movsq((unsigned long long*)d, (const unsigned long long*)s, num_words);
            d += num_words * sizeof(uint64);
            s += num_words * sizeof(uint64);
            n -= num_words * sizeof(uint64);
        }
        if (n) {
            __movsb(d, s, n);
        }
    }
    else {
        size_t tail = n % sizeof(uint64);
        d += n;
        s += n;
        if (tail) {
            d -= tail; s -= tail;
            // Highest bytes first: straight-line stores, deliberately not a loop.
            if (tail >= 7) d[6] = s[6];
            if (tail >= 6) d[5] = s[5];
            if (tail >= 5) d[4] = s[4];
            if (tail >= 4) d[3] = s[3];
            if (tail >= 3) d[2] = s[2];
            if (tail >= 2) d[1] = s[1];
            if (tail >= 1) d[0] = s[0];
        }
        n -= tail;
        while (n) {
            d -= sizeof(uint64); s -= sizeof(uint64); n -= sizeof(uint64);
            __movsq((unsigned long long*)d, (const unsigned long long*)s, 1);
        }
    }

    return dest;
}

unsigned int ccrc32(const void* buffer, unsigned int size) {
    static unsigned int crc32_lut[256] = { 0 };

    if (!crc32_lut[1])
    {
        const unsigned int polynomial = 0xEDB88320;
        for (unsigned int i = 0; i < 256; i++)
        {
            unsigned int crc = i;
            for (unsigned int j = 0; j < 8; j++)
                crc = (crc >> 1) ^ (-int(crc & 1) & polynomial);
            crc32_lut[i] = crc;
        }
    }

    unsigned int crc = ~0;
    const unsigned char* current = (const unsigned char*)buffer;

    while (size--) {
        crc = (crc >> 8) ^ crc32_lut[(crc & 0xFF) ^ *current++];
    }

    return ~crc;
}
unsigned __int64 ccrc64(const void* data, unsigned int size) {
    static unsigned __int64 crc64_lut[256] = { 0 };
    if (!crc64_lut[1]) {
        const unsigned __int64 polynomial = 0x42F0E1EBA9EA3693ULL;
        for (unsigned int i = 0; i < 256; i++) {
            unsigned __int64 crc = i;
            for (unsigned int j = 0; j < 8; j++)
                crc = (crc >> 1) ^ (-(__int64)(crc & 1) & polynomial);
            crc64_lut[i] = crc;
        }
    }

    unsigned __int64 crc = ~0ULL;
    const unsigned char* current = (const unsigned char*)data;
    while (size--) {
        crc = (crc >> 8) ^ crc64_lut[(crc ^ *current++) & 0xFF];
    }
    return ~crc;
}

// Zeroing allocator: every byte handed out is zeroed by the heap manager.
void* memalloc(int size) {
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
}
// Fast allocator: contents are garbage. Callers that need zeroed memory must
// zero it themselves - the win is that they only do it once (the heap manager
// would have zeroed it anyway, and then the caller zeroed it again).
void* memallocf(int size) {
    return HeapAlloc(GetProcessHeap(), 0, size);
}
void* operator new(size_t size) { return memalloc((int)size); }
void* operator new[](size_t size) { return memalloc((int)size); }
void operator delete(void* p) noexcept { memfree(p); }
void operator delete[](void* p) noexcept { memfree(p); }
void operator delete(void* p, size_t) noexcept { memfree(p); }
void operator delete[](void* p, size_t) noexcept { memfree(p); }
void* memrealloc(void* ptr, int size) {
    if (!ptr) return memalloc(size);
    if (size == 0) {
        memfree(ptr);
        return nullptr;
    }
    return HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, ptr, size);
}
void memfree(void* ptr) {
    if (!ptr) return;
    HeapFree(GetProcessHeap(), 0, ptr);
}