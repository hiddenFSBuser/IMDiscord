#include "pch.h"
#include "websocket.h"
#include "http.h"
#include "proxy.h"
#include "core/crypto.h"
#include "core/log.h"

#include <bcrypt.h>

namespace
{
    // Opcodes, from the protocol.
    const unsigned char OP_CONTINUATION = 0x0;
    const unsigned char OP_TEXT         = 0x1;
    const unsigned char OP_BINARY       = 0x2;
    const unsigned char OP_CLOSE        = 0x8;
    const unsigned char OP_PING         = 0x9;
    const unsigned char OP_PONG         = 0xA;

    // The constant the server mixes into the key to prove it understood the
    // request rather than merely echoing it back.
    const char* ACCEPT_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

    // Deliberately the long way round. BCryptHash, the one call version, only
    // exists from Windows 10 - using it here would have made the handshake
    // fail on every older system, which is the opposite of the point of
    // writing this websocket by hand in the first place.
    bool sha1(const void* data, unsigned int len, unsigned char out[20])
    {
        BCRYPT_ALG_HANDLE alg = 0;
        if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA1_ALGORITHM, 0, 0) != 0) return false;

        BCRYPT_HASH_HANDLE hash = 0;
        bool ok = BCryptCreateHash(alg, &hash, 0, 0, 0, 0, 0) == 0;

        if (ok) ok = BCryptHashData(hash, (PUCHAR)data, len, 0) == 0;
        if (ok) ok = BCryptFinishHash(hash, out, 20, 0) == 0;

        if (hash) BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(alg, 0);
        return ok;
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

void websocket::init()
{
    ccfset(&stream, 0, sizeof(stream));
    stream.plain = INVALID_SOCKET;

    open = 0;
    close_status = 0;
    inbox.init(1 << 16);
    inbox_at = 0;

    InitializeCriticalSection(&send_lock);
    lock_ready = true;
}

bool websocket::connect(const char* url, const char* extra_headers, const proxy_config* proxy)
{
    url_parts u;
    if (!http::parse_url(url, &u))
    {
        log_line("ws: неразборчивый адрес %s", url);
        return false;
    }

    if (!httpstream::connect(&stream, u.host, (unsigned short)u.port, u.secure, proxy, 15000))
    {
        log_line("ws: %s недоступен (%s)", u.host, httpstream::last_error(&stream));
        return false;
    }

    // Sixteen random bytes, base64. The server hashes them with a fixed guid
    // and hands the result back; anything else means whatever answered is not
    // speaking this protocol.
    unsigned char nonce[16];
    crypto::random_bytes(nonce, sizeof(nonce));

    ubuffer key;
    key.init(64);
    crypto::base64_encode(nonce, sizeof(nonce), &key);
    key.c_str();

    ubuffer request;
    request.init(1024);
    request.append_fmt("GET %s HTTP/1.1\r\n", u.path);
    request.append_fmt("Host: %s\r\n", u.host);
    request.append_str("Upgrade: websocket\r\n");
    request.append_str("Connection: Upgrade\r\n");
    request.append_fmt("Sec-WebSocket-Key: %s\r\n", (const char*)key.data);
    request.append_str("Sec-WebSocket-Version: 13\r\n");
    request.append_str("User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                       "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36\r\n");
    if (extra_headers && extra_headers[0]) request.append_str(extra_headers);
    request.append_str("\r\n");

    bool sent = httpstream::write_all(&stream, request.data, (int)request.size);
    request.free_buffer();

    if (!sent)
    {
        log_line("ws: рукопожатие не отправилось");
        key.free_buffer();
        close_handles();
        return false;
    }

    // ---- the reply
    inbox.clear();
    inbox_at = 0;

    int header_end = -1;
    unsigned long long deadline = GetTickCount64() + 15000;

    while (header_end < 0)
    {
        unsigned long long now = GetTickCount64();
        if (now >= deadline) break;

        char chunk[4096];
        int got = httpstream::read(&stream, chunk, (int)sizeof(chunk),
                                   (unsigned int)(deadline - now));
        if (got == -2) continue;
        if (got <= 0) break;

        inbox.append(chunk, (unsigned int)got);

        for (unsigned int i = 3; i < inbox.size; i++)
        {
            if (inbox.data[i - 3] == '\r' && inbox.data[i - 2] == '\n' &&
                inbox.data[i - 1] == '\r' && inbox.data[i] == '\n')
            {
                header_end = (int)i;
                break;
            }
        }
    }

    if (header_end < 0)
    {
        log_line("ws: сервер не ответил на рукопожатие");
        key.free_buffer();
        close_handles();
        return false;
    }

    // Status line and headers, read in place.
    int status = 0;
    char accept[64];
    accept[0] = 0;

    {
        char line[1024];
        unsigned int at = 0;
        bool first = true;

        while (at < (unsigned int)header_end)
        {
            unsigned int start = at;
            while (at + 1 <= (unsigned int)header_end &&
                   !(inbox.data[at] == '\r' && inbox.data[at + 1] == '\n')) at++;

            unsigned int len = at - start;
            if (len > sizeof(line) - 1) len = sizeof(line) - 1;
            ccpy(line, inbox.data + start, len);
            line[len] = 0;
            at += 2;

            if (first)
            {
                const char* p = line;
                while (*p && *p != ' ') p++;
                while (*p == ' ') p++;
                while (*p >= '0' && *p <= '9') { status = status * 10 + (*p - '0'); p++; }
                first = false;
                continue;
            }

            if (header_is(line, "sec-websocket-accept"))
                ccstrncpy(accept, header_value(line), sizeof(accept) - 1);
        }
    }

    if (status != 101)
    {
        log_line("ws: сервер ответил %d вместо 101", status);
        key.free_buffer();
        close_handles();
        return false;
    }

    // The proof. Skipping it would mean trusting that whatever answered on
    // this socket is the server that was asked for.
    {
        ubuffer combined;
        combined.init(128);
        combined.append(key.data, key.size);
        combined.append_str(ACCEPT_GUID);

        unsigned char digest[20];
        ubuffer expected;
        expected.init(64);

        bool matched = false;
        if (sha1(combined.data, combined.size, digest))
        {
            crypto::base64_encode(digest, sizeof(digest), &expected);
            expected.c_str();
            matched = ccscmp((const char*)expected.data, accept) == 0;
        }

        combined.free_buffer();
        expected.free_buffer();
        key.free_buffer();

        if (!matched)
        {
            log_line("ws: сервер не подтвердил ключ");
            close_handles();
            return false;
        }
    }

    // Anything past the blank line is already frame data - the first message
    // usually arrives in the same read as the handshake reply.
    inbox_at = (unsigned int)header_end + 1;

    close_status = 0;
    InterlockedExchange(&open, 1);
    return true;
}

namespace
{
    // A client frame is always masked; a server one never is. Everything else
    // about the two directions is the same.
    bool send_frame(websocket* ws, unsigned char opcode, const void* data, unsigned int len)
    {
        unsigned char head[14];
        int n = 0;

        head[n++] = (unsigned char)(0x80 | opcode);      // FIN, one frame

        const unsigned char MASKED = 0x80;
        if (len < 126)
        {
            head[n++] = (unsigned char)(MASKED | len);
        }
        else if (len <= 0xFFFF)
        {
            head[n++] = (unsigned char)(MASKED | 126);
            head[n++] = (unsigned char)(len >> 8);
            head[n++] = (unsigned char)(len);
        }
        else
        {
            head[n++] = (unsigned char)(MASKED | 127);
            for (int i = 7; i >= 0; i--)
                head[n++] = (unsigned char)((unsigned long long)len >> (i * 8));
        }

        unsigned char mask[4];
        crypto::random_bytes(mask, sizeof(mask));
        for (int i = 0; i < 4; i++) head[n++] = mask[i];

        // Masking is done into a copy, so the caller's buffer is left alone.
        ubuffer body;
        body.init(len + 1);
        if (len)
        {
            body.append(data, len);
            for (unsigned int i = 0; i < len; i++) body.data[i] ^= mask[i & 3];
        }

        EnterCriticalSection(&ws->send_lock);
        bool ok = httpstream::write_all(&ws->stream, head, n);
        if (ok && len) ok = httpstream::write_all(&ws->stream, body.data, (int)len);
        LeaveCriticalSection(&ws->send_lock);

        body.free_buffer();
        return ok;
    }
}

bool websocket::send_text(const void* data, unsigned int len)
{
    if (!open) return false;
    return send_frame(this, OP_TEXT, data, len);
}

bool websocket::send_binary(const void* data, unsigned int len)
{
    if (!open) return false;
    return send_frame(this, OP_BINARY, data, len);
}

void websocket::consume(unsigned int bytes)
{
    inbox_at += bytes;

    // Once everything has been read out, start again from the front rather
    // than letting the offset walk off into a buffer that only ever grows.
    if (inbox_at >= inbox.size)
    {
        inbox.clear();
        inbox_at = 0;
    }
}

bool websocket::refill()
{
    // Move the unread tail down before asking for more, so a connection that
    // lives for hours does not accumulate everything it ever received.
    if (inbox_at > 0 && inbox_at >= inbox.size)
    {
        inbox.clear();
        inbox_at = 0;
    }
    else if (inbox_at > (1 << 16))
    {
        unsigned int left = inbox.size - inbox_at;

        ubuffer moved;
        moved.init(left + 1);
        moved.append(inbox.data + inbox_at, left);

        inbox.clear();
        inbox.append(moved.data, moved.size);
        moved.free_buffer();
        inbox_at = 0;
    }

    char chunk[16384];

    // No overall deadline: a gateway is quiet for long stretches between
    // heartbeats, and the caller is the one who decides when silence has gone
    // on too long.
    for (;;)
    {
        int got = httpstream::read(&stream, chunk, (int)sizeof(chunk), 30000);
        if (got == -2) continue;                 // nothing yet, keep waiting

        if (got == 0)
        {
            log_line("ws: соединение закрыто другой стороной");
            InterlockedExchange(&open, 0);
            if (!close_status) close_status = 1006;
            return false;
        }
        if (got < 0)
        {
            InterlockedExchange(&open, 0);
            if (!close_status) close_status = 1006;
            return false;
        }

        inbox.append(chunk, (unsigned int)got);
        return true;
    }
}

ws_result websocket::receive(ubuffer* out, bool* out_is_binary)
{
    if (!out) return WS_ERROR;

    out->clear();
    if (out_is_binary) *out_is_binary = false;

    bool have_type = false;
    unsigned char message_op = 0;

    for (;;)
    {
        if (!open) return WS_CLOSED;

        unsigned int available = inbox.size - inbox_at;
        if (available < 2)
        {
            if (!refill()) return WS_CLOSED;
            continue;
        }

        const unsigned char* f = inbox.data + inbox_at;
        bool fin = (f[0] & 0x80) != 0;
        unsigned char opcode = f[0] & 0x0F;
        bool masked = (f[1] & 0x80) != 0;
        unsigned long long payload = f[1] & 0x7F;

        unsigned int header = 2;
        if (payload == 126)
        {
            if (available < 4) { if (!refill()) return WS_CLOSED; continue; }
            payload = ((unsigned long long)f[2] << 8) | f[3];
            header = 4;
        }
        else if (payload == 127)
        {
            if (available < 10) { if (!refill()) return WS_CLOSED; continue; }
            payload = 0;
            for (int i = 0; i < 8; i++) payload = (payload << 8) | f[2 + i];
            header = 10;
        }

        if (masked) header += 4;      // a server should not, but be exact

        if (payload > (1u << 28))
        {
            log_line("ws: кадр невозможного размера");
            return WS_ERROR;
        }

        if (available < header + payload)
        {
            if (!refill()) return WS_CLOSED;
            continue;
        }

        const unsigned char* body = f + header;
        unsigned int len = (unsigned int)payload;

        // Unmask into a copy if a server broke the rule and masked anyway.
        ubuffer unmasked;
        unmasked.init(1);
        if (masked && len)
        {
            const unsigned char* mask = f + header - 4;
            unmasked.append(body, len);
            for (unsigned int i = 0; i < len; i++) unmasked.data[i] ^= mask[i & 3];
            body = unmasked.data;
        }

        // ---- control frames are answered here and never handed upwards
        if (opcode == OP_CLOSE)
        {
            close_status = len >= 2 ? (unsigned short)((body[0] << 8) | body[1]) : 1005;
            log_line("ws: сервер закрыл соединение, код %u", close_status);

            send_frame(this, OP_CLOSE, body, len < 125 ? len : 125);
            InterlockedExchange(&open, 0);

            unmasked.free_buffer();
            consume(header + len);
            return WS_CLOSED;
        }

        if (opcode == OP_PING)
        {
            send_frame(this, OP_PONG, body, len);
            unmasked.free_buffer();
            consume(header + len);
            continue;
        }

        if (opcode == OP_PONG)
        {
            unmasked.free_buffer();
            consume(header + len);
            continue;
        }

        // ---- data
        if (opcode == OP_TEXT || opcode == OP_BINARY)
        {
            if (!have_type)
            {
                have_type = true;
                message_op = opcode;
            }
        }
        else if (opcode != OP_CONTINUATION)
        {
            log_line("ws: неизвестный код кадра %u", opcode);
            unmasked.free_buffer();
            consume(header + len);
            continue;
        }

        if (len) out->append(body, len);
        unmasked.free_buffer();
        consume(header + len);

        if (fin)
        {
            out->c_str();
            if (out_is_binary) *out_is_binary = (message_op == OP_BINARY);
            return WS_MESSAGE;
        }
    }
}

void websocket::close()
{
    if (open)
    {
        // A courteous goodbye, then the socket goes whether or not the far
        // end answers it.
        unsigned char reason[2] = { 0x03, 0xE8 };     // 1000, normal closure
        send_frame(this, OP_CLOSE, reason, sizeof(reason));
        InterlockedExchange(&open, 0);
    }
    close_handles();
}

void websocket::close_handles()
{
    httpstream::close(&stream);
    InterlockedExchange(&open, 0);
    inbox.clear();
    inbox_at = 0;
}

void websocket::destroy()
{
    close_handles();
    inbox.free_buffer();

    if (lock_ready)
    {
        DeleteCriticalSection(&send_lock);
        lock_ready = false;
    }
}

bool ws::self_test()
{
    const char* url = "wss://gateway.discord.gg/?v=9&encoding=json";
    log_line("wstest: подключаюсь к %s", url);

    websocket sock;
    sock.init();

    if (!sock.connect(url, "Origin: https://discord.com\r\n", 0))
    {
        log_line("wstest: рукопожатие не прошло");
        sock.destroy();
        return false;
    }

    log_line("wstest: рукопожатие прошло, ключ подтверждён");

    ubuffer message;
    message.init(1 << 14);

    bool binary = false;
    ws_result r = sock.receive(&message, &binary);

    bool ok = false;
    if (r == WS_MESSAGE)
    {
        // The gateway opens with op 10, the hello, and says how often it
        // wants to be heard from. Getting it means every layer worked.
        log_line("wstest: пришло %u байт%s: %s", message.size,
                 binary ? " (двоичных)" : "", (const char*)message.c_str());
        ok = message.size > 0;
    }
    else
    {
        log_line("wstest: сообщения не было (%d, код закрытия %u)", (int)r, sock.close_status);
    }

    sock.close();
    message.free_buffer();
    sock.destroy();

    log_line("wstest: итог %s", ok ? "всё работает" : "НЕ РАБОТАЕТ");
    return ok;
}
