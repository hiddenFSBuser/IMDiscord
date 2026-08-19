#pragma once
#include "ubuffer.h"
#include "proxy.h"

// Blocking HTTPS client on top of WinHTTP. WinHTTP is a plain DLL import, which
// keeps TLS out of the source tree and away from the CRT.

struct http_response
{
    int status;
    int retry_after_ms;     // parsed from a 429 body/header
    ubuffer body;
    char content_type[160];
    // Content-Length as advertised, 0 when the server did not send one. A body
    // shorter than this means the transfer was cut short.
    unsigned int expected_length;
    bool truncated;

    void init();
    void free_response();
    const char* text() { return (const char*)body.c_str(); }
    bool ok() const { return status >= 200 && status < 300; }
};

struct url_parts
{
    bool secure;
    int port;
    char host[256];
    char path[2048];
};

namespace http
{
    void init(const char* user_agent);
    void shutdown();

    bool parse_url(const char* url, url_parts* out);

    // Where requests go out through, or null for direct. Process wide on
    // purpose: every rest call and every picture download belongs to whichever
    // account is signed in at the front, and that is the one whose route they
    // should take. A held connection from another account carries its own
    // proxy on its own socket instead.
    void set_proxy(const proxy_config* cfg);

    // Cookies discord has set on this client, kept across requests.
    //
    // A client that ignores Set-Cookie sends every request as though it had
    // never spoken to the server before, and discord reads that the way it
    // reads any other script: the browser carries about a kilobyte of these
    // and is never asked for a CAPTCHA on an invite, this client carried none
    // and always was.
    //
    // Only names and values are kept. Expiry, domain and path would matter to
    // a general purpose client; everything here goes to one host.
    // A bot's requests carry none of the browser's furniture: no cookies,
    // and the user agent discord asks bots to send rather than a browser's.
    // Sending a request signed with a bot token but dressed as a browser is
    // what discord's edge answered with "internal network error".
    void set_bot_mode(bool on);

    void cookies_header(ubuffer* out);
    void clear_cookies();

    // headers_utf8 is a "Name: Value\r\n"-joined block, or null.
    bool request(const char* method,
                 const char* url,
                 const char* headers_utf8,
                 const void* body,
                 unsigned int body_len,
                 http_response* out);

    inline bool get(const char* url, http_response* out)
    {
        return request("GET", url, 0, 0, 0, out);
    }
}

// ---------------------------------------------------------------------------
// background jobs
// ---------------------------------------------------------------------------

typedef void (*job_fn)(void* user);

namespace jobs
{
    void init(int thread_count);
    void shutdown();
    void post(job_fn fn, void* user);
    int pending();
}
