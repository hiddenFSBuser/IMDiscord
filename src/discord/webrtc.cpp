#include "pch.h"
#include "webrtc.h"
#include "net/selfcert.h"
#include "core/crypto.h"
#include "core/log.h"
// The srtp half of tlse.h lives behind this define, and it is set on the
// command line only for the library's own translation unit. Here it exposes
// declarations and nothing else - no structure this file can see changes
// shape - so setting it locally is safe and keeps the build line alone.
#define TLS_SRTP
#include "tlse.h"

namespace
{
    struct TLSRTCPeerConnection* g_channel = 0;
    selfcert::material           g_cert;

    // Where the write callback sends. The peer connection hands out packets
    // without knowing what carries them, so the route is kept here for the
    // length of one handshake.
    proxy::udp_route* g_route = 0;

    // The certificate on the other end is self signed and says nothing. What
    // vouches for it is the fingerprint out of the sdp answer, and that has
    // already been checked by the time this is called - the handshake fails
    // before this point when it does not match. So there is nothing left to
    // decide here.
    //
    // The return value is an alert description, not a boolean, and "nothing to
    // report" is no_error - which is 255, not zero. Returning zero says
    // close_notify, and the library then sends exactly that: a fatal alert
    // whose reason is zero, right after the peer's certificate arrives.
    int accept_peer(struct TLSContext*, struct TLSCertificate**, int)
    {
        return no_error;
    }

    // ---- sha-1, hmac-sha1, crc32 -----------------------------------------
    //
    // Needed for one message. Stun signs its integrity attribute with
    // hmac-sha1 and closes with a crc32, and this client has neither: sha-256
    // is what everything else here uses, and stun predates that choice.
    //
    // Correctness is not argued, it is checked: the self test builds a request
    // with the extra attributes left out and requires it to come out byte for
    // byte identical to the one tlse builds. If sha-1, the hmac construction
    // or the crc were wrong, that comparison could not pass.

    struct sha1_ctx
    {
        unsigned int h[5];
        unsigned long long length;
        unsigned char block[64];
        unsigned int fill;
    };

    unsigned int rol(unsigned int v, int by)
    {
        return (v << by) | (v >> (32 - by));
    }

    void sha1_block(sha1_ctx* c, const unsigned char* p)
    {
        unsigned int w[80];

        for (int i = 0; i < 16; i++)
            w[i] = ((unsigned int)p[i * 4] << 24) | ((unsigned int)p[i * 4 + 1] << 16) |
                   ((unsigned int)p[i * 4 + 2] << 8) | (unsigned int)p[i * 4 + 3];

        for (int i = 16; i < 80; i++)
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

        unsigned int a = c->h[0], b = c->h[1], d = c->h[2], e = c->h[3], f = c->h[4];

        for (int i = 0; i < 80; i++)
        {
            unsigned int k, t;

            if (i < 20)      { t = (b & d) | (~b & e);            k = 0x5A827999; }
            else if (i < 40) { t = b ^ d ^ e;                     k = 0x6ED9EBA1; }
            else if (i < 60) { t = (b & d) | (b & e) | (d & e);   k = 0x8F1BBCDC; }
            else             { t = b ^ d ^ e;                     k = 0xCA62C1D6; }

            unsigned int next = rol(a, 5) + t + f + k + w[i];
            f = e; e = d; d = rol(b, 30); b = a; a = next;
        }

        c->h[0] += a; c->h[1] += b; c->h[2] += d; c->h[3] += e; c->h[4] += f;
    }

    void sha1_init(sha1_ctx* c)
    {
        c->h[0] = 0x67452301; c->h[1] = 0xEFCDAB89; c->h[2] = 0x98BADCFE;
        c->h[3] = 0x10325476; c->h[4] = 0xC3D2E1F0;
        c->length = 0;
        c->fill = 0;
    }

    void sha1_update(sha1_ctx* c, const void* data, unsigned int len)
    {
        const unsigned char* p = (const unsigned char*)data;
        c->length += len;

        while (len)
        {
            unsigned int room = 64 - c->fill;
            unsigned int take = len < room ? len : room;

            ccpy(c->block + c->fill, p, take);
            c->fill += take;
            p += take;
            len -= take;

            if (c->fill == 64) { sha1_block(c, c->block); c->fill = 0; }
        }
    }

    void sha1_final(sha1_ctx* c, unsigned char out[20])
    {
        unsigned long long bits = c->length * 8;

        unsigned char pad = 0x80;
        sha1_update(c, &pad, 1);

        unsigned char zero = 0;
        while (c->fill != 56) sha1_update(c, &zero, 1);

        unsigned char tail[8];
        for (int i = 0; i < 8; i++) tail[i] = (unsigned char)(bits >> (56 - i * 8));

        c->length = 0;
        sha1_update(c, tail, 8);

        for (int i = 0; i < 5; i++)
        {
            out[i * 4]     = (unsigned char)(c->h[i] >> 24);
            out[i * 4 + 1] = (unsigned char)(c->h[i] >> 16);
            out[i * 4 + 2] = (unsigned char)(c->h[i] >> 8);
            out[i * 4 + 3] = (unsigned char)(c->h[i]);
        }
    }

    void hmac_sha1(const void* key, unsigned int key_len,
                   const void* data, unsigned int data_len,
                   unsigned char out[20])
    {
        unsigned char k[64];
        ccfset(k, 0, sizeof(k));

        if (key_len > 64)
        {
            sha1_ctx c;
            sha1_init(&c);
            sha1_update(&c, key, key_len);
            sha1_final(&c, k);
        }
        else
        {
            ccpy(k, key, key_len);
        }

        unsigned char pad[64];
        for (int i = 0; i < 64; i++) pad[i] = (unsigned char)(k[i] ^ 0x36);

        unsigned char inner[20];
        sha1_ctx c;
        sha1_init(&c);
        sha1_update(&c, pad, 64);
        sha1_update(&c, data, data_len);
        sha1_final(&c, inner);

        for (int i = 0; i < 64; i++) pad[i] = (unsigned char)(k[i] ^ 0x5C);

        sha1_init(&c);
        sha1_update(&c, pad, 64);
        sha1_update(&c, inner, 20);
        sha1_final(&c, out);

        ccfset(k, 0, sizeof(k));
        ccfset(pad, 0, sizeof(pad));
    }

    unsigned int crc32_of(const unsigned char* p, int n)
    {
        unsigned int crc = 0xFFFFFFFF;

        for (int i = 0; i < n; i++)
        {
            crc ^= p[i];
            for (int b = 0; b < 8; b++)
                crc = (crc >> 1) ^ (0xEDB88320u & (unsigned int)(-(int)(crc & 1)));
        }

        return ~crc;
    }

    // ---- the connectivity check ------------------------------------------
    //
    // What tlse builds is a username, an integrity attribute and a
    // fingerprint. What rfc 8445 requires of a controlling agent is also
    // PRIORITY and ICE-CONTROLLING, and what a browser sends is all five.
    // Discord answered none of the short version, so this is the long one.

    void put16(unsigned char* p, unsigned int v)
    {
        p[0] = (unsigned char)(v >> 8);
        p[1] = (unsigned char)(v & 0xFF);
    }

    void put32(unsigned char* p, unsigned int v)
    {
        p[0] = (unsigned char)(v >> 24);
        p[1] = (unsigned char)(v >> 16);
        p[2] = (unsigned char)(v >> 8);
        p[3] = (unsigned char)(v);
    }

    int stun_check(unsigned char* out, int cap,
                   const unsigned char transaction[12],
                   const char* username, const char* pwd,
                   unsigned int priority, const unsigned char tie_breaker[8],
                   bool with_extras)
    {
        if (cap < 512) return 0;

        put16(out, 0x0001);              // binding request
        put16(out + 2, 0);               // length, filled in as it grows
        put32(out + 4, 0x2112A442);      // magic cookie
        ccpy(out + 8, transaction, 12);

        int len = 20;

        // USERNAME, padded to four bytes as every attribute is.
        {
            int n = (int)ccslenf(username);

            put16(out + len, 0x0006);
            put16(out + len + 2, (unsigned int)n);
            len += 4;

            ccpy(out + len, username, (size_t)n);
            len += n;

            while (len % 4) out[len++] = 0;
        }

        if (with_extras)
        {
            // ICE-CONTROLLING, with the tie breaker that settles a role
            // conflict. There is no conflict to settle against an ice-lite
            // peer, which never claims a role at all - but its absence is
            // what tells the far side which role this side took.
            put16(out + len, 0x802A);
            put16(out + len + 2, 8);
            ccpy(out + len + 4, tie_breaker, 8);
            len += 12;
        }

        // USE-CANDIDATE. Legal from the controlling agent, and with a single
        // candidate on each side there is nothing to choose between.
        put16(out + len, 0x0025);
        put16(out + len + 2, 0);
        len += 4;

        if (with_extras)
        {
            put16(out + len, 0x0024);       // PRIORITY
            put16(out + len + 2, 4);
            put32(out + len + 4, priority);
            len += 8;
        }

        // MESSAGE-INTEGRITY covers everything before itself, with the length
        // field already counting it. Then FINGERPRINT covers everything
        // before itself, with the length counting that too. Both are written
        // in that order and nothing may follow them.
        put16(out + 2, (unsigned int)(len + 24 - 20));

        put16(out + len, 0x0008);
        put16(out + len + 2, 20);

        unsigned char mac[20];
        hmac_sha1(pwd, (unsigned int)ccslenf(pwd), out, (unsigned int)len, mac);
        ccpy(out + len + 4, mac, 20);
        len += 24;

        put16(out + 2, (unsigned int)(len + 8 - 20));

        // The crc covers the message up to but not including the whole
        // FINGERPRINT attribute - its four byte header included. Counting
        // that header in is the kind of mistake that produces a message which
        // is valid in every respect a person checks by eye and is dropped by
        // every peer that checks it by machine.
        put16(out + len, 0x8028);
        put16(out + len + 2, 4);
        put32(out + len + 4, crc32_of(out, len) ^ 0x5354554Eu);
        len += 8;

        return len;
    }

    // The last dtls flight, kept for retransmission. Datagrams are allowed to
    // go missing and dtls expects the sender to say it again; a handshake that
    // speaks once and then waits is a handshake that a single lost packet ends.
    unsigned char g_flight[2048];
    int  g_flight_len = 0;
    int  g_said = 0;

    // One datagram can carry several dtls records, and a whole handshake
    // flight usually arrives as exactly that. Logging only the first record
    // hides the rest - which is how a flight that already contained
    // ServerHelloDone looked like one that did not.
    void describe(const char* dir, const unsigned char* p, int n)
    {
        int at = 0;
        int records = 0;

        while (at + 13 <= n && records < 8)
        {
            int type = p[at];
            int len = (p[at + 11] << 8) | p[at + 12];

            if (len < 0 || at + 13 + len > n)
            {
                log_line("webrtc: %s запись %d, длина %d - за границей датаграммы",
                         dir, type, len);
                return;
            }

            const char* name = "?";
            switch (type)
            {
                case 20: name = "change cipher spec"; break;
                case 21: name = "alert"; break;
                case 22: name = "handshake"; break;
                case 23: name = "данные"; break;
            }

            if (type == 22 && len >= 1)
            {
                log_line("webrtc: %s handshake тип %u, %d байт (эпоха %u)",
                         dir, p[at + 13], len, (unsigned int)((p[at + 3] << 8) | p[at + 4]));

                // A dtls handshake message carries eight bytes the tcp one
                // does not: a sequence, a fragment offset and a fragment
                // length. A message built without them parses as garbage on
                // the far side and is dropped in silence, which is the
                // hardest kind of wrong to see. One subtraction finds it.
                // Only in the clear. Once the epoch moves past zero the
                // payload is ciphertext, and reading a handshake header out of
                // it produces alarming nonsense about fragments megabytes long.
                unsigned int epoch = (unsigned int)((p[at + 3] << 8) | p[at + 4]);

                if (len >= 12 && epoch == 0)
                {
                    const unsigned char* h = p + at + 13;

                    int msg_len = (h[1] << 16) | (h[2] << 8) | h[3];
                    int off     = (h[6] << 16) | (h[7] << 8) | h[8];
                    int frag    = (h[9] << 16) | (h[10] << 8) | h[11];

                    // The invariant is that a record carries exactly one
                    // fragment: header plus fragment length. A fragment that
                    // is shorter than the whole message is ordinary and says
                    // so; only a record that does not add up is a fault.
                    if (len != frag + 12)
                        log_line("webrtc:      !! запись %d байт, а фрагмент обещает %d+12"
                                 " - заголовок dtls не на месте", len, frag);
                    else if (frag != msg_len)
                        log_line("webrtc:      фрагмент %d..%d из %d",
                                 off, off + frag, msg_len);
                }

                // The certificate verify is small and it is the one message
                // whose contents cannot be guessed from its length: the
                // signature algorithm it declares and the der signature that
                // follows are what the far side checks, and either can be
                // wrong without anything else looking wrong.
                if (p[at + 13] == 15 && len <= 160)
                    log_bytes("webrtc: certificate verify", p + at + 13, (unsigned int)len);
            }
            else if (type == 21 && len >= 2)
                log_line("webrtc: %s алерт: уровень %u, причина %u",
                         dir, p[at + 13], p[at + 14]);
            else
                log_line("webrtc: %s %s, %d байт (эпоха %u)",
                         dir, name, len, (unsigned int)((p[at + 3] << 8) | p[at + 4]));

            at += 13 + len;
            records++;
        }

        if (at < n)
            log_line("webrtc: %s ещё %d байт не разобрано", dir, n - at);
    }

    int write_out(struct TLSRTCPeerConnection*, const unsigned char* msg, int len)
    {
        if (!g_route || len <= 0) return -1;

        // Named on the way out as well as on the way in. Without this there is
        // no telling a handshake that was ignored from one that was never
        // sent, and those two need completely different work.
        g_said++;

        // An alert is the one packet worth reading in full: it is this side
        // saying why it is giving up, and the two bytes that say so are the
        // difference between a certificate problem, a parameter problem and a
        // cipher problem. A dtls record header is 13 bytes, then level and
        // description.
        if (g_said <= 12)
        {
            log_line("webrtc: ушло %d байт", len);
            if (msg[0] >= 20 && msg[0] <= 23) describe("  ->", msg, len);
        }

        // A dtls record starts with a content type in this range; stun starts
        // with 0x00 or 0x01. Only the dtls half is worth saying again.
        if (msg[0] >= 20 && msg[0] <= 64 && len <= (int)sizeof(g_flight))
        {
            ccpy(g_flight, msg, (size_t)len);
            g_flight_len = len;
        }

        int sent = proxy::udp_send(g_route, msg, len);
        if (sent == SOCKET_ERROR)
        {
            log_line("webrtc: отправка не удалась, ошибка %d", WSAGetLastError());
            return -1;
        }

        // The caller treats anything but a positive number as a failure and
        // stops, so a zero length write must not be reported as one.
        return sent > 0 ? sent : 1;
    }
}

bool webrtc::begin(const char** ufrag, const char** pwd, const char** fingerprint)
{
    webrtc::reset();

    if (!selfcert::generate(&g_cert))
    {
        log_line("webrtc: сертификат не создался");
        return false;
    }

    // active: this side is the dtls client and the controlling ice agent,
    // which is what discord's "a=setup:passive" and ice-lite ask for.
    g_channel = tls_peerconnection_context(1, accept_peer, 0);
    if (!g_channel)
    {
        log_line("webrtc: не создался контекст соединения");
        return false;
    }

    if (tls_peerconnection_load_keys(g_channel,
                                     (const unsigned char*)g_cert.cert_pem, g_cert.cert_pem_len,
                                     (const unsigned char*)g_cert.key_pem, g_cert.key_pem_len) != 0)
    {
        log_line("webrtc: dtls не принял наш сертификат");
        webrtc::reset();
        return false;
    }

    // Turns on caching of the handshake transcript. The certificate verify
    // this side has to send is a signature over exactly those bytes, and they
    // are not kept unless the context is expecting client authentication -
    // which, in webrtc, it always is: the peer asks for a certificate every
    // time. Has to happen before the hello, or the transcript starts late.
    if (!tls_request_client_certificate(tls_peerconnection_dtls_context(g_channel)))
    {
        // Checked rather than assumed: this call refused clients outright, the
        // transcript was never kept, and the signature went out over the first
        // message alone - which is indistinguishable from a correct handshake
        // right up to the moment the peer stops answering.
        log_line("webrtc: не удалось включить хранение транскрипта");
        webrtc::reset();
        return false;
    }

    if (ufrag)       *ufrag = tls_peerconnection_local_username(g_channel);
    if (pwd)         *pwd = tls_peerconnection_local_pwd(g_channel);
    if (fingerprint) *fingerprint = g_cert.fingerprint;

    log_line("webrtc: готов, ufrag=%s отпечаток %s",
             tls_peerconnection_local_username(g_channel), g_cert.fingerprint);
    log_line("webrtc: свой сертификат %d байт pem, ключ %d байт pem",
             g_cert.cert_pem_len, g_cert.key_pem_len);

    return true;
}

bool webrtc::handshake(proxy::udp_route* route, const sdp::answer* a,
                       int timeout_ms, const char** why)
{
    if (why) *why = "";

    if (!g_channel || !route || !a)
    {
        if (why) *why = "не подготовлено";
        return false;
    }

    g_route = route;

    // The fingerprint the far side has to match, without the algorithm name
    // that the sdp line puts in front of it: what gets compared is the bare
    // "AA:BB:..." and nothing else.
    const char* digest = a->fingerprint;
    while (*digest && *digest != ' ') digest++;
    while (*digest == ' ') digest++;

    char ufrag[72];
    char pwd[144];
    char print[160];

    ccstrncpy(ufrag, a->ufrag, sizeof(ufrag) - 1);
    ccstrncpy(pwd, a->pwd, sizeof(pwd) - 1);
    ccstrncpy(print, digest, sizeof(print) - 1);

    if (tls_peerconnection_remote_credentials(g_channel,
                                              ufrag, (int)ccslenf(ufrag),
                                              pwd, (int)ccslenf(pwd),
                                              print, (int)ccslenf(print)) != 0)
    {
        if (why) *why = "не приняты параметры другой стороны";
        return false;
    }

    // The peer address, for the stun attributes that carry it back.
    unsigned char peer_ip[4] = { 0, 0, 0, 0 };
    {
        const char* p = a->ip;
        for (int i = 0; i < 4 && *p; i++)
        {
            unsigned int v = 0;
            while (*p >= '0' && *p <= '9') { v = v * 10 + (unsigned int)(*p - '0'); p++; }
            peer_ip[i] = (unsigned char)v;
            if (*p == '.') p++;
        }
    }

    // What goes into every check: a transaction id kept for the session, the
    // username in the order ice wants it - the far side's fragment first -
    // and the priority of the one candidate there is. That priority value is
    // the same one discord puts on its own host candidate.
    unsigned char transaction[12];
    unsigned char tie_breaker[8];
    crypto::random_bytes(transaction, sizeof(transaction));
    crypto::random_bytes(tie_breaker, sizeof(tie_breaker));

    char full_user[224];
    cnprint(full_user, sizeof(full_user), "%s:%s", ufrag,
            tls_peerconnection_local_username(g_channel));

    unsigned char check[512];
    int check_len = stun_check(check, sizeof(check), transaction, full_user, pwd,
                               2130706431u, tie_breaker, true);

    if (check_len <= 0)
    {
        if (why) *why = "stun не собрался";
        g_route = 0;
        return false;
    }

    log_line("webrtc: проверка связности %d байт, user=%s", check_len, full_user);

    unsigned long long started = GetTickCount64();
    unsigned long long deadline = started + (unsigned long long)timeout_ms;
    unsigned long long last_probe = 0;

    unsigned char packet[4096];
    int last_status = -1;
    int heard = 0;

    g_flight_len = 0;
    g_said = 0;

    unsigned long long last_flight = 0;

    while (GetTickCount64() < deadline)
    {
        unsigned long long now = GetTickCount64();

        int now_status = tls_peerconnection_status(g_channel);

        // The binding request, repeated until the handshake is under way. One
        // datagram going missing on the way to a server that never probes back
        // would otherwise be the whole connection - and ice expects checks to
        // keep coming while the connection is being set up, not to stop at the
        // first answer.
        if (now_status < 2 && now - last_probe >= 250)
        {
            last_probe = now;

            if (proxy::udp_send(route, check, check_len) == SOCKET_ERROR)
            {
                log_line("webrtc: отправка stun не удалась, ошибка %d", WSAGetLastError());
                if (why) *why = "stun не отправился";
                g_route = 0;
                return false;
            }
        }

        // And the dtls flight said again if nothing has come back. This is
        // the retransmission dtls requires of its sender; without it one lost
        // hello is indistinguishable from a server that refuses to answer.
        if (g_flight_len && now_status < 3 && now - last_flight >= 600)
        {
            last_flight = now;

            if (proxy::udp_send(route, g_flight, g_flight_len) == SOCKET_ERROR)
                log_line("webrtc: повтор рукопожатия не ушёл, ошибка %d", WSAGetLastError());
        }

        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(route->data, &rd);

        timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 50000;

        if (select(0, &rd, 0, 0, &tv) > 0 && FD_ISSET(route->data, &rd))
        {
            int n = proxy::udp_recv(route, packet, (int)sizeof(packet));

            if (n > 0)
            {
                // Every datagram, named. The difference between "nothing came
                // back" and "something came back and was not understood" is
                // the whole diagnosis, and it cannot be recovered later.
                heard++;

                if (heard <= 24)
                {
                    log_line("webrtc: пришло %d байт", n);
                    if (packet[0] >= 20 && packet[0] <= 23) describe("  <-", packet, n);
                }

                int validate = 0;
                int err = tls_peerconnection_iterate(g_channel, packet, n,
                                                     peer_ip, (int)a->port, 0,
                                                     write_out, &validate);

                if (err < 0)
                {
                    log_line("webrtc: обработка пакета вернула %d", err);
                    log_line("webrtc: состояние на момент отказа %d",
                             tls_peerconnection_status(g_channel));
                    if (why) *why = "рукопожатие отклонено";
                    g_route = 0;
                    return false;
                }
            }
        }

        int status = tls_peerconnection_status(g_channel);

        if (status != last_status)
        {
            // 1 stun answered, 2 dtls started, 3 srtp keyed, 4 closed.
            static const char* names[] = { "ничего", "stun прошёл", "dtls пошёл",
                                           "srtp готов", "закрыто" };

            log_line("webrtc: состояние %d (%s), %u мс",
                     status, status >= 0 && status <= 4 ? names[status] : "?",
                     (unsigned int)(GetTickCount64() - started));

            last_status = status;
        }

        if (status == 3)
        {
            g_route = 0;
            return true;
        }

        if (status == 4)
        {
            if (why) *why = "другая сторона закрыла соединение";
            g_route = 0;
            return false;
        }
    }

    // Which stage it died at is the whole diagnosis, so it is named rather
    // than reported as a timeout.
    int status = tls_peerconnection_status(g_channel);

    log_line("webrtc: сдаюсь на состоянии %d, отправлено %d, получено %d, флайт %d байт",
             status, g_said, heard, g_flight_len);

    if (why)
    {
        if (status < 1)      *why = heard ? "stun ответил непонятным"
                                          : "stun остался без ответа";
        else if (status < 2) *why = "dtls не начался";
        else                 *why = "dtls не завершился";
    }

    g_route = 0;
    return false;
}

bool webrtc::self_test()
{
    // The hello this client would send, built with no network in sight and
    // written out in full. Discord answers every stun check and ignores this
    // packet, so what is in it is the only remaining question - and reading it
    // costs nothing compared to another round trip through somebody's call.
    {
        struct TLSRTCPeerConnection* probe = tls_peerconnection_context(1, accept_peer, 0);
        selfcert::material cert;

        if (probe && selfcert::generate(&cert) &&
            tls_peerconnection_load_keys(probe,
                                         (const unsigned char*)cert.cert_pem, cert.cert_pem_len,
                                         (const unsigned char*)cert.key_pem, cert.key_pem_len) == 0)
        {
            struct TLSContext* ctx = tls_peerconnection_dtls_context(probe);
            int rc = tls_client_connect(ctx);

            unsigned int out_len = 0;
            const unsigned char* out = tls_get_write_buffer(ctx, &out_len);

            log_line("webrtc: client hello, tls_client_connect=%d, %u байт", rc, out_len);

            // Walked rather than admired. A hello whose declared extension
            // length does not match the bytes that follow it is dropped by the
            // far side without a word - no alert, no reply, nothing to see in
            // a capture except silence. That cost a whole round of debugging
            // through somebody else's voice call, and it is one subtraction to
            // check.
            bool sane = out && out_len > 40;
            int at = 13 + 12;                 // dtls record header, handshake header

            if (sane) at += 2 + 32;           // client version, random
            if (sane && at < (int)out_len) at += 1 + out[at];          // session id
            if (sane && at < (int)out_len) at += 1 + out[at];          // cookie
            if (sane && at + 1 < (int)out_len)
                at += 2 + ((out[at] << 8) | out[at + 1]);              // cipher suites
            if (sane && at < (int)out_len) at += 1 + out[at];          // compression

            if (!sane || at + 2 > (int)out_len)
            {
                log_line("webrtc: client hello не разобрался");
                if (out && out_len) log_bytes("webrtc: hello", out, out_len);
                if (probe) tls_destroy_peerconnection(probe);
                return false;
            }

            int declared = (out[at] << 8) | out[at + 1];
            int present = (int)out_len - (at + 2);

            if (declared != present)
            {
                log_line("webrtc: client hello обещает %d байт расширений, лежит %d",
                         declared, present);
                log_bytes("webrtc: hello", out, out_len);
                if (probe) tls_destroy_peerconnection(probe);
                return false;
            }

            log_line("webrtc: client hello сходится, расширений %d байт", declared);
        }

        if (probe) tls_destroy_peerconnection(probe);
    }

    // The published check value for crc-32: the digest of "123456789" is
    // this and nothing else. Worth its own line rather than resting on
    // agreement with the library, because both could be wrong the same way -
    // and one of them was.
    if (crc32_of((const unsigned char*)"123456789", 9) != 0xCBF43926u)
    {
        log_line("webrtc: самопроверка crc32 не сошлась с эталоном");
        return false;
    }

    // What a p-256 signature actually measures, for comparison with what
    // goes out on the wire. A der encoded pair of 32 byte integers lands
    // around seventy bytes; anything much shorter was signed on a smaller
    // curve than the certificate claims.
    {
        unsigned char pub[65], priv[96];
        if (crypto::p256_generate(pub, priv))
        {
            unsigned char sig[128];
            unsigned int sig_len = 0;

            if (crypto::p256_sign(priv, "certificate verify", 18, sig, &sig_len))
                log_line("webrtc: своя подпись p-256 = %u байт der", sig_len);
        }
    }

    unsigned char transaction[12];
    for (int i = 0; i < 12; i++) transaction[i] = (unsigned char)(i * 7 + 3);

    char user[] = "wwKE:IKSB";
    char pwd[] = "3JVZc8VldL8r85pljwyPBP";

    unsigned char mine[512];
    int mine_len = stun_check(mine, sizeof(mine), transaction, user, pwd,
                              2130706431u, transaction, false);

    unsigned char theirs[1024];
    int theirs_len = tls_stun_build(transaction, user, (int)ccslenf(user),
                                    pwd, (int)ccslenf(pwd), theirs);

    if (mine_len <= 0 || mine_len != theirs_len)
    {
        log_line("webrtc: самопроверка stun: длина %d против %d", mine_len, theirs_len);
        return false;
    }

    for (int i = 0; i < mine_len; i++)
        if (mine[i] != theirs[i])
        {
            log_line("webrtc: самопроверка stun: байт %d, %02X против %02X",
                     i, mine[i], theirs[i]);
            return false;
        }

    // And the long form has to be longer by exactly the two attributes it
    // adds - twelve bytes of ICE-CONTROLLING and eight of PRIORITY - with the
    // length field in the header agreeing.
    unsigned char full[512];
    int full_len = stun_check(full, sizeof(full), transaction, user, pwd,
                              2130706431u, transaction, true);

    if (full_len != mine_len + 20)
    {
        log_line("webrtc: самопроверка stun: полная форма %d, ждали %d",
                 full_len, mine_len + 20);
        return false;
    }

    if (((full[2] << 8) | full[3]) != full_len - 20)
    {
        log_line("webrtc: самопроверка stun: длина в заголовке не сходится");
        return false;
    }

    log_line("webrtc: самопроверка пройдена, проверка связности %d байт", full_len);
    return true;
}

int webrtc::protect(const unsigned char* header, int header_len,
                    const unsigned char* payload, int payload_len,
                    unsigned char* out, int out_cap)
{
    if (!g_channel || header_len < 12 || payload_len <= 0) return 0;

    // Room for the header, the payload and the tag the library appends.
    if (header_len + payload_len + 32 > out_cap) return 0;

    ccpy(out, header, (size_t)header_len);

    int room = out_cap - header_len;
    if (tls_peerconnection_encrypt(g_channel, 0, header, header_len,
                                   payload, (unsigned int)payload_len,
                                   out + header_len, &room) != 0)
        return 0;

    return header_len + room;
}

int webrtc::unprotect(const unsigned char* header, int header_len,
                      const unsigned char* payload, int payload_len,
                      unsigned char* out, int out_cap)
{
    if (!g_channel || header_len < 12 || payload_len <= 0) return 0;

    // The library wants the capacity in the same variable it reports the
    // length in, and refuses outright when it is smaller than the input.
    if (payload_len > out_cap) return 0;

    int room = out_cap;
    if (tls_peerconnection_decrypt(g_channel, 0, header, header_len,
                                   payload, (unsigned int)payload_len,
                                   out, &room) != 0)
        return 0;

    return room > 0 ? room : 0;
}

bool webrtc::ready()
{
    return g_channel && tls_peerconnection_status(g_channel) == 3;
}

void webrtc::reset()
{
    if (g_channel)
    {
        tls_destroy_peerconnection(g_channel);
        g_channel = 0;
    }

    g_route = 0;
    ccfset(&g_cert, 0, sizeof(g_cert));
}
