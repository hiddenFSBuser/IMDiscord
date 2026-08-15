#pragma once
#include <vadefs.h>

int cvnprint(char* buffer, size_t count, const char* format, va_list args);
int cwvnprint(wchar_t* buffer, size_t count, const wchar_t* format, va_list args);

int cnprint(char* buffer, size_t count, const char* format, ...);
int cprint(const char* format, ...);
int wnprint(wchar_t* buffer, size_t count, const wchar_t* format, ...);
int wprint(const wchar_t* format, ...);
int ccscan(const char* str, const char* format, ...);