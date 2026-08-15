#include "pch.h"
#include "rest.h"
#include "store.h"
#include "archive.h"
#include "core/offline.h"
#include "core/log.h"
#include "core/crypto.h"
#include "system/io/ufile.h"
#include "net/json.h"

namespace
{
    const char* API_BASE = "https://discord.com/api/v9";

    char g_token[512];
    char g_super_properties[1024];
    char g_last_error[256];
    CRITICAL_SECTION g_err_lock;
    bool g_ready = false;

    void build_super_properties()
    {
        jwriter w;
        w.init();
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
        w.kv_i64("client_build_number", 9999);
        w.kv_null("client_event_source");
        w.end_obj();

        ubuffer b64;
        b64.init();
        crypto::base64_encode(w.buf.data, w.buf.size, &b64);
        ccstrncpy(g_super_properties, b64.c_str(), sizeof(g_super_properties) - 1);

        b64.free_buffer();
        w.free_writer();
    }

    void build_headers(ubuffer* out, const char* content_type)
    {
        out->clear();
        if (g_token[0])
        {
            out->append_str("Authorization: ");
            out->append_str(g_token);
            out->append_str("\r\n");
        }
        if (content_type)
        {
            out->append_str("Content-Type: ");
            out->append_str(content_type);
            out->append_str("\r\n");
        }
        out->append_str("X-Super-Properties: ");
        out->append_str(g_super_properties);
        out->append_str("\r\n");
        out->append_str("X-Discord-Locale: en-US\r\n");
        out->append_str("X-Debug-Options: bugReporterEnabled\r\n");
        out->append_str("Accept-Language: en-US,en;q=0.9\r\n");
        out->append_str("Origin: https://discord.com\r\n");
        out->append_str("Referer: https://discord.com/channels/@me\r\n");
        out->append_str("Sec-Fetch-Dest: empty\r\n");
        out->append_str("Sec-Fetch-Mode: cors\r\n");
        out->append_str("Sec-Fetch-Site: same-origin\r\n");
    }

    // Pulls the human readable part out of a discord error body.
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

        if (detail) cnprint(text, sizeof(text), "%s: %s", what, detail);
        else cnprint(text, sizeof(text), "%s: HTTP %d", what, res->status);

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

void api::set_token(const char* t)
{
    ccfset(g_token, 0, sizeof(g_token));
    if (t) ccstrncpy(g_token, t, sizeof(g_token) - 1);
}

const char* api::token() { return g_token; }
bool api::has_token() { return g_token[0] != 0; }

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

bool api::call_absolute(const char* method, const char* url, const char* json_body, http_response* out)
{
    ubuffer headers;
    headers.init();
    build_headers(&headers, json_body ? "application/json" : 0);

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

bool api::call(const char* method, const char* path, const char* json_body, http_response* out)
{
    char url[2048];
    cnprint(url, sizeof(url), "%s%s", API_BASE, path);
    if (out->status) offline::note_network_success();
    return call_absolute(method, url, json_body, out);
}

bool api::verify_token(const char* token_value, char* out_error, int error_cap)
{
    char saved[512];
    ccstrncpy(saved, g_token, sizeof(saved) - 1);
    saved[sizeof(saved) - 1] = 0;
    set_token(token_value);

    http_response res;
    res.init();
    bool ok = false;

    if (!call("GET", "/users/@me", 0, &res))
    {
        cnprint(out_error, error_cap, "Нет соединения с discord.com");
    }
    else if (res.status == 401)
    {
        cnprint(out_error, error_cap, "Токен отклонён (401)");
    }
    else if (!res.ok())
    {
        cnprint(out_error, error_cap, "Ошибка HTTP %d", res.status);
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
            cnprint(out_error, error_cap, "Неожиданный ответ сервера");
        }
        doc.free_doc();
    }

    res.free_response();
    if (!ok) set_token(saved);
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
            record_api_error("Не удалось загрузить историю", &res);
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
            record_api_error("Сообщение не отправлено", &res);
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
        build_headers(&headers, content_type);

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
            record_api_error("Файл не отправлен", &res);
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
            record_api_error("Профиль недоступен", &res);
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
            record_api_error("Каналы сервера недоступны", &res);
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
            record_api_error("Не удалось открыть личные сообщения", &res);
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
        if (!(api::call("PUT", path, "{}", &res) && res.ok()))
            record_api_error("Заявка в друзья не принята", &res);
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
        if (api::call("DELETE", path, 0, &res) && res.ok())
        {
            store::guard g;
            store::remove_relationship(j->a);
            store::bump_revision();
            api::clear_last_error();
        }
        else
        {
            record_api_error("Не удалось изменить отношение", &res);
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
        if (!(api::call("PUT", path, "{\"type\":2}", &res) && res.ok()))
            record_api_error("Не удалось заблокировать", &res);

        res.free_response();
        memfree(j);
    }

    void job_friend_by_name(void* user)
    {
        job_name* j = (job_name*)user;

        // Modern usernames have no discriminator; legacy ones use name#1234.
        const char* hash = 0;
        for (const char* p = j->text; *p; p++) if (*p == '#') hash = p;

        jwriter w;
        w.init();
        w.begin_obj();
        if (hash)
        {
            int name_len = (int)(hash - j->text);
            w.key("username");
            w.val_str(j->text, name_len);
            w.kv_str("discriminator", hash + 1);
        }
        else
        {
            w.kv_str("username", j->text);
            w.kv_null("discriminator");
        }
        w.end_obj();

        http_response res;
        res.init();
        if (api::call("POST", "/users/@me/relationships", w.c_str(), &res) && res.ok())
        {
            char msg[192];
            cnprint(msg, sizeof(msg), "Заявка отправлена: %s", j->text);
            api::set_last_error(msg);
        }
        else
        {
            record_api_error("Заявка не отправлена", &res);
        }

        res.free_response();
        w.free_writer();
        memfree(j);
    }

    void job_join_guild(void* user)
    {
        job_name* j = (job_name*)user;

        char path[192];
        cnprint(path, sizeof(path), "/invites/%s", j->text);

        http_response res;
        res.init();
        if (api::call("POST", path, "{}", &res) && res.ok())
        {
            jdoc doc;
            doc.init();
            if (doc.parse(res.text(), (int)res.body.size))
            {
                const jval* guild = doc.root->obj("guild");
                const char* name = guild->str("name", "сервер");
                char msg[192];
                cnprint(msg, sizeof(msg), "Вы присоединились: %s", name);
                api::set_last_error(msg);
            }
            doc.free_doc();
        }
        else
        {
            record_api_error("Не удалось использовать приглашение", &res);
        }

        res.free_response();
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
            record_api_error("Не удалось покинуть сервер", &res);
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
            record_api_error("Сообщение не удалено", &res);
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
            record_api_error("Не удалось позвонить", &res);

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
            record_api_error("Не удалось сменить статус", &res);

        res.free_response();
        w.free_writer();
        memfree(j);
    }

    void job_update_profile(void* user)
    {
        job_fields* j = (job_fields*)user;

        jwriter w;
        w.init();
        w.begin_obj();
        if (j->a[0]) w.kv_str("global_name", j->a);
        if (j->b[0]) w.kv_str("bio", j->b);
        w.end_obj();

        http_response res;
        res.init();
        if (api::call("PATCH", "/users/@me/profile", w.buf.c_str(), &res) && res.ok())
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
            api::set_last_error("Профиль обновлён");
        }
        else
        {
            record_api_error("Не удалось обновить профиль", &res);
        }

        res.free_response();
        w.free_writer();
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
                api::set_last_error(j->banner ? "Баннер убран" : "Аватарка убрана");
            else
                record_api_error("Не удалось убрать картинку", &res);

            res.free_response();
            w.free_writer();
            file.free_buffer();
            memfree(j);
            return;
        }

        if (!ufile::read_all(j->path, &file) || !file.size)
        {
            api::set_last_error("Файл не читается");
            file.free_buffer();
            memfree(j);
            return;
        }

        // Discord's own limit. Sending more just wastes the upload and comes
        // back rejected.
        if (file.size > 10u * 1024u * 1024u)
        {
            api::set_last_error("Файл больше 10 МБ");
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
            api::set_last_error(j->banner ? "Баннер обновлён" : "Аватарка обновлена");
        }
        else
        {
            record_api_error("Не удалось загрузить картинку", &res);
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

void api::send_friend_request(const char* username)
{
    job_name* j = make_name(username);
    if (j) jobs::post(job_friend_by_name, j);
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
    // Accept a full url as well as a bare code.
    const char* code = invite_code;
    for (const char* p = invite_code; *p; p++)
        if (*p == '/') code = p + 1;

    job_name* j = make_name(code);
    if (j) jobs::post(job_join_guild, j);
}

void api::leave_guild(snowflake guild_id)
{
    job_ids* j = make_ids(guild_id, 0);
    if (j) jobs::post(job_leave_guild, j);
}
