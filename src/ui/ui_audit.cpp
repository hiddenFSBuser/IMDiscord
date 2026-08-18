#include "pch.h"
#include "ui_state.h"
#include "theme.h"

#include "discord/store.h"
#include "discord/rest.h"
#include "discord/science.h"

// The audit log and the ban list.
//
// One window with two tabs because they are read for the same reason and need
// the same permission, and switching between them while looking into something
// is the normal way round.

namespace
{
    int g_tab = 0;

    // A snowflake carries the moment it was made, so an audit row needs no
    // separate timestamp field to be placed in time.
    void time_text(snowflake id, char* out, int cap)
    {
        unsigned long long ms = snowflake_time_ms(id);
        if (!ms) { ccstrncpy(out, "", cap - 1); return; }

        SYSTEMTIME st;
        FILETIME ft;

        // Unix milliseconds to the windows epoch, which starts in 1601.
        unsigned long long ticks = (ms + 11644473600000ULL) * 10000ULL;
        ft.dwLowDateTime = (DWORD)(ticks & 0xFFFFFFFFULL);
        ft.dwHighDateTime = (DWORD)(ticks >> 32);

        FILETIME local;
        FileTimeToLocalFileTime(&ft, &local);

        if (FileTimeToSystemTime(&local, &st))
            cnprint(out, cap, "%02d.%02d %02d:%02d", st.wDay, st.wMonth, st.wHour, st.wMinute);
        else
            ccstrncpy(out, "", cap - 1);
    }

    const char* name_of(snowflake user_id, char* scratch, int cap)
    {
        if (!user_id) return "";

        duser* u = store::find_user(user_id);
        if (u) return u->display_name();

        cnprint(scratch, cap, "%llu", user_id);
        return scratch;
    }
}

void ui_open_audit(snowflake guild_id)
{
    g_ui.audit_guild = guild_id;
    g_ui.open_audit_popup = true;
    g_tab = 0;

    api::clear_last_error();
    api::fetch_audit_log(guild_id);
    api::fetch_bans(guild_id);

    // Two screens in one window here, but discord counts them separately.
    science::guild_settings_viewed("audit_log", guild_id);
    science::guild_settings_viewed("bans", guild_id);
}

void ui_view_audit_popup()
{
    if (g_ui.open_audit_popup)
    {
        ImGui::OpenPopup("##audit");
        g_ui.open_audit_popup = false;
    }

    ImGui::SetNextWindowSize(ImVec2(760, 560), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("##audit", 0, ImGuiWindowFlags_NoTitleBar)) return;

    store::guard guard;
    dguild* g = store::find_guild(g_ui.audit_guild);

    if (!g)
    {
        ui_text_muted(tr("Сервер недоступен"));
        if (ImGui::Button(tr("Закрыть"), ImVec2(120, 30))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    ImGui::TextUnformatted(tr("Модерация"));
    ImGui::SameLine();
    ui_text_muted(g->name ? g->name : "");
    ImGui::Separator();

    if (ui_icon_button(tr("Журнал аудита"), ImVec2(0, 28),
                       g_tab == 0 ? col::accent : col::bg_panel,
                       g_tab == 0 ? col::accent_hover : col::bg_hover))
        g_tab = 0;

    ImGui::SameLine(0, 6);
    if (ui_icon_button(tr("Баны"), ImVec2(0, 28),
                       g_tab == 1 ? col::accent : col::bg_panel,
                       g_tab == 1 ? col::accent_hover : col::bg_hover))
        g_tab = 1;

    ImGui::SameLine(0, 12);
    if (ImGui::Button(tr("Обновить"), ImVec2(110, 28)))
    {
        if (g_tab == 0) api::fetch_audit_log(g->id);
        else            api::fetch_bans(g->id);
    }

    ImGui::SameLine();
    if ((g_tab == 0 && api::audit_loading()) || (g_tab == 1 && api::bans_loading()))
        ui_text_muted(tr("загружается..."));

    ImGui::Dummy(ImVec2(0, 6));
    ImGui::BeginChild("##auditbody", ImVec2(0, 400), true);

    if (g_tab == 0)
    {
        if (api::audit_forbidden())
        {
            // An empty log and a refused one look identical, and only one of
            // them is something the person can act on.
            ImGui::PushStyleColor(ImGuiCol_Text, col::yellow);
            ImGui::TextWrapped(tr("Нет права \"Управлять сервером\" - журнал недоступен"));
            ImGui::PopStyleColor();
        }
        else
        {
            api::audit_row rows[64];
            int count = api::audit_log(rows, 64);

            if (!count) ui_text_muted(tr("Записей нет"));

            for (int i = 0; i < count; i++)
            {
                char when[32];
                char actor[96];
                char target[96];
                char action[48];

                time_text(rows[i].id, when, sizeof(when));

                char line[320];
                cnprint(line, sizeof(line), "%s  %s  -  %s",
                        when,
                        name_of(rows[i].actor, actor, sizeof(actor)),
                        api::audit_action_name(rows[i].action, action, sizeof(action)));

                ImGui::TextUnformatted(line);

                if (rows[i].target)
                {
                    ImGui::SameLine();
                    ui_text_muted(name_of(rows[i].target, target, sizeof(target)));
                }

                if (rows[i].reason[0])
                {
                    ImGui::SameLine();
                    ImGui::PushStyleColor(ImGuiCol_Text, col::yellow);
                    ImGui::TextUnformatted(rows[i].reason);
                    ImGui::PopStyleColor();
                }
            }
        }
    }
    else
    {
        if (api::bans_forbidden())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, col::yellow);
            ImGui::TextWrapped(tr("Нет права банить - список недоступен"));
            ImGui::PopStyleColor();
        }
        else
        {
            api::ban_row rows[256];
            int count = api::bans(rows, 256);

            if (!count) ui_text_muted(tr("Забаненных нет"));

            for (int i = 0; i < count; i++)
            {
                ImGui::PushID((int)(rows[i].user_id & 0x7FFFFFFF));

                ImGui::TextUnformatted(rows[i].name);

                ImGui::SameLine();
                if (ImGui::SmallButton("ID")) ui_copy_id(rows[i].user_id);

                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, col::green);
                if (ImGui::SmallButton(tr("Разбанить"))) api::unban(g->id, rows[i].user_id);
                ImGui::PopStyleColor();

                if (rows[i].reason[0])
                {
                    ImGui::SameLine();
                    ui_text_muted(rows[i].reason);
                }

                ImGui::PopID();
            }
        }
    }

    ImGui::EndChild();

    if (api::last_error()[0])
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
