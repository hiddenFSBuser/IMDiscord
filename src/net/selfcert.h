#pragma once

// A certificate this client signs for itself, for one purpose: the dtls
// handshake underneath a webrtc voice connection.
//
// Nobody verifies it as a certificate. Discord is told the digest of it in the
// sdp offer - "a=fingerprint:sha-256 ..." - and the only thing that matters is
// that the certificate presented during the handshake hashes to what the offer
// promised. That is the whole of webrtc's identity model: the signalling
// channel vouches for the key, and the certificate is a container for it.
//
// Which is why it is generated fresh rather than shipped. A certificate baked
// into the binary would give every copy of this client the same fingerprint -
// a single value that identifies the software to the server across every
// account and every session, which is exactly the kind of mark this client has
// no reason to wear. Browsers generate one per session for the same reason.

namespace selfcert
{
    struct material
    {
        // Both in pem, because that is what the dtls implementation loads.
        char cert_pem[2048];
        char key_pem[1024];

        int cert_pem_len;
        int key_pem_len;

        // Ready for the sdp line, algorithm name and all:
        // "sha-256 AA:BB:CC:...".
        char fingerprint[128];

        // The private key in the layout the rest of this client's p256 code
        // uses, kept so signing can happen without parsing the pem back.
        unsigned char private_key[96];
        unsigned char public_key[65];
    };

    bool generate(material* out);

    bool self_test();
}
