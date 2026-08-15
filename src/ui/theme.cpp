#include "pch.h"
#include "theme.h"

static ImVec4 to_vec(ImU32 c)
{
    return ImGui::ColorConvertU32ToFloat4(c);
}

void theme_apply()
{
    ImGuiStyle& s = ImGui::GetStyle();

    s.WindowRounding = 0.0f;
    s.ChildRounding = 6.0f;
    s.FrameRounding = 4.0f;
    s.PopupRounding = 6.0f;
    s.ScrollbarRounding = 8.0f;
    s.GrabRounding = 4.0f;
    s.TabRounding = 4.0f;

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
