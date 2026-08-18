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

// Which build of the official client this one claims to be.
//
// Not decoration. Discord gates several endpoints on it and answers a request
// carrying an implausible number with a bare 400 and no message - which is
// exactly what adding a friend did, while every other relationship call went
// through. The gateway and the REST side were also disagreeing with each
// other, saying 363557 and 9999.
//
// Taken from a capture of the real client. Worth refreshing when it starts
// being refused again.
const int DISCORD_BUILD_NUMBER = 594031;

namespace api
{
    void init();
    void shutdown();

    void set_token(const char* token);
    const char* token();
    bool has_token();

    // ---- raw, blocking; call from a job thread ----
    // location fills X-Context-Properties, which discord requires on the
    // relationship endpoints and ignores everywhere else. Where in the
    // interface the action was taken - "Add Friend", "Friends".
    bool call(const char* method, const char* path, const char* json_body,
              http_response* out, const char* location = 0);
    bool call_absolute(const char* method, const char* url, const char* json_body,
                       http_response* out, const char* location = 0);

    // Same, signed by a token other than the one that is signed in. Analytics
    // about a voice call needs it: the call belongs to the account that opened
    // it even after the client has been switched to another one.
    bool call_as(const char* method, const char* path, const char* json_body,
                 http_response* out, const char* auth_token);

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
    // A friend request, optionally carrying a captcha token.
    //
    // Discord answers a request it considers unfamiliar with a 400 asking for
    // a captcha. Solving one is the person's job, not this client's: the token
    // they come back with is passed through here and nothing more.
    void send_friend_request(const char* username, const char* captcha_key = 0,
                             const char* captcha_rqtoken = 0);

    // What the last refusal asked for. Empty when nothing is pending.
    // The identifiers this run of the client reports in its properties.
    // Analytics has to quote the same ones or the two describe different
    // sessions.
    const char* heartbeat_session_id();
    const char* launch_signature();

    const char* captcha_sitekey();
    const char* captcha_rqtoken();
    void clear_captcha();
    void accept_friend_request(snowflake user_id);
    // Declines an incoming request, cancels an outgoing one, or removes a friend.
    void remove_relationship(snowflake user_id);
    void block_user(snowflake user_id);
    void join_guild_by_invite(const char* invite_code);
    void leave_guild(snowflake guild_id);

    // ---- links other people use ------------------------------------------
    //
    // Both of these belong to a channel rather than to the server: an invite
    // points at somewhere to arrive, and a webhook posts into somewhere.
    //
    // The answer is a url and nothing else in the client has any use for it, so
    // it lands in last_link() rather than in the store. One slot, because only
    // one of these is ever being made at a time and showing the previous one
    // beside a new request would be worse than showing nothing.
    //
    // max_age is in seconds and zero means it never expires; max_uses zero
    // means unlimited. temporary throws the newcomer out again when they
    // disconnect unless somebody gives them a role in the meantime.
    void create_invite(snowflake channel_id, int max_age, int max_uses, bool temporary);
    void create_webhook(snowflake channel_id, const char* name);

    const char* last_link();
    void clear_last_link();
    void set_last_link(const char* url);

    // What already exists. Webhooks are not gateway state - nothing announces
    // them and nothing keeps them up to date - so they are fetched on demand
    // and held here until the next fetch.
    struct webhook_row
    {
        snowflake id;
        snowflake channel_id;
        char name[96];
    };

    // The invites that already exist. Same story as webhooks: nothing on the
    // gateway announces one, so the list is asked for and held here.
    //
    // Listing needs Manage Server, and a client without it gets a plain 403 -
    // which is why an empty list and a refused one have to be told apart.
    struct invite_row
    {
        char code[16];
        snowflake channel_id;
        char inviter[64];
        int uses;
        int max_uses;
        int max_age;          // seconds; zero never expires
        bool temporary;
    };

    void fetch_invites(snowflake guild_id);
    void revoke_invite(const char* code, snowflake guild_id);

    int invites(invite_row* out, int cap);
    bool invites_loading();
    bool invites_forbidden();

    // ---- audit log and bans ----------------------------------------------
    //
    // Neither is gateway state, so both are fetched on demand and held here.
    // Both need Manage Server (bans also answer to Ban Members), and a refusal
    // is a plain 403 that has to be told apart from an empty result.
    struct audit_row
    {
        snowflake id;
        int action;             // discord's own numbering, see audit_action_name
        snowflake actor;        // who did it
        snowflake target;
        char reason[128];
    };

    struct ban_row
    {
        snowflake user_id;
        char name[64];
        char reason[160];
    };

    void fetch_audit_log(snowflake guild_id);
    int audit_log(audit_row* out, int cap);
    bool audit_loading();
    bool audit_forbidden();

    void fetch_bans(snowflake guild_id);
    int bans(ban_row* out, int cap);
    bool bans_loading();
    bool bans_forbidden();

    void unban(snowflake guild_id, snowflake user_id);

    // ---- moderation ----
    //
    // Every one of these is refused by the server without the right
    // permission, so the checks in the interface are there to keep pointless
    // requests off the screen rather than to enforce anything.

    // Out of whatever voice channel they are in. Discord has no separate call
    // for this: a disconnect is a move to no channel at all.
    void voice_kick(snowflake guild_id, snowflake user_id);

    // Into another one. The same field carrying a channel instead of null,
    // and it only works on somebody already sitting in voice - discord will
    // not pull anybody in who is not connected.
    void voice_move(snowflake guild_id, snowflake user_id, snowflake channel_id);

    // Silenced for everybody, not just for us - unlike the per person volume
    // in the voice menu, which never leaves this machine.
    void set_server_mute(snowflake guild_id, snowflake user_id, bool muted);

    // Minutes from now, or 0 to lift one. Discord's own ceiling is 28 days.
    void timeout_member(snowflake guild_id, snowflake user_id, int minutes);

    // delete_message_seconds asks the server to sweep up what they said in
    // the run-up; 0 leaves their messages alone.
    void ban_member(snowflake guild_id, snowflake user_id, int delete_message_seconds);

    // Plain language for an action number. Unknown ones come back as the
    // number itself rather than as nothing, because a log line that says only
    // who and when is still worth reading.
    const char* audit_action_name(int action, char* scratch, int cap);

    void fetch_webhooks(snowflake guild_id);
    void delete_webhook(snowflake webhook_id, snowflake guild_id);

    // Copies at most cap rows out under the lock; returns how many there were.
    int webhooks(webhook_row* out, int cap);
    bool webhooks_loading();

    // ---- channels ---------------------------------------------------------
    //
    // type is discord's own numbering: 0 text, 2 voice, 4 category. parent_id
    // is the category to put it in, or zero for none - and is ignored when
    // making a category, which cannot sit inside another.
    void create_channel(snowflake guild_id, const char* name, int type, snowflake parent_id);
    void delete_channel(snowflake channel_id);

    // The channels of one server in the order they should appear. Sent whole
    // rather than as one moved entry: a position only means anything next to
    // the others, and renumbering the rest is otherwise the server's guess.
    //
    // reparented names the one channel whose category changed, if any - the
    // request carries parent_id only for that one, because sending it for
    // every channel would move them all into whatever was passed.
    void reorder_channels(snowflake guild_id, const snowflake* ordered, int count,
                          snowflake reparented, snowflake parent_id);

    // A brand new server. Discord names the first channel itself.
    void create_guild(const char* name);

    // The server's own name and icon. An empty path takes the icon off.
    //
    // Sent to /guilds/{id}, not to the /guilds/{id}/profile a capture shows the
    // real client using: that one carries every field of the server identity on
    // every write and reads as a replacement, so a partial body would blank
    // whatever was left out. This one patches what it is given.
    void update_guild_name(snowflake guild_id, const char* name);
    void update_guild_icon(snowflake guild_id, const wchar_t* path);

    // One channel's permission overwrite for one role or one member. The two
    // masks are independent: a bit in neither is inherited from the server, and
    // a bit in both is a contradiction the server resolves as deny.
    //
    // is_role picks which of the two an id means - the same number space holds
    // both, and discord tells them apart only by this field.
    void set_channel_overwrite(snowflake channel_id, snowflake target_id, bool is_role,
                               unsigned long long allow, unsigned long long deny);

    // Removes the overwrite entirely, which is not the same as clearing both
    // masks: it puts the target back to inheriting everything.
    void clear_channel_overwrite(snowflake channel_id, snowflake target_id);

    // ---- roles -----------------------------------------------------------
    //
    // Every one of these needs Manage Roles, and the server refuses any of them
    // that touches a role at or above the caller's own highest - a rule worth
    // knowing about here, because it comes back as a plain 403 with nothing to
    // say which of the two reasons applied.
    //
    // Nothing is written into the store on success: the gateway sends
    // GUILD_ROLE_CREATE, GUILD_ROLE_UPDATE, GUILD_ROLE_DELETE and
    // GUILD_MEMBER_UPDATE for all of it, and applying the change twice would
    // leave the client disagreeing with the server whenever a call quietly
    // failed.
    void add_member_role(snowflake guild_id, snowflake user_id, snowflake role_id);
    void remove_member_role(snowflake guild_id, snowflake user_id, snowflake role_id);

    void create_role(snowflake guild_id, const char* name);
    void delete_role(snowflake guild_id, snowflake role_id);

    // Colour is 0xRRGGBB, and zero means "no colour" rather than black - which
    // is what discord means by it too.
    void edit_role(snowflake guild_id, snowflake role_id, const char* name,
                   unsigned long long permissions, unsigned int color,
                   bool hoist, bool mentionable);

    // Result of the most recent user-triggered action, for the UI to display.
    const char* last_error();
    void clear_last_error();
    void set_last_error(const char* text);
}
