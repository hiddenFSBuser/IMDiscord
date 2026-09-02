#pragma once

// Where an application's packets actually go.
//
// Written for one question that cannot be settled by reasoning: an
// application is configured to use a proxy, its voice works, and the proxy
// is known not to carry UDP. Either the media is going through the proxy by
// some route that was not expected, or it is leaving the machine directly and
// the proxy is only carrying the signalling. Those two look identical from
// inside the application and completely different on the wire.
//
// So the wire is what this reads. A raw socket per interface with SIO_RCVALL
// sees every ipv4 and ipv6 packet the machine sends or receives; the local
// port on each one is matched against the tcp and udp tables, which carry the
// owning process id. The result is every flow with the name of the process
// that made it.
//
// Two things it deliberately does: it never looks at payloads, only at
// headers and sizes, and it names the process behind a flow rather than
// guessing from addresses. A verdict that says "legcord.exe sent 50 udp
// packets a second to 66.22.x.x" needs no interpretation, and one built out
// of address ranges would need a table of discord's addresses that goes stale.
//
// Needs administrator rights: SIO_RCVALL is not available otherwise. That is
// checked and reported rather than failing quietly.

namespace netdump
{
    // Runs for the given number of seconds, printing as it goes, then prints
    // the flow table and a verdict. app_name, when given, is matched against
    // process names so the verdict can say whether that particular
    // application's media went out on its own.
    //
    // Returns 0 when it ran, 1 when it could not start.
    int run(int seconds, const char* app_name);

    // Feeds hand built packets through the same parser the capture
    // feeds, because the capture itself needs administrator rights
    // and the parsing does not. What it covers is the half where a
    // mistake would be silent: header layout, telling an outgoing
    // packet from an incoming one, keying two directions onto one
    // flow, and the rule that decides what counts as media.
    bool self_test();
}
