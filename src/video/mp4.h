#pragma once
#include "ubuffer.h"

// Taking an mp4 file apart.
//
// A wrapper over minimp4 that answers only the questions a player asks: which
// track is the picture, which is the sound, what does the decoder need to be
// told before the first frame, and what is the next chunk of each.
//
// Two conversions happen here rather than in the player. An mp4 stores H.264
// with each unit prefixed by its length, while every decoder on windows wants
// the start codes of an Annex-B stream; and the parameter sets live in the
// track header rather than in the samples, so they have to be pushed in front
// of the first picture or nothing decodes at all.

struct mp4_sample
{
    const unsigned char* data;
    unsigned int size;

    // Both in microseconds from the start of the file.
    unsigned long long time_us;
    unsigned long long duration_us;
};

struct mp4_file
{
    // Opaque; the demuxer state lives behind it so minimp4.h stays out of
    // every header that wants to play something.
    void* handle;

    const unsigned char* bytes;   // the whole file, borrowed, not owned
    unsigned int length;

    int video_track;              // -1 when there is none
    int audio_track;

    int width;
    int height;
    unsigned long long duration_us;

    // The picture parameters, already turned into Annex-B and ready to be the
    // first thing the decoder is handed.
    ubuffer video_header;

    // AudioSpecificConfig, as the AAC decoder wants it.
    const unsigned char* audio_config;
    unsigned int audio_config_size;
    int audio_rate;
    int audio_channels;

    unsigned int video_samples;
    unsigned int audio_samples;

    bool has_video() const { return video_track >= 0; }
    bool has_audio() const { return audio_track >= 0; }
};

namespace mp4
{
    // Reads the index out of a file already in memory. `bytes` has to stay
    // alive and unmoved until close, because samples point into it.
    bool open(mp4_file* f, const unsigned char* bytes, unsigned int length);
    void close(mp4_file* f);

    // One sample of a track by index. `out` points into the file for audio,
    // and into scratch owned by the file for video, where the length prefixes
    // have to be rewritten as start codes first.
    bool video_sample(mp4_file* f, unsigned int index, mp4_sample* out);

    // When a picture is due, without unpacking it. The player asks this of
    // the next frame on every pass of its loop and unpacks only when the
    // moment arrives; doing the conversion first meant rebuilding the same
    // frame a couple of hundred times before it was ever needed.
    bool video_time(mp4_file* f, unsigned int index, unsigned long long* out_us);

    // When a picture is due, without unpacking it. The player asks this of
    // the next frame on every pass of its loop and unpacks only when the
    // moment arrives; doing the conversion first meant rebuilding the same
    // frame a couple of hundred times before it was ever needed.
    bool video_time(mp4_file* f, unsigned int index, unsigned long long* out_us);
    bool audio_sample(mp4_file* f, unsigned int index, mp4_sample* out);

    // The sample to start at when jumping to a moment. Video only lands on a
    // keyframe, so this returns the last one at or before the time asked for.
    unsigned int seek_video(mp4_file* f, unsigned long long time_us);
    unsigned int seek_audio(mp4_file* f, unsigned long long time_us);

    const char* last_error();
}
