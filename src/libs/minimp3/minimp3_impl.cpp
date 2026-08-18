// The one translation unit that carries minimp3's code.
//
// Both headers are here rather than only the plain decoder: the _ex layer is
// what knows how to walk a whole file, count its length and seek to an exact
// sample, and doing any of that by hand over the frame decoder would be
// rewriting it worse.
//
// The library is ordinary C and asks for malloc, realloc, memcpy and friends.
// Those resolve to customcrt through the /alternatename directives in
// customcrt_extra.cpp, the same way libwebp's and minimp4's do, so nothing
// here drags in a C runtime.

#include "pch.h"

// Nothing opens a path through minimp3: the sounds are already in memory and
// the music player reads the file itself, so the stdio half of the _ex layer
// would only be there to pull in fopen.
#define MINIMP3_NO_STDIO

#define MINIMP3_IMPLEMENTATION
#include "minimp3/minimp3_ex.h"
