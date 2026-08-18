#include "pch.h"
#include <dwmapi.h>
#include <mmdeviceapi.h>
#include "oslib.h"
#include "log.h"

namespace
{
    typedef HRESULT (WINAPI *pfn_DwmGetWindowAttribute)(HWND, DWORD, PVOID, DWORD);
    typedef HRESULT (WINAPI *pfn_ActivateAudioInterfaceAsync)(
        LPCWSTR, REFIID, PROPVARIANT*,
        IActivateAudioInterfaceCompletionHandler*,
        IActivateAudioInterfaceAsyncOperation**);

    HMODULE g_dwm = 0;
    HMODULE g_mm = 0;

    pfn_DwmGetWindowAttribute g_dwm_attr = 0;
    pfn_ActivateAudioInterfaceAsync g_activate = 0;

    bool g_ready = false;
    char g_missing[192] = { 0 };

    void note_missing(const char* what)
    {
        if (g_missing[0]) ccstrncpy(g_missing + ccslenf(g_missing), ", ",
                                    sizeof(g_missing) - (int)ccslenf(g_missing) - 1);
        ccstrncpy(g_missing + ccslenf(g_missing), what,
                  sizeof(g_missing) - (int)ccslenf(g_missing) - 1);
    }
}

namespace
{
    // Loading a system library by bare name would take one sitting next to the
    // exe first, which is a way to be handed somebody else's code. The search
    // flag that prevents it is Windows 8, and Windows 7 only with an update
    // installed - so where it is refused, the full path is spelled out
    // instead. Same guarantee, older mechanism.
    HMODULE load_system(const wchar_t* name)
    {
        HMODULE lib = LoadLibraryExW(name, 0, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (lib) return lib;

        if (GetLastError() != ERROR_INVALID_PARAMETER) return 0;

        wchar_t path[MAX_PATH];
        UINT len = GetSystemDirectoryW(path, MAX_PATH);
        if (!len || len >= MAX_PATH - 2) return 0;

        path[len] = L'\\';
        path[len + 1] = 0;

        for (int i = 0; name[i] && len + 1 + i < MAX_PATH - 1; i++)
        {
            path[len + 1 + i] = name[i];
            path[len + 2 + i] = 0;
        }

        return LoadLibraryW(path);
    }
}

void oslib::init()
{
    if (g_ready) return;
    g_ready = true;
    g_missing[0] = 0;

    // From system32 by name only. Loading a system library the ordinary way
    // would take one sitting next to the exe first.
    g_dwm = load_system(L"dwmapi.dll");
    if (g_dwm)
        g_dwm_attr = (pfn_DwmGetWindowAttribute)GetProcAddress(g_dwm, "DwmGetWindowAttribute");
    if (!g_dwm_attr) note_missing("dwmapi");

    g_mm = load_system(L"mmdevapi.dll");
    if (g_mm)
        g_activate = (pfn_ActivateAudioInterfaceAsync)
            GetProcAddress(g_mm, "ActivateAudioInterfaceAsync");
    if (!g_activate) note_missing(tr("захват звука по процессу"));

    if (g_missing[0]) log_line("oslib: этой системе не хватает: %s", g_missing);
    else              log_line("oslib: всё нужное на месте");
}

void oslib::shutdown()
{
    g_dwm_attr = 0;
    g_activate = 0;

    if (g_dwm) { FreeLibrary(g_dwm); g_dwm = 0; }
    if (g_mm) { FreeLibrary(g_mm); g_mm = 0; }
    g_ready = false;
}

bool oslib::window_cloaked(void* hwnd)
{
    if (!g_dwm_attr || !hwnd) return false;

    BOOL cloaked = FALSE;
    // DWMWA_CLOAKED is a Windows 8 attribute. An older system returns an
    // error here, and "not cloaked" is the right answer for it: there are no
    // virtual desktops and no suspended store apps to be hidden by.
    if (FAILED(g_dwm_attr((HWND)hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))))
        return false;

    return cloaked != FALSE;
}

bool oslib::window_frame_bounds(void* hwnd, RECT* out)
{
    if (!g_dwm_attr || !hwnd || !out) return false;

    RECT r;
    if (FAILED(g_dwm_attr((HWND)hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r))))
        return false;

    if (r.right <= r.left || r.bottom <= r.top) return false;

    *out = r;
    return true;
}

bool oslib::has_process_loopback() { return g_activate != 0; }

HRESULT oslib::activate_audio_interface(const wchar_t* device_id, const GUID& iid,
                                        const PROPVARIANT* params,
                                        IActivateAudioInterfaceCompletionHandler* handler,
                                        IActivateAudioInterfaceAsyncOperation** out)
{
    if (!g_activate) return E_NOTIMPL;

    return g_activate(device_id, iid, (PROPVARIANT*)params, handler, out);
}

const char* oslib::missing() { return g_missing; }
