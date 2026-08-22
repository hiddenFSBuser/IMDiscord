#include "pch.h"
#include "ui_state.h"
#include "theme.h"

#include "discord/store.h"
#include "discord/rest.h"

// Renaming a channel, and by the same window a category - discord has no
// separate object for one, so the same request does both.
//
// A window rather than an editable field in the list. The list is a place
// things get clicked to be opened, and a name that turns into a text box on a
// stray double click is a name that gets changed by accident.

namespace
{
    char g_typed[128];
    snowflake g_loaded = 0;
}

void ui_open_rename_channel(snowflake channel_id)
{
    g_ui.rename_channel_id = channel_id;
    g_ui.open_rename_popup = true;
    g_loaded = 0;

    api::clear_last_error();
}

void ui_view_rename_popup()
{
    if (g_ui.open_rename_popup)
    {
        ImGui::OpenPopup("##renamechan");
        g_ui.open_rename_popup = false;
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f,
                                   vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(420, 0));

    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGui::ColorConvertU32ToFloat4(col::bg_panel));
    if (!ImGui::BeginPopupModal("##renamechan", 0,
                                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::PopStyleColor();
        return;
    }

    store::guard guard;

    dchannel* c = store::find_channel(g_ui.rename_channel_id);
    if (!c)
    {
        ui_text_muted(tr("Канал недоступен"));
        if (ImGui::Button(tr("Закрыть"), ImVec2(120, 30))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        ImGui::PopStyleColor();
        return;
    }

    // Filled once per opening. Every frame would overwrite what is being typed
    // with what is stored, and the field could never be changed at all.
    if (g_loaded != c->id)
    {
        g_loaded = c->id;
        ccfset(g_typed, 0, sizeof(g_typed));
        ccstrncpy(g_typed, c->name ? c->name : "", sizeof(g_typed) - 1);
    }

    bool category = c->type == CH_CATEGORY;
    ImGui::TextUnformatted(category ? tr("Переименовать категорию") : tr("Переименовать канал"));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 4));

    // Focused on the first frame, so the window can be opened, typed into and
    // dismissed with the return key without the mouse being touched.
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();

    ImGui::SetNextItemWidth(-1.0f);
    bool entered = ImGui::InputText("##newname", g_typed, sizeof(g_typed),
                                    ImGuiInputTextFlags_EnterReturnsTrue);

    // Discord lowercases a text channel's name and turns spaces into dashes
    // whatever is sent, so saying so beforehand is kinder than the name coming
    // back changed with no explanation.
    if (!category && (c->type == CH_GUILD_TEXT || c->type == CH_ANNOUNCEMENT))
        ui_text_muted(tr("Пробелы станут дефисами, буквы - строчными."));

    ImGui::Dummy(ImVec2(0, 8));

    bool ready = g_typed[0] != 0;
    if (!ready) ImGui::BeginDisabled();

    bool go = ImGui::Button(tr("Переименовать"), ImVec2(160, 30)) || (entered && ready);

    if (!ready) ImGui::EndDisabled();

    if (go)
    {
        api::rename_channel(c->id, g_typed);
        ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button(tr("Отмена"), ImVec2(120, 30)) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        api::clear_last_error();
        ImGui::CloseCurrentPopup();
    }

    if (api::last_error()[0])
    {
        ImGui::Dummy(ImVec2(0, 6));
        ImGui::PushStyleColor(ImGuiCol_Text, col::yellow);
        ImGui::TextWrapped("%s", api::last_error());
        ImGui::PopStyleColor();
    }

    ImGui::EndPopup();
    ImGui::PopStyleColor();
}
