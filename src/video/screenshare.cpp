#include "pch.h"
#include <objbase.h>

#include "screenshare.h"
#include "capture.h"
#include "encoder.h"
#include "censor.h"
#include "audio/loopback.h"
#include "rtp_video.h"

#include "discord/store.h"
#include "discord/gateway.h"
#include "discord/voice.h"
#include "core/log.h"
#include "core/crypto.h"
#include "net/websocket.h"
#include "net/json.h"
#include "dave/mls_types.h"
#include "dave/mls_message.h"
#include "dave/mls_group.h"
#include "dave/dave_frames.h"
#include "dave/tls_codec.h"
#include "audio/audio.h"
#include "system/io/ufile.h"
#include "opus.h"

// Go Live, which is a second voice connection carrying video only. The protocol
// notes live in rtp_video.h; this is the machinery.
//
// Nothing here is shared with voice.cpp on purpose. The two connections have
// separate sockets, keys and sequence numbers, and a stream that fails must not
// be able to disturb the microphone.

namespace
{
    enum
    {
        VOP_IDENTIFY = 0,
        VOP_SELECT_PROTOCOL = 1,
        VOP_READY = 2,
        VOP_HEARTBEAT = 3,
        VOP_SESSION_DESCRIPTION = 4,
        VOP_SPEAKING = 5,
        VOP_HEARTBEAT_ACK = 6,
        VOP_HELLO = 8,
        VOP_VIDEO = 12,
        VOP_MEDIA_SINK_WANTS = 15,
        VOP_BACKEND_VERSION = 16,

        // DAVE. Ops 21-24 arrive as JSON, 25-31 as binary frames shaped
        // [uint16 seq][uint8 opcode][payload].
        VOP_DAVE_PREPARE_TRANSITION = 21,
        VOP_DAVE_EXECUTE_TRANSITION = 22,
        VOP_DAVE_READY_FOR_TRANSITION = 23,
        VOP_DAVE_PREPARE_EPOCH = 24,
        VOP_MLS_EXTERNAL_SENDER = 25,
        VOP_MLS_KEY_PACKAGE = 26,
        VOP_MLS_PROPOSALS = 27,
        VOP_MLS_COMMIT_WELCOME = 28,
        VOP_MLS_ANNOUNCE_COMMIT_TRANSITION = 29,
        VOP_MLS_WELCOME = 30,
        VOP_MLS_INVALID_COMMIT_WELCOME = 31,
    };

    enum
    {
        GWOP_STREAM_CREATE = 18,
        GWOP_STREAM_DELETE = 19,
        GWOP_STREAM_PING = 21,
        GWOP_STREAM_SET_PAUSED = 22,
    };

    enum encryption_mode
    {
        MODE_NONE = 0,
        MODE_AES256_GCM,
        MODE_XCHACHA20,
    };

    // Confirmed against a working client's outgoing packets: it sends H.264 as
    // payload type 105 with retransmissions on 106. The 103 and 104 seen in a
    // capture belong to the other direction, which is the server talking to a
    // viewer, and are not what a sender uses.
    const int PAYLOAD_H264 = 105;
    const int PAYLOAD_H264_RTX = 106;

    // 1200 is what every WebRTC stack settles on for a path that has to cross
    // the open internet. The tag and nonce come off the top.
    const int PATH_MTU = 1200;
    const int CRYPTO_TAIL = 16 + 4;
    // 12 bytes of RTP header and 4 of extension header travel in the clear; the
    // 44 byte extension payload is encrypted with the media but still occupies
    // the path. Counting it here as well leaves the packet a little under the
    // budget rather than a little over.
    const int RTP_CLEAR_BYTES = 12 + 4 + 44;

    websocket g_ws;
    SOCKET g_udp = INVALID_SOCKET;

    HANDLE g_ws_thread = 0;
    HANDLE g_pump_thread = 0;
    HANDLE g_beat_thread = 0;

    volatile long g_running = 0;
    volatile long g_state = SHARE_IDLE;
    volatile long g_heartbeat_ms = 0;
    volatile long g_media_ready = 0;
    volatile long g_want_keyframe = 0;

    char g_status[192];
    char g_endpoint[256];
    char g_token[256];
    char g_stream_key[192];
    char g_rtc_server_id[64];
    char g_connection_id[48];

    snowflake g_guild_id = 0;
    snowflake g_channel_id = 0;

    // Whose stream this is, captured when it starts. The end to end keys hang
    // off it, so it must not follow the store across an account switch.
    snowflake g_self_id = 0;

    unsigned int g_audio_ssrc = 0;
    unsigned int g_video_ssrc = 0;
    unsigned int g_rtx_ssrc = 0;
    unsigned short g_udp_port = 0;
    char g_udp_host[128];
    unsigned char g_secret_key[32];
    encryption_mode g_mode = MODE_NONE;
    unsigned int g_nonce_counter = 0;
    bool g_udp_blocking = false;

    int g_monitor = 0;
    int g_max_w = 1280, g_max_h = 720, g_fps = 30, g_bitrate = 2500;

    volatile long g_frames = 0;
    volatile long g_packets = 0;
    volatile long g_bytes = 0;

    // A writable copy of the captured frame, kept between frames so covering
    // windows does not allocate several megabytes thirty times a second.
    unsigned char* g_censor_frame = 0;
    unsigned int g_censor_cap = 0;

    // Simulcast. A real client publishes the same picture at several sizes and
    // lets the server hand each viewer whichever one suits them; a sender with
    // a single layer leaves a viewer that wanted a smaller one with nothing to
    // subscribe to. Each layer is its own encoder, its own transport and its
    // own pair of sources.
    struct share_layer
    {
        venc_stream enc;
        rtp_video rtp;
        int w, h;
        int bitrate;
        unsigned int ssrc, rtx_ssrc;
        const char* rid;
        int quality;
        // Scratch for the shrunk picture, only for a layer below full size.
        unsigned char* scaled;
        unsigned long long media_start_us;
    };

    share_layer g_layers[2];
    int g_layer_count = 0;

    // Layer zero is the full sized one and is what everything that still speaks
    // of "the" transport means: the sender report clock and the retransmission
    // cache both belong to it.
    rtp_video& g_rtp = g_layers[0].rtp;

    // ---- DAVE ------------------------------------------------------------
    //
    // The stream negotiates end to end encryption of its own, separate from the
    // microphone's: its own signature key, its own key package, its own MLS
    // group. The channel this client joins requires it, so declaring anything
    // less than version one is closed with 4017 before the handshake starts,
    // and frames sent outside the group are simply undecodable at the far end.
    unsigned char g_sig_private[96];
    unsigned char g_sig_public[65];
    bool g_sig_ready = false;

    mls::key_package g_key_package;
    mls::key_package_private g_key_package_private;
    bool g_key_package_ready = false;

    mls::group_state g_group;
    bool g_group_ready = false;

    // Proposals kept from op 27. Ours are answered with a commit of our own,
    // but when somebody else's commit wins the race it arrives as op 29 naming
    // these by reference, and applying it is the only way to stay in step.
    mls::cached_proposal g_known[mls::MAX_MEMBERS];
    unsigned int g_known_count = 0;
    volatile long g_e2ee_ready = 0;
    unsigned int g_dave_nonce = 0;
    volatile long g_protect_failures = 0;
    bool g_passthrough_logged = false;

    // A diagnostic switch, read from the environment at start. The reference
    // implementation hands video over unencrypted whenever its session is not
    // ready to encrypt, so forcing that here says plainly whether our own frame
    // protection is what the far side is choking on.
    bool g_force_passthrough = false;

    HANDLE g_frame_dump = 0;
    unsigned int g_frames_dumped = 0;
    volatile long g_udp_in = 0;
    volatile long g_rtcp_receiver = 0;   // our packets are arriving
    volatile long g_rtcp_pli = 0;        // a viewer cannot decode and wants an IDR
    volatile long g_rtcp_nack = 0;       // packets went missing on the way
    volatile long g_rtx_sent = 0;        // answered from the send cache
    volatile long g_rtx_missed = 0;      // asked for after they aged out

    // One protected access unit. A keyframe at this resolution runs to tens of
    // kilobytes, and the trailer plus widened start codes add a little more.
    unsigned char g_protected[512 * 1024];

    void set_status(screenshare_state s, const char* text)
    {
        InterlockedExchange(&g_state, (long)s);
        ccfset(g_status, 0, sizeof(g_status));
        if (text) ccstrncpy(g_status, text, sizeof(g_status) - 1);
        log_line("share: %s", text ? text : "");
    }

    // The log's own formatter has no precision for %s, so a bounded copy is
    // made instead. Every frame of this exchange is worth recording: it is the
    // only way to see what the server objected to.
    void log_payload(const char* prefix, const void* data, unsigned int size)
    {
        char text[600];
        unsigned int n = size < sizeof(text) - 1 ? size : (unsigned int)sizeof(text) - 1;
        ccpy(text, data, n);
        text[n] = 0;
        log_line("share: %s %s%s", prefix, text, n < size ? " ..." : "");
    }

    // ---- gateway ---------------------------------------------------------

    void build_stream_key()
    {
        // The client builds this itself; the server does not hand one out
        // before the stream exists.
        if (g_guild_id)
            cnprint(g_stream_key, sizeof(g_stream_key), "guild:%llu:%llu:%llu",
                    g_guild_id, g_channel_id, g_self_id);
        else
            cnprint(g_stream_key, sizeof(g_stream_key), "call:%llu:%llu",
                    g_channel_id, g_self_id);
    }

    void send_gateway_stream_create()
    {
        jwriter w;
        w.init();
        w.begin_obj();
        w.kv_i64("op", GWOP_STREAM_CREATE);
        w.key("d");
        w.begin_obj();
        w.kv_str("type", g_guild_id ? "guild" : "call");
        if (g_guild_id) w.kv_snowflake("guild_id", g_guild_id);
        w.kv_snowflake("channel_id", g_channel_id);
        // The region discord itself ranked first for this connection, named
        // back the way a working client does rather than left open.
        const char* region = gateway::preferred_region();
        if (region && region[0]) w.kv_str("preferred_region", region);
        else w.kv_null("preferred_region");
        w.end_obj();
        w.end_obj();

        log_payload("-> gateway op 18", w.buf.data, w.buf.size);
        gateway::send_raw(w.buf.data, w.buf.size);
        w.free_writer();
    }

    void send_gateway_set_paused(bool paused)
    {
        jwriter w;
        w.init();
        w.begin_obj();
        w.kv_i64("op", GWOP_STREAM_SET_PAUSED);
        w.key("d");
        w.begin_obj();
        w.kv_str("stream_key", g_stream_key);
        w.kv_bool("paused", paused);
        w.end_obj();
        w.end_obj();

        gateway::send_raw(w.buf.data, w.buf.size);
        w.free_writer();
    }

    void send_gateway_stream_delete()
    {
        if (!g_stream_key[0]) return;

        jwriter w;
        w.init();
        w.begin_obj();
        w.kv_i64("op", GWOP_STREAM_DELETE);
        w.key("d");
        w.begin_obj();
        w.kv_str("stream_key", g_stream_key);
        w.end_obj();
        w.end_obj();

        log_line("share: -> gateway op 19 STREAM_DELETE");
        gateway::send_raw(w.buf.data, w.buf.size);
        w.free_writer();
    }

    // ---- stream voice websocket -----------------------------------------

    bool send_json(jwriter* w)
    {
        log_payload("->", w->buf.data, w->buf.size);
        return g_ws.send_text(w->buf.data, w->buf.size);
    }

    // A random identifier for this attempt at connecting. A working client
    // sends one with select_protocol, so it is not decoration.
    void make_connection_id(char* out, int cap)
    {
        unsigned char raw[16];
        crypto::random_bytes(raw, sizeof(raw));
        raw[6] = (unsigned char)((raw[6] & 0x0F) | 0x40);   // version 4
        raw[8] = (unsigned char)((raw[8] & 0x3F) | 0x80);   // variant

        const char* digits = "0123456789abcdef";
        int at = 0;
        for (int i = 0; i < 16 && at < cap - 2; i++)
        {
            if (i == 4 || i == 6 || i == 8 || i == 10) out[at++] = '-';
            out[at++] = digits[raw[i] >> 4];
            out[at++] = digits[raw[i] & 0x0F];
        }
        out[at] = 0;
    }

    void send_identify()
    {
        jwriter w;
        w.init();
        w.begin_obj();
        w.kv_i64("op", VOP_IDENTIFY);
        w.key("d");
        w.begin_obj();
        w.kv_str("server_id", g_rtc_server_id);
        // A working client names the channel here as well, and it is not the
        // voice channel: it is the rtc server id less one, the same number the
        // DAVE group is keyed on.
        {
            char channel[32];
            unsigned long long id = ccstrtoull(g_rtc_server_id, 0, 10);
            cnprint(channel, sizeof(channel), "%llu", id ? id - 1 : 0);
            w.kv_str("channel_id", channel);
        }
        w.kv_snowflake("user_id", g_self_id);
        w.kv_str("session_id", voice::session_id());
        w.kv_str("token", g_token);
        // Without this the server never allocates the video ssrcs.
        w.kv_bool("video", true);
        // A stream connection is held to the same rule as the voice one: leave
        // this out and the server closes with 4017 before it says anything,
        // and declaring zero is refused just the same.
        w.kv_i64("max_dave_protocol_version", 1);
        w.key("streams");
        w.begin_arr();
        w.begin_obj();
        // "screen", not "video": this is what a share declares itself as here,
        // and it is declared active from the outset.
        w.kv_str("type", "screen");
        w.kv_str("rid", "100");
        w.kv_i64("quality", 100);
        w.kv_bool("active", true);
        w.end_obj();
        w.end_arr();
        w.end_obj();
        w.end_obj();

        send_json(&w);
        w.free_writer();
    }

    void add_codec(jwriter* w, const char* name, const char* type, int payload, int rtx, int priority)
    {
        w->begin_obj();
        w->kv_str("name", name);
        w->kv_str("type", type);
        w->kv_i64("priority", priority);
        w->kv_i64("payload_type", payload);
        // Both flags are true for everything still on the list, because only
        // the codecs this client really handles are put on it at all.
        w->kv_bool("encode", true);
        w->kv_bool("decode", true);
        if (rtx >= 0) w->kv_i64("rtx_payload_type", rtx);
        w->end_obj();
    }

    void send_select_protocol(const char* address, unsigned short port, const char* mode)
    {
        jwriter w;
        w.init();
        w.begin_obj();
        w.kv_i64("op", VOP_SELECT_PROTOCOL);
        w.key("d");
        w.begin_obj();
        w.kv_str("protocol", "udp");

        // The whole list is offered even though only H264 is produced: the
        // server matches on it to decide what the viewers may ask for.
        // The current assignment, in the order and with the priorities the
        // server expects. The numbers are not ours to choose: the server maps
        // an incoming payload type back to a codec using this table.
        w.key("codecs");
        w.begin_arr();
        // The order and priorities a working client sends: what it can actually
        // encode comes first. Ours only produces H.264, so that leads.
        // Only what this client can really do.
        //
        // Listing the other four as encodable was a lie, and an expensive one:
        // a viewer negotiating with the server can settle on a codec we will
        // never produce, then sit waiting for a picture that cannot come. That
        // is what a browser based client showed as a handshake that gave up
        // after half a minute, while clients that happen to prefer H.264
        // anyway never noticed.
        //
        // The payload numbers are the client's to choose - the server honours
        // whatever this list declares - so they stay as they were.
        add_codec(&w, "opus", "audio", 120, -1, 1000);
        add_codec(&w, "H264", "video", PAYLOAD_H264, PAYLOAD_H264_RTX, 1000);
        w.end_arr();

        // A connection identifier and the experiment the server offered in
        // READY, both echoed back the way a working client does.
        w.kv_str("rtc_connection_id", g_connection_id);
        // Deliberately empty.
        //
        // This used to declare "fixed_keyframe_interval", copied from the
        // reference implementation, which hardcodes it and ignores the list the
        // gateway actually assigned. A real client sends back what it was
        // given. The flag tells the server this sender produces keyframes on a
        // schedule, which invites it to stop forwarding picture loss requests -
        // and a viewer that joins between two keyframes then waits for one that
        // is not coming. Both implementations that fail on a client with a
        // shorter patience than most declare it; no client that works does.
        w.key("experiments");
        w.begin_arr();
        w.end_arr();

        w.key("data");
        w.begin_obj();
        w.kv_str("address", address);
        w.kv_i64("port", port);
        w.kv_str("mode", mode);
        w.end_obj();
        w.end_obj();
        w.end_obj();

        send_json(&w);
        w.free_writer();
    }

    // A stream has to announce itself before its video is routed anywhere. The
    // flag is 2 rather than 1: on a stream connection that is what marks the
    // source as a live broadcast, and it is keyed to the audio ssrc even though
    // only video is being sent. Without it the server accepts every packet and
    // hands none of them to a viewer.
    void send_speaking(bool on)
    {
        jwriter w;
        w.init();
        w.begin_obj();
        w.kv_i64("op", VOP_SPEAKING);
        w.key("d");
        w.begin_obj();
        w.kv_i64("speaking", on ? 2 : 0);
        w.kv_i64("delay", 0);
        w.kv_i64("ssrc", (long long)g_audio_ssrc);
        w.end_obj();
        w.end_obj();

        send_json(&w);
        w.free_writer();
    }

    void send_video(bool on, int width, int height)
    {
        jwriter w;
        w.init();
        w.begin_obj();
        w.kv_i64("op", VOP_VIDEO);
        w.key("d");
        w.begin_obj();
        w.kv_i64("audio_ssrc", (long long)g_audio_ssrc);
        w.kv_i64("video_ssrc", on ? (long long)g_video_ssrc : 0);
        w.kv_i64("rtx_ssrc", on ? (long long)g_rtx_ssrc : 0);
        w.key("streams");
        w.begin_arr();
        for (int i = 0; on && i < g_layer_count; i++)
        {
            const share_layer* L = &g_layers[i];

            w.begin_obj();
            w.kv_str("type", "video");
            w.kv_str("rid", L->rid);
            w.kv_i64("ssrc", (long long)L->ssrc);
            w.kv_i64("rtx_ssrc", (long long)L->rtx_ssrc);
            w.kv_bool("active", true);
            w.kv_i64("quality", L->quality);
            w.kv_i64("max_bitrate", (long long)L->bitrate * 1000);
            w.kv_i64("max_framerate", g_fps);
            w.key("max_resolution");
            w.begin_obj();
            w.kv_str("type", "fixed");
            w.kv_i64("width", L->w);
            w.kv_i64("height", L->h);
            w.end_obj();
            w.end_obj();
        }
        w.end_arr();
        w.end_obj();
        w.end_obj();

        send_json(&w);
        w.free_writer();
    }

    // ---- DAVE handshake --------------------------------------------------

    // Outgoing binary frames carry only the opcode; the sequence number belongs
    // to the server side of the protocol.
    bool send_binary(unsigned char opcode, const void* payload, unsigned int len)
    {
        ubuffer frame;
        frame.init(len + 8);
        frame.append_char((char)opcode);
        if (len) frame.append(payload, len);

        bool ok = g_ws.send_binary(frame.data, frame.size);
        frame.free_buffer();
        return ok;
    }

    void send_key_package()
    {
        if (!g_sig_ready)
        {
            if (!crypto::p256_generate(g_sig_public, g_sig_private))
            {
                log_line("share/dave: could not create the MLS signature key");
                return;
            }
            g_sig_ready = true;
        }

        // Key packages are single use, so a fresh one is built every time.
        if (!mls::create_key_package(g_self_id, g_sig_private,
                                     &g_key_package, &g_key_package_private))
        {
            log_line("share/dave: key package creation failed");
            return;
        }
        g_key_package_ready = true;

        tls_writer w;
        w.init(1024);
        g_key_package.write(&w);

        log_line("share/dave: sending key package (%u bytes, self verify %s)",
                 w.size(), g_key_package.verify() ? "ok" : "FAILED");

        send_binary(VOP_MLS_KEY_PACKAGE, w.data(), w.size());
        w.free_writer();

        // A stream's group is not keyed on the voice channel the way the
        // microphone's is. It uses the rtc server id the gateway handed back,
        // minus one. Getting this wrong builds a group under an identifier
        // nobody else is using, and every viewer ends up outside it.
        unsigned long long dave_id = ccstrtoull(g_rtc_server_id, 0, 10);
        if (dave_id) dave_id -= 1;

        unsigned char group_id[8];
        for (int i = 0; i < 8; i++)
            group_id[i] = (unsigned char)(dave_id >> (56 - i * 8));

        log_line("share/dave: group id %llu (rtc server %s)", dave_id, g_rtc_server_id);

        g_group_ready = mls::create_group(&g_group, group_id, 8, &g_key_package.leaf,
                                          g_sig_private, g_key_package_private.encryption_private);

        log_line("share/dave: local group %s", g_group_ready ? "created" : "FAILED");
    }

    void send_transition_response(unsigned int transition_id, bool ready)
    {
        jwriter w;
        w.init();
        w.begin_obj();
        w.kv_i64("op", ready ? VOP_DAVE_READY_FOR_TRANSITION : VOP_MLS_INVALID_COMMIT_WELCOME);
        w.key("d");
        w.begin_obj();
        w.kv_i64("transition_id", (long long)transition_id);
        w.end_obj();
        w.end_obj();

        send_json(&w);
        w.free_writer();
    }

    void handle_proposals(const unsigned char* payload, unsigned int payload_len)
    {
        bool is_revoke = false;
        mls::proposal_message messages[16];
        unsigned int count = 0;

        if (!mls::parse_proposals_payload(payload, payload_len, &is_revoke,
                                          messages, 16, &count))
        {
            log_line("share/dave: proposals could not be parsed");
            return;
        }

        log_line("share/dave: %u proposal(s), revoke=%d", count, is_revoke ? 1 : 0);

        // Kept whether or not our own commit is the one that lands: if another
        // member's does, op 29 names these and we have to be able to find them.
        if (is_revoke) g_known_count = 0;
        for (unsigned int i = 0; i < count && !is_revoke && g_known_count < mls::MAX_MEMBERS; i++)
        {
            messages[i].compute_ref(g_known[g_known_count].ref);
            g_known[g_known_count].prop = messages[i].prop;
            g_known_count++;
        }

        if (is_revoke || !g_group_ready) return;

        ubuffer commit, welcome;
        commit.init();
        welcome.init();

        if (mls::build_commit(&g_group, messages, count, &commit, &welcome))
        {
            ubuffer out;
            out.init(commit.size + welcome.size + 16);
            out.append(commit.data, commit.size);
            out.append(welcome.data, welcome.size);

            log_line("share/dave: sending commit+welcome (%u bytes)", out.size);
            send_binary(VOP_MLS_COMMIT_WELCOME, out.data, out.size);
            out.free_buffer();
        }
        else
        {
            log_line("share/dave: commit generation failed");
        }

        commit.free_buffer();
        welcome.free_buffer();
    }

    void handle_binary_payload(const unsigned char* data, unsigned int len)
    {
        if (len < 3) return;

        unsigned int seq = ((unsigned int)data[0] << 8) | data[1];
        unsigned char opcode = data[2];
        const unsigned char* payload = data + 3;
        unsigned int payload_len = len - 3;

        log_line("share/dave: binary op %u, seq %u, %u bytes", opcode, seq, payload_len);

        switch (opcode)
        {
        case VOP_MLS_EXTERNAL_SENDER:
            // Discord's own signing identity for the group. Answering with our
            // key package is what gets this connection added.
            send_key_package();
            break;

        case VOP_MLS_PROPOSALS:
            handle_proposals(payload, payload_len);
            break;

        case VOP_MLS_ANNOUNCE_COMMIT_TRANSITION:
        {
            // Somebody else's commit won the race. Until this was applied, this
            // client kept protecting frames with the exporter of an epoch the
            // group had already left, and no viewer could open any of them.
            if (payload_len < 3 || !g_group_ready) break;

            unsigned int transition_id = ((unsigned int)payload[0] << 8) | payload[1];

            const char* why = "";
            bool applied = mls::process_commit(&g_group, payload + 2, payload_len - 2,
                                               g_known, g_known_count, &why);

            log_line("share/dave: коммит %s (transition %u)%s%s",
                     applied ? "применён" : "НЕ применён", transition_id,
                     applied ? "" : ": ", applied ? "" : why);

            if (applied)
            {
                g_known_count = 0;
                dave::reset_ratchets();
                g_dave_nonce = 0;
                // A new epoch is a new key, so a viewer needs a frame it can
                // start from.
                InterlockedExchange(&g_want_keyframe, 1);
            }

            send_transition_response(transition_id, applied);
            break;
        }

        case VOP_MLS_WELCOME:
        {
            if (payload_len < 3 || !g_key_package_ready)
            {
                log_line("share/dave: welcome arrived before we had a key package");
                break;
            }

            // [uint16 transition_id][Welcome]
            unsigned int transition_id = ((unsigned int)payload[0] << 8) | payload[1];

            g_group_ready = mls::process_welcome(&g_group, payload + 2, payload_len - 2,
                                                 &g_key_package, &g_key_package_private,
                                                 g_sig_private);

            dave::reset_ratchets();
            g_dave_nonce = 0;
            g_known_count = 0;
            InterlockedExchange(&g_e2ee_ready, g_group_ready ? 1 : 0);

            log_line("share/dave: welcome %s (transition %u)",
                     g_group_ready ? "accepted" : "REJECTED", transition_id);

            send_transition_response(transition_id, g_group_ready);
            break;
        }

        default:
            break;
        }
    }

    // ---- udp -------------------------------------------------------------

    bool udp_connect()
    {
        g_udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (g_udp == INVALID_SOCKET) return false;

        addrinfo hints;
        ccfset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;

        char port_text[16];
        cnprint(port_text, sizeof(port_text), "%u", (unsigned int)g_udp_port);

        addrinfo* result = 0;
        if (getaddrinfo(g_udp_host, port_text, &hints, &result) != 0 || !result)
        {
            closesocket(g_udp);
            g_udp = INVALID_SOCKET;
            return false;
        }

        int ok = connect(g_udp, result->ai_addr, (int)result->ai_addrlen);
        freeaddrinfo(result);
        if (ok == SOCKET_ERROR)
        {
            closesocket(g_udp);
            g_udp = INVALID_SOCKET;
            return false;
        }

        DWORD timeout = 250;
        setsockopt(g_udp, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
        g_udp_blocking = true;

        // Video bursts far harder than audio: a keyframe is tens of packets
        // back to back, and the default buffer drops them on the floor.
        int sndbuf = 1 << 20;
        setsockopt(g_udp, SOL_SOCKET, SO_SNDBUF, (const char*)&sndbuf, sizeof(sndbuf));
        return true;
    }

    bool udp_discover(char* out_ip, int ip_cap, unsigned short* out_port)
    {
        unsigned char packet[74];
        ccfset(packet, 0, sizeof(packet));
        packet[0] = 0x00; packet[1] = 0x01;
        packet[2] = 0x00; packet[3] = 0x46;
        packet[4] = (unsigned char)(g_audio_ssrc >> 24);
        packet[5] = (unsigned char)(g_audio_ssrc >> 16);
        packet[6] = (unsigned char)(g_audio_ssrc >> 8);
        packet[7] = (unsigned char)(g_audio_ssrc);

        for (int attempt = 0; attempt < 10; attempt++)
        {
            if (send(g_udp, (const char*)packet, sizeof(packet), 0) == SOCKET_ERROR) return false;

            unsigned char reply[128];
            int got = recv(g_udp, (char*)reply, sizeof(reply), 0);
            if (got < 74 || reply[0] != 0x00 || reply[1] != 0x02) continue;

            int i = 0;
            while (i < 64 && reply[8 + i] && i < ip_cap - 1) { out_ip[i] = (char)reply[8 + i]; i++; }
            out_ip[i] = 0;
            *out_port = (unsigned short)((reply[72] << 8) | reply[73]);
            return true;
        }
        return false;
    }

    // Encrypts one packetiser output and puts it on the wire. The additional
    // data is the RTP header plus the extension header, which travel in the
    // clear; the body is everything that gets encrypted.
    bool send_rtp(void*, const unsigned char* aad, int aad_len,
                  const unsigned char* body, int body_len)
    {
        if (g_udp == INVALID_SOCKET || g_mode == MODE_NONE) return false;

        unsigned char packet[1600];
        if (aad_len + body_len + CRYPTO_TAIL > (int)sizeof(packet)) return false;

        ccpy(packet, aad, (size_t)aad_len);

        unsigned int counter = InterlockedIncrement((volatile long*)&g_nonce_counter);
        unsigned char counter_bytes[4] = {
            (unsigned char)(counter >> 24), (unsigned char)(counter >> 16),
            (unsigned char)(counter >> 8), (unsigned char)(counter)
        };

        unsigned char* cipher = packet + aad_len;
        unsigned char tag[16];
        bool ok = false;

        if (g_mode == MODE_XCHACHA20)
        {
            unsigned char nonce[24];
            ccfset(nonce, 0, sizeof(nonce));
            ccpy(nonce, counter_bytes, 4);
            crypto::xchacha20poly1305_encrypt(g_secret_key, nonce, packet, (unsigned int)aad_len,
                                              body, (unsigned int)body_len, cipher, tag);
            ok = true;
        }
        else
        {
            unsigned char nonce[12];
            ccfset(nonce, 0, sizeof(nonce));
            ccpy(nonce, counter_bytes, 4);
            ok = crypto::aes256gcm_encrypt(g_secret_key, nonce, packet, (unsigned int)aad_len,
                                           body, (unsigned int)body_len, cipher, tag);
        }
        if (!ok) return false;

        int len = aad_len + body_len;
        ccpy(packet + len, tag, 16);
        len += 16;
        ccpy(packet + len, counter_bytes, 4);
        len += 4;

        if (send(g_udp, (const char*)packet, len, 0) == SOCKET_ERROR) return false;

        InterlockedIncrement(&g_packets);
        InterlockedAdd(&g_bytes, (long)len);
        return true;
    }

    // A receiver treats a stream with no sender reports as stalled: they are
    // what carries the wall clock the media timestamps hang off, and without
    // them the viewer eventually gives up on the stream. One a second is what
    // every WebRTC sender does.
    //
    // The eight byte RTCP header travels in the clear and is the additional
    // data; the twenty byte report body is encrypted like any media payload.
    void send_sender_report()
    {
        if (g_udp == INVALID_SOCKET || g_mode == MODE_NONE) return;

        unsigned char header[8];
        header[0] = 0x80;                      // version 2, no padding, no report blocks
        header[1] = 200;                       // sender report
        header[2] = 0x00;
        header[3] = 0x06;                      // length in words, minus one
        header[4] = (unsigned char)(g_video_ssrc >> 24);
        header[5] = (unsigned char)(g_video_ssrc >> 16);
        header[6] = (unsigned char)(g_video_ssrc >> 8);
        header[7] = (unsigned char)(g_video_ssrc);

        // NTP time is seconds since 1900 in 32.32 fixed point, and a Windows
        // file time counts hundreds of nanoseconds since 1601. The gap between
        // the two epochs is a fixed 9435484800 seconds.
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        unsigned long long ticks = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;

        unsigned int ntp_seconds = (unsigned int)(ticks / 10000000ULL - 9435484800ULL);
        unsigned int ntp_fraction = (unsigned int)(((ticks % 10000000ULL) << 32) / 10000000ULL);

        unsigned char body[20];
        unsigned int fields[5] = {
            ntp_seconds, ntp_fraction, g_rtp.timestamp,
            (unsigned int)g_packets, (unsigned int)g_bytes
        };
        for (int i = 0; i < 5; i++)
        {
            body[i * 4 + 0] = (unsigned char)(fields[i] >> 24);
            body[i * 4 + 1] = (unsigned char)(fields[i] >> 16);
            body[i * 4 + 2] = (unsigned char)(fields[i] >> 8);
            body[i * 4 + 3] = (unsigned char)(fields[i]);
        }

        send_rtp(0, header, sizeof(header), body, sizeof(body));
    }

    // A generic NACK asks for packets back by number: a base, then a bitmap of
    // the sixteen that follow it. Each entry is answered from the send cache.
    void handle_nack(const unsigned char* packet, int len)
    {
        // Header, sender ssrc, media ssrc, then the requests.
        if (len < 12) return;

        unsigned int media = ((unsigned int)packet[8] << 24) | ((unsigned int)packet[9] << 16) |
                             ((unsigned int)packet[10] << 8) | packet[11];
        if (media != g_video_ssrc) return;

        int answered = 0, missing = 0;
        for (int at = 12; at + 4 <= len; at += 4)
        {
            unsigned short base = (unsigned short)((packet[at] << 8) | packet[at + 1]);
            unsigned short bitmap = (unsigned short)((packet[at + 2] << 8) | packet[at + 3]);

            for (int i = 0; i <= 16; i++)
            {
                if (i > 0 && !((bitmap >> (i - 1)) & 1)) continue;

                unsigned short want = (unsigned short)(base + i);
                if (rtpvid::resend(&g_rtp, want, PATH_MTU - CRYPTO_TAIL, 0, send_rtp)) answered++;
                else missing++;
            }
        }

        InterlockedAdd(&g_rtx_sent, answered);
        if (missing) InterlockedAdd(&g_rtx_missed, missing);
    }

    // Whatever the server sends back on the media socket. Until now this was
    // never read, so a stream could be rejected at the far end without a trace
    // of it anywhere. RTCP tells us plainly whether anybody is receiving: a
    // receiver report means our packets are arriving and being counted, and a
    // picture loss indication means a viewer is decoding but wants a keyframe.
    void drain_udp()
    {
        if (g_udp == INVALID_SOCKET) return;

        unsigned char packet[2048];
        for (int i = 0; i < 16; i++)
        {
            int got = recv(g_udp, (char*)packet, sizeof(packet), 0);
            if (got <= 0) break;

            InterlockedIncrement(&g_udp_in);
            if (got < 8) continue;

            // RTCP packet types run from 200 to 207 and use the whole byte.
            // Masking the top bit off, the way an RTP marker flag would need,
            // turns 206 into 78 and silently loses every keyframe request.
            unsigned int pt = packet[1];
            if (pt < 200 || pt > 207) continue;

            if (pt == 201) InterlockedIncrement(&g_rtcp_receiver);
            else if (pt == 205)
            {
                InterlockedIncrement(&g_rtcp_nack);
                // Format 1 is a generic NACK; anything else on 205 is
                // congestion control feedback and needs no reply.
                if ((packet[0] & 0x1F) == 1) handle_nack(packet, got);
            }
            else if (pt == 206)
            {
                InterlockedIncrement(&g_rtcp_pli);
                // A viewer only asks for this when it cannot decode what it
                // already has, so it gets answered at once.
                InterlockedExchange(&g_want_keyframe, 1);
            }
        }
    }

    // How many leaves the group actually holds. A stream that nobody has joined
    // is a group of one.
    unsigned int group_members()
    {
        if (!g_group_ready || !g_group.established) return 0;

        unsigned int n = 0;
        for (unsigned int i = 0; i < g_group.leaf_count && i < mls::MAX_MEMBERS; i++)
            if (g_group.leaf_used[i]) n++;
        return n;
    }

    // A Go Live stream carries two tracks, not one. A working client keeps a
    // continuous opus stream running on the connection's audio source even when
    // there is nothing to hear, and a viewer will not start on video alone.
    // These frames are silence, but they have to be real opus and they have to
    // keep coming.
    // Twenty milliseconds cannot be timed with the tick count, whose steps are
    // most of that on their own.
    unsigned long long now_us()
    {
        static LARGE_INTEGER freq = { 0 };
        if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);

        LARGE_INTEGER c;
        QueryPerformanceCounter(&c);
        return (unsigned long long)((c.QuadPart * 1000000LL) / freq.QuadPart);
    }

    OpusEncoder* g_silence = 0;

    // Whether this share was asked to carry what the machine is playing.
    bool g_want_audio = false;
    // A capture_method: how the screen is read for this share.
    int g_capture_method = 0;
    unsigned short g_audio_sequence = 0;
    unsigned int g_audio_timestamp = 0;
    unsigned long long g_next_audio_us = 0;

    void send_silence()
    {
        if (!g_silence || g_udp == INVALID_SOCKET || g_mode == MODE_NONE) return;

        short pcm[AUDIO_FRAME_SAMPLES];

        // Whatever the machine is playing, or silence when there is nothing
        // yet. A frame always goes out either way: a gap in the stream is
        // read by the far side as a fault, not as quiet.
        if (!loopback::read_frame(pcm, AUDIO_FRAME_SAMPLES))
            ccfset(pcm, 0, sizeof(pcm));

        unsigned char opus[400];
        int len = opus_encode(g_silence, pcm, AUDIO_FRAME_SAMPLES, opus, (opus_int32)sizeof(opus));
        if (len <= 0) return;

        // Protected the same way the microphone is, when there is a group.
        unsigned char guarded[512];
        const unsigned char* payload = opus;
        unsigned int payload_len = (unsigned int)len;

        if (g_e2ee_ready && group_members() > 1 && !g_force_passthrough)
        {
            unsigned int out_len = 0;
            if (dave::encrypt_frame(&g_group, g_self_id, &g_dave_nonce,
                                    opus, (unsigned int)len, guarded, &out_len))
            {
                payload = guarded;
                payload_len = out_len;
            }
        }

        unsigned char aad[12];
        aad[0] = 0x80;
        aad[1] = 0x78;                       // opus, payload type 120
        aad[2] = (unsigned char)(g_audio_sequence >> 8);
        aad[3] = (unsigned char)(g_audio_sequence);
        aad[4] = (unsigned char)(g_audio_timestamp >> 24);
        aad[5] = (unsigned char)(g_audio_timestamp >> 16);
        aad[6] = (unsigned char)(g_audio_timestamp >> 8);
        aad[7] = (unsigned char)(g_audio_timestamp);
        aad[8] = (unsigned char)(g_audio_ssrc >> 24);
        aad[9] = (unsigned char)(g_audio_ssrc >> 16);
        aad[10] = (unsigned char)(g_audio_ssrc >> 8);
        aad[11] = (unsigned char)(g_audio_ssrc);

        g_audio_sequence++;
        g_audio_timestamp += AUDIO_FRAME_SAMPLES;

        send_rtp(0, aad, sizeof(aad), payload, payload_len);
    }

    // ---- media pump ------------------------------------------------------

    DWORD WINAPI pump_thread(LPVOID)
    {
        CoInitializeEx(0, COINIT_MULTITHREADED);

        capture_target monitors[8];
        int count = capture::list_monitors(monitors, 8);
        if (count <= 0)
        {
            set_status(SHARE_FAILED, "Не нашлось ни одного экрана");
            CoUninitialize();
            return 0;
        }
        int index = (g_monitor >= 0 && g_monitor < count) ? g_monitor : 0;

        // With bars, so the frame is a plain 1280x720 whatever shape the screen
        // is. A working client declares and sends that; ours was announcing an
        // unusual 900x720 because the monitor is five by four.
        if (!capture::start(&monitors[index], g_max_w, g_max_h, g_fps, true))
        {
            set_status(SHARE_FAILED, capture::last_error());
            CoUninitialize();
            return 0;
        }

        int w = capture::width(), h = capture::height();

        // Two layers: the captured size, and a half sized one for a viewer on a
        // small window or a thin link. Both sides are rounded down to even
        // numbers because H.264 chroma covers two by two blocks.
        ccfset(g_layers, 0, sizeof(g_layers));
        g_layer_count = 0;

        g_layers[0].w = w;
        g_layers[0].h = h;
        g_layers[0].bitrate = g_bitrate;
        g_layers[0].rid = "100";
        g_layers[0].quality = 100;
        g_layers[0].ssrc = g_video_ssrc;
        g_layers[0].rtx_ssrc = g_rtx_ssrc;
        g_layer_count = 1;

        int half_w = (w / 2) & ~1;
        int half_h = (h / 2) & ~1;
        if (half_w >= 160 && half_h >= 120)
        {
            g_layers[1].w = half_w;
            g_layers[1].h = half_h;
            g_layers[1].bitrate = g_bitrate / 4 < 300 ? 300 : g_bitrate / 4;
            g_layers[1].rid = "50";
            g_layers[1].quality = 50;
            // The audio source and the full layer's pair already occupy the
            // three numbers after the base, so this one carries on from there.
            g_layers[1].ssrc = g_audio_ssrc + 3;
            g_layers[1].rtx_ssrc = g_audio_ssrc + 4;
            g_layer_count = 2;
        }

        bool layers_ok = true;
        for (int i = 0; i < g_layer_count && layers_ok; i++)
        {
            share_layer* L = &g_layers[i];
            layers_ok = venc::start(&L->enc, L->w, L->h, g_fps, L->bitrate);
            if (!layers_ok)
            {
                set_status(SHARE_FAILED, venc::last_error(&L->enc));
                break;
            }

            if (L->w != w || L->h != h)
            {
                L->scaled = (unsigned char*)memalloc(L->w * L->h * 4);
                if (!L->scaled) layers_ok = false;
            }
        }

        if (!layers_ok)
        {
            for (int i = 0; i < g_layer_count; i++)
            {
                venc::stop(&g_layers[i].enc);
                if (g_layers[i].scaled) memfree(g_layers[i].scaled);
            }
            g_layer_count = 0;
            capture::stop();
            CoUninitialize();
            return 0;
        }

        log_line("share: слоёв %d", g_layer_count);

        {
            int err = 0;
            capture::set_method((capture_method)g_capture_method);

            if (g_want_audio && !loopback::start())
                log_line("share: звук системы не пошёл (%s)", loopback::last_error());

            g_silence = opus_encoder_create(AUDIO_SAMPLE_RATE, 1, OPUS_APPLICATION_AUDIO, &err);
            if (!g_silence) log_line("share: тишина не закодируется (opus %d)", err);
            g_audio_sequence = 0;
            g_audio_timestamp = 0;
            g_next_audio_us = 0;
        }

        for (int i = 0; i < g_layer_count; i++)
        {
            share_layer* L = &g_layers[i];
            rtpvid::init(&L->rtp, L->ssrc, PAYLOAD_H264, L->rid);
            L->rtp.content_type = 1;
            L->rtp.rtx_ssrc = L->rtx_ssrc;
            L->rtp.rtx_payload_type = PAYLOAD_H264_RTX;
        }
        // This is a screen, not a camera: it reaches the far end in the content
        // type extension and changes how the picture is treated.
        g_rtp.content_type = 1;
        // op 12 already told the server a retransmission source exists, so the
        // packetiser is given what it needs to answer for it.
        g_rtp.rtx_ssrc = g_rtx_ssrc;
        g_rtp.rtx_payload_type = PAYLOAD_H264_RTX;

        // The order a working client uses, taken off its websocket rather than
        // guessed: quiet first, then an empty declaration that clears whatever
        // the server had, then the real one, and only then does the source
        // announce itself as live.
        send_speaking(false);
        send_video(false, 0, 0);
        send_video(true, w, h);
        send_speaking(true);
        set_status(SHARE_LIVE, "Демонстрация идёт");
        log_line("share: streaming %dx%d at %d fps, video ssrc %u", w, h, g_fps, g_video_ssrc);

        // A viewer joining later needs an IDR to start from.
        for (int i = 0; i < g_layer_count; i++) venc::request_keyframe(&g_layers[i].enc);

        unsigned long long last_report = 0;
        unsigned long long last_stats = GetTickCount64();

        // The encoder's own interval is counted in frames, so it only means two
        // seconds while the capture keeps up with the rate it was asked for. A
        // slow screen stretches it without limit, and a viewer joining in that
        // gap has nothing to start from. This one is measured in real time.
        unsigned long long last_keyframe_ms = 0;

        // The video clock has to follow the wall clock, not the frame rate we
        // asked for. A GDI capture of a large screen does not keep up with
        // thirty a second, and stepping the timestamp by a thirtieth every time
        // a frame came out made media time run at about half real time: the
        // picture plays back slowly and drifts further from the sender report
        // with every frame.
        unsigned long long media_start_us = 0;

        while (g_running)
        {
            // A viewer that has just subscribed cannot show anything until an
            // IDR arrives, and the next scheduled one may be seconds away.
            {
                unsigned long long ms = GetTickCount64();
                bool asked = InterlockedExchange(&g_want_keyframe, 0) != 0;
                bool overdue = ms - last_keyframe_ms >= 2000;

                if (asked || overdue)
                {
                    last_keyframe_ms = ms;
                    for (int i = 0; i < g_layer_count; i++)
                        venc::request_keyframe(&g_layers[i].enc);
                }
            }

            drain_udp();

            // The audio track runs on its own twenty millisecond clock, quite
            // apart from the frame rate.
            {
                unsigned long long us = now_us();
                if (g_next_audio_us == 0) g_next_audio_us = us;
                while (us >= g_next_audio_us)
                {
                    send_silence();
                    g_next_audio_us += 20000;
                    // A long stall must not turn into a burst of catch-up.
                    if (g_next_audio_us + 200000 < us) g_next_audio_us = us;
                }
            }

            capture_frame f;
            if (capture::grab(&f))
            {
                // Anything the person marked gets covered before a single
                // encoder sees the frame, so nothing censored can reach the
                // wire even on the layer nobody is watching.
                if (censor::active())
                {
                    capture_mapping map;
                    if (capture::mapping(&map))
                    {
                        unsigned int need = (unsigned int)(f.stride * f.height);
                        if (g_censor_cap < need)
                        {
                            unsigned char* fresh = (unsigned char*)memalloc((int)need);
                            if (fresh)
                            {
                                if (g_censor_frame) memfree(g_censor_frame);
                                g_censor_frame = fresh;
                                g_censor_cap = need;
                            }
                        }

                        // The grabber owns its pixels and hands them out as
                        // const; drawing needs a copy. Only taken when there
                        // is actually something to draw.
                        if (g_censor_frame && g_censor_cap >= need)
                        {
                            ccpy(g_censor_frame, f.bgra, need);
                            if (censor::apply(g_censor_frame, f.width, f.height,
                                              f.stride, &map) > 0)
                                f.bgra = g_censor_frame;
                        }
                    }
                }

                for (int i = 0; i < g_layer_count; i++)
                {
                    share_layer* L = &g_layers[i];

                    if (!L->scaled)
                    {
                        venc::submit(&L->enc, f.bgra, f.width, f.height, f.stride, f.time_us);
                        continue;
                    }

                    venc::downscale_bgra(f.bgra, f.width, f.height, f.stride,
                                         L->scaled, L->w, L->h);
                    venc::submit(&L->enc, L->scaled, L->w, L->h, L->w * 4, f.time_us);
                }
            }

            for (int layer = 0; layer < g_layer_count; layer++)
            {
            share_layer* L = &g_layers[layer];

            const unsigned char* data = 0;
            int len = 0;
            bool key = false;
            while (venc::next(&L->enc, &data, &len, &key))
            {
                // Frames are never withheld. The group can take the better part
                // of a minute to form - the proposals that add a viewer arrive
                // only once they open the stream - and a viewer that receives
                // nothing in that window gives up long before it finishes.
                // Until there is somebody to encrypt to, the video travels in
                // the clear, which is what DAVE calls passthrough and what the
                // transport encryption already covers on the wire.
                // Debug aid: writes what the encoder produced, so the same
                // frames can be pushed through a client that is known to work.
                // Whichever end then fails is the end at fault.
                if (g_frame_dump && g_frames_dumped < 300)
                {
                    g_frames_dumped++;
                    // An access unit delimiter in front of each frame, because
                    // that is how a reader tells one from the next; the stream
                    // itself goes out without them.
                    const unsigned char aud[6] = { 0, 0, 0, 1, 9, 0x10 };
                    DWORD wrote = 0;
                    WriteFile(g_frame_dump, aud, sizeof(aud), &wrote, 0);
                    WriteFile(g_frame_dump, data, (DWORD)len, &wrote, 0);
                }

                const unsigned char* payload = data;
                unsigned int payload_len = (unsigned int)len;
                bool alone = !g_e2ee_ready || group_members() <= 1 || g_force_passthrough;

                if (!alone)
                {
                    unsigned int protectedLen = 0;
                    if (!dave::encrypt_frame_h264(&g_group, g_self_id, &g_dave_nonce,
                                                  data, (unsigned int)len,
                                                  g_protected, (unsigned int)sizeof(g_protected),
                                                  &protectedLen))
                    {
                        InterlockedIncrement(&g_protect_failures);
                        continue;
                    }
                    payload = g_protected;
                    payload_len = protectedLen;
                }

                if (alone != g_passthrough_logged)
                {
                    g_passthrough_logged = alone;
                    log_line("share: кадры идут %s (в группе %u)",
                             alone ? "без E2EE, passthrough" : "с E2EE", group_members());
                }

                // 90 kHz, counted from the first frame that went out. Two
                // frames leaving in the same microsecond would share a
                // timestamp and be read as one picture, so the clock is never
                // allowed to stand still.
                {
                    unsigned long long us = now_us();
                    if (!media_start_us) media_start_us = us;

                    unsigned int stamp = (unsigned int)(((us - media_start_us) * 9ULL) / 100ULL);
                    if (L->rtp.timestamp && stamp <= L->rtp.timestamp) stamp = L->rtp.timestamp + 1;
                    L->rtp.timestamp = stamp;
                }

                int n = rtpvid::send_frame(&L->rtp, payload, (int)payload_len,
                                           PATH_MTU - RTP_CLEAR_BYTES - CRYPTO_TAIL,
                                           0, send_rtp);
                // Only the full layer is counted, so the frame rate in the log
                // stays the frame rate of the picture rather than its sum over
                // however many sizes it went out at.
                if (n > 0 && layer == 0) InterlockedIncrement(&g_frames);
            }
            }

            unsigned long long now = GetTickCount64();

            if (now - last_report >= 1000)
            {
                last_report = now;
                send_sender_report();
            }

            if (now - last_stats >= 5000)
            {
                last_stats = now;
                log_line("share: %u кадров, %u пакетов, %u КБ, e2ee %s, сбоев %u | "
                         "от сервера %u: RR %u, PLI %u, NACK %u | RTX отдано %u, поздно %u",
                         (unsigned int)g_frames, (unsigned int)g_packets,
                         (unsigned int)(g_bytes / 1024),
                         g_e2ee_ready ? "да" : "НЕТ", (unsigned int)g_protect_failures,
                         (unsigned int)g_udp_in, (unsigned int)g_rtcp_receiver,
                         (unsigned int)g_rtcp_pli, (unsigned int)g_rtcp_nack,
                         (unsigned int)g_rtx_sent, (unsigned int)g_rtx_missed);
            }

            // Nothing is sent to the main gateway while streaming. op 21 is not
            // a keepalive the way its name suggests: it asks the server to
            // reallocate the stream server, so sending it on a timer kept
            // moving the stream out from under whoever was watching. A working
            // client sends one op 18 and then leaves the gateway alone.

            Sleep(1);
        }

        loopback::stop();
        if (g_censor_frame) { memfree(g_censor_frame); g_censor_frame = 0; g_censor_cap = 0; }
        if (g_silence) { opus_encoder_destroy(g_silence); g_silence = 0; }

        for (int i = 0; i < g_layer_count; i++)
        {
            venc::stop(&g_layers[i].enc);
            if (g_layers[i].scaled) { memfree(g_layers[i].scaled); g_layers[i].scaled = 0; }
        }
        g_layer_count = 0;
        capture::stop();
        CoUninitialize();
        return 0;
    }

    DWORD WINAPI beat_thread(LPVOID)
    {
        unsigned int nonce = 0;
        while (g_running)
        {
            long interval = g_heartbeat_ms;
            if (interval <= 0) { Sleep(50); continue; }

            for (long slept = 0; slept < interval && g_running; slept += 50) Sleep(50);
            if (!g_running) break;

            jwriter w;
            w.init();
            w.begin_obj();
            w.kv_i64("op", VOP_HEARTBEAT);
            w.key("d");
            w.begin_obj();
            w.kv_i64("t", (long long)(++nonce));
            w.kv_i64("seq_ack", -1);
            w.end_obj();
            w.end_obj();
            g_ws.send_text(w.buf.data, w.buf.size);
            w.free_writer();
        }
        return 0;
    }

    // ---- websocket loop --------------------------------------------------

    void handle_ready(const jval* d)
    {
        g_audio_ssrc = (unsigned int)d->i64("ssrc", 0);
        // The video and retransmission sources follow the audio one.
        g_video_ssrc = g_audio_ssrc + 1;
        g_rtx_ssrc = g_audio_ssrc + 2;

        const char* ip = d->str("ip", 0);
        if (ip) ccstrncpy(g_udp_host, ip, sizeof(g_udp_host) - 1);
        g_udp_port = (unsigned short)d->i64("port", 0);

        const jval* modes = d->arr("modes");
        bool has_gcm = false, has_xchacha = false;
        for (unsigned int i = 0; i < modes->count; i++)
        {
            const char* m = modes->at(i)->as_str("");
            if (ccscmp(m, "aead_aes256_gcm_rtpsize") == 0) has_gcm = true;
            if (ccscmp(m, "aead_xchacha20_poly1305_rtpsize") == 0) has_xchacha = true;
        }

        const char* chosen = 0;
        if (has_gcm && crypto::aes256gcm_available()) { g_mode = MODE_AES256_GCM; chosen = "aead_aes256_gcm_rtpsize"; }
        else if (has_xchacha) { g_mode = MODE_XCHACHA20; chosen = "aead_xchacha20_poly1305_rtpsize"; }
        else { set_status(SHARE_FAILED, "Сервер не предлагает поддерживаемое шифрование"); return; }

        log_line("share: ready audio_ssrc=%u video_ssrc=%u rtx=%u %s:%u mode=%s",
                 g_audio_ssrc, g_video_ssrc, g_rtx_ssrc, g_udp_host, g_udp_port, chosen);

        if (!udp_connect()) { set_status(SHARE_FAILED, "UDP-соединение не установлено"); return; }

        char external_ip[80];
        unsigned short external_port = 0;
        if (!udp_discover(external_ip, sizeof(external_ip), &external_port))
        {
            set_status(SHARE_FAILED, "IP discovery не ответил");
            return;
        }
        log_line("share: discovered %s:%u", external_ip, external_port);

        // A working client asks the server which backend it is talking to right
        // after ready, before selecting a protocol.
        {
            jwriter w;
            w.init();
            w.begin_obj();
            w.kv_i64("op", VOP_BACKEND_VERSION);
            w.key("d");
            w.begin_obj();
            w.end_obj();
            w.end_obj();
            send_json(&w);
            w.free_writer();
        }

        // Discovery is done, so the socket stops blocking: from here the pump
        // polls it between frames and a quarter second wait would eat the whole
        // frame budget every time nothing had arrived.
        unsigned long nonblocking = 1;
        ioctlsocket(g_udp, FIONBIO, &nonblocking);
        g_udp_blocking = false;

        send_select_protocol(external_ip, external_port, chosen);
    }

    void handle_session_description(const jval* d)
    {
        const jval* key = d->arr("secret_key");
        if (key->count < 32)
        {
            set_status(SHARE_FAILED, "Сервер не прислал ключ");
            return;
        }
        // as_i64, not i64: the elements of the array are bare numbers, and i64
        // looks a member up by name. Called on a number it finds nothing and
        // hands back the default, which left the whole key zero. Everything
        // downstream still worked - the stream connected, the group formed, the
        // packets went out - and the server simply could not open any of them,
        // which reaches us only as a keyframe request once a second forever.
        unsigned char seen = 0;
        for (int i = 0; i < 32; i++)
        {
            g_secret_key[i] = (unsigned char)key->at((unsigned int)i)->as_i64(0);
            seen |= g_secret_key[i];
        }
        if (!seen)
        {
            set_status(SHARE_FAILED, "Ключ сессии пустой");
            log_line("share: секретный ключ разобрался в одни нули, стрим не пойдёт");
            return;
        }

        const char* mode = d->str("mode", "");
        log_line("share: session description, mode %s", mode);

        // Debug aid: the session key and the sources, written where a capture
        // analyser can find them. The log itself is no help here, because a run
        // started elevated writes it into a different profile.
        {
            char path[MAX_PATH];
            if (GetEnvironmentVariableA("IMD_KEYDUMP", path, sizeof(path)) > 0)
            {
                char text[512];
                int at = cnprint(text, sizeof(text), "%u %u %u ",
                                 g_audio_ssrc, g_video_ssrc, g_rtx_ssrc);
                for (int i = 0; i < 32 && at < (int)sizeof(text) - 8; i++)
                    at += cnprint(text + at, sizeof(text) - at, "%s%u",
                                  i ? "," : "", (unsigned int)g_secret_key[i]);

                wchar_t wide[MAX_PATH];
                chartowcs(path, wide, MAX_PATH);
                ufile::write_all(wide, text, (unsigned int)at);
                log_line("share: ключ сессии записан в %s", path);
            }
        }

        InterlockedExchange(&g_media_ready, 1);

        if (!g_pump_thread)
            g_pump_thread = CreateThread(0, 0, pump_thread, 0, 0, 0);
    }

    DWORD WINAPI ws_thread(LPVOID)
    {
        CoInitializeEx(0, COINIT_MULTITHREADED);

        char url[400];
        cnprint(url, sizeof(url), "wss://%s/?v=8", g_endpoint);
        log_line("share: connecting to %s", url);

        g_ws.init();
        if (!g_ws.connect(url, 0))
        {
            set_status(SHARE_FAILED, "Голосовой сокет стрима не открылся");
            CoUninitialize();
            return 0;
        }

        set_status(SHARE_CONNECTING, "Рукопожатие стрима...");
        send_identify();

        ubuffer msg;
        msg.init();

        while (g_running)
        {
            bool binary = false;
            ws_result r = g_ws.receive(&msg, &binary);
            if (r != WS_MESSAGE) break;
            if (binary) { handle_binary_payload(msg.data, msg.size); continue; }

            jdoc doc;
            doc.init();
            if (!doc.parse((const char*)msg.data, (int)msg.size)) { doc.free_doc(); continue; }

            const jval* root = doc.r();
            int op = (int)root->i64("op", -1);
            const jval* d = root->obj("d");

            if (op != VOP_HEARTBEAT_ACK) log_payload("<-", msg.data, msg.size);

            switch (op)
            {
            case VOP_HELLO:
                InterlockedExchange(&g_heartbeat_ms, (long)d->i64("heartbeat_interval", 41250));
                if (!g_beat_thread) g_beat_thread = CreateThread(0, 0, beat_thread, 0, 0, 0);
                break;
            case VOP_READY:
                handle_ready(d);
                break;
            case VOP_SESSION_DESCRIPTION:
                handle_session_description(d);
                break;
            case VOP_DAVE_PREPARE_TRANSITION:
            case VOP_DAVE_EXECUTE_TRANSITION:
            case VOP_DAVE_PREPARE_EPOCH:
                // The epoch machinery arrives as plain JSON; the group itself is
                // rebuilt by the binary ops. Answering is what lets the server
                // move everyone forward.
                send_transition_response((unsigned int)d->i64("transition_id", 0), true);
                break;
            case VOP_MEDIA_SINK_WANTS:
            {
                // The server reports how many pixels each source is wanted at.
                // Our own ssrc turning non-zero means somebody has opened the
                // stream and is waiting for a frame they can decode from.
                char key[24];
                cnprint(key, sizeof(key), "%u", g_video_ssrc);
                long long wanted = d->i64(key, -1);
                if (wanted < 0) wanted = d->obj("pixelCounts")->i64(key, 0);
                if (wanted > 0)
                {
                    log_line("share: viewer wants %lld pixels, forcing a keyframe", wanted);
                    InterlockedExchange(&g_want_keyframe, 1);
                }
                break;
            }
            default:
                break;
            }

            doc.free_doc();
        }

        msg.free_buffer();
        log_line("share: websocket loop ended (close %u)", g_ws.close_status);
        CoUninitialize();
        return 0;
    }
}

// ---------------------------------------------------------------------------

void screenshare::init()
{
    ccfset(g_status, 0, sizeof(g_status));
    capture::init();
    venc::init();
}

void screenshare::shutdown()
{
    stop();
    venc::shutdown();
    capture::shutdown();
}

void screenshare::set_audio(bool on)
{
    g_want_audio = on;

    // Not streaming yet: the choice is simply remembered for when it starts.
    if (!g_running) return;

    if (on)
    {
        if (!loopback::running() && !loopback::start())
            log_line("share: звук системы не пошёл (%s)", loopback::last_error());
    }
    else if (loopback::running())
    {
        loopback::stop();
        log_line("share: звук системы выключен");
    }
}

bool screenshare::audio_running() { return loopback::running(); }
const char* screenshare::audio_error() { return loopback::last_error(); }

bool screenshare::start(int monitor_index, int max_width, int max_height, int fps,
                        int bitrate_kbps, bool with_audio, int method)
{
    g_self_id = store::self_id();

    if (g_running) return false;

    snowflake channel = voice::current_channel();
    if (!channel)
    {
        set_status(SHARE_FAILED, "Сначала надо зайти в голосовой канал");
        return false;
    }

    g_guild_id = voice::current_guild();
    g_channel_id = channel;
    g_monitor = monitor_index;
    g_max_w = max_width;
    g_max_h = max_height;
    g_fps = fps > 0 ? fps : 30;
    g_bitrate = bitrate_kbps > 0 ? bitrate_kbps : 2500;
    g_want_audio = with_audio;
    g_capture_method = method;

    g_frames = 0;
    g_packets = 0;
    g_bytes = 0;
    g_nonce_counter = 0;
    g_mode = MODE_NONE;
    g_media_ready = 0;
    g_endpoint[0] = 0;
    g_token[0] = 0;
    g_rtc_server_id[0] = 0;

    build_stream_key();
    make_connection_id(g_connection_id, sizeof(g_connection_id));
    log_line("share: stream key %s, connection %s", g_stream_key, g_connection_id);

    {
        char on[8];
        g_force_passthrough = GetEnvironmentVariableA("IMD_NO_E2EE", on, sizeof(on)) > 0;
        if (g_force_passthrough) log_line("share: E2EE отключено для проверки");

        char path[MAX_PATH];
        if (GetEnvironmentVariableA("IMD_FRAMEDUMP", path, sizeof(path)) > 0)
        {
            wchar_t wide[MAX_PATH];
            chartowcs(path, wide, MAX_PATH);
            g_frame_dump = CreateFileW(wide, GENERIC_WRITE, 0, 0, CREATE_ALWAYS,
                                       FILE_ATTRIBUTE_NORMAL, 0);
            g_frames_dumped = 0;
            log_line("share: кадры пишутся в %s", path);
        }
    }

    InterlockedExchange(&g_running, 1);
    set_status(SHARE_REQUESTING, "Запрашиваем стрим у discord...");

    send_gateway_stream_create();
    return true;
}

void screenshare::stop()
{
    if (!g_running && g_state == SHARE_IDLE) return;

    InterlockedExchange(&g_running, 0);

    if (g_media_ready)
    {
        send_video(false, 0, 0);
        send_speaking(false);
    }
    send_gateway_stream_delete();

    g_ws.close();

    if (g_pump_thread) { WaitForSingleObject(g_pump_thread, 4000); CloseHandle(g_pump_thread); g_pump_thread = 0; }
    if (g_beat_thread) { WaitForSingleObject(g_beat_thread, 2000); CloseHandle(g_beat_thread); g_beat_thread = 0; }
    if (g_ws_thread) { WaitForSingleObject(g_ws_thread, 4000); CloseHandle(g_ws_thread); g_ws_thread = 0; }

    g_ws.destroy();

    if (g_udp != INVALID_SOCKET) { closesocket(g_udp); g_udp = INVALID_SOCKET; }
    if (g_frame_dump) { CloseHandle(g_frame_dump); g_frame_dump = 0; }

    InterlockedExchange(&g_media_ready, 0);
    InterlockedExchange(&g_heartbeat_ms, 0);
    set_status(SHARE_IDLE, "");
}

screenshare_state screenshare::state() { return (screenshare_state)g_state; }
const char* screenshare::status_text() { return g_status; }
unsigned int screenshare::frames_sent() { return (unsigned int)g_frames; }
unsigned int screenshare::packets_sent() { return (unsigned int)g_packets; }
unsigned int screenshare::kb_sent() { return (unsigned int)(g_bytes / 1024); }

void screenshare::on_stream_create(const jval* d)
{
    if (!g_running) return;

    const char* key = d->str("stream_key", 0);
    const char* rtc = d->str("rtc_server_id", 0);

    // Several streams can exist in a channel; only ours matters.
    if (key && g_stream_key[0] && ccscmp(key, g_stream_key) != 0)
    {
        log_line("share: ignoring STREAM_CREATE for %s", key);
        return;
    }

    if (rtc) ccstrncpy(g_rtc_server_id, rtc, sizeof(g_rtc_server_id) - 1);
    if (key) ccstrncpy(g_stream_key, key, sizeof(g_stream_key) - 1);

    // Nothing is sent back. A working client answers this dispatch with
    // silence: the stream is already unpaused, and telling the server so again
    // is one more thing that can go wrong.
    log_line("share: STREAM_CREATE accepted, rtc_server_id %s", g_rtc_server_id);
}

void screenshare::on_stream_server_update(const jval* d)
{
    if (!g_running) return;

    // A stream being watched sends the same dispatch, and taking its endpoint
    // would point our own broadcast at somebody else's server.
    const char* key = d->str("stream_key", 0);
    if (key && g_stream_key[0] && ccscmp(key, g_stream_key) != 0) return;

    const char* endpoint = d->str("endpoint", 0);
    const char* token = d->str("token", 0);
    if (!endpoint || !token) return;

    ccstrncpy(g_endpoint, endpoint, sizeof(g_endpoint) - 1);
    ccstrncpy(g_token, token, sizeof(g_token) - 1);

    // The rtc server id also arrives here for a call, where STREAM_CREATE has
    // no guild to key it on.
    const char* guild = d->str("guild_id", 0);
    if (!g_rtc_server_id[0] && guild) ccstrncpy(g_rtc_server_id, guild, sizeof(g_rtc_server_id) - 1);

    log_line("share: STREAM_SERVER_UPDATE endpoint %s", g_endpoint);

    if (!g_ws_thread) g_ws_thread = CreateThread(0, 0, ws_thread, 0, 0, 0);
}

// Kept for the gateway to call. While media is being held across an account
// switch the gateway does not call it at all, which is the point: the stream
// has its own socket and its own accepted token.
void screenshare::on_gateway_disconnected()
{
    if (g_running) stop();
}
