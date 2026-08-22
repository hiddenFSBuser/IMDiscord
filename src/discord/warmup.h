#pragma once
#include "types.h"

// Loading every server without opening every server.
//
// Channels and members arrive when a server is looked at, and only then: that
// is how discord's own client works and this one follows it, because asking for
// all of it at sign-in would be a burst of requests for data nobody has asked
// to see. The cost is that anything built out of who is where - the servers two
// people have in common, most of all - only knows about the servers that have
// been clicked on.
//
// This does the clicking. One server at a time, slowly, in the background.
namespace warmup
{
    // Ignored while one is already running.
    void start();
    void stop();

    bool running();
    int done();
    int total();

    // What it is on now, for the line under the button.
    const char* current();
}
