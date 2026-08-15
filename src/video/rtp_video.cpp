#include "pch.h"
#include "rtp_video.h"

namespace
{
    // Every video packet carries a whole set of extensions, not one. The ids
    // below are the ones the current protocol uses, and they are not the ones
    // an older SDP dump would show: id 5 now means the transport sequence
    // number, so a lone playout-delay written there is read as a broken
    // congestion control counter.
    //
    // The stream id matters most of all. op 12 declares a stream under a rid,
    // and this is what ties each packet back to it; without it the server sees
    // the media arrive and has nothing to attach it to.
    // A sender's numbering. A capture shows different ids on the packets coming
    // back from the server, but those describe the other direction and are not
    // what a client writes.
    const int EXT_TRANSMISSION_OFFSET = 2;
    const int EXT_ABS_SEND_TIME = 3;
    const int EXT_VIDEO_ORIENTATION = 4;
    const int EXT_TRANSPORT_SEQUENCE = 5;
    const int EXT_PLAYOUT_DELAY = 6;
    const int EXT_VIDEO_CONTENT_TYPE = 7;
    const int EXT_VIDEO_TIMING = 8;
    const int EXT_MEDIA_STREAM_TYPE = 10;
    const int EXT_RID = 11;
    const int EXT_REPAIRED_RID = 12;

    // Profile 0xBEDE marks the one-byte format, then a 16 bit count of the
    // 32 bit words that follow.
    const int EXT_HEADER_BYTES = 4;

    // Nine extensions, each a byte of id and length plus its value, padded out
    // to a whole number of words.
    const int EXT_PAYLOAD_BYTES = 44;

    const int RTP_HEADER_BYTES = 12;

    void put_u16(unsigned char* p, unsigned int v)
    {
        p[0] = (unsigned char)(v >> 8);
        p[1] = (unsigned char)(v);
    }

    void put_u32(unsigned char* p, unsigned int v)
    {
        p[0] = (unsigned char)(v >> 24);
        p[1] = (unsigned char)(v >> 16);
        p[2] = (unsigned char)(v >> 8);
        p[3] = (unsigned char)(v);
    }

    // The RTP header and the extension header, which travel unencrypted and are
    // authenticated as additional data.
    int build_aad(rtp_video* v, bool marker, unsigned char* out)
    {
        // Version 2, no padding, extension present, no contributing sources.
        out[0] = (2 << 6) | 0x10;
        out[1] = (unsigned char)((marker ? 0x80 : 0x00) | (v->payload_type & 0x7F));
        put_u16(out + 2, v->sequence);
        put_u32(out + 4, v->timestamp);
        put_u32(out + 8, v->ssrc);

        v->sequence = (unsigned short)(v->sequence + 1);

        out[12] = 0xBE;
        out[13] = 0xDE;
        put_u16(out + 14, EXT_PAYLOAD_BYTES / 4);
        return RTP_HEADER_BYTES + EXT_HEADER_BYTES;
    }

    // One extension element: a byte holding the id and the length, then the
    // value. The length nibble stores one less than the real length.
    int put_ext(unsigned char* out, int id, const unsigned char* value, int len)
    {
        out[0] = (unsigned char)((id << 4) | (len - 1));
        for (int i = 0; i < len; i++) out[1 + i] = value[i];
        return 1 + len;
    }

    // Absolute send time: seconds since the epoch, wrapped at 64 and kept as
    // 6.18 fixed point in 24 bits. Receivers use it to measure how packets are
    // spread over time, so it has to be real rather than zero.
    void abs_send_time(unsigned char out[3])
    {
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        unsigned long long ticks = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;

        // 100 ns units since 1601; 11644473600 seconds of that is the offset to
        // 1970, which is what the field counts from.
        unsigned long long since_epoch = ticks - 11644473600ULL * 10000000ULL;
        unsigned long long fraction = since_epoch % (64ULL * 10000000ULL);
        unsigned int value = (unsigned int)((fraction * 262144ULL) / 10000000ULL) & 0xFFFFFF;

        out[0] = (unsigned char)(value >> 16);
        out[1] = (unsigned char)(value >> 8);
        out[2] = (unsigned char)(value);
    }

    // Keeps a copy of what just went out, so it can be sent again if asked.
    void remember(rtp_video* v, unsigned short seq, bool marker,
                  const unsigned char* media, int len)
    {
        if (len <= 0 || len > (int)sizeof(v->cache[0].data)) return;

        rtp_video_cached* slot = &v->cache[v->cache_at % (sizeof(v->cache) / sizeof(v->cache[0]))];
        v->cache_at++;

        slot->sequence = seq;
        slot->timestamp = v->timestamp;
        slot->marker = marker;
        slot->len = len;
        slot->used = true;
        ccpy(slot->data, media, (size_t)len);
    }

    // The extension body, which is encrypted along with the video bytes.
    // A retransmission names itself with the repaired stream id rather than the
    // ordinary one, which is how the far end tells the two apart.
    int build_ext_payload(rtp_video* v, unsigned char* out, bool repaired = false);

    int build_ext_payload(rtp_video* v, unsigned char* out, bool repaired)
    {
        const unsigned char zero3[3] = { 0, 0, 0 };
        const unsigned char zero13[13] = { 0 };
        const unsigned char zero1[1] = { 0 };

        int n = 0;
        n += put_ext(out + n, EXT_TRANSMISSION_OFFSET, zero3, 3);

        unsigned char send_time[3];
        abs_send_time(send_time);
        n += put_ext(out + n, EXT_ABS_SEND_TIME, send_time, 3);

        n += put_ext(out + n, EXT_VIDEO_ORIENTATION, zero1, 1);

        unsigned char seq[2];
        seq[0] = (unsigned char)(v->transport_sequence >> 8);
        seq[1] = (unsigned char)(v->transport_sequence);
        v->transport_sequence = (unsigned short)(v->transport_sequence + 1);
        n += put_ext(out + n, EXT_TRANSPORT_SEQUENCE, seq, 2);

        n += put_ext(out + n, EXT_PLAYOUT_DELAY, zero3, 3);

        unsigned char content = (unsigned char)(v->content_type ? 1 : 0);
        n += put_ext(out + n, EXT_VIDEO_CONTENT_TYPE, &content, 1);

        n += put_ext(out + n, EXT_VIDEO_TIMING, zero13, 13);

        // Both of these are ascii, and both are what op 12 declared.
        const unsigned char screen[6] = { 's', 'c', 'r', 'e', 'e', 'n' };
        const unsigned char camera[5] = { 'v', 'i', 'd', 'e', 'o' };
        if (v->content_type) n += put_ext(out + n, EXT_MEDIA_STREAM_TYPE, screen, 6);
        else                 n += put_ext(out + n, EXT_MEDIA_STREAM_TYPE, camera, 5);

        n += put_ext(out + n, repaired ? EXT_REPAIRED_RID : EXT_RID,
                     (const unsigned char*)v->rid, v->rid_len);

        while (n < EXT_PAYLOAD_BYTES) out[n++] = 0;
        return EXT_PAYLOAD_BYTES;
    }

    // Finds the next start code at or after p. Returns null when there is none,
    // and reports whether it was three or four bytes long.
    const unsigned char* find_start_code(const unsigned char* p, const unsigned char* end,
                                         int* code_len)
    {
        for (const unsigned char* q = p; q + 3 <= end; q++)
        {
            if (q[0] != 0 || q[1] != 0) continue;

            if (q[2] == 1) { *code_len = 3; return q; }
            if (q[2] == 0 && q + 4 <= end && q[3] == 1) { *code_len = 4; return q; }
        }
        return 0;
    }
}

void rtpvid::init(rtp_video* v, unsigned int ssrc, unsigned char payload_type, const char* rid)
{
    ccfset(v, 0, sizeof(*v));
    v->ssrc = ssrc;
    v->payload_type = payload_type;

    if (!rid || !rid[0]) rid = "100";
    ccstrncpy(v->rid, rid, sizeof(v->rid) - 1);
    v->rid_len = (int)ccslenf(v->rid);
}

bool rtpvid::resend(rtp_video* v, unsigned short sequence, int mtu,
                    void* user, rtp_video_sink sink)
{
    if (!v || !sink || !v->rtx_ssrc) return false;

    const unsigned int slots = sizeof(v->cache) / sizeof(v->cache[0]);
    rtp_video_cached* found = 0;
    for (unsigned int i = 0; i < slots; i++)
        if (v->cache[i].used && v->cache[i].sequence == sequence) { found = &v->cache[i]; break; }

    if (!found) return false;

    // Two bytes of original sequence number go in front of the media, and the
    // whole thing still has to fit.
    if (EXT_PAYLOAD_BYTES + 2 + found->len > mtu) return false;

    unsigned char aad[RTP_HEADER_BYTES + EXT_HEADER_BYTES];
    aad[0] = (2 << 6) | 0x10;
    aad[1] = (unsigned char)((found->marker ? 0x80 : 0x00) | (v->rtx_payload_type & 0x7F));
    // Its own sequence space: the retransmission stream is a stream in its own
    // right, and the original number travels in the payload instead.
    put_u16(aad + 2, v->rtx_sequence);
    v->rtx_sequence = (unsigned short)(v->rtx_sequence + 1);
    put_u32(aad + 4, found->timestamp);
    put_u32(aad + 8, v->rtx_ssrc);
    aad[12] = 0xBE;
    aad[13] = 0xDE;
    put_u16(aad + 14, EXT_PAYLOAD_BYTES / 4);

    int n = build_ext_payload(v, v->body, true);
    put_u16(v->body + n, sequence);
    n += 2;
    ccpy(v->body + n, found->data, (size_t)found->len);
    n += found->len;

    return sink(user, aad, RTP_HEADER_BYTES + EXT_HEADER_BYTES, v->body, n);
}

int rtpvid::send_frame(rtp_video* v, const unsigned char* annexb, int len, int mtu,
                       void* user, rtp_video_sink sink)
{
    if (!v || !annexb || len <= 0 || !sink) return -1;

    // mtu bounds the encrypted body, which opens with the extension payload on
    // every packet and spends two more bytes on the fragmentation header when
    // the NAL has to be split.
    const int overhead = EXT_PAYLOAD_BYTES + 2;
    if (mtu <= overhead || mtu > (int)sizeof(v->body)) return -1;

    const unsigned char* end = annexb + len;

    int first_code = 0;
    const unsigned char* cursor = find_start_code(annexb, end, &first_code);
    if (!cursor) return -1;             // not Annex-B at all

    int sent = 0;

    while (cursor)
    {
        int code_len = first_code;
        const unsigned char* nal = cursor + code_len;

        int next_code = 0;
        const unsigned char* next = find_start_code(nal, end, &next_code);
        const unsigned char* nal_end = next ? next : end;

        int nal_len = (int)(nal_end - nal);
        cursor = next;
        first_code = next_code;

        if (nal_len <= 0) continue;

        // Only the very last NAL of the frame carries the marker bit, and only
        // on its final packet: that is what tells the far end the picture is
        // complete.
        bool last_nal = (cursor == 0);

        if (nal_len + EXT_PAYLOAD_BYTES <= mtu)
        {
            // Small enough to travel whole.
            unsigned char aad[RTP_HEADER_BYTES + EXT_HEADER_BYTES];
            int aad_len = build_aad(v, last_nal, aad);

            int n = build_ext_payload(v, v->body);
            ccpy(v->body + n, nal, (size_t)nal_len);
            n += nal_len;

            remember(v, (unsigned short)((aad[2] << 8) | aad[3]), last_nal, nal, nal_len);

            if (!sink(user, aad, aad_len, v->body, n)) return -1;
            sent++;
            continue;
        }

        // Too big, so it goes out as Fragmentation Unit A. The original NAL
        // header is replaced by an indicator carrying its own importance bits,
        // and a fragment header carrying the type plus start and end flags.
        const unsigned char nal_header = nal[0];
        const unsigned char nal_type = (unsigned char)(nal_header & 0x1F);
        const unsigned char nal_nri = (unsigned char)(nal_header & 0x60);

        const unsigned char* data = nal + 1;         // the header is not resent
        int left = nal_len - 1;
        const int room = mtu - overhead;

        bool first_fragment = true;
        while (left > 0)
        {
            int take = left < room ? left : room;
            bool final_fragment = (take == left);

            unsigned char aad[RTP_HEADER_BYTES + EXT_HEADER_BYTES];
            int aad_len = build_aad(v, last_nal && final_fragment, aad);

            int n = build_ext_payload(v, v->body);

            // Type 28 is FU-A. The indicator keeps the importance bits so a
            // router dropping packets still drops the least useful ones.
            v->body[n++] = (unsigned char)(nal_nri | 28);
            v->body[n++] = (unsigned char)((first_fragment ? 0x80 : 0x00) |
                                           (final_fragment ? 0x40 : 0x00) |
                                           nal_type);

            ccpy(v->body + n, data, (size_t)take);
            n += take;

            // The fragment header belongs to the media half, so it is cached
            // with it: a retransmission has to arrive as the same fragment.
            remember(v, (unsigned short)((aad[2] << 8) | aad[3]),
                     last_nal && final_fragment,
                     v->body + EXT_PAYLOAD_BYTES, n - EXT_PAYLOAD_BYTES);

            if (!sink(user, aad, aad_len, v->body, n)) return -1;
            sent++;

            data += take;
            left -= take;
            first_fragment = false;
        }
    }

    return sent;
}

// ---------------------------------------------------------------------------
// Receiving
// ---------------------------------------------------------------------------

namespace
{
    const unsigned char START_CODE[4] = { 0, 0, 0, 1 };

    bool rx_reserve(rtp_h264_rx* r, unsigned int need)
    {
        if (need <= r->cap) return true;

        unsigned int cap = r->cap ? r->cap : 65536;
        while (cap < need)
        {
            // A frame that will not fit in a couple of megabytes is not a frame.
            if (cap >= (16u << 20)) return false;
            cap *= 2;
        }

        unsigned char* fresh = (unsigned char*)memalloc((int)cap);
        if (!fresh) return false;

        if (r->frame)
        {
            ccpy(fresh, r->frame, (size_t)r->len);
            memfree(r->frame);
        }
        r->frame = fresh;
        r->cap = cap;
        return true;
    }

    // One whole NAL unit, start code and all.
    bool rx_append_nal(rtp_h264_rx* r, const unsigned char* nal, unsigned int len)
    {
        if (!len) return false;
        if (!rx_reserve(r, r->len + 4 + len)) return false;

        ccpy(r->frame + r->len, START_CODE, 4);
        r->len += 4;
        ccpy(r->frame + r->len, nal, (size_t)len);
        r->len += len;
        return true;
    }

    // Raw bytes, for the continuation of a fragmented NAL.
    bool rx_append_raw(rtp_h264_rx* r, const unsigned char* data, unsigned int len)
    {
        if (!len) return true;
        if (!rx_reserve(r, r->len + len)) return false;

        ccpy(r->frame + r->len, data, (size_t)len);
        r->len += len;
        return true;
    }

    // Type 24: several small NAL units in one packet, each behind a two byte
    // length. The parameter sets usually travel this way.
    bool rx_push_stap_a(rtp_h264_rx* r, const unsigned char* p, unsigned int len)
    {
        unsigned int at = 1;
        while (at + 2 <= len)
        {
            unsigned int nal_len = ((unsigned int)p[at] << 8) | p[at + 1];
            at += 2;
            if (nal_len == 0 || at + nal_len > len) return false;
            if (!rx_append_nal(r, p + at, nal_len)) return false;
            at += nal_len;
        }
        return at == len;
    }

    // Type 28: one NAL cut across several packets. The original header is
    // rebuilt from the indicator's importance bits and the fragment header's
    // type, and only the first fragment writes it.
    bool rx_push_fu_a(rtp_h264_rx* r, const unsigned char* p, unsigned int len)
    {
        if (len < 2) return false;

        unsigned char indicator = p[0];
        unsigned char header = p[1];
        bool start = (header & 0x80) != 0;
        unsigned char nal_type = (unsigned char)(header & 0x1F);
        if (nal_type == 0 || nal_type > 23) return false;

        if (start)
        {
            unsigned char rebuilt = (unsigned char)((indicator & 0xE0) | nal_type);
            if (!rx_reserve(r, r->len + 5)) return false;
            ccpy(r->frame + r->len, START_CODE, 4);
            r->len += 4;
            r->frame[r->len++] = rebuilt;
        }
        else if (r->len == 0)
        {
            // The opening fragment never arrived, so there is nothing to
            // continue and the rest of the frame is worthless.
            r->discarding = true;
            return true;
        }

        return rx_append_raw(r, p + 2, len - 2);
    }

    bool rx_finish(rtp_h264_rx* r, const unsigned char** out, unsigned int* out_len)
    {
        unsigned int len = r->len;

        r->len = 0;
        r->have_timestamp = false;
        r->have_last = false;
        r->discarding = false;

        if (!len) return false;

        *out = r->frame;
        *out_len = len;
        return true;
    }
}

void rtpvid::rx_init(rtp_h264_rx* r)
{
    ccfset(r, 0, sizeof(*r));
}

void rtpvid::rx_free(rtp_h264_rx* r)
{
    if (r->frame) memfree(r->frame);
    ccfset(r, 0, sizeof(*r));
}

void rtpvid::rx_reset(rtp_h264_rx* r)
{
    r->len = 0;
    r->have_timestamp = false;
    r->have_last = false;
    r->discarding = false;
}

void rtpvid::rx_skip(rtp_h264_rx* r, unsigned short sequence)
{
    if (!r) return;
    // Only when it really is the next one: a stray old packet must not drag the
    // counter backwards and invent a gap of its own.
    if (r->have_last && (unsigned short)(r->last_sequence + 1) != sequence) return;

    r->last_sequence = sequence;
    r->have_last = true;
}

bool rtpvid::rx_push(rtp_h264_rx* r, const unsigned char* payload, int len,
                     bool marker, unsigned short sequence, unsigned int timestamp,
                     const unsigned char** out, unsigned int* out_len)
{
    if (!r || !payload || len <= 0) return false;

    // A new timestamp starts a new picture whether or not the last one was ever
    // closed; a break in the numbering inside one picture ruins it.
    if (!r->have_timestamp || r->timestamp != timestamp)
    {
        if (r->len) r->dropped++;
        r->timestamp = timestamp;
        r->have_timestamp = true;
        r->have_last = false;
        r->discarding = false;
        r->len = 0;
    }
    else if (r->have_last && (unsigned short)(r->last_sequence + 1) != sequence)
    {
        if (!r->discarding) r->dropped++;
        r->discarding = true;
        r->len = 0;
    }

    r->last_sequence = sequence;
    r->have_last = true;

    if (r->discarding)
    {
        if (marker) { r->len = 0; rx_reset(r); }
        return false;
    }

    bool ok = true;
    unsigned char type = (unsigned char)(payload[0] & 0x1F);

    if (type >= 1 && type <= 23)      ok = rx_append_nal(r, payload, (unsigned int)len);
    else if (type == 24)              ok = rx_push_stap_a(r, payload, (unsigned int)len);
    else if (type == 28)              ok = rx_push_fu_a(r, payload, (unsigned int)len);
    else                              ok = false;

    if (!ok)
    {
        r->dropped++;
        r->discarding = true;
        r->len = 0;
        return false;
    }

    if (!marker) return false;
    return rx_finish(r, out, out_len);
}
