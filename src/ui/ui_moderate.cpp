#include "pch.h"
#include "ui_state.h"
#include "theme.h"

#include "discord/store.h"
#include "discord/rest.h"

// Disconnecting somebody from voice, silencing them for the whole server,
// timing them out and banning them.
//
// One submenu shared by every place a member can be right-clicked, because
// these are the same four things wherever you reach for them, and because
// deciding what is allowed is the fiddly part and is worth having in one
// place. Two things decide it: the permission bits, and rank - discord
// refuses to let anybody act on somebody whose highest role sits at or above
// their own, whatever bits they hold.
//
// Nothing here confirms first except the ban, which is the one that cannot be
// undone from this menu.

namespace
{
    struct span
    {
        int minutes;
        const char* label;
    };

    // Discord's own list. The ceiling is 28 days and the server refuses
    // anything past it.
    const span SPANS[] = {
        { 60,    "60 минут" },
        { 300,   "5 часов" },
        { 1440,  "1 день" },
        { 10080, "1 неделя" },
        { 40320, "28 дней" },
    };

    const int SPAN_COUNT = (int)(sizeof(SPANS) / sizeof(SPANS[0]));

    // How much of what they said gets swept up with the ban. Discord counts
    // this in seconds and takes at most a week.
    struct sweep
    {
        int seconds;
        const char* label;
    };

    const sweep SWEEPS[] = {
        { 0,      "не трогать сообщения" },
        { 3600,   "за последний час" },
        { 86400,  "за последний день" },
        { 604800, "за последнюю неделю" },
    };

    const int SWEEP_COUNT = (int)(sizeof(SWEEPS) / sizeof(SWEEPS[0]));

    // The voice channel somebody is sitting in, or null. Muting and
    // disconnecting are judged against that channel's permissions rather than
    // the server's, the same way discord judges them.
    const dchannel* voice_channel_of(snowflake user_id)
    {
        const dvoice_state* vs = store::find_voice_state(user_id);
        return (vs && vs->channel_id) ? store::find_channel(vs->channel_id) : 0;
    }

    void timeout_left(unsigned long long until_ms, char* out, int cap)
    {
        unsigned long long now = unix_now_ms();
        if (until_ms <= now) { out[0] = 0; return; }

        unsigned long long left = (until_ms - now) / 60000ULL + 1;

        if (left >= 1440) cnprint(out, cap, tr("осталось %llu ч"), left / 60);
        else              cnprint(out, cap, tr("осталось %llu мин"), left);
    }
}

bool ui_can_move_member(snowflake guild_id, snowflake user_id, snowflake to_channel_id)
{
    if (!guild_id || !user_id) return false;

    store::guard guard;

    dguild* g = store::find_guild(guild_id);
    if (!g) return false;

    snowflake me = store::self_id();
    if (!me || me == user_id) return false;
    if (!store::outranks(g, me, user_id)) return false;

    // Only somebody already in voice can be moved. Discord ignores the field
    // on anybody else rather than pulling them in.
    const dchannel* from = voice_channel_of(user_id);
    if (!from) return false;

    if (!(store::member_permissions(g, me, from) & PERM_MOVE_MEMBERS)) return false;
    if (!to_channel_id) return true;

    const dchannel* to = store::find_channel(to_channel_id);
    if (!to || !to->is_voice() || to->id == from->id) return false;

    // The far end has to allow it too, and we have to be allowed in it at all:
    // moving somebody somewhere we cannot go is refused.
    unsigned long long there = store::member_permissions(g, me, to);
    return (there & PERM_MOVE_MEMBERS) != 0 && (there & PERM_CONNECT) != 0;
}

void ui_member_moderation_menu(snowflake guild_id, snowflake user_id)
{
    if (!guild_id || !user_id) return;

    store::guard guard;

    dguild* g = store::find_guild(guild_id);
    if (!g) return;

    snowflake me = store::self_id();
    if (!me || me == user_id) return;

    // Rank first: without it every item below would be drawn only to be
    // refused by the server.
    if (!store::outranks(g, me, user_id)) return;

    const dchannel* voice = voice_channel_of(user_id);

    unsigned long long here = store::member_permissions(g, me, voice);
    unsigned long long anywhere = store::member_permissions(g, me, 0);

    bool can_move = voice && (here & PERM_MOVE_MEMBERS);
    bool can_mute = voice && (here & PERM_MUTE_MEMBERS);
    bool can_timeout = (anywhere & PERM_MODERATE_MEMBERS) != 0;
    bool can_ban = (anywhere & PERM_BAN_MEMBERS) != 0;

    if (!can_move && !can_mute && !can_timeout && !can_ban) return;

    ImGui::Separator();

    if (can_move && ImGui::MenuItem(tr("Отключить от голосового")))
        api::voice_kick(guild_id, user_id);

    if (can_move && ImGui::BeginMenu(tr("Перекинуть в")))
    {
        int offered = 0;

        for (unsigned int i = 0; i < g->channels.count; i++)
        {
            dchannel* c = store::find_channel(g->channels[i]);
            if (!c || !ui_can_move_member(guild_id, user_id, c->id)) continue;

            char label[160];
            ui_channel_display_name(c, label, sizeof(label));

            ImGui::PushID((int)(c->id & 0x7FFFFFFF));
            if (ImGui::MenuItem(label)) api::voice_move(guild_id, user_id, c->id);
            ImGui::PopID();

            offered++;
        }

        if (!offered) ui_text_muted(tr("некуда"));

        ImGui::EndMenu();
    }

    if (can_mute)
    {
        // The server's mute, not ours. The one in the voice menu above turns
        // somebody down on this machine alone; this one silences them for
        // everybody in the channel, and they are told about it.
        const dvoice_state* vs = store::find_voice_state(user_id);
        bool muted = vs && vs->mute;

        if (ImGui::MenuItem(muted ? tr("Снять мьют сервера") : tr("Мьют на сервере"), 0, muted))
            api::set_server_mute(guild_id, user_id, !muted);
    }

    if (can_timeout)
    {
        dmember* m = store::find_member(g, user_id);
        bool timed_out = m && m->timeout_until_ms > unix_now_ms();

        if (timed_out)
        {
            char left[64];
            timeout_left(m->timeout_until_ms, left, sizeof(left));

            if (ImGui::MenuItem(tr("Снять таймаут"))) api::timeout_member(guild_id, user_id, 0);

            ImGui::SameLine();
            ui_text_muted(left);
        }
        else if (ImGui::BeginMenu(tr("Таймаут")))
        {
            for (int i = 0; i < SPAN_COUNT; i++)
                if (ImGui::MenuItem(tr(SPANS[i].label)))
                    api::timeout_member(guild_id, user_id, SPANS[i].minutes);

            ImGui::EndMenu();
        }
    }

    if (can_ban)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, col::red);
        bool open = ImGui::BeginMenu(tr("Забанить"));
        ImGui::PopStyleColor();

        if (open)
        {
            // The submenu is the confirmation: a ban is one click away from
            // the profile item otherwise, and it is not something to hand out
            // by a slip of the mouse.
            for (int i = 0; i < SWEEP_COUNT; i++)
                if (ImGui::MenuItem(tr(SWEEPS[i].label)))
                    api::ban_member(guild_id, user_id, SWEEPS[i].seconds);

            ImGui::EndMenu();
        }
    }
}
