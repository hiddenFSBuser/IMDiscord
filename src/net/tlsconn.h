#pragma once

// A TLS connection over a socket this client opened itself.
//
// The point of it is Windows 7 and 8. Everything here used to go through
// WinHTTP, whose websocket half only exists from 8.1 onwards, so the whole
// client stopped at an operating system boundary that has nothing to do with
// what it actually needs. A socket, a TLS library and a few hundred lines of
// HTTP is the same capability without the boundary.
//
// The library underneath is tlse, which does the handshake and the record
// layer and leaves the moving of bytes to the caller. That is exactly the
// shape wanted here: the socket may be a plain one, or one already tunnelled
// through a proxy, and TLS neither knows nor cares.

struct proxy_config;

struct tls_stream
{
    SOCKET sock;
    void* ctx;                 // TLSContext, kept opaque so tlse stays in one file

    bool established;
    char host[256];
    char error[192];

    // Bytes decrypted but not yet handed to the caller live inside tlse; this
    // is only about whether the socket has been seen to end.
    bool closed;
};

namespace tlsnet
{
    // Loads the trust store. Called once; safe to call again.
    void init();

    // Opens a socket, tunnels it if a proxy is configured, and runs the TLS
    // handshake with the name checked against the certificate.
    bool connect(tls_stream* s, const char* host, unsigned short port,
                 const proxy_config* proxy, unsigned int timeout_ms);

    void close(tls_stream* s);

    // Both return the number of bytes moved, 0 when the peer closed cleanly,
    // and -1 on error. read blocks until something arrives or the timeout
    // passes; a timeout is reported as -2 so a caller can tell it apart.
    int read(tls_stream* s, void* out, int cap, unsigned int timeout_ms);
    int write(tls_stream* s, const void* data, int len);

    const char* last_error(tls_stream* s);

    // Opens a connection to discord and asks it one question, reporting each
    // step. Run from --tlstest, before anything is built on top of this.
    bool self_test(const char* host);
}
