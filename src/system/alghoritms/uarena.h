#pragma once
#include "../../libs/customcrt/customcrt.h"

// Bump allocator. Everything that lives as long as the discord cache (user
// names, message content, urls) is duplicated into an arena instead of being
// owned by a string class - ulist relocates elements with raw byte copies, so
// heap-owning elements would double free. Arena strings are plain const char*
// and die together with the arena.
class uarena
{
public:
    struct chunk
    {
        chunk* next;
        unsigned int used;
        unsigned int cap;
    };

    static const unsigned int DEFAULT_CHUNK = 1 << 16;

    chunk* head;
    unsigned __int64 total;

    void init();
    void* alloc(unsigned int size, unsigned int align = 8);
    void* alloc_zero(unsigned int size, unsigned int align = 8);
    // Duplicates a string into the arena. len < 0 means "measure it".
    const char* dup(const char* str, int len = -1);
    void reset();

    template <typename T>
    T* make()
    {
        T* p = (T*)alloc_zero(sizeof(T), __alignof(T));
        return p;
    }
};
