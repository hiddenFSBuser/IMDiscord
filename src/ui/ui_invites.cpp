#include "pch.h"
#include "ui_state.h"
#include "theme.h"

#include "discord/store.h"
#include "discord/rest.h"
#include "discord/science.h"

// Invite links.
//
// Both belong to a channel rather than to the server - an invite points at
// somewhere to arrive, a webhook posts into somewhere - so the channel is
// chosen here rather than assumed, and both share the one window because the
// choice is the same choice.

namespace
{
    struct age_row
    {
        int seconds;
        const char* label;
    };

    // Zero is not a missing value here: discord reads it as "never expires",
    // which is why it sits at the end of the list rather than being special.
    const age_row AGES[] = {
        { 1800,   "30 минут" },
        { 3600,   "1 час" },
        { 21600,  "6 часов" },
        { 43200,  "12 часов" },
        { 86400,  "1 день" },
        { 604800, "7 дней" },
        { 0,      "никогда" },
    };

    const int AGE_COUNT = (int)(sizeof(AGES) / sizeof(AGES[0]));

    struct uses_row
    {
        int uses;
        const char* label;
    };

    const uses_row USES[] = {
        { 0,   "без ограничений" },
        { 1,   "1 раз" },
        { 5,   "5 раз" },
        { 10,  "10 раз" },
        { 25,  "25 раз" },
        { 50,  "50 раз" },
        { 100, "100 раз" },
    };

    const int USES_COUNT = (int)(sizeof(USES) / sizeof(USES[0]));

    int g_age = 4;          // a day, which is what discord defaults to
    int g_uses = 0;
    bool g_temporary = false;
    // Not "IMDiscord": discord refuses a webhook name containing "discord",
    // and answers with a bare "Invalid Form Body" that names no field at all.
    char g_hook_name[96] = "IMD Hook";

    // The first text channel of the server, used until somebody picks another.
    snowflake first_text_channel(dguild* g)
    {
        for (unsigned int i = 0; i < g->channels.count; i++)
        {
            dchannel* c = store::find_channel(g->channels[i]);
            if (c && (c->type == CH_GUILD_TEXT || c->type == CH_ANNOUNCEMENT)) return c->id;
        }
        return 0;
    }

    void channel_picker(dguild* g)
    {
        dchannel* current = store::find_channel(g_ui.invite_channel);
        if (!current)
        {
            g_ui.invite_channel = first_text_channel(g);
            current = store::find_channel(g_ui.invite_channel);
        }

        char label[160];
        if (current) ui_channel_display_name(current, label, sizeof(label));
        else ccstrncpy(label, tr("нет текстовых каналов"), sizeof(label) - 1);

        ImGui::SetNextItemWidth(300);
        if (!ImGui::BeginCombo(tr("Канал"), label)) return;

        for (unsigned int i = 0; i < g->channels.count; i++)
        {
            dchannel* c = store::find_channel(g->channels[i]);
            if (!c || (c->type != CH_GUILD_TEXT && c->type != CH_ANNOUNCEMENT)) continue;

            char row[160];
            ui_channel_display_name(c, row, sizeof(row));

            ImGui::PushID((int)(c->id & 0x7FFFFFFF));
            if (ImGui::Selectable(row, c->id == g_ui.invite_channel))
                g_ui.invite_channel = c->id;
            ImGui::PopID();
        }

        ImGui::EndCombo();
    }
}

void ui_open_invites(snowflake guild_id)
{
    science::guild_settings_viewed("invites_v2", guild_id);
    g_ui.invite_guild = guild_id;
    g_ui.invite_channel = 0;
    g_ui.open_invites_popup = true;
    api::clear_last_link();
    api::clear_last_error();

    // Asked for once on opening. Nothing announces invites, so without this
    // the panel would sit empty until somebody thought to press refresh.
    api::fetch_invites(guild_id);
}

void ui_view_invites_popup()
{
    if (g_ui.open_invites_popup)
    {
        ImGui::OpenPopup("##invites");
        g_ui.open_invites_popup = false;
    }

    ImGui::SetNextWindowSize(ImVec2(600, 660), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("##invites", 0, ImGuiWindowFlags_NoTitleBar)) return;

    store::guard guard;
    dguild* g = store::find_guild(g_ui.invite_guild);

    if (!g)
    {
        ui_text_muted(tr("Сервер недоступен"));
        if (ImGui::Button(tr("Закрыть"), ImVec2(120, 30))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    ImGui::TextUnformatted(tr("Приглашения"));
    ImGui::SameLine();
    ui_text_muted(g->name ? g->name : "");
    ImGui::Separator();

    channel_picker(g);
    ImGui::Dummy(ImVec2(0, 8));

    // ---- invite ---------------------------------------------------------
    ImGui::TextUnformatted(tr("Ссылка-приглашение"));

    ImGui::SetNextItemWidth(200);
    if (ImGui::BeginCombo(tr("Срок действия"), tr(AGES[g_age].label)))
    {
        for (int i = 0; i < AGE_COUNT; i++)
            if (ImGui::Selectable(tr(AGES[i].label), i == g_age)) g_age = i;
        ImGui::EndCombo();
    }

    ImGui::SetNextItemWidth(200);
    if (ImGui::BeginCombo(tr("Число использований"), tr(USES[g_uses].label)))
    {
        for (int i = 0; i < USES_COUNT; i++)
            if (ImGui::Selectable(tr(USES[i].label), i == g_uses)) g_uses = i;
        ImGui::EndCombo();
    }

    ImGui::Checkbox(tr("Временное участие"), &g_temporary);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(tr("Вошедшего выкинет при отключении, если он не получит роль"));

    if (ImGui::Button(tr("Создать приглашение"), ImVec2(190, 30)) && g_ui.invite_channel)
    {
        api::clear_last_link();
        api::clear_last_error();
        api::create_invite(g_ui.invite_channel, AGES[g_age].seconds, USES[g_uses].uses,
                           g_temporary);
    }

    ImGui::Dummy(ImVec2(0, 8));
    ImGui::Separator();

    // ---- what already exists ---------------------------------------------
    //
    // Fetched on opening rather than watched: invites are not gateway state,
    // so nothing would ever tell the client that one appeared or ran out.
    ImGui::TextUnformatted(tr("Уже созданные"));

    if (ImGui::Button(tr("Обновить"), ImVec2(120, 24))) api::fetch_invites(g->id);
    ImGui::SameLine();
    if (api::invites_loading()) ui_text_muted(tr("загружается..."));

    {
        api::invite_row rows[64];
        int count = api::invites(rows, 64);

        ImGui::BeginChild("##invlist", ImVec2(0, 150), true);

        if (api::invites_forbidden())
        {
            // An empty list and a refused one look the same, and only one of
            // them is the user's to do anything about.
            ImGui::PushStyleColor(ImGuiCol_Text, col::yellow);
            ImGui::TextWrapped(tr("Нет права \"Управлять сервером\" - список посмотреть нельзя"));
            ImGui::PopStyleColor();
        }
        else if (!count)
        {
            ui_text_muted(tr("Приглашений нет"));
        }

        for (int i = 0; i < count; i++)
        {
            ImGui::PushID(i);

            char line[256];
            char uses[48];
            char age[48];

            if (rows[i].max_uses) cnprint(uses, sizeof(uses), tr("%d из %d"), rows[i].uses, rows[i].max_uses);
            else                  cnprint(uses, sizeof(uses), tr("%d раз"), rows[i].uses);

            if (!rows[i].max_age)            ccstrncpy(age, tr("бессрочно"), sizeof(age) - 1);
            else if (rows[i].max_age >= 86400) cnprint(age, sizeof(age), tr("%d дн"), rows[i].max_age / 86400);
            else if (rows[i].max_age >= 3600)  cnprint(age, sizeof(age), tr("%d ч"), rows[i].max_age / 3600);
            else                               cnprint(age, sizeof(age), tr("%d мин"), rows[i].max_age / 60);

            dchannel* c = store::find_channel(rows[i].channel_id);
            char where[160];
            if (c) ui_channel_display_name(c, where, sizeof(where));
            else   ccstrncpy(where, "?", sizeof(where) - 1);

            cnprint(line, sizeof(line), "%s  -  %s, %s, %s%s",
                    rows[i].code, where, age, uses,
                    rows[i].temporary ? tr(", временное") : "");

            ImGui::TextUnformatted(line);

            if (rows[i].inviter[0])
            {
                ImGui::SameLine();
                ui_text_muted(rows[i].inviter);
            }

            ImGui::SameLine();
            if (ImGui::SmallButton(tr("Копировать")))
            {
                char url[128];
                cnprint(url, sizeof(url), "https://discord.gg/%s", rows[i].code);
                ImGui::SetClipboardText(url);
            }

            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, col::red);
            if (ImGui::SmallButton(tr("Отозвать"))) api::revoke_invite(rows[i].code, g->id);
            ImGui::PopStyleColor();

            ImGui::PopID();
        }

        ImGui::EndChild();
    }

    ImGui::Dummy(ImVec2(0, 8));
    ImGui::Separator();

    // ---- what came back --------------------------------------------------
    const char* link = api::last_link();
    if (link && link[0])
    {
        ImGui::TextUnformatted(tr("Готово:"));

        // Read only rather than plain text, so it can be selected and copied by
        // hand as well as by the button.
        char shown[256];
        ccstrncpy(shown, link, sizeof(shown) - 1);

        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##link", shown, sizeof(shown), ImGuiInputTextFlags_ReadOnly);

        if (ImGui::Button(tr("Скопировать"), ImVec2(140, 28))) ImGui::SetClipboardText(link);
    }
    else if (api::last_error()[0])
    {
        ImGui::PushStyleColor(ImGuiCol_Text, col::yellow);
        ImGui::TextWrapped("%s", api::last_error());
        ImGui::PopStyleColor();
    }
    else
    {
        ui_text_muted(tr("Ссылка появится здесь"));
    }

    ImGui::Dummy(ImVec2(0, 8));
    if (ImGui::Button(tr("Закрыть"), ImVec2(120, 30)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        api::clear_last_error();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}
