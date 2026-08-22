#pragma once
#include "types.h"

// What this client has learned about people, kept across accounts and across
// runs.
//
// Everything else here belongs to one account and is thrown away when it is
// switched: the store is emptied, the archive is closed, and what the previous
// account could see goes with them. That is right for messages and channels -
// they are that account's - but it throws away something that is not. Whether
// two people share a server, and who they have in common, is a fact about those
// people. One account saw it; it stays true when a different one is signed in,
// and it stays true when there is no connection at all.
//
// So this is one file for the whole client rather than one per account, and
// every entry says which account it was learned from. A profile then reads as
// "these servers, seen from this account of yours", which is more than any one
// account could say on its own.
namespace people
{
    struct sighting
    {
        snowflake guild_id;
        snowflake account_id;      // which of ours saw it
        unsigned long long when_ms;
    };

    struct mutual
    {
        snowflake user_id;
        snowflake account_id;
        unsigned long long when_ms;

        // Was a mutual friend the last time this was asked, and is not now.
        // Kept rather than deleted: that somebody stopped being a friend is
        // itself the interesting part, and it is the one thing a live fetch can
        // never tell you.
        bool gone;
    };

    void init();
    void shutdown();

    // Names and pictures, so a profile still reads when the store belongs to
    // another account or there is nothing to read it from.
    void note_user(const duser* u);
    void note_guild(const dguild* g);

    // One person seen in one server, by whichever account is signed in.
    void note_member(snowflake guild_id, snowflake user_id);

    // The whole mutual-friend list for one person as it stands now. Anybody in
    // the previous list who is not in this one is marked gone rather than
    // dropped.
    void note_mutual_friends(snowflake about, const snowflake* ids, int count);

    // Walks the store and files everything in it. Cheap enough to call on a
    // timer and idempotent, which is why it is a sweep rather than a hook on
    // every path a member can arrive by.
    void sweep_store();

    // The same walk with the timer ignored, for the moment a warm-up pass has
    // just brought a lot of people in and stopping the client would lose it.
    void sweep_now();

    int guilds_of(snowflake user_id, sighting* out, int cap);
    int friends_of(snowflake user_id, mutual* out, int cap);

    const char* user_name(snowflake id);
    const char* user_avatar(snowflake id);
    const char* guild_name(snowflake id);

    // Whose knowledge an entry is, by name, for the column on the right.
    const char* account_name(snowflake account_id);

    // Written on a timer and at shutdown; nothing else has to remember to.
    void save_if_due();

    int known_people();
    int known_sightings();
}
