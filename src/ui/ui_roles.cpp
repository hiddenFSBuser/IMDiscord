#include "pch.h"
#include "ui_state.h"
#include "theme.h"

#include "discord/store.h"
#include "discord/rest.h"
#include "discord/science.h"

// Roles: who has them, what they allow, and making new ones.
//
// The server is the authority throughout. Nothing here writes a change into the
// store - every one of these calls comes back as a gateway dispatch, and
// applying it locally as well would leave the client showing a change the
// server quietly refused. A rejected call shows up as the list not changing,
// which is the honest answer.

namespace
{
    // The permissions worth offering. Discord defines about fifty; naming the
    // ones somebody actually reaches for beats a wall of switches where the
    // useful ones are lost among Manage Emojis and Priority Speaker.
    struct perm_row
    {
        unsigned long long bit;
        const char* label;
    };

    const perm_row PERMS[] = {
        { 1ULL << 3,  "Администратор" },
        { 1ULL << 0,  "Создавать приглашения" },
        { 1ULL << 1,  "Управлять никами" },
        { 1ULL << 2,  "Банить" },
        { 1ULL << 4,  "Управлять каналами" },
        { 1ULL << 5,  "Управлять сервером" },
        { 1ULL << 28, "Управлять ролями" },
        { 1ULL << 10, "Видеть каналы" },
        { 1ULL << 11, "Писать сообщения" },
        { 1ULL << 13, "Удалять чужие сообщения" },
        { 1ULL << 14, "Встраивать ссылки" },
        { 1ULL << 15, "Прикреплять файлы" },
        { 1ULL << 16, "Читать историю" },
        { 1ULL << 17, "Упоминать everyone" },
        { 1ULL << 20, "Подключаться к голосовым" },
        { 1ULL << 21, "Говорить" },
        { 1ULL << 22, "Выключать микрофон другим" },
        { 1ULL << 23, "Глушить других" },
        { 1ULL << 24, "Перемещать между каналами" },
        { 1ULL << 9,  "Демонстрация экрана" },
    };

    const int PERM_COUNT = (int)(sizeof(PERMS) / sizeof(PERMS[0]));

    drole* find_role(dguild* g, snowflake id)
    {
        if (!g) return 0;
        for (unsigned int i = 0; i < g->roles.count; i++)
            if (g->roles[i].id == id) return &g->roles[i];
        return 0;
    }

    // Loaded into the edit fields on selection rather than every frame, or
    // typing into the name would be overwritten by what is already stored.
    void load_role(drole* r)
    {
        g_ui.roles_selected = r ? r->id : 0;
        if (!r) return;

        ccfset(g_ui.role_name, 0, sizeof(g_ui.role_name));
        ccstrncpy(g_ui.role_name, r->name ? r->name : "", sizeof(g_ui.role_name) - 1);

        g_ui.role_perms = r->permissions;
        g_ui.role_color = r->color;
        g_ui.role_hoist = r->hoist;
        g_ui.role_mentionable = r->mentionable;
    }

    ImU32 role_tint(const drole* r)
    {
        if (!r->color) return col::text_normal;
        return IM_COL32((r->color >> 16) & 0xFF, (r->color >> 8) & 0xFF, r->color & 0xFF, 255);
    }
}

void ui_open_roles(snowflake guild_id)
{
    science::guild_settings_viewed("roles", guild_id);
    g_ui.roles_guild = guild_id;
    g_ui.roles_selected = 0;
    g_ui.open_roles_popup = true;
    ccfset(g_ui.role_new_name, 0, sizeof(g_ui.role_new_name));
}

void ui_view_roles_popup()
{
    if (g_ui.open_roles_popup)
    {
        ImGui::OpenPopup("##roles");
        g_ui.open_roles_popup = false;
    }

    ImGui::SetNextWindowSize(ImVec2(740, 560), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("##roles", 0, ImGuiWindowFlags_NoTitleBar)) return;

    store::guard guard;
    dguild* g = store::find_guild(g_ui.roles_guild);

    if (!g)
    {
        ui_text_muted(tr("Сервер недоступен"));
        if (ImGui::Button(tr("Закрыть"), ImVec2(120, 30))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    ImGui::TextUnformatted(tr("Роли"));
    ImGui::SameLine();
    ui_text_muted(g->name ? g->name : "");
    ImGui::Separator();

    // ---- the list -------------------------------------------------------
    ImGui::BeginChild("##rolelist", ImVec2(240, 400), true);

    for (unsigned int i = 0; i < g->roles.count; i++)
    {
        drole* r = &g->roles[i];

        // The baseline role carries the guild's own id. It is not something
        // anybody is given or taken away, so it is not offered as one.
        if (r->id == g->id) continue;

        ImGui::PushID((int)(r->id & 0x7FFFFFFF));
        ImGui::PushStyleColor(ImGuiCol_Text, role_tint(r));

        bool picked = g_ui.roles_selected == r->id;
        if (ImGui::Selectable(r->name ? r->name : tr("без имени"), picked)) load_role(r);

        ImGui::PopStyleColor();
        ImGui::PopID();
    }

    ImGui::EndChild();
    ImGui::SameLine();

    // ---- the editor -----------------------------------------------------
    ImGui::BeginChild("##roleedit", ImVec2(460, 400), true);

    drole* sel = find_role(g, g_ui.roles_selected);
    if (!sel)
    {
        ui_text_muted(tr("Выберите роль слева"));
    }
    else
    {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##rname", g_ui.role_name, sizeof(g_ui.role_name));

        // Six hex digits, which is how discord writes a colour and how anybody
        // who wants a particular shade already has it written down.
        {
            char hex[8];
            cnprint(hex, sizeof(hex), "%06X", g_ui.role_color & 0xFFFFFF);

            ImGui::SetNextItemWidth(110);
            if (ImGui::InputText(tr("Цвет (RRGGBB)"), hex, sizeof(hex),
                                 ImGuiInputTextFlags_CharsHexadecimal))
                g_ui.role_color = (unsigned int)ccstrthi(hex) & 0xFFFFFF;

            ImGui::SameLine();
            ImVec2 at = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddRectFilled(
                at, ImVec2(at.x + 22.0f, at.y + 18.0f),
                IM_COL32((g_ui.role_color >> 16) & 0xFF, (g_ui.role_color >> 8) & 0xFF,
                         g_ui.role_color & 0xFF, 255), 3.0f);
            ImGui::Dummy(ImVec2(22, 18));
        }

        ImGui::Checkbox(tr("Отдельной группой в списке участников"), &g_ui.role_hoist);
        ImGui::Checkbox(tr("Разрешить упоминание"), &g_ui.role_mentionable);

        ImGui::Separator();
        ImGui::TextUnformatted(tr("Права"));

        ImGui::BeginChild("##perms", ImVec2(0, 180), true);
        for (int i = 0; i < PERM_COUNT; i++)
        {
            bool on = (g_ui.role_perms & PERMS[i].bit) != 0;
            if (ImGui::Checkbox(tr(PERMS[i].label), &on))
            {
                if (on) g_ui.role_perms |= PERMS[i].bit;
                else    g_ui.role_perms &= ~PERMS[i].bit;
            }
        }
        ImGui::EndChild();

        // Bits outside the list are kept rather than dropped: a role set up in
        // the real client carries permissions this panel does not name, and
        // saving here must not quietly strip them.
        ui_text_muted(tr("Права, которых нет в списке, сохраняются как были"));

        if (ImGui::Button(tr("Сохранить"), ImVec2(130, 30)))
            api::edit_role(g->id, sel->id, g_ui.role_name, g_ui.role_perms,
                           g_ui.role_color, g_ui.role_hoist, g_ui.role_mentionable);

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, col::red);
        if (ImGui::Button(tr("Удалить роль"), ImVec2(130, 30)))
        {
            api::delete_role(g->id, sel->id);
            g_ui.roles_selected = 0;
        }
        ImGui::PopStyleColor();
    }

    ImGui::EndChild();

    // ---- making one -----------------------------------------------------
    ImGui::SetNextItemWidth(240);
    ImGui::InputTextWithHint("##newrole", tr("название новой роли"),
                             g_ui.role_new_name, sizeof(g_ui.role_new_name));
    ImGui::SameLine();
    if (ImGui::Button(tr("Создать"), ImVec2(110, 26)) && g_ui.role_new_name[0])
    {
        api::create_role(g->id, g_ui.role_new_name);
        ccfset(g_ui.role_new_name, 0, sizeof(g_ui.role_new_name));
    }

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

// The other half: handing one role to one person, or taking it back. A submenu
// wherever a member is already right-clickable, because that is where somebody
// is standing when they decide to do it.
void ui_member_roles_menu(snowflake guild_id, snowflake user_id)
{
    store::guard guard;
    dguild* g = store::find_guild(guild_id);
    if (!g || !g->roles.count) return;

    dmember* m = 0;
    for (unsigned int i = 0; i < g->members.count; i++)
        if (g->members[i].user_id == user_id) { m = &g->members[i]; break; }

    if (!ImGui::BeginMenu(tr("Роли"))) return;

    for (unsigned int i = 0; i < g->roles.count; i++)
    {
        drole* r = &g->roles[i];
        if (r->id == g->id) continue;

        bool has = false;
        if (m)
            for (unsigned int k = 0; k < m->roles.count; k++)
                if (m->roles[k] == r->id) { has = true; break; }

        ImGui::PushID((int)(r->id & 0x7FFFFFFF));
        ImGui::PushStyleColor(ImGuiCol_Text, role_tint(r));

        if (ImGui::MenuItem(r->name ? r->name : tr("без имени"), 0, has))
        {
            if (has) api::remove_member_role(guild_id, user_id, r->id);
            else     api::add_member_role(guild_id, user_id, r->id);
        }

        ImGui::PopStyleColor();
        ImGui::PopID();
    }

    ImGui::EndMenu();
}
