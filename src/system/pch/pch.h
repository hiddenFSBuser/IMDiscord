#pragma once
#define UNICODE
#define _CRT_SECURE_NO_WARNING
#define _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS
#define DIRECTINPUT_VERSION 0x0800
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#define SECURITY_WIN32
#include <security.h>
#include <schannel.h>

// Minimal float constants for ImGui and custom no-CRT builds.
#ifndef FLT_MAX
#define FLT_MAX 3.402823466e+38F
#endif
#ifndef FLT_MIN
#define FLT_MIN 1.175494351e-38F
#endif
#pragma comment(lib, "ws2_32.lib")

#include "libs/customcrt/customcrt.h"

#include "system/alghoritms/ulist.h"
#include "system/alghoritms/umap.h"
#include "system/alghoritms/uset.h"
#include "system/alghoritms/uarena.h"
#include "system/alghoritms/ubuffer.h"

typedef char  sbyte;
typedef unsigned char  byte;
typedef char  int8;
typedef unsigned char  uint8;
typedef __int16  int16;
typedef unsigned __int16  uint16;
typedef int  int32;
typedef unsigned int  uint32;
typedef __int64  int64;
typedef unsigned __int64  uintptr;
typedef unsigned __int64  uint64;
typedef __int64  intptr;
typedef unsigned __int64  uptr;

#define rawfloat(val) (*(unsigned int*)&(const float&)(val))
#define deffor(iterator_name, count) for(int iterator_name = 0; iterator_name < count; iterator_name++)
#define revfor(iterator_name, count) for(int iterator_name = count - 1; iterator_name >= 0; iterator_name--)
#define simple_thread(method, stack_size) CreateThread(0, stack_size, (LPTHREAD_START_ROUTINE)method, 0, 0, 0);
#define ptrvalid(ptr) ((uintptr)ptr > 0x3000000 && (uintptr)ptr < 0x7FFFFFFFFFFF)
#define ptrinvalid(ptr) ((uintptr)ptr <= 0x3000000 || (uintptr)ptr >= 0x7FFFFFFFFFFF)
#define wcstochar(_wchar, _char, _charbufsize) WideCharToMultiByte(CP_UTF8, 0, (wchar_t*)_wchar, -1, (char*)_char, _charbufsize, 0, 0)
#define wcstocharl(_wchar, _char, _charbufsize, _wcharbufsize) WideCharToMultiByte(CP_UTF8, 0, (wchar_t*)_wchar_wcharbufsize, (char*)_char, _charbufsize, 0, 0)
#define chartowcs(_char, _wchar, _wcharbufsize) MultiByteToWideChar(CP_UTF8, 0, (char*)_char, -1, (wchar_t*)_wchar, _wcharbufsize)
#define chartowcsl(_char, _wchar, _charbufsize, _wcharbufsize) MultiByteToWideChar(CP_UTF8, 0, (char*)_char, _charbufsize, (wchar_t*)_wchar, _wcharbufsize)

// ---- no-CRT operator new/delete ------------------------------------------------
// The project links with /NODEFAULTLIB, so every allocation path goes through
// memalloc/memfree (process heap). Placement new is declared here because <new>
// is part of the CRT headers we deliberately do not pull in.
void* operator new(size_t size);
void* operator new[](size_t size);
void operator delete(void* p) noexcept;
void operator delete[](void* p) noexcept;
void operator delete(void* p, size_t) noexcept;
void operator delete[](void* p, size_t) noexcept;

#ifndef __PLACEMENT_NEW_INLINE
#define __PLACEMENT_NEW_INLINE
inline void* operator new(size_t, void* where) noexcept { return where; }
inline void operator delete(void*, void*) noexcept {}
#endif
