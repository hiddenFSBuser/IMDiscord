#include "pch.h"
// WIN32_LEAN_AND_MEAN cuts the COM headers out of windows.h, and Media
// Foundation needs them back.
#include <objbase.h>
#include <mfapi.h>
#include <mftransform.h>
#include <mferror.h>
#include <icodecapi.h>
#include <codecapi.h>

#include "decoder.h"
#include "core/log.h"

namespace
{
    IMFTransform* g_mft = 0;
    IMFMediaBuffer* g_out_buffer = 0;
    bool g_mf_up = false;
    bool g_running = false;
    bool g_mft_allocates = false;

    // What the buffer holds, and the part of it that is really the picture. A
    // decoder works in whole macroblocks, so a 1920x1080 stream comes back in a
    // 1920x1088 buffer with the bottom eight rows meaning nothing.
    int g_buf_w = 0, g_buf_h = 0;
    int g_vis_w = 0, g_vis_h = 0;
    int g_vis_x = 0, g_vis_y = 0;

    unsigned int g_in = 0, g_out = 0;
    // Stride the output type declared, and the average luma of the last picture.
    int g_stride = 0;
    unsigned int g_luma = 0;
    char g_error[192];
    char g_name[128];

    unsigned char* g_rgba = 0;
    int g_rgba_cap = 0;

    void set_error(const char* what, HRESULT hr)
    {
        cnprint(g_error, sizeof(g_error), "%s (0x%08x)", what, (unsigned int)hr);
        log_line("decoder: %s", g_error);
    }

    template <class T> void release(T** p)
    {
        if (*p) { (*p)->Release(); *p = 0; }
    }

    bool ensure_rgba(int need)
    {
        if (need <= g_rgba_cap) return true;

        int cap = g_rgba_cap ? g_rgba_cap : (1 << 20);
        while (cap < need) cap *= 2;

        unsigned char* fresh = (unsigned char*)memalloc(cap);
        if (!fresh) return false;

        if (g_rgba) memfree(g_rgba);
        g_rgba = fresh;
        g_rgba_cap = cap;
        return true;
    }

    // Whether the transform in hand asks for work instead of being asked, and
    // the queue it asks through.
    bool g_async = false;
    IMFMediaEventGenerator* g_events = 0;

    // Outstanding requests. An asynchronous transform says when it wants a
    // frame and when it has one; doing either unasked is a protocol error.
    int g_need_input = 0;
    int g_have_output = 0;

    // Frames waiting for a request to arrive. Small, because the transform asks
    // freely: this only covers the moment between handing over a picture and
    // being asked for the next.
    const int PENDING_MAX = 16;
    IMFSample* g_pending[PENDING_MAX];
    int g_pending_count = 0;

    void drop_pending()
    {
        for (int i = 0; i < g_pending_count; i++)
            if (g_pending[i]) g_pending[i]->Release();
        g_pending_count = 0;
    }

    // Tries one enumeration and takes the first transform it returns. Returns
    // false without complaint - the caller has another kind to try.
    bool try_decoder(UINT32 flags, const char* what)
    {
        MFT_REGISTER_TYPE_INFO want;
        want.guidMajorType = MFMediaType_Video;
        want.guidSubtype = MFVideoFormat_H264;

        IMFActivate** found = 0;
        UINT32 count = 0;

        if (FAILED(MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER, flags, &want, 0, &found, &count)) ||
            count == 0)
        {
            if (found) CoTaskMemFree(found);
            return false;
        }

        HRESULT hr = found[0]->ActivateObject(IID_IMFTransform, (void**)&g_mft);

        wchar_t* name = 0;
        UINT32 name_len = 0;
        if (SUCCEEDED(found[0]->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &name, &name_len)))
        {
            wcstochar(name, g_name, (int)sizeof(g_name));
            CoTaskMemFree(name);
        }

        for (UINT32 i = 0; i < count; i++) found[i]->Release();
        CoTaskMemFree(found);

        if (FAILED(hr) || !g_mft) { release(&g_mft); return false; }

        // A hardware transform arrives locked, and every call on it fails until
        // the caller says it understands the asynchronous contract.
        IMFAttributes* attrs = 0;
        g_async = false;

        if (SUCCEEDED(g_mft->GetAttributes(&attrs)) && attrs)
        {
            UINT32 is_async = 0;
            attrs->GetUINT32(MF_TRANSFORM_ASYNC, &is_async);
            g_async = is_async != 0;

            if (g_async && FAILED(attrs->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE)))
            {
                attrs->Release();
                release(&g_mft);
                return false;
            }
            attrs->Release();
        }

        if (g_async &&
            (FAILED(g_mft->QueryInterface(IID_IMFMediaEventGenerator, (void**)&g_events)) ||
             !g_events))
        {
            release(&g_mft);
            g_async = false;
            return false;
        }

        log_line("decoder: %s - %s%s", what, g_name, g_async ? " (асинхронный)" : "");
        return true;
    }

    bool pick_decoder()
    {
        // Hardware first, and hardware means asynchronous: the event driven
        // interface is the only one a graphics driver offers, which is why
        // enumerating synchronous transforms alone - as this did - could only
        // ever find Microsoft's software decoder. Decoding 1080p thirty times a
        // second on the processor is most of what watching a share used to cost.
        if (try_decoder(MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_ASYNCMFT |
                        MFT_ENUM_FLAG_SORTANDFILTER, "аппаратный"))
            return true;

        // Nothing on the card, or it refused to unlock. The software transform
        // still works and is what has been used all along.
        if (try_decoder(MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_SORTANDFILTER,
                        "программный"))
            return true;

        set_error("нет ни одного декодера H.264", 0);
        return false;
    }

    // Drains the request queue. Nothing here blocks: the events are polled, so
    // the caller keeps its submit-and-poll shape and no extra thread appears.
    void pump_events()
    {
        if (!g_events) return;

        for (;;)
        {
            IMFMediaEvent* ev = 0;
            if (FAILED(g_events->GetEvent(MF_EVENT_FLAG_NO_WAIT, &ev)) || !ev) break;

            MediaEventType type = MEUnknown;
            ev->GetType(&type);
            ev->Release();

            if (type == METransformNeedInput) g_need_input++;
            else if (type == METransformHaveOutput) g_have_output++;
        }
    }

    // Hands over as many waiting frames as the transform has asked for.
    void feed_pending()
    {
        while (g_pending_count > 0 && g_need_input > 0)
        {
            IMFSample* sample = g_pending[0];
            for (int i = 1; i < g_pending_count; i++) g_pending[i - 1] = g_pending[i];
            g_pending_count--;

            HRESULT hr = g_mft->ProcessInput(0, sample, 0);
            sample->Release();

            if (SUCCEEDED(hr)) { g_need_input--; g_in++; }
            else if (hr != MF_E_NOTACCEPTING) { set_error("ProcessInput", hr); break; }
            else break;      // asked too soon; the request stands
        }
    }

    // A decoder holds several pictures back by default so it can reorder them.
    // This stream has no B frames and every extra picture held is a frame of
    // delay on a live view, so it is asked not to.
    void configure_low_latency()
    {
        IMFAttributes* attrs = 0;
        if (SUCCEEDED(g_mft->GetAttributes(&attrs)) && attrs)
        {
            attrs->SetUINT32(MF_LOW_LATENCY, TRUE);
            attrs->Release();
        }

        ICodecAPI* api = 0;
        if (SUCCEEDED(g_mft->QueryInterface(__uuidof(ICodecAPI), (void**)&api)) && api)
        {
            VARIANT v;
            VariantInit(&v);
            v.vt = VT_BOOL;
            v.boolVal = VARIANT_TRUE;
            api->SetValue(&CODECAPI_AVLowLatencyMode, &v);
            api->Release();
        }
    }

    // The output type is settled twice: once now, so the transform has one, and
    // again after the first frames when the real size is known.
    bool choose_output_type()
    {
        for (DWORD i = 0; ; i++)
        {
            IMFMediaType* candidate = 0;
            HRESULT hr = g_mft->GetOutputAvailableType(0, i, &candidate);
            if (hr == MF_E_NO_MORE_TYPES || FAILED(hr) || !candidate) break;

            GUID sub;
            if (SUCCEEDED(candidate->GetGUID(MF_MT_SUBTYPE, &sub)) && sub == MFVideoFormat_NV12)
            {
                hr = g_mft->SetOutputType(0, candidate, 0);
                if (SUCCEEDED(hr))
                {
                    UINT32 w = 0, h = 0;
                    if (SUCCEEDED(MFGetAttributeSize(candidate, MF_MT_FRAME_SIZE, &w, &h)))
                    {
                        g_buf_w = (int)w;
                        g_buf_h = (int)h;
                        g_vis_w = g_buf_w;
                        g_vis_h = g_buf_h;
                        g_vis_x = 0;
                        g_vis_y = 0;
                    }

                    // Anything outside this rectangle is padding the decoder
                    // needed to reach a whole macroblock.
                    MFVideoArea area;
                    UINT32 got = 0;
                    if (SUCCEEDED(candidate->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE,
                                                     (UINT8*)&area, sizeof(area), &got)) &&
                        got == sizeof(area) && area.Area.cx > 0 && area.Area.cy > 0)
                    {
                        g_vis_x = area.OffsetX.value;
                        g_vis_y = area.OffsetY.value;
                        g_vis_w = area.Area.cx;
                        g_vis_h = area.Area.cy;
                    }

                    if (g_vis_x + g_vis_w > g_buf_w) g_vis_w = g_buf_w - g_vis_x;
                    if (g_vis_y + g_vis_h > g_buf_h) g_vis_h = g_buf_h - g_vis_y;

                    UINT32 stride = 0;
                    g_stride = SUCCEEDED(candidate->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride))
                             ? (int)stride : 0;

                    candidate->Release();
                    return true;
                }
            }
            candidate->Release();
        }

        set_error("декодер не предлагает NV12", 0);
        return false;
    }

    // The transform decides how big its output buffer has to be only once the
    // output type is known, so this is redone after every renegotiation.
    bool prepare_output_buffer()
    {
        release(&g_out_buffer);

        MFT_OUTPUT_STREAM_INFO osi;
        ccfset(&osi, 0, sizeof(osi));
        g_mft->GetOutputStreamInfo(0, &osi);

        g_mft_allocates = (osi.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES |
                                          MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) != 0;
        if (g_mft_allocates) return true;

        DWORD size = osi.cbSize;
        if (size == 0)
        {
            int need = g_buf_w * g_buf_h * 3 / 2;
            size = (DWORD)(need > 0 ? need : (1 << 22));
        }

        HRESULT hr = MFCreateMemoryBuffer(size, &g_out_buffer);
        if (FAILED(hr)) { set_error("MFCreateMemoryBuffer (выход)", hr); return false; }
        return true;
    }

    // Media Foundation asks for this whenever the picture size it discovered no
    // longer matches the type it was given.
    bool renegotiate()
    {
        if (!choose_output_type()) return false;
        if (!prepare_output_buffer()) return false;

        log_line("decoder: поток %dx%d (буфер %dx%d, шаг %d)",
             g_vis_w, g_vis_h, g_buf_w, g_buf_h, g_stride);
        return true;
    }
}

// ---------------------------------------------------------------------------
// colour conversion
// ---------------------------------------------------------------------------
//
// BT.601 limited range, the inverse of what the encoder writes. The chroma pair
// covers a two by two block, so each one is read by four luma samples.

void vdec::nv12_to_rgba(const unsigned char* nv12, int src_pitch, int plane_height,
                        int src_x, int src_y,
                        unsigned char* dst, int width, int height)
{
    if (!nv12 || !dst || width <= 0 || height <= 0) return;

    // A chroma pair covers a two by two block, so an odd corner would split it.
    src_x &= ~1;
    src_y &= ~1;

    const unsigned char* y_plane = nv12;
    const unsigned char* uv_plane = nv12 + (size_t)src_pitch * plane_height;

    // Two pixels at a time, because chroma is stored at half resolution and a
    // pair shares one. Done per pixel, as this was, every chroma sample is
    // fetched twice, masked into place twice and multiplied out twice for a
    // result that cannot differ - and at two million pixels thirty times a
    // second that duplication is most of the cost of watching somebody's
    // screen.
    //
    // The three chroma terms are lifted out of the pair as well, so what is
    // left inside is one multiply and an add per pixel.
    for (int y = 0; y < height; y++)
    {
        const unsigned char* y_row = y_plane + (size_t)(src_y + y) * src_pitch + src_x;
        const unsigned char* uv_row = uv_plane + (size_t)((src_y + y) / 2) * src_pitch + src_x;
        unsigned int* out = (unsigned int*)(dst + (size_t)y * width * 4);

        int x = 0;
        for (; x + 1 < width; x += 2)
        {
            int u = (int)uv_row[x + 0] - 128;
            int v = (int)uv_row[x + 1] - 128;

            int rc = 409 * v + 128;
            int gc = -100 * u - 208 * v + 128;
            int bc = 516 * u + 128;

            for (int k = 0; k < 2; k++)
            {
                int c = 298 * ((int)y_row[x + k] - 16);

                int r = (c + rc) >> 8;
                int g = (c + gc) >> 8;
                int b = (c + bc) >> 8;

                if (r < 0) r = 0; else if (r > 255) r = 255;
                if (g < 0) g = 0; else if (g > 255) g = 255;
                if (b < 0) b = 0; else if (b > 255) b = 255;

                // One store instead of four. The layout is RGBA in memory,
                // which on a little endian machine is this word.
                out[x + k] = (unsigned int)r | ((unsigned int)g << 8) |
                             ((unsigned int)b << 16) | 0xFF000000u;
            }
        }

        // An odd width leaves one pixel, which takes the chroma of the pair it
        // half belongs to.
        if (x < width)
        {
            int u = (int)uv_row[x & ~1] - 128;
            int v = (int)uv_row[(x & ~1) + 1] - 128;
            int c = 298 * ((int)y_row[x] - 16);

            int r = (c + 409 * v + 128) >> 8;
            int g = (c - 100 * u - 208 * v + 128) >> 8;
            int b = (c + 516 * u + 128) >> 8;

            if (r < 0) r = 0; else if (r > 255) r = 255;
            if (g < 0) g = 0; else if (g > 255) g = 255;
            if (b < 0) b = 0; else if (b > 255) b = 255;

            out[x] = (unsigned int)r | ((unsigned int)g << 8) |
                     ((unsigned int)b << 16) | 0xFF000000u;
        }
    }
}

// ---------------------------------------------------------------------------

bool vdec::init()
{
    if (g_mf_up) return true;

    // MFStartup counts its callers, so the encoder having done this already is
    // not a problem and neither is doing it again here.
    HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
    if (FAILED(hr)) { set_error("MFStartup", hr); return false; }

    g_mf_up = true;
    return true;
}

void vdec::shutdown()
{
    stop();
    if (g_mf_up) { MFShutdown(); g_mf_up = false; }

    if (g_rgba) { memfree(g_rgba); g_rgba = 0; g_rgba_cap = 0; }
}

bool vdec::start()
{
    if (!g_mf_up && !init()) return false;
    stop();

    g_in = 0;
    g_out = 0;
    g_error[0] = 0;
    g_name[0] = 0;
    g_buf_w = g_buf_h = g_vis_w = g_vis_h = g_vis_x = g_vis_y = 0;
    g_stride = 0;
    g_luma = 0;

    if (!pick_decoder()) { stop(); return false; }
    configure_low_latency();

    IMFMediaType* in = 0;
    HRESULT hr = MFCreateMediaType(&in);
    if (FAILED(hr)) { set_error("MFCreateMediaType", hr); stop(); return false; }

    in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    in->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    in->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    // A guess. The sequence parameter set in the stream overrules it, and the
    // transform tells us so by asking for the output type to be settled again.
    MFSetAttributeSize(in, MF_MT_FRAME_SIZE, 1280, 720);
    MFSetAttributeRatio(in, MF_MT_FRAME_RATE, 30, 1);
    MFSetAttributeRatio(in, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);

    hr = g_mft->SetInputType(0, in, 0);
    in->Release();
    if (FAILED(hr)) { set_error("SetInputType(H264)", hr); stop(); return false; }

    if (!choose_output_type()) { stop(); return false; }
    if (!prepare_output_buffer()) { stop(); return false; }

    g_mft->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
    g_mft->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    g_mft->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);

    g_running = true;
    log_line("decoder: %s готов", g_name[0] ? g_name : "H.264 MFT");
    return true;
}

void vdec::stop()
{
    if (g_mft)
    {
        g_mft->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
        g_mft->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
    }

    drop_pending();
    g_need_input = 0;
    g_have_output = 0;

    release(&g_events);
    release(&g_out_buffer);
    release(&g_mft);
    g_async = false;
    g_running = false;
}

void vdec::flush()
{
    if (g_mft) g_mft->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);

    // Requests outstanding across a flush describe frames that no longer
    // exist. Acting on them afterwards asks the transform for output it has
    // already thrown away, which it answers with an error that reads exactly
    // like a broken stream.
    drop_pending();
    g_need_input = 0;
    g_have_output = 0;
}

bool vdec::running() { return g_running; }
int vdec::width() { return g_vis_w; }
int vdec::height() { return g_vis_h; }
const char* vdec::decoder_name() { return g_name; }
const char* vdec::last_error() { return g_error; }
unsigned int vdec::frames_in() { return g_in; }
unsigned int vdec::frames_out() { return g_out; }
unsigned int vdec::last_luma() { return g_luma; }
int vdec::stride() { return g_stride; }

bool vdec::submit(const unsigned char* annexb, int len, unsigned long long time_us)
{
    if (!g_running || !annexb || len <= 0) return false;

    IMFMediaBuffer* buf = 0;
    HRESULT hr = MFCreateMemoryBuffer((DWORD)len, &buf);
    if (FAILED(hr)) { set_error("MFCreateMemoryBuffer (вход)", hr); return false; }

    BYTE* dst = 0;
    DWORD cap = 0, cur = 0;
    if (FAILED(buf->Lock(&dst, &cap, &cur)))
    {
        buf->Release();
        return false;
    }
    ccpy(dst, annexb, (size_t)len);
    buf->Unlock();
    buf->SetCurrentLength((DWORD)len);

    IMFSample* sample = 0;
    hr = MFCreateSample(&sample);
    if (FAILED(hr))
    {
        buf->Release();
        set_error("MFCreateSample", hr);
        return false;
    }

    sample->AddBuffer(buf);
    // Media Foundation counts in units of a hundred nanoseconds.
    sample->SetSampleTime((LONGLONG)(time_us * 10));
    buf->Release();

    if (g_async)
    {
        // Handed over only when asked for. Pushing at an asynchronous transform
        // unbidden is not merely rude, it is refused.
        pump_events();

        if (g_pending_count >= PENDING_MAX)
        {
            // The transform has stopped asking. Give up the oldest rather than
            // grow without limit - it is one picture, and the alternative is a
            // queue that never drains.
            g_pending[0]->Release();
            for (int i = 1; i < g_pending_count; i++) g_pending[i - 1] = g_pending[i];
            g_pending_count--;
        }

        g_pending[g_pending_count++] = sample;
        feed_pending();
        return true;
    }

    hr = g_mft->ProcessInput(0, sample, 0);
    sample->Release();

    // Being told to stop pushing is not a failure: it means output is waiting.
    if (hr == MF_E_NOTACCEPTING) return true;
    if (FAILED(hr)) { set_error("ProcessInput", hr); return false; }

    g_in++;
    return true;
}

namespace
{
    // next() and next_nv12() are the same walk over the transform's output and
    // differ only in what is written into the shared buffer at the end of it,
    // so they share one body rather than two copies that drift apart.
    bool g_want_planar = false;

    // The visible region packed as it came out of the decoder: luma rows, then
    // the interleaved chroma rows. Copying only - no arithmetic per pixel.
    void pack_nv12(const unsigned char* src, int pitch, int plane_h,
                   int vis_x, int vis_y, unsigned char* dst, int w, int h)
    {
        vis_x &= ~1;
        vis_y &= ~1;

        for (int y = 0; y < h; y++)
            ccpy(dst + (size_t)y * w, src + (size_t)(vis_y + y) * pitch + vis_x, (size_t)w);

        const unsigned char* uv = src + (size_t)pitch * plane_h;
        unsigned char* out = dst + (size_t)w * h;

        for (int y = 0; y < h / 2; y++)
            ccpy(out + (size_t)y * w, uv + (size_t)(vis_y / 2 + y) * pitch + vis_x, (size_t)w);
    }
}

bool vdec::next_nv12(const unsigned char** nv12, int* width, int* height)
{
    g_want_planar = true;
    bool ok = vdec::next(nv12, width, height);
    g_want_planar = false;
    return ok;
}

bool vdec::next(const unsigned char** rgba, int* width, int* height)
{
    if (!g_running) return false;

    if (g_async)
    {
        // Nothing is ready until the transform says so, and asking anyway
        // returns an error that looks exactly like a broken stream.
        pump_events();
        feed_pending();
        if (g_have_output <= 0) return false;
    }

    // The renegotiation below consumes the attempt, so the caller gets an
    // answer on the second pass rather than having to come back for it.
    for (int attempt = 0; attempt < 2; attempt++)
    {
        MFT_OUTPUT_DATA_BUFFER odb;
        ccfset(&odb, 0, sizeof(odb));

        IMFSample* holder = 0;
        if (!g_mft_allocates)
        {
            if (FAILED(MFCreateSample(&holder))) return false;

            // The buffer is handed back to the transform frame after frame, and
            // it still carries the length the last picture set. A transform
            // measures the room it has as max length less current length, so a
            // buffer left full looks like a buffer with nothing free: the first
            // frame comes out and every one after it is refused. Emptying it is
            // the caller's job and there is no warning when it is skipped.
            g_out_buffer->SetCurrentLength(0);
            holder->AddBuffer(g_out_buffer);
            odb.pSample = holder;
        }

        DWORD status = 0;
        HRESULT hr = g_mft->ProcessOutput(0, 1, &odb, &status);

        if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT)
        {
            release(&holder);
            return false;
        }
        if (hr == MF_E_TRANSFORM_STREAM_CHANGE)
        {
            // The picture is not the size the output type claims. Settle it
            // again and try once more.
            release(&holder);
            release(&odb.pEvents);
            if (!renegotiate()) return false;
            continue;
        }
        if (FAILED(hr))
        {
            set_error("ProcessOutput", hr);
            release(&holder);
            return false;
        }

        if (g_async && g_have_output > 0) g_have_output--;

        IMFSample* got = odb.pSample;
        bool ok = false;

        // Which buffer to read, and how, is not a free choice: asking for a
        // contiguous copy and then locking it two dimensionally are two
        // different requests. The copy is laid out at the default stride, so a
        // pitch read off the original buffer no longer describes it - and a
        // pitch read off the copy is whatever the interface feels like saying.
        //
        // So the original buffer is used whenever there is exactly one, which
        // keeps its real pitch meaningful, and the contiguous copy is only a
        // fallback for a sample that carries several.
        IMFMediaBuffer* buf = 0;
        DWORD buffer_count = 0;
        if (got && SUCCEEDED(got->GetBufferCount(&buffer_count)) && buffer_count == 1)
            got->GetBufferByIndex(0, &buf);
        else if (got)
            got->ConvertToContiguousBuffer(&buf);

        if (buf)
        {
            BYTE* src = 0;
            LONG pitch = 0;
            IMF2DBuffer* flat = 0;
            bool locked_2d = false;

            if (SUCCEEDED(buf->QueryInterface(IID_IMF2DBuffer, (void**)&flat)) && flat)
            {
                locked_2d = SUCCEEDED(flat->Lock2D(&src, &pitch));
                if (!locked_2d) { flat->Release(); flat = 0; }
            }

            DWORD cap = 0, cur = 0;
            if (!locked_2d)
            {
                // Without the two dimensional view the stride is whatever the
                // output type declared, and only failing that the width.
                pitch = g_stride ? g_stride : g_buf_w;
                if (FAILED(buf->Lock(&src, &cap, &cur))) src = 0;
            }

            // A negative pitch means the rows are stored bottom up, with the
            // pointer aimed at the last one.
            if (src && pitch < 0)
            {
                src += (LONG)(g_buf_h - 1) * pitch;
                pitch = -pitch;
            }

            int need = g_want_planar ? (g_vis_w * g_vis_h * 3 / 2)
                                     : (g_vis_w * g_vis_h * 4);

            if (src && g_vis_w > 0 && g_vis_h > 0 && ensure_rgba(need))
            {
                if (g_want_planar)
                    pack_nv12(src, (int)pitch, g_buf_h, g_vis_x, g_vis_y,
                              g_rgba, g_vis_w, g_vis_h);
                else
                    nv12_to_rgba(src, (int)pitch, g_buf_h, g_vis_x, g_vis_y,
                                 g_rgba, g_vis_w, g_vis_h);

                // The one measurement that tells a decode that produced nothing
                // apart from one that produced a picture nobody can see.
                unsigned long long sum = 0;
                for (int y = 0; y < g_vis_h; y += 4)
                {
                    const unsigned char* row = src + (size_t)(g_vis_y + y) * pitch + g_vis_x;
                    for (int x = 0; x < g_vis_w; x += 4) sum += row[x];
                }
                unsigned int samples = (unsigned int)(((g_vis_h + 3) / 4) * ((g_vis_w + 3) / 4));
                g_luma = samples ? (unsigned int)(sum / samples) : 0;

                *rgba = g_rgba;
                *width = g_vis_w;
                *height = g_vis_h;
                g_out++;
                ok = true;
            }

            if (locked_2d) { flat->Unlock2D(); flat->Release(); }
            else if (src) buf->Unlock();

            buf->Release();
        }

        if (g_mft_allocates) release(&got);
        release(&holder);
        release(&odb.pEvents);
        return ok;
    }

    return false;
}
