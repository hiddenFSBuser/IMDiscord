#include "pch.h"
#include "customcrt_memory.h"

#pragma optimize( "", off )
void* ccpy(void* dest, const void* src, size_t count)
{
    char* cdest = static_cast<char*>(dest);
    const char* csrc = static_cast<const char*>(src);
    constexpr size_t word_size = sizeof(size_t);

    if (count < word_size) {
        while (count--) {
            *cdest++ = *csrc++;
        }
        return dest;
    }

    size_t align_offset = word_size - (reinterpret_cast<uintptr_t>(cdest) % word_size);
    if (align_offset != word_size) {
        count -= align_offset;
        while (align_offset--) {
            *cdest++ = *csrc++;
        }
    }

    size_t* wdest = reinterpret_cast<size_t*>(cdest);
    const size_t* wsrc = reinterpret_cast<const size_t*>(csrc);
    size_t num_words = count / word_size;

    while (num_words--) {
        *wdest++ = *wsrc++;
    }

    cdest = reinterpret_cast<char*>(wdest);
    csrc = reinterpret_cast<const char*>(wsrc);
    count %= word_size;

    while (count--) {
        *cdest++ = *csrc++;
    }

    return dest;
}
#pragma optimize( "", on )

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

#pragma optimize( "", off )
void* ccfset(void* dest, int value, size_t count) {
    unsigned char* ptr = (unsigned char*)dest;
    unsigned char val = (unsigned char)value;

    while (count > 0 && ((uintptr)ptr % sizeof(uint64)) != 0) {
        *ptr = val;
        ptr++;
        count--;
    }

    if (count >= sizeof(uint64)) {
        uint64 word_val = 0;
        for (uint64 i = 0; i < sizeof(uint64); ++i) {
            word_val = (word_val << 8) | val;
        }

        uint64* word_ptr = (uint64*)ptr;
        uint64 num_words = count / sizeof(uint64);
        for (uint64 i = 0; i < num_words; ++i) {
            *word_ptr = word_val;
            word_ptr++;
        }

        ptr = (unsigned char*)word_ptr;
        count -= num_words * sizeof(uint64);
    }

    while (count > 0) {
        *ptr = val;
        ptr++;
        count--;
    }

    return dest;
}
#pragma optimize( "", on )

void* ccmov(void* dest, const void* src, size_t n) {
    if (n == 0 || dest == src) return dest;

    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;

    if (d < s) {
        while (n >= 32) {
            *(uint64*)(d + 0) = *(uint64*)(s + 0);
            *(uint64*)(d + 8) = *(uint64*)(s + 8);
            *(uint64*)(d + 16) = *(uint64*)(s + 16);
            *(uint64*)(d + 24) = *(uint64*)(s + 24);
            d += 32; s += 32; n -= 32;
        }
        while (n >= 8) {
            *(uint64*)d = *(uint64*)s;
            d += 8; s += 8; n -= 8;
        }
        while (n > 0) {
            *d++ = *s++;
            n--;
        }
    }
    else {
        d += n;
        s += n;
        while (n >= 32) {
            d -= 32; s -= 32; n -= 32;
            *(uint64*)(d + 24) = *(uint64*)(s + 24);
            *(uint64*)(d + 16) = *(uint64*)(s + 16);
            *(uint64*)(d + 8) = *(uint64*)(s + 8);
            *(uint64*)(d + 0) = *(uint64*)(s + 0);
        }
        while (n >= 8) {
            d -= 8; s -= 8; n -= 8;
            *(uint64*)d = *(uint64*)s;
        }
        while (n > 0) {
            d--; s--; n--;
            *d = *s;
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

void* memalloc(int size) {
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
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