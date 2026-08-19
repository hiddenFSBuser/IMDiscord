#include "pch.h"
#include "voice.h"
#include "store.h"
#include "gateway.h"
#include "audio/audio.h"
#include "audio/noise.h"
#include "audio/vad.h"
#include "audio/sounds.h"
#include "audio/music.h"
#include "science.h"
#include "rest.h"           // only for the token a call's analytics is signed with
#include "core/storage.h"
#include "net/proxy.h"
#include "core/log.h"
#include "core/wavdump.h"
#include "video/decoder.h"
#include "video/rtp_video.h"
#include "core/crypto.h"
#include "net/websocket.h"
#include "net/json.h"
#include "opus.h"
#include "dave/mls_types.h"
#include "dave/mls_message.h"
#include "dave/mls_group.h"
#include "dave/dave_frames.h"
#include "dave/tls_codec.h"
#include "system/io/ufile.h"
#include <timeapi.h>

#pragma comment(lib, "winmm.lib")

// Voice gateway v8 + RTP over UDP.
//
// Discord offers several AEADs per session; the two that matter are
// aead_aes256_gcm_rtpsize and aead_xchacha20_poly1305_rtpsize. Both are
// "rtpsize" variants: the RTP header (including CSRCs and the extension
// header, but not its body) is authenticated in the clear, and a 4 byte
// incrementing nonce is appended after the auth tag.

enum
{
    VOP_IDENTIFY = 0,
    VOP_SELECT_PROTOCOL = 1,
    VOP_READY = 2,
    VOP_HEARTBEAT = 3,
    VOP_SESSION_DESCRIPTION = 4,
    VOP_SPEAKING = 5,
    VOP_HEARTBEAT_ACK = 6,
    VOP_RESUME = 7,
    VOP_HELLO = 8,
    VOP_RESUMED = 9,

    // Who has a camera on, and which source it is coming from. Sent for every
    // participant whose video state changes, including a change to off - which
    // arrives as the same opcode with video_ssrc zero.
    VOP_VIDEO = 12,
    VOP_CLIENT_DISCONNECT = 13,

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

enum encryption_mode
{
    MODE_NONE = 0,
    MODE_AES256_GCM,
    MODE_XCHACHA20,
};

namespace
{
    const int MAX_PACKET = 1500;

    // Per speaker, decoded within a packet or two of arrival and drained by
    // the sound card's clock in the render callback - the model abaddon's
    // AudioManager runs on (a deque per ssrc, mixed inside the device
    // callback), carried here by a ring per speaker. One second of headroom:
    // network clumping lands in it harmlessly, and the only way it fills up
    // is a sender permanently ahead of us.
    //
    // What is deliberately gone: the jitter queue, the FEC rebuilds, the
    // limiter and the spike repairs - the paths of the prebuilt opus lib that
    // the full-scale spikes came from. Two small repairs stay, because the
    // network really does need them: a short reorder window (packets that
    // overtake each other are not losses) and one concealed frame per gap
    // after a bounded wait (a real loss, papered over by the in-tree opus,
    // whose concealment path is sound now that it is built from source).
    const int SPEAKER_RING_SAMPLES = AUDIO_SAMPLE_RATE * AUDIO_CHANNELS;   // 1 s

    struct speaker
    {
        unsigned int ssrc;
        snowflake user_id;
        OpusDecoder* decoder;

        // Decoded 48 kHz stereo, waiting for the device to ask for it.
        // Allocated on the first packet that arrives, freed with the speaker.
        short* pcm;
        volatile long read_pos;
        volatile long write_pos;

        unsigned short last_seq;    // the most recent packet decoded
        bool have_seq;

        // Reorder window. Packets wait here, keyed by sequence, until the ones
        // ahead of them are decoded or written off: udp hands a packet that
        // overtook its neighbour to us out of order, and decoding straight on
        // arrival drops the overtaken one as "late" - every such drop is a
        // 20 ms hole spliced into the waveform, which is the click this window
        // exists to remove. Eight packets is 160 ms of patience, far past any
        // reordering a healthy route produces.
        struct rx_pending
        {
            bool used;
            unsigned short seq;
            int len;
            unsigned char data[1280];   // an opus packet cannot exceed 1275
        };
        rx_pending pending[8];
        unsigned short next_seq;   // the sequence the decoder expects next
        bool have_next;            // next_seq is meaningful
        int gap_wait;              // ticks spent waiting on a missing head

        // The last few payloads handed to the decoder. When a frame rails,
        // the run-up to it is the evidence: dumped so the failure can be
        // replayed offline against a reference decoder.
        unsigned char hist[16][1280];
        unsigned short hist_seq[16];
        int hist_len[16];
        int hist_pos;
        int hist_count;

        // What this person was set to on our side, and where the smoothing has
        // got to. Two figures rather than one because a slider dragged mid
        // sentence must not put a step in the waveform: the applied volume
        // walks towards the wanted one instead of jumping to it.
        float volume;
        bool muted;
        float volume_now;

        unsigned long long last_audio_tick;
        float level;

        // Jitter cushion state: set once the ring has run dry mid-buffer,
        // cleared when enough has queued up again. While set the mixer holds
        // this speaker silent - a short pause once beats a hole in every
        // other device buffer, which is what jitter turns into without it.
        bool dry;

        // Only opened when IMD_AUDIODUMP is set.
        wavdump::sink dump;
    };

    // No wide printf in this crt, and the name is two pieces anyway.
    void speaker_dump_name(unsigned int ssrc, wchar_t* out, int cap)
    {
        char narrow[64];
        cnprint(narrow, sizeof(narrow), "dump_speaker_%u.wav", ssrc);

        int i = 0;
        while (narrow[i] && i < cap - 1) { out[i] = (wchar_t)(unsigned char)narrow[i]; i++; }
        out[i] = 0;
    }

    // 16-bit sequence numbers wrap. Everything asks "which came first", and
    // the answer has to survive 65535 -> 0.
    int seq_ahead(unsigned short a, unsigned short b)
    {
        return (int)(short)(unsigned short)(a - b);
    }

    // ---- connection state ----
    websocket g_ws;
    SOCKET g_udp = INVALID_SOCKET;

    HANDLE g_ws_thread = 0;
    HANDLE g_tick_thread = 0;
    HANDLE g_beat_thread = 0;
    HANDLE g_stop_event = 0;

    volatile long g_running = 0;
    volatile long g_state = VOICE_IDLE;
    volatile long g_heartbeat_ms = 0;
    volatile long g_session_ready = 0;

    // The next connection is a resume of the session that just died, not a
    // new one. Read by the hello handler, which is the only place the two
    // differ: everything after that is identical.
    volatile long g_resuming = 0;

    char g_status[192];
    char g_endpoint[256];
    char g_voice_token[256];
    char g_voice_session[128];

    snowflake g_guild_id = 0;
    snowflake g_channel_id = 0;
    snowflake g_pending_guild = 0;
    snowflake g_pending_channel = 0;

    // Whether to offer end to end encryption at all when identifying.
    //
    // With it on, every arrival and departure moves the whole channel to a new
    // MLS epoch, and this client has to keep step through a commit exchange it
    // gets exactly one chance at. When that goes wrong the call falls silent in
    // both directions and rejoining does not always bring it back - which is
    // the failure that has been chased for several rounds now.
    //
    // With it off the channel runs on transport encryption alone. That is not
    // a hack: it is what discord does by itself the moment anyone without DAVE
    // walks in, and it is what every client did before DAVE existed.

    bool g_muted = false;
    bool g_deafened = false;
    bool g_have_server = false;
    bool g_have_state = false;
    bool g_dave_active = false;

    // Whose connection this is. Read once, when it is set up, rather than
    // from the store on every frame: switching accounts changes what the
    // store answers, and the end to end keys are derived from this id. A
    // stream that changed identity halfway would simply stop decrypting for
    // everybody listening.
    snowflake g_self_id = 0;

    // The route out belongs to the account that placed the call, captured
    // when it starts. Switching accounts afterwards must not drag the call
    // onto somebody else's proxy - or off a proxy altogether.
    proxy_config g_proxy;
    proxy::udp_route g_udp_route;

    unsigned int g_ssrc = 0;
    unsigned short g_udp_port = 0;
    char g_udp_host[128];
    unsigned char g_secret_key[32];
    encryption_mode g_mode = MODE_NONE;

    unsigned short g_sequence = 0;
    unsigned int g_timestamp = 0;
    unsigned int g_nonce_counter = 0;
    bool g_speaking_sent = false;

    // Whether the encoder is currently set up for music rather than speech.
    bool g_music_encoding = false;

    // Frames of encoded silence to send after the last word before the
    // speaking flag is lowered.
    const int SILENCE_TAIL = 5;
    int g_silence_left = 0;

    OpusEncoder* g_encoder = 0;
    ulist<speaker> g_speakers;
    CRITICAL_SECTION g_speakers_lock;
    bool g_locks_ready = false;

    // Why the connection was last torn down, and by whom. Written at every
    // call site because a dropped call has too many possible authors - this
    // client, the far end, or discord - and guessing between them from the
    // outside has cost several rounds already.
    const char* g_stop_reason = "";
    unsigned short g_last_close = 0;

    unsigned long long g_tick = 0;

    // When the call last had no media keys. Zero while it has them.
    unsigned long long g_silent_since = 0;
    volatile long g_last_seq = 0;

    // Every mechanism that used to put a step or a hole in the output is gone
    // with the push pipeline; what remains worth counting is how the network
    // is treating us. Read rather than imagined, in the log every five
    // seconds.
    struct rx_stats
    {
        unsigned int played;      // opus frames decoded on arrival
        unsigned int late;        // packets older than what was already decoded
        unsigned int overflow;    // ring full: oldest queued audio had to go
        unsigned int railed;      // frames the decoder returned saturated end to end
        unsigned int concealed;   // lost frames papered over by opus concealment
        unsigned int nokey;       // protected frames dropped: no keys or no sender
        unsigned int unwrap;      // protected frames the ratchet refused
    };
    rx_stats g_stats;

    void reset_stats(rx_stats* s) { ccfset(s, 0, sizeof(*s)); }

    // The last completed five second window, kept so the settings pane can
    // show it. Reading a log file means restarting the client to get at it,
    // and restarting is exactly what clears the evidence.
    rx_stats g_stats_last;
    bool g_stats_have = false;

    void set_status(voice_state_kind s, const char* text)
    {
        InterlockedExchange(&g_state, (long)s);
        ccfset(g_status, 0, sizeof(g_status));
        if (text) ccstrncpy(g_status, text, sizeof(g_status) - 1);
    }

    // ---- per person volume ---------------------------------------------
    //
    // Kept in memory and mirrored into the settings file. In memory because
    // the mixer reads it fifty times a second on the tick thread and the
    // settings table is grown by the ui thread; in the file because a volume
    // set once is expected to still be there tomorrow.
    struct user_audio
    {
        snowflake user_id;
        float volume;
        bool muted;
    };

    ulist<user_audio> g_user_audio;
    const char* USER_AUDIO_PREFIX = "uvoice_";

    void user_audio_key(snowflake user_id, char* out, int cap)
    {
        cnprint(out, cap, "%s%llu", USER_AUDIO_PREFIX, user_id);
    }

    // "percent,muted" - one key rather than two, so a person who was never
    // touched costs nothing at all.
    void parse_user_audio(const char* text, float* volume, bool* muted)
    {
        *volume = 1.0f;
        *muted = false;
        if (!text || !text[0]) return;

        int percent = 0;
        const char* p = text;
        while (*p >= '0' && *p <= '9') { percent = percent * 10 + (*p - '0'); p++; }
        if (*p == ',') *muted = (p[1] == '1');

        if (percent > 0) *volume = (float)percent / 100.0f;
    }

    user_audio* find_user_audio(snowflake user_id)
    {
        for (unsigned int i = 0; i < g_user_audio.count; i++)
            if (g_user_audio[i].user_id == user_id) return &g_user_audio[i];
        return 0;
    }

    void load_user_audio()
    {
        g_user_audio = ulist<user_audio>();

        unsigned int prefix = (unsigned int)ccslenf(USER_AUDIO_PREFIX);
        int total = storage::settings_count();

        for (int i = 0; i < total; i++)
        {
            const char* key = storage::settings_key_at(i);
            if (ccsncmp(key, USER_AUDIO_PREFIX, prefix) != 0) continue;

            user_audio entry;
            entry.user_id = ccstrtoull(key + prefix, 0, 10);
            if (!entry.user_id) continue;

            parse_user_audio(storage::settings_value_at(i), &entry.volume, &entry.muted);
            g_user_audio.push(entry);
        }

        log_line("voice: загружено персональных громкостей: %u", g_user_audio.count);
    }

    // Writes through to both, and to the live speaker if that person happens
    // to be talking right now. The caller holds g_speakers_lock.
    void store_user_audio(snowflake user_id, float volume, bool muted)
    {
        if (!user_id) return;

        user_audio* entry = find_user_audio(user_id);
        if (!entry)
        {
            user_audio fresh;
            fresh.user_id = user_id;
            fresh.volume = 1.0f;
            fresh.muted = false;
            g_user_audio.push(fresh);
            entry = &g_user_audio[g_user_audio.count - 1];
        }

        entry->volume = volume;
        entry->muted = muted;

        for (unsigned int i = 0; i < g_speakers.count; i++)
        {
            if (g_speakers[i].user_id != user_id) continue;
            g_speakers[i].volume = volume;
            g_speakers[i].muted = muted;
        }

        char key[64];
        char value[32];
        user_audio_key(user_id, key, sizeof(key));
        cnprint(value, sizeof(value), "%d,%d", (int)(volume * 100.0f + 0.5f), muted ? 1 : 0);

        storage::settings_set(key, value);
        storage::settings_save();
    }

    // ---- speakers ------------------------------------------------------

    speaker* find_speaker(unsigned int ssrc)
    {
        for (unsigned int i = 0; i < g_speakers.count; i++)
            if (g_speakers[i].ssrc == ssrc) return &g_speakers[i];
        return 0;
    }

    speaker* ensure_speaker(unsigned int ssrc, snowflake user_id)
    {
        speaker* s = find_speaker(ssrc);
        if (s)
        {
            // The ssrc usually turns up before the name attached to it does,
            // so whatever this person was set to can only be applied here.
            if (user_id && s->user_id != user_id)
            {
                s->user_id = user_id;

                user_audio* saved = find_user_audio(user_id);
                s->volume = saved ? saved->volume : 1.0f;
                s->muted = saved ? saved->muted : false;
                s->volume_now = s->muted ? 0.0f : s->volume;
            }
            return s;
        }

        speaker fresh;
        ccfset(&fresh, 0, sizeof(fresh));
        fresh.ssrc = ssrc;
        fresh.user_id = user_id;

        user_audio* saved = user_id ? find_user_audio(user_id) : 0;
        fresh.volume = saved ? saved->volume : 1.0f;
        fresh.muted = saved ? saved->muted : false;
        fresh.volume_now = fresh.muted ? 0.0f : fresh.volume;

        int err = 0;
        fresh.decoder = opus_decoder_create(AUDIO_SAMPLE_RATE, AUDIO_CHANNELS, &err);
        if (err != OPUS_OK || !fresh.decoder)
        {
            log_line("voice: opus decoder for ssrc %u failed (%d)", ssrc, err);
            return 0;
        }

        g_speakers.push(fresh);
        return &g_speakers[g_speakers.count - 1];
    }

    void clear_speakers()
    {
        reset_stats(&g_stats);
        audio::reset_render_overruns();

        EnterCriticalSection(&g_speakers_lock);
        for (unsigned int i = 0; i < g_speakers.count; i++)
        {
            if (g_speakers[i].decoder) opus_decoder_destroy(g_speakers[i].decoder);
            wavdump::finish(&g_speakers[i].dump);
            if (g_speakers[i].pcm) memfree(g_speakers[i].pcm);
        }
        g_speakers.clear_fast();
        LeaveCriticalSection(&g_speakers_lock);
    }

    // ---- websocket -----------------------------------------------------

    bool send_json(jwriter* w)
    {
        return g_ws.send_text(w->buf.data, w->buf.size);
    }

    void send_identify()
    {
        jwriter w;
        w.init();
        w.begin_obj();
        w.kv_i64("op", VOP_IDENTIFY);
        w.key("d");
        w.begin_obj();
        w.kv_snowflake("server_id", g_guild_id ? g_guild_id : g_channel_id);
        w.kv_snowflake("user_id", g_self_id);
        w.kv_str("session_id", g_voice_session);
        w.kv_str("token", g_voice_token);
        w.kv_bool("video", false);
        // The DAVE level has to be declared or the server answers close code
        // 4017 and drops the connection - and declaring zero is refused the
        // same way. I read the documented "0 means no end to end encryption"
        // as something a client could simply announce, wired it to a setting,
        // and every first join since has been thrown out with 4017; the retry
        // that offered one was what made it look intermittent.
        //
        // What the field means here is the highest version this client can
        // speak, not what it would like to use. That is one, always. Whether
        // the channel actually runs encrypted is the server's answer in the
        // session description, not our request.
        w.kv_i64("max_dave_protocol_version", 1);
        w.end_obj();
        w.end_obj();

        send_json(&w);
        w.free_writer();
    }

    // Coming back to a session that is still ours.
    //
    // Everything expensive about a voice connection lives behind the session
    // rather than behind the socket: the udp flow, the negotiated keys, the
    // dave group and its epoch. A resume keeps all of it, so the audio picks
    // up where it stopped instead of the channel being rejoined from nothing
    // - which is what a dropped socket used to cost.
    //
    // v8 wants the sequence number of the last payload we took, the same one
    // the heartbeat carries, so the server can replay what was missed.
    void send_resume()
    {
        jwriter w;
        w.init();
        w.begin_obj();
        w.kv_i64("op", VOP_RESUME);
        w.key("d");
        w.begin_obj();
        w.kv_snowflake("server_id", g_guild_id ? g_guild_id : g_channel_id);
        w.kv_str("session_id", g_voice_session);
        w.kv_str("token", g_voice_token);
        w.kv_i64("seq_ack", g_last_seq);
        w.end_obj();
        w.end_obj();

        send_json(&w);
        w.free_writer();
    }

    // Discord's own numbering, taken from the stream viewer where it is already
    // proven on the wire. Getting these wrong is silent: the server relays a
    // codec nothing here can read.
    const int PAYLOAD_OPUS = 120;
    const int PAYLOAD_H264 = 105;
    const int PAYLOAD_H264_RTX = 106;

    // encode says what this client can send, decode what it can be sent. Audio
    // goes both ways; video only comes in, because there is no camera capture
    // on this side. Saying otherwise for opus would silence the microphone.
    void add_codec(jwriter* w, const char* name, const char* type,
                   int payload, int rtx, int priority, bool encode)
    {
        w->begin_obj();
        w->kv_str("name", name);
        w->kv_str("type", type);
        w->kv_i64("priority", priority);
        w->kv_i64("payload_type", payload);
        w->kv_bool("encode", encode);
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

        // Without this list the server has no reason to relay anybody's camera
        // here: the table is what maps an incoming payload type back to a
        // codec, so declaring it is what turns video on for the receiving side.
        //
        // Only what there is a decoder for. Claiming the other four invites the
        // server to send a camera in one of them, and then a picture arrives
        // that nothing here can turn into pixels - which looks exactly like a
        // camera that does not work.
        w.key("codecs");
        w.begin_arr();
        add_codec(&w, "opus", "audio", PAYLOAD_OPUS, -1, 1000, true);
        add_codec(&w, "H264", "video", PAYLOAD_H264, PAYLOAD_H264_RTX, 1000, false);
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

    // ---- DAVE / MLS ----------------------------------------------------

    unsigned char g_sig_private[96];
    unsigned char g_sig_public[65];
    bool g_sig_ready = false;

    mls::key_package g_key_package;
    mls::key_package_private g_key_package_private;
    bool g_key_package_ready = false;

    mls::group_state g_group;

    // Proposals kept from op 27, so the commit that references them can be
    // applied when it arrives.
    mls::cached_proposal g_known[mls::MAX_MEMBERS];
    unsigned int g_known_count = 0;
    // The locally created group exists as soon as the external sender arrives,
    // but it only carries the real epoch keys once a welcome has been accepted.
    // Media must not flow before that: plain opus on a DAVE channel is a
    // protocol violation.
    bool g_group_ready = false;
    bool g_media_ready = false;

    // The protocol version the channel is running, and the one a transition is
    // about to move it to. Version zero is a downgrade: the channel gives up on
    // end to end encryption entirely, usually because somebody joined who
    // cannot do it. Ignoring that, which is what happened here, meant carrying
    // on wrapping every frame for a channel that had stopped expecting it -
    // nobody could read us, and the call was dead from the moment they walked
    // in.
    int g_dave_version = 0;
    int g_dave_version_next = 0;
    bool g_dave_downgraded = false;

    // Answered transitions, so a repeat of one already done is not mistaken
    // for a transition this client missed.
    unsigned int g_last_transition_done = 0xFFFFFFFF;

    // When the session was last rebuilt, so a rebuild cannot provoke itself.
    unsigned long long g_last_reinit_tick = 0;
    unsigned int g_dave_nonce = 0;

    // A commit that has been handed to us but must not take effect yet.
    //
    // When somebody joins or leaves, the group moves to a new epoch, and every
    // member has to move at the same moment. The server announces the commit
    // first and only later says go. Applying it the instant it arrives - which
    // is what happened here - puts this client on the new epoch alone: it
    // starts sending with keys nobody else has yet, and it has already thrown
    // away the keys everybody else is still using. Both directions go silent,
    // which is exactly what a third person joining looked like.
    ubuffer g_pending_commit;
    unsigned int g_pending_transition = 0;
    bool g_have_pending_commit = false;

    // And the other half of the same problem: building a commit advances the
    // group that built it, there and then. So the moment somebody joined,
    // this client jumped an epoch ahead of everybody else - encrypting with
    // keys nobody had yet and unable to read anything still being sent with
    // the old ones. Both directions went silent, which is exactly what a
    // third person joining looked like.
    //
    // The commit is built on a copy instead. The live group only moves when
    // the server says the whole channel is moving.
    mls::group_state g_group_next;
    ubuffer g_own_commit;
    bool g_own_commit_ready = false;
    bool g_own_commit_won = false;

    // Outgoing binary frames carry only the opcode; the sequence number is
    // added by the server side of the protocol, not by us.
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
                log_line("dave: could not create the MLS signature key");
                return;
            }
            g_sig_ready = true;
        }

        // Key packages are single use, so a fresh one is built every time the
        // server asks.
        if (!mls::create_key_package(g_self_id, g_sig_private,
                                     &g_key_package, &g_key_package_private))
        {
            log_line("dave: key package creation failed");
            return;
        }
        g_key_package_ready = true;

        tls_writer w;
        w.init(1024);
        g_key_package.write(&w);

        log_line("dave: sending key package (%u bytes, self verify %s)",
                 w.size(), g_key_package.verify() ? "ok" : "FAILED");

        send_binary(VOP_MLS_KEY_PACKAGE, w.data(), w.size());
        w.free_writer();

        // The group id is the voice channel id as eight big-endian bytes.
        unsigned char group_id[8];
        for (int i = 0; i < 8; i++)
            group_id[i] = (unsigned char)(g_channel_id >> (56 - i * 8));

        g_group_ready = mls::create_group(&g_group, group_id, 8, &g_key_package.leaf,
                                          g_sig_private, g_key_package_private.encryption_private);

        log_line("dave: local group %s", g_group_ready ? "created" : "FAILED");
    }

    void send_invalid_commit(unsigned int transition_id);

    // Throws the group away and introduces this client to it again from
    // nothing. The server answers a fresh key package by re-adding us, which
    // is the only way back once this side and the channel have lost each
    // other - and losing each other is survivable, staying lost is not.
    void dave_reinit(const char* why)
    {
        log_line("dave: сессия пересобирается заново (%s)", why);

        g_last_reinit_tick = g_tick;
        g_have_pending_commit = false;
        g_own_commit_ready = false;
        g_own_commit_won = false;
        g_known_count = 0;
        g_group_ready = false;
        g_media_ready = false;
        dave::reset_ratchets();
        g_dave_nonce = 0;

        send_key_package();
    }

    // Moves the group on to the epoch the held commit describes. Called when
    // the server says the whole channel is switching, not when the commit
    // turns up.
    void execute_pending_transition(unsigned int transition_id)
    {
        bool ours = g_own_commit_won ||
                    (g_own_commit_ready && !g_have_pending_commit &&
                     transition_id == g_pending_transition);

        if (ours && transition_id == g_pending_transition)
        {
            // Our own commit was the one taken. The state it leads to was
            // built when it was written, so moving to it is a swap.
            g_group = g_group_next;

            g_own_commit_ready = false;
            g_own_commit_won = false;
            g_have_pending_commit = false;
            g_known_count = 0;
            g_last_transition_done = transition_id;

            dave::reset_ratchets();
            g_dave_nonce = 0;

            log_line("dave: переход %u выполнен своим коммитом", transition_id);
            return;
        }

        if (!g_have_pending_commit || transition_id != g_pending_transition)
        {
            // The channel is moving to an epoch this client has nothing for.
            // Sitting still leaves it holding keys the group has abandoned,
            // with no way back on its own - which is why a call stayed broken
            // once someone joined instead of recovering a moment later.
            //
            // Narrowly though. Transition zero carries no epoch move, a
            // downgraded channel has no group left to rejoin, and a rebuild
            // provokes the very traffic that can ask for another one - so this
            // gets one attempt every few seconds and no more. Rebuilding on
            // everything that reached here killed the call outright.
            bool worth_it = transition_id != 0 && g_dave_active &&
                            !g_dave_downgraded && g_key_package_ready;

            if (worth_it && g_tick - g_last_reinit_tick > 250)
            {
                // Answer first, rebuild second: op 31 is the protocol's way
                // to say "I cannot run this transition", and the server's
                // answer to it is a fresh welcome. A bare key package asks
                // for nothing - the server still counts us as a member in
                // good standing and quietly ignores it, which is why the
                // rebuild alone never recovered the call.
                send_invalid_commit(transition_id);
                dave_reinit(tr("переход без коммита"));
            }
            return;
        }

        g_have_pending_commit = false;

        const char* why = "";
        bool applied = mls::process_commit(&g_group, g_pending_commit.data,
                                           g_pending_commit.size,
                                           g_known, g_known_count, &why);

        log_line("dave: переход %u %s%s%s", transition_id,
                 applied ? "выполнен" : "ПРОВАЛЕН",
                 applied ? "" : ": ", applied ? "" : why);

        if (applied)
        {
            g_known_count = 0;
            g_last_transition_done = transition_id;
            dave::reset_ratchets();
            g_dave_nonce = 0;
            return;
        }

        // Saying ready and then failing leaves this client on an epoch the
        // group has left, with no way back on its own. Telling the server the
        // commit was no good is the designed way out: it re-adds us with a
        // fresh welcome.
        //
        // And it needs a key package to be re-added with. The one sent at the
        // start of the call was used up getting us in, so without a new one
        // the server has nothing to work with and the call stays silent - the
        // waiting that no amount of rejoining fixed. So introduce ourselves
        // again, now, rather than fifteen seconds later when the watchdog
        // notices.
        g_media_ready = false;
        send_invalid_commit(transition_id);
        dave_reinit(tr("коммит не применился"));
    }

    // The whole of a transition: the protocol version first, then the epoch.
    // A version change to zero is the channel giving up on end to end
    // encryption, and once that happens frames have to go out bare - a wrapped
    // one is unreadable to a channel that is no longer unwrapping.
    void execute_transition(unsigned int transition_id)
    {
        if (g_dave_version_next != g_dave_version)
        {
            int was = g_dave_version;
            g_dave_version = g_dave_version_next;

            if (g_dave_version == 0)
            {
                g_dave_active = false;
                g_dave_downgraded = true;
                g_media_ready = true;      // plain opus needs no group

                g_have_pending_commit = false;
                g_own_commit_ready = false;
                g_own_commit_won = false;

                log_line("dave: канал понижен с v%d до v0 - шифрование выключено", was);
                set_status(VOICE_CONNECTED, tr("В голосовом канале (без E2EE)"));
                return;
            }

            // Back up again, or on for the first time. Either way this client
            // has no standing in the group at the new version.
            g_dave_active = true;
            g_dave_downgraded = false;
            log_line("dave: канал переходит с v%d на v%d", was, g_dave_version);
            dave_reinit(tr("смена версии протокола"));
            return;
        }

        if (transition_id == g_last_transition_done)
        {
            // Already done. The server repeats these.
            return;
        }

        execute_pending_transition(transition_id);
    }

    // Tells the server whether the epoch transition can go ahead. Both replies
    // are plain JSON, not binary frames.
    void send_transition_response(unsigned int transition_id, bool ready);

    void send_invalid_commit(unsigned int transition_id)
    {
        send_transition_response(transition_id, false);
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
            log_line("dave: proposals could not be parsed");
            return;
        }

        log_line("dave: %u proposal(s), revoke=%d", count, is_revoke ? 1 : 0);

        // Kept so the commit that follows, which names them by reference rather
        // than repeating them, can be applied when it arrives as op 29.
        if (is_revoke) g_known_count = 0;
        for (unsigned int i = 0; i < count && !is_revoke && g_known_count < mls::MAX_MEMBERS; i++)
        {
            messages[i].compute_ref(g_known[g_known_count].ref);
            g_known[g_known_count].prop = messages[i].prop;
            g_known_count++;
        }

        for (unsigned int i = 0; i < count; i++)
        {
            const mls::proposal* p = &messages[i].prop;
            if (p->type == mls::PROPOSAL_ADD)
            {
                log_line("dave:   add user %llu (leaf sig %s, kp sig %s)",
                         p->add.leaf.cred.user_id(),
                         p->add.leaf.verify(0, 0, 0) ? "ok" : "bad",
                         p->add.verify() ? "ok" : "bad");
            }
            else if (p->type == mls::PROPOSAL_REMOVE)
            {
                log_line("dave:   remove leaf %u", p->remove_index);
            }
        }

        if (is_revoke || !g_group_ready) return;

        ubuffer commit, welcome;
        commit.init();
        welcome.init();

        // A copy, so the live group is untouched whether or not this commit
        // is the one the server picks.
        g_group_next = g_group;
        g_own_commit_ready = false;
        g_own_commit_won = false;

        if (mls::build_commit(&g_group_next, messages, count, &commit, &welcome))
        {
            g_own_commit.clear();
            g_own_commit.append(commit.data, commit.size);
            g_own_commit_ready = true;

            // op 28 carries the commit and the welcome back to back.
            ubuffer payload_out;
            payload_out.init(commit.size + welcome.size + 16);
            payload_out.append(commit.data, commit.size);
            payload_out.append(welcome.data, welcome.size);

            log_line("dave: sending commit+welcome (%u bytes)", payload_out.size);
            send_binary(VOP_MLS_COMMIT_WELCOME, payload_out.data, payload_out.size);
            payload_out.free_buffer();
        }
        else
        {
            log_line("dave: commit generation failed");
        }

        commit.free_buffer();
        welcome.free_buffer();
    }

    void handle_binary_payload(const unsigned char* data, unsigned int len)
    {
        if (len < 3)
        {
            log_line("dave: runt binary frame (%u bytes)", len);
            return;
        }

        unsigned int seq = ((unsigned int)data[0] << 8) | data[1];
        unsigned char opcode = data[2];
        const unsigned char* payload = data + 3;
        unsigned int payload_len = len - 3;

        InterlockedExchange(&g_last_seq, (long)seq);

        // DAVE only negotiates when there is somebody to encrypt to, so the
        // occupant count is the difference between "broken" and "nothing to do".
        unsigned int occupants = 0;
        {
            store::guard g;
            const ulist<dvoice_state>& states = store::voice_states();
            for (unsigned int i = 0; i < states.count; i++)
                if (states[i].channel_id == g_channel_id) occupants++;
        }

        log_line("dave: binary op %u, seq %u, %u bytes (%u in channel)",
                 opcode, seq, payload_len, occupants);

#ifdef IMD_VOICE_TEST
        // Test-only: keep the raw payloads so the MLS parsers can be developed
        // against what the server actually sends.
        {
            char narrow[64];
            cnprint(narrow, sizeof(narrow), "dave_op%u_seq%u.bin",
                    (unsigned int)opcode, seq);

            wchar_t name[64];
            chartowcs(narrow, name, 64);

            wchar_t path[MAX_PATH];
            if (ufile::app_path(name, path, MAX_PATH))
                ufile::write_all(path, payload, payload_len);
        }
#endif

        switch (opcode)
        {
        case VOP_MLS_EXTERNAL_SENDER:
            // The group's external sender (discord's own signing identity).
            // Answering with our key package is what gets us added.
            send_key_package();
            break;

        case VOP_MLS_PROPOSALS:
            handle_proposals(payload, payload_len);
            break;

        case VOP_MLS_WELCOME:
        {
            if (payload_len < 3 || !g_key_package_ready)
            {
                log_line("dave: welcome arrived before we had a key package");
                break;
            }

            // [uint16 transition_id][Welcome]
            unsigned int transition_id = ((unsigned int)payload[0] << 8) | payload[1];

            g_group_ready = mls::process_welcome(&g_group, payload + 2, payload_len - 2,
                                                 &g_key_package, &g_key_package_private,
                                                 g_sig_private);
            g_media_ready = g_group_ready;

            // A welcome that does not open is the same dead end as a commit
            // that does not apply, and the key package it was addressed to is
            // spent either way.
            if (!g_group_ready)
            {
                log_line("dave: welcome не открылся, представляюсь заново");
                send_transition_response(transition_id, false);
                dave_reinit(tr("welcome не открылся"));
                break;
            }

            // A new epoch means new media keys for everyone.
            dave::reset_ratchets();
            g_dave_nonce = 0;
            g_known_count = 0;

            // The welcome already puts us on the epoch its transition points
            // at, so when the server follows up with "execute transition N"
            // for the very same N, there is nothing left to execute. Without
            // this mark that follow-up looked like a transition we had no
            // commit for, the session was torn down and rebuilt, and the
            // server - which had just been told "ready" - never sent a second
            // welcome: both directions of the call went silent on rejoin.
            g_last_transition_done = transition_id;

            log_line("dave: welcome %s (transition %u)",
                     g_group_ready ? "accepted" : "REJECTED", transition_id);

            if (g_media_ready) set_status(VOICE_CONNECTED, tr("В голосовом канале (E2EE)"));
            send_transition_response(transition_id, g_group_ready);
            break;
        }

        case VOP_MLS_ANNOUNCE_COMMIT_TRANSITION:
        {
            // [uint16 transition_id][MLSMessage commit]. Somebody else's commit
            // won the race, and every member except its author has to apply it.
            // Merely noting it, which is what happened here before, left this
            // client deriving media keys from an epoch the group had left: its
            // own frames stopped being readable and nobody else's would open.
            if (payload_len < 3 || !g_group_ready) break;

            unsigned int transition_id = ((unsigned int)payload[0] << 8) | payload[1];

            // Transition zero means there is nobody to stay in step with, so
            // it takes effect at once. Anything else has to wait for the
            // server to tell the whole channel to switch together.
            if (transition_id == 0)
            {
                const char* why = "";
                bool applied = mls::process_commit(&g_group, payload + 2, payload_len - 2,
                                                   g_known, g_known_count, &why);

                log_line("dave: коммит %s немедленно%s%s",
                         applied ? "применён" : "НЕ применён",
                         applied ? "" : ": ", applied ? "" : why);

                if (applied)
                {
                    g_known_count = 0;
                    dave::reset_ratchets();
                    g_dave_nonce = 0;
                }

                send_transition_response(0, applied);
                break;
            }

            unsigned int commit_len = payload_len - 2;
            const unsigned char* commit_bytes = payload + 2;

            // The server broadcasts the winning commit to everybody, author
            // included. Ours cannot be applied like somebody else's - a
            // commit does not encrypt its path to the leaf that wrote it - so
            // recognising it matters: the state it produces is already built.
            bool mine = g_own_commit_ready &&
                        g_own_commit.size == commit_len &&
                        ccmp(g_own_commit.data, commit_bytes, commit_len) == 0;

            if (mine)
            {
                g_own_commit_won = true;
                g_have_pending_commit = false;
            }
            else
            {
                g_pending_commit.clear();
                g_pending_commit.append(commit_bytes, commit_len);
                g_have_pending_commit = true;
                g_own_commit_won = false;
            }

            g_pending_transition = transition_id;

            log_line("dave: %s коммит ждёт перехода %u (%u байт)",
                     mine ? "свой" : "чужой", transition_id, commit_len);

            send_transition_response(transition_id, true);
            break;
        }

        default:
            break;
        }
    }

    void send_speaking(bool speaking)
    {
        jwriter w;
        w.init();
        w.begin_obj();
        w.kv_i64("op", VOP_SPEAKING);
        w.key("d");
        w.begin_obj();
        w.kv_i64("speaking", speaking ? 1 : 0);
        w.kv_i64("delay", 0);
        w.kv_i64("ssrc", (long long)g_ssrc);
        w.end_obj();
        w.end_obj();

        send_json(&w);
        w.free_writer();
    }

    void send_heartbeat_now()
    {
        if (!g_ws.is_open()) return;

        jwriter w;
        w.init();
        w.begin_obj();
        w.kv_i64("op", VOP_HEARTBEAT);
        w.key("d");
        w.begin_obj();
        w.kv_i64("t", (long long)GetTickCount64());
        // v8 wants the sequence number of the last payload we processed.
        w.kv_i64("seq_ack", g_last_seq);
        w.end_obj();
        w.end_obj();
        send_json(&w);
        w.free_writer();
    }

    DWORD WINAPI voice_heartbeat_thread(LPVOID)
    {
        while (g_running)
        {
            long interval = g_heartbeat_ms;
            if (interval <= 0)
            {
                if (WaitForSingleObject(g_stop_event, 200) == WAIT_OBJECT_0) break;
                continue;
            }
            if (WaitForSingleObject(g_stop_event, (DWORD)interval) == WAIT_OBJECT_0) break;
            send_heartbeat_now();
        }
        return 0;
    }

    // ---- udp -----------------------------------------------------------

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

        sockaddr_in peer;
        ccpy(&peer, result->ai_addr, sizeof(sockaddr_in));
        freeaddrinfo(result);

        // With no proxy this is a plain connect. With socks5 it opens a udp
        // association first and points the socket at the relay instead, so
        // everything below carries on unchanged.
        const char* why = "";
        if (!proxy::open_udp(&g_udp_route, g_udp, &peer, &g_proxy, &why))
        {
            log_line("voice: udp до %s:%u не поднялся (%s)",
                     g_udp_host, g_udp_port, why[0] ? why : "нет связи");
            g_stop_reason = why[0] ? why : "UDP не поднялся";
            proxy::close_udp(&g_udp_route);
            closesocket(g_udp);
            g_udp = INVALID_SOCKET;
            return false;
        }

        // Generous while waiting for the discovery reply; the media loop
        // switches the socket to non-blocking right after.
        DWORD timeout = 250;
        setsockopt(g_udp, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
        return true;
    }

    // Discovery packet: type 0x0001, length 70, ssrc, then 64 bytes of address
    // and 2 bytes of port that the server fills in on the way back.
    bool udp_discover(char* out_ip, int ip_cap, unsigned short* out_port)
    {
        unsigned char packet[74];
        ccfset(packet, 0, sizeof(packet));
        packet[0] = 0x00; packet[1] = 0x01;
        packet[2] = 0x00; packet[3] = 0x46;
        packet[4] = (unsigned char)(g_ssrc >> 24);
        packet[5] = (unsigned char)(g_ssrc >> 16);
        packet[6] = (unsigned char)(g_ssrc >> 8);
        packet[7] = (unsigned char)(g_ssrc);

        for (int attempt = 0; attempt < 10; attempt++)
        {
            if (proxy::udp_send(&g_udp_route, packet, (int)sizeof(packet)) == SOCKET_ERROR)
            {
                log_line("voice: discovery send failed (%d)", WSAGetLastError());
                return false;
            }

            unsigned char reply[128];
            int got = proxy::udp_recv(&g_udp_route, reply, (int)sizeof(reply));
            if (got < 74) continue;
            if (reply[0] != 0x00 || reply[1] != 0x02) continue;

            int i = 0;
            while (i < 64 && reply[8 + i] && i < ip_cap - 1)
            {
                out_ip[i] = (char)reply[8 + i];
                i++;
            }
            out_ip[i] = 0;
            *out_port = (unsigned short)((reply[72] << 8) | reply[73]);
            return true;
        }
        return false;
    }

    // ---- rtp -----------------------------------------------------------

    int send_audio(const unsigned char* opus_data, int opus_len)
    {
        if (g_udp == INVALID_SOCKET || g_mode == MODE_NONE) return 0;

        unsigned char packet[MAX_PACKET];
        packet[0] = 0x80;
        packet[1] = 0x78;
        packet[2] = (unsigned char)(g_sequence >> 8);
        packet[3] = (unsigned char)(g_sequence);
        packet[4] = (unsigned char)(g_timestamp >> 24);
        packet[5] = (unsigned char)(g_timestamp >> 16);
        packet[6] = (unsigned char)(g_timestamp >> 8);
        packet[7] = (unsigned char)(g_timestamp);
        packet[8] = (unsigned char)(g_ssrc >> 24);
        packet[9] = (unsigned char)(g_ssrc >> 16);
        packet[10] = (unsigned char)(g_ssrc >> 8);
        packet[11] = (unsigned char)(g_ssrc);

        unsigned int counter = ++g_nonce_counter;
        unsigned char counter_bytes[4] = {
            (unsigned char)(counter >> 24), (unsigned char)(counter >> 16),
            (unsigned char)(counter >> 8), (unsigned char)(counter)
        };

        unsigned char* cipher = packet + 12;
        unsigned char tag[16];
        bool ok = false;

        if (g_mode == MODE_XCHACHA20)
        {
            unsigned char nonce[24];
            ccfset(nonce, 0, sizeof(nonce));
            ccpy(nonce, counter_bytes, 4);
            crypto::xchacha20poly1305_encrypt(g_secret_key, nonce, packet, 12,
                                              opus_data, (unsigned int)opus_len, cipher, tag);
            ok = true;
        }
        else
        {
            unsigned char nonce[12];
            ccfset(nonce, 0, sizeof(nonce));
            ccpy(nonce, counter_bytes, 4);
            ok = crypto::aes256gcm_encrypt(g_secret_key, nonce, packet, 12,
                                           opus_data, (unsigned int)opus_len, cipher, tag);
        }
        if (!ok) return 0;

        int len = 12 + opus_len;
        ccpy(packet + len, tag, 16);
        len += 16;
        ccpy(packet + len, counter_bytes, 4);
        len += 4;

        g_sequence++;
        g_timestamp += AUDIO_FRAME_SAMPLES;

        return proxy::udp_send(&g_udp_route, packet, len);
    }

    // Returns the length of the decrypted opus payload, or 0.
    int decrypt_packet(const unsigned char* packet, int len, unsigned char* out, int out_cap,
                       unsigned int* out_ssrc, unsigned short* out_seq)
    {
        if (len < 12 + 16 + 4) return 0;
        if ((packet[0] & 0xC0) != 0x80) return 0;
        // RTCP shares the port, so the payload type is what separates media
        // from control. The top bit of byte 1 is the marker flag and has to be
        // masked off first - on video that bit is meaningful, it closes a
        // frame, so reading it as part of the type would reject every last
        // packet of every picture.
        unsigned int pt = packet[1] & 0x7F;
        if (pt != PAYLOAD_OPUS && pt != PAYLOAD_H264 && pt != PAYLOAD_H264_RTX) return 0;

        *out_ssrc = ((unsigned int)packet[8] << 24) | ((unsigned int)packet[9] << 16) |
                    ((unsigned int)packet[10] << 8) | packet[11];
        *out_seq = (unsigned short)(((unsigned int)packet[2] << 8) | packet[3]);

        int csrc_count = packet[0] & 0x0F;
        bool has_extension = (packet[0] & 0x10) != 0;

        int header_len = 12 + csrc_count * 4;
        if (has_extension) header_len += 4;    // profile + length words, body is encrypted
        if (header_len + 16 + 4 > len) return 0;

        const unsigned char* counter_bytes = packet + len - 4;
        int cipher_len = len - header_len - 16 - 4;
        if (cipher_len <= 0 || cipher_len > out_cap) return 0;

        const unsigned char* cipher = packet + header_len;
        const unsigned char* tag = packet + header_len + cipher_len;

        bool ok = false;
        if (g_mode == MODE_XCHACHA20)
        {
            unsigned char nonce[24];
            ccfset(nonce, 0, sizeof(nonce));
            ccpy(nonce, counter_bytes, 4);
            ok = crypto::xchacha20poly1305_decrypt(g_secret_key, nonce, packet, (unsigned int)header_len,
                                                   cipher, (unsigned int)cipher_len, tag, out);
        }
        else if (g_mode == MODE_AES256_GCM)
        {
            unsigned char nonce[12];
            ccfset(nonce, 0, sizeof(nonce));
            ccpy(nonce, counter_bytes, 4);
            ok = crypto::aes256gcm_decrypt(g_secret_key, nonce, packet, (unsigned int)header_len,
                                           cipher, (unsigned int)cipher_len, tag, out);
        }
        if (!ok) return 0;

        int payload_len = cipher_len;
        int offset = 0;

        if (has_extension)
        {
            // The extension body sits at the front of the decrypted payload.
            int words = (packet[header_len - 2] << 8) | packet[header_len - 1];
            offset = words * 4;
            if (offset >= payload_len) return 0;
        }

        if (offset)
        {
            ccmov(out, out + offset, (size_t)(payload_len - offset));
            payload_len -= offset;
        }
        return payload_len;
    }

    // ---- tick ----------------------------------------------------------

    // One-shot diagnostics so a broken media path is obvious in the log
    // without drowning it at 50 packets a second.
    bool g_logged_first_rx = false;
    bool g_logged_first_tx = false;
    bool g_logged_decrypt_fail = false;
    bool g_logged_no_capture = false;
    bool g_logged_dave_frame = false;

    // ---- cameras ---------------------------------------------------------
    //
    // A camera is not a second connection the way Go Live is: it rides the
    // voice socket already open, on its own ssrc, and op 12 says whose it is.
    //
    // Only one is decoded at a time. The Media Foundation decoder in vdec is a
    // single instance shared with the stream viewer, so showing four people at
    // once would need it broken into instances first - a wider change than
    // this, and one worth doing separately rather than half.
    struct camera
    {
        snowflake user_id;
        unsigned int video_ssrc;
        unsigned int rtx_ssrc;
        rtp_h264_rx rx;
        bool rx_ready;
        unsigned long long last_packet_tick;
    };

    ulist<camera> g_cameras;
    snowflake g_watched_camera = 0;
    volatile long g_camera_frames = 0;
    volatile long g_camera_packets = 0;
    bool g_camera_decoding = false;

    camera* find_camera(snowflake user_id)
    {
        for (unsigned int i = 0; i < g_cameras.count; i++)
            if (g_cameras[i].user_id == user_id) return &g_cameras[i];
        return 0;
    }

    camera* find_camera_by_ssrc(unsigned int ssrc)
    {
        for (unsigned int i = 0; i < g_cameras.count; i++)
            if (g_cameras[i].video_ssrc && g_cameras[i].video_ssrc == ssrc)
                return &g_cameras[i];
        return 0;
    }

    void drop_camera(snowflake user_id)
    {
        for (unsigned int i = 0; i < g_cameras.count; i++)
        {
            if (g_cameras[i].user_id != user_id) continue;

            if (g_cameras[i].rx_ready) rtpvid::rx_free(&g_cameras[i].rx);
            g_cameras.delete_at((int)i);
            break;
        }

        if (g_watched_camera == user_id)
        {
            g_watched_camera = 0;
            if (g_camera_decoding) { vdec::stop(); g_camera_decoding = false; }
        }
    }

    // One packet of somebody's camera. Reassembled per person, but only the
    // one being watched is decoded: the rest are tracked so the badge stays
    // honest without paying for pictures nobody is looking at.
    void feed_camera(unsigned int ssrc, const unsigned char* media, int len,
                     bool marker, unsigned short seq, unsigned int timestamp)
    {
        camera* cam = find_camera_by_ssrc(ssrc);
        if (!cam) return;

        cam->last_packet_tick = g_tick;
        InterlockedIncrement(&g_camera_packets);

        if (cam->user_id != g_watched_camera) return;

        if (!cam->rx_ready)
        {
            rtpvid::rx_init(&cam->rx);
            cam->rx_ready = true;
        }

        const unsigned char* frame = 0;
        unsigned int frame_len = 0;
        if (!rtpvid::rx_push(&cam->rx, media, len, marker, seq, timestamp,
                             &frame, &frame_len))
            return;

        if (!g_camera_decoding)
        {
            if (!vdec::start()) return;
            g_camera_decoding = true;
        }

        // The video clock runs at 90 kHz, and the decoder wants microseconds.
        vdec::submit(frame, (int)frame_len,
                     (unsigned long long)timestamp * 1000ull / 90ull);
        InterlockedIncrement(&g_camera_frames);
    }

    void clear_cameras()
    {
        for (unsigned int i = 0; i < g_cameras.count; i++)
            if (g_cameras[i].rx_ready) rtpvid::rx_free(&g_cameras[i].rx);

        g_cameras = ulist<camera>();
        g_watched_camera = 0;
        if (g_camera_decoding) { vdec::stop(); g_camera_decoding = false; }
    }

    // ---- per-speaker pcm rings ------------------------------------------

    int speaker_avail(const speaker* s)
    {
        long w = s->write_pos, r = s->read_pos;
        long diff = w - r;
        if (diff < 0) diff += SPEAKER_RING_SAMPLES;
        return (int)diff;
    }

    void speaker_push(speaker* s, const short* src, int samples)
    {
        // A full second queued means the sender is permanently ahead of us and
        // the oldest audio has to go. The drop is rounded up to whole stereo
        // frames: sliding the read side by single samples, which this used to
        // do, desyncs left from right from then on and turns everything that
        // follows into crackle.
        int free_samples = SPEAKER_RING_SAMPLES - speaker_avail(s) - 1;
        if (free_samples < samples)
        {
            const int FRAME = AUDIO_FRAME_SAMPLES * AUDIO_CHANNELS;
            long need = samples - free_samples;
            long drop = ((need + FRAME - 1) / FRAME) * FRAME;
            long avail = speaker_avail(s);
            if (drop > avail) drop = avail & ~1L;
            s->read_pos = (s->read_pos + drop) % SPEAKER_RING_SAMPLES;
            g_stats.overflow++;
        }

        for (int i = 0; i < samples; i++)
        {
            s->pcm[s->write_pos] = src[i];
            s->write_pos++;
            if (s->write_pos >= SPEAKER_RING_SAMPLES) s->write_pos = 0;
        }
    }

    long frame_peak(const short* pcm, int samples)
    {
        long peak = 0;
        for (int i = 0; i < samples; i++)
        {
            long a = pcm[i] < 0 ? -pcm[i] : pcm[i];
            if (a > peak) peak = a;
        }
        return peak;
    }

    // Detector, not repair: a frame pinned at the rail for two dozen samples
    // in a row is not something a microphone produced, it is the decoder
    // returning garbage. The burst form matters just as much: an oscillating
    // decoder never holds the rail long enough to trip a 24-in-a-row rule,
    // so a frame that merely *visits* the rail sixteen times is garbage too.
    // Counted so the panel can say whether the opus build is still doing it;
    // the frame itself is played as-is.
    bool frame_railed(const short* pcm, int samples)
    {
        int run = 0, visits = 0;
        for (int i = 0; i < samples; i++)
        {
            long a = pcm[i] < 0 ? -pcm[i] : pcm[i];
            if (a >= 32700)
            {
                if (++run >= 24) return true;
                visits++;
            }
            else
            {
                run = 0;
            }
        }
        return visits >= 16;
    }

    // Unlike the wav taps this needs no switch: the history exists only to be
    // written on a blown frame, a blown frame is rare, and ten files of a few
    // kilobytes are all it can ever produce. The wav taps stay gated on
    // IMD_AUDIODUMP because they record everything.
    void pkt_history_add(speaker* s, unsigned short seq, const unsigned char* data, int len)
    {
        int i = s->hist_pos;
        s->hist_seq[i] = seq;
        s->hist_len[i] = len;
        ccpy(s->hist[i], data, len);
        s->hist_pos = (i + 1) & 15;
        s->hist_count++;
    }

    void pkt_history_dump(speaker* s, unsigned short at_seq)
    {
        wchar_t name[96];
        {
            char narrow[80];
            cnprint(narrow, sizeof(narrow), "pkt_blow_%u_%u.bin", s->ssrc, (unsigned int)at_seq);
            int i = 0;
            while (narrow[i] && i < 95) { name[i] = (wchar_t)(unsigned char)narrow[i]; i++; }
            name[i] = 0;
        }

        wchar_t path[MAX_PATH];
        if (!ufile::app_path(name, path, MAX_PATH)) return;

        HANDLE f = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, 0,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
        if (f == INVALID_HANDLE_VALUE) return;

        int n = s->hist_count < 16 ? s->hist_count : 16;
        int start = (s->hist_pos - n) & 15;
        for (int k = 0; k < n; k++)
        {
            int i = (start + k) & 15;
            unsigned char hdr[4] = {
                (unsigned char)(s->hist_seq[i] >> 8), (unsigned char)(s->hist_seq[i]),
                (unsigned char)(s->hist_len[i] >> 8), (unsigned char)(s->hist_len[i])
            };
            DWORD done = 0;
            WriteFile(f, hdr, 4, &done, 0);
            WriteFile(f, s->hist[i], (DWORD)s->hist_len[i], &done, 0);
        }
        CloseHandle(f);

        char utf8[160];
        wcstochar(path, utf8, sizeof(utf8));
        log_line("voice rx: контекст срыва записан в %s", utf8);
    }

    // The two halves of abaddon's AudioManager (reference/abaddon-audio/
    // manager.cpp), carried onto this client's WASAPI engine, with one
    // addition the bare model needs on a real network: a short reorder
    // window per speaker, so a packet that overtook its neighbour is decoded
    // in order instead of dropped, and a single concealed frame in place of
    // a packet that never showed up.
    //
    // decode_into_ring runs one opus payload (or one concealment, when data
    // is null) and lands the pcm on the speaker's ring.
    void decode_into_ring(speaker* s, const unsigned char* data, int len, unsigned short seq)
    {
        if (!s->pcm)
        {
            s->pcm = (short*)memalloc(SPEAKER_RING_SAMPLES * (int)sizeof(short));
            if (!s->pcm) return;
        }

        short pcm[120 * 48 * AUDIO_CHANNELS];   // opus frames run to 120 ms
        int frames;
        if (data)
        {
            frames = opus_decode(s->decoder, data, len, pcm, 120 * 48, 0);
        }
        else
        {
            // A lost frame: opus extrapolates from its own history. This is
            // the path the prebuilt lib used to rail on; the in-tree 1.4
            // build's concealment is sound, and the railed counter below is
            // there to say so if that ever stops being true.
            frames = opus_decode(s->decoder, 0, 0, pcm, AUDIO_FRAME_SAMPLES, 0);
        }
        if (frames <= 0) return;

        // A frame the decoder saturated end to end is not a voice, whatever
        // the network says: ciphertext that slipped past the marker check or
        // a bit-rotted payload both decode to a full-scale buzz. Playing it
        // as-is is the spike the listener hears, so it is swapped for a
        // concealment frame. The decoder state is already poisoned by the
        // garbage, but extrapolation at least decays smoothly instead of
        // holding the rail. The tail beyond what concealment produced is
        // zeroed, so the frame still occupies its full time slice.
        bool railed = frame_railed(pcm, frames * AUDIO_CHANNELS);
        if (railed && data)
        {
            int plc = opus_decode(s->decoder, 0, 0, pcm, frames, 0);
            if (plc < 0) plc = 0;
            for (int i = plc * AUDIO_CHANNELS; i < frames * AUDIO_CHANNELS; i++)
                pcm[i] = 0;
        }

        // The meter reads what arrived, before the listener's own volume:
        // somebody turned down or muted still lights up when they talk. A
        // railed frame's peak is garbage, so it lights nothing.
        long peak = frame_peak(pcm, frames * AUDIO_CHANNELS);
        if (data && !railed)
        {
            s->level = (float)peak / 32768.0f;
            if (peak > 300) s->last_audio_tick = g_tick;
        }
        if (railed)
        {
            g_stats.railed++;
            // Each blown frame is logged (throttled) with its sequence number
            // and size, so a dump can be matched against exactly what the
            // network delivered; with IMD_AUDIODUMP on, the payloads that led
            // up to it are saved alongside.
            static int g_railed_logged = 0;
            if (g_railed_logged < 30)
            {
                g_railed_logged++;
                log_line("voice rx: срыв кадра seq=%u len=%d%s",
                         (unsigned int)seq, len, data ? "" : " (маскировка)");
                if (g_railed_logged <= 10) pkt_history_dump(s, seq);
            }
        }

        // Straight off the decoder, before this code has touched it. Whatever
        // is wrong with the sound either shows up here or it does not, and
        // that alone halves the search.
        if (wavdump::enabled())
        {
            if (!s->dump.name[0])
            {
                wchar_t name[64];
                speaker_dump_name(s->ssrc, name, 64);
                wavdump::start(&s->dump, name);
            }
            wavdump::write(&s->dump, pcm, frames * AUDIO_CHANNELS);
        }

        speaker_push(s, pcm, frames * AUDIO_CHANNELS);
        if (data) g_stats.played++;
        else g_stats.concealed++;
    }

    // Decodes everything left in the window, earliest first, and empties it.
    // Used when the sequence jumps past the window: a sender coming back from
    // a gated silence starts wherever it starts, and concealing the gap would
    // invent sound for a pause nobody spoke in.
    void window_flush(speaker* s)
    {
        for (;;)
        {
            int best = -1;
            for (int i = 0; i < 8; i++)
            {
                if (!s->pending[i].used) continue;
                if (best < 0 || seq_ahead(s->pending[i].seq, s->pending[best].seq) < 0)
                    best = i;
            }
            if (best < 0) break;

            pkt_history_add(s, s->pending[best].seq, s->pending[best].data, s->pending[best].len);
            decode_into_ring(s, s->pending[best].data, s->pending[best].len, s->pending[best].seq);
            s->pending[best].used = false;
        }
        s->gap_wait = 0;
    }

    // Decodes every queued packet the horizon has reached. On the tick pass a
    // missing head gets sixty milliseconds of grace - three ticks - before it
    // is written off as a loss and concealed, so a hole is only ever paid for
    // a packet that is genuinely not coming.
    void window_drain(speaker* s, bool tick_pass)
    {
        if (!s->have_next) return;

        for (;;)
        {
            int slot = -1;
            for (int i = 0; i < 8; i++)
            {
                if (s->pending[i].used && s->pending[i].seq == s->next_seq) { slot = i; break; }
            }

            if (slot >= 0)
            {
                pkt_history_add(s, s->pending[slot].seq, s->pending[slot].data, s->pending[slot].len);
                decode_into_ring(s, s->pending[slot].data, s->pending[slot].len, s->pending[slot].seq);
                s->pending[slot].used = false;
                s->have_seq = true;
                s->last_seq = s->next_seq;
                s->next_seq++;
                s->gap_wait = 0;
                continue;
            }

            bool any = false;
            for (int i = 0; i < 8; i++)
                if (s->pending[i].used) { any = true; break; }
            if (!any) break;

            if (!tick_pass) break;              // arrival pass: spend no patience
            if (++s->gap_wait <= 3) break;      // 60 ms for the head to arrive

            decode_into_ring(s, 0, 0, s->next_seq); // gone for good: conceal one frame
            s->next_seq++;
            s->gap_wait = 0;
        }
    }

    void feed_opus(speaker* s, unsigned short seq, const unsigned char* data, int len)
    {
        if (len <= 0 || len > (int)sizeof(s->pending[0].data)) { g_stats.late++; return; }

        if (!s->have_next)
        {
            s->next_seq = seq;
            s->have_next = true;
        }

        int off = seq_ahead(seq, s->next_seq);
        if (off < 0)
        {
            // Older than the decode horizon - a duplicate, or a packet that
            // already spent its sixty milliseconds of grace somewhere upstream.
            g_stats.late++;
            return;
        }

        if (off >= 8)
        {
            // Beyond the window: a burst start after the sender's gate was
            // shut, or a hole too big to conceal. Play what is queued, then
            // reset the decoder's prediction state before the new burst -
            // a frame decoded against the leftovers of a phrase long gone
            // can throw the silk predictor into a limit cycle, which is the
            // rail-to-rail howl the dumps showed. Then restart here.
            window_flush(s);
            opus_decoder_ctl(s->decoder, OPUS_RESET_STATE);
            s->next_seq = seq;
            off = 0;
        }

        // A repeat of a packet already waiting is not a second helping.
        for (int i = 0; i < 8; i++)
            if (s->pending[i].used && s->pending[i].seq == seq) return;

        int slot = -1;
        for (int i = 0; i < 8; i++)
            if (!s->pending[i].used) { slot = i; break; }
        if (slot < 0) { g_stats.late++; return; }   // unreachable with off < 8

        s->pending[slot].used = true;
        s->pending[slot].seq = seq;
        s->pending[slot].len = len;
        ccpy(s->pending[slot].data, data, len);

        // The common case is in-order arrival, and it must not pay for the
        // window: what can be decoded right now is decoded right now.
        window_drain(s, false);
    }

    // mix_voice_into is data_callback: the sound card asks, each speaker
    // hands over whatever it has queued, and one with nothing is simply
    // silent. Runs on the render thread at the device's pace; `out` starts
    // zeroed, so a dry speaker leaves silence behind.
    void mix_voice_into(short* out, int samples)
    {
        if (!g_locks_ready || g_deafened) return;

        EnterCriticalSection(&g_speakers_lock);
        for (unsigned int i = 0; i < g_speakers.count; i++)
        {
            speaker* s = &g_speakers[i];
            if (!s->decoder || !s->pcm) continue;

            int avail = speaker_avail(s);
            if (avail <= 0) { s->dry = true; continue; }

            // Hold a refilling speaker until three frames are queued: playing
            // straight off an empty ring puts a hole in every buffer that
            // beats the next packet by a millisecond, and a pause this short
            // is inaudible where the holes were not.
            const int CUSHION = AUDIO_FRAME_SAMPLES * AUDIO_CHANNELS * 3;   // 60 ms
            if (s->dry && avail < CUSHION) continue;

            // Resuming at full amplitude mid-waveform is the same click as
            // cutting to silence was, so both edges of a dry spell are slid
            // over a couple of milliseconds.
            const int EDGE = 192;   // 2 ms, stereo
            bool fading_in = s->dry;
            s->dry = false;

            int n = avail < samples ? avail : samples;
            if (n <= 0) continue;

            // The listener's own setting for this person, walked towards
            // rather than jumped to: a slider dragged mid word is a fade,
            // not an edge.
            float want_vol = s->muted ? 0.0f : s->volume;
            float vol_from = s->volume_now;
            float vol_to = vol_from + (want_vol - vol_from) * 0.25f;
            float vol_step = (vol_to - vol_from) / (float)n;

            long pos = s->read_pos;
            int edge = n < EDGE ? n : EDGE;
            for (int k = 0; k < n; k++)
            {
                int v = (int)((float)s->pcm[pos] * (vol_from + vol_step * (float)k));
                if (fading_in && k < edge) v = v * k / edge;
                if (n < samples && k >= n - edge) v = v * (n - 1 - k) / edge;
                int sum = (int)out[k] + v;
                if (sum > 32767) sum = 32767;
                if (sum < -32768) sum = -32768;
                out[k] = (short)sum;

                pos++;
                if (pos >= SPEAKER_RING_SAMPLES) pos = 0;
            }
            s->read_pos = pos;
            s->volume_now = vol_to;
            if (n < samples) s->dry = true;     // ran dry mid-buffer: refill first
        }
        LeaveCriticalSection(&g_speakers_lock);
    }

    // Real audio needs no repair here any more: the defects these functions
    // used to chase came from opus concealment and FEC frames this pipeline
    // no longer asks for.



    // ---- receive pump ----------------------------------------------------

    void pump_incoming()
    {
        unsigned char packet[MAX_PACKET];
        unsigned char payload[1400];

        for (int i = 0; i < 32; i++)
        {
            int got = proxy::udp_recv(&g_udp_route, packet, (int)sizeof(packet));
            if (got <= 0) break;

            // Read before decryption: a retransmission carries a payload type
            // of its own and belongs to no picture directly, and a camera the
            // list does not know about must not be mistaken for a voice.
            unsigned int packet_pt = packet[1] & 0x7F;

            unsigned int ssrc = 0;
            unsigned short seq = 0;
            int len = decrypt_packet(packet, got, payload, sizeof(payload), &ssrc, &seq);
            if (len <= 0)
            {
                if (!g_logged_decrypt_fail)
                {
                    g_logged_decrypt_fail = true;
                    log_line("voice: first packet failed to decrypt (%d bytes, mode %d)", got, (int)g_mode);
                }
                continue;
            }

            // End-to-end protected media has to be unwrapped with the sender's
            // own key ratchet before it means anything to the decoder.
            const unsigned char* media = payload;
            unsigned char plain[1400];

            // protected_len, not a bare marker test: a padded frame keeps its
            // marker off the end of the buffer, and treating one as plain opus
            // feeds the decoder a block of ciphertext - which is the loud
            // full-scale garbage this pipeline has been chasing all along.
            unsigned int prot_len = dave::protected_len(payload, (unsigned int)len);
            if (prot_len)
            {
                snowflake sender = 0;
                {
                    EnterCriticalSection(&g_speakers_lock);
                    speaker* s = find_speaker(ssrc);
                    if (s) sender = s->user_id;

                    // A camera has no speaker behind its ssrc, and its frames
                    // are wrapped to the same person as their voice. Without
                    // this every picture is thrown away for want of a name.
                    if (!sender)
                    {
                        camera* cam = find_camera_by_ssrc(ssrc);
                        if (cam) sender = cam->user_id;
                    }
                    LeaveCriticalSection(&g_speakers_lock);
                }

                if (!sender || !g_media_ready)
                {
                    // The ssrc has not been mapped to a user yet; op 5 will
                    // arrive shortly and later frames will decode. Counted:
                    // during key churn this is where call audio silently goes.
                    g_stats.nokey++;
                    continue;
                }

                unsigned int plain_len = 0;
                if (!dave::decrypt_frame(&g_group, sender, payload, prot_len,
                                         plain, &plain_len))
                {
                    g_stats.unwrap++;
                    if (!g_logged_dave_frame)
                    {
                        g_logged_dave_frame = true;
                        log_line("voice: e2ee frame from user %llu did not unwrap", sender);
                    }
                    continue;
                }

                if (!g_logged_first_rx)
                {
                    g_logged_first_rx = true;
                    log_line("voice: first e2ee frame decoded from user %llu", sender);
                }

                media = plain;
                len = (int)plain_len;
            }

            // Video, if this ssrc belongs to somebody's camera. Checked before
            // the audio path, because handing a picture to the opus decoder is
            // exactly how a stream of noise gets made.
            if (packet_pt != PAYLOAD_OPUS)
            {
                bool marker = (packet[1] & 0x80) != 0;
                unsigned int timestamp =
                    ((unsigned int)packet[4] << 24) | ((unsigned int)packet[5] << 16) |
                    ((unsigned int)packet[6] << 8) | packet[7];

                EnterCriticalSection(&g_speakers_lock);
                feed_camera(ssrc, media, len, marker, seq, timestamp);
                LeaveCriticalSection(&g_speakers_lock);
                continue;
            }

            if (!g_logged_first_rx)
            {
                g_logged_first_rx = true;
                log_line("voice: first audio packet decoded from ssrc %u", ssrc);
            }

            EnterCriticalSection(&g_speakers_lock);
            speaker* s = ensure_speaker(ssrc, 0);
            if (s) feed_opus(s, seq, media, len);
            LeaveCriticalSection(&g_speakers_lock);
        }

        // The reorder window's patience runs on this tick: heads that never
        // arrived are written off and concealed here, so a lost packet cannot
        // hold up everything queued behind it for longer than 60 ms.
        EnterCriticalSection(&g_speakers_lock);
        for (unsigned int i = 0; i < g_speakers.count; i++)
            if (g_speakers[i].decoder) window_drain(&g_speakers[i], true);
        LeaveCriticalSection(&g_speakers_lock);
    }

    // Wraps one opus frame for whoever is listening and puts it on the wire.
    void send_media(const unsigned char* opus_data, int opus_len)
    {
        unsigned char wrapped[1400];

        if (g_dave_active && g_media_ready)
        {
            unsigned int wrapped_len = 0;
            if (!dave::encrypt_frame(&g_group, g_self_id, &g_dave_nonce,
                                     opus_data, (unsigned int)opus_len, wrapped, &wrapped_len))
                return;

            opus_data = wrapped;
            opus_len = (int)wrapped_len;
        }

        int sent = send_audio(opus_data, opus_len);
        if (!g_logged_first_tx)
        {
            g_logged_first_tx = true;
            log_line("voice: first audio packet sent (%d opus bytes, socket returned %d)", opus_len, sent);
        }
    }

    void pump_outgoing()
    {
        // The capture ring is fed by the device's clock and drained by this
        // wall-clock tick. The drift between the two is small but relentless,
        // and a scheduling stall adds to it in lumps; left alone it piles up
        // as mouth-to-ear latency until the ring overruns. Four frames of
        // queue is plenty of cushion - anything older is dropped unsent.
        audio::trim_capture(4 * AUDIO_FRAME_SAMPLES * AUDIO_CHANNELS);

        // The track, if one is playing. Pulled first and unconditionally: this
        // tick is its clock, and skipping a pull on a tick where the
        // microphone had nothing would leave the music running slow.
        short track[AUDIO_FRAME_SAMPLES * AUDIO_CHANNELS];
        bool have_music = music::next_frame(track);

        if (have_music && music::monitoring())
            audio::write_media(track, AUDIO_FRAME_SAMPLES * AUDIO_CHANNELS);

        short pcm[AUDIO_FRAME_SAMPLES * AUDIO_CHANNELS];
        bool have_mic = audio::read_capture_frame(pcm);

        if (!have_mic)
        {
            if (!g_logged_no_capture && g_tick > 100)
            {
                g_logged_no_capture = true;
                log_line("voice: no microphone frames after 2 s (capture active: %d)",
                         audio::capture_active() ? 1 : 0);
            }

            // A track still has to go out. Silence stands in for the
            // microphone and the mix below carries the music alone.
            if (!have_music) return;
            ccfset(pcm, 0, sizeof(pcm));
        }

        // Without a group there is nothing to encrypt to, and plain opus on a
        // DAVE channel gets the session dropped.
        if (g_dave_active && !g_media_ready)
        {
            if (g_speaking_sent) { send_speaking(false); g_speaking_sent = false; }
            vad::reset();
            return;
        }

        // Muting silences the microphone, not the track: the mute button is
        // about this room, and a person muting themselves to cough should not
        // cut the music off with it.
        bool mic_open = !g_muted && have_mic;

        if (!mic_open && !have_music)
        {
            if (g_speaking_sent) { send_speaking(false); g_speaking_sent = false; }
            vad::reset();
            return;
        }

        // Downmix before denoising: every suppressor expects mono.
        short mono[AUDIO_FRAME_SAMPLES];
        for (int i = 0; i < AUDIO_FRAME_SAMPLES; i++)
            mono[i] = mic_open ? (short)((pcm[i * 2] + pcm[i * 2 + 1]) / 2) : (short)0;

        if (mic_open) noise::process(mono, AUDIO_FRAME_SAMPLES);

        // Judged after the suppressor, so the threshold is set against what
        // would actually have been sent rather than against the raw room.
        bool talk = mic_open && vad::speaking(mono, AUDIO_FRAME_SAMPLES);

        // The music is added after the gate rather than before it. Judging a
        // frame that already has a track in it would leave the gate open for
        // as long as the track ran, and every silence between words would go
        // out as room noise.
        if (have_music)
        {
            for (int i = 0; i < AUDIO_FRAME_SAMPLES; i++)
            {
                int v = mono[i] + ((track[i * 2] + track[i * 2 + 1]) / 2);
                if (v > 32767) v = 32767;
                if (v < -32768) v = -32768;
                mono[i] = (short)v;
            }

            // Whatever the gate thought of the microphone, there is something
            // to send.
            talk = true;
        }

        if (!talk)
        {
            if (!g_speaking_sent) return;

            // Do not just stop mid-word. Opus has a three byte "nothing here"
            // frame; a few of them let the far side run its tail out and its
            // buffer down cleanly, and only then does the green ring go off.
            if (g_silence_left > 0)
            {
                g_silence_left--;

                // Straight onto the wire, not through the end to end wrapper.
                // The three byte silence frame is defined to travel bare, and
                // the reference client recognises it by those exact bytes
                // before it ever reaches a decryptor - wrapped, it is just an
                // unreadable packet.
                static const unsigned char quiet[3] = { 0xF8, 0xFF, 0xFE };
                send_audio(quiet, 3);
                return;
            }

            send_speaking(false);
            g_speaking_sent = false;
            return;
        }

        g_silence_left = SILENCE_TAIL;

        if (!g_speaking_sent)
        {
            // The other half of the same problem. Our own gate has just held
            // the microphone shut for a while, and the encoder still carries
            // the prediction state of the last word before it. Handing it the
            // first frame of a new one makes it code that frame as a
            // continuation of something the far side stopped hearing long ago,
            // and their decoder rings on it. Start the burst clean.
            science::start_speaking(g_channel_id, g_guild_id);

            opus_encoder_ctl(g_encoder, OPUS_RESET_STATE);
            opus_encoder_ctl(g_encoder, OPUS_SET_COMPLEXITY(0));

            send_speaking(true);
            g_speaking_sent = true;

            // Forces the settings below to be applied to the fresh state.
            g_music_encoding = !have_music;
        }

        // Speech and music want different encoders. Voice mode spends its
        // bits on a band a voice lives in and folds the rest away, which is
        // exactly wrong for a track; sixty four kilobits is also thin for one.
        // Changed only when it changes, because each of these walks into the
        // encoder's state.
        if (have_music != g_music_encoding)
        {
            g_music_encoding = have_music;

            opus_encoder_ctl(g_encoder, OPUS_SET_BITRATE(have_music ? 128000 : 64000));
            opus_encoder_ctl(g_encoder, OPUS_SET_SIGNAL(have_music ? OPUS_SIGNAL_MUSIC
                                                                   : OPUS_SIGNAL_VOICE));
        }

        unsigned char encoded[1400];
        int len = opus_encode(g_encoder, mono, AUDIO_FRAME_SAMPLES, encoded, sizeof(encoded));
        if (len > 1) send_media(encoded, len);
    }

    void report_stats()
    {
        if ((g_tick % 250) != 0) return;      // every 5 s

        rx_stats* c = &g_stats;
        g_stats_last = *c;
        g_stats_have = true;

        if (!c->late && !c->overflow && !c->railed && !c->concealed &&
            !c->nokey && !c->unwrap) { reset_stats(c); return; }

        log_line("voice rx: сыграно %u кадров, опоздало %u, срезано кольцом %u, "
                 "срывов декодера %u, скрыто потерь %u, без ключей %u, не развернуто %u, "
                 "медиа недобор %u / срез %u, буфер %u мс",
                 c->played, c->late, c->overflow, c->railed, c->concealed,
                 c->nokey, c->unwrap,
                 audio::render_underruns(), audio::render_overruns(),
                 audio::render_backlog_ms());

        reset_stats(c);
    }

    DWORD WINAPI tick_thread(LPVOID)
    {
        CoInitializeEx(0, COINIT_MULTITHREADED);
        timeBeginPeriod(1);

        LARGE_INTEGER freq, start;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&start);

        unsigned long long ticks_done = 0;

        while (g_running)
        {
            if (!g_session_ready)
            {
                if (WaitForSingleObject(g_stop_event, 20) == WAIT_OBJECT_0) break;
                continue;
            }

            pump_incoming();
            pump_outgoing();
            // No pump_mix here any more: playback is pulled by the render
            // thread straight out of the speaker rings, at the device's pace.
            report_stats();

            // Connected, but with no group there is nothing to encrypt to and
            // nothing to unwrap with: the call is silent in both directions
            // and nothing on screen says why. Rejoining does not always help,
            // because whatever the server is waiting for it is not a new
            // connection - it is a key package. So say so, and send one.
            if (g_dave_active && !g_media_ready)
            {
                if (!g_silent_since) g_silent_since = g_tick;
                else if (g_tick - g_silent_since > 750)      // 15 s
                {
                    g_silent_since = g_tick;
                    set_status(VOICE_CONNECTED, tr("E2EE не согласовано, пробую заново"));
                    dave_reinit(tr("пятнадцать секунд без ключей"));
                }
            }
            else
            {
                g_silent_since = 0;
            }

            g_tick++;
            ticks_done++;

            // Pace against the wall clock so drift does not accumulate.
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            long long target = start.QuadPart + (long long)(ticks_done * 20 * freq.QuadPart / 1000);
            long long remain_ms = (target - now.QuadPart) * 1000 / freq.QuadPart;

            if (remain_ms > 0)
            {
                if (remain_ms > 50) remain_ms = 50;
                if (WaitForSingleObject(g_stop_event, (DWORD)remain_ms) == WAIT_OBJECT_0) break;
            }
            else if (remain_ms < -200)
            {
                // Fell far behind (suspend/resume); resync the schedule.
                QueryPerformanceCounter(&start);
                ticks_done = 0;
            }
        }

        timeEndPeriod(1);
        CoUninitialize();
        return 0;
    }

    // ---- voice websocket loop ------------------------------------------

    void handle_ready(const jval* d)
    {
        g_ssrc = (unsigned int)d->i64("ssrc", 0);
        const char* ip = d->str("ip", 0);
        if (ip) ccstrncpy(g_udp_host, ip, sizeof(g_udp_host) - 1);
        g_udp_port = (unsigned short)d->i64("port", 0);

        // Prefer AES-GCM when the CPU/OS provides it; otherwise xchacha.
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
        else
        {
            log_line("voice: no supported encryption mode offered");
            set_status(VOICE_FAILED, tr("Сервер не предлагает поддерживаемое шифрование"));
            return;
        }

        log_line("voice: ready ssrc=%u %s:%u mode=%s", g_ssrc, g_udp_host, g_udp_port, chosen);
        set_status(VOICE_CONNECTING, tr("Согласование UDP..."));

        if (!udp_connect())
        {
            set_status(VOICE_FAILED, tr("UDP-соединение не установлено"));
            return;
        }

        char external_ip[80];
        unsigned short external_port = 0;
        if (!udp_discover(external_ip, sizeof(external_ip), &external_port))
        {
            set_status(VOICE_FAILED, tr("IP discovery не ответил"));
            return;
        }

        log_line("voice: discovered %s:%u", external_ip, external_port);

        // From here on the socket is polled from the 20 ms tick, so a blocking
        // recv would eat the whole frame budget whenever nothing has arrived.
        unsigned long nonblocking = 1;
        ioctlsocket(g_udp, FIONBIO, &nonblocking);

        send_select_protocol(external_ip, external_port, chosen);
    }

    void handle_session_description(const jval* d)
    {
        const jval* key = d->arr("secret_key");
        if (key->count < 32)
        {
            set_status(VOICE_FAILED, tr("Некорректный ключ сессии"));
            return;
        }
        for (int i = 0; i < 32; i++)
            g_secret_key[i] = (unsigned char)key->at((unsigned int)i)->as_i64(0);

        const char* mode = d->str("mode", "");
        if (ccscmp(mode, "aead_aes256_gcm_rtpsize") == 0) g_mode = MODE_AES256_GCM;
        else if (ccscmp(mode, "aead_xchacha20_poly1305_rtpsize") == 0) g_mode = MODE_XCHACHA20;

        // The server decides per session whether E2EE is switched on. Transport
        // encryption still works either way, so the session is kept alive; what
        // cannot be done is unwrapping individual E2E frames.
        int dave_version = d->i32("dave_protocol_version", 0);
        g_dave_active = dave_version > 0;
        g_dave_version = dave_version;
        g_dave_version_next = dave_version;
        g_dave_downgraded = false;
        if (g_dave_active)
            log_line("voice: server enabled DAVE v%d - E2EE frames cannot be decoded", dave_version);
        else
            log_line("voice: transport encryption %s, DAVE off", mode);

        int err = 0;
        if (!g_encoder)
        {
            // Mono: the microphone is one channel anyway, it halves the
            // bitrate, and the noise suppressors all work on mono frames.
            g_encoder = opus_encoder_create(AUDIO_SAMPLE_RATE, 1, OPUS_APPLICATION_VOIP, &err);
            if (err != OPUS_OK || !g_encoder)
            {
                log_line("voice: opus encoder failed (%d)", err);
                set_status(VOICE_FAILED, tr("Не удалось создать кодек"));
                return;
            }
            opus_encoder_ctl(g_encoder, OPUS_SET_BITRATE(64000));
            opus_encoder_ctl(g_encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
            // The bundled opus is a fixed-point build whose higher complexity
            // and FEC paths misbehave, which shows up as clicks and spikes.
            // Complexity 0 keeps the encoder on the code paths that work.
            opus_encoder_ctl(g_encoder, OPUS_SET_COMPLEXITY(0));
        }

        audio::start_render();
        audio::start_capture();

        g_sequence = (unsigned short)(GetTickCount() & 0xFFFF);
        g_timestamp = (unsigned int)GetTickCount();
        g_nonce_counter = 0;
        g_speaking_sent = false;
        g_silence_left = 0;
        g_have_pending_commit = false;
        g_pending_transition = 0;
        g_own_commit_ready = false;
        g_own_commit_won = false;
        vad::reset();
        g_logged_first_rx = false;
        g_logged_first_tx = false;
        g_logged_decrypt_fail = false;
        g_logged_no_capture = false;
        g_logged_dave_frame = false;

        InterlockedExchange(&g_session_ready, 1);
        set_status(VOICE_CONNECTED, g_dave_active ? tr("В канале, согласование E2EE...") : tr("В голосовом канале"));
        log_line("voice: session established");
    }

    void handle_voice_payload(const char* text, unsigned int len)
    {
        jdoc doc;
        doc.init();
        if (!doc.parse(text, (int)len)) { doc.free_doc(); return; }

        int op = doc.root->i32("op", -1);
        const jval* d = doc.root->obj("d");

        const jval* seq = doc.root->get("seq");
        if (seq->type == JTYPE_NUM) InterlockedExchange(&g_last_seq, (long)seq->inum);

        // Anything DAVE-related is worth seeing while the stack is incomplete.
        if (op >= VOP_DAVE_PREPARE_TRANSITION)
            log_line("dave: json op %d (%u bytes): %s", op, len, text);

        switch (op)
        {
        case VOP_HELLO:
            InterlockedExchange(&g_heartbeat_ms, (long)d->i64("heartbeat_interval", 13750));
            if (g_resuming) send_resume();
            else            send_identify();
            // The voice gateway expects the first beat right away; waiting a
            // full interval earns close code 4006.
            send_heartbeat_now();
            break;

        case VOP_READY:
            InterlockedExchange(&g_resuming, 0);
            handle_ready(d);
            break;

        case VOP_RESUMED:
            // No session description follows this one and none is needed:
            // the keys never went anywhere. The socket is simply back, and
            // the tick thread with it.
            InterlockedExchange(&g_resuming, 0);
            InterlockedExchange(&g_session_ready, 1);
            log_line("voice: сессия восстановлена");
            set_status(VOICE_CONNECTED, g_dave_active ? tr("В канале, согласование E2EE...")
                                                      : tr("В голосовом канале"));
            break;

        case VOP_SESSION_DESCRIPTION:
            handle_session_description(d);
            break;

        case VOP_SPEAKING:
        {
            unsigned int ssrc = (unsigned int)d->i64("ssrc", 0);
            snowflake uid = d->sf("user_id");
            if (ssrc)
            {
                EnterCriticalSection(&g_speakers_lock);
                ensure_speaker(ssrc, uid);
                LeaveCriticalSection(&g_speakers_lock);
            }
            break;
        }

        case VOP_DAVE_PREPARE_TRANSITION:
        {
            // Which version the channel is about to run. Remembered rather
            // than acted on: the switch happens when the server says the whole
            // channel is switching, not a moment earlier.
            unsigned int id = (unsigned int)d->i64("transition_id", 0);
            g_dave_version_next = (int)d->i64("protocol_version", g_dave_version);

            log_line("dave: подготовка перехода %u (версия %d, сейчас %d)",
                     id, g_dave_version_next, g_dave_version);

            // Only an answer. Nothing moves here - not even for transition
            // zero, which arrives at the start of every session: treating that
            // one as "go" tore down the group the moment it had been built.
            send_transition_response(id, true);
            break;
        }

        case VOP_DAVE_EXECUTE_TRANSITION:
        {
            unsigned int id = (unsigned int)d->i64("transition_id", 0);
            log_line("dave: выполнить переход %u", id);
            execute_transition(id);
            break;
        }

        case VOP_DAVE_PREPARE_EPOCH:
        {
            // Epoch one is a group starting over from nothing - the previous
            // one is gone, and this client has to introduce itself again with
            // a fresh key package or it will simply never be added.
            unsigned int id = (unsigned int)d->i64("transition_id", 0);
            unsigned int epoch = (unsigned int)d->i64("epoch", 0);
            int version = (int)d->i64("protocol_version", g_dave_version);

            log_line("dave: подготовка эпохи %u (переход %u, версия %d)", epoch, id, version);

            if (epoch == 1)
            {
                g_dave_version = version;
                g_dave_version_next = version;
                g_dave_active = version > 0;
                g_dave_downgraded = false;
                dave_reinit(tr("эпоха 1"));
            }
            break;
        }

        case VOP_VIDEO:
        {
            // Somebody's camera changed state. The same opcode carries both
            // "on" and "off": off is a video_ssrc of zero, and dropping the
            // entry then is what stops a tile lingering after they turn it off.
            snowflake uid = d->sf("user_id");
            unsigned int video_ssrc = (unsigned int)d->i64("video_ssrc", 0);
            unsigned int rtx_ssrc = (unsigned int)d->i64("rtx_ssrc", 0);
            unsigned int audio_ssrc = (unsigned int)d->i64("audio_ssrc", 0);

            if (!uid) break;

            EnterCriticalSection(&g_speakers_lock);

            // The audio ssrc comes with it, and it is often the first place a
            // name gets attached to a stream.
            if (audio_ssrc) ensure_speaker(audio_ssrc, uid);

            if (!video_ssrc)
            {
                if (find_camera(uid))
                {
                    log_line("voice: камера выключена у %llu", uid);
                    drop_camera(uid);
                }
            }
            else
            {
                camera* cam = find_camera(uid);
                if (!cam)
                {
                    camera fresh;
                    ccfset(&fresh, 0, sizeof(fresh));
                    fresh.user_id = uid;
                    g_cameras.push(fresh);
                    cam = &g_cameras[g_cameras.count - 1];
                }

                if (cam->video_ssrc != video_ssrc)
                {
                    if (cam->rx_ready) rtpvid::rx_reset(&cam->rx);
                    cam->video_ssrc = video_ssrc;
                }
                cam->rtx_ssrc = rtx_ssrc;

                log_line("voice: камера включена у %llu (video ssrc %u)", uid, video_ssrc);
            }

            LeaveCriticalSection(&g_speakers_lock);
            break;
        }

        case VOP_CLIENT_DISCONNECT:
        {
            snowflake uid = d->sf("user_id");
            EnterCriticalSection(&g_speakers_lock);
            drop_camera(uid);
            for (unsigned int i = 0; i < g_speakers.count; i++)
            {
                if (g_speakers[i].user_id == uid)
                {
                    if (g_speakers[i].decoder) opus_decoder_destroy(g_speakers[i].decoder);
            wavdump::finish(&g_speakers[i].dump);
                    if (g_speakers[i].pcm) memfree(g_speakers[i].pcm);
                    g_speakers.delete_at(i);
                    break;
                }
            }
            LeaveCriticalSection(&g_speakers_lock);
            break;
        }

        default:
            break;
        }

        doc.free_doc();
    }

    // Which reasons for a dead socket are worth coming back from.
    //
    // 1006 is the one that matters and the one being complained about: no
    // close frame at all, which is what a tcp connection dropped underneath
    // us looks like from up here - a router forgetting the mapping, a
    // network handover, discord's edge going away. It arrives on its own
    // schedule, anywhere from a quarter of an hour to hours in, and until
    // now it simply ended the call where it stood.
    //
    // The 4000s are discord saying why, and most of them are reasons not to
    // come back: 4006 the session is void, 4014 we were removed from the
    // channel, 4004 the token was refused. 4015 is its own voice server
    // falling over, which is exactly what resuming exists for.
    bool close_worth_resuming(unsigned int code)
    {
        return code == 1000 || code == 1001 || code == 1006 || code == 4015;
    }

    DWORD WINAPI voice_ws_thread(LPVOID)
    {
        CoInitializeEx(0, COINIT_MULTITHREADED);

        char url[512];
        cnprint(url, sizeof(url), "wss://%s/?v=8", g_endpoint);

        const proxy_config* via = g_proxy.in_use() ? &g_proxy : 0;

        ubuffer message;
        message.init(1 << 14);

        set_status(VOICE_CONNECTING, tr("Подключение к голосовому серверу..."));
        InterlockedExchange(&g_resuming, 0);

        unsigned int close_code = 0;
        unsigned int attempt = 0;
        unsigned int backoff = 1000;

        // Sticky: whether this voice session ever authenticated at all. A
        // resume attempt that cannot even open a socket has no session of its
        // own, and judging by the attempt rather than by the session would end
        // the call on the first blip - which is the case reconnecting is for.
        bool ever_ready = false;

        // Built once and reused across attempts. destroy() deletes the lock
        // every send goes through, and the heartbeat and tick threads are
        // still running underneath: taking the socket apart and putting it
        // back together beneath them is a race that would surface as a crash
        // nowhere near the reconnect that caused it. close_handles() drops
        // the connection and keeps the object.
        g_ws.init();

        for (;;)
        {
            bool opened = g_ws.connect(url, "Origin: https://discord.com\r\n", via);
            if (opened)
            {
                for (;;)
                {
                    bool binary = false;
                    ws_result r = g_ws.receive(&message, &binary);
                    if (r != WS_MESSAGE) break;
                    if (!g_running) break;

                    if (binary) handle_binary_payload(message.data, message.size);
                    else handle_voice_payload((const char*)message.c_str(), message.size);
                }
            }
            else if (!attempt)
            {
                set_status(VOICE_FAILED, tr("Голосовой сервер недоступен"));
            }

            // A connection that authenticated earns the budget back: four
            // tries is meant to cover one outage, not the whole call.
            if (g_session_ready)
            {
                ever_ready = true;
                attempt = 0;
                backoff = 1000;
            }

            // Only when the socket actually ran: a resume attempt that could
            // not even connect leaves close_status untouched, and the code
            // worth reporting is still the one that started all this.
            if (opened) close_code = g_ws.close_status;

            // Nothing may beat into a socket that is going away, and the tick
            // thread parks itself on this flag - which stops it pumping audio
            // into a session that is not answering. VOP_RESUMED puts it back.
            InterlockedExchange(&g_heartbeat_ms, 0);
            InterlockedExchange(&g_session_ready, 0);
            g_ws.close_handles();

            if (!g_running) break;

            // Only a session that got as far as existing can be resumed, and
            // discord does not keep a dropped one for long - a handful of
            // tries over the first few seconds is the whole of the window.
            // Past that the answer is 4006, which is the teardown below,
            // reached the slow way.
            bool worth = ever_ready && attempt < 4 &&
                         (!opened || close_worth_resuming(close_code));
            if (!worth) break;

            attempt++;
            InterlockedExchange(&g_resuming, 1);

            log_line("voice: сокет умер (код %u) - восстанавливаю сессию, попытка %u",
                     close_code, attempt);
            set_status(VOICE_CONNECTING, tr("Связь потеряна, восстанавливаю..."));

            if (WaitForSingleObject(g_stop_event, backoff) == WAIT_OBJECT_0) break;
            backoff = backoff < 8000 ? backoff * 2 : 8000;
        }

        // The close code is the whole answer when a connection dies during an
        // account switch. 4006 means discord invalidated the session, which is
        // its decision and cannot be argued with from here; anything else means
        // this client dropped it and that is ours to fix.
        g_last_close = close_code;

        // 4006 says the voice session we were handed is dead. Discord's own
        // view of us, though, is that we are still sitting in the channel - so
        // a later join asks it to move us from that channel to that channel,
        // it sees no change, and it sends neither a new state nor a new server.
        // The client then waits for a half that is never coming, and every
        // voice channel looks broken until a restart.
        //
        // Standing up and saying we have left is what makes the next join a
        // real transition again.
        if (close_code == 4006)
        {
            log_line("voice: 4006 - объявляю выход, иначе сервер считает нас всё ещё в канале");
            ccfset(g_voice_session, 0, sizeof(g_voice_session));
            gateway::update_voice_state(0, 0, false, false);
        }

        // The call is over however it ended, so a connection kept alive purely
        // to hold it should not outlive it.
        gateway::release_hold();
        if (!g_stop_reason[0])
            g_stop_reason = close_code == 4006 ? "сервер снёс сессию"
                                               : "сокет закрылся";

        log_line("voice: websocket loop ended (close code %u, state %d)%s",
                 close_code, (int)g_state,
                 close_code == 4006 ? " - сессия аннулирована сервером" : "");

        message.free_buffer();
        InterlockedExchange(&g_session_ready, 0);
        InterlockedExchange(&g_resuming, 0);
        InterlockedExchange(&g_heartbeat_ms, 0);
        g_ws.destroy();

        if (g_running && g_state != VOICE_FAILED)
            set_status(VOICE_IDLE, tr("Голосовой канал закрыт"));

        CoUninitialize();
        return 0;
    }

    void start_voice_connection()
    {
        // Cleared here, so what the panel shows always belongs to the most
        // recent teardown rather than to whatever happened before it. A field
        // that is only ever written is a field that eventually lies.
        g_stop_reason = "";
        g_last_close = 0;

        if (!g_have_server || !g_have_state)
        {
            log_line("voice: waiting for the other half (server=%d state=%d)",
                     g_have_server ? 1 : 0, g_have_state ? 1 : 0);
            return;
        }
        // VOICE_STATE_UPDATE and VOICE_SERVER_UPDATE both land here and can
        // arrive in either order; only the first complete pair starts threads.
        if (InterlockedCompareExchange(&g_running, 1, 0) != 0) return;

        log_line("voice: starting session for guild %llu channel %llu",
                 g_guild_id, g_channel_id);

        ResetEvent(g_stop_event);

        g_ws_thread = CreateThread(0, 0, voice_ws_thread, 0, 0, 0);
        g_beat_thread = CreateThread(0, 0, voice_heartbeat_thread, 0, 0, 0);
        g_tick_thread = CreateThread(0, 0, tick_thread, 0, 0, 0);
    }

    // keep_server leaves the endpoint and token in place. A move inside a
    // guild reuses both, and there is no second VOICE_SERVER_UPDATE coming to
    // put them back if they are thrown away here.
    void stop_voice_connection(bool keep_server = false)
    {
        // join() runs on the UI thread while the gateway thread can tear the
        // session down at the same time; only the winner of this exchange gets
        // to close the handles.
        if (InterlockedCompareExchange(&g_running, 0, 1) != 1) return;

        InterlockedExchange(&g_session_ready, 0);
        SetEvent(g_stop_event);
        g_ws.close();

        if (g_tick_thread) { WaitForSingleObject(g_tick_thread, 2000); CloseHandle(g_tick_thread); g_tick_thread = 0; }
        if (g_beat_thread) { WaitForSingleObject(g_beat_thread, 2000); CloseHandle(g_beat_thread); g_beat_thread = 0; }
        if (g_ws_thread) { WaitForSingleObject(g_ws_thread, 4000); CloseHandle(g_ws_thread); g_ws_thread = 0; }

        g_ws.destroy();

        proxy::close_udp(&g_udp_route);
        if (g_udp != INVALID_SOCKET) { closesocket(g_udp); g_udp = INVALID_SOCKET; }

        // Before the speakers, not after: the render thread pulls straight
        // out of their rings now, and a freed ring under a live render thread
        // is a crash, not a click.
        audio::stop_capture();
        audio::stop_render();

        clear_speakers();
        clear_cameras();
        if (g_encoder) { opus_encoder_destroy(g_encoder); g_encoder = 0; }

        if (!keep_server)
        {
            g_have_server = false;
            ccfset(g_endpoint, 0, sizeof(g_endpoint));
            ccfset(g_voice_token, 0, sizeof(g_voice_token));
        }
        g_have_state = false;
        g_dave_active = false;
        g_group_ready = false;
        g_media_ready = false;
        g_key_package_ready = false;
        g_dave_nonce = 0;
        g_dave_version = 0;
        g_dave_version_next = 0;
        g_dave_downgraded = false;
        g_last_transition_done = 0xFFFFFFFF;
        g_last_reinit_tick = 0;
        dave::reset_ratchets();
        g_mode = MODE_NONE;
        ccfset(g_secret_key, 0, sizeof(g_secret_key));
    }
}

// ---------------------------------------------------------------------------
// public
// ---------------------------------------------------------------------------

void voice::init()
{
    if (g_locks_ready) return;
    InitializeCriticalSection(&g_speakers_lock);
    g_speakers = ulist<speaker>();
    g_pending_commit.init(2048);
    g_own_commit.init(2048);
    g_stop_event = CreateEventW(0, TRUE, FALSE, 0);
    g_locks_ready = true;
    load_user_audio();
    set_status(VOICE_IDLE, "");
    audio::init();
    audio::set_voice_mixer(mix_voice_into);
}

void voice::shutdown()
{
    if (!g_locks_ready) return;
    g_stop_reason = "voice::shutdown";
    stop_voice_connection();
    gateway::release_hold();
    audio::set_voice_mixer(0);
    audio::shutdown();
    g_speakers.dispose();
    DeleteCriticalSection(&g_speakers_lock);
    if (g_stop_event) { CloseHandle(g_stop_event); g_stop_event = 0; }
    g_locks_ready = false;
}

void voice::join(snowflake guild_id, snowflake channel_id)
{
    // Captured here and nowhere else. Everything downstream, including the end
    // to end keys, uses this rather than whatever the store says later.
    g_self_id = store::self_id();

    // And so is the route out. An account switch later must not drag a live
    // call onto a different proxy, or off one.
    g_proxy = storage::active_proxy();

    const char* blocked = proxy::voice_blocked_reason(&g_proxy);
    if (blocked)
    {
        // Better to say so than to spend ten seconds failing to reach a media
        // server that was never reachable this way.
        g_stop_reason = blocked;
        set_status(VOICE_FAILED, blocked);
        log_line("voice: звонок отклонён - %s", blocked);
        return;
    }


    if (g_channel_id == channel_id && g_running) return;

    g_stop_reason = "другой канал";
    stop_voice_connection();

    g_pending_guild = guild_id;
    g_pending_channel = channel_id;
    g_guild_id = guild_id;
    g_channel_id = channel_id;

    // Whose call this is, taken now. The gateway that opened it is deliberately
    // kept alive across an account switch, so analytics about it has to keep
    // naming the account that is actually in the channel.
    science::set_voice_identity(api::token(), science::analytics_token());
    science::join_voice_channel(channel_id, guild_id);
    set_status(VOICE_CONNECTING, tr("Запрос голосового сервера..."));
    gateway::update_voice_state(guild_id, channel_id, g_muted, g_deafened);
}

void voice::leave()
{
    // Played here rather than left to the gateway: by the time the dispatch
    // announcing our own departure arrives the channel has been forgotten,
    // and the handler there has nothing left to compare against.
    sounds::play(SOUND_VOICE_LEAVE);

    snowflake guild = g_guild_id;
    g_stop_reason = "нажата трубка";
    stop_voice_connection();

    // Whatever was being kept alive for this call has nothing left to hold.
    gateway::release_hold();

    g_guild_id = 0;
    g_channel_id = 0;
    g_pending_guild = 0;
    g_pending_channel = 0;

    // Sends what is still queued under the call's account before forgetting it.
    science::clear_voice_identity();

    gateway::update_voice_state(guild, 0, false, false);
    set_status(VOICE_IDLE, "");
}

voice_state_kind voice::state() { return (voice_state_kind)g_state; }
const char* voice::status_text() { return g_status; }
snowflake voice::current_channel() { return g_channel_id; }
snowflake voice::current_guild() { return g_guild_id; }
const char* voice::session_id() { return g_voice_session; }

void voice::set_muted(bool m)
{
    g_muted = m;
    if (g_channel_id) gateway::update_voice_state(g_guild_id, g_channel_id, g_muted, g_deafened);
}

bool voice::muted() { return g_muted; }

void voice::set_deafened(bool d)
{
    g_deafened = d;
    if (d) g_muted = true;
    if (g_channel_id) gateway::update_voice_state(g_guild_id, g_channel_id, g_muted, g_deafened);
}

bool voice::deafened() { return g_deafened; }

const char* voice::last_stop_reason() { return g_stop_reason; }
unsigned short voice::last_close_code() { return g_last_close; }

bool voice::is_speaking(snowflake user_id)
{
    return speaking_level(user_id) > 0.01f;
}

const char* voice::blocked_reason()
{
    proxy_config cfg = storage::active_proxy();
    return proxy::voice_blocked_reason(&cfg);
}

bool voice::last_rx_report(voice::rx_report* out)
{
    if (!out || !g_stats_have) return false;

    rx_stats* c = &g_stats_last;
    out->played = c->played;
    out->late = c->late;
    out->overflow = c->overflow;
    out->railed = c->railed;
    out->concealed = c->concealed;
    out->nokey = c->nokey;
    out->unwrap = c->unwrap;
    out->underruns = audio::render_underruns();
    out->overruns = audio::render_overruns();
    return true;
}

bool voice::e2ee_active() { return g_dave_active && g_media_ready; }

// ---- cameras --------------------------------------------------------------

bool voice::camera_on(snowflake user_id)
{
    if (!g_locks_ready || !user_id) return false;

    EnterCriticalSection(&g_speakers_lock);
    bool on = find_camera(user_id) != 0;
    LeaveCriticalSection(&g_speakers_lock);
    return on;
}

int voice::cameras_on(snowflake* out, int cap)
{
    if (!g_locks_ready || !out || cap <= 0) return 0;

    int count = 0;
    EnterCriticalSection(&g_speakers_lock);
    for (unsigned int i = 0; i < g_cameras.count && count < cap; i++)
        out[count++] = g_cameras[i].user_id;
    LeaveCriticalSection(&g_speakers_lock);
    return count;
}

snowflake voice::watched_camera() { return g_watched_camera; }

void voice::watch_camera(snowflake user_id)
{
    if (!g_locks_ready) return;

    EnterCriticalSection(&g_speakers_lock);

    if (g_watched_camera != user_id)
    {
        // Whatever was half assembled belonged to the last person; keeping it
        // would splice two pictures together.
        camera* was = find_camera(g_watched_camera);
        if (was && was->rx_ready) rtpvid::rx_reset(&was->rx);

        g_watched_camera = user_id;

        if (g_camera_decoding)
        {
            vdec::stop();
            g_camera_decoding = false;
        }

        camera* now = find_camera(user_id);
        if (now && now->rx_ready) rtpvid::rx_reset(&now->rx);

        log_line(user_id ? "voice: смотрю камеру %llu" : "voice: камера закрыта", user_id);
    }

    LeaveCriticalSection(&g_speakers_lock);
}

bool voice::take_camera_frame(const unsigned char** rgba, int* width, int* height)
{
    if (!g_camera_decoding) return false;
    return vdec::next(rgba, width, height);
}

int voice::camera_width() { return g_camera_decoding ? vdec::width() : 0; }
int voice::camera_height() { return g_camera_decoding ? vdec::height() : 0; }
unsigned int voice::camera_frames() { return (unsigned int)g_camera_frames; }

float voice::user_volume(snowflake user_id)
{
    if (!g_locks_ready || !user_id) return 1.0f;

    EnterCriticalSection(&g_speakers_lock);
    user_audio* entry = find_user_audio(user_id);
    float volume = entry ? entry->volume : 1.0f;
    LeaveCriticalSection(&g_speakers_lock);
    return volume;
}

void voice::set_user_volume(snowflake user_id, float volume)
{
    if (!g_locks_ready || !user_id) return;

    if (volume < 0.0f) volume = 0.0f;
    if (volume > 10.0f) volume = 10.0f;

    EnterCriticalSection(&g_speakers_lock);
    user_audio* entry = find_user_audio(user_id);
    store_user_audio(user_id, volume, entry ? entry->muted : false);
    LeaveCriticalSection(&g_speakers_lock);
}

bool voice::user_muted(snowflake user_id)
{
    if (!g_locks_ready || !user_id) return false;

    EnterCriticalSection(&g_speakers_lock);
    user_audio* entry = find_user_audio(user_id);
    bool muted = entry ? entry->muted : false;
    LeaveCriticalSection(&g_speakers_lock);
    return muted;
}

void voice::set_user_muted(snowflake user_id, bool muted)
{
    if (!g_locks_ready || !user_id) return;

    EnterCriticalSection(&g_speakers_lock);
    user_audio* entry = find_user_audio(user_id);
    store_user_audio(user_id, entry ? entry->volume : 1.0f, muted);
    LeaveCriticalSection(&g_speakers_lock);
}

float voice::speaking_level(snowflake user_id)
{
    if (user_id == g_self_id)
        return (!g_muted && audio::capture_active()) ? audio::input_level() : 0.0f;

    float level = 0.0f;
    EnterCriticalSection(&g_speakers_lock);
    for (unsigned int i = 0; i < g_speakers.count; i++)
    {
        if (g_speakers[i].user_id == user_id)
        {
            if (g_tick - g_speakers[i].last_audio_tick < 8) level = g_speakers[i].level;
            break;
        }
    }
    LeaveCriticalSection(&g_speakers_lock);
    return level;
}

// Whether a dispatch off the main socket has any business with this
// connection. After an account switch the gateway belongs to somebody else,
// and their voice state - which is empty, because they are not in a call -
// would otherwise hang up a call this client is deliberately holding open.
//
// The dispatcher's own check is against whoever the store says we are, and
// that answer changes the moment accounts do. This one is against whoever
// this connection was opened by, which never changes.
static bool dispatch_is_ours()
{
    if (!g_self_id) return true;
    snowflake now = store::self_id();
    return now == 0 || now == g_self_id;
}

void voice::on_gateway_voice_state(const jval* d)
{
    if (!d || !dispatch_is_ours()) return;

    // And the payload has to name us too: a state for a different user reaching
    // this far means the dispatcher was reading a stale identity.
    snowflake who = d->sf("user_id");
    if (g_self_id && who && who != g_self_id) return;

    snowflake channel = d->sf("channel_id");
    const char* session = d->str("session_id", 0);

    if (!channel)
    {
        // We were moved out or disconnected elsewhere.
        if (g_channel_id) { g_stop_reason = "гейтвей вынес из канала"; stop_voice_connection(); }
        g_channel_id = 0;
        g_guild_id = 0;
        set_status(VOICE_IDLE, "");
        return;
    }

    // Read before it is overwritten. The comparison below used to happen after
    // the assignment, so it asked whether the channel equalled itself and was
    // answered yes every time - which is why being dragged into another
    // channel ended the call instead of following it.
    snowflake was_in = g_channel_id;

    g_channel_id = channel;
    if (d->has("guild_id")) g_guild_id = d->sf("guild_id");

    if (session)
    {
        bool moved = g_running && was_in && was_in != channel;

        // Signing back into the account whose call is being held brings a
        // fresh gateway that dutifully reports the voice state it can see -
        // the one we are already in, with the session id we are already using.
        // Restarting the connection on that news would drop a call that is
        // working perfectly well.
        if (!moved && g_running && ccscmp(g_voice_session, session) == 0)
        {
            log_line("voice: уже в этом канале с этой сессией, ничего не меняем");
            return;
        }

        ccstrncpy(g_voice_session, session, sizeof(g_voice_session) - 1);
        g_have_state = true;

        if (moved)
        {
            // Moved by somebody else. The endpoint and token stay valid within
            // a guild, so they are kept: dropping them would leave nothing to
            // reconnect with, because a move inside a guild brings no second
            // VOICE_SERVER_UPDATE to supply them again. A move that does cross
            // servers overwrites them a moment later and restarts on its own.
            log_line("voice: перенесли из канала %llu в %llu, переподключаюсь",
                     was_in, channel);
            g_stop_reason = "перенос в другой канал";
            stop_voice_connection(true);
        }
        else
        {
            log_line("voice: got session id for channel %llu", channel);
        }

        start_voice_connection();
    }
    else
    {
        log_line("voice: VOICE_STATE_UPDATE without a session id");
    }
}

void voice::on_gateway_voice_server(const jval* d)
{
    if (!d || !dispatch_is_ours()) return;

    const char* endpoint = d->str("endpoint", 0);
    const char* token = d->str("token", 0);
    if (!endpoint || !token) return;

    ccfset(g_endpoint, 0, sizeof(g_endpoint));
    ccstrncpy(g_endpoint, endpoint, sizeof(g_endpoint) - 1);
    ccfset(g_voice_token, 0, sizeof(g_voice_token));
    ccstrncpy(g_voice_token, token, sizeof(g_voice_token) - 1);

    if (d->has("guild_id")) g_guild_id = d->sf("guild_id");
    else if (d->has("channel_id")) g_channel_id = d->sf("channel_id");

    g_have_server = true;
    log_line("voice: got voice server %s", g_endpoint);
    start_voice_connection();
}

void voice::on_gateway_disconnected()
{
    g_stop_reason = "гейтвей отключился";
    stop_voice_connection();
    if (g_channel_id) set_status(VOICE_IDLE, tr("Соединение потеряно"));
}

