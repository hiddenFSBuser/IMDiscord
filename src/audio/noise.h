#pragma once

// Microphone noise suppression. All modes work on 48 kHz mono frames, which is
// what the outgoing path produces after downmixing the captured stereo.

enum noise_mode
{
    NOISE_OFF = 0,
    NOISE_GATE = 1,      // plain threshold gate, written here
    NOISE_SPEEX = 2,     // speexdsp preprocessor: denoise + AGC
    NOISE_RNNOISE = 3,   // rnnoise recurrent denoiser
};

namespace noise
{
    void set_mode(int mode);
    int mode();
    const char* mode_name(int mode);

    // Threshold is the gate's cut-off, 0..1 of full scale.
    void set_gate_threshold(float threshold);
    float gate_threshold();

    // Processes in place. samples is the count of mono samples.
    void process(short* mono, int samples);

    void shutdown();

    // Runs one frame through every mode. Used by --selftest so a broken
    // suppressor shows up without needing a voice session.
    bool self_test();
}
