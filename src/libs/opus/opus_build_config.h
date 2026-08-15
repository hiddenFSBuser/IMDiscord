#pragma once

// Build configuration for the bundled opus 1.4 sources, forced into every
// opus translation unit (/FI) in place of an autotools-generated config.h.
//
// NoCRT: opus never sees malloc/free. Its allocation hooks are routed into
// customcrt's heap wrappers, which live in customcrt_crtalias.cpp.

#include <stddef.h>

#define OPUS_BUILD 1
#define FIXED_POINT 1
#define USE_ALLOCA 1

#ifdef __cplusplus
extern "C" {
#endif
void* opus_heap_alloc(size_t size);
void  opus_heap_free(void* ptr);
#ifdef __cplusplus
}
#endif

#define OVERRIDE_OPUS_ALLOC 1
#define opus_alloc(size) opus_heap_alloc(size)
#define OVERRIDE_OPUS_ALLOC_SCRATCH 1
#define opus_alloc_scratch(size) opus_heap_alloc(size)
#define OVERRIDE_OPUS_FREE 1
#define opus_free(ptr) opus_heap_free(ptr)
