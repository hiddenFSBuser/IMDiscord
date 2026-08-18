#pragma once

// Playing a track into a voice call.
//
// The file is decoded on the voice tick, one twenty millisecond frame at a
// time, summed with whatever the microphone is sending and encoded with it.
// One clock runs the whole path - the call's - so there is nothing for the
// music to drift against, which is the failure a second timer would have
// bought: two clocks twenty milliseconds apart, quietly dropping or repeating
// a frame every few minutes.
//
// It follows from that clock that playback only runs while connected to a
// voice channel. There is nowhere else for the frames to go.

namespace music
{
    // Reads the file into memory and stops at its first frame. Wide path
    // because that is what the file dialog hands back.
    bool open(const wchar_t* path);
    void close();

    bool loaded();
    const char* title();          // the file's name, utf-8
    const char* error();          // why the last open failed, or empty

    bool playing();
    void set_playing(bool on);

    // Seconds, both of them.
    double position();
    double duration();
    void seek(double seconds);

    // What is sent. Separate from the call's output volume, which only
    // decides how loud everything is on this machine.
    float volume();
    void set_volume(float v);

    // Whether we hear it too. Off by default: the point of it is what the
    // channel hears, and hearing your own track over a call you are also
    // talking in is a distraction more often than it is a check.
    bool monitoring();
    void set_monitoring(bool on);

    // One 20 ms frame of 48 kHz stereo, at the chosen volume. False when
    // nothing is playing, in which case out is untouched. Called from the
    // voice tick and from nowhere else.
    bool next_frame(short* stereo);

    // "--mp3test <path>": decodes the whole track exactly as playback would
    // and writes it beside the log as a wav. Whether the decoder and the
    // resampler are doing their job is a question about a waveform, and the
    // only honest way to answer it is to produce one and listen.
    bool self_test(const wchar_t* path);
}
