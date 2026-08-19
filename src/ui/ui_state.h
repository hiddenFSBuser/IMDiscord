#include "net/proxy.h"
#pragma once
#include "imgui.h"
#include "discord/types.h"
#include "discord/rest.h"

// Shared state and helpers for the view files. Everything here is touched only
// from the UI thread.
struct ui_state
{
    bool logged_in;
    volatile long login_busy;

    snowflake active_guild;      // 0 means the direct-message list
    snowflake active_channel;
    snowflake reply_to;
    snowflake profile_user;
    snowflake profile_guild;
    snowflake server_info_guild;

    // The role editor: which server it is open on, which role is selected,
    // and the fields being edited before they are sent.
    snowflake roles_guild;
    snowflake roles_selected;
    bool open_roles_popup;
    char role_name[128];
    char role_new_name[128];
    unsigned long long role_perms;
    unsigned int role_color;
    bool role_hoist;
    bool role_mentionable;

    // The invite and webhook window: which server it was opened on and which
    // channel the link will point into.
    snowflake invite_guild;
    snowflake invite_channel;
    bool open_invites_popup;

    // The webhook box, split from invites: the two share only a channel.
    snowflake hooks_guild;
    snowflake hooks_channel;
    bool open_hooks_popup;

    // The new-channel box.
    snowflake newchan_guild;
    bool open_newchan_popup;

    // The channel permission box: which channel, and whose overwrite is
    // being edited inside it.
    snowflake chanperm_channel;
    snowflake chanperm_target;
    bool open_chanperm_popup;

    // The privacy box.
    bool open_privacy_popup;

    // Renaming a server and changing its icon.
    snowflake guildedit_guild;
    bool open_guildedit_popup;

    // The audit log and ban list.
    snowflake audit_guild;
    bool open_audit_popup;

    // The music player.
    bool open_music_popup;

    // Handing a server to somebody else: which server, to whom, and the code
    // discord mailed out.
    snowflake ownership_guild;
    snowflake ownership_user;
    bool open_ownership_popup;
    char ownership_code[32];

    // Deleting a server: which one, and what the person has typed so far into
    // the box that asks for its name back.
    snowflake delete_guild_id;
    bool open_delete_guild_popup;
    char delete_guild_typed[128];

    // imgui refuses to cut or copy from a password field, so the token input
    // can be switched to plain text on demand.
    bool token_visible;

    bool show_friends;
    bool show_settings;
    bool show_accounts;
    // Switching throws the whole store away, and the frame that asked for it is
    // still holding names and channels out of it. Both are deferred to the top
    // of the next frame, where nothing has read anything yet.
    int pending_account;          // -1 for none
    bool pending_logout;
    // Signed in from the archive alone, with no server behind it. A login that
    // comes back later may still promote this into a real session, but a login
    // that fails must not throw the user back out of it.
    bool offline_session;
    // Which remembered account to open from the archive, -1 for none.
    int pending_offline;
    // A token typed into the switcher, signed in with on the next frame for
    // the same reason.
    char pending_token[512];

    // Whether the token typed on the login screen, or the one waiting in
    // pending_token, belongs to a bot. Chosen with a checkbox rather than
    // guessed: the two kinds of token look alike, and trying one and then the
    // other would mean a failed sign-in on every second attempt.
    bool login_is_bot;
    bool pending_is_bot;
    // The switcher grows a token field only once "add an account" is pressed,
    // so the common case is a plain list.
    bool adding_account;
    bool open_profile_popup;
    bool open_server_info_popup;

    // The share setup box, which doubles as the censor picker while a stream
    // is already running.
    bool open_share_popup;

    // Channels this account has no right to read. Shown by default, marked as
    // such: knowing a private channel exists is most of what a person wants
    // from a client like this, and hiding it would only hide it from them.
    bool show_hidden_channels;
    snowflake channel_info_id;
    bool open_channel_info_popup;

    // Which remembered account has its proxy row open, or -1. Two special
    // values: PROXY_SLOT_DEFAULT edits the one everything uses by default,
    // PROXY_SLOT_NEW the one a not yet added account will start with.
    int proxy_editing;
    char proxy_host[128];
    char proxy_port[8];
    char proxy_user[64];
    char proxy_pass[64];
    char proxy_paste[256];
    int proxy_kind;
    bool proxy_own;          // this account wants its own instead of the default

    // Held between opening the add-account box and the account existing.
    bool new_proxy_own;
    proxy_config new_proxy;
    bool scroll_to_bottom;
    bool request_history_more;

    unsigned int seen_revision;
    unsigned int seen_message_count;

    char token_input[512];
    char login_error[256];
    char message_input[3800];
    char friend_input[128];

    // Held while a friend request is waiting on a captcha: who it was for,
    // and the token the person brings back. Tokens are long.
    char captcha_for[128];
    char captcha_token[2048];
    char invite_input[192];
    char new_guild_name[128];
    char member_filter[64];

    ulist<upload_file> pending_files;

    // Full-size preview of a chat image.
    char viewer_url[512];
    bool viewer_open;

    // How far in, and where the picture has been dragged to. Reset every time
    // the viewer opens on something new.
    float viewer_zoom;
    ImVec2 viewer_pan;
    // What to call the file if it is saved. A cdn address ends in a hash and
    // a size, which makes a useless name on disk.
    char viewer_name[128];

    unsigned long long last_typing_ms;
};

extern ui_state g_ui;

// Which image format to ask discord's media proxy for. png costs an extra
// transcode on their side but comes back lossless; webp is what the proxy
// serves natively and is what discord's own clients use.
enum image_format_pref
{
    IMAGE_PREFER_PNG = 0,
    IMAGE_ALWAYS_WEBP = 1,
};

int ui_image_format();
void ui_set_image_format(int pref);

// Whether an embedded picture that can animate is fetched from the site it
// lives on instead of from discord's proxy. The proxy hands back a single
// frame for those, so this is on by default; the cost is that the request
// reaches that site directly.
bool ui_embed_direct_gifs();

// Playing mp4 attachments in place. Off by default: it works, but not well
// enough to be what everybody gets - the sound drifts, scrubbing is coarse
// and it costs more than watching the file in a real player would.
bool ui_video_player();
void ui_set_video_player(bool on);
void ui_set_embed_direct_gifs(bool on);

// ---- shared drawing helpers ----
void ui_avatar(const duser* u, float size, bool show_status = false);
void ui_guild_bubble(const dguild* g, float size, bool active);
void ui_text_muted(const char* text);
bool ui_icon_button(const char* label, const ImVec2& size, ImU32 bg, ImU32 bg_hover);

// Vector icons; the app ships no icon font, so the few glyphs it needs are
// drawn straight into the draw list.
enum ui_icon
{
    ICON_MIC = 0,
    ICON_HEADPHONES,
    ICON_GEAR,
    ICON_PLUS,
    ICON_HASH,       // text channel
    ICON_SPEAKER,    // voice channel
    ICON_PHONE,      // start a call
    ICON_HANGUP,     // leave a call
    ICON_SCREEN,     // share the screen
    ICON_MUSIC,      // play a track into the call
};

bool ui_glyph_button(const char* id, ui_icon icon, bool crossed, const ImVec2& size,
                     ImU32 bg, ImU32 bg_hover, ImU32 fg);

// Draws an icon straight into a draw list, for rows that lay themselves out.
void ui_draw_icon(ImDrawList* dl, ui_icon icon, ImVec2 center, float size, ImU32 color);
void ui_open_profile(snowflake user_id, snowflake guild_id);

// Snowflakes on the clipboard, and the menu row that puts them there. A null
// id draws nothing, so callers can pass whatever they have without checking.
void ui_copy_id(snowflake id);
void ui_copy_id_item(snowflake id, const char* label = 0);
const char* ui_channel_display_name(const dchannel* c, char* buffer, int cap);

// ---- attachments ----
void ui_attach_path(const wchar_t* path);
void ui_attach_bytes(const char* name, const char* content_type, const void* data, unsigned int size);
void ui_paste_from_clipboard();
void ui_clear_attachments();

// ---- views ----
void ui_view_login();
void ui_view_guild_rail(float width, float height);
void ui_view_channel_list(float width, float height);
void ui_view_chat(float width, float height);
void ui_view_members(float width, float height);
void ui_view_friends(float width, float height);
const int PROXY_SLOT_DEFAULT = -2;
const int PROXY_SLOT_NEW = -3;

// The proxy editor, shared by the sign-in screen, the add-account box and each
// row of the account list. Returns true on the frame Save is pressed, having
// written the result into `out`. `own` is null where there is nothing to
// override - the default itself.
bool ui_proxy_editor(proxy_config* out, bool* own);

// Loads the editor fields from a config, ready to be shown.
void ui_proxy_editor_open(int slot, const proxy_config* cfg, bool own);

// The order servers are shown in, once somebody has dragged one. Discord
// keeps its own copy in account settings that this client does not write to,
// so a local override is remembered here instead and applied over it.
void ui_save_guild_order();
void ui_apply_saved_guild_order();

void ui_view_profile_popup();
void ui_view_server_info_popup();
void ui_view_roles_popup();
void ui_view_invites_popup();
void ui_open_invites(snowflake guild_id);
void ui_view_webhooks_popup();
void ui_open_webhooks(snowflake guild_id);
void ui_view_new_channel_popup();
void ui_open_new_channel(snowflake guild_id);
void ui_view_channel_perms_popup();
void ui_open_channel_perms(snowflake channel_id);
void ui_view_privacy_popup();
void ui_open_privacy();
void ui_view_audit_popup();
void ui_view_music_popup();
void ui_open_music();
void ui_view_ownership_popup();
void ui_open_ownership(snowflake guild_id, snowflake user_id);
void ui_view_delete_guild_popup();
void ui_open_delete_guild(snowflake guild_id);
void ui_open_audit(snowflake guild_id);
void ui_view_guild_edit_popup();
void ui_open_guild_edit(snowflake guild_id);
void ui_open_roles(snowflake guild_id);

// The give-and-take-away submenu, for any place a member is right-clickable.
void ui_member_roles_menu(snowflake guild_id, snowflake user_id);

// Whether this account may pull somebody out of the voice channel they are
// sitting in, and into to_channel_id. Passing 0 for the destination asks only
// whether they can be moved at all, which is what a drag has to know before
// it is allowed to start.
bool ui_can_move_member(snowflake guild_id, snowflake user_id, snowflake to_channel_id);

// Disconnecting, silencing, timing out and banning, for the same places. Each
// item only appears when the account holds the permission for it and the
// person on the other end is somebody it may be used on, so the menu is often
// empty - in which case it draws nothing at all, not an empty heading.
void ui_member_moderation_menu(snowflake guild_id, snowflake user_id);
void ui_view_channel_info_popup();
void ui_view_share_popup();

// What a share is started with. Kept in settings so it survives a restart.
int ui_share_height();          // 1080, 720, 480, 360
int ui_share_fps();
int ui_share_quality();         // 0 low, 1 normal, 2 high
void ui_set_share_settings(int height, int fps, int quality);

// Bitrate the chosen height and quality work out to.
int ui_share_bitrate();

// Whether a share should carry what the machine is playing.
// How the screen is read. DXGI is the fast path; BitBlt is the one that works
// on anything, including a single window.
int ui_capture_method();
void ui_set_capture_method(int method);

bool ui_share_audio();
void ui_set_share_audio(bool on);

// Crossed-out microphone and headset, drawn at `at` in a box `size` across.
void ui_draw_muted_marks(ImDrawList* dl, ImVec2 at, float size,
                         bool mic_off, bool ears_off);
// The owner's crown, drawn at `at` in a box `size` across.
void ui_draw_crown(ImDrawList* dl, ImVec2 at, float size);
// The bottom panel grows by a row while a voice session is up.
const float USER_ROW_HEIGHT = 58.0f;
const float VOICE_ROW_HEIGHT = 46.0f;
// A band explaining why the last call ended, when there is no call.
const float STOPPED_ROW_HEIGHT = 36.0f;
float ui_voice_panel_height();

void ui_view_voice_panel(float width);
void ui_view_settings_popup();
// The remembered sign-ins: switch between them, add one, forget one.
void ui_view_accounts_popup();
// The bar that says the client is running on what it saved, and why. Tall
// enough for two lines of text beside a button, which is what it holds.
const float OFFLINE_BANNER_HEIGHT = 58.0f;
// The bar that says the client is running on what it saved, and why.
void ui_view_offline_banner();
void ui_view_image_viewer();

// Opens the viewer on one picture. `name` is what a saved copy is called, and
// may be null to take the last part of the address.
void ui_open_image_viewer(const char* url, const char* name);
// Somebody else's screen. Draws nothing while no stream is being watched.
void ui_view_stream_window();
void ui_view_camera_window();

// True when something on screen moves by itself and the frame cannot wait.
bool ui_wants_redraw();
