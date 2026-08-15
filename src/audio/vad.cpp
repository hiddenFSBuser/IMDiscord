#include "pch.h"
#include "vad.h"
#include "audio.h"
#include "core/log.h"

namespace
{
    bool g_enabled = true;
    bool g_automatic = true;
    float g_manual = 0.02f;

    // How long the gate stays open after the level drops back under the line.
    // Short enough not to hold the room for a second after a word, long
    // enough to ride over the stop in the middle of "stop it".
    const int HOLD_FRAMES = 15;          // 300 ms at 20 ms a frame

    // Coming down through the line takes a lower level than going up through
    // it, so a voice sitting right on the threshold does not chatter.
    const float CLOSE_RATIO = 0.65f;

    // Automatic mode puts the line this far above the measured floor. Under
    // that, no floor estimate is trusted enough to gate on.
    const float FLOOR_MARGIN = 3.5f;
    const float FLOOR_MIN = 0.004f;

    float g_level = 0.0f;
    float g_smoothed = 0.0f;
    float g_floor = 0.02f;
    float g_line = 0.02f;
    bool g_open = false;
    int g_hold = 0;

    float frame_rms(const short* mono, int samples)
    {
        if (samples <= 0) return 0.0f;

        // Sum of squares in double: a full-scale 20 ms frame overflows a
        // 32-bit accumulator less than halfway through.
        double sum = 0.0;
        for (int i = 0; i < samples; i++)
        {
            double v = (double)mono[i] / 32768.0;
            sum += v * v;
        }

        double mean = sum / (double)samples;
        if (mean <= 0.0) return 0.0f;
        return csqrtf((float)mean);
    }
}

void vad::init()
{
    reset();
}

void vad::set_enabled(bool on)
{
    if (g_enabled == on) return;
    g_enabled = on;
    if (!on) { g_open = true; g_hold = 0; }
}

bool vad::enabled() { return g_enabled; }

void vad::set_automatic(bool on) { g_automatic = on; }
bool vad::automatic() { return g_automatic; }

void vad::set_threshold(float v)
{
    if (v < 0.0f) v = 0.0f;
    if (v > 0.5f) v = 0.5f;
    g_manual = v;
}

float vad::threshold() { return g_manual; }

float vad::level() { return g_level; }
float vad::active_threshold() { return g_line; }
bool vad::open() { return g_open; }

void vad::reset()
{
    g_level = 0.0f;
    g_smoothed = 0.0f;
    g_floor = 0.02f;
    g_line = g_automatic ? 0.02f : g_manual;
    g_open = !g_enabled;
    g_hold = 0;
}

bool vad::speaking(const short* mono, int samples)
{
    float rms = frame_rms(mono, samples);
    g_level = rms;

    // A short envelope so a single quiet frame inside a word does not read as
    // a pause, and a single click does not read as speech.
    if (rms > g_smoothed) g_smoothed += (rms - g_smoothed) * 0.6f;
    else                  g_smoothed += (rms - g_smoothed) * 0.2f;

    if (g_automatic)
    {
        // The floor drops to meet quiet quickly and climbs back only slowly.
        // Chasing it upward fast would let a long sentence raise the floor
        // above the speaker's own voice and gate them out mid-word.
        if (rms < g_floor) g_floor += (rms - g_floor) * 0.15f;
        else               g_floor += (rms - g_floor) * 0.0008f;

        g_line = g_floor * FLOOR_MARGIN;
        if (g_line < FLOOR_MIN) g_line = FLOOR_MIN;
    }
    else
    {
        g_line = g_manual;
    }

    if (!g_enabled) { g_open = true; return true; }

    if (g_smoothed >= g_line)
    {
        g_open = true;
        g_hold = HOLD_FRAMES;
        return true;
    }

    if (g_open && g_smoothed >= g_line * CLOSE_RATIO)
    {
        g_hold = HOLD_FRAMES;
        return true;
    }

    if (g_hold > 0)
    {
        g_hold--;
        return true;
    }

    g_open = false;
    return false;
}
