#include "pch.h"
#include "hpke.h"
#include "core/crypto.h"

namespace
{
    const char HPKE_VERSION[] = "HPKE-v1";

    // suite_id for the KEM is "KEM" || I2OSP(kem_id, 2); for everything else it
    // is "HPKE" || I2OSP(kem_id,2) || I2OSP(kdf_id,2) || I2OSP(aead_id,2).
    unsigned int kem_suite_id(unsigned char* out)
    {
        out[0] = 'K'; out[1] = 'E'; out[2] = 'M';
        out[3] = (unsigned char)(hpke::KEM_ID >> 8);
        out[4] = (unsigned char)(hpke::KEM_ID & 0xFF);
        return 5;
    }

    unsigned int hpke_suite_id(unsigned char* out)
    {
        out[0] = 'H'; out[1] = 'P'; out[2] = 'K'; out[3] = 'E';
        out[4] = (unsigned char)(hpke::KEM_ID >> 8);
        out[5] = (unsigned char)(hpke::KEM_ID & 0xFF);
        out[6] = (unsigned char)(hpke::KDF_ID >> 8);
        out[7] = (unsigned char)(hpke::KDF_ID & 0xFF);
        out[8] = (unsigned char)(hpke::AEAD_ID >> 8);
        out[9] = (unsigned char)(hpke::AEAD_ID & 0xFF);
        return 10;
    }

    // labeled_ikm = "HPKE-v1" || suite_id || label || ikm
    void labeled_extract(const unsigned char* salt, unsigned int salt_len,
                         const unsigned char* suite_id, unsigned int suite_len,
                         const char* label,
                         const unsigned char* ikm, unsigned int ikm_len,
                         unsigned char prk[32])
    {
        unsigned int label_len = (unsigned int)ccslenf(label);
        unsigned int total = 7 + suite_len + label_len + ikm_len;

        unsigned char stack_buf[256];
        unsigned char* buf = stack_buf;
        unsigned char* heap = 0;
        if (total > sizeof(stack_buf))
        {
            heap = (unsigned char*)memalloc((int)total);
            if (!heap) { ccfset(prk, 0, 32); return; }
            buf = heap;
        }

        unsigned int n = 0;
        ccpy(buf + n, HPKE_VERSION, 7); n += 7;
        ccpy(buf + n, suite_id, suite_len); n += suite_len;
        ccpy(buf + n, label, label_len); n += label_len;
        if (ikm_len) { ccpy(buf + n, ikm, ikm_len); n += ikm_len; }

        crypto::hkdf_extract(salt, salt_len, buf, n, prk);

        if (heap) { ccfset(heap, 0, total); memfree(heap); }
    }

    // labeled_info = I2OSP(L,2) || "HPKE-v1" || suite_id || label || info
    bool labeled_expand(const unsigned char prk[32],
                        const unsigned char* suite_id, unsigned int suite_len,
                        const char* label,
                        const unsigned char* info, unsigned int info_len,
                        unsigned char* out, unsigned int out_len)
    {
        unsigned int label_len = (unsigned int)ccslenf(label);
        unsigned int total = 2 + 7 + suite_len + label_len + info_len;

        unsigned char stack_buf[256];
        unsigned char* buf = stack_buf;
        unsigned char* heap = 0;
        if (total > sizeof(stack_buf))
        {
            heap = (unsigned char*)memalloc((int)total);
            if (!heap) return false;
            buf = heap;
        }

        unsigned int n = 0;
        buf[n++] = (unsigned char)(out_len >> 8);
        buf[n++] = (unsigned char)(out_len & 0xFF);
        ccpy(buf + n, HPKE_VERSION, 7); n += 7;
        ccpy(buf + n, suite_id, suite_len); n += suite_len;
        ccpy(buf + n, label, label_len); n += label_len;
        if (info_len) { ccpy(buf + n, info, info_len); n += info_len; }

        bool ok = crypto::hkdf_expand(prk, buf, n, out, out_len);

        if (heap) { ccfset(heap, 0, total); memfree(heap); }
        return ok;
    }

    // DHKEM ExtractAndExpand, RFC 9180 section 4.1.
    bool extract_and_expand(const unsigned char dh[32],
                            const unsigned char* kem_context, unsigned int kem_context_len,
                            unsigned char shared_secret[32])
    {
        unsigned char suite[8];
        unsigned int suite_len = kem_suite_id(suite);

        unsigned char eae_prk[32];
        labeled_extract(0, 0, suite, suite_len, "eae_prk", dh, 32, eae_prk);

        bool ok = labeled_expand(eae_prk, suite, suite_len, "shared_secret",
                                 kem_context, kem_context_len, shared_secret, hpke::NSECRET);
        ccfset(eae_prk, 0, sizeof(eae_prk));
        return ok;
    }

    // Base mode key schedule, RFC 9180 section 5.1.
    bool key_schedule(const unsigned char shared_secret[32],
                      const void* info, unsigned int info_len,
                      hpke::context* out)
    {
        unsigned char suite[16];
        unsigned int suite_len = hpke_suite_id(suite);

        // Base mode: psk and psk_id are both empty.
        unsigned char psk_id_hash[32];
        labeled_extract(0, 0, suite, suite_len, "psk_id_hash", 0, 0, psk_id_hash);

        unsigned char info_hash[32];
        labeled_extract(0, 0, suite, suite_len, "info_hash",
                        (const unsigned char*)info, info_len, info_hash);

        // key_schedule_context = mode || psk_id_hash || info_hash
        unsigned char ks_context[1 + 32 + 32];
        ks_context[0] = 0x00; // mode_base
        ccpy(ks_context + 1, psk_id_hash, 32);
        ccpy(ks_context + 33, info_hash, 32);

        unsigned char secret[32];
        labeled_extract(shared_secret, 32, suite, suite_len, "secret", 0, 0, secret);

        bool ok = labeled_expand(secret, suite, suite_len, "key",
                                 ks_context, sizeof(ks_context), out->key, hpke::NK);
        ok = ok && labeled_expand(secret, suite, suite_len, "base_nonce",
                                  ks_context, sizeof(ks_context), out->base_nonce, hpke::NN);
        ok = ok && labeled_expand(secret, suite, suite_len, "exp",
                                  ks_context, sizeof(ks_context), out->exporter_secret, 32);
        out->seq = 0;

        ccfset(secret, 0, sizeof(secret));
        return ok;
    }

    // nonce = base_nonce XOR I2OSP(seq, Nn)
    void compute_nonce(const hpke::context* ctx, unsigned char nonce[hpke::NN])
    {
        ccpy(nonce, ctx->base_nonce, hpke::NN);
        for (int i = 0; i < 8; i++)
        {
            nonce[hpke::NN - 1 - i] ^= (unsigned char)(ctx->seq >> (i * 8));
        }
    }
}

bool hpke::setup_base_sender(const unsigned char recipient_public[NPK],
                             const void* info, unsigned int info_len,
                             unsigned char enc[NENC], context* out)
{
    unsigned char ephemeral_public[65];
    unsigned char ephemeral_private[96];
    if (!crypto::p256_generate(ephemeral_public, ephemeral_private)) return false;

    unsigned char dh[32];
    bool ok = crypto::p256_ecdh(ephemeral_private, recipient_public, dh);
    ccfset(ephemeral_private, 0, sizeof(ephemeral_private));
    if (!ok) return false;

    // kem_context = enc || pkR
    unsigned char kem_context[NENC + NPK];
    ccpy(kem_context, ephemeral_public, NENC);
    ccpy(kem_context + NENC, recipient_public, NPK);

    unsigned char shared_secret[32];
    ok = extract_and_expand(dh, kem_context, sizeof(kem_context), shared_secret);
    ccfset(dh, 0, sizeof(dh));
    if (!ok) return false;

    ccpy(enc, ephemeral_public, NENC);
    ok = key_schedule(shared_secret, info, info_len, out);
    ccfset(shared_secret, 0, sizeof(shared_secret));
    return ok;
}

bool hpke::setup_base_receiver(const unsigned char enc[NENC],
                               const unsigned char recipient_private[NSK],
                               const void* info, unsigned int info_len,
                               context* out)
{
    unsigned char dh[32];
    if (!crypto::p256_ecdh(recipient_private, enc, dh)) return false;

    unsigned char recipient_public[65];
    crypto::p256_public_from_private(recipient_private, recipient_public);

    unsigned char kem_context[NENC + NPK];
    ccpy(kem_context, enc, NENC);
    ccpy(kem_context + NENC, recipient_public, NPK);

    unsigned char shared_secret[32];
    bool ok = extract_and_expand(dh, kem_context, sizeof(kem_context), shared_secret);
    ccfset(dh, 0, sizeof(dh));
    if (!ok) return false;

    ok = key_schedule(shared_secret, info, info_len, out);
    ccfset(shared_secret, 0, sizeof(shared_secret));
    return ok;
}

bool hpke::seal(context* ctx,
                const void* aad, unsigned int aad_len,
                const void* plain, unsigned int plain_len,
                unsigned char* cipher, unsigned char tag[NT])
{
    unsigned char nonce[NN];
    compute_nonce(ctx, nonce);

    if (!crypto::aesgcm_encrypt(ctx->key, NK, nonce, NN, aad, aad_len,
                                plain, plain_len, cipher, tag, NT))
        return false;

    ctx->seq++;
    return true;
}

bool hpke::open(context* ctx,
                const void* aad, unsigned int aad_len,
                const void* cipher, unsigned int cipher_len,
                const unsigned char tag[NT],
                unsigned char* plain)
{
    unsigned char nonce[NN];
    compute_nonce(ctx, nonce);

    if (!crypto::aesgcm_decrypt(ctx->key, NK, nonce, NN, aad, aad_len,
                                cipher, cipher_len, tag, NT, plain))
        return false;

    ctx->seq++;
    return true;
}

bool hpke::seal_single(const unsigned char recipient_public[NPK],
                       const void* info, unsigned int info_len,
                       const void* aad, unsigned int aad_len,
                       const void* plain, unsigned int plain_len,
                       unsigned char enc[NENC],
                       unsigned char* cipher, unsigned char tag[NT])
{
    context ctx;
    if (!setup_base_sender(recipient_public, info, info_len, enc, &ctx)) return false;

    bool ok = seal(&ctx, aad, aad_len, plain, plain_len, cipher, tag);
    ccfset(&ctx, 0, sizeof(ctx));
    return ok;
}

bool hpke::open_single(const unsigned char enc[NENC],
                       const unsigned char recipient_private[NSK],
                       const void* info, unsigned int info_len,
                       const void* aad, unsigned int aad_len,
                       const void* cipher, unsigned int cipher_len,
                       const unsigned char tag[NT],
                       unsigned char* plain)
{
    context ctx;
    if (!setup_base_receiver(enc, recipient_private, info, info_len, &ctx)) return false;

    bool ok = open(&ctx, aad, aad_len, cipher, cipher_len, tag, plain);
    ccfset(&ctx, 0, sizeof(ctx));
    return ok;
}

// ---------------------------------------------------------------------------
// DeriveKeyPair
// ---------------------------------------------------------------------------
//
// RFC 9180 section 7.1.3. MLS needs it: TreeKEM hands every member the same
// node secret and each has to turn it into the same key pair. Rejection
// sampling is what makes it deterministic across implementations - the counter
// is part of the derivation, not an implementation detail.

bool hpke::derive_key_pair(const void* ikm, unsigned int ikm_len,
                           unsigned char public_key[NPK],
                           unsigned char private_key[NSK])
{
    unsigned char suite[8];
    unsigned int suite_len = kem_suite_id(suite);

    unsigned char dkp_prk[32];
    labeled_extract(0, 0, suite, suite_len, "dkp_prk",
                    (const unsigned char*)ikm, ikm_len, dkp_prk);

    for (unsigned int counter = 0; counter < 256; counter++)
    {
        unsigned char info[1];
        info[0] = (unsigned char)counter;

        unsigned char candidate[32];
        if (!labeled_expand(dkp_prk, suite, suite_len, "candidate",
                            info, sizeof(info), candidate, 32))
            return false;

        // The bitmask for P-256 is 0xFF, so nothing is cleared here. It is
        // written out because the other curves in the same family do clear
        // bits, and a reader should not have to go and check which.
        candidate[0] &= 0xFF;

        if (!crypto::p256_scalar_in_range(candidate)) continue;
        return crypto::p256_keypair_from_scalar(candidate, public_key, private_key);
    }

    return false;
}
