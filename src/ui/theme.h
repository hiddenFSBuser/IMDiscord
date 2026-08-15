#pragma once
#include "imgui.h"

// Palette lifted from the discord dark theme so the client does not look alien
// next to the real one.
namespace col
{
    const ImU32 bg_deep      = IM_COL32(30, 31, 34, 255);
    const ImU32 bg_panel     = IM_COL32(43, 45, 49, 255);
    const ImU32 bg_chat      = IM_COL32(49, 51, 56, 255);
    const ImU32 bg_hover     = IM_COL32(57, 60, 67, 255);
    const ImU32 bg_active    = IM_COL32(66, 70, 77, 255);
    const ImU32 bg_input     = IM_COL32(56, 58, 64, 255);

    const ImU32 text_normal  = IM_COL32(219, 222, 225, 255);
    const ImU32 text_muted   = IM_COL32(148, 155, 164, 255);
    const ImU32 text_link    = IM_COL32(0, 168, 252, 255);

    const ImU32 accent       = IM_COL32(88, 101, 242, 255);
    const ImU32 accent_hover = IM_COL32(71, 82, 196, 255);
    const ImU32 green        = IM_COL32(35, 165, 90, 255);
    const ImU32 red          = IM_COL32(218, 55, 60, 255);
    const ImU32 yellow       = IM_COL32(240, 178, 50, 255);
    const ImU32 separator    = IM_COL32(60, 63, 69, 255);
}

void theme_apply();
