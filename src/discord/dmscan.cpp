#include "pch.h"
#include "dmscan.h"
#include "store.h"
#include "rest.h"
#include "core/log.h"
#include "net/http.h"
#include "net/json.h"

namespace
{
    volatile long g_running = 0;
    HANDLE g_thread = 0;
    HANDLE g_stop = 0;

    // The token the walk belongs to. Held separately because an account switch
    // changes the one everything else uses, and every request from here has to
    // keep speaking as the bot it was started for - including the ones that go
    // out during the minute after the switch.
    char g_token[512];

    // Zero while the account is still the one signed in. Set on a switch, and
    // the thread gives up once it passes.
    volatile unsigned long long g_deadline_ms = 0;

    // Whether results may still be written down. An account switch throws the
    // store away and opens somebody else's, so anything learned after that
    // belongs nowhere and must not be filed.
    volatile long g_may_store = 0;

    volatile long g_asked = 0;
    volatile long g_found = 0;
    volatile long g_total = 0;

    // Gentle on purpose. This is a walk over every person on every server, and
    // there is nothing time-critical about it: a conversation that turns up two
    // minutes from now is as useful as one that turns up immediately, and a
    // client that spends a burst of requests on this is a client that gets
    // nothing else done meanwhile.
    const unsigned int BETWEEN_MS = 1200;

    bool waited(unsigned int ms)
    {
        return WaitForSingleObject(g_stop, ms) == WAIT_OBJECT_0;
    }

    bool expired()
    {
        unsigned long long at = g_deadline_ms;
        return at && GetTickCount64() >= at;
    }

    // Everybody worth asking about: the people in the servers the bot is in,
    // minus itself, minus other bots, minus anybody there is already a
    // conversation with.
    void collect(ulist<snowflake>* out)
    {
        store::guard guard;

        snowflake self = store::self_id();

        // The conversations already known, so the walk does not ask about them.
        uset<snowflake> known;

        const ulist<snowflake>& dms = store::dm_order();
        for (unsigned int i = 0; i < dms.count; i++)
        {
            dchannel* c = store::find_channel(dms[i]);
            if (!c) continue;
            for (unsigned int k = 0; k < c->recipients.count; k++) known.push(c->recipients[k]);
        }

        uset<snowflake> seen;

        const ulist<snowflake>& guilds = store::guild_order();
        for (unsigned int i = 0; i < guilds.count; i++)
        {
            dguild* g = store::find_guild(guilds[i]);
            if (!g) continue;

            for (unsigned int k = 0; k < g->members.count; k++)
            {
                snowflake uid = g->members[k].user_id;
                if (!uid || uid == self) continue;
                if (known.contains(uid) || seen.contains(uid)) continue;

                duser* u = store::find_user(uid);
                if (u && u->bot) continue;      // bots cannot write to each other

                seen.push(uid);
                out->push(uid);
            }
        }

        known.dispose();
        seen.dispose();
    }

    // Asks discord for the conversation with one person. It is created if there
    // was none, which costs nothing and tells nobody - no message is sent and
    // the other side sees no change. What comes back says whether anything was
    // ever said in it.
    void ask_about(snowflake user_id)
    {
        jwriter w;
        w.init();
        w.begin_obj();

        // A bot names one person; the field a person's client uses is a list,
        // and discord refuses each in the other's place.
        char id[24];
        cnprint(id, sizeof(id), "%llu", user_id);
        w.kv_str("recipient_id", id);
        w.end_obj();

        http_response res;
        res.init();

        bool ok = api::call_as("POST", "/users/@me/channels", w.c_str(), &res, g_token);

        if (ok && res.status == 429 && res.retry_after_ms > 0)
        {
            // Told to slow down, so slow down rather than pressing on and being
            // told again.
            waited((unsigned int)res.retry_after_ms + 200);
            res.free_response();
            w.free_writer();
            return;
        }

        if (ok && res.ok())
        {
            jdoc doc;
            doc.init();

            if (doc.parse(res.text(), (int)res.body.size))
            {
                // An empty conversation is one nobody has used. Adding it would
                // put a blank row in the list for every member of every server,
                // which is noise standing exactly where the real ones belong.
                snowflake last = doc.root->sf("last_message_id");

                if (last && g_may_store)
                {
                    store::guard guard;
                    store::upsert_channel(doc.root, 0);
                    store::touch_dm_order();
                    store::bump_revision();

                    InterlockedIncrement(&g_found);
                    log_line("dmscan: нашлась переписка с %llu", user_id);
                }
            }

            doc.free_doc();
        }

        res.free_response();
        w.free_writer();
    }

    bool store_has_people()
    {
        store::guard guard;
        return store::guild_order().count > 0;
    }

    DWORD WINAPI scan_thread(LPVOID)
    {
        // Started at sign-in, which is before READY has said a word - so at
        // this moment there are no servers and nobody in them. Waiting is the
        // whole of the fix; without it the walk looked at an empty store, found
        // nothing to do and finished in a millisecond.
        while (g_running && !expired())
        {
            if (store_has_people()) break;
            if (waited(500)) return 0;
        }

        // And a moment more, because the members of a server arrive after the
        // server itself does.
        if (waited(3000)) return 0;

        // A pass, then another later. People join servers and conversations
        // start, and one walk at sign-in would see neither.
        for (;;)
        {
            ulist<snowflake> people;
            collect(&people);

            InterlockedExchange(&g_total, (long)people.count);
            log_line("dmscan: обхожу %u человек", people.count);

            for (unsigned int i = 0; i < people.count; i++)
            {
                if (!g_running || expired()) break;

                ask_about(people[i]);
                InterlockedIncrement(&g_asked);

                if (waited(BETWEEN_MS)) break;
            }

            people.dispose();
            log_line("dmscan: проход закончен, спрошено %ld, найдено %ld", g_asked, g_found);

            if (!g_running || expired()) break;
            if (waited(300000)) break;      // five minutes, then look again
        }

        InterlockedExchange(&g_running, 0);
        return 0;
    }
}

void dmscan::start()
{
    // A person's client is handed the whole list in READY, so there is nothing
    // here for it to do.
    if (!api::is_bot()) return;
    if (InterlockedCompareExchange(&g_running, 1, 0) != 0) return;

    ccfset(g_token, 0, sizeof(g_token));
    ccstrncpy(g_token, api::token(), sizeof(g_token) - 1);

    g_deadline_ms = 0;
    InterlockedExchange(&g_may_store, 1);
    InterlockedExchange(&g_asked, 0);
    InterlockedExchange(&g_found, 0);
    InterlockedExchange(&g_total, 0);

    if (!g_stop) g_stop = CreateEventW(0, TRUE, FALSE, 0);
    ResetEvent(g_stop);

    g_thread = CreateThread(0, 0, scan_thread, 0, 0, 0);
    if (!g_thread) InterlockedExchange(&g_running, 0);
}

void dmscan::wind_down()
{
    if (!g_running) return;

    // Nothing may be written down from here on: the store belongs to whoever is
    // signed in now, and a conversation of the previous account's would appear
    // in their list.
    InterlockedExchange(&g_may_store, 0);
    g_deadline_ms = GetTickCount64() + 60000ULL;

    log_line("dmscan: аккаунт сменился - доработаю минуту и остановлюсь");
}

void dmscan::stop()
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

bool dmscan::running() { return g_running != 0; }
int dmscan::asked() { return (int)g_asked; }
int dmscan::found() { return (int)g_found; }
int dmscan::total() { return (int)g_total; }
