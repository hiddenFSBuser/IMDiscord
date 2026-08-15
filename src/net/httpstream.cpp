#include "pch.h"
#include "httpstream.h"
#include "proxy.h"
#include "core/log.h"

namespace
{
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

bool httpstream::connect(http_stream* s, const char* host, unsigned short port, bool secure,
                         const proxy_config* proxy, unsigned int timeout_ms)
{
    ccfset(s, 0, sizeof(*s));
    s->plain = INVALID_SOCKET;
    s->secure = secure;

    if (secure)
    {
        if (!tlsnet::connect(&s->tls, host, port, proxy, timeout_ms))
        {
            ccstrncpy(s->error, tlsnet::last_error(&s->tls), sizeof(s->error) - 1);
            return false;
        }
        return true;
    }

    const char* why = "";
    SOCKET sock = INVALID_SOCKET;
    if (!proxy::dial_through(proxy, host, port, &sock, &why))
    {
        ccstrncpy(s->error, why[0] ? why : "сервер не отвечает", sizeof(s->error) - 1);
        return false;
    }

    s->plain = sock;
    return true;
}

void httpstream::close(http_stream* s)
{
    if (!s) return;

    if (s->secure) tlsnet::close(&s->tls);
    else if (s->plain != INVALID_SOCKET) { closesocket(s->plain); s->plain = INVALID_SOCKET; }
}

int httpstream::read(http_stream* s, void* out, int cap, unsigned int timeout_ms)
{
    if (!s) return -1;

    if (s->secure) return tlsnet::read(&s->tls, out, cap, timeout_ms);

    if (s->plain == INVALID_SOCKET) return -1;
    if (!wait_readable(s->plain, timeout_ms)) return -2;

    int got = recv(s->plain, (char*)out, cap, 0);
    if (got == 0) return 0;
    return got < 0 ? -1 : got;
}

bool httpstream::write_all(http_stream* s, const void* data, int len)
{
    if (!s || len <= 0) return false;

    if (s->secure) return tlsnet::write(&s->tls, data, len) == len;

    if (s->plain == INVALID_SOCKET) return false;

    int done = 0;
    while (done < len)
    {
        int put = send(s->plain, (const char*)data + done, len - done, 0);
        if (put <= 0) return false;
        done += put;
    }
    return true;
}

const char* httpstream::last_error(http_stream* s) { return s ? s->error : ""; }
