#include "pch.h"
#include "proxy.h"
#include "core/log.h"
#include "core/crypto.h"

namespace
{
    const int MAX_SHIMS = 4;
    const int PUMP_BUFFER = 16 * 1024;

    // One loopback listener standing in for a socks proxy. Started the first
    // time an account needs it and kept for the life of the process: a
    // gateway socket can be parked on one for hours during a call held across
    // an account switch, and closing the listener under it would drop it.
    struct shim
    {
        proxy_config cfg;
        SOCKET listener;
        unsigned short port;
        char endpoint[64];
        HANDLE thread;
        volatile long running;
        bool used;
    };

    shim g_shims[MAX_SHIMS];
    CRITICAL_SECTION g_lock;
    bool g_ready = false;

    bool same_config(const proxy_config* a, const proxy_config* b)
    {
        if (a->kind != b->kind || a->port != b->port) return false;
        if (ccscmp(a->host, b->host) != 0) return false;
        if (ccscmp(a->user, b->user) != 0) return false;
        return ccscmp(a->pass, b->pass) == 0;
    }

    // ---- small socket helpers -------------------------------------------

    bool recv_exact(SOCKET s, unsigned char* out, int len)
    {
        int done = 0;
        while (done < len)
        {
            int got = recv(s, (char*)out + done, len - done, 0);
            if (got <= 0) return false;
            done += got;
        }
        return true;
    }

    bool send_all(SOCKET s, const unsigned char* data, int len)
    {
        int done = 0;
        while (done < len)
        {
            int put = send(s, (const char*)data + done, len - done, 0);
            if (put <= 0) return false;
            done += put;
        }
        return true;
    }

    // Resolves a host to one IPv4 address. Everything here needs a sockaddr,
    // and discord's edge is reachable over v4 everywhere this runs.
    bool resolve_v4(const char* host, unsigned short port, sockaddr_in* out)
    {
        ccfset(out, 0, sizeof(*out));
        out->sin_family = AF_INET;
        out->sin_port = htons(port);

        unsigned long literal = inet_addr(host);
        if (literal != INADDR_NONE)
        {
            out->sin_addr.s_addr = literal;
            return true;
        }

        char service[16];
        cnprint(service, sizeof(service), "%u", (unsigned int)port);

        addrinfo hints;
        ccfset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        addrinfo* result = 0;
        if (getaddrinfo(host, service, &hints, &result) != 0 || !result) return false;

        ccpy(out, result->ai_addr, sizeof(sockaddr_in));
        out->sin_port = htons(port);
        freeaddrinfo(result);
        return true;
    }

    SOCKET dial(const char* host, unsigned short port)
    {
        sockaddr_in addr;
        if (!resolve_v4(host, port, &addr)) return INVALID_SOCKET;

        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) return INVALID_SOCKET;

        if (connect(s, (sockaddr*)&addr, sizeof(addr)) != 0)
        {
            closesocket(s);
            return INVALID_SOCKET;
        }
        return s;
    }

    // ---- socks handshakes -----------------------------------------------

    // Greets a socks5 proxy and authenticates if it asks. Returns false on any
    // refusal; the caller closes the socket.
    bool socks5_greet(SOCKET s, const proxy_config* cfg)
    {
        bool have_login = cfg->user[0] != 0;

        unsigned char hello[4];
        int n = 0;
        hello[n++] = 0x05;
        hello[n++] = have_login ? 2 : 1;
        hello[n++] = 0x00;                       // no authentication
        if (have_login) hello[n++] = 0x02;       // username / password

        if (!send_all(s, hello, n)) return false;

        unsigned char reply[2];
        if (!recv_exact(s, reply, 2) || reply[0] != 0x05) return false;

        if (reply[1] == 0x00) return true;
        if (reply[1] != 0x02 || !have_login) return false;

        unsigned int ulen = (unsigned int)ccslenf(cfg->user);
        unsigned int plen = (unsigned int)ccslenf(cfg->pass);
        if (ulen > 255 || plen > 255) return false;

        unsigned char auth[1 + 1 + 255 + 1 + 255];
        n = 0;
        auth[n++] = 0x01;                        // sub-negotiation version
        auth[n++] = (unsigned char)ulen;
        ccpy(auth + n, cfg->user, ulen); n += (int)ulen;
        auth[n++] = (unsigned char)plen;
        ccpy(auth + n, cfg->pass, plen); n += (int)plen;

        if (!send_all(s, auth, n)) return false;
        if (!recv_exact(s, reply, 2)) return false;
        return reply[1] == 0x00;
    }

    // Reads the address that follows a socks5 reply code, leaving the socket
    // positioned after it. Fills `bound` when the caller wants it.
    bool socks5_read_address(SOCKET s, unsigned char atyp, sockaddr_in* bound)
    {
        unsigned char addr[256];

        if (atyp == 0x01)
        {
            if (!recv_exact(s, addr, 4 + 2)) return false;
            if (bound)
            {
                ccfset(bound, 0, sizeof(*bound));
                bound->sin_family = AF_INET;
                ccpy(&bound->sin_addr, addr, 4);
                bound->sin_port = (unsigned short)((addr[4] << 8) | addr[5]);
                bound->sin_port = htons(bound->sin_port);
            }
            return true;
        }

        if (atyp == 0x03)
        {
            unsigned char len = 0;
            if (!recv_exact(s, &len, 1)) return false;
            return recv_exact(s, addr, (int)len + 2);
        }

        if (atyp == 0x04) return recv_exact(s, addr, 16 + 2);
        return false;
    }

    // CONNECT through socks5, naming the target as a hostname so the proxy
    // resolves it. Resolving here would leak the lookup onto the local
    // network, which is half the point of using a tunnel.
    bool socks5_connect(SOCKET s, const char* host, unsigned short port)
    {
        unsigned int hlen = (unsigned int)ccslenf(host);
        if (hlen == 0 || hlen > 255) return false;

        unsigned char req[4 + 1 + 255 + 2];
        int n = 0;
        req[n++] = 0x05;
        req[n++] = 0x01;                         // connect
        req[n++] = 0x00;
        req[n++] = 0x03;                         // domain name
        req[n++] = (unsigned char)hlen;
        ccpy(req + n, host, hlen); n += (int)hlen;
        req[n++] = (unsigned char)(port >> 8);
        req[n++] = (unsigned char)(port & 0xFF);

        if (!send_all(s, req, n)) return false;

        unsigned char head[4];
        if (!recv_exact(s, head, 4)) return false;
        if (head[0] != 0x05 || head[1] != 0x00) return false;

        return socks5_read_address(s, head[3], 0);
    }

    // socks4a: the 'a' is the trailing hostname, which the 0.0.0.x address
    // tells the proxy to expect. Plain socks4 would need the name resolved on
    // this side.
    bool socks4_connect(SOCKET s, const proxy_config* cfg,
                        const char* host, unsigned short port)
    {
        unsigned int hlen = (unsigned int)ccslenf(host);
        unsigned int ulen = (unsigned int)ccslenf(cfg->user);
        if (hlen > 255 || ulen > 63) return false;

        unsigned char req[9 + 64 + 256];
        int n = 0;
        req[n++] = 0x04;
        req[n++] = 0x01;
        req[n++] = (unsigned char)(port >> 8);
        req[n++] = (unsigned char)(port & 0xFF);
        req[n++] = 0; req[n++] = 0; req[n++] = 0; req[n++] = 1;   // 0.0.0.1
        if (ulen) { ccpy(req + n, cfg->user, ulen); n += (int)ulen; }
        req[n++] = 0;
        ccpy(req + n, host, hlen); n += (int)hlen;
        req[n++] = 0;

        if (!send_all(s, req, n)) return false;

        unsigned char reply[8];
        if (!recv_exact(s, reply, 8)) return false;
        return reply[1] == 0x5A;
    }

    // ---- the loopback shim ----------------------------------------------

    struct pump_pair
    {
        SOCKET from;
        SOCKET to;
    };

    DWORD WINAPI pump_thread(LPVOID param)
    {
        pump_pair* pair = (pump_pair*)param;
        char* buffer = (char*)memalloc(PUMP_BUFFER);

        if (buffer)
        {
            for (;;)
            {
                int got = recv(pair->from, buffer, PUMP_BUFFER, 0);
                if (got <= 0) break;
                if (!send_all(pair->to, (const unsigned char*)buffer, got)) break;
            }
            memfree(buffer);
        }

        // One direction ending means the conversation is over. Half closing
        // the other side lets its reader see the end instead of hanging.
        shutdown(pair->to, SD_SEND);
        memfree(pair);
        return 0;
    }

    struct shim_client
    {
        shim* owner;
        SOCKET client;
    };

    // Reads the CONNECT line WinHTTP sends, opens the far side through socks,
    // then gets out of the way and copies bytes.
    DWORD WINAPI shim_client_thread(LPVOID param)
    {
        shim_client* job = (shim_client*)param;
        SOCKET client = job->client;
        proxy_config cfg = job->owner->cfg;
        memfree(job);

        char request[1024];
        int filled = 0;
        int header_end = -1;

        // The request ends at a blank line. Nothing after it matters until
        // the tunnel is up, and WinHTTP waits for the reply before sending.
        while (filled < (int)sizeof(request) - 1)
        {
            int got = recv(client, request + filled, (int)sizeof(request) - 1 - filled, 0);
            if (got <= 0) break;
            filled += got;
            request[filled] = 0;

            for (int i = 3; i < filled; i++)
            {
                if (request[i - 3] == '\r' && request[i - 2] == '\n' &&
                    request[i - 1] == '\r' && request[i] == '\n')
                {
                    header_end = i;
                    break;
                }
            }
            if (header_end >= 0) break;
        }

        char host[256];
        unsigned short port = 443;
        bool parsed = false;

        if (header_end >= 0 &&
            request[0] == 'C' && request[1] == 'O' && request[2] == 'N' && request[3] == 'N')
        {
            // "CONNECT host:port HTTP/1.1"
            const char* p = request + 7;
            while (*p == ' ') p++;

            int n = 0;
            while (*p && *p != ':' && *p != ' ' && n < (int)sizeof(host) - 1) host[n++] = *p++;
            host[n] = 0;

            if (*p == ':')
            {
                p++;
                unsigned int value = 0;
                while (*p >= '0' && *p <= '9') { value = value * 10 + (unsigned int)(*p - '0'); p++; }
                if (value && value < 65536) port = (unsigned short)value;
            }
            parsed = host[0] != 0;
        }

        SOCKET remote = INVALID_SOCKET;
        if (parsed)
        {
            remote = dial(cfg.host, cfg.port);
            if (remote != INVALID_SOCKET)
            {
                bool ok = false;
                if (cfg.kind == PROXY_SOCKS5)
                    ok = socks5_greet(remote, &cfg) && socks5_connect(remote, host, port);
                else if (cfg.kind == PROXY_SOCKS4)
                    ok = socks4_connect(remote, &cfg, host, port);

                if (!ok)
                {
                    log_line("proxy: %s отказал в соединении с %s:%u",
                             cfg.kind == PROXY_SOCKS5 ? "socks5" : "socks4", host, port);
                    closesocket(remote);
                    remote = INVALID_SOCKET;
                }
            }
            else
            {
                log_line("proxy: не дозвонился до %s:%u", cfg.host, cfg.port);
            }
        }

        if (remote == INVALID_SOCKET)
        {
            const char* refusal = "HTTP/1.1 502 Bad Gateway\r\n\r\n";
            send_all(client, (const unsigned char*)refusal, (int)ccslenf(refusal));
            closesocket(client);
            return 0;
        }

        log_line("proxy: прослойка ведёт %s:%u", host, (unsigned int)port);

        const char* accepted = "HTTP/1.1 200 Connection established\r\n\r\n";
        if (!send_all(client, (const unsigned char*)accepted, (int)ccslenf(accepted)))
        {
            closesocket(client);
            closesocket(remote);
            return 0;
        }

        // Anything the client sent past the blank line is already in hand.
        if (filled > header_end + 1)
            send_all(remote, (const unsigned char*)(request + header_end + 1),
                     filled - header_end - 1);

        pump_pair* up = (pump_pair*)memalloc(sizeof(pump_pair));
        if (up)
        {
            up->from = client;
            up->to = remote;
            HANDLE t = CreateThread(0, 0, pump_thread, up, 0, 0);
            if (t) CloseHandle(t);
            else memfree(up);
        }

        // The other direction runs here rather than on a third thread.
        char* buffer = (char*)memalloc(PUMP_BUFFER);
        if (buffer)
        {
            for (;;)
            {
                int got = recv(remote, buffer, PUMP_BUFFER, 0);
                if (got <= 0) break;
                if (!send_all(client, (const unsigned char*)buffer, got)) break;
            }
            memfree(buffer);
        }

        shutdown(client, SD_SEND);
        closesocket(remote);
        closesocket(client);
        return 0;
    }

    DWORD WINAPI shim_listen_thread(LPVOID param)
    {
        shim* sh = (shim*)param;

        while (sh->running)
        {
            sockaddr_in from;
            int from_len = (int)sizeof(from);
            SOCKET client = accept(sh->listener, (sockaddr*)&from, &from_len);
            if (client == INVALID_SOCKET) break;

            shim_client* job = (shim_client*)memalloc(sizeof(shim_client));
            if (!job) { closesocket(client); continue; }

            job->owner = sh;
            job->client = client;

            HANDLE t = CreateThread(0, 0, shim_client_thread, job, 0, 0);
            if (t) CloseHandle(t);
            else { memfree(job); closesocket(client); }
        }
        return 0;
    }

    shim* start_shim(const proxy_config* cfg)
    {
        shim* sh = 0;
        for (int i = 0; i < MAX_SHIMS; i++)
            if (!g_shims[i].used) { sh = &g_shims[i]; break; }

        if (!sh)
        {
            log_line("proxy: слишком много разных прокси одновременно");
            return 0;
        }

        SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener == INVALID_SOCKET) return 0;

        // Loopback only. This listener speaks no authentication, so anything
        // that could reach it could use the tunnel.
        sockaddr_in addr;
        ccfset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;                       // any free port

        if (bind(listener, (sockaddr*)&addr, sizeof(addr)) != 0 ||
            listen(listener, 16) != 0)
        {
            closesocket(listener);
            return 0;
        }

        int addr_len = (int)sizeof(addr);
        if (getsockname(listener, (sockaddr*)&addr, &addr_len) != 0)
        {
            closesocket(listener);
            return 0;
        }

        ccfset(sh, 0, sizeof(*sh));
        sh->cfg = *cfg;
        sh->listener = listener;
        sh->port = ntohs(addr.sin_port);
        sh->running = 1;
        sh->used = true;
        cnprint(sh->endpoint, sizeof(sh->endpoint), "127.0.0.1:%u", (unsigned int)sh->port);

        sh->thread = CreateThread(0, 0, shim_listen_thread, sh, 0, 0);
        if (!sh->thread)
        {
            closesocket(listener);
            sh->used = false;
            return 0;
        }

        log_line("proxy: %s %s:%u доступен через %s",
                 cfg->kind == PROXY_SOCKS5 ? "socks5" : "socks4",
                 cfg->host, (unsigned int)cfg->port, sh->endpoint);
        return sh;
    }
}

void proxy::init()
{
    if (g_ready) return;
    InitializeCriticalSection(&g_lock);
    ccfset(g_shims, 0, sizeof(g_shims));
    g_ready = true;
}

void proxy::shutdown()
{
    if (!g_ready) return;

    EnterCriticalSection(&g_lock);
    for (int i = 0; i < MAX_SHIMS; i++)
    {
        shim* sh = &g_shims[i];
        if (!sh->used) continue;

        InterlockedExchange(&sh->running, 0);
        closesocket(sh->listener);
        sh->listener = INVALID_SOCKET;

        if (sh->thread)
        {
            WaitForSingleObject(sh->thread, 1000);
            CloseHandle(sh->thread);
            sh->thread = 0;
        }
        sh->used = false;
    }
    LeaveCriticalSection(&g_lock);

    DeleteCriticalSection(&g_lock);
    g_ready = false;
}

const char* proxy::endpoint_for(const proxy_config* cfg)
{
    if (!cfg || !cfg->in_use()) return 0;

    // An http proxy needs no help: WinHTTP has spoken CONNECT since it was
    // written, and letting it do so keeps proxy authentication working too.
    static char direct[192];
    if (cfg->kind == PROXY_HTTPS)
    {
        cnprint(direct, sizeof(direct), "%s:%u", cfg->host, (unsigned int)cfg->port);
        return direct;
    }

    if (!g_ready) init();

    EnterCriticalSection(&g_lock);

    shim* found = 0;
    for (int i = 0; i < MAX_SHIMS && !found; i++)
        if (g_shims[i].used && same_config(&g_shims[i].cfg, cfg)) found = &g_shims[i];

    if (!found) found = start_shim(cfg);

    const char* result = found ? found->endpoint : 0;
    LeaveCriticalSection(&g_lock);
    return result;
}

bool proxy::dial_through(const proxy_config* cfg, const char* host, unsigned short port,
                         SOCKET* out, const char** why)
{
    if (why) *why = "";
    if (!out || !host || !host[0]) { if (why) *why = "некуда подключаться"; return false; }

    *out = INVALID_SOCKET;

    if (!cfg || !cfg->in_use())
    {
        SOCKET s = dial(host, port);
        if (s == INVALID_SOCKET) { if (why) *why = "сервер не отвечает"; return false; }
        *out = s;
        return true;
    }

    SOCKET s = dial(cfg->host, cfg->port);
    if (s == INVALID_SOCKET) { if (why) *why = "прокси не отвечает"; return false; }

    bool ok = false;

    if (cfg->kind == PROXY_SOCKS5)
    {
        ok = socks5_greet(s, cfg) && socks5_connect(s, host, port);
        if (!ok && why) *why = "socks5 отказал";
    }
    else if (cfg->kind == PROXY_SOCKS4)
    {
        ok = socks4_connect(s, cfg, host, port);
        if (!ok && why) *why = "socks4 отказал";
    }
    else if (cfg->kind == PROXY_HTTPS)
    {
        // Straight CONNECT, which is all an http proxy offers and all that is
        // wanted: what follows is a TLS handshake it never sees inside.
        char request[512];
        int n = cnprint(request, sizeof(request),
                        "CONNECT %s:%u HTTP/1.1\r\nHost: %s:%u\r\n",
                        host, (unsigned int)port, host, (unsigned int)port);

        if (cfg->user[0])
        {
            char creds[160];
            cnprint(creds, sizeof(creds), "%s:%s", cfg->user, cfg->pass);

            ubuffer encoded;
            encoded.init(256);
            crypto::base64_encode((const unsigned char*)creds, (unsigned int)ccslenf(creds),
                                  &encoded);

            n += cnprint(request + n, (int)sizeof(request) - n,
                         "Proxy-Authorization: Basic %.*s\r\n",
                         (int)encoded.size, (const char*)encoded.data);
            encoded.free_buffer();
            ccfset(creds, 0, sizeof(creds));
        }

        n += cnprint(request + n, (int)sizeof(request) - n, "\r\n");

        if (send_all(s, (const unsigned char*)request, n))
        {
            // Only the status line matters, and it arrives well inside one
            // read in every implementation worth talking to.
            char reply[512];
            int got = recv(s, reply, (int)sizeof(reply) - 1, 0);
            if (got > 12)
            {
                reply[got] = 0;
                ok = reply[9] == '2' && reply[10] == '0' && reply[11] == '0';
            }
        }
        if (!ok && why) *why = "http-прокси отказал в CONNECT";
    }

    if (!ok)
    {
        closesocket(s);
        return false;
    }

    *out = s;
    return true;
}

const char* proxy::voice_blocked_reason(const proxy_config* cfg)
{
    if (!cfg || !cfg->in_use()) return 0;
    if (cfg->kind == PROXY_HTTPS) return "HTTPS-прокси не пропускает UDP, звонки недоступны";
    if (cfg->kind == PROXY_SOCKS4) return "SOCKS4 не умеет UDP, звонки недоступны";
    return 0;
}

// ---------------------------------------------------------------------------
// udp
// ---------------------------------------------------------------------------

bool proxy::open_udp(udp_route* route, SOCKET s, const sockaddr_in* peer,
                     const proxy_config* cfg, const char** why)
{
    ccfset(route, 0, sizeof(*route));
    route->control = INVALID_SOCKET;
    route->data = s;
    route->peer = *peer;
    if (why) *why = "";

    if (!cfg || !cfg->in_use() || !cfg->carries_udp())
    {
        // No proxy, or one that cannot carry datagrams. Straight out.
        route->active = false;
        return connect(s, (const sockaddr*)peer, sizeof(*peer)) == 0;
    }

    SOCKET control = dial(cfg->host, cfg->port);
    if (control == INVALID_SOCKET)
    {
        if (why) *why = "прокси не отвечает";
        return false;
    }

    if (!socks5_greet(control, cfg))
    {
        closesocket(control);
        if (why) *why = "прокси не принял вход";
        return false;
    }

    // UDP ASSOCIATE. The address given here is the one this client will send
    // datagrams from; zeroes mean "work it out", which is what a machine
    // behind NAT has to say.
    unsigned char req[10];
    req[0] = 0x05;
    req[1] = 0x03;                    // udp associate
    req[2] = 0x00;
    req[3] = 0x01;                    // ipv4
    ccfset(req + 4, 0, 4);
    req[8] = 0; req[9] = 0;

    if (!send_all(control, req, 10))
    {
        closesocket(control);
        if (why) *why = "прокси оборвал связь";
        return false;
    }

    unsigned char head[4];
    if (!recv_exact(control, head, 4) || head[0] != 0x05)
    {
        closesocket(control);
        if (why) *why = "прокси ответил непонятно";
        return false;
    }

    if (head[1] != 0x00)
    {
        closesocket(control);
        // 0x07 is "command not supported", which is what a proxy without udp
        // says. Worth naming, because it is the common case and it is not a
        // misconfiguration on this side.
        if (why) *why = head[1] == 0x07 ? "прокси не поддерживает UDP"
                                        : "прокси отказал в UDP";
        return false;
    }

    sockaddr_in relay;
    if (!socks5_read_address(control, head[3], &relay))
    {
        closesocket(control);
        if (why) *why = "прокси не назвал адрес для UDP";
        return false;
    }

    // A proxy is allowed to answer with an unspecified address, meaning "the
    // one you are already talking to".
    if (relay.sin_addr.s_addr == 0)
    {
        sockaddr_in via;
        int len = (int)sizeof(via);
        if (getpeername(control, (sockaddr*)&via, &len) == 0)
            relay.sin_addr = via.sin_addr;
    }

    // The data socket talks only to the relay from here on. Connecting it
    // keeps send/recv usable and drops anything from elsewhere.
    if (connect(s, (const sockaddr*)&relay, sizeof(relay)) != 0)
    {
        closesocket(control);
        if (why) *why = "не удалось привязаться к UDP-каналу прокси";
        return false;
    }

    route->control = control;
    route->relay = relay;
    route->active = true;

    log_line("proxy: UDP через socks5, реле %u.%u.%u.%u:%u",
             (unsigned int)(relay.sin_addr.s_addr & 0xFF),
             (unsigned int)((relay.sin_addr.s_addr >> 8) & 0xFF),
             (unsigned int)((relay.sin_addr.s_addr >> 16) & 0xFF),
             (unsigned int)((relay.sin_addr.s_addr >> 24) & 0xFF),
             (unsigned int)ntohs(relay.sin_port));
    return true;
}

void proxy::close_udp(udp_route* route)
{
    if (!route) return;
    if (route->control != INVALID_SOCKET)
    {
        closesocket(route->control);
        route->control = INVALID_SOCKET;
    }
    route->active = false;
}

int proxy::udp_send(udp_route* route, const void* data, int len)
{
    if (!route->active) return send(route->data, (const char*)data, len, 0);

    // RSV(2) FRAG(1) ATYP(1) ADDR(4) PORT(2), then the datagram. One relay
    // serves every destination, so each packet names its own.
    unsigned char packet[1600 + 10];
    if (len > 1600) return -1;

    packet[0] = 0; packet[1] = 0;
    packet[2] = 0;                    // not a fragment
    packet[3] = 0x01;                 // ipv4
    ccpy(packet + 4, &route->peer.sin_addr, 4);
    ccpy(packet + 8, &route->peer.sin_port, 2);
    ccpy(packet + 10, data, (size_t)len);

    int put = send(route->data, (const char*)packet, len + 10, 0);
    if (put <= 10) return put;
    return put - 10;
}

int proxy::udp_recv(udp_route* route, void* data, int cap)
{
    if (!route->active) return recv(route->data, (char*)data, cap, 0);

    unsigned char packet[1600 + 262];
    int got = recv(route->data, (char*)packet, (int)sizeof(packet), 0);
    if (got < 10) return got <= 0 ? got : -1;

    // Fragmented datagrams are not reassembled. Media never asks for them and
    // a proxy that starts sending them is broken for this purpose anyway.
    if (packet[2] != 0) return -1;

    int header = 0;
    if (packet[3] == 0x01)      header = 10;
    else if (packet[3] == 0x04) header = 22;
    else if (packet[3] == 0x03) header = 5 + packet[4] + 2;
    else return -1;

    if (got <= header) return -1;

    int payload = got - header;
    if (payload > cap) payload = cap;
    ccpy(data, packet + header, (size_t)payload);
    return payload;
}

// ---------------------------------------------------------------------------
// parsing and diagnostics
// ---------------------------------------------------------------------------

bool proxy::parse_url(const char* text, proxy_config* out)
{
    if (!text || !out) return false;
    ccfset(out, 0, sizeof(*out));

    const char* p = text;
    while (*p == ' ') p++;

    // scheme
    out->kind = PROXY_SOCKS5;
    const char* scheme_end = 0;
    for (const char* q = p; *q; q++)
    {
        if (q[0] == ':' && q[1] == '/' && q[2] == '/') { scheme_end = q; break; }
        if (*q == '@' || *q == '/') break;
    }

    if (scheme_end)
    {
        char scheme[16];
        int n = 0;
        for (const char* q = p; q < scheme_end && n < 15; q++)
            scheme[n++] = (char)cctolower(*q);
        scheme[n] = 0;

        if (ccscmp(scheme, "socks5") == 0 || ccscmp(scheme, "socks5h") == 0) out->kind = PROXY_SOCKS5;
        else if (ccscmp(scheme, "socks4") == 0 || ccscmp(scheme, "socks4a") == 0) out->kind = PROXY_SOCKS4;
        else if (ccscmp(scheme, "http") == 0 || ccscmp(scheme, "https") == 0) out->kind = PROXY_HTTPS;
        else return false;

        p = scheme_end + 3;
    }

    // credentials, if there is an @ before any slash
    const char* at = 0;
    for (const char* q = p; *q && *q != '/'; q++)
        if (*q == '@') at = q;

    if (at)
    {
        char creds[160];
        int n = 0;
        for (const char* q = p; q < at && n < 159; q++) creds[n++] = *q;
        creds[n] = 0;

        char* colon = 0;
        for (char* q = creds; *q; q++) if (*q == ':') { colon = q; break; }

        if (colon)
        {
            *colon = 0;
            ccstrncpy(out->user, creds, sizeof(out->user) - 1);
            ccstrncpy(out->pass, colon + 1, sizeof(out->pass) - 1);
        }
        else
        {
            ccstrncpy(out->user, creds, sizeof(out->user) - 1);
        }
        p = at + 1;
    }

    // host and port
    int n = 0;
    while (*p && *p != ':' && *p != '/' && n < (int)sizeof(out->host) - 1)
        out->host[n++] = *p++;
    out->host[n] = 0;

    if (*p == ':')
    {
        p++;
        unsigned int value = 0;
        while (*p >= '0' && *p <= '9') { value = value * 10 + (unsigned int)(*p - '0'); p++; }
        if (value && value < 65536) out->port = (unsigned short)value;
    }

    if (!out->host[0] || !out->port) return false;
    return true;
}

bool proxy::self_test(const proxy_config* cfg)
{
    if (!cfg || !cfg->in_use())
    {
        log_line("proxytest: конфигурация пустая");
        return false;
    }

    const char* names[] = { "нет", "socks5", "socks4", "https" };
    log_line("proxytest: %s %s:%u, логин %s",
             names[cfg->kind & 3], cfg->host, (unsigned int)cfg->port,
             cfg->user[0] ? cfg->user : "нет");

    bool all_ok = true;

    // ---- step one: reach the proxy at all
    SOCKET s = dial(cfg->host, cfg->port);
    if (s == INVALID_SOCKET)
    {
        log_line("proxytest: 1/4 не дозвонился до прокси (%d)", WSAGetLastError());
        return false;
    }
    log_line("proxytest: 1/4 tcp до прокси есть");

    if (cfg->kind == PROXY_SOCKS5)
    {
        if (!socks5_greet(s, cfg))
        {
            log_line("proxytest: 2/4 socks5 не принял вход");
            closesocket(s);
            return false;
        }
        log_line("proxytest: 2/4 socks5 вход принят");

        if (!socks5_connect(s, "discord.com", 443))
        {
            log_line("proxytest: 3/4 CONNECT к discord.com:443 отклонён");
            all_ok = false;
        }
        else
        {
            log_line("proxytest: 3/4 CONNECT к discord.com:443 прошёл");
        }
    }
    else if (cfg->kind == PROXY_SOCKS4)
    {
        if (!socks4_connect(s, cfg, "discord.com", 443))
        {
            log_line("proxytest: 2/4 socks4 отклонил CONNECT");
            all_ok = false;
        }
        else
        {
            log_line("proxytest: 2/4 socks4 CONNECT прошёл");
        }
    }
    closesocket(s);

    // ---- step two: the loopback shim WinHTTP will be pointed at
    const char* endpoint = proxy::endpoint_for(cfg);
    log_line("proxytest: 4/4 точка для WinHTTP - %s", endpoint ? endpoint : "нет");

    if (endpoint && cfg->kind != PROXY_HTTPS)
    {
        // Talk to the shim exactly as WinHTTP would.
        char shim_host[64];
        unsigned short shim_port = 0;
        {
            int n = 0;
            const char* q = endpoint;
            while (*q && *q != ':' && n < 63) shim_host[n++] = *q++;
            shim_host[n] = 0;
            if (*q == ':') { q++; unsigned int v = 0;
                while (*q >= '0' && *q <= '9') { v = v * 10 + (unsigned int)(*q - '0'); q++; }
                shim_port = (unsigned short)v; }
        }

        SOCKET c = dial(shim_host, shim_port);
        if (c == INVALID_SOCKET)
        {
            log_line("proxytest: прослойка не отвечает");
            all_ok = false;
        }
        else
        {
            const char* req = "CONNECT discord.com:443 HTTP/1.1\r\nHost: discord.com:443\r\n\r\n";
            send_all(c, (const unsigned char*)req, (int)ccslenf(req));

            char reply[128];
            int got = recv(c, reply, (int)sizeof(reply) - 1, 0);
            if (got > 0)
            {
                reply[got] = 0;
                for (int i = 0; i < got; i++) if (reply[i] == '\r' || reply[i] == '\n') { reply[i] = 0; break; }
                log_line("proxytest: прослойка ответила \"%s\"", reply);
                if (!(reply[9] == '2' && reply[10] == '0' && reply[11] == '0')) all_ok = false;
            }
            else
            {
                log_line("proxytest: прослойка ничего не ответила");
                all_ok = false;
            }
            closesocket(c);
        }
    }

    // ---- step three: udp, which is what decides whether calls work
    if (cfg->kind == PROXY_SOCKS5)
    {
        SOCKET u = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        sockaddr_in fake;
        ccfset(&fake, 0, sizeof(fake));
        fake.sin_family = AF_INET;
        fake.sin_port = htons(443);
        fake.sin_addr.s_addr = inet_addr("162.159.128.233");

        proxy::udp_route route;
        const char* why = "";
        if (proxy::open_udp(&route, u, &fake, cfg, &why))
        {
            log_line("proxytest: UDP ASSOCIATE поднялся - звонки возможны");
            proxy::close_udp(&route);
        }
        else
        {
            log_line("proxytest: UDP ASSOCIATE не поднялся (%s) - звонков не будет", why);
            all_ok = false;
        }
        closesocket(u);
    }

    log_line("proxytest: итог %s", all_ok ? "всё в порядке" : "ЕСТЬ ПРОБЛЕМЫ");
    return all_ok;
}

// ---------------------------------------------------------------------------
// the check button
// ---------------------------------------------------------------------------

namespace
{
    volatile long g_checking = 0;
    char g_check_text[192] = { 0 };
    volatile long g_check_ok = 0;

    struct check_job
    {
        proxy_config cfg;
    };

    void set_check_text(const char* text, bool ok)
    {
        ccfset(g_check_text, 0, sizeof(g_check_text));
        ccstrncpy(g_check_text, text, sizeof(g_check_text) - 1);
        InterlockedExchange(&g_check_ok, ok ? 1 : 0);
    }

    DWORD WINAPI check_thread(LPVOID param)
    {
        check_job* job = (check_job*)param;

        SOCKET s = dial(job->cfg.host, job->cfg.port);
        if (s == INVALID_SOCKET)
        {
            set_check_text("Прокси не отвечает", false);
            memfree(job);
            InterlockedExchange(&g_checking, 0);
            return 0;
        }

        if (job->cfg.kind == PROXY_HTTPS)
        {
            // Nothing to negotiate: reaching it is as much as can be checked
            // without making a real request.
            closesocket(s);
            set_check_text("Прокси отвечает. Звонки через HTTPS невозможны", true);
            memfree(job);
            InterlockedExchange(&g_checking, 0);
            return 0;
        }

        bool reached = false;
        if (job->cfg.kind == PROXY_SOCKS5)
            reached = socks5_greet(s, &job->cfg) && socks5_connect(s, "discord.com", 443);
        else
            reached = socks4_connect(s, &job->cfg, "discord.com", 443);
        closesocket(s);

        if (!reached)
        {
            set_check_text(job->cfg.kind == PROXY_SOCKS5
                               ? "Прокси не пустил: логин или пароль неверны"
                               : "SOCKS4 отклонил соединение",
                           false);
            memfree(job);
            InterlockedExchange(&g_checking, 0);
            return 0;
        }

        if (job->cfg.kind != PROXY_SOCKS5)
        {
            set_check_text("Discord доступен. Звонки через SOCKS4 невозможны", true);
            memfree(job);
            InterlockedExchange(&g_checking, 0);
            return 0;
        }

        // Whether calls will work is the one thing worth knowing in advance,
        // because it is the thing this client cannot work around.
        SOCKET u = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        sockaddr_in probe;
        ccfset(&probe, 0, sizeof(probe));
        probe.sin_family = AF_INET;
        probe.sin_port = htons(443);
        probe.sin_addr.s_addr = inet_addr("162.159.128.233");

        proxy::udp_route route;
        const char* why = "";
        bool udp = proxy::open_udp(&route, u, &probe, &job->cfg, &why);
        proxy::close_udp(&route);
        if (u != INVALID_SOCKET) closesocket(u);

        if (udp) set_check_text("Discord доступен, звонки тоже", true);
        else
        {
            char text[192];
            cnprint(text, sizeof(text), "Discord доступен, но звонков не будет: %s", why);
            set_check_text(text, true);
        }

        memfree(job);
        InterlockedExchange(&g_checking, 0);
        return 0;
    }
}

void proxy::begin_check(const proxy_config* cfg)
{
    if (!cfg || !cfg->in_use())
    {
        set_check_text("Заполни адрес и порт", false);
        return;
    }
    if (InterlockedCompareExchange(&g_checking, 1, 0) != 0) return;

    check_job* job = (check_job*)memalloc(sizeof(check_job));
    if (!job) { InterlockedExchange(&g_checking, 0); return; }
    job->cfg = *cfg;

    set_check_text("Проверяю...", true);

    HANDLE t = CreateThread(0, 0, check_thread, job, 0, 0);
    if (t) CloseHandle(t);
    else { memfree(job); InterlockedExchange(&g_checking, 0); }
}

bool proxy::checking() { return g_checking != 0; }
const char* proxy::check_result() { return g_check_text; }
bool proxy::check_passed() { return g_check_ok != 0; }
