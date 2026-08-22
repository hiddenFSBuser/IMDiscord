#include "pch.h"
#include "ui_state.h"
#include "theme.h"

#include "discord/store.h"
#include "discord/rest.h"
#include "discord/privacy.h"

// The privacy switches.
//
// Deliberately short. These live in a protobuf whose field numbering is not
// published, and the two offered here are the two a capture of the real client
// actually showed being written. Adding switches for guessed field numbers
// would produce controls that either do nothing or quietly change something
// else, which is worse than not having them.

void ui_open_privacy()
{
    g_ui.open_privacy_popup = true;
    api::clear_last_error();
    privacy::fetch();
}

void ui_view_privacy_popup()
{
    if (g_ui.open_privacy_popup)
    {
        ImGui::OpenPopup("##privacy");
        g_ui.open_privacy_popup = false;
    }

    ImGui::SetNextWindowSize(ImVec2(560, 500), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("##privacy", 0, ImGuiWindowFlags_NoTitleBar)) return;

    ImGui::TextUnformatted(tr("Приватность"));
    ImGui::SameLine();
    if (privacy::busy()) ui_text_muted(tr("загружается..."));
    ImGui::Separator();

    if (!privacy::ready())
    {
        ui_text_muted(tr("Настройки ещё не получены с сервера"));
    }
    else
    {
        ImGui::TextUnformatted(tr("Личные сообщения от участников серверов"));
        ui_text_muted(tr("Кто состоит с вами на одном сервере, но не в друзьях"));
        ImGui::Dummy(ImVec2(0, 4));

        bool by_default = privacy::dms_allowed_by_default();
        if (ImGui::Checkbox(tr("Разрешать на новых серверах"), &by_default))
            privacy::set_dms_allowed_by_default(by_default);

        ImGui::Dummy(ImVec2(0, 6));
        ImGui::TextUnformatted(tr("По серверам"));

        ImGui::BeginChild("##privguilds", ImVec2(0, 280), true);

        store::guard guard;

        // The same order the rail shows, so the list reads as the servers the
        // person already knows rather than as whatever order a map produced.
        const ulist<snowflake>& guilds = store::guild_order();

        for (unsigned int i = 0; i < guilds.count; i++)
        {
            dguild* g = store::find_guild(guilds[i]);
            if (!g) continue;

            bool allowed = privacy::dms_allowed_from(g->id);

            ImGui::PushID((const void*)(size_t)g->id);
            if (ImGui::Checkbox(g->name ? g->name : tr("без имени"), &allowed))
                privacy::set_dms_allowed_from(g->id, allowed);
            ImGui::PopID();
        }

        if (!guilds.count) ui_text_muted(tr("Серверов нет"));

        ImGui::EndChild();

        ui_text_muted(tr("Снятая галочка запрещает писать вам участникам этого сервера"));
    }

    if (api::last_error()[0])
    {
        ImGui::PushStyleColor(ImGuiCol_Text, col::yellow);
        ImGui::TextWrapped("%s", api::last_error());
        ImGui::PopStyleColor();
    }

    ImGui::Separator();
    if (ImGui::Button(tr("Обновить"), ImVec2(120, 30))) privacy::fetch();

    ImGui::SameLine();
    if (ImGui::Button(tr("Закрыть"), ImVec2(120, 30)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        api::clear_last_error();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}
