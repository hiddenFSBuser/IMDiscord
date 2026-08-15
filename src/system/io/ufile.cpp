#include "pch.h"
#include "ufile.h"
#include <commdlg.h>

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")

namespace ufile
{
    bool read_all(const wchar_t* path, ubuffer* out)
    {
        HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
        if (h == INVALID_HANDLE_VALUE) return false;

        LARGE_INTEGER sz;
        if (!GetFileSizeEx(h, &sz) || sz.QuadPart > 0x7FFFFFFF)
        {
            CloseHandle(h);
            return false;
        }

        out->clear();
        out->reserve((unsigned int)sz.QuadPart);
        if (out->cap < (unsigned int)sz.QuadPart + 1)
        {
            CloseHandle(h);
            return false;
        }

        DWORD read = 0;
        DWORD total = 0;
        while (total < (DWORD)sz.QuadPart)
        {
            if (!ReadFile(h, out->data + total, (DWORD)sz.QuadPart - total, &read, 0) || read == 0) break;
            total += read;
        }
        CloseHandle(h);
        out->size = total;
        return total == (DWORD)sz.QuadPart;
    }

    bool read_all(const char* path_utf8, ubuffer* out)
    {
        wchar_t wpath[1024];
        chartowcs(path_utf8, wpath, 1024);
        return read_all(wpath, out);
    }

    bool write_all(const wchar_t* path, const void* data, unsigned int size)
    {
        HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
        if (h == INVALID_HANDLE_VALUE) return false;

        DWORD written = 0;
        DWORD total = 0;
        bool ok = true;
        while (total < size)
        {
            if (!WriteFile(h, (const char*)data + total, size - total, &written, 0) || written == 0)
            {
                ok = false;
                break;
            }
            total += written;
        }
        CloseHandle(h);
        return ok;
    }

    bool write_all(const char* path_utf8, const void* data, unsigned int size)
    {
        wchar_t wpath[1024];
        chartowcs(path_utf8, wpath, 1024);
        return write_all(wpath, data, size);
    }

    bool exists(const wchar_t* path)
    {
        DWORD attr = GetFileAttributesW(path);
        return attr != INVALID_FILE_ATTRIBUTES;
    }

    bool app_dir(wchar_t* out, int out_chars)
    {
        wchar_t base[MAX_PATH];
        if (!SUCCEEDED(SHGetFolderPathW(0, CSIDL_LOCAL_APPDATA, 0, 0, base))) return false;

        int i = 0;
        while (base[i] && i < out_chars - 1) { out[i] = base[i]; i++; }

        const wchar_t* suffix = L"\\IMDiscord";
        int j = 0;
        while (suffix[j] && i < out_chars - 1) { out[i++] = suffix[j++]; }
        out[i] = 0;

        CreateDirectoryW(out, 0);
        return true;
    }

    bool app_path(const wchar_t* filename, wchar_t* out, int out_chars)
    {
        if (!app_dir(out, out_chars)) return false;

        int i = 0;
        while (out[i]) i++;
        if (i < out_chars - 1) out[i++] = L'\\';

        int j = 0;
        while (filename[j] && i < out_chars - 1) out[i++] = filename[j++];
        out[i] = 0;
        return true;
    }

    bool open_dialog(wchar_t* out, int out_chars)
    {
        if (out_chars < 4) return false;
        out[0] = 0;

        OPENFILENAMEW ofn;
        ccfset(&ofn, 0, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = GetActiveWindow();
        ofn.lpstrFile = out;
        ofn.nMaxFile = (DWORD)out_chars;
        ofn.lpstrFilter = L"All files\0*.*\0";
        ofn.nFilterIndex = 1;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
        return GetOpenFileNameW(&ofn) != 0;
    }

    bool save_dialog(const wchar_t* suggested_name, wchar_t* out, int out_chars)
    {
        if (out_chars < 4) return false;

        int i = 0;
        if (suggested_name)
        {
            while (suggested_name[i] && i < out_chars - 1)
            {
                wchar_t c = suggested_name[i];
                // Strip characters the shell will refuse.
                if (c == L'/' || c == L'\\' || c == L':' || c == L'*' || c == L'?' ||
                    c == L'\"' || c == L'<' || c == L'>' || c == L'|') c = L'_';
                out[i] = c;
                i++;
            }
        }
        out[i] = 0;

        OPENFILENAMEW ofn;
        ccfset(&ofn, 0, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = GetActiveWindow();
        ofn.lpstrFile = out;
        ofn.nMaxFile = (DWORD)out_chars;
        ofn.lpstrFilter = L"All files\0*.*\0";
        ofn.nFilterIndex = 1;
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR | OFN_EXPLORER;
        return GetSaveFileNameW(&ofn) != 0;
    }
}
