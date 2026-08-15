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

    bool pick_decoder()
    {
        MFT_REGISTER_TYPE_INFO want;
        want.guidMajorType = MFMediaType_Video;
        want.guidSubtype = MFVideoFormat_H264;

        IMFActivate** found = 0;
        UINT32 count = 0;

        // Synchronous only, for the same reason the encoder is: the event driven
        // interface an asynchronous transform wants is not implemented here.
        HRESULT hr = MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER,
                               MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_SORTANDFILTER,
                               &want, 0, &found, &count);
        if (FAILED(hr) || count == 0)
        {
            set_error("нет ни одного декодера H.264", hr);
            if (found) CoTaskMemFree(found);
            return false;
        }

        hr = found[0]->ActivateObject(IID_IMFTransform, (void**)&g_mft);

        wchar_t* name = 0;
        UINT32 name_len = 0;
        if (SUCCEEDED(found[0]->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &name, &name_len)))
        {
            wcstochar(name, g_name, (int)sizeof(g_name));
            CoTaskMemFree(name);
        }

        for (UINT32 i = 0; i < count; i++) found[i]->Release();
        CoTaskMemFree(found);

        if (FAILED(hr) || !g_mft)
        {
            set_error("декодер не запустился", hr);
            return false;
        }
        return true;
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

    for (int y = 0; y < height; y++)
    {
        const unsigned char* y_row = y_plane + (size_t)(src_y + y) * src_pitch + src_x;
        const unsigned char* uv_row = uv_plane + (size_t)((src_y + y) / 2) * src_pitch + src_x;
        unsigned char* out = dst + (size_t)y * width * 4;

        for (int x = 0; x < width; x++)
        {
            int c = (int)y_row[x] - 16;
            int u = (int)uv_row[(x & ~1) + 0] - 128;
            int v = (int)uv_row[(x & ~1) + 1] - 128;

            int r = (298 * c + 409 * v + 128) >> 8;
            int g = (298 * c - 100 * u - 208 * v + 128) >> 8;
            int b = (298 * c + 516 * u + 128) >> 8;

            if (r < 0) r = 0; else if (r > 255) r = 255;
            if (g < 0) g = 0; else if (g > 255) g = 255;
            if (b < 0) b = 0; else if (b > 255) b = 255;

            out[x * 4 + 0] = (unsigned char)r;
            out[x * 4 + 1] = (unsigned char)g;
            out[x * 4 + 2] = (unsigned char)b;
            out[x * 4 + 3] = 255;
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

    release(&g_out_buffer);
    release(&g_mft);
    g_running = false;
}

void vdec::flush()
{
    if (g_mft) g_mft->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
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

    hr = g_mft->ProcessInput(0, sample, 0);
    sample->Release();
    buf->Release();

    // Being told to stop pushing is not a failure: it means output is waiting.
    if (hr == MF_E_NOTACCEPTING) return true;
    if (FAILED(hr)) { set_error("ProcessInput", hr); return false; }

    g_in++;
    return true;
}

bool vdec::next(const unsigned char** rgba, int* width, int* height)
{
    if (!g_running) return false;

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

            if (src && g_vis_w > 0 && g_vis_h > 0 && ensure_rgba(g_vis_w * g_vis_h * 4))
            {
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
