#include "pch.h"
#include "archive.h"
#include "store.h"
#include "core/log.h"
#include "core/storage.h"
#include "net/json.h"
#include "system/io/ufile.h"

namespace
{
    bool g_ready = false;
    snowflake g_self = 0;
    wchar_t g_dir[MAX_PATH];
    CRITICAL_SECTION g_lock;
    bool g_lock_ready = false;

    unsigned int g_total_messages = 0;

    // What has already been written this session, so a message that passes
    // through the client fifty times is appended once. The stamp is a cheap
    // hash of everything that can change about it.
    struct known
    {
        snowflake channel_id;
        snowflake message_id;
        unsigned int stamp;
    };

    ulist<known> g_known;
    ulist<snowflake> g_channels;

    void wappend(wchar_t* dst, int cap, const wchar_t* add)
    {
        int n = 0;
        while (n < cap - 1 && dst[n]) n++;
        for (int i = 0; add[i] && n < cap - 1; i++) dst[n++] = add[i];
        dst[n] = 0;
    }

    bool channel_path(snowflake channel_id, const wchar_t* extension,
                      wchar_t* out, int cap)
    {
        if (!g_ready) return false;

        out[0] = 0;
        wappend(out, cap, g_dir);

        char name[64];
        cnprint(name, sizeof(name), "\\%llu", channel_id);

        wchar_t wide[64];
        chartowcs(name, wide, 64);
        wappend(out, cap, wide);
        wappend(out, cap, extension);
        return true;
    }

    // Appending through CreateFile every time is what keeps this crash safe:
    // whatever reached the disk stays readable, and a half written last line is
    // simply skipped when it is read back.
    bool append(const wchar_t* path, const void* data, unsigned int size)
    {
        HANDLE h = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, 0,
                               OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
        if (h == INVALID_HANDLE_VALUE) return false;

        DWORD wrote = 0;
        bool ok = WriteFile(h, data, size, &wrote, 0) != 0 && wrote == size;
        CloseHandle(h);
        return ok;
    }

    unsigned int hash_text(const char* s, unsigned int seed)
    {
        unsigned int h = seed;
        for (int i = 0; s && s[i]; i++) h = h * 16777619u ^ (unsigned char)s[i];
        return h;
    }

    unsigned int message_stamp(const dmessage* m)
    {
        unsigned int h = 2166136261u;
        h = hash_text(m->content, h);
        h = hash_text(m->edited_timestamp, h);
        h = h * 16777619u ^ (unsigned int)m->attachments.count;
        h = h * 16777619u ^ (unsigned int)(m->pending ? 1 : 0);
        return h;
    }

    known* find_known(snowflake channel_id, snowflake message_id)
    {
        for (unsigned int i = 0; i < g_known.count; i++)
            if (g_known[i].message_id == message_id && g_known[i].channel_id == channel_id)
                return &g_known[i];
        return 0;
    }

    void note_channel(snowflake channel_id)
    {
        for (unsigned int i = 0; i < g_channels.count; i++)
            if (g_channels[i] == channel_id) return;
        g_channels.push(channel_id);
    }

    // The line written for one message. Only what the client can show is kept;
    // the rest of what discord sends would triple the size for nothing.
    void write_message(jwriter* w, const dmessage* m, bool deleted)
    {
        w->begin_obj();
        w->kv_snowflake("id", m->id);
        w->kv_snowflake("channel_id", m->channel_id);
        if (m->guild_id) w->kv_snowflake("guild_id", m->guild_id);
        w->kv_snowflake("author_id", m->author_id);
        if (m->referenced_id) w->kv_snowflake("referenced_id", m->referenced_id);
        w->kv_i64("type", m->type);
        if (m->content) w->kv_str("content", m->content);
        if (m->timestamp) w->kv_str("timestamp", m->timestamp);
        if (m->edited_timestamp) w->kv_str("edited_timestamp", m->edited_timestamp);
        if (deleted) w->kv_bool("deleted", true);

        // The author is written alongside the message rather than looked up
        // later: an export from a year ago has to name people even if the
        // client has never seen them since.
        {
            store::guard guard;
            duser* u = store::find_user(m->author_id);
            if (u)
            {
                w->key("author");
                w->begin_obj();
                w->kv_snowflake("id", u->id);
                if (u->username) w->kv_str("username", u->username);
                if (u->global_name) w->kv_str("global_name", u->global_name);
                if (u->avatar) w->kv_str("avatar", u->avatar);
                if (u->bot) w->kv_bool("bot", true);
                w->end_obj();
            }
        }

        if (m->attachments.count)
        {
            w->key("attachments");
            w->begin_arr();
            for (unsigned int i = 0; i < m->attachments.count; i++)
            {
                const dattachment* a = &m->attachments[i];
                w->begin_obj();
                w->kv_snowflake("id", a->id);
                if (a->filename) w->kv_str("filename", a->filename);
                if (a->url) w->kv_str("url", a->url);
                if (a->content_type) w->kv_str("content_type", a->content_type);
                w->kv_i64("size", (long long)a->size);
                w->kv_i64("width", a->width);
                w->kv_i64("height", a->height);
                w->end_obj();
            }
            w->end_arr();
        }

        w->end_obj();
    }

    void append_line(snowflake channel_id, jwriter* w)
    {
        w->buf.append("\n", 1);

        wchar_t path[MAX_PATH];
        if (channel_path(channel_id, L".jsonl", path, MAX_PATH))
        {
            append(path, w->buf.data, w->buf.size);
            note_channel(channel_id);
        }
    }
}

void archive::init(snowflake self_id)
{
    shutdown();

    if (!g_lock_ready)
    {
        InitializeCriticalSection(&g_lock);
        g_lock_ready = true;
    }

    g_self = self_id;
    g_known = ulist<known>();
    g_channels = ulist<snowflake>();
    g_total_messages = 0;

    wchar_t base[MAX_PATH];
    if (!ufile::app_path(L"archive", base, MAX_PATH)) return;
    CreateDirectoryW(base, 0);

    // Named after the account rather than by its id, so a folder full of chat
    // history does not announce whose it is to anybody browsing the disk.
    char tag[40];
    storage::account_tag(self_id, tag, sizeof(tag));

    wchar_t wtag[40];
    chartowcs(tag, wtag, 40);

    g_dir[0] = 0;
    wappend(g_dir, MAX_PATH, base);
    wappend(g_dir, MAX_PATH, L"\\");
    wappend(g_dir, MAX_PATH, wtag);
    CreateDirectoryW(g_dir, 0);

    g_ready = true;
    log_line("archive: открыт для аккаунта %llu", self_id);
}

void archive::shutdown()
{
    if (!g_ready) return;

    EnterCriticalSection(&g_lock);
    g_known.dispose();
    g_channels.dispose();
    g_ready = false;
    g_self = 0;
    LeaveCriticalSection(&g_lock);
}

bool archive::ready() { return g_ready; }

void archive::put(const dmessage* m)
{
    if (!g_ready || !m || !m->id || !m->channel_id) return;
    // A message that has not been accepted by the server yet has no permanent
    // identity, and saving it would leave a duplicate behind once it does.
    if (m->pending) return;

    EnterCriticalSection(&g_lock);

    unsigned int stamp = message_stamp(m);
    known* seen = find_known(m->channel_id, m->id);
    if (seen && seen->stamp == stamp)
    {
        LeaveCriticalSection(&g_lock);
        return;
    }

    jwriter w;
    w.init();
    write_message(&w, m, false);
    append_line(m->channel_id, &w);
    w.free_writer();

    if (seen)
    {
        seen->stamp = stamp;
    }
    else
    {
        known fresh;
        fresh.channel_id = m->channel_id;
        fresh.message_id = m->id;
        fresh.stamp = stamp;
        g_known.push(fresh);
        g_total_messages++;
    }

    LeaveCriticalSection(&g_lock);
}

void archive::put_json(const jval* v)
{
    if (!g_ready || !v) return;

    snowflake id = v->sf("id");
    snowflake channel_id = v->sf("channel_id");
    if (!id || !channel_id) return;

    store::guard guard;
    dchannel* c = store::find_channel(channel_id);
    dmessage* m = c ? store::find_message(c, id) : 0;
    if (m) put(m);
}

void archive::mark_deleted(snowflake channel_id, snowflake message_id)
{
    if (!g_ready || !channel_id || !message_id) return;

    // Only what was actually held can be marked: a deletion for something never
    // seen has nothing to attach to.
    dmessage copy;
    ccfset(&copy, 0, sizeof(copy));
    bool found = false;

    {
        store::guard guard;
        dchannel* c = store::find_channel(channel_id);
        dmessage* m = c ? store::find_message(c, message_id) : 0;
        if (m)
        {
            copy = *m;
            found = true;
        }
    }

    if (!found)
    {
        copy.id = message_id;
        copy.channel_id = channel_id;
    }

    EnterCriticalSection(&g_lock);

    jwriter w;
    w.init();
    write_message(&w, &copy, true);
    append_line(channel_id, &w);
    w.free_writer();

    known* seen = find_known(channel_id, message_id);
    if (seen) seen->stamp = 0xFFFFFFFFu;

    LeaveCriticalSection(&g_lock);
}

void archive::note_range(snowflake channel_id, snowflake from_id, snowflake to_id)
{
    if (!g_ready || !channel_id || !from_id || !to_id) return;
    if (from_id > to_id) { snowflake t = from_id; from_id = to_id; to_id = t; }

    char line[64];
    int n = cnprint(line, sizeof(line), "%llu %llu\n", from_id, to_id);

    wchar_t path[MAX_PATH];
    if (channel_path(channel_id, L".ranges", path, MAX_PATH))
        append(path, line, (unsigned int)n);
}

int archive::load_channel(snowflake channel_id)
{
    if (!g_ready) return 0;

    wchar_t path[MAX_PATH];
    if (!channel_path(channel_id, L".jsonl", path, MAX_PATH)) return 0;

    ubuffer blob;
    blob.init();
    if (!ufile::read_all(path, &blob)) { blob.free_buffer(); return 0; }

    const char* text = (const char*)blob.c_str();
    unsigned int size = blob.size;
    int added = 0;

    // The lines are walked oldest first, which means backwards through the
    // file: a warm-up pages from newest to oldest, so that is the order it
    // wrote them in.
    //
    // This is not cosmetic. The store keeps a channel's messages sorted by id
    // and finds the insertion point by scanning back from the end, so feeding
    // it newest first makes every insert walk the entire list. Four thousand
    // messages took five seconds of frozen interface that way, and a busy
    // channel would take a minute. Oldest first, every insert lands at the end
    // and the file loads as fast as it parses.
    ulist<unsigned int> starts;
    starts = ulist<unsigned int>();

    unsigned int scan = 0;
    while (scan < size)
    {
        starts.push(scan);
        while (scan < size && text[scan] != '\n') scan++;
        scan++;
    }

    for (unsigned int line = starts.count; line > 0; line--)
    {
        unsigned int at = starts[line - 1];
        unsigned int end = at;
        while (end < size && text[end] != '\n') end++;

        unsigned int len = end - at;
        if (len > 2)
        {
            jdoc doc;
            doc.init();
            if (doc.parse(text + at, (int)len) && doc.r()->type == JTYPE_OBJ)
            {
                const jval* v = doc.r();

                // The author travelled with the message so a name survives even
                // when the person has not been seen since.
                {
                    store::guard guard;
                    const jval* author = v->obj("author");
                    if (author && author->type == JTYPE_OBJ) store::upsert_user(author);

                    dmessage* m = store::upsert_message(v);
                    if (m)
                    {
                        // Monotonic on purpose. The message's own line never
                        // carries the flag, only the later deletion marker
                        // does, and the marker is read FIRST (it sits closer
                        // to the end of the file). A plain assignment here
                        // let the unmarked original undo the deletion on
                        // every restart.
                        if (v->boolean("deleted", false)) m->deleted = true;
                        added++;
                    }
                }
            }
            doc.free_doc();
        }
    }

    starts.dispose();
    blob.free_buffer();

    // Logged either way. A silent zero looks exactly like a channel that was
    // never archived, and telling the two apart used to need a disk browser.
    log_line("archive: канал %llu, восстановлено %d сообщений из %u байт",
             channel_id, added, size);
    return added;
}

unsigned int archive::channel_count(snowflake channel_id)
{
    unsigned int n = 0;
    for (unsigned int i = 0; i < g_known.count; i++)
        if (g_known[i].channel_id == channel_id) n++;
    return n;
}

unsigned int archive::total_messages() { return g_total_messages; }
unsigned int archive::total_channels() { return g_channels.count; }

int archive::channel_spans(snowflake channel_id, span* out, int cap)
{
    if (!g_ready || cap <= 0) return 0;

    wchar_t path[MAX_PATH];
    if (!channel_path(channel_id, L".ranges", path, MAX_PATH)) return 0;

    ubuffer blob;
    blob.init();
    if (!ufile::read_all(path, &blob)) { blob.free_buffer(); return 0; }

    const char* text = (const char*)blob.c_str();
    int count = 0;

    unsigned int at = 0;
    while (at < blob.size && count < cap)
    {
        unsigned int end = at;
        while (end < blob.size && text[end] != '\n') end++;

        char line[64];
        unsigned int len = end - at;
        if (len > 2 && len < sizeof(line))
        {
            ccpy(line, text + at, len);
            line[len] = 0;

            char* space = 0;
            for (char* p = line; *p; p++) if (*p == ' ') { space = p; break; }

            if (space)
            {
                *space = 0;
                out[count].from_id = ccstrtoull(line, 0, 10);
                out[count].to_id = ccstrtoull(space + 1, 0, 10);
                if (out[count].from_id && out[count].to_id) count++;
            }
        }

        at = end + 1;
    }

    blob.free_buffer();

    // Merged so overlapping runs, which are the normal result of scrolling the
    // same conversation twice, do not look like separate stretches.
    for (int i = 0; i < count; i++)
    {
        for (int k = i + 1; k < count; k++)
        {
            if (out[k].from_id < out[i].from_id)
            {
                span t = out[i]; out[i] = out[k]; out[k] = t;
            }
        }
    }

    int merged = 0;
    for (int i = 0; i < count; i++)
    {
        if (merged && out[i].from_id <= out[merged - 1].to_id)
        {
            if (out[i].to_id > out[merged - 1].to_id) out[merged - 1].to_id = out[i].to_id;
            continue;
        }
        out[merged++] = out[i];
    }
    return merged;
}

int archive::all_channels(snowflake* out, int cap)
{
    if (!g_ready) return 0;

    wchar_t pattern[MAX_PATH];
    pattern[0] = 0;
    wappend(pattern, MAX_PATH, g_dir);
    wappend(pattern, MAX_PATH, L"\\*.jsonl");

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;

    int count = 0;
    do
    {
        if (count >= cap) break;

        char name[64];
        wcstochar(fd.cFileName, name, (int)sizeof(name));

        snowflake id = ccstrtoull(name, 0, 10);
        if (id) out[count++] = id;
    }
    while (FindNextFileW(h, &fd));

    FindClose(h);
    return count;
}

// ---------------------------------------------------------------------------
// the snapshot
// ---------------------------------------------------------------------------

namespace
{
    bool snapshot_path(wchar_t* out, int cap)
    {
        if (!g_ready) return false;
        out[0] = 0;
        wappend(out, cap, g_dir);
        wappend(out, cap, L"\\state.json");
        return true;
    }

    void write_user(jwriter* w, const duser* u)
    {
        w->begin_obj();
        w->kv_snowflake("id", u->id);
        if (u->username) w->kv_str("username", u->username);
        if (u->global_name) w->kv_str("global_name", u->global_name);
        if (u->discriminator) w->kv_str("discriminator", u->discriminator);
        if (u->avatar) w->kv_str("avatar", u->avatar);
        if (u->banner) w->kv_str("banner", u->banner);
        if (u->bio) w->kv_str("bio", u->bio);
        if (u->accent_color) w->kv_i64("accent_color", (long long)u->accent_color);
        if (u->public_flags) w->kv_i64("public_flags", u->public_flags);
        if (u->premium_type) w->kv_i64("premium_type", u->premium_type);
        if (u->bot) w->kv_bool("bot", true);
        w->end_obj();
    }

    void write_channel(jwriter* w, const dchannel* c)
    {
        w->begin_obj();
        w->kv_snowflake("id", c->id);
        w->kv_i64("type", c->type);
        if (c->guild_id) w->kv_snowflake("guild_id", c->guild_id);
        if (c->parent_id) w->kv_snowflake("parent_id", c->parent_id);
        if (c->last_message_id) w->kv_snowflake("last_message_id", c->last_message_id);
        if (c->name) w->kv_str("name", c->name);
        if (c->topic) w->kv_str("topic", c->topic);
        if (c->icon) w->kv_str("icon", c->icon);
        w->kv_i64("position", c->position);

        if (c->recipients.count)
        {
            w->key("recipient_ids");
            w->begin_arr();
            for (unsigned int i = 0; i < c->recipients.count; i++)
            {
                char id[24];
                cnprint(id, sizeof(id), "%llu", c->recipients[i]);
                w->val_str(id);
            }
            w->end_arr();
        }
        w->end_obj();
    }
}

bool archive::snapshot_save()
{
    if (!g_ready) return false;

    jwriter w;
    w.init();
    w.begin_obj();

    ulist<snowflake> ids;
    ids = ulist<snowflake>();

    {
        store::guard guard;

        w.kv_snowflake("self", store::self_id());

        // Everybody the client knows about, which after a while is everybody
        // who has said anything. Their names and avatars are what makes a
        // saved conversation readable rather than a wall of numbers.
        store::all_users(&ids);
        w.key("users");
        w.begin_arr();
        for (unsigned int i = 0; i < ids.count; i++)
        {
            duser* u = store::find_user(ids[i]);
            if (u) write_user(&w, u);
        }
        w.end_arr();

        const ulist<drelationship>& rels = store::relationships();
        w.key("relationships");
        w.begin_arr();
        for (unsigned int i = 0; i < rels.count; i++)
        {
            w.begin_obj();
            w.kv_snowflake("id", rels[i].user_id);
            w.kv_i64("type", rels[i].type);
            if (rels[i].nickname) w.kv_str("nickname", rels[i].nickname);
            w.end_obj();
        }
        w.end_arr();

        const ulist<snowflake>& guilds = store::guild_order();
        w.key("guilds");
        w.begin_arr();
        for (unsigned int i = 0; i < guilds.count; i++)
        {
            dguild* g = store::find_guild(guilds[i]);
            if (!g) continue;

            w.begin_obj();
            w.kv_snowflake("id", g->id);
            if (g->name) w.kv_str("name", g->name);
            if (g->icon) w.kv_str("icon", g->icon);
            if (g->owner_id) w.kv_snowflake("owner_id", g->owner_id);
            w.kv_i64("position", g->position);

            w.key("channels");
            w.begin_arr();
            for (unsigned int k = 0; k < g->channels.count; k++)
            {
                dchannel* c = store::find_channel(g->channels[k]);
                if (c) write_channel(&w, c);
            }
            w.end_arr();
            w.end_obj();
        }
        w.end_arr();

        // Direct messages live outside any server and would be lost otherwise.
        store::all_channels(&ids);
        w.key("dms");
        w.begin_arr();
        for (unsigned int i = 0; i < ids.count; i++)
        {
            dchannel* c = store::find_channel(ids[i]);
            if (c && c->is_dm()) write_channel(&w, c);
        }
        w.end_arr();
    }

    w.end_obj();
    ids.dispose();

    // Written beside the real file and moved into place, so a snapshot
    // interrupted halfway never replaces a good one with a truncated one.
    wchar_t path[MAX_PATH];
    if (!snapshot_path(path, MAX_PATH)) { w.free_writer(); return false; }

    wchar_t temp[MAX_PATH];
    temp[0] = 0;
    wappend(temp, MAX_PATH, path);
    wappend(temp, MAX_PATH, L".new");

    bool ok = ufile::write_all(temp, w.buf.data, w.buf.size);
    w.free_writer();

    if (!ok) return false;
    if (!MoveFileExW(temp, path, MOVEFILE_REPLACE_EXISTING))
    {
        DeleteFileW(temp);
        return false;
    }

    log_line("archive: снимок состояния сохранён");
    return true;
}

bool archive::snapshot_load()
{
    if (!g_ready) return false;

    wchar_t path[MAX_PATH];
    if (!snapshot_path(path, MAX_PATH)) return false;

    ubuffer blob;
    blob.init();
    if (!ufile::read_all(path, &blob)) { blob.free_buffer(); return false; }

    jdoc doc;
    doc.init();

    bool ok = false;
    if (doc.parse((const char*)blob.c_str(), (int)blob.size) && doc.r()->type == JTYPE_OBJ)
    {
        const jval* root = doc.r();
        store::guard guard;

        snowflake self = root->sf("self");
        if (self && !store::self_id()) store::set_self_id(self);

        const jval* users = root->arr("users");
        for (unsigned int i = 0; i < users->count; i++) store::upsert_user(users->at(i));

        const jval* rels = root->arr("relationships");
        for (unsigned int i = 0; i < rels->count; i++)
        {
            const jval* r = rels->at(i);
            store::set_relationship(r->sf("id"), (int)r->i64("type", 0), r->str("nickname", 0));
        }

        const jval* guilds = root->arr("guilds");
        for (unsigned int i = 0; i < guilds->count; i++)
        {
            const jval* g = guilds->at(i);
            dguild* guild = store::upsert_guild(g);

            const jval* channels = g->arr("channels");
            for (unsigned int k = 0; k < channels->count; k++)
                store::upsert_channel(channels->at(k), guild ? guild->id : g->sf("id"));
        }

        const jval* dms = root->arr("dms");
        for (unsigned int i = 0; i < dms->count; i++)
            store::upsert_channel(dms->at(i), 0);

        store::touch_dm_order();
        store::bump_revision();

        log_line("archive: снимок восстановлен, %u человек, %u серверов",
                 users->count, guilds->count);
        ok = true;
    }

    doc.free_doc();
    blob.free_buffer();
    return ok;
}

unsigned long long archive::snapshot_age_seconds()
{
    wchar_t path[MAX_PATH];
    if (!snapshot_path(path, MAX_PATH)) return 0;

    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &fad)) return 0;

    FILETIME now_ft;
    GetSystemTimeAsFileTime(&now_ft);

    unsigned long long now = ((unsigned long long)now_ft.dwHighDateTime << 32) | now_ft.dwLowDateTime;
    unsigned long long then = ((unsigned long long)fad.ftLastWriteTime.dwHighDateTime << 32) |
                              fad.ftLastWriteTime.dwLowDateTime;
    if (then >= now) return 0;

    return (now - then) / 10000000ULL;
}
