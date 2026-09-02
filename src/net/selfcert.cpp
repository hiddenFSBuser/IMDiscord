#include "pch.h"
#include "selfcert.h"
#include "core/crypto.h"
#include "core/log.h"
#include "tlse.h"

// ---------------------------------------------------------------------------
// Just enough DER to write one certificate
//
// Built inside out: every piece is encoded into a buffer of its own and then
// wrapped by its parent. That costs a few copies of a document under a
// kilobyte, once per call, and it avoids the back-patching that makes a
// hand written encoder hard to read and easy to get wrong.
// ---------------------------------------------------------------------------

namespace
{
    // Tag, length, body. Returns the total written, or 0 when it will not fit.
    int tlv(unsigned char* out, int cap, unsigned char tag,
            const unsigned char* body, int body_len)
    {
        if (body_len < 0) return 0;

        int header = 2;
        if (body_len > 0xFFFF)     header = 5;
        else if (body_len > 0xFF)  header = 4;
        else if (body_len > 0x7F)  header = 3;

        if (header + body_len > cap) return 0;

        int at = 0;
        out[at++] = tag;

        if (header == 2)
        {
            out[at++] = (unsigned char)body_len;
        }
        else
        {
            out[at++] = (unsigned char)(0x80 | (header - 2));
            for (int shift = (header - 3) * 8; shift >= 0; shift -= 8)
                out[at++] = (unsigned char)((body_len >> shift) & 0xFF);
        }

        if (body_len) ccpy(out + at, body, (size_t)body_len);
        return at + body_len;
    }

    int seq(unsigned char* out, int cap, const unsigned char* body, int len)
    { return tlv(out, cap, 0x30, body, len); }

    int set_of(unsigned char* out, int cap, const unsigned char* body, int len)
    { return tlv(out, cap, 0x31, body, len); }

    // An INTEGER that must not come out negative: der reads the high bit as a
    // sign, so a value that starts with one needs a leading zero.
    int integer(unsigned char* out, int cap, const unsigned char* value, int len)
    {
        while (len > 1 && value[0] == 0 && (value[1] & 0x80) == 0) { value++; len--; }

        if (value[0] & 0x80)
        {
            unsigned char padded[64];
            if (len + 1 > (int)sizeof(padded)) return 0;

            padded[0] = 0;
            ccpy(padded + 1, value, (size_t)len);
            return tlv(out, cap, 0x02, padded, len + 1);
        }

        return tlv(out, cap, 0x02, value, len);
    }

    int integer_small(unsigned char* out, int cap, unsigned char value)
    { return tlv(out, cap, 0x02, &value, 1); }

    // A BIT STRING carrying whole bytes, so the count of unused bits is zero.
    int bitstring(unsigned char* out, int cap, const unsigned char* body, int len)
    {
        unsigned char wrapped[256];
        if (len + 1 > (int)sizeof(wrapped)) return 0;

        wrapped[0] = 0;
        ccpy(wrapped + 1, body, (size_t)len);
        return tlv(out, cap, 0x03, wrapped, len + 1);
    }

    // 1.2.840.10045.2.1
    const unsigned char OID_EC_PUBLIC_KEY[] = { 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x02, 0x01 };
    // 1.2.840.10045.3.1.7
    const unsigned char OID_PRIME256V1[]    = { 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03, 0x01, 0x07 };
    // 1.2.840.10045.4.3.2
    const unsigned char OID_ECDSA_SHA256[]  = { 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x04, 0x03, 0x02 };
    // 2.5.4.3
    const unsigned char OID_COMMON_NAME[]   = { 0x55, 0x04, 0x03 };

    int oid(unsigned char* out, int cap, const unsigned char* body, int len)
    { return tlv(out, cap, 0x06, body, len); }

    // SEQUENCE { OID ecdsa-with-SHA256 }. No parameters: for ecdsa they are
    // absent rather than NULL, and a NULL here is what makes some parsers
    // reject the certificate.
    int alg_ecdsa_sha256(unsigned char* out, int cap)
    {
        unsigned char body[16];
        int n = oid(body, sizeof(body), OID_ECDSA_SHA256, sizeof(OID_ECDSA_SHA256));
        if (!n) return 0;

        return seq(out, cap, body, n);
    }

    // SEQUENCE { OID id-ecPublicKey, OID prime256v1 }
    int alg_ec_p256(unsigned char* out, int cap)
    {
        unsigned char body[32];
        int n = oid(body, sizeof(body), OID_EC_PUBLIC_KEY, sizeof(OID_EC_PUBLIC_KEY));
        if (!n) return 0;

        int m = oid(body + n, (int)sizeof(body) - n, OID_PRIME256V1, sizeof(OID_PRIME256V1));
        if (!m) return 0;

        return seq(out, cap, body, n + m);
    }

    // SEQUENCE { SEQUENCE { OID ec, OID p256 }, BIT STRING 04||X||Y }
    int public_key_info(unsigned char* out, int cap, const unsigned char public_key[65])
    {
        unsigned char body[128];

        int n = alg_ec_p256(body, sizeof(body));
        if (!n) return 0;

        int m = bitstring(body + n, (int)sizeof(body) - n, public_key, 65);
        if (!m) return 0;

        return seq(out, cap, body, n + m);
    }

    // Name ::= SEQUENCE { SET { SEQUENCE { OID commonName, UTF8String } } }
    int name_with_cn(unsigned char* out, int cap, const char* cn)
    {
        int cn_len = (int)ccslenf(cn);

        unsigned char pair[128];
        int n = oid(pair, sizeof(pair), OID_COMMON_NAME, sizeof(OID_COMMON_NAME));
        if (!n) return 0;

        int m = tlv(pair + n, (int)sizeof(pair) - n, 0x0C, (const unsigned char*)cn, cn_len);
        if (!m) return 0;

        unsigned char inner[160];
        int s = seq(inner, sizeof(inner), pair, n + m);
        if (!s) return 0;

        unsigned char outer[192];
        int t = set_of(outer, sizeof(outer), inner, s);
        if (!t) return 0;

        return seq(out, cap, outer, t);
    }

    void utc_time(char* out, int cap, int days_from_now)
    {
        SYSTEMTIME st;
        GetSystemTime(&st);

        // Whole days moved by shifting the file time, which handles month and
        // year ends without a calendar of our own.
        FILETIME ft;
        SystemTimeToFileTime(&st, &ft);

        unsigned long long ticks =
            ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;

        ticks += (unsigned long long)days_from_now * 24ull * 3600ull * 10000000ull;

        ft.dwLowDateTime  = (DWORD)(ticks & 0xFFFFFFFF);
        ft.dwHighDateTime = (DWORD)(ticks >> 32);
        FileTimeToSystemTime(&ft, &st);

        // UTCTime is two digit years. Fine until 2049, and a certificate that
        // lives for thirty days has no opinion about 2049.
        cnprint(out, cap, "%02u%02u%02u%02u%02u%02uZ",
                (unsigned int)(st.wYear % 100), (unsigned int)st.wMonth,
                (unsigned int)st.wDay, (unsigned int)st.wHour,
                (unsigned int)st.wMinute, (unsigned int)st.wSecond);
    }

    // SEQUENCE { UTCTime notBefore, UTCTime notAfter }
    int validity(unsigned char* out, int cap)
    {
        char before[24], after[24];

        // A day back, because a machine whose clock is a little fast would
        // otherwise present a certificate that is not valid yet.
        utc_time(before, sizeof(before), -1);
        utc_time(after, sizeof(after), 30);

        unsigned char body[64];
        int n = tlv(body, sizeof(body), 0x17,
                    (const unsigned char*)before, (int)ccslenf(before));
        if (!n) return 0;

        int m = tlv(body + n, (int)sizeof(body) - n, 0x17,
                    (const unsigned char*)after, (int)ccslenf(after));
        if (!m) return 0;

        return seq(out, cap, body, n + m);
    }

    int pem_wrap(char* out, int cap, const char* label,
                 const unsigned char* der, int der_len)
    {
        ubuffer b64;
        b64.init();
        crypto::base64_encode(der, (unsigned int)der_len, &b64);

        int at = cnprint(out, cap, "-----BEGIN %s-----\n", label);

        // Sixty four characters to the line, as every tool that reads pem
        // expects and some of them require.
        for (unsigned int i = 0; i < b64.size; i += 64)
        {
            unsigned int run = b64.size - i;
            if (run > 64) run = 64;

            if (at + (int)run + 2 > cap) { b64.free_buffer(); return 0; }

            ccpy(out + at, b64.data + i, run);
            at += (int)run;
            out[at++] = '\n';
        }

        b64.free_buffer();

        at += cnprint(out + at, cap - at, "-----END %s-----\n", label);
        if (at >= cap) return 0;

        out[at] = 0;
        return at;
    }
}

bool selfcert::generate(material* out)
{
    if (!out) return false;
    ccfset(out, 0, sizeof(*out));

    if (!crypto::p256_generate(out->public_key, out->private_key))
    {
        log_line("selfcert: не удалось создать ключ p-256");
        return false;
    }

    // ---- tbsCertificate ---------------------------------------------------
    unsigned char tbs_body[1024];
    int at = 0;

    // [0] EXPLICIT version, v3
    {
        unsigned char v[8];
        int n = integer_small(v, sizeof(v), 2);
        if (!n) return false;

        int m = tlv(tbs_body + at, (int)sizeof(tbs_body) - at, 0xA0, v, n);
        if (!m) return false;
        at += m;
    }

    // serialNumber. Random, and kept short of the sign bit by clearing the
    // top one - a negative serial is legal and universally disliked.
    {
        unsigned char serial[16];
        crypto::random_bytes(serial, sizeof(serial));
        serial[0] &= 0x7F;
        if (!serial[0]) serial[0] = 1;

        int n = integer(tbs_body + at, (int)sizeof(tbs_body) - at, serial, sizeof(serial));
        if (!n) return false;
        at += n;
    }

    {
        int n = alg_ecdsa_sha256(tbs_body + at, (int)sizeof(tbs_body) - at);
        if (!n) return false;
        at += n;
    }

    // A name that says nothing. Browsers put "WebRTC" here and so does this;
    // anything derived from the machine or the account would be a detail
    // handed to the server for no reason.
    {
        int n = name_with_cn(tbs_body + at, (int)sizeof(tbs_body) - at, "WebRTC");
        if (!n) return false;
        at += n;

        int v = validity(tbs_body + at, (int)sizeof(tbs_body) - at);
        if (!v) return false;
        at += v;

        int s = name_with_cn(tbs_body + at, (int)sizeof(tbs_body) - at, "WebRTC");
        if (!s) return false;
        at += s;
    }

    {
        int n = public_key_info(tbs_body + at, (int)sizeof(tbs_body) - at, out->public_key);
        if (!n) return false;
        at += n;
    }

    unsigned char tbs[1024];
    int tbs_len = seq(tbs, sizeof(tbs), tbs_body, at);
    if (!tbs_len) return false;

    // ---- signature --------------------------------------------------------
    unsigned char sig[128];
    unsigned int sig_len = 0;

    if (!crypto::p256_sign(out->private_key, tbs, (unsigned int)tbs_len, sig, &sig_len))
    {
        log_line("selfcert: подпись не получилась");
        return false;
    }

    unsigned char cert_body[1400];
    int body_at = 0;

    ccpy(cert_body, tbs, (size_t)tbs_len);
    body_at += tbs_len;

    {
        int n = alg_ecdsa_sha256(cert_body + body_at, (int)sizeof(cert_body) - body_at);
        if (!n) return false;
        body_at += n;

        int m = bitstring(cert_body + body_at, (int)sizeof(cert_body) - body_at,
                          sig, (int)sig_len);
        if (!m) return false;
        body_at += m;
    }

    unsigned char cert_der[1500];
    int cert_len = seq(cert_der, sizeof(cert_der), cert_body, body_at);
    if (!cert_len) return false;

    out->cert_pem_len = pem_wrap(out->cert_pem, sizeof(out->cert_pem),
                                 "CERTIFICATE", cert_der, cert_len);
    if (!out->cert_pem_len) return false;

    // ---- the private key, sec1 shaped -------------------------------------
    //
    // ECPrivateKey rather than pkcs#8: the dtls implementation here looks for
    // the scalar at a fixed place in the structure, and that place is this
    // one.
    {
        unsigned char body[256];
        int k = 0;

        int n = integer_small(body, sizeof(body), 1);
        if (!n) return false;
        k += n;

        // The scalar sits last in the layout p256 uses: x, y, then d.
        n = tlv(body + k, (int)sizeof(body) - k, 0x04, out->private_key + 64, 32);
        if (!n) return false;
        k += n;

        unsigned char params[16];
        int p = oid(params, sizeof(params), OID_PRIME256V1, sizeof(OID_PRIME256V1));
        if (!p) return false;

        n = tlv(body + k, (int)sizeof(body) - k, 0xA0, params, p);
        if (!n) return false;
        k += n;

        unsigned char pub[80];
        int q = bitstring(pub, sizeof(pub), out->public_key, 65);
        if (!q) return false;

        n = tlv(body + k, (int)sizeof(body) - k, 0xA1, pub, q);
        if (!n) return false;
        k += n;

        unsigned char key_der[320];
        int key_len = seq(key_der, sizeof(key_der), body, k);
        if (!key_len) return false;

        out->key_pem_len = pem_wrap(out->key_pem, sizeof(out->key_pem),
                                    "EC PRIVATE KEY", key_der, key_len);

        ccfset(body, 0, sizeof(body));
        ccfset(key_der, 0, sizeof(key_der));

        if (!out->key_pem_len) return false;
    }

    // ---- the digest the offer promises ------------------------------------
    {
        unsigned char digest[32];
        crypto::sha256(cert_der, (unsigned int)cert_len, digest);

        int n = cnprint(out->fingerprint, sizeof(out->fingerprint), "sha-256");
        for (int i = 0; i < 32; i++)
            n += cnprint(out->fingerprint + n, (int)sizeof(out->fingerprint) - n,
                         "%c%02X", i ? ':' : ' ', digest[i]);
    }

    return true;
}

bool selfcert::self_test()
{
    selfcert::material m;
    if (!selfcert::generate(&m))
    {
        log_line("selfcert: генерация не удалась");
        return false;
    }

    // The point of the whole exercise: the dtls implementation has to accept
    // both halves. A certificate this client cannot load is a certificate
    // discord will never see.
    struct TLSContext* ctx = tls_create_context(0, DTLS_V12);
    if (!ctx)
    {
        log_line("selfcert: не создался контекст dtls");
        return false;
    }

    bool ok = true;

    if (tls_load_certificates(ctx, (const unsigned char*)m.cert_pem, m.cert_pem_len) <= 0)
    {
        log_line("selfcert: tlse не принял сертификат");
        ok = false;
    }

    if (ok && tls_load_private_key(ctx, (const unsigned char*)m.key_pem, m.key_pem_len) <= 0)
    {
        log_line("selfcert: tlse не принял закрытый ключ");
        ok = false;
    }

    tls_destroy_context(ctx);
    if (!ok) return false;

    // And the digest has to be the one tlse computes over the same file,
    // because that is the value the far side checks the handshake against.
    // Two independent paths to it: ours over the der, tlse's over the pem.
    char theirs[160];
    if (tls_cert_fingerprint(m.cert_pem, m.cert_pem_len, theirs, sizeof(theirs)) < 0)
    {
        log_line("selfcert: tlse не посчитал отпечаток");
        return false;
    }

    // Ours carries the algorithm name in front, as the sdp line wants it.
    const char* ours = m.fingerprint;
    while (*ours && *ours != ' ') ours++;
    while (*ours == ' ') ours++;

    int i = 0, j = 0;
    for (;;)
    {
        while (theirs[i] == ':') i++;
        while (ours[j] == ':') j++;

        if (!theirs[i] || !ours[j]) break;

        if (cctolower(theirs[i]) != cctolower(ours[j]))
        {
            log_line("selfcert: отпечатки разошлись");
            log_line("  наш:  %s", m.fingerprint);
            log_line("  tlse: %s", theirs);
            return false;
        }

        i++;
        j++;
    }

    if (theirs[i] || ours[j])
    {
        log_line("selfcert: отпечатки разной длины");
        return false;
    }

    // Two calls must not produce the same certificate: a fingerprint that
    // repeats is a mark the server can follow between sessions, which is the
    // reason this is generated rather than shipped.
    selfcert::material other;
    if (!selfcert::generate(&other))
    {
        log_line("selfcert: вторая генерация не удалась");
        return false;
    }

    if (ccscmp(other.fingerprint, m.fingerprint) == 0)
    {
        log_line("selfcert: два сертификата подряд вышли одинаковыми");
        return false;
    }

    log_line("selfcert: самопроверка пройдена, отпечаток %s", m.fingerprint);
    return true;
}
