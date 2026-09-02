#pragma once
#include "discord/sdp.h"
#include "net/proxy.h"

// The half of a webrtc voice connection that happens before any media does.
//
// Three things in order, on one udp socket:
//
//   ice    a stun binding request to the single candidate discord publishes.
//          Discord is ice-lite and controlled, which means it never probes
//          this side - it only answers. So the request has to come from here,
//          and it carries "<their ufrag>:<our ufrag>" with an integrity
//          attribute keyed by their password.
//
//   dtls   an ordinary dtls 1.2 handshake, this side as the client, on the
//          same socket the stun went out on. The certificate that comes back
//          is self signed and worthless as a certificate; what makes it the
//          right one is that its sha-256 matches the fingerprint the sdp
//          answer carried.
//
//   srtp   keys exported from the finished handshake with the
//          EXTRACTOR-dtls_srtp label, which is what every rtp packet after
//          this point is encrypted with instead of discord's own scheme.
//
// All of it runs synchronously, the way the ip discovery on the other
// transport already does: it is a few round trips against one server, and a
// thread of its own would buy nothing but a lifetime to get wrong.

namespace webrtc
{
    // Makes the certificate and the dtls context, and reports the local ice
    // credentials that the offer has to advertise. They come from the dtls
    // implementation rather than from us: it checks incoming stun against its
    // own password, so an offer promising a different one would be answered
    // and then ignored.
    bool begin(const char** ufrag, const char** pwd, const char** fingerprint);

    // Runs ice and dtls to completion, or gives up after timeout_ms. `why`
    // receives a short reason on failure.
    bool handshake(proxy::udp_route* route, const sdp::answer* a,
                   int timeout_ms, const char** why);

    // Checks the stun message builder against the one in the vendored dtls
    // library: the same request, minus the two attributes added here, has to
    // come out byte for byte identical. Sha-1, the hmac construction, the
    // crc32 and every length field are all covered by that one comparison,
    // and none of them can be checked by looking at a failed handshake.
    bool self_test();

    // ---- media -----------------------------------------------------------
    //
    // Srtp keeps the rtp header in the clear - the far side needs the
    // sequence and the ssrc to make sense of anything - and encrypts only
    // what follows, then appends an authentication tag over both. So both
    // calls take the header and the payload apart rather than one buffer.

    // Header and payload in, one srtp packet out. Returns its length, or 0.
    int protect(const unsigned char* header, int header_len,
                const unsigned char* payload, int payload_len,
                unsigned char* out, int out_cap);

    // The reverse: `payload` is everything after the header, tag included.
    // Returns the length of the plain payload, or 0.
    int unprotect(const unsigned char* header, int header_len,
                  const unsigned char* payload, int payload_len,
                  unsigned char* out, int out_cap);

    bool ready();
    void reset();
}
