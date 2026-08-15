#pragma once

// Why the client is not talking to discord.
//
// Two causes, one behaviour. Either the network is gone, or the token stopped
// being accepted. In both cases what is on disk is all there is, and the client
// should still be worth opening: saved servers, saved friends, saved
// conversations. The only thing the user needs told is which of the two it is,
// and that is said in a bar across the top that cannot be dismissed - a client
// quietly showing month old data is worse than one that will not open.
//
// While either holds, the picture cache stops expiring. Its whole purpose in
// this state is to be the last copy of an avatar nobody can fetch again.

enum offline_reason
{
    OFFLINE_NONE = 0,
    OFFLINE_NO_NETWORK,     // the connection is down
    OFFLINE_TOKEN_REVOKED,  // discord refused the token
};

namespace offline
{
    void init();

    // Called by the gateway and the REST layer as they learn things.
    void note_network_failure();
    void note_network_success();
    void note_token_rejected();

    // Entered on purpose, by somebody who is tired of waiting for a server
    // that is not answering. The whole point of the saved side of the client
    // is to be reachable when discord is not, and it was not reachable at all
    // while the sign-in screen insisted on a reply first.
    void enter(offline_reason why);
    void leave();

    offline_reason reason();
    bool active();

    // What to put in the bar, already worded for a person rather than a log.
    const char* headline();
    const char* detail();

    // How long the client has been in this state, in seconds.
    unsigned int seconds();
}
