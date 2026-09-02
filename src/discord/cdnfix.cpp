#include "pch.h"
#include "cdnfix.h"
#include "rest.h"
#include "net/json.h"
#include "core/log.h"
#include "net/http.h"

namespace
{
    struct fixed
    {
        unsigned long long key;        // the path, without the query
        unsigned long long expires;    // seconds, as the query spells it
        char url[512];
        bool asking;
    };

    const int MAX_FIXED = 512;
    const int BATCH = 40;

    CRITICAL_SECTION g_lock;
    bool g_ready = false;

    fixed g_fixed[MAX_FIXED];
    int g_count = 0;

    volatile long g_in_flight = 0;
    unsigned long long g_last_send = 0;

    // Waiting to be asked about. Kept separately from the table above because
    // a link is only worth a slot there once there is something to put in it.
    char g_queue[BATCH][512];
    int g_queued = 0;

    unsigned long long now_seconds()
    {
        // The same clock the expiry is expressed in: seconds since 1970.
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);

        unsigned long long ticks =
            ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;

        // From 1601 to 1970 in hundred nanosecond units.
        return (ticks - 116444736000000000ull) / 10000000ull;
    }

    // The part before the query. Two links to the same attachment differ only
    // after the question mark, so this is what identifies one.
    unsigned long long path_key(const char* url)
    {
        char path[512];
        int n = 0;

        while (url[n] && url[n] != '?' && n < (int)sizeof(path) - 1)
        {
            path[n] = url[n];
            n++;
        }

        path[n] = 0;
        return ccscrc64(path);
    }

    // "ex" out of the query, as hex. Zero when the link carries none, which is
    // how an avatar or a guild icon looks - those never expire.
    unsigned long long expiry_of(const char* url)
    {
        for (const char* p = url; *p; p++)
        {
            if (p[0] != 'e' || p[1] != 'x' || p[2] != '=') continue;
            if (p != url && p[-1] != '?' && p[-1] != '&') continue;

            unsigned long long v = 0;
            const char* q = p + 3;

            while (*q && *q != '&')
            {
                int digit;
                if (*q >= '0' && *q <= '9')      digit = *q - '0';
                else if (*q >= 'a' && *q <= 'f') digit = *q - 'a' + 10;
                else if (*q >= 'A' && *q <= 'F') digit = *q - 'A' + 10;
                else return 0;

                v = v * 16 + (unsigned long long)digit;
                q++;
            }

            return v;
        }

        return 0;
    }

    fixed* find(unsigned long long key)
    {
        for (int i = 0; i < g_count; i++)
            if (g_fixed[i].key == key) return &g_fixed[i];

        return 0;
    }

    fixed* make(unsigned long long key)
    {
        if (g_count >= MAX_FIXED)
        {
            // Oldest expiry goes first: it is the one most likely to be dead
            // again anyway.
            int victim = 0;
            for (int i = 1; i < g_count; i++)
                if (g_fixed[i].expires < g_fixed[victim].expires) victim = i;

            ccfset(&g_fixed[victim], 0, sizeof(fixed));
            g_fixed[victim].key = key;
            return &g_fixed[victim];
        }

        fixed* f = &g_fixed[g_count++];
        ccfset(f, 0, sizeof(*f));
        f->key = key;
        return f;
    }

    void job_refresh(void* user)
    {
        char (*batch)[512] = (char (*)[512])user;

        jwriter w;
        w.init();
        w.begin_obj();
        w.key("attachment_urls");
        w.begin_arr();

        int sent = 0;
        for (int i = 0; i < BATCH && batch[i][0]; i++) { w.val_str(batch[i]); sent++; }

        w.end_arr();
        w.end_obj();

        http_response res;
        res.init();

        bool ok = api::call("POST", "/attachments/refresh-urls", w.buf.c_str(), &res) && res.ok();

        if (ok)
        {
            jdoc doc;
            doc.init();

            if (doc.parse(res.text(), (int)res.body.size) && doc.root->type == JTYPE_OBJ)
            {
                const jval* list = doc.root->arr("refreshed_urls");

                EnterCriticalSection(&g_lock);

                for (unsigned int i = 0; i < list->count; i++)
                {
                    const jval* e = list->at(i);

                    const char* original = e->str("original", 0);
                    const char* fresh = e->str("refreshed", 0);
                    if (!original || !fresh || !fresh[0]) continue;

                    fixed* f = find(path_key(original));
                    if (!f) f = make(path_key(original));

                    ccstrncpy(f->url, fresh, sizeof(f->url) - 1);
                    f->expires = expiry_of(fresh);
                    f->asking = false;
                }

                LeaveCriticalSection(&g_lock);

                log_line("cdnfix: обновлено ссылок %u из %d", list->count, sent);
            }
            else
            {
                // Written down rather than guessed at: this request was built
                // from the shape the official client uses, and the first thing
                // worth knowing when it does not work is what came back.
                log_line("cdnfix: ответ не разобрался: %.300s", res.body.size ? res.text() : "");
                ok = false;
            }

            doc.free_doc();
        }
        else
        {
            log_line("cdnfix: обновление ссылок не вышло, http %d %.200s",
                     res.status, res.body.size ? res.text() : "");
        }

        if (!ok)
        {
            // Let them be asked about again rather than sit marked as pending
            // for the rest of the session.
            EnterCriticalSection(&g_lock);
            for (int i = 0; i < BATCH && batch[i][0]; i++)
            {
                fixed* f = find(path_key(batch[i]));
                if (f) f->asking = false;
            }
            LeaveCriticalSection(&g_lock);
        }

        res.free_response();
        w.free_writer();
        memfree(batch);

        InterlockedDecrement(&g_in_flight);
    }
}

void cdnfix::init()
{
    if (g_ready) return;

    InitializeCriticalSection(&g_lock);
    g_ready = true;
}

void cdnfix::reset()
{
    if (!g_ready) return;

    EnterCriticalSection(&g_lock);
    ccfset(g_fixed, 0, sizeof(g_fixed));
    g_count = 0;
    g_queued = 0;
    LeaveCriticalSection(&g_lock);
}

const char* cdnfix::usable(const char* url)
{
    if (!g_ready || !url || !url[0]) return url;

    unsigned long long expires = expiry_of(url);
    if (!expires) return url;

    // A minute of margin: a link that dies while the picture is being fetched
    // is a picture that fails for no visible reason.
    unsigned long long now = now_seconds();
    if (expires > now + 60) return url;

    EnterCriticalSection(&g_lock);

    unsigned long long key = path_key(url);
    fixed* f = find(key);

    if (f && f->url[0] && f->expires > now + 60)
    {
        const char* fresh = f->url;
        LeaveCriticalSection(&g_lock);
        return fresh;
    }

    if (!f) f = make(key);

    if (!f->asking && g_queued < BATCH)
    {
        f->asking = true;
        ccstrncpy(g_queue[g_queued], url, sizeof(g_queue[0]) - 1);
        g_queued++;
    }

    LeaveCriticalSection(&g_lock);
    return 0;
}

void cdnfix::tick()
{
    if (!g_ready) return;

    EnterCriticalSection(&g_lock);

    // Sent when the batch is full or when the trickle has stopped, so that a
    // page with three stale pictures is not held waiting for thirty-seven more.
    unsigned long long now = GetTickCount64();
    bool due = g_queued >= BATCH || (g_queued > 0 && now - g_last_send > 400);

    if (!due || g_in_flight >= 2)
    {
        LeaveCriticalSection(&g_lock);
        return;
    }

    char (*batch)[512] = (char (*)[512])memalloc(BATCH * 512);
    if (!batch)
    {
        LeaveCriticalSection(&g_lock);
        return;
    }

    ccfset(batch, 0, BATCH * 512);
    for (int i = 0; i < g_queued; i++) ccpy(batch[i], g_queue[i], 512);

    g_queued = 0;
    g_last_send = now;

    LeaveCriticalSection(&g_lock);

    InterlockedIncrement(&g_in_flight);
    jobs::post(job_refresh, batch);
}

bool cdnfix::self_test()
{
    cdnfix::init();
    cdnfix::reset();

    // The expiry is read out of the query and nowhere else.
    const char* signed_url =
        "https://cdn.discordapp.com/attachments/1/2/a.png?ex=6a8a2eed&is=6a88dd6d&hm=ab";

    if (expiry_of(signed_url) != 0x6a8a2eedull)
    {
        log_line("cdnfix: самопроверка: срок разобран как %llu", expiry_of(signed_url));
        return false;
    }

    // A link with no expiry is handed straight back: avatars and icons never
    // carry one and must not be queued for refreshing.
    const char* plain = "https://cdn.discordapp.com/avatars/1/abc.png?size=64";

    if (expiry_of(plain) != 0)
    {
        log_line("cdnfix: самопроверка: у обычной ссылки нашёлся срок");
        return false;
    }

    if (cdnfix::usable(plain) != plain)
    {
        log_line("cdnfix: самопроверка: обычная ссылка не вернулась как есть");
        return false;
    }

    // "ex" has to be a parameter, not any two letters that happen to sit next
    // to an equals sign.
    const char* decoy = "https://cdn.discordapp.com/attachments/1/2/index=3.png?flex=99";

    if (expiry_of(decoy) != 0)
    {
        log_line("cdnfix: самопроверка: 'flex=' принято за срок");
        return false;
    }

    // Two links to the same attachment differ only in the query, and have to
    // land on the same entry.
    unsigned long long a = path_key(signed_url);
    unsigned long long b = path_key("https://cdn.discordapp.com/attachments/1/2/a.png?ex=1&hm=zz");

    if (a != b)
    {
        log_line("cdnfix: самопроверка: один и тот же файл дал разные ключи");
        return false;
    }

    // An expired one is not handed back, and it is queued exactly once
    // however often it is asked for.
    if (cdnfix::usable(signed_url) != 0)
    {
        log_line("cdnfix: самопроверка: просроченная ссылка выдана как годная");
        return false;
    }

    cdnfix::usable(signed_url);
    cdnfix::usable(signed_url);

    if (g_queued != 1)
    {
        log_line("cdnfix: самопроверка: в очереди %d, ждали 1", g_queued);
        return false;
    }

    cdnfix::reset();

    log_line("cdnfix: самопроверка пройдена");
    return true;
}
