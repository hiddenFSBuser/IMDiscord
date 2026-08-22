#include "pch.h"
#include "ui_state.h"
#include "discord/warmup.h"
#include "theme.h"
#include "textures.h"

#include "core/app.h"
#include "core/log.h"
#include "core/storage.h"
#include "discord/store.h"
#include "system/io/ufile.h"
#include "discord/exporter.h"
#include "discord/archive.h"
#include "discord/rest.h"
#include "discord/gateway.h"
#include "discord/voice.h"
#include "audio/audio.h"
#include "audio/noise.h"
#include "audio/vad.h"
#include "audio/sounds.h"
#include "audio/music.h"
#include "discord/science.h"
#include "video/screenshare.h"
#include "video/streamview.h"

// ---------------------------------------------------------------------------
// guild rail
// ---------------------------------------------------------------------------

namespace
{
    // Everything the card under the cursor says about a server. Built once per
    // hover rather than per frame: it walks every voice state and every
    // member, and the cursor sits still for whole seconds at a time.
    struct guild_card
    {
        snowflake guild_id;
        unsigned int revision;

        int in_voice;
        int streaming;
        int online;

        // Up to a handful of names, which is all a card has room for.
        snowflake voice_users[8];
        bool voice_streaming[8];
        int voice_named;
    };

    guild_card g_card = { 0, 0, 0, 0, 0, { 0 }, { false }, 0 };

    void build_guild_card(dguild* g)
    {
        if (g_card.guild_id == g->id && g_card.revision == store::revision()) return;

        ccfset(&g_card, 0, sizeof(g_card));
        g_card.guild_id = g->id;
        g_card.revision = store::revision();

        const ulist<dvoice_state>& states = store::voice_states();
        for (unsigned int i = 0; i < states.count; i++)
        {
            if (states[i].guild_id != g->id || !states[i].channel_id) continue;

            g_card.in_voice++;
            if (states[i].self_stream) g_card.streaming++;

            if (g_card.voice_named < 8)
            {
                g_card.voice_users[g_card.voice_named] = states[i].user_id;
                g_card.voice_streaming[g_card.voice_named] = states[i].self_stream;
                g_card.voice_named++;
            }
        }

        // Only what has been loaded can be counted. The server's own total is
        // shown next to it so the difference is visible rather than
        // misleading.
        for (unsigned int i = 0; i < g->members.count; i++)
        {
            duser* u = store::find_user(g->members[i].user_id);
            if (u && u->status != STATUS_OFFLINE) g_card.online++;
        }
    }

    void draw_guild_card(dguild* g)
    {
        build_guild_card(g);

        // Asked for the moment the cursor lands, so the numbers are there
        // without anybody having to open the server first. Cached, so resting
        // on a bubble does not send a request a frame.
        api::fetch_guild_counts(g->id);

        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(320.0f);

        ImGui::TextUnformatted(g->name ? g->name : tr("Сервер"));

        char line[192];

        // The server's own totals when it has given them, which needs nothing
        // loaded. What has actually been loaded is only worth mentioning when
        // there is nothing better.
        int total = g->approx_members ? g->approx_members : g->member_count;
        int online = g->approx_online ? g->approx_online : g_card.online;

        if (total > 0 && online > 0)
            cnprint(line, sizeof(line), tr("%d в сети  ·  %d всего"), online, total);
        else if (total > 0)
            cnprint(line, sizeof(line), tr("%d участников"), total);
        else if (g->counts_loading)
            ccstrncpy(line, tr("считаем..."), sizeof(line) - 1);
        else
            cnprint(line, sizeof(line), tr("%d в сети из %u загруженных"),
                    g_card.online, g->members.count);
        ui_text_muted(line);

        if (g_card.in_voice)
        {
            ImGui::Separator();

            if (g_card.streaming)
                cnprint(line, sizeof(line), tr("В голосовых: %d, из них с демонстрацией %d"),
                        g_card.in_voice, g_card.streaming);
            else
                cnprint(line, sizeof(line), tr("В голосовых: %d"), g_card.in_voice);
            ImGui::TextUnformatted(line);

            for (int i = 0; i < g_card.voice_named; i++)
            {
                duser* u = store::find_user(g_card.voice_users[i]);
                if (!u) continue;

                ImGui::PushStyleColor(ImGuiCol_Text,
                                      g_card.voice_streaming[i] ? col::red : col::text_muted);
                cnprint(line, sizeof(line), "  %s%s", u->display_name(),
                        g_card.voice_streaming[i] ? tr("  (демонстрация)") : "");
                ImGui::TextUnformatted(line);
                ImGui::PopStyleColor();
            }

            if (g_card.in_voice > g_card.voice_named)
            {
                cnprint(line, sizeof(line), tr("  и ещё %d"), g_card.in_voice - g_card.voice_named);
                ui_text_muted(line);
            }
        }

        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }

    // A small speaker in the top right corner of a bubble, the way discord
    // marks a server where something is happening without opening it.
    void draw_voice_pip(ImDrawList* dl, ImVec2 bubble_pos, float bubble, bool streaming)
    {
        float r = 9.0f;
        ImVec2 c(bubble_pos.x + bubble - r + 2.0f, bubble_pos.y + r - 2.0f);

        dl->AddCircleFilled(c, r + 2.0f, col::bg_deep);
        dl->AddCircleFilled(c, r, streaming ? col::red : col::green);

        // Speaker cone: a square body and a triangle, small enough that
        // anything more detailed would just be mush at this size.
        dl->AddRectFilled(ImVec2(c.x - 3.5f, c.y - 1.8f), ImVec2(c.x - 1.0f, c.y + 1.8f),
                          col::bg_deep, 0.5f);
        ImVec2 tri[3] = {
            ImVec2(c.x - 1.5f, c.y - 3.6f),
            ImVec2(c.x - 1.5f, c.y + 3.6f),
            ImVec2(c.x + 2.2f, c.y),
        };
        dl->AddConvexPolyFilled(tri, 3, col::bg_deep);
    }
}

void ui_view_guild_rail(float width, float height)
{
    ImVec2 origin = ImGui::GetWindowPos();
    ImGui::GetWindowDrawList()->AddRectFilled(
        origin, ImVec2(origin.x + width, origin.y + height), col::bg_deep);

    const float bubble = 48.0f;
    const float indent = (width - bubble) * 0.5f;

    // The rail scrolls. Without this a twentieth server simply had nowhere to
    // be drawn and was silently unreachable.
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 6.0f);
    ImGui::BeginChild("##railscroll", ImVec2(width, height), false,
                      ImGuiWindowFlags_NoScrollbar);

    // Taken again inside the child, and this is not a formality. The list
    // captured before BeginChild belongs to the window behind it: everything
    // drawn into it ignores the child's scroll and clip, which is why the
    // voice badge was landing somewhere below its own server instead of in
    // the corner of it.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    origin = ImGui::GetWindowPos();

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 8));
    ImGui::Dummy(ImVec2(0, 8));

    // Direct messages entry.
    {
        ImGui::SetCursorPosX(indent);
        bool active = (g_ui.active_guild == 0);

        ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddRectFilled(p, ImVec2(p.x + bubble, p.y + bubble),
                          active ? col::accent : col::bg_panel,
                          active ? bubble * 0.3f : bubble * 0.5f);
        ImVec2 ts = ImGui::CalcTextSize("DM");
        dl->AddText(ImVec2(p.x + (bubble - ts.x) * 0.5f, p.y + (bubble - ts.y) * 0.5f), col::text_normal, "DM");

        if (ImGui::InvisibleButton("##dms", ImVec2(bubble, bubble)))
        {
            g_ui.active_guild = 0;
            g_ui.active_channel = 0;
            g_ui.show_friends = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Личные сообщения и друзья"));
    }

    ImGui::SetCursorPosX(indent);
    ImGui::Dummy(ImVec2(bubble, 1));
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddLine(ImVec2(p.x + indent - 8.0f, p.y - 4.0f), ImVec2(p.x + indent + bubble + 8.0f, p.y - 4.0f),
                    col::separator, 2.0f);
    }

    const ulist<snowflake>& order = store::guild_order();

    // Applied after the loop: reordering underneath it would make the rest of
    // the pass draw the wrong servers.
    snowflake dragged = 0;
    int drop_at = -1;

    for (unsigned int i = 0; i < order.count; i++)
    {
        dguild* g = store::find_guild(order[i]);
        if (!g) continue;

        ImGui::PushID((int)i);
        ImGui::SetCursorPosX(indent);

        bool active = (g_ui.active_guild == g->id);
        ImVec2 p = ImGui::GetCursorScreenPos();
        ui_guild_bubble(g, bubble, active);

        ImGui::SetCursorScreenPos(p);
        if (ImGui::InvisibleButton("##guild", ImVec2(bubble, bubble)))
        {
            g_ui.active_guild = g->id;
            g_ui.show_friends = false;

            if (!g->loaded) api::fetch_guild_channels(g->id);
            gateway::subscribe_guild(g->id, 0);

            // Asked about once per server per run. There is no dispatch that
            // says "this one still wants an answer" - the only way to know is
            // to ask, so it is asked the first time the server is opened and
            // never again in this session.
            {
                static uset<snowflake> probed;
                if (!probed.contains(g->id))
                {
                    probed.push(g->id);
                    api::probe_onboarding(g->id);
                }
            }

            // Jump to the first readable text channel.
            g_ui.active_channel = 0;
            for (unsigned int k = 0; k < g->channels.count; k++)
            {
                dchannel* c = store::find_channel(g->channels[k]);
                if (c && c->is_textual()) { g_ui.active_channel = c->id; break; }
            }
            g_ui.scroll_to_bottom = true;
        }

        // Dragging a bubble onto another one puts it there. The order is the
        // account's own and lives only here, so nothing is sent anywhere.
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoPreviewTooltip))
        {
            ImGui::SetDragDropPayload("IMD_GUILD", &g->id, sizeof(snowflake));
            ImGui::TextUnformatted(g->name ? g->name : tr("Сервер"));
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget())
        {
            const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("IMD_GUILD");
            if (payload && payload->DataSize == (int)sizeof(snowflake))
            {
                dragged = *(const snowflake*)payload->Data;
                drop_at = (int)i;
            }
            ImGui::EndDragDropTarget();
        }

        store::guild_voice_summary voice = store::voice_summary(g->id);
        if (voice.in_voice > 0)
            draw_voice_pip(dl, p, bubble, voice.streaming > 0);

        if (ImGui::IsItemHovered()) draw_guild_card(g);

        if (ImGui::BeginPopupContextItem("##guildctx"))
        {
            if (ImGui::MenuItem(tr("Информация о сервере")))
            {
                g_ui.server_info_guild = g->id;
                g_ui.open_server_info_popup = true;
            }
            {
                // Same picture, full size: the 48px bubble in the info popup
                // opens it too, but a menu row is easier to find.
                char big[320];
                cdn::guild_icon(g, 1024, big, sizeof(big));
                if (ImGui::MenuItem(tr("Приблизить аватарку"), 0, false, big[0] != 0))
                    ui_open_image_viewer(big, g->name ? g->name : tr("Сервер"));
            }
            ImGui::Separator();
            if (ImGui::MenuItem(tr("Роли и правила при входе"))) ui_open_onboarding(g->id);
            if (ImGui::MenuItem(tr("Настройки сервера"))) ui_open_guild_edit(g->id);
            if (ImGui::MenuItem(tr("Управление ролями"))) ui_open_roles(g->id);
            if (ImGui::MenuItem(tr("Аудит и баны"))) ui_open_audit(g->id);
            if (ImGui::MenuItem(tr("Создать канал"))) ui_open_new_channel(g->id);
            if (ImGui::MenuItem(tr("Приглашения"))) ui_open_invites(g->id);
            if (ImGui::MenuItem(tr("Вебхуки"))) ui_open_webhooks(g->id);
            ImGui::Separator();
            ui_copy_id_item(g->id, tr("Скопировать ID сервера"));
            ImGui::Separator();
            // The owner cannot leave - discord refuses while the crown is
            // theirs - so they get the other way out instead. It asks for the
            // server's name before it does anything.
            if (g->owner_id && g->owner_id == store::self_id())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, col::red);
                bool go = ImGui::MenuItem(tr("Удалить сервер"));
                ImGui::PopStyleColor();

                if (go) ui_open_delete_guild(g->id);
            }
            else if (ImGui::MenuItem(tr("Покинуть сервер")))
            {
                api::leave_guild(g->id);
                if (g_ui.active_guild == g->id) { g_ui.active_guild = 0; g_ui.active_channel = 0; }
            }
            ImGui::EndPopup();
        }

        if (active)
            dl->AddRectFilled(ImVec2(origin.x, p.y + bubble * 0.2f),
                              ImVec2(origin.x + 4.0f, p.y + bubble * 0.8f), col::text_normal, 2.0f);

        ImGui::PopID();
    }

    if (dragged && drop_at >= 0)
    {
        store::guard guard;
        store::move_guild(dragged, drop_at);
        ui_save_guild_order();
    }

    // Join by invite.
    ImGui::SetCursorPosX(indent);
    ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(p, ImVec2(p.x + bubble, p.y + bubble), col::bg_panel, bubble * 0.5f);
    ImVec2 ts = ImGui::CalcTextSize("+");
    dl->AddText(ImVec2(p.x + (bubble - ts.x) * 0.5f, p.y + (bubble - ts.y) * 0.5f), col::green, "+");

    if (ImGui::InvisibleButton("##join", ImVec2(bubble, bubble)))
    {
        ImGui::OpenPopup("##joinguild");

        // Discord's own box opens on a landing screen and is stepped through
        // to the join one. This box is both at once, but the person pressing
        // "+" and then typing a code has done what those two screens are for,
        // and the chain is what discord checks before it decides whether the
        // join needs a CAPTCHA.
        science::guild_add_opened();
        science::guild_add_join_step();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Присоединиться к серверу"));

    if (ImGui::BeginPopup("##joinguild"))
    {
        ImGui::TextUnformatted(tr("Ссылка-приглашение или код"));
        ImGui::SetNextItemWidth(300);
        bool go = ImGui::InputText("##invite", g_ui.invite_input, sizeof(g_ui.invite_input),
                                   ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if (ImGui::Button(tr("Войти")) || go)
        {
            if (g_ui.invite_input[0])
            {
                api::join_guild_by_invite(g_ui.invite_input);
                ccfset(g_ui.invite_input, 0, sizeof(g_ui.invite_input));
                ImGui::CloseCurrentPopup();
            }
        }

        // Making one, in the same box: both answers to "I want to be on a
        // server I am not on", and the plus button is where somebody looks for
        // either of them.
        ImGui::Separator();
        ImGui::TextUnformatted(tr("Или создать свой"));

        ImGui::SetNextItemWidth(300);
        bool make = ImGui::InputTextWithHint("##newguild", tr("название сервера"),
                                             g_ui.new_guild_name, sizeof(g_ui.new_guild_name),
                                             ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if ((ImGui::Button(tr("Создать")) || make) && g_ui.new_guild_name[0])
        {
            api::create_guild(g_ui.new_guild_name);
            ccfset(g_ui.new_guild_name, 0, sizeof(g_ui.new_guild_name));
            ImGui::CloseCurrentPopup();
        }

        if (api::last_error()[0])
        {
            ImGui::PushStyleColor(ImGuiCol_Text, col::yellow);
            ImGui::TextWrapped("%s", api::last_error());
            ImGui::PopStyleColor();
        }

        ImGui::EndPopup();
    }

    ImGui::Dummy(ImVec2(0, 8));
    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

// ---------------------------------------------------------------------------
// channel / dm list
// ---------------------------------------------------------------------------

namespace
{
    // A little padlock, for a channel this account is not allowed into.
    void draw_lock(ImDrawList* dl, ImVec2 c, float size, ImU32 colour)
    {
        float w = size * 0.62f;
        float h = size * 0.5f;

        dl->AddRectFilled(ImVec2(c.x - w * 0.5f, c.y - h * 0.1f),
                          ImVec2(c.x + w * 0.5f, c.y + h * 0.9f), colour, 1.5f);
        dl->PathArcTo(ImVec2(c.x, c.y - h * 0.1f), w * 0.3f, 3.14159f, 6.28318f, 10);
        dl->PathStroke(colour, 0, 1.6f);
    }

    // Right click menu on somebody sitting in a voice channel. Everything here
    // is local: nothing is sent, and the person on the other end is never told
    // that they have been turned down or silenced.
    void ui_voice_member_menu(duser* u, snowflake guild_id, bool self)
    {
        if (!ImGui::BeginPopupContextItem("##vmemctx")) return;

        ImGui::TextUnformatted(u->display_name());
        ImGui::Separator();

        if (ImGui::MenuItem(tr("Профиль"))) ui_open_profile(u->id, guild_id);

        // Turning our own playback down would silence the room, not us, and
        // muting ourselves already has its own button.
        if (!self)
        {
            bool muted = voice::user_muted(u->id);
            if (ImGui::MenuItem(muted ? tr("Вернуть звук") : tr("Заглушить"), 0, muted))
                voice::set_user_muted(u->id, !muted);

            ImGui::Separator();

            float volume = voice::user_volume(u->id);
            int percent = (int)(volume * 100.0f + 0.5f);

            ImGui::TextUnformatted(tr("Громкость"));
            ImGui::SetNextItemWidth(200.0f);

            // Logarithmic, or the useful part of the range - everything either
            // side of a hundred - would be squeezed into the first fifth of a
            // slider that runs to a thousand.
            if (ImGui::SliderInt("##uvol", &percent, 0, 1000, "%d%%",
                                 ImGuiSliderFlags_Logarithmic))
                voice::set_user_volume(u->id, (float)percent / 100.0f);

            if (percent > 200)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, col::yellow);
                ImGui::TextUnformatted(tr("Ограничитель срежет часть прибавки"));
                ImGui::PopStyleColor();
            }

            if (percent != 100 && ImGui::MenuItem(tr("Сбросить на 100%")))
                voice::set_user_volume(u->id, 1.0f);
        }

        // Everything above this line stays on this machine. Everything below
        // it is sent, and the person on the other end is told.
        ui_member_moderation_menu(guild_id, u->id);

        ImGui::Separator();
        ui_copy_id_item(u->id, tr("Скопировать ID пользователя"));

        ImGui::EndPopup();
    }

    // Works out the whole new order after one channel is dropped on another and
    // sends it. The whole list rather than the pair, because positions are only
    // meaningful relative to each other: moving one and leaving the rest to be
    // renumbered by the server gives an order nobody asked for.
    //
    // The mover also takes on the target's category, which is what makes
    // dragging into and out of one work without a second gesture.
    void dropped_on(dguild* g, snowflake moved_id, snowflake target_id)
    {
        dchannel* moved = store::find_channel(moved_id);
        dchannel* target = store::find_channel(target_id);
        if (!moved || !target) return;

        // A category cannot be put inside anything, and nothing can be put
        // inside a channel that is not a category.
        if (moved->type == CH_CATEGORY && target->type != CH_CATEGORY) return;

        snowflake parent = (moved->type == CH_CATEGORY) ? 0
                         : (target->type == CH_CATEGORY ? target->id : target->parent_id);

        // Rebuilt as a plain list: everything except the mover, with the mover
        // put back where the target sits.
        snowflake order[256];
        int count = 0;

        for (unsigned int i = 0; i < g->channels.count && count < 255; i++)
        {
            snowflake id = g->channels[i];
            if (id == moved_id) continue;

            dchannel* c = store::find_channel(id);
            if (!c) continue;

            // Only things of the mover's own kind are renumbered together:
            // discord keeps categories in a sequence of their own.
            bool same_kind = (moved->type == CH_CATEGORY) == (c->type == CH_CATEGORY);
            if (!same_kind) continue;

            if (id == target_id) order[count++] = moved_id;
            order[count++] = id;
        }

        if (!count) return;
        api::reorder_channels(g->id, order, count, moved_id, parent);
    }

    // A category heading, and now a thing in its own right rather than a
    // line of grey text.
    //
    // It had no item behind it at all, which meant it could not be dragged,
    // could not be right clicked, and a category with nothing in it did not
    // appear anywhere - there was no first child to print it before. All
    // three follow from the same omission.
    void category_row(dchannel* cat, dguild* g, float width, bool hidden)
    {
        float row_w = width - 16.0f;

        ImGui::Dummy(ImVec2(0, 6));

        ImVec2 start = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##cat", ImVec2(row_w, 20.0f));

        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (ImGui::IsItemHovered())
            dl->AddRectFilled(start, ImVec2(start.x + row_w, start.y + 20.0f),
                              col::bg_hover, 4.0f);

        const char* name = cat->name ? cat->name : tr("КАНАЛЫ");
        ui_draw_text_emoji(dl, ImVec2(start.x + 2.0f, start.y + 3.0f), col::text_muted, name);

        // A category nobody here can see is marked the way a hidden channel
        // is, rather than being told apart by a shade of grey nobody would
        // notice.
        if (hidden)
            draw_lock(dl, ImVec2(start.x + row_w - 14.0f, start.y + 10.0f), 13.0f, col::text_muted);

        // Same payload as a channel: dropped_on already knows a category
        // only sorts against other categories and never lands inside one.
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoDisableHover))
        {
            ImGui::SetDragDropPayload("IMD_CHANNEL", &cat->id, sizeof(snowflake));
            ImGui::TextUnformatted(name);
            ImGui::EndDragDropSource();
        }

        // Dropping a channel on the heading puts it in that category, which
        // is the only way to reach an empty one.
        if (ImGui::BeginDragDropTarget())
        {
            const ImGuiPayload* p = ImGui::AcceptDragDropPayload("IMD_CHANNEL");
            if (p && p->DataSize == sizeof(snowflake))
            {
                snowflake moved = *(const snowflake*)p->Data;
                if (moved != cat->id) dropped_on(g, moved, cat->id);
            }
            ImGui::EndDragDropTarget();
        }

        if (ImGui::BeginPopupContextItem("##catctx"))
        {
            if (ImGui::MenuItem(tr("Переименовать категорию"))) ui_open_rename_channel(cat->id);
            if (ImGui::MenuItem(tr("Права категории"))) ui_open_channel_perms(cat->id);
            if (ImGui::MenuItem(tr("Удалить категорию"))) api::delete_channel(cat->id);
            ImGui::Separator();
            ui_copy_id_item(cat->id, tr("Скопировать ID категории"));
            ImGui::EndPopup();
        }

        ImGui::Dummy(ImVec2(0, 2));
    }

    bool channel_row(dchannel* c, bool active, float width, bool hidden)
    {
        char label[256];
        ui_channel_display_name(c, label, sizeof(label));

        bool is_voice = (c->type == CH_GUILD_VOICE || c->type == CH_STAGE);
        float row_w = width - 16.0f;
        float row_h = 26.0f;

        ImVec2 start = ImGui::GetCursorScreenPos();
        bool clicked = ImGui::InvisibleButton("##ch", ImVec2(row_w, row_h));
        bool hovered = ImGui::IsItemHovered();

        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (active || hovered)
            dl->AddRectFilled(start, ImVec2(start.x + row_w, start.y + row_h),
                              active ? col::bg_active : col::bg_hover, 4.0f);

        // Dimmed further than an ordinary unselected channel, so the ones that
        // can actually be opened stand out at a glance.
        ImU32 tint = active ? col::text_normal : col::text_muted;
        if (hidden && !active) tint = IM_COL32(110, 114, 124, 255);

        ui_draw_icon(dl, is_voice ? ICON_SPEAKER : ICON_HASH,
                     ImVec2(start.x + 13.0f, start.y + row_h * 0.5f), 15.0f, tint);

        ImVec2 text_size = ImGui::CalcTextSize(label);
        ui_draw_text_emoji(dl, ImVec2(start.x + 28.0f, start.y + (row_h - text_size.y) * 0.5f),
                           tint, label);

        if (hidden)
            draw_lock(dl, ImVec2(start.x + row_w - 14.0f, start.y + row_h * 0.5f), 14.0f, tint);

        return clicked;
    }

    // The red count discord puts on a conversation with something waiting in
    // it. Deliberately only ever drawn for conversations: a server has a
    // dozen busy channels and would wear a number permanently, which tells
    // nobody anything.
    void draw_unread_badge(ImDrawList* dl, ImVec2 right_edge, float centre_y, int count)
    {
        char text[16];
        if (count > 99) ccstrncpy(text, "99+", sizeof(text) - 1);
        else            cnprint(text, sizeof(text), "%d", count);

        ImVec2 size = ImGui::CalcTextSize(text);
        float h = 18.0f;
        float w = size.x + 12.0f;
        if (w < h) w = h;

        ImVec2 a(right_edge.x - w, centre_y - h * 0.5f);
        ImVec2 b(right_edge.x, centre_y + h * 0.5f);

        dl->AddRectFilled(a, b, col::red, h * 0.5f);
        dl->AddText(ImVec2(a.x + (w - size.x) * 0.5f, a.y + (h - size.y) * 0.5f),
                    IM_COL32(255, 255, 255, 255), text);
    }

    // No number, just "there is something here". Used when a conversation is
    // unread but nothing in it was addressed to this account in particular.
    void draw_unread_dot(ImDrawList* dl, ImVec2 right_edge, float centre_y)
    {
        dl->AddCircleFilled(ImVec2(right_edge.x - 5.0f, centre_y), 4.0f, col::text_normal);
    }

    // Shortens a name with "..." until it fits max_w, so a long one cannot
    // carry the badges and the LIVE button past the sidebar's edge. Returns
    // the original when it already fits. Binary search over whole UTF-8
    // code points only - a continuation byte is never the cut.
    const char* fit_name(const char* name, float max_w, char* out, int cap)
    {
        if (!name || !name[0]) name = "?";
        if (ImGui::CalcTextSize(name).x <= max_w) return name;

        const char* dots = "...";
        float dots_w = ImGui::CalcTextSize(dots).x;

        // Walked forward one code point at a time, keeping the longest prefix
        // that still fits.
        //
        // This was a binary search, and it could hang the whole client. The
        // midpoint was pulled *down* onto a code point boundary before being
        // tested, so a midpoint that landed on a continuation byte could end
        // up below `lo`; the fitting branch then set `lo = mid + 1`, which
        // moved nothing, and the next round computed the same midpoint. In a
        // cyrillic name every second byte is a continuation byte, so whether
        // it stalled came down to the exact width - which is why it only
        // appeared when a LIVE badge shrank the space available.
        //
        // Names are a few dozen bytes, so stepping through them costs nothing
        // worth defending, and each step strictly advances.
        int best = 0;
        int at = 0;
        while (name[at])
        {
            int next = at + 1;
            while (name[next] && ((unsigned char)name[next] & 0xC0) == 0x80) next++;

            if (ImGui::CalcTextSize(name, name + next).x + dots_w > max_w) break;
            best = next;
            at = next;
        }

        // The copy stops on a boundary too: cutting a code point in half here
        // would only move the damage from a hang to a broken glyph.
        int limit = cap - 4;
        if (best > limit)
        {
            best = limit < 0 ? 0 : limit;
            while (best > 0 && ((unsigned char)name[best] & 0xC0) == 0x80) best--;
        }

        int i = 0;
        for (; i < best; i++) out[i] = name[i];
        for (int k = 0; dots[k] && i < cap - 1; k++) out[i++] = dots[k];
        out[i] = 0;
        return out;
    }

    void dm_row(dchannel* c, bool active, float width)
    {
        ImGui::PushID((const void*)(size_t)c->id);

        ImVec2 start = ImGui::GetCursorScreenPos();
        bool clicked = ImGui::InvisibleButton("##dm", ImVec2(width - 16.0f, 42.0f));
        bool hovered = ImGui::IsItemHovered();

        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (active || hovered)
            dl->AddRectFilled(start, ImVec2(start.x + width - 16.0f, start.y + 42.0f),
                              active ? col::bg_active : col::bg_hover, 4.0f);

        duser* peer = (c->type == CH_DM && c->recipients.count > 0) ? store::find_user(c->recipients[0]) : 0;

        // Right click. A conversation had none at all, so the person on the
        // other side of it could not be looked up from the one place in the
        // client that is entirely about them.
        if (ImGui::BeginPopupContextItem("##dmctx"))
        {
            if (peer)
            {
                if (ImGui::MenuItem(tr("Открыть профиль"))) ui_open_profile(peer->id, 0);
            }
            else if (c->recipients.count && ImGui::BeginMenu(tr("Открыть профиль")))
            {
                // A group has several, so it asks which.
                for (unsigned int k = 0; k < c->recipients.count; k++)
                {
                    duser* m = store::find_user(c->recipients[k]);
                    if (!m) continue;

                    ImGui::PushID((const void*)(size_t)m->id);
                    if (ImGui::MenuItem(m->display_name())) ui_open_profile(m->id, 0);
                    ImGui::PopID();
                }
                ImGui::EndMenu();
            }

            ui_invite_to_server_menu(c->id);

            ImGui::Separator();
            ui_copy_id_item(c->id, tr("Скопировать ID чата"));
            if (peer) ui_copy_id_item(peer->id, tr("Скопировать ID пользователя"));

            ImGui::EndPopup();
        }

        ImGui::SetCursorScreenPos(ImVec2(start.x + 6.0f, start.y + 5.0f));
        if (peer)
        {
            ui_avatar(peer, 32.0f, true);
        }
        else
        {
            char url[320];
            cdn::channel_icon(c, 64, url, sizeof(url));
            ImVec2 p = ImGui::GetCursorScreenPos();
            const texture* t = url[0] ? tex::get(url) : 0;
            if (t && t->ready())
                dl->AddImageRounded(t->id(), p, ImVec2(p.x + 32, p.y + 32), ImVec2(0, 0), ImVec2(1, 1),
                                    IM_COL32_WHITE, 32.0f * theme::avatar_rounding());
            else
                dl->AddRectFilled(p, ImVec2(p.x + 32, p.y + 32), col::bg_hover,
                                  32.0f * theme::avatar_rounding());
            ImGui::Dummy(ImVec2(32, 32));
        }

        bool unread = c->unread() && !active;

        char label[256];
        ui_channel_display_name(c, label, sizeof(label));
        dl->AddText(ImVec2(start.x + 46.0f, start.y + 12.0f),
                    (active || unread) ? col::text_normal : col::text_muted, label);

        if (unread)
        {
            ImVec2 edge(start.x + width - 24.0f, 0);
            if (c->mention_count > 0)
                draw_unread_badge(dl, edge, start.y + 21.0f, c->mention_count);
            else
                draw_unread_dot(dl, edge, start.y + 21.0f);
        }

        ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + 42.0f));

        // Who is in the call, underneath, the way a server's voice channel
        // lists its occupants. A call in a direct message announces itself
        // nowhere else in the interface: without this the only way to find
        // out whether anybody is waiting in one is to join it and look.
        {
            static ulist<snowflake> in_call;
            store::users_in_voice(c->id, &in_call);

            for (unsigned int k = 0; k < in_call.count; k++)
            {
                duser* u = store::find_user(in_call[k]);
                if (!u) continue;

                ImGui::PushID((const void*)(size_t)u->id);
                ImGui::Indent(18.0f);

                ImVec2 p = ImGui::GetCursorScreenPos();
                ui_avatar(u, 18.0f, false);

                float level = voice::speaking_level(u->id);
                if (level > 0.02f)
                    dl->AddCircle(ImVec2(p.x + 9, p.y + 9), 10.0f, col::green, 0, 2.0f);

                const dvoice_state* vs = store::find_voice_state(u->id);
                bool mic_off = vs && (vs->self_mute || vs->mute);
                bool ears_off = vs && (vs->self_deaf || vs->deaf);

                float reserve = 0.0f;
                if (mic_off || ears_off) reserve = 6.0f + ((mic_off && ears_off) ? 30.0f : 14.0f);

                float avail = ImGui::GetContentRegionAvail().x - reserve;
                if (avail < 20.0f) avail = 20.0f;

                char fitted[96];
                const char* shown = fit_name(u->display_name(), avail, fitted, sizeof(fitted));

                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, col::text_muted);
                ImGui::TextUnformatted(shown);
                ImGui::PopStyleColor();

                if (ImGui::IsItemClicked()) ui_open_profile(u->id, 0);

                if (mic_off || ears_off)
                {
                    ImGui::SameLine(0, 6);
                    ImVec2 mark = ImGui::GetCursorScreenPos();
                    ui_draw_muted_marks(dl, ImVec2(mark.x, mark.y + 2.0f), 13.0f,
                                        mic_off, ears_off);
                    ImGui::Dummy(ImVec2((mic_off && ears_off) ? 30.0f : 14.0f, 13.0f));
                }

                ImGui::Unindent(18.0f);
                ImGui::PopID();
            }

            // Rung and not yet answered. An empty call that is ringing looks
            // exactly like no call at all, and those are not the same thing
            // to somebody deciding whether to pick up.
            if (!in_call.count && store::call_is_ringing(c->id))
            {
                ImGui::Indent(18.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, col::green);
                ImGui::TextUnformatted(tr("идёт вызов..."));
                ImGui::PopStyleColor();
                ImGui::Unindent(18.0f);
            }
        }

        if (clicked)
        {
            g_ui.active_channel = c->id;
            g_ui.show_friends = false;
            g_ui.scroll_to_bottom = true;
            g_ui.reply_to = 0;

            // Opening it is reading it, here and on every other device this
            // account is signed in to.
            snowflake newest = c->last_message_id;
            store::mark_channel_read(c->id);
            if (newest) api::ack_message(c->id, newest);

            if (!c->history_loaded) api::fetch_messages(c->id, 0);
        }

        ImGui::PopID();
    }
}

void ui_view_channel_list(float width, float height)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetWindowPos();
    dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), col::bg_panel);

    ImGui::BeginChild("##listscroll", ImVec2(width, height), false);
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::Indent(8.0f);

    if (g_ui.active_guild == 0)
    {
        ImGui::PushStyleColor(ImGuiCol_Header, ImGui::ColorConvertU32ToFloat4(col::bg_active));
        if (ImGui::Selectable(tr("Друзья"), g_ui.show_friends, 0, ImVec2(width - 16.0f, 30.0f)))
            g_ui.show_friends = true;
        ImGui::PopStyleColor();

        ImGui::Dummy(ImVec2(0, 6));
        ui_text_muted(tr("ЛИЧНЫЕ СООБЩЕНИЯ"));
        ImGui::Dummy(ImVec2(0, 2));

        const ulist<snowflake>& dms = store::dm_order();
        for (unsigned int i = 0; i < dms.count; i++)
        {
            dchannel* c = store::find_channel(dms[i]);
            if (!c) continue;
            dm_row(c, !g_ui.show_friends && g_ui.active_channel == c->id, width);
        }

        if (dms.count == 0) ui_text_muted(tr("Пока пусто"));
    }
    else
    {
        dguild* g = store::find_guild(g_ui.active_guild);
        if (g)
        {
            ImGui::PushFont(g_app.font_bold);
            ImGui::TextWrapped("%s", g->name ? g->name : tr("Сервер"));
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(0, 6));

            if (!g->loaded)
            {
                ui_text_muted(tr("Загрузка каналов..."));
            }

            // What to draw, in the order to draw it: the channels that
            // belong to no category first, then each category followed by
            // its own.
            //
            // Worked out up front rather than by walking the list twice and
            // printing a heading before whichever child came first. That way
            // round, a category with nothing in it had nowhere to be printed
            // and simply did not exist - which is also why one could never be
            // dragged anywhere or renamed.
            static ulist<snowflake> rows;      // a category id means a heading
            rows.clear_fast();

            for (unsigned int i = 0; i < g->channels.count; i++)
            {
                dchannel* c = store::find_channel(g->channels[i]);
                if (!c || c->type == CH_CATEGORY || c->parent_id) continue;
                rows.push(c->id);
            }

            for (unsigned int i = 0; i < g->channels.count; i++)
            {
                dchannel* cat = store::find_channel(g->channels[i]);
                if (!cat || cat->type != CH_CATEGORY) continue;

                rows.push(cat->id);

                for (unsigned int k = 0; k < g->channels.count; k++)
                {
                    dchannel* c = store::find_channel(g->channels[k]);
                    if (!c || c->type == CH_CATEGORY) continue;
                    if (c->parent_id != cat->id) continue;
                    rows.push(c->id);
                }
            }

            {
                for (unsigned int i = 0; i < rows.count; i++)
                {
                    dchannel* c = store::find_channel(rows[i]);
                    if (!c) continue;

                    if (c->type == CH_CATEGORY)
                    {
                        bool cat_hidden = !store::can_view_channel(g, store::self_id(), c);
                        if (cat_hidden && !g_ui.show_hidden_channels) continue;

                        ImGui::PushID((const void*)(size_t)c->id);
                        category_row(c, g, width, cat_hidden);
                        ImGui::PopID();
                        continue;
                    }

                    if (!c->is_textual() && c->type != CH_GUILD_VOICE && c->type != CH_STAGE) continue;

                    ImGui::PushID((const void*)(size_t)c->id);
                    bool active = (g_ui.active_channel == c->id);

                    bool hidden = !store::can_view_channel(g, store::self_id(), c);
                    if (hidden && !g_ui.show_hidden_channels) { ImGui::PopID(); continue; }

                    bool clicked = channel_row(c, active, width, hidden);

                    // Dragging one channel onto another puts it where that one
                    // is. The payload is the id rather than the index, because
                    // the list is walked in two passes and an index means
                    // nothing outside the pass that produced it.
                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoDisableHover))
                    {
                        ImGui::SetDragDropPayload("IMD_CHANNEL", &c->id, sizeof(snowflake));

                        char label[160];
                        ui_channel_display_name(c, label, sizeof(label));
                        ImGui::TextUnformatted(label);

                        ImGui::EndDragDropSource();
                    }

                    if (ImGui::BeginDragDropTarget())
                    {
                        const ImGuiPayload* p = ImGui::AcceptDragDropPayload("IMD_CHANNEL");
                        if (p && p->DataSize == (int)sizeof(snowflake))
                        {
                            snowflake moved = *(const snowflake*)p->Data;
                            if (moved != c->id) dropped_on(g, moved, c->id);
                        }

                        // A person dropped here rather than a channel. The
                        // same row answers both because that is where the eye
                        // goes: the destination is the channel, whatever is
                        // being carried.
                        const ImGuiPayload* who = ImGui::AcceptDragDropPayload("IMD_VOICE_MEMBER");
                        if (who && who->DataSize == (int)sizeof(snowflake))
                        {
                            snowflake moved = *(const snowflake*)who->Data;
                            if (ui_can_move_member(g->id, moved, c->id))
                                api::voice_move(g->id, moved, c->id);
                        }

                        ImGui::EndDragDropTarget();
                    }

                    // A channel that cannot be read opens its properties
                    // instead of itself. Trying to load messages would only
                    // earn a 403 and an empty view.
                    bool open_chat = clicked && !hidden;
                    if (clicked && hidden)
                    {
                        g_ui.channel_info_id = c->id;
                        g_ui.open_channel_info_popup = true;
                    }

                    // A voice channel carries a text chat of its own, and
                    // reading it is not the same as walking into the call.
                    // Left click still joins, because that is what every
                    // other client does; right click opens just the chat.
                    if (ImGui::BeginPopupContextItem("##chctx"))
                    {
                        if (ImGui::MenuItem(tr("Свойства канала")))
                        {
                            g_ui.channel_info_id = c->id;
                            g_ui.open_channel_info_popup = true;
                        }
                        if (ImGui::MenuItem(tr("Переименовать"))) ui_open_rename_channel(c->id);
                        if (ImGui::MenuItem(tr("Права канала"))) ui_open_channel_perms(c->id);
                        if (ImGui::MenuItem(tr("Удалить канал"))) api::delete_channel(c->id);
                        ImGui::Separator();
                        ui_copy_id_item(c->id, tr("Скопировать ID канала"));
                        ui_copy_id_item(g->id, tr("Скопировать ID сервера"));
                        ImGui::EndPopup();
                    }

                    if (c->is_voice() && !hidden && ImGui::BeginPopupContextItem("##vcctx"))
                    {
                        if (ImGui::MenuItem(tr("Открыть чат канала"))) open_chat = true;

                        if (voice::current_channel() == c->id)
                        {
                            if (ImGui::MenuItem(tr("Выйти из канала")))
                                { g_ui.pending_voice_leave = true; g_ui.pending_voice_join = false; }
                        }
                        else if (ImGui::MenuItem(tr("Зайти в канал")))
                        {
                            {
                                g_ui.pending_voice_guild = g->id;
                                g_ui.pending_voice_channel = c->id;
                                g_ui.pending_voice_join = true;
                            }
                            open_chat = true;
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem(tr("Переименовать"))) ui_open_rename_channel(c->id);
                        if (ImGui::MenuItem(tr("Права канала"))) ui_open_channel_perms(c->id);
                        if (ImGui::MenuItem(tr("Удалить канал"))) api::delete_channel(c->id);
                        ImGui::Separator();
                        ui_copy_id_item(c->id, tr("Скопировать ID канала"));
                        ui_copy_id_item(g->id, tr("Скопировать ID сервера"));
                        ImGui::EndPopup();
                    }

                    if (clicked && c->is_voice() && !hidden)
                    {
                        // Joining and opening its chat are one action, the
                        // way every other client does it.
                        {
                            g_ui.pending_voice_guild = g->id;
                            g_ui.pending_voice_channel = c->id;
                            g_ui.pending_voice_join = true;
                        }
                    }

                    if (open_chat)
                    {
                        g_ui.active_channel = c->id;
                        g_ui.show_friends = false;
                        science::channel_opened(c->id, g->id);
                        g_ui.scroll_to_bottom = true;
                        g_ui.reply_to = 0;
                        if (!c->history_loaded) api::fetch_messages(c->id, 0);
                        gateway::subscribe_guild(g->id, c->id);
                    }

                    // Occupants of a voice channel are listed underneath it.
                    if (c->type == CH_GUILD_VOICE || c->type == CH_STAGE)
                    {
                        static ulist<snowflake> occupants;
                        store::users_in_voice(c->id, &occupants);
                        for (unsigned int k = 0; k < occupants.count; k++)
                        {
                            duser* u = store::find_user(occupants[k]);
                            if (!u) continue;

                            bool self = u->id == store::self_id();
                            bool quieted = !self && voice::user_muted(u->id);

                            ImGui::PushID((const void*)(size_t)u->id);
                            ImGui::Indent(18.0f);
                            ImVec2 p = ImGui::GetCursorScreenPos();
                            ui_avatar(u, 18.0f, false);

                            float level = voice::speaking_level(u->id);
                            if (level > 0.02f)
                                ImGui::GetWindowDrawList()->AddCircle(ImVec2(p.x + 9, p.y + 9), 10.0f,
                                                                     quieted ? col::text_muted : col::green,
                                                                     0, 2.0f);

                            // The badges and the LIVE button sit on the same
                            // line after the name, so a long name used to carry
                            // them past the sidebar's edge and the button was
                            // gone. The name gives up what they need instead.
                            const dvoice_state* mvs = store::find_voice_state(u->id);
                            bool mic_off = mvs && (mvs->self_mute || mvs->mute);
                            bool ears_off = mvs && (mvs->self_deaf || mvs->deaf);
                            bool shows_live = mvs && mvs->self_stream && !self;

                            float reserve = 0.0f;
                            if (mic_off || ears_off)
                                reserve += 6.0f + ((mic_off && ears_off) ? 30.0f : 14.0f);
                            if (shows_live)
                                reserve += 6.0f + ImGui::CalcTextSize("LIVE").x + 10.0f;
                            if (voice::camera_on(u->id))
                                reserve += 6.0f + ImGui::CalcTextSize("CAM").x + 10.0f;

                            float avail = ImGui::GetContentRegionAvail().x - reserve;
                            if (avail < 20.0f) avail = 20.0f;

                            char fitted[96];
                            const char* shown = fit_name(u->display_name(), avail,
                                                         fitted, sizeof(fitted));

                            // A button carrying the name rather than plain
                            // text. Text has no id of its own, and everything
                            // hung off this row - the profile, the menu, and
                            // now dragging somebody into another channel -
                            // wants one: drag and drop on an id-less item is
                            // keyed on where the item sits, which stops being
                            // true the moment the list underneath it moves.
                            //
                            // The button takes exactly the space the text
                            // would have, so the badges after it land where
                            // they always did.
                            ImVec2 name_size = ImGui::CalcTextSize(shown);
                            if (name_size.x < 1.0f) name_size.x = 1.0f;

                            ImGui::SameLine();
                            ImVec2 name_at = ImGui::GetCursorScreenPos();
                            ImGui::InvisibleButton("##name",
                                                   ImVec2(name_size.x, ImGui::GetTextLineHeight()));

                            ImGui::GetWindowDrawList()->AddText(
                                name_at, quieted ? col::red : col::text_muted, shown);

                            bool can_drag = ui_can_move_member(g->id, u->id, 0);

                            // Dragged onto another voice channel, they are
                            // moved into it. Offered only when it would work:
                            // a drag that can only ever be refused is worse
                            // than no drag at all.
                            if (can_drag && ImGui::BeginDragDropSource())
                            {
                                ImGui::SetDragDropPayload("IMD_VOICE_MEMBER", &u->id, sizeof(snowflake));
                                ImGui::TextUnformatted(u->display_name());
                                ImGui::EndDragDropSource();
                            }

                            // On release, and only if the pointer stayed put.
                            // Opening the profile on the press - which is what
                            // IsItemClicked does - put a modal window up the
                            // instant the button went down, and a drag can
                            // never begin underneath one.
                            ImGuiIO& io = ImGui::GetIO();
                            float travel = io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left];
                            bool dragged = travel >= io.MouseDragThreshold * io.MouseDragThreshold;

                            if (ImGui::IsItemHovered() &&
                                ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !dragged)
                                ui_open_profile(u->id, g->id);

                            if (ImGui::IsItemHovered() && !ImGui::IsPopupOpen("##vmemctx"))
                            {
                                const char* hint = can_drag
                                    ? tr("ЛКМ - профиль, ПКМ - звук, тянуть - перенести")
                                    : tr("ЛКМ - профиль, ПКМ - звук");

                                if (shown != u->display_name())
                                    ImGui::SetTooltip("%s\n%s", u->display_name(), hint);
                                else
                                    ImGui::SetTooltip("%s", hint);
                            }

                            ui_voice_member_menu(u, g->id, self);

                            // Muted or deafened, by their own hand or by a
                            // moderator. Without this the list shows somebody
                            // sitting in the channel with no hint that they
                            // cannot answer.
                            if (mic_off || ears_off)
                            {
                                ImGui::SameLine(0, 6);
                                ImVec2 mark = ImGui::GetCursorScreenPos();
                                ui_draw_muted_marks(ImGui::GetWindowDrawList(),
                                                    ImVec2(mark.x, mark.y + 2.0f), 13.0f,
                                                    mic_off, ears_off);
                                ImGui::Dummy(ImVec2((mic_off && ears_off) ? 30.0f : 14.0f, 13.0f));

                                if (ImGui::IsItemHovered())
                                    ImGui::SetTooltip(ears_off ? tr("Звук выключен полностью")
                                                               : tr("Микрофон выключен"));
                            }

                            // A camera, when they have one on. Same idea as the
                            // LIVE badge and deliberately next to it: both are
                            // "this person has a picture", and both need the
                            // person picked before anything can be shown.
                            if (voice::camera_on(u->id))
                            {
                                bool watching = voice::watched_camera() == u->id;

                                ImGui::SameLine(0, 6);
                                ImGui::PushStyleColor(ImGuiCol_Button, watching ? col::accent : col::bg_panel);
                                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, watching ? col::accent : col::bg_hover);
                                ImGui::PushStyleColor(ImGuiCol_ButtonActive, col::bg_active);
                                ImGui::PushStyleColor(ImGuiCol_Text, col::text_normal);
                                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5, 1));

                                if (ImGui::SmallButton("CAM"))
                                {
                                    // The decoder is one instance, so opening a
                                    // camera closes whatever else was using it.
                                    if (watching) voice::watch_camera(0);
                                    else
                                    {
                                        if (streamview::watching_user()) streamview::stop();
                                        voice::watch_camera(u->id);
                                    }
                                }

                                ImGui::PopStyleVar();
                                ImGui::PopStyleColor(4);

                                if (ImGui::IsItemHovered())
                                    ImGui::SetTooltip(watching ? tr("Закрыть камеру")
                                                               : tr("Смотреть камеру"));
                            }

                            // Anybody sharing their screen gets a badge that
                            // opens it. Watching is offered from the list
                            // rather than the voice panel because the person
                            // has to be picked, and this is where they are.
                            if (shows_live)
                            {
                                bool watching = streamview::watching_user() == u->id;

                                ImGui::SameLine(0, 6);
                                ImGui::PushStyleColor(ImGuiCol_Button, watching ? col::green : col::bg_panel);
                                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, watching ? col::green : col::bg_hover);
                                ImGui::PushStyleColor(ImGuiCol_ButtonActive, col::bg_active);
                                ImGui::PushStyleColor(ImGuiCol_Text, watching ? col::text_normal : col::red);
                                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5, 1));

                                if (ImGui::SmallButton("LIVE"))
                                {
                                    if (watching) streamview::stop();
                                    else          streamview::watch(g->id, c->id, u->id);
                                }

                                ImGui::PopStyleVar();
                                ImGui::PopStyleColor(4);

                                if (ImGui::IsItemHovered())
                                    ImGui::SetTooltip(watching ? tr("Закрыть демонстрацию")
                                                               : tr("Смотреть демонстрацию"));
                            }

                            ImGui::Unindent(18.0f);
                            ImGui::PopID();
                        }
                    }

                    ImGui::PopID();
                }
            }
        }
    }

    ImGui::Unindent(8.0f);
    ImGui::Dummy(ImVec2(0, 12));
    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// bottom user panel
// ---------------------------------------------------------------------------

float ui_voice_panel_height()
{
    bool in_voice = voice::state() == VOICE_CONNECTED || voice::state() == VOICE_CONNECTING;
    if (in_voice) return USER_ROW_HEIGHT + VOICE_ROW_HEIGHT;

    // Room for the one line explaining why the last call ended.
    if (voice::last_stop_reason()[0]) return USER_ROW_HEIGHT + STOPPED_ROW_HEIGHT;
    return USER_ROW_HEIGHT;
}

void ui_view_voice_panel(float width)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();

    bool in_voice = voice::state() == VOICE_CONNECTED || voice::state() == VOICE_CONNECTING;

    // The voice bar sits above the user row so its disconnect button stays
    // inside the panel instead of being clipped by the window edge.
    if (in_voice)
    {
        dl->AddRectFilled(p, ImVec2(p.x + width, p.y + VOICE_ROW_HEIGHT), col::bg_deep);
        dl->AddLine(ImVec2(p.x + 8.0f, p.y + VOICE_ROW_HEIGHT),
                    ImVec2(p.x + width - 8.0f, p.y + VOICE_ROW_HEIGHT), col::separator);

        bool connected = voice::state() == VOICE_CONNECTED;
        dchannel* vc = store::find_channel(voice::current_channel());

        char title[160];
        if (vc)
        {
            char name[128];
            ui_channel_display_name(vc, name, sizeof(name));
            cnprint(title, sizeof(title), "%s", name);
        }
        else
        {
            ccstrncpy(title, tr("Голосовой канал"), sizeof(title) - 1);
        }

        screenshare_state share = screenshare::state();
        bool sharing = share != SHARE_IDLE && share != SHARE_FAILED;

        // Three buttons now sit on the right of this row, so the labels are
        // cut off before they can run underneath them.
        dl->PushClipRect(ImVec2(p.x + 12.0f, p.y),
                         ImVec2(p.x + width - 128.0f, p.y + VOICE_ROW_HEIGHT), true);

        // While a share is running its own state is the more interesting of the
        // two, so it takes the top line and the channel name stays below.
        if (sharing)
            dl->AddText(ImVec2(p.x + 12.0f, p.y + 7.0f),
                        share == SHARE_LIVE ? col::green : col::yellow,
                        screenshare::status_text());
        else if (share == SHARE_FAILED)
            dl->AddText(ImVec2(p.x + 12.0f, p.y + 7.0f), col::red, screenshare::status_text());
        else
            dl->AddText(ImVec2(p.x + 12.0f, p.y + 7.0f), connected ? col::green : col::yellow,
                        connected ? tr("Голос подключён") : tr("Подключение..."));

        dl->AddText(ImVec2(p.x + 12.0f, p.y + 25.0f), col::text_muted, title);
        dl->PopClipRect();

        // Music. First of the three because it is the one that is opened and
        // left alone, where the other two are reached for in a hurry.
        ImGui::SetCursorScreenPos(ImVec2(p.x + width - 122.0f, p.y + 10.0f));
        {
            bool on = music::playing();

            if (ui_glyph_button("##music", ICON_MUSIC, false, ImVec2(34, 26),
                                on ? col::green : col::bg_panel,
                                on ? col::green : col::bg_hover, col::text_normal))
                ui_open_music();

            if (ImGui::IsItemHovered())
            {
                if (on) ImGui::SetTooltip(tr("Играет: %s"), music::title());
                else    ImGui::SetTooltip(tr("Музыка в голосовой канал"));
            }
        }

        // Go Live. Only offered once the voice side is up, because the stream is
        // requested for the channel that connection is already sitting in.
        ImGui::SetCursorScreenPos(ImVec2(p.x + width - 84.0f, p.y + 10.0f));
        {
            ImU32 bg = sharing ? col::green : (share == SHARE_FAILED ? col::red : col::bg_panel);
            ImU32 fg = connected ? col::text_normal : col::text_muted;

            if (ui_glyph_button("##share", ICON_SCREEN, false, ImVec2(34, 26),
                                bg, sharing ? col::green : col::bg_hover, fg) && connected)
            {
                if (sharing)
                {
                    // While it is running the same box is where windows get
                    // covered up, which is the one thing somebody needs in a
                    // hurry mid-stream.
                    g_ui.open_share_popup = true;
                }
                else
                {
                    // A failed attempt leaves the machinery half up and start
                    // refuses while it is, so a retry tears it down first.
                    if (share == SHARE_FAILED) screenshare::stop();
                    g_ui.open_share_popup = true;
                }
            }

            if (ImGui::IsItemHovered())
            {
                if (!connected)            ImGui::SetTooltip(tr("Сначала дождись голоса"));
                else if (sharing)          ImGui::SetTooltip(tr("%s - нажми, чтобы настроить или закончить"),
                                                             screenshare::status_text());
                else if (share == SHARE_FAILED) ImGui::SetTooltip(tr("%s - нажми, чтобы попробовать снова"),
                                                                  screenshare::status_text());
                else                       ImGui::SetTooltip(tr("Демонстрация экрана"));
            }
        }

        ImGui::SetCursorScreenPos(ImVec2(p.x + width - 46.0f, p.y + 10.0f));
        if (ui_glyph_button("##hangup", ICON_HANGUP, false, ImVec2(34, 26),
                            col::red, col::red, col::text_normal))
        {
            if (sharing) screenshare::stop();
            g_ui.pending_voice_leave = true;
            g_ui.pending_voice_join = false;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Отключиться"));

        p.y += VOICE_ROW_HEIGHT;
        ImGui::SetCursorScreenPos(p);
    }

    // Why the last call ended, shown where the call used to be. The log has
    // been unreachable for several rounds running, and a dropped connection
    // that will not say who dropped it cannot be fixed by guessing.
    // Drawn as its own band at the top of the panel, and the cursor moved past
    // it, exactly as the voice row does. Drawing above the cursor put it on top
    // of the channel list and the profile row at the same time.
    if (!in_voice && voice::last_stop_reason()[0])
    {
        dl->AddRectFilled(p, ImVec2(p.x + width, p.y + STOPPED_ROW_HEIGHT), col::bg_deep);

        char first[96];
        char second[96];
        cnprint(first, sizeof(first), tr("голос закрыт, код %u"), voice::last_close_code());
        // Translated here rather than where it is set: the reason is kept as a
        // pointer to a literal, and one set before the language changed would
        // otherwise stay in the language it was set in.
        cnprint(second, sizeof(second), "%s", tr(voice::last_stop_reason()));

        dl->PushClipRect(p, ImVec2(p.x + width - 8.0f, p.y + STOPPED_ROW_HEIGHT), true);
        dl->AddText(ImVec2(p.x + 10.0f, p.y + 3.0f), col::red, first);
        dl->AddText(ImVec2(p.x + 10.0f, p.y + 17.0f), col::text_muted, second);
        dl->PopClipRect();

        p.y += STOPPED_ROW_HEIGHT;
        ImGui::SetCursorScreenPos(p);
    }

    float h = USER_ROW_HEIGHT;
    dl->AddRectFilled(p, ImVec2(p.x + width, p.y + h), col::bg_deep);

    duser* me = store::self();

    ImGui::SetCursorScreenPos(ImVec2(p.x + 8.0f, p.y + 8.0f));
    ui_avatar(me, 32.0f, true);

    // The avatar is the way into the account switcher, which is where the
    // profile now lives too. There is no room on this row for another button
    // and every other client puts it behind the avatar as well.
    if (ImGui::IsItemClicked()) g_ui.show_accounts = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Аккаунты"));

    dl->AddText(ImVec2(p.x + 48.0f, p.y + 10.0f), col::text_normal, me ? me->display_name() : "...");

    dl->AddText(ImVec2(p.x + 48.0f, p.y + 28.0f), col::text_muted, gateway::status_text());

    // Mute / deafen / settings buttons.
    ImGui::SetCursorScreenPos(ImVec2(p.x + width - 96.0f, p.y + 14.0f));

    bool muted = voice::muted();
    if (ui_glyph_button("##mute", ICON_MIC, muted, ImVec2(28, 28), col::bg_panel, col::bg_hover,
                        muted ? col::text_muted : col::text_normal))
        voice::set_muted(!muted);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(muted ? tr("Включить микрофон") : tr("Выключить микрофон"));

    ImGui::SameLine(0, 4);
    bool deaf = voice::deafened();
    if (ui_glyph_button("##deaf", ICON_HEADPHONES, deaf, ImVec2(28, 28), col::bg_panel, col::bg_hover,
                        deaf ? col::text_muted : col::text_normal))
        voice::set_deafened(!deaf);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(deaf ? tr("Включить звук") : tr("Отключить звук"));

    ImGui::SameLine(0, 4);
    if (ui_glyph_button("##settings", ICON_GEAR, false, ImVec2(28, 28), col::bg_panel, col::bg_hover, col::text_normal))
        g_ui.show_settings = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Настройки"));

    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
}

// ---------------------------------------------------------------------------
// settings
// ---------------------------------------------------------------------------

void ui_view_settings_popup()
{
    if (g_ui.show_settings)
    {
        ImGui::OpenPopup(tr("Настройки###settings"));
        g_ui.show_settings = false;

        // Asked once per opening, not on a timer: the list changes rarely
        // and costs one GET.
        if (store::sessions_state() == 0) api::fetch_sessions();
    }

    // Two columns: the left is everything you change, the right is everything
    // you read - account, sessions, call health, diagnostics. A fixed modest
    // size instead of growing to fit: both columns scroll on their own.
    ImGui::SetNextWindowSize(ImVec2(720, 520), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(tr("Настройки###settings"), 0, 0)) return;

    // ---- language ----
    //
    // Above the two columns rather than inside them, because somebody who
    // opened this window looking for it cannot read the headings that would
    // tell them where it is.
    {
        ui_lang chosen = lang::current();

        ImGui::SetNextItemWidth(200);
        if (ImGui::BeginCombo(tr("Язык"), lang::name(chosen)))
        {
            for (int i = 0; i < LANG_COUNT; i++)
                if (ImGui::Selectable(lang::name((ui_lang)i), i == (int)chosen))
                    lang::set((ui_lang)i);
            ImGui::EndCombo();
        }
    }

    ImGui::Dummy(ImVec2(0, 6));

    float cols_h = ImGui::GetContentRegionAvail().y - 42.0f;   // "Закрыть" row
    ImGui::BeginChild("##settings_left", ImVec2(336.0f, cols_h), false,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);

    // ---- appearance ----
    ImGui::TextUnformatted(tr("Внешний вид"));
    ImGui::Separator();

    {
        // Whole looks first: most people want a different one, not fifteen
        // different ones, and the list underneath is there for whoever does.
        ImGui::TextUnformatted(tr("Готовые темы"));

        for (int i = 0; i < theme::preset_count(); i++)
        {
            if (i && (i % 3)) ImGui::SameLine(0, 6);
            if (ui_icon_button(tr(theme::preset_name(i)), ImVec2(100, 26),
                               col::bg_panel, col::bg_hover))
                theme::apply_preset(i);
        }

        ImGui::Dummy(ImVec2(0, 8));

        // Shape. Both are fractions of the thing being drawn, so one number
        // holds at every size on the screen.
        float avatar = theme::avatar_rounding() * 200.0f;   // 0..100 reads better
        ImGui::SetNextItemWidth(200);
        if (ImGui::SliderFloat(tr("Форма аватарок"), &avatar, 0.0f, 100.0f, "%.0f%%"))
            theme::set_avatar_rounding(avatar / 200.0f);

        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(tr("0% - квадрат, 100% - круг"));

        float corner = theme::corner_rounding();
        ImGui::SetNextItemWidth(200);
        if (ImGui::SliderFloat(tr("Закругление углов"), &corner, 0.0f, 16.0f, "%.0f"))
            theme::set_corner_rounding(corner);

        ImGui::Dummy(ImVec2(0, 6));

        // Every colour, one per row. Folded away by default: fifteen pickers
        // open is a wall, and most people never touch them.
        if (ImGui::TreeNode(tr("Цвета по отдельности")))
        {
            for (int i = 0; i < theme::color_count(); i++)
            {
                theme::entry* e = theme::color_at(i);
                if (!e) continue;

                ImVec4 v = ImGui::ColorConvertU32ToFloat4(*e->value);

                ImGui::PushID(i);
                if (ImGui::ColorEdit3(tr(e->label), &v.x,
                                      ImGuiColorEditFlags_NoInputs |
                                      ImGuiColorEditFlags_AlphaPreview))
                {
                    *e->value = ImGui::ColorConvertFloat4ToU32(v);
                    storage::settings_set_int(e->key, (int)(unsigned int)*e->value);
                    storage::settings_save();
                    theme::apply();
                }
                ImGui::PopID();
            }

            ImGui::TreePop();
        }

        ImGui::Dummy(ImVec2(0, 6));
        if (ImGui::Button(tr("Сбросить оформление"), ImVec2(220, 28))) theme::reset();
    }

    ImGui::Dummy(ImVec2(0, 10));

    // ---- archive and export ----
    ImGui::TextUnformatted(tr("Архив переписки"));
    ImGui::Separator();

    {
        char line[192];
        cnprint(line, sizeof(line), tr("Сохранено сообщений за сеанс: %u, каналов: %u"),
                archive::total_messages(), archive::total_channels());
        ui_text_muted(line);

        unsigned long long age = archive::snapshot_age_seconds();
        if (age)
        {
            cnprint(line, sizeof(line), tr("Снимок серверов и друзей обновлён %llu мин назад"),
                    age / 60);
            ui_text_muted(line);
        }
    }

    static bool save_files = false;
    ImGui::Checkbox(tr("Сохранять вложения рядом с файлом"), &save_files);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(tr("Иначе в экспорт попадут только ссылки. Ссылки discord со временем "
                          "перестают открываться, копии остаются навсегда."));

    export_attachments attach_mode = save_files ? EXPORT_SAVE_FILES : EXPORT_LINKS_ONLY;

    if (ImGui::Button(tr("Экспортировать этот канал"), ImVec2(-1, 0)) && g_ui.active_channel)
    {
        wchar_t suggested[64];
        chartowcs("chat.html", suggested, 64);

        wchar_t chosen[MAX_PATH];
        if (ufile::save_dialog(suggested, chosen, MAX_PATH))
        {
            if (exporter::channel_to_html(g_ui.active_channel, chosen, attach_mode))
                api::set_last_error(tr("Экспорт готов"));
            else
                api::set_last_error(tr("Экспортировать не удалось"));
        }
    }

    if (ImGui::Button(tr("Экспортировать весь архив"), ImVec2(-1, 0)))
    {
        wchar_t suggested[64];
        chartowcs("export.html", suggested, 64);

        wchar_t chosen[MAX_PATH];
        if (ufile::save_dialog(suggested, chosen, MAX_PATH))
        {
            // The chosen name marks the folder; every channel becomes a file
            // inside it.
            int cut = 0;
            for (int i = 0; chosen[i]; i++)
                if (chosen[i] == L'\\' || chosen[i] == L'/') cut = i;
            chosen[cut] = 0;

            int n = exporter::everything_to_html(chosen, attach_mode);

            char done[128];
            cnprint(done, sizeof(done), tr("Записано каналов: %d"), n);
            api::set_last_error(done);
        }
    }

    ImGui::Dummy(ImVec2(0, 6));

    if (exporter::warming())
    {
        char line[224];
        cnprint(line, sizeof(line), tr("Прогрев: %u из %u каналов, сообщений %u — %s"),
                exporter::warm_channels_done(), exporter::warm_channels_total(),
                exporter::warm_messages(), exporter::warm_status());
        ui_text_muted(line);

        if (ImGui::Button(tr("Остановить прогрев"), ImVec2(-1, 0))) exporter::warm_stop();
    }
    else
    {
        ImGui::TextUnformatted(tr("Что прогревать"));

        int scope = (int)exporter::warm_current_scope();
        bool changed = false;

        changed |= ImGui::RadioButton(tr("Только личные чаты"), &scope, exporter::WARM_DIRECT_ONLY);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(tr("По умолчанию. Один людный сервер бывает больше всей личной "
                              "переписки вместе взятой, а нужна обычно именно она."));

        changed |= ImGui::RadioButton(tr("Всё подряд"), &scope, exporter::WARM_EVERYTHING);
        changed |= ImGui::RadioButton(tr("Выбранное вручную"), &scope, exporter::WARM_SELECTED);

        if (changed) exporter::warm_set_scope((exporter::warm_scope)scope);

        if (scope == exporter::WARM_SELECTED)
        {
            ImGui::BeginChild("##warmpick", ImVec2(-1, 190), true);

            store::guard guard;

            // Servers first, each with a tick that takes all of its channels.
            const ulist<snowflake>& guilds = store::guild_order();
            for (unsigned int i = 0; i < guilds.count; i++)
            {
                dguild* g = store::find_guild(guilds[i]);
                if (!g) continue;

                ImGui::PushID((const void*)(size_t)g->id);

                bool whole = exporter::warm_guild_fully_selected(g->id);
                if (ImGui::Checkbox(g->name ? g->name : tr("сервер"), &whole))
                    exporter::warm_select_guild(g->id, whole);

                ImGui::Indent(18.0f);
                for (unsigned int k = 0; k < g->channels.count; k++)
                {
                    dchannel* c = store::find_channel(g->channels[k]);
                    if (!c || !c->is_textual()) continue;

                    ImGui::PushID((const void*)(size_t)c->id);

                    char label[160];
                    ui_channel_display_name(c, label, sizeof(label));

                    bool on = exporter::warm_is_selected(c->id);
                    if (ImGui::Checkbox(label, &on)) exporter::warm_select(c->id, on);

                    ImGui::PopID();
                }
                ImGui::Unindent(18.0f);
                ImGui::PopID();
            }

            // Then the private conversations.
            const ulist<snowflake>& dms = store::dm_order();
            if (dms.count)
            {
                ImGui::Separator();
                ui_text_muted(tr("Личные чаты"));
            }

            for (unsigned int i = 0; i < dms.count; i++)
            {
                dchannel* c = store::find_channel(dms[i]);
                if (!c) continue;

                ImGui::PushID((const void*)(size_t)c->id);

                char label[160];
                ui_channel_display_name(c, label, sizeof(label));

                bool on = exporter::warm_is_selected(c->id);
                if (ImGui::Checkbox(label, &on)) exporter::warm_select(c->id, on);

                ImGui::PopID();
            }

            ImGui::EndChild();

            char picked[96];
            cnprint(picked, sizeof(picked), tr("Отмечено каналов: %u"),
                    exporter::warm_selection_count());
            ui_text_muted(picked);

            ImGui::SameLine();
            if (ImGui::SmallButton(tr("Снять всё"))) exporter::warm_clear_selection();
        }

        char go[128];
        cnprint(go, sizeof(go), tr("Прогреть (%u каналов)"), exporter::warm_planned_count());

        if (ImGui::Button(go, ImVec2(-1, 0))) exporter::warm_start();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(tr("Прочитать историю и сложить её в архив. Долго и много "
                              "запросов, зато потом всё открывается мгновенно и читается "
                              "без сети."));
    }

    ImGui::Dummy(ImVec2(0, 10));
    ImGui::TextUnformatted(tr("Устройства"));
    ImGui::Separator();

    // The device lists are refreshed while the popup is open, so hot-plugging a
    // headset shows up without restarting.
    static audio_device inputs[32];
    static audio_device outputs[32];
    static int input_count = 0;
    static int output_count = 0;
    static unsigned long long last_refresh = 0;

    unsigned long long now = GetTickCount64();
    if (now - last_refresh > 2000)
    {
        input_count = audio::list_devices(true, inputs, 32);
        output_count = audio::list_devices(false, outputs, 32);
        last_refresh = now;
    }

    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##mic", audio::device_name(true)))
    {
        for (int i = 0; i < input_count; i++)
        {
            bool selected = ccwcmp(inputs[i].id, audio::device(true)) == 0;
            if (ImGui::Selectable(inputs[i].name, selected))
            {
                audio::set_device(true, inputs[i].id);
                storage::settings_set("input_device", "");
                char utf8[512];
                wcstochar(inputs[i].id, utf8, sizeof(utf8));
                storage::settings_set("input_device", utf8);
            }
        }
        ImGui::EndCombo();
    }
    ui_text_muted(tr("Микрофон"));

    ImGui::Dummy(ImVec2(0, 4));
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##speakers", audio::device_name(false)))
    {
        for (int i = 0; i < output_count; i++)
        {
            bool selected = ccwcmp(outputs[i].id, audio::device(false)) == 0;
            if (ImGui::Selectable(outputs[i].name, selected))
            {
                audio::set_device(false, outputs[i].id);
                char utf8[512];
                wcstochar(outputs[i].id, utf8, sizeof(utf8));
                storage::settings_set("output_device", utf8);
            }
        }
        ImGui::EndCombo();
    }
    ui_text_muted(tr("Вывод звука"));

    ImGui::Dummy(ImVec2(0, 10));
    ImGui::TextUnformatted(tr("Шумоподавление"));
    ImGui::Separator();

    const char* modes[] = { tr("Выключено"), "Noise gate", "SpeexDSP", "RNNoise" };
    int mode = noise::mode();

    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##noise", modes[mode & 3]))
    {
        for (int i = 0; i < 4; i++)
        {
            if (ImGui::Selectable(modes[i], mode == i))
            {
                noise::set_mode(i);
                storage::settings_set_int("noise_mode", i);
            }
        }
        ImGui::EndCombo();
    }

    if (noise::mode() == NOISE_GATE)
    {
        float threshold = noise::gate_threshold();
        if (ImGui::SliderFloat(tr("Порог"), &threshold, 0.0f, 0.2f, "%.3f"))
        {
            noise::set_gate_threshold(threshold);
            storage::settings_set_int("gate_threshold", (int)(threshold * 1000.0f));
        }
    }

    ImGui::Dummy(ImVec2(0, 10));
    ImGui::TextUnformatted(tr("Чувствительность"));
    ImGui::Separator();

    bool vad_on = vad::enabled();
    if (ImGui::Checkbox(tr("Определять голос"), &vad_on))
    {
        vad::set_enabled(vad_on);
        storage::settings_set_int("vad_enabled", vad_on ? 1 : 0);
    }
    ui_text_muted(vad_on ? tr("Передача только когда ты говоришь")
                         : tr("Микрофон открыт постоянно"));

    if (vad_on)
    {
        bool automatic = vad::automatic();
        if (ImGui::RadioButton(tr("Автоматически"), automatic))
        {
            vad::set_automatic(true);
            storage::settings_set_int("vad_auto", 1);
        }
        ImGui::SameLine();
        if (ImGui::RadioButton(tr("Вручную"), !automatic))
        {
            vad::set_automatic(false);
            storage::settings_set_int("vad_auto", 0);
        }

        if (!vad::automatic())
        {
            float thr = vad::threshold();
            if (ImGui::SliderFloat("##vadthr", &thr, 0.0f, 0.15f, tr("порог %.3f")))
            {
                vad::set_threshold(thr);
                storage::settings_set_int("vad_threshold", (int)(thr * 1000.0f));
            }
        }
        else
        {
            ui_text_muted(tr("Порог сам подстраивается под тишину в комнате"));
        }

        // The meter is the only way to set a threshold without guessing:
        // talk, watch where the bar lands, put the line under it. The bar
        // turns while the gate is open so it is obvious what is going out.
        ImVec2 at = ImGui::GetCursorScreenPos();
        float width = ImGui::GetContentRegionAvail().x;
        const float BAR = 12.0f;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(at, ImVec2(at.x + width, at.y + BAR),
                          IM_COL32(30, 32, 38, 255), 3.0f);

        // Loudness is compressed low down, so a linear bar shows speech as a
        // sliver at the far left. A square root spreads the quiet end out.
        float lvl = vad::level();
        if (lvl > 0.4f) lvl = 0.4f;
        if (lvl < 0.0f) lvl = 0.0f;
        float shown = csqrtf(lvl / 0.4f);

        dl->AddRectFilled(at, ImVec2(at.x + width * shown, at.y + BAR),
                          vad::open() ? IM_COL32(67, 181, 129, 255)
                                      : IM_COL32(90, 94, 104, 255), 3.0f);

        float line = vad::active_threshold();
        if (line > 0.4f) line = 0.4f;
        if (line < 0.0f) line = 0.0f;
        float lx = csqrtf(line / 0.4f);

        dl->AddLine(ImVec2(at.x + width * lx, at.y - 1.0f),
                    ImVec2(at.x + width * lx, at.y + BAR + 1.0f),
                    IM_COL32(240, 200, 90, 255), 2.0f);

        ImGui::Dummy(ImVec2(width, BAR + 4.0f));
    }

    ImGui::Dummy(ImVec2(0, 10));
    ImGui::TextUnformatted(tr("Громкость"));
    ImGui::Separator();

    float in_gain = audio::input_gain();
    if (ImGui::SliderFloat(tr("Микрофон"), &in_gain, 0.0f, 2.0f, "%.2f"))
        audio::set_input_gain(in_gain);

    float out_gain = audio::output_gain();
    if (ImGui::SliderFloat(tr("Динамики"), &out_gain, 0.0f, 2.0f, "%.2f"))
        audio::set_output_gain(out_gain);

    ImGui::Dummy(ImVec2(0, 10));
    ImGui::TextUnformatted(tr("Приватность"));
    ImGui::Separator();

    {
        // Discord's own analytics. Four settings rather than a switch,
        // because the events are not alike: reporting that the audit log was
        // opened is the same kind of fact as opening it, while reporting that
        // the pointer moved over a picture, or that speech began, is watching
        // the person. The middle setting is that line.
        int mode = (int)science::mode();

        // A bot sends none of it whatever this says, so the control says so
        // rather than sitting there offering a choice that does nothing.
        if (api::is_bot())
        {
            ui_text_muted(tr("Режим бота: телеметрия не отправляется"));
        }
        else
        {

        ImGui::SetNextItemWidth(240);
        if (ImGui::BeginCombo(tr("Телеметрия Discord"), science::mode_name((science_mode)mode)))
        {
            for (int i = 0; i <= SCIENCE_ALL; i++)
                if (ImGui::Selectable(science::mode_name((science_mode)i), i == mode))
                    science::set_mode((science_mode)i);
            ImGui::EndCombo();
        }

        ImGui::PushTextWrapPos(0.0f);
        ui_text_muted(science::mode_note((science_mode)mode));
        ImGui::PopTextWrapPos();

        }
    }

    if (ImGui::Button(tr("Настроить приватность"), ImVec2(220, 28))) ui_open_privacy();
    ui_text_muted(tr("Кто может писать вам в личные сообщения"));

    ImGui::Dummy(ImVec2(0, 10));
    ImGui::TextUnformatted(tr("Звуки"));
    ImGui::Separator();

    {
        // Its own volume, not the call's: a chime at the level of somebody's
        // voice is startling, and the two are turned up and down for quite
        // different reasons.
        float snd = sounds::volume();
        if (ImGui::SliderFloat(tr("Громкость звуков"), &snd, 0.0f, 1.0f, "%.2f"))
            sounds::set_volume(snd);

        struct { ui_sound id; const char* label; } rows[] = {
            { SOUND_VOICE_JOIN,   tr("Заход в голосовой") },
            { SOUND_VOICE_LEAVE,  tr("Выход из голосового") },
            { SOUND_STREAM_START, tr("Начало демонстрации") },
            { SOUND_STREAM_STOP,  tr("Конец демонстрации") },
            { SOUND_NOTIFY,       tr("Личное сообщение") },
        };

        for (int i = 0; i < 5; i++)
        {
            bool on = sounds::enabled(rows[i].id);

            ImGui::PushID(i);
            if (ImGui::Checkbox(rows[i].label, &on)) sounds::set_enabled(rows[i].id, on);
            ImGui::SameLine();
            if (ImGui::SmallButton(tr("Проверить"))) sounds::play(rows[i].id);
            ImGui::PopID();
        }
    }

    ImGui::Dummy(ImVec2(0, 10));
    ImGui::TextUnformatted(tr("Каналы"));
    ImGui::Separator();

    {
        bool show = g_ui.show_hidden_channels;
        if (ImGui::Checkbox(tr("Показывать закрытые каналы"), &show))
        {
            g_ui.show_hidden_channels = show;
            storage::settings_set_int("show_hidden_channels", show ? 1 : 0);
        }
        ui_text_muted(g_ui.show_hidden_channels
                          ? tr("С замком - те, куда нет доступа. Клик открывает свойства")
                          : tr("Видно только то, что можно читать"));
    }

    ImGui::Dummy(ImVec2(0, 10));
    ImGui::TextUnformatted(tr("Картинки"));
    ImGui::Separator();

    const char* formats[] = { tr("PNG по возможности"), tr("Всегда WebP") };
    int format = ui_image_format();

    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##imgformat", formats[format & 1]))
    {
        for (int i = 0; i < 2; i++)
        {
            if (ImGui::Selectable(formats[i], format == i)) ui_set_image_format(i);
        }
        ImGui::EndCombo();
    }
    ImGui::PushStyleColor(ImGuiCol_Text, col::text_muted);
    ImGui::TextWrapped(tr("PNG запрашивается у прокси discord и приходит без потерь, "
                       "но стоит им лишней перекодировки. WebP отдаётся как есть. "
                       "Гифки и анимированный WebP настройка не трогает: у прокси "
                       "от них остался бы один кадр."));
    ImGui::PopStyleColor();

    ImGui::Dummy(ImVec2(0, 6));

    bool emoji = ui_custom_emoji();
    if (ImGui::Checkbox(tr("Показывать серверные смайлики картинками"), &emoji))
        ui_set_custom_emoji(emoji);
    ui_text_muted(emoji ? tr("Каждый смайлик - отдельная картинка из сети")
                        : tr("Выключено - смайлик остаётся текстом вида <:имя:id>"));

    bool uni = ui_unicode_emoji();
    if (ImGui::Checkbox(tr("Показывать обычные смайлики картинками"), &uni))
        ui_set_unicode_emoji(uni);

    ImGui::PushStyleColor(ImGuiCol_Text, col::text_muted);
    ImGui::TextWrapped(tr("Обычные смайлики (те, что просто символы) шрифт этого клиента "
                       "нарисовать не может, поэтому они берутся картинками с постороннего "
                       "сайта - не discord. Об аккаунте он ничего не узнает, запрос идёт за "
                       "файлом с именем из кода символа, но сам запрос он видит."));
    ImGui::PopStyleColor();

    ImGui::Dummy(ImVec2(0, 6));

    bool direct_gifs = ui_embed_direct_gifs();
    if (ImGui::Checkbox(tr("Подгружать гифки с ссылок"), &direct_gifs))
        ui_set_embed_direct_gifs(direct_gifs);

    bool video = ui_video_player();
    if (ImGui::Checkbox(tr("Проигрывать mp4 прямо в чате"), &video))
        ui_set_video_player(video);
    ui_text_muted(video ? tr("Сырое: звук уходит вперёд, перемотка грубая")
                        : tr("Выключено - видео показывается кадром со ссылкой на скачивание"));

    ImGui::PushStyleColor(ImGuiCol_Text, col::text_muted);
    ImGui::TextWrapped(tr("Гифку с чужого сайта discord пропускает через свой прокси, а тот "
                       "оставляет от неё первый кадр. С галочкой такая картинка берётся "
                       "прямо с сайта и играет целиком; сайт при этом видит запрос от тебя, "
                       "а не от discord. Картинок и вложений самого discord это не касается."));
    ImGui::PopStyleColor();

    ImGui::Dummy(ImVec2(0, 10));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 6));

    // Channels and members arrive when a server is looked at, and only then.
    // Everything built out of who is where knows about the servers that have
    // been opened and no others, which for somebody in thirty of them means
    // opening thirty of them by hand.
    if (warmup::running())
    {
        char line[160];
        cnprint(line, sizeof(line), tr("Прогреваю: %d из %d"),
                warmup::done(), warmup::total());
        ui_text_muted(line);

        const char* now = warmup::current();
        if (now && now[0]) ui_text_muted(now);

        if (ImGui::Button(tr("Остановить"), ImVec2(200, 30))) warmup::stop();
    }
    else
    {
        if (ImGui::Button(tr("Прогреть сервера"), ImVec2(200, 30))) warmup::start();

        ImGui::PushStyleColor(ImGuiCol_Text, col::text_muted);
        ImGui::TextWrapped(tr("Пройдёт по всем серверам и загрузит каналы и участников, "
                           "как если бы каждый был открыт руками. Полторы секунды на "
                           "сервер - медленно нарочно, иначе discord ограничит скорость "
                           "и всему остальному тоже. Нужно для \"общих серверов\" в "
                           "профиле: они знают только те сервера, которые уже открывали."));
        ImGui::PopStyleColor();
    }

    ImGui::Dummy(ImVec2(0, 6));

    int cache_hours = tex::cache_ttl_hours();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderInt("##cachehours", &cache_hours, 0, 720,
                         cache_hours > 0 ? tr("Хранить на диске: %d ч") : tr("Не хранить на диске"),
                         ImGuiSliderFlags_AlwaysClamp))
        tex::set_cache_ttl_hours(cache_hours);

    ImGui::PushStyleColor(ImGuiCol_Text, col::text_muted);
    if (tex::cache_ttl_hours() > 0)
        ImGui::TextWrapped(tr("Скачанные картинки и гифки лежат файлами и переживают перезапуск. "
                           "Сейчас занято %u МБ в %d файлах; всё, что старше срока, удаляется "
                           "при запуске и при смене этой настройки."),
                           tex::cache_disk_kb() / 1024, tex::cache_file_count());
    else
        ImGui::TextWrapped(tr("Кэш на диске выключен и очищен: каждая картинка качается заново "
                           "после того, как её выгрузили из памяти."));
    ImGui::PopStyleColor();

    ImGui::Dummy(ImVec2(0, 4));
    ImGui::TextUnformatted(tr("Уровень входа"));
    ImGui::ProgressBar(audio::input_level(), ImVec2(-1, 10), "");
    ImGui::TextUnformatted(tr("Уровень выхода"));
    ImGui::ProgressBar(audio::output_level(), ImVec2(-1, 10), "");

    if (audio::last_error()[0])
    {
        ImGui::PushStyleColor(ImGuiCol_Text, col::yellow);
        ImGui::TextWrapped("%s", audio::last_error());
        ImGui::PopStyleColor();
    }

    ImGui::EndChild();

    // Right column: everything you read rather than change.
    ImGui::SameLine(0, 14);
    ImGui::BeginChild("##settings_right", ImVec2(0, cols_h), false,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);

    ImGui::TextUnformatted(tr("Аккаунт"));
    ImGui::Separator();

    {
        // These fields arrive with the owner's own user object (READY,
        // /users/@me); discord never sends them about anybody else.
        store::guard g;
        duser* me = store::self();
        char line[256];

        cnprint(line, sizeof(line), tr("Почта: %s"),
                (me && me->email && me->email[0]) ? me->email : tr("не указана"));
        ImGui::TextUnformatted(line);
        cnprint(line, sizeof(line), tr("Телефон: %s"),
                (me && me->phone && me->phone[0]) ? me->phone : tr("не привязан"));
        ImGui::TextUnformatted(line);

        cnprint(line, sizeof(line), tr("Почта подтверждена: %s, двухфакторка: %s"),
                (me && me->verified) ? tr("да") : tr("нет"),
                (me && me->mfa_enabled) ? tr("включена") : tr("выключена"));
        ui_text_muted(line);
    }

    ImGui::Dummy(ImVec2(0, 10));
    ImGui::TextUnformatted(tr("Сеансы"));
    ImGui::Separator();

    if (ImGui::Button(tr("Обновить список"), ImVec2(-1, 0))) api::fetch_sessions();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(tr("Где ещё открыт этот аккаунт"));

    {
        store::guard g;
        int st = store::sessions_state();
        const ulist<dsession>& list = store::sessions();

        if (st == 1) ui_text_muted(tr("Загружаю..."));
        else if (st == 3) ui_text_muted(tr("Не удалось загрузить"));
        else if (st == 2 && list.count == 0) ui_text_muted(tr("Сеансов нет"));
        else if (st == 0) ui_text_muted(tr("Ещё не запрашивалось"));

        for (unsigned int i = 0; i < list.count; i++)
        {
            const dsession* s = &list[i];

            // "2026-08-15T06:31:52.747735+00:00" -> "2026-08-15 06:31"
            char when[20];
            ccfset(when, 0, sizeof(when));
            const char* t = s->last_used;
            if (ccslenf(t) >= 16)
            {
                for (int k = 0; k < 10; k++) when[k] = t[k];
                when[10] = ' ';
                for (int k = 0; k < 5; k++) when[11 + k] = t[11 + k];
            }
            else ccstrncpy(when, t, sizeof(when) - 1);

            char title[224];
            cnprint(title, sizeof(title), "%s · %s",
                    s->os[0] ? s->os : "?",
                    s->platform[0] ? s->platform : "?");
            ImGui::TextUnformatted(title);

            char sub[256];
            cnprint(sub, sizeof(sub), "%s%s%s",
                    s->location[0] ? s->location : tr("место неизвестно"),
                    when[0] ? " — " : "", when);
            ui_text_muted(sub);
        }
    }

    ImGui::Dummy(ImVec2(0, 10));
    ImGui::TextUnformatted(tr("Качество звонка"));
    ImGui::Separator();

    {
        // Live counts from the receive path, refreshed every five seconds.
        // The pipeline got a lot shorter: packets decode on arrival into a
        // per-speaker ring and the device pulls straight out of it, so the
        // whole of what can still go wrong fits on two lines.
        voice::rx_report r;
        if (!voice::last_rx_report(&r))
        {
            ui_text_muted(tr("Появится через несколько секунд в звонке"));
        }
        else
        {
            char line[192];

            cnprint(line, sizeof(line), tr("Сыграно %u кадров, опоздало %u, срезано кольцом %u"),
                    r.played, r.late, r.overflow);
            ImGui::TextUnformatted(line);

            // Сколько потерянных пакетов пришлось достроить маскировкой opus:
            // растёт - значит сеть рвётся, а не декодер или микшер.
            cnprint(line, sizeof(line), tr("Скрыто потерь %u"), r.concealed);
            if (r.concealed)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, col::yellow);
                ImGui::TextUnformatted(line);
                ImGui::PopStyleColor();
            }
            else
            {
                ui_text_muted(line);
            }

            // E2EE-дропы: растут - значит обрывы это не сеть, а согласование
            // ключей DAVE (кто-то зашёл/вышел, эпоха сменилась).
            cnprint(line, sizeof(line), tr("E2EE: без ключей %u, не развернуто %u"),
                    r.nokey, r.unwrap);
            if (r.nokey || r.unwrap)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, col::yellow);
                ImGui::TextUnformatted(line);
                ImGui::PopStyleColor();
            }
            else
            {
                ui_text_muted(line);
            }

            // Поднимается только когда сам декодер отдаёт полношкальный
            // мусор: это показание отличает больной opus от больной сети.
            cnprint(line, sizeof(line), tr("Срывов декодера %u"), r.railed);
            if (r.railed)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, col::yellow);
                ImGui::TextUnformatted(line);
                ImGui::PopStyleColor();
            }
            else
            {
                ui_text_muted(line);
            }

            cnprint(line, sizeof(line), tr("Медиа и стримы: недобор %u, срез %u"),
                    r.underruns, r.overruns);
            ui_text_muted(line);

            cnprint(line, sizeof(line), tr("Буфер вывода %u мс"), audio::render_backlog_ms());
            ui_text_muted(line);
        }
    }

    ImGui::Dummy(ImVec2(0, 10));
    ImGui::TextUnformatted(tr("Диагностика"));
    ImGui::Separator();
    ImGui::Text(tr("Шлюз: %s"), gateway::status_text());
    ImGui::Text(tr("Текстур в памяти: %u КБ"), tex::memory_used() / 1024);
    ImGui::Text(tr("Загрузок в очереди: %d"), tex::pending_downloads());

    ImGui::EndChild();

    ImGui::Dummy(ImVec2(0, 4));
    if (ImGui::Button(tr("Закрыть"), ImVec2(120, 30))) ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}
