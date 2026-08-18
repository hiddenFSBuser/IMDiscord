#include "pch.h"
#include "ui_state.h"
#include "ufile.h"
#include "video/capture.h"
#include "video/censor.h"
#include "audio/loopback.h"
#include "video/screenshare.h"
#include "theme.h"
#include "textures.h"

#include "core/app.h"
#include "discord/store.h"
#include "core/offline.h"
#include "system/io/ufile.h"
#include "discord/rest.h"
#include "discord/science.h"
#include "discord/gateway.h"
#include "discord/voice.h"

namespace
{
    enum friends_tab
    {
        TAB_ALL = 0,
        TAB_ONLINE,
        TAB_PENDING,
        TAB_BLOCKED,
        TAB_ADD,
    };

    int g_friends_tab = TAB_ALL;

    bool relationship_matches(int type, unsigned char status, int tab)
    {
        switch (tab)
        {
        case TAB_ALL:     return type == REL_FRIEND;
        case TAB_ONLINE:  return type == REL_FRIEND && status != STATUS_OFFLINE;
        case TAB_PENDING: return type == REL_INCOMING || type == REL_OUTGOING;
        case TAB_BLOCKED: return type == REL_BLOCKED;
        default:          return false;
        }
    }

    void friend_row(const drelationship* rel, float width)
    {
        duser* u = store::find_user(rel->user_id);
        if (!u) return;

        ImGui::PushID((int)(rel->user_id & 0x7FFFFFFF));

        ImVec2 start = ImGui::GetCursorScreenPos();
        float row_h = 52.0f;

        // The row is one wide invisible button, and the real controls are put
        // on top of it afterwards by moving the cursor back. Without this the
        // row takes the click first - it is submitted first, so it becomes the
        // active item on mouse down and the buttons behind it never see the
        // press. That is why nothing in this list could be clicked.
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("##row", ImVec2(width - 40.0f, row_h));
        bool hovered = ImGui::IsItemHovered();

        if (ImGui::BeginPopupContextItem("##relctx"))
        {
            if (ImGui::MenuItem(tr("Открыть профиль"))) ui_open_profile(u->id, 0);
            ImGui::Separator();
            ui_copy_id_item(u->id, tr("Скопировать ID пользователя"));
            ImGui::EndPopup();
        }

        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (hovered)
            dl->AddRectFilled(start, ImVec2(start.x + width - 40.0f, start.y + row_h), col::bg_hover, 6.0f);

        ImGui::SetCursorScreenPos(ImVec2(start.x + 8.0f, start.y + 8.0f));
        ui_avatar(u, 36.0f, true);
        if (ImGui::IsItemClicked()) ui_open_profile(u->id, 0);

        dl->AddText(ImVec2(start.x + 54.0f, start.y + 10.0f), col::text_normal, u->display_name());

        const char* sub = "";
        if (rel->type == REL_INCOMING) sub = tr("входящая заявка в друзья");
        else if (rel->type == REL_OUTGOING) sub = tr("исходящая заявка");
        else if (rel->type == REL_BLOCKED) sub = tr("заблокирован");
        else if (u->username) sub = u->username;
        dl->AddText(ImVec2(start.x + 54.0f, start.y + 28.0f), col::text_muted, sub);

        float bx = start.x + width - 60.0f;
        ImGui::SetCursorScreenPos(ImVec2(bx - 200.0f, start.y + 12.0f));

        if (rel->type == REL_INCOMING)
        {
            if (ui_icon_button(tr("Принять##acc"), ImVec2(96, 28), col::green, col::green))
                api::accept_friend_request(u->id);
            ImGui::SameLine(0, 6);
            if (ui_icon_button(tr("Отклонить##dec"), ImVec2(96, 28), col::red, col::red))
                api::remove_relationship(u->id);
        }
        else if (rel->type == REL_OUTGOING)
        {
            ImGui::SetCursorScreenPos(ImVec2(bx - 100.0f, start.y + 12.0f));
            if (ui_icon_button(tr("Отменить##cancel"), ImVec2(96, 28), col::bg_panel, col::red))
                api::remove_relationship(u->id);
        }
        else if (rel->type == REL_BLOCKED)
        {
            ImGui::SetCursorScreenPos(ImVec2(bx - 100.0f, start.y + 12.0f));
            if (ui_icon_button(tr("Разблокировать##unb"), ImVec2(96, 28), col::bg_panel, col::bg_hover))
                api::remove_relationship(u->id);
        }
        else
        {
            if (ui_icon_button(tr("Написать##msg"), ImVec2(96, 28), col::accent, col::accent_hover))
            {
                api::open_dm(u->id);
                g_ui.show_friends = false;
                g_ui.active_guild = 0;
            }
            ImGui::SameLine(0, 6);
            if (ui_icon_button(tr("Удалить##rem"), ImVec2(96, 28), col::bg_panel, col::red))
                api::remove_relationship(u->id);
        }

        ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + row_h + 2.0f));
        ImGui::PopID();
    }
}

// ---------------------------------------------------------------------------
// friends
// ---------------------------------------------------------------------------

void ui_view_friends(float width, float height)
{
    // Arriving at the screen, as distinct from moving between its tabs.
    // Reported once per arrival rather than once per frame.
    {
        // A gap in the frames this drew on means the screen was closed and
        // opened again. A plain "have we ever" flag would report the first
        // arrival of the whole run and nothing after it.
        static int last_frame = -10;
        int now = ImGui::GetFrameCount();

        if (now - last_frame > 1)
        {
            static const char* NAMES[] = { "ALL", "ONLINE", "PENDING",
                                           "BLOCKED", "ADD_FRIEND" };
            science::friends_list_viewed(NAMES[g_friends_tab]);
        }
        last_frame = now;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetWindowPos();
    dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), col::bg_chat);

    ImGui::SetCursorPos(ImVec2(16, 12));
    ImGui::PushFont(g_app.font_bold);
    ImGui::TextUnformatted(tr("Друзья"));
    ImGui::PopFont();

    ImGui::SetCursorPos(ImVec2(16, 44));

    struct { const char* label; int tab; } tabs[] = {
        { tr("Все"), TAB_ALL },
        { tr("В сети"), TAB_ONLINE },
        { tr("Ожидают"), TAB_PENDING },
        { tr("Заблокированные"), TAB_BLOCKED },
        { tr("Добавить"), TAB_ADD },
    };

    for (int i = 0; i < 5; i++)
    {
        if (i) ImGui::SameLine(0, 6);
        bool active = (g_friends_tab == tabs[i].tab);
        if (ui_icon_button(tabs[i].label, ImVec2(0, 28),
                           active ? col::accent : col::bg_panel,
                           active ? col::accent_hover : col::bg_hover))
        {
            g_friends_tab = tabs[i].tab;

            // Reported the way the official client reports it. The names are
            // discord's own, and the add-friend one is the one that matters.
            static const char* NAMES[] = { "ALL", "ONLINE", "PENDING",
                                           "BLOCKED", "ADD_FRIEND" };
            science::friends_list_clicked(NAMES[tabs[i].tab]);

            // Discord will not show blocked accounts in the list itself; it
            // sends people to a settings screen for them. Opening that tab
            // here is the same intent, so it is reported the same way - the
            // notice, the badge and the visit, exactly as arriving at that
            // screen produces them.
            if (tabs[i].tab == TAB_BLOCKED) science::blocked_settings_viewed();
        }
    }

    ImGui::SetCursorPos(ImVec2(8, 86));
    ImGui::BeginChild("##friendlist", ImVec2(width - 16.0f, height - 96.0f), false);

    if (g_friends_tab == TAB_ADD)
    {
        ImGui::Indent(8.0f);
        ImGui::Dummy(ImVec2(0, 8));
        ImGui::TextUnformatted(tr("Введите имя пользователя"));
        ui_text_muted(tr("Новый формат: username. Старый: username#1234"));
        ImGui::Dummy(ImVec2(0, 6));

        ImGui::SetNextItemWidth(340);
        bool go = ImGui::InputText("##friendname", g_ui.friend_input, sizeof(g_ui.friend_input),
                                   ImGuiInputTextFlags_EnterReturnsTrue);

        // The field being focused is its own step in the sequence the official
        // client sends before a request goes out.
        if (ImGui::IsItemActivated()) science::add_friend_input_clicked();
        ImGui::SameLine();
        if (ImGui::Button(tr("Отправить заявку"), ImVec2(180, 0)) || go)
        {
            if (g_ui.friend_input[0])
            {
                api::clear_captcha();
                ccstrncpy(g_ui.captcha_for, g_ui.friend_input,
                          sizeof(g_ui.captcha_for) - 1);
                api::send_friend_request(g_ui.friend_input);
            }
        }

        // Discord refuses a request it finds unfamiliar until a captcha has
        // been answered. Solving one is not something this client does or
        // should do - it is put in front of the person, and whatever they come
        // back with is carried on the retry and nothing else.
        if (api::captcha_sitekey()[0])
        {
            ImGui::Dummy(ImVec2(0, 10));
            ImGui::PushStyleColor(ImGuiCol_Text, col::yellow);
            ImGui::TextWrapped(tr("Discord просит пройти CAPTCHA для заявки к %s"),
                               g_ui.captcha_for);
            ImGui::PopStyleColor();

            ui_text_muted(tr("Проще всего отправить эту заявку один раз из браузера. "
                          "Если у вас есть готовый токен - вставьте его сюда."));

            if (ImGui::Button(tr("Открыть Discord в браузере"), ImVec2(250, 0)))
                ShellExecuteW(0, L"open", L"https://discord.com/channels/@me", 0, 0, SW_SHOWNORMAL);

            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##captcha", tr("токен капчи"),
                                     g_ui.captcha_token, sizeof(g_ui.captcha_token));

            if (ImGui::Button(tr("Повторить с токеном"), ImVec2(200, 0)) &&
                g_ui.captcha_token[0] && g_ui.captcha_for[0])
            {
                api::send_friend_request(g_ui.captcha_for, g_ui.captcha_token,
                                         api::captcha_rqtoken());
                ccfset(g_ui.captcha_token, 0, sizeof(g_ui.captcha_token));
            }

            ImGui::SameLine();
            if (ImGui::Button(tr("Отмена"), ImVec2(120, 0)))
            {
                api::clear_captcha();
                api::clear_last_error();
            }
        }

        if (api::last_error()[0])
        {
            ImGui::Dummy(ImVec2(0, 10));
            ImGui::PushStyleColor(ImGuiCol_Text, col::yellow);
            ImGui::TextWrapped("%s", api::last_error());
            ImGui::PopStyleColor();
        }
        ImGui::Unindent(8.0f);
    }
    else
    {
        const ulist<drelationship>& rels = store::relationships();
        int shown = 0;

        for (unsigned int i = 0; i < rels.count; i++)
        {
            duser* u = store::find_user(rels[i].user_id);
            unsigned char status = u ? u->status : (unsigned char)STATUS_OFFLINE;
            if (!relationship_matches(rels[i].type, status, g_friends_tab)) continue;

            friend_row(&rels[i], width - 16.0f);
            shown++;
        }

        if (!shown)
        {
            ImGui::Dummy(ImVec2(0, 20));
            ImGui::Indent(16.0f);
            ui_text_muted(tr("Здесь пока никого нет"));
            ImGui::Unindent(16.0f);
        }
    }

    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// guild member list
// ---------------------------------------------------------------------------

// Draws the little crossed-out microphone and headset that say somebody
// cannot be heard or cannot hear. Drawn rather than fetched: two glyphs are
// not worth an icon font, and these have to sit at the end of a row of text
// at whatever size the row happens to be.
void ui_draw_muted_marks(ImDrawList* dl, ImVec2 at, float size,
                                bool mic_off, bool ears_off)
{
    const ImU32 colour = col::red;
    float step = size + 4.0f;

    if (mic_off)
    {
        float cx = at.x + size * 0.5f;
        float top = at.y + size * 0.18f;
        float w = size * 0.22f;

        dl->AddRectFilled(ImVec2(cx - w, top), ImVec2(cx + w, top + size * 0.42f),
                          colour, w);
        dl->AddLine(ImVec2(cx, top + size * 0.52f), ImVec2(cx, at.y + size * 0.82f),
                    colour, 1.6f);
        dl->AddLine(ImVec2(cx - size * 0.28f, at.y + size * 0.82f),
                    ImVec2(cx + size * 0.28f, at.y + size * 0.82f), colour, 1.6f);
        dl->AddLine(ImVec2(at.x + size * 0.1f, at.y + size * 0.9f),
                    ImVec2(at.x + size * 0.9f, at.y + size * 0.1f), colour, 1.8f);
        at.x += step;
    }

    if (ears_off)
    {
        float cx = at.x + size * 0.5f;
        float cy = at.y + size * 0.55f;
        float r = size * 0.32f;

        dl->PathArcTo(ImVec2(cx, cy), r, 3.14159f, 6.28318f, 12);
        dl->PathStroke(colour, 0, 1.8f);
        dl->AddRectFilled(ImVec2(cx - r - 1.5f, cy - 1.0f),
                          ImVec2(cx - r + 2.5f, cy + r * 0.7f), colour, 1.5f);
        dl->AddRectFilled(ImVec2(cx + r - 2.5f, cy - 1.0f),
                          ImVec2(cx + r + 1.5f, cy + r * 0.7f), colour, 1.5f);
        dl->AddLine(ImVec2(at.x + size * 0.1f, at.y + size * 0.9f),
                    ImVec2(at.x + size * 0.9f, at.y + size * 0.1f), colour, 1.8f);
    }
}

namespace
{
    // A member's place in the list: which heading they sit under, and where
    // they sort inside it.
    struct member_slot
    {
        snowflake user_id;
        const drole* section;    // hoisted role, or null for the plain groups
        const drole* colour;
        const char* name;
        bool online;
    };

    bool name_matches(const char* name, const char* needle)
    {
        if (!needle[0]) return true;
        for (const char* p = name; *p; p++)
        {
            const char* a = p;
            const char* b = needle;
            while (*a && *b && cctolower(*a) == cctolower(*b)) { a++; b++; }
            if (!*b) return true;
        }
        return false;
    }
}

// The people in a group chat.
//
// A group has no roles, no permissions and no member chunks - it is a list of
// recipients and an owner - so none of the machinery below applies to it and
// it gets its own short function rather than a pile of branches inside that
// one. Who is in the call is drawn here too: a group is the one place where
// the call and the membership are the same small list of faces.
static void draw_group_members(dchannel* c, float width)
{
    ulist<snowflake> in_call;
    store::users_in_voice(c->id, &in_call);

    char header[96];
    cnprint(header, sizeof(header), tr("УЧАСТНИКИ - %u"), c->recipients.count + 1);

    ImGui::Indent(10.0f);
    ui_text_muted(header);
    ImGui::Unindent(10.0f);
    ImGui::Dummy(ImVec2(0, 4));

    // Ourselves first, then everybody else. Discord counts the owner as a
    // member of their own group and so does this.
    for (unsigned int pass = 0; pass < 2; pass++)
    {
        unsigned int count = pass == 0 ? 1 : c->recipients.count;

        for (unsigned int i = 0; i < count; i++)
        {
            duser* u = pass == 0 ? store::self() : store::find_user(c->recipients[i]);
            if (!u) continue;

            bool talking = false;
            for (unsigned int k = 0; k < in_call.count && !talking; k++)
                talking = in_call[k] == u->id;

            ImGui::PushID((int)(u->id & 0x7FFFFFFF));

            float row_w = width - 20.0f;
            ImVec2 start = ImGui::GetCursorScreenPos();

            ImGui::SetCursorScreenPos(ImVec2(start.x + 10.0f, start.y));
            ImGui::InvisibleButton("##gm", ImVec2(row_w, 34.0f));

            if (ImGui::IsItemHovered())
                ImGui::GetWindowDrawList()->AddRectFilled(
                    ImVec2(start.x + 10.0f, start.y),
                    ImVec2(start.x + 10.0f + row_w, start.y + 34.0f), col::bg_hover, 4.0f);

            if (ImGui::IsItemClicked()) ui_open_profile(u->id, 0);

            if (ImGui::BeginPopupContextItem("##gmctx"))
            {
                if (ImGui::MenuItem(tr("Открыть профиль"))) ui_open_profile(u->id, 0);
                ImGui::Separator();
                ui_copy_id_item(u->id, tr("Скопировать ID пользователя"));
                ImGui::EndPopup();
            }

            ImGui::SetCursorScreenPos(ImVec2(start.x + 12.0f, start.y + 3.0f));
            ui_avatar(u, 28.0f, true);

            ImU32 tint = u->status != STATUS_OFFLINE ? col::text_normal : col::text_muted;

            ImDrawList* d = ImGui::GetWindowDrawList();
            d->AddText(ImVec2(start.x + 48.0f, start.y + 9.0f), tint, u->display_name());

            // The owner of the group, and whoever is in the call right now.
            float mark_x = start.x + 10.0f + row_w - 8.0f;

            if (talking)
            {
                const char* label = tr("в звонке");
                float w = ImGui::CalcTextSize(label).x;
                mark_x -= w;
                d->AddText(ImVec2(mark_x, start.y + 9.0f), col::green, label);
                mark_x -= 8.0f;
            }

            if (c->owner_id && u->id == c->owner_id)
            {
                const char* label = tr("владелец");
                float w = ImGui::CalcTextSize(label).x;
                d->AddText(ImVec2(mark_x - w, start.y + 9.0f), col::text_muted, label);
            }

            ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + 34.0f));
            ImGui::PopID();
        }
    }
}

void ui_view_members(float width, float height)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetWindowPos();
    dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), col::bg_panel);

    dguild* g = store::find_guild(g_ui.active_guild);
    if (!g)
    {
        // Not on a server: a group chat has a member list of its own, and it
        // is the one thing this panel could usefully show there.
        dchannel* group = store::find_channel(g_ui.active_channel);
        if (group && group->type == CH_GROUP_DM)
        {
            ImGui::Dummy(ImVec2(0, 8));
            draw_group_members(group, width);
        }
        return;
    }

    // The list belongs to the channel being read, not to the server. Discord
    // shows the people who can see this channel, and on a server with private
    // sections those are very different lists.
    dchannel* channel = store::find_channel(g_ui.active_channel);
    if (channel && channel->guild_id != g->id) channel = 0;

    ImGui::Dummy(ImVec2(0, 8));
    ImGui::Indent(10.0f);

    ImGui::SetNextItemWidth(width - 24.0f);
    ImGui::InputTextWithHint("##memberfilter", tr("Поиск участника"), g_ui.member_filter, sizeof(g_ui.member_filter));

    if (ImGui::IsItemDeactivatedAfterEdit() && g_ui.member_filter[0])
        gateway::request_guild_members(g->id, g_ui.member_filter, 25);

    ImGui::Unindent(10.0f);
    ImGui::Dummy(ImVec2(0, 4));

    // Sort everybody into sections once, then draw the sections in order.
    static ulist<member_slot> slots;
    slots.clear_fast();

    for (unsigned int i = 0; i < g->members.count; i++)
    {
        dmember* m = &g->members[i];
        duser* u = store::find_user(m->user_id);
        if (!u) continue;

        if (!store::can_view_channel(g, m->user_id, channel)) continue;

        const char* name = m->nick ? m->nick : u->display_name();
        if (!name_matches(name, g_ui.member_filter)) continue;

        member_slot slot;
        slot.user_id = m->user_id;
        slot.name = name;
        slot.online = u->status != STATUS_OFFLINE;
        slot.colour = store::member_color_role(g, m);
        // Offline people drop out of their role section: discord collects
        // them all at the bottom regardless of rank.
        slot.section = slot.online ? store::member_hoist_role(g, m) : 0;
        slots.push(slot);
    }

    // Highest hoisted role first, then everyone else online, then offline.
    // Inside a section, by name.
    for (unsigned int i = 1; i < slots.count; i++)
    {
        member_slot moving = slots[i];
        unsigned int k = i;
        while (k > 0)
        {
            member_slot* prev = &slots[k - 1];

            int a_rank = moving.section ? moving.section->position : (moving.online ? -1 : -2);
            int b_rank = prev->section ? prev->section->position : (prev->online ? -1 : -2);

            bool before = false;
            if (a_rank != b_rank) before = a_rank > b_rank;
            else before = ccscmp(moving.name, prev->name) < 0;

            if (!before) break;
            slots[k] = slots[k - 1];
            k--;
        }
        slots[k] = moving;
    }

    char header[96];
    if (g->member_count > (int)g->members.count)
        cnprint(header, sizeof(header), tr("УЧАСТНИКИ - %u из %d"),
                slots.count, g->member_count);
    else
        cnprint(header, sizeof(header), tr("УЧАСТНИКИ - %u"), slots.count);

    ImGui::Indent(10.0f);
    ui_text_muted(header);
    if (channel && channel->name)
    {
        char sub[128];
        cnprint(sub, sizeof(sub), tr("с доступом к #%s"), channel->name);
        ui_text_muted(sub);
    }
    ImGui::Unindent(10.0f);

    ImGui::BeginChild("##memberscroll", ImVec2(width, height - 110.0f), false);
    ImGui::Indent(8.0f);

    const drole* drawn_section = 0;
    bool drawn_any_section = false;
    int drawn_group = -3;
    unsigned int drawn = 0;

    for (unsigned int i = 0; i < slots.count && drawn < 400; i++)
    {
        member_slot* slot = &slots[i];
        duser* u = store::find_user(slot->user_id);
        if (!u) continue;

        int group = slot->section ? 0 : (slot->online ? -1 : -2);
        if (group != drawn_group || slot->section != drawn_section || !drawn_any_section)
        {
            drawn_group = group;
            drawn_section = slot->section;
            drawn_any_section = true;

            // Count how far this run goes so the heading can carry a number,
            // the way every other client does it.
            unsigned int run = 0;
            for (unsigned int k = i; k < slots.count; k++)
            {
                int kg = slots[k].section ? 0 : (slots[k].online ? -1 : -2);
                if (kg != group || slots[k].section != slot->section) break;
                run++;
            }

            char title[96];
            if (slot->section)      cnprint(title, sizeof(title), "%s - %u", slot->section->name, run);
            else if (slot->online)  cnprint(title, sizeof(title), tr("В СЕТИ - %u"), run);
            else                    cnprint(title, sizeof(title), tr("НЕ В СЕТИ - %u"), run);

            ImGui::Dummy(ImVec2(0, 8));
            ui_text_muted(title);
            ImGui::Dummy(ImVec2(0, 2));
        }

        ImGui::PushID((int)(u->id & 0x7FFFFFFF));
        ImVec2 start = ImGui::GetCursorScreenPos();
        float row_w = width - 24.0f;

        ImGui::InvisibleButton("##m", ImVec2(row_w, 34.0f));
        if (ImGui::IsItemHovered())
            ImGui::GetWindowDrawList()->AddRectFilled(start, ImVec2(start.x + row_w, start.y + 34.0f),
                                                      col::bg_hover, 4.0f);
        if (ImGui::IsItemClicked()) ui_open_profile(u->id, g->id);

        // Attached to the row's own button, so the whole strip answers the
        // right button rather than just the name.
        if (ImGui::BeginPopupContextItem("##memctx"))
        {
            if (ImGui::MenuItem(tr("Открыть профиль"))) ui_open_profile(u->id, g->id);
            ui_member_roles_menu(g->id, u->id);
            ui_member_moderation_menu(g->id, u->id);
            ImGui::Separator();
            ui_copy_id_item(u->id, tr("Скопировать ID пользователя"));
            ui_copy_id_item(g->id, tr("Скопировать ID сервера"));
            ImGui::EndPopup();
        }

        ImGui::SetCursorScreenPos(ImVec2(start.x + 2.0f, start.y + 3.0f));
        ui_avatar(u, 28.0f, true);

        // A role colour is stored the way css writes one, 0xRRGGBB, and a
        // role with no colour set stores zero rather than black.
        ImU32 tint = slot->online ? col::text_normal : col::text_muted;
        if (slot->colour && slot->online)
        {
            unsigned int c = slot->colour->color;
            tint = IM_COL32((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, 255);
        }

        ImDrawList* rows = ImGui::GetWindowDrawList();
        rows->AddText(ImVec2(start.x + 38.0f, start.y + 9.0f), tint, slot->name);

        // Whoever is in a voice channel and cannot speak or cannot hear says
        // so here, the same as in the channel list.
        const dvoice_state* vs = store::find_voice_state(u->id);
        if (vs && vs->channel_id)
        {
            bool mic_off = vs->self_mute || vs->mute;
            bool ears_off = vs->self_deaf || vs->deaf;
            if (mic_off || ears_off)
            {
                float marks = (mic_off && ears_off) ? 34.0f : 16.0f;
                ui_draw_muted_marks(rows, ImVec2(start.x + row_w - marks, start.y + 10.0f),
                                    14.0f, mic_off, ears_off);
            }
        }

        ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + 34.0f));
        ImGui::PopID();
        drawn++;
    }

    if (!drawn) ui_text_muted(tr("Никого - откройте канал, чтобы загрузить"));

    ImGui::Unindent(8.0f);
    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// profile
// ---------------------------------------------------------------------------

void ui_view_profile_popup()
{
    if (g_ui.open_profile_popup)
    {
        ImGui::OpenPopup("##profile");
        g_ui.open_profile_popup = false;
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(420, 0));

    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGui::ColorConvertU32ToFloat4(col::bg_panel));
    if (!ImGui::BeginPopupModal("##profile", 0, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::PopStyleColor();
        return;
    }

    store::guard guard;
    duser* u = store::find_user(g_ui.profile_user);

    if (!u)
    {
        ui_text_muted(tr("Профиль загружается..."));
        if (ImGui::Button(tr("Закрыть"), ImVec2(120, 30))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        ImGui::PopStyleColor();
        return;
    }

    // Banner strip.
    char banner_url[320];
    cdn::user_banner(u, 512, banner_url, sizeof(banner_url));

    ImVec2 p = ImGui::GetCursorScreenPos();
    float banner_h = 96.0f;
    float banner_w = 388.0f;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    bool drew_banner = false;
    if (banner_url[0])
    {
        const texture* t = tex::get(banner_url);
        if (t->ready())
        {
            dl->AddImageRounded(t->id(), p, ImVec2(p.x + banner_w, p.y + banner_h),
                                ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 8.0f);
            drew_banner = true;

            // Clicking it opens the real thing rather than this strip. The
            // one on screen is a 512 pixel wide crop; the viewer asks the cdn
            // for the largest it will give.
            ImVec2 back = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(p);
            if (ImGui::InvisibleButton("##bannerfull", ImVec2(banner_w, banner_h)))
            {
                char full[320];
                cdn::user_banner(u, 2048, full, sizeof(full));

                char name[160];
                cnprint(name, sizeof(name), "%s-banner.png",
                        u->username ? u->username : "user");
                ui_open_image_viewer(full, name);
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                ImGui::SetTooltip(tr("Открыть баннер целиком"));
            }
            ImGui::SetCursorScreenPos(back);
        }
    }
    if (!drew_banner)
    {
        ImU32 c = u->accent_color ? IM_COL32((u->accent_color >> 16) & 0xFF,
                                             (u->accent_color >> 8) & 0xFF,
                                             u->accent_color & 0xFF, 255)
                                  : col::accent;
        dl->AddRectFilled(p, ImVec2(p.x + banner_w, p.y + banner_h), c, 8.0f);
    }
    ImGui::Dummy(ImVec2(banner_w, banner_h - 24.0f));

    ImGui::Indent(14.0f);
    {
        ImVec2 at = ImGui::GetCursorScreenPos();
        ui_avatar(u, 76.0f, true);

        ImGui::SetCursorScreenPos(at);
        if (ImGui::InvisibleButton("##avatarfull", ImVec2(76.0f, 76.0f)))
        {
            // 4096 is as large as the cdn goes, and it hands back whatever
            // the original was if it is smaller. Animated avatars keep their
            // gif form, so one opens as an animation rather than a still.
            char full[320];
            cdn::user_avatar(u, 4096, full, sizeof(full));

            char name[160];
            cnprint(name, sizeof(name), "%s-avatar.png",
                    u->username ? u->username : "user");
            ui_open_image_viewer(full, name);
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::SetTooltip(tr("Открыть аватарку целиком"));
        }
    }
    ImGui::Dummy(ImVec2(0, 4));

    ImGui::PushFont(g_app.font_big);
    ImGui::TextUnformatted(u->display_name());
    ImGui::PopFont();

    if (u->username)
    {
        char handle[160];
        if (u->discriminator && u->discriminator[0] && ccscmp(u->discriminator, "0") != 0)
            cnprint(handle, sizeof(handle), "%s#%s", u->username, u->discriminator);
        else
            cnprint(handle, sizeof(handle), "%s", u->username);

        // A field rather than a label, so the name can be selected and copied
        // out. It is the username, not the display name above it, and it is
        // the one thing on this card somebody actually needs to hand to
        // something else - retyping it by hand is how a typo becomes a bug
        // report about friend requests.
        ImGui::SetNextItemWidth(240);
        ImGui::InputText("##handle", handle, sizeof(handle), ImGuiInputTextFlags_ReadOnly);

        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Копировать##handle"))) ImGui::SetClipboardText(handle);
    }

    char id_text[48];
    cnprint(id_text, sizeof(id_text), "ID: %llu", u->id);
    ui_text_muted(id_text);
    ImGui::SameLine();
    if (ImGui::SmallButton(tr("Копировать##uid"))) ui_copy_id(u->id);

    if (u->bot)
    {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, col::accent);
        ImGui::TextUnformatted("BOT");
        ImGui::PopStyleColor();
    }

    if (u->bio && u->bio[0])
    {
        ImGui::Dummy(ImVec2(0, 8));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::PushTextWrapPos(380.0f);
        ImGui::TextWrapped("%s", u->bio);
        ImGui::PopTextWrapPos();
    }

    ImGui::Dummy(ImVec2(0, 12));

    int rel = store::relationship_type(u->id);
    bool is_self = (u->id == store::self_id());

    // Editing, but only ever of one's own profile. The fields open filled with
    // what is there now, so a change is an edit rather than a retype.
    if (is_self)
    {
        static bool editing = false;
        static snowflake editing_who = 0;
        static char edit_name[128];
        static char edit_bio[1024];

        if (editing && editing_who != u->id) editing = false;

        if (!editing)
        {
            if (ImGui::Button(tr("Изменить профиль"), ImVec2(-1, 30)))
            {
                editing = true;
                editing_who = u->id;

                ccfset(edit_name, 0, sizeof(edit_name));
                ccfset(edit_bio, 0, sizeof(edit_bio));
                ccstrncpy(edit_name, u->display_name(), sizeof(edit_name) - 1);
                if (u->bio) ccstrncpy(edit_bio, u->bio, sizeof(edit_bio) - 1);
            }

            if (offline::active())
                ui_text_muted(tr("Пока нет связи, изменения не уйдут"));
        }
        else
        {
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 4));

            ui_text_muted(tr("Отображаемое имя"));
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##editname", edit_name, sizeof(edit_name));

            ui_text_muted(tr("О себе"));
            ImGui::InputTextMultiline("##editbio", edit_bio, sizeof(edit_bio),
                                      ImVec2(-1, 90));

            ImGui::Dummy(ImVec2(0, 4));

            bool blocked = offline::active();
            if (blocked) ui_text_muted(tr("Нет связи — сохранить нельзя"));

            if (!blocked && ImGui::Button(tr("Сохранить"), ImVec2(160, 30)))
            {
                // The name is only sent when it actually changed: discord
                // rejects a display name equal to the username, and sending
                // the same value back is exactly how that happens by accident.
                const char* current = u->display_name();
                bool name_changed = ccscmp(edit_name, current) != 0;

                api::update_self_profile(name_changed ? edit_name : 0, edit_bio);
                editing = false;
            }

            if (!blocked) ImGui::SameLine();
            if (ImGui::Button(tr("Отмена"), ImVec2(160, 30))) editing = false;

            if (!blocked)
            {
                ImGui::Dummy(ImVec2(0, 6));
                ui_text_muted(tr("Картинки заменяются сразу, отдельно от имени"));

                if (ImGui::Button(tr("Аватарка..."), ImVec2(160, 26)))
                {
                    wchar_t chosen[MAX_PATH];
                    if (ufile::open_dialog(chosen, MAX_PATH))
                        api::update_self_image(false, chosen);
                }
                ImGui::SameLine();
                if (ImGui::Button(tr("Баннер..."), ImVec2(160, 26)))
                {
                    wchar_t chosen[MAX_PATH];
                    if (ufile::open_dialog(chosen, MAX_PATH))
                        api::update_self_image(true, chosen);
                }

                if (ImGui::SmallButton(tr("убрать аватарку"))) api::update_self_image(false, 0);
                ImGui::SameLine();
                if (ImGui::SmallButton(tr("убрать баннер"))) api::update_self_image(true, 0);
            }
        }

        ImGui::Dummy(ImVec2(0, 6));
    }

    if (!is_self)
    {
        if (ImGui::Button(tr("Написать"), ImVec2(120, 32)))
        {
            api::open_dm(u->id);
            g_ui.show_friends = false;
            g_ui.active_guild = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();

        if (rel == REL_FRIEND)
        {
            if (ui_icon_button(tr("Удалить из друзей"), ImVec2(170, 32), col::bg_input, col::red))
                api::remove_relationship(u->id);
        }
        else if (rel == REL_INCOMING)
        {
            if (ui_icon_button(tr("Принять заявку"), ImVec2(150, 32), col::green, col::green))
                api::accept_friend_request(u->id);
            ImGui::SameLine();
            if (ui_icon_button(tr("Отклонить"), ImVec2(110, 32), col::bg_input, col::red))
                api::remove_relationship(u->id);
        }
        else if (rel == REL_OUTGOING)
        {
            if (ui_icon_button(tr("Отменить заявку"), ImVec2(170, 32), col::bg_input, col::red))
                api::remove_relationship(u->id);
        }
        else if (rel == REL_BLOCKED)
        {
            if (ui_icon_button(tr("Разблокировать"), ImVec2(170, 32), col::bg_input, col::bg_hover))
                api::remove_relationship(u->id);
        }
        else
        {
            if (ui_icon_button(tr("Добавить в друзья"), ImVec2(170, 32), col::green, col::green))
            {
                char handle[160];
                if (u->discriminator && u->discriminator[0] && ccscmp(u->discriminator, "0") != 0)
                    cnprint(handle, sizeof(handle), "%s#%s", u->username, u->discriminator);
                else
                    cnprint(handle, sizeof(handle), "%s", u->username ? u->username : "");
                api::send_friend_request(handle);
            }
        }

        if (rel != REL_BLOCKED)
        {
            if (ui_icon_button(tr("Заблокировать"), ImVec2(150, 28), col::bg_input, col::red))
                api::block_user(u->id);
        }
    }

    if (api::last_error()[0])
    {
        ImGui::Dummy(ImVec2(0, 6));
        ImGui::PushStyleColor(ImGuiCol_Text, col::yellow);
        ImGui::PushTextWrapPos(380.0f);
        ImGui::TextWrapped("%s", api::last_error());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }

    ImGui::Dummy(ImVec2(0, 8));
    if (ImGui::Button(tr("Закрыть"), ImVec2(120, 30)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        api::clear_last_error();
        ImGui::CloseCurrentPopup();
    }

    // The profile is where somebody looks a person up, so the id is offered
    // here too rather than only from the row that opened it.
    ImGui::SameLine();
    if (ImGui::Button(tr("Скопировать ID"), ImVec2(150, 30))) ui_copy_id(u->id);

    ImGui::Unindent(14.0f);
    ImGui::EndPopup();
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------------------
// server info
// ---------------------------------------------------------------------------

namespace
{
    // One labelled value in the three-across grid.
    void info_cell(const char* label, const char* value, float column)
    {
        ImGui::BeginGroup();
        ui_text_muted(label);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + column);
        ImGui::TextUnformatted(value && value[0] ? value : "-");
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
    }

    const char* verification_name(int level)
    {
        switch (level)
        {
        case 0:  return tr("Нет");
        case 1:  return tr("Низкий");
        case 2:  return tr("Средний");
        case 3:  return tr("Высокий");
        case 4:  return tr("Очень высокий");
        default: return "-";
        }
    }
}

void ui_view_server_info_popup()
{
    if (g_ui.open_server_info_popup)
    {
        ImGui::OpenPopup("##serverinfo");
        g_ui.open_server_info_popup = false;
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f,
                                   vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(620, 0));

    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGui::ColorConvertU32ToFloat4(col::bg_panel));
    if (!ImGui::BeginPopupModal("##serverinfo", 0,
                                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::PopStyleColor();
        return;
    }

    store::guard guard;
    dguild* g = store::find_guild(g_ui.server_info_guild);

    if (!g)
    {
        ui_text_muted(tr("Сервер не найден"));
        if (ImGui::Button(tr("Закрыть"), ImVec2(120, 30))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        ImGui::PopStyleColor();
        return;
    }

    ImGui::Dummy(ImVec2(0, 4));
    ImGui::BeginGroup();
    {
        ImVec2 bubble_pos = ImGui::GetCursorScreenPos();
        ui_guild_bubble(g, 48.0f, false);
        if (g->icon && g->icon[0])
        {
            ImGui::SetCursorScreenPos(bubble_pos);
            if (ImGui::InvisibleButton("##guildicon", ImVec2(48.0f, 48.0f)))
            {
                char big[320];
                cdn::guild_icon(g, 1024, big, sizeof(big));
                ui_open_image_viewer(big, g->name ? g->name : tr("Сервер"));
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(tr("Приблизить аватарку"));
        }
    }
    ImGui::EndGroup();
    ImGui::SameLine(0, 12);
    ImGui::BeginGroup();
    ImGui::TextUnformatted(g->name ? g->name : tr("Сервер"));
    if (g->description) ui_text_muted(g->description);
    ImGui::EndGroup();

    ImGui::Dummy(ImVec2(0, 10));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 8));

    const float COLUMN = 180.0f;

    // Row one: the two that matter, plus when this account arrived.
    ImGui::BeginGroup();
    ui_text_muted(tr("Создатель сервера"));
    {
        duser* owner = store::find_user(g->owner_id);

        // The owner is usually somebody this client has never seen speak, and
        // waiting for the whole member list to arrive before naming them is
        // waiting for something that may never finish on a big server. Ask
        // for that one person directly instead.
        if (!owner && g->owner_id)
        {
            static snowflake asked_for = 0;
            if (asked_for != g->owner_id)
            {
                asked_for = g->owner_id;
                api::fetch_user_profile(g->owner_id, g->id);
            }
        }
        if (owner)
        {
            ImVec2 at = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##ownerrow", ImVec2(COLUMN, 24.0f));
            bool hovered = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked()) ui_open_profile(owner->id, g->id);

            ImGui::SetCursorScreenPos(at);
            ui_avatar(owner, 20.0f, false);
            ImGui::SameLine(0, 6);

            char handle[96];
            cnprint(handle, sizeof(handle), "@%s", owner->username ? owner->username : "?");
            ImGui::PushStyleColor(ImGuiCol_Text, hovered ? col::text_normal : col::accent);
            ImGui::TextUnformatted(handle);
            ImGui::PopStyleColor();

            ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + 24.0f));
        }
        else
        {
            ui_text_muted(g->owner_id ? tr("загружается...") : "-");
        }
    }
    ImGui::EndGroup();

    ImGui::SameLine(0, 24);

    char created[64];
    format_epoch_ms(g->created_ms(), created, sizeof(created));
    info_cell(tr("Дата создания"), created, COLUMN);

    ImGui::SameLine(0, 24);

    char joined[64];
    joined[0] = 0;
    if (g->joined_at) format_timestamp(g->joined_at, joined, sizeof(joined));
    info_cell(tr("Ты зашёл"), joined, COLUMN);

    ImGui::Dummy(ImVec2(0, 14));

    // Row two.
    char vanity[96];
    if (g->vanity_url_code && g->vanity_url_code[0])
        cnprint(vanity, sizeof(vanity), "discord.gg/%s", g->vanity_url_code);
    else
        vanity[0] = 0;
    info_cell(tr("Своя ссылка"), vanity, COLUMN);

    ImGui::SameLine(0, 24);
    info_cell(tr("Уровень проверки"), verification_name(g->verification_level), COLUMN);

    ImGui::SameLine(0, 24);
    char boosts[64];
    cnprint(boosts, sizeof(boosts), tr("%d (уровень %d)"), g->premium_subscribers, g->premium_tier);
    info_cell(tr("Бусты"), boosts, COLUMN);

    ImGui::Dummy(ImVec2(0, 14));

    // Row three.
    char channels[32];
    cnprint(channels, sizeof(channels), "%u", g->channels.count);
    info_cell(tr("Каналов"), channels, COLUMN);

    ImGui::SameLine(0, 24);
    char roles[32];
    cnprint(roles, sizeof(roles), "%u", g->roles.count);
    info_cell(tr("Ролей"), roles, COLUMN);

    ImGui::SameLine(0, 24);
    api::fetch_guild_counts(g->id);

    char members[64];
    int total = g->approx_members ? g->approx_members : g->member_count;
    if (total > 0 && g->approx_online > 0)
        cnprint(members, sizeof(members), tr("%d, в сети %d"), total, g->approx_online);
    else if (total > 0)
        cnprint(members, sizeof(members), "%d", total);
    else
        cnprint(members, sizeof(members), tr("%u загружено"), g->members.count);
    info_cell(tr("Участников"), members, COLUMN);

    ImGui::Dummy(ImVec2(0, 16));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 8));

    if (ImGui::Button(tr("Закрыть"), ImVec2(120, 30))) ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------------------
// channel properties
// ---------------------------------------------------------------------------

namespace
{
    const char* channel_kind_name(int type)
    {
        switch (type)
        {
        case CH_GUILD_TEXT:           return tr("Текстовый");
        case CH_DM:                   return tr("Личные сообщения");
        case CH_GUILD_VOICE:          return tr("Голосовой");
        case CH_GROUP_DM:             return tr("Групповой чат");
        case CH_CATEGORY:             return tr("Категория");
        case CH_ANNOUNCEMENT:         return tr("Объявления");
        case CH_ANNOUNCEMENT_THREAD:  return tr("Ветка объявлений");
        case CH_PUBLIC_THREAD:        return tr("Ветка");
        case CH_PRIVATE_THREAD:       return tr("Закрытая ветка");
        case CH_STAGE:                return tr("Трибуна");
        case CH_FORUM:                return tr("Форум");
        default:                      return tr("Неизвестный");
        }
    }

    void info_line(const char* label, const char* value)
    {
        ui_text_muted(label);
        ImGui::SameLine(190.0f);
        ImGui::TextUnformatted(value && value[0] ? value : "-");
    }
}

void ui_view_channel_info_popup()
{
    if (g_ui.open_channel_info_popup)
    {
        ImGui::OpenPopup("##channelinfo");
        g_ui.open_channel_info_popup = false;
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f,
                                   vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(560, 0));

    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGui::ColorConvertU32ToFloat4(col::bg_panel));
    if (!ImGui::BeginPopupModal("##channelinfo", 0,
                                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::PopStyleColor();
        return;
    }

    store::guard guard;
    dchannel* c = store::find_channel(g_ui.channel_info_id);

    if (!c)
    {
        ui_text_muted(tr("Канал не найден"));
        if (ImGui::Button(tr("Закрыть"), ImVec2(120, 30))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        ImGui::PopStyleColor();
        return;
    }

    dguild* g = store::find_guild(c->guild_id);
    bool hidden = g && !store::can_view_channel(g, store::self_id(), c);

    char line[256];

    ImGui::PushFont(g_app.font_big);
    cnprint(line, sizeof(line), "#%s", c->name ? c->name : tr("канал"));
    ImGui::TextUnformatted(line);
    ImGui::PopFont();

    if (hidden)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, col::yellow);
        ImGui::TextUnformatted(tr("Доступа нет - всё ниже получено из описания сервера"));
        ImGui::PopStyleColor();
    }

    if (c->topic)
    {
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::PushTextWrapPos(540.0f);
        ui_text_muted(c->topic);
        ImGui::PopTextWrapPos();
    }

    ImGui::Dummy(ImVec2(0, 8));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 6));

    info_line(tr("Тип"), channel_kind_name(c->type));

    cnprint(line, sizeof(line), "%llu", c->id);
    info_line("ID", line);

    {
        char created[64];
        format_epoch_ms(snowflake_time_ms(c->id), created, sizeof(created));
        info_line(tr("Создан"), created);
    }

    if (c->parent_id)
    {
        dchannel* parent = store::find_channel(c->parent_id);
        info_line(tr("Категория"), parent && parent->name ? parent->name : "-");
    }

    cnprint(line, sizeof(line), "%d", c->position);
    info_line(tr("Позиция"), line);

    // The last message id is a timestamp, so a channel nobody can read still
    // says when it was last used. That is usually the thing people want to
    // know about a closed channel.
    if (c->last_message_id)
    {
        char when[64];
        format_epoch_ms(snowflake_time_ms(c->last_message_id), when, sizeof(when));
        info_line(tr("Последнее сообщение"), when);
    }
    else
    {
        info_line(tr("Последнее сообщение"), tr("нет данных"));
    }

    if (c->is_voice())
    {
        cnprint(line, sizeof(line), tr("%d кбит/с"), c->bitrate / 1000);
        info_line(tr("Битрейт"), c->bitrate ? line : "-");

        if (c->user_limit) cnprint(line, sizeof(line), "%d", c->user_limit);
        info_line(tr("Лимит участников"), c->user_limit ? line : tr("без лимита"));
    }
    else
    {
        if (c->rate_limit_per_user)
        {
            cnprint(line, sizeof(line), tr("%d с"), c->rate_limit_per_user);
            info_line(tr("Медленный режим"), line);
        }
        info_line(tr("Возрастное ограничение"), c->nsfw ? tr("да") : tr("нет"));
    }

    if (c->type == CH_PUBLIC_THREAD || c->type == CH_PRIVATE_THREAD ||
        c->type == CH_ANNOUNCEMENT_THREAD)
    {
        info_line(tr("Архивирована"), c->archived ? tr("да") : tr("нет"));
        info_line(tr("Закрыта"), c->locked ? tr("да") : tr("нет"));

        cnprint(line, sizeof(line), "%d", c->message_count);
        info_line(tr("Сообщений"), line);
        cnprint(line, sizeof(line), "%d", c->member_count);
        info_line(tr("Участников"), line);
    }

    // ---- who is allowed in
    if (g)
    {
        ImGui::Dummy(ImVec2(0, 8));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 6));
        ImGui::TextUnformatted(tr("Доступ"));
        ImGui::Dummy(ImVec2(0, 4));

        if (!c->overwrites.count)
        {
            ui_text_muted(tr("Своих правил нет - как у категории или у сервера"));
        }

        for (unsigned int i = 0; i < c->overwrites.count; i++)
        {
            const doverwrite* o = &c->overwrites[i];

            const char* who = "?";
            char who_buf[96];
            if (o->type == 0)
            {
                if (o->id == g->id) who = "@everyone";
                else
                {
                    const drole* r = store::find_role(g, o->id);
                    who = r && r->name ? r->name : tr("роль");
                }
            }
            else
            {
                duser* u = store::find_user(o->id);
                if (u) { cnprint(who_buf, sizeof(who_buf), "@%s", u->display_name()); who = who_buf; }
                else   { cnprint(who_buf, sizeof(who_buf), "%llu", o->id); who = who_buf; }
            }

            // Only the one bit that decides whether a channel is visible is
            // worth spelling out; the rest is noise for this purpose.
            bool allows_view = (o->allow & PERM_VIEW_CHANNEL) != 0;
            bool denies_view = (o->deny & PERM_VIEW_CHANNEL) != 0;

            ImU32 colour = denies_view ? col::red : (allows_view ? col::green : col::text_muted);
            ImGui::PushStyleColor(ImGuiCol_Text, colour);
            cnprint(line, sizeof(line), "%s  %s", who,
                    denies_view ? tr("нет доступа") : (allows_view ? tr("доступ открыт") : tr("без изменений")));
            ImGui::TextUnformatted(line);
            ImGui::PopStyleColor();
        }

        // And the answer for this account specifically, which is the question
        // that was actually being asked.
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::PushStyleColor(ImGuiCol_Text, hidden ? col::red : col::green);
        ImGui::TextUnformatted(hidden ? tr("Этот аккаунт читать канал не может")
                                      : tr("Этот аккаунт канал читать может"));
        ImGui::PopStyleColor();
    }

    ImGui::Dummy(ImVec2(0, 10));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 6));

    if (ImGui::Button(tr("Закрыть"), ImVec2(120, 30))) ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------------------
// screen share setup, and covering windows up
// ---------------------------------------------------------------------------

namespace
{
    // Refreshed on demand rather than every frame: enumerating every top
    // level window is not free, and the list only has to be right at the
    // moment somebody looks at it.
    capture_target g_windows[64];
    int g_window_count = 0;
    unsigned long long g_windows_at = 0;

    void refresh_windows(bool force)
    {
        unsigned long long now = GetTickCount64();
        if (!force && g_windows_at && now - g_windows_at < 2000) return;

        g_window_count = capture::list_windows(g_windows, 64);
        g_windows_at = now;
    }

    const char* quality_name(int q)
    {
        switch (q)
        {
        case 0:  return tr("Экономная");
        case 2:  return tr("Высокая");
        default: return tr("Обычная");
        }
    }

    void draw_censor_section()
    {
        ImGui::TextUnformatted(tr("Закрытые окна"));
        ui_text_muted(tr("Выбранные окна зрители не увидят - на их месте будет заглушка"));
        ImGui::Dummy(ImVec2(0, 4));

        // What is already covered, including anything whose window has since
        // closed - it stays listed so the row can be taken off deliberately.
        for (int i = 0; i < censor::count(); i++)
        {
            const censor_entry* e = censor::at(i);
            if (!e) continue;

            ImGui::PushID(1000 + i);

            bool alive = IsWindow((HWND)e->window) != 0;
            ImGui::PushStyleColor(ImGuiCol_Text, alive ? col::red : col::text_muted);
            ImGui::TextUnformatted(e->title[0] ? e->title : tr("окно"));
            ImGui::PopStyleColor();

            ImGui::SameLine(300.0f);
            if (ImGui::SmallButton(alive ? tr("открыть") : tr("убрать из списка")))
            {
                censor::remove(e->window);
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }

        if (!censor::count()) ui_text_muted(tr("Пока ничего не закрыто"));

        ImGui::Dummy(ImVec2(0, 6));

        if (ImGui::SmallButton(tr("Обновить список окон"))) refresh_windows(true);
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Снять всё"))) censor::clear();

        ImGui::Dummy(ImVec2(0, 4));

        refresh_windows(false);

        ImGui::BeginChild("##windowlist", ImVec2(0, 190.0f), true);
        for (int i = 0; i < g_window_count; i++)
        {
            capture_target* w = &g_windows[i];

            ImGui::PushID(i);
            bool on = censor::is_censored(w->window);

            // Minimised windows are listed too: marking one you are about to
            // open is the whole point of setting this up in advance.
            char label[192];
            cnprint(label, sizeof(label), "%s%s", w->name[0] ? w->name : tr("окно"),
                    w->minimized ? tr("   (свёрнуто)") : "");

            if (w->minimized) ImGui::PushStyleColor(ImGuiCol_Text, col::text_muted);
            if (ImGui::Checkbox(label, &on))
            {
                if (on) censor::add(w->window, w->name);
                else    censor::remove(w->window);
            }
            if (w->minimized) ImGui::PopStyleColor();

            if (ImGui::IsItemHovered())
            {
                char size[80];
                if (w->minimized) ccstrncpy(size, tr("свёрнуто - закроется, когда появится"),
                                            sizeof(size) - 1);
                else cnprint(size, sizeof(size), "%d x %d", w->width, w->height);
                ImGui::SetTooltip("%s", size);
            }
            ImGui::PopID();
        }
        if (!g_window_count) ui_text_muted(tr("Окон не нашлось"));
        ImGui::EndChild();

        ImGui::Dummy(ImVec2(0, 6));
        ImGui::TextUnformatted(tr("Чем закрывать"));

        if (censor::has_cover_image())
        {
            char line[192];
            cnprint(line, sizeof(line), tr("Картинка: %s"), censor::cover_name());
            ui_text_muted(line);
        }
        else
        {
            ui_text_muted(tr("Чёрный прямоугольник со словом CENSORED"));
        }

        if (ImGui::SmallButton(tr("Выбрать картинку...")))
        {
            wchar_t chosen[MAX_PATH];
            if (ufile::open_dialog(chosen, MAX_PATH))
            {
                if (!censor::set_cover_image(chosen))
                    api::set_last_error(tr("Картинка не открылась"));
            }
        }
        if (censor::has_cover_image())
        {
            ImGui::SameLine();
            if (ImGui::SmallButton(tr("Вернуть заглушку"))) censor::clear_cover_image();
        }
    }
}

void ui_view_share_popup()
{
    if (g_ui.open_share_popup)
    {
        ImGui::OpenPopup("##sharesetup");
        g_ui.open_share_popup = false;
        refresh_windows(true);
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f,
                                   vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(560, 0));

    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGui::ColorConvertU32ToFloat4(col::bg_panel));
    if (!ImGui::BeginPopupModal("##sharesetup", 0,
                                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::PopStyleColor();
        return;
    }

    screenshare_state st = screenshare::state();
    bool live = (st != SHARE_IDLE && st != SHARE_FAILED);

    ImGui::PushFont(g_app.font_big);
    ImGui::TextUnformatted(live ? tr("Демонстрация идёт") : tr("Демонстрация экрана"));
    ImGui::PopFont();

    if (live) ui_text_muted(screenshare::status_text());

    ImGui::Dummy(ImVec2(0, 8));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 6));

    // ---- what it is sent at
    int height = ui_share_height();
    int fps = ui_share_fps();
    int quality = ui_share_quality();

    if (live)
    {
        char line[192];
        cnprint(line, sizeof(line), tr("Идёт в %dp, %d к/с, ~%d кбит/с"),
                height, fps, ui_share_bitrate());
        ui_text_muted(line);

        // Sound can be turned on and off while the stream runs: only the audio
        // track changes, and the picture never stops.
        bool live_audio = ui_share_audio();
        if (ImGui::Checkbox(tr("Передавать звук системы"), &live_audio))
        {
            ui_set_share_audio(live_audio);
            screenshare::set_audio(live_audio);
        }

        if (screenshare::audio_running())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, col::green);
            ImGui::TextUnformatted(tr("Звук системы передаётся"));
            ImGui::PopStyleColor();
        }
        else if (live_audio)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, col::red);
            ImGui::TextWrapped(tr("Звук не пошёл: %s"), screenshare::audio_error());
            ImGui::PopStyleColor();
        }
        else
        {
            ui_text_muted(tr("Зрители слышат только микрофон"));
        }

        ui_text_muted(tr("Размер и частоту можно поменять только перезапуском"));
    }
    else
    {
        ImGui::TextUnformatted(tr("Размер картинки"));

        const int HEIGHTS[4] = { 1080, 720, 480, 360 };
        for (int i = 0; i < 4; i++)
        {
            char label[16];
            cnprint(label, sizeof(label), "%dp", HEIGHTS[i]);
            if (ImGui::RadioButton(label, height == HEIGHTS[i])) height = HEIGHTS[i];
            if (i < 3) ImGui::SameLine();
        }
        ui_text_muted(tr("Меньше - мягче картинка, но заметно легче каналу"));

        ImGui::Dummy(ImVec2(0, 6));
        ImGui::TextUnformatted(tr("Частота кадров"));
        ImGui::SetNextItemWidth(-1);
        // A slider, which also takes a typed number once tabbed into - and
        // clamps it to the range rather than accepting something the encoder
        // will refuse a moment later.
        ImGui::SliderInt("##fps", &fps, 1, SHARE_FPS_MAX, tr("%d к/с"));

        if (fps < 1) fps = 1;
        if (fps > SHARE_FPS_MAX) fps = SHARE_FPS_MAX;

        ImGui::Dummy(ImVec2(0, 6));
        ImGui::TextUnformatted(tr("Качество"));
        for (int i = 0; i < 3; i++)
        {
            if (ImGui::RadioButton(quality_name(i), quality == i)) quality = i;
            if (i < 2) ImGui::SameLine();
        }

        char bits[128];
        cnprint(bits, sizeof(bits), tr("Выйдет примерно %d кбит/с"), ui_share_bitrate());
        ui_text_muted(bits);

        // Said plainly rather than left to be discovered: the setting people
        // ask for by name does not exist in this pipeline.
        ui_text_muted(tr("Поток кодируется в H.264, не в JPEG - «качество» это битрейт"));

        ImGui::Dummy(ImVec2(0, 8));
        ImGui::TextUnformatted(tr("Как читать экран"));

        for (int m = 0; m < CAPTURE_METHOD_COUNT; m++)
        {
            if (ImGui::RadioButton(capture::method_name((capture_method)m),
                                   ui_capture_method() == m))
                ui_set_capture_method(m);
            if (m + 1 < CAPTURE_METHOD_COUNT) ImGui::SameLine();
        }

        if (ui_capture_method() == CAPTURE_DXGI)
            ui_text_muted(tr("Кадр берётся у композитора и не пересобирается, пока экран не менялся. "
                          "Нужна Windows 8; при отказе сам вернётся на BitBlt"));
        else
            ui_text_muted(tr("Полная перерисовка каждый кадр. Медленно, но работает везде"));

        ImGui::Dummy(ImVec2(0, 8));

        bool with_audio = ui_share_audio();
        if (ImGui::Checkbox(tr("Передавать звук системы"), &with_audio))
            ui_set_share_audio(with_audio);

        if (with_audio)
            ui_text_muted(tr("Свой звук исключён, чтобы никто не слышал сам себя. "
                          "Вместе с ним не слышно и видео, проигранного в IMDiscord"));
        else
            ui_text_muted(tr("Зрители услышат только микрофон"));

        if (height != ui_share_height() || fps != ui_share_fps() || quality != ui_share_quality())
            ui_set_share_settings(height, fps, quality);
    }

    ImGui::Dummy(ImVec2(0, 10));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 6));

    draw_censor_section();

    ImGui::Dummy(ImVec2(0, 10));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 6));

    if (live)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, col::red);
        if (ImGui::Button(tr("Закончить"), ImVec2(150, 32)))
        {
            screenshare::stop();
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::Button(tr("Свернуть"), ImVec2(150, 32))) ImGui::CloseCurrentPopup();
    }
    else
    {
        if (ImGui::Button(tr("Начать"), ImVec2(150, 32)))
        {
            // Width follows the height at sixteen by nine; the capture keeps
            // the screen's real shape inside that box anyway.
            int w = ui_share_height() * 16 / 9;
            screenshare::start(0, w & ~1, ui_share_height(), ui_share_fps(),
                               ui_share_bitrate(), ui_share_audio(), ui_capture_method());
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Отмена"), ImVec2(150, 32))) ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
    ImGui::PopStyleColor();
}
