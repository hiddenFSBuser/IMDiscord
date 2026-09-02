#pragma once

// Attachment links that go stale.
//
// Discord signs the url of every attachment and puts the moment it stops
// working straight into the query: "?ex=<hex unix time>&is=...&hm=<signature>".
// After that moment the cdn answers 404, and it does so for a link that looks
// perfectly well formed.
//
// This client keeps messages on disk, so it hands those links back weeks after
// they were minted. The pictures in a channel opened today then load, because
// their messages were just fetched, and everything above them fails - which
// looks exactly like a broken image loader and is not one.
//
// The fix is the endpoint the official client uses: hand discord the dead
// links and get living ones back. It answers for a batch, so requests are
// gathered rather than sent one at a time.

namespace cdnfix
{
    void init();
    void reset();

    // The link to actually use for this attachment.
    //
    // Returns the original when it carries no expiry or has not reached it, a
    // refreshed one when that has arrived, and null while a refresh is still
    // in flight - which the caller should draw as "loading" rather than as a
    // failure, because that is what it is.
    const char* usable(const char* url);

    // Sends whatever has been gathered. Called from the frame loop; does
    // nothing until there is something to ask about.
    void tick();

    bool self_test();
}
