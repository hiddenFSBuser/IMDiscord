#include "pch.h"
#include "crypto.h"
#include "log.h"

extern "C" unsigned char __stdcall SystemFunction036(void* buffer, unsigned long length);

namespace crypto
{

// ---------------------------------------------------------------------------
// random
// ---------------------------------------------------------------------------

void random_bytes(void* out, unsigned int len)
{
    // RtlGenRandom out of advapi32, which is linked anyway. No bcrypt involved.
    if (len && SystemFunction036(out, (unsigned long)len))
        return;

    // Fall back to a time/counter mix; only reached if the system RNG fails.
    unsigned char* p = (unsigned char*)out;
    LARGE_INTEGER qpc;
    QueryPerformanceCounter(&qpc);
    unsigned long long s = (unsigned long long)qpc.QuadPart ^ ((unsigned long long)GetCurrentThreadId() << 32) ^ GetTickCount64();
    for (unsigned int i = 0; i < len; i++)
    {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        p[i] = (unsigned char)(s >> 24);
    }
}

// ---------------------------------------------------------------------------
// sha256
// ---------------------------------------------------------------------------

static const unsigned int SHA256_K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static inline unsigned int rotr32(unsigned int x, int n) { return (x >> n) | (x << (32 - n)); }

static void sha256_compress(unsigned int state[8], const unsigned char block[64])
{
    unsigned int w[64];
    for (int i = 0; i < 16; i++)
    {
        w[i] = ((unsigned int)block[i * 4 + 0] << 24) | ((unsigned int)block[i * 4 + 1] << 16) |
               ((unsigned int)block[i * 4 + 2] << 8) | ((unsigned int)block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; i++)
    {
        unsigned int s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        unsigned int s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    unsigned int a = state[0], b = state[1], c = state[2], d = state[3];
    unsigned int e = state[4], f = state[5], g = state[6], h = state[7];

    for (int i = 0; i < 64; i++)
    {
        unsigned int S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        unsigned int ch = (e & f) ^ ((~e) & g);
        unsigned int t1 = h + S1 + ch + SHA256_K[i] + w[i];
        unsigned int S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        unsigned int maj = (a & b) ^ (a & c) ^ (b & c);
        unsigned int t2 = S0 + maj;

        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

void sha256_init(sha256_ctx* ctx)
{
    ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
    ctx->length = 0;
    ctx->block_len = 0;
}

void sha256_update(sha256_ctx* ctx, const void* data, unsigned int len)
{
    const unsigned char* p = (const unsigned char*)data;
    ctx->length += len;

    while (len > 0)
    {
        unsigned int space = 64 - ctx->block_len;
        unsigned int take = len < space ? len : space;
        ccpy(ctx->block + ctx->block_len, p, take);
        ctx->block_len += take;
        p += take;
        len -= take;

        if (ctx->block_len == 64)
        {
            sha256_compress(ctx->state, ctx->block);
            ctx->block_len = 0;
        }
    }
}

void sha256_final(sha256_ctx* ctx, unsigned char out[32])
{
    unsigned long long bits = ctx->length * 8;

    unsigned char pad = 0x80;
    sha256_update(ctx, &pad, 1);

    unsigned char zero = 0;
    while (ctx->block_len != 56) sha256_update(ctx, &zero, 1);

    unsigned char tail[8];
    for (int i = 0; i < 8; i++) tail[i] = (unsigned char)(bits >> (56 - i * 8));
    // Length bytes must not re-enter the counter, so write them directly.
    ccpy(ctx->block + 56, tail, 8);
    sha256_compress(ctx->state, ctx->block);
    ctx->block_len = 0;

    for (int i = 0; i < 8; i++)
    {
        out[i * 4 + 0] = (unsigned char)(ctx->state[i] >> 24);
        out[i * 4 + 1] = (unsigned char)(ctx->state[i] >> 16);
        out[i * 4 + 2] = (unsigned char)(ctx->state[i] >> 8);
        out[i * 4 + 3] = (unsigned char)(ctx->state[i]);
    }
}

void sha256(const void* data, unsigned int len, unsigned char out[32])
{
    sha256_ctx ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, out);
}

// ---------------------------------------------------------------------------
// sha1 (FIPS 180-4; the websocket handshake is its only user)
// ---------------------------------------------------------------------------

static inline unsigned int rotl32(unsigned int x, int n) { return (x << n) | (x >> (32 - n)); }

static void sha1_compress(unsigned int state[5], const unsigned char block[64])
{
    unsigned int w[80];
    for (int i = 0; i < 16; i++)
    {
        w[i] = ((unsigned int)block[i * 4 + 0] << 24) | ((unsigned int)block[i * 4 + 1] << 16) |
               ((unsigned int)block[i * 4 + 2] << 8) | ((unsigned int)block[i * 4 + 3]);
    }
    for (int i = 16; i < 80; i++)
        w[i] = rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

    unsigned int a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];

    for (int i = 0; i < 80; i++)
    {
        unsigned int f, k;
        if (i < 20)      { f = (b & c) | ((~b) & d); k = 0x5A827999; }
        else if (i < 40) { f = b ^ c ^ d;            k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
        else             { f = b ^ c ^ d;            k = 0xCA62C1D6; }

        unsigned int t = rotl32(a, 5) + f + e + k + w[i];
        e = d; d = c; c = rotl32(b, 30); b = a; a = t;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e;
}

void sha1_init(sha1_ctx* ctx)
{
    ctx->state[0] = 0x67452301; ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE; ctx->state[3] = 0x10325476;
    ctx->state[4] = 0xC3D2E1F0;
    ctx->length = 0;
    ctx->block_len = 0;
}

void sha1_update(sha1_ctx* ctx, const void* data, unsigned int len)
{
    const unsigned char* p = (const unsigned char*)data;
    ctx->length += len;

    while (len > 0)
    {
        unsigned int space = 64 - ctx->block_len;
        unsigned int take = len < space ? len : space;
        ccpy(ctx->block + ctx->block_len, p, take);
        ctx->block_len += take;
        p += take;
        len -= take;

        if (ctx->block_len == 64)
        {
            sha1_compress(ctx->state, ctx->block);
            ctx->block_len = 0;
        }
    }
}

void sha1_final(sha1_ctx* ctx, unsigned char out[20])
{
    unsigned long long bits = ctx->length * 8;

    unsigned char pad = 0x80;
    sha1_update(ctx, &pad, 1);

    unsigned char zero = 0;
    while (ctx->block_len != 56) sha1_update(ctx, &zero, 1);

    unsigned char tail[8];
    for (int i = 0; i < 8; i++) tail[i] = (unsigned char)(bits >> (56 - i * 8));
    // Length bytes must not re-enter the counter, so write them directly.
    ccpy(ctx->block + 56, tail, 8);
    sha1_compress(ctx->state, ctx->block);
    ctx->block_len = 0;

    for (int i = 0; i < 5; i++)
    {
        out[i * 4 + 0] = (unsigned char)(ctx->state[i] >> 24);
        out[i * 4 + 1] = (unsigned char)(ctx->state[i] >> 16);
        out[i * 4 + 2] = (unsigned char)(ctx->state[i] >> 8);
        out[i * 4 + 3] = (unsigned char)(ctx->state[i]);
    }
}

void sha1(const void* data, unsigned int len, unsigned char out[20])
{
    sha1_ctx ctx;
    sha1_init(&ctx);
    sha1_update(&ctx, data, len);
    sha1_final(&ctx, out);
}

// ---------------------------------------------------------------------------
// base64
// ---------------------------------------------------------------------------

static const char B64_ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void base64_encode(const void* data, unsigned int len, ubuffer* out)
{
    const unsigned char* p = (const unsigned char*)data;
    out->reserve(out->size + ((len + 2) / 3) * 4 + 1);

    unsigned int i = 0;
    while (i + 3 <= len)
    {
        unsigned int v = ((unsigned int)p[i] << 16) | ((unsigned int)p[i + 1] << 8) | p[i + 2];
        out->append_char(B64_ALPHABET[(v >> 18) & 0x3F]);
        out->append_char(B64_ALPHABET[(v >> 12) & 0x3F]);
        out->append_char(B64_ALPHABET[(v >> 6) & 0x3F]);
        out->append_char(B64_ALPHABET[v & 0x3F]);
        i += 3;
    }

    unsigned int rest = len - i;
    if (rest == 1)
    {
        unsigned int v = (unsigned int)p[i] << 16;
        out->append_char(B64_ALPHABET[(v >> 18) & 0x3F]);
        out->append_char(B64_ALPHABET[(v >> 12) & 0x3F]);
        out->append_char('=');
        out->append_char('=');
    }
    else if (rest == 2)
    {
        unsigned int v = ((unsigned int)p[i] << 16) | ((unsigned int)p[i + 1] << 8);
        out->append_char(B64_ALPHABET[(v >> 18) & 0x3F]);
        out->append_char(B64_ALPHABET[(v >> 12) & 0x3F]);
        out->append_char(B64_ALPHABET[(v >> 6) & 0x3F]);
        out->append_char('=');
    }
}

static int b64_value(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}

bool base64_decode(const char* text, int len, ubuffer* out)
{
    if (!text) return false;
    if (len < 0) len = (int)ccslenf(text);

    unsigned int acc = 0;
    int bits = 0;
    for (int i = 0; i < len; i++)
    {
        char c = text[i];
        if (c == '=' || c == '\r' || c == '\n' || c == ' ') continue;
        int v = b64_value(c);
        if (v < 0) return false;

        acc = (acc << 6) | (unsigned int)v;
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out->append_char((char)((acc >> bits) & 0xFF));
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// chacha20 / poly1305
// ---------------------------------------------------------------------------

static inline unsigned int load32_le(const unsigned char* p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}
static inline void store32_le(unsigned char* p, unsigned int v)
{
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}

#define QROUND(a, b, c, d) \
    a += b; d ^= a; d = rotl32(d, 16); \
    c += d; b ^= c; b = rotl32(b, 12); \
    a += b; d ^= a; d = rotl32(d, 8);  \
    c += d; b ^= c; b = rotl32(b, 7);

static void chacha20_rounds(unsigned int x[16])
{
    for (int i = 0; i < 10; i++)
    {
        QROUND(x[0], x[4], x[8], x[12]);
        QROUND(x[1], x[5], x[9], x[13]);
        QROUND(x[2], x[6], x[10], x[14]);
        QROUND(x[3], x[7], x[11], x[15]);
        QROUND(x[0], x[5], x[10], x[15]);
        QROUND(x[1], x[6], x[11], x[12]);
        QROUND(x[2], x[7], x[8], x[13]);
        QROUND(x[3], x[4], x[9], x[14]);
    }
}

static void chacha20_init_state(unsigned int st[16], const unsigned char key[32],
                                unsigned int counter, const unsigned char nonce[12])
{
    st[0] = 0x61707865; st[1] = 0x3320646e; st[2] = 0x79622d32; st[3] = 0x6b206574;
    for (int i = 0; i < 8; i++) st[4 + i] = load32_le(key + i * 4);
    st[12] = counter;
    st[13] = load32_le(nonce + 0);
    st[14] = load32_le(nonce + 4);
    st[15] = load32_le(nonce + 8);
}

static void chacha20_block(const unsigned char key[32], unsigned int counter,
                           const unsigned char nonce[12], unsigned char out[64])
{
    unsigned int st[16], x[16];
    chacha20_init_state(st, key, counter, nonce);
    ccpy(x, st, sizeof(st));
    chacha20_rounds(x);
    for (int i = 0; i < 16; i++) store32_le(out + i * 4, x[i] + st[i]);
}

static void chacha20_xor(const unsigned char key[32], unsigned int counter,
                         const unsigned char nonce[12],
                         const unsigned char* in, unsigned char* outp, unsigned int len)
{
    unsigned char stream[64];
    unsigned int offset = 0;
    while (offset < len)
    {
        chacha20_block(key, counter, nonce, stream);
        counter++;
        unsigned int chunk = len - offset;
        if (chunk > 64) chunk = 64;
        for (unsigned int i = 0; i < chunk; i++) outp[offset + i] = in[offset + i] ^ stream[i];
        offset += chunk;
    }
}

static void hchacha20(const unsigned char key[32], const unsigned char nonce16[16], unsigned char out[32])
{
    unsigned int x[16];
    x[0] = 0x61707865; x[1] = 0x3320646e; x[2] = 0x79622d32; x[3] = 0x6b206574;
    for (int i = 0; i < 8; i++) x[4 + i] = load32_le(key + i * 4);
    for (int i = 0; i < 4; i++) x[12 + i] = load32_le(nonce16 + i * 4);

    chacha20_rounds(x);

    for (int i = 0; i < 4; i++) store32_le(out + i * 4, x[i]);
    for (int i = 0; i < 4; i++) store32_le(out + 16 + i * 4, x[12 + i]);
}

struct poly1305_ctx
{
    unsigned int r[5];
    unsigned int h[5];
    unsigned int pad[4];
    unsigned char buffer[16];
    unsigned int leftover;
    bool final_block;
};

static void poly1305_init(poly1305_ctx* st, const unsigned char key[32])
{
    st->r[0] = (load32_le(key + 0)) & 0x3ffffff;
    st->r[1] = (load32_le(key + 3) >> 2) & 0x3ffff03;
    st->r[2] = (load32_le(key + 6) >> 4) & 0x3ffc0ff;
    st->r[3] = (load32_le(key + 9) >> 6) & 0x3f03fff;
    st->r[4] = (load32_le(key + 12) >> 8) & 0x00fffff;

    for (int i = 0; i < 5; i++) st->h[i] = 0;
    for (int i = 0; i < 4; i++) st->pad[i] = load32_le(key + 16 + i * 4);

    st->leftover = 0;
    st->final_block = false;
}

static void poly1305_blocks(poly1305_ctx* st, const unsigned char* m, unsigned int bytes)
{
    const unsigned int hibit = st->final_block ? 0 : (1 << 24);

    unsigned int r0 = st->r[0], r1 = st->r[1], r2 = st->r[2], r3 = st->r[3], r4 = st->r[4];
    unsigned int s1 = r1 * 5, s2 = r2 * 5, s3 = r3 * 5, s4 = r4 * 5;
    unsigned int h0 = st->h[0], h1 = st->h[1], h2 = st->h[2], h3 = st->h[3], h4 = st->h[4];

    while (bytes >= 16)
    {
        h0 += (load32_le(m + 0)) & 0x3ffffff;
        h1 += (load32_le(m + 3) >> 2) & 0x3ffffff;
        h2 += (load32_le(m + 6) >> 4) & 0x3ffffff;
        h3 += (load32_le(m + 9) >> 6) & 0x3ffffff;
        h4 += (load32_le(m + 12) >> 8) | hibit;

        unsigned long long d0 = (unsigned long long)h0 * r0 + (unsigned long long)h1 * s4 +
                                (unsigned long long)h2 * s3 + (unsigned long long)h3 * s2 + (unsigned long long)h4 * s1;
        unsigned long long d1 = (unsigned long long)h0 * r1 + (unsigned long long)h1 * r0 +
                                (unsigned long long)h2 * s4 + (unsigned long long)h3 * s3 + (unsigned long long)h4 * s2;
        unsigned long long d2 = (unsigned long long)h0 * r2 + (unsigned long long)h1 * r1 +
                                (unsigned long long)h2 * r0 + (unsigned long long)h3 * s4 + (unsigned long long)h4 * s3;
        unsigned long long d3 = (unsigned long long)h0 * r3 + (unsigned long long)h1 * r2 +
                                (unsigned long long)h2 * r1 + (unsigned long long)h3 * r0 + (unsigned long long)h4 * s4;
        unsigned long long d4 = (unsigned long long)h0 * r4 + (unsigned long long)h1 * r3 +
                                (unsigned long long)h2 * r2 + (unsigned long long)h3 * r1 + (unsigned long long)h4 * r0;

        unsigned int c = (unsigned int)(d0 >> 26); h0 = (unsigned int)d0 & 0x3ffffff;
        d1 += c; c = (unsigned int)(d1 >> 26); h1 = (unsigned int)d1 & 0x3ffffff;
        d2 += c; c = (unsigned int)(d2 >> 26); h2 = (unsigned int)d2 & 0x3ffffff;
        d3 += c; c = (unsigned int)(d3 >> 26); h3 = (unsigned int)d3 & 0x3ffffff;
        d4 += c; c = (unsigned int)(d4 >> 26); h4 = (unsigned int)d4 & 0x3ffffff;
        h0 += c * 5; c = h0 >> 26; h0 &= 0x3ffffff;
        h1 += c;

        m += 16;
        bytes -= 16;
    }

    st->h[0] = h0; st->h[1] = h1; st->h[2] = h2; st->h[3] = h3; st->h[4] = h4;
}

static void poly1305_update(poly1305_ctx* st, const unsigned char* m, unsigned int bytes)
{
    if (st->leftover)
    {
        unsigned int want = 16 - st->leftover;
        if (want > bytes) want = bytes;
        ccpy(st->buffer + st->leftover, m, want);
        bytes -= want;
        m += want;
        st->leftover += want;
        if (st->leftover < 16) return;
        poly1305_blocks(st, st->buffer, 16);
        st->leftover = 0;
    }

    if (bytes >= 16)
    {
        unsigned int want = bytes & ~((unsigned int)15);
        poly1305_blocks(st, m, want);
        m += want;
        bytes -= want;
    }

    if (bytes)
    {
        ccpy(st->buffer + st->leftover, m, bytes);
        st->leftover += bytes;
    }
}

static void poly1305_finish(poly1305_ctx* st, unsigned char mac[16])
{
    if (st->leftover)
    {
        unsigned int i = st->leftover;
        st->buffer[i++] = 1;
        for (; i < 16; i++) st->buffer[i] = 0;
        st->final_block = true;
        poly1305_blocks(st, st->buffer, 16);
    }

    unsigned int h0 = st->h[0], h1 = st->h[1], h2 = st->h[2], h3 = st->h[3], h4 = st->h[4];

    unsigned int c = h1 >> 26; h1 &= 0x3ffffff;
    h2 += c; c = h2 >> 26; h2 &= 0x3ffffff;
    h3 += c; c = h3 >> 26; h3 &= 0x3ffffff;
    h4 += c; c = h4 >> 26; h4 &= 0x3ffffff;
    h0 += c * 5; c = h0 >> 26; h0 &= 0x3ffffff;
    h1 += c;

    unsigned int g0 = h0 + 5; c = g0 >> 26; g0 &= 0x3ffffff;
    unsigned int g1 = h1 + c; c = g1 >> 26; g1 &= 0x3ffffff;
    unsigned int g2 = h2 + c; c = g2 >> 26; g2 &= 0x3ffffff;
    unsigned int g3 = h3 + c; c = g3 >> 26; g3 &= 0x3ffffff;
    unsigned int g4 = h4 + c - (1u << 26);

    unsigned int mask = (g4 >> 31) - 1;
    g0 &= mask; g1 &= mask; g2 &= mask; g3 &= mask; g4 &= mask;
    mask = ~mask;
    h0 = (h0 & mask) | g0;
    h1 = (h1 & mask) | g1;
    h2 = (h2 & mask) | g2;
    h3 = (h3 & mask) | g3;
    h4 = (h4 & mask) | g4;

    h0 = (h0 | (h1 << 26)) & 0xffffffff;
    h1 = ((h1 >> 6) | (h2 << 20)) & 0xffffffff;
    h2 = ((h2 >> 12) | (h3 << 14)) & 0xffffffff;
    h3 = ((h3 >> 18) | (h4 << 8)) & 0xffffffff;

    unsigned long long f = (unsigned long long)h0 + st->pad[0];
    h0 = (unsigned int)f;
    f = (unsigned long long)h1 + st->pad[1] + (f >> 32); h1 = (unsigned int)f;
    f = (unsigned long long)h2 + st->pad[2] + (f >> 32); h2 = (unsigned int)f;
    f = (unsigned long long)h3 + st->pad[3] + (f >> 32); h3 = (unsigned int)f;

    store32_le(mac + 0, h0);
    store32_le(mac + 4, h1);
    store32_le(mac + 8, h2);
    store32_le(mac + 12, h3);
}

// RFC 8439 tag: aad || pad16 || cipher || pad16 || len(aad) || len(cipher).
static void poly1305_aead_tag(const unsigned char poly_key[32],
                              const void* aad, unsigned int aad_len,
                              const unsigned char* cipher, unsigned int cipher_len,
                              unsigned char tag[16])
{
    static const unsigned char zeros[16] = { 0 };

    poly1305_ctx st;
    poly1305_init(&st, poly_key);

    if (aad_len) poly1305_update(&st, (const unsigned char*)aad, aad_len);
    if (aad_len % 16) poly1305_update(&st, zeros, 16 - (aad_len % 16));

    if (cipher_len) poly1305_update(&st, cipher, cipher_len);
    if (cipher_len % 16) poly1305_update(&st, zeros, 16 - (cipher_len % 16));

    unsigned char lengths[16];
    unsigned long long a = aad_len;
    unsigned long long c = cipher_len;
    for (int i = 0; i < 8; i++) lengths[i] = (unsigned char)(a >> (i * 8));
    for (int i = 0; i < 8; i++) lengths[8 + i] = (unsigned char)(c >> (i * 8));
    poly1305_update(&st, lengths, 16);

    poly1305_finish(&st, tag);
}

void xchacha20poly1305_encrypt(const unsigned char key[32], const unsigned char nonce[24],
                               const void* aad, unsigned int aad_len,
                               const void* plain, unsigned int plain_len,
                               unsigned char* cipher, unsigned char tag[16])
{
    unsigned char subkey[32];
    hchacha20(key, nonce, subkey);

    unsigned char subnonce[12];
    ccfset(subnonce, 0, 4);
    ccpy(subnonce + 4, nonce + 16, 8);

    unsigned char poly_key[64];
    chacha20_block(subkey, 0, subnonce, poly_key);

    chacha20_xor(subkey, 1, subnonce, (const unsigned char*)plain, cipher, plain_len);
    poly1305_aead_tag(poly_key, aad, aad_len, cipher, plain_len, tag);
}

bool xchacha20poly1305_decrypt(const unsigned char key[32], const unsigned char nonce[24],
                               const void* aad, unsigned int aad_len,
                               const void* cipher, unsigned int cipher_len,
                               const unsigned char tag[16],
                               unsigned char* plain)
{
    unsigned char subkey[32];
    hchacha20(key, nonce, subkey);

    unsigned char subnonce[12];
    ccfset(subnonce, 0, 4);
    ccpy(subnonce + 4, nonce + 16, 8);

    unsigned char poly_key[64];
    chacha20_block(subkey, 0, subnonce, poly_key);

    unsigned char expected[16];
    poly1305_aead_tag(poly_key, aad, aad_len, (const unsigned char*)cipher, cipher_len, expected);

    unsigned char diff = 0;
    for (int i = 0; i < 16; i++) diff |= (unsigned char)(expected[i] ^ tag[i]);
    if (diff) return false;

    chacha20_xor(subkey, 1, subnonce, (const unsigned char*)cipher, plain, cipher_len);
    return true;
}

// ---------------------------------------------------------------------------
// hmac / hkdf
// ---------------------------------------------------------------------------

void hmac_sha256(const void* key, unsigned int key_len,
                 const void* data, unsigned int data_len,
                 unsigned char out[32])
{
    unsigned char block[64];
    ccfset(block, 0, sizeof(block));

    if (key_len > 64)
    {
        sha256(key, key_len, block);
    }
    else if (key_len)
    {
        ccpy(block, key, key_len);
    }

    unsigned char ipad[64], opad[64];
    for (int i = 0; i < 64; i++)
    {
        ipad[i] = (unsigned char)(block[i] ^ 0x36);
        opad[i] = (unsigned char)(block[i] ^ 0x5C);
    }

    unsigned char inner[32];
    sha256_ctx ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, ipad, 64);
    if (data_len) sha256_update(&ctx, data, data_len);
    sha256_final(&ctx, inner);

    sha256_init(&ctx);
    sha256_update(&ctx, opad, 64);
    sha256_update(&ctx, inner, 32);
    sha256_final(&ctx, out);

    ccfset(block, 0, sizeof(block));
    ccfset(ipad, 0, sizeof(ipad));
    ccfset(opad, 0, sizeof(opad));
}

void hkdf_extract(const void* salt, unsigned int salt_len,
                  const void* ikm, unsigned int ikm_len,
                  unsigned char prk[32])
{
    static const unsigned char zero_salt[32] = { 0 };
    if (!salt || !salt_len)
    {
        salt = zero_salt;
        salt_len = 32;
    }
    hmac_sha256(salt, salt_len, ikm, ikm_len, prk);
}

bool hkdf_expand(const unsigned char prk[32],
                 const void* info, unsigned int info_len,
                 unsigned char* out, unsigned int out_len)
{
    return hkdf_expand_n(prk, 32, info, info_len, out, out_len);
}

bool hkdf_expand_n(const unsigned char* prk, unsigned int prk_len,
                   const void* info, unsigned int info_len,
                   unsigned char* out, unsigned int out_len)
{
    if (out_len > 255 * 32) return false;

    // T(n) = HMAC(PRK, T(n-1) || info || n). MLS contexts can be kilobytes, so
    // the working block moves to the heap once it outgrows the stack buffer.
    unsigned char stack_block[32 + 256 + 1];
    unsigned char* block = stack_block;
    unsigned int block_cap = (unsigned int)sizeof(stack_block);
    unsigned char* heap_block = 0;

    if (info_len + 33 > block_cap)
    {
        heap_block = (unsigned char*)memalloc((int)(info_len + 33));
        if (!heap_block) return false;
        block = heap_block;
        block_cap = info_len + 33;
    }

    unsigned char t[32];
    unsigned int t_len = 0;
    unsigned int produced = 0;
    unsigned char counter = 1;

    while (produced < out_len)
    {
        unsigned int n = 0;
        if (t_len) { ccpy(block, t, t_len); n += t_len; }
        if (info_len) { ccpy(block + n, info, info_len); n += info_len; }
        block[n++] = counter;

        hmac_sha256(prk, prk_len, block, n, t);
        t_len = 32;

        unsigned int take = out_len - produced;
        if (take > 32) take = 32;
        ccpy(out + produced, t, take);
        produced += take;
        counter++;
    }

    ccfset(t, 0, sizeof(t));
    if (heap_block) { ccfset(heap_block, 0, block_cap); memfree(heap_block); }
    return true;
}

// MLS variable-length integer, RFC 9420 section 2.1.2.
unsigned int varint_size(unsigned int value)
{
    if (value < 64) return 1;
    if (value < 16384) return 2;
    return 4;
}

unsigned int varint_write(unsigned char* out, unsigned int value)
{
    if (value < 64)
    {
        out[0] = (unsigned char)value;
        return 1;
    }
    if (value < 16384)
    {
        out[0] = (unsigned char)(0x40 | (value >> 8));
        out[1] = (unsigned char)(value & 0xFF);
        return 2;
    }
    out[0] = (unsigned char)(0x80 | (value >> 24));
    out[1] = (unsigned char)((value >> 16) & 0xFF);
    out[2] = (unsigned char)((value >> 8) & 0xFF);
    out[3] = (unsigned char)(value & 0xFF);
    return 4;
}

bool mls_expand_with_label(const unsigned char secret[32],
                           const char* label,
                           const void* context, unsigned int context_len,
                           unsigned char* out, unsigned int out_len)
{
    return mls_expand_with_label_n(secret, 32, label, context, context_len, out, out_len);
}

bool mls_expand_with_label_n(const unsigned char* secret, unsigned int secret_len,
                             const char* label,
                             const void* context, unsigned int context_len,
                             unsigned char* out, unsigned int out_len)
{
    // struct { uint16 length; opaque label<V>; opaque context<V>; } KDFLabel
    const char* prefix = "MLS 1.0 ";
    unsigned int prefix_len = (unsigned int)ccslenf(prefix);
    unsigned int label_len = (unsigned int)ccslenf(label);
    unsigned int full_label_len = prefix_len + label_len;

    unsigned int need = 2 + varint_size(full_label_len) + full_label_len +
                        varint_size(context_len) + context_len;

    unsigned char stack_info[256];
    unsigned char* info = stack_info;
    unsigned char* heap_info = 0;

    if (need > sizeof(stack_info))
    {
        heap_info = (unsigned char*)memalloc((int)need);
        if (!heap_info) return false;
        info = heap_info;
    }

    unsigned int n = 0;
    info[n++] = (unsigned char)(out_len >> 8);
    info[n++] = (unsigned char)(out_len & 0xFF);

    n += varint_write(info + n, full_label_len);
    ccpy(info + n, prefix, prefix_len); n += prefix_len;
    ccpy(info + n, label, label_len); n += label_len;

    n += varint_write(info + n, context_len);
    if (context_len) { ccpy(info + n, context, context_len); n += context_len; }

    bool ok = hkdf_expand_n(secret, secret_len, info, n, out, out_len);
    if (heap_info) memfree(heap_info);
    return ok;
}

bool mls_derive_secret(const unsigned char secret[32], const char* label,
                       unsigned char out[32])
{
    return mls_expand_with_label(secret, label, 0, 0, out, 32);
}

// ---------------------------------------------------------------------------
// aes-gcm with a caller-chosen key and tag size
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// aes-gcm with a caller-chosen key and tag size.
//
// No system crypto: the FIPS 197 block cipher (encryption half only, which is
// all GCM's CTR mode needs) plus the GHASH construction, written here by hand.
// ---------------------------------------------------------------------------

namespace
{
    static const unsigned char AES_SBOX[256] = {
        0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
        0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
        0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
        0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
        0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
        0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
        0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
        0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
        0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
        0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
        0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
        0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
        0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
        0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
        0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
        0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16 };

    static const unsigned char AES_RCON[10] =
        { 0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36 };

    struct aes_round_keys
    {
        unsigned char rk[240];   // 14 rounds + 1, the 256 bit worst case
        int rounds;
    };

    static inline unsigned char aes_xtime(unsigned char x)
    {
        return (unsigned char)((x << 1) ^ ((x & 0x80) ? 0x1B : 0x00));
    }

    bool aes_expand(const unsigned char* key, unsigned int key_len, aes_round_keys* out)
    {
        int nk;
        if (key_len == 16)      { nk = 4; out->rounds = 10; }
        else if (key_len == 24) { nk = 6; out->rounds = 12; }
        else if (key_len == 32) { nk = 8; out->rounds = 14; }
        else return false;

        ccpy(out->rk, key, key_len);
        unsigned int total = (unsigned int)(out->rounds + 1) * 16;
        for (unsigned int i = key_len; i < total; i += 4)
        {
            unsigned char t[4];
            t[0] = out->rk[i - 4]; t[1] = out->rk[i - 3];
            t[2] = out->rk[i - 2]; t[3] = out->rk[i - 1];

            unsigned int word = i / 4;
            if (word % (unsigned int)nk == 0)
            {
                unsigned char u = t[0];
                t[0] = (unsigned char)(AES_SBOX[t[1]] ^ AES_RCON[word / (unsigned int)nk - 1]);
                t[1] = AES_SBOX[t[2]];
                t[2] = AES_SBOX[t[3]];
                t[3] = AES_SBOX[u];
            }
            else if (nk > 6 && word % (unsigned int)nk == 4)
            {
                t[0] = AES_SBOX[t[0]]; t[1] = AES_SBOX[t[1]];
                t[2] = AES_SBOX[t[2]]; t[3] = AES_SBOX[t[3]];
            }

            out->rk[i + 0] = (unsigned char)(out->rk[i - key_len + 0] ^ t[0]);
            out->rk[i + 1] = (unsigned char)(out->rk[i - key_len + 1] ^ t[1]);
            out->rk[i + 2] = (unsigned char)(out->rk[i - key_len + 2] ^ t[2]);
            out->rk[i + 3] = (unsigned char)(out->rk[i - key_len + 3] ^ t[3]);
        }
        return true;
    }

    void aes_encrypt_block(const aes_round_keys* k, const unsigned char in[16], unsigned char out[16])
    {
        unsigned char s[16];
        for (int i = 0; i < 16; i++) s[i] = (unsigned char)(in[i] ^ k->rk[i]);

        for (int round = 1; round <= k->rounds; round++)
        {
            for (int i = 0; i < 16; i++) s[i] = AES_SBOX[s[i]];

            // ShiftRows through a copy: row r rotates left by r.
            unsigned char t[16];
            ccpy(t, s, 16);
            s[0] = t[0];  s[4] = t[4];  s[8]  = t[8];  s[12] = t[12];
            s[1] = t[5];  s[5] = t[9];  s[9]  = t[13]; s[13] = t[1];
            s[2] = t[10]; s[6] = t[14]; s[10] = t[2];  s[14] = t[6];
            s[3] = t[15]; s[7] = t[3];  s[11] = t[7];  s[15] = t[11];

            if (round != k->rounds)
            {
                for (int c = 0; c < 4; c++)
                {
                    unsigned char a0 = s[c * 4 + 0], a1 = s[c * 4 + 1];
                    unsigned char a2 = s[c * 4 + 2], a3 = s[c * 4 + 3];
                    s[c * 4 + 0] = (unsigned char)(aes_xtime(a0) ^ (aes_xtime(a1) ^ a1) ^ a2 ^ a3);
                    s[c * 4 + 1] = (unsigned char)(a0 ^ aes_xtime(a1) ^ (aes_xtime(a2) ^ a2) ^ a3);
                    s[c * 4 + 2] = (unsigned char)(a0 ^ a1 ^ aes_xtime(a2) ^ (aes_xtime(a3) ^ a3));
                    s[c * 4 + 3] = (unsigned char)((aes_xtime(a0) ^ a0) ^ a1 ^ a2 ^ aes_xtime(a3));
                }
            }

            const unsigned char* rk = k->rk + (unsigned int)round * 16;
            for (int i = 0; i < 16; i++) s[i] ^= rk[i];
        }

        ccpy(out, s, 16);
    }

    // GHASH multiply in GF(2^128): Z = X * H, blocks big endian, the reduction
    // constant is 0xE1 followed by zeros.
    void ghash_mul(const unsigned char x[16], const unsigned char h[16], unsigned char z[16])
    {
        unsigned char v[16];
        ccpy(v, h, 16);
        ccfset(z, 0, 16);

        for (int i = 0; i < 16; i++)
        {
            for (int bit = 7; bit >= 0; bit--)
            {
                if ((x[i] >> bit) & 1)
                    for (int j = 0; j < 16; j++) z[j] ^= v[j];

                unsigned char lsb = (unsigned char)(v[15] & 1);
                for (int j = 15; j > 0; j--) v[j] = (unsigned char)((v[j] >> 1) | (v[j - 1] << 7));
                v[0] >>= 1;
                if (lsb) v[0] ^= 0xE1;
            }
        }
    }

    // S = (S xor pad(data)) * H streamed over whole and partial blocks.
    void ghash_absorb(const unsigned char h[16], unsigned char s[16],
                      const void* data, unsigned int len)
    {
        const unsigned char* p = (const unsigned char*)data;
        while (len >= 16)
        {
            for (int i = 0; i < 16; i++) s[i] ^= p[i];
            unsigned char t[16];
            ghash_mul(s, h, t);
            ccpy(s, t, 16);
            p += 16;
            len -= 16;
        }
        if (len)
        {
            for (unsigned int i = 0; i < len; i++) s[i] ^= p[i];
            unsigned char t[16];
            ghash_mul(s, h, t);
            ccpy(s, t, 16);
        }
    }

    void ghash_lengths(const unsigned char h[16], unsigned char s[16],
                       unsigned long long aad_bits, unsigned long long text_bits)
    {
        unsigned char blk[16];
        for (int i = 0; i < 8; i++) blk[i] = (unsigned char)(aad_bits >> (56 - i * 8));
        for (int i = 0; i < 8; i++) blk[8 + i] = (unsigned char)(text_bits >> (56 - i * 8));
        for (int i = 0; i < 16; i++) s[i] ^= blk[i];
        unsigned char t[16];
        ghash_mul(s, h, t);
        ccpy(s, t, 16);
    }

    void gcm_next_counter(unsigned char ctr[16])
    {
        for (int i = 15; i >= 12; i--)
        {
            ctr[i]++;
            if (ctr[i]) break;
        }
    }

    bool aesgcm_run(bool encrypt,
                    const unsigned char* key, unsigned int key_len,
                    const unsigned char* nonce, unsigned int nonce_len,
                    const void* aad, unsigned int aad_len,
                    const void* input, unsigned int input_len,
                    unsigned char* output,
                    unsigned char* tag, unsigned int tag_len)
    {
        if (tag_len > 16) return false;

        aes_round_keys k;
        if (!aes_expand(key, key_len, &k)) return false;

        unsigned char zero[16];
        ccfset(zero, 0, sizeof(zero));
        unsigned char H[16];
        aes_encrypt_block(&k, zero, H);

        unsigned char j0[16];
        if (nonce_len == 12)
        {
            ccpy(j0, nonce, 12);
            j0[12] = 0; j0[13] = 0; j0[14] = 0; j0[15] = 1;
        }
        else
        {
            unsigned char s[16];
            ccfset(s, 0, sizeof(s));
            ghash_absorb(H, s, nonce, nonce_len);
            ghash_lengths(H, s, 0, (unsigned long long)nonce_len * 8);
            ccpy(j0, s, 16);
        }

        const unsigned char* in = (const unsigned char*)input;

        if (!encrypt)
        {
            // Verify first: the tag covers the aad and the ciphertext as given.
            unsigned char s[16];
            ccfset(s, 0, sizeof(s));
            if (aad_len) ghash_absorb(H, s, aad, aad_len);
            if (input_len) ghash_absorb(H, s, in, input_len);
            ghash_lengths(H, s, (unsigned long long)aad_len * 8,
                          (unsigned long long)input_len * 8);

            unsigned char mask[16];
            aes_encrypt_block(&k, j0, mask);

            unsigned char diff = 0;
            for (unsigned int i = 0; i < tag_len; i++)
                diff |= (unsigned char)(s[i] ^ mask[i] ^ tag[i]);
            if (diff) return false;
        }

        unsigned char ctr[16];
        ccpy(ctr, j0, 16);
        unsigned int at = 0;
        while (at < input_len)
        {
            gcm_next_counter(ctr);
            unsigned char pad[16];
            aes_encrypt_block(&k, ctr, pad);

            unsigned int n = input_len - at;
            if (n > 16) n = 16;
            for (unsigned int i = 0; i < n; i++)
                output[at + i] = (unsigned char)(in[at + i] ^ pad[i]);
            at += n;
        }

        if (encrypt)
        {
            unsigned char s[16];
            ccfset(s, 0, sizeof(s));
            if (aad_len) ghash_absorb(H, s, aad, aad_len);
            if (input_len) ghash_absorb(H, s, output, input_len);
            ghash_lengths(H, s, (unsigned long long)aad_len * 8,
                          (unsigned long long)input_len * 8);

            unsigned char mask[16];
            aes_encrypt_block(&k, j0, mask);
            for (unsigned int i = 0; i < tag_len; i++)
                tag[i] = (unsigned char)(s[i] ^ mask[i]);
        }
        return true;
    }
}

bool aesgcm_encrypt(const unsigned char* key, unsigned int key_len,
                    const unsigned char* nonce, unsigned int nonce_len,
                    const void* aad, unsigned int aad_len,
                    const void* plain, unsigned int plain_len,
                    unsigned char* cipher,
                    unsigned char* tag, unsigned int tag_len)
{
    if (tag_len > 16) return false;
    return aesgcm_run(true, key, key_len, nonce, nonce_len, aad, aad_len,
                      plain, plain_len, cipher, tag, tag_len);
}

bool aesgcm_decrypt(const unsigned char* key, unsigned int key_len,
                    const unsigned char* nonce, unsigned int nonce_len,
                    const void* aad, unsigned int aad_len,
                    const void* cipher, unsigned int cipher_len,
                    const unsigned char* tag, unsigned int tag_len,
                    unsigned char* plain)
{
    if (tag_len > 16) return false;
    // A truncated tag verifies directly: the full tag is recomputed and only
    // the requested prefix is compared.
    return aesgcm_run(false, key, key_len, nonce, nonce_len, aad, aad_len,
                      cipher, cipher_len, plain, (unsigned char*)tag, tag_len);
}

// ---------------------------------------------------------------------------
// NIST P-256: key layout helpers and ECDH (the arithmetic, including ECDSA,
// lives in p256.cpp)
// ---------------------------------------------------------------------------

void p256_public_from_private(const unsigned char private_key[96], unsigned char public_key[65])
{
    public_key[0] = 0x04;
    ccpy(public_key + 1, private_key, 64);
}

bool p256_ecdh(const unsigned char private_key[96],
               const unsigned char peer_public[65],
               unsigned char shared_x[32])
{
    if (peer_public[0] != 0x04) return false;

    // The stored key is the public point first, then the scalar.
    const unsigned char* scalar = private_key + 64;

    unsigned char x[32], y[32];
    if (!crypto::p256_scalar_point_mult(scalar, peer_public + 1, peer_public + 33, x, y))
        return false;

    // RFC 9180 takes the x coordinate alone as the shared secret.
    ccpy(shared_x, x, 32);
    ccfset(x, 0, sizeof(x));
    ccfset(y, 0, sizeof(y));
    return true;
}

namespace
{
    // One DER INTEGER holding a 32 byte big-endian value: leading zeros are
    // stripped, and a 0x00 is prepended when the top bit would read as negative.
    unsigned int der_encode_int(const unsigned char value[32], unsigned char* out)
    {
        unsigned int skip = 0;
        while (skip < 31 && value[skip] == 0) skip++;

        unsigned int len = 32 - skip;
        bool pad = (value[skip] & 0x80) != 0;

        out[0] = 0x02;
        out[1] = (unsigned char)(len + (pad ? 1 : 0));

        unsigned int n = 2;
        if (pad) out[n++] = 0x00;
        ccpy(out + n, value + skip, len);
        return n + len;
    }
}

unsigned int der_encode_signature(const unsigned char raw[64], unsigned char* out)
{
    unsigned char body[80];
    unsigned int n = der_encode_int(raw, body);
    n += der_encode_int(raw + 32, body + n);

    out[0] = 0x30;
    out[1] = (unsigned char)n;
    ccpy(out + 2, body, n);
    return n + 2;
}

bool der_decode_signature(const unsigned char* der, unsigned int der_len, unsigned char raw[64])
{
    if (der_len < 8 || der[0] != 0x30) return false;
    if ((unsigned int)der[1] + 2 != der_len) return false;

    unsigned int pos = 2;
    for (int k = 0; k < 2; k++)
    {
        if (pos + 2 > der_len || der[pos] != 0x02) return false;

        unsigned int len = der[pos + 1];
        pos += 2;
        if (len == 0 || pos + len > der_len) return false;

        const unsigned char* value = der + pos;
        pos += len;

        if (len > 32)
        {
            if (len != 33 || value[0] != 0x00) return false;
            value++;
            len--;
        }

        unsigned char* dst = raw + k * 32;
        ccfset(dst, 0, 32);
        ccpy(dst + (32 - len), value, len);
    }

    return pos == der_len;
}

// ---------------------------------------------------------------------------
// aes-256-gcm: the same construction, fixed to the voice mode's parameters
// ---------------------------------------------------------------------------

bool aes256gcm_available()
{
    // Hand written above, so always.
    return true;
}

bool aes256gcm_encrypt(const unsigned char key[32], const unsigned char nonce[12],
                       const void* aad, unsigned int aad_len,
                       const void* plain, unsigned int plain_len,
                       unsigned char* cipher, unsigned char tag[16])
{
    return aesgcm_run(true, key, 32, nonce, 12, aad, aad_len,
                      plain, plain_len, cipher, tag, 16);
}

bool aes256gcm_decrypt(const unsigned char key[32], const unsigned char nonce[12],
                       const void* aad, unsigned int aad_len,
                       const void* cipher, unsigned int cipher_len,
                       const unsigned char tag[16],
                       unsigned char* plain)
{
    return aesgcm_run(false, key, 32, nonce, 12, aad, aad_len,
                      cipher, cipher_len, plain, (unsigned char*)tag, 16);
}

// ---------------------------------------------------------------------------
// known-answer tests
// ---------------------------------------------------------------------------

namespace
{
    bool hex_equals(const unsigned char* data, unsigned int len, const char* expected_hex)
    {
        for (unsigned int i = 0; i < len; i++)
        {
            char hi = expected_hex[i * 2];
            char lo = expected_hex[i * 2 + 1];
            if (!hi || !lo) return false;

            int v = 0;
            for (int k = 0; k < 2; k++)
            {
                char c = k ? lo : hi;
                int d;
                if (c >= '0' && c <= '9') d = c - '0';
                else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
                else return false;
                v = (v << 4) | d;
            }
            if (data[i] != (unsigned char)v) return false;
        }
        return true;
    }

    bool check(const char* name, bool ok, int* failures)
    {
        if (!ok)
        {
            (*failures)++;
            log_line("selftest: FAIL %s", name);
        }
        return ok;
    }

    // Test vectors are written as hex; this parses them into a fixed buffer.
    bool hex_parse(const char* hex, unsigned char* out, unsigned int out_len)
    {
        for (unsigned int i = 0; i < out_len; i++)
        {
            int v = 0;
            for (int k = 0; k < 2; k++)
            {
                char c = hex[i * 2 + k];
                int d;
                if (c >= '0' && c <= '9') d = c - '0';
                else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
                else return false;
                v = (v << 4) | d;
            }
            out[i] = (unsigned char)v;
        }
        return hex[out_len * 2] == 0;
    }
}

bool self_test()
{
    int failures = 0;

    // SHA-256 of "abc", FIPS 180-4.
    {
        unsigned char out[32];
        sha256("abc", 3, out);
        check("sha256(abc)", hex_equals(out, 32,
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"), &failures);
    }

    // SHA-1 of "abc", FIPS 180-4. The websocket handshake is its only user.
    {
        unsigned char out[20];
        sha1("abc", 3, out);
        check("sha1(abc)", hex_equals(out, 20,
            "a9993e364706816aba3e25717850c26c9cd0d89d"), &failures);
    }

    // HMAC-SHA256, RFC 4231 test case 1.
    {
        unsigned char key[20];
        ccfset(key, 0x0b, sizeof(key));
        unsigned char out[32];
        hmac_sha256(key, sizeof(key), "Hi There", 8, out);
        check("hmac-sha256 rfc4231-1", hex_equals(out, 32,
            "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"), &failures);
    }

    // HKDF-SHA256, RFC 5869 test case 1.
    {
        unsigned char ikm[22];
        ccfset(ikm, 0x0b, sizeof(ikm));
        unsigned char salt[13];
        for (int i = 0; i < 13; i++) salt[i] = (unsigned char)i;
        unsigned char info[10];
        for (int i = 0; i < 10; i++) info[i] = (unsigned char)(0xf0 + i);

        unsigned char prk[32];
        hkdf_extract(salt, sizeof(salt), ikm, sizeof(ikm), prk);
        check("hkdf-extract rfc5869-1", hex_equals(prk, 32,
            "077709362c2e32df0ddc3f0dc47bba6390b6c73bb50f9c3122ec844ad7c2b3e5"), &failures);

        unsigned char okm[42];
        hkdf_expand(prk, info, sizeof(info), okm, sizeof(okm));
        check("hkdf-expand rfc5869-1", hex_equals(okm, 42,
            "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf"
            "34007208d5b887185865"), &failures);
    }

    // AES-128-GCM, McGrew & Viega test case 2: all-zero key, nonce and input.
    {
        unsigned char key[16], nonce[12], plain[16], cipher[16], tag[16];
        ccfset(key, 0, sizeof(key));
        ccfset(nonce, 0, sizeof(nonce));
        ccfset(plain, 0, sizeof(plain));

        bool ok = aesgcm_encrypt(key, 16, nonce, 12, 0, 0, plain, 16, cipher, tag, 16);
        check("aes128gcm encrypt", ok &&
            hex_equals(cipher, 16, "0388dace60b6a392f328c2b971b2fe78") &&
            hex_equals(tag, 16, "ab6e47d42cec13bdf53a67b21257bddf"), &failures);

        // Round trip with the 8 byte truncated tag DAVE uses.
        unsigned char short_tag[8];
        unsigned char roundtrip[16];
        ok = aesgcm_encrypt(key, 16, nonce, 12, "aad", 3, plain, 16, cipher, short_tag, 8);
        ok = ok && aesgcm_decrypt(key, 16, nonce, 12, "aad", 3, cipher, 16, short_tag, 8, roundtrip);
        bool same = ok;
        for (int i = 0; i < 16 && same; i++) same = roundtrip[i] == plain[i];
        check("aes128gcm truncated tag round trip", same, &failures);

        // A flipped tag bit must be rejected.
        short_tag[0] ^= 0x01;
        check("aes128gcm rejects a bad tag",
              !aesgcm_decrypt(key, 16, nonce, 12, "aad", 3, cipher, 16, short_tag, 8, roundtrip),
              &failures);
    }

    // AES-GCM, NIST GCMVS vectors: K is FEFFE992.../67308308, the IV is
    // CAFEBABE/FACEDBAD/DECAF888. These pin the block cipher, the counter
    // mode and GHASH together, including the empty-input GMAC corner.
    {
        unsigned char key128[16], key256[32], iv[12];
        unsigned char pt[64], ct[64], tag[16], aad[64], back[64], dummy = 0;
        hex_parse("FEFFE9928665731C6D6A8F9467308308", key128, 16);
        hex_parse("FEFFE9928665731C6D6A8F9467308308FEFFE9928665731C6D6A8F9467308308",
                  key256, 32);
        hex_parse("CAFEBABE" "FACEDBAD" "DECAF888", iv, 12);
        hex_parse("D9313225F88406E5A55909C5AFF5269A86A7A9531534F7DA2E4C303D8A318A72"
                  "1C3C0C95956809532FCF0E2449A6B525B16AEDF5AA0DE657BA637B391AAFD255",
                  pt, 64);
        hex_parse("3AD77BB40D7A3660A89ECAF32466EF97F5D3D58503B9699DE785895A96FDBAAF"
                  "43B1CD7F598ECE23881B00E3ED0306887B0C785E27E8AD3F8223207104725DD4",
                  aad, 64);

        // AES-128, empty plaintext and aad.
        bool ok = aesgcm_encrypt(key128, 16, iv, 12, 0, 0, &dummy, 0, &dummy, tag, 16);
        check("aes128gcm nist empty", ok &&
            hex_equals(tag, 16, "3247184B3C4F69A44DBCD22887BBB418"), &failures);

        // AES-128, 64 bytes of plaintext, no aad.
        ok = aesgcm_encrypt(key128, 16, iv, 12, 0, 0, pt, 64, ct, tag, 16);
        check("aes128gcm nist pt64", ok &&
            hex_equals(ct, 64, "42831EC2217774244B7221B784D0D49CE3AA212F2C02A4E035C17E2329ACA12E"
                               "21D514B25466931C7D8F6A5AAC84AA051BA30B396A0AAC973D58E091473F5985") &&
            hex_equals(tag, 16, "4D5C2AF327CD64A62CF35ABD2BA6FAB4"), &failures);

        // AES-128, 64 bytes of aad plus the same plaintext.
        ok = aesgcm_encrypt(key128, 16, iv, 12, aad, 64, pt, 64, ct, tag, 16);
        check("aes128gcm nist aad64+pt64", ok &&
            hex_equals(tag, 16, "64C0232904AF398A5B67C10B53A5024D"), &failures);

        // The 8 byte truncation DAVE uses is the tag's prefix, byte for byte.
        unsigned char short_tag[8];
        ok = aesgcm_encrypt(key128, 16, iv, 12, 0, 0, pt, 64, ct, short_tag, 8);
        check("aes128gcm nist truncated prefix", ok &&
            hex_equals(short_tag, 8, "4D5C2AF327CD64A6"), &failures);
        ok = ok && aesgcm_decrypt(key128, 16, iv, 12, 0, 0, ct, 64, short_tag, 8, back);
        bool same = ok;
        for (int i = 0; i < 64 && same; i++) same = back[i] == pt[i];
        check("aes128gcm nist truncated round trip", same, &failures);

        // AES-256, empty plaintext and aad.
        ok = aesgcm_encrypt(key256, 32, iv, 12, 0, 0, &dummy, 0, &dummy, tag, 16);
        check("aes256gcm nist empty", ok &&
            hex_equals(tag, 16, "FD2CAA16A5832E76AA132C1453EEDA7E"), &failures);

        // AES-256, 64 bytes of plaintext, no aad, then back.
        ok = aesgcm_encrypt(key256, 32, iv, 12, 0, 0, pt, 64, ct, tag, 16);
        check("aes256gcm nist pt64", ok &&
            hex_equals(ct, 64, "522DC1F099567D07F47F37A32A84427D643A8CDCBFE5C0C97598A2BD2555D1AA"
                               "8CB08E48590DBB3DA7B08B1056828838C5F61E6393BA7A0ABCC9F662898015AD") &&
            hex_equals(tag, 16, "B094DAC5D93471BDEC1A502270E3CC6C"), &failures);
        ok = ok && aesgcm_decrypt(key256, 32, iv, 12, 0, 0, ct, 64, tag, 16, back);
        same = ok;
        for (int i = 0; i < 64 && same; i++) same = back[i] == pt[i];
        check("aes256gcm nist round trip", same, &failures);

        // AES-256 through its own entry points, with aad.
        unsigned char tag256[16];
        ok = aes256gcm_encrypt(key256, iv, aad, 64, pt, 64, ct, tag256);
        ok = ok && aes256gcm_decrypt(key256, iv, aad, 64, ct, 64, tag256, back);
        same = ok;
        for (int i = 0; i < 64 && same; i++) same = back[i] == pt[i];
        check("aes256gcm entry round trip", same, &failures);
        tag256[15] ^= 0x01;
        check("aes256gcm rejects a bad tag",
              !aes256gcm_decrypt(key256, iv, aad, 64, ct, 64, tag256, back), &failures);
    }

    // Scalar multiplication cross-checked against key generation. Every key
    // the generator hands out comes with both the scalar and the point it
    // belongs to, which makes an endless supply of test vectors for the
    // base-point multiplication written here by hand.
    {
        bool all = true;
        for (int round = 0; round < 8 && all; round++)
        {
            unsigned char pub[65], priv[96];
            if (!p256_generate(pub, priv)) { all = false; break; }

            unsigned char x[32], y[32];
            if (!p256_scalar_base_mult(priv + 64, x, y)) { all = false; break; }

            for (int i = 0; i < 32 && all; i++) all = x[i] == priv[i];
            for (int i = 0; i < 32 && all; i++) all = y[i] == priv[32 + i];
        }
        check("p256 base point multiplication", all, &failures);

        // The scalar has to be rejected outside [1, n).
        unsigned char zero[32];
        ccfset(zero, 0, sizeof(zero));
        check("p256 rejects a zero scalar", !p256_scalar_in_range(zero), &failures);

        unsigned char too_big[32];
        ccfset(too_big, 0xFF, sizeof(too_big));
        check("p256 rejects a scalar above the order", !p256_scalar_in_range(too_big), &failures);

        // A key pair built from a scalar must agree with a real one: the point
        // it names has to be usable for an actual exchange.
        unsigned char pub_c[65], priv_c[96], pub_d[65], priv_d[96];
        bool ok = p256_generate(pub_d, priv_d);
        ok = ok && p256_keypair_from_scalar(priv_d + 64, pub_c, priv_c);

        bool same_pub = ok;
        for (int i = 0; i < 65 && same_pub; i++) same_pub = pub_c[i] == pub_d[i];
        check("p256 keypair from scalar", same_pub, &failures);

        unsigned char shared_c[32], shared_e[32];
        unsigned char pub_e[65], priv_e[96];
        ok = ok && p256_generate(pub_e, priv_e);
        ok = ok && p256_ecdh(priv_c, pub_e, shared_c);
        ok = ok && p256_ecdh(priv_e, pub_c, shared_e);

        bool agree = ok;
        for (int i = 0; i < 32 && agree; i++) agree = shared_c[i] == shared_e[i];
        check("p256 rebuilt keypair agrees over ecdh", agree, &failures);
    }

    // P-256: both sides of an ECDH agree, and a signature verifies.
    {
        unsigned char pub_a[65], priv_a[96], pub_b[65], priv_b[96];
        bool ok = p256_generate(pub_a, priv_a) && p256_generate(pub_b, priv_b);

        unsigned char shared_ab[32], shared_ba[32];
        ok = ok && p256_ecdh(priv_a, pub_b, shared_ab);
        ok = ok && p256_ecdh(priv_b, pub_a, shared_ba);

        bool same = ok;
        for (int i = 0; i < 32 && same; i++) same = shared_ab[i] == shared_ba[i];
        check("p256 ecdh agreement", same, &failures);

        unsigned char sig[P256_SIGNATURE_MAX_BYTES];
        unsigned int sig_len = 0;
        ok = p256_sign(priv_a, "message", 7, sig, &sig_len);

        // MLS expects DER, so the encoding itself is part of the contract.
        check("p256 signature is DER",
              ok && sig_len >= 8 && sig_len <= 72 && sig[0] == 0x30 && sig[1] + 2 == (int)sig_len,
              &failures);
        check("p256 sign/verify", ok && p256_verify(pub_a, "message", 7, sig, sig_len), &failures);

        // A DER round trip must preserve both integers exactly.
        unsigned char raw[64], again[72];
        bool der_ok = der_decode_signature(sig, sig_len, raw);
        unsigned int again_len = der_encode_signature(raw, again);
        bool same_der = der_ok && again_len == sig_len;
        for (unsigned int i = 0; i < sig_len && same_der; i++) same_der = again[i] == sig[i];
        check("p256 DER round trip", same_der, &failures);

        sig[sig_len - 1] ^= 0x01;
        check("p256 rejects a bad signature",
              !p256_verify(pub_a, "message", 7, sig, sig_len), &failures);
    }

    // MLS key derivation must be reproducible and label sensitive.
    {
        unsigned char secret[32];
        ccfset(secret, 0x42, sizeof(secret));

        unsigned char a[32], b[32], c[32];
        mls_derive_secret(secret, "epoch", a);
        mls_derive_secret(secret, "epoch", b);
        mls_derive_secret(secret, "sender data", c);

        bool stable = true;
        for (int i = 0; i < 32 && stable; i++) stable = a[i] == b[i];

        bool different = false;
        for (int i = 0; i < 32 && !different; i++) different = a[i] != c[i];

        check("mls derive-secret", stable && different, &failures);
    }

    if (failures == 0) log_line("selftest: all crypto tests passed");
    else log_line("selftest: %d failure(s)", failures);

    return failures == 0;
}

} // namespace crypto
