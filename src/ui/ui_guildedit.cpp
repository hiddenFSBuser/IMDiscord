#include "pch.h"
#include "ui_state.h"
#include "theme.h"

#include "discord/store.h"
#include "discord/rest.h"
#include "discord/science.h"
#include "system/io/ufile.h"

// Renaming a server and changing its icon.

namespace
{
    char g_name[128];
    snowflake g_loaded = 0;
}

void ui_open_guild_edit(snowflake guild_id)
{
    science::guild_settings_viewed("profile", guild_id);
    g_ui.guildedit_guild = guild_id;
    g_ui.open_guildedit_popup = true;
    g_loaded = 0;
    api::clear_last_error();
}

void ui_view_guild_edit_popup()
{
    if (g_ui.open_guildedit_popup)
    {
        ImGui::OpenPopup("##guildedit");
        g_ui.open_guildedit_popup = false;
    }

    ImGui::SetNextWindowSize(ImVec2(440, 260), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("##guildedit", 0, ImGuiWindowFlags_NoTitleBar)) return;

    store::guard guard;
    dguild* g = store::find_guild(g_ui.guildedit_guild);

    if (!g)
    {
        ui_text_muted(tr("Сервер недоступен"));
        if (ImGui::Button(tr("Закрыть"), ImVec2(120, 30))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    // Loaded once per opening, not every frame: otherwise what is being typed
    // is overwritten by what is stored on the very next frame.
    if (g_loaded != g->id)
    {
        g_loaded = g->id;
        ccfset(g_name, 0, sizeof(g_name));
        ccstrncpy(g_name, g->name ? g->name : "", sizeof(g_name) - 1);
    }

    ImGui::TextUnformatted(tr("Настройки сервера"));
    ImGui::Separator();

    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##gname", g_name, sizeof(g_name));

    if (ImGui::Button(tr("Переименовать"), ImVec2(160, 30)) && g_name[0])
        api::update_guild_name(g->id, g_name);

    ImGui::Dummy(ImVec2(0, 10));
    ImGui::TextUnformatted(tr("Иконка"));
    ui_text_muted(tr("PNG, JPEG, GIF или WEBP, до 10 МБ"));

    if (ImGui::Button(tr("Выбрать файл"), ImVec2(160, 30)))
    {
        wchar_t chosen[MAX_PATH];
        if (ufile::open_dialog(chosen, MAX_PATH))
            api::update_guild_icon(g->id, chosen);
    }

    ImGui::SameLine();
    if (ImGui::Button(tr("Убрать"), ImVec2(120, 30)))
        api::update_guild_icon(g->id, 0);

    if (api::last_error()[0])
    {
        ImGui::Dummy(ImVec2(0, 6));
        ImGui::PushStyleColor(ImGuiCol_Text, col::yellow);
        ImGui::TextWrapped("%s", api::last_error());
        ImGui::PopStyleColor();
    }

    ImGui::Separator();
    if (ImGui::Button(tr("Закрыть"), ImVec2(120, 30)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        api::clear_last_error();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}
