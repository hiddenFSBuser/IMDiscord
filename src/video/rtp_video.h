#pragma once

// H.264 over Discord's video RTP, and the notes needed to wire the rest of it.
//
// ---------------------------------------------------------------------------
// How a "Go Live" stream is set up
// ---------------------------------------------------------------------------
// A stream is a second voice connection, separate from the one carrying the
// microphone. On the main gateway:
//
//   op 4  VOICE_STATE_UPDATE  { guild_id, channel_id, self_mute, self_deaf,
//                               self_video }
//   op 18 STREAM_CREATE       { type: "guild", guild_id, channel_id,
//                               preferred_region: null }
//   op 22 STREAM_SET_PAUSED   { stream_key, paused: false }
//   op 19 STREAM_DELETE       { stream_key }            to stop
//
// The stream key is built by the client, not handed out:
//
//   guild:<guild_id>:<channel_id>:<user_id>       in a server
//   call:<channel_id>:<user_id>                   in a direct call
//
// The server answers with a STREAM_CREATE dispatch carrying rtc_server_id and
// its own stream_key, and a STREAM_SERVER_UPDATE with the token and endpoint
// for the stream's voice websocket. That connection then runs the ordinary
// voice handshake with two differences:
//
//   op 0  IDENTIFY        gains  video: true
//   op 1  SELECT_PROTOCOL gains  codecs: [...]
//   op 12 VIDEO           declares the ssrcs and the stream description
//
// The codec list is fixed, and the payload types are Discord's own:
//
//   opus 120 | H264 101 (rtx 102) | H265 103/104
//   VP8  105/106 | VP9 107/108 | AV1 109/110
//
// op 2 READY gives the audio ssrc; the video and retransmission ssrcs are that
// number plus one and plus two. op 12 then describes the stream:
//
//   { audio_ssrc, video_ssrc, rtx_ssrc,
//     streams: [ { type: "video", rid: "100", ssrc, rtx_ssrc, active: true,
//                  quality: 100, max_bitrate, max_framerate,
//                  max_resolution: { type: "fixed", width, height } } ] }
//
// Turning video off is the same opcode with video_ssrc 0, rtx_ssrc 0 and an
// empty stream list.
//
// ---------------------------------------------------------------------------
// What this file does
// ---------------------------------------------------------------------------
// Everything above is signalling. This is the media path: it cuts an Annex-B
// H.264 frame into RTP packets the way RFC 6184 packetisation-mode 1 wants,
// which means a single NAL unit per packet while it fits and Fragmentation
// Unit A once it does not. Aggregation packets are not produced.
//
// Encryption is left to the caller, because the voice code already has it and
// the two transports share a key. Each packet comes back in two pieces:
//
//   aad   the RTP header plus the four byte extension header. Sent in the clear
//         and fed to the cipher as additional data. A capture of a working
//         client confirms this is where the readable part stops: its outgoing
//         extension bodies are ciphertext, and only the packets coming back
//         from the server have readable ones.
//   body  the extension payload, then the fragmentation header when there is
//         one, then the video bytes. This is what gets encrypted.
//
// The video clock runs at 90 kHz, so a frame at n frames a second advances the
// timestamp by 90000 / n.

// One packet kept back in case the far end asks for it again. Only the media
// half is stored: the header and the extensions are rebuilt on the way out,
// because a retransmission carries its own sequence number and its own place
// in the congestion control counter.
struct rtp_video_cached
{
    unsigned short sequence;
    unsigned int timestamp;
    bool marker;
    bool used;
    int len;
    unsigned char data[1300];
};

struct rtp_video
{
    unsigned int ssrc;
    unsigned char payload_type;
    unsigned short sequence;
    unsigned int timestamp;

    // Retransmission, RFC 4588. op 12 promises the server a retransmission
    // source exists, so it may ask for a lost packet rather than give up on
    // the frame. A sender that never answers leaves permanent holes.
    unsigned int rtx_ssrc;
    unsigned char rtx_payload_type;
    unsigned short rtx_sequence;

    // A couple of seconds of history at a typical rate, which is far longer
    // than a request can usefully arrive.
    rtp_video_cached cache[128];
    unsigned int cache_at;

    // Counts every packet leaving this transport, not every frame. Discord's
    // congestion control reads it out of the header extension.
    unsigned short transport_sequence;

    // 1 for a screen share, 0 for a camera. It reaches the far end as the
    // video content type and decides how the picture is treated.
    int content_type;

    // Which simulcast layer this transport is. It goes out in the stream id
    // extension and is what ties each packet to the entry op 12 declared, so
    // two layers sharing a rid would arrive as one confused stream.
    char rid[8];
    int rid_len;

    // Scratch for the packet being built, big enough for one MTU.
    unsigned char body[1500];
};

// Called once per packet. Returning false stops the frame.
typedef bool (*rtp_video_sink)(void* user,
                               const unsigned char* aad, int aad_len,
                               const unsigned char* body, int body_len);

namespace rtpvid
{
    void init(rtp_video* v, unsigned int ssrc, unsigned char payload_type,
              const char* rid = "100");

    // Splits one Annex-B frame and hands every packet to sink. mtu is the space
    // available for the encrypted body, so the caller must have already taken
    // off the IP, UDP and authentication overhead. Returns the number of
    // packets sent, or -1 if the frame was malformed or the sink refused.
    int send_frame(rtp_video* v, const unsigned char* annexb, int len, int mtu,
                   void* user, rtp_video_sink sink);

    // The timestamp field is set by the caller before each frame rather than
    // stepped here: only the caller knows when the picture was actually taken,
    // and a fixed step per frame drifts away from real time as soon as the
    // capture cannot keep up with the rate it was asked for.

    // Sends a cached packet again under the retransmission source. The payload
    // opens with the sequence number it originally went out as, which is how
    // the far end puts it back in place. False when it has aged out of the
    // history, which is not an error: the frame it belonged to is long gone.
    bool resend(rtp_video* v, unsigned short sequence, int mtu,
                void* user, rtp_video_sink sink);
}

// ---------------------------------------------------------------------------
// Receiving
// ---------------------------------------------------------------------------
//
// The other half of the same protocol: RTP payloads back into whole Annex-B
// access units. A frame is all the packets carrying one timestamp, and the
// marker bit on the last of them says the picture is complete.
//
// A gap in the sequence numbers throws the frame away rather than handing over
// something with a hole in it. That matters more here than in an ordinary
// player: the frame is protected end to end and the trailer holds byte offsets
// into it, so a missing fragment does not blur the picture, it stops the whole
// thing decrypting.
struct rtp_h264_rx
{
    unsigned int timestamp;
    unsigned short last_sequence;
    bool have_timestamp;
    bool have_last;
    bool discarding;

    unsigned char* frame;
    unsigned int cap;
    unsigned int len;

    // Frames given up on, for the statistics line.
    unsigned int dropped;
};

namespace rtpvid
{
    void rx_init(rtp_h264_rx* r);
    void rx_free(rtp_h264_rx* r);
    void rx_reset(rtp_h264_rx* r);

    // payload is the media half of one packet: whatever came out of the cipher
    // with the extension block already taken off the front. Returns true and
    // points out at the finished access unit when the marker closed a frame;
    // those bytes belong to r and stay valid until the next call.
    // Tells the reassembler that a sequence number went by carrying no media,
    // which is what a padding packet is. Without this the next real packet
    // looks like it follows a hole and the whole frame is thrown away.
    void rx_skip(rtp_h264_rx* r, unsigned short sequence);

    bool rx_push(rtp_h264_rx* r, const unsigned char* payload, int len,
                 bool marker, unsigned short sequence, unsigned int timestamp,
                 const unsigned char** out, unsigned int* out_len);
}
