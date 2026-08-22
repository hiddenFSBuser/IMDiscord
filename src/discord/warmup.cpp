#include "pch.h"
#include "warmup.h"
#include "store.h"
#include "gateway.h"
#include "rest.h"
#include "people.h"
#include "core/log.h"

namespace
{
    volatile long g_running = 0;
    HANDLE g_thread = 0;
    HANDLE g_stop = 0;

    volatile long g_done = 0;
    volatile long g_total = 0;

    CRITICAL_SECTION g_name_lock;
    bool g_name_ready = false;
    char g_name[96];

    // Slow on purpose. Each server here is one channel listing over http and one
    // subscription over the gateway, and doing thirty of them as fast as the
    // socket allows is how a client earns a rate limit that then applies to
    // everything else it was in the middle of.
    const unsigned int BETWEEN_MS = 1500;

    // Between one window of the member list and the next. Shorter than the
    // gap between servers because it is one small frame on a socket that is
    // already open, not a request over http.
    const unsigned int WINDOW_MS = 700;

    // Past this a server is not worth walking to the end of.
    const int MAX_MEMBERS = 3000;

    bool waited(unsigned int ms)
    {
        return WaitForSingleObject(g_stop, ms) == WAIT_OBJECT_0;
    }

    void set_name(const char* text)
    {
        if (!g_name_ready) return;

        EnterCriticalSection(&g_name_lock);
        ccfset(g_name, 0, sizeof(g_name));
        if (text) ccstrncpy(g_name, text, sizeof(g_name) - 1);
        LeaveCriticalSection(&g_name_lock);
    }

    // A channel of this server the account is allowed to read. The member
    // list is asked for against a channel, because that is how discord thinks
    // about it: the list shown beside a channel is the list of people who can
    // see that channel.
    snowflake readable_channel(snowflake guild_id)
    {
        store::guard guard;

        dguild* g = store::find_guild(guild_id);
        if (!g) return 0;

        for (unsigned int i = 0; i < g->channels.count; i++)
        {
            dchannel* c = store::find_channel(g->channels[i]);
            if (!c || !c->is_textual()) continue;
            if (!store::can_view_channel(g, store::self_id(), c)) continue;
            return c->id;
        }
        return 0;
    }

    // How many people the server says it has, and how many have arrived.
    void member_counts(snowflake guild_id, int* total, int* have)
    {
        store::guard guard;

        dguild* g = store::find_guild(guild_id);
        *total = g ? g->member_count : 0;
        *have = g ? (int)g->members.count : 0;
    }

    void warm_one(snowflake id)
    {
        {
            store::guard guard;
            dguild* g = store::find_guild(id);

            // Gone since the list was taken - left, or the account changed
            // under us. Either way there is nothing here to warm.
            if (!g) return;

            set_name(g->name ? g->name : "...");
        }

        // The channels first, and waited for: the member list is asked for
        // against a channel, so without them there is nothing to ask with.
        api::fetch_guild_channels(id);

        snowflake channel = 0;
        for (int i = 0; i < 20 && g_running; i++)
        {
            channel = readable_channel(id);
            if (channel) break;
            if (waited(300)) return;
        }

        // Nothing readable in it. A server can be like that, and asking for
        // its member list would be answered with silence.
        if (!channel) return;

        // Now the list itself, a window at a time. Discord streams the slice
        // being looked at rather than the whole thing, so this walks the
        // positions the way somebody scrolling would - which is the only way
        // a person's account can see past the first hundred.
        gateway::request_member_window(id, channel, 0);
        if (waited(WINDOW_MS)) return;

        int total = 0, have = 0;
        member_counts(id, &total, &have);

        // Bounded. A server with fifty thousand people in it would otherwise
        // hold the walk for an hour on its own, and the point of this is the
        // servers a person is actually in with somebody.
        int reach = total < MAX_MEMBERS ? total : MAX_MEMBERS;

        for (int at = 300; at < reach && g_running; at += 300)
        {
            gateway::request_member_window(id, channel, at);
            if (waited(WINDOW_MS)) return;

            // Stopped arriving before the count said it would: the account
            // cannot see the rest, and asking again would only be silence.
            int now = 0;
            member_counts(id, &total, &now);
            if (now <= have) break;
            have = now;
        }

        member_counts(id, &total, &have);
        log_line("warmup: %llu - участников %d из %d", id, have, total);

        // Filed as we go rather than at the end, so stopping halfway keeps
        // what the first half found.
        people::sweep_now();
    }

    DWORD WINAPI warm_thread(LPVOID)
    {
        // A copy, because the walk takes minutes and the store can be emptied
        // under it by an account switch. Ids are safe to hold; the objects they
        // point at are not.
        ulist<snowflake> guilds;
        {
            store::guard guard;
            const ulist<snowflake>& order = store::guild_order();
            for (unsigned int i = 0; i < order.count; i++) guilds.push(order[i]);
        }

        InterlockedExchange(&g_total, (long)guilds.count);
        log_line("warmup: прогреваю %u серверов", guilds.count);

        for (unsigned int i = 0; i < guilds.count && g_running; i++)
        {
            warm_one(guilds[i]);
            InterlockedIncrement(&g_done);

            if (waited(BETWEEN_MS)) break;
        }

        // Straight into the file rather than waiting for the sweep on its own
        // timer, so closing the client right after this does not throw away
        // what it just spent minutes collecting.
        people::sweep_now();

        set_name("");
        log_line("warmup: закончено, %ld из %ld", g_done, g_total);

        guilds.dispose();
        InterlockedExchange(&g_running, 0);
        return 0;
    }
}

void warmup::start()
{
    if (InterlockedCompareExchange(&g_running, 1, 0) != 0) return;

    if (!g_name_ready)
    {
        InitializeCriticalSection(&g_name_lock);
        g_name_ready = true;
    }
    set_name("");

    InterlockedExchange(&g_done, 0);
    InterlockedExchange(&g_total, 0);

    if (!g_stop) g_stop = CreateEventW(0, TRUE, FALSE, 0);
    ResetEvent(g_stop);

    g_thread = CreateThread(0, 0, warm_thread, 0, 0, 0);
    if (!g_thread) InterlockedExchange(&g_running, 0);
}

void warmup::stop()
{
    if (g_stop) SetEvent(g_stop);
    InterlockedExchange(&g_running, 0);

    if (g_thread)
    {
        WaitForSingleObject(g_thread, 5000);
        CloseHandle(g_thread);
        g_thread = 0;
    }

    if (g_stop) { CloseHandle(g_stop); g_stop = 0; }
}

bool warmup::running() { return g_running != 0; }
int warmup::done() { return (int)g_done; }
int warmup::total() { return (int)g_total; }

const char* warmup::current()
{
    return g_name_ready ? g_name : "";
}
