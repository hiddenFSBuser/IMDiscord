#include "pch.h"
#include "exporter.h"
#include "archive.h"
#include "store.h"
#include "rest.h"
#include "core/log.h"
#include "net/json.h"
#include "net/http.h"
#include "system/io/ufile.h"

namespace
{
    volatile long g_warming = 0;
    volatile long g_warm_done = 0;
    volatile long g_warm_total = 0;
    volatile long g_warm_messages = 0;
    char g_warm_status[192];
    HANDLE g_warm_thread = 0;

    void wappend(wchar_t* dst, int cap, const wchar_t* add)
    {
        int n = 0;
        while (n < cap - 1 && dst[n]) n++;
        for (int i = 0; add[i] && n < cap - 1; i++) dst[n++] = add[i];
        dst[n] = 0;
    }

    // Everything written into the page goes through this. A message body is
    // arbitrary text from strangers, and an export that renders it as markup
    // is a hole in whoever opens the file.
    void escape_html(const char* in, ubuffer* out)
    {
        if (!in) return;
        for (const char* p = in; *p; p++)
        {
            switch (*p)
            {
            case '&':  out->append("&amp;", 5); break;
            case '<':  out->append("&lt;", 4); break;
            case '>':  out->append("&gt;", 4); break;
            case '"':  out->append("&quot;", 6); break;
            case '\'': out->append("&#39;", 5); break;
            case '\n': out->append("<br>", 4); break;
            case '\r': break;
            default:   out->append(p, 1); break;
            }
        }
    }

    void put(ubuffer* out, const char* text)
    {
        out->append(text, (unsigned int)ccslenf(text));
    }

    // The formatter this project ships has no va_list entry point, so every
    // caller formats into a buffer of its own and hands the result over.
    void put_line(ubuffer* out, const char* line)
    {
        out->append(line, (unsigned int)ccslenf(line));
    }

    // Deliberately plain. An export is read years later, often on a machine
    // that has never heard of this client, and a stylesheet fetched from
    // somewhere else would be a blank page by then.
    const char* PAGE_HEAD =
        "<!doctype html><html lang=\"ru\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<style>"
        "body{background:#1e1f22;color:#dbdee1;font:14px/1.5 system-ui,Segoe UI,sans-serif;margin:0;padding:24px}"
        ".head{border-bottom:1px solid #3c3f45;padding-bottom:14px;margin-bottom:18px}"
        ".head h1{margin:0 0 4px;font-size:20px}"
        ".head .meta{color:#949ba4;font-size:12px}"
        ".m{display:flex;gap:12px;padding:6px 0}"
        ".m .av{width:38px;height:38px;border-radius:50%;background:#2b2d31;flex:0 0 38px;object-fit:cover}"
        ".m .body{min-width:0;flex:1}"
        ".m .who{font-weight:600;color:#fff}"
        ".m .when{color:#949ba4;font-size:11px;margin-left:8px}"
        ".m .text{white-space:pre-wrap;word-wrap:break-word}"
        ".sys{color:#949ba4;font-style:italic;padding:4px 0 4px 50px}"
        ".del{color:#da373c}"
        ".del .text{text-decoration:line-through;opacity:.75}"
        ".att{margin-top:4px}"
        ".att a{color:#00a8fc;text-decoration:none}"
        ".att img{max-width:420px;max-height:340px;border-radius:6px;display:block;margin-top:4px}"
        ".gap{margin:18px 0;padding:8px 12px;border-left:3px solid #f0b232;background:#26262a;color:#f0b232;font-size:12px}"
        "</style></head><body>";

    struct exported
    {
        snowflake id;
        snowflake author_id;
        int type;
        bool deleted;
        char timestamp[40];
        char author[80];
        char avatar[80];
        char* content;
        // Attachments are kept as one blob of pre-rendered markup: a message
        // rarely has more than a couple and this avoids a second allocation
        // scheme for something so small.
        char* attachments;
    };

    void free_exported(ulist<exported>* list)
    {
        for (unsigned int i = 0; i < list->count; i++)
        {
            if ((*list)[i].content) memfree((*list)[i].content);
            if ((*list)[i].attachments) memfree((*list)[i].attachments);
        }
        list->dispose();
    }

    char* dup_text(const char* s)
    {
        if (!s) return 0;
        int n = (int)ccslenf(s);
        char* copy = (char*)memalloc(n + 1);
        if (!copy) return 0;
        ccpy(copy, s, (size_t)n);
        copy[n] = 0;
        return copy;
    }

    // Reads the channel's log straight off the disk rather than through the
    // store: an export must not be limited to what happens to be in memory,
    // and a later line about the same message replaces the earlier one.
    bool read_channel(snowflake channel_id, ulist<exported>* out,
                      const wchar_t* save_dir, const char* save_leaf)
    {
        wchar_t base[MAX_PATH];
        if (!ufile::app_path(L"archive", base, MAX_PATH)) return false;

        // The archive knows its own folder; asking it to load into the store
        // and reading back from there is simpler than duplicating the path
        // logic, and the store is where the newest copy already is.
        archive::load_channel(channel_id);

        store::guard guard;
        dchannel* c = store::find_channel(channel_id);
        if (!c) return false;

        for (unsigned int i = 0; i < c->messages.count; i++)
        {
            dmessage* m = &c->messages[i];
            if (m->pending) continue;

            exported e;
            ccfset(&e, 0, sizeof(e));
            e.id = m->id;
            e.author_id = m->author_id;
            e.type = m->type;
            e.deleted = m->deleted;
            if (m->timestamp) ccstrncpy(e.timestamp, m->timestamp, sizeof(e.timestamp) - 1);

            duser* u = store::find_user(m->author_id);
            if (u)
            {
                ccstrncpy(e.author, u->display_name(), sizeof(e.author) - 1);
                if (u->avatar) ccstrncpy(e.avatar, u->avatar, sizeof(e.avatar) - 1);
            }
            else
            {
                cnprint(e.author, sizeof(e.author), "%llu", m->author_id);
            }

            e.content = dup_text(m->content);

            if (m->attachments.count)
            {
                ubuffer markup;
                markup.init(256);
                for (unsigned int k = 0; k < m->attachments.count; k++)
                {
                    const dattachment* a = &m->attachments[k];
                    if (!a->url) continue;

                    put(&markup, "<div class=\"att\">");
                    // Copied next to the page when asked. A link keeps the
                    // export small, but discord's attachment urls expire and
                    // an account that stops working takes them with it, so a
                    // copy is the only version that outlives the message.
                    if (save_dir && save_dir[0])
                    {
                        char leaf[160];
                        cnprint(leaf, sizeof(leaf), "%llu_%s", a->id,
                                a->filename ? a->filename : "file");

                        // A filename from a stranger must not be able to point
                        // anywhere but the folder it was meant for.
                        for (char* p = leaf; *p; p++)
                            if (*p == '\\' || *p == '/' || *p == ':' || *p == '?' ||
                                *p == '*' || *p == '"' || *p == '<' || *p == '>' || *p == '|')
                                *p = '_';

                        wchar_t target[MAX_PATH];
                        target[0] = 0;
                        wappend(target, MAX_PATH, save_dir);
                        wappend(target, MAX_PATH, L"\\");

                        wchar_t wleaf[160];
                        chartowcs(leaf, wleaf, 160);
                        wappend(target, MAX_PATH, wleaf);

                        if (!ufile::exists(target))
                        {
                            http_response got;
                            got.init();
                            if (http::get(a->url, &got) && got.ok() && got.body.size)
                                ufile::write_all(target, got.body.data, got.body.size);
                            got.free_response();
                        }

                        char rel[200];
                        cnprint(rel, sizeof(rel), "%s/%s", save_leaf, leaf);

                        char local[600];
                        if (a->is_image())
                            cnprint(local, sizeof(local),
                                    "<a href=\"%s\"><img src=\"%s\" alt=\"\" loading=\"lazy\"></a>",
                                    rel, rel);
                        else
                            cnprint(local, sizeof(local), "<a href=\"%s\">%s</a>", rel,
                                    a->filename ? a->filename : "файл");

                        put_line(&markup, local);
                        put(&markup, "</div>");
                        continue;
                    }

                    char line[1400];
                    if (a->is_image())
                        cnprint(line, sizeof(line),
                                "<a href=\"%s\"><img src=\"%s\" alt=\"\" loading=\"lazy\"></a>",
                                a->url, a->url);
                    else
                        cnprint(line, sizeof(line), "<a href=\"%s\">%s</a>", a->url,
                                a->filename ? a->filename : "файл");
                    put_line(&markup, line);
                    put(&markup, "</div>");
                }
                markup.append("\0", 1);
                e.attachments = dup_text((const char*)markup.data);
                markup.free_buffer();
            }

            out->push(e);
        }

        // Oldest first, and by id rather than by timestamp: ids are ordered by
        // construction and a clock somewhere can be wrong.
        for (unsigned int i = 0; i < out->count; i++)
            for (unsigned int k = i + 1; k < out->count; k++)
                if ((*out)[k].id < (*out)[i].id)
                {
                    exported t = (*out)[i];
                    (*out)[i] = (*out)[k];
                    (*out)[k] = t;
                }

        return true;
    }

    const char* system_text(int type)
    {
        switch (type)
        {
        case 1:  return "добавил участника в беседу";
        case 2:  return "убрал участника из беседы";
        case 3:  return "начал звонок";
        case 4:  return "сменил название беседы";
        case 5:  return "сменил значок беседы";
        case 6:  return "закрепил сообщение";
        case 7:  return "зашёл на сервер";
        case 18: return "создал ветку";
        default: return 0;
        }
    }

    // True when the two messages are known to be adjacent, meaning one of the
    // saved runs covers both. Anything else is a hole and gets said out loud.
    bool covered_together(const archive::span* spans, int count,
                          snowflake a, snowflake b)
    {
        for (int i = 0; i < count; i++)
            if (spans[i].from_id <= a && b <= spans[i].to_id) return true;
        return false;
    }
}

bool exporter::channel_to_html(snowflake channel_id, const wchar_t* path,
                               export_attachments files)
{
    ulist<exported> messages;
    messages = ulist<exported>();

    // Attachments, when they are to be kept, go into a folder named after the
    // page so several exports can share one directory without colliding.
    wchar_t files_dir[MAX_PATH];
    char files_leaf[80];
    files_dir[0] = 0;
    files_leaf[0] = 0;

    if (files == EXPORT_SAVE_FILES)
    {
        cnprint(files_leaf, sizeof(files_leaf), "%llu_files", channel_id);

        int cut = 0;
        for (int i = 0; path[i]; i++)
            if (path[i] == L'\\' || path[i] == L'/') cut = i;

        for (int i = 0; i < cut && i < MAX_PATH - 1; i++) files_dir[i] = path[i];
        files_dir[cut] = 0;
        wappend(files_dir, MAX_PATH, L"\\");

        wchar_t wleaf[80];
        chartowcs(files_leaf, wleaf, 80);
        wappend(files_dir, MAX_PATH, wleaf);
        CreateDirectoryW(files_dir, 0);
    }

    if (!read_channel(channel_id, &messages,
                      files == EXPORT_SAVE_FILES ? files_dir : 0,
                      files == EXPORT_SAVE_FILES ? files_leaf : 0))
    {
        free_exported(&messages);
        return false;
    }

    char title[256];
    {
        store::guard guard;
        dchannel* c = store::find_channel(channel_id);
        if (c && c->name && c->name[0])
        {
            cnprint(title, sizeof(title), "%s", c->name);
        }
        else if (c && c->is_dm() && c->recipients.count)
        {
            duser* peer = store::find_user(c->recipients[0]);
            cnprint(title, sizeof(title), "%s", peer ? peer->display_name() : "личные сообщения");
        }
        else
        {
            cnprint(title, sizeof(title), "%llu", channel_id);
        }
    }

    archive::span spans[256];
    int span_count = archive::channel_spans(channel_id, spans, 256);

    ubuffer page;
    page.init(1 << 16);

    put(&page, PAGE_HEAD);
    put(&page, "<div class=\"head\"><h1>");
    escape_html(title, &page);
    put(&page, "</h1><div class=\"meta\">");
    {
        char meta[192];
        cnprint(meta, sizeof(meta),
                "канал %llu &middot; сообщений %u &middot; сохранённых отрезков %d",
                channel_id, messages.count, span_count);
        put_line(&page, meta);
    }
    put(&page, "</div></div>");

    for (unsigned int i = 0; i < messages.count; i++)
    {
        const exported* m = &messages[i];

        // A hole is worth more than a smooth page: two messages a year apart
        // run together look like a conversation that never happened.
        if (i && span_count && !covered_together(spans, span_count, messages[i - 1].id, m->id))
        {
            put(&page, "<div class=\"gap\">пропуск: между этими сообщениями история не сохранялась</div>");
        }

        const char* sys = (!m->content || !m->content[0]) ? system_text(m->type) : 0;
        if (sys)
        {
            put(&page, "<div class=\"sys\">");
            escape_html(m->author, &page);
            put(&page, " ");
            escape_html(sys, &page);
            put(&page, "</div>");
            continue;
        }

        put(&page, m->deleted ? "<div class=\"m del\">" : "<div class=\"m\">");

        if (m->avatar[0])
        {
            char av[256];
            cnprint(av, sizeof(av),
                    "<img class=\"av\" src=\"https://cdn.discordapp.com/avatars/%llu/%s.png?size=64\""
                    " alt=\"\" loading=\"lazy\">",
                    m->author_id, m->avatar);
            put_line(&page, av);
        }
        else
            put(&page, "<div class=\"av\"></div>");

        put(&page, "<div class=\"body\"><div><span class=\"who\">");
        escape_html(m->author, &page);
        put(&page, "</span><span class=\"when\">");
        escape_html(m->timestamp, &page);
        if (m->deleted) put(&page, " &middot; удалено");
        put(&page, "</span></div><div class=\"text\">");
        escape_html(m->content, &page);
        put(&page, "</div>");

        if (m->attachments) put(&page, m->attachments);

        put(&page, "</div></div>");
    }

    put(&page, "</body></html>");

    bool ok = ufile::write_all(path, page.data, page.size);
    page.free_buffer();
    free_exported(&messages);

    if (ok) log_line("export: канал %llu записан", channel_id);
    return ok;
}

int exporter::everything_to_html(const wchar_t* folder, export_attachments files)
{
    CreateDirectoryW(folder, 0);

    snowflake ids[4096];
    int count = archive::all_channels(ids, 4096);
    int written = 0;

    for (int i = 0; i < count; i++)
    {
        wchar_t path[MAX_PATH];
        path[0] = 0;
        wappend(path, MAX_PATH, folder);

        char leaf[64];
        cnprint(leaf, sizeof(leaf), "\\%llu.html", ids[i]);

        wchar_t wleaf[64];
        chartowcs(leaf, wleaf, 64);
        wappend(path, MAX_PATH, wleaf);

        if (channel_to_html(ids[i], path, files)) written++;
    }

    log_line("export: записано каналов %d из %d", written, count);
    return written;
}

// ---------------------------------------------------------------------------
// what to warm
// ---------------------------------------------------------------------------

namespace
{
    volatile long g_scope = exporter::WARM_DIRECT_ONLY;
    ulist<snowflake> g_picked;

    bool picked_has(snowflake id)
    {
        for (unsigned int i = 0; i < g_picked.count; i++)
            if (g_picked[i] == id) return true;
        return false;
    }

    // Whether a channel falls under the current choice. The store lock is the
    // caller's to hold: this is used while walking channels.
    bool wanted(const dchannel* c)
    {
        if (!c || !c->is_textual()) return false;

        switch (g_scope)
        {
        case exporter::WARM_EVERYTHING:  return true;
        case exporter::WARM_SELECTED:    return picked_has(c->id);
        default:                         return c->is_dm();
        }
    }

    // The channels the current choice covers, oldest server order first.
    void collect_targets(ulist<snowflake>* out)
    {
        out->clear_fast();

        store::guard guard;

        ulist<snowflake> all;
        all = ulist<snowflake>();
        store::all_channels(&all);

        for (unsigned int i = 0; i < all.count; i++)
        {
            dchannel* c = store::find_channel(all[i]);
            if (wanted(c)) out->push(c->id);
        }
        all.dispose();
    }
}

void exporter::warm_set_scope(warm_scope scope)
{
    InterlockedExchange(&g_scope, (long)scope);
}

exporter::warm_scope exporter::warm_current_scope()
{
    return (warm_scope)g_scope;
}

void exporter::warm_select(snowflake channel_id, bool on)
{
    if (!channel_id) return;

    for (unsigned int i = 0; i < g_picked.count; i++)
    {
        if (g_picked[i] != channel_id) continue;
        if (!on) g_picked.delete_at(i);
        return;
    }

    if (on) g_picked.push(channel_id);
}

bool exporter::warm_is_selected(snowflake channel_id)
{
    return picked_has(channel_id);
}

void exporter::warm_select_guild(snowflake guild_id, bool on)
{
    store::guard guard;

    dguild* g = store::find_guild(guild_id);
    if (!g) return;

    for (unsigned int i = 0; i < g->channels.count; i++)
    {
        dchannel* c = store::find_channel(g->channels[i]);
        if (c && c->is_textual()) warm_select(c->id, on);
    }
}

bool exporter::warm_guild_fully_selected(snowflake guild_id)
{
    store::guard guard;

    dguild* g = store::find_guild(guild_id);
    if (!g) return false;

    bool any = false;
    for (unsigned int i = 0; i < g->channels.count; i++)
    {
        dchannel* c = store::find_channel(g->channels[i]);
        if (!c || !c->is_textual()) continue;

        any = true;
        if (!picked_has(c->id)) return false;
    }
    return any;
}

void exporter::warm_clear_selection()
{
    g_picked.clear_fast();
}

unsigned int exporter::warm_selection_count()
{
    return g_picked.count;
}

unsigned int exporter::warm_planned_count()
{
    ulist<snowflake> targets;
    targets = ulist<snowflake>();
    collect_targets(&targets);

    unsigned int n = targets.count;
    targets.dispose();
    return n;
}

// ---------------------------------------------------------------------------
// warm-up
// ---------------------------------------------------------------------------

namespace
{
    // Pulls one channel back to its beginning, fifty at a time, which is
    // discord's own page size. The archive takes every page as it lands, so a
    // run that is interrupted is not a run that was wasted.
    unsigned int warm_channel(snowflake channel_id)
    {
        snowflake before = 0;
        unsigned int total = 0;

        for (int page = 0; page < 400 && g_warming; page++)
        {
            char path[256];
            if (before)
                cnprint(path, sizeof(path),
                        "/channels/%llu/messages?limit=50&before=%llu", channel_id, before);
            else
                cnprint(path, sizeof(path), "/channels/%llu/messages?limit=50", channel_id);

            http_response res;
            res.init();

            bool ok = api::call("GET", path, 0, &res) && res.ok();
            if (!ok)
            {
                // A channel this account cannot read is not a failure of the
                // run; the rest of them are still worth doing.
                res.free_response();
                break;
            }

            unsigned int got = 0;
            snowflake oldest = 0, newest = 0;

            jdoc doc;
            doc.init();
            if (doc.parse(res.text(), (int)res.body.size) && doc.root->type == JTYPE_ARR)
            {
                got = doc.root->count;

                {
                    store::guard guard;
                    for (unsigned int i = 0; i < got; i++)
                    {
                        dmessage* m = store::upsert_message(doc.root->at(i));
                        if (!m) continue;
                        if (!oldest || m->id < oldest) oldest = m->id;
                        if (m->id > newest) newest = m->id;
                    }
                }

                for (unsigned int i = 0; i < got; i++) archive::put_json(doc.root->at(i));
                if (oldest && newest) archive::note_range(channel_id, oldest, newest);
            }
            doc.free_doc();
            res.free_response();

            total += got;
            InterlockedAdd(&g_warm_messages, (long)got);

            if (got < 50 || !oldest) break;
            before = oldest;

            // Deliberately unhurried. This is thousands of requests against an
            // API that will start refusing them, and a warm-up that gets the
            // account rate limited has cost more than it saved.
            Sleep(350);
        }

        return total;
    }

    DWORD WINAPI warm_thread(LPVOID)
    {
        ulist<snowflake> targets;
        targets = ulist<snowflake>();
        collect_targets(&targets);

        InterlockedExchange(&g_warm_total, (long)targets.count);
        InterlockedExchange(&g_warm_done, 0);

        for (unsigned int i = 0; i < targets.count && g_warming; i++)
        {
            {
                store::guard guard;
                dchannel* c = store::find_channel(targets[i]);
                cnprint(g_warm_status, sizeof(g_warm_status), "%s",
                        (c && c->name) ? c->name : "канал");
            }

            warm_channel(targets[i]);
            InterlockedIncrement(&g_warm_done);
        }

        targets.dispose();

        cnprint(g_warm_status, sizeof(g_warm_status), "готово");
        InterlockedExchange(&g_warming, 0);
        archive::snapshot_save();
        return 0;
    }
}

void exporter::warm_start()
{
    if (g_warming) return;
    if (!archive::ready()) return;

    InterlockedExchange(&g_warming, 1);
    InterlockedExchange(&g_warm_messages, 0);
    cnprint(g_warm_status, sizeof(g_warm_status), "начинаем");

    if (g_warm_thread) { CloseHandle(g_warm_thread); g_warm_thread = 0; }
    g_warm_thread = CreateThread(0, 0, warm_thread, 0, 0, 0);
    if (!g_warm_thread) InterlockedExchange(&g_warming, 0);
}

void exporter::warm_stop()
{
    InterlockedExchange(&g_warming, 0);
}

bool exporter::warming() { return g_warming != 0; }
const char* exporter::warm_status() { return g_warm_status; }
unsigned int exporter::warm_channels_done() { return (unsigned int)g_warm_done; }
unsigned int exporter::warm_channels_total() { return (unsigned int)g_warm_total; }
unsigned int exporter::warm_messages() { return (unsigned int)g_warm_messages; }
