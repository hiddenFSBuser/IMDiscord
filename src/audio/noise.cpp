#include "pch.h"
#include "noise.h"
#include "audio.h"
#include "core/log.h"

// config.h supplies EXPORT for the speexdsp declarations; without it the
// header does not parse.
#include "speexdsp/config.h"
#include "speexdsp/speex_preprocess.h"
#include "rnnoise/rnnoise.h"

namespace
{
    const int RNNOISE_FRAME = 480;   // rnnoise is fixed at 10 ms of 48 kHz

    int g_mode = NOISE_OFF;
    float g_gate_threshold = 0.02f;

    SpeexPreprocessState* g_speex = 0;
    int g_speex_frame = 0;

    DenoiseState* g_rnnoise = 0;

    // Envelope follower for the gate, so it opens quickly and closes slowly
    // instead of chopping the tail off every word.
    float g_gate_envelope = 0.0f;
    float g_gate_gain = 0.0f;

    void close_speex()
    {
        if (g_speex)
        {
            speex_preprocess_state_destroy(g_speex);
            g_speex = 0;
        }
        g_speex_frame = 0;
    }

    void close_rnnoise()
    {
        if (g_rnnoise)
        {
            rnnoise_destroy(g_rnnoise);
            g_rnnoise = 0;
        }
    }

    void open_speex()
    {
        close_speex();

        g_speex_frame = AUDIO_FRAME_SAMPLES;   // 20 ms at 48 kHz
        g_speex = speex_preprocess_state_init(g_speex_frame, AUDIO_SAMPLE_RATE);
        if (!g_speex)
        {
            log_line("noise: speexdsp preprocessor failed to start");
            return;
        }

        int on = 1;
        speex_preprocess_ctl(g_speex, SPEEX_PREPROCESS_SET_DENOISE, &on);

        int suppress = -15;
        speex_preprocess_ctl(g_speex, SPEEX_PREPROCESS_SET_NOISE_SUPPRESS, &suppress);

        speex_preprocess_ctl(g_speex, SPEEX_PREPROCESS_SET_AGC, &on);

        int agc_level = 8000;
        speex_preprocess_ctl(g_speex, SPEEX_PREPROCESS_SET_AGC_LEVEL, &agc_level);

        int max_gain = 20;
        speex_preprocess_ctl(g_speex, SPEEX_PREPROCESS_SET_AGC_MAX_GAIN, &max_gain);

        int off = 0;
        speex_preprocess_ctl(g_speex, SPEEX_PREPROCESS_SET_VAD, &off);
        speex_preprocess_ctl(g_speex, SPEEX_PREPROCESS_SET_DEREVERB, &off);
    }

    void open_rnnoise()
    {
        close_rnnoise();
        g_rnnoise = rnnoise_create(0);
        if (!g_rnnoise) log_line("noise: rnnoise failed to start");
    }

    void run_gate(short* mono, int samples)
    {
        const float attack = 0.35f;
        const float release = 0.02f;

        for (int i = 0; i < samples; i++)
        {
            float level = mono[i] < 0 ? -(float)mono[i] : (float)mono[i];
            level /= 32768.0f;

            // Fast attack, slow release on the envelope.
            if (level > g_gate_envelope) g_gate_envelope += (level - g_gate_envelope) * attack;
            else g_gate_envelope += (level - g_gate_envelope) * release;

            float target = g_gate_envelope > g_gate_threshold ? 1.0f : 0.0f;
            g_gate_gain += (target - g_gate_gain) * (target > g_gate_gain ? attack : release);

            mono[i] = (short)((float)mono[i] * g_gate_gain);
        }
    }

    void run_rnnoise(short* mono, int samples)
    {
        if (!g_rnnoise) return;

        float scratch[RNNOISE_FRAME];
        int done = 0;

        while (done + RNNOISE_FRAME <= samples)
        {
            for (int i = 0; i < RNNOISE_FRAME; i++) scratch[i] = (float)mono[done + i];

            rnnoise_process_frame(g_rnnoise, scratch, scratch);

            for (int i = 0; i < RNNOISE_FRAME; i++)
            {
                float v = scratch[i];
                if (v > 32767.0f) v = 32767.0f;
                else if (v < -32768.0f) v = -32768.0f;
                mono[done + i] = (short)v;
            }

            done += RNNOISE_FRAME;
        }
    }

    void run_speex(short* mono, int samples)
    {
        if (!g_speex || g_speex_frame <= 0) return;

        int done = 0;
        while (done + g_speex_frame <= samples)
        {
            speex_preprocess_run(g_speex, mono + done);
            done += g_speex_frame;
        }
    }
}

void noise::set_mode(int mode)
{
    if (mode == g_mode) return;

    g_mode = mode;
    close_speex();
    close_rnnoise();
    g_gate_envelope = 0.0f;
    g_gate_gain = 0.0f;

    if (mode == NOISE_SPEEX) open_speex();
    else if (mode == NOISE_RNNOISE) open_rnnoise();

    log_line("noise: mode set to %s", mode_name(mode));
}

int noise::mode() { return g_mode; }

const char* noise::mode_name(int mode)
{
    switch (mode)
    {
    case NOISE_GATE:    return "noise gate";
    case NOISE_SPEEX:   return "speexdsp";
    case NOISE_RNNOISE: return "rnnoise";
    default:            return "off";
    }
}

void noise::set_gate_threshold(float threshold) { g_gate_threshold = threshold; }
float noise::gate_threshold() { return g_gate_threshold; }

void noise::process(short* mono, int samples)
{
    switch (g_mode)
    {
    case NOISE_GATE:    run_gate(mono, samples); break;
    case NOISE_SPEEX:   run_speex(mono, samples); break;
    case NOISE_RNNOISE: run_rnnoise(mono, samples); break;
    default: break;
    }
}

bool noise::self_test()
{
    log_line("noise: rnnoise reports frame size %d, state size %d",
             rnnoise_get_frame_size(), rnnoise_get_size());

    short frame[AUDIO_FRAME_SAMPLES];

    for (int m = NOISE_OFF; m <= NOISE_RNNOISE; m++)
    {
        log_line("noise: testing %s", mode_name(m));
        set_mode(m);

        for (int i = 0; i < AUDIO_FRAME_SAMPLES; i++)
            frame[i] = (short)(((i * 37) % 2000) - 1000);

        process(frame, AUDIO_FRAME_SAMPLES);
        log_line("noise:   %s ok", mode_name(m));
    }

    set_mode(NOISE_OFF);
    log_line("noise: all modes passed");
    return true;
}

void noise::shutdown()
{
    close_speex();
    close_rnnoise();
    g_mode = NOISE_OFF;
}
