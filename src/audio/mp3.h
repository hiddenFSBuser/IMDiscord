#pragma once

// Reading mp3, for the two places that want it: the five interface sounds,
// which are mp3 files carried in the binary, and whatever track the music
// player is pointed at.
//
// Everything comes out at 48 kHz because that is the only rate opus encodes
// at and the only one the mixer speaks. Files are almost never 48 kHz - 44.1
// is what the world stores music at - so a resampler sits behind this, and it
// is speexdsp's polyphase one rather than an interpolation: the difference is
// audible on music, which is the whole point of the feature.

struct mp3_stream
{
    // Both kept behind void* so minimp3 and speexdsp stay out of every header
    // that wants to play something.
    void* decoder;
    void* resampler;

    const unsigned char* bytes;   // borrowed, must outlive the stream
    unsigned int length;

    int rate;                     // as the file stores it
    int channels;

    // Both counted in frames at 48 kHz - one frame being one moment, whatever
    // the channel count - so the player never has to think in the file's rate.
    unsigned long long frames;
    unsigned long long at;

    // Decoded but not yet resampled, as stereo at the file's own rate.
    short* pending;
    int pending_frames;
    int pending_used;

    bool ended;
};

namespace mp3
{
    // The buffer is borrowed and has to stay put for as long as the stream is
    // open: minimp3 reads it in place rather than copying it.
    bool open(mp3_stream* s, const unsigned char* bytes, unsigned int length);
    void close(mp3_stream* s);

    // 48 kHz stereo interleaved. Returns frames written, which is short of
    // what was asked for only at the end of the file.
    int read(mp3_stream* s, short* out, int frames);

    void seek(mp3_stream* s, unsigned long long frame);

    // The whole file at once as 48 kHz mono, for sounds short enough that
    // streaming them would be more machinery than they are worth. The caller
    // owns what comes back and releases it with memfree.
    bool decode_all_mono(const unsigned char* bytes, unsigned int length,
                         short** out_pcm, unsigned int* out_frames);
}
