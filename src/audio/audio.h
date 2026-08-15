#pragma once

// WASAPI capture and render, both locked to 48 kHz stereo 16-bit because that
// is what discord's opus stream expects. The audio engine does the conversion
// for us via AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM, so no resampler is needed.
//
// The render side is pull-based, the way abaddon's AudioManager works on top
// of miniaudio: the sound card's clock is the only clock, every device buffer
// is filled on demand by whoever has audio to give, and a source with nothing
// queued simply contributes silence. Nothing is primed, padded ahead of time
// or topped up by a timer, so there is no second clock for the device to
// drift against - the drift that used to reopen as clicks every few seconds.

const int AUDIO_SAMPLE_RATE = 48000;
const int AUDIO_CHANNELS = 2;
const int AUDIO_FRAME_SAMPLES = 960;   // 20 ms per channel

// Fills `samples` interleaved int16 samples of voice-call audio into `out`,
// which starts zeroed. Called on the render thread, at the sound card's pace,
// for every buffer the device asks for. Sources with nothing queued leave
// their share silent.
typedef void (*voice_mix_fn)(short* out, int samples);

struct audio_device
{
    wchar_t id[256];      // endpoint id, empty for "system default"
    char name[128];       // friendly name, utf-8
};

namespace audio
{
    bool init();
    void shutdown();

    // ---- device selection ----
    // Enumerates active endpoints. Returns how many were written.
    int list_devices(bool capture, audio_device* out, int cap);

    // An empty or null id means "follow the system default".
    void set_device(bool capture, const wchar_t* id);
    const wchar_t* device(bool capture);
    // Friendly name of the device currently selected, for the settings view.
    const char* device_name(bool capture);

    bool start_capture();
    void stop_capture();
    bool capture_active();
    // Fills 960*2 interleaved samples. False when the ring has not filled yet.
    bool read_capture_frame(short* out);

    // Drops queued capture down to keep_samples (in whole frames). The voice
    // tick calls this before every frame it reads: the ring is fed by the
    // device's clock and drained by the wall clock, and the drift between
    // them must not pile up as send latency.
    void trim_capture(int keep_samples);

    bool start_render();
    void stop_render();
    bool render_active();

    // Registers the voice-call mixer the render thread pulls from. One owner,
    // set once at voice::init; null detaches it.
    void set_voice_mixer(voice_mix_fn fn);

    // How much audio the device itself is holding right now. With pull mixing
    // there is no queued pipeline to measure - the device buffer is the whole
    // of it, and it is also the whole playback latency.
    unsigned int render_backlog_ms();

    // ---- media playback ----
    //
    // A second input to the speakers, for a video playing in a chat. It gets
    // its own ring rather than sharing the call's: both write whenever they
    // have something, and a single ring would lay one after the other in time
    // instead of on top of each other. The render thread sums them.
    //
    // Samples are 48 kHz stereo, interleaved, like everything else here.
    void write_media(const short* pcm, int samples);
    unsigned int media_backlog_ms();
    void clear_media();

    // ---- somebody else's screen share ----
    //
    // A third input for the same reason the second one exists: the call, a
    // video in a chat and a stream being watched can all be making noise at
    // once, and a shared ring would lay them one after another in time
    // instead of on top of each other.
    void write_stream(const short* pcm, int samples);
    unsigned int stream_backlog_ms();
    void clear_stream();

    // Shortfalls and drops on the media and stream rings - the two push-style
    // inputs that remain. The call itself can no longer appear here: it is
    // pulled by the device, so it has nothing to overrun or underrun against.
    unsigned int render_overruns();
    unsigned int render_underruns();
    void reset_render_overruns();

    void set_input_gain(float gain);
    void set_output_gain(float gain);
    float input_gain();
    float output_gain();

    // 0..1 envelope of the last captured frame, for a level meter.
    float input_level();
    float output_level();

    const char* last_error();
}
