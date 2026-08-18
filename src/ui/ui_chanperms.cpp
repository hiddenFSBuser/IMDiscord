#include "pch.h"
#include "ui_state.h"
#include "theme.h"

#include "discord/store.h"
#include "discord/rest.h"

// Channel permissions.
//
// Every permission here has three states, not two: allowed, denied, or left to
// whatever the server-wide role says. Two masks carry that - a bit set in
// neither is inherited - and a checkbox cannot express it, which is why these
// are three buttons per row rather than one switch.

namespace
{
    struct perm_row
    {
        unsigned long long bit;
        const char* label;
    };

    // Only what can actually be overridden on a channel. Server-wide powers
    // like Manage Server have no meaning here and listing them would invite
    // setting something that does nothing.
    const perm_row PERMS[] = {
        { 1ULL << 10, "Видеть канал" },
        { 1ULL << 11, "Писать сообщения" },
        { 1ULL << 16, "Читать историю" },
        { 1ULL << 13, "Удалять чужие сообщения" },
        { 1ULL << 14, "Встраивать ссылки" },
        { 1ULL << 15, "Прикреплять файлы" },
        { 1ULL << 17, "Упоминать everyone" },
        { 1ULL << 6,  "Добавлять реакции" },
        { 1ULL << 0,  "Создавать приглашения" },
        { 1ULL << 4,  "Управлять каналом" },
        { 1ULL << 20, "Подключаться" },
        { 1ULL << 21, "Говорить" },
        { 1ULL << 9,  "Демонстрация экрана" },
        { 1ULL << 22, "Выключать микрофон другим" },
        { 1ULL << 23, "Глушить других" },
        { 1ULL << 24, "Перемещать между каналами" },
    };

    const int PERM_COUNT = (int)(sizeof(PERMS) / sizeof(PERMS[0]));

    // The masks being edited, loaded when a target is picked so that typing is
    // not overwritten by what is stored every frame.
    unsigned long long g_allow = 0;
    unsigned long long g_deny = 0;

    const doverwrite* find_overwrite(const dchannel* c, snowflake target)
    {
        for (unsigned int i = 0; i < c->overwrites.count; i++)
            if (c->overwrites[i].id == target) return &c->overwrites[i];
        return 0;
    }

    void load_target(const dchannel* c, snowflake target)
    {
        g_ui.chanperm_target = target;
        g_allow = 0;
        g_deny = 0;

        const doverwrite* o = find_overwrite(c, target);
        if (o) { g_allow = o->allow; g_deny = o->deny; }
    }

    // Three exclusive buttons. Neither pressed is the third state, which is
    // what "inherit" is - there is nothing to store for it.
    void tri_state(const perm_row* p)
    {
        bool allowed = (g_allow & p->bit) != 0;
        bool denied = (g_deny & p->bit) != 0;

        ImGui::PushID((int)(p->bit ? (unsigned int)(p->bit % 1000000007ULL) : 0));

        ImU32 off = col::bg_panel;

        ImGui::PushStyleColor(ImGuiCol_Button, denied ? col::red : off);
        if (ImGui::Button("✕", ImVec2(26, 22)))
        {
            g_deny = denied ? (g_deny & ~p->bit) : (g_deny | p->bit);
            if (!denied) g_allow &= ~p->bit;
        }
        ImGui::PopStyleColor();

        ImGui::SameLine(0, 3);
        ImGui::PushStyleColor(ImGuiCol_Button, (!allowed && !denied) ? col::accent : off);
        if (ImGui::Button("/", ImVec2(26, 22)))
        {
            g_allow &= ~p->bit;
            g_deny &= ~p->bit;
        }
        ImGui::PopStyleColor();

        ImGui::SameLine(0, 3);
        ImGui::PushStyleColor(ImGuiCol_Button, allowed ? col::green : off);
        if (ImGui::Button("✓", ImVec2(26, 22)))
        {
            g_allow = allowed ? (g_allow & ~p->bit) : (g_allow | p->bit);
            if (!allowed) g_deny &= ~p->bit;
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();
        ImGui::TextUnformatted(tr(p->label));
        ImGui::PopID();
    }
}

void ui_open_channel_perms(snowflake channel_id)
{
    g_ui.chanperm_channel = channel_id;
    g_ui.chanperm_target = 0;
    g_allow = 0;
    g_deny = 0;
    g_ui.open_chanperm_popup = true;
    api::clear_last_error();
}

void ui_view_channel_perms_popup()
{
    if (g_ui.open_chanperm_popup)
    {
        ImGui::OpenPopup("##chanperms");
        g_ui.open_chanperm_popup = false;
    }

    ImGui::SetNextWindowSize(ImVec2(760, 560), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("##chanperms", 0, ImGuiWindowFlags_NoTitleBar)) return;

    store::guard guard;
    dchannel* c = store::find_channel(g_ui.chanperm_channel);
    dguild* g = c ? store::find_guild(c->guild_id) : 0;

    if (!c || !g)
    {
        ui_text_muted(tr("Канал недоступен"));
        if (ImGui::Button(tr("Закрыть"), ImVec2(120, 30))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    {
        char title[192];
        ui_channel_display_name(c, title, sizeof(title));
        ImGui::TextUnformatted(tr("Права канала"));
        ImGui::SameLine();
        ui_text_muted(title);
    }
    ImGui::Separator();

    // ---- who ------------------------------------------------------------
    ImGui::BeginChild("##targets", ImVec2(220, 400), true);

    for (unsigned int i = 0; i < g->roles.count; i++)
    {
        drole* r = &g->roles[i];

        ImGui::PushID((int)(r->id & 0x7FFFFFFF));

        // The everyone role is listed here, unlike in the role editor: it is
        // exactly what a channel is usually closed off against.
        const char* name = (r->id == g->id) ? "@everyone"
                                            : (r->name ? r->name : tr("без имени"));

        ImU32 tint = col::text_normal;
        if (r->color && r->id != g->id)
            tint = IM_COL32((r->color >> 16) & 0xFF, (r->color >> 8) & 0xFF, r->color & 0xFF, 255);

        ImGui::PushStyleColor(ImGuiCol_Text, tint);

        bool picked = g_ui.chanperm_target == r->id;
        if (ImGui::Selectable(name, picked)) load_target(c, r->id);

        // A dot for a role this channel already says something about, so the
        // ones that matter are findable in a list of forty.
        if (find_overwrite(c, r->id))
        {
            ImGui::SameLine();
            ui_text_muted("•");
        }

        ImGui::PopStyleColor();
        ImGui::PopID();
    }

    ImGui::EndChild();
    ImGui::SameLine();

    // ---- what -----------------------------------------------------------
    ImGui::BeginChild("##bits", ImVec2(500, 400), true);

    if (!g_ui.chanperm_target)
    {
        ui_text_muted(tr("Выберите роль слева"));
    }
    else
    {
        ui_text_muted(tr("✕ запретить   /  наследовать   ✓ разрешить"));
        ImGui::Separator();

        for (int i = 0; i < PERM_COUNT; i++) tri_state(&PERMS[i]);
    }

    ImGui::EndChild();

    // ---- saving ----------------------------------------------------------
    if (g_ui.chanperm_target)
    {
        if (ImGui::Button(tr("Сохранить"), ImVec2(130, 30)))
            api::set_channel_overwrite(c->id, g_ui.chanperm_target, true, g_allow, g_deny);

        ImGui::SameLine();
        if (ImGui::Button(tr("Сбросить всё"), ImVec2(150, 30)))
        {
            // Not the same as saving two empty masks: this removes the entry,
            // which is what puts the role back to inheriting.
            api::clear_channel_overwrite(c->id, g_ui.chanperm_target);
            g_allow = 0;
            g_deny = 0;
        }
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
