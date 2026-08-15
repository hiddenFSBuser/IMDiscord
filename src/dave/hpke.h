#pragma once

// HPKE (RFC 9180) base mode, restricted to the one ciphersuite DAVE v1 uses:
//
//   KEM  0x0010  DHKEM(P-256, HKDF-SHA256)
//   KDF  0x0001  HKDF-SHA256
//   AEAD 0x0001  AES-128-GCM
//
// Only single-shot Seal/Open are needed by MLS, so the exported context keeps
// just the key, base nonce and sequence number.

namespace hpke
{
    const unsigned int KEM_ID = 0x0010;
    const unsigned int KDF_ID = 0x0001;
    const unsigned int AEAD_ID = 0x0001;

    const unsigned int NSECRET = 32;   // KEM shared secret
    const unsigned int NENC = 65;      // encapsulated key (uncompressed point)
    const unsigned int NPK = 65;
    const unsigned int NSK = 96;       // CNG private blob: X || Y || d
    const unsigned int NK = 16;        // AES-128 key
    const unsigned int NN = 12;        // AES-GCM nonce
    const unsigned int NT = 16;        // AES-GCM tag

    struct context
    {
        unsigned char key[NK];
        unsigned char base_nonce[NN];
        unsigned char exporter_secret[32];
        unsigned long long seq;
    };

    // enc receives the encapsulated public key (NENC bytes).
    bool setup_base_sender(const unsigned char recipient_public[NPK],
                           const void* info, unsigned int info_len,
                           unsigned char enc[NENC], context* out);

    bool setup_base_receiver(const unsigned char enc[NENC],
                             const unsigned char recipient_private[NSK],
                             const void* info, unsigned int info_len,
                             context* out);

    // cipher needs plain_len bytes, tag needs NT.
    bool seal(context* ctx,
              const void* aad, unsigned int aad_len,
              const void* plain, unsigned int plain_len,
              unsigned char* cipher, unsigned char tag[NT]);

    bool open(context* ctx,
              const void* aad, unsigned int aad_len,
              const void* cipher, unsigned int cipher_len,
              const unsigned char tag[NT],
              unsigned char* plain);

    // Convenience wrappers matching MLS's single-shot usage. The output layout
    // is enc || ciphertext || tag.
    bool seal_single(const unsigned char recipient_public[NPK],
                     const void* info, unsigned int info_len,
                     const void* aad, unsigned int aad_len,
                     const void* plain, unsigned int plain_len,
                     unsigned char enc[NENC],
                     unsigned char* cipher, unsigned char tag[NT]);

    // Turns key material into the key pair it names, RFC 9180 section 7.1.3.
    // Deterministic: every member of an MLS group derives the same node secret
    // and has to end up with the same pair.
    bool derive_key_pair(const void* ikm, unsigned int ikm_len,
                         unsigned char public_key[NPK],
                         unsigned char private_key[NSK]);

    bool open_single(const unsigned char enc[NENC],
                     const unsigned char recipient_private[NSK],
                     const void* info, unsigned int info_len,
                     const void* aad, unsigned int aad_len,
                     const void* cipher, unsigned int cipher_len,
                     const unsigned char tag[NT],
                     unsigned char* plain);
}
