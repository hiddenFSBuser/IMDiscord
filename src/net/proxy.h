#pragma once

// Sending this client's traffic through somebody else's machine.
//
// The setting belongs to an account, not to the application. Somebody who
// keeps one account on their own address and another behind a tunnel needs
// both at once - and needs them to stay right through an account switch that
// keeps a call alive, where two connections with two different proxies are
// open at the same moment.
//
// Three kinds, and they are not equivalent:
//
//   socks5  carries TCP, and carries UDP as well if the proxy implements the
//           UDP ASSOCIATE command. Many do not, so voice is attempted and
//           reported honestly rather than promised.
//   socks4  TCP only. The protocol has no UDP at all, so calls are refused.
//   https   TCP only, through the CONNECT verb. Same story - no calls.
//
// WinHTTP, which carries every request and every websocket here, speaks to an
// http proxy natively and knows nothing about socks. Rather than rewrite the
// transport, a socks proxy is reached through a loopback shim: WinHTTP is
// pointed at a local listener that speaks CONNECT, and that listener opens the
// real connection through socks and copies bytes between the two. TLS is
// untouched - it is still negotiated end to end with discord, and the shim
// only ever sees ciphertext.

enum proxy_kind
{
    PROXY_NONE = 0,
    PROXY_SOCKS5,
    PROXY_SOCKS4,
    PROXY_HTTPS,
};

struct proxy_config
{
    int kind;
    char host[128];
    unsigned short port;
    char user[64];
    char pass[64];

    bool in_use() const { return kind != PROXY_NONE && host[0] && port; }

    // Whether a voice call can run over it. Media is UDP, and only socks5 has
    // any way to carry that.
    bool carries_udp() const { return kind == PROXY_SOCKS5; }
};

namespace proxy
{
    void init();

    // Parses "socks5://user:pass@host:port". Also accepts socks4:// and
    // https://, and a bare "host:port" which is taken as socks5. Written
    // because typing five fields into five boxes to try a proxy somebody sent
    // you as one line is tedious and easy to get wrong.
    bool parse_url(const char* text, proxy_config* out);

    // Opens a connection through the proxy the same way the shim does, and
    // reports each step. Run from --proxytest.
    bool self_test(const proxy_config* cfg);

    // The same walk, run on a worker so a dead proxy does not freeze the
    // window for thirty seconds. One at a time; a second call while one is
    // running is ignored.
    void begin_check(const proxy_config* cfg);
    bool checking();

    // Result of the last check, worded for a person. Empty before the first.
    const char* check_result();
    bool check_passed();

    void shutdown();
    // The address to hand WinHTTP for this configuration, as "host:port".
    // Returns null when the traffic should go out directly.
    //
    // For socks this starts (or reuses) a loopback shim and returns its
    // address; the shim lives until shutdown, because tearing one down while
    // a gateway socket is still using it would drop the connection.
    const char* endpoint_for(const proxy_config* cfg);

    // Why a call cannot be placed over this proxy, or null when it can.
    const char* voice_blocked_reason(const proxy_config* cfg);

    // Opens a tcp connection to host:port, through the proxy when there is
    // one. This is what the client's own transport uses; the loopback shim
    // above exists only for WinHTTP, which cannot be told about socks.
    bool dial_through(const proxy_config* cfg, const char* host, unsigned short port,
                      SOCKET* out, const char** why);

    // ---- udp ----
    //
    // A datagram socket that reaches the outside through socks5 UDP
    // ASSOCIATE. The control connection has to stay open for as long as the
    // association is wanted, so it is held here rather than by the caller.

    struct udp_route
    {
        SOCKET control;          // the tcp side that keeps the association alive
        SOCKET data;             // what the caller sends and receives on
        sockaddr_in relay;       // where datagrams are handed to the proxy
        bool active;             // false means `data` is an ordinary socket

        // The peer the caller thinks it is connected to. Datagrams carry it in
        // the socks header, since one relay serves every destination.
        sockaddr_in peer;
    };

    // Prepares `route` for talking to peer. With no proxy, or one that cannot
    // carry udp, this connects the socket directly and reports success - the
    // decision to refuse a call belongs to the voice layer, not to here.
    bool open_udp(udp_route* route, SOCKET s, const sockaddr_in* peer,
                  const proxy_config* cfg, const char** why);

    void close_udp(udp_route* route);

    // Stand-ins for send/recv that add and strip the socks header when the
    // route needs one. Both behave like the originals otherwise.
    int udp_send(udp_route* route, const void* data, int len);
    int udp_recv(udp_route* route, void* data, int cap);
}
