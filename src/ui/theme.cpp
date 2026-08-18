#include "pch.h"
#include "theme.h"
#include "core/storage.h"

// The palette lives here rather than in the header so there is one of each.
// Defaults are discord's own, which is what "сбросить" puts back.
namespace col
{
    ImU32 bg_deep      = IM_COL32(30, 31, 34, 255);
    ImU32 bg_panel     = IM_COL32(43, 45, 49, 255);
    ImU32 bg_chat      = IM_COL32(49, 51, 56, 255);
    ImU32 bg_hover     = IM_COL32(57, 60, 67, 255);
    ImU32 bg_active    = IM_COL32(66, 70, 77, 255);
    ImU32 bg_input     = IM_COL32(56, 58, 64, 255);

    ImU32 text_normal  = IM_COL32(219, 222, 225, 255);
    ImU32 text_muted   = IM_COL32(148, 155, 164, 255);
    ImU32 text_link    = IM_COL32(0, 168, 252, 255);

    ImU32 accent       = IM_COL32(88, 101, 242, 255);
    ImU32 accent_hover = IM_COL32(71, 82, 196, 255);
    ImU32 green        = IM_COL32(35, 165, 90, 255);
    ImU32 red          = IM_COL32(218, 55, 60, 255);
    ImU32 yellow       = IM_COL32(240, 178, 50, 255);
    ImU32 separator    = IM_COL32(60, 63, 69, 255);
}

namespace
{
    // The order here is the order the settings screen shows them in:
    // backgrounds first, then text, then the colours that mean something.
    theme::entry g_colors[] = {
        { "Фон - самый тёмный", "col_bg_deep",   &col::bg_deep,     IM_COL32(30, 31, 34, 255) },
        { "Фон панелей",        "col_bg_panel",  &col::bg_panel,    IM_COL32(43, 45, 49, 255) },
        { "Фон чата",           "col_bg_chat",   &col::bg_chat,     IM_COL32(49, 51, 56, 255) },
        { "Наведение",          "col_bg_hover",  &col::bg_hover,    IM_COL32(57, 60, 67, 255) },
        { "Выбранное",          "col_bg_active", &col::bg_active,   IM_COL32(66, 70, 77, 255) },
        { "Поля ввода",         "col_bg_input",  &col::bg_input,    IM_COL32(56, 58, 64, 255) },

        { "Текст",              "col_text",      &col::text_normal, IM_COL32(219, 222, 225, 255) },
        { "Текст приглушённый", "col_text_dim",  &col::text_muted,  IM_COL32(148, 155, 164, 255) },
        { "Ссылки",             "col_link",      &col::text_link,   IM_COL32(0, 168, 252, 255) },

        { "Акцент",             "col_accent",    &col::accent,      IM_COL32(88, 101, 242, 255) },
        { "Акцент при нажатии", "col_accent2",   &col::accent_hover,IM_COL32(71, 82, 196, 255) },
        { "Зелёный",            "col_green",     &col::green,       IM_COL32(35, 165, 90, 255) },
        { "Красный",            "col_red",       &col::red,         IM_COL32(218, 55, 60, 255) },
        { "Жёлтый",             "col_yellow",    &col::yellow,      IM_COL32(240, 178, 50, 255) },
        { "Разделители",        "col_separator", &col::separator,   IM_COL32(60, 63, 69, 255) },
    };

    const int COLOR_COUNT = (int)(sizeof(g_colors) / sizeof(g_colors[0]));

    float g_avatar_rounding = -1.0f;
    float g_corner_rounding = -1.0f;

    // A whole look in one press.
    //
    // Each is the six backgrounds and the accent; the text and the meaning
    // colours are left alone, because a red that stops looking like a warning
    // is not a theme, it is a bug somebody chose.
    struct preset
    {
        const char* name;
        ImU32 deep, panel, chat, hover, active, input, accent, accent2;
    };

    const preset g_presets[] = {
        { "Discord",
          IM_COL32(30, 31, 34, 255),  IM_COL32(43, 45, 49, 255),
          IM_COL32(49, 51, 56, 255),  IM_COL32(57, 60, 67, 255),
          IM_COL32(66, 70, 77, 255),  IM_COL32(56, 58, 64, 255),
          IM_COL32(88, 101, 242, 255), IM_COL32(71, 82, 196, 255) },

        // For an OLED screen, where a true black costs nothing to light.
        { "Полночь",
          IM_COL32(0, 0, 0, 255),      IM_COL32(10, 10, 12, 255),
          IM_COL32(16, 16, 19, 255),   IM_COL32(28, 28, 32, 255),
          IM_COL32(38, 38, 44, 255),   IM_COL32(22, 22, 26, 255),
          IM_COL32(120, 130, 255, 255), IM_COL32(96, 105, 220, 255) },

        { "Уголь",
          IM_COL32(24, 24, 24, 255),   IM_COL32(34, 34, 34, 255),
          IM_COL32(40, 40, 40, 255),   IM_COL32(52, 52, 52, 255),
          IM_COL32(64, 64, 64, 255),   IM_COL32(46, 46, 46, 255),
          IM_COL32(200, 200, 200, 255), IM_COL32(160, 160, 160, 255) },

        { "Ночное море",
          IM_COL32(14, 22, 33, 255),   IM_COL32(20, 30, 44, 255),
          IM_COL32(25, 37, 54, 255),   IM_COL32(33, 48, 68, 255),
          IM_COL32(42, 60, 84, 255),   IM_COL32(28, 41, 59, 255),
          IM_COL32(56, 160, 220, 255), IM_COL32(42, 128, 178, 255) },

        { "Тёплый",
          IM_COL32(32, 27, 24, 255),   IM_COL32(45, 38, 33, 255),
          IM_COL32(52, 44, 38, 255),   IM_COL32(66, 56, 48, 255),
          IM_COL32(80, 68, 58, 255),   IM_COL32(58, 49, 42, 255),
          IM_COL32(214, 132, 74, 255), IM_COL32(176, 106, 58, 255) },

        { "Лес",
          IM_COL32(19, 28, 22, 255),   IM_COL32(27, 39, 31, 255),
          IM_COL32(33, 47, 37, 255),   IM_COL32(44, 61, 48, 255),
          IM_COL32(56, 76, 60, 255),   IM_COL32(37, 52, 41, 255),
          IM_COL32(88, 178, 110, 255), IM_COL32(66, 140, 86, 255) },
    };

    const int PRESET_COUNT = (int)(sizeof(g_presets) / sizeof(g_presets[0]));
}

int theme::color_count() { return COLOR_COUNT; }

theme::entry* theme::color_at(int index)
{
    if (index < 0 || index >= COLOR_COUNT) return 0;
    return &g_colors[index];
}

float theme::avatar_rounding()
{
    if (g_avatar_rounding < 0.0f)
        g_avatar_rounding = (float)storage::settings_get_int("avatar_rounding", 50) / 100.0f;
    return g_avatar_rounding;
}

float theme::corner_rounding()
{
    if (g_corner_rounding < 0.0f)
        g_corner_rounding = (float)storage::settings_get_int("corner_rounding", 4);
    return g_corner_rounding;
}

void theme::set_avatar_rounding(float v)
{
    if (v < 0.0f) v = 0.0f;
    if (v > 0.5f) v = 0.5f;

    g_avatar_rounding = v;
    storage::settings_set_int("avatar_rounding", (int)(v * 100.0f + 0.5f));
    storage::settings_save();
}

void theme::set_corner_rounding(float v)
{
    if (v < 0.0f) v = 0.0f;
    if (v > 16.0f) v = 16.0f;

    g_corner_rounding = v;
    storage::settings_set_int("corner_rounding", (int)(v + 0.5f));
    storage::settings_save();
    theme::apply();
}

void theme::load()
{
    for (int i = 0; i < COLOR_COUNT; i++)
    {
        // Stored as the integer imgui already keeps them in, so nothing is
        // lost to a round trip through text.
        int v = storage::settings_get_int(g_colors[i].key, 0);
        if (v) *g_colors[i].value = (ImU32)(unsigned int)v;
    }

    theme::avatar_rounding();
    theme::corner_rounding();
}

void theme::reset()
{
    for (int i = 0; i < COLOR_COUNT; i++)
    {
        *g_colors[i].value = g_colors[i].fallback;
        storage::settings_set_int(g_colors[i].key, 0);
    }

    theme::set_avatar_rounding(0.5f);
    theme::set_corner_rounding(4.0f);
    theme::apply();
}

int theme::preset_count() { return PRESET_COUNT; }

const char* theme::preset_name(int index)
{
    if (index < 0 || index >= PRESET_COUNT) return "";
    return g_presets[index].name;
}

void theme::apply_preset(int index)
{
    if (index < 0 || index >= PRESET_COUNT) return;

    const preset* p = &g_presets[index];

    col::bg_deep = p->deep;
    col::bg_panel = p->panel;
    col::bg_chat = p->chat;
    col::bg_hover = p->hover;
    col::bg_active = p->active;
    col::bg_input = p->input;
    col::accent = p->accent;
    col::accent_hover = p->accent2;

    // A preset that only lasts until the client restarts is a preview, not a
    // setting, so each colour it moved is written down.
    for (int i = 0; i < COLOR_COUNT; i++)
        storage::settings_set_int(g_colors[i].key, (int)(unsigned int)*g_colors[i].value);

    storage::settings_save();
    theme::apply();
}

void theme::apply() { theme_apply(); }

static ImVec4 to_vec(ImU32 c)
{
    return ImGui::ColorConvertU32ToFloat4(c);
}

void theme_apply()
{
    ImGuiStyle& s = ImGui::GetStyle();

    s.WindowRounding = 0.0f;
    // One number behind all of them, so "закругления" moves the whole
    // interface together rather than one widget at a time.
    float r = theme::corner_rounding();

    s.ChildRounding = r * 1.5f;
    s.FrameRounding = r;
    s.PopupRounding = r * 1.5f;
    s.ScrollbarRounding = r * 2.0f;
    s.GrabRounding = r;
    s.TabRounding = r;

    s.WindowBorderSize = 0.0f;
    s.ChildBorderSize = 0.0f;
    s.FrameBorderSize = 0.0f;
    s.PopupBorderSize = 1.0f;

    s.WindowPadding = ImVec2(8, 8);
    s.FramePadding = ImVec2(8, 5);
    s.ItemSpacing = ImVec2(8, 6);
    s.ItemInnerSpacing = ImVec2(6, 4);
    s.ScrollbarSize = 10.0f;
    s.GrabMinSize = 12.0f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = to_vec(col::text_normal);
    c[ImGuiCol_TextDisabled] = to_vec(col::text_muted);
    c[ImGuiCol_WindowBg] = to_vec(col::bg_chat);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = to_vec(col::bg_panel);
    c[ImGuiCol_Border] = to_vec(col::separator);
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = to_vec(col::bg_input);
    c[ImGuiCol_FrameBgHovered] = to_vec(col::bg_hover);
    c[ImGuiCol_FrameBgActive] = to_vec(col::bg_active);
    c[ImGuiCol_TitleBg] = to_vec(col::bg_deep);
    c[ImGuiCol_TitleBgActive] = to_vec(col::bg_deep);
    c[ImGuiCol_TitleBgCollapsed] = to_vec(col::bg_deep);
    c[ImGuiCol_MenuBarBg] = to_vec(col::bg_panel);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = to_vec(col::bg_deep);
    c[ImGuiCol_ScrollbarGrabHovered] = to_vec(col::bg_active);
    c[ImGuiCol_ScrollbarGrabActive] = to_vec(col::accent);
    c[ImGuiCol_CheckMark] = to_vec(col::accent);
    c[ImGuiCol_SliderGrab] = to_vec(col::accent);
    c[ImGuiCol_SliderGrabActive] = to_vec(col::accent_hover);
    c[ImGuiCol_Button] = to_vec(col::accent);
    c[ImGuiCol_ButtonHovered] = to_vec(col::accent_hover);
    c[ImGuiCol_ButtonActive] = to_vec(col::accent_hover);
    c[ImGuiCol_Header] = to_vec(col::bg_hover);
    c[ImGuiCol_HeaderHovered] = to_vec(col::bg_hover);
    c[ImGuiCol_HeaderActive] = to_vec(col::bg_active);
    c[ImGuiCol_Separator] = to_vec(col::separator);
    c[ImGuiCol_SeparatorHovered] = to_vec(col::accent);
    c[ImGuiCol_SeparatorActive] = to_vec(col::accent);
    c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_Tab] = to_vec(col::bg_panel);
    c[ImGuiCol_TabHovered] = to_vec(col::bg_hover);
    c[ImGuiCol_TabActive] = to_vec(col::bg_chat);
    c[ImGuiCol_TabUnfocused] = to_vec(col::bg_panel);
    c[ImGuiCol_TabUnfocusedActive] = to_vec(col::bg_chat);
    c[ImGuiCol_TextSelectedBg] = ImVec4(0.35f, 0.40f, 0.95f, 0.35f);
    c[ImGuiCol_NavHighlight] = to_vec(col::accent);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.55f);
}
