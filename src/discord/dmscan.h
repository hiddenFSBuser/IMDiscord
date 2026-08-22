#pragma once
#include "types.h"

// Finding a bot's direct conversations.
//
// Discord tells a bot nothing about them: READY carries no private channels,
// and the endpoint that used to list them answers a bot with an empty array.
// The only way a conversation turns up on its own is somebody writing in it
// while the client happens to be running - so anything said while it was not is
// invisible, and stays invisible.
//
// What can be asked is whether a conversation with one particular person
// exists. So this walks the people the bot can see - everybody in the servers
// it is in - and asks about each of them in turn, slowly. The ones that turn
// out to have something in them are added; the ones that do not are left alone,
// or the list would fill with a blank entry per member of every server.
//
// For bots only. A person's account is handed its whole list in READY and needs
// none of this.
namespace dmscan
{
    void start();

    // Stops within a minute rather than at once. Asked for on an account
    // switch: a request already on the wire is allowed to come back, and the
    // walk gives up at the deadline.
    void wind_down();

    // Stops now and waits for the thread, for shutdown.
    void stop();

    // How far it has got, for the settings screen to show.
    bool running();
    int asked();
    int found();
    int total();
}
