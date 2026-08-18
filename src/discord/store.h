#pragma once
#include "types.h"
#include "net/json.h"

// In-memory cache of everything the gateway and REST calls hand us.
//
// Strings live in one arena and are never individually freed; objects are
// arena-allocated too and referenced by pointer, so the maps can relocate
// freely. The gateway thread writes and the UI thread reads, guarded by a
// single lock that the UI holds for the duration of a frame.
namespace store
{
    void init();
    void shutdown();

    // Throws the whole cache away without tearing the store down, for a switch
    // to another account. Every pointer handed out before this is dead after.
    void reset();

    void lock();
    void unlock();

    struct guard
    {
        guard() { lock(); }
        ~guard() { unlock(); }
    };

    const char* intern(const char* s, int len = -1);

    // ---- identity ----
    snowflake self_id();
    void set_self_id(snowflake id);
    duser* self();

    // ---- users ----
    duser* find_user(snowflake id);
    duser* upsert_user(const jval* v);

    // ---- channels ----
    dchannel* find_channel(snowflake id);
    dchannel* upsert_channel(const jval* v, snowflake guild_id);
    void remove_channel(snowflake id);

    // ---- guilds ----
    dguild* find_guild(snowflake id);
    dguild* upsert_guild(const jval* v);
    // Puts a guild's channels into the order every other client shows them in.
    // Discord sends them in no particular order and expects the client to sort.
    void sort_guild_channels(dguild* g);
    // One entry of a member list: the user, their nickname and whatever
    // presence came with it.
    void add_guild_member(dguild* g, const jval* member);

    // A role created or changed on the server. The same call serves both, so
    // the dispatcher does not have to know which it is holding.
    void upsert_role(dguild* g, const jval* role);
    void remove_role(dguild* g, snowflake role_id);
    void remove_guild(snowflake id);

    // ---- messages ----
    dmessage* upsert_message(const jval* v);
    dmessage* find_message(dchannel* ch, snowflake id);
    void remove_message(snowflake channel_id, snowflake message_id);
    // Keeps it in place and flags it. What somebody deleted stays on screen,
    // struck through, which is the whole point of holding an archive.
    void mark_message_deleted(snowflake channel_id, snowflake message_id);
    // Inserts a locally created message so the UI can echo it immediately.
    dmessage* add_pending_message(snowflake channel_id, const char* content, snowflake local_id);

    // ---- presence ----
    // Accepts any of the shapes discord uses: a READY presence, an entry of
    // merged_presences, or a PRESENCE_UPDATE payload.
    void apply_presence(const jval* p);

    // ---- relationships ----
    void set_relationship(snowflake user_id, int type, const char* nickname);
    void remove_relationship(snowflake user_id);
    int relationship_type(snowflake user_id);
    const ulist<drelationship>& relationships();

    // ---- voice ----
    void set_voice_state(const jval* v, snowflake guild_id);
    // Null when the user is not in any voice channel we know about.
    const dvoice_state* find_voice_state(snowflake user_id);
    void clear_voice_states_for_channel(snowflake channel_id);

    // Who a direct-message or group call is still ringing.
    //
    // Separate from the voice states because it is a different fact: a state
    // says somebody is in the call, this says somebody has been asked to join
    // and has not answered. Without it an unanswered call looks identical to
    // no call at all.
    void set_call_ringing(snowflake channel_id, const ulist<snowflake>* users);
    void clear_call_ringing(snowflake channel_id);
    bool call_is_ringing(snowflake channel_id);
    const ulist<dvoice_state>& voice_states();

    // ---- permissions ----
    //
    // What a member is allowed to do in a channel, worked out the way discord
    // does it: the roles they hold combined, then the channel's own table
    // applied over the top. The owner and anybody with administrator short
    // circuit to everything.
    unsigned long long member_permissions(const dguild* g, snowflake user_id,
                                          const dchannel* c);

    // Whether one member may be moderated by another: discord refuses when the
    // target's highest role sits at or above the actor's, whatever permissions
    // the actor holds. The owner is above everyone and answerable to no one.
    bool outranks(const dguild* g, snowflake actor_id, snowflake target_id);

    // Whether a channel should appear to that member at all. The member list
    // on the right is the set of people for whom this is true of the channel
    // being looked at - not everyone in the server.
    bool can_view_channel(const dguild* g, snowflake user_id, const dchannel* c);

    dmember* find_member(dguild* g, snowflake user_id);

    // The role that decides what colour a name is drawn in: the highest one
    // the member holds that has a colour set. Returns null when they have
    // none, which is the common case.
    const drole* member_color_role(const dguild* g, const dmember* m);

    // The role whose section the member belongs under in the list: the
    // highest one they hold that the server marked as hoisted.
    const drole* member_hoist_role(const dguild* g, const dmember* m);

    const drole* find_role(const dguild* g, snowflake role_id);
    void users_in_voice(snowflake channel_id, ulist<snowflake>* out);

    // Everything currently held, for the snapshot that keeps the client
    // readable with no connection. The maps have no iteration of their own.
    void all_users(ulist<snowflake>* out);
    void all_channels(ulist<snowflake>* out);

    // ---- read state ----
    //
    // Fed from READY and kept up to date by message dispatches. Only ever
    // shown for direct messages: a server with a busy channel would otherwise
    // sit there permanently wearing a number nobody asked for.
    void apply_read_state(const jval* entry);
    void mark_channel_read(snowflake channel_id);

    // ---- typing ----
    //
    // Discord announces that somebody started, never that they stopped, so
    // each note carries its own expiry and the list is swept as it is read.
    void note_typing(snowflake channel_id, snowflake user_id);
    int typing_in(snowflake channel_id, snowflake* out, int cap);

    // Counts a freshly arrived message against the channel it landed in,
    // unless this account sent it or is already reading there.
    void note_incoming(const jval* message);

    // Which channel the client is showing, so that what arrives in it is not
    // announced as unread a frame before it is read.
    void set_open_channel(snowflake channel_id);

    // ---- guild ordering ----
    //
    // Discord keeps the order in the account's own settings, not in anything
    // about the guilds themselves, so it has to be read out of READY and
    // applied over the arrival order.
    void apply_guild_order(const ulist<snowflake>* ids);
    void move_guild(snowflake id, int to_index);

    // What a guild's people are doing right now, for the card under the
    // cursor. Counts every voice channel of the guild at once.
    struct guild_voice_summary
    {
        int in_voice;
        int streaming;
    };
    guild_voice_summary voice_summary(snowflake guild_id);

    // ---- ordered views (rebuilt on demand) ----
    const ulist<snowflake>& guild_order();
    const ulist<snowflake>& dm_order();
    void touch_dm_order();

    // ---- auth sessions ----
    // The account's sign-ins from /auth/sessions, fetched on demand for the
    // settings popup rather than kept fresh: nobody needs this live.
    int sessions_state();                 // 0 = never asked, 1 = loading, 2 = ready, 3 = failed
    void set_sessions_loading();
    void set_sessions(const jval* arr);   // the user_sessions array
    void set_sessions_failed();
    const ulist<dsession>& sessions();

    // Bumped whenever anything the sidebar shows changes.
    unsigned int revision();
    void bump_revision();
}
