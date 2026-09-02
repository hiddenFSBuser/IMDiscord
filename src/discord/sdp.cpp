#include "pch.h"
#include "sdp.h"
#include "core/log.h"

namespace
{
    bool head(const char* line, const char* prefix, const char** rest)
    {
        size_t n = ccslenf(prefix);
        if (ccsncmpf(line, prefix, n) != 0) return false;

        *rest = line + n;
        return true;
    }

    // Copies up to the end of the line, dropping the spaces a hand written
    // description tends to collect at both ends.
    void take(char* dst, int cap, const char* src, int len)
    {
        while (len > 0 && (*src == ' ' || *src == '\t')) { src++; len--; }
        while (len > 0 && (src[len - 1] == ' ' || src[len - 1] == '\t' ||
                           src[len - 1] == '\r')) len--;

        if (len > cap - 1) len = cap - 1;

        for (int i = 0; i < len; i++) dst[i] = src[i];
        dst[len] = 0;
    }

    bool contains(const char* hay, const char* needle)
    {
        for (int i = 0; hay[i]; i++)
        {
            int j = 0;
            while (needle[j] && hay[i + j] == needle[j]) j++;
            if (!needle[j]) return true;
        }

        return false;
    }
}

bool sdp::parse_answer(const char* text, answer* out)
{
    if (!text || !out) return false;

    ccfset(out, 0, sizeof(*out));

    const char* p = text;

    while (*p)
    {
        const char* line = p;

        int len = 0;
        while (line[len] && line[len] != '\n') len++;

        p = line[len] ? line + len + 1 : line + len;

        const char* rest = 0;

        // "c=IN IP4 <address>". Only the address is wanted; the network and
        // address type are always these two here.
        if (head(line, "c=IN IP4 ", &rest))
        {
            take(out->ip, sizeof(out->ip), rest, len - (int)(rest - line));
        }
        // "a=rtcp:<port>", and only that. Matching "a=rtcp" loosely would take
        // the port out of "a=rtcp-mux" or "a=rtcp-fb:120 nack", which is how a
        // parser ends up with a port of zero and no idea why.
        else if (head(line, "a=rtcp:", &rest))
        {
            char text_port[16];
            take(text_port, sizeof(text_port), rest, len - (int)(rest - line));

            // A port may be followed by "IN IP4 ..." in a full description.
            for (int i = 0; text_port[i]; i++)
                if (text_port[i] == ' ') { text_port[i] = 0; break; }

            out->port = (unsigned short)ccstrtoull(text_port, 0, 10);
        }
        else if (head(line, "a=ice-ufrag:", &rest))
        {
            take(out->ufrag, sizeof(out->ufrag), rest, len - (int)(rest - line));
        }
        else if (head(line, "a=ice-pwd:", &rest))
        {
            take(out->pwd, sizeof(out->pwd), rest, len - (int)(rest - line));
        }
        else if (head(line, "a=fingerprint:", &rest))
        {
            take(out->fingerprint, sizeof(out->fingerprint), rest, len - (int)(rest - line));
        }
        // The last one wins, which is what the reference implementation does
        // too. Discord sends exactly one; if that ever changes, the last is as
        // good a guess as the first and this is where to start looking.
        else if (head(line, "a=candidate:", &rest))
        {
            take(out->candidate, sizeof(out->candidate), rest, len - (int)(rest - line));

            out->candidate_is_udp = contains(out->candidate, " UDP ") ||
                                    contains(out->candidate, " udp ");
        }
    }

    return out->ok();
}

int sdp::build_offer(char* out, int cap, const offer* o)
{
    if (!out || cap < 2 || !o) return 0;

    // Formatted into a buffer of our own first. cvnprint truncates silently
    // and returns what it managed to write, so a short caller buffer cannot
    // be told from a description that simply is that long - and a truncated
    // description would go out as if it were whole. Room here is cheap; the
    // offer runs to about 1.6 kb.
    char work[4096];

    // Written from the shape of the answer the reference builds and from what
    // an ordinary webrtc offer carries, not from a capture of discord's own
    // client. Discord answers with transport lines only, so most of this is
    // never echoed back and cannot be checked against a reply - the codecs it
    // actually honours come from the codecs list in SELECT_PROTOCOL, which is
    // sent alongside and does have a capture behind it.
    //
    // Port 9 and address 0.0.0.0 are what an offer with no gathered candidate
    // says. There is nothing to gather here: discord is ice-lite and this side
    // simply sends to the one candidate it publishes.
    int n = cnprint(work, sizeof(work),
        "v=0\r\n"
        "o=- 0 0 IN IP4 0.0.0.0\r\n"
        "s=-\r\n"
        "t=0 0\r\n"
        "a=msid-semantic: WMS *\r\n"
        "a=group:BUNDLE 0 1\r\n"

        "m=audio 9 UDP/TLS/RTP/SAVPF %d\r\n"
        "c=IN IP4 0.0.0.0\r\n"
        "a=rtcp:9 IN IP4 0.0.0.0\r\n"
        "a=ice-ufrag:%s\r\n"
        "a=ice-pwd:%s\r\n"
        "a=fingerprint:%s\r\n"
        "a=setup:actpass\r\n"
        "a=mid:0\r\n"
        "a=sendrecv\r\n"
        "a=rtcp-mux\r\n"
        "a=rtpmap:%d opus/48000/2\r\n"
        "a=fmtp:%d minptime=10;useinbandfec=1;usedtx=1\r\n"
        "a=rtcp-fb:%d transport-cc\r\n"
        "a=extmap:1 urn:ietf:params:rtp-hdrext:ssrc-audio-level\r\n"
        "a=extmap:3 http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01\r\n"
        "a=ssrc:%u cname:%s\r\n"

        "m=video 9 UDP/TLS/RTP/SAVPF %d %d\r\n"
        "c=IN IP4 0.0.0.0\r\n"
        "a=rtcp:9 IN IP4 0.0.0.0\r\n"
        "a=ice-ufrag:%s\r\n"
        "a=ice-pwd:%s\r\n"
        "a=fingerprint:%s\r\n"
        "a=setup:actpass\r\n"
        "a=mid:1\r\n"
        "a=sendrecv\r\n"
        "a=rtcp-mux\r\n"
        "a=rtpmap:%d H264/90000\r\n"
        "a=fmtp:%d level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f\r\n"
        "a=rtpmap:%d rtx/90000\r\n"
        "a=fmtp:%d apt=%d\r\n"
        "a=rtcp-fb:%d ccm fir\r\n"
        "a=rtcp-fb:%d nack\r\n"
        "a=rtcp-fb:%d nack pli\r\n"
        "a=rtcp-fb:%d goog-remb\r\n"
        "a=rtcp-fb:%d transport-cc\r\n"
        "a=extmap:2 http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time\r\n"
        "a=extmap:3 http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01\r\n"
        "a=extmap:5 http://www.webrtc.org/experiments/rtp-hdrext/playout-delay\r\n"
        "a=ssrc:%u cname:%s\r\n"
        "a=ssrc:%u cname:%s\r\n",

        o->opus_payload,
        o->ufrag, o->pwd, o->fingerprint,
        o->opus_payload, o->opus_payload, o->opus_payload,
        o->audio_ssrc, o->cname,

        o->h264_payload, o->h264_rtx_payload,
        o->ufrag, o->pwd, o->fingerprint,
        o->h264_payload, o->h264_payload,
        o->h264_rtx_payload, o->h264_rtx_payload, o->h264_payload,
        o->h264_payload, o->h264_payload, o->h264_payload,
        o->h264_payload, o->h264_payload,
        o->video_ssrc, o->cname,
        o->rtx_ssrc, o->cname);

    // Filled to the brim means it was cut off. Nothing legitimate comes
    // anywhere near four kilobytes, so this is truncation and not a very
    // long description.
    if (n <= 0 || n >= (int)sizeof(work) - 1) return 0;
    if (n + 1 > cap) return 0;

    ccpy(out, work, (size_t)n + 1);
    return n;
}

bool sdp::self_test()
{
    // The reply exactly as it was captured from discord's voice server, down
    // to the bare newlines. Anything that parses this parses the real thing.
    const char* real =
        "m=audio 19296 ICE/SDP\n"
        "c=IN IP4 104.29.141.148\n"
        "a=rtcp:19296\n"
        "a=ice-ufrag:Qjnz\n"
        "a=ice-pwd:x6whuONccSDsHoC5I9Dxuq\n"
        "a=fingerprint:sha-256 04:02:36:4B:DC:E0:91:C6:22:B8:8A:30:DC:DD:57:EB:0F:9B:E3:80:D3:91:3E:81:2B:DC:51:97:FE:27:DD:BC\n"
        "a=candidate:1 1 UDP 2130706431 104.29.141.148 19296 typ host\n";

    sdp::answer a;
    if (!sdp::parse_answer(real, &a))
    {
        log_line("sdp: настоящий ответ не разобрался");
        return false;
    }

    if (ccscmp(a.ip, "104.29.141.148") != 0 || a.port != 19296)
    {
        log_line("sdp: адрес разобран как %s:%u", a.ip, a.port);
        return false;
    }

    if (ccscmp(a.ufrag, "Qjnz") != 0 ||
        ccscmp(a.pwd, "x6whuONccSDsHoC5I9Dxuq") != 0)
    {
        log_line("sdp: ice-параметры разобраны неверно");
        return false;
    }

    if (ccsncmpf(a.fingerprint, "sha-256 04:02:36:", 17) != 0)
    {
        log_line("sdp: отпечаток разобран неверно: %s", a.fingerprint);
        return false;
    }

    if (!a.candidate_is_udp)
    {
        log_line("sdp: кандидат не признан udp");
        return false;
    }

    // The same thing with crlf endings and a full description around it. The
    // rtcp lines here are the trap: a parser that matches "a=rtcp" loosely
    // takes a port out of one of them and never notices.
    const char* noisy =
        "v=0\r\n"
        "c=IN IP4 10.0.0.7\r\n"
        "a=rtcp-mux\r\n"
        "a=rtcp-fb:120 nack\r\n"
        "a=rtcp:5004 IN IP4 10.0.0.7\r\n"
        "a=ice-ufrag:  Ab12  \r\n"
        "a=ice-pwd:secretsecretsecret\r\n"
        "a=fingerprint:sha-256 AA:BB\r\n"
        "a=candidate:1 1 TCP 2130706431 10.0.0.7 5004 typ host\r\n";

    sdp::answer b;
    if (!sdp::parse_answer(noisy, &b))
    {
        log_line("sdp: описание с crlf не разобралось");
        return false;
    }

    if (b.port != 5004)
    {
        log_line("sdp: порт взят не из той строки: %u", b.port);
        return false;
    }

    if (ccscmp(b.ufrag, "Ab12") != 0)
    {
        log_line("sdp: пробелы вокруг ufrag не срезаны: [%s]", b.ufrag);
        return false;
    }

    if (b.candidate_is_udp)
    {
        log_line("sdp: tcp-кандидат признан udp");
        return false;
    }

    // Something missing has to fail rather than half succeed: a description
    // without a fingerprint cannot be used and must not look usable.
    sdp::answer c;
    if (sdp::parse_answer("c=IN IP4 1.2.3.4\na=rtcp:1000\n", &c))
    {
        log_line("sdp: неполный ответ признан годным");
        return false;
    }

    // The offer has to carry the three things discord matches on, and both
    // media sections.
    sdp::offer o;
    ccfset(&o, 0, sizeof(o));
    o.ufrag = "abcd";
    o.pwd = "0123456789abcdef0123";
    o.fingerprint = "sha-256 11:22:33";
    o.cname = "imd";
    o.audio_ssrc = 111;
    o.video_ssrc = 222;
    o.rtx_ssrc = 333;
    o.opus_payload = 120;
    o.h264_payload = 105;
    o.h264_rtx_payload = 106;

    char text[4096];
    int n = sdp::build_offer(text, sizeof(text), &o);

    if (n <= 0)
    {
        log_line("sdp: предложение не собралось");
        return false;
    }

    if (!contains(text, "a=ice-ufrag:abcd") ||
        !contains(text, "a=ice-pwd:0123456789abcdef0123") ||
        !contains(text, "a=fingerprint:sha-256 11:22:33") ||
        !contains(text, "m=audio 9 UDP/TLS/RTP/SAVPF 120") ||
        !contains(text, "m=video 9 UDP/TLS/RTP/SAVPF 105 106") ||
        !contains(text, "a=fmtp:106 apt=105") ||
        !contains(text, "a=ssrc:333 cname:imd"))
    {
        log_line("sdp: в предложении не хватает обязательных строк");
        return false;
    }

    // And it has to survive a buffer that cannot hold it, rather than writing
    // a truncated description that would be sent anyway.
    char cramped[64];
    if (sdp::build_offer(cramped, sizeof(cramped), &o) != 0)
    {
        log_line("sdp: обрезанное предложение выдано за целое");
        return false;
    }

    log_line("sdp: самопроверка пройдена");
    return true;
}
