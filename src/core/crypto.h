#pragma once
#include "ubuffer.h"

// Everything the client needs cryptographically: a hash for key derivation,
// base64 for headers and the token store, and two AEADs because discord voice
// negotiates one of them per session.

namespace crypto
{
    void random_bytes(void* out, unsigned int len);

    // ---- sha256 ----
    struct sha256_ctx
    {
        unsigned int state[8];
        unsigned long long length;
        unsigned char block[64];
        unsigned int block_len;
    };

    void sha256_init(sha256_ctx* ctx);
    void sha256_update(sha256_ctx* ctx, const void* data, unsigned int len);
    void sha256_final(sha256_ctx* ctx, unsigned char out[32]);
    void sha256(const void* data, unsigned int len, unsigned char out[32]);

    // ---- base64 ----
    void base64_encode(const void* data, unsigned int len, ubuffer* out);
    bool base64_decode(const char* text, int len, ubuffer* out);

    // ---- xchacha20-poly1305 (aead_xchacha20_poly1305_rtpsize) ----
    // cipher must have room for plain_len bytes, tag is written separately.
    void xchacha20poly1305_encrypt(const unsigned char key[32],
                                   const unsigned char nonce[24],
                                   const void* aad, unsigned int aad_len,
                                   const void* plain, unsigned int plain_len,
                                   unsigned char* cipher, unsigned char tag[16]);

    bool xchacha20poly1305_decrypt(const unsigned char key[32],
                                   const unsigned char nonce[24],
                                   const void* aad, unsigned int aad_len,
                                   const void* cipher, unsigned int cipher_len,
                                   const unsigned char tag[16],
                                   unsigned char* plain);

    // ---- hmac / hkdf (sha-256) ----
    void hmac_sha256(const void* key, unsigned int key_len,
                     const void* data, unsigned int data_len,
                     unsigned char out[32]);

    void hkdf_extract(const void* salt, unsigned int salt_len,
                      const void* ikm, unsigned int ikm_len,
                      unsigned char prk[32]);

    bool hkdf_expand(const unsigned char prk[32],
                     const void* info, unsigned int info_len,
                     unsigned char* out, unsigned int out_len);

    // The hash ratchet starts from a 16 byte secret, so the pseudorandom key
    // is not always digest sized.
    bool hkdf_expand_n(const unsigned char* prk, unsigned int prk_len,
                       const void* info, unsigned int info_len,
                       unsigned char* out, unsigned int out_len);

    bool mls_expand_with_label_n(const unsigned char* secret, unsigned int secret_len,
                                 const char* label,
                                 const void* context, unsigned int context_len,
                                 unsigned char* out, unsigned int out_len);

    // Writes the MLS variable-length integer of RFC 9420 2.1.2. Returns the
    // number of bytes written (1, 2 or 4).
    unsigned int varint_write(unsigned char* out, unsigned int value);
    unsigned int varint_size(unsigned int value);

    // MLS KDF.ExpandWithLabel: the info is a serialized KDFLabel structure
    // (length, "MLS 1.0 " || label, context).
    bool mls_expand_with_label(const unsigned char secret[32],
                               const char* label,
                               const void* context, unsigned int context_len,
                               unsigned char* out, unsigned int out_len);

    // MLS KDF.DeriveSecret: ExpandWithLabel with an empty context, 32 bytes.
    bool mls_derive_secret(const unsigned char secret[32], const char* label,
                           unsigned char out[32]);

    // ---- aes-gcm with a caller-chosen key and tag size ----
    // DAVE uses AES-128-GCM with the tag truncated to 8 bytes.
    bool aesgcm_encrypt(const unsigned char* key, unsigned int key_len,
                        const unsigned char* nonce, unsigned int nonce_len,
                        const void* aad, unsigned int aad_len,
                        const void* plain, unsigned int plain_len,
                        unsigned char* cipher,
                        unsigned char* tag, unsigned int tag_len);

    bool aesgcm_decrypt(const unsigned char* key, unsigned int key_len,
                        const unsigned char* nonce, unsigned int nonce_len,
                        const void* aad, unsigned int aad_len,
                        const void* cipher, unsigned int cipher_len,
                        const unsigned char* tag, unsigned int tag_len,
                        unsigned char* plain);

    // ---- NIST P-256 (the only ciphersuite DAVE v1 uses) ----
    const unsigned int P256_PUBLIC_BYTES = 65;   // uncompressed 0x04 || X || Y
    // CNG cannot rebuild the public point from the scalar alone, so a private
    // key is kept as the full X || Y || d triple that BCryptExportKey hands out.
    const unsigned int P256_PRIVATE_BYTES = 96;
    // MLS carries ECDSA signatures DER encoded as SEQUENCE{INTEGER r, INTEGER s},
    // which is 70 to 72 bytes, not the raw 64 byte R||S that CNG produces.
    const unsigned int P256_SIGNATURE_MAX_BYTES = 72;

    unsigned int der_encode_signature(const unsigned char raw[64], unsigned char* out);
    bool der_decode_signature(const unsigned char* der, unsigned int der_len,
                              unsigned char raw[64]);

    // Scalar times the base point, which CNG has no call for. MLS needs it:
    // TreeKEM turns a secret derived up the tree into a key pair, and every
    // member has to arrive at the same one. Implemented in p256.cpp.
    bool p256_scalar_base_mult(const unsigned char scalar[32],
                               unsigned char out_x[32], unsigned char out_y[32]);

    // Builds the CNG shaped private blob and the matching public point from a
    // scalar. False when the scalar is zero or not below the group order.
    bool p256_keypair_from_scalar(const unsigned char scalar[32],
                                  unsigned char public_key[65],
                                  unsigned char private_key[96]);
    bool p256_scalar_in_range(const unsigned char scalar[32]);

    // scalar times an arbitrary point, which is the whole of ECDH. Written
    // here rather than asked of the system because the system's answer costs
    // more than it is worth: BCryptDeriveKey with a raw secret only exists
    // from Windows 8.1, and that one call was the entire reason end to end
    // encrypted calls had a floor two versions above the rest of the client.
    //
    // Returns false for a point that is not on the curve, which is a check
    // that has to happen somewhere and belongs next to the arithmetic.
    bool p256_scalar_point_mult(const unsigned char scalar[32],
                                const unsigned char point_x[32],
                                const unsigned char point_y[32],
                                unsigned char out_x[32], unsigned char out_y[32]);

    bool p256_generate(unsigned char public_key[65], unsigned char private_key[96]);
    // Raw ECDH: returns the X coordinate of the shared point, per RFC 9180.
    bool p256_ecdh(const unsigned char private_key[96],
                   const unsigned char peer_public[65],
                   unsigned char shared_x[32]);
    void p256_public_from_private(const unsigned char private_key[96],
                                  unsigned char public_key[65]);

    // signature receives DER bytes; signature_len reports how many were written.
    bool p256_sign(const unsigned char private_key[96],
                   const void* data, unsigned int data_len,
                   unsigned char* signature, unsigned int* signature_len);
    bool p256_verify(const unsigned char public_key[65],
                     const void* data, unsigned int data_len,
                     const unsigned char* signature, unsigned int signature_len);

    // Runs the built-in known-answer tests; results go to the log.
    bool self_test();

    // ---- aes-256-gcm (aead_aes256_gcm_rtpsize), via bcrypt ----
    bool aes256gcm_available();
    bool aes256gcm_encrypt(const unsigned char key[32],
                           const unsigned char nonce[12],
                           const void* aad, unsigned int aad_len,
                           const void* plain, unsigned int plain_len,
                           unsigned char* cipher, unsigned char tag[16]);

    bool aes256gcm_decrypt(const unsigned char key[32],
                           const unsigned char nonce[12],
                           const void* aad, unsigned int aad_len,
                           const void* cipher, unsigned int cipher_len,
                           const unsigned char tag[16],
                           unsigned char* plain);
}
