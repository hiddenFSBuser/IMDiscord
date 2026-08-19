#include "pch.h"
#include <objbase.h>

#include "streamview.h"
#include "decoder.h"
#include "rtp_video.h"

#include "discord/store.h"
#include "discord/rest.h"
#include "discord/gateway.h"
#include "discord/voice.h"
#include "core/log.h"
#include "audio/audio.h"
#include "opus.h"
#include "core/crypto.h"
#include "net/websocket.h"
#include "net/json.h"
#include "dave/mls_types.h"
#include "dave/mls_message.h"
#include "dave/mls_group.h"
#include "dave/dave_frames.h"
#include "dave/tls_codec.h"

// Watching somebody else's Go Live stream. This is screenshare.cpp read
// backwards: the same second voice connection, the same DAVE group, the same
// RTP shape, with the media running the other way.
//
// The differences worth knowing before reading:
//
//   * The stream is joined with gateway op 20 STREAM_WATCH rather than created
//     with op 18. Stopping is op 19 with the same key, which the server reads
//     as leaving rather than deleting because the stream is not ours.
//   * IDENTIFY declares no streams and op 12 declares no video ssrc: nothing is
//     produced on this connection.
//   * op 15 MEDIA_SINK_WANTS is what actually asks for the media. Until it is
//     sent the server has a viewer that never said what it wanted, and not one
//     packet arrives.
//   * The whole picture is protected end to end, so a packet lost inside a
//     frame does not blur it, it stops the frame decrypting. Missing frames are
//     answered with a keyframe request rather than patched over.
//
// Only one stream is watched at a time. Two would need two of everything below
// and a viewer cannot look at two screens at once anyway.

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
        GWOP_STREAM_DELETE = 19,
        GWOP_STREAM_WATCH = 20,
    };

    enum encryption_mode
    {
        MODE_NONE = 0,
        MODE_AES256_GCM,
        MODE_XCHACHA20,
    };

    const int PAYLOAD_H264 = 105;
    const int PAYLOAD_OPUS = 120;

    websocket g_ws;
    SOCKET g_udp = INVALID_SOCKET;

    HANDLE g_ws_thread = 0;
    HANDLE g_pump_thread = 0;
    HANDLE g_beat_thread = 0;

    volatile long g_running = 0;
    volatile long g_state = WATCH_IDLE;
    volatile long g_heartbeat_ms = 0;

    char g_status[192];
    char g_endpoint[256];
    char g_token[256];
    char g_stream_key[192];
    char g_rtc_server_id[64];
    char g_connection_id[48];

    snowflake g_guild_id = 0;
    snowflake g_channel_id = 0;
    snowflake g_user_id = 0;

    unsigned int g_self_ssrc = 0;          // ours on this connection, for RTCP
    volatile long g_remote_video_ssrc = 0; // theirs, announced by op 12
    volatile long g_remote_audio_ssrc = 0;

    // Sound from the share, which used to be thrown away at the door. Its own
    // decoder rather than the call's: this is a different sender, a different
    // sequence and a different end to end group.
    OpusDecoder* g_audio_decoder = 0;
    unsigned short g_audio_next_seq = 0;
    bool g_audio_have_seq = false;
    volatile long g_audio_packets = 0;
    volatile long g_audio_muted = 0;
    volatile long g_remote_rtx_ssrc = 0;
    unsigned short g_udp_port = 0;
    char g_udp_host[128];
    unsigned char g_secret_key[32];
    encryption_mode g_mode = MODE_NONE;
    unsigned int g_nonce_counter = 0;

    volatile long g_packets = 0;      // everything the socket handed us
    volatile long g_video_packets = 0;// of those, the ones that opened as video
    volatile long g_assembled = 0;    // whole access units the depacketiser built
    volatile long g_frames = 0;       // pictures the decoder produced
    volatile long g_decrypt_failures = 0;
    volatile long g_want_keyframe = 0;
    bool g_logged_first_media = false;
    const char* g_last_dave_error = "";
    const char* g_last_dave_detail = "";

    // Set whenever a frame is lost, for any reason. H.264 is a chain of
    // differences: a decoder handed the frames after a missing one paints them
    // onto whatever it had, and the damage never washes out on its own. Until a
    // frame that can stand alone arrives, nothing is worth decoding.
    bool g_need_keyframe_ref = true;
    volatile long g_skipped_until_idr = 0;
    unsigned long long g_waiting_since = 0;
    unsigned int g_dave_reports = 0;

    rtp_h264_rx g_rx;

    // ---- DAVE ------------------------------------------------------------
    //
    // A viewer joins the streamer's group rather than starting one, but the
    // machinery is the same: answer the external sender with a key package,
    // then wait to be welcomed. Until that happens the sender is either alone
    // and sending in the clear, or sending to a group we are not in yet, and in
    // the second case there is nothing to do but wait.
    unsigned char g_sig_private[96];
    unsigned char g_sig_public[65];
    bool g_sig_ready = false;

    mls::key_package g_key_package;
    mls::key_package_private g_key_package_private;
    bool g_key_package_ready = false;

    mls::group_state g_group;
    bool g_group_ready = false;
    volatile long g_e2ee_ready = 0;

    // A commit names the proposals it carries by reference, so the ones op 27
    // announced have to be kept until the commit that applies them arrives.
    mls::cached_proposal g_known[mls::MAX_MEMBERS];
    unsigned int g_known_count = 0;

    // What the group side of the handshake actually did, for the window. A
    // frame that will not open is nearly always a key from an epoch the group
    // has left, and these are what tell that apart from anything else.
    volatile long g_op_external = 0;
    volatile long g_op_proposals = 0;
    volatile long g_op_commit = 0;
    volatile long g_op_welcome = 0;
    volatile long g_commits_applied = 0;
    const char* g_commit_error = "";

    // ---- decoded pictures --------------------------------------------------
    //
    // Three buffers: one the pump is writing, one holding the newest finished
    // picture, and one the UI is drawing from. They rotate under the lock, so
    // neither side ever waits on the other for longer than a pointer swap.
    struct vframe
    {
        unsigned char* rgba;
        int w, h;
        int cap;
    };

    vframe g_decode = { 0, 0, 0, 0 };
    vframe g_ready = { 0, 0, 0, 0 };
    vframe g_display = { 0, 0, 0, 0 };
    bool g_ready_full = false;
    CRITICAL_SECTION g_frame_lock;
    // The handshake, the heartbeat and the pump all write to the websocket, and
    // two frames interleaved on it are two frames the server cannot read.
    CRITICAL_SECTION g_send_lock;
    bool g_locks_ready = false;

    volatile long g_last_w = 0;
    volatile long g_last_h = 0;

    // ---- RTCP bookkeeping --------------------------------------------------
    bool g_have_base = false;
    unsigned short g_base_seq = 0;
    unsigned short g_max_seq = 0;
    unsigned int g_seq_cycles = 0;
    unsigned int g_received = 0;
    unsigned int g_last_expected = 0;
    unsigned int g_last_received = 0;
    unsigned int g_last_sr_middle = 0;
    unsigned long long g_last_sr_at = 0;

    void set_status(streamview_state s, const char* text)
    {
        InterlockedExchange(&g_state, (long)s);
        ccfset(g_status, 0, sizeof(g_status));
        if (text) ccstrncpy(g_status, text, sizeof(g_status) - 1);
        log_line("watch: %s", text ? text : "");
    }

    void log_payload(const char* prefix, const void* data, unsigned int size)
    {
        char text[600];
        unsigned int n = size < sizeof(text) - 1 ? size : (unsigned int)sizeof(text) - 1;
        ccpy(text, data, n);
        text[n] = 0;
        log_line("watch: %s %s%s", prefix, text, n < size ? " ..." : "");
    }

    // NV12 now, not RGBA: the conversion happens on the graphics card when the
    // picture is sampled, so what travels through here is what the decoder
    // produced. Three bytes for every two pixels instead of four for each.
    bool frame_reserve(vframe* f, int w, int h)
    {
        int need = w * h * 3 / 2;
        if (need <= 0) return false;

        if (need > f->cap)
        {
            unsigned char* fresh = (unsigned char*)memalloc(need);
            if (!fresh) return false;
            if (f->rgba) memfree(f->rgba);
            f->rgba = fresh;
            f->cap = need;
        }
        f->w = w;
        f->h = h;
        return true;
    }

    void free_frames()
    {
        vframe* all[3] = { &g_decode, &g_ready, &g_display };
        for (int i = 0; i < 3; i++)
        {
            if (all[i]->rgba) memfree(all[i]->rgba);
            all[i]->rgba = 0;
            all[i]->cap = all[i]->w = all[i]->h = 0;
        }
        g_ready_full = false;
    }

    // ---- gateway ---------------------------------------------------------

    void build_stream_key()
    {
        if (g_guild_id)
            cnprint(g_stream_key, sizeof(g_stream_key), "guild:%llu:%llu:%llu",
                    g_guild_id, g_channel_id, g_user_id);
        else
            cnprint(g_stream_key, sizeof(g_stream_key), "call:%llu:%llu",
                    g_channel_id, g_user_id);
    }

    void send_gateway_watch()
    {
        jwriter w;
        w.init();
        w.begin_obj();
        w.kv_i64("op", GWOP_STREAM_WATCH);
        w.key("d");
        w.begin_obj();
        w.kv_str("stream_key", g_stream_key);
        w.end_obj();
        w.end_obj();

        log_payload("-> gateway op 20", w.buf.data, w.buf.size);
        gateway::send_raw(w.buf.data, w.buf.size);
        w.free_writer();
    }

    // The same opcode that ends a stream of our own. On somebody else's key the
    // server reads it as this viewer leaving.
    void send_gateway_unwatch()
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

        log_line("watch: -> gateway op 19 (перестаём смотреть)");
        gateway::send_raw(w.buf.data, w.buf.size);
        w.free_writer();
    }

    // ---- stream voice websocket -----------------------------------------

    bool send_json(jwriter* w)
    {
        log_payload("->", w->buf.data, w->buf.size);

        if (!g_locks_ready) return g_ws.send_text(w->buf.data, w->buf.size);

        EnterCriticalSection(&g_send_lock);
        bool ok = g_ws.send_text(w->buf.data, w->buf.size);
        LeaveCriticalSection(&g_send_lock);
        return ok;
    }

    void make_connection_id(char* out, int cap)
    {
        unsigned char raw[16];
        crypto::random_bytes(raw, sizeof(raw));
        raw[6] = (unsigned char)((raw[6] & 0x0F) | 0x40);
        raw[8] = (unsigned char)((raw[8] & 0x3F) | 0x80);

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
        {
            char channel[32];
            unsigned long long id = ccstrtoull(g_rtc_server_id, 0, 10);
            cnprint(channel, sizeof(channel), "%llu", id ? id - 1 : 0);
            w.kv_str("channel_id", channel);
        }
        w.kv_snowflake("user_id", store::self_id());
        w.kv_str("session_id", voice::session_id());
        w.kv_str("token", g_token);
        // Still true for a viewer: it says this connection deals in video, not
        // that this end produces any.
        w.kv_bool("video", true);
        w.kv_i64("max_dave_protocol_version", 1);
        // Empty, because nothing is sent from here. Declaring a stream would
        // have the server allocate sources for a picture that never comes.
        w.key("streams");
        w.begin_arr();
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

        // The same table the sending side offers. It is what maps an incoming
        // payload type back to a codec, so a viewer needs it just as much.
        w.key("codecs");
        w.begin_arr();
        // The same honesty is needed on this side. Claiming to decode four
        // codecs there is no decoder for invites the server to relay a stream
        // in one of them, and then nothing arrives that can be shown.
        add_codec(&w, "opus", "audio", PAYLOAD_OPUS, -1, 1000);
        add_codec(&w, "H264", "video", PAYLOAD_H264, 106, 1000);
        w.end_arr();

        w.kv_str("rtc_connection_id", g_connection_id);
        w.key("experiments");
        w.begin_arr();
        w.val_str("fixed_keyframe_interval");
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

    // Nothing is produced here, and the server is told so plainly. Leaving this
    // out leaves it expecting a picture from a viewer.
    void send_no_video()
    {
        jwriter w;
        w.init();
        w.begin_obj();
        w.kv_i64("op", VOP_VIDEO);
        w.key("d");
        w.begin_obj();
        w.kv_i64("audio_ssrc", (long long)g_self_ssrc);
        w.kv_i64("video_ssrc", 0);
        w.kv_i64("rtx_ssrc", 0);
        w.key("streams");
        w.begin_arr();
        w.end_arr();
        w.end_obj();
        w.end_obj();

        send_json(&w);
        w.free_writer();
    }

    // This is the subscription, and nothing arrives without it. The reference
    // client only ever sends it naming a source, so the shape with the ssrc in
    // it is the one known to work; "any" is sent alongside because the source
    // is not known until the server announces it, and something has to ask in
    // the meantime.
    //
    // The layout is the server's own, read straight off what it sends a
    // broadcaster: a quality per ssrc, an "any" fallback, and a pixel count.
    void send_sink_wants()
    {
        unsigned int remote = (unsigned int)g_remote_video_ssrc;

        jwriter w;
        w.init();
        w.begin_obj();
        w.kv_i64("op", VOP_MEDIA_SINK_WANTS);
        w.key("d");
        w.begin_obj();
        w.kv_i64("any", 100);

        if (remote)
        {
            char key[24];
            cnprint(key, sizeof(key), "%u", remote);
            w.kv_i64(key, 100);

            w.key("pixelCounts");
            w.begin_obj();
            // 1280x720, which is what a full quality share is asked for at.
            w.kv_i64(key, 921600);
            w.end_obj();
        }
        w.end_obj();
        w.end_obj();

        send_json(&w);
        w.free_writer();
    }

    // ---- DAVE handshake --------------------------------------------------

    bool send_binary(unsigned char opcode, const void* payload, unsigned int len)
    {
        ubuffer frame;
        frame.init(len + 8);
        frame.append_char((char)opcode);
        if (len) frame.append(payload, len);

        bool ok;
        if (g_locks_ready)
        {
            EnterCriticalSection(&g_send_lock);
            ok = g_ws.send_binary(frame.data, frame.size);
            LeaveCriticalSection(&g_send_lock);
        }
        else
        {
            ok = g_ws.send_binary(frame.data, frame.size);
        }

        frame.free_buffer();
        return ok;
    }

    void send_key_package()
    {
        if (!g_sig_ready)
        {
            if (!crypto::p256_generate(g_sig_public, g_sig_private))
            {
                log_line("watch/dave: could not create the MLS signature key");
                return;
            }
            g_sig_ready = true;
        }

        if (!mls::create_key_package(store::self_id(), g_sig_private,
                                     &g_key_package, &g_key_package_private))
        {
            log_line("watch/dave: key package creation failed");
            return;
        }
        g_key_package_ready = true;

        tls_writer w;
        w.init(1024);
        g_key_package.write(&w);

        log_line("watch/dave: sending key package (%u bytes, self verify %s)",
                 w.size(), g_key_package.verify() ? "ok" : "FAILED");

        send_binary(VOP_MLS_KEY_PACKAGE, w.data(), w.size());
        w.free_writer();

        // The same identifier the streamer keyed the group on: the rtc server
        // id less one. A viewer that computes a different one is welcomed into
        // a group whose messages it cannot follow.
        unsigned long long dave_id = ccstrtoull(g_rtc_server_id, 0, 10);
        if (dave_id) dave_id -= 1;

        unsigned char group_id[8];
        for (int i = 0; i < 8; i++)
            group_id[i] = (unsigned char)(dave_id >> (56 - i * 8));

        g_group_ready = mls::create_group(&g_group, group_id, 8, &g_key_package.leaf,
                                          g_sig_private, g_key_package_private.encryption_private);

        log_line("watch/dave: local group %s (id %llu)",
                 g_group_ready ? "created" : "FAILED", dave_id);
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

    void handle_binary_payload(const unsigned char* data, unsigned int len)
    {
        if (len < 3) return;

        unsigned int seq = ((unsigned int)data[0] << 8) | data[1];
        unsigned char opcode = data[2];
        const unsigned char* payload = data + 3;
        unsigned int payload_len = len - 3;

        log_line("watch/dave: binary op %u, seq %u, %u bytes", opcode, seq, payload_len);

        switch (opcode)
        {
        case VOP_MLS_EXTERNAL_SENDER:
            InterlockedIncrement(&g_op_external);
            send_key_package();
            break;

        case VOP_MLS_PROPOSALS:
        {
            InterlockedIncrement(&g_op_proposals);
            // A viewer never commits - the group is the streamer's to change -
            // but it has to remember what was proposed, because the commit that
            // follows refers back to it rather than repeating it.
            bool is_revoke = false;
            mls::proposal_message messages[mls::MAX_MEMBERS];
            unsigned int count = 0;

            if (!mls::parse_proposals_payload(payload, payload_len, &is_revoke,
                                              messages, mls::MAX_MEMBERS, &count))
            {
                log_line("watch/dave: предложения не разбираются");
                break;
            }

            if (is_revoke)
            {
                g_known_count = 0;
                break;
            }

            for (unsigned int i = 0; i < count && g_known_count < mls::MAX_MEMBERS; i++)
            {
                messages[i].compute_ref(g_known[g_known_count].ref);
                g_known[g_known_count].prop = messages[i].prop;
                g_known_count++;
            }
            log_line("watch/dave: запомнено предложений %u (всего %u)", count, g_known_count);
            break;
        }

        case VOP_MLS_ANNOUNCE_COMMIT_TRANSITION:
        {
            // [uint16 transition_id][MLSMessage commit]. Every member except
            // the author has to apply this; one that does not keeps deriving
            // media keys from an epoch the group has already left, and nothing
            // it receives will open again.
            InterlockedIncrement(&g_op_commit);
            if (payload_len < 3 || !g_group_ready) { g_commit_error = "группа ещё не готова"; break; }

            unsigned int transition_id = ((unsigned int)payload[0] << 8) | payload[1];

            const char* why = "";
            bool applied = mls::process_commit(&g_group, payload + 2, payload_len - 2,
                                               g_known, g_known_count, &why);
            g_commit_error = applied ? "применён" : why;
            if (applied) InterlockedIncrement(&g_commits_applied);

            log_line("watch/dave: коммит %s (transition %u)%s%s",
                     applied ? "применён" : "НЕ применён", transition_id,
                     applied ? "" : ": ", applied ? "" : why);

            if (applied)
            {
                // A new epoch means a new exporter secret, so every ratchet
                // built on the old one is worthless.
                g_known_count = 0;
                dave::reset_ratchets();
                InterlockedExchange(&g_want_keyframe, 1);
            }

            send_transition_response(transition_id, applied);
            break;
        }

        case VOP_MLS_WELCOME:
        {
            InterlockedIncrement(&g_op_welcome);
            if (payload_len < 3 || !g_key_package_ready)
            {
                log_line("watch/dave: welcome arrived before we had a key package");
                break;
            }

            unsigned int transition_id = ((unsigned int)payload[0] << 8) | payload[1];

            g_group_ready = mls::process_welcome(&g_group, payload + 2, payload_len - 2,
                                                 &g_key_package, &g_key_package_private,
                                                 g_sig_private);

            dave::reset_ratchets();
            g_known_count = 0;
            InterlockedExchange(&g_e2ee_ready, g_group_ready ? 1 : 0);

            log_line("watch/dave: welcome %s (transition %u)",
                     g_group_ready ? "accepted" : "REJECTED", transition_id);

            send_transition_response(transition_id, g_group_ready);

            // The ratchets restarted, so nothing already in flight decrypts.
            InterlockedExchange(&g_want_keyframe, 1);
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

        // A keyframe arrives as a burst of packets. The default receive buffer
        // drops the tail of it and the frame is then unrecoverable, because a
        // protected frame with a hole does not decrypt.
        int rcvbuf = 1 << 20;
        setsockopt(g_udp, SOL_SOCKET, SO_RCVBUF, (const char*)&rcvbuf, sizeof(rcvbuf));
        return true;
    }

    bool udp_discover(char* out_ip, int ip_cap, unsigned short* out_port)
    {
        unsigned char packet[74];
        ccfset(packet, 0, sizeof(packet));
        packet[0] = 0x00; packet[1] = 0x01;
        packet[2] = 0x00; packet[3] = 0x46;
        packet[4] = (unsigned char)(g_self_ssrc >> 24);
        packet[5] = (unsigned char)(g_self_ssrc >> 16);
        packet[6] = (unsigned char)(g_self_ssrc >> 8);
        packet[7] = (unsigned char)(g_self_ssrc);

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

    // The RTCP header travels in the clear and is the additional data; the body
    // is encrypted like any media payload. Same shape as the sending side.
    bool send_rtcp(const unsigned char* header, int header_len,
                   const unsigned char* body, int body_len)
    {
        if (g_udp == INVALID_SOCKET || g_mode == MODE_NONE) return false;

        unsigned char packet[512];
        if (header_len + body_len + 16 + 4 > (int)sizeof(packet)) return false;

        ccpy(packet, header, (size_t)header_len);

        unsigned int counter = InterlockedIncrement((volatile long*)&g_nonce_counter);
        unsigned char counter_bytes[4] = {
            (unsigned char)(counter >> 24), (unsigned char)(counter >> 16),
            (unsigned char)(counter >> 8), (unsigned char)(counter)
        };

        unsigned char* cipher = packet + header_len;
        unsigned char tag[16];
        bool ok = false;

        if (g_mode == MODE_XCHACHA20)
        {
            unsigned char nonce[24];
            ccfset(nonce, 0, sizeof(nonce));
            ccpy(nonce, counter_bytes, 4);
            crypto::xchacha20poly1305_encrypt(g_secret_key, nonce, packet, (unsigned int)header_len,
                                              body, (unsigned int)body_len, cipher, tag);
            ok = true;
        }
        else
        {
            unsigned char nonce[12];
            ccfset(nonce, 0, sizeof(nonce));
            ccpy(nonce, counter_bytes, 4);
            ok = crypto::aes256gcm_encrypt(g_secret_key, nonce, packet, (unsigned int)header_len,
                                           body, (unsigned int)body_len, cipher, tag);
        }
        if (!ok) return false;

        int len = header_len + body_len;
        ccpy(packet + len, tag, 16);
        len += 16;
        ccpy(packet + len, counter_bytes, 4);
        len += 4;

        return send(g_udp, (const char*)packet, len, 0) != SOCKET_ERROR;
    }

    // Picture loss indication: the one message that makes a sender produce an
    // IDR on demand. Without it a viewer joining mid-stream stares at nothing
    // until the sender's own keyframe timer comes round.
    void send_pli()
    {
        unsigned int remote = (unsigned int)g_remote_video_ssrc;
        if (!remote) return;

        unsigned char header[8];
        header[0] = 0x80 | 1;                  // version 2, feedback format 1
        header[1] = 206;                       // payload specific feedback
        header[2] = 0x00;
        header[3] = 0x02;                      // three words, minus one
        header[4] = (unsigned char)(g_self_ssrc >> 24);
        header[5] = (unsigned char)(g_self_ssrc >> 16);
        header[6] = (unsigned char)(g_self_ssrc >> 8);
        header[7] = (unsigned char)(g_self_ssrc);

        unsigned char body[4];
        body[0] = (unsigned char)(remote >> 24);
        body[1] = (unsigned char)(remote >> 16);
        body[2] = (unsigned char)(remote >> 8);
        body[3] = (unsigned char)(remote);

        send_rtcp(header, sizeof(header), body, sizeof(body));
    }

    // One report block for the stream being watched. A sender with no reports
    // coming back treats the route as dead and eventually gives up on it.
    void send_receiver_report()
    {
        unsigned int remote = (unsigned int)g_remote_video_ssrc;
        if (!remote || !g_have_base) return;

        unsigned char header[8];
        header[0] = 0x80 | 1;                  // one report block
        header[1] = 201;                       // receiver report
        header[2] = 0x00;
        header[3] = 0x07;                      // eight words, minus one
        header[4] = (unsigned char)(g_self_ssrc >> 24);
        header[5] = (unsigned char)(g_self_ssrc >> 16);
        header[6] = (unsigned char)(g_self_ssrc >> 8);
        header[7] = (unsigned char)(g_self_ssrc);

        unsigned int extended_max = (g_seq_cycles << 16) | g_max_seq;
        unsigned int expected = extended_max - g_base_seq + 1;
        unsigned int lost = expected > g_received ? expected - g_received : 0;

        unsigned int expected_interval = expected - g_last_expected;
        unsigned int received_interval = g_received - g_last_received;
        g_last_expected = expected;
        g_last_received = g_received;

        unsigned int lost_interval = expected_interval > received_interval
                                   ? expected_interval - received_interval : 0;
        unsigned char fraction = 0;
        if (expected_interval > 0)
            fraction = (unsigned char)((lost_interval << 8) / expected_interval);

        // How long ago the last sender report arrived, in 1/65536 of a second.
        unsigned int dlsr = 0;
        if (g_last_sr_at)
        {
            unsigned long long delta = GetTickCount64() - g_last_sr_at;
            dlsr = (unsigned int)((delta * 65536ULL) / 1000ULL);
        }

        unsigned char body[24];
        body[0] = (unsigned char)(remote >> 24);
        body[1] = (unsigned char)(remote >> 16);
        body[2] = (unsigned char)(remote >> 8);
        body[3] = (unsigned char)(remote);
        body[4] = fraction;
        body[5] = (unsigned char)(lost >> 16);
        body[6] = (unsigned char)(lost >> 8);
        body[7] = (unsigned char)(lost);

        // Highest sequence, interarrival jitter (not measured), last sender
        // report and the delay since it.
        unsigned int rest[4] = { extended_max, 0, g_last_sr_middle, dlsr };
        for (int i = 0; i < 4; i++)
        {
            body[8 + i * 4 + 0] = (unsigned char)(rest[i] >> 24);
            body[8 + i * 4 + 1] = (unsigned char)(rest[i] >> 16);
            body[8 + i * 4 + 2] = (unsigned char)(rest[i] >> 8);
            body[8 + i * 4 + 3] = (unsigned char)(rest[i]);
        }

        send_rtcp(header, sizeof(header), body, sizeof(body));
    }

    void note_sequence(unsigned short seq)
    {
        if (!g_have_base)
        {
            g_have_base = true;
            g_base_seq = seq;
            g_max_seq = seq;
            g_received = 1;
            return;
        }

        g_received++;
        // Wrapped when the new number is far below the old one rather than a
        // little, which is a reordering.
        if (seq < g_max_seq && (unsigned short)(g_max_seq - seq) > 0x8000) g_seq_cycles++;
        if (seq > g_max_seq || (unsigned short)(seq - g_max_seq) < 0x8000) g_max_seq = seq;
    }

    // ---- media -------------------------------------------------------------

    bool open_with_aad(const unsigned char* packet, int len, int aad_len,
                       unsigned char* out, int out_cap, int* out_len)
    {
        int cipher_len = len - aad_len - 16 - 4;
        if (cipher_len <= 0 || cipher_len > out_cap) return false;

        const unsigned char* counter_bytes = packet + len - 4;
        const unsigned char* cipher = packet + aad_len;
        const unsigned char* tag = packet + aad_len + cipher_len;

        bool ok = false;
        if (g_mode == MODE_XCHACHA20)
        {
            unsigned char nonce[24];
            ccfset(nonce, 0, sizeof(nonce));
            ccpy(nonce, counter_bytes, 4);
            ok = crypto::xchacha20poly1305_decrypt(g_secret_key, nonce, packet, (unsigned int)aad_len,
                                                   cipher, (unsigned int)cipher_len, tag, out);
        }
        else if (g_mode == MODE_AES256_GCM)
        {
            unsigned char nonce[12];
            ccfset(nonce, 0, sizeof(nonce));
            ccpy(nonce, counter_bytes, 4);
            ok = crypto::aes256gcm_decrypt(g_secret_key, nonce, packet, (unsigned int)aad_len,
                                           cipher, (unsigned int)cipher_len, tag, out);
        }

        if (!ok) return false;
        *out_len = cipher_len;
        return true;
    }

    // Which of the two layouts the far end is using. Settled by the first
    // packet that opens and reused after that, because trying the wrong one
    // first costs a whole AES pass per packet.
    //
    // The direction is not symmetric, and a capture of a working client shows
    // it plainly. What we send has the extension block inside the ciphertext,
    // so its additional data stops after the four byte extension header. What
    // comes back has the extension block in the clear - the ids and the rid
    // read straight off the wire - so its additional data covers the extension
    // payload as well and the media starts after it.
    int g_rx_layout = 0;                   // 0 unknown, 1 clear extensions, 2 encrypted

    // Undoes the transport layer and hands back the media alone.
    //
    // out_padding_only marks a packet that carried nothing but RTP padding.
    // Those are how a WebRTC sender probes for bandwidth, and the official
    // discord client sends a great many of them while our own sender sends
    // none - which is exactly why watching ourselves worked and watching them
    // did not.
    int decrypt_packet(const unsigned char* packet, int len,
                       unsigned char* out, int out_cap, bool* out_padding_only)
    {
        *out_padding_only = false;
        if (len < 12 + 16 + 4) return 0;
        if ((packet[0] & 0xC0) != 0x80) return 0;

        int csrc_count = packet[0] & 0x0F;
        bool has_extension = (packet[0] & 0x10) != 0;

        int base = 12 + csrc_count * 4;
        if (base + 16 + 4 > len) return 0;

        int words = 0;
        if (has_extension)
        {
            if (base + 4 + 16 + 4 > len) return 0;
            words = (packet[base + 2] << 8) | packet[base + 3];
            base += 4;
        }

        const int clear_aad = base + words * 4;   // extension payload readable
        const int inner_aad = base;               // extension payload encrypted

        int payload_len = 0;

        // Clear extensions first, then the other way round, until one is known
        // to work.
        for (int attempt = 0; attempt < 2; attempt++)
        {
            int layout = g_rx_layout ? g_rx_layout : (attempt == 0 ? 1 : 2);
            if (g_rx_layout && attempt) break;

            if (layout == 1)
            {
                if (clear_aad + 16 + 4 > len) continue;
                if (!open_with_aad(packet, len, clear_aad, out, out_cap, &payload_len)) continue;
            }
            else
            {
                if (!open_with_aad(packet, len, inner_aad, out, out_cap, &payload_len)) continue;

                int offset = words * 4;
                if (offset >= payload_len) continue;
                ccmov(out, out + offset, (size_t)(payload_len - offset));
                payload_len -= offset;
            }

            if (!g_rx_layout)
            {
                g_rx_layout = layout;
                log_line("watch: расширения приходят %s",
                         layout == 1 ? "открытыми" : "зашифрованными");
            }

            // The padding bit says the tail of the payload is filler, and its
            // last byte counts it, itself included.
            if ((packet[0] & 0x20) && payload_len > 0)
            {
                unsigned int pad = out[payload_len - 1];
                if (pad == 0 || (int)pad > payload_len) return 0;
                payload_len -= (int)pad;
            }

            if (payload_len == 0) *out_padding_only = true;
            return payload_len;
        }

        return 0;
    }

    // Sound from the share. Same shape as the picture path - transport
    // decryption, then the end to end layer, then a codec - but far simpler:
    // one opus packet is one frame, so there is nothing to reassemble.
    void handle_audio_packet(const unsigned char* packet, int got)
    {
        if (!g_audio_decoder || g_audio_muted) return;

        unsigned char media[1600];
        bool padding_only = false;
        int len = decrypt_packet(packet, got, media, sizeof(media), &padding_only);
        if (padding_only || len <= 0) return;

        const unsigned char* payload = media;
        unsigned int payload_len = (unsigned int)len;

        unsigned char plain[1600];
        // Padded protected frames keep the marker off the end of the buffer;
        // protected_len sees through the padding block to it.
        unsigned int prot_len = dave::protected_len(media, payload_len);
        if (prot_len)
        {
            if (!g_e2ee_ready || !g_group_ready) return;

            unsigned int out_len = 0;
            if (!dave::decrypt_frame(&g_group, g_user_id, media, prot_len,
                                     plain, &out_len))
                return;

            payload = plain;
            payload_len = out_len;
        }

        // Losses are covered rather than ignored: handing opus a packet that
        // does not follow the last one it saw makes it produce artefacts, and
        // saying so lets it conceal instead.
        unsigned short seq = (unsigned short)(((unsigned int)packet[2] << 8) | packet[3]);

        short pcm[AUDIO_FRAME_SAMPLES * AUDIO_CHANNELS];

        if (g_audio_have_seq)
        {
            int gap = (int)(short)(unsigned short)(seq - g_audio_next_seq);
            if (gap < 0) return;                 // arrived after its turn

            // A long gap is a pause in the share, not a burst of loss worth
            // inventing audio for.
            for (int i = 0; i < gap && i < 3; i++)
            {
                int concealed = opus_decode(g_audio_decoder, 0, 0, pcm,
                                            AUDIO_FRAME_SAMPLES, 0);
                if (concealed > 0) audio::write_stream(pcm, concealed * AUDIO_CHANNELS);
            }
        }

        int frames = opus_decode(g_audio_decoder, payload, (opus_int32)payload_len,
                                 pcm, AUDIO_FRAME_SAMPLES, 0);

        g_audio_next_seq = (unsigned short)(seq + 1);
        g_audio_have_seq = true;

        if (frames <= 0) return;

        // Do not let it pile up. Anything queued here is sound already
        // committed, and a share that stalls should go quiet rather than
        // catch up half a second later.
        if (audio::stream_backlog_ms() > 400) audio::clear_stream();

        audio::write_stream(pcm, frames * AUDIO_CHANNELS);
        InterlockedIncrement(&g_audio_packets);
    }

    // One finished access unit: unwrap the end to end protection if it is
    // there, decode, and put the picture where the UI can pick it up.
    unsigned char g_plain[1024 * 1024];

    void handle_frame(const unsigned char* frame, unsigned int len)
    {
        const unsigned char* payload = frame;
        unsigned int payload_len = len;

        // Same padding caveat as the audio path: the marker may sit ahead of
        // a padding block rather than at the end of the frame.
        unsigned int prot_len = dave::protected_len(frame, len);
        if (prot_len)
        {
            if (!g_e2ee_ready || !g_group_ready)
            {
                // Protected frames before we are in the group are not an error,
                // they are the window between the stream starting and the
                // streamer committing us in.
                return;
            }

            unsigned int out_len = 0;
            if (!dave::decrypt_frame_h264(&g_group, g_user_id, frame, prot_len,
                                          g_plain, (unsigned int)sizeof(g_plain), &out_len))
            {
                InterlockedIncrement(&g_decrypt_failures);
                g_need_keyframe_ref = true;
                g_last_dave_error = dave::last_h264_error();
                g_last_dave_detail = dave::last_h264_detail();

                // The first few are written out whole. Which check refused, and
                // what the frame looked like when it did, is the difference
                // between fixing this and guessing at it again.
                if (g_dave_reports < 5)
                {
                    g_dave_reports++;

                    char head[64], tail[80];
                    unsigned int n = len < 16 ? len : 16;
                    for (unsigned int i = 0; i < n; i++)
                        cnprint(head + i * 3, 4, "%02x ", frame[i]);
                    head[n * 3] = 0;

                    unsigned int m = len < 24 ? len : 24;
                    for (unsigned int i = 0; i < m; i++)
                        cnprint(tail + i * 3, 4, "%02x ", frame[len - m + i]);
                    tail[m * 3] = 0;

                    log_line("watch/dave: не открылся кадр %u байт: %s | трейлер объявлен %u | "
                             "начало %s| хвост %s",
                             len, g_last_dave_error, len >= 3 ? frame[len - 3] : 0, head, tail);
                }

                // A frame that will not open is usually one that arrived
                // during a key change; the next keyframe fixes it.
                InterlockedExchange(&g_want_keyframe, 1);
                return;
            }
            g_last_dave_error = "";
            payload = g_plain;
            payload_len = out_len;
        }

        if (!vdec::running() && !vdec::start()) return;

        // What this access unit carries decides whether it is any use right
        // now. A parameter set or an IDR can rebuild the picture from nothing;
        // anything else only makes sense on top of a picture we still have.
        bool standalone = false;
        for (unsigned int at = 0; at + 4 < payload_len; at++)
        {
            if (payload[at] || payload[at + 1] || payload[at + 2] != 1) continue;

            unsigned char type = (unsigned char)(payload[at + 3] & 0x1F);
            if (type == 5 || type == 7 || type == 8) { standalone = true; break; }
        }

        if (g_need_keyframe_ref && !standalone)
        {
            unsigned long long now = GetTickCount64();
            if (!g_waiting_since) g_waiting_since = now;

            // Feeding this would paint differences onto a reference that is
            // already wrong, which is how a stream turns into smeared rubbish
            // that never recovers.
            //
            // But holding out forever is worse than a spoiled picture: if the
            // sender will not produce a frame we can start from, showing
            // something beats an empty window. After a few seconds the gate
            // opens and the keyframe is simply asked for again.
            if (now - g_waiting_since < 1200)
            {
                InterlockedIncrement(&g_skipped_until_idr);
                InterlockedExchange(&g_want_keyframe, 1);
                return;
            }
        }

        if (standalone)
        {
            g_need_keyframe_ref = false;
            g_waiting_since = 0;
        }

        vdec::submit(payload, (int)payload_len, GetTickCount64() * 1000ULL);

        const unsigned char* planes = 0;
        int w = 0, h = 0;
        while (vdec::next_nv12(&planes, &w, &h))
        {
            if (!frame_reserve(&g_decode, w, h)) break;
            ccpy(g_decode.rgba, planes, (size_t)w * h * 3 / 2);

            EnterCriticalSection(&g_frame_lock);
            vframe swap = g_ready;
            g_ready = g_decode;
            g_decode = swap;
            g_ready_full = true;
            LeaveCriticalSection(&g_frame_lock);

            InterlockedExchange(&g_last_w, w);
            InterlockedExchange(&g_last_h, h);
            InterlockedIncrement(&g_frames);

            if (g_state == WATCH_WAITING) set_status(WATCH_LIVE, "Смотрим демонстрацию");
        }
    }

    // What the server sends is not what it accepts. Outgoing video is payload
    // type 105 because that is what select_protocol offered, but the packets
    // coming back carry the server's own numbering - a capture of a working
    // client shows the same source arriving as 103, its retransmissions as 104
    // and opus as 111. Matching on the payload type would throw the whole
    // stream away, so the source is chosen by ssrc instead: op 12 names it, and
    // a stream connection carries one broadcaster anyway.
    bool is_audio_payload(unsigned int pt)
    {
        return pt == PAYLOAD_OPUS || pt == 111;
    }

    void handle_media(const unsigned char* packet, int got)
    {
        unsigned int pt = packet[1] & 0x7F;
        unsigned int ssrc = ((unsigned int)packet[8] << 24) | ((unsigned int)packet[9] << 16) |
                            ((unsigned int)packet[10] << 8) | packet[11];

        if (!g_logged_first_media)
        {
            g_logged_first_media = true;
            int words = (packet[0] & 0x10) ? ((packet[12] << 8) | packet[13]) : 0;
            log_line("watch: первый медиапакет: pt %u, ssrc %u, %d байт, расширений %d слов",
                     pt, ssrc, got, words);
        }

        if (ssrc == (unsigned int)g_remote_audio_ssrc || is_audio_payload(pt))
        {
            handle_audio_packet(packet, got);
            return;
        }
        if (ssrc == (unsigned int)g_remote_rtx_ssrc) return;

        if (!g_remote_video_ssrc)
        {
            // Nothing announced yet. Anything that is plainly opus is not it.
            if (is_audio_payload(pt)) return;

            InterlockedExchange(&g_remote_video_ssrc, (long)ssrc);
            log_line("watch: видео пришло с ssrc %u (payload type %u)", ssrc, pt);
        }
        else if (ssrc != (unsigned int)g_remote_video_ssrc)
        {
            return;
        }

        unsigned short sequence = (unsigned short)(((unsigned int)packet[2] << 8) | packet[3]);

        unsigned char media[1600];
        bool padding_only = false;
        int len = decrypt_packet(packet, got, media, sizeof(media), &padding_only);

        if (padding_only)
        {
            // Nothing to reassemble, but the number it used still has to be
            // accounted for or the next packet looks like it follows a loss.
            note_sequence(sequence);
            rtpvid::rx_skip(&g_rx, sequence);
            return;
        }

        if (len <= 0)
        {
            InterlockedIncrement(&g_decrypt_failures);
            return;
        }
        InterlockedIncrement(&g_video_packets);

        bool marker = (packet[1] & 0x80) != 0;
        unsigned int timestamp = ((unsigned int)packet[4] << 24) | ((unsigned int)packet[5] << 16) |
                                 ((unsigned int)packet[6] << 8) | packet[7];

        note_sequence(sequence);

        const unsigned char* frame = 0;
        unsigned int frame_len = 0;
        unsigned int dropped_before = g_rx.dropped;
        bool complete = rtpvid::rx_push(&g_rx, media, len, marker, sequence, timestamp,
                                        &frame, &frame_len);
        if (g_rx.dropped != dropped_before) g_need_keyframe_ref = true;

        if (complete)
        {
            InterlockedIncrement(&g_assembled);
            handle_frame(frame, frame_len);
        }
    }

    void handle_rtcp(const unsigned char* packet, int got)
    {
        unsigned int pt = packet[1];
        if (pt != 200 || got < 8 + 20 + 16 + 4) return;

        // A sender report carries the wall clock the timestamps hang off, and
        // the middle 32 bits of it go back in our own reports.
        unsigned char body[64];
        int len = got - 8 - 16 - 4;
        if (len <= 0 || len > (int)sizeof(body)) return;

        const unsigned char* counter_bytes = packet + got - 4;
        const unsigned char* cipher = packet + 8;
        const unsigned char* tag = packet + 8 + len;

        bool ok = false;
        if (g_mode == MODE_XCHACHA20)
        {
            unsigned char nonce[24];
            ccfset(nonce, 0, sizeof(nonce));
            ccpy(nonce, counter_bytes, 4);
            ok = crypto::xchacha20poly1305_decrypt(g_secret_key, nonce, packet, 8,
                                                   cipher, (unsigned int)len, tag, body);
        }
        else if (g_mode == MODE_AES256_GCM)
        {
            unsigned char nonce[12];
            ccfset(nonce, 0, sizeof(nonce));
            ccpy(nonce, counter_bytes, 4);
            ok = crypto::aes256gcm_decrypt(g_secret_key, nonce, packet, 8,
                                           cipher, (unsigned int)len, tag, body);
        }
        if (!ok || len < 20) return;

        unsigned int ntp_seconds = ((unsigned int)body[0] << 24) | ((unsigned int)body[1] << 16) |
                                   ((unsigned int)body[2] << 8) | body[3];
        unsigned int ntp_fraction = ((unsigned int)body[4] << 24) | ((unsigned int)body[5] << 16) |
                                    ((unsigned int)body[6] << 8) | body[7];

        g_last_sr_middle = ((ntp_seconds & 0xFFFF) << 16) | (ntp_fraction >> 16);
        g_last_sr_at = GetTickCount64();
    }

    DWORD WINAPI pump_thread(LPVOID)
    {
        CoInitializeEx(0, COINIT_MULTITHREADED);

        unsigned long long last_report = 0;
        unsigned long long last_pli = 0;
        unsigned long long last_wants = GetTickCount64();
        unsigned long long last_stats = GetTickCount64();
        unsigned long long started = GetTickCount64();

        unsigned char packet[2048];

        while (g_running)
        {
            // The socket blocks for a quarter of a second at most, so this loop
            // needs no sleep of its own and adds no latency to a frame.
            int got = recv(g_udp, (char*)packet, sizeof(packet), 0);
            if (got > 8)
            {
                InterlockedIncrement(&g_packets);

                unsigned int pt = packet[1];
                if (pt >= 200 && pt <= 207) handle_rtcp(packet, got);
                else if ((packet[0] & 0xC0) == 0x80) handle_media(packet, got);
            }

            unsigned long long now = GetTickCount64();

            // Nothing at all coming back means the subscription did not take.
            // It is one small message, and asking again costs less than a
            // viewer sitting in front of a blank window because the first one
            // went out before the server had the source.
            if (!g_video_packets && now - last_wants >= 2000 && now - started < 20000)
            {
                last_wants = now;
                send_sink_wants();
            }

            if (now - last_report >= 1000)
            {
                last_report = now;
                send_receiver_report();
            }

            // A keyframe is asked for when something could not be decoded, and
            // once a second while nothing has been decoded at all: a viewer
            // that joined between two of the sender's own keyframes would
            // otherwise sit in front of a blank window for seconds.
            // While the picture cannot be rebuilt there is nothing to lose by
            // asking often; once it is running, once a second is plenty.
            bool nothing_yet = g_frames == 0 && (now - started) < 30000;
            unsigned long long pli_gap = (nothing_yet || g_need_keyframe_ref) ? 400 : 1000;

            if ((InterlockedExchange(&g_want_keyframe, 0) || nothing_yet) &&
                now - last_pli >= pli_gap)
            {
                last_pli = now;
                send_pli();
            }

            if (now - last_stats >= 5000)
            {
                last_stats = now;
                log_line("watch: пакетов %u (видео %u), собрано кадров %u, декодировано %u, "
                         "выброшено %u, сбоев расшифровки %u, e2ee %s, видео ssrc %u",
                         (unsigned int)g_packets, (unsigned int)g_video_packets,
                         (unsigned int)g_assembled, (unsigned int)g_frames, g_rx.dropped,
                         (unsigned int)g_decrypt_failures, g_e2ee_ready ? "да" : "нет",
                         (unsigned int)g_remote_video_ssrc);
            }
        }

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
        g_self_ssrc = (unsigned int)d->i64("ssrc", 0);

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
        else { set_status(WATCH_FAILED, "Сервер не предлагает поддерживаемое шифрование"); return; }

        log_line("watch: ready ssrc=%u %s:%u mode=%s", g_self_ssrc, g_udp_host, g_udp_port, chosen);

        if (!udp_connect()) { set_status(WATCH_FAILED, "UDP-соединение не установлено"); return; }

        char external_ip[80];
        unsigned short external_port = 0;
        if (!udp_discover(external_ip, sizeof(external_ip), &external_port))
        {
            set_status(WATCH_FAILED, "IP discovery не ответил");
            return;
        }
        log_line("watch: discovered %s:%u", external_ip, external_port);

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

        send_select_protocol(external_ip, external_port, chosen);
    }

    void handle_session_description(const jval* d)
    {
        const jval* key = d->arr("secret_key");
        if (key->count < 32)
        {
            set_status(WATCH_FAILED, "Сервер не прислал ключ");
            return;
        }

        // as_i64, not i64: these are bare numbers in an array, and the member
        // lookup i64 does would quietly hand back a key of nothing but zeroes.
        unsigned char seen = 0;
        for (int i = 0; i < 32; i++)
        {
            g_secret_key[i] = (unsigned char)key->at((unsigned int)i)->as_i64(0);
            seen |= g_secret_key[i];
        }
        if (!seen)
        {
            set_status(WATCH_FAILED, "Ключ сессии пустой");
            return;
        }

        log_line("watch: session description, mode %s", d->str("mode", ""));

        send_no_video();
        send_sink_wants();

        set_status(WATCH_WAITING, "Ждём картинку...");

        if (!g_pump_thread)
            g_pump_thread = CreateThread(0, 0, pump_thread, 0, 0, 0);
    }

    // The server describes each source on the connection with op 12. On a
    // stream there is only the broadcaster, and this is where their video
    // source is named.
    void handle_remote_video(const jval* d)
    {
        snowflake user = d->sf("user_id");
        unsigned int video = (unsigned int)d->i64("video_ssrc", 0);

        if (user && g_user_id && user != g_user_id) return;

        // Both of these are needed to tell the three sources apart: the media
        // is chosen by ssrc, so the other two have to be known to be skipped.
        unsigned int audio = (unsigned int)d->i64("audio_ssrc", 0);
        unsigned int rtx = (unsigned int)d->i64("rtx_ssrc", 0);
        if (audio) InterlockedExchange(&g_remote_audio_ssrc, (long)audio);
        if (rtx) InterlockedExchange(&g_remote_rtx_ssrc, (long)rtx);

        if (!video) return;

        if ((unsigned int)g_remote_video_ssrc != video)
        {
            InterlockedExchange(&g_remote_video_ssrc, (long)video);
            log_line("watch: сервер назвал видеоисточник %u", video);
            rtpvid::rx_reset(&g_rx);
            // The subscription names the source once it is known, and the new
            // source has to start from a keyframe.
            send_sink_wants();
            InterlockedExchange(&g_want_keyframe, 1);
        }
    }

    DWORD WINAPI ws_thread(LPVOID)
    {
        CoInitializeEx(0, COINIT_MULTITHREADED);

        char url[400];
        cnprint(url, sizeof(url), "wss://%s/?v=8", g_endpoint);
        log_line("watch: connecting to %s", url);

        g_ws.init();
        if (!g_ws.connect(url, 0))
        {
            set_status(WATCH_FAILED, "Голосовой сокет стрима не открылся");
            CoUninitialize();
            return 0;
        }

        set_status(WATCH_CONNECTING, "Подключаемся к стриму...");
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
            case VOP_VIDEO:
                handle_remote_video(d);
                break;
            case VOP_DAVE_PREPARE_TRANSITION:
            case VOP_DAVE_EXECUTE_TRANSITION:
            case VOP_DAVE_PREPARE_EPOCH:
                send_transition_response((unsigned int)d->i64("transition_id", 0), true);
                break;
            default:
                break;
            }

            doc.free_doc();
        }

        msg.free_buffer();
        log_line("watch: websocket loop ended (close %u)", g_ws.close_status);
        CoUninitialize();
        return 0;
    }

    // Tearing down waits on the threads above, which takes long enough that
    // doing it inside a gateway dispatch would hold up the heartbeat. The
    // dispatch handlers hand the job to a thread of its own instead.
    //
    // Whether to tell the gateway we are leaving is decided by the caller: when
    // the socket has just gone down there is nobody to tell, and a send racing
    // the gateway tearing its own websocket down is worse than saying nothing.
    volatile long g_notify_on_stop = 1;

    DWORD WINAPI stop_thread(LPVOID)
    {
        streamview::stop();
        return 0;
    }

    void stop_later(bool notify)
    {
        InterlockedExchange(&g_notify_on_stop, notify ? 1 : 0);

        HANDLE h = CreateThread(0, 0, stop_thread, 0, 0, 0);
        if (h) CloseHandle(h);
        else streamview::stop();
    }
}

// ---------------------------------------------------------------------------

void streamview::init()
{
    ccfset(g_status, 0, sizeof(g_status));
    if (!g_locks_ready)
    {
        InitializeCriticalSection(&g_frame_lock);
        InitializeCriticalSection(&g_send_lock);
        g_locks_ready = true;
    }
    rtpvid::rx_init(&g_rx);
    vdec::init();
}

void streamview::shutdown()
{
    stop();
    vdec::shutdown();
    rtpvid::rx_free(&g_rx);
    free_frames();

    if (g_locks_ready)
    {
        g_locks_ready = false;
        DeleteCriticalSection(&g_frame_lock);
        DeleteCriticalSection(&g_send_lock);
    }
}

bool streamview::watch(snowflake guild_id, snowflake channel_id, snowflake user_id)
{
    // Go Live is a person's feature. The opcodes that set one up - 18 to
    // start, 20 to watch - do not exist for a bot, and discord answers one
    // that sends them by closing the gateway with 4001, unknown opcode. The
    // whole client falls over for the sake of a button that was never going
    // to work, so it is refused here with a reason instead.
    if (api::is_bot())
    {
        log_line("stream: бот - просмотр демонстрации недоступен, op 20 уронил бы гейтвей");
        return false;
    }

    if (g_running) stop();
    if (!channel_id || !user_id) return false;
    if (user_id == store::self_id()) return false;

    // The stream connection identifies with the voice session, and the server
    // only hands one out to somebody already sitting in the channel. Trying
    // from outside gets as far as the websocket and is then closed without a
    // word, which looks like a bug rather than a rule.
    if (voice::state() != VOICE_CONNECTED || voice::current_channel() != channel_id)
    {
        set_status(WATCH_FAILED, "Сначала зайди в этот голосовой канал");
        return false;
    }

    g_guild_id = guild_id;
    g_channel_id = channel_id;
    g_user_id = user_id;

    // Stereo at 48 kHz, which is what the render side wants and what every
    // share sends. Opus upmixes a mono stream to it for free.
    if (!g_audio_decoder)
    {
        int err = 0;
        g_audio_decoder = opus_decoder_create(AUDIO_SAMPLE_RATE, AUDIO_CHANNELS, &err);
        if (err != OPUS_OK || !g_audio_decoder)
        {
            g_audio_decoder = 0;
            log_line("watch: декодер звука не создался (%d)", err);
        }
    }
    g_audio_have_seq = false;
    g_audio_packets = 0;

    g_packets = 0;
    g_video_packets = 0;
    g_assembled = 0;
    g_frames = 0;
    g_decrypt_failures = 0;
    g_logged_first_media = false;
    g_last_dave_error = "";
    g_last_dave_detail = "";
    g_dave_reports = 0;
    g_need_keyframe_ref = true;
    g_skipped_until_idr = 0;
    g_waiting_since = 0;
    g_op_external = 0;
    g_op_proposals = 0;
    g_op_commit = 0;
    g_op_welcome = 0;
    g_commits_applied = 0;
    g_commit_error = "";
    g_nonce_counter = 0;
    g_self_ssrc = 0;
    g_remote_video_ssrc = 0;
    g_remote_audio_ssrc = 0;
    g_remote_rtx_ssrc = 0;
    g_rx_layout = 0;
    g_mode = MODE_NONE;
    g_endpoint[0] = 0;
    g_token[0] = 0;
    g_rtc_server_id[0] = 0;
    g_last_w = 0;
    g_last_h = 0;

    g_group_ready = false;
    g_key_package_ready = false;
    g_known_count = 0;
    InterlockedExchange(&g_e2ee_ready, 0);

    g_have_base = false;
    g_seq_cycles = 0;
    g_received = 0;
    g_last_expected = 0;
    g_last_received = 0;
    g_last_sr_middle = 0;
    g_last_sr_at = 0;

    rtpvid::rx_reset(&g_rx);
    g_rx.dropped = 0;

    build_stream_key();
    make_connection_id(g_connection_id, sizeof(g_connection_id));
    log_line("watch: stream key %s", g_stream_key);

    InterlockedExchange(&g_running, 1);
    set_status(WATCH_REQUESTING, "Просим у discord стрим...");

    send_gateway_watch();
    return true;
}

void streamview::stop()
{
    if (!g_running && g_state == WATCH_IDLE) return;

    // Both the UI and a gateway dispatch can decide to stop at the same moment,
    // and two of these running at once would close the same handles twice.
    static volatile long stopping = 0;
    if (InterlockedExchange(&stopping, 1)) return;

    InterlockedExchange(&g_running, 0);

    if (InterlockedExchange(&g_notify_on_stop, 1)) send_gateway_unwatch();
    g_ws.close();

    if (g_pump_thread) { WaitForSingleObject(g_pump_thread, 4000); CloseHandle(g_pump_thread); g_pump_thread = 0; }
    if (g_beat_thread) { WaitForSingleObject(g_beat_thread, 2000); CloseHandle(g_beat_thread); g_beat_thread = 0; }
    if (g_ws_thread) { WaitForSingleObject(g_ws_thread, 4000); CloseHandle(g_ws_thread); g_ws_thread = 0; }

    g_ws.destroy();

    if (g_udp != INVALID_SOCKET) { closesocket(g_udp); g_udp = INVALID_SOCKET; }

    vdec::stop();
    rtpvid::rx_reset(&g_rx);

    if (g_audio_decoder) { opus_decoder_destroy(g_audio_decoder); g_audio_decoder = 0; }
    g_audio_have_seq = false;
    audio::clear_stream();

    if (g_locks_ready)
    {
        EnterCriticalSection(&g_frame_lock);
        g_ready_full = false;
        LeaveCriticalSection(&g_frame_lock);
    }

    g_stream_key[0] = 0;
    g_user_id = 0;
    InterlockedExchange(&g_heartbeat_ms, 0);
    set_status(WATCH_IDLE, "");

    InterlockedExchange(&stopping, 0);
}

streamview_state streamview::state() { return (streamview_state)g_state; }
const char* streamview::status_text() { return g_status; }
snowflake streamview::watching_user() { return g_user_id; }

void streamview::set_muted(bool muted)
{
    InterlockedExchange(&g_audio_muted, muted ? 1 : 0);
    if (muted) audio::clear_stream();
}

bool streamview::muted() { return g_audio_muted != 0; }
unsigned int streamview::audio_packets() { return (unsigned int)g_audio_packets; }
const char* streamview::stream_key() { return g_stream_key; }
int streamview::width() { return (int)g_last_w; }
int streamview::height() { return (int)g_last_h; }
unsigned int streamview::frames_decoded() { return (unsigned int)g_frames; }
unsigned int streamview::packets_in() { return (unsigned int)g_packets; }

void streamview::request_keyframe() { InterlockedExchange(&g_want_keyframe, 1); }

void streamview::read_stats(stats* out)
{
    if (!out) return;

    out->packets = (unsigned int)g_packets;
    out->video_packets = (unsigned int)g_video_packets;
    out->assembled = (unsigned int)g_assembled;
    out->frames = (unsigned int)g_frames;
    out->dropped = g_rx.dropped;
    out->decrypt_fail = (unsigned int)g_decrypt_failures;
    out->video_ssrc = (unsigned int)g_remote_video_ssrc;
    out->e2ee = g_e2ee_ready != 0;
    out->dave_error = g_last_dave_error ? g_last_dave_error : "";
    out->epoch = g_group_ready ? (unsigned int)g_group.epoch : 0;
    out->external_sender_ops = (unsigned int)g_op_external;
    out->proposal_ops = (unsigned int)g_op_proposals;
    out->commit_ops = (unsigned int)g_op_commit;
    out->welcome_ops = (unsigned int)g_op_welcome;
    out->commits_applied = (unsigned int)g_commits_applied;
    out->commit_error = g_commit_error ? g_commit_error : "";
    out->dave_detail = g_last_dave_detail ? g_last_dave_detail : "";
    out->decoded_w = (unsigned int)g_last_w;
    out->decoded_h = (unsigned int)g_last_h;
    out->luma = vdec::last_luma();
    out->stride = vdec::stride();
    out->decoder_in = vdec::frames_in();
    out->decoder_error = vdec::last_error();
    out->skipped = (unsigned int)g_skipped_until_idr;
    out->waiting_for_idr = g_need_keyframe_ref;
}

bool streamview::take_frame(const unsigned char** rgba, int* width, int* height)
{
    if (!g_locks_ready) return false;

    EnterCriticalSection(&g_frame_lock);
    bool got = g_ready_full;
    if (got)
    {
        vframe swap = g_display;
        g_display = g_ready;
        g_ready = swap;
        g_ready_full = false;
    }
    LeaveCriticalSection(&g_frame_lock);

    if (!got || !g_display.rgba) return false;

    *rgba = g_display.rgba;
    *width = g_display.w;
    *height = g_display.h;
    return true;
}

void streamview::on_stream_create(const jval* d)
{
    if (!g_running) return;

    const char* key = d->str("stream_key", 0);
    if (!key || !g_stream_key[0] || ccscmp(key, g_stream_key) != 0) return;

    const char* rtc = d->str("rtc_server_id", 0);
    if (rtc) ccstrncpy(g_rtc_server_id, rtc, sizeof(g_rtc_server_id) - 1);

    log_line("watch: STREAM_CREATE accepted, rtc_server_id %s", g_rtc_server_id);
}

void streamview::on_stream_server_update(const jval* d)
{
    if (!g_running) return;

    const char* key = d->str("stream_key", 0);
    if (key && g_stream_key[0] && ccscmp(key, g_stream_key) != 0) return;

    const char* endpoint = d->str("endpoint", 0);
    const char* token = d->str("token", 0);
    if (!endpoint || !token) return;

    ccstrncpy(g_endpoint, endpoint, sizeof(g_endpoint) - 1);
    ccstrncpy(g_token, token, sizeof(g_token) - 1);

    log_line("watch: STREAM_SERVER_UPDATE endpoint %s", g_endpoint);

    if (!g_ws_thread) g_ws_thread = CreateThread(0, 0, ws_thread, 0, 0, 0);
}

void streamview::on_stream_delete(const jval* d)
{
    if (!g_running) return;

    const char* key = d->str("stream_key", 0);
    if (key && g_stream_key[0] && ccscmp(key, g_stream_key) != 0) return;

    log_line("watch: поток закрыт с той стороны");
    // The stream is already gone, so there is nothing to unsubscribe from.
    stop_later(false);
}

void streamview::on_gateway_disconnected()
{
    if (g_running) stop_later(false);
}
