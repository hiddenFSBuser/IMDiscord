#include "pch.h"
#include "science.h"
#include "rest.h"
#include "store.h"

#include "core/log.h"
#include "core/crypto.h"
#include "core/storage.h"
#include "core/app.h"
#include "net/json.h"
#include "net/http.h"

namespace
{
    char g_token[256];
    char g_ad_session[40];
    char g_client_uuid[40];

    unsigned long long g_started_ms = 0;
    volatile long g_sequence = 0;

    int g_mode = -1;        // read from settings on first use

    void make_guid(char* out, int cap)
    {
        unsigned char b[16];
        crypto::random_bytes(b, sizeof(b));

        static const char* HEX = "0123456789abcdef";
        int at = 0;

        for (int i = 0; i < 16 && at < cap - 3; i++)
        {
            if (i == 4 || i == 6 || i == 8 || i == 10) out[at++] = '-';
            out[at++] = HEX[b[i] >> 4];
            out[at++] = HEX[b[i] & 0x0F];
        }
        out[at] = 0;
    }

    // ---- the queue -------------------------------------------------------
    //
    // Events are gathered and sent in batches on one thread of their own.
    //
    // A request per event was what this did first, on the shared job pool, and
    // it made the client crawl: analytics is chatty, the pool is what avatars
    // and account switching also run on, and every event took a slot from
    // them. The official client batches for the same reason - the captures
    // show several events per POST.
    //
    // Two queues, because a call belongs to whoever opened it. Switching
    // accounts leaves the call running on the old one, and its events have to
    // go out under that identity or they describe somebody sitting in a
    // channel they are not in.
    struct queued
    {
        // One finished event object. Two kilobytes because the invite chain
        // has events carrying a dozen fields of their own on top of the
        // common set, and a truncated event is worse than none.
        char json[2048];
        bool voice;             // belongs to the call's account
    };

    const int QUEUE_MAX = 64;

    queued g_queue[QUEUE_MAX];
    int g_queued = 0;
    CRITICAL_SECTION g_lock;
    bool g_lock_ready = false;

    // What is on screen. Discord fills guild_id and channel_id into events that
    // have nothing to do with either - hovering a picture, closing the viewer -
    // because it takes them from whatever the person is looking at. Recorded
    // when a channel is opened, and kept whatever the setting is, since it
    // costs nothing and the events that use it are gated on their own.
    snowflake g_ctx_channel = 0;
    snowflake g_ctx_guild = 0;

    char g_voice_auth[512];
    char g_voice_analytics[256];

    // The window, as discord reports it. Real numbers rather than a constant:
    // the capture shows the client's actual client area, and a client that
    // always claims the same size is a client somebody could pick out of a
    // crowd.
    void viewport(int* w, int* h)
    {
        *w = 1280;
        *h = 720;

        RECT r;
        if (g_app.hwnd && GetClientRect(g_app.hwnd, &r))
        {
            if (r.right > r.left) *w = (int)(r.right - r.left);
            if (r.bottom > r.top) *h = (int)(r.bottom - r.top);
        }
    }

    // Every batch, written next to the log, when IMD_SCIENCELOG is set. What
    // discord makes of a chain can only be compared against a capture of the
    // real client, and that comparison needs both halves.
    bool science_logging()
    {
        static int on = -1;
        if (on < 0)
        {
            wchar_t v[8];
            on = GetEnvironmentVariableW(L"IMD_SCIENCELOG", v, 8) > 0 ? 1 : 0;
        }
        return on != 0;
    }

    HANDLE g_thread = 0;
    HANDLE g_wake = 0;
    volatile long g_running = 0;

    // One POST carrying the envelope and everything queued for that identity.
    void send_group(bool voice)
    {
        char body[QUEUE_MAX * 2048 + 2048];
        int at = 0;

        const char* analytics = voice ? g_voice_analytics : g_token;
        if (!analytics[0]) return;

        // Unix milliseconds, not ticks since boot. Discord's own numbers here
        // are wall clock, and a client whose timestamps sit near zero while
        // claiming an hour of uptime is a client unlike every other one.
        unsigned long long now = unix_now_ms();

        int view_w = 0, view_h = 0;
        viewport(&view_w, &view_h);

        at += cnprint(body + at, (int)sizeof(body) - at,
                      "{\"token\":\"%s\",\"events\":[", analytics);

        // The envelope first, exactly as the official client sends it.
        at += cnprint(body + at, (int)sizeof(body) - at,
            "{\"type\":\"client_ad_heartbeat\",\"properties\":{"
            "\"client_track_timestamp\":%llu,"
            "\"client_heartbeat_session_id\":\"%s\","
            "\"event_sequence_number\":%d,"
            "\"client_ad_session_id\":\"%s\","
            "\"client_heartbeat_initialization_timestamp\":%llu,"
            "\"client_heartbeat_version\":3,"
            "\"client_performance_memory\":0,"
            "\"accessibility_features\":524416,"
            "\"rendered_locale\":\"en-US\","
            "\"uptime_app\":%llu,"
            "\"launch_signature\":\"%s\","
            "\"client_rtc_state\":\"%s\","
            "\"client_app_state\":\"focused\","
            "\"client_viewport_width\":%d,"
            "\"client_viewport_height\":%d,"
            "\"client_uuid\":\"%s\","
            "\"client_send_timestamp\":%llu}}",
            now, api::heartbeat_session_id(), (int)InterlockedIncrement(&g_sequence),
            g_ad_session, g_started_ms, (now - g_started_ms) / 1000,
            api::launch_signature(), voice ? "RTC_CONNECTED" : "DISCONNECTED",
            view_w, view_h, g_client_uuid, now);

        int taken = 0;

        EnterCriticalSection(&g_lock);
        for (int i = 0; i < g_queued; i++)
        {
            if (g_queue[i].voice != voice) continue;
            if (at + (int)ccslenf(g_queue[i].json) + 8 >= (int)sizeof(body)) break;

            at += cnprint(body + at, (int)sizeof(body) - at, ",%s", g_queue[i].json);
            g_queue[i].json[0] = 0;      // marked as taken
            taken++;
        }

        // Compacted in one pass rather than removing as we went.
        int keep = 0;
        for (int i = 0; i < g_queued; i++)
            if (g_queue[i].json[0]) g_queue[keep++] = g_queue[i];
        g_queued = keep;
        LeaveCriticalSection(&g_lock);

        if (!taken) return;

        at += cnprint(body + at, (int)sizeof(body) - at, "]}");

        if (science_logging()) log_line("science: %s", body);

        http_response res;
        res.init();

        // The call's account signs its own events. Everything else goes out as
        // whoever is signed in.
        if (voice && g_voice_auth[0])
            api::call_as("POST", "/science", body, &res, g_voice_auth);
        else
            api::call("POST", "/science", body, &res);

        res.free_response();
    }

    DWORD WINAPI flush_thread(LPVOID)
    {
        while (g_running)
        {
            // Two seconds, or sooner when somebody asks. Long enough that a
            // burst of events becomes one request, short enough that the last
            // one is never far behind what happened.
            WaitForSingleObject(g_wake, 2000);
            if (!g_running) break;

            send_group(false);
            send_group(true);
        }
        return 0;
    }

    // One finished event object onto the queue. Dropped when the queue is full
    // rather than grown: analytics falling behind is not worth memory, and the
    // envelope still carries the sequence number so the gap is visible.
    void enqueue(const char* json, bool voice)
    {
        if (!g_lock_ready || !json) return;

        EnterCriticalSection(&g_lock);
        if (g_queued < QUEUE_MAX)
        {
            ccstrncpy(g_queue[g_queued].json, json, sizeof(g_queue[0].json) - 1);
            g_queue[g_queued].voice = voice;
            g_queued++;
        }
        bool full = g_queued >= QUEUE_MAX / 2;
        LeaveCriticalSection(&g_lock);

        if (full && g_wake) SetEvent(g_wake);
    }
}

void science::init()
{
    InitializeCriticalSection(&g_lock);
    g_lock_ready = true;

    g_wake = CreateEventW(0, FALSE, FALSE, 0);
    g_running = 1;
    g_thread = CreateThread(0, 0, flush_thread, 0, 0, 0);

    g_started_ms = unix_now_ms();
    make_guid(g_ad_session, sizeof(g_ad_session));

    // The official client's is an opaque blob rather than a guid. Its exact
    // meaning is not published, so a random value of the right shape is what
    // is sent - it identifies nothing here beyond one run.
    unsigned char raw[18];
    crypto::random_bytes(raw, sizeof(raw));

    ubuffer b64;
    b64.init(64);
    crypto::base64_encode(raw, sizeof(raw), &b64);
    ccstrncpy(g_client_uuid, b64.c_str(), sizeof(g_client_uuid) - 1);
    b64.free_buffer();
}

void science::flush()
{
    if (g_wake) SetEvent(g_wake);
}

void science::shutdown()
{
    if (!g_lock_ready) return;

    // One last pass, so what happened just before the client closed is not
    // simply thrown away.
    send_group(false);
    send_group(true);

    InterlockedExchange(&g_running, 0);
    if (g_wake) SetEvent(g_wake);

    if (g_thread) { WaitForSingleObject(g_thread, 2000); CloseHandle(g_thread); g_thread = 0; }
    if (g_wake) { CloseHandle(g_wake); g_wake = 0; }

    DeleteCriticalSection(&g_lock);
    g_lock_ready = false;
}

void science::set_voice_identity(const char* auth_token, const char* analytics_token)
{
    ccfset(g_voice_auth, 0, sizeof(g_voice_auth));
    ccfset(g_voice_analytics, 0, sizeof(g_voice_analytics));

    if (auth_token) ccstrncpy(g_voice_auth, auth_token, sizeof(g_voice_auth) - 1);
    if (analytics_token) ccstrncpy(g_voice_analytics, analytics_token,
                                   sizeof(g_voice_analytics) - 1);
}

void science::clear_voice_identity()
{
    // Whatever is still queued for the call goes out under its own account
    // before the identity is forgotten.
    send_group(true);
    science::set_voice_identity(0, 0);
}

void science::set_token(const char* token)
{
    ccfset(g_token, 0, sizeof(g_token));
    if (token) ccstrncpy(g_token, token, sizeof(g_token) - 1);
}

const char* science::analytics_token() { return g_token; }

bool science::ready() { return g_token[0] != 0; }

science_mode science::mode()
{
    // A bot sends none of this, whatever the setting says.
    //
    // Analytics is here for one reason: a person's client that never reports
    // anything looks unlike every other client on the account, and discord
    // answers that with captchas. A bot is not pretending to be a person and
    // is never asked for one, so the whole mechanism is noise - and reporting
    // a bot's activity as though somebody were sitting there watching would
    // be a lie told for no purpose.
    if (api::is_bot()) return SCIENCE_OFF;

    if (g_mode < 0) g_mode = storage::settings_get_int("science_mode", SCIENCE_MINIMAL);
    if (g_mode < SCIENCE_OFF || g_mode > SCIENCE_ALL) g_mode = SCIENCE_MINIMAL;
    return (science_mode)g_mode;
}

void science::set_mode(science_mode m)
{
    g_mode = (int)m;
    storage::settings_set_int("science_mode", g_mode);
    storage::settings_save();
}

// A batch with nothing in it but the envelope. The official client sends one
// with every action, and it is what makes a batch look like a batch.
static void heartbeat_only()
{
    // The envelope goes out with the next batch whether or not anything is
    // queued beside it, so asking for a flush is the whole of this.
    science::flush();
}

// The properties every event carries. Anything specific to one event is added
// by its caller on top of these.
//
// This used to be three fields, on the reasoning that the rest belong to the
// envelope. The captures say otherwise: every event in them carries the whole
// set, envelope or not, and an event carrying three of them is an event that
// does not look like anybody else's. Which is the entire point of sending
// them at all.
static void common(jwriter* p)
{
    unsigned long long now = unix_now_ms();

    p->begin_obj();
    p->kv_u64("client_track_timestamp", now);
    p->kv_str("client_heartbeat_session_id", api::heartbeat_session_id());
    p->kv_i64("event_sequence_number", InterlockedIncrement(&g_sequence));
    p->kv_i64("client_performance_memory", 0);
    p->kv_i64("accessibility_features", 524416);
    p->kv_str("rendered_locale", "en-US");
    p->kv_u64("uptime_app", (now - g_started_ms) / 1000);
    p->kv_str("launch_signature", api::launch_signature());
    p->kv_str("client_rtc_state", g_voice_analytics[0] ? "RTC_CONNECTED" : "DISCONNECTED");
    p->kv_str("client_app_state", "focused");
    int view_w = 0, view_h = 0;
    viewport(&view_w, &view_h);
    p->kv_i64("client_viewport_width", view_w);
    p->kv_i64("client_viewport_height", view_h);
    p->kv_str("client_uuid", g_client_uuid);
    p->kv_u64("client_send_timestamp", now);
}

// One event, sent only when the chosen setting reaches its tier.
static void emit(const char* type, science_tier tier, jwriter* p, bool voice = false)
{
    if ((int)science::mode() < (int)tier) { if (p) p->free_writer(); return; }

    p->end_obj();

    jwriter ev;
    ev.init();
    ev.begin_obj();
    ev.kv_str("type", type);
    ev.kv_raw("properties", p->c_str());
    ev.end_obj();

    enqueue(ev.c_str(), voice);

    ev.free_writer();
    p->free_writer();
}

// The common case: an event with an id or two and nothing else.
static void emit_simple(const char* type, science_tier tier,
                        const char* key_a = 0, snowflake a = 0,
                        const char* key_b = 0, snowflake b = 0,
                        bool voice = false)
{
    if ((int)science::mode() < (int)tier) return;

    jwriter p;
    p.init();
    common(&p);
    if (key_a && a) p.kv_snowflake(key_a, a);
    if (key_b && b) p.kv_snowflake(key_b, b);
    emit(type, tier, &p, voice);
}

const char* science::mode_name(science_mode m)
{
    switch (m)
    {
    case SCIENCE_OFF:           return tr("Не отправлять");
    case SCIENCE_MINIMAL:       return tr("Только необходимые");
    case SCIENCE_ACCOMPANYING:  return tr("Сопровождающие");
    default:                    return tr("Все");
    }
}

const char* science::mode_note(science_mode m)
{
    switch (m)
    {
    case SCIENCE_OFF:
        return tr("Ничего не уходит. Discord чаще требует CAPTCHA и строже "
               "относится к новым аккаунтам.");
    case SCIENCE_MINIMAL:
        return tr("Только то, что сопровождает добавление в друзья и вход по "
                  "приглашению - без этого discord требует CAPTCHA.");
    case SCIENCE_ACCOMPANYING:
        return tr("То, что вы сделали намеренно: открыли канал, сервер, настройки, "
               "зашли в голосовой. Без слежки за наведением мыши и речью.");
    default:
        return tr("Всё, включая наведение на картинки, начало речи и переключение "
               "микрофона.");
    }
}

// Both friends events carry the same body, so it is written once.
//
// The add-friend tab is the one the narrow setting keeps: it accompanies a
// request and is part of what makes one acceptable. Reading the online list,
// the whole list or the outgoing requests says what somebody is looking at
// and belongs with the rest of the watching.
static void friends_list_event(const char* type, const char* tab)
{
    bool adding = tab && ccscmp(tab, "ADD_FRIEND") == 0;
    science_tier tier = adding ? TIER_ESSENTIAL : TIER_BEHAVIOUR;

    if ((int)science::mode() < (int)tier) return;

    int friends = 0, incoming = 0, outgoing = 0;
    int online = 0, idle = 0, dnd = 0;
    {
        store::guard g;
        const ulist<drelationship>& rels = store::relationships();
        for (unsigned int i = 0; i < rels.count; i++)
        {
            if (rels[i].type == REL_INCOMING) { incoming++; continue; }
            if (rels[i].type == REL_OUTGOING) { outgoing++; continue; }
            if (rels[i].type != REL_FRIEND) continue;

            friends++;

            // Counted rather than sent as zero, which is what this did and
            // what made every one of these events describe a person with no
            // friends online whatever the screen actually showed.
            duser* u = store::find_user(rels[i].user_id);
            if (!u) continue;

            if (u->status == STATUS_ONLINE) online++;
            else if (u->status == STATUS_IDLE) idle++;
            else if (u->status == STATUS_DND) dnd++;
        }
    }

    jwriter p;
    p.init();
    common(&p);
    p.kv_str("tab_opened", tab ? tab : "ALL");
    p.kv_i64("num_friends", friends);
    p.kv_i64("num_friends_online", online);
    p.kv_i64("num_friends_idle", idle);
    p.kv_i64("num_friends_dnd", dnd);
    p.kv_i64("num_outgoing_requests", outgoing);
    p.kv_i64("num_incoming_requests", incoming);

    // Nothing here watches what anybody is playing, so the game counts are
    // the honest zero rather than an invented number, and the list of games
    // detected is empty rather than absent - the field is always present in
    // the capture.
    p.kv_i64("num_game_friends", 0);
    p.kv_i64("num_game_outgoing_requests", 0);
    p.kv_i64("num_game_incoming_requests", 0);
    p.kv_bool("now_playing_visible", false);
    p.kv_i64("now_playing_num_cards", 0);
    p.kv_raw("now_playing_games_detected", "[]");

    emit(type, tier, &p);
}

void science::friends_list_viewed(const char* tab)
{
    friends_list_event("friends_list_viewed", tab);
}

void science::friends_list_clicked(const char* tab)
{
    friends_list_event("friends_list_clicked", tab);
}

void science::blocked_settings_viewed()
{
    if ((int)science::mode() < (int)TIER_BEHAVIOUR) return;

    // Three events for one screen, in the order the capture shows them: the
    // notice discord puts at the top of it, the badge that goes with it, and
    // the visit itself.
    {
        jwriter p;
        p.init();
        common(&p);
        p.kv_str("notice_type", "restricted_accounts_setting_notice");
        p.kv_str("action", "learn_more");
        emit("safety_settings_notice_action", TIER_BEHAVIOUR, &p);
    }

    {
        jwriter p;
        p.init();
        common(&p);
        p.kv_str("type", "CLIENT_THEMES_APPEARANCE_SETTINGS_NEW_BADGE");
        p.kv_i64("content_count", 2);
        p.kv_i64("fatigable_content_count", 0);
        p.kv_bool("bypass_fatigue", true);
        p.kv_null("guild_id");
        emit("dismissible_content_shown", TIER_BEHAVIOUR, &p);
    }

    science::settings_pane_viewed("messaging_permissions_panel");
}

void science::add_friend_input_clicked()
{
    // The official client sends the envelope alone here - the click on the
    // field carries no event of its own. Matching that matters more than
    // inventing one would.
    if ((int)science::mode() >= (int)TIER_ESSENTIAL) heartbeat_only();
}

// ---- the invite chain -----------------------------------------------------

namespace
{
    // When the "+" box was opened, so the step to the join screen can say how
    // long somebody looked at the first one. Discord sends that number and it
    // is never zero for a real person.
    unsigned long long g_guild_add_ms = 0;

    // Both impressions have the same shape and differ only in which screen
    // they name.
    void guild_add_impression(const char* section)
    {
        jwriter p;
        p.init();
        common(&p);
        p.kv_str("impression_type", "modal");
        p.kv_str("impression_group", "guild_add_flow");
        p.kv_raw("location_stack", "[]");
        p.kv_str("location_section", section);
        emit(section, TIER_ESSENTIAL, &p);
    }

    void nuo_transition(const char* from, const char* to, double seconds)
    {
        jwriter p;
        p.init();
        common(&p);
        p.kv_str("flow_type", "create_guild");
        p.kv_str("from_step", from);
        p.kv_str("to_step", to);
        p.key("seconds_on_from_step");
        p.val_dbl(seconds);
        emit("nuo_transition", TIER_ESSENTIAL, &p);
    }

    void open_modal(const char* type)
    {
        jwriter p;
        p.init();
        common(&p);
        p.kv_str("type", type);
        p.kv_str("location", "Guild List");
        emit("open_modal", TIER_ESSENTIAL, &p);
    }
}

void science::guild_add_opened()
{
    if ((int)science::mode() < (int)TIER_ESSENTIAL) return;

    g_guild_add_ms = unix_now_ms();

    guild_add_impression("impression_guild_add_landing");

    // The landing screen is a page inside the modal, and discord reports it
    // separately under a name that has nothing to do with joining.
    {
        jwriter p;
        p.init();
        common(&p);
        p.kv_str("impression_type", "page");
        p.kv_str("variant", "CreateGuildModal");
        p.kv_str("location", "impression_modal_root_legacy");
        p.kv_str("location_page", "impression_modal_root_legacy");
        p.kv_str("location_section", "impression_guild_add_landing");
        emit("impression_modal_root_legacy", TIER_ESSENTIAL, &p);
    }

    // The steps read backwards on purpose. Opening the box arrives at the
    // template picker from wherever the person was, and discord names that
    // previous place join_guild.
    nuo_transition("join_guild", "guild_templates", 0.0);
    open_modal("Create Guild Templates");
}

void science::guild_add_join_step()
{
    if ((int)science::mode() < (int)TIER_ESSENTIAL) return;

    double waited = 0.0;
    if (g_guild_add_ms)
    {
        unsigned long long ms = unix_now_ms() - g_guild_add_ms;
        waited = (double)ms / 1000.0;
    }

    guild_add_impression("impression_guild_add_join");
    nuo_transition("guild_templates", "join_guild", waited);
    open_modal("Join Guild");
}

void science::invite_opened(const char* code)
{
    if ((int)science::mode() < (int)TIER_ESSENTIAL) return;

    // The envelope goes on its own first, exactly as the capture shows: the
    // real client sends a bare heartbeat while the request is in flight and
    // this event in the batch after it.
    heartbeat_only();

    jwriter p;
    p.init();
    common(&p);
    p.kv_str("invite_code", code ? code : "");
    emit("invite_opened", TIER_ESSENTIAL, &p);
}

void science::invite_resolved(const invite_result* r)
{
    if ((int)science::mode() < (int)TIER_ESSENTIAL || !r) return;

    // Two events describing the same answer. They overlap almost entirely -
    // one is filed under the request that was made, the other under what it
    // meant - and discord sends both, in this order, in one batch.
    const char* code = r->code ? r->code : "";
    const char* typed = r->input_value ? r->input_value : code;

    {
        char url[96];
        cnprint(url, sizeof(url), "/invites/%s", code);

        jwriter p;
        p.init();
        common(&p);
        p.kv_i64("status_code", r->status_code);
        p.kv_str("url", url);
        p.kv_str("request_method", "get");
        p.kv_bool("resolved", r->resolved);
        if (r->guild_id) p.kv_snowflake("guild_id", r->guild_id);
        if (r->channel_id) p.kv_snowflake("channel_id", r->channel_id);
        p.kv_i64("channel_type", r->channel_type);
        if (r->inviter_id) p.kv_snowflake("inviter_id", r->inviter_id);
        p.kv_str("code", code);
        p.kv_str("input_value", typed);
        p.kv_bool("authenticated", true);
        p.kv_i64("size_total", r->size_total);
        p.kv_i64("size_online", r->size_online);
        p.kv_str("invite_type", "Server Invite");
        p.kv_bool("user_banned", r->user_banned);
        p.kv_bool("user_is_member", r->user_is_member);
        p.kv_str("location", "Join Guild");
        emit("network_action_invite_resolve", TIER_ESSENTIAL, &p);
    }

    {
        jwriter p;
        p.init();
        common(&p);
        p.kv_bool("resolved", r->resolved);
        if (r->guild_id) p.kv_snowflake("guild_id", r->guild_id);
        if (r->channel_id) p.kv_snowflake("channel_id", r->channel_id);
        p.kv_i64("channel_type", r->channel_type);
        if (r->inviter_id) p.kv_snowflake("inviter_id", r->inviter_id);
        p.kv_str("code", code);
        p.kv_str("input_value", typed);
        p.kv_bool("authenticated", true);
        p.kv_i64("size_total", r->size_total);
        p.kv_i64("size_online", r->size_online);
        p.kv_null("destination_user_id");
        p.kv_str("invite_type", "Server Invite");
        p.kv_bool("user_is_member", r->user_is_member);
        p.kv_null("invite_instance_id");
        p.kv_str("location", "Join Guild");
        emit("resolve_invite", TIER_ESSENTIAL, &p);
    }

    // Straight out rather than waiting for the flush: the join request goes
    // next, and these two are what make it look like the join of somebody who
    // was reading the preview a moment earlier.
    science::flush();
}

// The server a channel belongs to. Callers that already know it pass it in;
// the rest would only be looking it up the same way.
static snowflake guild_of(snowflake channel_id)
{
    if (!channel_id) return 0;

    store::guard g;
    dchannel* c = store::find_channel(channel_id);
    return c ? c->guild_id : 0;
}

void science::channel_opened(snowflake channel_id, snowflake guild_id)
{
    g_ctx_channel = channel_id;
    g_ctx_guild = guild_id;

    emit_simple("channel_opened", TIER_ACTION, "channel_id", channel_id,
                "guild_id", guild_id);
}

void science::guild_viewed(snowflake guild_id)
{
    emit_simple("guild_viewed", TIER_ACTION, "guild_id", guild_id);
}

void science::dm_list_viewed()
{
    emit_simple("dm_list_viewed", TIER_ACTION);
}

void science::settings_pane_viewed(const char* pane)
{
    if ((int)science::mode() < (int)TIER_ACTION) return;

    // "settings_pane" was invented; the capture calls it destination_pane and
    // says which kind of settings these are. A field discord does not know is
    // a field that marks the sender out.
    jwriter p;
    p.init();
    common(&p);
    p.kv_str("settings_type", "user");
    p.kv_str("destination_pane", pane ? pane : "");
    p.kv_raw("location_stack", "[]");
    p.kv_null("search_session_id");
    if (g_ctx_channel) p.kv_snowflake("channel_id", g_ctx_channel);
    if (g_ctx_guild) p.kv_snowflake("guild_id", g_ctx_guild);
    emit("settings_pane_viewed", TIER_ACTION, &p);
}

void science::guild_settings_viewed(const char* which, snowflake guild_id)
{
    if ((int)science::mode() < (int)TIER_ACTION) return;

    // Discord names one event per screen rather than one with a field saying
    // which, so the name is built here.
    char type[64];
    cnprint(type, sizeof(type), "impression_guild_settings_%s", which ? which : "profile");

    jwriter p;
    p.init();
    common(&p);
    if (guild_id) p.kv_snowflake("guild_id", guild_id);
    emit(type, TIER_ACTION, &p);
}

void science::transfer_ownership_opened(snowflake guild_id)
{
    if ((int)science::mode() < (int)TIER_ACTION) return;

    jwriter p;
    p.init();
    common(&p);
    p.kv_str("impression_type", "modal");
    if (guild_id) p.kv_snowflake("guild_id", guild_id);
    p.kv_str("location_section", "impression_guild_transfer_ownership");
    emit("impression_guild_transfer_ownership", TIER_ACTION, &p);
}

void science::transfer_ownership_code_sent(snowflake guild_id, int status)
{
    if ((int)science::mode() < (int)TIER_ACTION) return;

    char url[96];
    cnprint(url, sizeof(url), "/guilds/%llu/pincode", guild_id);

    jwriter p;
    p.init();
    common(&p);
    p.kv_i64("status_code", status);
    p.kv_str("url", url);
    p.kv_str("request_method", "put");
    if (guild_id) p.kv_snowflake("guild_id", guild_id);
    p.kv_bool("is_resend", false);
    p.kv_str("location_section", "impression_guild_settings_members");
    emit("network_action_guild_transfer_ownership_send_code", TIER_ACTION, &p);
}

void science::transfer_ownership_done(snowflake guild_id, int status)
{
    if ((int)science::mode() < (int)TIER_ACTION) return;

    char url[96];
    cnprint(url, sizeof(url), "/guilds/%llu", guild_id);

    jwriter p;
    p.init();
    common(&p);
    p.kv_i64("status_code", status);
    p.kv_str("url", url);
    p.kv_str("request_method", "patch");
    if (guild_id) p.kv_snowflake("guild_id", guild_id);
    p.kv_str("verification_type", "email");
    emit("network_action_guild_transfer_ownership", TIER_ACTION, &p);
}

void science::user_profile_viewed(snowflake user_id)
{
    emit_simple("user_profile_ui_viewed", TIER_ACTION, "other_user_id", user_id);
}

void science::join_voice_channel(snowflake channel_id, snowflake guild_id)
{
    emit_simple("join_voice_channel", TIER_ACTION, "channel_id", channel_id,
                "guild_id", guild_id, true);
}

void science::call_button_clicked(snowflake channel_id)
{
    emit_simple("call_button_clicked", TIER_ACTION, "channel_id", channel_id,
                "guild_id", guild_of(channel_id));
}

void science::ack_messages(snowflake channel_id)
{
    emit_simple("ack_messages", TIER_ACTION, "channel_id", channel_id,
                "guild_id", guild_of(channel_id));
}

// ---- the watching tier ----------------------------------------------------

void science::image_hovered()
{
    emit_simple("image_hovered", TIER_BEHAVIOUR, "channel_id", g_ctx_channel,
                "guild_id", g_ctx_guild);
}

void science::start_speaking(snowflake channel_id, snowflake guild_id)
{
    emit_simple("start_speaking", TIER_BEHAVIOUR, "channel_id", channel_id,
                "guild_id", guild_id, true);
}

void science::start_listening(snowflake channel_id, snowflake guild_id)
{
    emit_simple("start_listening", TIER_BEHAVIOUR, "channel_id", channel_id,
                "guild_id", guild_id, true);
}

void science::input_mute_toggled(bool muted, snowflake channel_id, snowflake guild_id)
{
    if ((int)science::mode() < (int)TIER_BEHAVIOUR) return;

    jwriter p;
    p.init();
    common(&p);
    p.kv_bool("muted", muted);
    if (channel_id) p.kv_snowflake("channel_id", channel_id);
    if (guild_id) p.kv_snowflake("guild_id", guild_id);
    emit("input_mute_toggled", TIER_BEHAVIOUR, &p, true);
}

void science::media_viewer_closed()
{
    emit_simple("media_viewer_session_completed", TIER_BEHAVIOUR,
                "channel_id", g_ctx_channel, "guild_id", g_ctx_guild);
}

void science::voice_connection_success(snowflake channel_id, snowflake guild_id)
{
    emit_simple("voice_connection_success", TIER_BEHAVIOUR, "channel_id", channel_id,
                "guild_id", guild_id, true);
}
