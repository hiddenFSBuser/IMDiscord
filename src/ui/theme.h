#pragma once
#include "imgui.h"

// The palette, and the handful of shape settings that go with it.
//
// These started as constants lifted from discord's dark theme so the client
// would not look alien beside the real one. They are variables now because
// people want their own client to look like theirs - the defaults are still
// discord's, and "сбросить" puts every one of them back.
//
// Written as plain globals rather than behind accessors: every drawing site in
// the client already says col::something, and a client that has to call a
// function for each colour it draws would be a worse read for no gain.
namespace col
{
    extern ImU32 bg_deep;
    extern ImU32 bg_panel;
    extern ImU32 bg_chat;
    extern ImU32 bg_hover;
    extern ImU32 bg_active;
    extern ImU32 bg_input;

    extern ImU32 text_normal;
    extern ImU32 text_muted;
    extern ImU32 text_link;

    extern ImU32 accent;
    extern ImU32 accent_hover;
    extern ImU32 green;
    extern ImU32 red;
    extern ImU32 yellow;
    extern ImU32 separator;
}

namespace theme
{
    // How round things are drawn, both as a fraction of the shape's own size
    // so one number works at every scale.
    //
    // 0.5 on an avatar is the circle discord uses; 0 is a square. Panels and
    // buttons take their own, which imgui wants in pixels rather than as a
    // fraction, so that one is in pixels.
    float avatar_rounding();          // 0 .. 0.5
    float corner_rounding();          // pixels

    void set_avatar_rounding(float v);
    void set_corner_rounding(float v);

    // One entry per colour, so the settings screen can walk them rather than
    // naming fifteen of them twice.
    struct entry
    {
        const char* label;            // russian, translated at the draw site
        const char* key;              // where it is remembered
        ImU32* value;
        ImU32 fallback;               // discord's own, for "сбросить"
    };

    int color_count();
    entry* color_at(int index);

    // Reads what was saved and hands it to imgui. Called once at startup and
    // again after anything here changes.
    void load();
    void apply();

    // Everything back to the colours discord ships with.
    void reset();

    // A whole look in one press. The first is discord's own.
    int preset_count();
    const char* preset_name(int index);
    void apply_preset(int index);
}

void theme_apply();
