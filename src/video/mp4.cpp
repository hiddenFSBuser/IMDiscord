#include "pch.h"
#include "mp4.h"
#include "core/log.h"

#include "minimp4/minimp4.h"

namespace
{
    char g_error[192] = { 0 };

    void fail(const char* what)
    {
        ccstrncpy(g_error, what, sizeof(g_error) - 1);
        log_line("mp4: %s", what);
    }

    struct demux_state
    {
        MP4D_demux_t mp4;

        // Where each track's samples were found, so the player can walk them
        // without asking minimp4 to re-derive the answer every frame.
        ubuffer video_scratch;

        // Timescales, kept out of the track struct so the conversion to
        // microseconds is one multiply rather than a lookup.
        unsigned int video_scale;
        unsigned int audio_scale;

        // Bytes of length in front of each unit of a video sample.
        int prefix;

        // Every keyframe in the file, found once. Seeking used to walk the
        // whole track asking the index for one sample at a time, which on a
        // long video is thousands of lookups for every twitch of the slider.
        ulist<unsigned int> keyframes;
        ulist<unsigned int> keyframe_us;
    };

    // minimp4 reads through a callback rather than from a pointer, which is
    // what lets it work on a file being streamed. Everything here is already
    // in memory, so the callback is a bounds check and a copy.
    int read_from_memory(int64_t offset, void* buffer, size_t size, void* token)
    {
        mp4_file* f = (mp4_file*)token;
        if (offset < 0 || offset + (int64_t)size > (int64_t)f->length) return 1;

        ccpy(buffer, f->bytes + offset, size);
        return 0;
    }

    const unsigned char* bytes_at(const mp4_file* f, unsigned long long offset)
    {
        return f->bytes + offset;
    }

    unsigned long long to_us(unsigned int ticks, unsigned int scale)
    {
        if (!scale) return 0;
        return (unsigned long long)ticks * 1000000ULL / (unsigned long long)scale;
    }

    // An mp4 keeps H.264 as [length][unit][length][unit]..., with the width of
    // the length field written once in the track header. Decoders want the
    // start codes of an Annex-B stream instead, so every prefix is rewritten
    // in place of being merely skipped.
    bool avcc_to_annexb(const unsigned char* in, unsigned int len,
                        int prefix_bytes, ubuffer* out)
    {
        out->clear();

        unsigned int at = 0;
        while (at + (unsigned int)prefix_bytes <= len)
        {
            unsigned int size = 0;
            for (int i = 0; i < prefix_bytes; i++)
                size = (size << 8) | in[at + i];

            at += (unsigned int)prefix_bytes;
            if (!size || at + size > len) break;

            static const unsigned char start[4] = { 0, 0, 0, 1 };
            out->append(start, 4);
            out->append(in + at, size);

            at += size;
        }

        return out->size > 4;
    }

    // The parameter sets live in the track's decoder-specific info, not in any
    // sample. Without them in front of the first picture the decoder has no
    // idea how big the frame is and produces nothing at all.
    void build_video_header(mp4_file* f, demux_state* st)
    {
        f->video_header.clear();

        static const unsigned char start[4] = { 0, 0, 0, 1 };

        for (int i = 0;; i++)
        {
            int bytes = 0;
            const void* sps = MP4D_read_sps(&st->mp4, (unsigned int)f->video_track, i, &bytes);
            if (!sps || bytes <= 0) break;

            f->video_header.append(start, 4);
            f->video_header.append(sps, (unsigned int)bytes);
        }

        for (int i = 0;; i++)
        {
            int bytes = 0;
            const void* pps = MP4D_read_pps(&st->mp4, (unsigned int)f->video_track, i, &bytes);
            if (!pps || bytes <= 0) break;

            f->video_header.append(start, 4);
            f->video_header.append(pps, (unsigned int)bytes);
        }
    }

    // How wide the length prefix in front of each unit is.
    //
    // The avcC record says so in its fifth byte, and minimp4 reads that byte,
    // masks it, and throws it away - what it keeps as `dsi` begins after it,
    // at the parameter set count. Reading dsi[4] therefore picks up a byte of
    // an SPS and gets a plausible-looking wrong answer: on the first file
    // tried it said three, every length came out 256 times too small, and the
    // decoder was handed 19 bytes of nonsense per frame.
    //
    // So it is worked out from the data instead. A width is right when the
    // lengths tile the sample exactly and every unit header is well formed;
    // wrong widths fall off the end almost immediately. Four is by far the
    // most common, so it is tried first and the rest are a formality.
    int detect_prefix_width(const unsigned char* sample, unsigned int len)
    {
        static const int widths[4] = { 4, 2, 1, 3 };

        for (int w = 0; w < 4; w++)
        {
            int prefix = widths[w];
            unsigned int at = 0;
            bool sane = true;
            int units = 0;

            while (at + (unsigned int)prefix <= len)
            {
                unsigned int size = 0;
                for (int i = 0; i < prefix; i++) size = (size << 8) | sample[at + i];
                at += (unsigned int)prefix;

                if (!size || at + size > len) { sane = false; break; }

                // Bit seven of a unit header is required to be zero. It is the
                // cheapest way to notice that the walk has drifted.
                if (sample[at] & 0x80) { sane = false; break; }

                at += size;
                units++;
            }

            if (sane && at == len && units > 0) return prefix;
        }

        return 4;
    }
}

bool mp4::open(mp4_file* f, const unsigned char* bytes, unsigned int length)
{
    ccfset(f, 0, sizeof(*f));
    f->video_track = -1;
    f->audio_track = -1;
    f->bytes = bytes;
    f->length = length;
    f->video_header.init(1024);

    if (!bytes || length < 32) { fail("файл слишком короткий"); return false; }

    demux_state* st = (demux_state*)memalloc(sizeof(demux_state));
    if (!st) { fail("нет памяти"); return false; }
    ccfset(st, 0, sizeof(*st));
    st->video_scratch.init(1 << 16);
    st->keyframes = ulist<unsigned int>();
    st->keyframe_us = ulist<unsigned int>();

    if (!MP4D_open(&st->mp4, read_from_memory, f, (int64_t)length))
    {
        st->video_scratch.free_buffer();
        memfree(st);
        fail("не разобрался в файле");
        return false;
    }

    f->handle = st;

    for (unsigned int i = 0; i < st->mp4.track_count; i++)
    {
        MP4D_track_t* tr = &st->mp4.track[i];

        if (tr->handler_type == MP4D_HANDLER_TYPE_VIDE && f->video_track < 0)
        {
            // Only H.264. Windows will decode HEVC too, but only where the
            // codec has been installed, and a video that silently stays black
            // is worse than one that says it cannot be played.
            if (tr->object_type_indication != MP4_OBJECT_TYPE_AVC) continue;

            f->video_track = (int)i;
            f->width = (int)tr->SampleDescription.video.width;
            f->height = (int)tr->SampleDescription.video.height;
            f->video_samples = tr->sample_count;
            st->video_scale = tr->timescale;
            build_video_header(f, st);

            // Settled from the first sample, then reused for the rest.
            st->prefix = 4;
            if (tr->sample_count)
            {
                unsigned int bytes = 0, ts = 0, dur = 0;
                MP4D_file_offset_t at = MP4D_frame_offset(&st->mp4, i, 0, &bytes, &ts, &dur);
                if (bytes && at + bytes <= length)
                    st->prefix = detect_prefix_width(bytes_at(f, at), bytes);
            }

            // One pass over the track now, so seeking later is a lookup.
            for (unsigned int k = 0; k < tr->sample_count; k++)
            {
                unsigned int bytes = 0, ts = 0, dur = 0;
                MP4D_file_offset_t at = MP4D_frame_offset(&st->mp4, i, k, &bytes, &ts, &dur);
                if (!bytes || at + bytes > length) break;
                if (bytes <= (unsigned int)st->prefix) continue;

                // Unit type five is an IDR: the only kind that can be decoded
                // without whatever came before it.
                if ((f->bytes[at + st->prefix] & 0x1F) == 5)
                {
                    st->keyframes.push(k);
                    st->keyframe_us.push((unsigned int)(to_us(ts, tr->timescale) / 1000));
                }
            }
        }
        else if (tr->handler_type == MP4D_HANDLER_TYPE_SOUN && f->audio_track < 0)
        {
            f->audio_track = (int)i;
            f->audio_samples = tr->sample_count;
            f->audio_rate = (int)tr->SampleDescription.audio.samplerate_hz;
            f->audio_channels = (int)tr->SampleDescription.audio.channelcount;
            f->audio_config = tr->dsi;
            f->audio_config_size = tr->dsi_bytes;
            st->audio_scale = tr->timescale;
        }
    }

    if (st->mp4.timescale)
    {
        unsigned long long ticks =
            ((unsigned long long)st->mp4.duration_hi << 32) | st->mp4.duration_lo;
        f->duration_us = ticks * 1000000ULL / st->mp4.timescale;
    }

    if (f->video_track < 0 && f->audio_track < 0)
    {
        fail("ни картинки, ни звука");
        mp4::close(f);
        return false;
    }

    if (f->video_track >= 0 && f->video_header.size == 0)
    {
        fail("в файле нет параметров картинки");
        mp4::close(f);
        return false;
    }

    // A fragmented mp4 keeps its index in the fragments rather than up front,
    // and the demuxer here does not follow them: the tracks are found but
    // every one of them is empty. Saying so beats a card that never starts.
    if (f->video_samples == 0 && f->audio_samples == 0)
    {
        fail("фрагментированный mp4 не поддерживается");
        mp4::close(f);
        return false;
    }

    log_line("mp4: %dx%d, %llu мс, видео %u кадров (префикс %d), звук %u кадров (%d Гц, %d кан)",
             f->width, f->height, f->duration_us / 1000,
             f->video_samples, st->prefix,
             f->audio_samples, f->audio_rate, f->audio_channels);

    g_error[0] = 0;
    return true;
}

void mp4::close(mp4_file* f)
{
    if (!f) return;

    demux_state* st = (demux_state*)f->handle;
    if (st)
    {
        MP4D_close(&st->mp4);
        st->video_scratch.free_buffer();
        st->keyframes.dispose();
        st->keyframe_us.dispose();
        memfree(st);
    }

    f->video_header.free_buffer();
    ccfset(f, 0, sizeof(*f));
    f->video_track = -1;
    f->audio_track = -1;
}

bool mp4::video_sample(mp4_file* f, unsigned int index, mp4_sample* out)
{
    demux_state* st = (demux_state*)f->handle;
    if (!st || f->video_track < 0 || index >= f->video_samples) return false;

    unsigned int bytes = 0, timestamp = 0, duration = 0;
    MP4D_file_offset_t at = MP4D_frame_offset(&st->mp4, (unsigned int)f->video_track,
                                              index, &bytes, &timestamp, &duration);
    if (!bytes || at + bytes > f->length) return false;

    if (!avcc_to_annexb(f->bytes + at, bytes, st->prefix, &st->video_scratch))
        return false;

    out->data = st->video_scratch.data;
    out->size = st->video_scratch.size;
    out->time_us = to_us(timestamp, st->video_scale);
    out->duration_us = to_us(duration, st->video_scale);
    return true;
}

bool mp4::video_time(mp4_file* f, unsigned int index, unsigned long long* out_us)
{
    demux_state* st = (demux_state*)f->handle;
    if (!st || f->video_track < 0 || index >= f->video_samples) return false;

    unsigned int bytes = 0, timestamp = 0, duration = 0;
    MP4D_frame_offset(&st->mp4, (unsigned int)f->video_track, index,
                      &bytes, &timestamp, &duration);
    if (!bytes) return false;

    *out_us = to_us(timestamp, st->video_scale);
    return true;
}

bool mp4::audio_sample(mp4_file* f, unsigned int index, mp4_sample* out)
{
    demux_state* st = (demux_state*)f->handle;
    if (!st || f->audio_track < 0 || index >= f->audio_samples) return false;

    unsigned int bytes = 0, timestamp = 0, duration = 0;
    MP4D_file_offset_t at = MP4D_frame_offset(&st->mp4, (unsigned int)f->audio_track,
                                              index, &bytes, &timestamp, &duration);
    if (!bytes || at + bytes > f->length) return false;

    // Audio needs no rewriting: an mp4 stores raw AAC frames.
    out->data = f->bytes + at;
    out->size = bytes;
    out->time_us = to_us(timestamp, st->audio_scale);
    out->duration_us = to_us(duration, st->audio_scale);
    return true;
}

unsigned int mp4::seek_video(mp4_file* f, unsigned long long time_us)
{
    demux_state* st = (demux_state*)f->handle;
    if (!st || f->video_track < 0 || !st->keyframes.count) return 0;

    // The last keyframe at or before the moment asked for. A picture that is
    // not one cannot be decoded on its own, so landing anywhere else would
    // show rubbish until the next.
    unsigned int want_ms = (unsigned int)(time_us / 1000);
    unsigned int best = st->keyframes[0];

    for (unsigned int i = 0; i < st->keyframes.count; i++)
    {
        if (st->keyframe_us[i] > want_ms) break;
        best = st->keyframes[i];
    }

    return best;
}

unsigned int mp4::seek_audio(mp4_file* f, unsigned long long time_us)
{
    demux_state* st = (demux_state*)f->handle;
    if (!st || f->audio_track < 0) return 0;

    for (unsigned int i = 0; i < f->audio_samples; i++)
    {
        unsigned int bytes = 0, timestamp = 0, duration = 0;
        MP4D_frame_offset(&st->mp4, (unsigned int)f->audio_track, i,
                          &bytes, &timestamp, &duration);

        if (to_us(timestamp, st->audio_scale) >= time_us) return i;
    }

    return f->audio_samples ? f->audio_samples - 1 : 0;
}

const char* mp4::last_error() { return g_error; }
