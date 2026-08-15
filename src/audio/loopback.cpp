#include "pch.h"
#include "loopback.h"
#include "audio.h"
#include "core/log.h"
#include "core/oslib.h"

#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>

namespace
{
    const int RING_SAMPLES = 48000 * 2;      // two seconds of mono headroom

    IAudioClient* g_client = 0;
    IAudioCaptureClient* g_capture = 0;

    HANDLE g_thread = 0;
    HANDLE g_ready_event = 0;      // signalled by the device when data is due
    HANDLE g_stop_event = 0;
    volatile long g_running = 0;

    short* g_ring = 0;
    volatile long g_read = 0;
    volatile long g_write = 0;
    CRITICAL_SECTION g_lock;
    bool g_lock_ready = false;

    volatile long g_level = 0;
    char g_error[192] = { 0 };

    void set_error(const char* what, HRESULT hr)
    {
        cnprint(g_error, sizeof(g_error), "%s (0x%08x)", what, (unsigned int)hr);
        log_line("loopback: %s", g_error);
    }

    int available()
    {
        long w = g_write, r = g_read;
        long diff = w - r;
        if (diff < 0) diff += RING_SAMPLES;
        return (int)diff;
    }

    void push(const short* mono, int count)
    {
        EnterCriticalSection(&g_lock);
        for (int i = 0; i < count; i++)
        {
            long next = (g_write + 1) % RING_SAMPLES;
            if (next == g_read)
            {
                // Nobody is draining, which means the share is not running.
                // Dropping the oldest keeps this bounded.
                g_read = (g_read + 1) % RING_SAMPLES;
            }
            g_ring[g_write] = mono[i];
            g_write = next;
        }
        LeaveCriticalSection(&g_lock);
    }

    // ActivateAudioInterfaceAsync answers on a thread of its own, so the call
    // that started it has to wait for this. A COM object with three methods
    // and no base class to inherit them from - there is no ATL here.
    struct activation_handler : IActivateAudioInterfaceCompletionHandler
    {
        HANDLE done;
        HRESULT result;
        IAudioClient* client;

        activation_handler() : done(0), result(E_FAIL), client(0) {}

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out)
        {
            if (!out) return E_POINTER;

            if (riid == __uuidof(IUnknown) ||
                riid == __uuidof(IActivateAudioInterfaceCompletionHandler))
            {
                *out = this;
                return S_OK;
            }

            // The activation runs on a thread of the system's choosing and
            // calls back across apartments. Microsoft's own sample gets this
            // by deriving from FtmBase; there is no WRL here, so agility is
            // claimed directly. Without it the call is refused outright
            // rather than failing later - which is what an illegal method
            // call meant here.
            if (riid == __uuidof(IAgileObject))
            {
                *out = this;
                return S_OK;
            }

            *out = 0;
            return E_NOINTERFACE;
        }

        // Lifetime is the caller's stack frame, which outlives the wait.
        ULONG STDMETHODCALLTYPE AddRef() { return 1; }
        ULONG STDMETHODCALLTYPE Release() { return 1; }

        HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation* op)
        {
            HRESULT hr = E_FAIL;
            IUnknown* unknown = 0;

            if (op) op->GetActivateResult(&hr, &unknown);

            if (SUCCEEDED(hr) && unknown)
                unknown->QueryInterface(__uuidof(IAudioClient), (void**)&client);

            if (unknown) unknown->Release();

            result = SUCCEEDED(hr) && client ? S_OK : (FAILED(hr) ? hr : E_FAIL);
            SetEvent(done);
            return S_OK;
        }
    };

    DWORD WINAPI capture_thread(LPVOID)
    {
        CoInitializeEx(0, COINIT_MULTITHREADED);

        // The format asked for below, so the conversion is fixed and known.
        const int CHANNELS = 2;

        while (g_running)
        {
            DWORD waited = WaitForSingleObject(g_ready_event, 200);
            if (!g_running) break;
            if (waited != WAIT_OBJECT_0) continue;

            for (;;)
            {
                BYTE* data = 0;
                UINT32 frames = 0;
                DWORD flags = 0;

                HRESULT hr = g_capture->GetBuffer(&data, &frames, &flags, 0, 0);
                if (hr != S_OK || !frames)
                {
                    if (hr == S_OK) g_capture->ReleaseBuffer(frames);
                    break;
                }

                short mono[4096];
                int made = 0;
                long peak = 0;

                bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
                const float* src = (const float*)data;

                for (UINT32 i = 0; i < frames && made < 4096; i++)
                {
                    float sum = 0.0f;
                    if (!silent)
                    {
                        for (int c = 0; c < CHANNELS; c++) sum += src[i * CHANNELS + c];
                        sum /= (float)CHANNELS;
                    }

                    int v = (int)(sum * 32767.0f);
                    if (v > 32767) v = 32767;
                    if (v < -32768) v = -32768;

                    mono[made++] = (short)v;

                    long a = v < 0 ? -v : v;
                    if (a > peak) peak = a;
                }

                g_capture->ReleaseBuffer(frames);

                if (made) push(mono, made);
                InterlockedExchange(&g_level, peak * 10000 / 32768);
            }
        }

        CoUninitialize();
        return 0;
    }
}

bool loopback::start()
{
    if (g_running) return true;

    g_error[0] = 0;

    // Said once, plainly, instead of surfacing as a mysterious activation
    // failure further down.
    if (!oslib::has_process_loopback())
    {
        ccstrncpy(g_error, "эта версия Windows не умеет захват в обход своего процесса",
                  sizeof(g_error) - 1);
        log_line("loopback: %s", g_error);
        return false;
    }

    if (!g_lock_ready)
    {
        InitializeCriticalSection(&g_lock);
        g_lock_ready = true;
    }

    if (!g_ring)
    {
        g_ring = (short*)memalloc(RING_SAMPLES * (int)sizeof(short));
        if (!g_ring) { ccstrncpy(g_error, "нет памяти", sizeof(g_error) - 1); return false; }
    }
    g_read = 0;
    g_write = 0;

    // Everything the machine is playing except this process and anything it
    // started. That exclusion is the whole reason this exists.
    AUDIOCLIENT_ACTIVATION_PARAMS params;
    ccfset(&params, 0, sizeof(params));
    params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    params.ProcessLoopbackParams.TargetProcessId = GetCurrentProcessId();
    params.ProcessLoopbackParams.ProcessLoopbackMode =
        PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE;

    PROPVARIANT pv;
    ccfset(&pv, 0, sizeof(pv));
    pv.vt = VT_BLOB;
    pv.blob.cbSize = sizeof(params);
    pv.blob.pBlobData = (BYTE*)&params;

    activation_handler handler;
    handler.done = CreateEventW(0, TRUE, FALSE, 0);
    if (!handler.done) { ccstrncpy(g_error, "нет события", sizeof(g_error) - 1); return false; }

    IActivateAudioInterfaceAsyncOperation* op = 0;
    HRESULT hr = oslib::activate_audio_interface(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
                                                 __uuidof(IAudioClient), &pv, &handler, &op);
    if (FAILED(hr))
    {
        CloseHandle(handler.done);
        if (op) op->Release();
        // 0x8000000e is E_ILLEGAL_METHOD_CALL, which is what this returns on a
        // windows that has no per process loopback at all. Worth naming: it
        // is the difference between "misconfigured" and "this build cannot".
        if (hr == E_ILLEGAL_METHOD_CALL)
            ccstrncpy(g_error, "эта версия Windows не умеет захват в обход своего процесса",
                      sizeof(g_error) - 1);
        else
            set_error("захват без своего процесса не активировался", hr);

        log_line("loopback: ActivateAudioInterfaceAsync 0x%08x", (unsigned int)hr);
        return false;
    }

    WaitForSingleObject(handler.done, 4000);
    CloseHandle(handler.done);
    if (op) op->Release();

    if (FAILED(handler.result) || !handler.client)
    {
        set_error("захват звука не активировался", handler.result);
        return false;
    }

    g_client = handler.client;

    // The mix format cannot be asked for on a process loopback client, so one
    // is stated outright. Float at 48 kHz stereo is what the mixer works in,
    // and the encoder downstream wants 48 kHz anyway.
    WAVEFORMATEX wf;
    ccfset(&wf, 0, sizeof(wf));
    wf.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    wf.nChannels = 2;
    wf.nSamplesPerSec = 48000;
    wf.wBitsPerSample = 32;
    wf.nBlockAlign = (WORD)(wf.nChannels * wf.wBitsPerSample / 8);
    wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;
    wf.cbSize = 0;

    hr = g_client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                              AUDCLNT_STREAMFLAGS_LOOPBACK |
                              AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                              2000000,     // 200 ms, in 100 ns units
                              0, &wf, 0);
    if (FAILED(hr))
    {
        set_error("Initialize", hr);
        loopback::stop();
        return false;
    }

    g_ready_event = CreateEventW(0, FALSE, FALSE, 0);
    g_stop_event = CreateEventW(0, TRUE, FALSE, 0);
    if (!g_ready_event || !g_stop_event)
    {
        ccstrncpy(g_error, "нет события", sizeof(g_error) - 1);
        loopback::stop();
        return false;
    }

    hr = g_client->SetEventHandle(g_ready_event);
    if (FAILED(hr)) { set_error("SetEventHandle", hr); loopback::stop(); return false; }

    hr = g_client->GetService(__uuidof(IAudioCaptureClient), (void**)&g_capture);
    if (FAILED(hr) || !g_capture)
    {
        set_error("GetService(capture)", hr);
        loopback::stop();
        return false;
    }

    hr = g_client->Start();
    if (FAILED(hr)) { set_error("Start", hr); loopback::stop(); return false; }

    InterlockedExchange(&g_running, 1);
    g_thread = CreateThread(0, 0, capture_thread, 0, 0, 0);
    if (!g_thread)
    {
        InterlockedExchange(&g_running, 0);
        ccstrncpy(g_error, "поток не запустился", sizeof(g_error) - 1);
        loopback::stop();
        return false;
    }

    log_line("loopback: звук системы захватывается, свой процесс исключён");
    return true;
}

void loopback::stop()
{
    if (g_running)
    {
        InterlockedExchange(&g_running, 0);
        if (g_stop_event) SetEvent(g_stop_event);
        if (g_ready_event) SetEvent(g_ready_event);
    }

    if (g_thread)
    {
        WaitForSingleObject(g_thread, 2000);
        CloseHandle(g_thread);
        g_thread = 0;
    }

    if (g_client) g_client->Stop();

    if (g_capture) { g_capture->Release(); g_capture = 0; }
    if (g_client) { g_client->Release(); g_client = 0; }

    if (g_ready_event) { CloseHandle(g_ready_event); g_ready_event = 0; }
    if (g_stop_event) { CloseHandle(g_stop_event); g_stop_event = 0; }

    InterlockedExchange(&g_level, 0);
}

bool loopback::running() { return g_running != 0; }

bool loopback::read_frame(short* mono, int samples)
{
    if (!g_running || !g_ring || !mono || samples <= 0) return false;

    EnterCriticalSection(&g_lock);

    if (available() < samples)
    {
        LeaveCriticalSection(&g_lock);
        return false;
    }

    for (int i = 0; i < samples; i++)
    {
        mono[i] = g_ring[g_read];
        g_read = (g_read + 1) % RING_SAMPLES;
    }

    LeaveCriticalSection(&g_lock);
    return true;
}

float loopback::level() { return (float)g_level / 10000.0f; }
const char* loopback::last_error() { return g_error; }

bool loopback::self_test()
{
    CoInitializeEx(0, COINIT_MULTITHREADED);

    OSVERSIONINFOEXW os;
    ccfset(&os, 0, sizeof(os));
    os.dwOSVersionInfoSize = sizeof(os);

    // GetVersionEx lies to unmanifested processes; the build number in the
    // registry does not.
    {
        HKEY key = 0;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                          L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                          0, KEY_READ, &key) == ERROR_SUCCESS)
        {
            wchar_t build[32];
            DWORD size = sizeof(build);
            if (RegQueryValueExW(key, L"CurrentBuildNumber", 0, 0,
                                 (LPBYTE)build, &size) == ERROR_SUCCESS)
            {
                char narrow[32];
                wcstochar(build, narrow, (int)sizeof(narrow));
                log_line("audiotest: сборка Windows %s", narrow);
            }
            RegCloseKey(key);
        }
    }

    bool ok = loopback::start();
    log_line("audiotest: захват без своего процесса - %s", ok ? "работает" : loopback::last_error());

    if (ok)
    {
        // Give it a moment to produce something, so "started" is not confused
        // with "actually delivers".
        short frame[AUDIO_FRAME_SAMPLES];
        int got = 0;
        for (int i = 0; i < 200 && got < 5; i++)
        {
            if (loopback::read_frame(frame, AUDIO_FRAME_SAMPLES)) got++;
            else Sleep(10);
        }
        log_line("audiotest: получено кадров за две секунды: %d", got);
        loopback::stop();
    }

    CoUninitialize();
    return ok;
}
