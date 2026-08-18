#include "pch.h"
#include "offline.h"
#include "log.h"

namespace
{
    volatile long g_reason = OFFLINE_NONE;
    unsigned long long g_since = 0;

    // One failed request is a hiccup, not an outage. Only a run of them counts,
    // so a single dropped packet does not throw a banner across the client.
    volatile long g_failures = 0;
    const long FAILURES_BEFORE_OFFLINE = 3;

    void enter_reason(offline_reason why)
    {
        if (g_reason == (long)why) return;

        InterlockedExchange(&g_reason, (long)why);
        g_since = GetTickCount64();

        log_line("offline: режим %s",
                 why == OFFLINE_TOKEN_REVOKED ? "удалённого аккаунта" : "без сети");
    }
}

void offline::init()
{
    InterlockedExchange(&g_reason, OFFLINE_NONE);
    InterlockedExchange(&g_failures, 0);
    g_since = 0;
}

void offline::enter(offline_reason why)
{
    if (why == OFFLINE_NONE) { leave(); return; }
    enter_reason(why);
}

void offline::leave()
{
    InterlockedExchange(&g_reason, OFFLINE_NONE);
    InterlockedExchange(&g_failures, 0);
    g_since = 0;
}

void offline::note_network_failure()
{
    // A rejected token outranks a missing network: it is the more specific
    // explanation and it will not fix itself by waiting.
    if (g_reason == OFFLINE_TOKEN_REVOKED) return;

    if (InterlockedIncrement(&g_failures) >= FAILURES_BEFORE_OFFLINE)
        enter_reason(OFFLINE_NO_NETWORK);
}

void offline::note_network_success()
{
    InterlockedExchange(&g_failures, 0);

    // Something answered, so the network is back. A revoked token is not
    // cleared by this: the requests that succeed are the ones that need no
    // token, and the account is still gone.
    if (g_reason == OFFLINE_NO_NETWORK)
    {
        InterlockedExchange(&g_reason, OFFLINE_NONE);
        g_since = 0;
        log_line("offline: связь вернулась");
    }
}

void offline::note_token_rejected()
{
    enter_reason(OFFLINE_TOKEN_REVOKED);
}

offline_reason offline::reason() { return (offline_reason)g_reason; }
bool offline::active() { return g_reason != OFFLINE_NONE; }

const char* offline::headline()
{
    switch (g_reason)
    {
    case OFFLINE_NO_NETWORK:    return tr("Нет связи с discord — показано сохранённое");
    case OFFLINE_TOKEN_REVOKED: return tr("Токен больше не действует — показано сохранённое");
    default:                    return "";
    }
}

const char* offline::detail()
{
    switch (g_reason)
    {
    case OFFLINE_NO_NETWORK:
        return tr("Серверы, друзья и переписка читаются из архива. Кэш картинок не чистится.");
    case OFFLINE_TOKEN_REVOKED:
        return tr("Войди заново или переключи аккаунт. Всё сохранённое остаётся на месте.");
    default:
        return "";
    }
}

unsigned int offline::seconds()
{
    if (!g_since) return 0;
    return (unsigned int)((GetTickCount64() - g_since) / 1000);
}
