#include "pch.h"
#include "tlsconn.h"
#include "proxy.h"
#include "core/log.h"

extern "C" {
#include "tlse.h"
}

// Defined in tls_roots.cpp, which is the one place the bundle is included.
extern "C" const char* tlsnet_root_ca_pem();

namespace
{
    bool g_roots_loaded = false;
    unsigned char* g_roots = 0;
    unsigned int g_roots_len = 0;

    void fail(tls_stream* s, const char* what)
    {
        ccfset(s->error, 0, sizeof(s->error));
        ccstrncpy(s->error, what, sizeof(s->error) - 1);
        log_line("tls: %s", what);
    }

    // Everything tlse wants to say goes out through the socket here. It never
    // touches the socket itself, which is what lets the same code run over a
    // proxy tunnel without knowing about one.
    bool flush(tls_stream* s)
    {
        unsigned int len = 0;
        const unsigned char* out = tls_get_write_buffer((TLSContext*)s->ctx, &len);
        if (!out || !len) return true;

        int done = 0;
        while (done < (int)len)
        {
            int put = send(s->sock, (const char*)out + done, (int)len - done, 0);
            if (put <= 0) return false;
            done += put;
        }

        tls_buffer_clear((TLSContext*)s->ctx);
        return true;
    }

    bool wait_readable(SOCKET sock, unsigned int timeout_ms)
    {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(sock, &set);

        timeval tv;
        tv.tv_sec = (long)(timeout_ms / 1000);
        tv.tv_usec = (long)((timeout_ms % 1000) * 1000);

        return select(0, &set, 0, 0, &tv) > 0;
    }
}

void tlsnet::init()
{
    if (g_roots_loaded) return;
    g_roots_loaded = true;

    // The trust store ships with tlse as one PEM bundle. Baking it in rather
    // than reading the system store keeps this identical on every windows
    // back to seven, which is the entire reason for the exercise.
    const char* pem = tlsnet_root_ca_pem();

    g_roots = (unsigned char*)pem;
    g_roots_len = (unsigned int)ccslenf(pem);

    log_line("tls: доверенных корней загружено %u байт", g_roots_len);
}

bool tlsnet::connect(tls_stream* s, const char* host, unsigned short port,
                     const proxy_config* proxy, unsigned int timeout_ms)
{
    ccfset(s, 0, sizeof(*s));
    s->sock = INVALID_SOCKET;
    ccstrncpy(s->host, host ? host : "", sizeof(s->host) - 1);

    tlsnet::init();

    // Everything that does not need the network happens before the socket is
    // opened. Parsing a hundred and fifty root certificates takes long enough
    // that doing it afterwards leaves the connection sitting idle, and a
    // server that sees nothing from a fresh client simply hangs up.
    TLSContext* ctx = tls_create_context(0, TLS_V13);
    if (!ctx)
    {
        fail(s, "tls контекст не создался");
        return false;
    }
    s->ctx = ctx;

    // The name goes in the hello, without which a shared address hands back
    // the wrong certificate, and it is also what the certificate is checked
    // against afterwards.
    tls_sni_set(ctx, host);

    if (g_roots && g_roots_len)
        tls_load_root_certificates(ctx, g_roots, (int)g_roots_len);

    const char* why = "";
    SOCKET sock = INVALID_SOCKET;
    if (!proxy::dial_through(proxy, host, port, &sock, &why))
    {
        fail(s, why[0] ? why : "не удалось открыть соединение");
        tlsnet::close(s);
        return false;
    }
    s->sock = sock;

    tls_client_connect(ctx);
    if (!flush(s))
    {
        fail(s, "рукопожатие не отправилось");
        tlsnet::close(s);
        return false;
    }

    unsigned long long deadline = GetTickCount64() + timeout_ms;
    unsigned char buffer[8192];

    while (!tls_established(ctx))
    {
        if (GetTickCount64() > deadline)
        {
            fail(s, "рукопожатие не уложилось во время");
            tlsnet::close(s);
            return false;
        }

        if (!wait_readable(s->sock, 250)) continue;

        int got = recv(s->sock, (char*)buffer, (int)sizeof(buffer), 0);
        if (got <= 0)
        {
            fail(s, "соединение закрылось во время рукопожатия");
            tlsnet::close(s);
            return false;
        }

        // The verifier is the library's own: it walks the chain to a root in
        // the store and checks the name against what was set above.
        int rc = tls_consume_stream(ctx, buffer, got, tls_default_verify);
        if (rc < 0)
        {
            // Record type 22 is a handshake, 21 an alert. Saying which came
            // back turns "it did not work" into something diagnosable.
            log_line("tls: разбор не прошёл (%d), пришло %d байт: %02x %02x %02x %02x %02x %02x",
                     rc, got, buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5]);

            char text[96];
            if (buffer[0] == 21) cnprint(text, sizeof(text), "сервер прислал alert (%d)", rc);
            else                 cnprint(text, sizeof(text), "рукопожатие не разобралось (%d)", rc);
            fail(s, text);
            tlsnet::close(s);
            return false;
        }

        if (!flush(s))
        {
            fail(s, "ответ рукопожатия не отправился");
            tlsnet::close(s);
            return false;
        }
    }

    s->established = true;
    return true;
}

void tlsnet::close(tls_stream* s)
{
    if (!s) return;

    if (s->ctx)
    {
        tls_destroy_context((TLSContext*)s->ctx);
        s->ctx = 0;
    }
    if (s->sock != INVALID_SOCKET)
    {
        closesocket(s->sock);
        s->sock = INVALID_SOCKET;
    }
    s->established = false;
}

int tlsnet::write(tls_stream* s, const void* data, int len)
{
    if (!s || !s->established || len <= 0) return -1;

    // tls_write silently caps what it takes at one record and reports the
    // capped figure. Treating that as "all of it went" quietly truncated every
    // payload over sixteen kilobytes - which is why a small screenshot
    // uploaded and a large one came back as HTTP 0.
    const unsigned char* at = (const unsigned char*)data;
    int left = len;

    while (left > 0)
    {
        int put = tls_write((TLSContext*)s->ctx, at, (unsigned int)left);
        if (put <= 0) return -1;

        // Each record is pushed out as it is made rather than piling the whole
        // body up in the library's buffer first.
        if (!flush(s)) return -1;

        at += put;
        left -= put;
    }

    return len;
}

int tlsnet::read(tls_stream* s, void* out, int cap, unsigned int timeout_ms)
{
    if (!s || !s->established || cap <= 0) return -1;

    TLSContext* ctx = (TLSContext*)s->ctx;

    // Whatever a previous record already produced comes first: one socket
    // read can carry several records, and asking the socket again before
    // draining them would block with the answer already in hand.
    int have = tls_read(ctx, (unsigned char*)out, (unsigned int)cap);
    if (have > 0) return have;

    unsigned long long deadline = GetTickCount64() + timeout_ms;
    unsigned char buffer[16384];

    for (;;)
    {
        if (s->closed) return 0;

        unsigned long long now = GetTickCount64();
        if (now >= deadline) return -2;

        unsigned int slice = (unsigned int)(deadline - now);
        if (slice > 250) slice = 250;
        if (!wait_readable(s->sock, slice)) continue;

        int got = recv(s->sock, (char*)buffer, (int)sizeof(buffer), 0);
        if (got == 0) { s->closed = true; return 0; }
        if (got < 0) return -1;

        if (tls_consume_stream(ctx, buffer, got, tls_default_verify) < 0) return -1;

        // A rekey or an alert can leave something to send back.
        flush(s);

        have = tls_read(ctx, (unsigned char*)out, (unsigned int)cap);
        if (have > 0) return have;
        if (!tls_established(ctx)) { s->closed = true; return 0; }
    }
}

const char* tlsnet::last_error(tls_stream* s) { return s ? s->error : ""; }

bool tlsnet::self_test(const char* host)
{
    if (!host || !host[0]) host = "discord.com";

    log_line("tlstest: подключаюсь к %s:443", host);

    tls_stream s;
    if (!tlsnet::connect(&s, host, 443, 0, 15000))
    {
        log_line("tlstest: не вышло - %s", tlsnet::last_error(&s));
        return false;
    }

    log_line("tlstest: рукопожатие прошло, сертификат принят");

    char request[512];
    int n = cnprint(request, sizeof(request),
                    "GET /api/v9/gateway HTTP/1.1\r\n"
                    "Host: %s\r\n"
                    "User-Agent: IMDiscord/1.0\r\n"
                    "Accept: */*\r\n"
                    "Connection: close\r\n"
                    "\r\n", host);

    if (tlsnet::write(&s, request, n) != n)
    {
        log_line("tlstest: запрос не ушёл");
        tlsnet::close(&s);
        return false;
    }

    char reply[4096];
    int filled = 0;
    for (;;)
    {
        int got = tlsnet::read(&s, reply + filled, (int)sizeof(reply) - 1 - filled, 8000);
        if (got <= 0) break;
        filled += got;
        if (filled >= (int)sizeof(reply) - 1) break;
    }
    reply[filled] = 0;

    tlsnet::close(&s);

    if (filled <= 0)
    {
        log_line("tlstest: ответа не было");
        return false;
    }

    // The status line, and then whatever the body turned out to be.
    char status[128];
    int k = 0;
    while (reply[k] && reply[k] != '\r' && reply[k] != '\n' && k < 127) { status[k] = reply[k]; k++; }
    status[k] = 0;

    log_line("tlstest: %s (всего %d байт)", status, filled);

    const char* body = reply;
    for (int i = 3; i < filled; i++)
    {
        if (reply[i - 3] == '\r' && reply[i - 2] == '\n' &&
            reply[i - 1] == '\r' && reply[i] == '\n')
        {
            body = reply + i + 1;
            break;
        }
    }
    log_line("tlstest: тело начинается с %.80s", body);

    return status[9] == '2';
}
