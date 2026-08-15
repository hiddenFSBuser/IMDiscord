#pragma once
#include "tlsconn.h"

// One connection to a server, encrypted or not.
//
// Everything above this stops caring which it is. Discord is https throughout,
// but an attachment or a picture can be served over plain http, and the two
// differ only in whether the bytes go through tlse on the way.

struct http_stream
{
    bool secure;
    tls_stream tls;
    SOCKET plain;

    char error[192];

    bool open() const { return secure ? tls.established : plain != INVALID_SOCKET; }
};

namespace httpstream
{
    bool connect(http_stream* s, const char* host, unsigned short port, bool secure,
                 const proxy_config* proxy, unsigned int timeout_ms);
    void close(http_stream* s);

    // Same contract as the tls layer: bytes moved, 0 when the peer closed,
    // -1 on error, -2 on timeout.
    int read(http_stream* s, void* out, int cap, unsigned int timeout_ms);
    bool write_all(http_stream* s, const void* data, int len);

    const char* last_error(http_stream* s);
}
