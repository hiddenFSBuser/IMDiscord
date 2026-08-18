#include "pch.h"
#include "http.h"
#include "proxy.h"
#include "httpstream.h"
#include "core/log.h"


// ---------------------------------------------------------------------------
// response
// ---------------------------------------------------------------------------

void http_response::init()
{
    status = 0;
    retry_after_ms = 0;
    body.init();
    content_type[0] = 0;
    expected_length = 0;
    truncated = false;
}

void http_response::free_response()
{
    body.free_buffer();
    status = 0;
}

// ---------------------------------------------------------------------------
// session + per-host connection cache
// ---------------------------------------------------------------------------

namespace
{
    // One live connection, kept so a burst of requests to the same host does
    // not pay for a handshake each time. Discord's api is one host and the
    // cdn is two more, so a handful of slots covers it.
    struct host_entry
    {
        char host[256];
        unsigned short port;
        bool secure;
        http_stream stream;
        unsigned long long idle_since;
        bool in_use;
    };

    const int MAX_HOSTS = 8;
    const unsigned long long IDLE_LIMIT_MS = 30000;

    host_entry g_hosts[MAX_HOSTS];
    CRITICAL_SECTION g_lock;
    bool g_ready = false;

    char g_agent[256] = { 0 };
    proxy_config g_proxy;

    // Guarded by g_lock. A request holds a connection out of the table while
    // it uses it, so two threads never write to the same socket.
    host_entry* take_connection(const char* host, unsigned short port, bool secure)
    {
        EnterCriticalSection(&g_lock);

        host_entry* found = 0;
        for (int i = 0; i < MAX_HOSTS && !found; i++)
        {
            host_entry* e = &g_hosts[i];
            if (e->in_use || !e->stream.open()) continue;
            if (e->port != port || e->secure != secure) continue;
            if (ccscmp(e->host, host) != 0) continue;

            // A connection nobody has touched for a while has probably been
            // closed by the far end already; opening a fresh one costs less
            // than discovering that halfway through a request.
            if (GetTickCount64() - e->idle_since > IDLE_LIMIT_MS)
            {
                httpstream::close(&e->stream);
                continue;
            }

            found = e;
        }

        if (!found)
        {
            for (int i = 0; i < MAX_HOSTS && !found; i++)
                if (!g_hosts[i].in_use && !g_hosts[i].stream.open()) found = &g_hosts[i];
        }

        if (!found)
        {
            // Everything is busy or parked. Take the oldest idle one.
            unsigned long long oldest = ~0ULL;
            for (int i = 0; i < MAX_HOSTS; i++)
            {
                if (g_hosts[i].in_use) continue;
                if (g_hosts[i].idle_since < oldest) { oldest = g_hosts[i].idle_since; found = &g_hosts[i]; }
            }
            if (found) httpstream::close(&found->stream);
        }

        if (found)
        {
            found->in_use = true;
            ccstrncpy(found->host, host, sizeof(found->host) - 1);
            found->port = port;
            found->secure = secure;
        }

        LeaveCriticalSection(&g_lock);
        return found;
    }

    void give_back(host_entry* e, bool reusable)
    {
        if (!e) return;

        EnterCriticalSection(&g_lock);
        if (!reusable) httpstream::close(&e->stream);
        e->idle_since = GetTickCount64();
        e->in_use = false;
        LeaveCriticalSection(&g_lock);
    }

    void drop_all()
    {
        EnterCriticalSection(&g_lock);
        for (int i = 0; i < MAX_HOSTS; i++)
        {
            if (g_hosts[i].in_use) continue;
            httpstream::close(&g_hosts[i].stream);
            g_hosts[i].host[0] = 0;
        }
        LeaveCriticalSection(&g_lock);
    }

    // ---- reading -------------------------------------------------------

    // Everything read from the socket, with a cursor. Headers and body arrive
    // in the same stream and often in the same packet, so the body has to be
    // able to start from what the header read already pulled in.
    struct reader
    {
        http_stream* s;
        ubuffer buf;
        unsigned int at;
        unsigned long long deadline;
        bool ended;
    };

    bool pull(reader* r)
    {
        if (r->ended) return false;

        unsigned long long now = GetTickCount64();
        if (now >= r->deadline) return false;

        char chunk[16384];
        int got = httpstream::read(r->s, chunk, (int)sizeof(chunk),
                                   (unsigned int)(r->deadline - now));
        if (got == 0) { r->ended = true; return false; }
        if (got < 0) { if (got == -1) r->ended = true; return false; }

        r->buf.append(chunk, (unsigned int)got);
        return true;
    }

    // The next line, without its terminator. False when the connection ended
    // before one arrived.
    bool read_line(reader* r, char* out, int cap)
    {
        for (;;)
        {
            for (unsigned int i = r->at; i + 1 < r->buf.size; i++)
            {
                if (r->buf.data[i] != '\r' || r->buf.data[i + 1] != '\n') continue;

                unsigned int len = i - r->at;
                if ((int)len > cap - 1) len = (unsigned int)cap - 1;

                ccpy(out, r->buf.data + r->at, len);
                out[len] = 0;
                r->at = i + 2;
                return true;
            }

            if (!pull(r)) return false;
        }
    }

    bool read_exact(reader* r, ubuffer* out, unsigned int want)
    {
        while (r->buf.size - r->at < want)
            if (!pull(r)) break;

        unsigned int have = r->buf.size - r->at;
        unsigned int take = have < want ? have : want;

        if (take) out->append(r->buf.data + r->at, take);
        r->at += take;
        return take == want;
    }

    unsigned int hex_value(const char* text)
    {
        unsigned int value = 0;
        for (const char* p = text; *p; p++)
        {
            int digit;
            if (*p >= '0' && *p <= '9') digit = *p - '0';
            else if (*p >= 'a' && *p <= 'f') digit = *p - 'a' + 10;
            else if (*p >= 'A' && *p <= 'F') digit = *p - 'A' + 10;
            else break;
            value = value * 16 + (unsigned int)digit;
        }
        return value;
    }

    // ---- cookies ---------------------------------------------------------
    //
    // One jar for the process, the same way the proxy is one setting: every
    // request here goes to discord, and the cookies it sets are about this
    // installation rather than about one account.
    struct cookie
    {
        char name[64];
        char value[512];
    };

    const int MAX_COOKIES = 24;

    cookie g_cookies[MAX_COOKIES];
    int g_cookie_count = 0;
    CRITICAL_SECTION g_cookie_lock;
    volatile long g_cookie_lock_ready = 0;

    void cookie_lock()
    {
        if (InterlockedCompareExchange(&g_cookie_lock_ready, 1, 0) == 0)
            InitializeCriticalSection(&g_cookie_lock);
        EnterCriticalSection(&g_cookie_lock);
    }

    void cookie_unlock() { LeaveCriticalSection(&g_cookie_lock); }

    // "name=value; Path=/; Secure; HttpOnly" - everything after the first
    // semicolon is about how a browser should manage it and is not sent back.
    void remember_cookie(const char* value)
    {
        while (*value == ' ') value++;

        const char* eq = 0;
        for (const char* p = value; *p && *p != ';'; p++)
            if (*p == '=') { eq = p; break; }

        if (!eq || eq == value) return;

        char name[64];
        int n = (int)(eq - value);
        if (n > (int)sizeof(name) - 1) n = (int)sizeof(name) - 1;
        for (int i = 0; i < n; i++) name[i] = value[i];
        name[n] = 0;

        char val[512];
        int v = 0;
        for (const char* p = eq + 1; *p && *p != ';' && v < (int)sizeof(val) - 1; p++)
            val[v++] = *p;
        val[v] = 0;

        cookie_lock();

        // A cookie set again replaces the one held, which is what a browser
        // does and what keeps a rotating one - discord rotates __cf_bm -
        // from filling the jar.
        for (int i = 0; i < g_cookie_count; i++)
        {
            if (ccscmp(g_cookies[i].name, name) != 0) continue;
            ccstrncpy(g_cookies[i].value, val, sizeof(g_cookies[i].value) - 1);
            cookie_unlock();
            return;
        }

        if (g_cookie_count < MAX_COOKIES)
        {
            // Said once per name, so the log shows what discord set on this
            // client without turning into a line per request.
            log_line("http: cookie %s принят", name);
            ccstrncpy(g_cookies[g_cookie_count].name, name, sizeof(name) - 1);
            ccstrncpy(g_cookies[g_cookie_count].value, val, sizeof(val) - 1);
            g_cookie_count++;
        }

        cookie_unlock();
    }

    bool header_is(const char* line, const char* name)
    {
        unsigned int n = (unsigned int)ccslenf(name);
        for (unsigned int i = 0; i < n; i++)
            if (cctolower(line[i]) != cctolower(name[i])) return false;
        return line[n] == ':';
    }

    const char* header_value(const char* line)
    {
        const char* p = line;
        while (*p && *p != ':') p++;
        if (*p == ':') p++;
        while (*p == ' ') p++;
        return p;
    }
}


bool http::parse_url(const char* url, url_parts* out)
{
    if (!url || !out) return false;

    ccfset(out, 0, sizeof(*out));

    const char* p = url;
    if (ccsncmpf(p, "https://", 8) == 0) { out->secure = true; p += 8; }
    else if (ccsncmpf(p, "http://", 7) == 0) { out->secure = false; p += 7; }
    else if (ccsncmpf(p, "wss://", 6) == 0) { out->secure = true; p += 6; }
    else if (ccsncmpf(p, "ws://", 5) == 0) { out->secure = false; p += 5; }
    else return false;

    int i = 0;
    while (*p && *p != '/' && *p != ':' && i < 255) out->host[i++] = *p++;
    out->host[i] = 0;
    if (i == 0) return false;

    out->port = out->secure ? 443 : 80;
    if (*p == ':')
    {
        p++;
        int port = 0;
        while (*p >= '0' && *p <= '9') { port = port * 10 + (*p - '0'); p++; }
        if (port > 0 && port < 65536) out->port = port;
    }

    if (*p != '/')
    {
        out->path[0] = '/';
        out->path[1] = 0;

        // A query with no path, e.g. "gateway.discord.gg/?v=9".
        if (*p == '?')
        {
            int k = 1;
            while (*p && k < 2047) out->path[k++] = *p++;
            out->path[k] = 0;
        }
        return true;
    }

    int k = 0;
    while (*p && k < 2047) out->path[k++] = *p++;
    out->path[k] = 0;
    return true;
}

void http::set_proxy(const proxy_config* cfg)
{
    if (!g_ready) return;

    EnterCriticalSection(&g_lock);
    bool changed = false;

    if (!cfg)
    {
        changed = g_proxy.in_use();
        ccfset(&g_proxy, 0, sizeof(g_proxy));
    }
    else
    {
        changed = ccmp(&g_proxy, cfg, sizeof(g_proxy)) != 0;
        g_proxy = *cfg;
    }
    LeaveCriticalSection(&g_lock);

    // Anything already open went out the old way and would keep doing so.
    if (changed)
    {
        drop_all();
        log_line("http: запросы идут %s%s", g_proxy.in_use() ? "через " : "напрямую",
                 g_proxy.in_use() ? g_proxy.host : "");
    }
}

void http::init(const char* user_agent)
{
    if (g_ready) return;

    InitializeCriticalSection(&g_lock);
    ccfset(g_hosts, 0, sizeof(g_hosts));
    for (int i = 0; i < MAX_HOSTS; i++) g_hosts[i].stream.plain = INVALID_SOCKET;

    ccfset(&g_proxy, 0, sizeof(g_proxy));
    ccstrncpy(g_agent, user_agent ? user_agent : "IMDiscord/1.0", sizeof(g_agent) - 1);

    tlsnet::init();
    g_ready = true;
}

void http::shutdown()
{
    if (!g_ready) return;

    EnterCriticalSection(&g_lock);
    for (int i = 0; i < MAX_HOSTS; i++) httpstream::close(&g_hosts[i].stream);
    LeaveCriticalSection(&g_lock);

    DeleteCriticalSection(&g_lock);
    g_ready = false;
}

bool http::request(const char* method, const char* url, const char* headers_utf8,
                   const void* body, unsigned int body_len, http_response* out)
{
    if (!g_ready || !out) return false;

    out->status = 0;
    out->retry_after_ms = 0;
    out->body.clear();
    out->content_type[0] = 0;
    out->expected_length = 0;
    out->truncated = false;

    char current[2200];
    ccstrncpy(current, url, sizeof(current) - 1);

    // A redirect is followed rather than reported: the cdn answers with one
    // routinely, and every caller here wants the thing, not the notice.
    for (int hop = 0; hop < 5; hop++)
    {
        url_parts u;
        if (!http::parse_url(current, &u))
        {
            log_line("http: неразборчивый адрес %s", current);
            return false;
        }

        proxy_config route;
        EnterCriticalSection(&g_lock);
        route = g_proxy;
        LeaveCriticalSection(&g_lock);

        host_entry* e = take_connection(u.host, (unsigned short)u.port, u.secure);
        if (!e) return false;

        bool fresh = false;
        if (!e->stream.open())
        {
            if (!httpstream::connect(&e->stream, u.host, (unsigned short)u.port,
                                     u.secure, route.in_use() ? &route : 0, 15000))
            {
                log_line("http: %s недоступен (%s)", u.host, httpstream::last_error(&e->stream));
                give_back(e, false);
                return false;
            }
            fresh = true;
        }

        // ---- the request
        ubuffer head;
        head.init(2048);
        head.append_fmt("%s %s HTTP/1.1\r\n", method, u.path);
        head.append_fmt("Host: %s\r\n", u.host);
        head.append_fmt("User-Agent: %s\r\n", g_agent);
        head.append_str("Accept: */*\r\n");
        // No compression is offered on purpose: nothing here would gain from
        // it and every response then needs an inflater.
        head.append_str("Accept-Encoding: identity\r\n");
        head.append_str("Connection: keep-alive\r\n");

        {
            ubuffer jar;
            jar.init(1024);
            http::cookies_header(&jar);
            if (jar.size) head.append_fmt("Cookie: %s\r\n", jar.c_str());
            jar.free_buffer();
        }

        if (headers_utf8 && headers_utf8[0]) head.append_str(headers_utf8);

        if (body && body_len) head.append_fmt("Content-Length: %u\r\n", body_len);
        head.append_str("\r\n");

        bool sent = httpstream::write_all(&e->stream, head.data, (int)head.size);
        if (sent && body && body_len)
            sent = httpstream::write_all(&e->stream, body, (int)body_len);
        head.free_buffer();

        if (!sent)
        {
            // A kept connection the server closed while it was idle fails
            // here, on the first write. One retry on a fresh socket is the
            // whole of the recovery.
            give_back(e, false);
            if (fresh) return false;
            continue;
        }

        // ---- the answer
        reader r;
        r.s = &e->stream;
        r.buf.init(16384);
        r.at = 0;
        r.deadline = GetTickCount64() + 30000;
        r.ended = false;

        char line[4096];
        if (!read_line(&r, line, sizeof(line)))
        {
            r.buf.free_buffer();
            give_back(e, false);
            if (fresh) return false;
            continue;      // idle connection died; try once more
        }

        // "HTTP/1.1 200 OK"
        int status = 0;
        {
            const char* p = line;
            while (*p && *p != ' ') p++;
            while (*p == ' ') p++;
            while (*p >= '0' && *p <= '9') { status = status * 10 + (*p - '0'); p++; }
        }
        out->status = status;

        bool chunked = false;
        bool close_after = false;
        unsigned int content_length = 0;
        bool have_length = false;
        char location[2200];
        location[0] = 0;

        for (;;)
        {
            if (!read_line(&r, line, sizeof(line))) break;
            if (!line[0]) break;                     // blank line ends the headers

            if (header_is(line, "set-cookie"))
                remember_cookie(header_value(line));
            else if (header_is(line, "content-type"))
                ccstrncpy(out->content_type, header_value(line), sizeof(out->content_type) - 1);
            else if (header_is(line, "content-length"))
            {
                content_length = (unsigned int)ccstrtoull(header_value(line), 0, 10);
                have_length = true;
                out->expected_length = content_length;
            }
            else if (header_is(line, "transfer-encoding"))
            {
                const char* v = header_value(line);
                if (cctolower(v[0]) == 'c') chunked = true;
            }
            else if (header_is(line, "connection"))
            {
                const char* v = header_value(line);
                if (cctolower(v[0]) == 'c') close_after = true;
            }
            else if (header_is(line, "location"))
                ccstrncpy(location, header_value(line), sizeof(location) - 1);
            else if (header_is(line, "retry-after"))
            {
                // Seconds by the spec; discord also sends fractional ones.
                const char* v = header_value(line);
                double seconds = 0;
                unsigned long long whole = ccstrtoull(v, 0, 10);
                seconds = (double)whole;
                out->retry_after_ms = (int)(seconds * 1000.0);
            }
        }

        // ---- the body
        if (chunked)
        {
            for (;;)
            {
                if (!read_line(&r, line, sizeof(line))) { out->truncated = true; break; }

                unsigned int size = hex_value(line);
                if (!size)
                {
                    // The trailer, then a blank line.
                    while (read_line(&r, line, sizeof(line)) && line[0]) {}
                    break;
                }

                if (!read_exact(&r, &out->body, size)) { out->truncated = true; break; }
                read_line(&r, line, sizeof(line));      // the CRLF after the chunk
            }
        }
        else if (have_length)
        {
            if (!read_exact(&r, &out->body, content_length)) out->truncated = true;
        }
        else
        {
            // No length and no chunking: the body is whatever arrives until
            // the connection ends.
            close_after = true;
            while (pull(&r)) {}
            if (r.buf.size > r.at) out->body.append(r.buf.data + r.at, r.buf.size - r.at);
            r.at = r.buf.size;
        }

        r.buf.free_buffer();
        give_back(e, !close_after && !r.ended && !out->truncated);

        // ---- redirects
        if ((status == 301 || status == 302 || status == 303 ||
             status == 307 || status == 308) && location[0])
        {
            out->body.clear();
            out->content_type[0] = 0;
            out->expected_length = 0;

            if (location[0] == '/')
            {
                // Relative: keep the host it came from.
                cnprint(current, sizeof(current), "%s://%s%s",
                        u.secure ? "https" : "http", u.host, location);
            }
            else
            {
                ccstrncpy(current, location, sizeof(current) - 1);
            }
            continue;
        }

        return status > 0;
    }

    log_line("http: слишком много перенаправлений");
    return false;
}

namespace
{
    // ---- job pool ------------------------------------------------------
    //
    // Everything slow in this client runs here: rest calls, picture
    // downloads, the exporter. The ui thread only ever posts.
    struct job_item
    {
        job_fn fn;
        void* user;
    };

    volatile long g_job_running = 0;
    volatile long g_job_pending = 0;

    CRITICAL_SECTION g_job_lock;
    ulist<job_item> g_job_queue;
    HANDLE g_job_sem = 0;
    HANDLE g_job_threads[16] = { 0 };
    int g_job_thread_count = 0;

    DWORD WINAPI job_thread(LPVOID)
    {
        CoInitializeEx(0, COINIT_MULTITHREADED);

        while (g_job_running)
        {
            WaitForSingleObject(g_job_sem, INFINITE);
            if (!g_job_running) break;

            job_item item;
            item.fn = 0;
            item.user = 0;

            EnterCriticalSection(&g_job_lock);
            if (g_job_queue.count)
            {
                item = g_job_queue[0];
                g_job_queue.delete_at(0);
            }
            LeaveCriticalSection(&g_job_lock);

            if (item.fn) item.fn(item.user);
            InterlockedDecrement(&g_job_pending);
        }

        CoUninitialize();
        return 0;
    }
}

void jobs::init(int thread_count)
{
    if (g_job_running) return;
    if (thread_count < 1) thread_count = 1;
    if (thread_count > 16) thread_count = 16;

    InitializeCriticalSection(&g_job_lock);
    g_job_queue = ulist<job_item>();
    g_job_sem = CreateSemaphoreW(0, 0, 0x7FFFFFFF, 0);
    g_job_running = 1;
    g_job_thread_count = thread_count;

    for (int i = 0; i < thread_count; i++)
        g_job_threads[i] = CreateThread(0, 0, job_thread, 0, 0, 0);
}

void jobs::shutdown()
{
    if (!g_job_running) return;

    g_job_running = 0;
    ReleaseSemaphore(g_job_sem, g_job_thread_count, 0);
    WaitForMultipleObjects((DWORD)g_job_thread_count, g_job_threads, TRUE, 3000);

    for (int i = 0; i < g_job_thread_count; i++)
        if (g_job_threads[i]) CloseHandle(g_job_threads[i]);

    CloseHandle(g_job_sem);
    g_job_sem = 0;
    g_job_queue.dispose();
}

void jobs::post(job_fn fn, void* user)
{
    if (!g_job_running || !fn) return;

    EnterCriticalSection(&g_job_lock);
    job_item item;
    item.fn = fn;
    item.user = user;
    g_job_queue.push(item);
    LeaveCriticalSection(&g_job_lock);

    InterlockedIncrement(&g_job_pending);
    ReleaseSemaphore(g_job_sem, 1, 0);
}

int jobs::pending()
{
    return (int)g_job_pending;
}

void http::cookies_header(ubuffer* out)
{
    if (!out) return;
    out->clear();

    cookie_lock();
    for (int i = 0; i < g_cookie_count; i++)
    {
        if (out->size) out->append_str("; ");
        out->append_str(g_cookies[i].name);
        out->append_str("=");
        out->append_str(g_cookies[i].value);
    }
    cookie_unlock();
}

void http::clear_cookies()
{
    cookie_lock();
    g_cookie_count = 0;
    cookie_unlock();
}
