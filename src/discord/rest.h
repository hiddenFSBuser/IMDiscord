#pragma once
#include "types.h"
#include "net/http.h"

struct jval;

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

    // A bot token is a different kind of credential, not just a different
    // string: it is sent with a "Bot " prefix, it identifies with intents
    // rather than with a browser's properties, and none of the analytics
    // belongs to it. Which one this is has to be known before the first
    // request, so it is chosen at sign-in rather than discovered.
    void set_token(const char* token, bool bot = false);
    const char* token();
    bool has_token();
    bool is_bot();

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
    bool verify_token(const char* token_value, bool is_bot, char* out_error, int error_cap);

    // ---- async ----
    void fetch_messages(snowflake channel_id, snowflake before_id);

    // The other direction: everything newer than this id, oldest first. Used
    // to close a hole - the stretch between what was saved on an earlier visit
    // and what was fetched on this one, which nothing else ever asks for.
    // ---- is anything actually missing here? ------------------------------
    //
    // Between two messages the client holds, either the archive already knows
    // the stretch arrived in one piece, or nobody knows. Guessing from how
    // much time passed marks a quiet night as a hole and a busy minute as
    // continuous, so instead discord is asked what follows: one message, one
    // small request, once ever. The answer goes into the archive's ranges when
    // it is "nothing", so the question is never repeated.
    enum gap_state
    {
        GAP_UNKNOWN = 0,   // nobody has looked
        GAP_CHECKING,      // asked, waiting
        GAP_NONE,          // discord says these two are neighbours
        GAP_REAL,          // something is in between
    };

    int gap_status(snowflake channel_id, snowflake after_id);
    void check_gap(snowflake channel_id, snowflake after_id, snowflake until_id);

    // until_id is the message on the far side of the hole, or zero when
    // there is none. Reaching it - or running out before it - is what proves
    // the stretch is whole, and that gets written down so the hole is not
    // offered again.
    //
    // budget caps one pass at this many messages, pulled page by page (100
    // per request, which is discord's maximum). Zero or negative closes the
    // hole completely, up to a safety cap inside the job: a "17 days" hole
    // on a busy server is thousands of messages, and one page of fifty
    // barely scratches it.
    void fetch_messages_after(snowflake channel_id, snowflake after_id, snowflake until_id,
                              int budget = 0);
    // The mirror walk: from the hole's far side backwards, down to stop_id
    // (the near side) or the budget. Same paging, same progress slot.
    void fetch_gap_backward(snowflake channel_id, snowflake before_id, snowflake stop_id,
                            int budget = 0);
    // Progress of a hole fill in flight, for its marker to show. Returns
    // false when no fill is running for the channel; wanted is zero when
    // the fill runs to the far side rather than to a count.
    bool fill_progress(snowflake channel_id, int* fetched, int* wanted);
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
    // One channel by id, for when something arrives in a channel this client
    // has never been told about. A bot is told about none of its direct
    // conversations, so for one this is the only way to learn what they are.
    void fetch_channel(snowflake channel_id);
    // One guild member by id (roles, nickname, join date included).
    void fetch_guild_member(snowflake guild_id, snowflake user_id);
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
    // Rewrites one of our own. Discord takes only the text; an empty one is
    // refused rather than treated as a delete.
    void edit_message(snowflake channel_id, snowflake message_id, const char* content);
    // Presses a button or answers a menu that a bot put under a message. The
    // values are the chosen options and are ignored for a button.
    void use_component(snowflake guild_id, snowflake channel_id, snowflake message_id,
                       snowflake application_id, int component_type,
                       const char* custom_id, const char* const* values, int value_count,
                       int message_flags);

    // ---- bot forms (modals) ----
    // A button whose answer is a type-9 interaction response: a form the bot
    // describes on the spot. The worker parses it into this and the interface
    // shows it; the filled copy comes back through submit_modal.
    struct modal_option
    {
        char label[100];
        char value[100];
        char description[100];
    };

    struct modal_field
    {
        int kind;               // 4 text, 3/5/6/7/8 a menu, anything else echoed back untouched
        char custom_id[128];
        char label[160];
        char placeholder[256];
        bool required;
        int min_len;
        int max_len;
        int style;              // text only: 1 one line, 2 paragraph

        char text[4096];        // the answer being typed

        modal_option options[25];
        int option_count;
        int selected;           // menu choice, -1 for none
        int min_values;
        int max_values;
    };

    struct modal_form
    {
        char title[160];
        char custom_id[128];
        // data.id from the interaction response, when it carries one. A
        // string in the protocol, not a number; empty means the submit
        // carries no id at all.
        char submit_id[32];

        snowflake guild_id;
        snowflake channel_id;
        snowflake application_id;

        modal_field fields[12];
        int field_count;
    };

    // Takes a form the worker parsed, if one arrived. True when out was
    // filled and a popup should open for it.
    bool take_pending_modal(modal_form* out);
    // Sends the filled form back (interaction type 5).
    void submit_modal(const modal_form* form);
    // A form that arrived over the gateway (INTERACTION_MODAL_CREATE) rather
    // than in a POST response. Opens it only when its nonce matches a click
    // this client sent: the same account in a browser must not pop a window
    // over here.
    void handle_modal_dispatch(const jval* d);

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
                             const char* captcha_rqtoken = 0, const char* note = 0);

    // What the last refusal asked for. Empty when nothing is pending.
    // The identifiers this run of the client reports in its properties.
    // Analytics has to quote the same ones or the two describe different
    // sessions.
    const char* heartbeat_session_id();
    const char* launch_signature();

    const char* captcha_sitekey();
    const char* captcha_rqtoken();
    const char* captcha_session();
    void clear_captcha();
    // Accepts an incoming request. confirm answers the "is this a stranger"
    // question discord asks about people it does not recognise: without it
    // the accept comes back 400/80013.
    void accept_friend_request(snowflake user_id, bool confirm);    // Declines an incoming request, cancels an outgoing one, or removes a friend.
    void remove_relationship(snowflake user_id);
    void block_user(snowflake user_id);

    // ---- group DMs ----
    // Adding someone to a 1-1 conversation turns it into a group (the answer
    // carries the new channel id); adding to a group just grows it. Done one
    // member per request, in order, the way the official client does it.
    void group_add_members(snowflake channel_id, const snowflake* user_ids, int count);
    // A fresh group from a bare list of friends.
    void group_create(const snowflake* user_ids, int count);
    void group_remove_member(snowflake channel_id, snowflake user_id);
    // Leaving a group and hiding a 1-1 conversation are the same request;
    // the server tells them apart. Messages stay on disk either way.
    void close_conversation(snowflake channel_id);
    // Hands the crown to someone else. A plain PATCH, no codes involved.
    void group_transfer_owner(snowflake channel_id, snowflake user_id);

    // Picks up a channel a group operation just produced, for the interface
    // to open. True once per channel.
    bool take_created_channel(snowflake* out);

    // ---- pins ----
    // The pinned messages of a channel, kept in store::channel_pins. Pinning
    // and unpinning only report refusals: the CHANNEL_PINS_UPDATE dispatch
    // that follows refreshes the list on its own.
    void fetch_pins(snowflake channel_id);
    void pin_message(snowflake channel_id, snowflake message_id);
    void unpin_message(snowflake channel_id, snowflake message_id);

    // ---- forums ----
    // Posts: the search-ordered thread list with first messages, page by
    // page (25). The offset is whatever is already held. history_loading on
    // the forum channel is the in-flight guard, the way it is for history.
    void fetch_forum_posts(snowflake forum_id);
    // A new post: name, starter text and tag ids. The result is picked up
    // once with take_forum_post_result; forum_post_busy is set meanwhile
    // (separate from history_loading, which the post list uses).
    void create_forum_post(snowflake forum_id, const char* name, const char* content,
                          const snowflake* tags, int tag_count);
    bool forum_post_busy(snowflake forum_id);
    // Takes the creation result for the forum, once. False when there is
    // none yet (or it belongs to another forum); ok tells success, thread
    // the new post's id, error the server's words on failure.
    bool take_forum_post_result(snowflake forum_id, bool* ok, snowflake* thread_id,
                               char* error, int error_cap);
    // Membership in a thread: idempotent, fire and forget. Reading usually
    // works without it; writing does not.
    void join_thread(snowflake thread_id);
    // ---- an invite sitting in a message ----------------------------------
    //
    // Discord draws those as a panel with the server on it rather than as a
    // link, and asks the server about the code to fill it in. Resolved once
    // per code and kept, because the same link posted in five places is one
    // server and one request.

    struct invite_card
    {
        char code[16];

        snowflake guild_id;
        char guild_name[104];
        char guild_icon[64];

        snowflake channel_id;
        char channel_name[104];
        int channel_type;

        snowflake inviter_id;
        int size_online;
        int size_total;

        // The request has come back, and what it said. An invite that has
        // expired resolves to nothing and the panel says so rather than
        // sitting empty for ever.
        bool done;
        bool ok;
        bool already_member;
    };

    // Asks about a code once. Where it is - the message and its channel - is
    // for the analytics that goes with it, which discord expects to name the
    // panel rather than just the invite.
    void resolve_invite(const char* code, snowflake message_id,
                        snowflake channel_id, int channel_type, snowflake location_guild);

    // What is known about a code, or false if nothing has been asked yet.
    bool invite_card_of(const char* code, invite_card* out);

    // Joining from the panel, which is a different request from joining a
    // pasted link: it names the message the panel is on.
    void join_invite_from_message(const char* code, snowflake message_id,
                                  snowflake channel_id, int channel_type,
                                  snowflake location_guild);

    void join_guild_by_invite(const char* invite_code);
    void leave_guild(snowflake guild_id);
    // Owner only, and gone for good: discord keeps no copy to restore from.
    void delete_guild(snowflake guild_id);

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

    // Makes an invite to the server `from_channel` belongs to and sends it as
    // a message into `to_channel`. One job rather than two, because the link
    // does not exist until the first request answers.
    void send_guild_invite(snowflake from_channel, snowflake to_channel);
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
        // What lets anything post through it, and the reason a webhook url
        // is worth guarding: it needs no account behind it. Discord only
        // hands it over to somebody who could read it from the settings
        // page anyway, so it arrives with the listing or not at all.
        char token[128];
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

    // Out of the server, but able to come back with a fresh invite. The
    // difference from a ban, and the reason both exist.
    void kick_member(snowflake guild_id, snowflake user_id);
    void unban(snowflake guild_id, snowflake user_id);

    // ---- handing the server over ------------------------------------------
    //
    // Two steps, and the second cannot be reached without the first: discord
    // mails a six digit code to the owner's address and will not move the
    // crown without it. Nothing here can read that mail, so the code is typed
    // in by the person - which is the point of it.
    void request_ownership_code(snowflake guild_id);
    void transfer_ownership(snowflake guild_id, snowflake user_id, const char* code);

    // Whether the code has been asked for on this guild, so the box can say
    // what to do next rather than showing both steps at once.
    bool ownership_code_sent();
    // How long ago it was mailed, or 0 if none has been.
    unsigned long long ownership_code_age_ms();
    void clear_ownership_state();

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

    // ---- the screen a big server shows on the way in ----------------------
    //
    // Two separate things that arrive together and read as one panel: the
    // roles a server offers to pick from, and the rules it asks to be agreed
    // to. Discord calls the first onboarding and the second member
    // verification, and a server can have either, both or neither.

    struct onboard_option
    {
        snowflake id;
        char title[128];
        char description[256];

        // A custom emoji has an id and is fetched as a picture; a plain one
        // is the characters themselves.
        snowflake emoji_id;
        bool emoji_animated;
        char emoji_name[64];

        snowflake roles[8];
        int role_count;
    };

    struct onboard_prompt
    {
        snowflake id;
        char title[128];

        bool single_select;
        bool required;
        // Shown on the way in. The others are the ones a server keeps in its
        // channel list for later, and they are not part of this panel.
        bool in_onboarding;

        // Where this prompt's options begin in the flat option list, and how
        // many there are. Flat because the two are fetched together and are
        // only ever read together.
        int first_option;
        int option_count;
    };

    // The server the last join by invite landed in, once. Cleared by the
    // read, so the panel it opens opens once.
    snowflake take_joined_guild();

    void fetch_onboarding(snowflake guild_id);

    // The same request, but only to find out whether the panel is worth
    // showing. Answers through take_onboarding_prompt rather than by filling
    // the window, so it can be fired at a server nobody has asked about.
    void probe_onboarding(snowflake guild_id);
    snowflake take_onboarding_prompt();

    // Whether this account has already answered the panel. A server keeps its
    // prompts forever; only this says whether it is still asking.
    bool onboarding_answered();
    void fetch_rules(snowflake guild_id);

    bool onboarding_loading();
    // Whether the last fetch found anything worth showing.
    bool onboarding_ready();
    bool rules_ready();

    int onboarding_prompts(onboard_prompt* out, int cap);
    int onboarding_options(onboard_option* out, int cap);

    // The rules themselves, copied out one line at a time: the list behind
    // them is refilled by a worker thread and a pointer into it would not
    // survive the next fetch.
    int rules_count();
    void rules_line(int index, char* out, int cap);
    void rules_description(char* out, int cap);

    // The chosen options, by id. Everything else the request needs - which
    // prompts were shown, which options were on screen - is worked out from
    // what was fetched.
    void submit_onboarding(snowflake guild_id, const snowflake* chosen, int count);

    // Sends the rules form back with every field answered yes. There is only
    // one kind of field discord puts in it, and agreeing is the only answer
    // it takes.
    void accept_rules(snowflake guild_id);

    void fetch_webhooks(snowflake guild_id);
    void delete_webhook(snowflake webhook_id, snowflake guild_id);

    // Copies at most cap rows out under the lock; returns how many there were.
    int webhooks(webhook_row* out, int cap);

    // Posts as the webhook rather than as this account. The name and the
    // picture are whatever is passed - that is the whole of what a webhook
    // is - and both may be null to use the ones it was made with.
    void send_via_webhook(snowflake webhook_id, const char* token, const char* content,
                          const char* username, const char* avatar_url);
    bool webhooks_loading();

    // ---- channels ---------------------------------------------------------
    //
    // type is discord's own numbering: 0 text, 2 voice, 4 category. parent_id
    // is the category to put it in, or zero for none - and is ignored when
    // making a category, which cannot sit inside another.
    void create_channel(snowflake guild_id, const char* name, int type, snowflake parent_id);
    void delete_channel(snowflake channel_id);
    // Categories are channels too, so this renames one of those as well.
    void rename_channel(snowflake channel_id, const char* name);

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
