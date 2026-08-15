#include "pch.h"
#include "gateway.h"
#include "store.h"
#include "rest.h"
#include "voice.h"
#include "video/screenshare.h"
#include "video/streamview.h"
#include "archive.h"
#include "core/offline.h"
#include "core/log.h"
#include "core/crypto.h"
#include "core/storage.h"
#include "net/proxy.h"
#include "net/websocket.h"
#include "net/json.h"

// Gateway opcodes (v9).
enum
{
    OP_DISPATCH = 0,
    OP_HEARTBEAT = 1,
    OP_IDENTIFY = 2,
    OP_PRESENCE_UPDATE = 3,
    OP_VOICE_STATE_UPDATE = 4,
    OP_RESUME = 6,
    OP_RECONNECT = 7,
    OP_REQUEST_GUILD_MEMBERS = 8,
    OP_INVALID_SESSION = 9,
    OP_HELLO = 10,
    OP_HEARTBEAT_ACK = 11,
    OP_LAZY_REQUEST = 14,
};

namespace
{
    const char* GATEWAY_URL = "wss://gateway.discord.gg/?v=9&encoding=json";

    // One connection to discord. There are two of them, because an account
    // switch must not end a call.
    //
    // Discord ties presence in a voice channel to the gateway session that put
    // the user there. Close that session and the voice server hangs up with
    // 4014 - measured, not guessed. So the old connection is not closed: it
    // stops feeding the client and keeps heartbeating, holding the voice state
    // open, while a second one signs in as the new account and drives
    // everything else. The held one is let go when the call ends.
    struct gw_conn
    {
        websocket ws;
        HANDLE thread;
        HANDLE heartbeat_thread;
        HANDLE stop_event;
        HANDLE beat_event;

        volatile long running;
        volatile long heartbeat_ms;
        volatile long sequence;
        volatile long acked;

        char session_id[128];
        // Its own copy. A held connection has to keep identifying as the
        // account that opened it, long after the client has moved on.
        char token[512];

        // And its own route out. Two accounts can sit behind two different
        // proxies, and the held one has to keep using the one it was opened
        // with even after the client has switched to an account that goes
        // somewhere else entirely.
        proxy_config proxy;

        bool want_resume;
        unsigned int backoff_ms;

        // Only the client's current connection folds anything into the store.
        // The other one exists to be alive, and nothing more.
        volatile long dispatching;
    };

    gw_conn g_conns[2];
    int g_active = 0;

    gw_conn* active() { return &g_conns[g_active]; }
    gw_conn* spare()  { return &g_conns[g_active ^ 1]; }
    bool holding()    { return spare()->running != 0; }

    volatile long g_state = GW_OFFLINE;
    volatile long g_hold_media = 0;

    // The voice regions READY offered, in the order it gave them.
    const unsigned int MAX_REGIONS = 8;
    char g_regions[MAX_REGIONS][32];
    unsigned int g_region_count = 0;
    char g_status[192];

    void set_state(gateway_state s, const char* text)
    {
        InterlockedExchange(&g_state, (long)s);
        ccfset(g_status, 0, sizeof(g_status));
        if (text) ccstrncpy(g_status, text, sizeof(g_status) - 1);
    }

    // ---- outgoing ------------------------------------------------------
    bool send_json(gw_conn* c, jwriter* w)
    {
        return c->ws.send_text(w->buf.data, w->buf.size);
    }

    void send_identify(gw_conn* c)
    {
        jwriter w;
        w.init();
        w.begin_obj();
        w.kv_i64("op", OP_IDENTIFY);
        w.key("d");
        w.begin_obj();
        w.kv_str("token", c->token);
        // Value taken from a known-good client; discord answers op 9 to made-up
        // capability masks.
        w.kv_i64("capabilities", 4605);
        w.key("properties");
        w.begin_obj();
        w.kv_str("os", "Windows");
        w.kv_str("browser", "Chrome");
        w.kv_str("device", "");
        w.kv_str("system_locale", "en-US");
        w.kv_bool("has_client_mods", false);
        w.kv_str("browser_user_agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
        w.kv_str("browser_version", "120.0.0.0");
        w.kv_str("os_version", "10");
        w.kv_str("referrer", "");
        w.kv_str("referring_domain", "");
        w.kv_str("referrer_current", "");
        w.kv_str("referring_domain_current", "");
        w.kv_str("release_channel", "stable");
        w.kv_i64("client_build_number", 363557);
        w.kv_null("client_event_source");
        w.end_obj();
        w.key("presence");
        w.begin_obj();
        w.kv_str("status", "online");
        w.kv_i64("since", 0);
        w.key("activities");
        w.begin_arr();
        w.end_arr();
        w.kv_bool("afk", false);
        w.end_obj();
        w.kv_bool("compress", false);
        w.key("client_state");
        w.begin_obj();
        // guild_hashes must be an object and highest_last_message_id a string;
        // sending either with the wrong type gets the session invalidated.
        w.key("guild_hashes");
        w.begin_obj();
        w.end_obj();
        w.kv_str("highest_last_message_id", "0");
        w.kv_i64("read_state_version", 0);
        w.kv_i64("user_guild_settings_version", -1);
        w.kv_i64("user_settings_version", -1);
        w.end_obj();
        w.end_obj();
        w.end_obj();

        log_line("gateway: sending identify (%u bytes)", w.buf.size);
        send_json(c, &w);
        w.free_writer();
        set_state(GW_IDENTIFYING, "Авторизация...");
    }

    void send_resume(gw_conn* c)
    {
        jwriter w;
        w.init();
        w.begin_obj();
        w.kv_i64("op", OP_RESUME);
        w.key("d");
        w.begin_obj();
        w.kv_str("token", c->token);
        w.kv_str("session_id", c->session_id);
        w.kv_i64("seq", c->sequence);
        w.end_obj();
        w.end_obj();

        send_json(c, &w);
        w.free_writer();
        set_state(GW_IDENTIFYING, "Восстановление сессии...");
    }

    void send_heartbeat(gw_conn* c)
    {
        jwriter w;
        w.init();
        w.begin_obj();
        w.kv_i64("op", OP_HEARTBEAT);
        long seq = c->sequence;
        if (seq > 0) w.kv_i64("d", seq);
        else w.kv_null("d");
        w.end_obj();

        send_json(c, &w);
        w.free_writer();
    }

    DWORD WINAPI heartbeat_thread(LPVOID param)
    {
        gw_conn* c = (gw_conn*)param;

        while (c->running)
        {
            long interval = c->heartbeat_ms;
            if (interval <= 0)
            {
                if (WaitForSingleObject(c->beat_event, 250) == WAIT_OBJECT_0) continue;
                continue;
            }

            HANDLE waits[2] = { c->stop_event, c->beat_event };
            DWORD r = WaitForMultipleObjects(2, waits, FALSE, (DWORD)interval);
            if (r == WAIT_OBJECT_0) break;      // stopping
            if (r == WAIT_OBJECT_0 + 1) continue; // interval changed, restart timing

            if (!c->ws.is_open()) continue;

            if (!InterlockedExchange(&c->acked, 0))
            {
                // The previous beat went unanswered - the socket is a zombie.
                log_line("gateway: heartbeat not acked, forcing reconnect");
                c->ws.close();
                continue;
            }
            send_heartbeat(c);
        }
        return 0;
    }

    // ---- dispatch ------------------------------------------------------

    void handle_ready(gw_conn* c, const jval* d)
    {
        store::guard g;

        const char* sid = d->str("session_id", 0);
        if (sid) ccstrncpy(c->session_id, sid, sizeof(c->session_id) - 1);

        // Discord hands out the voice regions in the order it thinks are best
        // for this connection, and a client is expected to name them back when
        // it joins. They are kept here rather than guessed from an endpoint.
        g_region_count = 0;
        const jval* regions = d->arr("geo_ordered_rtc_regions");
        for (unsigned int i = 0; i < regions->count && g_region_count < MAX_REGIONS; i++)
        {
            const char* name = regions->at(i)->as_str(0);
            if (!name || !name[0]) continue;
            ccstrncpy(g_regions[g_region_count], name, sizeof(g_regions[0]) - 1);
            g_region_count++;
        }
        if (g_region_count)
            log_line("gateway: %u voice regions offered, best is %s", g_region_count, g_regions[0]);

        duser* me = store::upsert_user(d->obj("user"));
        if (me) store::set_self_id(me->id);

        // Our own presence is not in the presence list - discord never tells a
        // client about itself that way. It comes from the account settings, and
        // failing that from whatever session is already signed in. Without this
        // the client shows its owner as offline forever, which reads as
        // invisible mode and is simply wrong.
        if (me)
        {
            const char* mine = d->obj("user_settings")->str("status", 0);
            if (!mine)
            {
                const jval* sessions = d->arr("sessions");
                for (unsigned int i = 0; i < sessions->count && !mine; i++)
                    mine = sessions->at(i)->str("status", 0);
            }

            if (mine)
            {
                jdoc fake;
                fake.init();
                // apply_presence reads a status out of an object; building one
                // is cheaper than duplicating the mapping here.
                char shim[96];
                cnprint(shim, sizeof(shim), "{\"user_id\":\"%llu\",\"status\":\"%s\"}",
                        me->id, mine);
                if (fake.parse(shim, (int)ccslenf(shim))) store::apply_presence(fake.r());
                fake.free_doc();

                log_line("gateway: свой статус %s", mine);
            }
        }

        const jval* users = d->arr("users");
        for (unsigned int i = 0; i < users->count; i++)
            store::upsert_user(users->at(i));

        const jval* privates = d->arr("private_channels");
        for (unsigned int i = 0; i < privates->count; i++)
            store::upsert_channel(privates->at(i), 0);

        const jval* guilds = d->arr("guilds");
        for (unsigned int i = 0; i < guilds->count; i++)
            store::upsert_guild(guilds->at(i));

        // Which roles this account holds in each server, and nothing arrives
        // to say so anywhere else until somebody opens a channel and the
        // member list starts loading. Without it every permission works out
        // from @everyone alone, and a server whose channels are opened up by
        // role reads as entirely locked - which is exactly what it looked
        // like. One array per guild, in the same order as "guilds" above.
        const jval* merged = d->arr("merged_members");
        for (unsigned int i = 0; i < merged->count && i < guilds->count; i++)
        {
            snowflake gid = guilds->at(i)->sf("id");
            dguild* g = store::find_guild(gid);
            if (!g) continue;

            const jval* list = merged->at(i);
            for (unsigned int k = 0; k < list->count; k++)
                store::add_guild_member(g, list->at(k));
        }

        // Read state arrives after the channels it talks about, so the
        // channels have somewhere to put it.
        {
            const jval* rs = d->obj("read_state");
            const jval* entries = (rs && rs->type == JTYPE_OBJ) ? rs->arr("entries")
                                                                : d->arr("read_state");
            for (unsigned int i = 0; i < entries->count; i++)
                store::apply_read_state(entries->at(i));
        }

        // And so does the order the account keeps its servers in. Folders are
        // the current shape of it; a folder with no id is a single server
        // sitting loose, which is why only the ids inside matter here.
        {
            const jval* settings = d->obj("user_settings");
            const jval* folders = settings->arr("guild_folders");

            ulist<snowflake> wanted;
            wanted = ulist<snowflake>();

            for (unsigned int i = 0; i < folders->count; i++)
            {
                const jval* ids = folders->at(i)->arr("guild_ids");
                for (unsigned int k = 0; k < ids->count; k++)
                {
                    snowflake gid = ids->at(k)->as_snowflake();
                    if (gid) wanted.push(gid);
                }
            }

            if (!wanted.count)
            {
                const jval* positions = settings->arr("guild_positions");
                for (unsigned int i = 0; i < positions->count; i++)
                {
                    snowflake gid = positions->at(i)->as_snowflake();
                    if (gid) wanted.push(gid);
                }
            }

            if (wanted.count)
            {
                store::apply_guild_order(&wanted);
                log_line("gateway: порядок серверов взят из настроек аккаунта (%u)", wanted.count);
            }
            wanted.dispose();
        }

        const jval* rels = d->arr("relationships");
        for (unsigned int i = 0; i < rels->count; i++)
        {
            const jval* r = rels->at(i);
            snowflake uid = r->sf("user_id");
            if (!uid)
            {
                duser* ru = store::upsert_user(r->obj("user"));
                if (ru) uid = ru->id;
            }
            else
            {
                store::upsert_user(r->obj("user"));
            }
            if (uid) store::set_relationship(uid, r->i32("type", 0), r->str("nickname", 0));
        }

        // Initial online states. Without this everyone stays grey until their
        // first PRESENCE_UPDATE, which may never arrive for idle contacts.
        const jval* presences = d->arr("presences");
        for (unsigned int i = 0; i < presences->count; i++)
            store::apply_presence(presences->at(i));

        store::touch_dm_order();
        store::bump_revision();

        log_line("gateway: READY, %u guilds, %u dms, %u relationships",
                 guilds->count, privates->count, rels->count);

        char text[192];
        cnprint(text, sizeof(text), "В сети как %s",
                me ? me->display_name() : "?");
        set_state(GW_READY, text);
        c->backoff_ms = 1000;
    }

    void handle_dispatch(gw_conn* c, const char* type, const jval* d)
    {
        if (!type) return;

        // A held connection is alive only to keep a call open. Folding its
        // dispatches into the store would drag the previous account's servers
        // and messages back over the one that is signed in now.
        if (!c->dispatching) return;

        if (ccscmp(type, "READY") == 0) { handle_ready(c, d); return; }

        if (ccscmp(type, "READY_SUPPLEMENTAL") == 0)
        {
            store::guard g;
            const jval* guilds = d->arr("guilds");
            for (unsigned int i = 0; i < guilds->count; i++)
            {
                const jval* gv = guilds->at(i);
                snowflake gid = gv->sf("id");
                const jval* states = gv->arr("voice_states");
                for (unsigned int k = 0; k < states->count; k++)
                    store::set_voice_state(states->at(k), gid);
            }

            // merged_presences carries the real online states: "friends" is a
            // flat list, "guilds" is one list per guild in READY order.
            const jval* merged = d->obj("merged_presences");
            const jval* friends = merged->arr("friends");
            for (unsigned int i = 0; i < friends->count; i++)
                store::apply_presence(friends->at(i));

            const jval* guild_presences = merged->arr("guilds");
            for (unsigned int i = 0; i < guild_presences->count; i++)
            {
                const jval* list = guild_presences->at(i);
                for (unsigned int k = 0; k < list->count; k++)
                    store::apply_presence(list->at(k));
            }

            store::bump_revision();
            return;
        }

        if (ccscmp(type, "RESUMED") == 0)
        {
            set_state(GW_READY, "Сессия восстановлена");
            return;
        }

        if (ccscmp(type, "MESSAGE_CREATE") == 0 || ccscmp(type, "MESSAGE_UPDATE") == 0)
        {
            {
                store::guard g;
                store::upsert_message(d);

                // Something arriving in the channel already open is read the
                // moment it lands; anything else counts against the badge.
                if (ccscmp(type, "MESSAGE_CREATE") == 0)
                    store::note_incoming(d);

                store::bump_revision();
            }
            // Everything that passes through is kept, so a channel opens from
            // disk next time and still reads when there is no connection.
            archive::put_json(d);
            return;
        }

        // Another client of the same account read something; the badge here
        // has to agree with it.
        if (ccscmp(type, "MESSAGE_ACK") == 0)
        {
            store::guard g;
            store::apply_read_state(d);
            store::bump_revision();
            return;
        }

        if (ccscmp(type, "TYPING_START") == 0)
        {
            store::guard g;
            store::note_typing(d->sf("channel_id"), d->sf("user_id"));

            // The member object rides along in a guild, and it is often the
            // only place a name for somebody who has not spoken comes from.
            dguild* guild = store::find_guild(d->sf("guild_id"));
            if (guild) store::add_guild_member(guild, d->obj("member"));

            store::bump_revision();
            return;
        }

        if (ccscmp(type, "MESSAGE_DELETE") == 0)
        {
            snowflake channel_id = d->sf("channel_id");
            snowflake message_id = d->sf("id");

            // Written down before it is dropped from the live view: what
            // somebody took back is the one thing an archive is for.
            archive::mark_deleted(channel_id, message_id);

            store::guard g;
            store::mark_message_deleted(channel_id, message_id);
            store::bump_revision();
            return;
        }

        if (ccscmp(type, "CHANNEL_CREATE") == 0 || ccscmp(type, "CHANNEL_UPDATE") == 0)
        {
            store::guard g;
            snowflake gid = d->sf("guild_id");
            dchannel* c = store::upsert_channel(d, gid);
            if (c && gid)
            {
                dguild* guild = store::find_guild(gid);
                if (guild)
                {
                    bool known = false;
                    for (unsigned int i = 0; i < guild->channels.count; i++)
                        if (guild->channels[i] == c->id) { known = true; break; }
                    if (!known) guild->channels.push(c->id);

                    // A new channel, or one whose position moved, changes where
                    // everything after it belongs.
                    store::sort_guild_channels(guild);
                }
            }
            store::bump_revision();
            return;
        }

        if (ccscmp(type, "CHANNEL_DELETE") == 0)
        {
            store::guard g;
            store::remove_channel(d->sf("id"));
            return;
        }

        if (ccscmp(type, "GUILD_CREATE") == 0 || ccscmp(type, "GUILD_UPDATE") == 0)
        {
            store::guard g;
            store::upsert_guild(d);
            return;
        }

        if (ccscmp(type, "GUILD_DELETE") == 0)
        {
            store::guard g;
            store::remove_guild(d->sf("id"));
            return;
        }

        // What op 14 answers with. The client used to handle only the reply to
        // op 8, which it never sends, so every server showed nobody at all.
        if (ccscmp(type, "GUILD_MEMBER_LIST_UPDATE") == 0)
        {
            store::guard g;

            snowflake gid = d->sf("guild_id");
            dguild* guild = store::find_guild(gid);
            if (!guild) return;

            int total = (int)d->i64("member_count", 0);
            if (total) guild->member_count = total;

            const jval* ops = d->arr("ops");
            for (unsigned int i = 0; i < ops->count; i++)
            {
                const jval* entry = ops->at(i);
                const char* what = entry->str("op", "");

                // SYNC carries a window of the list; INSERT and UPDATE carry a
                // single row. DELETE and INVALIDATE only say a window is stale,
                // and re-reading it is the next scroll's problem.
                bool many = ccscmp(what, "SYNC") == 0;
                bool one = ccscmp(what, "INSERT") == 0 || ccscmp(what, "UPDATE") == 0;
                if (!many && !one) continue;

                const jval* items = many ? entry->arr("items") : 0;
                unsigned int n = many ? items->count : 1;

                for (unsigned int k = 0; k < n; k++)
                {
                    const jval* row = many ? items->at(k) : entry->obj("item");
                    const jval* member = row->obj("member");
                    if (!member || member->type != JTYPE_OBJ) continue;   // a group header

                    store::add_guild_member(guild, member);
                }
            }

            store::bump_revision();
            return;
        }

        if (ccscmp(type, "GUILD_MEMBERS_CHUNK") == 0)
        {
            store::guard g;
            snowflake gid = d->sf("guild_id");
            dguild* guild = store::find_guild(gid);
            const jval* members = d->arr("members");
            for (unsigned int i = 0; i < members->count; i++)
            {
                const jval* m = members->at(i);
                duser* u = store::upsert_user(m->obj("user"));
                if (!u || !guild) continue;

                bool known = false;
                for (unsigned int k = 0; k < guild->members.count; k++)
                    if (guild->members[k].user_id == u->id) { known = true; break; }
                if (known) continue;

                dmember mem;
                ccfset(&mem, 0, sizeof(mem));
                mem.user_id = u->id;
                mem.nick = m->str("nick", 0) ? store::intern(m->str("nick", 0)) : 0;
                guild->members.push(mem);
            }
            store::bump_revision();
            return;
        }

        if (ccscmp(type, "RELATIONSHIP_ADD") == 0)
        {
            store::guard g;
            duser* u = store::upsert_user(d->obj("user"));
            snowflake uid = d->sf("id");
            if (!uid && u) uid = u->id;
            if (uid) store::set_relationship(uid, d->i32("type", 0), d->str("nickname", 0));
            return;
        }

        if (ccscmp(type, "RELATIONSHIP_REMOVE") == 0)
        {
            store::guard g;
            store::remove_relationship(d->sf("id"));
            return;
        }

        if (ccscmp(type, "PRESENCE_UPDATE") == 0)
        {
            store::guard g;
            store::apply_presence(d);
            return;
        }

        if (ccscmp(type, "VOICE_STATE_UPDATE") == 0)
        {
            snowflake uid = d->sf("user_id");
            {
                store::guard g;
                store::set_voice_state(d, d->sf("guild_id"));
            }
            if (uid == store::self_id()) voice::on_gateway_voice_state(d);
            return;
        }

        if (ccscmp(type, "VOICE_SERVER_UPDATE") == 0)
        {
            voice::on_gateway_voice_server(d);
            return;
        }

        // A Go Live stream is negotiated through its own pair of dispatches,
        // parallel to the voice ones above. Both a stream of ours and one we
        // are watching arrive here, told apart by the stream key, so each side
        // is offered every dispatch and ignores the ones that are not its own.
        if (ccscmp(type, "STREAM_CREATE") == 0)
        {
            screenshare::on_stream_create(d);
            streamview::on_stream_create(d);
            return;
        }

        if (ccscmp(type, "STREAM_SERVER_UPDATE") == 0)
        {
            screenshare::on_stream_server_update(d);
            streamview::on_stream_server_update(d);
            return;
        }

        if (ccscmp(type, "STREAM_DELETE") == 0)
        {
            log_line("gateway: STREAM_DELETE");
            streamview::on_stream_delete(d);
            return;
        }

        // Sent while a stream is live to say who is watching it. Nothing here
        // acts on it, but it is worth not treating as an unknown dispatch.
        if (ccscmp(type, "STREAM_UPDATE") == 0) return;

        if (ccscmp(type, "CHANNEL_RECIPIENT_ADD") == 0 || ccscmp(type, "CHANNEL_RECIPIENT_REMOVE") == 0)
        {
            store::guard g;
            store::bump_revision();
            return;
        }

        // Everything else is deliberately ignored.
    }

    void handle_payload(gw_conn* c, const char* text, unsigned int len)
    {
        jdoc doc;
        doc.init();
        if (!doc.parse(text, (int)len))
        {
            log_line("gateway: unparsable payload (%u bytes)", len);
            doc.free_doc();
            return;
        }

        const jval* root = doc.root;
        int op = root->i32("op", -1);

        const jval* seq = root->get("s");
        if (seq->type == JTYPE_NUM) InterlockedExchange(&c->sequence, (long)seq->inum);

        switch (op)
        {
        case OP_HELLO:
        {
            long interval = (long)root->obj("d")->i64("heartbeat_interval", 41250);
            InterlockedExchange(&c->heartbeat_ms, interval);
            InterlockedExchange(&c->acked, 1);
            offline::note_network_success();
            SetEvent(c->beat_event);

            if (c->want_resume && c->session_id[0]) send_resume(c);
            else send_identify(c);
            c->want_resume = false;
            break;
        }

        case OP_HEARTBEAT:
            send_heartbeat(c);
            break;

        case OP_HEARTBEAT_ACK:
            InterlockedExchange(&c->acked, 1);
            break;

        case OP_RECONNECT:
            log_line("gateway: server asked for a reconnect");
            c->want_resume = true;
            c->ws.close();
            break;

        case OP_INVALID_SESSION:
            log_line("gateway: session invalidated");
            c->want_resume = root->get("d")->as_bool(false);
            if (!c->want_resume) c->session_id[0] = 0;
            c->ws.close();
            break;

        case OP_DISPATCH:
            // Deliberately not logged per event: the log flushes on every line,
            // and a busy account produces a steady stream of dispatches.
            handle_dispatch(c, root->str("t", 0), root->obj("d"));
            break;

        default:
            break;
        }

        doc.free_doc();
    }

    // ---- connection loop ----------------------------------------------

    DWORD WINAPI gateway_thread(LPVOID param)
    {
        gw_conn* c = (gw_conn*)param;

        ubuffer message;
        message.init(1 << 16);

        while (c->running)
        {
            set_state(GW_CONNECTING, "Подключение к discord...");

            c->ws.init();
            if (!c->ws.connect(GATEWAY_URL, "Origin: https://discord.com\r\n",
                               c->proxy.in_use() ? &c->proxy : 0))
            {
                c->ws.destroy();
                set_state(GW_RECONNECTING, "Нет связи, повтор...");

                // The socket refusing to open is the earliest and plainest sign
                // that there is no network; nothing else has to fail first.
                offline::note_network_failure();

                if (WaitForSingleObject(c->stop_event, c->backoff_ms) == WAIT_OBJECT_0) break;
                c->backoff_ms = c->backoff_ms < 30000 ? c->backoff_ms * 2 : 30000;
                continue;
            }

            InterlockedExchange(&c->acked, 1);

            for (;;)
            {
                bool binary = false;
                ws_result r = c->ws.receive(&message, &binary);
                if (r != WS_MESSAGE) break;
                if (!c->running) break;
                if (binary) continue;   // compression is disabled, so this is unexpected

                handle_payload(c, (const char*)message.c_str(), message.size);
            }

            log_line("gateway: receive loop ended (close code %u)", c->ws.close_status);

            InterlockedExchange(&c->heartbeat_ms, 0);
            // The streams go first: they are signalled over this socket, so
            // once it is gone neither can be stopped or kept alive.
            //
            // Unless the socket is being replaced on purpose, which is what an
            // account switch is. The voice connection and the stream have their
            // own sockets and their own accepted tokens; nothing about them
            // needs this one.
            if (!g_hold_media)
            {
                screenshare::on_gateway_disconnected();
                streamview::on_gateway_disconnected();
                voice::on_gateway_disconnected();
            }
            c->ws.destroy();

            if (!c->running) break;

            set_state(GW_RECONNECTING, "Переподключение...");
            if (WaitForSingleObject(c->stop_event, c->backoff_ms) == WAIT_OBJECT_0) break;
            c->backoff_ms = c->backoff_ms < 30000 ? c->backoff_ms * 2 : 30000;
        }

        message.free_buffer();
        set_state(GW_OFFLINE, "Отключено");
        return 0;
    }
}

namespace
{
    void stop_conn(gw_conn* c)
    {
        if (!c->running) return;

        InterlockedExchange(&c->running, 0);
        SetEvent(c->stop_event);
        c->ws.close();

        if (c->heartbeat_thread)
        {
            WaitForSingleObject(c->heartbeat_thread, 3000);
            CloseHandle(c->heartbeat_thread);
            c->heartbeat_thread = 0;
        }
        if (c->thread)
        {
            WaitForSingleObject(c->thread, 5000);
            CloseHandle(c->thread);
            c->thread = 0;
        }

        c->ws.destroy();

        if (c->stop_event) { CloseHandle(c->stop_event); c->stop_event = 0; }
        if (c->beat_event) { CloseHandle(c->beat_event); c->beat_event = 0; }
    }
}

void gateway::start()
{
    gw_conn* c = active();
    if (c->running) return;

    // A slot is reused, so everything about the previous life of it goes.
    HANDLE keep_thread = 0;
    ccfset(c, 0, sizeof(*c));
    (void)keep_thread;

    ccstrncpy(c->token, api::token(), sizeof(c->token) - 1);

    // Its own copy of the route, for the same reason it keeps its own token.
    c->proxy = storage::active_proxy();

    c->backoff_ms = 1000;
    c->dispatching = 1;

    c->stop_event = CreateEventW(0, TRUE, FALSE, 0);
    c->beat_event = CreateEventW(0, FALSE, FALSE, 0);
    c->running = 1;

    c->thread = CreateThread(0, 0, gateway_thread, c, 0, 0);
    c->heartbeat_thread = CreateThread(0, 0, heartbeat_thread, c, 0, 0);
}

void gateway::stop()
{
    stop_conn(active());
    set_state(GW_OFFLINE, "Отключено");
}

// The current connection stops being the client's and becomes the one holding
// a call open. It is not closed, which is the whole point: discord keeps the
// voice state only for as long as the session that created it lives.
void gateway::detach_for_voice()
{
    gw_conn* c = active();
    if (!c->running) return;

    // There is one call, so there is at most one connection worth holding.
    //
    // Switching a second time - including back to the account whose call is
    // being held - must not touch it. Tearing the spare down here is exactly
    // what killed the call on the way back: the session holding the voice
    // state went with it and the voice server answered 4006.
    if (holding())
    {
        log_line("gateway: удержание уже занято, текущее соединение закрывается");
        stop_conn(c);
        return;
    }

    InterlockedExchange(&c->dispatching, 0);
    g_active ^= 1;

    log_line("gateway: старое соединение удержано ради голоса");
}

void gateway::release_hold()
{
    gw_conn* held = spare();
    if (!held->running) return;

    log_line("gateway: удержанное соединение отпущено");
    stop_conn(held);
}

bool gateway::is_holding() { return holding(); }

gateway_state gateway::state() { return (gateway_state)g_state; }
const char* gateway::status_text() { return g_status; }
const char* gateway::session_id() { return active()->session_id; }

bool gateway::send_raw(const void* json, unsigned int len)
{
    return active()->ws.send_text(json, len);
}

namespace
{
    // Anything about a call goes to the session that owns it. After a switch
    // that is the held connection, not the one the client is now using: the
    // new account is not in the channel and telling it to leave would do
    // nothing at all.
    gw_conn* voice_conn()
    {
        return holding() ? spare() : active();
    }
}

void gateway::update_voice_state(snowflake guild_id, snowflake channel_id, bool self_mute, bool self_deaf)
{
    jwriter w;
    w.init();
    w.begin_obj();
    w.kv_i64("op", OP_VOICE_STATE_UPDATE);
    w.key("d");
    w.begin_obj();
    if (guild_id) w.kv_snowflake("guild_id", guild_id);
    else w.kv_null("guild_id");
    if (channel_id) w.kv_snowflake("channel_id", channel_id);
    else w.kv_null("channel_id");
    w.kv_bool("self_mute", self_mute);
    w.kv_bool("self_deaf", self_deaf);
    w.kv_bool("self_video", false);

    // Naming the regions back is what a working client does, and it is what
    // decides where a Go Live stream server is allocated. Only sent when
    // actually joining: leaving carries nulls and nothing else.
    if (channel_id && g_region_count)
    {
        w.key("preferred_regions");
        w.begin_arr();
        for (unsigned int i = 0; i < g_region_count; i++) w.val_str(g_regions[i]);
        w.end_arr();
        w.kv_str("preferred_region", g_regions[0]);
    }
    w.end_obj();
    w.end_obj();

    send_json(voice_conn(), &w);
    w.free_writer();
}

const char* gateway::preferred_region()
{
    return g_region_count ? g_regions[0] : "";
}

void gateway::request_guild_members(snowflake guild_id, const char* query, int limit)
{
    jwriter w;
    w.init();
    w.begin_obj();
    w.kv_i64("op", OP_REQUEST_GUILD_MEMBERS);
    w.key("d");
    w.begin_obj();
    w.kv_snowflake("guild_id", guild_id);
    w.kv_str("query", query ? query : "");
    w.kv_i64("limit", limit);
    w.kv_bool("presences", true);
    w.end_obj();
    w.end_obj();

    send_json(active(), &w);
    w.free_writer();
}

void gateway::hold_media(bool on)
{
    InterlockedExchange(&g_hold_media, on ? 1 : 0);
}

void gateway::set_status(const char* status)
{
    if (!status || !status[0]) return;

    jwriter w;
    w.init();
    w.begin_obj();
    w.kv_i64("op", OP_PRESENCE_UPDATE);
    w.key("d");
    w.begin_obj();
    w.kv_str("status", status);
    w.kv_i64("since", 0);
    w.key("activities");
    w.begin_arr();
    w.end_arr();
    w.kv_bool("afk", false);
    w.end_obj();
    w.end_obj();

    send_json(active(), &w);
    w.free_writer();

    // The socket changes it now; the settings call is what makes it stick and
    // what tells the user's other clients.
    api::update_status(status);

    // Invisible is offline as far as everybody else is concerned, and that is
    // what we show ourselves too - otherwise there is no way to tell it worked.
    store::guard g;
    duser* me = store::self();
    if (me)
    {
        if (ccscmp(status, "online") == 0) me->status = STATUS_ONLINE;
        else if (ccscmp(status, "idle") == 0) me->status = STATUS_IDLE;
        else if (ccscmp(status, "dnd") == 0) me->status = STATUS_DND;
        else me->status = STATUS_OFFLINE;
        store::bump_revision();
    }
}

void gateway::subscribe_guild(snowflake guild_id, snowflake channel_id)
{
    jwriter w;
    w.init();
    w.begin_obj();
    w.kv_i64("op", OP_LAZY_REQUEST);
    w.key("d");
    w.begin_obj();
    w.kv_snowflake("guild_id", guild_id);
    w.kv_bool("typing", true);
    w.kv_bool("threads", false);
    w.kv_bool("activities", true);
    if (channel_id)
    {
        w.key("channels");
        w.begin_obj();
        char key[32];
        cnprint(key, sizeof(key), "%llu", channel_id);
        w.key(key);
        w.begin_arr();
        w.begin_arr();
        w.val_i64(0);
        w.val_i64(99);
        w.end_arr();
        w.end_arr();
        w.end_obj();
    }
    w.end_obj();
    w.end_obj();

    send_json(active(), &w);
    w.free_writer();
}
