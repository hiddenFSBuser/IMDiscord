#include "pch.h"
#include "ui_state.h"
#include "theme.h"

#include "discord/store.h"
#include "discord/rest.h"

// Webhooks: what exists, and making another.
//
// Split from invites because the two share only the channel they point at. An
// invite is something handed to a person, a webhook is an address handed to a
// program, and putting them in one window meant every use of it was half
// irrelevant.

namespace
{
    // Not "IMDiscord": discord refuses a webhook name containing "discord" and
    // answers with a bare "Invalid Form Body" that names no field at all.
    char g_hook_name[96] = "IMD Hook";

    snowflake first_text_channel(dguild* g)
    {
        for (unsigned int i = 0; i < g->channels.count; i++)
        {
            dchannel* c = store::find_channel(g->channels[i]);
            if (c && (c->type == CH_GUILD_TEXT || c->type == CH_ANNOUNCEMENT)) return c->id;
        }
        return 0;
    }
}

void ui_open_webhooks(snowflake guild_id)
{
    g_ui.hooks_guild = guild_id;
    g_ui.hooks_channel = 0;
    g_ui.open_hooks_popup = true;

    api::clear_last_link();
    api::clear_last_error();
    api::fetch_webhooks(guild_id);
}

void ui_view_webhooks_popup()
{
    if (g_ui.open_hooks_popup)
    {
        ImGui::OpenPopup("##webhooks");
        g_ui.open_hooks_popup = false;
    }

    ImGui::SetNextWindowSize(ImVec2(620, 520), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("##webhooks", 0, ImGuiWindowFlags_NoTitleBar)) return;

    store::guard guard;
    dguild* g = store::find_guild(g_ui.hooks_guild);

    if (!g)
    {
        ui_text_muted(tr("Сервер недоступен"));
        if (ImGui::Button(tr("Закрыть"), ImVec2(120, 30))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    ImGui::TextUnformatted(tr("Вебхуки"));
    ImGui::SameLine();
    ui_text_muted(g->name ? g->name : "");
    ImGui::Separator();

    // ---- what exists -----------------------------------------------------
    if (ImGui::Button(tr("Обновить"), ImVec2(120, 24))) api::fetch_webhooks(g->id);
    ImGui::SameLine();
    if (api::webhooks_loading()) ui_text_muted(tr("загружается..."));

    {
        api::webhook_row rows[64];
        int count = api::webhooks(rows, 64);

        ImGui::BeginChild("##hooklist", ImVec2(0, 200), true);

        if (!count) ui_text_muted(tr("Вебхуков нет"));

        for (int i = 0; i < count; i++)
        {
            ImGui::PushID((const void*)(size_t)rows[i].id);

            ImGui::TextUnformatted(rows[i].name);

            dchannel* c = store::find_channel(rows[i].channel_id);
            if (c)
            {
                char where[160];
                ui_channel_display_name(c, where, sizeof(where));
                ImGui::SameLine();
                ui_text_muted(where);
            }

            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, col::red);
            if (ImGui::SmallButton(tr("Удалить"))) api::delete_webhook(rows[i].id, g->id);
            ImGui::PopStyleColor();

            ImGui::PopID();
        }

        ImGui::EndChild();
    }

    ImGui::Dummy(ImVec2(0, 8));
    ImGui::Separator();

    // ---- making one ------------------------------------------------------
    ImGui::TextUnformatted(tr("Новый вебхук"));
    ui_text_muted(tr("Адрес показывается один раз - потом его уже не узнать"));
    ui_text_muted(tr("В имени нельзя использовать слова discord и clyde"));

    {
        dchannel* current = store::find_channel(g_ui.hooks_channel);
        if (!current)
        {
            g_ui.hooks_channel = first_text_channel(g);
            current = store::find_channel(g_ui.hooks_channel);
        }

        char label[160];
        if (current) ui_channel_display_name(current, label, sizeof(label));
        else ccstrncpy(label, tr("нет текстовых каналов"), sizeof(label) - 1);

        ImGui::SetNextItemWidth(280);
        if (ImGui::BeginCombo(tr("Канал"), label))
        {
            for (unsigned int i = 0; i < g->channels.count; i++)
            {
                dchannel* c = store::find_channel(g->channels[i]);
                if (!c || (c->type != CH_GUILD_TEXT && c->type != CH_ANNOUNCEMENT)) continue;

                char row[160];
                ui_channel_display_name(c, row, sizeof(row));

                ImGui::PushID((const void*)(size_t)c->id);
                if (ImGui::Selectable(row, c->id == g_ui.hooks_channel))
                    g_ui.hooks_channel = c->id;
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
    }

    ImGui::SetNextItemWidth(220);
    ImGui::InputText(tr("Имя"), g_hook_name, sizeof(g_hook_name));

    ImGui::SameLine();
    if (ImGui::Button(tr("Создать"), ImVec2(130, 26)) && g_ui.hooks_channel)
    {
        api::clear_last_link();
        api::clear_last_error();
        api::create_webhook(g_ui.hooks_channel, g_hook_name);
    }

    const char* link = api::last_link();
    if (link && link[0])
    {
        ImGui::Dummy(ImVec2(0, 6));
        ImGui::TextUnformatted(tr("Адрес:"));

        char shown[256];
        ccstrncpy(shown, link, sizeof(shown) - 1);

        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##hooklink", shown, sizeof(shown), ImGuiInputTextFlags_ReadOnly);

        if (ImGui::Button(tr("Скопировать"), ImVec2(140, 28))) ImGui::SetClipboardText(link);
    }
    else if (api::last_error()[0])
    {
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
