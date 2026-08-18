#include "pch.h"
#include "sounds.h"
#include "audio.h"
#include "mp3.h"

#include "core/log.h"
#include "core/storage.h"

namespace
{
    struct sound_entry
    {
        const unsigned char* mp3;
        const unsigned int* size;
        const char* key;        // where its switch is remembered
    };

    const sound_entry SOUNDS[SOUND_COUNT] = {
        { MP3_VOICE_JOIN,   &MP3_VOICE_JOIN_SIZE,   "snd_voice_join" },
        { MP3_VOICE_LEAVE,  &MP3_VOICE_LEAVE_SIZE,  "snd_voice_leave" },
        { MP3_STREAM_START, &MP3_STREAM_START_SIZE, "snd_stream_start" },
        { MP3_STREAM_STOP,  &MP3_STREAM_STOP_SIZE,  "snd_stream_stop" },
        { MP3_NOTIFY,       &MP3_NOTIFY_SIZE,       "snd_notify" },
    };

    // Decoded the first time each is asked for, and kept. A sound is about a
    // second of mono, so the whole set is a few hundred kilobytes even if all
    // five end up used - and the ones somebody switched off never cost
    // anything at all.
    struct decoded
    {
        short* pcm;
        unsigned int frames;
        bool tried;
    };

    decoded g_pcm[SOUND_COUNT];

    // play() is called from whichever thread noticed the event - the
    // gateway's, the voice tick's, the ui's - and two of them arrive together
    // often enough that the first decode has to be guarded.
    CRITICAL_SECTION g_lock;
    volatile long g_lock_ready = 0;

    const decoded* samples_of(ui_sound which)
    {
        if (InterlockedCompareExchange(&g_lock_ready, 1, 0) == 0)
            InitializeCriticalSection(&g_lock);

        EnterCriticalSection(&g_lock);

        decoded* d = &g_pcm[which];
        if (!d->tried)
        {
            d->tried = true;
            if (!mp3::decode_all_mono(SOUNDS[which].mp3, *SOUNDS[which].size,
                                      &d->pcm, &d->frames))
                log_line("sounds: %s did not decode", SOUNDS[which].key);
        }

        LeaveCriticalSection(&g_lock);
        return d;
    }

    float g_volume = -1.0f;

    // Two of the same sound at once happens - two people leaving together -
    // and mixing them is right, but a stream of them is not: a channel
    // emptying would otherwise be a wall of noise. One every eighth of a
    // second per kind is enough to hear each and not enough to pile up.
    unsigned long long g_last_ms[SOUND_COUNT];
    const unsigned long long GAP_MS = 120;
}

float sounds::volume()
{
    if (g_volume < 0.0f)
    {
        // Quieter than a voice by default: these are interruptions, and one at
        // conversational level is startling.
        g_volume = (float)storage::settings_get_int("snd_volume", 55) / 100.0f;
    }
    return g_volume;
}

void sounds::set_volume(float v)
{
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;

    g_volume = v;
    storage::settings_set_int("snd_volume", (int)(v * 100.0f + 0.5f));
    storage::settings_save();
}

bool sounds::enabled(ui_sound which)
{
    if (which < 0 || which >= SOUND_COUNT) return false;
    return storage::settings_get_int(SOUNDS[which].key, 1) != 0;
}

void sounds::set_enabled(ui_sound which, bool on)
{
    if (which < 0 || which >= SOUND_COUNT) return;

    storage::settings_set_int(SOUNDS[which].key, on ? 1 : 0);
    storage::settings_save();
}

void sounds::play(ui_sound which)
{
    if (which < 0 || which >= SOUND_COUNT) return;
    if (!sounds::enabled(which)) return;

    unsigned long long now = GetTickCount64();
    if (now - g_last_ms[which] < GAP_MS) return;
    g_last_ms[which] = now;

    const decoded* d = samples_of(which);
    if (!d->pcm || !d->frames) return;

    const short* src = d->pcm;
    unsigned int count = d->frames;

    float gain = sounds::volume();
    if (gain <= 0.0f) return;

    // The device is only running while a call is up. Started here so a message
    // arriving with no call in progress is still heard; stopping it again is
    // left to whoever stops the call, because a sound every few seconds would
    // otherwise open and close the device every few seconds.
    if (!audio::render_active() && !audio::start_render()) return;

    // Handed over in pieces rather than as one block: the ring is a second
    // long and a sound is over a second, so the whole of it does not fit in
    // one go on top of whatever is already queued.
    const unsigned int CHUNK = 4800;      // 100 ms
    short stereo[CHUNK * AUDIO_CHANNELS];

    for (unsigned int at = 0; at < count; at += CHUNK)
    {
        unsigned int n = count - at;
        if (n > CHUNK) n = CHUNK;

        for (unsigned int i = 0; i < n; i++)
        {
            int v = (int)((float)src[at + i] * gain);
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;

            // Mono into both channels.
            stereo[i * 2 + 0] = (short)v;
            stereo[i * 2 + 1] = (short)v;
        }

        audio::write_media(stereo, (int)(n * AUDIO_CHANNELS));
    }
}
