#pragma once
#include "uarena.h"

typedef unsigned long long snowflake;
const snowflake SNOWFLAKE_INVALID = 0;

// Discord epoch, 2015-01-01T00:00:00Z, in milliseconds.
const unsigned long long DISCORD_EPOCH_MS = 1420070400000ULL;

inline unsigned long long snowflake_time_ms(snowflake id)
{
    return (id >> 22) + DISCORD_EPOCH_MS;
}

// Wall clock in unix milliseconds. GetTickCount64 counts since boot and says
// nothing about the date, which is what a timeout has to be written in.
inline unsigned long long unix_now_ms()
{
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);

    unsigned long long ticks = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    if (ticks < 116444736000000000ULL) return 0;

    return (ticks - 116444736000000000ULL) / 10000ULL;
}

enum channel_type
{
    CH_GUILD_TEXT = 0,
    CH_DM = 1,
    CH_GUILD_VOICE = 2,
    CH_GROUP_DM = 3,
    CH_CATEGORY = 4,
    CH_ANNOUNCEMENT = 5,
    CH_ANNOUNCEMENT_THREAD = 10,
    CH_PUBLIC_THREAD = 11,
    CH_PRIVATE_THREAD = 12,
    CH_STAGE = 13,
    CH_FORUM = 15,
};

enum relationship_type
{
    REL_NONE = 0,
    REL_FRIEND = 1,
    REL_BLOCKED = 2,
    REL_INCOMING = 3,   // they sent us a request
    REL_OUTGOING = 4,   // we sent them a request
};

enum presence_status
{
    STATUS_OFFLINE = 0,
    STATUS_ONLINE,
    STATUS_IDLE,
    STATUS_DND,
};

struct duser
{
    snowflake id;
    const char* username;
    const char* global_name;    // display name, may be null
    const char* discriminator;  // "0" for migrated accounts
    const char* avatar;         // hash, may be null
    const char* banner;
    const char* bio;
    unsigned int accent_color;
    int public_flags;
    int premium_type;
    bool bot;
    bool profile_loaded;
    unsigned char status;

    // Account-private fields. Discord sends them only on the owner's own
    // object (/users/@me, READY), so for anybody else they stay null/false.
    const char* email;
    const char* phone;
    bool verified;
    bool mfa_enabled;

    const char* display_name() const
    {
        if (global_name && global_name[0]) return global_name;
        return username ? username : "unknown";
    }
};

// One entry of /auth/sessions: somewhere the account is signed in.
struct dsession
{
    char id_hash[64];
    char os[48];
    char platform[48];
    char location[96];
    char last_used[40];     // ISO8601 as it came
};

struct dattachment
{
    snowflake id;
    const char* filename;
    const char* url;
    const char* proxy_url;
    const char* content_type;
    unsigned int size;
    int width;
    int height;

    // Something this client can actually play. Only mp4: the decoder behind
    // it is the one windows ships for H.264, and claiming to play a webm
    // would just be a black rectangle.
    bool is_video() const
    {
        if (content_type && ccsncmpf(content_type, "video/mp4", 9) == 0) return true;
        if (!filename) return false;

        int n = (int)ccslenf(filename);
        if (n < 4) return false;
        const char* ext = filename + n - 4;
        return (cctolower(ext[0]) == '.' && cctolower(ext[1]) == 'm' &&
                cctolower(ext[2]) == 'p' && ext[3] == '4');
    }

    // Note this stays true for a video: discord gives one a width and a
    // height, and drawing it down the image path is what puts a still and a
    // download button on it. Whoever wants the player asks is_video() first.
    bool is_image() const
    {
        if (width > 0 && height > 0) return true;
        if (!content_type) return false;
        return ccsncmpf(content_type, "image/", 6) == 0;
    }
};

struct dembed
{
    const char* title;
    const char* description;
    const char* url;
    // The proxied copy discord serves, and the address on the original site.
    // Animations survive only at the source: the proxy flattens them.
    const char* image_url;
    const char* image_src;
    const char* thumbnail_url;
    const char* thumbnail_src;
    const char* author_name;
    const char* footer;
    int image_w;
    int image_h;
    unsigned int color;
};

struct dreaction
{
    const char* emoji_name;
    snowflake emoji_id;
    int count;
    bool me;
};

struct dmessage
{
    snowflake id;
    snowflake channel_id;
    snowflake guild_id;
    snowflake author_id;
    snowflake referenced_id;
    const char* content;
    const char* timestamp;       // raw ISO8601
    const char* edited_timestamp;
    int type;
    bool pending;                // optimistic local echo
    bool failed;
    // Taken back by its author. The message is kept and shown struck through
    // rather than removed: seeing what somebody deleted is the point of
    // keeping an archive at all.
    bool deleted;
    ulist<dattachment> attachments;
    ulist<dembed> embeds;
    ulist<dreaction> reactions;

    // How tall this message drew last time, so the ones scrolled out of
    // sight can be skipped without changing the length of the list. Zero
    // until it has been drawn once.
    float draw_height;
};

struct dmember
{
    snowflake user_id;
    const char* nick;
    ulist<snowflake> roles;

    // When a timeout on this member runs out, as unix milliseconds. Zero when
    // there is none. Discord leaves the stamp in place after it has passed
    // rather than clearing it, so a value in the past means the same as none.
    unsigned long long timeout_until_ms;
};

// The permission bits this client actually reasons about. Discord defines
// several dozen; naming the ones that matter here is clearer than carrying
// the whole table around for no reason.
const unsigned long long PERM_CREATE_INVITE    = 1ULL << 0;
const unsigned long long PERM_BAN_MEMBERS      = 1ULL << 2;
const unsigned long long PERM_ADMINISTRATOR    = 1ULL << 3;
const unsigned long long PERM_VIEW_CHANNEL     = 1ULL << 10;
const unsigned long long PERM_MANAGE_MESSAGES  = 1ULL << 13;
const unsigned long long PERM_CONNECT          = 1ULL << 20;
const unsigned long long PERM_MUTE_MEMBERS     = 1ULL << 22;
// Moving somebody out of a voice channel and disconnecting them are the same
// permission: a disconnect is a move to nowhere.
const unsigned long long PERM_MOVE_MEMBERS     = 1ULL << 24;
const unsigned long long PERM_MANAGE_ROLES     = 1ULL << 28;
const unsigned long long PERM_MODERATE_MEMBERS = 1ULL << 40;

// One line of a channel's permission table: who it is about, and what it
// turns on and off for them. Denies win over allows at the same level, and
// the levels run @everyone, then roles, then the single member entry.
struct doverwrite
{
    snowflake id;            // a role id, or a user id
    int type;                // 0 role, 1 member
    unsigned long long allow;
    unsigned long long deny;
};

struct dchannel
{
    snowflake id;
    snowflake guild_id;
    snowflake parent_id;
    snowflake last_message_id;
    const char* name;
    const char* topic;
    const char* icon;            // group DM icon hash
    int type;
    int position;
    ulist<snowflake> recipients; // DM / group DM

    // Who owns a group chat. Only groups have one; a server's channel belongs
    // to the server and a one to one DM belongs to nobody.
    snowflake owner_id;
    ulist<doverwrite> overwrites;
    ulist<dmessage> messages;    // ascending by id

    bool history_loaded;
    bool history_loading;
    bool history_exhausted;
    // Set when a fetch failed, so the chat view does not retry every frame.
    bool history_failed;
    // Whether the saved copy has been read in, and how much of it there was.
    // Kept on the channel so it dies with the store when accounts change,
    // instead of outliving it in a list somewhere.
    bool archive_loaded;
    int archive_messages;

    // What discord last told us this account had read here, and how many of
    // the messages since then were addressed to them. Unread is a comparison
    // of ids, not a count: message ids are timestamps, so "anything newer than
    // what I last read" is one integer compare.
    snowflake last_read_id;
    int mention_count;

    // Everything else discord hands over about a channel. Held rather than
    // used, because a channel this account cannot open is one where these are
    // the only things it can be told about.
    int user_limit;           // voice
    int bitrate;              // voice
    int rate_limit_per_user;  // slowmode, seconds
    bool nsfw;
    bool archived;            // threads
    bool locked;              // threads
    int member_count;         // threads
    int message_count;        // threads

    bool unread() const
    {
        return last_message_id != 0 && last_message_id > last_read_id;
    }
    bool is_dm() const { return type == CH_DM || type == CH_GROUP_DM; }
    // A voice channel carries a text chat of its own, under the same id. It
    // reads and posts exactly like any other channel, so everything that asks
    // this question should say yes for one.
    bool is_voice() const { return type == CH_GUILD_VOICE || type == CH_STAGE; }

    bool is_textual() const
    {
        return type == CH_GUILD_TEXT || type == CH_DM || type == CH_GROUP_DM ||
               type == CH_ANNOUNCEMENT || type == CH_PUBLIC_THREAD ||
               type == CH_PRIVATE_THREAD || type == CH_ANNOUNCEMENT_THREAD ||
               is_voice();
    }
};

struct drole
{
    snowflake id;
    const char* name;
    unsigned int color;
    int position;
    unsigned long long permissions;
    // Whether the server asked for this role to head its own section in the
    // member list. Most roles do not; without this the list would be one
    // heading per role and unreadable.
    bool hoist;

    // Carried so that editing a role does not quietly clear it: the edit
    // sends the whole role back, and a field nobody parsed would go out as
    // false whatever it had been.
    bool mentionable;
};

struct dguild
{
    snowflake id;
    snowflake owner_id;
    const char* name;
    const char* icon;
    ulist<snowflake> channels;
    ulist<dmember> members;
    ulist<drole> roles;
    int position;
    // What the server says the total is. The member list arrives a window at a
    // time, so the number of entries held is not the size of the server.
    int member_count;
    bool loaded;

    // Shown in the server info panel and nowhere else. The creation date is
    // not carried in the payload at all - it is unpacked from the id, which
    // has the timestamp built into it.
    const char* joined_at;         // raw ISO8601
    const char* vanity_url_code;
    const char* description;
    int verification_level;
    int premium_tier;
    int premium_subscribers;

    // What discord answers when asked about the server without joining
    // anything. The member list arrives a window at a time and only after
    // subscribing, so counting what is loaded is no answer at all until
    // somebody has already walked into the server - which is exactly what
    // this saves them doing.
    int approx_members;
    int approx_online;
    bool counts_loading;
    unsigned long long counts_at_ms;

    unsigned long long created_ms() const { return snowflake_time_ms(id); }
};

struct drelationship
{
    snowflake user_id;
    int type;
    const char* nickname;
};

struct dvoice_state
{
    snowflake user_id;
    snowflake channel_id;
    snowflake guild_id;
    bool self_mute;
    bool self_deaf;
    bool mute;
    bool deaf;
    // Go Live. The camera is self_video and is a different thing entirely.
    bool self_stream;
};

// ---------------------------------------------------------------------------
// CDN helpers
// ---------------------------------------------------------------------------

namespace cdn
{
    // All of these write a NUL terminated url into out.
    void user_avatar(const duser* u, int size, char* out, int cap);
    void user_banner(const duser* u, int size, char* out, int cap);
    void guild_icon(const dguild* g, int size, char* out, int cap);
    void channel_icon(const dchannel* c, int size, char* out, int cap);
    void custom_emoji(snowflake emoji_id, bool animated, int size, char* out, int cap);
}

// Formats "HH:MM" (today) or "DD.MM.YYYY HH:MM" from an ISO8601 stamp.
void format_timestamp(const char* iso, char* out, int cap);

// Formats "DD.MM.YYYY, HH:MM" from a unix time in milliseconds. Used for the
// dates that arrive as a snowflake rather than as text.
void format_epoch_ms(unsigned long long ms, char* out, int cap);

// An ISO8601 stamp as discord writes them ("2026-08-17T18:34:31.123+00:00")
// to unix milliseconds. Zero when there is nothing to read. Any offset is
// ignored: every stamp discord sends is already UTC.
unsigned long long iso_to_unix_ms(const char* iso);

// The other way, to the shape discord accepts in a request body.
void unix_ms_to_iso(unsigned long long ms, char* out, int cap);
