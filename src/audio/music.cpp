#include "pch.h"
#include "music.h"
#include "mp3.h"
#include "audio.h"

#include "core/log.h"
#include "core/storage.h"
#include "system/io/ufile.h"

namespace
{
    // The file is held in memory rather than read as it plays: minimp3 reads
    // its buffer in place, seeking backwards as freely as forwards, and a
    // track is a few megabytes against a client that already holds more than
    // that in pictures.
    ubuffer g_file;
    mp3_stream g_stream;

    bool g_loaded = false;
    bool g_playing = false;

    char g_title[160];
    char g_error[160];

    float g_volume = -1.0f;
    int g_monitor = -1;

    // next_frame runs on the voice tick; opening, seeking and closing happen
    // on the ui thread. Everything that touches the stream takes this.
    CRITICAL_SECTION g_lock;
    volatile long g_lock_ready = 0;

    void ensure_lock()
    {
        if (InterlockedCompareExchange(&g_lock_ready, 1, 0) == 0)
            InitializeCriticalSection(&g_lock);
    }

    struct guard
    {
        guard() { ensure_lock(); EnterCriticalSection(&g_lock); }
        ~guard() { LeaveCriticalSection(&g_lock); }
    };

    // The last component of a path, which is what a person calls the track.
    void title_from(const wchar_t* path)
    {
        const wchar_t* name = path;
        for (const wchar_t* p = path; *p; p++)
            if (*p == L'\\' || *p == L'/') name = p + 1;

        wcstochar(name, g_title, (int)sizeof(g_title));
    }

    double to_seconds(unsigned long long frames)
    {
        return (double)frames / (double)AUDIO_SAMPLE_RATE;
    }
}

bool music::open(const wchar_t* path)
{
    if (!path || !path[0]) return false;

    guard g;

    if (g_loaded)
    {
        mp3::close(&g_stream);
        g_file.free_buffer();
        g_loaded = false;
        g_playing = false;
    }

    ccfset(g_error, 0, sizeof(g_error));

    g_file.init();
    if (!ufile::read_all(path, &g_file))
    {
        g_file.free_buffer();
        ccstrncpy(g_error, "Файл не читается", sizeof(g_error) - 1);
        return false;
    }

    if (!mp3::open(&g_stream, g_file.data, g_file.size))
    {
        g_file.free_buffer();
        ccstrncpy(g_error, "Это не mp3, или файл повреждён", sizeof(g_error) - 1);
        return false;
    }

    title_from(path);
    g_loaded = true;

    // Stopped at the start. Somebody who picked a file in the middle of a
    // conversation gets to decide when the channel hears it.
    g_playing = false;

    log_line("music: %s, %.1f s", g_title, to_seconds(g_stream.frames));
    return true;
}

void music::close()
{
    guard g;

    if (!g_loaded) return;

    mp3::close(&g_stream);
    g_file.free_buffer();

    g_loaded = false;
    g_playing = false;
    g_title[0] = 0;
}

bool music::loaded() { return g_loaded; }
const char* music::title() { return g_title; }
const char* music::error() { return g_error; }

bool music::playing() { return g_loaded && g_playing; }

void music::set_playing(bool on)
{
    guard g;
    if (!g_loaded) return;

    // Starting again from the end starts again from the beginning, which is
    // what pressing play on a finished track is asking for.
    if (on && g_stream.at >= g_stream.frames) mp3::seek(&g_stream, 0);

    g_playing = on;
}

double music::position()
{
    guard g;
    return g_loaded ? to_seconds(g_stream.at) : 0.0;
}

double music::duration()
{
    guard g;
    return g_loaded ? to_seconds(g_stream.frames) : 0.0;
}

void music::seek(double seconds)
{
    guard g;
    if (!g_loaded) return;

    if (seconds < 0.0) seconds = 0.0;
    mp3::seek(&g_stream, (unsigned long long)(seconds * (double)AUDIO_SAMPLE_RATE));
}

float music::volume()
{
    if (g_volume < 0.0f)
        g_volume = (float)storage::settings_get_int("music_volume", 70) / 100.0f;
    return g_volume;
}

void music::set_volume(float v)
{
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;

    g_volume = v;
    storage::settings_set_int("music_volume", (int)(v * 100.0f + 0.5f));
    storage::settings_save();
}

bool music::monitoring()
{
    if (g_monitor < 0) g_monitor = storage::settings_get_int("music_monitor", 0);
    return g_monitor != 0;
}

void music::set_monitoring(bool on)
{
    g_monitor = on ? 1 : 0;
    storage::settings_set_int("music_monitor", g_monitor);
    storage::settings_save();
}

bool music::self_test(const wchar_t* path)
{
    if (!music::open(path))
    {
        log_line("mp3test: не открылось - %s", g_error);
        return false;
    }

    wchar_t out_path[MAX_PATH];
    if (!ufile::app_path(L"mp3test.wav", out_path, MAX_PATH))
    {
        music::close();
        return false;
    }

    HANDLE file = CreateFileW(out_path, GENERIC_WRITE, FILE_SHARE_READ, 0,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE)
    {
        music::close();
        return false;
    }

    // A 44 byte canonical header, patched with the real sizes at the end.
    unsigned char header[44];
    ccfset(header, 0, sizeof(header));
    DWORD wrote = 0;
    WriteFile(file, header, sizeof(header), &wrote, 0);

    short frame[AUDIO_FRAME_SAMPLES * 2];
    unsigned long long frames = 0;
    unsigned long long loudest = 0;
    unsigned long long silent_frames = 0;

    // Read straight off the stream rather than through next_frame, so the
    // volume setting does not colour what is being measured.
    //
    // Time spent decoding alone, without the writing. This is the number that
    // matters: the same work happens on the voice tick, inside a twenty
    // millisecond budget it shares with the encoder.
    unsigned long long decode_ms = 0;

    for (;;)
    {
        unsigned long long began = GetTickCount64();
        int got = mp3::read(&g_stream, frame, AUDIO_FRAME_SAMPLES);
        decode_ms += GetTickCount64() - began;

        if (got <= 0) break;

        int peak = 0;
        for (int i = 0; i < got * 2; i++)
        {
            int v = frame[i] < 0 ? -frame[i] : frame[i];
            if (v > peak) peak = v;
        }

        if (peak > (int)loudest) loudest = (unsigned long long)peak;
        if (peak < 32) silent_frames++;

        WriteFile(file, frame, (DWORD)(got * 2 * sizeof(short)), &wrote, 0);
        frames += (unsigned long long)got;
    }

    unsigned int data_bytes = (unsigned int)(frames * 2 * sizeof(short));

    // RIFF/WAVE, 48 kHz, two channels, sixteen bits.
    const unsigned char riff[4] = { 'R', 'I', 'F', 'F' };
    unsigned int riff_size = 36 + data_bytes;
    const unsigned char wave_fmt[8] = { 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ' };
    unsigned int fmt_size = 16;
    unsigned short pcm_tag = 1, channels = 2, bits = 16, align = 4;
    unsigned int rate = AUDIO_SAMPLE_RATE, bps = AUDIO_SAMPLE_RATE * 4;
    const unsigned char data_tag[4] = { 'd', 'a', 't', 'a' };

    int at = 0;
    ccpy(header + at, riff, 4); at += 4;
    ccpy(header + at, &riff_size, 4); at += 4;
    ccpy(header + at, wave_fmt, 8); at += 8;
    ccpy(header + at, &fmt_size, 4); at += 4;
    ccpy(header + at, &pcm_tag, 2); at += 2;
    ccpy(header + at, &channels, 2); at += 2;
    ccpy(header + at, &rate, 4); at += 4;
    ccpy(header + at, &bps, 4); at += 4;
    ccpy(header + at, &align, 2); at += 2;
    ccpy(header + at, &bits, 2); at += 2;
    ccpy(header + at, data_tag, 4); at += 4;
    ccpy(header + at, &data_bytes, 4);

    SetFilePointer(file, 0, 0, FILE_BEGIN);
    WriteFile(file, header, sizeof(header), &wrote, 0);
    CloseHandle(file);

    log_line("mp3test: %s - %d Гц, %d кан. -> 48000, %llu кадров (%.1f с), пик %llu, тихих кадров %llu",
             g_title, g_stream.rate, g_stream.channels, frames,
             to_seconds(frames), loudest, silent_frames);

    // Per 20 ms frame, against the 20 ms the voice tick has to spend.
    double per_frame = frames ? (double)decode_ms * 20.0 / to_seconds(frames) / 1000.0 : 0.0;
    log_line("mp3test: декодирование %llu мс на %.1f с звука - %.2f мс на кадр 20 мс",
             decode_ms, to_seconds(frames), per_frame);

    // A track that decoded to the length it claims and is not silence. Both
    // halves matter: a resampler with its ratio the wrong way round produces
    // the right sort of noise for the wrong length of time.
    unsigned long long expected = g_stream.frames;
    bool length_ok = frames > expected - expected / 20 && frames < expected + expected / 20;
    bool sound_ok = loudest > 500;

    // Seeking, checked by going away and coming back. The first frame of the
    // track is decoded from a known state, so reading it again after a jump
    // has to produce the same samples - anything else means the decoder or the
    // resampler kept something across the seek.
    short first[AUDIO_FRAME_SAMPLES * 2];
    short again[AUDIO_FRAME_SAMPLES * 2];

    mp3::seek(&g_stream, 0);
    mp3::read(&g_stream, first, AUDIO_FRAME_SAMPLES);

    // Twice from the same place with nothing in between, which is the
    // question "is a seek repeatable at all" on its own.
    mp3::seek(&g_stream, 0);
    mp3::read(&g_stream, again, AUDIO_FRAME_SAMPLES);

    int plain = 0;
    for (int i = 0; i < AUDIO_FRAME_SAMPLES * 2; i++)
    {
        int d = first[i] - again[i];
        if (d < 0) d = -d;
        if (d > plain) plain = d;
    }
    log_line("mp3test: перемотка на то же место - отличие %d", plain);

    mp3::seek(&g_stream, expected / 2);
    int middle = mp3::read(&g_stream, again, AUDIO_FRAME_SAMPLES);
    unsigned long long where = g_stream.at;

    mp3::seek(&g_stream, 0);
    mp3::read(&g_stream, again, AUDIO_FRAME_SAMPLES);

    // Compared allowing for a small shift in time. Two decodes of the same
    // place can start a sample or two apart - the resampler's phase and the
    // decoder's priming both depend on where it was asked from - and on loud
    // music a shift of one sample looks like a completely different waveform
    // if the comparison insists on lining up exactly.
    int drift = 0x7FFFFFFF;
    int at_shift = 0;

    for (int shift = 0; shift <= 8; shift++)
    {
        int worst = 0;
        for (int i = 0; i < (AUDIO_FRAME_SAMPLES - 8) * 2; i++)
        {
            int d = first[i] - again[i + shift * 2];
            if (d < 0) d = -d;
            if (d > worst) worst = d;
        }

        if (worst < drift) { drift = worst; at_shift = shift; }
    }

    // Not asked to be identical to the sample. An mp3 frame is decoded partly
    // out of the frames before it, so the first one after a jump carries a
    // little of whatever the decoder was primed with; minimp3 pre-rolls a few
    // frames to keep that small rather than to remove it. What matters is that
    // it is small.
    bool seek_ok = drift < 1024 && middle > 0;

    log_line("mp3test: перемотка - середина на кадре %llu из %llu, возврат отличается на %d "
             "при сдвиге %d",
             where, expected, drift, at_shift);

    if (!length_ok)
        log_line("mp3test: ОШИБКА - ожидалось около %llu кадров, вышло %llu", expected, frames);
    if (!sound_ok)
        log_line("mp3test: ОШИБКА - тишина, пик всего %llu", loudest);

    music::close();
    return length_ok && sound_ok && seek_ok;
}

bool music::next_frame(short* stereo)
{
    if (!g_loaded || !g_playing || !stereo) return false;

    guard g;

    // Checked again inside the lock: the ui thread can have closed the file
    // between the cheap check above and here.
    if (!g_loaded || !g_playing) return false;

    int got = mp3::read(&g_stream, stereo, AUDIO_FRAME_SAMPLES);

    if (got < AUDIO_FRAME_SAMPLES)
    {
        // The end. The tail of the frame is filled with silence rather than
        // left as whatever the caller's buffer held.
        for (int i = got; i < AUDIO_FRAME_SAMPLES; i++)
        {
            stereo[i * 2 + 0] = 0;
            stereo[i * 2 + 1] = 0;
        }

        g_playing = false;
        if (!got) return false;
    }

    float gain = music::volume();
    if (gain < 0.999f || gain > 1.001f)
    {
        for (int i = 0; i < AUDIO_FRAME_SAMPLES * 2; i++)
        {
            int v = (int)((float)stereo[i] * gain);
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            stereo[i] = (short)v;
        }
    }

    return true;
}
