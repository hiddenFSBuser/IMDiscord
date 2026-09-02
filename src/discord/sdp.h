#pragma once

// The session description discord speaks when a voice connection is negotiated
// as webrtc instead of as its own udp protocol.
//
// Two shapes, and they are not the same shape.
//
// What goes out is a complete offer: media sections, codecs, extensions, the
// lot. What comes back is not an answer in any sense a webrtc stack would
// accept - five lines about the transport and nothing about the media:
//
//     m=audio 19296 ICE/SDP
//     c=IN IP4 104.29.141.148
//     a=rtcp:19296
//     a=ice-ufrag:Qjnz
//     a=ice-pwd:x6whuONccSDsHoC5I9Dxuq
//     a=fingerprint:sha-256 04:02:36:...
//     a=candidate:1 1 UDP 2130706431 104.29.141.148 19296 typ host
//
// That is not a defect on their side. The codecs were settled by the codecs
// list in SELECT_PROTOCOL, so the answer carries only what the offer could not
// know: where to send, who to greet, and whose certificate to expect.
//
// A stack that consumes sdp has to be handed a rebuilt answer with media
// sections invented to match the offer. This client does not need that step:
// the transport underneath takes ice credentials and a fingerprint directly,
// so the five values are used as values and never turned back into text.
//
// One thing worth noticing in the reply: the candidate is UDP, and it is the
// only one. Discord is ice-lite, publishes a single host candidate and does no
// trickle. There is no tcp candidate to be had, which is why "webrtc" is a
// different packet format rather than a different way out of the machine.

namespace sdp
{
    // Everything discord's reply actually carries.
    struct answer
    {
        char ip[64];
        char ufrag[72];
        char pwd[144];

        // Kept as written, hash algorithm and all: "sha-256 04:02:36:...".
        char fingerprint[192];

        // The whole a=candidate line, for the log and for the address.
        char candidate[192];

        unsigned short port;
        bool candidate_is_udp;

        bool ok() const
        {
            return ip[0] && ufrag[0] && pwd[0] && fingerprint[0] && port != 0;
        }
    };

    bool parse_answer(const char* text, answer* out);

    // What this client offers. The ssrcs are the ones the voice websocket
    // already handed out in READY, so both sides agree on them without the sdp
    // having to negotiate anything.
    struct offer
    {
        const char* ufrag;
        const char* pwd;
        const char* fingerprint;      // "sha-256 AA:BB:..."
        const char* cname;

        unsigned int audio_ssrc;
        unsigned int video_ssrc;
        unsigned int rtx_ssrc;

        int opus_payload;
        int h264_payload;
        int h264_rtx_payload;
    };

    // Writes the offer and returns its length, or 0 when it does not fit.
    int build_offer(char* out, int cap, const offer* o);

    bool self_test();
}
