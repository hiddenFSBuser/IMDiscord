#include "pch.h"
#include "rtcp.h"
#include "core/log.h"

namespace
{
    // ---- what is known about one stream coming in -------------------------

    struct source
    {
        unsigned int ssrc;
        bool video;
        bool used;

        // Sequence bookkeeping, in the shape rfc 3550 appendix A.1 describes:
        // the wire carries sixteen bits and the arithmetic needs more, so the
        // wraps are counted separately and added back on.
        unsigned int cycles;
        unsigned short last_seq;
        unsigned int base_seq;
        unsigned int received;
        unsigned int expected_prior;
        unsigned int received_prior;
        bool started;

        // Jitter, kept in the fixed point form the report field wants: the
        // running mean of how much the gap between two packets differed
        // between the sender's clock and ours.
        unsigned int jitter_q4;
        int last_transit;
        bool have_transit;

        // The middle thirty two bits of the last sender report's timestamp,
        // and when it reached us. Together they let the far side work out the
        // round trip without either side owning a clock the other trusts.
        unsigned int last_sr_middle;
        unsigned long long last_sr_at;

        // Sequence numbers seen to be missing and not yet asked for.
        unsigned short missing[64];
        int missing_count;
    };

    const int MAX_SOURCES = 24;

    source g_sources[MAX_SOURCES];
    unsigned long long g_last_report_at = 0;

    // ---- what the far side says about us ----------------------------------

    int g_bitrate = 64000;
    int g_loss_percent = 0;
    int g_remb = 0;

    const int BITRATE_FLOOR = 16000;
    const int BITRATE_CEILING = 96000;

    source* find(unsigned int ssrc, bool video, bool make)
    {
        for (int i = 0; i < MAX_SOURCES; i++)
            if (g_sources[i].used && g_sources[i].ssrc == ssrc) return &g_sources[i];

        if (!make) return 0;

        for (int i = 0; i < MAX_SOURCES; i++)
            if (!g_sources[i].used)
            {
                source* s = &g_sources[i];
                ccfset(s, 0, sizeof(*s));

                s->used = true;
                s->ssrc = ssrc;
                s->video = video;
                return s;
            }

        return 0;
    }

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

    unsigned int get32(const unsigned char* p)
    {
        return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) |
               ((unsigned int)p[2] << 8) | (unsigned int)p[3];
    }

    unsigned int get16(const unsigned char* p)
    {
        return ((unsigned int)p[0] << 8) | (unsigned int)p[1];
    }

    void remember_missing(source* s, unsigned short from, unsigned short to)
    {
        // A gap of more than a handful is a route that changed or a stream
        // that restarted, not something worth asking to have sent again.
        int span = (int)(unsigned short)(to - from);
        if (span <= 0 || span > 32) return;

        for (unsigned short q = from; q != to; q++)
        {
            if (s->missing_count >= 64) return;
            s->missing[s->missing_count++] = q;
        }
    }

    // ---- steering the encoder ---------------------------------------------

    void loss_reported(unsigned int fraction)
    {
        // The field is a fraction of 256, so 26 is about a tenth.
        g_loss_percent = (int)((fraction * 100) / 256);

        int before = g_bitrate;

        if (fraction > 26)
        {
            // Back off quickly. Losing a tenth of the packets means the path
            // is already past what it can carry, and easing down gently from
            // there just prolongs it.
            g_bitrate = (g_bitrate * 85) / 100;
        }
        else if (fraction < 5)
        {
            // Climb slowly. The only way to find out there is more room is to
            // use a little more and watch, and doing that in big steps turns
            // a healthy link into a congested one.
            g_bitrate = (g_bitrate * 105) / 100;
        }

        if (g_remb && g_bitrate > g_remb) g_bitrate = g_remb;
        if (g_bitrate < BITRATE_FLOOR) g_bitrate = BITRATE_FLOOR;
        if (g_bitrate > BITRATE_CEILING) g_bitrate = BITRATE_CEILING;

        if (g_bitrate != before)
            log_line("rtcp: потери %d%%, битрейт %d -> %d",
                     g_loss_percent, before, g_bitrate);
    }

    void remb_reported(int bits)
    {
        if (bits <= 0) return;

        g_remb = bits;

        if (g_bitrate > g_remb)
        {
            log_line("rtcp: remb просит не больше %d, битрейт %d -> %d",
                     g_remb, g_bitrate, g_remb < BITRATE_FLOOR ? BITRATE_FLOOR : g_remb);

            g_bitrate = g_remb;
            if (g_bitrate < BITRATE_FLOOR) g_bitrate = BITRATE_FLOOR;
        }
    }
}

void rtcp::reset()
{
    ccfset(g_sources, 0, sizeof(g_sources));

    g_last_report_at = 0;
    g_bitrate = 64000;
    g_loss_percent = 0;
    g_remb = 0;
}

void rtcp::on_media(unsigned int ssrc, unsigned short seq, unsigned int rtp_timestamp,
                    bool video)
{
    source* s = find(ssrc, video, true);
    if (!s) return;

    if (!s->started)
    {
        s->started = true;
        s->base_seq = seq;
        s->last_seq = seq;
        s->received = 1;
        return;
    }

    s->received++;

    // Jitter: how much the spacing between two packets differed on the
    // sender's clock and on ours. Kept as the running mean rfc 3550 defines,
    // in sixteenths so the arithmetic stays in integers.
    {
        unsigned int rate = video ? 90000u : 48000u;
        unsigned int arrival = (unsigned int)((GetTickCount64() * rate) / 1000ull);

        int transit = (int)(arrival - rtp_timestamp);

        if (s->have_transit)
        {
            int d = transit - s->last_transit;
            if (d < 0) d = -d;

            s->jitter_q4 += (unsigned int)d - ((s->jitter_q4 + 8) >> 4);
        }

        s->last_transit = transit;
        s->have_transit = true;
    }

    unsigned short expected_next = (unsigned short)(s->last_seq + 1);

    if (seq == expected_next)
    {
        s->last_seq = seq;
    }
    else
    {
        // Ahead of where the count was: whatever lies between is missing, and
        // the wrap is counted when the number goes backwards past it.
        unsigned short ahead = (unsigned short)(seq - s->last_seq);

        if (ahead < 0x8000)
        {
            if (seq < s->last_seq) s->cycles += 0x10000;

            if (s->video) remember_missing(s, expected_next, seq);
            s->last_seq = seq;
        }
        // Behind: a reordering or the retransmission of something asked for.
        // Neither is a loss and neither moves the high water mark.
    }
}

void rtcp::on_control(const unsigned char* p, int len, unsigned int self_ssrc)
{
    int at = 0;

    // A control packet is a compound of several: each carries its own length
    // in words, and stopping at the first is how half the feedback in a call
    // goes unread.
    while (at + 4 <= len)
    {
        int type = p[at + 1];
        int words = (int)get16(p + at + 2);
        int size = (words + 1) * 4;

        if (size <= 0 || at + size > len) break;

        const unsigned char* body = p + at;
        int count = body[0] & 0x1F;

        if (type == 200 && size >= 28)
        {
            // Sender report. Only two things in it are wanted: whose it is,
            // and the middle of its timestamp, which comes back in our next
            // report so the sender can measure the round trip.
            unsigned int ssrc = get32(body + 4);

            source* s = find(ssrc, false, false);
            if (s)
            {
                s->last_sr_middle = (get32(body + 8) << 16) | (get32(body + 12) >> 16);
                s->last_sr_at = GetTickCount64();
            }
        }
        else if ((type == 200 || type == 201))
        {
            // Report blocks about somebody. The ones about us are the point:
            // they carry how much of what we sent went missing.
            int blocks_at = (type == 200) ? 28 : 8;

            for (int i = 0; i < count; i++)
            {
                int b = blocks_at + i * 24;
                if (b + 24 > size) break;

                if (get32(body + b) == self_ssrc)
                    loss_reported(body[b + 4]);
            }
        }
        else if (type == 206 && count == 15 && size >= 20)
        {
            // Remb: "application layer feedback", identified by four ascii
            // bytes rather than by a number.
            if (body[12] == 'R' && body[13] == 'E' && body[14] == 'M' && body[15] == 'B')
            {
                unsigned int exponent = (unsigned int)(body[17] >> 2);
                unsigned int mantissa = (((unsigned int)(body[17] & 0x03)) << 16) |
                                        ((unsigned int)body[18] << 8) | body[19];

                // The field is a mantissa shifted by an exponent, which can
                // name numbers far larger than any link; anything absurd is
                // treated as no opinion at all.
                if (exponent <= 40)
                {
                    unsigned long long bits = (unsigned long long)mantissa << exponent;
                    if (bits > 0 && bits < 100000000ull) remb_reported((int)bits);
                }
            }
        }

        at += size;
    }
}

int rtcp::build(unsigned int self_ssrc, unsigned char* out, int cap)
{
    unsigned long long now = GetTickCount64();

    if (!g_last_report_at) { g_last_report_at = now; return 0; }
    if (now - g_last_report_at < 1000) return 0;

    g_last_report_at = now;

    int blocks = 0;
    for (int i = 0; i < MAX_SOURCES; i++)
        if (g_sources[i].used && g_sources[i].started) blocks++;

    if (!blocks) return 0;
    if (blocks > 31) blocks = 31;

    if (8 + blocks * 24 > cap) return 0;

    out[0] = (unsigned char)(0x80 | blocks);
    out[1] = 201;                                  // receiver report
    put16(out + 2, (unsigned int)(1 + blocks * 6));
    put32(out + 4, self_ssrc);

    int at = 8;
    int written = 0;

    for (int i = 0; i < MAX_SOURCES && written < blocks; i++)
    {
        source* s = &g_sources[i];
        if (!s->used || !s->started) continue;

        unsigned int extended = s->cycles + s->last_seq;
        unsigned int expected = extended - s->base_seq + 1;

        unsigned int expected_interval = expected - s->expected_prior;
        unsigned int received_interval = s->received - s->received_prior;

        s->expected_prior = expected;
        s->received_prior = s->received;

        unsigned int lost_interval = expected_interval > received_interval
                                   ? expected_interval - received_interval : 0;

        unsigned int fraction = expected_interval
                              ? (lost_interval * 256) / expected_interval : 0;
        if (fraction > 255) fraction = 255;

        unsigned int cumulative = expected > s->received ? expected - s->received : 0;
        if (cumulative > 0x7FFFFF) cumulative = 0x7FFFFF;

        unsigned int dlsr = 0;
        if (s->last_sr_at)
        {
            // In units of 1/65536 of a second, which is what the field is.
            unsigned long long delta = now - s->last_sr_at;
            dlsr = (unsigned int)((delta * 65536ull) / 1000ull);
        }

        put32(out + at, s->ssrc);
        out[at + 4] = (unsigned char)fraction;
        out[at + 5] = (unsigned char)(cumulative >> 16);
        out[at + 6] = (unsigned char)(cumulative >> 8);
        out[at + 7] = (unsigned char)(cumulative);
        put32(out + at + 8, extended);
        put32(out + at + 12, s->jitter_q4 >> 4);
        put32(out + at + 16, s->last_sr_middle);
        put32(out + at + 20, dlsr);

        at += 24;
        written++;
    }

    // ---- and the nacks, one message per source that is missing something --
    for (int i = 0; i < MAX_SOURCES; i++)
    {
        source* s = &g_sources[i];
        if (!s->used || !s->missing_count) continue;

        // Each entry names one sequence number and, in a bitmask, up to
        // sixteen that follow it. Packing them this way is what keeps a
        // request for a whole burst inside one small message.
        unsigned char fci[64 * 4];
        int pairs = 0;
        int m = 0;

        while (m < s->missing_count && pairs < 16)
        {
            unsigned short pid = s->missing[m++];
            unsigned int blp = 0;

            while (m < s->missing_count)
            {
                int delta = (int)(unsigned short)(s->missing[m] - pid);
                if (delta < 1 || delta > 16) break;

                blp |= 1u << (delta - 1);
                m++;
            }

            put16(fci + pairs * 4, pid);
            put16(fci + pairs * 4 + 2, blp);
            pairs++;
        }

        s->missing_count = 0;

        int size = 12 + pairs * 4;
        if (!pairs || at + size > cap) continue;

        out[at] = 0x80 | 1;                        // generic nack
        out[at + 1] = 205;                         // transport layer feedback
        put16(out + at + 2, (unsigned int)(2 + pairs));
        put32(out + at + 4, self_ssrc);
        put32(out + at + 8, s->ssrc);
        ccpy(out + at + 12, fci, (size_t)(pairs * 4));

        at += size;
    }

    return at;
}

int rtcp::audio_bitrate() { return g_bitrate; }
int rtcp::reported_loss_percent() { return g_loss_percent; }
int rtcp::requested_bitrate() { return g_remb; }

bool rtcp::self_test()
{
    rtcp::reset();

    // ---- a clean stream reports no loss ----------------------------------
    for (int i = 0; i < 100; i++)
        rtcp::on_media(0x11223344, (unsigned short)(1000 + i), 0, false);

    unsigned char out[512];

    // The first call only starts the clock; reports are due a second apart.
    if (rtcp::build(0xAABBCCDD, out, sizeof(out)) != 0)
    {
        log_line("rtcp: самопроверка: отчёт ушёл раньше срока");
        return false;
    }

    g_last_report_at = GetTickCount64() - 2000;

    int n = rtcp::build(0xAABBCCDD, out, sizeof(out));
    if (n != 8 + 24)
    {
        log_line("rtcp: самопроверка: отчёт %d байт, ждали %d", n, 8 + 24);
        return false;
    }

    if (out[1] != 201 || (out[0] & 0x1F) != 1)
    {
        log_line("rtcp: самопроверка: не тот тип отчёта");
        return false;
    }

    if (get16(out + 2) != 7)
    {
        log_line("rtcp: самопроверка: длина в словах %u, ждали 7", get16(out + 2));
        return false;
    }

    if (out[12] != 0)
    {
        log_line("rtcp: самопроверка: чистый поток показал потери %u", out[12]);
        return false;
    }

    // Extended highest sequence has to be the last one seen, not the count.
    if (get32(out + 16) != 1099)
    {
        log_line("rtcp: самопроверка: старший номер %u, ждали 1099", get32(out + 16));
        return false;
    }

    // ---- a hole in a video stream turns into a nack ----------------------
    rtcp::reset();

    rtcp::on_media(0x55667788, 40, 0, true);
    rtcp::on_media(0x55667788, 41, 0, true);
    // 42, 43 and 45 never arrive
    rtcp::on_media(0x55667788, 44, 0, true);
    rtcp::on_media(0x55667788, 46, 0, true);

    g_last_report_at = GetTickCount64() - 2000;

    n = rtcp::build(0xAABBCCDD, out, sizeof(out));

    int nack_at = 8 + 24;
    if (n != nack_at + 16)
    {
        log_line("rtcp: самопроверка: с nack вышло %d байт, ждали %d", n, nack_at + 16);
        return false;
    }

    if (out[nack_at + 1] != 205 || (out[nack_at] & 0x1F) != 1)
    {
        log_line("rtcp: самопроверка: nack не той формы");
        return false;
    }

    if (get32(out + nack_at + 8) != 0x55667788)
    {
        log_line("rtcp: самопроверка: nack не про тот поток");
        return false;
    }

    // 42 named directly, 43 one after it, 45 three after it.
    if (get16(out + nack_at + 12) != 42)
    {
        log_line("rtcp: самопроверка: nack начинается с %u, ждали 42",
                 get16(out + nack_at + 12));
        return false;
    }

    unsigned int blp = get16(out + nack_at + 14);
    if (blp != ((1u << 0) | (1u << 2)))
    {
        log_line("rtcp: самопроверка: маска nack %u, ждали %u",
                 blp, (1u << 0) | (1u << 2));
        return false;
    }

    // Audio must not ask for anything back: a retransmitted opus frame is a
    // frame that arrives after the moment it belonged to.
    rtcp::reset();
    rtcp::on_media(0x99, 10, 0, false);
    rtcp::on_media(0x99, 14, 0, false);

    g_last_report_at = GetTickCount64() - 2000;

    n = rtcp::build(0xAABBCCDD, out, sizeof(out));
    if (n != 8 + 24)
    {
        log_line("rtcp: самопроверка: звук попросил повтор");
        return false;
    }

    // ---- what comes back steers the encoder ------------------------------
    rtcp::reset();

    // A receiver report about us saying a fifth was lost.
    unsigned char rr[32];
    ccfset(rr, 0, sizeof(rr));
    rr[0] = 0x80 | 1;
    rr[1] = 201;
    put16(rr + 2, 7);
    put32(rr + 4, 0x12345678);      // whoever is reporting
    put32(rr + 8, 0xAABBCCDD);      // about us
    rr[12] = 51;                    // a fifth of 256

    int before = rtcp::audio_bitrate();
    rtcp::on_control(rr, 8 + 24, 0xAABBCCDD);

    if (rtcp::audio_bitrate() >= before)
    {
        log_line("rtcp: самопроверка: потери не сбавили битрейт (%d -> %d)",
                 before, rtcp::audio_bitrate());
        return false;
    }

    if (rtcp::reported_loss_percent() != 19)
    {
        log_line("rtcp: самопроверка: потери посчитаны как %d%%, ждали 19",
                 rtcp::reported_loss_percent());
        return false;
    }

    // A report about somebody else must not touch anything.
    int steady = rtcp::audio_bitrate();
    put32(rr + 8, 0x0BADF00D);
    rtcp::on_control(rr, 8 + 24, 0xAABBCCDD);

    if (rtcp::audio_bitrate() != steady)
    {
        log_line("rtcp: самопроверка: чужой отчёт изменил битрейт");
        return false;
    }

    // ---- remb puts a ceiling on it ---------------------------------------
    unsigned char remb[24];
    ccfset(remb, 0, sizeof(remb));
    remb[0] = 0x80 | 15;
    remb[1] = 206;
    put16(remb + 2, 5);
    put32(remb + 4, 0x12345678);
    put32(remb + 8, 0);
    remb[12] = 'R'; remb[13] = 'E'; remb[14] = 'M'; remb[15] = 'B';
    remb[16] = 1;                                  // one ssrc follows
    // 24000 = 3000 << 3
    remb[17] = (unsigned char)((3 << 2) | 0);
    remb[18] = (unsigned char)(3000 >> 8);
    remb[19] = (unsigned char)(3000 & 0xFF);

    rtcp::on_control(remb, 24, 0xAABBCCDD);

    if (rtcp::requested_bitrate() != 24000)
    {
        log_line("rtcp: самопроверка: remb разобран как %d, ждали 24000",
                 rtcp::requested_bitrate());
        return false;
    }

    if (rtcp::audio_bitrate() > 24000)
    {
        log_line("rtcp: самопроверка: битрейт %d выше потолка remb",
                 rtcp::audio_bitrate());
        return false;
    }

    // ---- a compound packet has to be walked to the end -------------------
    rtcp::reset();

    unsigned char both[64];
    ccfset(both, 0, sizeof(both));

    // An empty receiver report first, then the remb behind it.
    both[0] = 0x80;
    both[1] = 201;
    put16(both + 2, 1);
    put32(both + 4, 0x12345678);
    ccpy(both + 8, remb, 24);

    rtcp::on_control(both, 8 + 24, 0xAABBCCDD);

    if (rtcp::requested_bitrate() != 24000)
    {
        log_line("rtcp: самопроверка: второй пакет в составном не прочитан");
        return false;
    }

    rtcp::reset();

    log_line("rtcp: самопроверка пройдена");
    return true;
}
