#include "pch.h"
#include "types.h"

namespace
{
    const char* CDN = "https://cdn.discordapp.com";

    bool is_animated_hash(const char* hash)
    {
        return hash && hash[0] == 'a' && hash[1] == '_';
    }

    // An "a_" prefix marks an animated upload. The CDN no longer keeps a gif
    // for those and answers 415 "Invalid resource" if one is asked for; the
    // animation is only served as animated WebP, and only when the flag is set
    // explicitly, otherwise a single still frame comes back.
    void avatar_style_url(char* out, int cap, const char* kind, snowflake id,
                          const char* hash, int size)
    {
        if (is_animated_hash(hash))
            cnprint(out, cap, "%s/%s/%llu/%s.webp?size=%d&animated=true", CDN, kind, id, hash, size);
        else
            cnprint(out, cap, "%s/%s/%llu/%s.png?size=%d", CDN, kind, id, hash, size);
    }
}

void cdn::user_avatar(const duser* u, int size, char* out, int cap)
{
    if (!u)
    {
        cnprint(out, cap, "%s/embed/avatars/0.png", CDN);
        return;
    }

    if (u->avatar && u->avatar[0])
    {
        avatar_style_url(out, cap, "avatars", u->id, u->avatar, size);
        return;
    }

    int index;
    if (u->discriminator && u->discriminator[0] && ccscmp(u->discriminator, "0") != 0)
        index = ccstrti(u->discriminator) % 5;
    else
        index = (int)((u->id >> 22) % 6);

    cnprint(out, cap, "%s/embed/avatars/%d.png", CDN, index);
}

void cdn::user_banner(const duser* u, int size, char* out, int cap)
{
    if (!u || !u->banner || !u->banner[0]) { out[0] = 0; return; }
    avatar_style_url(out, cap, "banners", u->id, u->banner, size);
}

void cdn::guild_icon(const dguild* g, int size, char* out, int cap)
{
    if (!g || !g->icon || !g->icon[0]) { out[0] = 0; return; }
    avatar_style_url(out, cap, "icons", g->id, g->icon, size);
}

void cdn::channel_icon(const dchannel* c, int size, char* out, int cap)
{
    if (!c || !c->icon || !c->icon[0]) { out[0] = 0; return; }
    cnprint(out, cap, "%s/channel-icons/%llu/%s.png?size=%d", CDN, c->id, c->icon, size);
}

void cdn::custom_emoji(snowflake emoji_id, bool animated, int size, char* out, int cap)
{
    cnprint(out, cap, "%s/emojis/%llu.%s?size=%d", CDN, emoji_id, animated ? "gif" : "png", size);
}

// ---------------------------------------------------------------------------

static int read_int(const char* s, int digits)
{
    int v = 0;
    for (int i = 0; i < digits; i++)
    {
        if (s[i] < '0' || s[i] > '9') return v;
        v = v * 10 + (s[i] - '0');
    }
    return v;
}

void format_epoch_ms(unsigned long long ms, char* out, int cap)
{
    if (cap > 0) out[0] = 0;
    if (!ms) return;

    // FILETIME counts 100 ns ticks from 1601; unix time counts milliseconds
    // from 1970. The constant is the gap between the two epochs.
    unsigned long long ticks = ms * 10000ULL + 116444736000000000ULL;

    FILETIME ft;
    ft.dwLowDateTime = (DWORD)(ticks & 0xFFFFFFFFULL);
    ft.dwHighDateTime = (DWORD)(ticks >> 32);

    FILETIME local_ft;
    if (!FileTimeToLocalFileTime(&ft, &local_ft)) local_ft = ft;

    SYSTEMTIME st;
    if (!FileTimeToSystemTime(&local_ft, &st)) return;

    cnprint(out, cap, "%02d.%02d.%04d, %02d:%02d",
            st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute);
}

void format_timestamp(const char* iso, char* out, int cap)
{
    if (!iso || ccslenf(iso) < 19)
    {
        if (cap > 0) out[0] = 0;
        return;
    }

    SYSTEMTIME utc;
    ccfset(&utc, 0, sizeof(utc));
    utc.wYear = (WORD)read_int(iso + 0, 4);
    utc.wMonth = (WORD)read_int(iso + 5, 2);
    utc.wDay = (WORD)read_int(iso + 8, 2);
    utc.wHour = (WORD)read_int(iso + 11, 2);
    utc.wMinute = (WORD)read_int(iso + 14, 2);
    utc.wSecond = (WORD)read_int(iso + 17, 2);

    SYSTEMTIME local;
    if (!SystemTimeToTzSpecificLocalTime(0, &utc, &local)) local = utc;

    SYSTEMTIME now;
    GetLocalTime(&now);

    if (local.wYear == now.wYear && local.wMonth == now.wMonth && local.wDay == now.wDay)
        cnprint(out, cap, "%02d:%02d", local.wHour, local.wMinute);
    else
        cnprint(out, cap, "%02d.%02d.%04d %02d:%02d", local.wDay, local.wMonth, local.wYear, local.wHour, local.wMinute);
}
