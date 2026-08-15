#pragma once
#include "ubuffer.h"
#include "httpstream.h"

// A websocket written against RFC 6455 directly, over a socket this process
// opens itself.
//
// It used to be WinHTTP's, which was less code but put a floor under the whole
// client: WinHTTP only learned websockets in Windows 8.1, so everything here
// stopped at an operating system boundary that had nothing to do with what a
// chat client actually needs. The framing is a couple of hundred lines and the
// floor is gone.
//
// One send and one receive may be in flight at the same time, which is exactly
// what a gateway needs: a reader loop plus a heartbeat timer on another thread.

namespace ws
{
    // Opens the real gateway and waits for its first message, which arrives
    // without any credentials. Run from --wstest: it exercises the handshake,
    // the accept key and the framing in one go.
    bool self_test();
}

enum ws_result
{
    WS_ERROR = -1,
    WS_CLOSED = 0,
    WS_MESSAGE = 1,
};

struct proxy_config;

struct websocket
{
    http_stream stream;

    CRITICAL_SECTION send_lock;
    volatile long open;
    bool lock_ready;
    unsigned short close_status;

    // Bytes read from the socket but not yet consumed as frames. One read can
    // land halfway through a frame or carry three of them.
    ubuffer inbox;
    unsigned int inbox_at;

    void init();

    // extra_headers is a "Name: Value\r\n"-joined block, or null.
    //
    // The proxy is passed in rather than read from a global because two of
    // these can be open at once, on different accounts, with different routes
    // out - which is exactly what happens while a call is held across an
    // account switch.
    bool connect(const char* url, const char* extra_headers,
                 const proxy_config* proxy = 0);

    bool send_text(const void* data, unsigned int len);
    bool send_binary(const void* data, unsigned int len);

    // Blocks until a whole message arrives. out is cleared first.
    ws_result receive(ubuffer* out, bool* out_is_binary);

    void close();
    // Drops the connection but keeps the object reusable.
    void close_handles();
    void destroy();

    bool is_open() const { return open != 0; }

private:
    // Pulls more from the socket into `inbox`, compacting what has already
    // been consumed. False when the connection ended or errored.
    bool refill();
    void consume(unsigned int bytes);
};
