#pragma once
#include "types.h"

struct jval;

enum voice_state_kind
{
    VOICE_IDLE = 0,
    VOICE_CONNECTING,
    VOICE_CONNECTED,
    VOICE_FAILED,
};

// How a call's media is carried.
//
// Discord speaks two of these. The first is its own: a compact udp protocol
// with its own header and its own encryption, which is what this client has
// always used and what nothing but discord's own app implements. The second is
// ordinary webrtc - sdp, ice, dtls, srtp - which is what every browser based
// client ends up using because it is what a browser already has.
//
// Both go out over udp. Choosing webrtc changes the shape of the packets and
// not the way they leave the machine: discord publishes exactly one ice
// candidate and it is a udp one.
enum voice_transport
{
    TRANSPORT_OWN = 0,     // discord's own udp protocol
    TRANSPORT_WEBRTC,      // sdp / ice / dtls-srtp
};

namespace voice
{
    // Which transport the next call will use, and changing it. Kept in
    // settings, so it survives a restart.
    int  transport();
    void set_transport(int t);

    // True while a call's media is leaving without the proxy that the account
    // is otherwise using. Not a fault - it is what "иначе напрямую" asks for -
    // but it means discord's voice server sees this machine's address, and
    // somebody running an account behind a proxy should be told rather than
    // left to assume.
    bool media_unproxied();

    void init();
    void shutdown();

    void join(snowflake guild_id, snowflake channel_id);
    void leave();

    voice_state_kind state();
    const char* status_text();
    snowflake current_channel();
    snowflake current_guild();

    // The voice session id the gateway handed out. A Go Live stream opens its
    // own connection but identifies with this same session.
    const char* session_id();

    void set_muted(bool muted);
    bool muted();
    void set_deafened(bool deafened);
    bool deafened();

    // True while the user's stream carried audio in the last few frames.
    // Why the last connection ended and with what close code. A dropped call
    // has several possible authors and telling them apart from the outside is
    // otherwise guesswork.
    const char* last_stop_reason();
    unsigned short last_close_code();

    // Quiets the notification sounds for a while. A 4014 is discord rotating
    // the voice server, not anybody leaving: the call drops and comes back
    // within seconds, and chiming every leave, join and stream stop/start in
    // between is noise about nothing. Manual actions clear it first, so
    // something the person did themselves still sounds.
    void hush(unsigned long long ms);
    bool hushed();

    // ---- cameras ---------------------------------------------------------
    //
    // Somebody else's webcam. It rides the voice connection already open, on
    // its own source. Watching one subscribes to it with op 15
    // MEDIA_SINK_WANTS first: without the subscription the server forwards
    // nothing and the window waits forever.
    //
    // One is decoded at a time, and it is the one being watched. The Media
    // Foundation decoder is a single instance shared with the stream viewer,
    // so several at once would need it split into instances first; watching a
    // camera and a screen share together does not work for the same reason.
    bool camera_on(snowflake user_id);
    int cameras_on(snowflake* out, int cap);

    snowflake watched_camera();
    void watch_camera(snowflake user_id);   // zero stops watching

    // The newest decoded picture as RGBA, false when nothing new has arrived.
    // The bytes belong to the decoder and last until the next call.
    bool take_camera_frame(const unsigned char** rgba, int* width, int* height);
    int camera_width();
    int camera_height();
    unsigned int camera_frames();

    // What the camera path has done since the watched camera was opened:
    // packets heard from every camera, frames the watched one assembled,
    // frames its reassembler threw away, and the video source subscribed to.
    // The window shows these, because a picture that never arrives is
    // otherwise indistinguishable from a window that never asked.
    unsigned int camera_packets();
    unsigned int camera_dropped();
    unsigned int camera_video_ssrc();

    bool is_speaking(snowflake user_id);
    float speaking_level(snowflake user_id);

    // Per person playback, remembered between sessions. The volume is a plain
    // multiplier with 1.0 meaning untouched. Muting silences that one person
    // for us alone - nothing is sent, and they are not told.
    float user_volume(snowflake user_id);
    void set_user_volume(snowflake user_id, float volume);
    bool user_muted(snowflake user_id);
    void set_user_muted(snowflake user_id, bool muted);

    // What the receive path did over the last five seconds. Only here so the
    // audio settings can show it: chasing crackle by reading a log means
    // restarting the client to reach the file, and restarting is what clears
    // the evidence.
    struct rx_report
    {
        unsigned int played;      // opus frames decoded on arrival
        unsigned int late;        // packets older than what was already decoded
        unsigned int overflow;    // speaker ring full, oldest queued audio dropped
        unsigned int railed;      // frames the decoder returned saturated end to end
        unsigned int concealed;   // lost frames papered over by opus concealment
        unsigned int nokey;       // protected frames dropped: no keys or no sender
        unsigned int unwrap;      // protected frames the ratchet refused
        unsigned int underruns;   // media/stream rings ran dry mid-buffer
        unsigned int overruns;    // media/stream rings dropped queued audio
    };

    bool last_rx_report(rx_report* out);

    // Whether the call happening right now is actually end to end encrypted.
    // Not a setting: the identify declares the highest version this client can
    // speak and the server decides per session whether the channel uses it.
    bool e2ee_active();

    // Why a call cannot be placed on the account signed in right now, or null
    // when it can. A proxy that carries no udp is the only reason so far.
    const char* blocked_reason();

    // Called by the gateway dispatcher.
    void on_gateway_voice_state(const jval* d);
    void on_gateway_voice_server(const jval* d);
    void on_gateway_disconnected();
}
