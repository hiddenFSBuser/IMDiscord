#include "pch.h"
#include "audio.h"
#include "core/log.h"
#include "core/wavdump.h"

#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <propvarutil.h>
#include <avrt.h>

// PKEY_Device_FriendlyName is only declared by the SDK headers; the definition
// lives in a lib this project does not link, so it is spelled out here.
static const PROPERTYKEY IMD_PKEY_Device_FriendlyName =
{
    { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } },
    14
};

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "avrt.lib")

namespace
{
    const int RING_FRAMES = 48000;                       // 1 s of headroom
    const int RING_SAMPLES = RING_FRAMES * AUDIO_CHANNELS;

    // Two frames of cushion before playback starts on the push-style rings
    // (media and stream audio). The call itself no longer passes through a
    // ring at all: it is mixed on demand inside the device callback.
    const int PREBUFFER_SAMPLES = AUDIO_FRAME_SAMPLES * AUDIO_CHANNELS * 2;

    // Samples the media/stream rings had to throw away because the writer got
    // ahead of the device. Each one is a hole in the output.
    volatile long g_out_overruns = 0;

    // Whether anything has been written to the media ring since it was last
    // emptied. Without it the render thread would pop from a ring nobody is
    // filling and count an underrun on every single buffer.
    volatile long g_media_live = 0;
    volatile long g_stream_live = 0;

    // Times a push ring ran dry partway through a buffer. The voice mixer
    // cannot appear here: it fills whatever the device asked for, with
    // silence standing in for whoever has nothing queued.
    volatile long g_out_underruns = 0;

    // How far to slide in and out of silence at a shortfall. Long enough to
    // be inaudible, short enough not to eat a syllable.
    const int EDGE_SAMPLES = 96;      // 1 ms, stereo

    struct ring
    {
        short* data;
        volatile long read_pos;
        volatile long write_pos;
        CRITICAL_SECTION lock;
        bool ready;
        bool primed;
        bool faded_out;     // last delivery ended in a ramp to silence

        void create()
        {
            data = (short*)memalloc(RING_SAMPLES * (int)sizeof(short));
            if (data) ccfset(data, 0, RING_SAMPLES * sizeof(short));
            read_pos = 0;
            write_pos = 0;
            primed = false;
            faded_out = false;
            InitializeCriticalSection(&lock);
            ready = true;
        }

        void destroy()
        {
            if (!ready) return;
            DeleteCriticalSection(&lock);
            if (data) memfree(data);
            data = 0;
            ready = false;
        }

        int available() const
        {
            long w = write_pos, r = read_pos;
            long diff = w - r;
            if (diff < 0) diff += RING_SAMPLES;
            return (int)diff;
        }

        void push(const short* src, int samples)
        {
            EnterCriticalSection(&lock);

            // Overrun: the oldest audio goes, in whole stereo frames. Sliding
            // the read side by single samples desyncs left from right for
            // everything that follows, which crackles until the next odd drop
            // happens to swap them back.
            int free_samples = RING_SAMPLES - available() - 1;
            if (free_samples < samples)
            {
                const int FRAME = AUDIO_FRAME_SAMPLES * AUDIO_CHANNELS;
                long need = samples - free_samples;
                long drop = ((need + FRAME - 1) / FRAME) * FRAME;
                long avail = available();
                if (drop > avail) drop = avail & ~1L;
                read_pos = (read_pos + drop) % RING_SAMPLES;
            }

            for (int i = 0; i < samples; i++)
            {
                data[write_pos] = src[i];
                write_pos++;
                if (write_pos >= RING_SAMPLES) write_pos = 0;
            }
            LeaveCriticalSection(&lock);
        }

        // Adds into the ring instead of overwriting, so several speakers mix.
        void mix(const short* src, int samples)
        {
            EnterCriticalSection(&lock);

            // Overrun. Abandoning queued audio the reader never took is bad
            // enough on its own, but it has to be cleared as it goes: pop is
            // what zeroes samples, and anything skipped past keeps its old
            // value and gets *added* to on the next lap. That sums two
            // unrelated moments of audio together, which is as loud and as
            // ugly as it sounds. Whole stereo frames at a time, so the
            // channel order survives the drop.
            int free_samples = RING_SAMPLES - available() - 1;
            if (free_samples < samples)
            {
                const int FRAME = AUDIO_FRAME_SAMPLES * AUDIO_CHANNELS;
                long need = samples - free_samples;
                long drop = ((need + FRAME - 1) / FRAME) * FRAME;
                long avail = available();
                if (drop > avail) drop = avail & ~1L;
                for (long i = 0; i < drop; i++)
                {
                    data[read_pos] = 0;
                    read_pos++;
                    if (read_pos >= RING_SAMPLES) read_pos = 0;
                }
                g_out_overruns++;
            }

            long pos = write_pos;
            for (int i = 0; i < samples; i++)
            {
                int v = data[pos] + src[i];
                if (v > 32767) v = 32767;
                if (v < -32768) v = -32768;
                data[pos] = (short)v;

                pos++;
                if (pos >= RING_SAMPLES) pos = 0;
            }
            write_pos = pos;
            LeaveCriticalSection(&lock);
        }

        bool pop(short* dst, int samples)
        {
            EnterCriticalSection(&lock);
            if (available() < samples)
            {
                LeaveCriticalSection(&lock);
                return false;
            }
            for (int i = 0; i < samples; i++)
            {
                dst[i] = data[read_pos];
                data[read_pos] = 0;
                read_pos = (read_pos + 1) % RING_SAMPLES;
            }
            LeaveCriticalSection(&lock);
            return true;
        }

        // Playback variant: takes whatever is queued and pads the rest with
        // silence. An all-or-nothing pop drops a whole device buffer whenever
        // the ring is a few samples short, which is exactly what a listener
        // hears as clicking.
        void pop_padded(short* dst, int samples)
        {
            EnterCriticalSection(&lock);

            int have = available();

            // Wait for a small cushion before starting, otherwise playback
            // rides the edge of the buffer and underruns continuously.
            if (!primed)
            {
                if (have < PREBUFFER_SAMPLES)
                {
                    LeaveCriticalSection(&lock);
                    ccfset(dst, 0, (size_t)samples * sizeof(short));
                    return;
                }
                primed = true;
            }

            int take = have < samples ? have : samples;
            for (int i = 0; i < take; i++)
            {
                dst[i] = data[read_pos];
                data[read_pos] = 0;
                read_pos = (read_pos + 1) % RING_SAMPLES;
            }
            for (int i = take; i < samples; i++) dst[i] = 0;

            // Coming back after a shortfall, the first sample is wherever the
            // waveform happens to resume - a step away from the silence that
            // preceded it. Slide into it.
            if (faded_out && take > 0)
            {
                int edge = take < EDGE_SAMPLES ? take : EDGE_SAMPLES;
                for (int i = 0; i < edge; i++)
                    dst[i] = (short)((int)dst[i] * i / edge);
                faded_out = false;
            }

            // Only a real shortfall counts as an underrun. Landing exactly on
            // empty is the normal case when the writer and the device are in
            // step, and treating that as a stall re-armed the cushion over and
            // over - silence punched into the middle of speech.
            if (take < samples)
            {
                // Dropping straight to zero mid-waveform is a click, and this
                // happens on every scheduling wobble. Ramp the tail of what
                // there was down into the silence that follows it instead.
                if (take > 0)
                {
                    int edge = take < EDGE_SAMPLES ? take : EDGE_SAMPLES;
                    for (int i = 0; i < edge; i++)
                    {
                        int at = take - edge + i;
                        dst[at] = (short)((int)dst[at] * (edge - 1 - i) / edge);
                    }
                }

                faded_out = true;
                primed = false;
                g_out_underruns++;
            }

            LeaveCriticalSection(&lock);
        }

        // Throws away the oldest queued samples without reading them. Kept
        // even so the stereo phase survives; callers pass whole frames.
        void drop_samples(int samples)
        {
            EnterCriticalSection(&lock);
            int avail = available();
            if (samples > avail) samples = avail;
            samples &= ~1;
            read_pos = (read_pos + samples) % RING_SAMPLES;
            LeaveCriticalSection(&lock);
        }
    };

    struct endpoint
    {
        IMMDeviceEnumerator* enumerator;
        IMMDevice* device;
        IAudioClient* client;
        IAudioCaptureClient* capture;
        IAudioRenderClient* render;
        HANDLE event;
        HANDLE thread;
        volatile long running;
        UINT32 buffer_frames;
    };

    wavdump::sink g_mix_dump;

    endpoint g_in;
    endpoint g_out;
    ring g_in_ring;
    ring g_media_ring;   // video playing in a chat
    ring g_stream_ring;  // a screen share being watched

    // The voice call's half of the output, produced on demand at the device's
    // pace rather than pushed into a ring ahead of it. Set once by voice::init.
    volatile voice_mix_fn g_voice_mix = 0;

    float g_in_gain = 1.0f;
    float g_out_gain = 1.0f;
    volatile long g_in_level = 0;   // scaled by 10000
    volatile long g_out_level = 0;
    char g_error[192];
    bool g_com_ready = false;

    wchar_t g_input_id[256];
    wchar_t g_output_id[256];
    char g_input_name[128];
    char g_output_name[128];

    void set_error(const char* what, HRESULT hr)
    {
        cnprint(g_error, sizeof(g_error), "%s (0x%08X)", what, (unsigned int)hr);
        log_line("audio: %s", g_error);
    }

    void fill_format(WAVEFORMATEX* wf)
    {
        ccfset(wf, 0, sizeof(WAVEFORMATEX));
        wf->wFormatTag = WAVE_FORMAT_PCM;
        wf->nChannels = AUDIO_CHANNELS;
        wf->nSamplesPerSec = AUDIO_SAMPLE_RATE;
        wf->wBitsPerSample = 16;
        wf->nBlockAlign = (WORD)(wf->nChannels * wf->wBitsPerSample / 8);
        wf->nAvgBytesPerSec = wf->nSamplesPerSec * wf->nBlockAlign;
        wf->cbSize = 0;
    }

    bool open_endpoint(endpoint* ep, bool capture_side)
    {
        ccfset(ep, 0, sizeof(*ep));

        HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), 0, CLSCTX_ALL,
                                      __uuidof(IMMDeviceEnumerator), (void**)&ep->enumerator);
        if (FAILED(hr)) { set_error("device enumerator", hr); return false; }

        const wchar_t* wanted = capture_side ? g_input_id : g_output_id;
        if (wanted[0])
        {
            hr = ep->enumerator->GetDevice(wanted, &ep->device);
            if (FAILED(hr))
            {
                log_line("audio: the selected device is gone, falling back to the default");
                ep->device = 0;
            }
        }

        if (!ep->device)
        {
            // eConsole rather than eCommunications: the communications role
            // makes Windows treat this as a call and duck every other app.
            hr = ep->enumerator->GetDefaultAudioEndpoint(capture_side ? eCapture : eRender,
                                                         eConsole, &ep->device);
            if (FAILED(hr))
            {
                hr = ep->enumerator->GetDefaultAudioEndpoint(capture_side ? eCapture : eRender,
                                                             eCommunications, &ep->device);
            }
            if (FAILED(hr)) { set_error("default endpoint", hr); return false; }
        }

        hr = ep->device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, 0, (void**)&ep->client);
        if (FAILED(hr)) { set_error("activate audio client", hr); return false; }

        WAVEFORMATEX wf;
        fill_format(&wf);

        // AUTOCONVERTPCM lets the engine accept our fixed format regardless of
        // what the device's mix format happens to be.
        DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                      AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                      AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;

        // Two periods of headroom rather than four. In a pull model whatever
        // still sits in the device buffer is pure latency - it has already
        // been mixed and cannot be revised - so the buffer is kept small and
        // simply topped up on every event.
        REFERENCE_TIME duration = 20 * 10000 * 2; // 40 ms
        hr = ep->client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, duration, 0, &wf, 0);
        if (FAILED(hr))
        {
            // Older engines reject the auto-convert flags; retry without them.
            flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
            hr = ep->client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, duration, 0, &wf, 0);
        }
        if (FAILED(hr)) { set_error("initialize audio client", hr); return false; }

        ep->event = CreateEventW(0, FALSE, FALSE, 0);
        hr = ep->client->SetEventHandle(ep->event);
        if (FAILED(hr)) { set_error("set event handle", hr); return false; }

        ep->client->GetBufferSize(&ep->buffer_frames);

        // Opt the session out of the ducking experience as well, so the volume
        // of other applications is left alone even if the device is flagged for
        // communications.
        IAudioSessionControl* session = 0;
        if (SUCCEEDED(ep->client->GetService(__uuidof(IAudioSessionControl), (void**)&session)) && session)
        {
            IAudioSessionControl2* session2 = 0;
            if (SUCCEEDED(session->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&session2)) && session2)
            {
                session2->SetDuckingPreference(TRUE);
                session2->Release();
            }
            session->Release();
        }

        if (capture_side)
            hr = ep->client->GetService(__uuidof(IAudioCaptureClient), (void**)&ep->capture);
        else
            hr = ep->client->GetService(__uuidof(IAudioRenderClient), (void**)&ep->render);

        if (FAILED(hr)) { set_error("get audio service", hr); return false; }
        return true;
    }

    void close_endpoint(endpoint* ep)
    {
        if (ep->client) ep->client->Stop();
        if (ep->capture) { ep->capture->Release(); ep->capture = 0; }
        if (ep->render) { ep->render->Release(); ep->render = 0; }
        if (ep->client) { ep->client->Release(); ep->client = 0; }
        if (ep->device) { ep->device->Release(); ep->device = 0; }
        if (ep->enumerator) { ep->enumerator->Release(); ep->enumerator = 0; }
        if (ep->event) { CloseHandle(ep->event); ep->event = 0; }
    }

    DWORD WINAPI capture_thread(LPVOID)
    {
        CoInitializeEx(0, COINIT_MULTITHREADED);

        // This thread and the render one sit on the device clock's critical
        // path now: a scheduling stall is a hole in the audio. MMCSS "Pro
        // Audio" is what miniaudio asks for its callback threads for the
        // same reason.
        DWORD mmcss_task = 0;
        HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &mmcss_task);

        g_in.client->Start();

        while (g_in.running)
        {
            if (WaitForSingleObject(g_in.event, 200) != WAIT_OBJECT_0) continue;

            UINT32 packet = 0;
            while (SUCCEEDED(g_in.capture->GetNextPacketSize(&packet)) && packet > 0)
            {
                BYTE* data = 0;
                UINT32 frames = 0;
                DWORD flags = 0;
                if (FAILED(g_in.capture->GetBuffer(&data, &frames, &flags, 0, 0))) break;

                if (frames)
                {
                    int samples = (int)frames * AUDIO_CHANNELS;
                    short* pcm = (short*)data;

                    if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
                    {
                        short* silence = (short*)memalloc(samples * (int)sizeof(short));
                        if (silence)
                        {
                            ccfset(silence, 0, samples * sizeof(short));
                            g_in_ring.push(silence, samples);
                            memfree(silence);
                        }
                        InterlockedExchange(&g_in_level, 0);
                    }
                    else
                    {
                        long peak = 0;
                        short* scaled = (short*)memalloc(samples * (int)sizeof(short));
                        if (scaled)
                        {
                            for (int i = 0; i < samples; i++)
                            {
                                int v = (int)(pcm[i] * g_in_gain);
                                if (v > 32767) v = 32767;
                                if (v < -32768) v = -32768;
                                scaled[i] = (short)v;
                                long a = v < 0 ? -v : v;
                                if (a > peak) peak = a;
                            }
                            g_in_ring.push(scaled, samples);
                            memfree(scaled);
                        }
                        InterlockedExchange(&g_in_level, peak * 10000 / 32768);
                    }
                }

                g_in.capture->ReleaseBuffer(frames);
            }
        }

        g_in.client->Stop();
        if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
        CoUninitialize();
        return 0;
    }

    DWORD WINAPI render_thread(LPVOID)
    {
        CoInitializeEx(0, COINIT_MULTITHREADED);

        // Same MMCSS reason as the capture thread: this one fills the device
        // buffer on every event, and a late run is heard as a hole.
        DWORD mmcss_task = 0;
        HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &mmcss_task);

        // Prime the buffer with silence so the stream starts cleanly.
        BYTE* data = 0;
        if (SUCCEEDED(g_out.render->GetBuffer(g_out.buffer_frames, &data)))
            g_out.render->ReleaseBuffer(g_out.buffer_frames, AUDCLNT_BUFFERFLAGS_SILENT);

        g_out.client->Start();

        while (g_out.running)
        {
            if (WaitForSingleObject(g_out.event, 200) != WAIT_OBJECT_0) continue;

            UINT32 padding = 0;
            if (FAILED(g_out.client->GetCurrentPadding(&padding))) continue;

            UINT32 free_frames = g_out.buffer_frames - padding;
            if (!free_frames) continue;

            // The whole free space is filled every time, keeping the device
            // topped up to its small buffer. This is the abaddon model: the
            // sound card asks, the sources answer, and whatever a source does
            // not have is silence. Nothing queues ahead of the device, so
            // there is no second clock for it to drift against.
            if (FAILED(g_out.render->GetBuffer(free_frames, &data))) continue;

            int samples = (int)free_frames * AUDIO_CHANNELS;
            short* dst = (short*)data;
            ccfset(dst, 0, (size_t)samples * sizeof(short));

            // The voice call, mixed straight into the device buffer.
            voice_mix_fn mix = g_voice_mix;
            if (mix) mix(dst, samples);

            // A video playing in a chat and a screen share being watched,
            // added on top of the call. Summed with a clamp rather than
            // replacing: hearing somebody talk over a clip is the normal
            // case, not a conflict to be resolved.
            if (g_media_live || g_stream_live)
            {
                short extra[4096];
                int chunk = samples < 4096 ? samples : 4096;

                if (g_media_live)
                {
                    g_media_ring.pop_padded(extra, chunk);
                    for (int i = 0; i < chunk; i++)
                    {
                        int v = (int)dst[i] + (int)extra[i];
                        if (v > 32767) v = 32767;
                        if (v < -32768) v = -32768;
                        dst[i] = (short)v;
                    }
                }

                if (g_stream_live)
                {
                    g_stream_ring.pop_padded(extra, chunk);
                    for (int i = 0; i < chunk; i++)
                    {
                        int v = (int)dst[i] + (int)extra[i];
                        if (v > 32767) v = 32767;
                        if (v < -32768) v = -32768;
                        dst[i] = (short)v;
                    }
                }
            }

            // A soft knee instead of a hard wall: linear up to KNEE, then an
            // asymptotic squeeze towards full scale. Whatever the pipeline
            // still gets wrong - a decoder limit cycle, a hot mix of three
            // people shouting - comes out loud instead of shattering.
            const int KNEE = 28000;
            const int KNEE_ROOM = 4767;   // 32767 - 28000

            long peak = 0;
            for (int i = 0; i < samples; i++)
            {
                int v = (int)(dst[i] * g_out_gain);
                int a = v < 0 ? -v : v;
                if (a > KNEE)
                {
                    long long over = (long long)a - KNEE;
                    a = (int)(KNEE + over * KNEE_ROOM / (over + KNEE_ROOM));
                }
                v = v < 0 ? -a : a;
                dst[i] = (short)v;
                if ((long)a > peak) peak = a;
            }
            InterlockedExchange(&g_out_level, peak * 10000 / 32768);

            // The other end of the tap: exactly what the device is about to
            // play, after every source and every gain. A defect present both
            // in a speaker's file and here came out of the codec; one present
            // only here was made in between the two.
            if (wavdump::enabled())
            {
                if (!g_mix_dump.name[0]) wavdump::start(&g_mix_dump, L"dump_output.wav");
                wavdump::write(&g_mix_dump, dst, samples);
            }

            g_out.render->ReleaseBuffer(free_frames, 0);
        }

        g_out.client->Stop();
        if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
        CoUninitialize();
        return 0;
    }
}

namespace
{
    // Reads the PKEY_Device_FriendlyName property into a utf-8 buffer.
    void read_friendly_name(IMMDevice* device, char* out, int cap)
    {
        out[0] = 0;

        IPropertyStore* props = 0;
        if (FAILED(device->OpenPropertyStore(STGM_READ, &props)) || !props) return;

        PROPVARIANT value;
        PropVariantInit(&value);

        if (SUCCEEDED(props->GetValue(IMD_PKEY_Device_FriendlyName, &value)) &&
            value.vt == VT_LPWSTR && value.pwszVal)
        {
            wcstochar(value.pwszVal, out, cap);
        }

        PropVariantClear(&value);
        props->Release();
    }
}

int audio::list_devices(bool capture, audio_device* out, int cap)
{
    if (cap < 1) return 0;

    // The first entry is always the system default.
    ccfset(&out[0], 0, sizeof(audio_device));
    ccstrncpy(out[0].name, "Системное устройство по умолчанию", sizeof(out[0].name) - 1);
    int count = 1;

    IMMDeviceEnumerator* enumerator = 0;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), 0, CLSCTX_ALL,
                                __uuidof(IMMDeviceEnumerator), (void**)&enumerator)) || !enumerator)
        return count;

    IMMDeviceCollection* devices = 0;
    if (SUCCEEDED(enumerator->EnumAudioEndpoints(capture ? eCapture : eRender,
                                                 DEVICE_STATE_ACTIVE, &devices)) && devices)
    {
        UINT total = 0;
        devices->GetCount(&total);

        for (UINT i = 0; i < total && count < cap; i++)
        {
            IMMDevice* device = 0;
            if (FAILED(devices->Item(i, &device)) || !device) continue;

            LPWSTR id = 0;
            if (SUCCEEDED(device->GetId(&id)) && id)
            {
                ccfset(&out[count], 0, sizeof(audio_device));

                int k = 0;
                while (id[k] && k < 255) { out[count].id[k] = id[k]; k++; }
                out[count].id[k] = 0;

                read_friendly_name(device, out[count].name, sizeof(out[count].name));
                if (!out[count].name[0])
                    ccstrncpy(out[count].name, "Без имени", sizeof(out[count].name) - 1);

                count++;
                CoTaskMemFree(id);
            }
            device->Release();
        }
        devices->Release();
    }

    enumerator->Release();
    return count;
}

void audio::set_device(bool capture, const wchar_t* id)
{
    wchar_t* target = capture ? g_input_id : g_output_id;
    char* name = capture ? g_input_name : g_output_name;

    ccfset(target, 0, 256 * sizeof(wchar_t));
    ccfset(name, 0, 128);

    if (id && id[0])
    {
        int i = 0;
        while (id[i] && i < 255) { target[i] = id[i]; i++; }
        target[i] = 0;
    }

    // Restart whichever side is running so the change takes effect now.
    if (capture && g_in.running) { audio::stop_capture(); audio::start_capture(); }
    if (!capture && g_out.running) { audio::stop_render(); audio::start_render(); }
}

const wchar_t* audio::device(bool capture)
{
    return capture ? g_input_id : g_output_id;
}

const char* audio::device_name(bool capture)
{
    const wchar_t* id = capture ? g_input_id : g_output_id;
    char* cached = capture ? g_input_name : g_output_name;

    if (!id[0]) return "Системное устройство по умолчанию";
    if (cached[0]) return cached;

    audio_device list[32];
    int count = list_devices(capture, list, 32);
    for (int i = 0; i < count; i++)
    {
        if (ccwcmp(list[i].id, id) == 0)
        {
            ccstrncpy(cached, list[i].name, 127);
            return cached;
        }
    }
    return "Устройство недоступно";
}

bool audio::init()
{
    if (!g_com_ready)
    {
        g_in_ring.create();
        g_media_ring.create();
        g_stream_ring.create();
        g_com_ready = true;
    }
    return true;
}

void audio::shutdown()
{
    stop_capture();
    stop_render();
    // Without this the output dump keeps a zeroed wav header and no player
    // will open it.
    wavdump::finish(&g_mix_dump);
    g_in_ring.destroy();
    g_media_ring.destroy();
    g_stream_ring.destroy();
    g_com_ready = false;
}

bool audio::start_capture()
{
    if (g_in.running) return true;
    if (!open_endpoint(&g_in, true))
    {
        close_endpoint(&g_in);
        return false;
    }

    g_in.running = 1;
    g_in.thread = CreateThread(0, 0, capture_thread, 0, 0, 0);
    log_line("audio: capture started");
    return true;
}

void audio::stop_capture()
{
    if (!g_in.running) return;

    g_in.running = 0;
    SetEvent(g_in.event);
    if (g_in.thread)
    {
        WaitForSingleObject(g_in.thread, 2000);
        CloseHandle(g_in.thread);
        g_in.thread = 0;
    }
    close_endpoint(&g_in);
    log_line("audio: capture stopped");
}

bool audio::capture_active() { return g_in.running != 0; }

bool audio::read_capture_frame(short* out)
{
    return g_in_ring.pop(out, AUDIO_FRAME_SAMPLES * AUDIO_CHANNELS);
}

void audio::trim_capture(int keep_samples)
{
    if (!g_in_ring.ready) return;

    // Fed by the device clock, drained by the voice thread's wall clock: any
    // drift or stall between the two accumulates here. Past the cushion the
    // oldest audio is dropped unsent - late audio is how a call ends up
    // hearing itself talk over itself, and a full ring is worse than a gap.
    int extra = g_in_ring.available() - keep_samples;
    if (extra > 0)
        g_in_ring.drop_samples(extra - (extra % (AUDIO_FRAME_SAMPLES * AUDIO_CHANNELS)));
}

bool audio::start_render()
{
    if (g_out.running) return true;
    if (!open_endpoint(&g_out, false))
    {
        close_endpoint(&g_out);
        return false;
    }

    g_out.running = 1;
    g_out.thread = CreateThread(0, 0, render_thread, 0, 0, 0);
    log_line("audio: render started");
    return true;
}

void audio::stop_render()
{
    if (!g_out.running) return;

    g_out.running = 0;
    SetEvent(g_out.event);
    if (g_out.thread)
    {
        WaitForSingleObject(g_out.thread, 2000);
        CloseHandle(g_out.thread);
        g_out.thread = 0;
    }
    close_endpoint(&g_out);
    log_line("audio: render stopped");
}

bool audio::render_active() { return g_out.running != 0; }

void audio::write_media(const short* pcm, int samples)
{
    if (!g_media_ring.ready || samples <= 0) return;

    InterlockedExchange(&g_media_live, 1);
    g_media_ring.mix(pcm, samples);
}

unsigned int audio::media_backlog_ms()
{
    int samples = g_media_ring.ready ? g_media_ring.available() : 0;
    return (unsigned int)(samples / AUDIO_CHANNELS * 1000 / AUDIO_SAMPLE_RATE);
}

void audio::clear_media()
{
    if (!g_media_ring.ready) return;

    // Dropping what is queued is the point: a video that was stopped should
    // go quiet now, not after the half second already handed over.
    short scratch[AUDIO_FRAME_SAMPLES * AUDIO_CHANNELS];
    while (g_media_ring.available() >= AUDIO_FRAME_SAMPLES * AUDIO_CHANNELS)
        g_media_ring.pop(scratch, AUDIO_FRAME_SAMPLES * AUDIO_CHANNELS);

    InterlockedExchange(&g_media_live, 0);
}

void audio::write_stream(const short* pcm, int samples)
{
    if (!g_stream_ring.ready || samples <= 0) return;

    InterlockedExchange(&g_stream_live, 1);
    g_stream_ring.mix(pcm, samples);
}

unsigned int audio::stream_backlog_ms()
{
    int samples = g_stream_ring.ready ? g_stream_ring.available() : 0;
    return (unsigned int)(samples / AUDIO_CHANNELS * 1000 / AUDIO_SAMPLE_RATE);
}

void audio::clear_stream()
{
    if (!g_stream_ring.ready) return;

    short scratch[AUDIO_FRAME_SAMPLES * AUDIO_CHANNELS];
    while (g_stream_ring.available() >= AUDIO_FRAME_SAMPLES * AUDIO_CHANNELS)
        g_stream_ring.pop(scratch, AUDIO_FRAME_SAMPLES * AUDIO_CHANNELS);

    InterlockedExchange(&g_stream_live, 0);
}

void audio::set_voice_mixer(voice_mix_fn fn)
{
    g_voice_mix = fn;
}

unsigned int audio::render_overruns() { return (unsigned int)g_out_overruns; }
unsigned int audio::render_underruns() { return (unsigned int)g_out_underruns; }

void audio::reset_render_overruns()
{
    InterlockedExchange(&g_out_overruns, 0);
    InterlockedExchange(&g_out_underruns, 0);
}

unsigned int audio::render_backlog_ms()
{
    // What the device is still holding. That number is the playback latency
    // now that nothing queues ahead of it.
    if (!g_out.running || !g_out.client) return 0;

    UINT32 padding = 0;
    if (FAILED(g_out.client->GetCurrentPadding(&padding))) return 0;
    return (unsigned int)(padding * 1000 / AUDIO_SAMPLE_RATE);
}

void audio::set_input_gain(float gain) { g_in_gain = gain; }
void audio::set_output_gain(float gain) { g_out_gain = gain; }
float audio::input_gain() { return g_in_gain; }
float audio::output_gain() { return g_out_gain; }

float audio::input_level() { return (float)g_in_level / 10000.0f; }
float audio::output_level() { return (float)g_out_level / 10000.0f; }

const char* audio::last_error() { return g_error; }
