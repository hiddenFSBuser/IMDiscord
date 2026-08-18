#pragma once
#include "discord/types.h"

// Discord's privacy settings.
//
// These do not live in the ordinary settings json. They are one field of a
// protobuf carried base64 in `PATCH /users/@me/settings-proto/1`, and the
// client sends the whole privacy subtree every time rather than the part it
// changed.
//
// That matters more than it looks. The subtree holds fields this client has no
// name for - measured in a capture, present in every payload, meaning unknown -
// and rebuilding it from what is understood would silently drop them. So the
// blob is fetched, walked, and re-emitted with every byte preserved except the
// one field being changed.
//
// Only what a capture actually showed is offered here. Guessing at the rest of
// the field numbers would produce switches that either do nothing or turn off
// something else.

namespace privacy
{
    // Fetches the current blob so the switches below have something to show and
    // something to patch. Answers arrive asynchronously; ready() turns true.
    void fetch();
    bool ready();
    bool busy();

    // Direct messages from people who share a server with us. Discord stores
    // the negative - a server on the restricted list is one whose members may
    // not write - so this reads the way the switch is labelled, not the way it
    // is stored.
    bool dms_allowed_from(snowflake guild_id);
    void set_dms_allowed_from(snowflake guild_id, bool allowed);

    // The same thing for servers joined later.
    bool dms_allowed_by_default();
    void set_dms_allowed_by_default(bool allowed);
}
