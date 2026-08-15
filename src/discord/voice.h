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

namespace voice
{
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
