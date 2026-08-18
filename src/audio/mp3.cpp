#include "pch.h"
#include "mp3.h"
#include "audio.h"

#include "core/log.h"

#include "minimp3/minimp3_ex.h"
#include "speexdsp/speex_resampler.h"

namespace
{
    // One pull of the decoder. Big enough that the resampler is handed
    // something worth its while, small enough to sit in a heap block nobody
    // notices.
    const int PENDING_FRAMES = 4096;

    mp3dec_ex_t* dec_of(mp3_stream* s) { return (mp3dec_ex_t*)s->decoder; }

    // Whatever the file has, as stereo. One channel is copied into both;
    // more than two are cut down to the first two, which is the front pair on
    // every layout mp3 can carry.
    void to_stereo(const short* in, int channels, int frames, short* out)
    {
        if (channels == 2)
        {
            ccpy(out, in, (size_t)frames * 2 * sizeof(short));
            return;
        }

        if (channels == 1)
        {
            for (int i = 0; i < frames; i++)
            {
                out[i * 2 + 0] = in[i];
                out[i * 2 + 1] = in[i];
            }
            return;
        }

        for (int i = 0; i < frames; i++)
        {
            out[i * 2 + 0] = in[i * channels + 0];
            out[i * 2 + 1] = in[i * channels + 1];
        }
    }

    // Refills the pending buffer. False when the file is over.
    bool pull(mp3_stream* s)
    {
        s->pending_frames = 0;
        s->pending_used = 0;

        if (s->ended) return false;

        // Read into the tail of the same block and expand in place, so a
        // stereo file needs no second buffer at all.
        int want = PENDING_FRAMES * s->channels;

        short* raw = (short*)memalloc((int)(want * sizeof(short)));
        if (!raw) return false;

        size_t got = mp3dec_ex_read(dec_of(s), raw, (size_t)want);
        int frames = (int)(got / (size_t)s->channels);

        if (frames > 0) to_stereo(raw, s->channels, frames, s->pending);
        memfree(raw);

        if (frames <= 0) { s->ended = true; return false; }

        s->pending_frames = frames;
        return true;
    }
}

bool mp3::open(mp3_stream* s, const unsigned char* bytes, unsigned int length)
{
    if (!s || !bytes || !length) return false;

    ccfset(s, 0, sizeof(*s));
    s->bytes = bytes;
    s->length = length;

    mp3dec_ex_t* dec = (mp3dec_ex_t*)memalloc((int)sizeof(mp3dec_ex_t));
    if (!dec) return false;

    // Seeking to an exact sample rather than to a byte. It costs a scan of
    // the file at open time to build the index, which is what also gives an
    // honest duration for a variable bitrate track - the alternative is a
    // progress bar that lies.
    if (mp3dec_ex_open_buf(dec, bytes, length, MP3D_SEEK_TO_SAMPLE))
    {
        memfree(dec);
        return false;
    }

    s->decoder = dec;
    s->rate = dec->info.hz;
    s->channels = dec->info.channels;

    if (s->rate <= 0 || s->channels <= 0)
    {
        mp3::close(s);
        return false;
    }

    unsigned long long in_frames = dec->samples / (unsigned long long)s->channels;
    s->frames = in_frames * (unsigned long long)AUDIO_SAMPLE_RATE / (unsigned long long)s->rate;

    s->pending = (short*)memalloc((int)(PENDING_FRAMES * 2 * sizeof(short)));
    if (!s->pending)
    {
        mp3::close(s);
        return false;
    }

    if (s->rate != AUDIO_SAMPLE_RATE)
    {
        int err = 0;
        // Quality 7 of 10. Above this the gain is inaudible and the cost is
        // not: this runs on the voice tick, twenty milliseconds at a time,
        // beside an encoder that also has to finish inside the same window.
        s->resampler = speex_resampler_init(2, (spx_uint32_t)s->rate,
                                            (spx_uint32_t)AUDIO_SAMPLE_RATE, 7, &err);
        if (!s->resampler)
        {
            log_line("mp3: resampler %d -> 48000 failed (%d)", s->rate, err);
            mp3::close(s);
            return false;
        }
    }

    log_line("mp3: %d Hz, %d ch, %llu frames at 48k", s->rate, s->channels, s->frames);
    return true;
}

void mp3::close(mp3_stream* s)
{
    if (!s) return;

    if (s->decoder)
    {
        mp3dec_ex_close(dec_of(s));
        memfree(s->decoder);
    }
    if (s->resampler) speex_resampler_destroy((SpeexResamplerState*)s->resampler);
    if (s->pending) memfree(s->pending);

    ccfset(s, 0, sizeof(*s));
}

int mp3::read(mp3_stream* s, short* out, int frames)
{
    if (!s || !s->decoder || !out || frames <= 0) return 0;

    int made = 0;

    while (made < frames)
    {
        if (s->pending_used >= s->pending_frames && !pull(s)) break;

        int have = s->pending_frames - s->pending_used;
        const short* src = s->pending + s->pending_used * 2;

        if (!s->resampler)
        {
            int take = have < (frames - made) ? have : (frames - made);
            ccpy(out + made * 2, src, (size_t)take * 2 * sizeof(short));

            made += take;
            s->pending_used += take;
            continue;
        }

        spx_uint32_t in_len = (spx_uint32_t)have;
        spx_uint32_t out_len = (spx_uint32_t)(frames - made);

        speex_resampler_process_interleaved_int((SpeexResamplerState*)s->resampler,
                                                src, &in_len, out + made * 2, &out_len);

        // Neither side is guaranteed to move: the resampler holds samples back
        // to fill its window. Without this guard an unlucky ratio would spin
        // here forever rather than asking the decoder for more.
        if (in_len == 0 && out_len == 0)
        {
            s->pending_used = s->pending_frames;
            continue;
        }

        s->pending_used += (int)in_len;
        made += (int)out_len;
    }

    s->at += (unsigned long long)made;
    return made;
}

void mp3::seek(mp3_stream* s, unsigned long long frame)
{
    if (!s || !s->decoder) return;
    if (frame > s->frames) frame = s->frames;

    unsigned long long in_frame =
        frame * (unsigned long long)s->rate / (unsigned long long)AUDIO_SAMPLE_RATE;

    mp3dec_ex_seek(dec_of(s), in_frame * (unsigned long long)s->channels);

    // The resampler carries the tail of what came before it in its window, and
    // playing that after a jump is a click at best.
    //
    // Thrown away and built again rather than reset: speex_resampler_reset_mem
    // walks the filter memory as one flat run of nb_channels*(filt_len-1)
    // samples, while the memory is really laid out one mem_alloc_size block
    // per channel - and those two are not the same length. The second channel
    // therefore keeps its history across a reset. It shows up exactly as you
    // would expect, as a click on one side only, and it cost a while to find.
    // A seek happens when somebody drags a slider, so building a new one is
    // free at the rate it happens.
    if (s->resampler)
    {
        speex_resampler_destroy((SpeexResamplerState*)s->resampler);
        s->resampler = 0;

        int err = 0;
        s->resampler = speex_resampler_init(2, (spx_uint32_t)s->rate,
                                            (spx_uint32_t)AUDIO_SAMPLE_RATE, 7, &err);
    }

    s->pending_frames = 0;
    s->pending_used = 0;
    s->ended = false;
    s->at = frame;
}

bool mp3::decode_all_mono(const unsigned char* bytes, unsigned int length,
                          short** out_pcm, unsigned int* out_frames)
{
    if (out_pcm) *out_pcm = 0;
    if (out_frames) *out_frames = 0;

    mp3_stream s;
    if (!mp3::open(&s, bytes, length)) return false;

    // The length is known up front, so this is one allocation rather than a
    // buffer that doubles. A couple of frames of slack covers the resampler
    // rounding a ratio the other way.
    unsigned int cap = (unsigned int)s.frames + 64;
    short* mono = (short*)memalloc((int)(cap * sizeof(short)));

    if (!mono) { mp3::close(&s); return false; }

    const int CHUNK = 1024;
    short stereo[CHUNK * 2];
    unsigned int at = 0;

    for (;;)
    {
        int want = (int)(cap - at);
        if (want <= 0) break;
        if (want > CHUNK) want = CHUNK;

        int got = mp3::read(&s, stereo, want);
        if (got <= 0) break;

        for (int i = 0; i < got; i++)
            mono[at + i] = (short)((stereo[i * 2] + stereo[i * 2 + 1]) / 2);

        at += (unsigned int)got;
    }

    mp3::close(&s);

    if (!at) { memfree(mono); return false; }

    *out_pcm = mono;
    *out_frames = at;
    return true;
}
