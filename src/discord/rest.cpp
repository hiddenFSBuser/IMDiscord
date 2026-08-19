#include "pch.h"
#include "rest.h"
#include "store.h"
#include "archive.h"
#include "core/offline.h"
#include "core/log.h"
#include "core/crypto.h"
#include "core/storage.h"
#include "system/io/ufile.h"
#include "net/json.h"
#include "gateway.h"
#include "science.h"

namespace
{
    const char* API_BASE = "https://discord.com/api/v9";

    char g_token[512];
    char g_super_properties[1024];
    char g_last_error[256];
    char g_last_link[256];
    char g_captcha_sitekey[128];
    char g_captcha_rqtoken[256];
    CRITICAL_SECTION g_err_lock;
    bool g_ready = false;

    char g_launch_id[40];
    char g_launch_signature[40];
    char g_heartbeat_session[40];
    char g_installation_id[96];

    // Whether the token above belongs to a bot application.
    bool g_token_is_bot = false;

    // 8-4-4-4-12 hex, which is the shape these fields take. Random rather than
    // derived from anything: they identify a run, not a machine.
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

    // Which installation this is, as the browser reports it: a snowflake, a
    // dot, and an opaque tail. Unlike the three above it survives restarts -
    // that is the whole of what it says, "the same install as last time" - so
    // it is made once and kept beside the other settings.
    //
    // The browser sends it on every request. This client sent none, and one
    // of the two gets asked for a CAPTCHA on an invite.
    void build_installation_id()
    {
        const char* stored = storage::settings_get("installation_id", "");
        if (stored && stored[0])
        {
            ccstrncpy(g_installation_id, stored, sizeof(g_installation_id) - 1);
            return;
        }

        unsigned char raw[21];
        crypto::random_bytes(raw, sizeof(raw));

        ubuffer b64;
        b64.init(64);
        crypto::base64_encode(raw, sizeof(raw), &b64);

        // Url-safe and unpadded, which is the alphabet the real one uses.
        char tail[48];
        int at = 0;
        for (const char* q = b64.c_str(); *q && at < (int)sizeof(tail) - 1; q++)
        {
            char c = *q;
            if (c == '=') continue;
            if (c == '+') c = '-';
            if (c == '/') c = '_';
            tail[at++] = c;
        }
        tail[at] = 0;
        b64.free_buffer();

        // A snowflake is a moment in time; the low bits discord fills with
        // worker and sequence numbers are left at zero rather than invented.
        unsigned long long id = (unix_now_ms() - DISCORD_EPOCH_MS) << 22;

        cnprint(g_installation_id, sizeof(g_installation_id), "%llu.%s", id, tail);
        storage::settings_set("installation_id", g_installation_id);
        storage::settings_save();
    }

    void build_super_properties()
    {
        make_guid(g_launch_id, sizeof(g_launch_id));
        make_guid(g_launch_signature, sizeof(g_launch_signature));
        make_guid(g_heartbeat_session, sizeof(g_heartbeat_session));

        jwriter w;
        w.init();
        w.begin_obj();
        w.kv_str("os", "Windows");
        // Browser, version and user agent have to agree with each other. They
        // did not: this said Chrome with a Chrome 120 agent while the number
        // beside them described a build from years later. An inconsistent set
        // is one of the things that gets a request answered with a captcha
        // demand rather than a result.
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

        // Four fields the real client always carries and this one never did.
        // They identify one run of the application, so they are made once at
        // startup and kept for its lifetime - a value that changed per request
        // would describe a client restarting between every two of them.
        w.kv_str("client_launch_id", g_launch_id);
        w.kv_str("launch_signature", g_launch_signature);
        w.kv_str("client_heartbeat_session_id", g_heartbeat_session);
        w.kv_str("client_app_state", "focused");
        w.end_obj();

        ubuffer b64;
        b64.init();
        crypto::base64_encode(w.buf.data, w.buf.size, &b64);
        ccstrncpy(g_super_properties, b64.c_str(), sizeof(g_super_properties) - 1);

        b64.free_buffer();
        w.free_writer();
    }

    // Where in the interface an action was taken. Discord requires it on the
    // relationship endpoints and rejects the request without it - which is why
    // adding, accepting and removing friends all failed while looking like
    // ordinary requests on the wire.
    //
    // Base64 of a small json object, exactly as a capture of the real client
    // shows: {"location":"Add Friend"} for adding by name, {"location":
    // "Friends"} for everything done from the friends list.
    void build_headers(ubuffer* out, const char* content_type, const char* location,
                       const char* auth_override = 0)
    {
        out->clear();

        // Normally whoever is signed in. The override exists for analytics
        // about a call: a call outlives switching accounts, and its events
        // have to be signed by the account actually sitting in the channel.
        const char* auth = (auth_override && auth_override[0]) ? auth_override : g_token;

        if (auth[0])
        {
            out->append_str("Authorization: ");
            // Bot tokens are prefixed; a user token sent with the prefix is
            // refused, and so is a bot token sent without it.
            if (g_token_is_bot) out->append_str("Bot ");
            out->append_str(auth);
            out->append_str("\r\n");
        }
        if (content_type)
        {
            out->append_str("Content-Type: ");
            out->append_str(content_type);
            out->append_str("\r\n");
        }

        // Everything below this line describes a person at a browser: which
        // build they run, where they clicked, what their locale is. A bot has
        // none of it, and sending it anyway is not merely untrue - discord's
        // edge refuses the request, which is what "internal network error" on
        // every message and every history load turned out to be.
        if (g_token_is_bot) return;
        if (location && location[0])
        {
            // Either a bare place name, which is the common case, or a whole
            // object already written by the caller. The invite join needs the
            // second: discord wants the channel it is joining through named
            // in there beside the location.
            char json[288];
            if (location[0] == '{') ccstrncpy(json, location, sizeof(json) - 1);
            else cnprint(json, sizeof(json), "{\"location\":\"%s\"}", location);

            ubuffer b64;
            b64.init(sizeof(json) * 2);
            crypto::base64_encode(json, (unsigned int)ccslenf(json), &b64);

            out->append_str("X-Context-Properties: ");
            out->append_str(b64.c_str());
            out->append_str("\r\n");
            b64.free_buffer();
        }
        out->append_str("X-Super-Properties: ");
        out->append_str(g_super_properties);
        out->append_str("\r\n");
        if (g_installation_id[0])
        {
            out->append_str("X-Installation-ID: ");
            out->append_str(g_installation_id);
            out->append_str("\r\n");
        }
        out->append_str("X-Discord-Locale: en-US\r\n");
        out->append_str("X-Discord-Timezone: Europe/Moscow\r\n");
        out->append_str("X-Debug-Options: bugReporterEnabled\r\n");
        out->append_str("Accept-Language: en-US,en;q=0.9\r\n");
        out->append_str("Origin: https://discord.com\r\n");
        out->append_str("Referer: https://discord.com/channels/@me\r\n");
        out->append_str("Sec-Fetch-Dest: empty\r\n");
        out->append_str("Sec-Fetch-Mode: cors\r\n");
        out->append_str("Sec-Fetch-Site: same-origin\r\n");
    }

    // Pulls the human readable part out of a discord error body.
    // "Invalid Form Body" is the whole of what the top of a 50035 says. Which
    // field discord disliked, and why, sits one or more levels down under
    // "errors", keyed by the field name, with the reason in an "_errors"
    // array. Reporting only the top line leaves nothing to act on - it is the
    // same sentence whether the code was mistyped or the account cannot be
    // handed a server at all.
    //
    // Walks to the first "_errors" it finds and builds the dotted path on the
    // way, so the report names the field.
    bool first_form_error(const jval* node, char* path, int path_cap, const char** msg)
    {
        if (!node || node->type != JTYPE_OBJ) return false;

        const jval* list = node->arr("_errors");
        if (list->size())
        {
            const jval* first = list->at(0);
            const char* m = first ? first->str("message", 0) : 0;
            if (m) { *msg = m; return true; }
        }

        int used = 0;
        while (path[used]) used++;

        for (unsigned int i = 0; i < node->size(); i++)
        {
            const jmember* mem = node->member_at(i);
            if (!mem || !mem->key || !mem->value) continue;

            int at = used;
            if (at && at < path_cap - 1) path[at++] = '.';
            for (unsigned int k = 0; k < mem->key_len && at < path_cap - 1; k++)
                path[at++] = mem->key[k];
            path[at] = 0;

            if (first_form_error(mem->value, path, path_cap, msg)) return true;

            path[used] = 0;
        }

        return false;
    }

    void record_api_error(const char* what, http_response* res)
    {
        // Two answers mean more than the request that got them. Nothing at all
        // says the network is gone; a 401 says the token is.
        if (res->status == 0) offline::note_network_failure();
        else if (res->status == 401) offline::note_token_rejected();

        char text[256];
        const char* detail = 0;

        jdoc doc;
        doc.init();
        if (doc.parse(res->text(), (int)res->body.size))
        {
            detail = doc.root->str("message", 0);
            if (!detail)
            {
                const char* errs = doc.root->obj("errors")->str("message", 0);
                if (errs) detail = errs;
            }
        }

        // Discord answers some refusals with a body that carries no message at
        // all - a captcha demand is the common one, and it is what a friend
        // request from an unfamiliar client tends to get. Reporting those as a
        // bare status code hides the only useful thing in the response.
        bool captcha = doc.root && (doc.root->has("captcha_key") ||
                                    doc.root->has("captcha_sitekey") ||
                                    doc.root->has("captcha_service"));

        if (captcha)
        {
            cnprint(text, sizeof(text),
                    tr("%s: discord требует пройти CAPTCHA - этот запрос надо "
                    "сделать один раз в браузере или официальном клиенте"), what);
        }
        else if (detail)
        {
            char field[128];
            field[0] = 0;

            const char* deep = 0;
            if (doc.root && first_form_error(doc.root->obj("errors"), field,
                                             sizeof(field), &deep))
                cnprint(text, sizeof(text), "%s: %s - %s: %s", what, detail, field, deep);
            else
                cnprint(text, sizeof(text), "%s: %s", what, detail);
        }
        else if (res->body.size)
        {
            // The raw answer, cut short. Better an unfamiliar json fragment
            // than a number that says only that something was refused.
            cnprint(text, sizeof(text), "%s: HTTP %d %.120s", what, res->status, res->text());
        }
        else
        {
            cnprint(text, sizeof(text), "%s: HTTP %d", what, res->status);
        }

        doc.free_doc();
        api::set_last_error(text);
        log_line("api: %s", text);
    }
}

void api::init()
{
    if (g_ready) return;
    InitializeCriticalSection(&g_err_lock);
    ccfset(g_token, 0, sizeof(g_token));
    ccfset(g_last_error, 0, sizeof(g_last_error));
    build_installation_id();
    build_super_properties();
    g_ready = true;
}

void api::shutdown()
{
    if (!g_ready) return;
    ccfset(g_token, 0, sizeof(g_token));
    DeleteCriticalSection(&g_err_lock);
    g_ready = false;
}

void api::set_token(const char* t, bool bot)
{
    ccfset(g_token, 0, sizeof(g_token));
    if (t) ccstrncpy(g_token, t, sizeof(g_token) - 1);

    g_token_is_bot = bot;
    http::set_bot_mode(bot);
}

const char* api::token() { return g_token; }
bool api::has_token() { return g_token[0] != 0; }
bool api::is_bot() { return g_token_is_bot; }

void api::set_last_error(const char* text)
{
    if (!g_ready) return;
    EnterCriticalSection(&g_err_lock);
    ccfset(g_last_error, 0, sizeof(g_last_error));
    if (text) ccstrncpy(g_last_error, text, sizeof(g_last_error) - 1);
    LeaveCriticalSection(&g_err_lock);
}

const char* api::last_error() { return g_last_error; }
void api::clear_last_error() { set_last_error(""); }

// Guarded by the same lock as the error text, and for the same reason: it is
// written on a job thread and read by the interface.
void api::set_last_link(const char* url)
{
    if (!g_ready) return;
    EnterCriticalSection(&g_err_lock);
    ccfset(g_last_link, 0, sizeof(g_last_link));
    if (url) ccstrncpy(g_last_link, url, sizeof(g_last_link) - 1);
    LeaveCriticalSection(&g_err_lock);
}

const char* api::last_link() { return g_last_link; }
void api::clear_last_link() { set_last_link(""); }

bool api::call_absolute(const char* method, const char* url, const char* json_body,
                        http_response* out, const char* location)
{
    ubuffer headers;
    headers.init();
    build_headers(&headers, json_body ? "application/json" : 0, location);

    unsigned int body_len = json_body ? (unsigned int)ccslenf(json_body) : 0;
    bool ok = http::request(method, url, headers.c_str(), json_body, body_len, out);
    headers.free_buffer();

    // A 429 asks us to back off; the caller decides whether to retry.
    if (ok && out->status == 429)
    {
        jdoc doc;
        doc.init();
        if (doc.parse(out->text(), (int)out->body.size))
            out->retry_after_ms = (int)(doc.root->dbl("retry_after", 1.0) * 1000.0);
        doc.free_doc();
        if (out->retry_after_ms <= 0) out->retry_after_ms = 1000;
    }
    return ok;
}

bool api::call(const char* method, const char* path, const char* json_body,
               http_response* out, const char* location)
{
    char url[2048];
    cnprint(url, sizeof(url), "%s%s", API_BASE, path);
    if (out->status) offline::note_network_success();
    return call_absolute(method, url, json_body, out, location);
}

bool api::call_as(const char* method, const char* path, const char* json_body,
                  http_response* out, const char* auth_token)
{
    char url[2048];
    cnprint(url, sizeof(url), "%s%s", API_BASE, path);

    ubuffer headers;
    headers.init();
    build_headers(&headers, json_body ? "application/json" : 0, 0, auth_token);

    unsigned int body_len = json_body ? (unsigned int)ccslenf(json_body) : 0;
    bool ok = http::request(method, url, headers.c_str(), json_body, body_len, out);
    headers.free_buffer();
    return ok;
}

bool api::verify_token(const char* token_value, bool is_bot, char* out_error, int error_cap)
{
    char saved[512];
    ccstrncpy(saved, g_token, sizeof(saved) - 1);
    saved[sizeof(saved) - 1] = 0;

    bool saved_bot = g_token_is_bot;
    set_token(token_value, is_bot);

    http_response res;
    res.init();
    bool ok = false;

    if (!call("GET", "/users/@me", 0, &res))
    {
        cnprint(out_error, error_cap, tr("Нет соединения с discord.com"));
    }
    else if (res.status == 401)
    {
        cnprint(out_error, error_cap, tr("Токен отклонён (401)"));
    }
    else if (!res.ok())
    {
        cnprint(out_error, error_cap, tr("Ошибка HTTP %d"), res.status);
    }
    else
    {
        jdoc doc;
        doc.init();
        if (doc.parse(res.text(), (int)res.body.size) && doc.root->sf("id"))
        {
            store::guard g;
            duser* me = store::upsert_user(doc.root);
            if (me) store::set_self_id(me->id);
            ok = true;
            out_error[0] = 0;
        }
        else
        {
            cnprint(out_error, error_cap, tr("Неожиданный ответ сервера"));
        }
        doc.free_doc();
    }

    res.free_response();
    if (!ok) set_token(saved, saved_bot);
    ccfset(saved, 0, sizeof(saved));
    return ok;
}

// ---------------------------------------------------------------------------
// async jobs
// ---------------------------------------------------------------------------

namespace
{
    struct job_ids
    {
        snowflake a;
        snowflake b;
    };

    struct job_text
    {
        snowflake channel_id;
        snowflake reply_to;
        char text[4096];
    };

    struct job_upload
    {
        snowflake channel_id;
        char text[2048];
        ulist<upload_file> files;
    };

    struct job_name
    {
        char text[128];

        // What was typed before it was cut down to a code. Only the invite
        // join uses it: discord wants the whole link reported beside the
        // code, and the two are not the same string.
        char full[192];
    };

    struct job_friend
    {
        char name[128];
        char captcha_key[2048];      // hcaptcha tokens are long
        char captcha_rqtoken[256];
    };

    void job_fetch_messages(void* user)
    {
        job_ids* j = (job_ids*)user;

        char path[256];
        if (j->b)
            cnprint(path, sizeof(path), "/channels/%llu/messages?limit=50&before=%llu", j->a, j->b);
        else
            cnprint(path, sizeof(path), "/channels/%llu/messages?limit=50", j->a);

        http_response res;
        res.init();

        if (api::call("GET", path, 0, &res) && res.ok())
        {
            jdoc doc;
            doc.init();
            if (doc.parse(res.text(), (int)res.body.size) && doc.root->type == JTYPE_ARR)
            {
                snowflake oldest = 0, newest = 0;

                {
                    store::guard g;
                    for (unsigned int i = 0; i < doc.root->count; i++)
                    {
                        dmessage* m = store::upsert_message(doc.root->at(i));
                        if (!m) continue;
                        if (!oldest || m->id < oldest) oldest = m->id;
                        if (m->id > newest) newest = m->id;
                    }

                    dchannel* ch = store::find_channel(j->a);
                    if (ch)
                    {
                        ch->history_loading = false;
                        ch->history_loaded = true;
                        ch->history_failed = false;
                        if (doc.root->count < 50) ch->history_exhausted = true;
                    }
                    store::bump_revision();
                }

                // Everything discord hands over is kept, and the stretch it
                // covers is written down: an export can then tell a quiet
                // evening from a hole where nothing was ever fetched.
                for (unsigned int i = 0; i < doc.root->count; i++)
                    archive::put_json(doc.root->at(i));

                if (oldest && newest) archive::note_range(j->a, oldest, newest);
            }
            doc.free_doc();
        }
        else
        {
            record_api_error(tr("Не удалось загрузить историю"), &res);
            store::guard g;
            dchannel* ch = store::find_channel(j->a);
            if (ch)
            {
                ch->history_loading = false;
                ch->history_failed = true;
            }
        }

        res.free_response();
        memfree(j);
    }

    void job_fetch_sessions(void* user)
    {
        http_response res;
        res.init();

        bool ok = false;
        if (api::call("GET", "/auth/sessions", 0, &res) && res.ok())
        {
            jdoc doc;
            doc.init();
            if (doc.parse(res.text(), (int)res.body.size) && doc.root->type == JTYPE_OBJ)
            {
                store::guard g;
                store::set_sessions(doc.root->arr("user_sessions"));
                ok = true;
            }
            doc.free_doc();
        }

        if (!ok)
        {
            store::guard g;
            store::set_sessions_failed();
        }

        res.free_response();
    }

    void job_send_message(void* user)
    {
        job_text* j = (job_text*)user;

        jwriter w;
        w.init();
        w.begin_obj();
        w.kv_str("content", j->text);
        w.kv_i64("flags", 0);
        // A random nonce lets discord de-duplicate resends.
        unsigned long long nonce = 0;
        crypto::random_bytes(&nonce, sizeof(nonce));
        w.kv_snowflake("nonce", nonce >> 4);
        if (j->reply_to)
        {
            w.key("message_reference");
            w.begin_obj();
            w.kv_snowflake("message_id", j->reply_to);
            w.kv_snowflake("channel_id", j->channel_id);
            w.end_obj();
        }
        w.end_obj();

        char path[128];
        cnprint(path, sizeof(path), "/channels/%llu/messages", j->channel_id);

        http_response res;
        res.init();
        if (api::call("POST", path, w.c_str(), &res) && res.ok())
        {
            jdoc doc;
            doc.init();
            if (doc.parse(res.text(), (int)res.body.size))
            {
                store::guard g;
                store::upsert_message(doc.root);
                store::bump_revision();
            }
            doc.free_doc();
        }
        else
        {
            record_api_error(tr("Сообщение не отправлено"), &res);
        }

        res.free_response();
        w.free_writer();
        memfree(j);
    }

    void job_send_upload(void* user)
    {
        job_upload* j = (job_upload*)user;

        char boundary[64];
        unsigned char rnd[16];
        crypto::random_bytes(rnd, sizeof(rnd));
        cnprint(boundary, sizeof(boundary), "----IMDiscord%llx%llx",
                *(unsigned long long*)rnd, *(unsigned long long*)(rnd + 8));

        jwriter payload;
        payload.init();
        payload.begin_obj();
        payload.kv_str("content", j->text);
        payload.key("attachments");
        payload.begin_arr();
        for (unsigned int i = 0; i < j->files.count; i++)
        {
            payload.begin_obj();
            payload.kv_i64("id", (long long)i);
            payload.kv_str("filename", j->files[i].name);
            payload.end_obj();
        }
        payload.end_arr();
        payload.end_obj();

        ubuffer body;
        body.init(1 << 20);

        body.append_fmt("--%s\r\n", boundary);
        body.append_str("Content-Disposition: form-data; name=\"payload_json\"\r\n");
        body.append_str("Content-Type: application/json\r\n\r\n");
        body.append(payload.buf.data, payload.buf.size);
        body.append_str("\r\n");

        for (unsigned int i = 0; i < j->files.count; i++)
        {
            upload_file* f = &j->files[i];
            body.append_fmt("--%s\r\n", boundary);
            body.append_fmt("Content-Disposition: form-data; name=\"files[%u]\"; filename=\"%s\"\r\n", i, f->name);
            body.append_fmt("Content-Type: %s\r\n\r\n", f->content_type[0] ? f->content_type : "application/octet-stream");
            body.append(f->data, f->size);
            body.append_str("\r\n");
        }
        body.append_fmt("--%s--\r\n", boundary);

        char content_type[128];
        cnprint(content_type, sizeof(content_type), "multipart/form-data; boundary=%s", boundary);

        ubuffer headers;
        headers.init();
        build_headers(&headers, content_type, 0);

        char url[256];
        cnprint(url, sizeof(url), "%s/channels/%llu/messages", API_BASE, j->channel_id);

        http_response res;
        res.init();
        if (http::request("POST", url, headers.c_str(), body.data, body.size, &res) && res.ok())
        {
            jdoc doc;
            doc.init();
            if (doc.parse(res.text(), (int)res.body.size))
            {
                store::guard g;
                store::upsert_message(doc.root);
                store::bump_revision();
            }
            doc.free_doc();
        }
        else
        {
            record_api_error(tr("Файл не отправлен"), &res);
        }

        res.free_response();
        headers.free_buffer();
        body.free_buffer();
        payload.free_writer();

        for (unsigned int i = 0; i < j->files.count; i++)
            if (j->files[i].data) memfree(j->files[i].data);
        j->files.dispose();
        memfree(j);
    }

    void job_fetch_profile(void* user)
    {
        job_ids* j = (job_ids*)user;

        char path[256];
        if (j->b)
            cnprint(path, sizeof(path), "/users/%llu/profile?with_mutual_guilds=false&guild_id=%llu", j->a, j->b);
        else
            cnprint(path, sizeof(path), "/users/%llu/profile?with_mutual_guilds=false", j->a);

        http_response res;
        res.init();
        bool ok = api::call("GET", path, 0, &res) && res.ok();

        if (!ok)
        {
            // Profile endpoint is restricted for bot tokens; fall back.
            res.free_response();
            res.init();
            cnprint(path, sizeof(path), "/users/%llu", j->a);
            ok = api::call("GET", path, 0, &res) && res.ok();
        }

        if (ok)
        {
            jdoc doc;
            doc.init();
            if (doc.parse(res.text(), (int)res.body.size))
            {
                store::guard g;
                const jval* uobj = doc.root->has("user") ? doc.root->obj("user") : doc.root;
                duser* u = store::upsert_user(uobj);
                if (u)
                {
                    u->profile_loaded = true;
                    const char* bio = doc.root->str("bio", uobj->str("bio", 0));
                    if (bio) u->bio = store::intern(bio);
                }
                store::bump_revision();
            }
            doc.free_doc();
        }
        else
        {
            record_api_error(tr("Профиль недоступен"), &res);
        }

        res.free_response();
        memfree(j);
    }

    void job_fetch_guild_channels(void* user)
    {
        job_ids* j = (job_ids*)user;

        char path[128];
        cnprint(path, sizeof(path), "/guilds/%llu/channels", j->a);

        http_response res;
        res.init();
        if (api::call("GET", path, 0, &res) && res.ok())
        {
            jdoc doc;
            doc.init();
            if (doc.parse(res.text(), (int)res.body.size) && doc.root->type == JTYPE_ARR)
            {
                store::guard g;
                dguild* guild = store::find_guild(j->a);
                if (guild)
                {
                    guild->channels.clear_fast();
                    for (unsigned int i = 0; i < doc.root->count; i++)
                    {
                        dchannel* c = store::upsert_channel(doc.root->at(i), j->a);
                        if (c) guild->channels.push(c->id);
                    }
                    guild->loaded = true;
                }
                store::bump_revision();
            }
            doc.free_doc();
        }
        else
        {
            record_api_error(tr("Каналы сервера недоступны"), &res);
        }

        res.free_response();
        memfree(j);
    }

    void job_open_dm(void* user)
    {
        job_ids* j = (job_ids*)user;

        jwriter w;
        w.init();
        w.begin_obj();
        w.key("recipients");
        w.begin_arr();
        char id[32];
        cnprint(id, sizeof(id), "%llu", j->a);
        w.val_str(id);
        w.end_arr();
        w.end_obj();

        http_response res;
        res.init();
        if (api::call("POST", "/users/@me/channels", w.c_str(), &res) && res.ok())
        {
            jdoc doc;
            doc.init();
            if (doc.parse(res.text(), (int)res.body.size))
            {
                store::guard g;
                store::upsert_channel(doc.root, 0);
                store::bump_revision();
            }
            doc.free_doc();
        }
        else
        {
            record_api_error(tr("Не удалось открыть личные сообщения"), &res);
        }

        res.free_response();
        w.free_writer();
        memfree(j);
    }

    void job_simple_put_relationship(void* user)
    {
        job_ids* j = (job_ids*)user;

        char path[128];
        cnprint(path, sizeof(path), "/users/@me/relationships/%llu", j->a);

        http_response res;
        res.init();
        // The field is not optional any more: without it discord refuses to
        // accept a request from somebody it considers a stranger.
        if (!(api::call("PUT", path, "{\"confirm_stranger_request\":false}", &res,
                        "Friends") && res.ok()))
            record_api_error(tr("Заявка в друзья не принята"), &res);
        else
            api::clear_last_error();

        res.free_response();
        memfree(j);
    }

    void job_delete_relationship(void* user)
    {
        job_ids* j = (job_ids*)user;

        char path[128];
        cnprint(path, sizeof(path), "/users/@me/relationships/%llu", j->a);

        http_response res;
        res.init();
        if (api::call("DELETE", path, 0, &res, "Friends") && res.ok())
        {
            store::guard g;
            store::remove_relationship(j->a);
            store::bump_revision();
            api::clear_last_error();
        }
        else
        {
            record_api_error(tr("Не удалось изменить отношение"), &res);
        }

        res.free_response();
        memfree(j);
    }

    void job_block_user(void* user)
    {
        job_ids* j = (job_ids*)user;

        char path[128];
        cnprint(path, sizeof(path), "/users/@me/relationships/%llu", j->a);

        http_response res;
        res.init();
        if (!(api::call("PUT", path, "{\"type\":2}", &res, "Friends") && res.ok()))
            record_api_error(tr("Не удалось заблокировать"), &res);

        res.free_response();
        memfree(j);
    }

    void job_friend_by_name(void* user)
    {
        job_friend* j = (job_friend*)user;

        // Modern usernames have no discriminator; legacy ones use name#1234.
        const char* hash = 0;
        for (const char* p = j->name; *p; p++) if (*p == '#') hash = p;

        jwriter w;
        w.init();
        w.begin_obj();
        if (hash)
        {
            int name_len = (int)(hash - j->name);
            w.key("username");
            w.val_str(j->name, name_len);
            w.kv_str("discriminator", hash + 1);
        }
        else
        {
            w.kv_str("username", j->name);
            w.kv_null("discriminator");
        }

        // Carried only when the person has been through a captcha and handed
        // the token back. Solving it is theirs to do; this passes it on.
        if (j->captcha_key[0]) w.kv_str("captcha_key", j->captcha_key);
        if (j->captcha_rqtoken[0]) w.kv_str("captcha_rqtoken", j->captcha_rqtoken);

        w.end_obj();

        http_response res;
        res.init();
        // Logged in full because this one keeps coming back 400 with nothing
        // the error extractor can read, and the difference between what is
        // sent and what a working client sends is the whole question.
        log_line("friend: заявка на %s%s", j->name,
                 j->captcha_key[0] ? " (с токеном капчи)" : "");

        bool sent = api::call("POST", "/users/@me/relationships", w.c_str(), &res,
                              "Add Friend");

        if (!sent || !res.ok())
            log_line("friend: ответ %d, тело %.400s", res.status,
                     res.body.size ? res.text() : "(пусто)");

        // Written next to the log line as well, because the log is rewritten
        // at every start and the answer to this is wanted after the fact.
        if (!sent || !res.ok())
        {
            wchar_t path[MAX_PATH];
            if (ufile::app_path(L"friend_last_response.txt", path, MAX_PATH))
            {
                ubuffer dump;
                dump.init(1024);
                dump.append_fmt("request: %s\n", w.c_str());
                dump.append_fmt("status: %d\n", res.status);
                dump.append_str("body: ");
                if (res.body.size) dump.append(res.body.data, res.body.size);
                else dump.append_str("(пусто)");
                ufile::write_all(path, dump.data, dump.size);
                dump.free_buffer();
            }
        }

        if (sent && res.ok())
        {
            char msg[192];
            cnprint(msg, sizeof(msg), tr("Заявка отправлена: %s"), j->name);
            api::set_last_error(msg);
            api::clear_captcha();
        }
        else
        {
            // What the refusal is asking for, kept so the interface can put
            // the challenge in front of the person and take their answer.
            jdoc doc;
            doc.init();

            if (doc.parse(res.text(), (int)res.body.size))
            {
                const char* site = doc.root->str("captcha_sitekey", 0);
                const char* rq = doc.root->str("captcha_rqtoken", 0);

                ccfset(g_captcha_sitekey, 0, sizeof(g_captcha_sitekey));
                ccfset(g_captcha_rqtoken, 0, sizeof(g_captcha_rqtoken));

                if (site) ccstrncpy(g_captcha_sitekey, site, sizeof(g_captcha_sitekey) - 1);
                if (rq) ccstrncpy(g_captcha_rqtoken, rq, sizeof(g_captcha_rqtoken) - 1);
            }
            doc.free_doc();

            record_api_error(tr("Заявка не отправлена"), &res);
        }

        res.free_response();
        w.free_writer();
        memfree(j);
    }

    // Percent encoding, for the one query parameter this client sends that
    // can contain anything a person typed. Unreserved characters as RFC 3986
    // lists them go through untouched; everything else becomes %XX.
    void url_escape(const char* in, char* out, int cap)
    {
        static const char* HEX = "0123456789ABCDEF";
        int at = 0;

        for (const unsigned char* p = (const unsigned char*)in; *p && at < cap - 4; p++)
        {
            unsigned char c = *p;
            bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                         (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';

            if (plain)
            {
                out[at++] = (char)c;
            }
            else
            {
                out[at++] = '%';
                out[at++] = HEX[c >> 4];
                out[at++] = HEX[c & 0x0F];
            }
        }

        out[at] = 0;
    }

    // Using an invite, the way the official client does it: look the code up
    // first, report what came back, and only then join.
    //
    // One POST was what this did before, and discord answered it with a
    // CAPTCHA - which is what it does to a client that joins a server it has
    // never seen the preview of. The lookup is not decoration; it is the half
    // of the exchange that makes the join credible.
    void job_join_guild(void* user)
    {
        job_name* j = (job_name*)user;

        // What the person typed, kept whole. Discord reports it beside the
        // code and the two are not the same thing - one is a link.
        char typed[192];
        ccstrncpy(typed, j->full[0] ? j->full : j->text, sizeof(typed) - 1);

        char escaped[256];
        url_escape(typed, escaped, sizeof(escaped));

        char lookup[512];
        cnprint(lookup, sizeof(lookup),
                "/invites/%s?inputValue=%s&with_counts=true&with_expiration=true"
                "&with_permissions=true",
                j->text, escaped);

        science::invite_result found;
        ccfset(&found, 0, sizeof(found));
        found.code = j->text;
        found.input_value = typed;

        char context[256];
        cnprint(context, sizeof(context), "{\"location\":\"Join Guild\"}");

        http_response probe;
        probe.init();

        // The envelope alone goes out while the lookup is in flight, and the
        // event naming the code follows once it has answered - which is the
        // order in the capture, thirty milliseconds after the GET rather than
        // before it.
        science::invite_opened(j->text);

        if (api::call("GET", lookup, 0, &probe))
        {
            found.status_code = probe.status;
            found.resolved = probe.ok();

            jdoc doc;
            doc.init();
            if (probe.ok() && doc.parse(probe.text(), (int)probe.body.size))
            {
                const jval* guild = doc.root->obj("guild");
                const jval* channel = doc.root->obj("channel");
                const jval* inviter = doc.root->obj("inviter");

                found.guild_id = guild->sf("id");
                found.channel_id = channel->sf("id");
                found.channel_type = channel->i32("type", 0);
                found.inviter_id = inviter->sf("id");
                found.size_total = doc.root->i32("approximate_member_count", 0);
                found.size_online = doc.root->i32("approximate_presence_count", 0);

                // Discord names the channel and its type in the header of the
                // join itself, and refuses the ones that disagree with what
                // the preview said.
                if (found.guild_id && found.channel_id)
                    cnprint(context, sizeof(context),
                            "{\"location\":\"Join Guild\",\"location_guild_id\":\"%llu\","
                            "\"location_channel_id\":\"%llu\",\"location_channel_type\":%d}",
                            found.guild_id, found.channel_id, found.channel_type);
            }
            doc.free_doc();
        }

        probe.free_response();

        // A person reads the preview before pressing join. The capture shows
        // about seven tenths of a second between the two requests; going
        // straight from one to the other is a thing only a script does, and
        // this is the one request discord answers with a CAPTCHA.
        Sleep(700);

        char path[192];
        cnprint(path, sizeof(path), "/invites/%s", j->text);

        // The gateway session this join belongs to. The official client sends
        // it and nothing else in the body.
        jwriter body;
        body.init();
        body.begin_obj();
        body.kv_str("session_id", gateway::session_id());
        body.end_obj();

        http_response res;
        res.init();
        if (api::call("POST", path, body.buf.c_str(), &res, context) && res.ok())
        {
            jdoc doc;
            doc.init();
            if (doc.parse(res.text(), (int)res.body.size))
            {
                const jval* guild = doc.root->obj("guild");
                const char* name = guild->str("name", tr("сервер"));
                char msg[192];
                cnprint(msg, sizeof(msg), tr("Вы присоединились: %s"), name);
                api::set_last_error(msg);
            }
            doc.free_doc();
        }
        else
        {
            record_api_error(tr("Не удалось использовать приглашение"), &res);
        }

        // Reported after the join rather than before it, which is the order
        // the capture shows: the preview is described once it has been acted
        // on, not while it is still being looked at.
        science::invite_resolved(&found);

        res.free_response();
        body.free_writer();
        memfree(j);
    }

    // ---- invites and webhooks --------------------------------------------

    struct job_link
    {
        snowflake channel_id;
        int max_age;
        int max_uses;
        bool temporary;
        char name[96];
    };

    void job_create_invite(void* user)
    {
        job_link* j = (job_link*)user;

        jwriter w;
        w.init();
        w.begin_obj();
        w.kv_i64("max_age", j->max_age);
        w.kv_i64("max_uses", j->max_uses);
        w.kv_bool("temporary", j->temporary);

        // Without this discord hands back whatever equivalent invite already
        // exists, which means asking for a one-use link can return somebody
        // else's unlimited one made an hour ago.
        w.kv_bool("unique", true);
        w.end_obj();

        char path[96];
        cnprint(path, sizeof(path), "/channels/%llu/invites", j->channel_id);

        http_response res;
        res.init();

        if (api::call("POST", path, w.buf.c_str(), &res) && res.ok())
        {
            jdoc doc;
            doc.init();

            const char* code = 0;
            if (doc.parse(res.text(), (int)res.body.size)) code = doc.root->str("code", 0);

            if (code && code[0])
            {
                char url[128];
                cnprint(url, sizeof(url), "https://discord.gg/%s", code);
                api::set_last_link(url);
            }
            else
            {
                api::set_last_error(tr("Сервер не вернул код приглашения"));
            }
            doc.free_doc();
        }
        else
        {
            record_api_error(tr("Приглашение"), &res);
        }

        res.free_response();
        w.free_writer();
        memfree(j);
    }

    // Discord refuses a webhook whose name contains "discord" or "clyde", and
    // says so as a bare "Invalid Form Body" that names no field. Worth catching
    // here, where the reason can be given.
    bool name_has(const char* name, const char* banned)
    {
        for (const char* p = name; *p; p++)
        {
            const char* a = p;
            const char* b = banned;
            while (*a && *b && cctolower(*a) == cctolower(*b)) { a++; b++; }
            if (!*b) return true;
        }
        return false;
    }

    void job_create_webhook(void* user)
    {
        job_link* j = (job_link*)user;

        if (name_has(j->name, "discord") || name_has(j->name, "clyde"))
        {
            api::set_last_error(tr("Имя вебхука не может содержать \"discord\" или \"clyde\""));
            memfree(j);
            return;
        }

        jwriter w;
        w.init();
        w.begin_obj();
        w.kv_str("name", j->name[0] ? j->name : "webhook");
        w.end_obj();

        char path[96];
        cnprint(path, sizeof(path), "/channels/%llu/webhooks", j->channel_id);

        http_response res;
        res.init();

        if (api::call("POST", path, w.buf.c_str(), &res) && res.ok())
        {
            jdoc doc;
            doc.init();

            snowflake id = 0;
            const char* token = 0;
            const char* ready = 0;

            if (doc.parse(res.text(), (int)res.body.size))
            {
                // The answer carries the finished address as well as the parts
                // it is made of. Taking it whole means the client is not
                // guessing at a url format that is discord's to change.
                ready = doc.root->str("url", 0);
                id = doc.root->sf("id");
                token = doc.root->str("token", 0);
            }

            // The token is only ever shown at creation. Losing it means the
            // webhook is unusable and has to be made again, so it goes straight
            // where the caller can copy it.
            if (ready && ready[0])
            {
                api::set_last_link(ready);
            }
            else if (id && token && token[0])
            {
                char url[256];
                cnprint(url, sizeof(url), "https://discord.com/api/webhooks/%llu/%s", id, token);
                api::set_last_link(url);
            }
            else
            {
                api::set_last_error(tr("Сервер не вернул адрес вебхука"));
            }
            doc.free_doc();
        }
        else
        {
            record_api_error(tr("Вебхук"), &res);
        }

        res.free_response();
        w.free_writer();
        memfree(j);
    }

    // ---- audit log and bans ----------------------------------------------

    ulist<api::audit_row> g_audit;
    volatile long g_audit_busy = 0;
    volatile long g_audit_denied = 0;

    ulist<api::ban_row> g_bans;
    volatile long g_bans_busy = 0;
    volatile long g_bans_denied = 0;

    void job_fetch_audit(void* user)
    {
        job_ids* j = (job_ids*)user;

        char path[128];
        cnprint(path, sizeof(path), "/guilds/%llu/audit-logs?limit=50", j->a);

        http_response res;
        res.init();

        bool called = api::call("GET", path, 0, &res);
        InterlockedExchange(&g_audit_denied, (called && res.status == 403) ? 1 : 0);

        if (called && res.ok())
        {
            jdoc doc;
            doc.init();

            if (doc.parse(res.text(), (int)res.body.size))
            {
                // The names come with the log rather than being looked up one
                // by one afterwards - a person who did something may have left
                // the server since, and then nothing else knows who they were.
                {
                    store::guard g;
                    const jval* users = doc.root->arr("users");
                    for (unsigned int i = 0; i < users->count; i++)
                        store::upsert_user(users->at(i));
                }

                EnterCriticalSection(&g_err_lock);
                g_audit.clear();

                const jval* rows = doc.root->arr("audit_log_entries");
                for (unsigned int i = 0; i < rows->count; i++)
                {
                    const jval* v = rows->at(i);

                    api::audit_row row;
                    ccfset(&row, 0, sizeof(row));
                    row.id = v->sf("id");
                    row.action = v->i32("action_type", 0);
                    row.actor = v->sf("user_id");
                    row.target = v->sf("target_id");

                    const char* why = v->str("reason", 0);
                    if (why) ccstrncpy(row.reason, why, sizeof(row.reason) - 1);

                    if (row.id) g_audit.push(row);
                }
                LeaveCriticalSection(&g_err_lock);
            }
            doc.free_doc();
        }
        else if (!g_audit_denied)
        {
            record_api_error(tr("Журнал аудита"), &res);
        }

        res.free_response();
        InterlockedExchange(&g_audit_busy, 0);
        memfree(j);
    }

    void job_fetch_bans(void* user)
    {
        job_ids* j = (job_ids*)user;

        char path[128];
        cnprint(path, sizeof(path), "/guilds/%llu/bans?limit=1000", j->a);

        http_response res;
        res.init();

        bool called = api::call("GET", path, 0, &res);
        InterlockedExchange(&g_bans_denied, (called && res.status == 403) ? 1 : 0);

        if (called && res.ok())
        {
            jdoc doc;
            doc.init();

            if (doc.parse(res.text(), (int)res.body.size) && doc.root->type == JTYPE_ARR)
            {
                EnterCriticalSection(&g_err_lock);
                g_bans.clear();

                for (unsigned int i = 0; i < doc.root->count; i++)
                {
                    const jval* v = doc.root->at(i);
                    const jval* u = v->obj("user");

                    api::ban_row row;
                    ccfset(&row, 0, sizeof(row));
                    row.user_id = u->sf("id");

                    const char* name = u->str("global_name", 0);
                    if (!name) name = u->str("username", 0);
                    ccstrncpy(row.name, name ? name : tr("неизвестно"), sizeof(row.name) - 1);

                    const char* why = v->str("reason", 0);
                    ccstrncpy(row.reason, why ? why : "", sizeof(row.reason) - 1);

                    if (row.user_id) g_bans.push(row);
                }
                LeaveCriticalSection(&g_err_lock);
            }
            doc.free_doc();
        }
        else if (!g_bans_denied)
        {
            record_api_error(tr("Список банов"), &res);
        }

        res.free_response();
        InterlockedExchange(&g_bans_busy, 0);
        memfree(j);
    }

    void job_unban(void* user)
    {
        job_ids* j = (job_ids*)user;

        snowflake guild_id = j->a;
        char path[128];
        cnprint(path, sizeof(path), "/guilds/%llu/bans/%llu", j->a, j->b);
        memfree(j);

        http_response res;
        res.init();

        bool ok = api::call("DELETE", path, 0, &res) && res.ok();
        if (!ok) record_api_error(tr("Разбан"), &res);
        res.free_response();

        // Nothing announces a lifted ban, so the list is asked for again.
        if (ok) api::fetch_bans(guild_id);
    }

    // ---- handing the server over -----------------------------------------

    volatile long g_ownership_code_sent = 0;

    // When it was mailed. The pincode does not last, and discord answers an
    // expired one with the same "incorrect code" it gives a mistyped one, so
    // the only way anybody can tell the two apart is by being told how old
    // the code in front of them is. Written and read as one aligned 64-bit
    // value, which needs no lock on the only platform this builds for.
    volatile unsigned long long g_ownership_code_at = 0;

    struct job_transfer
    {
        snowflake guild_id;
        snowflake user_id;
        char code[32];
    };

    void job_ownership_code(void* user)
    {
        job_transfer* j = (job_transfer*)user;

        char path[128];
        cnprint(path, sizeof(path), "/guilds/%llu/pincode", j->guild_id);

        // No body at all. Discord answers 204 and puts a six digit code in the
        // owner's mail; nothing about it comes back over the wire.
        http_response res;
        res.init();

        bool ok = api::call("PUT", path, 0, &res) && res.ok();
        if (ok)
        {
            g_ownership_code_at = unix_now_ms();
            InterlockedExchange(&g_ownership_code_sent, 1);
        }
        else    record_api_error(tr("Код не отправлен"), &res);

        // Logged because each one invalidates the code before it: two of these
        // close together explain an "incorrect code" that is nobody's typo.
        log_line("ownership: PUT /guilds/%llu/pincode -> %d", j->guild_id, res.status);

        science::transfer_ownership_code_sent(j->guild_id, res.status);

        res.free_response();
        memfree(j);
    }

    void job_transfer_ownership(void* user)
    {
        job_transfer* j = (job_transfer*)user;

        jwriter w;
        w.init();
        w.begin_obj();
        w.kv_snowflake("owner_id", j->user_id);
        w.kv_str("code", j->code);
        w.end_obj();

        char path[96];
        cnprint(path, sizeof(path), "/guilds/%llu", j->guild_id);

        http_response res;
        res.init();

        if (api::call("PATCH", path, w.buf.c_str(), &res) && res.ok())
        {
            InterlockedExchange(&g_ownership_code_sent, 0);
            api::set_last_error(tr("Сервер передан"));
        }
        else
        {
            record_api_error(tr("Не удалось передать сервер"), &res);
        }

        science::transfer_ownership_done(j->guild_id, res.status);

        // The code itself stays out of the log - it is single use and there is
        // nothing to learn from its value that its length does not say. What is
        // worth keeping is discord's answer: a refusal here says only "Invalid
        // Form Body" on screen unless the nested part came through.
        log_line("ownership: PATCH /guilds/%llu owner=%llu code=%d симв -> %d %.300s",
                 j->guild_id, j->user_id, (int)ccslenf(j->code), res.status,
                 res.body.size ? res.text() : "");

        res.free_response();
        w.free_writer();
        memfree(j);
    }

    job_transfer* make_transfer(snowflake guild_id, snowflake user_id, const char* code)
    {
        job_transfer* j = (job_transfer*)memalloc(sizeof(job_transfer));
        if (!j) return 0;

        ccfset(j, 0, sizeof(*j));
        j->guild_id = guild_id;
        j->user_id = user_id;
        if (code) ccstrncpy(j->code, code, sizeof(j->code) - 1);
        return j;
    }

    // ---- moderation ------------------------------------------------------
    //
    // Kicking somebody out of voice, silencing them for the whole server and
    // timing them out are all one endpoint - PATCH on the member - differing
    // only in which field the body carries. Banning is its own, and is here
    // because it belongs to the same menu.

    struct job_moderate
    {
        snowflake guild_id;
        snowflake user_id;
        int number;              // minutes, or seconds of messages to sweep
        char field[48];
        char value[48];          // written raw, so null and true are possible
    };

    void job_patch_member(void* user)
    {
        job_moderate* j = (job_moderate*)user;

        jwriter w;
        w.init();
        w.begin_obj();
        w.kv_raw(j->field, j->value);
        w.end_obj();

        char path[128];
        cnprint(path, sizeof(path), "/guilds/%llu/members/%llu", j->guild_id, j->user_id);

        http_response res;
        res.init();

        if (!api::call("PATCH", path, w.buf.c_str(), &res) || !res.ok())
            record_api_error(tr("Модерация"), &res);

        res.free_response();
        w.free_writer();
        memfree(j);
    }

    void job_ban_member(void* user)
    {
        job_moderate* j = (job_moderate*)user;

        jwriter w;
        w.init();
        w.begin_obj();
        w.kv_i64("delete_message_seconds", j->number);
        w.end_obj();

        char path[128];
        cnprint(path, sizeof(path), "/guilds/%llu/bans/%llu", j->guild_id, j->user_id);

        snowflake guild_id = j->guild_id;

        http_response res;
        res.init();

        bool ok = api::call("PUT", path, w.buf.c_str(), &res) && res.ok();
        if (!ok) record_api_error(tr("Бан"), &res);

        res.free_response();
        w.free_writer();
        memfree(j);

        // A ban arrives as nothing on the gateway, so the list is re-read for
        // whoever has the ban window open.
        if (ok) api::fetch_bans(guild_id);
    }

    job_moderate* make_moderate_job(snowflake guild_id, snowflake user_id)
    {
        job_moderate* j = (job_moderate*)memalloc(sizeof(job_moderate));
        if (j)
        {
            ccfset(j, 0, sizeof(*j));
            j->guild_id = guild_id;
            j->user_id = user_id;
        }
        return j;
    }

    // ---- the invites that already exist ----------------------------------

    ulist<api::invite_row> g_invites;
    volatile long g_invites_busy = 0;
    volatile long g_invites_denied = 0;

    void job_fetch_invites(void* user)
    {
        job_ids* j = (job_ids*)user;

        char path[96];
        cnprint(path, sizeof(path), "/guilds/%llu/invites", j->a);

        http_response res;
        res.init();

        bool called = api::call("GET", path, 0, &res);

        // Told apart deliberately: no invites and no permission to look both
        // show an empty list, and only one of them is worth explaining.
        InterlockedExchange(&g_invites_denied, (called && res.status == 403) ? 1 : 0);

        if (called && res.ok())
        {
            jdoc doc;
            doc.init();

            if (doc.parse(res.text(), (int)res.body.size) && doc.root->type == JTYPE_ARR)
            {
                EnterCriticalSection(&g_err_lock);
                g_invites.clear();

                for (unsigned int i = 0; i < doc.root->count; i++)
                {
                    const jval* v = doc.root->at(i);

                    api::invite_row row;
                    ccfset(&row, 0, sizeof(row));

                    const char* code = v->str("code", 0);
                    if (!code || !code[0]) continue;
                    ccstrncpy(row.code, code, sizeof(row.code) - 1);

                    row.channel_id = v->obj("channel")->sf("id");
                    row.uses = v->i32("uses", 0);
                    row.max_uses = v->i32("max_uses", 0);
                    row.max_age = v->i32("max_age", 0);
                    row.temporary = v->boolean("temporary", false);

                    const char* who = v->obj("inviter")->str("global_name", 0);
                    if (!who) who = v->obj("inviter")->str("username", 0);
                    ccstrncpy(row.inviter, who ? who : "", sizeof(row.inviter) - 1);

                    g_invites.push(row);
                }
                LeaveCriticalSection(&g_err_lock);
            }
            doc.free_doc();
        }
        else if (!g_invites_denied)
        {
            record_api_error(tr("Список приглашений"), &res);
        }

        res.free_response();
        InterlockedExchange(&g_invites_busy, 0);
        memfree(j);
    }

    struct job_code
    {
        snowflake guild_id;
        char code[16];
    };

    void job_revoke_invite(void* user)
    {
        job_code* j = (job_code*)user;

        snowflake guild_id = j->guild_id;
        char path[64];
        cnprint(path, sizeof(path), "/invites/%s", j->code);
        memfree(j);

        http_response res;
        res.init();

        bool ok = api::call("DELETE", path, 0, &res) && res.ok();
        if (!ok) record_api_error(tr("Отзыв приглашения"), &res);
        res.free_response();

        if (ok) api::fetch_invites(guild_id);
    }

    // ---- the webhooks that already exist ---------------------------------

    ulist<api::webhook_row> g_webhooks;
    volatile long g_webhooks_busy = 0;

    void job_fetch_webhooks(void* user)
    {
        job_ids* j = (job_ids*)user;

        char path[96];
        cnprint(path, sizeof(path), "/guilds/%llu/webhooks", j->a);

        http_response res;
        res.init();

        if (api::call("GET", path, 0, &res) && res.ok())
        {
            jdoc doc;
            doc.init();

            if (doc.parse(res.text(), (int)res.body.size) && doc.root->type == JTYPE_ARR)
            {
                EnterCriticalSection(&g_err_lock);
                g_webhooks.clear();

                for (unsigned int i = 0; i < doc.root->count; i++)
                {
                    const jval* h = doc.root->at(i);

                    api::webhook_row row;
                    ccfset(&row, 0, sizeof(row));
                    row.id = h->sf("id");
                    row.channel_id = h->sf("channel_id");

                    const char* name = h->str("name", 0);
                    ccstrncpy(row.name, name ? name : tr("без имени"), sizeof(row.name) - 1);

                    if (row.id) g_webhooks.push(row);
                }
                LeaveCriticalSection(&g_err_lock);
            }
            doc.free_doc();
        }
        else
        {
            record_api_error(tr("Список вебхуков"), &res);
        }

        res.free_response();
        InterlockedExchange(&g_webhooks_busy, 0);
        memfree(j);
    }

    void job_delete_webhook(void* user)
    {
        job_ids* j = (job_ids*)user;

        // Read out before the job is freed, because the refresh below outlives
        // it and asking afterwards reads memory that has been handed back.
        snowflake webhook_id = j->a;
        snowflake guild_id = j->b;
        memfree(j);

        char path[96];
        cnprint(path, sizeof(path), "/webhooks/%llu", webhook_id);

        http_response res;
        res.init();

        bool ok = api::call("DELETE", path, 0, &res) && res.ok();
        if (!ok) record_api_error(tr("Удаление вебхука"), &res);
        res.free_response();

        // Nothing announces a deleted webhook, so the list is asked for again
        // rather than edited here: the server is the only thing that knows
        // what is left.
        if (ok) api::fetch_webhooks(guild_id);
    }

    // ---- channels ---------------------------------------------------------

    struct job_channel
    {
        snowflake guild_id;
        snowflake parent_id;
        int type;
        char name[96];
    };

    void job_create_channel(void* user)
    {
        job_channel* j = (job_channel*)user;

        jwriter w;
        w.init();
        w.begin_obj();
        w.kv_str("name", j->name);
        w.kv_i64("type", j->type);

        // A category cannot sit inside another, and sending a parent for one is
        // rejected rather than ignored.
        if (j->parent_id && j->type != 4) w.kv_snowflake("parent_id", j->parent_id);
        w.end_obj();

        char path[96];
        cnprint(path, sizeof(path), "/guilds/%llu/channels", j->guild_id);

        http_response res;
        res.init();

        // Nothing is written into the store: CHANNEL_CREATE arrives on the
        // gateway and is already handled there.
        if (!api::call("POST", path, w.buf.c_str(), &res) || !res.ok())
            record_api_error(tr("Создание канала"), &res);

        res.free_response();
        w.free_writer();
        memfree(j);
    }

    void job_delete_channel(void* user)
    {
        job_ids* j = (job_ids*)user;

        char path[96];
        cnprint(path, sizeof(path), "/channels/%llu", j->a);

        http_response res;
        res.init();

        // CHANNEL_DELETE arrives on the gateway, so nothing is removed here.
        if (!api::call("DELETE", path, 0, &res) || !res.ok())
            record_api_error(tr("Удаление канала"), &res);

        res.free_response();
        memfree(j);
    }

    struct job_order
    {
        snowflake guild_id;
        snowflake reparented;
        snowflake parent_id;
        int count;
        snowflake ids[256];
    };

    void job_reorder_channels(void* user)
    {
        job_order* j = (job_order*)user;

        jwriter w;
        w.init();
        w.begin_arr();

        for (int i = 0; i < j->count; i++)
        {
            w.begin_obj();
            w.kv_snowflake("id", j->ids[i]);
            w.kv_i64("position", i);

            // Only for the one that actually moved between categories. Sent for
            // every entry it would drag the whole list into one category.
            if (j->ids[i] == j->reparented)
            {
                if (j->parent_id) w.kv_snowflake("parent_id", j->parent_id);
                else              w.kv_null("parent_id");
            }
            w.end_obj();
        }
        w.end_arr();

        char path[96];
        cnprint(path, sizeof(path), "/guilds/%llu/channels", j->guild_id);

        http_response res;
        res.init();

        // CHANNEL_UPDATE arrives for each moved channel, so nothing is
        // reordered here.
        if (!api::call("PATCH", path, w.buf.c_str(), &res) || !res.ok())
            record_api_error(tr("Порядок каналов"), &res);

        res.free_response();
        w.free_writer();
        memfree(j);
    }

    void job_create_guild(void* user)
    {
        job_name* j = (job_name*)user;

        jwriter w;
        w.init();
        w.begin_obj();
        w.kv_str("name", j->text);
        w.kv_null("icon");
        w.end_obj();

        http_response res;
        res.init();

        // GUILD_CREATE follows on the gateway with the whole server in it.
        if (!api::call("POST", "/guilds", w.buf.c_str(), &res) || !res.ok())
            record_api_error(tr("Создание сервера"), &res);

        res.free_response();
        w.free_writer();
        memfree(j);
    }

    struct job_overwrite
    {
        snowflake channel_id;
        snowflake target_id;
        unsigned long long allow;
        unsigned long long deny;
        bool is_role;
        bool clearing;
    };

    void job_channel_overwrite(void* user)
    {
        job_overwrite* j = (job_overwrite*)user;

        char path[128];
        cnprint(path, sizeof(path), "/channels/%llu/permissions/%llu",
                j->channel_id, j->target_id);

        http_response res;
        res.init();

        if (j->clearing)
        {
            if (!api::call("DELETE", path, 0, &res) || !res.ok())
                record_api_error(tr("Сброс прав канала"), &res);
        }
        else
        {
            jwriter w;
            w.init();
            w.begin_obj();
            w.kv_i64("type", j->is_role ? 0 : 1);

            // Strings, for the same reason role permissions are: there are more
            // of these bits than a json number can carry without rounding the
            // top ones away.
            char bits[32];
            cnprint(bits, sizeof(bits), "%llu", j->allow);
            w.kv_str("allow", bits);
            cnprint(bits, sizeof(bits), "%llu", j->deny);
            w.kv_str("deny", bits);
            w.end_obj();

            if (!api::call("PUT", path, w.buf.c_str(), &res) || !res.ok())
                record_api_error(tr("Права канала"), &res);

            w.free_writer();
        }

        res.free_response();
        memfree(j);
    }

    // ---- roles -----------------------------------------------------------

    struct job_role
    {
        snowflake guild_id;
        snowflake role_id;
        snowflake user_id;
        unsigned long long permissions;
        unsigned int color;
        bool hoist;
        bool mentionable;
        char name[128];
    };

    job_role* make_role_job(snowflake guild_id)
    {
        job_role* j = (job_role*)memalloc(sizeof(job_role));
        if (j)
        {
            ccfset(j, 0, sizeof(*j));
            j->guild_id = guild_id;
        }
        return j;
    }

    // Adding and removing differ by one word, so they share everything else.
    void role_membership(void* user, const char* method)
    {
        job_role* j = (job_role*)user;

        char path[160];
        cnprint(path, sizeof(path), "/guilds/%llu/members/%llu/roles/%llu",
                j->guild_id, j->user_id, j->role_id);

        http_response res;
        res.init();

        if (!api::call(method, path, 0, &res) || !res.ok())
            api::set_last_error(tr("Не удалось изменить роли участника"));

        res.free_response();
        memfree(j);
    }

    void job_add_member_role(void* user) { role_membership(user, "PUT"); }
    void job_remove_member_role(void* user) { role_membership(user, "DELETE"); }

    void job_create_role(void* user)
    {
        job_role* j = (job_role*)user;

        // Named only. Everything else is left at the server's defaults, and
        // whoever created it edits it from the same panel a moment later - one
        // request that can fail instead of two.
        jwriter w;
        w.init();
        w.begin_obj();
        w.kv_str("name", j->name[0] ? j->name : tr("новая роль"));
        w.end_obj();

        char path[96];
        cnprint(path, sizeof(path), "/guilds/%llu/roles", j->guild_id);

        http_response res;
        res.init();

        if (!api::call("POST", path, w.buf.c_str(), &res) || !res.ok())
            api::set_last_error(tr("Не удалось создать роль"));

        res.free_response();
        w.free_writer();
        memfree(j);
    }

    void job_delete_role(void* user)
    {
        job_role* j = (job_role*)user;

        char path[128];
        cnprint(path, sizeof(path), "/guilds/%llu/roles/%llu", j->guild_id, j->role_id);

        http_response res;
        res.init();

        if (!api::call("DELETE", path, 0, &res) || !res.ok())
            api::set_last_error(tr("Не удалось удалить роль"));

        res.free_response();
        memfree(j);
    }

    void job_edit_role(void* user)
    {
        job_role* j = (job_role*)user;

        jwriter w;
        w.init();
        w.begin_obj();
        w.kv_str("name", j->name);

        // Permissions travel as a decimal string, not a number: there are more
        // than fifty three of them now, and a json number cannot carry the top
        // ones without rounding them away.
        {
            char bits[32];
            cnprint(bits, sizeof(bits), "%llu", j->permissions);
            w.kv_str("permissions", bits);
        }

        w.kv_i64("color", (long long)j->color);
        w.kv_bool("hoist", j->hoist);
        w.kv_bool("mentionable", j->mentionable);
        w.end_obj();

        char path[128];
        cnprint(path, sizeof(path), "/guilds/%llu/roles/%llu", j->guild_id, j->role_id);

        http_response res;
        res.init();

        if (!api::call("PATCH", path, w.buf.c_str(), &res) || !res.ok())
            api::set_last_error(tr("Не удалось изменить роль"));

        res.free_response();
        w.free_writer();
        memfree(j);
    }

    void job_leave_guild(void* user)
    {
        job_ids* j = (job_ids*)user;

        char path[128];
        cnprint(path, sizeof(path), "/users/@me/guilds/%llu", j->a);

        http_response res;
        res.init();
        if (api::call("DELETE", path, "{\"lurking\":false}", &res) && res.ok())
        {
            store::guard g;
            store::remove_guild(j->a);
        }
        else
        {
            record_api_error(tr("Не удалось покинуть сервер"), &res);
        }

        res.free_response();
        memfree(j);
    }

    void job_delete_guild(void* user)
    {
        job_ids* j = (job_ids*)user;

        char path[128];
        cnprint(path, sizeof(path), "/guilds/%llu/delete", j->a);

        // POST with no body at all, and nothing comes back but a 204. Notably
        // there is no science event for this one - the real client sends none,
        // and inventing one would be the only packet here that discord has
        // never seen.
        http_response res;
        res.init();

        if (api::call("POST", path, 0, &res) && res.ok())
        {
            store::guard g;
            store::remove_guild(j->a);
        }
        else
        {
            record_api_error(tr("Не удалось удалить сервер"), &res);
        }

        res.free_response();
        memfree(j);
    }

    void ack_request(void* user)
    {
        job_ids* j = (job_ids*)user;

        char path[192];
        cnprint(path, sizeof(path), "/channels/%llu/messages/%llu/ack", j->a, j->b);

        http_response res;
        res.init();
        api::call("POST", path, "{\"token\":null}", &res);
        res.free_response();
        memfree(j);
    }

    void job_typing(void* user)
    {
        job_ids* j = (job_ids*)user;

        char path[128];
        cnprint(path, sizeof(path), "/channels/%llu/typing", j->a);

        http_response res;
        res.init();
        api::call("POST", path, "", &res);
        res.free_response();
        memfree(j);
    }

    void job_delete_message(void* user)
    {
        job_ids* j = (job_ids*)user;

        char path[192];
        cnprint(path, sizeof(path), "/channels/%llu/messages/%llu", j->a, j->b);

        http_response res;
        res.init();
        if (api::call("DELETE", path, 0, &res) && res.ok())
        {
            store::guard g;
            store::remove_message(j->a, j->b);
            store::bump_revision();
        }
        else
        {
            record_api_error(tr("Сообщение не удалено"), &res);
        }

        res.free_response();
        memfree(j);
    }

    void job_ring(void* user)
    {
        job_ids* j = (job_ids*)user;

        char path[128];
        cnprint(path, sizeof(path), "/channels/%llu/call/ring", j->a);

        http_response res;
        res.init();
        if (!(api::call("POST", path, "{\"recipients\":null}", &res) && res.ok()))
            record_api_error(tr("Не удалось позвонить"), &res);

        res.free_response();
        memfree(j);
    }

    job_ids* make_ids(snowflake a, snowflake b)
    {
        job_ids* j = (job_ids*)memalloc(sizeof(job_ids));
        if (j) { j->a = a; j->b = b; }
        return j;
    }

    job_name* make_name(const char* text)
    {
        job_name* j = (job_name*)memalloc(sizeof(job_name));
        if (j)
        {
            ccfset(j, 0, sizeof(job_name));
            ccstrncpy(j->text, text, sizeof(j->text) - 1);
        }
        return j;
    }
}

void api::fetch_messages(snowflake channel_id, snowflake before_id)
{
    // Nothing to ask and nobody to ask. Marked as loaded rather than left
    // pending, so the view settles on whatever the archive gave it instead of
    // spinning forever - and, more importantly, is never marked failed, which
    // is a state the chat view reads as "do not bother with this channel".
    if (offline::active())
    {
        store::guard g;
        dchannel* ch = store::find_channel(channel_id);
        if (ch)
        {
            ch->history_loading = false;
            ch->history_failed = false;
            ch->history_loaded = true;
            ch->history_exhausted = true;
        }
        return;
    }

    {
        store::guard g;
        dchannel* ch = store::find_channel(channel_id);
        if (!ch || ch->history_loading) return;
        ch->history_loading = true;
    }
    job_ids* j = make_ids(channel_id, before_id);
    if (j) jobs::post(job_fetch_messages, j);
}

void api::fetch_sessions()
{
    if (offline::active())
    {
        store::guard g;
        store::set_sessions_failed();
        return;
    }

    {
        store::guard g;
        if (store::sessions_state() == 1) return;   // already in flight
        store::set_sessions_loading();
    }

    jobs::post(job_fetch_sessions, 0);
}

void api::send_message(snowflake channel_id, const char* content, snowflake reply_to)
{
    job_text* j = (job_text*)memalloc(sizeof(job_text));
    if (!j) return;
    ccfset(j, 0, sizeof(job_text));
    j->channel_id = channel_id;
    j->reply_to = reply_to;
    ccstrncpy(j->text, content, sizeof(j->text) - 1);
    jobs::post(job_send_message, j);
}

void api::send_message_with_files(snowflake channel_id, const char* content, ulist<upload_file>* files)
{
    job_upload* j = (job_upload*)memalloc(sizeof(job_upload));
    if (!j) return;
    ccfset(j, 0, sizeof(job_upload));
    j->channel_id = channel_id;
    if (content) ccstrncpy(j->text, content, sizeof(j->text) - 1);
    j->files = *files;

    // Ownership moved into the job.
    files->listPTR = 0;
    files->count = 0;
    files->reservedCount = 0;

    jobs::post(job_send_upload, j);
}

void api::fetch_user_profile(snowflake user_id, snowflake guild_id)
{
    job_ids* j = make_ids(user_id, guild_id);
    if (j) jobs::post(job_fetch_profile, j);
}

void api::fetch_guild_channels(snowflake guild_id)
{
    job_ids* j = make_ids(guild_id, 0);
    if (j) jobs::post(job_fetch_guild_channels, j);
}

void api::open_dm(snowflake user_id)
{
    job_ids* j = make_ids(user_id, 0);
    if (j) jobs::post(job_open_dm, j);
}

namespace
{
    // Two short strings, for the settings and profile calls. Named apart from
    // the message job, which already owns "job_text".
    struct job_fields
    {
        char a[512];
        char b[1024];
    };

    void job_update_settings(void* user)
    {
        job_fields* j = (job_fields*)user;

        jwriter w;
        w.init();
        w.begin_obj();
        w.kv_str("status", j->a);
        w.end_obj();

        http_response res;
        res.init();
        if (!api::call("PATCH", "/users/@me/settings", w.buf.c_str(), &res) || !res.ok())
            record_api_error(tr("Не удалось сменить статус"), &res);

        res.free_response();
        w.free_writer();
        memfree(j);
    }

    void job_update_profile(void* user)
    {
        job_fields* j = (job_fields*)user;

        // Two fields, two endpoints. The display name belongs to the account
        // and goes to /users/@me; the biography belongs to the profile and goes
        // to /users/@me/profile. Sending the name to the profile endpoint - as
        // this did - is accepted with a 200 and quietly ignored, which is
        // exactly what "renaming does nothing" looked like.
        bool ok = true;

        if (j->a[0])
        {
            jwriter w;
            w.init();
            w.begin_obj();
            w.kv_str("global_name", j->a);
            w.end_obj();

            http_response res;
            res.init();

            if (api::call("PATCH", "/users/@me", w.buf.c_str(), &res) && res.ok())
            {
                jdoc doc;
                doc.init();
                if (doc.parse(res.text(), (int)res.body.size))
                {
                    store::guard g;
                    store::upsert_user(doc.r());
                    store::bump_revision();
                }
                doc.free_doc();
            }
            else
            {
                record_api_error(tr("Не удалось сменить имя"), &res);
                ok = false;
            }

            res.free_response();
            w.free_writer();
        }

        if (ok && j->b[0])
        {
            jwriter w;
            w.init();
            w.begin_obj();
            w.kv_str("bio", j->b);
            w.end_obj();

            http_response res;
            res.init();

            if (!api::call("PATCH", "/users/@me/profile", w.buf.c_str(), &res) || !res.ok())
            {
                record_api_error(tr("Не удалось обновить профиль"), &res);
                ok = false;
            }

            res.free_response();
            w.free_writer();
        }

        if (ok) api::set_last_error(tr("Профиль обновлён"));
        memfree(j);
    }
}

namespace
{
    struct job_image
    {
        bool banner;
        wchar_t path[MAX_PATH];
    };

    struct job_guild_image
    {
        snowflake guild_id;
        wchar_t path[MAX_PATH];
    };

    struct job_name_id
    {
        snowflake id;
        char text[128];
    };

    // Guessed from the extension rather than sniffed: discord only cares that
    // the prefix matches the bytes, and every picture a person will pick here
    // names itself honestly.
    const char* mime_for(const wchar_t* path)
    {
        int last = -1;
        for (int i = 0; path[i]; i++) if (path[i] == L'.') last = i;
        if (last < 0) return "image/png";

        wchar_t ext[8];
        int n = 0;
        for (int i = last + 1; path[i] && n < 7; i++)
        {
            wchar_t ch = path[i];
            if (ch >= L'A' && ch <= L'Z') ch = (wchar_t)(ch - L'A' + L'a');
            ext[n++] = ch;
        }
        ext[n] = 0;

        if (n == 3 && ext[0] == L'g' && ext[1] == L'i' && ext[2] == L'f') return "image/gif";
        if (n == 3 && ext[0] == L'j' && ext[1] == L'p' && ext[2] == L'g') return "image/jpeg";
        if (n == 4 && ext[0] == L'j' && ext[1] == L'p' && ext[2] == L'e' && ext[3] == L'g') return "image/jpeg";
        if (n == 4 && ext[0] == L'w' && ext[1] == L'e' && ext[2] == L'b' && ext[3] == L'p') return "image/webp";
        return "image/png";
    }

    void job_guild_name(void* user)
    {
        job_name_id* j = (job_name_id*)user;

        jwriter w;
        w.init();
        w.begin_obj();
        w.kv_str("name", j->text);
        w.end_obj();

        char path[64];
        cnprint(path, sizeof(path), "/guilds/%llu", j->id);

        http_response res;
        res.init();

        // GUILD_UPDATE arrives on the gateway, so nothing is written here.
        if (!api::call("PATCH", path, w.buf.c_str(), &res) || !res.ok())
            record_api_error(tr("Не удалось переименовать сервер"), &res);

        res.free_response();
        w.free_writer();
        memfree(j);
    }

    void job_guild_icon(void* user)
    {
        job_guild_image* j = (job_guild_image*)user;

        char path[64];
        cnprint(path, sizeof(path), "/guilds/%llu", j->guild_id);

        jwriter w;
        w.init();
        w.begin_obj();

        ubuffer file;
        ubuffer base64;
        ubuffer uri;
        file.init();
        base64.init();
        uri.init();

        if (!j->path[0])
        {
            w.kv_null("icon");        // taking it off
        }
        else if (!ufile::read_all(j->path, &file) || !file.size)
        {
            api::set_last_error(tr("Файл не читается"));
            w.free_writer(); file.free_buffer(); base64.free_buffer(); uri.free_buffer();
            memfree(j);
            return;
        }
        else if (file.size > 10u * 1024u * 1024u)
        {
            api::set_last_error(tr("Файл больше 10 МБ"));
            w.free_writer(); file.free_buffer(); base64.free_buffer(); uri.free_buffer();
            memfree(j);
            return;
        }
        else
        {
            crypto::base64_encode(file.data, file.size, &base64);

            uri.append("data:", 5);
            const char* mime = mime_for(j->path);
            uri.append(mime, (unsigned int)ccslenf(mime));
            uri.append(";base64,", 8);
            uri.append(base64.data, base64.size);
            uri.c_str();

            w.kv_str("icon", (const char*)uri.data);
        }

        w.end_obj();

        http_response res;
        res.init();

        if (api::call("PATCH", path, w.buf.c_str(), &res) && res.ok())
            api::set_last_error(j->path[0] ? tr("Иконка сервера обновлена") : tr("Иконка сервера убрана"));
        else
            record_api_error(tr("Не удалось сменить иконку сервера"), &res);

        res.free_response();
        w.free_writer();
        uri.free_buffer();
        base64.free_buffer();
        file.free_buffer();
        memfree(j);
    }

    void job_update_image(void* user)
    {
        job_image* j = (job_image*)user;

        ubuffer file;
        file.init();

        if (!j->path[0])
        {
            // An empty path means take it off.
            jwriter w;
            w.init();
            w.begin_obj();
            w.kv_null(j->banner ? "banner" : "avatar");
            w.end_obj();

            http_response res;
            res.init();
            if (api::call("PATCH", "/users/@me", w.buf.c_str(), &res) && res.ok())
                api::set_last_error(j->banner ? tr("Баннер убран") : tr("Аватарка убрана"));
            else
                record_api_error(tr("Не удалось убрать картинку"), &res);

            res.free_response();
            w.free_writer();
            file.free_buffer();
            memfree(j);
            return;
        }

        if (!ufile::read_all(j->path, &file) || !file.size)
        {
            api::set_last_error(tr("Файл не читается"));
            file.free_buffer();
            memfree(j);
            return;
        }

        // Discord's own limit. Sending more just wastes the upload and comes
        // back rejected.
        if (file.size > 10u * 1024u * 1024u)
        {
            api::set_last_error(tr("Файл больше 10 МБ"));
            file.free_buffer();
            memfree(j);
            return;
        }

        ubuffer base64;
        base64.init(file.size * 4 / 3 + 64);
        crypto::base64_encode(file.data, file.size, &base64);

        ubuffer uri;
        uri.init(base64.size + 64);
        uri.append("data:", 5);
        {
            const char* mime = mime_for(j->path);
            uri.append(mime, (unsigned int)ccslenf(mime));
        }
        uri.append(";base64,", 8);
        uri.append(base64.data, base64.size);
        uri.append("\0", 1);   // the url is handed on as a C string

        jwriter w;
        w.init();
        w.begin_obj();
        w.kv_str(j->banner ? "banner" : "avatar", (const char*)uri.data);
        w.end_obj();

        http_response res;
        res.init();
        if (api::call("PATCH", "/users/@me", w.buf.c_str(), &res) && res.ok())
        {
            jdoc doc;
            doc.init();
            if (doc.parse(res.text(), (int)res.body.size))
            {
                store::guard g;
                store::upsert_user(doc.r());
                store::bump_revision();
            }
            doc.free_doc();
            api::set_last_error(j->banner ? tr("Баннер обновлён") : tr("Аватарка обновлена"));
        }
        else
        {
            record_api_error(tr("Не удалось загрузить картинку"), &res);
        }

        res.free_response();
        w.free_writer();
        uri.free_buffer();
        base64.free_buffer();
        file.free_buffer();
        memfree(j);
    }
}

void api::update_self_image(bool banner, const wchar_t* path)
{
    job_image* j = (job_image*)memalloc(sizeof(job_image));
    if (!j) return;
    ccfset(j, 0, sizeof(job_image));
    j->banner = banner;

    if (path)
        for (int i = 0; path[i] && i < MAX_PATH - 1; i++) j->path[i] = path[i];

    jobs::post(job_update_image, j);
}

static void do_fetch_counts(void* user)
{
    snowflake guild_id = *(snowflake*)user;
    memfree(user);

    char route[96];
    cnprint(route, sizeof(route), "/guilds/%llu?with_counts=true", guild_id);

    http_response res;
    res.init();

    if (api::call("GET", route, 0, &res) && res.ok())
    {
        jdoc doc;
        doc.init();
        if (doc.parse(res.text(), (int)res.body.size))
        {
            store::guard guard;
            dguild* g = store::find_guild(guild_id);
            if (g)
            {
                g->approx_members = doc.r()->i32("approximate_member_count", 0);
                g->approx_online = doc.r()->i32("approximate_presence_count", 0);
                g->counts_at_ms = GetTickCount64();
                store::bump_revision();
            }
        }
        doc.free_doc();
    }
    else
    {
        // Not worth an error in the corner of the screen: a server that will
        // not answer this just shows what was already known.
        store::guard guard;
        dguild* g = store::find_guild(guild_id);
        if (g) g->counts_at_ms = GetTickCount64();
    }

    {
        store::guard guard;
        dguild* g = store::find_guild(guild_id);
        if (g) g->counts_loading = false;
    }

    res.free_response();
}

void api::fetch_guild_counts(snowflake guild_id)
{
    if (!guild_id || offline::active()) return;

    {
        store::guard guard;
        dguild* g = store::find_guild(guild_id);
        if (!g || g->counts_loading) return;

        // Five minutes is plenty: this is a number on a tooltip, not a live
        // readout, and asking per hover would be rude to the server.
        if (g->counts_at_ms && GetTickCount64() - g->counts_at_ms < 5 * 60 * 1000ULL) return;
        g->counts_loading = true;
    }

    snowflake* id = (snowflake*)memalloc(sizeof(snowflake));
    if (!id) return;
    *id = guild_id;

    jobs::post(do_fetch_counts, id);
}

void api::update_status(const char* status)
{
    if (!status || !status[0]) return;

    job_fields* j = (job_fields*)memalloc(sizeof(job_fields));
    if (!j) return;
    ccfset(j, 0, sizeof(job_fields));
    ccstrncpy(j->a, status, sizeof(j->a) - 1);
    jobs::post(job_update_settings, j);
}

void api::update_self_profile(const char* global_name, const char* bio)
{
    job_fields* j = (job_fields*)memalloc(sizeof(job_fields));
    if (!j) return;
    ccfset(j, 0, sizeof(job_fields));
    if (global_name) ccstrncpy(j->a, global_name, sizeof(j->a) - 1);
    if (bio) ccstrncpy(j->b, bio, sizeof(j->b) - 1);
    jobs::post(job_update_profile, j);
}

void api::ack_message(snowflake channel_id, snowflake message_id)
{
    job_ids* j = make_ids(channel_id, message_id);
    if (j) jobs::post(ack_request, j);
}

void api::trigger_typing(snowflake channel_id)
{
    job_ids* j = make_ids(channel_id, 0);
    if (j) jobs::post(job_typing, j);
}

void api::delete_message(snowflake channel_id, snowflake message_id)
{
    job_ids* j = make_ids(channel_id, message_id);
    if (j) jobs::post(job_delete_message, j);
}

void api::ring_call(snowflake channel_id)
{
    job_ids* j = make_ids(channel_id, 0);
    if (j) jobs::post(job_ring, j);
}

void api::send_friend_request(const char* username, const char* captcha_key,
                              const char* captcha_rqtoken)
{
    if (!username || !username[0]) return;

    job_friend* j = (job_friend*)memalloc(sizeof(job_friend));
    if (!j) return;

    ccfset(j, 0, sizeof(*j));
    ccstrncpy(j->name, username, sizeof(j->name) - 1);
    if (captcha_key) ccstrncpy(j->captcha_key, captcha_key, sizeof(j->captcha_key) - 1);
    if (captcha_rqtoken)
        ccstrncpy(j->captcha_rqtoken, captcha_rqtoken, sizeof(j->captcha_rqtoken) - 1);

    jobs::post(job_friend_by_name, j);
}

const char* api::heartbeat_session_id() { return g_heartbeat_session; }
const char* api::launch_signature() { return g_launch_signature; }

const char* api::captcha_sitekey() { return g_captcha_sitekey; }
const char* api::captcha_rqtoken() { return g_captcha_rqtoken; }

void api::clear_captcha()
{
    ccfset(g_captcha_sitekey, 0, sizeof(g_captcha_sitekey));
    ccfset(g_captcha_rqtoken, 0, sizeof(g_captcha_rqtoken));
}

void api::accept_friend_request(snowflake user_id)
{
    job_ids* j = make_ids(user_id, 0);
    if (j) jobs::post(job_simple_put_relationship, j);
}

void api::remove_relationship(snowflake user_id)
{
    job_ids* j = make_ids(user_id, 0);
    if (j) jobs::post(job_delete_relationship, j);
}

void api::block_user(snowflake user_id)
{
    job_ids* j = make_ids(user_id, 0);
    if (j) jobs::post(job_block_user, j);
}

void api::join_guild_by_invite(const char* invite_code)
{
    // Accept a full url as well as a bare code. Both are kept: the code is
    // what the request is made with, the whole of it is what gets reported.
    const char* code = invite_code;
    for (const char* p = invite_code; *p; p++)
        if (*p == '/') code = p + 1;

    job_name* j = make_name(code);
    if (!j) return;

    ccstrncpy(j->full, invite_code, sizeof(j->full) - 1);
    jobs::post(job_join_guild, j);
}

void api::fetch_audit_log(snowflake guild_id)
{
    if (InterlockedCompareExchange(&g_audit_busy, 1, 0) != 0) return;

    job_ids* j = make_ids(guild_id, 0);
    if (j) jobs::post(job_fetch_audit, j);
    else InterlockedExchange(&g_audit_busy, 0);
}

int api::audit_log(audit_row* out, int cap)
{
    if (!g_ready || !out || cap <= 0) return 0;

    EnterCriticalSection(&g_err_lock);
    int total = (int)g_audit.count;
    int copied = total < cap ? total : cap;
    for (int i = 0; i < copied; i++) out[i] = g_audit[(unsigned int)i];
    LeaveCriticalSection(&g_err_lock);

    return copied;
}

bool api::audit_loading() { return g_audit_busy != 0; }
bool api::audit_forbidden() { return g_audit_denied != 0; }

void api::fetch_bans(snowflake guild_id)
{
    if (InterlockedCompareExchange(&g_bans_busy, 1, 0) != 0) return;

    job_ids* j = make_ids(guild_id, 0);
    if (j) jobs::post(job_fetch_bans, j);
    else InterlockedExchange(&g_bans_busy, 0);
}

int api::bans(ban_row* out, int cap)
{
    if (!g_ready || !out || cap <= 0) return 0;

    EnterCriticalSection(&g_err_lock);
    int total = (int)g_bans.count;
    int copied = total < cap ? total : cap;
    for (int i = 0; i < copied; i++) out[i] = g_bans[(unsigned int)i];
    LeaveCriticalSection(&g_err_lock);

    return copied;
}

bool api::bans_loading() { return g_bans_busy != 0; }
bool api::bans_forbidden() { return g_bans_denied != 0; }

void api::request_ownership_code(snowflake guild_id)
{
    job_transfer* j = make_transfer(guild_id, 0, 0);
    if (j) jobs::post(job_ownership_code, j);
}

void api::transfer_ownership(snowflake guild_id, snowflake user_id, const char* code)
{
    job_transfer* j = make_transfer(guild_id, user_id, code);
    if (j) jobs::post(job_transfer_ownership, j);
}

bool api::ownership_code_sent() { return g_ownership_code_sent != 0; }

unsigned long long api::ownership_code_age_ms()
{
    unsigned long long at = g_ownership_code_at;
    if (!at) return 0;

    unsigned long long now = unix_now_ms();
    return now > at ? now - at : 0;
}
void api::clear_ownership_state()
{
    g_ownership_code_at = 0;
    InterlockedExchange(&g_ownership_code_sent, 0);
}

void api::unban(snowflake guild_id, snowflake user_id)
{
    job_ids* j = make_ids(guild_id, user_id);
    if (j) jobs::post(job_unban, j);
}

void api::voice_kick(snowflake guild_id, snowflake user_id)
{
    job_moderate* j = make_moderate_job(guild_id, user_id);
    if (!j) return;

    ccstrncpy(j->field, "channel_id", sizeof(j->field) - 1);
    ccstrncpy(j->value, "null", sizeof(j->value) - 1);
    jobs::post(job_patch_member, j);
}

void api::voice_move(snowflake guild_id, snowflake user_id, snowflake channel_id)
{
    if (!channel_id) { api::voice_kick(guild_id, user_id); return; }

    job_moderate* j = make_moderate_job(guild_id, user_id);
    if (!j) return;

    ccstrncpy(j->field, "channel_id", sizeof(j->field) - 1);
    cnprint(j->value, sizeof(j->value), "\"%llu\"", channel_id);
    jobs::post(job_patch_member, j);
}

void api::set_server_mute(snowflake guild_id, snowflake user_id, bool muted)
{
    job_moderate* j = make_moderate_job(guild_id, user_id);
    if (!j) return;

    ccstrncpy(j->field, "mute", sizeof(j->field) - 1);
    ccstrncpy(j->value, muted ? "true" : "false", sizeof(j->value) - 1);
    jobs::post(job_patch_member, j);
}

void api::timeout_member(snowflake guild_id, snowflake user_id, int minutes)
{
    job_moderate* j = make_moderate_job(guild_id, user_id);
    if (!j) return;

    ccstrncpy(j->field, "communication_disabled_until", sizeof(j->field) - 1);

    if (minutes <= 0)
    {
        // Null lifts it. Sending a stamp in the past does not: discord keeps
        // the later of the two.
        ccstrncpy(j->value, "null", sizeof(j->value) - 1);
    }
    else
    {
        unsigned long long until_ms = unix_now_ms() + (unsigned long long)minutes * 60000ULL;

        char iso[48];
        unix_ms_to_iso(until_ms, iso, sizeof(iso));
        cnprint(j->value, sizeof(j->value), "\"%s\"", iso);
    }

    jobs::post(job_patch_member, j);
}

void api::ban_member(snowflake guild_id, snowflake user_id, int delete_message_seconds)
{
    job_moderate* j = make_moderate_job(guild_id, user_id);
    if (!j) return;

    j->number = delete_message_seconds;
    jobs::post(job_ban_member, j);
}

const char* api::audit_action_name(int action, char* scratch, int cap)
{
    switch (action)
    {
    case 1:  return tr("сервер изменён");
    case 10: return tr("канал создан");
    case 11: return tr("канал изменён");
    case 12: return tr("канал удалён");
    case 13: return tr("права канала выданы");
    case 14: return tr("права канала изменены");
    case 15: return tr("права канала сняты");
    case 20: return tr("участник выгнан");
    case 21: return tr("чистка участников");
    case 22: return tr("участник забанен");
    case 23: return tr("участник разбанен");
    case 24: return tr("участник изменён");
    case 25: return tr("роли участника изменены");
    case 26: return tr("участник перемещён");
    case 27: return tr("участник отключён");
    case 28: return tr("бот добавлен");
    case 30: return tr("роль создана");
    case 31: return tr("роль изменена");
    case 32: return tr("роль удалена");
    case 40: return tr("приглашение создано");
    case 41: return tr("приглашение изменено");
    case 42: return tr("приглашение отозвано");
    case 50: return tr("вебхук создан");
    case 51: return tr("вебхук изменён");
    case 52: return tr("вебхук удалён");
    case 60: return tr("эмодзи добавлено");
    case 61: return tr("эмодзи изменено");
    case 62: return tr("эмодзи удалено");
    case 72: return tr("сообщение удалено");
    case 73: return tr("сообщения вычищены");
    case 74: return tr("сообщение закреплено");
    case 75: return tr("сообщение откреплено");
    case 80: return tr("интеграция добавлена");
    case 81: return tr("интеграция изменена");
    case 82: return tr("интеграция удалена");
    case 83: return tr("событие создано");
    case 84: return tr("событие изменено");
    case 85: return tr("событие удалено");
    case 90: return tr("ветка создана");
    case 91: return tr("ветка изменена");
    case 92: return tr("ветка удалена");
    default:
        // Discord adds numbers faster than anybody can name them, and a row
        // that says who and when is still worth showing.
        cnprint(scratch, cap, tr("действие %d"), action);
        return scratch;
    }
}

void api::fetch_invites(snowflake guild_id)
{
    if (InterlockedCompareExchange(&g_invites_busy, 1, 0) != 0) return;

    job_ids* j = make_ids(guild_id, 0);
    if (j) jobs::post(job_fetch_invites, j);
    else InterlockedExchange(&g_invites_busy, 0);
}

void api::revoke_invite(const char* code, snowflake guild_id)
{
    if (!code || !code[0]) return;

    job_code* j = (job_code*)memalloc(sizeof(job_code));
    if (!j) return;

    ccfset(j, 0, sizeof(*j));
    j->guild_id = guild_id;
    ccstrncpy(j->code, code, sizeof(j->code) - 1);

    jobs::post(job_revoke_invite, j);
}

int api::invites(invite_row* out, int cap)
{
    if (!g_ready || !out || cap <= 0) return 0;

    EnterCriticalSection(&g_err_lock);
    int total = (int)g_invites.count;
    int copied = total < cap ? total : cap;
    for (int i = 0; i < copied; i++) out[i] = g_invites[(unsigned int)i];
    LeaveCriticalSection(&g_err_lock);

    return copied;
}

bool api::invites_loading() { return g_invites_busy != 0; }
bool api::invites_forbidden() { return g_invites_denied != 0; }

void api::fetch_webhooks(snowflake guild_id)
{
    if (InterlockedCompareExchange(&g_webhooks_busy, 1, 0) != 0) return;

    job_ids* j = make_ids(guild_id, 0);
    if (j) jobs::post(job_fetch_webhooks, j);
    else InterlockedExchange(&g_webhooks_busy, 0);
}

void api::delete_webhook(snowflake webhook_id, snowflake guild_id)
{
    job_ids* j = make_ids(webhook_id, guild_id);
    if (j) jobs::post(job_delete_webhook, j);
}

int api::webhooks(webhook_row* out, int cap)
{
    if (!g_ready || !out || cap <= 0) return 0;

    EnterCriticalSection(&g_err_lock);
    int total = (int)g_webhooks.count;
    int copied = total < cap ? total : cap;
    for (int i = 0; i < copied; i++) out[i] = g_webhooks[(unsigned int)i];
    LeaveCriticalSection(&g_err_lock);

    return copied;
}

bool api::webhooks_loading() { return g_webhooks_busy != 0; }

void api::create_channel(snowflake guild_id, const char* name, int type, snowflake parent_id)
{
    if (!name || !name[0]) return;

    job_channel* j = (job_channel*)memalloc(sizeof(job_channel));
    if (!j) return;

    ccfset(j, 0, sizeof(*j));
    j->guild_id = guild_id;
    j->parent_id = parent_id;
    j->type = type;
    ccstrncpy(j->name, name, sizeof(j->name) - 1);

    jobs::post(job_create_channel, j);
}

void api::reorder_channels(snowflake guild_id, const snowflake* ordered, int count,
                           snowflake reparented, snowflake parent_id)
{
    if (!ordered || count <= 0) return;
    if (count > 256) count = 256;

    job_order* j = (job_order*)memalloc(sizeof(job_order));
    if (!j) return;

    ccfset(j, 0, sizeof(*j));
    j->guild_id = guild_id;
    j->reparented = reparented;
    j->parent_id = parent_id;
    j->count = count;
    for (int i = 0; i < count; i++) j->ids[i] = ordered[i];

    jobs::post(job_reorder_channels, j);
}

void api::update_guild_name(snowflake guild_id, const char* name)
{
    if (!name || !name[0]) return;

    job_name_id* j = (job_name_id*)memalloc(sizeof(job_name_id));
    if (!j) return;

    ccfset(j, 0, sizeof(*j));
    j->id = guild_id;
    ccstrncpy(j->text, name, sizeof(j->text) - 1);

    jobs::post(job_guild_name, j);
}

void api::update_guild_icon(snowflake guild_id, const wchar_t* path)
{
    job_guild_image* j = (job_guild_image*)memalloc(sizeof(job_guild_image));
    if (!j) return;

    ccfset(j, 0, sizeof(*j));
    j->guild_id = guild_id;

    if (path)
    {
        int i = 0;
        while (path[i] && i < MAX_PATH - 1) { j->path[i] = path[i]; i++; }
        j->path[i] = 0;
    }

    jobs::post(job_guild_icon, j);
}

void api::create_guild(const char* name)
{
    job_name* j = make_name(name);
    if (j) jobs::post(job_create_guild, j);
}

void api::delete_channel(snowflake channel_id)
{
    job_ids* j = make_ids(channel_id, 0);
    if (j) jobs::post(job_delete_channel, j);
}

namespace
{
    job_overwrite* make_overwrite(snowflake channel_id, snowflake target_id)
    {
        job_overwrite* j = (job_overwrite*)memalloc(sizeof(job_overwrite));
        if (j)
        {
            ccfset(j, 0, sizeof(*j));
            j->channel_id = channel_id;
            j->target_id = target_id;
        }
        return j;
    }
}

void api::set_channel_overwrite(snowflake channel_id, snowflake target_id, bool is_role,
                                unsigned long long allow, unsigned long long deny)
{
    job_overwrite* j = make_overwrite(channel_id, target_id);
    if (!j) return;

    j->is_role = is_role;
    j->allow = allow;
    j->deny = deny;

    jobs::post(job_channel_overwrite, j);
}

void api::clear_channel_overwrite(snowflake channel_id, snowflake target_id)
{
    job_overwrite* j = make_overwrite(channel_id, target_id);
    if (!j) return;

    j->clearing = true;
    jobs::post(job_channel_overwrite, j);
}

void api::create_invite(snowflake channel_id, int max_age, int max_uses, bool temporary)
{
    job_link* j = (job_link*)memalloc(sizeof(job_link));
    if (!j) return;

    ccfset(j, 0, sizeof(*j));
    j->channel_id = channel_id;
    j->max_age = max_age;
    j->max_uses = max_uses;
    j->temporary = temporary;

    jobs::post(job_create_invite, j);
}

void api::create_webhook(snowflake channel_id, const char* name)
{
    job_link* j = (job_link*)memalloc(sizeof(job_link));
    if (!j) return;

    ccfset(j, 0, sizeof(*j));
    j->channel_id = channel_id;
    if (name) ccstrncpy(j->name, name, sizeof(j->name) - 1);

    jobs::post(job_create_webhook, j);
}

void api::add_member_role(snowflake guild_id, snowflake user_id, snowflake role_id)
{
    job_role* j = make_role_job(guild_id);
    if (!j) return;
    j->user_id = user_id;
    j->role_id = role_id;
    jobs::post(job_add_member_role, j);
}

void api::remove_member_role(snowflake guild_id, snowflake user_id, snowflake role_id)
{
    job_role* j = make_role_job(guild_id);
    if (!j) return;
    j->user_id = user_id;
    j->role_id = role_id;
    jobs::post(job_remove_member_role, j);
}

void api::create_role(snowflake guild_id, const char* name)
{
    job_role* j = make_role_job(guild_id);
    if (!j) return;
    if (name) ccstrncpy(j->name, name, sizeof(j->name) - 1);
    jobs::post(job_create_role, j);
}

void api::delete_role(snowflake guild_id, snowflake role_id)
{
    job_role* j = make_role_job(guild_id);
    if (!j) return;
    j->role_id = role_id;
    jobs::post(job_delete_role, j);
}

void api::edit_role(snowflake guild_id, snowflake role_id, const char* name,
                    unsigned long long permissions, unsigned int color,
                    bool hoist, bool mentionable)
{
    job_role* j = make_role_job(guild_id);
    if (!j) return;

    j->role_id = role_id;
    j->permissions = permissions;
    j->color = color;
    j->hoist = hoist;
    j->mentionable = mentionable;
    if (name) ccstrncpy(j->name, name, sizeof(j->name) - 1);

    jobs::post(job_edit_role, j);
}

void api::delete_guild(snowflake guild_id)
{
    job_ids* j = make_ids(guild_id, 0);
    if (j) jobs::post(job_delete_guild, j);
}

void api::leave_guild(snowflake guild_id)
{
    job_ids* j = make_ids(guild_id, 0);
    if (j) jobs::post(job_leave_guild, j);
}
