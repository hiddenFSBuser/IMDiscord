#pragma once

// Deciding when to actually transmit.
//
// An open microphone sends a packet every 20 ms whether anybody spoke or not,
// which lights the green ring permanently and puts every keyboard press and
// every fan into the call. The gate in noise.cpp only turns the samples down;
// it does not stop them being encoded and sent, and opus encoding silence
// still produces a packet. So the decision has to live here, before the
// encoder, and it has to be taken on the denoised signal - otherwise the
// threshold is fighting whatever the suppressor was about to remove anyway.
//
// Two modes. Manual is a plain threshold the person drags until the meter
// stops tripping on the room. Automatic watches how quiet the room gets when
// nobody talks and puts the threshold a fixed distance above that, so it
// follows a fan spinning up or a window being opened without being touched.
//
// Either way the decision is sticky: it opens on the first frame over the
// line and stays open for a moment after the level drops, because speech is
// full of gaps shorter than the pauses between sentences and a gate that
// tracks them exactly chops the ends off words.

namespace vad
{
    void init();

    // Off means transmit continuously, which is what a push-to-talk setup or
    // a studio microphone wants.
    void set_enabled(bool on);
    bool enabled();

    void set_automatic(bool on);
    bool automatic();

    // Manual threshold, 0..1 against full scale.
    void set_threshold(float v);
    float threshold();

    // Judges one frame of mono 48 kHz audio and returns whether it should go
    // out. Call once per frame and no more: the hold timer advances here.
    bool speaking(const short* mono, int samples);

    // For the meter: the loudness of the last frame judged, the line it was
    // judged against, and whether the gate is currently open. All 0..1.
    float level();
    float active_threshold();
    bool open();

    // Forgets the noise floor. Called when a call starts, so a previous
    // room's silence is not held against this one.
    void reset();
}
