#pragma once

// The parts of Windows that are not on every Windows.
//
// Two libraries here are newer than the oldest system this client is meant to
// run on, and each fails differently:
//
//   dwmapi     is present back to Vista, but the attributes asked of it are
//              not. On Windows 7 a query for "is this window cloaked" simply
//              returns an error, and the honest reading of that is "no".
//   mmdevapi   is present, but ActivateAudioInterfaceAsync arrived in
//              Windows 8. Imported the usual way it stops the process from
//              starting at all - the loader refuses before a line of this
//              client runs.
//
// Both are therefore loaded by hand, and every entry point has an answer for
// the case where it is not there. A feature that cannot work says so; nothing
// prevents the client from opening.

struct IActivateAudioInterfaceAsyncOperation;
struct IActivateAudioInterfaceCompletionHandler;

namespace oslib
{
    void init();
    void shutdown();

    // ---- dwm ----

    // Whether the compositor is hiding this window: another virtual desktop,
    // or a suspended store app. False when the system cannot answer, which on
    // Windows 7 it never can - and there are no virtual desktops there to ask
    // about either.
    bool window_cloaked(void* hwnd);

    // The window's true frame, without the invisible resize border that
    // GetWindowRect has included since Windows 10. False when unavailable, and
    // the caller should fall back to GetWindowRect.
    bool window_frame_bounds(void* hwnd, RECT* out);

    // ---- audio ----

    // Whether per process loopback capture exists on this system at all.
    bool has_process_loopback();

    // ActivateAudioInterfaceAsync, or E_NOTIMPL where it does not exist.
    HRESULT activate_audio_interface(const wchar_t* device_id, const GUID& iid,
                                     const PROPVARIANT* params,
                                     IActivateAudioInterfaceCompletionHandler* handler,
                                     IActivateAudioInterfaceAsyncOperation** out);

    // What is missing on this machine, for the log. Empty when all present.
    const char* missing();
}
