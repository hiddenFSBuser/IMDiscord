#include "pch.h"
#include "player.h"
#include "mp4.h"
#include "decoder.h"
#include "audio/audio.h"
#include "ui/textures.h"
#include "core/log.h"
#include "ufile.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <mferror.h>
#include <codecapi.h>

namespace
{
    // ---- shared -------------------------------------------------------

    char g_error[192] = { 0 };
    char g_url[700] = { 0 };

    volatile long g_state = PLAYER_IDLE;
    volatile long g_muted = 0;
    volatile long g_want_pause = 0;
    volatile long g_seek_pending = 0;
    unsigned long long g_seek_to_us = 0;

    HANDLE g_thread = 0;
    HANDLE g_stop_event = 0;
    volatile long g_running = 0;

    CRITICAL_SECTION g_lock;
    bool g_locked_ready = false;

    // Two buffers. The decoder fills the spare one and then swaps the
    // pointers under the lock, so the ui never reads a half written picture
    // and the handover costs nothing - a full frame at 1080p is eight
    // megabytes, and copying that twice a frame was most of the stutter.
    unsigned char* g_front = 0;
    unsigned char* g_back = 0;
    int g_front_w = 0, g_front_h = 0;
    int g_back_cap = 0;
    unsigned int g_front_serial = 0;

    unsigned long long g_position_us = 0;
    unsigned long long g_duration_us = 0;
    int g_width = 0, g_height = 0;

    // Bumped by the ui every frame it draws the video; the player gives up on
    // a file nobody is looking at.
    volatile long g_watch_ticks = 0;

    template <class T> void release(T** p) { if (*p) { (*p)->Release(); *p = 0; } }

    void set_error(const char* what, HRESULT hr)
    {
        cnprint(g_error, sizeof(g_error), "%s (0x%08x)", what, (unsigned int)hr);
        log_line("player: %s", g_error);
    }

    // ---- video decode --------------------------------------------------
    //
    // A decoder of its own rather than the one in decoder.cpp: that one is a
    // single instance owned by the stream viewer, and clicking a video while
    // watching somebody's screen must not take their picture away.

    struct video_decoder
    {
        IMFTransform* mft;
        IMFMediaBuffer* out_buffer;

        int buf_w, buf_h;
        int vis_w, vis_h, vis_x, vis_y;
        int stride;

        unsigned char* rgba;
        int rgba_cap;
    };

    bool vd_output_type(video_decoder* d)
    {
        // NV12 is what every hardware decoder produces, and the conversion to
        // RGBA is already written for the stream viewer.
        for (DWORD i = 0;; i++)
        {
            IMFMediaType* type = 0;
            HRESULT hr = d->mft->GetOutputAvailableType(0, i, &type);
            if (hr == MF_E_NO_MORE_TYPES || FAILED(hr)) return false;

            GUID sub;
            if (SUCCEEDED(type->GetGUID(MF_MT_SUBTYPE, &sub)) && sub == MFVideoFormat_NV12)
            {
                hr = d->mft->SetOutputType(0, type, 0);
                if (SUCCEEDED(hr))
                {
                    UINT32 w = 0, h = 0;
                    MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &w, &h);
                    d->buf_w = (int)w;
                    d->buf_h = (int)h;
                    d->vis_w = (int)w;
                    d->vis_h = (int)h;
                    d->vis_x = 0;
                    d->vis_y = 0;

                    // The visible rectangle is what crops the padding a
                    // decoder adds to reach whole macroblocks.
                    MFVideoArea area;
                    UINT32 got = 0;
                    if (SUCCEEDED(type->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE,
                                                (UINT8*)&area, sizeof(area), &got)) &&
                        got == sizeof(area) && area.Area.cx > 0 && area.Area.cy > 0)
                    {
                        d->vis_w = area.Area.cx;
                        d->vis_h = area.Area.cy;
                        d->vis_x = area.OffsetX.value;
                        d->vis_y = area.OffsetY.value;
                    }

                    LONG stride = 0;
                    if (SUCCEEDED(type->GetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32*)&stride)))
                        d->stride = (int)stride;
                    else
                        d->stride = d->buf_w;

                    type->Release();
                    return true;
                }
            }
            type->Release();
        }
    }

    bool vd_prepare_buffer(video_decoder* d)
    {
        release(&d->out_buffer);

        MFT_OUTPUT_STREAM_INFO info;
        ccfset(&info, 0, sizeof(info));
        if (FAILED(d->mft->GetOutputStreamInfo(0, &info))) return false;

        if (info.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES |
                            MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES))
            return true;      // the transform hands over its own buffers

        DWORD size = info.cbSize ? info.cbSize
                                 : (DWORD)(d->buf_w * d->buf_h * 3 / 2 + 4096);
        return SUCCEEDED(MFCreateMemoryBuffer(size, &d->out_buffer));
    }

    bool vd_start(video_decoder* d, const unsigned char* sps_pps, unsigned int len,
                  int width, int height)
    {
        ccfset(d, 0, sizeof(*d));

        MFT_REGISTER_TYPE_INFO want;
        want.guidMajorType = MFMediaType_Video;
        want.guidSubtype = MFVideoFormat_H264;

        IMFActivate** found = 0;
        UINT32 count = 0;
        HRESULT hr = MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER,
                               MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_SORTANDFILTER,
                               &want, 0, &found, &count);
        if (FAILED(hr) || !count)
        {
            set_error("нет декодера H.264", hr);
            if (found) CoTaskMemFree(found);
            return false;
        }

        hr = found[0]->ActivateObject(IID_IMFTransform, (void**)&d->mft);
        for (UINT32 i = 0; i < count; i++) found[i]->Release();
        CoTaskMemFree(found);

        if (FAILED(hr) || !d->mft) { set_error("декодер не запустился", hr); return false; }

        IMFMediaType* in = 0;
        if (FAILED(MFCreateMediaType(&in))) return false;

        in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        in->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
        in->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        MFSetAttributeSize(in, MF_MT_FRAME_SIZE,
                           (UINT32)(width > 0 ? width : 1280),
                           (UINT32)(height > 0 ? height : 720));
        MFSetAttributeRatio(in, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);

        // Handing over the parameter sets up front saves the transform having
        // to discover the format and renegotiate mid-stream.
        if (sps_pps && len) in->SetBlob(MF_MT_MPEG_SEQUENCE_HEADER, sps_pps, len);

        hr = d->mft->SetInputType(0, in, 0);
        in->Release();
        if (FAILED(hr)) { set_error("SetInputType(H264)", hr); return false; }

        if (!vd_output_type(d)) { set_error("нет подходящего выходного формата", 0); return false; }
        if (!vd_prepare_buffer(d)) { set_error("не выделился выходной буфер", 0); return false; }

        d->mft->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
        d->mft->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
        return true;
    }

    void vd_stop(video_decoder* d)
    {
        if (d->mft)
        {
            d->mft->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
            d->mft->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
        }
        release(&d->out_buffer);
        release(&d->mft);
        if (d->rgba) { memfree(d->rgba); d->rgba = 0; }
        d->rgba_cap = 0;
    }

    bool vd_submit(video_decoder* d, const unsigned char* annexb, unsigned int len,
                   unsigned long long time_us)
    {
        IMFMediaBuffer* buffer = 0;
        if (FAILED(MFCreateMemoryBuffer(len, &buffer))) return false;

        BYTE* dst = 0;
        DWORD max = 0;
        if (FAILED(buffer->Lock(&dst, &max, 0))) { buffer->Release(); return false; }
        ccpy(dst, annexb, len);
        buffer->Unlock();
        buffer->SetCurrentLength(len);

        IMFSample* sample = 0;
        if (FAILED(MFCreateSample(&sample))) { buffer->Release(); return false; }

        sample->AddBuffer(buffer);
        sample->SetSampleTime((LONGLONG)(time_us * 10));       // 100 ns units
        buffer->Release();

        HRESULT hr = d->mft->ProcessInput(0, sample, 0);
        sample->Release();
        return SUCCEEDED(hr);
    }

    bool vd_ensure_rgba(video_decoder* d, int need)
    {
        if (need <= d->rgba_cap) return true;

        int cap = d->rgba_cap ? d->rgba_cap : (1 << 20);
        while (cap < need) cap *= 2;

        unsigned char* fresh = (unsigned char*)memalloc(cap);
        if (!fresh) return false;
        if (d->rgba) memfree(d->rgba);
        d->rgba = fresh;
        d->rgba_cap = cap;
        return true;
    }

    // Pulls one picture if there is one. Returns false when the transform
    // wants more input, which is the normal state most of the time.
    bool vd_next(video_decoder* d, const unsigned char** rgba, int* w, int* h)
    {
        MFT_OUTPUT_DATA_BUFFER out;
        ccfset(&out, 0, sizeof(out));
        out.pSample = 0;

        IMFSample* holder = 0;
        if (d->out_buffer)
        {
            if (FAILED(MFCreateSample(&holder))) return false;
            holder->AddBuffer(d->out_buffer);
            // Reused between calls; without this the transform sees a full
            // buffer and reports it has nowhere to write.
            d->out_buffer->SetCurrentLength(0);
            out.pSample = holder;
        }

        DWORD status = 0;
        HRESULT hr = d->mft->ProcessOutput(0, 1, &out, &status);

        if (hr == MF_E_TRANSFORM_STREAM_CHANGE)
        {
            // The stream said what size it really is. Settle the output type
            // again and try on the next pass.
            if (holder) holder->Release();
            if (out.pEvents) out.pEvents->Release();
            vd_output_type(d);
            vd_prepare_buffer(d);
            return false;
        }

        if (FAILED(hr) || !out.pSample)
        {
            if (holder) holder->Release();
            if (out.pEvents) out.pEvents->Release();
            return false;
        }

        IMFMediaBuffer* buffer = 0;
        bool made = false;

        if (SUCCEEDED(out.pSample->ConvertToContiguousBuffer(&buffer)))
        {
            BYTE* src = 0;
            DWORD max = 0, len = 0;
            if (SUCCEEDED(buffer->Lock(&src, &max, &len)))
            {
                int pitch = d->stride > 0 ? d->stride : d->buf_w;
                int plane_h = pitch ? (int)(len / (unsigned int)pitch * 2 / 3) : d->buf_h;
                if (plane_h < d->vis_h + d->vis_y) plane_h = d->buf_h;

                if (vd_ensure_rgba(d, d->vis_w * d->vis_h * 4))
                {
                    vdec::nv12_to_rgba(src, pitch, plane_h, d->vis_x, d->vis_y,
                                       d->rgba, d->vis_w, d->vis_h);
                    *rgba = d->rgba;
                    *w = d->vis_w;
                    *h = d->vis_h;
                    made = true;
                }
                buffer->Unlock();
            }
            buffer->Release();
        }

        if (out.pSample != holder) out.pSample->Release();
        if (holder) holder->Release();
        if (out.pEvents) out.pEvents->Release();
        return made;
    }

    // ---- audio decode --------------------------------------------------

    struct audio_decoder
    {
        IMFTransform* mft;
        int rate;
        int channels;
    };

    bool ad_start(audio_decoder* a, const unsigned char* config, unsigned int config_len,
                  int rate, int channels)
    {
        ccfset(a, 0, sizeof(*a));
        a->rate = rate > 0 ? rate : 48000;
        a->channels = channels > 0 ? channels : 2;

        MFT_REGISTER_TYPE_INFO want;
        want.guidMajorType = MFMediaType_Audio;
        want.guidSubtype = MFAudioFormat_AAC;

        IMFActivate** found = 0;
        UINT32 count = 0;
        HRESULT hr = MFTEnumEx(MFT_CATEGORY_AUDIO_DECODER,
                               MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_SORTANDFILTER,
                               &want, 0, &found, &count);
        if (FAILED(hr) || !count) { if (found) CoTaskMemFree(found); return false; }

        hr = found[0]->ActivateObject(IID_IMFTransform, (void**)&a->mft);
        for (UINT32 i = 0; i < count; i++) found[i]->Release();
        CoTaskMemFree(found);
        if (FAILED(hr) || !a->mft) return false;

        // HEAACWAVEINFO, then the AudioSpecificConfig from the mp4 track. The
        // twelve leading bytes are the part of the structure that follows the
        // wave format header, and payload type 0 means raw AAC frames with no
        // ADTS wrapper - which is exactly what an mp4 stores.
        unsigned char user[64];
        ccfset(user, 0, sizeof(user));
        unsigned int user_len = 12;
        if (config && config_len && config_len <= sizeof(user) - 12)
        {
            ccpy(user + 12, config, config_len);
            user_len += config_len;
        }

        IMFMediaType* in = 0;
        if (FAILED(MFCreateMediaType(&in))) return false;

        in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        in->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
        in->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, (UINT32)a->rate);
        in->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, (UINT32)a->channels);
        in->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        in->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 0);
        in->SetBlob(MF_MT_USER_DATA, user, user_len);

        hr = a->mft->SetInputType(0, in, 0);
        in->Release();
        if (FAILED(hr)) { release(&a->mft); return false; }

        IMFMediaType* out = 0;
        if (FAILED(MFCreateMediaType(&out))) { release(&a->mft); return false; }

        out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        out->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
        out->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, (UINT32)a->rate);
        out->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, (UINT32)a->channels);
        out->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        out->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, (UINT32)(a->channels * 2));
        out->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, (UINT32)(a->rate * a->channels * 2));
        out->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, 1);

        hr = a->mft->SetOutputType(0, out, 0);
        out->Release();
        if (FAILED(hr)) { release(&a->mft); return false; }

        a->mft->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
        a->mft->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
        return true;
    }

    void ad_stop(audio_decoder* a)
    {
        if (a->mft) a->mft->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
        release(&a->mft);
    }

    // Whatever came out of the decoder, turned into the 48 kHz stereo the
    // speakers want. Linear interpolation: a chat clip is not worth a
    // polyphase resampler, and the alternative is another Media Foundation
    // transform in the chain.
    void push_audio(const short* pcm, int frames, int rate, int channels)
    {
        if (frames <= 0 || channels <= 0) return;

        const int OUT_RATE = 48000;
        short out[4096];
        int out_frames_cap = 4096 / 2;

        double step = (double)rate / (double)OUT_RATE;
        double at = 0.0;
        int made = 0;

        while (at < (double)(frames - 1) && made < out_frames_cap)
        {
            int i = (int)at;
            double frac = at - (double)i;

            for (int c = 0; c < 2; c++)
            {
                int src_c = (channels == 1) ? 0 : (c < channels ? c : channels - 1);
                int a = pcm[i * channels + src_c];
                int b = pcm[(i + 1) * channels + src_c];
                out[made * 2 + c] = (short)(a + (int)((double)(b - a) * frac));
            }

            made++;
            at += step;
        }

        if (made) audio::write_media(out, made * 2);
    }

    // ---- the playback thread -------------------------------------------

    struct playback
    {
        mp4_file file;
        ubuffer blob;

        video_decoder video;
        audio_decoder audio;
        bool have_video;
        bool have_audio;

        unsigned int next_video;
        unsigned int next_audio;

        unsigned long long clock_us;        // where playback has got to
        unsigned long long started_ticks;   // wall clock at the last resume
        unsigned long long started_us;      // clock_us at the last resume
    };

    void publish_frame(const unsigned char* rgba, int w, int h)
    {
        int need = w * h * 4;

        // Filled outside the lock; only the swap is inside it.
        if (!g_back || g_back_cap < need)
        {
            unsigned char* fresh = (unsigned char*)memalloc(need);
            if (!fresh) return;
            if (g_back) memfree(g_back);
            g_back = fresh;
            g_back_cap = need;
        }

        ccpy(g_back, rgba, (size_t)need);

        EnterCriticalSection(&g_lock);

        unsigned char* was = g_front;
        int was_cap = g_front_w * g_front_h * 4;

        g_front = g_back;
        g_front_w = w;
        g_front_h = h;
        g_front_serial++;

        g_back = was;
        g_back_cap = was_cap;

        LeaveCriticalSection(&g_lock);
    }

    void feed_audio(playback* pb)
    {
        if (!pb->have_audio) return;

        // Keep a short lead, and no more. Whatever is queued here is sound
        // already committed to the card: the picture cannot catch up to it,
        // so this number is the floor on how far the two can drift apart.
        while (audio::media_backlog_ms() < 90 && pb->next_audio < pb->file.audio_samples)
        {
            mp4_sample s;
            if (!mp4::audio_sample(&pb->file, pb->next_audio, &s)) break;
            pb->next_audio++;

            IMFMediaBuffer* buffer = 0;
            if (FAILED(MFCreateMemoryBuffer(s.size, &buffer))) break;

            BYTE* dst = 0;
            DWORD max = 0;
            if (SUCCEEDED(buffer->Lock(&dst, &max, 0)))
            {
                ccpy(dst, s.data, s.size);
                buffer->Unlock();
                buffer->SetCurrentLength(s.size);
            }

            IMFSample* sample = 0;
            if (SUCCEEDED(MFCreateSample(&sample)))
            {
                sample->AddBuffer(buffer);
                sample->SetSampleTime((LONGLONG)(s.time_us * 10));
                pb->audio.mft->ProcessInput(0, sample, 0);
                sample->Release();
            }
            buffer->Release();

            // Drain whatever that produced.
            for (;;)
            {
                MFT_OUTPUT_DATA_BUFFER out;
                ccfset(&out, 0, sizeof(out));

                IMFSample* holder = 0;
                IMFMediaBuffer* space = 0;
                if (FAILED(MFCreateSample(&holder))) break;
                if (FAILED(MFCreateMemoryBuffer(64 * 1024, &space))) { holder->Release(); break; }
                holder->AddBuffer(space);
                out.pSample = holder;

                DWORD status = 0;
                HRESULT hr = pb->audio.mft->ProcessOutput(0, 1, &out, &status);

                if (SUCCEEDED(hr) && out.pSample)
                {
                    IMFMediaBuffer* got = 0;
                    if (SUCCEEDED(out.pSample->ConvertToContiguousBuffer(&got)))
                    {
                        BYTE* src = 0;
                        DWORD cap = 0, len = 0;
                        if (SUCCEEDED(got->Lock(&src, &cap, &len)) && len)
                        {
                            if (!g_muted)
                            {
                                int frames = (int)(len / (unsigned int)(pb->audio.channels * 2));
                                push_audio((const short*)src, frames,
                                           pb->audio.rate, pb->audio.channels);
                            }
                            got->Unlock();
                        }
                        got->Release();
                    }
                }

                if (out.pEvents) out.pEvents->Release();
                space->Release();
                holder->Release();

                if (FAILED(hr) || !SUCCEEDED(hr)) break;
                if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) break;
                break;   // one output per input is the normal case for AAC
            }
        }
    }

    void teardown(playback* pb)
    {
        if (pb->have_video) vd_stop(&pb->video);
        if (pb->have_audio) ad_stop(&pb->audio);
        mp4::close(&pb->file);
        pb->blob.free_buffer();
        audio::clear_media();
    }

    DWORD WINAPI play_thread(LPVOID)
    {
        CoInitializeEx(0, COINIT_MULTITHREADED);
        MFStartup(MF_VERSION, MFSTARTUP_LITE);

        playback pb;
        ccfset(&pb, 0, sizeof(pb));
        pb.blob.init();

        char url[700];
        EnterCriticalSection(&g_lock);
        ccstrncpy(url, g_url, sizeof(url) - 1);
        LeaveCriticalSection(&g_lock);

        InterlockedExchange(&g_state, PLAYER_LOADING);

        if (!tex::fetch_blob(url, &pb.blob) || !pb.blob.size)
        {
            ccstrncpy(g_error, "не удалось скачать файл", sizeof(g_error) - 1);
            InterlockedExchange(&g_state, PLAYER_FAILED);
            goto done;
        }

        if (!mp4::open(&pb.file, pb.blob.data, pb.blob.size))
        {
            ccstrncpy(g_error, mp4::last_error(), sizeof(g_error) - 1);
            InterlockedExchange(&g_state, PLAYER_FAILED);
            goto done;
        }

        g_duration_us = pb.file.duration_us;
        g_width = pb.file.width;
        g_height = pb.file.height;

        if (pb.file.has_video())
        {
            pb.have_video = vd_start(&pb.video, pb.file.video_header.data,
                                     pb.file.video_header.size,
                                     pb.file.width, pb.file.height);
            if (pb.have_video)
                vd_submit(&pb.video, pb.file.video_header.data,
                          pb.file.video_header.size, 0);
        }

        if (pb.file.has_audio())
            pb.have_audio = ad_start(&pb.audio, pb.file.audio_config,
                                     pb.file.audio_config_size,
                                     pb.file.audio_rate, pb.file.audio_channels);

        if (!pb.have_video && !pb.have_audio)
        {
            ccstrncpy(g_error, "ни картинку, ни звук раскодировать нечем", sizeof(g_error) - 1);
            InterlockedExchange(&g_state, PLAYER_FAILED);
            goto done;
        }

        // The speakers may not be running: nothing else has needed them
        // unless a call is up.
        if (pb.have_audio && !audio::render_active()) audio::start_render();

        // The first picture and the first sound both take a moment to come
        // out of their decoders. Starting the clock before then leaves the
        // film already a fraction of a second old when it begins, and the
        // sound permanently behind by however long that took.
        if (pb.have_audio)
        {
            feed_audio(&pb);
            while (g_running && audio::media_backlog_ms() < 40 &&
                   pb.next_audio < pb.file.audio_samples)
                feed_audio(&pb);
        }

        InterlockedExchange(&g_state, PLAYER_PLAYING);
        pb.started_ticks = GetTickCount64();
        pb.started_us = 0;

        while (g_running)
        {
            if (WaitForSingleObject(g_stop_event, 0) == WAIT_OBJECT_0) break;

            if (g_seek_pending)
            {
                InterlockedExchange(&g_seek_pending, 0);

                unsigned long long to = g_seek_to_us;
                if (pb.have_video)
                {
                    pb.next_video = mp4::seek_video(&pb.file, to);
                    pb.video.mft->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
                    vd_submit(&pb.video, pb.file.video_header.data,
                              pb.file.video_header.size, 0);
                }
                if (pb.have_audio)
                {
                    pb.next_audio = mp4::seek_audio(&pb.file, to);
                    pb.audio.mft->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
                }

                audio::clear_media();
                pb.clock_us = to;
                pb.started_us = to;
                pb.started_ticks = GetTickCount64();
                g_position_us = to;

                // Decode up to where the slider was dropped and show it. Being
                // able to scrub while paused is the whole point of pausing to
                // find a moment, and without this the picture stayed on
                // whatever frame was up when the pause happened.
                if (pb.have_video)
                {
                    const unsigned char* rgba = 0;
                    int w = 0, h = 0;
                    bool shown = false;

                    for (int guard = 0; guard < 240 && !shown; guard++)
                    {
                        if (pb.next_video >= pb.file.video_samples) break;

                        unsigned long long when = 0;
                        if (!mp4::video_time(&pb.file, pb.next_video, &when)) break;

                        mp4_sample s;
                        if (mp4::video_sample(&pb.file, pb.next_video, &s))
                            vd_submit(&pb.video, s.data, s.size, s.time_us);
                        pb.next_video++;

                        while (vd_next(&pb.video, &rgba, &w, &h))
                        {
                            publish_frame(rgba, w, h);
                            // Past the mark is close enough: the nearest
                            // keyframe may be a second or two behind it.
                            if (when + 40000ULL >= to) shown = true;
                        }
                    }
                }
            }

            if (g_want_pause)
            {
                if (g_state == PLAYER_PLAYING)
                {
                    InterlockedExchange(&g_state, PLAYER_PAUSED);
                    audio::clear_media();
                }

                // Idling on a paused video costs nothing but the wait.
                if (WaitForSingleObject(g_stop_event, 20) == WAIT_OBJECT_0) break;
                continue;
            }

            if (g_state == PLAYER_PAUSED)
            {
                InterlockedExchange(&g_state, PLAYER_PLAYING);
                pb.started_us = pb.clock_us;
                pb.started_ticks = GetTickCount64();
            }

            // Where the film should be by now. The wall clock is the master:
            // sound comes out of the card at its own pace and cannot be told
            // to wait, so the picture is what follows.
            pb.clock_us = pb.started_us + (GetTickCount64() - pb.started_ticks) * 1000ULL;
            g_position_us = pb.clock_us;

            feed_audio(&pb);

            bool ended = true;

            bool did_work = false;

            if (pb.have_video && pb.next_video < pb.file.video_samples)
            {
                ended = false;

                // Ask when the next picture is due before unpacking it.
                // Unpacking first meant rewriting the same frame on every
                // pass of this loop - a couple of hundred times over - just
                // to look at its timestamp and put it back.
                unsigned long long when = 0;
                if (!mp4::video_time(&pb.file, pb.next_video, &when))
                {
                    pb.next_video++;
                }
                else if (when <= pb.clock_us + 60000ULL)
                {
                    mp4_sample s;
                    if (mp4::video_sample(&pb.file, pb.next_video, &s))
                        vd_submit(&pb.video, s.data, s.size, s.time_us);

                    pb.next_video++;
                    did_work = true;

                    const unsigned char* rgba = 0;
                    int w = 0, h = 0;
                    while (vd_next(&pb.video, &rgba, &w, &h))
                        publish_frame(rgba, w, h);
                }
            }

            if (pb.have_audio && pb.next_audio < pb.file.audio_samples) ended = false;

            if (ended)
            {
                InterlockedExchange(&g_state, PLAYER_ENDED);
                break;
            }

            // Nothing was due this pass, so there is no reason to come round
            // again in four milliseconds. Waiting properly is what keeps a
            // thirty frame video from spinning a core at two hundred and
            // fifty passes a second.
            if (!did_work && WaitForSingleObject(g_stop_event, 6) == WAIT_OBJECT_0) break;
        }

    done:
        teardown(&pb);
        MFShutdown();
        CoUninitialize();
        InterlockedExchange(&g_running, 0);
        return 0;
    }

    void stop_thread()
    {
        if (!g_thread) return;

        InterlockedExchange(&g_running, 0);
        SetEvent(g_stop_event);
        WaitForSingleObject(g_thread, 4000);
        CloseHandle(g_thread);
        g_thread = 0;
        ResetEvent(g_stop_event);
    }
}

bool player::self_test(const wchar_t* path)
{
    CoInitializeEx(0, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION, MFSTARTUP_LITE);

    ubuffer blob;
    blob.init();

    if (!ufile::read_all(path, &blob) || !blob.size)
    {
        log_line("mp4test: файл не читается");
        blob.free_buffer();
        return false;
    }
    log_line("mp4test: %u байт", blob.size);

    mp4_file file;
    if (!mp4::open(&file, blob.data, blob.size))
    {
        log_line("mp4test: %s", mp4::last_error());
        blob.free_buffer();
        return false;
    }

    log_line("mp4test: видео дорожка %d, звук %d, заголовок картинки %u байт",
             file.video_track, file.audio_track, file.video_header.size);

    bool ok = false;

    if (file.has_video())
    {
        video_decoder vd;
        if (!vd_start(&vd, file.video_header.data, file.video_header.size,
                      file.width, file.height))
        {
            log_line("mp4test: декодер не поднялся: %s", g_error);
        }
        else
        {
            log_line("mp4test: декодер поднялся, буфер %dx%d, видимое %dx%d, шаг %d",
                     vd.buf_w, vd.buf_h, vd.vis_w, vd.vis_h, vd.stride);

            vd_submit(&vd, file.video_header.data, file.video_header.size, 0);

            unsigned int fed = 0, got = 0;
            unsigned long long first_luma = 0;

            for (unsigned int i = 0; i < file.video_samples && i < 120; i++)
            {
                mp4_sample s;
                if (!mp4::video_sample(&file, i, &s))
                {
                    log_line("mp4test: кадр %u не достался", i);
                    continue;
                }

                if (i == 0)
                    log_line("mp4test: первый кадр %u байт, начинается %02x %02x %02x %02x %02x",
                             s.size, s.data[0], s.data[1], s.data[2], s.data[3], s.data[4]);

                if (vd_submit(&vd, s.data, s.size, s.time_us)) fed++;

                const unsigned char* rgba = 0;
                int w = 0, h = 0;
                while (vd_next(&vd, &rgba, &w, &h))
                {
                    if (!got)
                    {
                        // A picture that decodes to nothing but black is the
                        // same as no picture, so the brightness is worth
                        // knowing before anybody goes looking at the ui.
                        unsigned long long sum = 0;
                        for (int k = 0; k < w * h; k++) sum += rgba[k * 4];
                        first_luma = sum / (unsigned long long)(w * h);
                        log_line("mp4test: первая картинка %dx%d, средняя яркость %llu",
                                 w, h, first_luma);
                    }
                    got++;
                }
            }

            log_line("mp4test: скормлено %u, получено %u картинок", fed, got);
            ok = got > 0;
            vd_stop(&vd);
        }
    }

    mp4::close(&file);
    blob.free_buffer();

    MFShutdown();
    CoUninitialize();

    log_line("mp4test: итог %s", ok ? "картинка есть" : "КАРТИНКИ НЕТ");
    return ok;
}

void player::init()
{
    if (g_locked_ready) return;
    InitializeCriticalSection(&g_lock);
    g_stop_event = CreateEventW(0, TRUE, FALSE, 0);
    g_locked_ready = true;
}

void player::shutdown()
{
    if (!g_locked_ready) return;

    stop_thread();
    if (g_front) { memfree(g_front); g_front = 0; }
    if (g_back) { memfree(g_back); g_back = 0; g_back_cap = 0; }
    if (g_stop_event) { CloseHandle(g_stop_event); g_stop_event = 0; }
    DeleteCriticalSection(&g_lock);
    g_locked_ready = false;
}

void player::open(const char* url)
{
    if (!url || !url[0]) return;
    if (!g_locked_ready) init();

    stop_thread();

    EnterCriticalSection(&g_lock);
    ccfset(g_url, 0, sizeof(g_url));
    ccstrncpy(g_url, url, sizeof(g_url) - 1);
    g_front_w = 0;
    g_front_h = 0;
    g_front_serial++;
    LeaveCriticalSection(&g_lock);

    g_error[0] = 0;
    g_position_us = 0;
    g_duration_us = 0;
    g_width = 0;
    g_height = 0;
    InterlockedExchange(&g_want_pause, 0);
    InterlockedExchange(&g_seek_pending, 0);
    InterlockedExchange(&g_watch_ticks, 0);
    InterlockedExchange(&g_state, PLAYER_LOADING);
    InterlockedExchange(&g_running, 1);

    g_thread = CreateThread(0, 0, play_thread, 0, 0, 0);
    if (!g_thread)
    {
        InterlockedExchange(&g_running, 0);
        InterlockedExchange(&g_state, PLAYER_FAILED);
    }
}

void player::stop()
{
    stop_thread();
    audio::clear_media();

    EnterCriticalSection(&g_lock);
    ccfset(g_url, 0, sizeof(g_url));
    g_front_w = 0;
    g_front_h = 0;
    LeaveCriticalSection(&g_lock);

    InterlockedExchange(&g_state, PLAYER_IDLE);
}

void player::toggle_pause()
{
    InterlockedExchange(&g_want_pause, g_want_pause ? 0 : 1);
}

void player::seek(unsigned long long time_us)
{
    g_seek_to_us = time_us;
    InterlockedExchange(&g_seek_pending, 1);
}

void player::set_muted(bool m)
{
    InterlockedExchange(&g_muted, m ? 1 : 0);
    if (m) audio::clear_media();
}

bool player::muted() { return g_muted != 0; }

player_state player::state() { return (player_state)g_state; }
const char* player::last_error() { return g_error; }

const char* player::current_url()
{
    return g_url;
}

bool player::is_current(const char* url)
{
    if (!url || !g_url[0]) return false;
    return ccscmp(g_url, url) == 0;
}

unsigned long long player::position_us() { return g_position_us; }
unsigned long long player::duration_us() { return g_duration_us; }
int player::width() { return g_width; }
int player::height() { return g_height; }

const unsigned char* player::frame_rgba(unsigned int* out_serial)
{
    if (!g_locked_ready) return 0;

    InterlockedExchange(&g_watch_ticks, 0);

    EnterCriticalSection(&g_lock);
    const unsigned char* result = (g_front && g_front_w > 0) ? g_front : 0;
    if (out_serial) *out_serial = g_front_serial;
    LeaveCriticalSection(&g_lock);

    return result;
}

void player::tick()
{
    if (g_state == PLAYER_IDLE) return;

    // Nobody has drawn the video for a couple of seconds: the message scrolled
    // away, or the chat was closed. Let the file go rather than keep decoding
    // into a picture nobody reads.
    if (InterlockedIncrement(&g_watch_ticks) > 120 && g_state != PLAYER_LOADING)
        player::stop();
}
