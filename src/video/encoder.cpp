#include "pch.h"
// WIN32_LEAN_AND_MEAN cuts the COM headers out of windows.h, and Media
// Foundation needs them back.
#include <objbase.h>
#include <mfapi.h>
#include <mftransform.h>
#include <mferror.h>
#include <icodecapi.h>
#include <codecapi.h>

#include "encoder.h"
#include "core/log.h"

namespace
{
    bool g_mf_up = false;

    void set_error(venc_stream* e, const char* what, HRESULT hr)
    {
        cnprint(e->error, sizeof(e->error), "%s (0x%08x)", what, (unsigned int)hr);
        log_line("encoder: %s", e->error);
    }

    template <class T> void release(T** p)
    {
        if (*p) { (*p)->Release(); *p = 0; }
    }

    // Walks Annex-B start codes. Returns the offset of the next NAL body, or -1.
    int next_nal(const unsigned char* d, int len, int from, int* nal_len)
    {
        for (int i = from; i + 4 <= len; i++)
        {
            bool three = d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1;
            bool four = !three && d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 0 && d[i + 3] == 1;
            if (!three && !four) continue;

            int body = i + (three ? 3 : 4);
            int end = len;
            for (int j = body; j + 3 <= len; j++)
            {
                if (d[j] == 0 && d[j + 1] == 0 && (d[j + 2] == 1 ||
                    (d[j + 2] == 0 && j + 4 <= len && d[j + 3] == 1)))
                {
                    end = j;
                    break;
                }
            }
            *nal_len = end - body;
            return body;
        }
        return -1;
    }

    // Remembers the sequence and picture parameter sets, start codes and all,
    // so they can be replayed later.
    void remember_params(venc_stream* e, const unsigned char* d, int len)
    {
        int at = 0, nal_len = 0;
        int found = 0;
        int written = 0;
        unsigned char scratch[sizeof(e->params)];

        while ((at = next_nal(d, len, at, &nal_len)) >= 0 && nal_len > 0)
        {
            int type = d[at] & 0x1F;
            if (type == 7 || type == 8)
            {
                if (written + 4 + nal_len <= (int)sizeof(scratch))
                {
                    scratch[written++] = 0; scratch[written++] = 0;
                    scratch[written++] = 0; scratch[written++] = 1;
                    ccpy(scratch + written, d + at, (size_t)nal_len);
                    written += nal_len;
                    found++;
                }
            }
            at += nal_len;
        }

        // Both have to be present, otherwise what is kept is worse than nothing.
        if (found >= 2)
        {
            ccpy(e->params, scratch, (size_t)written);
            e->params_len = written;
        }
    }

    // Drops the NAL units a WebRTC path will not carry through unchanged.
    //
    // Access unit delimiters, which Media Foundation puts at the head of every
    // frame and no WebRTC sender emits, and filler data, which the encoder
    // pads a constant bitrate with and every packetiser throws away.
    //
    // This matters far beyond the bytes themselves: the frame is protected end
    // to end and the ranges in that trailer are offsets into it. A NAL the far
    // side drops shifts every offset after it, and then nothing decrypts at
    // all - which reaches the sender as no complaint whatsoever and the viewer
    // as a picture that never arrives. Returns the new length.
    int strip_undeliverable_nals(unsigned char* d, int len)
    {
        int write = 0;
        int at = 0, nal_len = 0;

        while (true)
        {
            int body = next_nal(d, len, at, &nal_len);
            if (body < 0 || nal_len <= 0) break;

            unsigned char type = (unsigned char)(d[body] & 0x1F);
            if (type != 9 && type != 12)
            {
                // Long start codes throughout, matching what the protection
                // layer writes and what a receiver reconstructs.
                d[write++] = 0; d[write++] = 0; d[write++] = 0; d[write++] = 1;
                // The source and destination can overlap once something has
                // been removed, so this copies forwards by hand.
                for (int i = 0; i < nal_len; i++) d[write + i] = d[body + i];
                write += nal_len;
            }

            at = body + nal_len;
        }

        return write > 0 ? write : len;
    }

    bool frame_has_params(const unsigned char* d, int len)
    {
        int at = 0, nal_len = 0;
        while ((at = next_nal(d, len, at, &nal_len)) >= 0 && nal_len > 0)
        {
            if ((d[at] & 0x1F) == 7) return true;
            at += nal_len;
        }
        return false;
    }

    bool ensure_frame_cap(venc_stream* e, int need)
    {
        if (need <= e->frame_cap) return true;

        int cap = e->frame_cap ? e->frame_cap : 65536;
        while (cap < need) cap *= 2;

        unsigned char* fresh = (unsigned char*)memalloc(cap);
        if (!fresh) return false;

        if (e->frame) memfree(e->frame);
        e->frame = fresh;
        e->frame_cap = cap;
        return true;
    }

    // The encoder is asked for constrained baseline with no B frames: Discord
    // and every other WebRTC endpoint expect decode order to match display
    // order, and a reordered stream would break the RTP timestamps.
    bool configure_codec(venc_stream* e)
    {
        ICodecAPI* api = 0;
        if (FAILED(e->mft->QueryInterface(__uuidof(ICodecAPI), (void**)&api)) || !api) return true;

        VARIANT v;

        VariantInit(&v);
        v.vt = VT_UI4;
        v.ulVal = eAVEncCommonRateControlMode_CBR;
        api->SetValue(&CODECAPI_AVEncCommonRateControlMode, &v);

        VariantInit(&v);
        v.vt = VT_UI4;
        v.ulVal = 0;
        api->SetValue(&CODECAPI_AVEncMPVDefaultBPictureCount, &v);

        // A keyframe every two seconds keeps a late viewer from waiting long
        // without spending much of the bitrate on them.
        VariantInit(&v);
        v.vt = VT_UI4;
        v.ulVal = (ULONG)(e->fps * 2);
        api->SetValue(&CODECAPI_AVEncMPVGOPSize, &v);

        VariantInit(&v);
        v.vt = VT_BOOL;
        v.boolVal = VARIANT_TRUE;
        api->SetValue(&CODECAPI_AVLowLatencyMode, &v);

        api->Release();
        return true;
    }

    bool pick_encoder(venc_stream* e)
    {
        MFT_REGISTER_TYPE_INFO want;
        want.guidMajorType = MFMediaType_Video;
        want.guidSubtype = MFVideoFormat_H264;

        IMFActivate** found = 0;
        UINT32 count = 0;

        // Synchronous only: an asynchronous transform needs the event driven
        // path, which the hardware encoders use and this does not implement yet.
        HRESULT hr = MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER,
                               MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_SORTANDFILTER,
                               0, &want, &found, &count);
        if (FAILED(hr) || count == 0)
        {
            set_error(e, "нет ни одного кодировщика H.264", hr);
            if (found) CoTaskMemFree(found);
            return false;
        }

        hr = found[0]->ActivateObject(IID_IMFTransform, (void**)&e->mft);

        wchar_t* name = 0;
        UINT32 name_len = 0;
        if (SUCCEEDED(found[0]->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &name, &name_len)))
        {
            wcstochar(name, e->name, (int)sizeof(e->name));
            CoTaskMemFree(name);
        }

        for (UINT32 i = 0; i < count; i++) found[i]->Release();
        CoTaskMemFree(found);

        if (FAILED(hr) || !e->mft)
        {
            set_error(e, "кодировщик не запустился", hr);
            return false;
        }
        return true;
    }

    bool set_types(venc_stream* e, int bitrate_kbps)
    {
        // An encoder wants its output described before its input: the input it
        // can accept depends on what it has been asked to produce.
        IMFMediaType* out = 0;
        HRESULT hr = MFCreateMediaType(&out);
        if (FAILED(hr)) { set_error(e, "MFCreateMediaType", hr); return false; }

        out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
        out->SetUINT32(MF_MT_AVG_BITRATE, (UINT32)(bitrate_kbps * 1000));
        out->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        out->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_ConstrainedBase);
        MFSetAttributeSize(out, MF_MT_FRAME_SIZE, (UINT32)e->w, (UINT32)e->h);
        MFSetAttributeRatio(out, MF_MT_FRAME_RATE, (UINT32)e->fps, 1);
        MFSetAttributeRatio(out, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);

        hr = e->mft->SetOutputType(0, out, 0);
        out->Release();
        if (FAILED(hr)) { set_error(e, "SetOutputType", hr); return false; }

        IMFMediaType* in = 0;
        hr = MFCreateMediaType(&in);
        if (FAILED(hr)) { set_error(e, "MFCreateMediaType", hr); return false; }

        in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        in->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
        in->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        MFSetAttributeSize(in, MF_MT_FRAME_SIZE, (UINT32)e->w, (UINT32)e->h);
        MFSetAttributeRatio(in, MF_MT_FRAME_RATE, (UINT32)e->fps, 1);
        MFSetAttributeRatio(in, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);

        hr = e->mft->SetInputType(0, in, 0);
        in->Release();
        if (FAILED(hr)) { set_error(e, "SetInputType(NV12)", hr); return false; }

        return true;
    }
}

// ---------------------------------------------------------------------------
// colour conversion
// ---------------------------------------------------------------------------
//
// BT.601 limited range, which is what an H.264 stream without explicit colour
// information is read as. Chroma is averaged over each two by two block rather
// than sampled from one corner, so thin coloured text does not shimmer.

void venc::bgra_to_nv12(const unsigned char* bgra, int stride,
                        unsigned char* dst, int width, int height)
{
    if (!bgra || !dst || width <= 0 || height <= 0) return;

    unsigned char* y_plane = dst;
    unsigned char* uv_plane = dst + (size_t)width * height;

    for (int y = 0; y < height; y++)
    {
        const unsigned char* src = bgra + (size_t)y * stride;
        unsigned char* row = y_plane + (size_t)y * width;

        for (int x = 0; x < width; x++)
        {
            int b = src[x * 4 + 0];
            int g = src[x * 4 + 1];
            int r = src[x * 4 + 2];
            row[x] = (unsigned char)(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);
        }
    }

    for (int y = 0; y < height; y += 2)
    {
        const unsigned char* row0 = bgra + (size_t)y * stride;
        // An odd height would leave the last block without a second row.
        const unsigned char* row1 = (y + 1 < height) ? row0 + stride : row0;
        unsigned char* uv = uv_plane + (size_t)(y / 2) * width;

        for (int x = 0; x < width; x += 2)
        {
            int x1 = (x + 1 < width) ? x + 1 : x;

            int b = row0[x * 4 + 0] + row0[x1 * 4 + 0] + row1[x * 4 + 0] + row1[x1 * 4 + 0];
            int g = row0[x * 4 + 1] + row0[x1 * 4 + 1] + row1[x * 4 + 1] + row1[x1 * 4 + 1];
            int r = row0[x * 4 + 2] + row0[x1 * 4 + 2] + row1[x * 4 + 2] + row1[x1 * 4 + 2];

            b >>= 2; g >>= 2; r >>= 2;

            uv[x + 0] = (unsigned char)(((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128);
            uv[x + 1] = (unsigned char)(((112 * r - 94 * g - 18 * b + 128) >> 8) + 128);
        }
    }
}

// ---------------------------------------------------------------------------

void venc::log_encoders()
{
    bool started_here = false;
    if (!g_mf_up)
    {
        if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) { log_line("encoders: MFStartup failed"); return; }
        g_mf_up = true;
        started_here = true;
    }

    struct { const GUID* sub; const char* name; } wanted[] = {
        { &MFVideoFormat_H264, "H264" },
        { &MFVideoFormat_HEVC, "H265" },
        { &MFVideoFormat_VP80, "VP8"  },
        { &MFVideoFormat_VP90, "VP9"  },
        { &MFVideoFormat_AV1,  "AV1"  },
    };

    for (int i = 0; i < 5; i++)
    {
        MFT_REGISTER_TYPE_INFO want;
        want.guidMajorType = MFMediaType_Video;
        want.guidSubtype = *wanted[i].sub;

        IMFActivate** found = 0;
        UINT32 count = 0;
        HRESULT hr = MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER,
                               MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_ASYNCMFT |
                               MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER,
                               0, &want, &found, &count);

        if (FAILED(hr) || count == 0)
        {
            log_line("encoders: %s - нет", wanted[i].name);
            if (found) CoTaskMemFree(found);
            continue;
        }

        for (UINT32 k = 0; k < count; k++)
        {
            wchar_t* name = 0;
            UINT32 name_len = 0;
            char narrow[160];
            narrow[0] = 0;
            if (SUCCEEDED(found[k]->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &name, &name_len)))
            {
                wcstochar(name, narrow, (int)sizeof(narrow));
                CoTaskMemFree(name);
            }
            log_line("encoders: %s - %s", wanted[i].name, narrow[0] ? narrow : "(без имени)");
            found[k]->Release();
        }
        CoTaskMemFree(found);
    }

    // What the encoder actually produces, which is not always what it was
    // asked for. Chromium refuses an H.264 stream whose profile does not match
    // what was negotiated for it, where other decoders shrug and carry on, so
    // the bytes in the sequence parameter set are worth reading rather than
    // trusting the request.
    venc_stream probe;
    ccfset(&probe, 0, sizeof(probe));
    if (venc::start(&probe, 1280, 720, 30, 2500))
    {
        int stride = 1280 * 4;
        unsigned char* frame = (unsigned char*)memalloc(stride * 720);
        if (frame)
        {
            for (int y = 0; y < 720; y++)
                for (int x = 0; x < 1280; x++)
                {
                    unsigned char* px = frame + y * stride + x * 4;
                    px[0] = (unsigned char)x; px[1] = (unsigned char)y;
                    px[2] = (unsigned char)(x ^ y); px[3] = 255;
                }

            bool described = false;
            for (int round = 0; round < 60 && !described; round++)
            {
                venc::submit(&probe, frame, 1280, 720, stride, (unsigned long long)round * 33333);

                const unsigned char* data = 0;
                int len = 0;
                bool key = false;
                while (!described && venc::next(&probe, &data, &len, &key))
                {
                    for (int i = 0; i + 8 < len && !described; i++)
                    {
                        if (data[i] || data[i + 1] || data[i + 2] != 1) continue;
                        if ((data[i + 3] & 0x1F) != 7) continue;   // sequence parameter set

                        unsigned char profile = data[i + 4];
                        unsigned char constraints = data[i + 5];
                        unsigned char level = data[i + 6];

                        const char* named = profile == 66 ? "baseline"
                                          : profile == 77 ? "main"
                                          : profile == 88 ? "extended"
                                          : profile == 100 ? "high" : "?";

                        log_line("encoders: выдаёт profile_idc %u (%s), constraints 0x%02x, "
                                 "level %u.%u%s",
                                 profile, named, constraints, level / 10, level % 10,
                                 (profile == 66 && (constraints & 0x40)) ? ", constrained" : "");
                        described = true;
                    }
                }
            }

            if (!described) log_line("encoders: не удалось прочитать SPS у своего же потока");
            memfree(frame);
        }
        venc::stop(&probe);
    }

    if (started_here) { MFShutdown(); g_mf_up = false; }
}

bool venc::init()
{
    if (g_mf_up) return true;

    HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
    if (FAILED(hr)) { log_line("encoder: MFStartup failed (0x%08x)", (unsigned int)hr); return false; }

    g_mf_up = true;
    return true;
}

void venc::shutdown()
{
    // Only the platform. Each encoder is stopped by whoever owns it.
    if (g_mf_up) { MFShutdown(); g_mf_up = false; }
}

bool venc::start(venc_stream* e, int width, int height, int fps, int bitrate_kbps)
{
    if (!g_mf_up && !init()) return false;
    stop(e);
    ccfset(e, 0, sizeof(*e));

    if (width <= 0 || height <= 0 || (width & 1) || (height & 1))
    {
        ccstrncpy(e->error, "размер кадра должен быть чётным по обеим сторонам", sizeof(e->error) - 1);
        return false;
    }
    if (fps <= 0) fps = 30;
    if (bitrate_kbps <= 0) bitrate_kbps = 2500;

    e->w = width;
    e->h = height;
    e->fps = fps;
    e->in = 0;
    e->out = 0;
    e->error[0] = 0;
    e->name[0] = 0;

    if (!pick_encoder(e)) { stop(e); return false; }
    if (!set_types(e, bitrate_kbps)) { stop(e); return false; }
    configure_codec(e);

    e->nv12_size = e->w * e->h * 3 / 2;
    e->nv12 = (unsigned char*)memalloc(e->nv12_size);
    if (!e->nv12)
    {
        ccstrncpy(e->error, "не хватило памяти под кадр NV12", sizeof(e->error) - 1);
        stop(e);
        return false;
    }

    HRESULT hr = MFCreateMemoryBuffer((DWORD)e->nv12_size, &e->in_buffer);
    if (FAILED(hr)) { set_error(e, "MFCreateMemoryBuffer", hr); stop(e); return false; }

    MFT_OUTPUT_STREAM_INFO osi;
    ccfset(&osi, 0, sizeof(osi));
    e->mft->GetOutputStreamInfo(0, &osi);

    // Some transforms hand back their own buffers and some expect to be given
    // one; the flags say which.
    e->mft_allocates = (osi.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES |
                                      MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) != 0;
    if (!e->mft_allocates)
    {
        DWORD size = osi.cbSize ? osi.cbSize : (DWORD)(1 << 21);
        hr = MFCreateMemoryBuffer(size, &e->out_buffer);
        if (FAILED(hr)) { set_error(e, "MFCreateMemoryBuffer (выход)", hr); stop(e); return false; }
    }

    e->mft->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
    e->mft->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    e->mft->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);

    e->running = true;
    log_line("encoder: %s, %dx%d при %d к/с, %d кбит/с",
             e->name[0] ? e->name : "H.264 MFT", e->w, e->h, e->fps, bitrate_kbps);
    return true;
}

void venc::stop(venc_stream* e)
{
    if (e->mft)
    {
        e->mft->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
        e->mft->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
    }

    release(&e->in_buffer);
    release(&e->out_buffer);
    release(&e->mft);

    if (e->nv12) { memfree(e->nv12); e->nv12 = 0; e->nv12_size = 0; }
    if (e->frame) { memfree(e->frame); e->frame = 0; e->frame_cap = 0; }

    e->frame_len = 0;
    e->running = false;
}

bool venc::running(const venc_stream* e) { return e->running; }
int venc::width(const venc_stream* e) { return e->w; }
int venc::height(const venc_stream* e) { return e->h; }

bool venc::submit(venc_stream* e, const unsigned char* bgra, int width, int height, int stride,
                  unsigned long long time_us)
{
    if (!e->running || !bgra) return false;

    if (width != e->w || height != e->h)
    {
        cnprint(e->error, sizeof(e->error),
                "кадр %dx%d не совпадает с настройкой кодировщика %dx%d",
                width, height, e->w, e->h);
        return false;
    }
    if (stride < width * 4) return false;

    bgra_to_nv12(bgra, stride, e->nv12, e->w, e->h);

    BYTE* dst = 0;
    DWORD cap = 0, cur = 0;
    HRESULT hr = e->in_buffer->Lock(&dst, &cap, &cur);
    if (FAILED(hr)) { set_error(e, "Lock входного буфера", hr); return false; }

    ccpy(dst, e->nv12, (size_t)e->nv12_size);
    e->in_buffer->Unlock();
    e->in_buffer->SetCurrentLength((DWORD)e->nv12_size);

    IMFSample* sample = 0;
    hr = MFCreateSample(&sample);
    if (FAILED(hr)) { set_error(e, "MFCreateSample", hr); return false; }

    sample->AddBuffer(e->in_buffer);
    // Media Foundation counts in units of a hundred nanoseconds.
    sample->SetSampleTime((LONGLONG)(time_us * 10));
    sample->SetSampleDuration((LONGLONG)(10000000 / e->fps));

    hr = e->mft->ProcessInput(0, sample, 0);
    sample->Release();

    // Being told to stop pushing is not a failure: it means output is waiting.
    if (hr == MF_E_NOTACCEPTING) return true;
    if (FAILED(hr)) { set_error(e, "ProcessInput", hr); return false; }

    e->in++;
    return true;
}

bool venc::next(venc_stream* e, const unsigned char** data, int* len, bool* keyframe)
{
    if (!e->running) return false;

    MFT_OUTPUT_DATA_BUFFER odb;
    ccfset(&odb, 0, sizeof(odb));

    IMFSample* holder = 0;
    if (!e->mft_allocates)
    {
        if (FAILED(MFCreateSample(&holder))) return false;
        holder->AddBuffer(e->out_buffer);
        odb.pSample = holder;
    }

    DWORD status = 0;
    HRESULT hr = e->mft->ProcessOutput(0, 1, &odb, &status);

    if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT)
    {
        release(&holder);
        return false;
    }
    if (FAILED(hr))
    {
        set_error(e, "ProcessOutput", hr);
        release(&holder);
        return false;
    }

    IMFSample* got = odb.pSample;
    bool ok = false;

    IMFMediaBuffer* buf = 0;
    if (got && SUCCEEDED(got->ConvertToContiguousBuffer(&buf)) && buf)
    {
        BYTE* src = 0;
        DWORD cap = 0, cur = 0;
        if (SUCCEEDED(buf->Lock(&src, &cap, &cur)))
        {
            // A clean point is a frame that can be decoded with nothing before
            // it, which for H.264 means an IDR.
            UINT32 clean = 0;
            bool key = SUCCEEDED(got->GetUINT32(MFSampleExtension_CleanPoint, &clean)) && clean;

            remember_params(e, src, (int)cur);

            // Only a keyframe is worth the extra bytes, and only when the
            // encoder did not send them itself.
            bool prepend = key && e->params_len > 0 && !frame_has_params(src, (int)cur);
            int extra = prepend ? e->params_len : 0;

            if (ensure_frame_cap(e, (int)cur + extra))
            {
                if (prepend) ccpy(e->frame, e->params, (size_t)e->params_len);
                ccpy(e->frame + extra, src, (size_t)cur);
                e->frame_len = strip_undeliverable_nals(e->frame, (int)cur + extra);
                if (keyframe) *keyframe = key;
                ok = true;
            }
            buf->Unlock();
        }
        buf->Release();
    }

    if (odb.pEvents) odb.pEvents->Release();
    // A transform that allocates hands over a reference that has to go back.
    if (e->mft_allocates && got) got->Release();
    release(&holder);

    if (!ok) return false;

    if (data) *data = e->frame;
    if (len) *len = e->frame_len;
    e->out++;
    return true;
}

void venc::request_keyframe(venc_stream* e)
{
    if (!e->running || !e->mft) return;

    ICodecAPI* api = 0;
    if (FAILED(e->mft->QueryInterface(__uuidof(ICodecAPI), (void**)&api)) || !api) return;

    VARIANT v;
    VariantInit(&v);
    v.vt = VT_UI4;
    v.ulVal = 1;
    api->SetValue(&CODECAPI_AVEncVideoForceKeyFrame, &v);
    api->Release();
}

const char* venc::encoder_name(const venc_stream* e) { return e->name[0] ? e->name : "H.264 MFT"; }
const char* venc::last_error(const venc_stream* e) { return e->error; }
unsigned int venc::frames_in(const venc_stream* e) { return e->in; }
unsigned int venc::frames_out(const venc_stream* e) { return e->out; }

// ---------------------------------------------------------------------------

void venc::downscale_bgra(const unsigned char* src, int src_w, int src_h, int src_stride,
                          unsigned char* dst, int dst_w, int dst_h)
{
    if (!src || !dst || dst_w <= 0 || dst_h <= 0 || src_w <= 0 || src_h <= 0) return;

    // Averaged over the whole source block rather than sampled at one point:
    // text on a shared screen turns to noise under point sampling, and noise is
    // the one thing a video encoder cannot compress.
    for (int y = 0; y < dst_h; y++)
    {
        int y0 = y * src_h / dst_h;
        int y1 = (y + 1) * src_h / dst_h;
        if (y1 <= y0) y1 = y0 + 1;
        if (y1 > src_h) y1 = src_h;

        unsigned char* out = dst + (size_t)y * dst_w * 4;

        for (int x = 0; x < dst_w; x++)
        {
            int x0 = x * src_w / dst_w;
            int x1 = (x + 1) * src_w / dst_w;
            if (x1 <= x0) x1 = x0 + 1;
            if (x1 > src_w) x1 = src_w;

            unsigned int b = 0, g = 0, r = 0, n = 0;
            for (int sy = y0; sy < y1; sy++)
            {
                const unsigned char* row = src + (size_t)sy * src_stride;
                for (int sx = x0; sx < x1; sx++)
                {
                    const unsigned char* px = row + (size_t)sx * 4;
                    b += px[0]; g += px[1]; r += px[2];
                    n++;
                }
            }
            if (!n) n = 1;

            out[x * 4 + 0] = (unsigned char)(b / n);
            out[x * 4 + 1] = (unsigned char)(g / n);
            out[x * 4 + 2] = (unsigned char)(r / n);
            out[x * 4 + 3] = 255;
        }
    }
}
