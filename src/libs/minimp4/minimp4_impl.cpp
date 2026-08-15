// The one translation unit that carries minimp4's code.
//
// Only the demuxer is wanted: this client reads mp4 files people post, it does
// not write any. The muxer comes along because the header is one piece, but it
// is never called and the linker drops it.
//
// The library is ordinary C and asks for malloc, memcpy and friends. Those
// resolve to customcrt the same way libwebp's and speexdsp's do - see the
// /alternatename directives in customcrt_extra.cpp - so nothing here drags in
// a C runtime.

#include "pch.h"

// Printing movie information to stdout would need a real stdio, and there is
// no console to print it to.
#define MP4D_PRINT_INFO_SUPPORTED 0

#define MINIMP4_IMPLEMENTATION
#include "minimp4/minimp4.h"
