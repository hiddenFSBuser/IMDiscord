#include "pch.h"
#include <shellapi.h>
#include "captcha.h"
#include "core/log.h"
#include "core/crypto.h"
#include "core/storage.h"
#include "net/json.h"

namespace
{
    enum cap_state
    {
        CAP_IDLE,
        CAP_WAITING,
    };

    // One solve at a time. The worker thread owns the wait; the listener
    // thread only fills the token in and signals. Everything is torn down
    // by the waiter, never by the listener.
    cap_state g_state = CAP_IDLE;
    HANDLE g_done = 0;
    HANDLE g_thread = 0;
    SOCKET g_listener = INVALID_SOCKET;
    volatile long g_stop = 0;

    captcha::challenge g_ch;
    char g_action[128];
    char g_nonce[40];
    unsigned short g_port = 0;
    unsigned long long g_deadline_ms = 0;
    char g_token[4096];
    bool g_solved = false;

    CRITICAL_SECTION g_lock;
    bool g_lock_ready = false;

    void lock_ready()
    {
        if (g_lock_ready) return;
        InitializeCriticalSection(&g_lock);
        g_lock_ready = true;
    }

    bool header_value(const char* headers, const char* name, char* out, int cap)
    {
        // Case-insensitive name match over "Name: value\r\n" lines.
        int namelen = 0;
        while (name[namelen]) namelen++;

        for (const char* p = headers; *p; p++)
        {
            int i = 0;
            while (i < namelen && p[i] &&
                   (p[i] | 0x20) == (name[i] | 0x20)) i++;
            if (i != namelen || p[i] != ':') continue;

            p += i + 1;
            while (*p == ' ' || *p == '\t') p++;

            int at = 0;
            while (*p && *p != '\r' && *p != '\n' && at < cap - 1)
                out[at++] = *p++;
            out[at] = 0;
            return true;
        }
        return false;
    }

    int header_int(const char* headers, const char* name, int fallback)
    {
        char value[32];
        if (!header_value(headers, name, value, sizeof(value))) return fallback;

        int v = 0;
        for (const char* p = value; *p >= '0' && *p <= '9'; p++)
            v = v * 10 + (*p - '0');
        return v;
    }

    // The token out of {"token":"..."}. Hand-rolled: it is one string in a
    // one-field object, and pulling jdoc in for that is the heavier move.
    bool extract_token(const char* body, int len, char* out, int cap)
    {
        if (!body || len <= 0 || !out || cap <= 0) return false;

        int at = -1;
        for (int i = 0; i + 7 < len; i++)
        {
            if (body[i] == '"' && body[i + 1] == 't' && body[i + 2] == 'o' &&
                body[i + 3] == 'k' && body[i + 4] == 'e' && body[i + 5] == 'n' &&
                body[i + 6] == '"')
            {
                at = i + 7;
                break;
            }
        }
        if (at < 0) return false;

        while (at < len && (body[at] == ' ' || body[at] == '\t' ||
                            body[at] == '\r' || body[at] == '\n' ||
                            body[at] == ':')) at++;
        if (at >= len || body[at] != '"') return false;
        at++;

        int n = 0;
        while (at < len && n < cap - 1)
        {
            char c = body[at];
            if (c == '"') break;
            if (c == '\\' && at + 1 < len)
            {
                at++;
                c = body[at];
                if (c == 'n') c = '\n';
                else if (c == 'r') c = '\r';
                else if (c == 't') c = '\t';
            }
            out[n++] = c;
            at++;
        }
        out[n] = 0;

        // A solved hCaptcha token is long. Anything shorter is garbage that
        // would only earn a second refusal.
        return n >= 32;
    }

    void send_all(SOCKET s, const char* data, int len)
    {
        int sent = 0;
        while (sent < len)
        {
            int n = send(s, data + sent, len - sent, 0);
            if (n <= 0) return;
            sent += n;
        }
    }

    void reply(SOCKET s, int code, const char* phrase, const char* type,
               const char* body, int body_len)
    {
        char head[256];
        cnprint(head, sizeof(head),
                "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %d\r\n"
                "Connection: close\r\n\r\n",
                code, phrase, type, body_len);
        send_all(s, head, (int)ccslenf(head));
        if (body && body_len > 0) send_all(s, body, body_len);
    }

    void build_page(char* out, int cap, const captcha::challenge* ch,
                    const char* token_url, bool invisible)
    {
        // Config as JSON, escaped by the writer rather than by hand.
        jwriter cfg;
        cfg.init();
        cfg.begin_obj();
        cfg.kv_str("sitekey", ch->sitekey);
        if (ch->rqdata[0]) cfg.kv_str("rqdata", ch->rqdata);
        else cfg.kv_null("rqdata");
        cfg.kv_bool("invisible", invisible);
        cfg.kv_str("tokenUrl", token_url);
        cfg.end_obj();

        cnprint(out, cap,
            "<!doctype html><html><head><meta charset=\"utf-8\">"
            "<title>IMDiscord captcha</title>"
            "<script src=\"https://js.hcaptcha.com/1/api.js?render=explicit\" async defer></script>"
            "<style>body{font-family:sans-serif;background:#1e1f22;color:#dbdee1;"
            "display:flex;align-items:center;justify-content:center;min-height:100vh;margin:0}"
            ".box{text-align:center;max-width:420px}#ok{color:#57f287}</style>"
            "</head><body><div class=\"box\">"
            "<h3>IMDiscord: подтверди, что ты человек</h3>"
            "<div id=\"c\"></div><p id=\"st\">Загрузка проверки...</p>"
            "<script>var cfg=%s;"
            "function onSolved(t){"
            "document.getElementById('st').innerHTML='<span id=\"ok\">Готово. Можно закрыть вкладку.</span>';"
            "fetch(cfg.tokenUrl,{method:'POST',headers:{'Content-Type':'application/json'},"
            "body:JSON.stringify({token:t})});}"
            "function renderWidget(){try{"
            "var id=hcaptcha.render('c',{sitekey:cfg.sitekey,callback:onSolved,"
            "'error-callback':function(){document.getElementById('st').textContent='Ошибка. Обнови страницу.';},"
            "size:(cfg.invisible?'invisible':'normal')});"
            "if(cfg.rqdata){hcaptcha.execute(id,{rqdata:cfg.rqdata});}"
            "else if(cfg.invisible){hcaptcha.execute(id);}"
            "else{document.getElementById('st').textContent='Поставь галочку.';}"
            "}catch(e){setTimeout(renderWidget,300);}}"
            "var n=0;var iv=setInterval(function(){"
            "if(window.hcaptcha){clearInterval(iv);renderWidget();}"
            "else if(++n>200){clearInterval(iv);"
            "document.getElementById('st').textContent='Не загрузился api.js. Проверь сеть.';}},100);"
            "</script></div></body></html>",
            cfg.c_str());
        cfg.free_writer();
    }

    // Reads one browser request and answers it. Returns false when the
    // solve is over and the thread should stop.
    bool serve_once(SOCKET listener, const char* nonce, unsigned short port)
    {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(listener, &rfds);
        timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        if (select(0, &rfds, 0, 0, &tv) <= 0)
        {
            // Timeout: re-check the deadline and the stop flag.
            if (GetTickCount64() >= g_deadline_ms)
            {
                SetEvent(g_done);
                return false;
            }
            return InterlockedCompareExchange(&g_stop, 0, 0) == 0;
        }

        sockaddr_in from;
        int from_len = (int)sizeof(from);
        SOCKET s = accept(listener, (sockaddr*)&from, &from_len);
        if (s == INVALID_SOCKET) return InterlockedCompareExchange(&g_stop, 0, 0) == 0;

        // Loopback only, belt and suspenders beside the bind itself.
        if (from.sin_addr.s_addr != htonl(INADDR_LOOPBACK))
        {
            closesocket(s);
            return InterlockedCompareExchange(&g_stop, 0, 0) == 0;
        }

        char req[24576];
        int got = 0;
        bool complete = false;

        while (got < (int)sizeof(req) - 1)
        {
            FD_ZERO(&rfds);
            FD_SET(s, &rfds);
            tv.tv_sec = 5;
            tv.tv_usec = 0;
            if (select(0, &rfds, 0, 0, &tv) <= 0) break;

            int n = recv(s, req + got, (int)sizeof(req) - 1 - got, 0);
            if (n <= 0) break;
            got += n;
            req[got] = 0;

            const char* end = 0;
            for (int i = 0; i + 3 < got; i++)
            {
                if (req[i] == '\r' && req[i + 1] == '\n' &&
                    req[i + 2] == '\r' && req[i + 3] == '\n')
                {
                    end = req + i + 4;
                    break;
                }
            }
            if (!end) continue;

            int need = header_int(req, "Content-Length", 0);
            if (need < 0) need = 0;
            if (need > 8192) need = 8192;
            if (got < (end - req) + need) continue;

            complete = true;
            break;
        }

        if (complete)
        {
            char method[16] = { 0 };
            char path[160] = { 0 };
            {
                int i = 0;
                while (req[i] && req[i] != ' ' && i < (int)sizeof(method) - 1)
                {
                    method[i] = req[i];
                    i++;
                }
                method[i] = 0;
                while (req[i] == ' ') i++;
                int k = 0;
                while (req[i] && req[i] != ' ' && req[i] != '?' && k < (int)sizeof(path) - 1)
                    path[k++] = req[i++];
                path[k] = 0;
            }

            char want_page[80];
            char want_token[80];
            cnprint(want_page, sizeof(want_page), "/%s", nonce);
            cnprint(want_token, sizeof(want_token), "/%s/token", nonce);

            if (ccscmp(method, "GET") == 0 && ccscmp(path, want_page) == 0)
            {
                char token_url[160];
                cnprint(token_url, sizeof(token_url),
                        "http://127.0.0.1:%u/%s/token",
                        (unsigned int)port, nonce);

                // rqdata forces the invisible shape even when the demand
                // did not say so outright: it is executed, not ticked.
                bool invisible = false;
                EnterCriticalSection(&g_lock);
                invisible = g_ch.rqdata[0] != 0;
                LeaveCriticalSection(&g_lock);

                static char page[12288];
                EnterCriticalSection(&g_lock);
                build_page(page, sizeof(page), &g_ch, token_url, invisible);
                LeaveCriticalSection(&g_lock);

                reply(s, 200, "OK", "text/html; charset=utf-8",
                      page, (int)ccslenf(page));
            }
            else if (ccscmp(method, "POST") == 0 && ccscmp(path, want_token) == 0)
            {
                char origin[160];
                char expect[64];
                cnprint(expect, sizeof(expect), "http://127.0.0.1:%u",
                        (unsigned int)port);

                bool ok = header_value(req, "Origin", origin, sizeof(origin)) &&
                          ccscmp(origin, expect) == 0;

                char token[4096];
                token[0] = 0;
                if (ok)
                {
                    const char* end = 0;
                    for (int i = 0; i + 3 < got; i++)
                    {
                        if (req[i] == '\r' && req[i + 1] == '\n' &&
                            req[i + 2] == '\r' && req[i + 3] == '\n')
                        {
                            end = req + i + 4;
                            break;
                        }
                    }
                    int body_len = end ? (got - (int)(end - req)) : 0;
                    ok = extract_token(end ? end : req, body_len,
                                       token, sizeof(token));
                }

                if (ok)
                {
                    static const char done_body[] = "{\"ok\":true}";
                    reply(s, 200, "OK", "application/json",
                          done_body, (int)sizeof(done_body) - 1);

                    EnterCriticalSection(&g_lock);
                    ccstrncpy(g_token, token, sizeof(g_token) - 1);
                    g_solved = true;
                    LeaveCriticalSection(&g_lock);

                    InterlockedExchange(&g_stop, 1);
                    SetEvent(g_done);
                    log_line("captcha: токен получен из браузера");
                    closesocket(s);
                    return false;
                }

                static const char bad_body[] = "{\"ok\":false}";
                reply(s, 400, "Bad Request", "application/json",
                      bad_body, (int)sizeof(bad_body) - 1);
            }
            else
            {
                static const char nf[] = "not found";
                reply(s, 404, "Not Found", "text/plain", nf, (int)sizeof(nf) - 1);
            }
        }

        closesocket(s);

        if (GetTickCount64() >= g_deadline_ms)
        {
            SetEvent(g_done);
            return false;
        }
        return InterlockedCompareExchange(&g_stop, 0, 0) == 0;
    }

    DWORD WINAPI listen_thread(LPVOID)
    {
        char nonce[40];
        unsigned short port = 0;

        EnterCriticalSection(&g_lock);
        ccstrncpy(nonce, g_nonce, sizeof(nonce) - 1);
        port = g_port;
        LeaveCriticalSection(&g_lock);

        while (InterlockedCompareExchange(&g_stop, 0, 0) == 0)
        {
            if (!serve_once(g_listener, nonce, port)) break;
        }
        return 0;
    }

    void teardown_locked()
    {
        InterlockedExchange(&g_stop, 1);
        if (g_listener != INVALID_SOCKET)
        {
            closesocket(g_listener);
            g_listener = INVALID_SOCKET;
        }
        if (g_thread)
        {
            HANDLE t = g_thread;
            g_thread = 0;
            LeaveCriticalSection(&g_lock);
            WaitForSingleObject(t, 3000);
            CloseHandle(t);
            EnterCriticalSection(&g_lock);
        }
        if (g_done)
        {
            CloseHandle(g_done);
            g_done = 0;
        }
        g_state = CAP_IDLE;
        g_solved = false;
        g_token[0] = 0;
    }
}

bool captcha::parse_demand(const jval* root, challenge* out)
{
    if (!root || root->type != JTYPE_OBJ || !out) return false;
    if (!root->has("captcha_sitekey") && !root->has("captcha_key")) return false;

    ccfset(out, 0, sizeof(*out));

    const char* svc = root->str("captcha_service", 0);
    ccstrncpy(out->service, (svc && svc[0]) ? svc : "hcaptcha", sizeof(out->service) - 1);

    const char* site = root->str("captcha_sitekey", 0);
    if (!site || !site[0]) return false;
    ccstrncpy(out->sitekey, site, sizeof(out->sitekey) - 1);

    const char* sess = root->str("captcha_session_id", 0);
    if (sess) ccstrncpy(out->session, sess, sizeof(out->session) - 1);

    const char* rqdata = root->str("captcha_rqdata", 0);
    if (rqdata) ccstrncpy(out->rqdata, rqdata, sizeof(out->rqdata) - 1);

    const char* rqtoken = root->str("captcha_rqtoken", 0);
    if (rqtoken) ccstrncpy(out->rqtoken, rqtoken, sizeof(out->rqtoken) - 1);

    return true;
}

bool captcha::solve_blocking(const challenge* ch, const char* action,
                             char* token_out, int token_cap)
{
    if (!ch || !ch->sitekey[0] || !token_out || token_cap <= 0) return false;
    if (ccscmp(ch->service, "hcaptcha") != 0)
    {
        log_line("captcha: сервис %s не поддерживается", ch->service);
        return false;
    }

    lock_ready();
    EnterCriticalSection(&g_lock);
    if (g_state != CAP_IDLE)
    {
        LeaveCriticalSection(&g_lock);
        log_line("captcha: уже решается, вторая отклонена");
        return false;
    }

    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET)
    {
        LeaveCriticalSection(&g_lock);
        return false;
    }

    sockaddr_in addr;
    ccfset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if (bind(listener, (sockaddr*)&addr, sizeof(addr)) != 0 ||
        listen(listener, 4) != 0)
    {
        closesocket(listener);
        LeaveCriticalSection(&g_lock);
        return false;
    }

    int addr_len = (int)sizeof(addr);
    if (getsockname(listener, (sockaddr*)&addr, &addr_len) != 0)
    {
        closesocket(listener);
        LeaveCriticalSection(&g_lock);
        return false;
    }

    // The page address doubles as the secret: unguessable, single use,
    // loopback only.
    unsigned char rnd[16];
    crypto::random_bytes(rnd, sizeof(rnd));
    const char* hex = "0123456789abcdef";
    for (int i = 0; i < 16; i++)
    {
        g_nonce[i * 2] = hex[rnd[i] >> 4];
        g_nonce[i * 2 + 1] = hex[rnd[i] & 0x0F];
    }
    g_nonce[32] = 0;

    ccfset(&g_ch, 0, sizeof(g_ch));
    g_ch = *ch;
    ccstrncpy(g_action, (action && action[0]) ? action : "?", sizeof(g_action) - 1);

    g_listener = listener;
    g_port = ntohs(addr.sin_port);
    g_deadline_ms = GetTickCount64() + 5ULL * 60ULL * 1000ULL;
    g_token[0] = 0;
    g_solved = false;
    InterlockedExchange(&g_stop, 0);
    g_done = CreateEventW(0, TRUE, FALSE, 0);
    if (!g_done)
    {
        closesocket(listener);
        g_listener = INVALID_SOCKET;
        LeaveCriticalSection(&g_lock);
        return false;
    }

    g_thread = CreateThread(0, 0, listen_thread, 0, 0, 0);
    if (!g_thread)
    {
        CloseHandle(g_done);
        g_done = 0;
        closesocket(listener);
        g_listener = INVALID_SOCKET;
        LeaveCriticalSection(&g_lock);
        return false;
    }

    g_state = CAP_WAITING;

    char url[160];
    cnprint(url, sizeof(url), "http://127.0.0.1:%u/%s",
            (unsigned int)g_port, g_nonce);

    bool open_now = auto_open();
    LeaveCriticalSection(&g_lock);

    if (open_now)
    {
        log_line("captcha: %s, страница открыта в браузере (%s)", g_action, url);

        wchar_t wurl[192];
        chartowcs(url, wurl, 192);
        if ((INT_PTR)ShellExecuteW(0, L"open", wurl, 0, 0, SW_SHOWNORMAL) <= 32)
        {
            log_line("captcha: браузер не открылся");
            EnterCriticalSection(&g_lock);
            teardown_locked();
            LeaveCriticalSection(&g_lock);
            return false;
        }
    }
    else
    {
        log_line("captcha: %s, ждёт открытия из интерфейса (%s)", g_action, url);
    }

    WaitForSingleObject(g_done, 5UL * 60UL * 1000UL);

    EnterCriticalSection(&g_lock);
    bool ok = g_solved && g_token[0];
    if (ok && token_cap > 0) ccstrncpy(token_out, g_token, token_cap - 1);
    if (!ok) log_line("captcha: не решена (отмена или время вышло)");
    teardown_locked();
    LeaveCriticalSection(&g_lock);

    return ok;
}

void captcha::set_auto_open(bool on)
{
    storage::settings_set_int("captcha_auto_open", on ? 1 : 0);
}

bool captcha::auto_open()
{
    return storage::settings_get_int("captcha_auto_open", 0) != 0;
}

bool captcha::waiting()
{
    lock_ready();
    EnterCriticalSection(&g_lock);
    bool w = (g_state == CAP_WAITING);
    LeaveCriticalSection(&g_lock);
    return w;
}

const char* captcha::action_text()
{
    lock_ready();
    EnterCriticalSection(&g_lock);
    const char* a = g_action[0] ? g_action : "?";
    LeaveCriticalSection(&g_lock);
    return a;
}

void captcha::cancel()
{
    lock_ready();
    EnterCriticalSection(&g_lock);
    if (g_state == CAP_WAITING)
    {
        log_line("captcha: отменена из интерфейса");
        InterlockedExchange(&g_stop, 1);
        SetEvent(g_done);
    }
    LeaveCriticalSection(&g_lock);
}

void captcha::reopen_browser()
{
    lock_ready();
    EnterCriticalSection(&g_lock);
    bool active = (g_state == CAP_WAITING);
    char url[160];
    url[0] = 0;
    if (active)
        cnprint(url, sizeof(url), "http://127.0.0.1:%u/%s",
                (unsigned int)g_port, g_nonce);
    LeaveCriticalSection(&g_lock);

    if (!active || !url[0]) return;

    wchar_t wurl[192];
    chartowcs(url, wurl, 192);
    ShellExecuteW(0, L"open", wurl, 0, 0, SW_SHOWNORMAL);
}
