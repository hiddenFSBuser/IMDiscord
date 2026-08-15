#pragma once

// Recording taps for the voice path.
//
// Chasing the "spikes" through counters has cost many rounds and produced
// several confident wrong answers, mine included. A counter says how often
// something crossed a threshold somebody chose in advance; it cannot say what
// the waveform actually looked like. These taps write the samples themselves,
// so the question stops being "which detector fired" and becomes "open the
// file and look".
//
// Two taps, deliberately: one on what the decoder produced for each speaker,
// one on the exact bytes handed to the sound card. If a defect is in the first
// it came out of the codec; if it is only in the second it was made here,
// between the two. No third answer is possible, which is the point.
//
// Off unless IMD_AUDIODUMP is set in the environment. Files land next to the
// log and are capped, so leaving it on cannot fill a disk.

namespace wavdump
{
    struct sink
    {
        void* file;               // HANDLE, or null
        unsigned int written;     // sample bytes so far
        bool opened;              // an attempt has been made
        wchar_t name[64];
    };

    // Whether IMD_AUDIODUMP was set. Read once.
    bool enabled();

    // Names the file; nothing is created until the first sample arrives, so a
    // speaker who never talks leaves no file behind.
    void start(sink* s, const wchar_t* name);

    void write(sink* s, const short* pcm, int samples);

    // Patches the sizes into the header and closes. Safe on an unopened sink.
    void finish(sink* s);
}
