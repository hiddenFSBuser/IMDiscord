#include "pch.h"
#include "people.h"
#include "store.h"
#include "core/storage.h"
#include "core/log.h"
#include "net/json.h"
#include "system/io/ufile.h"

namespace
{
    // One name and picture per person, so a profile still reads with the store
    // belonging to somebody else or with nothing to read from at all.
    struct known_user
    {
        snowflake id;
        char name[64];
        char avatar[64];
    };

    struct known_guild
    {
        snowflake id;
        char name[96];
        char icon[64];
    };

    struct seen_row
    {
        snowflake user_id;
        snowflake guild_id;
        snowflake account_id;
        unsigned long long when_ms;
    };

    struct friend_row
    {
        snowflake about;           // whose profile this was read from
        snowflake friend_id;
        snowflake account_id;
        unsigned long long when_ms;
        bool gone;
    };

    CRITICAL_SECTION g_lock;
    bool g_ready = false;
    bool g_dirty = false;
    unsigned long long g_saved_ms = 0;
    unsigned long long g_swept_ms = 0;

    ulist<known_user> g_users;
    ulist<known_guild> g_guilds;
    ulist<seen_row> g_seen;
    ulist<friend_row> g_friends;

    // Both of these exist because the sweep is the hot path and everything
    // else is not. It walks every member of every server every half minute,
    // and asks two questions per person: do I know this name, and have I
    // already filed this sighting. Answered by walking the lists, that is
    // tens of thousands of comparisons per person and a visible stall on the
    // thread that draws the window.
    umap<snowflake, unsigned int> g_user_at;      // id -> index into g_users
    uset<unsigned long long> g_seen_keys;         // person+server+account

    const unsigned long long SAVE_EVERY_MS = 30000;
    const unsigned long long SWEEP_EVERY_MS = 30000;

    // Bounded, and the bounds are not arbitrary.
    //
    // Everything here lives in sorted arrays, which is this codebase's idiom
    // and the right one for lists that are read far more often than written.
    // The price is that an insert in the middle shifts what follows it, so
    // the cost of filling one of these is quadratic in its length. At sixty
    // thousand that is a fraction of a second spread over a warm-up; at a
    // million it is minutes, and at ten million the client would never
    // finish starting.
    //
    // So they stop rather than degrade. Somebody feeding this every member of
    // a thousand servers gets the first sixty thousand and a line in the log
    // saying so, which is a great deal better than a client that hangs.
    const unsigned int MAX_USERS = 60000;
    const unsigned int MAX_GUILDS = 4000;
    const unsigned int MAX_SEEN = 60000;
    const unsigned int MAX_FRIENDS = 20000;

    bool g_said_full = false;

    void say_full(const char* what, unsigned int cap)
    {
        if (g_said_full) return;
        g_said_full = true;
        log_line("people: предел %s (%u) - дальше не записываю", what, cap);
    }

    // One number standing for "this person, in this server, seen by this
    // account". Mixed rather than concatenated: the three are all snowflakes
    // and share their high bits, so anything simpler collides on the half
    // that actually differs.
    unsigned long long seen_key(snowflake user_id, snowflake guild_id, snowflake account)
    {
        unsigned long long h = 1469598103934665603ULL;
        unsigned long long parts[3] = { user_id, guild_id, account };

        for (int i = 0; i < 3; i++)
        {
            h ^= parts[i];
            h *= 1099511628211ULL;
        }
        return h;
    }

    void path_of(wchar_t* out, int cap)
    {
        if (!ufile::app_path(L"people.json", out, cap)) out[0] = 0;
    }

    known_user* find_user(snowflake id)
    {
        unsigned int* at = g_user_at.find(id);
        return at ? &g_users[*at] : 0;
    }

    known_user* add_user(snowflake id)
    {
        known_user fresh;
        ccfset(&fresh, 0, sizeof(fresh));
        fresh.id = id;

        g_users.push(fresh);
        g_user_at.insert(id, g_users.count - 1);
        return &g_users[g_users.count - 1];
    }

    known_guild* find_guild(snowflake id)
    {
        for (unsigned int i = 0; i < g_guilds.count; i++)
            if (g_guilds[i].id == id) return &g_guilds[i];
        return 0;
    }

    void put_str(char* dst, int cap, const char* src)
    {
        ccfset(dst, 0, (unsigned int)cap);
        if (src) ccstrncpy(dst, src, (size_t)cap - 1);
    }

    // Sorted before writing, so reading it back is cheap.
    //
    // Both of the tables these load into keep themselves in order, and an
    // insert lands wherever it belongs - which means loading a file in
    // arbitrary order shifts half the table on every one of sixty thousand
    // rows. Written in ascending order every insert is an append instead, and
    // start-up goes from tens of seconds to none.
    template <typename T, typename LESS>
    void sort_rows(ulist<T>& list, LESS less)
    {
        if (list.count < 2) return;

        // Iterative, with the smaller half pushed and the larger looped, so a
        // sorted or reversed file cannot run the stack out.
        struct span { int lo, hi; };
        span stack[64];
        int top = 0;

        stack[top].lo = 0;
        stack[top].hi = (int)list.count - 1;
        top++;

        while (top > 0)
        {
            top--;
            int lo = stack[top].lo;
            int hi = stack[top].hi;

            while (lo < hi)
            {
                T pivot = list[(unsigned int)(lo + (hi - lo) / 2)];
                int i = lo, j = hi;

                while (i <= j)
                {
                    while (less(list[(unsigned int)i], pivot)) i++;
                    while (less(pivot, list[(unsigned int)j])) j--;

                    if (i <= j)
                    {
                        T t = list[(unsigned int)i];
                        list[(unsigned int)i] = list[(unsigned int)j];
                        list[(unsigned int)j] = t;
                        i++;
                        j--;
                    }
                }

                if (j - lo < hi - i)
                {
                    if (lo < j && top < 63) { stack[top].lo = lo; stack[top].hi = j; top++; }
                    lo = i;
                }
                else
                {
                    if (i < hi && top < 63) { stack[top].lo = i; stack[top].hi = hi; top++; }
                    hi = j;
                }
            }
        }
    }

    struct user_less
    {
        bool operator()(const known_user& a, const known_user& b) const { return a.id < b.id; }
    };

    struct seen_less
    {
        bool operator()(const seen_row& a, const seen_row& b) const
        {
            return seen_key(a.user_id, a.guild_id, a.account_id) <
                   seen_key(b.user_id, b.guild_id, b.account_id);
        }
    };

    void write_file()
    {
        // In the order the reader wants them. The index has to be rebuilt
        // afterwards because every position in it has just moved.
        sort_rows(g_users, user_less());
        sort_rows(g_seen, seen_less());

        g_user_at.dispose();
        g_user_at = umap<snowflake, unsigned int>();
        for (unsigned int i = 0; i < g_users.count; i++) g_user_at.insert(g_users[i].id, i);

        jwriter w;
        w.init();
        w.begin_obj();

        w.key("users");
        w.begin_arr();
        for (unsigned int i = 0; i < g_users.count; i++)
        {
            w.begin_obj();
            w.kv_snowflake("id", g_users[i].id);
            if (g_users[i].name[0]) w.kv_str("name", g_users[i].name);
            if (g_users[i].avatar[0]) w.kv_str("avatar", g_users[i].avatar);
            w.end_obj();
        }
        w.end_arr();

        w.key("guilds");
        w.begin_arr();
        for (unsigned int i = 0; i < g_guilds.count; i++)
        {
            w.begin_obj();
            w.kv_snowflake("id", g_guilds[i].id);
            if (g_guilds[i].name[0]) w.kv_str("name", g_guilds[i].name);
            if (g_guilds[i].icon[0]) w.kv_str("icon", g_guilds[i].icon);
            w.end_obj();
        }
        w.end_arr();

        w.key("seen");
        w.begin_arr();
        for (unsigned int i = 0; i < g_seen.count; i++)
        {
            w.begin_obj();
            w.kv_snowflake("u", g_seen[i].user_id);
            w.kv_snowflake("g", g_seen[i].guild_id);
            w.kv_snowflake("a", g_seen[i].account_id);
            w.kv_i64("t", (long long)g_seen[i].when_ms);
            w.end_obj();
        }
        w.end_arr();

        w.key("friends");
        w.begin_arr();
        for (unsigned int i = 0; i < g_friends.count; i++)
        {
            w.begin_obj();
            w.kv_snowflake("about", g_friends[i].about);
            w.kv_snowflake("f", g_friends[i].friend_id);
            w.kv_snowflake("a", g_friends[i].account_id);
            w.kv_i64("t", (long long)g_friends[i].when_ms);
            if (g_friends[i].gone) w.kv_bool("gone", true);
            w.end_obj();
        }
        w.end_arr();

        w.end_obj();

        wchar_t path[MAX_PATH];
        path_of(path, MAX_PATH);
        if (path[0]) ufile::write_all(path, w.buf.data, w.buf.size);

        w.free_writer();
    }

    void read_file()
    {
        wchar_t path[MAX_PATH];
        path_of(path, MAX_PATH);
        if (!path[0]) return;

        ubuffer buf;
        buf.init();
        if (!ufile::read_all(path, &buf)) { buf.free_buffer(); return; }

        jdoc doc;
        doc.init();

        if (doc.parse((const char*)buf.data, (int)buf.size))
        {
            const jval* arr = doc.root->arr("users");
            for (unsigned int i = 0; i < arr->count; i++)
            {
                const jval* v = arr->at(i);
                known_user u;
                ccfset(&u, 0, sizeof(u));
                u.id = v->sf("id");
                put_str(u.name, sizeof(u.name), v->str("name", 0));
                put_str(u.avatar, sizeof(u.avatar), v->str("avatar", 0));
                if (u.id && !g_user_at.find(u.id))
                {
                    g_users.push(u);
                    g_user_at.insert(u.id, g_users.count - 1);
                }
            }

            arr = doc.root->arr("guilds");
            for (unsigned int i = 0; i < arr->count; i++)
            {
                const jval* v = arr->at(i);
                known_guild g;
                ccfset(&g, 0, sizeof(g));
                g.id = v->sf("id");
                put_str(g.name, sizeof(g.name), v->str("name", 0));
                put_str(g.icon, sizeof(g.icon), v->str("icon", 0));
                if (g.id) g_guilds.push(g);
            }

            arr = doc.root->arr("seen");
            for (unsigned int i = 0; i < arr->count; i++)
            {
                const jval* v = arr->at(i);
                seen_row r;
                ccfset(&r, 0, sizeof(r));
                r.user_id = v->sf("u");
                r.guild_id = v->sf("g");
                r.account_id = v->sf("a");
                r.when_ms = (unsigned long long)v->i64("t", 0);
                if (r.user_id && r.guild_id)
                {
                    g_seen.push(r);
                    g_seen_keys.push(seen_key(r.user_id, r.guild_id, r.account_id));
                }
            }

            arr = doc.root->arr("friends");
            for (unsigned int i = 0; i < arr->count; i++)
            {
                const jval* v = arr->at(i);
                friend_row r;
                ccfset(&r, 0, sizeof(r));
                r.about = v->sf("about");
                r.friend_id = v->sf("f");
                r.account_id = v->sf("a");
                r.when_ms = (unsigned long long)v->i64("t", 0);
                r.gone = v->boolean("gone", false);
                if (r.about && r.friend_id) g_friends.push(r);
            }
        }

        doc.free_doc();
        buf.free_buffer();

        log_line("people: загружено %u человек, %u серверов, %u встреч, %u дружб",
                 g_users.count, g_guilds.count, g_seen.count, g_friends.count);
    }
}

void people::init()
{
    if (g_ready) return;

    InitializeCriticalSection(&g_lock);
    g_users = ulist<known_user>();
    g_guilds = ulist<known_guild>();
    g_seen = ulist<seen_row>();
    g_friends = ulist<friend_row>();
    g_user_at = umap<snowflake, unsigned int>();
    g_seen_keys = uset<unsigned long long>();
    g_ready = true;

    EnterCriticalSection(&g_lock);
    read_file();
    LeaveCriticalSection(&g_lock);

    g_saved_ms = GetTickCount64();
}

void people::shutdown()
{
    if (!g_ready) return;

    EnterCriticalSection(&g_lock);
    if (g_dirty) write_file();
    g_dirty = false;
    LeaveCriticalSection(&g_lock);

    g_users.dispose();
    g_guilds.dispose();
    g_seen.dispose();
    g_friends.dispose();
    g_user_at.dispose();
    g_seen_keys.dispose();

    DeleteCriticalSection(&g_lock);
    g_ready = false;
}

void people::note_user(const duser* u)
{
    if (!g_ready || !u || !u->id) return;

    EnterCriticalSection(&g_lock);

    known_user* k = find_user(u->id);
    if (!k)
    {
        if (g_users.count >= MAX_USERS)
        {
            say_full("людей", MAX_USERS);
            LeaveCriticalSection(&g_lock);
            return;
        }

        k = add_user(u->id);
        g_dirty = true;
    }

    const char* name = u->display_name();
    if (name && name[0] && ccscmp(k->name, name) != 0)
    {
        put_str(k->name, sizeof(k->name), name);
        g_dirty = true;
    }

    if (u->avatar && ccscmp(k->avatar, u->avatar) != 0)
    {
        put_str(k->avatar, sizeof(k->avatar), u->avatar);
        g_dirty = true;
    }

    LeaveCriticalSection(&g_lock);
}

void people::note_guild(const dguild* g)
{
    if (!g_ready || !g || !g->id) return;

    EnterCriticalSection(&g_lock);

    known_guild* k = find_guild(g->id);
    if (!k && g_guilds.count >= MAX_GUILDS)
    {
        say_full("серверов", MAX_GUILDS);
        LeaveCriticalSection(&g_lock);
        return;
    }

    if (!k)
    {
        known_guild fresh;
        ccfset(&fresh, 0, sizeof(fresh));
        fresh.id = g->id;
        g_guilds.push(fresh);
        k = &g_guilds[g_guilds.count - 1];
        g_dirty = true;
    }

    if (g->name && ccscmp(k->name, g->name) != 0)
    {
        put_str(k->name, sizeof(k->name), g->name);
        g_dirty = true;
    }

    if (g->icon && ccscmp(k->icon, g->icon) != 0)
    {
        put_str(k->icon, sizeof(k->icon), g->icon);
        g_dirty = true;
    }

    LeaveCriticalSection(&g_lock);
}

void people::note_member(snowflake guild_id, snowflake user_id)
{
    if (!g_ready || !guild_id || !user_id) return;

    snowflake account = store::self_id();
    if (!account) return;

    unsigned long long key = seen_key(user_id, guild_id, account);

    EnterCriticalSection(&g_lock);

    // One row per person per server per account of ours, and the date on it
    // is when it was first noticed rather than most recently. Seeing somebody
    // again says nothing new, and looking the row up to touch it is the one
    // thing here that would have to walk the whole file.
    if (!g_seen_keys.contains(key) && g_seen.count >= MAX_SEEN) say_full("встреч", MAX_SEEN);

    if (!g_seen_keys.contains(key) && g_seen.count < MAX_SEEN)
    {
        seen_row r;
        ccfset(&r, 0, sizeof(r));
        r.user_id = user_id;
        r.guild_id = guild_id;
        r.account_id = account;
        r.when_ms = unix_now_ms();

        g_seen.push(r);
        g_seen_keys.push(key);
        g_dirty = true;
    }

    LeaveCriticalSection(&g_lock);
}

void people::note_mutual_friends(snowflake about, const snowflake* ids, int count)
{
    if (!g_ready || !about) return;

    snowflake account = store::self_id();
    if (!account) return;

    EnterCriticalSection(&g_lock);

    // Everybody this account knew about before. Anyone in that list who is not
    // in the one that just arrived has stopped being a mutual friend, and that
    // is worth keeping - it is the one thing a fresh answer never contains.
    for (unsigned int i = 0; i < g_friends.count; i++)
    {
        friend_row* r = &g_friends[i];
        if (r->about != about || r->account_id != account) continue;

        bool still = false;
        for (int k = 0; k < count; k++)
            if (ids[k] == r->friend_id) { still = true; break; }

        if (r->gone != !still)
        {
            r->gone = !still;
            g_dirty = true;
        }
        if (still) r->when_ms = unix_now_ms();
    }

    for (int k = 0; k < count; k++)
    {
        bool had = false;
        for (unsigned int i = 0; i < g_friends.count; i++)
        {
            friend_row* r = &g_friends[i];
            if (r->about == about && r->account_id == account && r->friend_id == ids[k])
            {
                had = true;
                break;
            }
        }
        if (had) continue;

        if (g_friends.count >= MAX_FRIENDS) break;

        friend_row r;
        ccfset(&r, 0, sizeof(r));
        r.about = about;
        r.friend_id = ids[k];
        r.account_id = account;
        r.when_ms = unix_now_ms();
        g_friends.push(r);
        g_dirty = true;
    }

    LeaveCriticalSection(&g_lock);
}

void people::sweep_store()
{
    if (!g_ready) return;

    unsigned long long now = GetTickCount64();
    if (now - g_swept_ms < SWEEP_EVERY_MS) return;

    sweep_now();
}

void people::sweep_now()
{
    if (!g_ready) return;
    g_swept_ms = GetTickCount64();

    // A sweep rather than a hook on every path a member can arrive by: they
    // come from READY, from GUILD_CREATE, from a chunk asked for by a search,
    // from a message, from a voice state. Catching all of those is five places
    // that have to stay caught; walking what ended up in the store is one.
    store::guard guard;

    if (!store::self_id()) return;

    const ulist<snowflake>& guilds = store::guild_order();
    for (unsigned int i = 0; i < guilds.count; i++)
    {
        dguild* g = store::find_guild(guilds[i]);
        if (!g) continue;

        note_guild(g);

        for (unsigned int k = 0; k < g->members.count; k++)
        {
            snowflake uid = g->members[k].user_id;
            if (!uid) continue;

            note_member(g->id, uid);

            duser* u = store::find_user(uid);
            if (u) note_user(u);
        }
    }
}

int people::guilds_of(snowflake user_id, sighting* out, int cap)
{
    if (!g_ready || !out || cap <= 0) return 0;

    EnterCriticalSection(&g_lock);

    int n = 0;
    for (unsigned int i = 0; i < g_seen.count && n < cap; i++)
    {
        if (g_seen[i].user_id != user_id) continue;

        out[n].guild_id = g_seen[i].guild_id;
        out[n].account_id = g_seen[i].account_id;
        out[n].when_ms = g_seen[i].when_ms;
        n++;
    }

    LeaveCriticalSection(&g_lock);
    return n;
}

int people::friends_of(snowflake user_id, mutual* out, int cap)
{
    if (!g_ready || !out || cap <= 0) return 0;

    EnterCriticalSection(&g_lock);

    int n = 0;
    for (unsigned int i = 0; i < g_friends.count && n < cap; i++)
    {
        if (g_friends[i].about != user_id) continue;

        out[n].user_id = g_friends[i].friend_id;
        out[n].account_id = g_friends[i].account_id;
        out[n].when_ms = g_friends[i].when_ms;
        out[n].gone = g_friends[i].gone;
        n++;
    }

    LeaveCriticalSection(&g_lock);
    return n;
}

const char* people::user_name(snowflake id)
{
    if (!g_ready) return 0;

    EnterCriticalSection(&g_lock);
    known_user* k = find_user(id);
    const char* name = (k && k->name[0]) ? k->name : 0;
    LeaveCriticalSection(&g_lock);
    return name;
}

const char* people::user_avatar(snowflake id)
{
    if (!g_ready) return 0;

    EnterCriticalSection(&g_lock);
    known_user* k = find_user(id);
    const char* hash = (k && k->avatar[0]) ? k->avatar : 0;
    LeaveCriticalSection(&g_lock);
    return hash;
}

const char* people::guild_name(snowflake id)
{
    if (!g_ready) return 0;

    EnterCriticalSection(&g_lock);
    known_guild* k = find_guild(id);
    const char* name = (k && k->name[0]) ? k->name : 0;
    LeaveCriticalSection(&g_lock);
    return name;
}

const char* people::account_name(snowflake account_id)
{
    for (int i = 0; i < storage::accounts_count(); i++)
    {
        const saved_account* a = storage::account_at(i);
        if (a && a->id == account_id) return a->name;
    }

    // An account that has since been removed from the list still has its name
    // in here, because it is a person like any other.
    const char* name = user_name(account_id);
    return name ? name : "?";
}

void people::save_if_due()
{
    if (!g_ready || !g_dirty) return;

    unsigned long long now = GetTickCount64();
    if (now - g_saved_ms < SAVE_EVERY_MS) return;

    EnterCriticalSection(&g_lock);
    write_file();
    g_dirty = false;
    LeaveCriticalSection(&g_lock);

    g_saved_ms = now;
}

int people::known_people() { return (int)g_users.count; }
int people::known_sightings() { return (int)g_seen.count; }
