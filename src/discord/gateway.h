#pragma once
#include "types.h"

enum gateway_state
{
    GW_OFFLINE = 0,
    GW_CONNECTING,
    GW_IDENTIFYING,
    GW_READY,
    GW_RECONNECTING,
    GW_FAILED,
};

namespace gateway
{
    void start();
    void stop();

    gateway_state state();
    const char* status_text();
    const char* session_id();

    // Raw op payload, already serialized. Used by the voice layer for op 4.
    // Sends on the connection the call belongs to, which after an account
    // switch is not the one in front.
    bool send_raw(const void* json, unsigned int len);
    // Whose session that is, or 0 if it never got as far as READY.
    snowflake call_owner_id();

    void update_voice_state(snowflake guild_id, snowflake channel_id, bool self_mute, bool self_deaf);

    // The voice region discord ranked first for this connection, or an empty
    // string before READY has arrived. A Go Live stream is created against it.
    const char* preferred_region();
    void request_guild_members(snowflake guild_id, const char* query, int limit);
    // Lazy guild subscription; without it discord withholds member/presence data.
    void subscribe_guild(snowflake guild_id, snowflake channel_id);

    // Sets our own presence. "online", "idle", "dnd" or "invisible". Sent over
    // the socket for immediate effect and written to the account settings so
    // it survives a restart and reaches the other clients.
    void set_status(const char* status);

    // While this is set, losing the socket does not tear down the voice
    // connection or anything being streamed. Used across an account switch,
    // where the main socket is deliberately replaced and the media has no
    // reason to go with it.
    void hold_media(bool on);

    // Turns the current connection into one that only keeps a call alive, and
    // frees the client to sign in again on a second one. Discord ends a voice
    // session together with the gateway session that opened it, so the old
    // socket has to outlive the account switch.
    void detach_for_voice();
    void release_hold();
    bool is_holding();
}
