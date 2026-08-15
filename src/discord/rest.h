#pragma once
#include "types.h"
#include "net/http.h"

// REST side of the discord API. Every high level call is fire-and-forget: it
// posts a job, the worker performs the blocking request and folds the result
// into the store, and the UI notices on its next frame.

struct upload_file
{
    char name[260];
    char content_type[128];
    unsigned char* data;    // owned, released after upload
    unsigned int size;
};

namespace api
{
    void init();
    void shutdown();

    void set_token(const char* token);
    const char* token();
    bool has_token();

    // ---- raw, blocking; call from a job thread ----
    bool call(const char* method, const char* path, const char* json_body, http_response* out);
    bool call_absolute(const char* method, const char* url, const char* json_body, http_response* out);

    // ---- blocking, used by the login flow ----
    bool verify_token(const char* token_value, char* out_error, int error_cap);

    // ---- async ----
    void fetch_messages(snowflake channel_id, snowflake before_id);
    void send_message(snowflake channel_id, const char* content, snowflake reply_to);
    // Takes ownership of every file buffer in the list, and of the list storage.
    void send_message_with_files(snowflake channel_id, const char* content, ulist<upload_file>* files);
    void fetch_user_profile(snowflake user_id, snowflake guild_id);
    void fetch_guild_channels(snowflake guild_id);

    // The account's sign-ins (GET /auth/sessions), for the settings popup.
    // Result lands in the store: state loading -> ready/failed.
    void fetch_sessions();

    // Member and online totals for a server nobody has opened. Cheap, cached,
    // and asked for only when something is about to show them.
    void fetch_guild_counts(snowflake guild_id);
    void open_dm(snowflake user_id);
    void ack_message(snowflake channel_id, snowflake message_id);
    // Writes the presence into the account settings, so it survives a restart
    // and reaches the user's other clients. The socket handles the immediate
    // change; this is what makes it stick.
    void update_status(const char* status);

    // Edits our own profile. Any argument may be null to leave it alone.
    void update_self_profile(const char* global_name, const char* bio);

    // Replaces the avatar or the banner with the contents of a file. Discord
    // takes these as data URIs on the account itself, not on the profile, and
    // works out the format from the prefix. Passing an empty path clears it.
    void update_self_image(bool banner, const wchar_t* path);
    void trigger_typing(snowflake channel_id);
    void delete_message(snowflake channel_id, snowflake message_id);
    // Rings the other side of a direct-message call. Joining the voice channel
    // alone connects us but never makes their client notify them.
    void ring_call(snowflake channel_id);

    // ---- friends / guilds ----
    void send_friend_request(const char* username);
    void accept_friend_request(snowflake user_id);
    // Declines an incoming request, cancels an outgoing one, or removes a friend.
    void remove_relationship(snowflake user_id);
    void block_user(snowflake user_id);
    void join_guild_by_invite(const char* invite_code);
    void leave_guild(snowflake guild_id);

    // Result of the most recent user-triggered action, for the UI to display.
    const char* last_error();
    void clear_last_error();
    void set_last_error(const char* text);
}
