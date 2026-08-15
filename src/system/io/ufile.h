#pragma once
#include "ubuffer.h"

// Thin Win32 file helpers. The project has no CRT, so every path goes through
// CreateFileW/ReadFile/WriteFile directly.
namespace ufile
{
    bool read_all(const wchar_t* path, ubuffer* out);
    bool read_all(const char* path_utf8, ubuffer* out);
    bool write_all(const wchar_t* path, const void* data, unsigned int size);
    bool write_all(const char* path_utf8, const void* data, unsigned int size);
    bool exists(const wchar_t* path);

    // %LOCALAPPDATA%\IMDiscord, created on demand. Returns false if it could
    // not be resolved or created.
    bool app_dir(wchar_t* out, int out_chars);
    bool app_path(const wchar_t* filename, wchar_t* out, int out_chars);

    // Picks a file with the common item dialog. Returns false when cancelled.
    bool open_dialog(wchar_t* out, int out_chars);
    bool save_dialog(const wchar_t* suggested_name, wchar_t* out, int out_chars);
}
