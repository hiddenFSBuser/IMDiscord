#include "pch.h"
#include "ui_state.h"
#include "theme.h"

#include "discord/store.h"
#include "discord/rest.h"

// Deleting a server.
//
// One request, POST /guilds/{id}/delete with no body, and discord answers 204
// and the server is gone. There is nothing to undo it with and no copy kept on
// their side, which is the whole reason this file exists rather than a menu row
// that fires straight away.
//
// So the name has to be typed back in. Not a yes/no box: a yes/no box put in
// front of somebody who meant to press "leave" is answered by reflex, and the
// point here is to make the hand stop. Typing the name cannot be done by
// accident, and it also makes it plain which server is about to go - the reflex
// case is usually the right button on the wrong bubble.
//
// Discord sends no science event for any of this, so neither do we. A packet
// they have never seen is worse than no packet at all.

namespace
{
    // The request is out and the answer has not come back. Nothing else can be
    // done in here until it does, and pressing the button twice would send a
    // second delete for a server that is already gone.
    bool g_sent = false;
}

void ui_open_delete_guild(snowflake guild_id)
{
    g_ui.delete_guild_id = guild_id;
    g_ui.open_delete_guild_popup = true;
    g_sent = false;

    ccfset(g_ui.delete_guild_typed, 0, sizeof(g_ui.delete_guild_typed));
    api::clear_last_error();
}

void ui_view_delete_guild_popup()
{
    if (g_ui.open_delete_guild_popup)
    {
        ImGui::OpenPopup("##deleteguild");
        g_ui.open_delete_guild_popup = false;
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f,
                                   vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(460, 0));

    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGui::ColorConvertU32ToFloat4(col::bg_panel));
    if (!ImGui::BeginPopupModal("##deleteguild", 0,
                                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::PopStyleColor();
        return;
    }

    store::guard guard;

    dguild* g = store::find_guild(g_ui.delete_guild_id);

    // It can go while the box is open - somebody else deleting it, or this
    // very request coming back. Either way there is nothing left to confirm,
    // and this is where a successful delete ends up.
    if (!g)
    {
        if (g_ui.active_guild == g_ui.delete_guild_id)
        {
            g_ui.active_guild = 0;
            g_ui.active_channel = 0;
        }

        ui_text_muted(api::last_error()[0] ? api::last_error() : tr("Сервер удалён"));
        ImGui::Dummy(ImVec2(0, 8));
        if (ImGui::Button(tr("Закрыть"), ImVec2(120, 30)))
        {
            api::clear_last_error();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
        ImGui::PopStyleColor();
        return;
    }

    const char* name = g->name ? g->name : "";

    ImGui::TextUnformatted(tr("Удаление сервера"));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 4));

    char line[256];
    cnprint(line, sizeof(line), tr("Сервер «%s» будет удалён вместе со всеми каналами, "
                                   "сообщениями и ролями."), name);

    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(line);

    ImGui::Dummy(ImVec2(0, 4));
    ImGui::PushStyleColor(ImGuiCol_Text, col::yellow);
    ImGui::TextUnformatted(tr("Это необратимо. Отменить или восстановить будет нечем."));
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();

    ImGui::Dummy(ImVec2(0, 10));

    ImGui::TextUnformatted(tr("Введите название сервера, чтобы подтвердить:"));

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##guildname", name, g_ui.delete_guild_typed,
                             sizeof(g_ui.delete_guild_typed));

    ImGui::Dummy(ImVec2(0, 8));

    // Exactly, including case. A near miss here is somebody typing from memory
    // rather than reading what is in front of them, which is the state this box
    // is meant to catch.
    bool matches = name[0] && ccscmp(g_ui.delete_guild_typed, name) == 0;

    if (!matches || g_sent) ImGui::BeginDisabled();

    ImGui::PushStyleColor(ImGuiCol_Button, col::red);
    if (ImGui::Button(tr("Удалить сервер"), ImVec2(200, 32)))
    {
        api::delete_guild(g->id);
        g_sent = true;
    }
    ImGui::PopStyleColor();

    if (!matches || g_sent) ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button(tr("Отмена"), ImVec2(120, 32)) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        api::clear_last_error();
        ccfset(g_ui.delete_guild_typed, 0, sizeof(g_ui.delete_guild_typed));
        ImGui::CloseCurrentPopup();
    }

    ImGui::Dummy(ImVec2(0, 6));

    if (api::last_error()[0])
    {
        // Refused, so the button comes back: there is something to read now,
        // and trying again is a reasonable thing to want.
        g_sent = false;

        ImGui::PushStyleColor(ImGuiCol_Text, col::yellow);
        ImGui::TextWrapped("%s", api::last_error());
        ImGui::PopStyleColor();
    }
    else if (g_sent)
    {
        ui_text_muted(tr("удаляется..."));
    }

    ImGui::EndPopup();
    ImGui::PopStyleColor();
}
