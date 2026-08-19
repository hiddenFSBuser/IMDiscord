#include "pch.h"
#include "ui.h"
#include "ui_state.h"
#include "theme.h"
#include "textures.h"

#include "core/app.h"
#include "core/log.h"
#include "core/storage.h"
#include "core/offline.h"
#include "discord/archive.h"
#include "discord/store.h"
#include "discord/rest.h"
#include "discord/science.h"
#include "discord/gateway.h"
#include "discord/voice.h"
#include "video/screenshare.h"
#include "video/streamview.h"
#include "audio/audio.h"
#include "audio/noise.h"
#include "audio/vad.h"
#include "video/player.h"
#include "video/censor.h"
#include "video/capture.h"
#include "net/proxy.h"
#include "net/http.h"
#include "system/io/ufile.h"
#include "stb_image_write.h"

ui_state g_ui;

namespace
{
    // Set once the saved server order has been applied for this session.
    bool g_guild_order_applied = false;

    const float RAIL_WIDTH = 72.0f;
    const float LIST_WIDTH = 240.0f;
    const float MEMBERS_WIDTH = 210.0f;
    const float USER_PANEL_HEIGHT = 58.0f;

    struct login_job
    {
        char token[512];
        bool is_bot;
    };

    // Set for test logins so a throwaway account does not overwrite the token
    // the user actually signed in with.
    bool g_ephemeral_login = false;

    void job_login(void* user)
    {
        login_job* j = (login_job*)user;

        char error[256];
        error[0] = 0;

        if (api::verify_token(j->token, j->is_bot, error, sizeof(error)))
        {
            if (!g_ephemeral_login)
            {
                storage::save_token(j->token);

                // verify_token has just fetched /users/@me, so the account can
                // be remembered with the name and avatar the switcher shows
                // rather than as a bare token.
                snowflake id = 0;
                char name[64];
                char avatar[64];
                name[0] = 0;
                avatar[0] = 0;
                {
                    store::guard g;
                    duser* me = store::self();
                    if (me)
                    {
                        id = me->id;
                        ccstrncpy(name, me->display_name(), sizeof(name) - 1);
                        if (me->avatar) ccstrncpy(avatar, me->avatar, sizeof(avatar) - 1);
                    }
                }

                int slot = storage::account_remember(j->token, id, name, avatar, j->is_bot);
                storage::set_active_account(slot);

                // The route was already chosen and already used to make this
                // very request; writing it onto the account now is what makes
                // it stick for next time.
                if (g_ui.new_proxy_own)
                {
                    storage::account_set_proxy(slot, &g_ui.new_proxy, true);
                    g_ui.new_proxy_own = false;
                    ccfset(&g_ui.new_proxy, 0, sizeof(g_ui.new_proxy));
                }

                // Everything from here is written down under this account, and
                // whatever was written last time comes straight back: the
                // client has its servers and its friends before the gateway
                // has said a word.
                if (id)
                {
                    archive::init(id);
                    archive::snapshot_load();
                }
            }

            g_ui.login_error[0] = 0;
            g_ui.logged_in = true;
            g_ui.offline_session = false;
            offline::leave();

            // The new socket owns the media again from here.
            gateway::hold_media(false);
            gateway::start();
        }
        else
        {
            ccfset(g_ui.login_error, 0, sizeof(g_ui.login_error));
            ccstrncpy(g_ui.login_error, error, sizeof(g_ui.login_error) - 1);

            // Somebody who gave up waiting and opened their archive stays in
            // it. Throwing them back to the sign-in screen because the attempt
            // they had already abandoned finally failed would be absurd.
            if (!g_ui.offline_session) g_ui.logged_in = false;
        }

        ccfset(j, 0, sizeof(login_job));
        memfree(j);
        InterlockedExchange(&g_ui.login_busy, 0);
    }

    // Points every request and every picture download at the route the
    // account in front is meant to use. Called on sign-in and on every
    // switch, because it is process wide - a held connection from another
    // account keeps its own on its own socket.
    void apply_active_proxy()
    {
        proxy_config cfg = storage::active_proxy();
        http::set_proxy(&cfg);
    }

    void begin_login(const char* token, bool is_bot)
    {
        if (g_ui.login_busy) return;
        if (!token || !token[0]) return;

        // Checking the token is a request like any other. If this account is
        // meant to go through a proxy, it has to go through it from the very
        // first one - otherwise the account is announced from the wrong
        // address before it has even been added.
        if (g_ui.new_proxy_own)
        {
            http::set_proxy(&g_ui.new_proxy);
        }
        else
        {
            apply_active_proxy();
        }

        login_job* j = (login_job*)memalloc(sizeof(login_job));
        if (!j) return;
        ccfset(j, 0, sizeof(login_job));
        ccstrncpy(j->token, token, sizeof(j->token) - 1);
        j->is_bot = is_bot;

        // Everything from the very first request onwards is signed the way
        // this kind of token has to be signed.
        api::set_token(token, is_bot);

        InterlockedExchange(&g_ui.login_busy, 1);
        ccfset(g_ui.login_error, 0, sizeof(g_ui.login_error));
        jobs::post(job_login, j);
    }

    // Everything that belongs to whoever is signed in right now. Shared by
    // signing out and by switching, which differ only in whether the account is
    // forgotten afterwards.
    // keep_media leaves the voice connection and anything being streamed
    // running across the change.
    //
    // They are separate connections with their own sockets and their own keys,
    // authenticated by tokens the voice server already accepted, so nothing
    // about them depends on the main socket staying up. Dropping them was
    // simply the easy thing to do, and it meant changing accounts cut you off
    // mid-sentence.
    void tear_down_session(bool keep_media = false)
    {
        g_guild_order_applied = false;

        // One last write on the way out, so the newest state survives even if
        // the timer was not due.
        if (archive::ready() && !offline::active()) archive::snapshot_save();

        if (!keep_media)
        {
            streamview::stop();
            screenshare::stop();
        }

        // Set before the socket goes, so its closing does not take the voice
        // connection with it.
        gateway::hold_media(keep_media);

        // With a call up, the socket is not closed at all: it steps aside and
        // keeps heartbeating so discord leaves the voice state alone. Closing
        // it is what made the voice server hang up with 4014.
        bool call_up = keep_media && voice::state() != VOICE_IDLE;
        if (call_up) gateway::detach_for_voice();
        else         gateway::stop();

        if (!keep_media)
        {
            voice::shutdown();
            voice::init();
        }

        api::set_token("");

        archive::shutdown();
        store::reset();

        g_ui.logged_in = false;
        g_ui.active_guild = 0;
        g_ui.active_channel = 0;
        g_ui.reply_to = 0;
        g_ui.profile_user = 0;
        g_ui.show_friends = false;
        g_ui.offline_session = false;
        ui_clear_attachments();
    }

    // Signs out without forgetting anybody: the account stays in the list and
    // the login view offers it. Removing one is a separate, deliberate action.
    void logout()
    {
        tear_down_session();

        storage::set_active_account(-1);
        storage::clear_token();

        ccfset(g_ui.token_input, 0, sizeof(g_ui.token_input));
    }

    // Signs in as one of the remembered accounts. The old session is torn down
    // first: the cache, the voice connection and the gateway all belong to the
    // account being left.

    void switch_account(int index)
    {
        const saved_account* entry = storage::account_at(index);
        if (!entry || !entry->token[0]) return;
        if (g_ui.login_busy) return;

        char token[256];
        ccstrncpy(token, entry->token, sizeof(token) - 1);
        token[sizeof(token) - 1] = 0;
        bool bot = entry->is_bot;

        // The voice session stays up: the whole point of switching accounts
        // mid-conversation is not to have to leave it.
        tear_down_session(true);
        storage::set_active_account(index);
        apply_active_proxy();

        begin_login(token, bot);
        ccfset(token, 0, sizeof(token));
    }

    // Opens the client on what is saved, with nothing behind it. No token is
    // checked and no socket is opened: the account is identified by the entry
    // that was picked, which is enough to find its archive.
    void open_saved_session(int account_index, offline_reason why)
    {
        const saved_account* entry = storage::account_at(account_index);
        if (!entry || !entry->id) return;

        store::set_self_id(entry->id);

        archive::init(entry->id);
        archive::snapshot_load();

        offline::enter(why);

        g_ui.offline_session = true;
        g_ui.logged_in = true;
        g_ui.show_friends = true;
        g_ui.active_guild = 0;
        g_ui.active_channel = 0;

        log_line("ui: открыт архив аккаунта %llu без подключения", entry->id);
    }

    void consume_dropped_files()
    {
        while (g_app.dropped_files.count > 0)
        {
            wchar_t* path = g_app.dropped_files[0];
            g_app.dropped_files.delete_at_fast(0);
            if (path)
            {
                ui_attach_path(path);
                memfree(path);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

int ui_image_format()
{
    return storage::settings_get_int("image_format", IMAGE_PREFER_PNG);
}

void ui_set_image_format(int pref)
{
    storage::settings_set_int("image_format", pref);
}

int ui_share_height()
{
    int h = storage::settings_get_int("share_height", 720);
    if (h != 1080 && h != 720 && h != 480 && h != 360) h = 720;
    return h;
}

int ui_share_fps()
{
    int f = storage::settings_get_int("share_fps", 30);

    if (f < 1) f = 1;
    if (f > SHARE_FPS_MAX) f = SHARE_FPS_MAX;
    return f;
}

int ui_share_quality()
{
    int q = storage::settings_get_int("share_quality", 1);
    if (q < 0) q = 0;
    if (q > 2) q = 2;
    return q;
}

void ui_set_share_settings(int height, int fps, int quality)
{
    storage::settings_set_int("share_height", height);
    storage::settings_set_int("share_fps", fps);
    storage::settings_set_int("share_quality", quality);
    storage::settings_save();
}

bool ui_share_audio()
{
    return storage::settings_get_int("share_audio", 0) != 0;
}

void ui_set_share_audio(bool on)
{
    storage::settings_set_int("share_audio", on ? 1 : 0);
    storage::settings_save();
}

int ui_capture_method()
{
    int m = storage::settings_get_int("capture_method", CAPTURE_DXGI);
    if (m < 0 || m >= CAPTURE_METHOD_COUNT) m = CAPTURE_DXGI;
    return m;
}

void ui_set_capture_method(int method)
{
    storage::settings_set_int("capture_method", method);
    storage::settings_save();
}

int ui_share_bitrate()
{
    // Roughly what each size needs to look reasonable, then bent by the
    // quality choice. There is no jpeg anywhere in this path - the stream is
    // H.264 - so "quality" is bitrate, which is the knob that actually
    // decides how much detail survives.
    int base;
    switch (ui_share_height())
    {
    case 1080: base = 4500; break;
    case 480:  base = 1200; break;
    case 360:  base = 700;  break;
    default:   base = 2500; break;
    }

    // A higher frame rate has more frames to spend the same budget on.
    int fps = ui_share_fps();
    if (fps > 30) base = base * 3 / 2;
    else if (fps < 20) base = base * 3 / 4;

    switch (ui_share_quality())
    {
    case 0: return base / 2;
    case 2: return base * 2;
    default: return base;
    }
}

bool ui_video_player()
{
    return storage::settings_get_int("video_player", 0) != 0;
}

void ui_set_video_player(bool on)
{
    storage::settings_set_int("video_player", on ? 1 : 0);
    storage::settings_save();

    // Turning it off with something playing should stop it there and then.
    if (!on) player::stop();
}

bool ui_embed_direct_gifs()
{
    return storage::settings_get_int("embed_direct_gifs", 1) != 0;
}

void ui_set_embed_direct_gifs(bool on)
{
    storage::settings_set_int("embed_direct_gifs", on ? 1 : 0);
    storage::settings_save();
}

void ui_text_muted(const char* text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, col::text_muted);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

bool ui_icon_button(const char* label, const ImVec2& size, ImU32 bg, ImU32 bg_hover)
{
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(bg));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4(bg_hover));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::ColorConvertU32ToFloat4(bg_hover));
    bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return pressed;
}

static void draw_glyph(ImDrawList* dl, ui_icon icon, ImVec2 center, float s, ImU32 fg)
{
    switch (icon)
    {
    case ICON_MIC:
    {
        // Capsule capsule + cradle arc + stem.
        float cw = s * 0.26f, ch = s * 0.42f;
        ImVec2 a(center.x - cw, center.y - s * 0.46f);
        ImVec2 b(center.x + cw, center.y + ch * 0.1f);
        dl->AddRectFilled(a, b, fg, cw);

        dl->PathArcTo(ImVec2(center.x, center.y + s * 0.06f), s * 0.38f, 0.0f, 3.14159265f, 16);
        dl->PathStroke(fg, 0, s * 0.11f);

        dl->AddLine(ImVec2(center.x, center.y + s * 0.44f), ImVec2(center.x, center.y + s * 0.62f), fg, s * 0.11f);
        break;
    }

    case ICON_HEADPHONES:
    {
        dl->PathArcTo(ImVec2(center.x, center.y + s * 0.08f), s * 0.44f, 3.14159265f, 2.0f * 3.14159265f, 20);
        dl->PathStroke(fg, 0, s * 0.12f);

        float ew = s * 0.16f, eh = s * 0.30f;
        dl->AddRectFilled(ImVec2(center.x - s * 0.52f, center.y + s * 0.02f),
                          ImVec2(center.x - s * 0.52f + ew * 2.0f, center.y + s * 0.02f + eh), fg, ew * 0.6f);
        dl->AddRectFilled(ImVec2(center.x + s * 0.52f - ew * 2.0f, center.y + s * 0.02f),
                          ImVec2(center.x + s * 0.52f, center.y + s * 0.02f + eh), fg, ew * 0.6f);
        break;
    }

    case ICON_GEAR:
    {
        dl->AddCircle(center, s * 0.34f, fg, 16, s * 0.13f);
        for (int i = 0; i < 6; i++)
        {
            float angle = (float)i * (3.14159265f / 3.0f);
            float cx = ccosf(angle), sy = csinf(angle);
            dl->AddLine(ImVec2(center.x + cx * s * 0.34f, center.y + sy * s * 0.34f),
                        ImVec2(center.x + cx * s * 0.52f, center.y + sy * s * 0.52f), fg, s * 0.12f);
        }
        break;
    }

    case ICON_PLUS:
        dl->AddLine(ImVec2(center.x - s * 0.4f, center.y), ImVec2(center.x + s * 0.4f, center.y), fg, s * 0.14f);
        dl->AddLine(ImVec2(center.x, center.y - s * 0.4f), ImVec2(center.x, center.y + s * 0.4f), fg, s * 0.14f);
        break;

    case ICON_HASH:
    {
        // Two slanted verticals crossed by two horizontals.
        float t = s * 0.10f;
        float slant = s * 0.10f;
        dl->AddLine(ImVec2(center.x - s * 0.18f + slant, center.y - s * 0.42f),
                    ImVec2(center.x - s * 0.18f - slant, center.y + s * 0.42f), fg, t);
        dl->AddLine(ImVec2(center.x + s * 0.18f + slant, center.y - s * 0.42f),
                    ImVec2(center.x + s * 0.18f - slant, center.y + s * 0.42f), fg, t);
        dl->AddLine(ImVec2(center.x - s * 0.42f, center.y - s * 0.14f),
                    ImVec2(center.x + s * 0.42f, center.y - s * 0.14f), fg, t);
        dl->AddLine(ImVec2(center.x - s * 0.42f, center.y + s * 0.14f),
                    ImVec2(center.x + s * 0.42f, center.y + s * 0.14f), fg, t);
        break;
    }

    case ICON_SPEAKER:
    {
        // Cone plus two sound arcs.
        ImVec2 cone[4] =
        {
            ImVec2(center.x - s * 0.42f, center.y - s * 0.14f),
            ImVec2(center.x - s * 0.22f, center.y - s * 0.14f),
            ImVec2(center.x - s * 0.02f, center.y - s * 0.40f),
            ImVec2(center.x - s * 0.02f, center.y + s * 0.40f),
        };
        dl->AddTriangleFilled(cone[0], cone[1], ImVec2(cone[0].x, center.y + s * 0.14f), fg);
        dl->AddRectFilled(ImVec2(center.x - s * 0.42f, center.y - s * 0.14f),
                          ImVec2(center.x - s * 0.22f, center.y + s * 0.14f), fg);
        dl->AddTriangleFilled(cone[1], cone[2], cone[3], fg);
        dl->AddTriangleFilled(cone[1], cone[3], ImVec2(cone[1].x, center.y + s * 0.14f), fg);

        dl->PathArcTo(ImVec2(center.x - s * 0.02f, center.y), s * 0.26f, -1.0f, 1.0f, 10);
        dl->PathStroke(fg, 0, s * 0.09f);
        dl->PathArcTo(ImVec2(center.x - s * 0.02f, center.y), s * 0.44f, -1.0f, 1.0f, 12);
        dl->PathStroke(fg, 0, s * 0.09f);
        break;
    }

    case ICON_PHONE:
    case ICON_HANGUP:
    {
        // A handset: two ear pieces joined by a bar. Hangup is the same shape
        // rotated, which is how every other client draws it.
        float w = s * 0.44f;
        float t = s * 0.16f;
        float lift = (icon == ICON_HANGUP) ? -s * 0.12f : s * 0.12f;

        dl->AddLine(ImVec2(center.x - w, center.y - lift),
                    ImVec2(center.x + w, center.y - lift), fg, t);
        dl->AddCircleFilled(ImVec2(center.x - w, center.y - lift + (icon == ICON_HANGUP ? -t : t)), t * 0.75f, fg);
        dl->AddCircleFilled(ImVec2(center.x + w, center.y - lift + (icon == ICON_HANGUP ? -t : t)), t * 0.75f, fg);

        if (icon == ICON_HANGUP)
            dl->AddLine(ImVec2(center.x - s * 0.5f, center.y + s * 0.5f),
                        ImVec2(center.x + s * 0.5f, center.y - s * 0.5f), fg, s * 0.11f);
        break;
    }

    case ICON_SCREEN:
    {
        // An outlined monitor on a stand.
        float t = s * 0.11f;
        dl->AddRect(ImVec2(center.x - s * 0.50f, center.y - s * 0.42f),
                    ImVec2(center.x + s * 0.50f, center.y + s * 0.22f), fg, s * 0.12f, 0, t);
        dl->AddLine(ImVec2(center.x, center.y + s * 0.22f),
                    ImVec2(center.x, center.y + s * 0.42f), fg, t);
        dl->AddLine(ImVec2(center.x - s * 0.26f, center.y + s * 0.44f),
                    ImVec2(center.x + s * 0.26f, center.y + s * 0.44f), fg, t);
        break;
    }

    case ICON_MUSIC:
    {
        // A quaver: one filled head, a stem, and a flag off the top.
        float t = s * 0.11f;
        float head_x = center.x - s * 0.18f;
        float head_y = center.y + s * 0.28f;

        dl->AddCircleFilled(ImVec2(head_x, head_y), s * 0.17f, fg, 12);
        dl->AddLine(ImVec2(head_x + s * 0.16f, head_y),
                    ImVec2(head_x + s * 0.16f, center.y - s * 0.42f), fg, t);
        dl->AddLine(ImVec2(head_x + s * 0.16f, center.y - s * 0.42f),
                    ImVec2(head_x + s * 0.46f, center.y - s * 0.24f), fg, t);
        break;
    }
    }
}

void ui_draw_icon(ImDrawList* dl, ui_icon icon, ImVec2 center, float size, ImU32 color)
{
    draw_glyph(dl, icon, center, size, color);
}

bool ui_glyph_button(const char* id, ui_icon icon, bool crossed, const ImVec2& size,
                     ImU32 bg, ImU32 bg_hover, ImU32 fg)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(id, size);
    bool hovered = ImGui::IsItemHovered();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), hovered ? bg_hover : bg, 5.0f);

    ImVec2 center(p.x + size.x * 0.5f, p.y + size.y * 0.5f);
    float s = (size.x < size.y ? size.x : size.y) * 0.62f;
    draw_glyph(dl, icon, center, s, fg);

    if (crossed)
        dl->AddLine(ImVec2(center.x - s * 0.55f, center.y + s * 0.55f),
                    ImVec2(center.x + s * 0.55f, center.y - s * 0.55f), col::red, s * 0.14f);

    return pressed;
}

static ImU32 status_color(unsigned char status)
{
    switch (status)
    {
    case STATUS_ONLINE: return col::green;
    case STATUS_IDLE:   return col::yellow;
    case STATUS_DND:    return col::red;
    default:            return IM_COL32(128, 132, 142, 255);
    }
}

void ui_avatar(const duser* u, float size, bool show_status)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    char url[320];
    cdn::user_avatar(u, size > 48.0f ? 128 : 64, url, sizeof(url));

    const texture* t = tex::get(url);
    if (t->ready())
    {
        // As round as the person asked for. A half of the size is the circle
        // discord uses; zero is a square.
        dl->AddImageRounded(t->id(), p, ImVec2(p.x + size, p.y + size),
                            ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE,
                            size * theme::avatar_rounding());
    }
    else
    {
        dl->AddRectFilled(p, ImVec2(p.x + size, p.y + size), col::bg_hover,
                          size * theme::avatar_rounding());
        const char* name = u ? u->display_name() : "?";
        char initial[8] = { name[0], 0 };
        ImVec2 ts = ImGui::CalcTextSize(initial);
        dl->AddText(ImVec2(p.x + (size - ts.x) * 0.5f, p.y + (size - ts.y) * 0.5f), col::text_normal, initial);
    }

    if (show_status && u)
    {
        float r = size * 0.18f;
        ImVec2 c(p.x + size - r, p.y + size - r);
        dl->AddCircleFilled(c, r + 2.0f, col::bg_panel);
        dl->AddCircleFilled(c, r, status_color(u->status));
    }

    ImGui::Dummy(ImVec2(size, size));
}

void ui_guild_bubble(const dguild* g, float size, bool active)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    char url[320];
    cdn::guild_icon(g, 128, url, sizeof(url));

    // A server bubble squares off a little when it is the one being read,
    // which is discord's own touch. Both ends move with the setting so the
    // difference between them survives a person choosing square avatars.
    float full = theme::avatar_rounding();
    float rounding = active ? size * full * 0.6f : size * full;

    if (url[0])
    {
        const texture* t = tex::get(url);
        if (t->ready())
        {
            dl->AddImageRounded(t->id(), p, ImVec2(p.x + size, p.y + size),
                                ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, rounding);
            ImGui::Dummy(ImVec2(size, size));
            return;
        }
    }

    dl->AddRectFilled(p, ImVec2(p.x + size, p.y + size), active ? col::accent : col::bg_panel, rounding);

    // Initials of the first two words, discord style.
    char initials[8] = { 0 };
    int k = 0;
    const char* name = g && g->name ? g->name : "?";
    bool word_start = true;
    for (const char* c = name; *c && k < 2; c++)
    {
        if (*c == ' ') { word_start = true; continue; }
        if (word_start) { initials[k++] = *c; word_start = false; }
    }

    ImVec2 ts = ImGui::CalcTextSize(initials);
    dl->AddText(ImVec2(p.x + (size - ts.x) * 0.5f, p.y + (size - ts.y) * 0.5f), col::text_normal, initials);
    ImGui::Dummy(ImVec2(size, size));
}

const char* ui_channel_display_name(const dchannel* c, char* buffer, int cap)
{
    if (!c) { buffer[0] = 0; return buffer; }

    // Channel kind is shown with an icon by the list, so the name is plain.
    if (c->name && c->name[0])
    {
        ccstrncpy(buffer, c->name, cap - 1);
        buffer[cap - 1] = 0;
        return buffer;
    }

    if (c->type == CH_DM && c->recipients.count > 0)
    {
        duser* u = store::find_user(c->recipients[0]);
        ccstrncpy(buffer, u ? u->display_name() : tr("Личные сообщения"), cap - 1);
        return buffer;
    }

    if (c->type == CH_GROUP_DM)
    {
        buffer[0] = 0;
        for (unsigned int i = 0; i < c->recipients.count && i < 3; i++)
        {
            duser* u = store::find_user(c->recipients[i]);
            if (!u) continue;
            if (buffer[0]) ccstrncpy(buffer + ccslenf(buffer), ", ", cap - (int)ccslenf(buffer) - 1);
            ccstrncpy(buffer + ccslenf(buffer), u->display_name(), cap - (int)ccslenf(buffer) - 1);
        }
        if (!buffer[0]) ccstrncpy(buffer, tr("Групповой чат"), cap - 1);
        return buffer;
    }

    ccstrncpy(buffer, tr("без названия"), cap - 1);
    return buffer;
}

// One place so every menu spells an id the same way: plain decimal digits,
// which is the form discord itself copies and the form anything that accepts
// one expects to be pasted.
void ui_copy_id(snowflake id)
{
    if (!id) return;

    char text[32];
    cnprint(text, sizeof(text), "%llu", id);
    ImGui::SetClipboardText(text);
}

// The menu row itself, since it reads identically everywhere.
void ui_copy_id_item(snowflake id, const char* label)
{
    if (!id) return;
    if (ImGui::MenuItem(label ? label : tr("Скопировать ID"))) ui_copy_id(id);
}

void ui_open_profile(snowflake user_id, snowflake guild_id)
{
    g_ui.profile_user = user_id;
    g_ui.profile_guild = guild_id;
    g_ui.open_profile_popup = true;
    science::user_profile_viewed(user_id);

    duser* u = store::find_user(user_id);
    if (!u || !u->profile_loaded) api::fetch_user_profile(user_id, guild_id);
}

// ---------------------------------------------------------------------------
// attachments
// ---------------------------------------------------------------------------

static void guess_content_type(const char* name, char* out, int cap)
{
    const char* dot = 0;
    for (const char* p = name; *p; p++) if (*p == '.') dot = p;

    const char* type = "application/octet-stream";
    if (dot)
    {
        if (ccscmpi(dot, ".png") == 0) type = "image/png";
        else if (ccscmpi(dot, ".jpg") == 0 || ccscmpi(dot, ".jpeg") == 0) type = "image/jpeg";
        else if (ccscmpi(dot, ".gif") == 0) type = "image/gif";
        else if (ccscmpi(dot, ".webp") == 0) type = "image/webp";
        else if (ccscmpi(dot, ".bmp") == 0) type = "image/bmp";
        else if (ccscmpi(dot, ".mp4") == 0) type = "video/mp4";
        else if (ccscmpi(dot, ".webm") == 0) type = "video/webm";
        else if (ccscmpi(dot, ".mp3") == 0) type = "audio/mpeg";
        else if (ccscmpi(dot, ".txt") == 0) type = "text/plain";
        else if (ccscmpi(dot, ".pdf") == 0) type = "application/pdf";
        else if (ccscmpi(dot, ".zip") == 0) type = "application/zip";
    }
    ccstrncpy(out, type, cap - 1);
}

void ui_attach_bytes(const char* name, const char* content_type, const void* data, unsigned int size)
{
    // Discord's free tier rejects anything past 25 MB outright.
    if (!size || size > (25u << 20))
    {
        api::set_last_error(tr("Файл слишком большой (лимит 25 МБ)"));
        return;
    }
    if (g_ui.pending_files.count >= 10)
    {
        api::set_last_error(tr("Не больше 10 файлов за раз"));
        return;
    }

    upload_file f;
    ccfset(&f, 0, sizeof(f));
    ccstrncpy(f.name, name, sizeof(f.name) - 1);
    if (content_type && content_type[0]) ccstrncpy(f.content_type, content_type, sizeof(f.content_type) - 1);
    else guess_content_type(name, f.content_type, sizeof(f.content_type));

    f.data = (unsigned char*)memalloc((int)size);
    if (!f.data) return;
    ccpy(f.data, data, size);
    f.size = size;

    g_ui.pending_files.push(f);
}

void ui_attach_path(const wchar_t* path)
{
    ubuffer blob;
    blob.init();
    if (!ufile::read_all(path, &blob))
    {
        blob.free_buffer();
        api::set_last_error(tr("Не удалось прочитать файл"));
        return;
    }

    const wchar_t* base = path;
    for (const wchar_t* p = path; *p; p++)
        if (*p == L'\\' || *p == L'/') base = p + 1;

    char name[260];
    wcstochar(base, name, sizeof(name));

    ui_attach_bytes(name, 0, blob.data, blob.size);
    blob.free_buffer();
}

void ui_clear_attachments()
{
    for (unsigned int i = 0; i < g_ui.pending_files.count; i++)
        if (g_ui.pending_files[i].data) memfree(g_ui.pending_files[i].data);
    g_ui.pending_files.clear_fast();
}

namespace
{
    void png_writer(void* context, void* data, int size)
    {
        ((ubuffer*)context)->append(data, (unsigned int)size);
    }

    // Converts a packed DIB from the clipboard into RGBA and encodes a PNG.
    bool dib_to_png(const BITMAPINFOHEADER* bih, ubuffer* out)
    {
        if (bih->biCompression != BI_RGB && bih->biCompression != BI_BITFIELDS) return false;
        if (bih->biBitCount != 24 && bih->biBitCount != 32) return false;

        int w = (int)bih->biWidth;
        int h = bih->biHeight < 0 ? (int)-bih->biHeight : (int)bih->biHeight;
        bool bottom_up = bih->biHeight > 0;
        if (w <= 0 || h <= 0 || w > 8192 || h > 8192) return false;

        unsigned int header_size = bih->biSize;
        if (bih->biCompression == BI_BITFIELDS) header_size += 12;

        const unsigned char* bits = (const unsigned char*)bih + header_size;
        int src_stride = ((w * bih->biBitCount + 31) / 32) * 4;
        int bpp = bih->biBitCount / 8;

        unsigned char* rgba = (unsigned char*)memalloc(w * h * 4);
        if (!rgba) return false;

        for (int y = 0; y < h; y++)
        {
            const unsigned char* row = bits + (bottom_up ? (h - 1 - y) : y) * src_stride;
            unsigned char* dst = rgba + y * w * 4;
            for (int x = 0; x < w; x++)
            {
                dst[x * 4 + 0] = row[x * bpp + 2];
                dst[x * 4 + 1] = row[x * bpp + 1];
                dst[x * 4 + 2] = row[x * bpp + 0];
                // 32-bit clipboard bitmaps very often carry a zero alpha channel.
                dst[x * 4 + 3] = 255;
            }
        }

        bool ok = stbi_write_png_to_func(png_writer, out, w, h, 4, rgba, w * 4) != 0;
        memfree(rgba);
        return ok;
    }
}

void ui_paste_from_clipboard()
{
    if (!OpenClipboard(g_app.hwnd)) return;

    if (IsClipboardFormatAvailable(CF_HDROP))
    {
        HDROP drop = (HDROP)GetClipboardData(CF_HDROP);
        if (drop)
        {
            UINT n = DragQueryFileW(drop, 0xFFFFFFFF, 0, 0);
            for (UINT i = 0; i < n; i++)
            {
                wchar_t path[1024];
                if (DragQueryFileW(drop, i, path, 1024)) ui_attach_path(path);
            }
        }
        CloseClipboard();
        return;
    }

    UINT format = IsClipboardFormatAvailable(CF_DIBV5) ? CF_DIBV5
                : (IsClipboardFormatAvailable(CF_DIB) ? CF_DIB : 0);
    if (format)
    {
        HANDLE handle = GetClipboardData(format);
        if (handle)
        {
            const BITMAPINFOHEADER* bih = (const BITMAPINFOHEADER*)GlobalLock(handle);
            if (bih)
            {
                ubuffer png;
                png.init(1 << 18);
                if (dib_to_png(bih, &png))
                {
                    SYSTEMTIME st;
                    GetLocalTime(&st);
                    char name[64];
                    cnprint(name, sizeof(name), "screenshot-%04d%02d%02d-%02d%02d%02d.png",
                            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
                    ui_attach_bytes(name, "image/png", png.data, png.size);
                }
                else
                {
                    api::set_last_error(tr("Формат изображения в буфере не поддерживается"));
                }
                png.free_buffer();
                GlobalUnlock(handle);
            }
        }
    }

    CloseClipboard();
}

// ---------------------------------------------------------------------------
// login screen
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// accounts
// ---------------------------------------------------------------------------

// A bar across the very top saying why the client is showing saved data. It
// has no close button on purpose: a client quietly presenting month old
// history as if it were live is worse than one that will not shut up about it.
void ui_view_offline_banner()
{
    if (!offline::active()) return;

    const ImGuiViewport* vp = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, OFFLINE_BANNER_HEIGHT));

    ImU32 background = offline::reason() == OFFLINE_TOKEN_REVOKED ? col::red : col::yellow;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 8));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 3));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4(background));
    ImGui::Begin("##offline", 0,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                 ImGuiWindowFlags_NoBringToFrontOnFocus);

    // Dark text: the bar is yellow or red, and the client's own light grey
    // would be unreadable on either.
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(20, 20, 22, 255));

    // The button is placed first, pinned to the right, and the text laid out
    // to its left. Putting it after the text with SameLine made its position
    // depend on how long the sentence happened to be, and it landed on top of
    // the second line.
    const float button_w = 210.0f;
    float text_w = vp->WorkSize.x - button_w - 44.0f;
    if (text_w < 120.0f) text_w = 120.0f;

    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(text_w);

    ImGui::PushFont(g_app.font_bold);
    ImGui::TextUnformatted(offline::headline());
    ImGui::PopFont();

    unsigned int secs = offline::seconds();
    char detail[320];
    if (secs >= 60)
        cnprint(detail, sizeof(detail), tr("%s  (уже %u мин)"), offline::detail(), secs / 60);
    else
        cnprint(detail, sizeof(detail), "%s", offline::detail());
    ImGui::TextUnformatted(detail);

    ImGui::PopTextWrapPos();
    ImGui::EndGroup();

    ImGui::SameLine();
    ImGui::SetCursorPosX(vp->WorkSize.x - button_w - 14.0f);
    ImGui::SetCursorPosY(9.0f);

    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 55));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 0, 0, 105));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(0, 0, 0, 140));

    if (ImGui::Button(tr("Попробовать подключиться"), ImVec2(button_w, OFFLINE_BANNER_HEIGHT - 18.0f)))
    {
        int active = storage::active_account();
        const saved_account* entry = storage::account_at(active);

        offline::leave();
        g_ui.offline_session = false;

        if (entry && entry->token[0])
            ccstrncpy(g_ui.pending_token, entry->token, sizeof(g_ui.pending_token) - 1);
    }

    ImGui::PopStyleColor(3);
    ImGui::PopStyleColor();
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

namespace
{
    // Per account, because two accounts are in different servers and one
    // order would be wrong for both.
    void guild_order_key(char* out, int cap)
    {
        char tag[40];
        storage::account_tag(store::self_id(), tag, sizeof(tag));
        cnprint(out, cap, "guild_order_%s", tag);
    }
}

void ui_save_guild_order()
{
    char key[64];
    guild_order_key(key, sizeof(key));

    // One long comma separated line. A list of ids has no structure worth a
    // format of its own, and settings.json holds strings.
    ubuffer text;
    text.init(1024);

    const ulist<snowflake>& order = store::guild_order();
    for (unsigned int i = 0; i < order.count; i++)
    {
        char one[32];
        cnprint(one, sizeof(one), i ? ",%llu" : "%llu", order[i]);
        text.append(one, (unsigned int)ccslenf(one));
    }

    storage::settings_set(key, (const char*)text.c_str());
    storage::settings_save();
    text.free_buffer();

    g_guild_order_applied = true;
}

void ui_apply_saved_guild_order()
{
    if (g_guild_order_applied) return;
    if (!store::guild_order().count) return;      // nothing to order yet

    g_guild_order_applied = true;

    char key[64];
    guild_order_key(key, sizeof(key));

    const char* saved = storage::settings_get(key, "");
    if (!saved || !saved[0]) return;

    ulist<snowflake> ids;
    ids = ulist<snowflake>();

    const char* p = saved;
    while (*p)
    {
        while (*p == ',' || *p == ' ') p++;
        if (!*p) break;

        unsigned long long value = 0;
        while (*p >= '0' && *p <= '9') { value = value * 10 + (unsigned long long)(*p - '0'); p++; }
        if (value) ids.push(value);
        while (*p && *p != ',') p++;
    }

    if (ids.count)
    {
        store::guard guard;
        store::apply_guild_order(&ids);
    }
    ids.dispose();
}

void ui_proxy_editor_open(int slot, const proxy_config* cfg, bool own)
{
    g_ui.proxy_editing = slot;
    g_ui.proxy_own = own;
    g_ui.proxy_kind = cfg ? cfg->kind : PROXY_NONE;

    ccfset(g_ui.proxy_host, 0, sizeof(g_ui.proxy_host));
    ccfset(g_ui.proxy_user, 0, sizeof(g_ui.proxy_user));
    ccfset(g_ui.proxy_pass, 0, sizeof(g_ui.proxy_pass));
    ccfset(g_ui.proxy_port, 0, sizeof(g_ui.proxy_port));
    ccfset(g_ui.proxy_paste, 0, sizeof(g_ui.proxy_paste));

    if (!cfg) return;

    ccstrncpy(g_ui.proxy_host, cfg->host, sizeof(g_ui.proxy_host) - 1);
    ccstrncpy(g_ui.proxy_user, cfg->user, sizeof(g_ui.proxy_user) - 1);
    ccstrncpy(g_ui.proxy_pass, cfg->pass, sizeof(g_ui.proxy_pass) - 1);
    if (cfg->port) cnprint(g_ui.proxy_port, sizeof(g_ui.proxy_port), "%u", (unsigned int)cfg->port);
}

bool ui_proxy_editor(proxy_config* out, bool* own)
{
    bool saved = false;

    if (own)
    {
        bool flag = g_ui.proxy_own;
        if (ImGui::Checkbox(tr("Свой прокси для этого аккаунта"), &flag)) g_ui.proxy_own = flag;
        if (!g_ui.proxy_own)
        {
            ui_text_muted(tr("Пойдёт через общий"));
            if (ImGui::Button(tr("Сохранить"), ImVec2(110, 0)))
            {
                ccfset(out, 0, sizeof(*out));
                *own = false;
                saved = true;
            }
            return saved;
        }
    }

    // One line in, five fields out. A proxy arrives as a single string far
    // more often than as five separate values, and retyping it by hand is
    // where the typos come from.
    ImGui::SetNextItemWidth(-58.0f);
    bool entered = ImGui::InputTextWithHint("##ppaste", tr("socks5://логин:пароль@адрес:порт"),
                                            g_ui.proxy_paste, sizeof(g_ui.proxy_paste),
                                            ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if ((ImGui::Button(tr("Разобрать"), ImVec2(-1, 0)) || entered) && g_ui.proxy_paste[0])
    {
        proxy_config parsed;
        if (proxy::parse_url(g_ui.proxy_paste, &parsed))
        {
            ui_proxy_editor_open(g_ui.proxy_editing, &parsed, g_ui.proxy_own);
            g_ui.proxy_kind = parsed.kind;
        }
        else
        {
            ccfset(g_ui.proxy_paste, 0, sizeof(g_ui.proxy_paste));
            ccstrncpy(g_ui.proxy_paste, tr("не разобрал"), sizeof(g_ui.proxy_paste) - 1);
        }
    }

    const char* kinds[] = { tr("Без прокси"), "SOCKS5", "SOCKS4", "HTTPS" };
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##pkind", kinds[g_ui.proxy_kind & 3]))
    {
        for (int k = 0; k < 4; k++)
            if (ImGui::Selectable(kinds[k], g_ui.proxy_kind == k)) g_ui.proxy_kind = k;
        ImGui::EndCombo();
    }

    if (g_ui.proxy_kind != PROXY_NONE)
    {
        float full = ImGui::GetContentRegionAvail().x;

        ImGui::SetNextItemWidth(full * 0.66f);
        ImGui::InputTextWithHint("##phost", tr("адрес"), g_ui.proxy_host, sizeof(g_ui.proxy_host));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##pport", tr("порт"), g_ui.proxy_port, sizeof(g_ui.proxy_port),
                                 ImGuiInputTextFlags_CharsDecimal);

        ImGui::SetNextItemWidth(full * 0.5f - 4.0f);
        ImGui::InputTextWithHint("##puser", tr("логин"), g_ui.proxy_user, sizeof(g_ui.proxy_user));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##ppass", tr("пароль"), g_ui.proxy_pass, sizeof(g_ui.proxy_pass),
                                 ImGuiInputTextFlags_Password);

        // Said now rather than discovered when somebody tries to join a call.
        if (g_ui.proxy_kind == PROXY_HTTPS)
            ui_text_muted(tr("HTTPS не пропускает UDP - звонки будут недоступны"));
        else if (g_ui.proxy_kind == PROXY_SOCKS4)
            ui_text_muted(tr("SOCKS4 не умеет UDP - звонки будут недоступны"));
        else
            ui_text_muted(tr("Звонки пойдут через UDP ASSOCIATE, если прокси его умеет"));
    }

    proxy_config draft;
    ccfset(&draft, 0, sizeof(draft));
    draft.kind = g_ui.proxy_kind;
    ccstrncpy(draft.host, g_ui.proxy_host, sizeof(draft.host) - 1);
    ccstrncpy(draft.user, g_ui.proxy_user, sizeof(draft.user) - 1);
    ccstrncpy(draft.pass, g_ui.proxy_pass, sizeof(draft.pass) - 1);
    draft.port = (unsigned short)ccstrtoull(g_ui.proxy_port, 0, 10);

    if (ImGui::Button(tr("Сохранить"), ImVec2(110, 0)))
    {
        *out = draft;
        if (own) *own = g_ui.proxy_own;
        saved = true;
    }

    ImGui::SameLine();
    if (g_ui.proxy_kind != PROXY_NONE)
    {
        if (proxy::checking())
        {
            ImGui::BeginDisabled();
            ImGui::Button(tr("Проверяю..."), ImVec2(110, 0));
            ImGui::EndDisabled();
        }
        else if (ImGui::Button(tr("Проверить"), ImVec2(110, 0)))
        {
            proxy::begin_check(&draft);
        }
        ImGui::SameLine();
    }

    if (ImGui::Button(tr("Закрыть"), ImVec2(-1, 0))) g_ui.proxy_editing = -1;

    if (proxy::check_result()[0])
    {
        ImGui::PushStyleColor(ImGuiCol_Text, proxy::check_passed() ? col::green : col::red);
        ImGui::TextWrapped("%s", proxy::check_result());
        ImGui::PopStyleColor();
    }

    return saved;
}

void ui_view_accounts_popup()
{
    if (g_ui.show_accounts)
    {
        ImGui::OpenPopup("##accounts");
        g_ui.show_accounts = false;
        g_ui.adding_account = false;
        ccfset(g_ui.token_input, 0, sizeof(g_ui.token_input));
    }

    ImGui::SetNextWindowSize(ImVec2(340, 0));
    if (!ImGui::BeginPopup("##accounts")) return;

    bool busy = g_ui.login_busy != 0;

    {
        store::guard guard;
        duser* me = store::self();
        if (me)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, col::text_muted);
            ImGui::TextUnformatted(tr("Сейчас в аккаунте"));
            ImGui::PopStyleColor();

            ui_avatar(me, 20.0f, false);
            ImGui::SameLine();
            ImGui::TextUnformatted(me->display_name());

            ImGui::SameLine(0, 8);
            if (ImGui::SmallButton(tr("Профиль")))
            {
                ui_open_profile(me->id, 0);
                ImGui::CloseCurrentPopup();
            }

            // Discord never reports a client's own presence back to it, so
            // without setting this the client showed its owner as offline for
            // ever, which reads as invisible mode and is simply untrue.
            struct { const char* label; const char* value; unsigned char code; ImU32 tint; } choices[] = {
                { tr("В сети"),       "online",    STATUS_ONLINE,  col::green  },
                { tr("Неактивен"),    "idle",      STATUS_IDLE,    col::yellow },
                { tr("Не беспокоить"),"dnd",       STATUS_DND,     col::red    },
                { tr("Невидимка"),    "invisible", STATUS_OFFLINE, col::text_muted },
            };

            ImGui::Dummy(ImVec2(0, 4));
            for (int i = 0; i < 4; i++)
            {
                bool current = me->status == choices[i].code;

                ImGui::PushID(i);
                ImGui::PushStyleColor(ImGuiCol_Text, choices[i].tint);
                if (ImGui::RadioButton(choices[i].label, current) && !current)
                    gateway::set_status(choices[i].value);
                ImGui::PopStyleColor();
                ImGui::PopID();

                if (i < 3) ImGui::SameLine();
            }

            if (offline::active())
                ui_text_muted(tr("Статус сменится, когда связь вернётся"));

            ImGui::Separator();
        }
    }

    int count = storage::accounts_count();
    int active = storage::active_account();
    int switch_to = -1;
    int forget = -1;

    // The route everything takes unless an account asks for its own. First,
    // because it is the one most people will ever touch.
    {
        proxy_config shared = storage::default_proxy();
        const char* kinds[] = { tr("нет"), "SOCKS5", "SOCKS4", "HTTPS" };

        char summary[160];
        if (shared.in_use())
            cnprint(summary, sizeof(summary), "%s %s:%u", kinds[shared.kind & 3],
                    shared.host, (unsigned int)shared.port);
        else
            ccstrncpy(summary, tr("напрямую"), sizeof(summary) - 1);

        if (ImGui::SmallButton(g_ui.proxy_editing == PROXY_SLOT_DEFAULT ? tr("свернуть")
                                                                       : tr("общий прокси")))
        {
            if (g_ui.proxy_editing == PROXY_SLOT_DEFAULT) g_ui.proxy_editing = -1;
            else ui_proxy_editor_open(PROXY_SLOT_DEFAULT, &shared, true);
        }
        ImGui::SameLine();
        ui_text_muted(summary);

        if (g_ui.proxy_editing == PROXY_SLOT_DEFAULT)
        {
            proxy_config chosen;
            if (ui_proxy_editor(&chosen, 0))
            {
                storage::set_default_proxy(&chosen);
                apply_active_proxy();
                g_ui.proxy_editing = -1;
                ccfset(g_ui.proxy_pass, 0, sizeof(g_ui.proxy_pass));
            }
        }
        ImGui::Separator();
    }

    for (int i = 0; i < count; i++)
    {
        const saved_account* entry = storage::account_at(i);
        if (!entry) continue;

        ImGui::PushID(i);

        // The stored hash is enough to ask the cdn for the picture; a stack
        // user is the shortest way to reuse the url builder and the cache.
        duser shim;
        ccfset(&shim, 0, sizeof(shim));
        shim.id = entry->id;
        shim.avatar = entry->avatar[0] ? entry->avatar : 0;
        shim.discriminator = "0";

        ui_avatar(&shim, 24.0f, false);
        ImGui::SameLine();

        bool current = (i == active);
        ImGui::PushStyleColor(ImGuiCol_Text, current ? col::green : col::text_normal);

        char label[96];
        cnprint(label, sizeof(label), "%s%s", entry->name[0] ? entry->name : tr("Аккаунт"),
                current ? tr("  (сейчас)") : "");

        // Sized so every row lines up and the forget button always fits.
        if (ImGui::Selectable(label, current, 0, ImVec2(215.0f, 26.0f)) && !current && !busy)
            switch_to = i;
        ImGui::PopStyleColor();

        if (!current && ImGui::IsItemHovered())
            ImGui::SetTooltip(tr("Переключиться на этот аккаунт"));

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, col::bg_panel);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col::red);
        if (ImGui::SmallButton("x")) forget = i;
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Забыть этот аккаунт"));

        // One line per account saying where its traffic goes, and a way in.
        {
            const char* kinds[] = { tr("нет"), "SOCKS5", "SOCKS4", "HTTPS" };
            bool own = storage::account_overrides_proxy(i);

            char summary[192];
            if (!own)
                ccstrncpy(summary, tr("общий прокси"), sizeof(summary) - 1);
            else if (entry->proxy.in_use())
                cnprint(summary, sizeof(summary), "%s %s:%u", kinds[entry->proxy.kind & 3],
                        entry->proxy.host, (unsigned int)entry->proxy.port);
            else
                ccstrncpy(summary, tr("напрямую"), sizeof(summary) - 1);

            ImGui::Indent(30.0f);
            if (ImGui::SmallButton(g_ui.proxy_editing == i ? tr("свернуть") : tr("прокси")))
            {
                if (g_ui.proxy_editing == i) g_ui.proxy_editing = -1;
                else ui_proxy_editor_open(i, &entry->proxy, own);
            }
            ImGui::SameLine();
            ui_text_muted(summary);
            ImGui::Unindent(30.0f);
        }

        if (g_ui.proxy_editing == i)
        {
            ImGui::Indent(30.0f);

            proxy_config chosen;
            bool own = g_ui.proxy_own;
            if (ui_proxy_editor(&chosen, &own))
            {
                storage::account_set_proxy(i, &chosen, own);
                if (i == active) apply_active_proxy();
                g_ui.proxy_editing = -1;
                ccfset(g_ui.proxy_pass, 0, sizeof(g_ui.proxy_pass));
            }

            if (i == active)
                ui_text_muted(tr("Применится сразу, но открытый гейтвей переподключится "
                              "только при следующем входе"));

            ImGui::Separator();
            ImGui::Unindent(30.0f);
        }

        ImGui::PopID();
    }

    if (count > 0) ImGui::Separator();

    if (!g_ui.adding_account)
    {
        if (ImGui::Button(tr("Добавить аккаунт"), ImVec2(-1, 0))) g_ui.adding_account = true;
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_Text, col::text_muted);
        ImGui::TextUnformatted(tr("Токен нового аккаунта"));
        ImGui::PopStyleColor();

        ImGui::SetNextItemWidth(-1);
        bool entered = ImGui::InputText("##newtoken", g_ui.token_input, sizeof(g_ui.token_input),
                                        ImGuiInputTextFlags_Password |
                                        ImGuiInputTextFlags_EnterReturnsTrue);

        // Chosen before signing in, because signing in is itself a request and
        // it has to go the right way round.
        if (ImGui::SmallButton(g_ui.proxy_editing == PROXY_SLOT_NEW ? tr("свернуть прокси")
                                                                    : tr("прокси для этого аккаунта")))
        {
            if (g_ui.proxy_editing == PROXY_SLOT_NEW) g_ui.proxy_editing = -1;
            else ui_proxy_editor_open(PROXY_SLOT_NEW, &g_ui.new_proxy, g_ui.new_proxy_own);
        }
        ImGui::SameLine();
        ui_text_muted(g_ui.new_proxy_own && g_ui.new_proxy.in_use() ? g_ui.new_proxy.host
                                                                    : tr("общий"));

        if (g_ui.proxy_editing == PROXY_SLOT_NEW)
        {
            proxy_config chosen;
            bool own = g_ui.proxy_own;
            if (ui_proxy_editor(&chosen, &own))
            {
                g_ui.new_proxy = chosen;
                g_ui.new_proxy_own = own;
                g_ui.proxy_editing = -1;
            }
        }

    // Which kind of token this is. The two look alike, and the difference
    // decides the prefix on every request and the shape of the identify - so
    // it is asked once here rather than discovered by failing.
    ImGui::Checkbox(tr("Токен бота"), &g_ui.login_is_bot);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(tr("Бот не может добавлять в друзья и заходить по ссылке - "
                             "его добавляют через OAuth2. Телеметрия ему не шлётся."));
        ImGui::Dummy(ImVec2(0, 4));

        bool go = ImGui::Button(tr("Войти"), ImVec2(120, 0));
        ImGui::SameLine();
        if (ImGui::Button(tr("Отмена"), ImVec2(120, 0)))
        {
            g_ui.adding_account = false;
            ccfset(g_ui.token_input, 0, sizeof(g_ui.token_input));
        }

        if ((entered || go) && g_ui.token_input[0] && !busy)
        {
            // Signing in as somebody new ends the current session first: the
            // gateway, the voice connection and the whole cache belong to the
            // account being left.
            char token[512];
            ccstrncpy(token, g_ui.token_input, sizeof(token) - 1);
            ccfset(g_ui.token_input, 0, sizeof(g_ui.token_input));

            g_ui.adding_account = false;
            g_ui.pending_is_bot = g_ui.login_is_bot;
            ccstrncpy(g_ui.pending_token, token, sizeof(g_ui.pending_token) - 1);
            ccfset(token, 0, sizeof(token));

            ImGui::CloseCurrentPopup();
        }
    }

    if (busy)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, col::text_muted);
        ImGui::TextUnformatted(tr("Входим..."));
        ImGui::PopStyleColor();
    }

    if (g_ui.login_error[0])
    {
        ImGui::PushStyleColor(ImGuiCol_Text, col::red);
        ImGui::TextWrapped("%s", g_ui.login_error);
        ImGui::PopStyleColor();
    }

    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Button, col::bg_panel);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col::red);
    if (ImGui::Button(tr("Выйти"), ImVec2(-1, 0)))
    {
        g_ui.pending_logout = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::PopStyleColor(2);

    // Applied after the loop: both of these change the list underneath it.
    if (forget >= 0)
    {
        // Forgetting the one currently signed in also ends the session; there
        // would be nothing left to write it back to.
        bool was_active = (forget == active);
        if (was_active) g_ui.pending_logout = true;
        storage::account_forget(forget);
    }
    else if (switch_to >= 0)
    {
        g_ui.pending_account = switch_to;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void ui_view_login()
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImVec2 center(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f);

    // The window grows to fit however many accounts are remembered, so signing
    // back in as one of them is a click rather than another paste.
    int remembered = storage::accounts_count();
    float proxy_room = (g_ui.proxy_editing == PROXY_SLOT_DEFAULT) ? 230.0f : 24.0f;
    ImVec2 size(460.0f, 330.0f + proxy_room +
                        (remembered > 0 ? 46.0f + remembered * 30.0f : 0.0f));
    ImGui::SetNextWindowPos(ImVec2(center.x - size.x * 0.5f, center.y - size.y * 0.5f));
    ImGui::SetNextWindowSize(size);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4(col::bg_panel));
    ImGui::Begin("##login", 0, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                               ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);

    ImGui::PushFont(g_app.font_big);
    ImGui::TextUnformatted("IMDiscord");
    ImGui::PopFont();

    ui_text_muted(tr("Вход по токену аккаунта"));
    ImGui::Dummy(ImVec2(0, 6));

    // Reachable before signing in, because a proxy is often the reason
    // signing in does not work. Editing it here edits the shared one: there
    // is no account yet to attach it to.
    {
        proxy_config shared = storage::default_proxy();
        const char* kinds[] = { tr("нет"), "SOCKS5", "SOCKS4", "HTTPS" };

        char summary[160];
        if (shared.in_use())
            cnprint(summary, sizeof(summary), "%s %s:%u", kinds[shared.kind & 3],
                    shared.host, (unsigned int)shared.port);
        else
            ccstrncpy(summary, tr("соединение напрямую"), sizeof(summary) - 1);

        if (ImGui::SmallButton(g_ui.proxy_editing == PROXY_SLOT_DEFAULT ? tr("свернуть")
                                                                       : tr("прокси")))
        {
            if (g_ui.proxy_editing == PROXY_SLOT_DEFAULT) g_ui.proxy_editing = -1;
            else ui_proxy_editor_open(PROXY_SLOT_DEFAULT, &shared, true);
        }
        ImGui::SameLine();
        ui_text_muted(summary);

        if (g_ui.proxy_editing == PROXY_SLOT_DEFAULT)
        {
            proxy_config chosen;
            if (ui_proxy_editor(&chosen, 0))
            {
                storage::set_default_proxy(&chosen);
                apply_active_proxy();
                g_ui.proxy_editing = -1;
                ccfset(g_ui.proxy_pass, 0, sizeof(g_ui.proxy_pass));
            }
        }
    }

    ImGui::Dummy(ImVec2(0, 6));

    ImGui::TextUnformatted(tr("Токен"));

    ImGuiInputTextFlags token_flags = ImGuiInputTextFlags_EnterReturnsTrue;
    if (!g_ui.token_visible) token_flags |= ImGuiInputTextFlags_Password;

    ImGui::SetNextItemWidth(-96.0f);
    bool submitted = ImGui::InputText("##token", g_ui.token_input, sizeof(g_ui.token_input), token_flags);

    ImGui::SameLine();
    if (ui_icon_button(g_ui.token_visible ? tr("Скрыть##tok") : tr("Показать##tok"), ImVec2(88, 0),
                       col::bg_input, col::bg_hover))
        g_ui.token_visible = !g_ui.token_visible;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(tr("Вырезание и копирование работают только в открытом виде"));

    ImGui::Dummy(ImVec2(0, 6));

    // Which kind of token this is. The two look alike, and the difference
    // decides the prefix on every request and the shape of the identify - so
    // it is asked once here rather than discovered by failing.
    ImGui::Checkbox(tr("Токен бота"), &g_ui.login_is_bot);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(tr("Бот не может добавлять в друзья и заходить по ссылке - "
                             "его добавляют через OAuth2. Телеметрия ему не шлётся."));

    ImGui::Dummy(ImVec2(0, 8));

    if (g_ui.login_busy)
    {
        ui_text_muted(tr("Проверка токена..."));
    }
    else
    {
        if (ImGui::Button(tr("Войти"), ImVec2(140, 34)) || submitted)
            begin_login(g_ui.token_input, g_ui.login_is_bot);
    }

    // Always offered, including while an attempt is still spinning. A server
    // that is not answering used to hold the whole client hostage: the archive
    // was sitting on disk and there was no way in to it, which is exactly
    // backwards from what it is for.
    if (remembered > 0)
    {
        int fallback = storage::active_account();
        if (fallback < 0) fallback = 0;

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, col::bg_input);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col::bg_hover);
        if (ImGui::Button(tr("Открыть сохранённое"), ImVec2(200, 34)))
            g_ui.pending_offline = fallback;
        ImGui::PopStyleColor(2);

        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(tr("Не ждать ответа: открыть серверы, друзей и переписку из "
                              "архива. Подключение можно будет повторить в любой момент."));
    }

    if (g_ui.login_error[0])
    {
        ImGui::Dummy(ImVec2(0, 6));
        ImGui::PushStyleColor(ImGuiCol_Text, col::red);
        ImGui::TextWrapped("%s", g_ui.login_error);
        ImGui::PopStyleColor();
    }

    if (remembered > 0)
    {
        ImGui::Dummy(ImVec2(0, 10));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 6));
        ui_text_muted(tr("Сохранённые аккаунты"));

        int pick = -1;
        int drop = -1;

        for (int i = 0; i < remembered; i++)
        {
            const saved_account* entry = storage::account_at(i);
            if (!entry) continue;

            ImGui::PushID(i);

            duser shim;
            ccfset(&shim, 0, sizeof(shim));
            shim.id = entry->id;
            shim.avatar = entry->avatar[0] ? entry->avatar : 0;
            shim.discriminator = "0";

            ui_avatar(&shim, 22.0f, false);
            ImGui::SameLine();

            if (ImGui::Selectable(entry->name[0] ? entry->name : tr("Аккаунт"), false, 0,
                                  ImVec2(270.0f, 24.0f)) && !g_ui.login_busy)
                pick = i;

            ImGui::SameLine();
            if (ImGui::SmallButton(tr("архив"))) g_ui.pending_offline = i;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(tr("Открыть этот аккаунт из сохранённого, без подключения"));

            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, col::bg_panel);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col::red);
            if (ImGui::SmallButton("x")) drop = i;
            ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Забыть"));

            ImGui::PopID();
        }

        // Applied after the loop, which is walking the list being changed.
        if (drop >= 0) storage::account_forget(drop);
        else if (pick >= 0) g_ui.pending_account = pick;
    }

    ImGui::Dummy(ImVec2(0, 14));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 8));

    ImGui::PushStyleColor(ImGuiCol_Text, col::text_muted);
    ImGui::TextWrapped(tr("Токены сохраняются зашифрованными и привязаны к этому компьютеру. "
                       "На другой машине файл не расшифруется."));
    ImGui::PopStyleColor();

    if (storage::has_token())
    {
        ImGui::Dummy(ImVec2(0, 6));
        if (ImGui::Button(tr("Удалить сохранённый токен"), ImVec2(-1, 28)))
        {
            storage::clear_token();
            ccfset(g_ui.login_error, 0, sizeof(g_ui.login_error));
        }
    }

    ImGui::End();
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------------------
// root
// ---------------------------------------------------------------------------

void ui_init()
{
    ccfset(&g_ui, 0, sizeof(g_ui));
    g_ui.pending_files = ulist<upload_file>();
    // Zero is a valid account index, so "nothing pending" has to be spelled out
    // or the first frame would switch to the first account.
    g_ui.pending_account = -1;
    g_ui.pending_offline = -1;
    // Land on the friends list, the way the real client does, instead of an
    // empty pane that looks like nothing loaded.
    g_ui.show_friends = true;
    g_ui.proxy_editing = -1;

    storage::settings_load();
    audio::set_input_gain((float)storage::settings_get_int("input_gain", 100) / 100.0f);
    audio::set_output_gain((float)storage::settings_get_int("output_gain", 100) / 100.0f);

    noise::set_mode(storage::settings_get_int("noise_mode", NOISE_OFF));
    noise::set_gate_threshold((float)storage::settings_get_int("gate_threshold", 20) / 1000.0f);

    g_ui.show_hidden_channels = storage::settings_get_int("show_hidden_channels", 1) != 0;

    vad::init();
    vad::set_enabled(storage::settings_get_int("vad_enabled", 1) != 0);
    vad::set_automatic(storage::settings_get_int("vad_auto", 1) != 0);
    vad::set_threshold((float)storage::settings_get_int("vad_threshold", 20) / 1000.0f);

    {
        wchar_t id[512];
        chartowcs(storage::settings_get("input_device", ""), id, 512);
        audio::set_device(true, id);

        chartowcs(storage::settings_get("output_device", ""), id, 512);
        audio::set_device(false, id);
    }

    proxy::init();
    player::init();
    censor::init();
    http::init("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    apply_active_proxy();
    jobs::init(6);
    tex::init();
    store::init();
    offline::init();
    api::init();
    voice::init();
    screenshare::init();
    streamview::init();

#ifdef IMD_VOICE_TEST
    {
        // Test-only: sign in with a token from the environment and leave the
        // stored one untouched.
        char token[512];
        DWORD n = GetEnvironmentVariableA("IMD_TOKEN", token, sizeof(token));
        if (n > 0 && n < sizeof(token))
        {
            g_ephemeral_login = true;
            log_line("ui: test login from the environment");
            begin_login(token, false);
            ccfset(token, 0, sizeof(token));
            return;
        }
    }
#endif

    // Whichever account was last signed in. accounts_load carries a token from
    // before the list existed into the first slot, so an upgrade signs straight
    // back in rather than asking for it again.
    int active = storage::active_account();
    const saved_account* entry = storage::account_at(active);
    if (entry && entry->token[0])
    {
        log_line("ui: signing in as the last used account");
        char token[256];
        ccstrncpy(token, entry->token, sizeof(token) - 1);
        begin_login(token, entry->is_bot);
        ccfset(token, 0, sizeof(token));
    }
}

// Whether the next frame has to be drawn at once, or whether the client can
// wait for something to happen first.
//
// A chat window at rest is a still picture, and drawing it a hundred and forty
// times a second is most of what this process costs when nobody is touching
// it. The whole interface is rebuilt every frame - every guild walked, every
// channel, the voice roster with a lock taken per person - so the saving is
// the whole of that work, not just the drawing.
//
// Anything that moves by itself has to be named here, because nothing else
// will ask for a redraw: a picture arriving, a level meter, a typing line
// running out, or the store changing underneath.
bool ui_wants_redraw()
{
    if (voice::state() != VOICE_IDLE) return true;          // meters, speaking rings
    if (streamview::state() != WATCH_IDLE) return true;     // somebody's screen
    if (voice::watched_camera()) return true;               // somebody's camera

    screenshare_state share = screenshare::state();
    if (share != SHARE_IDLE && share != SHARE_FAILED) return true;

    // Something arrived, was edited or was deleted since the last frame drew.
    static unsigned int seen = 0;
    unsigned int now = store::revision();
    if (now != seen) { seen = now; return true; }

    return false;
}

void ui_frame()
{
    // Anything that tears the session down waits for here, before a single
    // pointer out of the store has been read this frame.
    if (g_ui.pending_logout)
    {
        g_ui.pending_logout = false;
        logout();
    }
    if (g_ui.pending_account >= 0)
    {
        int index = g_ui.pending_account;
        g_ui.pending_account = -1;
        switch_account(index);
    }
    if (g_ui.pending_offline >= 0)
    {
        int index = g_ui.pending_offline;
        g_ui.pending_offline = -1;
        open_saved_session(index, OFFLINE_NO_NETWORK);
    }
    if (g_ui.pending_token[0])
    {
        char token[512];
        ccstrncpy(token, g_ui.pending_token, sizeof(token) - 1);
        ccfset(g_ui.pending_token, 0, sizeof(g_ui.pending_token));

        bool bot = g_ui.pending_is_bot;
        g_ui.pending_is_bot = false;

        tear_down_session();
        begin_login(token, bot);
        ccfset(token, 0, sizeof(token));
    }

    // Every quarter hour the friend list, the servers and everybody's profile
    // are written down. None of it can be fetched again once the account is
    // unreachable, and that is exactly when it is wanted.
    if (archive::ready() && !offline::active())
    {
        static unsigned long long next_snapshot = 0;
        unsigned long long now = GetTickCount64();

        if (!next_snapshot) next_snapshot = now + 60ULL * 1000ULL;
        if (now >= next_snapshot)
        {
            next_snapshot = now + 15ULL * 60ULL * 1000ULL;
            archive::snapshot_save();
        }
    }

    consume_dropped_files();
    tex::advance_animations();
    // Before the UI is built, so nothing released here can still be sitting in
    // a draw list from the frame that just went out.
    tex::collect();

#ifdef IMD_VOICE_TEST
    // Test-only: join a voice channel right after READY so the DAVE handshake
    // can be exercised without driving the UI. Never compiled into a release.
    {
        static bool auto_joined = false;
        if (!auto_joined && gateway::state() == GW_READY)
        {
            char spec[128];
            DWORD n = GetEnvironmentVariableA("IMD_AUTOJOIN", spec, sizeof(spec));
            if (n > 0 && n < sizeof(spec))
            {
                char* comma = 0;
                for (char* p = spec; *p; p++) if (*p == ',') comma = p;
                if (comma)
                {
                    *comma = 0;
                    voice::join(ccstrtoull(spec, 0, 10), ccstrtoull(comma + 1, 0, 10));
                    auto_joined = true;
                }
            }
        }

        // And start a Go Live stream once the voice side is up, so the whole
        // path can be exercised without a hand on the mouse.
        static bool auto_shared = false;
        if (auto_joined && !auto_shared && voice::state() == VOICE_CONNECTED)
        {
            char on[8];
            if (GetEnvironmentVariableA("IMD_AUTOSHARE", on, sizeof(on)) > 0)
            {
                auto_shared = true;
                screenshare::start(0, 1280, 720, 30, 2500);
            }
        }
    }
#endif


    if (!g_ui.logged_in)
    {
        ui_view_login();
        return;
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();

    // The bar is not an overlay: the client is pushed down so nothing hides
    // behind it.
    float banner_h = offline::active() ? OFFLINE_BANNER_HEIGHT : 0.0f;
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + banner_h));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, vp->WorkSize.y - banner_h));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4(col::bg_deep));
    ImGui::Begin("##root", 0,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    float total_w = vp->WorkSize.x;
    float total_h = vp->WorkSize.y - banner_h;

    bool show_members = g_ui.active_guild != 0 && total_w > 900.0f;
    float members_w = show_members ? MEMBERS_WIDTH : 0.0f;
    float chat_w = total_w - RAIL_WIDTH - LIST_WIDTH - members_w;
    if (chat_w < 200.0f) chat_w = 200.0f;

    store::guard guard;

    ImGui::BeginChild("##rail", ImVec2(RAIL_WIDTH, total_h), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ui_view_guild_rail(RAIL_WIDTH, total_h);
    ImGui::EndChild();

    ImGui::SameLine(0, 0);
    ImGui::BeginChild("##list", ImVec2(LIST_WIDTH, total_h), false, ImGuiWindowFlags_NoScrollbar);
    ui_view_channel_list(LIST_WIDTH, total_h - ui_voice_panel_height());
    ui_view_voice_panel(LIST_WIDTH);
    ImGui::EndChild();

    ImGui::SameLine(0, 0);
    ImGui::BeginChild("##main", ImVec2(chat_w, total_h), false, ImGuiWindowFlags_NoScrollbar);
    if (g_ui.show_friends) ui_view_friends(chat_w, total_h);
    else ui_view_chat(chat_w, total_h);
    ImGui::EndChild();

    if (show_members)
    {
        ImGui::SameLine(0, 0);
        ImGui::BeginChild("##members", ImVec2(members_w, total_h), false);
        ui_view_members(members_w, total_h);
        ImGui::EndChild();
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    ui_apply_saved_guild_order();

    // What arrives in the channel being looked at is read on arrival, so the
    // store needs to know which one that is.
    {
        static snowflake last_open = 0;
        store::set_open_channel(g_ui.active_channel);

        if (g_ui.active_channel && g_ui.active_channel != last_open)
        {
            last_open = g_ui.active_channel;

            store::guard guard;
            dchannel* c = store::find_channel(g_ui.active_channel);
            if (c && c->unread())
            {
                snowflake newest = c->last_message_id;
                store::mark_channel_read(c->id);
                if (newest) api::ack_message(c->id, newest);
            }
        }
    }

    ui_view_profile_popup();
    ui_view_server_info_popup();
    ui_view_roles_popup();
    ui_view_invites_popup();
    ui_view_webhooks_popup();
    ui_view_new_channel_popup();
    ui_view_channel_perms_popup();
    ui_view_privacy_popup();
    ui_view_audit_popup();
    ui_view_music_popup();
    ui_view_ownership_popup();
    ui_view_delete_guild_popup();
    ui_view_guild_edit_popup();
    ui_view_channel_info_popup();
    ui_view_share_popup();

    // Lets the player notice a video nobody is looking at any more.
    if (ui_video_player()) player::tick();
    ui_view_settings_popup();
    ui_view_image_viewer();
    ui_view_stream_window();
    ui_view_camera_window();
    ui_view_offline_banner();
    ui_view_accounts_popup();

    // Global shortcut: logging out is always available.
    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Q, false)) logout();
}

void ui_shutdown()
{
    gateway::stop();
    streamview::shutdown();
    screenshare::shutdown();
    voice::shutdown();

    storage::settings_set_int("input_gain", (int)(audio::input_gain() * 100.0f));
    storage::settings_set_int("output_gain", (int)(audio::output_gain() * 100.0f));
    storage::settings_save();

    ui_clear_attachments();
    g_ui.pending_files.dispose();

    jobs::shutdown();
    tex::shutdown();
    api::shutdown();
    store::shutdown();
    player::shutdown();
    censor::shutdown();
    http::shutdown();
    proxy::shutdown();
}
