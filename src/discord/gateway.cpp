#include "pch.h"
#include "gateway.h"
#include "store.h"
#include "rest.h"
#include "voice.h"
#include "audio/sounds.h"
#include "science.h"
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

        // Where a resume has to go. READY names a host of its own for this,
        // and it is not the address everybody connects to: the session lives
        // on one node, and asking a different node to resume it gets an
        // invalid session and a full identify - which looks exactly like a
        // resume that discord refused, while nothing was ever wrong with it.
        char resume_url[256];

        // Whose connection this is. A held one belongs to the account that
        // started the call, which after a switch is not the account signed
        // in - and a Go Live built with the wrong id would be refused by a
        // server that had no way to explain why.
        snowflake user_id;

        // Whether this connection has already been READY once. A second
        // READY on the same connection means the session it had is gone and
        // a new one was handed out - which is the moment, and the only
        // moment, that a call cannot survive.
        bool had_session;
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

    // Anything about a call goes to the session that owns it. After a switch
    // that is the held connection, not the one the client is now using: the
    // new account is not in the channel and telling it to leave would do
    // nothing at all.
    gw_conn* voice_conn() { return holding() ? spare() : active(); }

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

    // What a bot asks to be told about.
    //
    // Three of these are privileged and a bot that has not had them switched
    // on in the developer portal is refused outright, with close code 4014 -
    // so they are asked for, and dropped on that refusal rather than left as
    // a wall somebody has to know about in advance.
    const long long INTENT_MEMBERS = 1LL << 1;
    const long long INTENT_PRESENCES = 1LL << 8;
    const long long INTENT_MESSAGE_CONTENT = 1LL << 15;

    const long long INTENTS_PRIVILEGED =
        INTENT_MEMBERS | INTENT_PRESENCES | INTENT_MESSAGE_CONTENT;

    // Guilds, moderation, voice states, messages, reactions, typing, and the
    // same three in direct messages. Everything this client draws.
    const long long INTENTS_PLAIN =
        (1LL << 0) | (1LL << 2) | (1LL << 7) | (1LL << 9) | (1LL << 10) |
        (1LL << 11) | (1LL << 12) | (1LL << 13) | (1LL << 14);

    bool g_intents_denied = false;

    void send_identify(gw_conn* c)
    {
        // A bot identifies with what it wants to hear about; a person's client
        // identifies with what it is. Neither field belongs in the other's
        // payload - discord answers a bot that sends capabilities, or a user
        // that sends intents, by closing the socket.
        if (api::is_bot())
        {
            long long intents = INTENTS_PLAIN;
            if (!g_intents_denied) intents |= INTENTS_PRIVILEGED;

            jwriter b;
            b.init();
            b.begin_obj();
            b.kv_i64("op", OP_IDENTIFY);
            b.key("d");
            b.begin_obj();
            b.kv_str("token", c->token);
            b.kv_i64("intents", intents);
            b.key("properties");
            b.begin_obj();
            b.kv_str("os", "windows");
            b.kv_str("browser", "IMDiscord");
            b.kv_str("device", "IMDiscord");
            b.end_obj();
            b.key("presence");
            b.begin_obj();
            b.kv_str("status", "online");
            b.kv_i64("since", 0);
            b.key("activities");
            b.begin_arr();
            b.end_arr();
            b.kv_bool("afk", false);
            b.end_obj();
            b.end_obj();
            b.end_obj();

            log_line("gateway: identify бота, intents %lld%s", intents,
                     g_intents_denied ? " (без привилегированных)" : "");

            send_json(c, &b);
            b.free_writer();
            set_state(GW_IDENTIFYING, tr("Авторизация..."));
            return;
        }

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
        // The same identity the REST side reports. Two halves of one client
        // describing themselves as different browsers is exactly the sort of
        // inconsistency that draws a captcha demand.
        w.kv_str("browser", "Firefox");
        w.kv_str("device", "");
        w.kv_str("system_locale", "en-US");
        w.kv_bool("has_client_mods", false);
        w.kv_str("browser_user_agent",
                 "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:153.0) "
                 "Gecko/20100101 Firefox/153.0");
        w.kv_str("browser_version", "153.0");
        w.kv_str("os_version", "10");
        w.kv_str("referrer", "");
        w.kv_str("referring_domain", "");
        w.kv_str("referrer_current", "");
        w.kv_str("referring_domain_current", "");
        w.kv_str("release_channel", "stable");
        w.kv_i64("client_build_number", DISCORD_BUILD_NUMBER);
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
        set_state(GW_IDENTIFYING, tr("Авторизация..."));
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
        set_state(GW_IDENTIFYING, tr("Восстановление сессии..."));
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
        // A second READY means the resume was refused and discord issued a
        // new session. The voice state lived on the old one, so whatever was
        // running is already dead on their side; ending it here is what stops
        // it from sitting there sending into nothing.
        //
        // Before the store lock, because stopping a stream waits on its own
        // threads and they have their own reasons to touch the store.
        if (c->had_session && c == voice_conn() && !g_hold_media)
        {
            log_line("gateway: сессия заменена - медиа с прошлой не переживёт этого");
            screenshare::on_gateway_disconnected();
            streamview::on_gateway_disconnected();
            voice::on_gateway_disconnected();
        }
        c->had_session = true;

        store::guard g;

        const char* sid = d->str("session_id", 0);
        if (sid) ccstrncpy(c->session_id, sid, sizeof(c->session_id) - 1);

        const char* rurl = d->str("resume_gateway_url", 0);
        c->resume_url[0] = 0;
        if (rurl && rurl[0])
            cnprint(c->resume_url, sizeof(c->resume_url), "%s/?v=9&encoding=json", rurl);

        // Analytics identifies the session by this, not by the auth token, so
        // nothing can be reported until READY hands it over.
        science::set_token(d->str("analytics_token", 0));

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
        c->user_id = d->obj("user")->sf("id");

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
        cnprint(text, sizeof(text), tr("В сети как %s"),
                me ? me->display_name() : "?");
        set_state(GW_READY, text);
        c->backoff_ms = 1000;
    }

    // The dispatches that belong to the call rather than to the account.
    //
    // These are the ones a held connection has to be allowed to deliver: it
    // is the session discord associates the call with, so every answer about
    // that call arrives on it and nowhere else. Dropping them is what left a
    // Go Live hanging after a switch and back - op 18 went out and the
    // STREAM_CREATE that answers it was thrown away unread, so the share sat
    // in "requesting" until the channel was rejoined on the new session.
    bool call_dispatch(const char* type)
    {
        return ccscmp(type, "VOICE_STATE_UPDATE") == 0 ||
               ccscmp(type, "VOICE_SERVER_UPDATE") == 0 ||
               ccscmp(type, "STREAM_CREATE") == 0 ||
               ccscmp(type, "STREAM_SERVER_UPDATE") == 0 ||
               ccscmp(type, "STREAM_UPDATE") == 0 ||
               ccscmp(type, "STREAM_DELETE") == 0;
    }

    void handle_dispatch(gw_conn* c, const char* type, const jval* d)
    {
        if (!type) return;

        // A held connection is alive only to keep a call open. Folding its
        // dispatches into the store would drag the previous account's servers
        // and messages back over the one that is signed in now - everything
        // except what the call itself is made of.
        if (!c->dispatching && !call_dispatch(type)) return;

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
            set_state(GW_READY, tr("Сессия восстановлена"));

            // A resume that worked is a connection that worked, so the wait
            // before the next attempt goes back to a second. Left growing, a
            // run of ordinary drops would have the client sitting out half a
            // minute before reconnecting from a socket that dies instantly.
            c->backoff_ms = 1000;
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

            // Direct messages only, and not our own coming back. A chime for
            // every message on every server would be unusable, and a server is
            // where a person expects to be the one who goes looking.
            if (ccscmp(type, "MESSAGE_CREATE") == 0)
            {
                store::guard g;
                snowflake author = d->obj("author")->sf("id");
                dchannel* c = store::find_channel(d->sf("channel_id"));

                if (c && c->is_dm() && author && author != store::self_id())
                    sounds::play(SOUND_NOTIFY);
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

        // Roles, created, changed and deleted. Without these the client keeps
        // showing what the roles were when it connected, which after editing
        // one from inside the client looks exactly like the edit failing.
        if (ccscmp(type, "GUILD_ROLE_CREATE") == 0 ||
            ccscmp(type, "GUILD_ROLE_UPDATE") == 0)
        {
            store::guard g;
            dguild* guild = store::find_guild(d->sf("guild_id"));
            if (guild) store::upsert_role(guild, d->obj("role"));
            return;
        }

        if (ccscmp(type, "GUILD_ROLE_DELETE") == 0)
        {
            store::guard g;
            dguild* guild = store::find_guild(d->sf("guild_id"));
            if (guild) store::remove_role(guild, d->sf("role_id"));
            return;
        }

        // Somebody's roles changed, which is what comes back from handing one
        // out or taking it away.
        if (ccscmp(type, "GUILD_MEMBER_UPDATE") == 0)
        {
            store::guard g;
            dguild* guild = store::find_guild(d->sf("guild_id"));
            if (guild) store::add_guild_member(guild, d);
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
                mem.timeout_until_ms = iso_to_unix_ms(m->str("communication_disabled_until", 0));
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
            snowflake channel = d->sf("channel_id");

            // What this person was doing a moment ago, read before the store is
            // told the new state. Arriving, leaving and starting a share are
            // all the same dispatch, and only the difference says which.
            snowflake was_in = 0;
            bool was_streaming = false;
            {
                store::guard g;
                const dvoice_state* before = store::find_voice_state(uid);
                if (before)
                {
                    was_in = before->channel_id;
                    was_streaming = before->self_stream;
                }
                store::set_voice_state(d, d->sf("guild_id"));
            }

            // Only about the channel this client is sitting in. Somebody
            // joining a call three servers away is not an event here.
            snowflake mine = voice::current_channel();
            if (mine)
            {
                bool here_now = channel == mine;
                bool here_before = was_in == mine;

                if (here_now && !here_before) sounds::play(SOUND_VOICE_JOIN);
                else if (!here_now && here_before) sounds::play(SOUND_VOICE_LEAVE);

                if (here_now && uid != store::self_id())
                {
                    bool streaming = d->boolean("self_stream", false);
                    if (streaming && !was_streaming) sounds::play(SOUND_STREAM_START);
                    else if (!streaming && was_streaming) sounds::play(SOUND_STREAM_STOP);
                }
            }

            if (uid == store::self_id()) voice::on_gateway_voice_state(d);
            return;
        }

        // A call in a direct message or a group. Unlike a server's voice
        // channel, which announces itself through the channel list, a call
        // here exists only as this dispatch and the voice states it carries -
        // so without reading it there is no way to know a call is running at
        // all, which is exactly how it looked from the inside.
        if (ccscmp(type, "CALL_CREATE") == 0 || ccscmp(type, "CALL_UPDATE") == 0)
        {
            store::guard g;

            snowflake channel_id = d->sf("channel_id");

            // CALL_CREATE carries everyone already in it. CALL_UPDATE mostly
            // carries who is still being rung, and leaves the states alone.
            const jval* states = d->arr("voice_states");
            for (unsigned int i = 0; i < states->count; i++)
                store::set_voice_state(states->at(i), 0);

            // Who the call is ringing, so a chat that is calling this account
            // can say so rather than only lighting up when somebody answers.
            ulist<snowflake> ringing;

            const jval* who = d->arr("ringing");
            for (unsigned int i = 0; i < who->count; i++)
                ringing.push(who->at(i)->as_snowflake());

            store::set_call_ringing(channel_id, &ringing);
            ringing.freelist();

            store::bump_revision();
            return;
        }

        if (ccscmp(type, "CALL_DELETE") == 0)
        {
            store::guard g;

            snowflake channel_id = d->sf("channel_id");
            store::clear_voice_states_for_channel(channel_id);
            store::clear_call_ringing(channel_id);
            store::bump_revision();
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

            // Logged either way. A resume that never happens is otherwise
            // indistinguishable from one that failed, and the difference is
            // the difference between a bug here and a decision at discord.
            if (c->want_resume && c->session_id[0])
            {
                log_line("gateway: RESUME, сессия %.8s..., seq %ld%s",
                         c->session_id, (long)c->sequence,
                         c->resume_url[0] ? "" : " (общий адрес - READY не дал свой)");
                send_resume(c);
            }
            else
            {
                if (c->want_resume)
                    log_line("gateway: восстановить нечего - сессии нет, представляюсь заново");
                send_identify(c);
            }
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
            set_state(GW_CONNECTING, tr("Подключение к discord..."));

            c->ws.init();
            const char* where = (c->want_resume && c->resume_url[0]) ? c->resume_url
                                                                    : GATEWAY_URL;

            if (!c->ws.connect(where, "Origin: https://discord.com\r\n",
                               c->proxy.in_use() ? &c->proxy : 0))
            {
                c->ws.destroy();
                set_state(GW_RECONNECTING, tr("Нет связи, повтор..."));

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

            // 4014: a privileged intent this bot has not been granted. Asking
            // again without them is the difference between a client that
            // explains itself and one that simply never connects.
            if (c->ws.close_status == 4014 && api::is_bot() && !g_intents_denied)
            {
                g_intents_denied = true;
                log_line("gateway: привилегированные intents не разрешены в портале - "
                         "переподключаемся без них (не будет списка участников, "
                         "статусов и текста сообщений)");
                c->session_id[0] = 0;
            }

            // A socket that died on its own is the case resume exists for.
            //
            // Only two paths used to ask for one - the server saying reconnect,
            // and a session it says is still good - so every ordinary drop
            // ended in a fresh identify: the whole store thrown away and
            // rebuilt, the open channel and the call with it. That is what
            // being "kicked out" looked like, and a 1006 arrives on its own
            // schedule, anywhere from a quarter of an hour to five hours in.
            //
            // Resuming asks discord to replay what was missed on the same
            // session. If it will not, it answers with an invalid session and
            // the identify happens then - one round trip later and nothing
            // lost in the ordinary case.
            if (!c->want_resume && c->session_id[0] && c->sequence > 0)
            {
                log_line("gateway: попробуем восстановить сессию (seq %ld)", (long)c->sequence);
                c->want_resume = true;
            }

            InterlockedExchange(&c->heartbeat_ms, 0);
            // The streams go first: they are signalled over this socket, so
            // once it is gone neither can be stopped or kept alive.
            //
            // Unless the socket is being replaced on purpose, which is what an
            // account switch is. The voice connection and the stream have their
            // own sockets and their own accepted tokens; nothing about them
            // needs this one.
            //
            // And unless this is not the connection the call is on. After an
            // account switch the call sits on the held socket, kept alive only
            // so the server does not decide the call left with it, and the one
            // in front has nothing to do with it either way. Letting whichever
            // socket happened to be in front run this made release_hold end
            // whatever call was running at the time - the call it was being
            // held for included, which is the whole point of holding it - and
            // the log showed exactly that: the hold released, and the voice
            // socket dying two milliseconds behind it.
            //
            // And unless the session is coming back. This is the one that was
            // still throwing people out of calls.
            //
            // A call does not belong to this socket, it belongs to the
            // session behind it, and a resume keeps that session. Discord
            // asks for a reconnect on its own schedule - which is exactly the
            // "fifteen minutes or five hours" of the complaint - and every
            // one of those ended the call here, three lines before the
            // reconnect that would have made it unnecessary. The log read:
            // server asked for a reconnect, hold released, voice socket dead.
            //
            // If the resume is refused, the session really is gone, and the
            // fresh READY tears the media down then - see handle_ready.
            if (!g_hold_media && c == voice_conn() && !c->want_resume)
            {
                screenshare::on_gateway_disconnected();
                streamview::on_gateway_disconnected();
                voice::on_gateway_disconnected();
            }
            c->ws.destroy();

            if (!c->running) break;

            set_state(GW_RECONNECTING, tr("Переподключение..."));
            if (WaitForSingleObject(c->stop_event, c->backoff_ms) == WAIT_OBJECT_0) break;
            c->backoff_ms = c->backoff_ms < 30000 ? c->backoff_ms * 2 : 30000;
        }

        message.free_buffer();
        set_state(GW_OFFLINE, tr("Отключено"));
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
    set_state(GW_OFFLINE, tr("Отключено"));
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

// Every caller of this is Go Live - starting one, ending one, or asking to
// watch somebody else's - and all of it belongs to the call, so it goes
// where the call lives.
//
// It used to go to whichever connection was in front. Switching accounts and
// coming back leaves two: the one holding the call, and the fresh one the
// account signed in on. Sending op 18 to the fresh one asks a session that
// is not in the channel to start a stream in it, and discord simply says
// nothing back.
bool gateway::send_raw(const void* json, unsigned int len)
{
    return voice_conn()->ws.send_text(json, len);
}

snowflake gateway::call_owner_id() { return voice_conn()->user_id; }

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
    // Op 14 belongs to the client a person uses: it asks discord to start
    // streaming the member list and typing for the part of a server being
    // looked at. A bot is told about its servers whether it asks or not, and
    // sending this on a bot connection is at best ignored and at worst a
    // decode error that closes the socket.
    if (api::is_bot()) return;

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
