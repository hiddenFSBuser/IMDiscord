#include "pch.h"
#include "uarena.h"

void uarena::init()
{
    head = 0;
    total = 0;
}

void* uarena::alloc(unsigned int size, unsigned int align)
{
    if (align < 1) align = 1;

    if (head)
    {
        unsigned int base = (head->used + (align - 1)) & ~(align - 1);
        if (base + size <= head->cap)
        {
            head->used = base + size;
            return (char*)(head + 1) + base;
        }
    }

    unsigned int want = size + align + (unsigned int)sizeof(chunk);
    unsigned int cap = DEFAULT_CHUNK;
    while (cap < want) cap *= 2;

    chunk* c = (chunk*)memalloc((int)(cap + sizeof(chunk)));
    if (!c) return 0;

    c->next = head;
    c->used = 0;
    c->cap = cap;
    head = c;
    total += cap;

    unsigned int base = (unsigned int)((align - (unsigned __int64)((char*)(c + 1)) % align) % align);
    c->used = base + size;
    return (char*)(c + 1) + base;
}

void* uarena::alloc_zero(unsigned int size, unsigned int align)
{
    void* p = alloc(size, align);
    if (p) ccfset(p, 0, size);
    return p;
}

const char* uarena::dup(const char* str, int len)
{
    if (!str) return 0;
    if (len < 0) len = (int)ccslenf(str);

    char* out = (char*)alloc((unsigned int)len + 1, 1);
    if (!out) return 0;
    if (len > 0) ccpy(out, str, (size_t)len);
    out[len] = 0;
    return out;
}

void uarena::reset()
{
    chunk* c = head;
    while (c)
    {
        chunk* next = c->next;
        memfree(c);
        c = next;
    }
    head = 0;
    total = 0;
}
