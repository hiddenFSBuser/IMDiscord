#pragma once
#include "discord/types.h"

// Discord's own analytics, sent back to it.
//
// The official client reports what the person is doing to POST /science. A
// client that never sends any of it looks unlike every other client on the
// account, and that shows up as captcha demands on ordinary actions and, on
// fresh accounts, as bans.
//
// Four settings, because "send telemetry or not" is not the real question. A
// capture of the official client shows fifty three distinct event types, and
// they are not alike: reporting that somebody opened the audit log is the same
// kind of fact as opening it, while reporting that they moved the pointer over
// a picture, or began speaking, is watching them. The middle setting is the
// line between those two.

enum science_mode
{
    SCIENCE_OFF = 0,          // nothing is sent
    SCIENCE_MINIMAL,          // only what discord gates behind a CAPTCHA
    SCIENCE_ACCOMPANYING,     // deliberate actions: what was opened, joined, changed
    SCIENCE_ALL,              // everything, including hovering and speaking
};

// Which setting an event needs before it goes out. An event is sent when the
// chosen mode is at least its tier.
enum science_tier
{
    // What accompanies the two actions discord answers with a CAPTCHA when it
    // does not recognise the client: adding a friend, and using an invite.
    // Both are refused outright without their chain, so this tier is the one
    // that is not really optional.
    TIER_ESSENTIAL = SCIENCE_MINIMAL,
    TIER_ACTION = SCIENCE_ACCOMPANYING,
    TIER_BEHAVIOUR = SCIENCE_ALL,
};

namespace science
{
    void init();

    // The analytics token from READY. Without it nothing can be sent: the
    // endpoint identifies the session by this and not by the auth token.
    void set_token(const char* token);
    const char* analytics_token();

    // The account a call belongs to, captured when it starts.
    //
    // A call survives switching accounts - the gateway that opened it is kept
    // alive on purpose - so voice events belong to whoever is actually sitting
    // in the channel, not to whoever the client is showing now. Reporting them
    // on the new account describes somebody in a voice channel they are not in.
    void set_voice_identity(const char* auth_token, const char* analytics_token);
    void clear_voice_identity();

    // Sends whatever has been queued. Called on the way out so a batch is not
    // lost, and by the flush thread on its own schedule.
    void flush();
    void shutdown();
    bool ready();

    science_mode mode();
    void set_mode(science_mode m);
    const char* mode_name(science_mode m);
    const char* mode_note(science_mode m);

    // ---- the friend chain (TIER_ESSENTIAL) ---------------------------------
    // Arriving at the friends screen and switching tabs on it are two events
    // with the same body: discord calls the first viewed and the second
    // clicked. Only the add-friend tab belongs to the narrow setting - that
    // one is part of what makes a friend request acceptable. Which of the
    // other tabs somebody is reading is watching them, and sits with the rest
    // of the watching.
    void friends_list_viewed(const char* tab);
    void friends_list_clicked(const char* tab);
    void add_friend_input_clicked();

    // The settings screen discord points people at when it will not show
    // something in place - blocked accounts are behind it - reported as the
    // visit it pretends to be, with the notice it shows on arrival.
    void blocked_settings_viewed();

    // ---- the invite chain (TIER_ESSENTIAL) -------------------------------
    //
    // Seven events for what looks like one action, because to discord it is
    // not one action: a modal opened on its landing screen, a step taken to
    // the join screen, a code typed, a code resolved, and only then a join. A
    // client that sends the last request alone has done something no person
    // sitting in front of the official client could have done, and discord
    // answers that with a CAPTCHA.

    // The "+" box opening, and the step from its landing screen to the join
    // screen. Our box is one screen where discord's is two; somebody pressing
    // "+" and then typing a code has been through both of them.
    void guild_add_opened();
    void guild_add_join_step();

    // A code entered, before anything is asked of the server.
    void invite_opened(const char* code);

    // What came back from GET /invites/{code}. Reported whether or not it
    // resolved: a dead code is a thing that happens to people, and hiding it
    // makes the chain look stranger rather than safer.
    struct invite_result
    {
        const char* code;
        const char* input_value;     // what was typed, link and all

        snowflake guild_id;
        snowflake channel_id;
        snowflake inviter_id;

        int channel_type;
        int status_code;
        int size_total;              // members, as the preview reports them
        int size_online;

        bool resolved;
        bool user_is_member;
        bool user_banned;
    };

    void invite_resolved(const invite_result* r);

    // ---- deliberate actions (TIER_ACTION) --------------------------------
    void channel_opened(snowflake channel_id, snowflake guild_id);
    void guild_viewed(snowflake guild_id);
    void dm_list_viewed();
    void settings_pane_viewed(const char* pane);

    // The server settings screens, which discord reports one impression event
    // per screen for: "audit_log", "bans", "roles", "invites_v2", "profile".
    void guild_settings_viewed(const char* which, snowflake guild_id);

    void user_profile_viewed(snowflake user_id);
    void join_voice_channel(snowflake channel_id, snowflake guild_id);
    void call_button_clicked(snowflake channel_id);
    void ack_messages(snowflake channel_id);

    // ---- watching (TIER_BEHAVIOUR) ---------------------------------------
    void image_hovered();
    void start_speaking(snowflake channel_id, snowflake guild_id);
    void start_listening(snowflake channel_id, snowflake guild_id);
    void input_mute_toggled(bool muted, snowflake channel_id, snowflake guild_id);
    void media_viewer_closed();
    void voice_connection_success(snowflake channel_id, snowflake guild_id);
}
