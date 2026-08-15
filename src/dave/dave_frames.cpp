#include "pch.h"
#include "dave_frames.h"
#include "core/crypto.h"
#include "core/log.h"

namespace dave
{

namespace
{
    const char* MEDIA_KEY_LABEL = "Discord Secure Frames v0";
    const unsigned int MAX_RATCHETS = 32;
    const unsigned int KEY_CACHE = 8;

    // One sender's hash ratchet. mlspp seeds it with the 16 byte exported
    // secret and grows the chaining secret to 32 bytes on the first step.
    struct ratchet
    {
        // A user id alone does not identify a ratchet. This client holds two
        // groups at once - the microphone's and the screen share's - and its own
        // id is the same in both, while their exporter secrets are not. Keyed on
        // the id alone, whichever group asked first would hand its media key to
        // the other, and the far side would never decrypt a thing.
        const mls::group_state* group;
        unsigned long long user_id;
        unsigned char secret[32];
        unsigned int secret_len;
        unsigned int generation;      // generation that `secret` produces

        unsigned char cached_keys[KEY_CACHE][KEY_BYTES];
        unsigned int cached_generation[KEY_CACHE];
        bool cached_valid[KEY_CACHE];

        bool in_use;
    };

    ratchet g_ratchets[MAX_RATCHETS];

    // The table is reached from three threads now: the microphone, an outgoing
    // share and an incoming one. Two of them claiming the same free slot, or
    // one walking a ratchet forward while the other reads it, corrupts a key
    // that nothing downstream can tell apart from a bad frame.
    CRITICAL_SECTION g_ratchet_lock;
    volatile long g_ratchet_lock_state = 0;   // 0 none, 1 building, 2 ready

    void lock_ratchets()
    {
        if (InterlockedCompareExchange(&g_ratchet_lock_state, 1, 0) == 0)
        {
            InitializeCriticalSection(&g_ratchet_lock);
            InterlockedExchange(&g_ratchet_lock_state, 2);
        }
        while (g_ratchet_lock_state != 2) Sleep(0);
        EnterCriticalSection(&g_ratchet_lock);
    }

    void unlock_ratchets() { LeaveCriticalSection(&g_ratchet_lock); }

    void generation_context(unsigned int generation, unsigned char out[4])
    {
        out[0] = (unsigned char)(generation >> 24);
        out[1] = (unsigned char)(generation >> 16);
        out[2] = (unsigned char)(generation >> 8);
        out[3] = (unsigned char)(generation);
    }

    ratchet* find_ratchet(const mls::group_state* g, unsigned long long user_id)
    {
        for (unsigned int i = 0; i < MAX_RATCHETS; i++)
            if (g_ratchets[i].in_use && g_ratchets[i].user_id == user_id &&
                g_ratchets[i].group == g)
                return &g_ratchets[i];

        unsigned int slot = MAX_RATCHETS;
        for (unsigned int i = 0; i < MAX_RATCHETS; i++)
            if (!g_ratchets[i].in_use) { slot = i; break; }
        if (slot == MAX_RATCHETS) slot = 0;   // recycle the oldest entry

        ratchet* r = &g_ratchets[slot];
        ccfset(r, 0, sizeof(*r));

        // The exporter context is the user id as eight little-endian bytes.
        unsigned char context[8];
        for (int i = 0; i < 8; i++) context[i] = (unsigned char)(user_id >> (i * 8));

        if (!mls::export_secret(g, MEDIA_KEY_LABEL, context, sizeof(context),
                                r->secret, KEY_BYTES))
            return 0;

        r->group = g;
        r->user_id = user_id;
        r->secret_len = KEY_BYTES;
        r->generation = 0;
        r->in_use = true;
        return r;
    }

    bool cached_key(ratchet* r, unsigned int generation, unsigned char key[KEY_BYTES])
    {
        for (unsigned int i = 0; i < KEY_CACHE; i++)
        {
            if (r->cached_valid[i] && r->cached_generation[i] == generation)
            {
                ccpy(key, r->cached_keys[i], KEY_BYTES);
                return true;
            }
        }
        return false;
    }

    void cache_key(ratchet* r, unsigned int generation, const unsigned char key[KEY_BYTES])
    {
        unsigned int slot = generation % KEY_CACHE;
        r->cached_generation[slot] = generation;
        ccpy(r->cached_keys[slot], key, KEY_BYTES);
        r->cached_valid[slot] = true;
    }

    // Walks the ratchet forward to the requested generation. Going backwards is
    // impossible once the secret has moved on, which is why recent keys are
    // cached.
    bool derive_key(ratchet* r, unsigned int generation, unsigned char key[KEY_BYTES])
    {
        if (cached_key(r, generation, key)) return true;
        if (generation < r->generation) return false;

        // A far-future generation would mean an unbounded loop; discord caps
        // the gap it will ever ask for.
        if (generation - r->generation > 250) return false;

        while (r->generation <= generation)
        {
            unsigned char context[4];
            generation_context(r->generation, context);

            unsigned char step_key[KEY_BYTES];
            if (!crypto::mls_expand_with_label_n(r->secret, r->secret_len, "key",
                                                 context, sizeof(context),
                                                 step_key, KEY_BYTES))
                return false;

            cache_key(r, r->generation, step_key);
            if (r->generation == generation) ccpy(key, step_key, KEY_BYTES);

            unsigned char next_secret[32];
            if (!crypto::mls_expand_with_label_n(r->secret, r->secret_len, "secret",
                                                 context, sizeof(context),
                                                 next_secret, 32))
                return false;

            ccpy(r->secret, next_secret, 32);
            r->secret_len = 32;
            r->generation++;
        }

        return true;
    }

    // Finds a sender's ratchet and walks it to the generation asked for, both
    // under the table lock so the pair cannot be split by another thread.
    bool ratchet_key(const mls::group_state* g, unsigned long long user_id,
                     unsigned int generation, unsigned char key[KEY_BYTES])
    {
        lock_ratchets();
        ratchet* r = find_ratchet(g, user_id);
        bool ok = r && derive_key(r, generation, key);
        unlock_ratchets();
        return ok;
    }

    void build_nonce(unsigned int sync_nonce, unsigned char out[NONCE_BYTES])
    {
        ccfset(out, 0, NONCE_BYTES);
        // The counter is copied in host order, matching libdave's memcpy.
        for (unsigned int i = 0; i < SYNC_NONCE_BYTES; i++)
            out[SYNC_NONCE_OFFSET + i] = (unsigned char)(sync_nonce >> (i * 8));
    }

    unsigned int leb128_size(unsigned int value)
    {
        unsigned int n = 1;
        while (value >= 0x80) { value >>= 7; n++; }
        return n;
    }

    unsigned int leb128_write(unsigned char* out, unsigned int value)
    {
        unsigned int n = 0;
        do
        {
            unsigned char byte = (unsigned char)(value & 0x7F);
            value >>= 7;
            if (value) byte |= 0x80;
            out[n++] = byte;
        } while (value);
        return n;
    }

    bool leb128_read(const unsigned char* in, unsigned int len, unsigned int* value, unsigned int* used)
    {
        unsigned int result = 0;
        unsigned int shift = 0;
        unsigned int n = 0;

        while (n < len && n < 5)
        {
            unsigned char byte = in[n++];
            result |= (unsigned int)(byte & 0x7F) << shift;
            if ((byte & 0x80) == 0)
            {
                *value = result;
                *used = n;
                return true;
            }
            shift += 7;
        }
        return false;
    }
}

void reset_ratchets()
{
    // Called from a websocket thread while media threads are reading the table.
    lock_ratchets();
    ccfset(g_ratchets, 0, sizeof(g_ratchets));
    unlock_ratchets();
}

bool is_protected(const unsigned char* data, unsigned int len)
{
    if (len < 4) return false;
    return data[len - 2] == 0xFA && data[len - 1] == 0xFA;
}

unsigned int protected_len(const unsigned char* data, unsigned int len)
{
    if (len < 4) return 0;

    // Unpadded form: the marker is the last word.
    if (is_protected(data, len)) return len;

    // Padded form: the last byte is the padding length, every byte of the
    // block repeats it, and the marker sits right before the block.
    unsigned int pad = data[len - 1];
    if (pad < 1 || pad > len - 4) return 0;

    unsigned int end = len - pad;      // one past the marker
    if (data[end - 2] != 0xFA || data[end - 1] != 0xFA) return 0;
    for (unsigned int i = end; i < len; i++)
        if (data[i] != pad) return 0;  // chance bytes are not a padding block

    return end;
}

bool encrypt_frame(const mls::group_state* g, unsigned long long self_user_id,
                   unsigned int* nonce_counter,
                   const unsigned char* in, unsigned int in_len,
                   unsigned char* out, unsigned int* out_len)
{
    if (!g || !g->established) return false;


    unsigned int sync_nonce = *nonce_counter;
    unsigned int generation = sync_nonce >> GENERATION_SHIFT;

    unsigned char key[KEY_BYTES];
    if (!ratchet_key(g, self_user_id, generation, key)) return false;

    unsigned char nonce[NONCE_BYTES];
    build_nonce(sync_nonce, nonce);

    unsigned char tag[16];
    if (!crypto::aesgcm_encrypt(key, KEY_BYTES, nonce, NONCE_BYTES, 0, 0,
                                in, in_len, out, tag, 16))
        return false;

    unsigned int pos = in_len;
    ccpy(out + pos, tag, TRUNCATED_TAG_BYTES);
    pos += TRUNCATED_TAG_BYTES;

    unsigned int nonce_size = leb128_write(out + pos, sync_nonce);
    pos += nonce_size;

    // Opus has no unencrypted ranges, so that section is empty.
    unsigned int supplemental = TRUNCATED_TAG_BYTES + nonce_size + 1 + 2;
    out[pos++] = (unsigned char)supplemental;

    out[pos++] = 0xFA;
    out[pos++] = 0xFA;

    *out_len = pos;
    (*nonce_counter)++;
    return true;
}

// ---------------------------------------------------------------------------
// H.264
// ---------------------------------------------------------------------------

namespace
{
    // A slice header opens with three exponential golomb values, the last of
    // which is the picture parameter set id the depacketiser needs to read.
    // This returns how many bytes of the slice payload have to stay in the
    // clear to cover all three.
    unsigned int bytes_covering_pps(const unsigned char* p, unsigned int len)
    {
        unsigned int bit = 0;
        int zeros = 0;
        int parsed = 0;

        while (bit < len * 8 && parsed < 3)
        {
            unsigned int byte_index = bit / 8;
            unsigned int bit_index = bit % 8;

            // Emulation prevention bytes are not part of the bit stream and are
            // stepped over whole.
            if (bit_index == 0 && byte_index >= 2 && p[byte_index] == 0x03 &&
                p[byte_index - 1] == 0 && p[byte_index - 2] == 0)
            {
                bit += 8;
                continue;
            }

            if ((p[byte_index] & (1 << (7 - bit_index))) == 0)
            {
                zeros++;
                bit++;
                if (zeros >= 32) return 0;      // not a sane golomb value
            }
            else
            {
                parsed++;
                bit += 1 + (unsigned int)zeros;
                zeros = 0;
            }
        }

        return (bit / 8) + 1;
    }

    // Finds the next start code at or after from. Returns the offset of the NAL
    // body, with the start code length written to code_len, or -1.
    int next_nal_offset(const unsigned char* d, unsigned int len, unsigned int from,
                        unsigned int* code_len)
    {
        for (unsigned int i = from; i + 3 <= len; i++)
        {
            if (d[i] != 0 || d[i + 1] != 0) continue;
            if (d[i + 2] == 1) { *code_len = 3; return (int)(i + 3); }
            if (d[i + 2] == 0 && i + 4 <= len && d[i + 3] == 1) { *code_len = 4; return (int)(i + 4); }
        }
        return -1;
    }

    // Scratch for one frame, split by direction. A client that shares its own
    // screen while watching somebody else's runs both of these at once on two
    // pump threads, and one set of buffers between them means each frame is
    // built on top of the other one's half-written contents.
    struct scratch
    {
        unsigned char* aad;
        unsigned char* plain;
        unsigned int cap;
    };

    scratch g_enc = { 0, 0, 0 };
    scratch g_dec = { 0, 0, 0 };

    bool ensure_scratch(scratch* s, unsigned int need)
    {
        if (need <= s->cap) return true;

        unsigned int cap = s->cap ? s->cap : 65536;
        while (cap < need) cap *= 2;

        unsigned char* a = (unsigned char*)memalloc((int)cap);
        unsigned char* p = (unsigned char*)memalloc((int)cap);
        if (!a || !p)
        {
            if (a) memfree(a);
            if (p) memfree(p);
            return false;
        }

        if (s->aad) memfree(s->aad);
        if (s->plain) memfree(s->plain);
        s->aad = a;
        s->plain = p;
        s->cap = cap;
        return true;
    }
}

bool encrypt_frame_h264(const mls::group_state* g, unsigned long long self_user_id,
                        unsigned int* nonce_counter,
                        const unsigned char* in, unsigned int in_len,
                        unsigned char* out, unsigned int out_cap, unsigned int* out_len)
{
    if (!g || !g->established || !in || in_len < 5) return false;
    if (!ensure_scratch(&g_enc, in_len + 64)) return false;


    // Ranges are (offset, size) pairs over the rebuilt frame. Neighbouring
    // clear spans are merged, which keeps the list short enough to fit the one
    // byte the trailer gives it.
    const unsigned int MAX_RANGES = 48;
    unsigned int range_off[MAX_RANGES];
    unsigned int range_len[MAX_RANGES];
    unsigned int ranges = 0;

    unsigned int wrote = 0;        // bytes of rebuilt frame
    unsigned int aad_len = 0;
    unsigned int plain_len = 0;

    // Where each encrypted span landed, so the ciphertext can be put back.
    unsigned int enc_off[MAX_RANGES * 2];
    unsigned int enc_len[MAX_RANGES * 2];
    unsigned int enc_count = 0;

    unsigned int code_len = 0;
    int nal = next_nal_offset(in, in_len, 0, &code_len);
    if (nal < 0) return false;

    while (nal >= 0)
    {
        unsigned int start = (unsigned int)nal;

        unsigned int next_code = 0;
        int next = next_nal_offset(in, in_len, start, &next_code);
        unsigned int end = (next >= 0) ? (unsigned int)next - next_code : in_len;
        if (end <= start) break;

        unsigned int nal_len = end - start;
        unsigned char type = (unsigned char)(in[start] & 0x1F);

        // Slices carry the picture; everything else is parameter sets and
        // metadata the server is allowed to read in full.
        bool is_slice = (type == 1 || type == 5);
        unsigned int clear = nal_len;

        if (is_slice && nal_len > 1)
        {
            unsigned int pps = bytes_covering_pps(in + start + 1, nal_len - 1);
            clear = 1 + pps;
            if (clear > nal_len) clear = nal_len;
        }

        unsigned int need = wrote + 4 + nal_len;
        if (need > out_cap || ranges >= MAX_RANGES || enc_count >= MAX_RANGES * 2) return false;

        // Always a long start code: the receiver rewrites short ones anyway,
        // and a fixed size keeps the offsets in the trailer honest.
        //
        // The start code is inside the clear range, so it is part of what gets
        // authenticated. The receiver rebuilds the associated data by reading
        // the ranges straight out of the frame, and leaving these four bytes
        // out here would make the two sides compute different tags.
        unsigned int clear_start = wrote;
        const unsigned char start_code[4] = { 0, 0, 0, 1 };
        ccpy(out + wrote, start_code, 4);
        ccpy(g_enc.aad + aad_len, start_code, 4);
        wrote += 4;
        aad_len += 4;

        ccpy(out + wrote, in + start, (size_t)clear);
        ccpy(g_enc.aad + aad_len, in + start, (size_t)clear);
        aad_len += clear;
        wrote += clear;

        unsigned int clear_span = 4 + clear;

        // Merge with the previous range when this NAL begins where that ended.
        if (ranges > 0 && range_off[ranges - 1] + range_len[ranges - 1] == clear_start)
            range_len[ranges - 1] += clear_span;
        else
        {
            range_off[ranges] = clear_start;
            range_len[ranges] = clear_span;
            ranges++;
        }

        if (clear < nal_len)
        {
            unsigned int secret = nal_len - clear;
            ccpy(g_enc.plain + plain_len, in + start + clear, (size_t)secret);
            plain_len += secret;

            enc_off[enc_count] = wrote;
            enc_len[enc_count] = secret;
            enc_count++;

            wrote += secret;
        }

        nal = next;
        code_len = next_code;
    }

    if (plain_len == 0) return false;      // nothing to protect

    unsigned int sync_nonce = *nonce_counter;
    unsigned int generation = sync_nonce >> GENERATION_SHIFT;

    unsigned char key[KEY_BYTES];
    if (!ratchet_key(g, self_user_id, generation, key)) return false;

    unsigned char nonce[NONCE_BYTES];
    build_nonce(sync_nonce, nonce);

    unsigned char tag[16];
    if (!crypto::aesgcm_encrypt(key, KEY_BYTES, nonce, NONCE_BYTES,
                                g_enc.aad, aad_len, g_enc.plain, plain_len,
                                g_enc.plain, tag, 16))
        return false;

    // Put the ciphertext back where its plaintext came from.
    unsigned int taken = 0;
    for (unsigned int i = 0; i < enc_count; i++)
    {
        ccpy(out + enc_off[i], g_enc.plain + taken, (size_t)enc_len[i]);
        taken += enc_len[i];
    }

    unsigned char ranges_buf[128];
    unsigned int ranges_size = 0;
    for (unsigned int i = 0; i < ranges; i++)
    {
        if (ranges_size + 10 > sizeof(ranges_buf)) return false;
        ranges_size += leb128_write(ranges_buf + ranges_size, range_off[i]);
        ranges_size += leb128_write(ranges_buf + ranges_size, range_len[i]);
    }

    unsigned char nonce_buf[8];
    unsigned int nonce_size = leb128_write(nonce_buf, sync_nonce);

    unsigned int supplemental = TRUNCATED_TAG_BYTES + nonce_size + ranges_size + 1 + 2;
    if (supplemental > 255) return false;
    if (wrote + supplemental > out_cap) return false;

    ccpy(out + wrote, tag, TRUNCATED_TAG_BYTES);
    wrote += TRUNCATED_TAG_BYTES;
    ccpy(out + wrote, nonce_buf, (size_t)nonce_size);
    wrote += nonce_size;
    ccpy(out + wrote, ranges_buf, (size_t)ranges_size);
    wrote += ranges_size;

    out[wrote++] = (unsigned char)supplemental;
    out[wrote++] = 0xFA;
    out[wrote++] = 0xFA;

    *out_len = wrote;
    (*nonce_counter)++;
    return true;
}

// Why the last unwrap gave up. Every refusal below looks the same to the
// caller, and a viewer that decrypts nothing needs to know which of a dozen
// checks it tripped rather than that one of them did.
static const char* g_h264_error = "";
static char g_h264_detail[192] = { 0 };

const char* last_h264_error() { return g_h264_error; }
const char* last_h264_detail() { return g_h264_detail; }

// A tag that will not verify says nothing about why. There are only two
// possibilities worth telling apart: the key is from the wrong epoch, or the
// frame reaching us is not byte for byte the one that was protected. The
// ranges answer that on their own - every clear span of an H.264 frame begins
// at a start code, so if they no longer land on one the frame has shifted
// somewhere between the sender and here.
static void describe_ranges(const unsigned char* in, unsigned int media_len,
                            const unsigned int* range_off, const unsigned int* range_len,
                            unsigned int ranges, unsigned int aad_len, unsigned int cipher_len)
{
    unsigned int aligned = 0;
    for (unsigned int i = 0; i < ranges; i++)
    {
        unsigned int at = range_off[i];
        if (at + 4 <= media_len &&
            in[at] == 0 && in[at + 1] == 0 && in[at + 2] == 0 && in[at + 3] == 1)
            aligned++;
    }

    cnprint(g_h264_detail, sizeof(g_h264_detail),
            "диапазонов %u, на старт-коде %u, медиа %u, aad %u, шифра %u",
            ranges, aligned, media_len, aad_len, cipher_len);
}

#define H264_FAIL(reason) do { g_h264_error = (reason); return false; } while (0)

bool decrypt_frame_h264(const mls::group_state* g, unsigned long long sender_user_id,
                        const unsigned char* in, unsigned int in_len,
                        unsigned char* out, unsigned int out_cap, unsigned int* out_len)
{
    if (!g || !g->established || !in) H264_FAIL("нет группы");
    if (!is_protected(in, in_len)) H264_FAIL("не похоже на защищённый кадр");

    unsigned int supplemental = in[in_len - 3];
    if (supplemental < TRUNCATED_TAG_BYTES + 1 + 2 + 1 || supplemental > in_len) H264_FAIL("длина трейлера не годится");

    unsigned int media_len = in_len - supplemental;
    if (media_len == 0 || media_len > out_cap) H264_FAIL("кадр не помещается");
    if (!ensure_scratch(&g_dec, in_len + 64)) H264_FAIL("нет памяти");

    const unsigned char* tag = in + media_len;

    // Between the nonce and the size byte sits the range list, however long
    // that turns out to be.
    unsigned int nonce_at = media_len + TRUNCATED_TAG_BYTES;
    unsigned int size_byte_at = in_len - 3;

    unsigned int sync_nonce = 0;
    unsigned int nonce_size = 0;
    if (!leb128_read(in + nonce_at, size_byte_at - nonce_at, &sync_nonce, &nonce_size))
        H264_FAIL("nonce не читается");

    unsigned int at = nonce_at + nonce_size;

    // Rebuild the split the sender used: clear spans go to the associated data,
    // everything between them is ciphertext.
    unsigned int aad_len = 0;
    unsigned int cipher_len = 0;

    const unsigned int MAX_RANGES = 64;
    unsigned int range_off[MAX_RANGES];
    unsigned int range_len[MAX_RANGES];
    unsigned int ranges = 0;

    unsigned int cursor = 0;
    while (at < size_byte_at)
    {
        unsigned int offset = 0, size = 0, used = 0;
        if (!leb128_read(in + at, size_byte_at - at, &offset, &used)) H264_FAIL("смещение диапазона не читается");
        at += used;
        if (at >= size_byte_at) H264_FAIL("список диапазонов обрывается");
        if (!leb128_read(in + at, size_byte_at - at, &size, &used)) H264_FAIL("длина диапазона не читается");
        at += used;

        if (ranges >= MAX_RANGES) H264_FAIL("слишком много диапазонов");
        if (offset < cursor || offset > media_len || size > media_len - offset) H264_FAIL("диапазон выходит за кадр");

        if (offset > cursor)
        {
            ccpy(g_dec.plain + cipher_len, in + cursor, (size_t)(offset - cursor));
            cipher_len += offset - cursor;
        }
        ccpy(g_dec.aad + aad_len, in + offset, (size_t)size);
        aad_len += size;

        range_off[ranges] = offset;
        range_len[ranges] = size;
        ranges++;

        cursor = offset + size;
    }

    if (cursor < media_len)
    {
        ccpy(g_dec.plain + cipher_len, in + cursor, (size_t)(media_len - cursor));
        cipher_len += media_len - cursor;
    }
    if (cipher_len == 0) H264_FAIL("шифровать нечего");


    unsigned int generation = sync_nonce >> GENERATION_SHIFT;

    unsigned char key[KEY_BYTES];
    if (!ratchet_key(g, sender_user_id, generation, key))
        H264_FAIL("ключ поколения не выводится");

    unsigned char nonce[NONCE_BYTES];
    build_nonce(sync_nonce, nonce);

    if (!crypto::aesgcm_decrypt(key, KEY_BYTES, nonce, NONCE_BYTES,
                                g_dec.aad, aad_len, g_dec.plain, cipher_len,
                                tag, TRUNCATED_TAG_BYTES, g_dec.plain))
    {
        describe_ranges(in, media_len, range_off, range_len, ranges, aad_len, cipher_len);
        H264_FAIL("тег не сошёлся");
    }

    // Put both halves back where the ranges say they belong.
    ccpy(out, in, (size_t)media_len);

    unsigned int taken = 0;
    cursor = 0;
    for (unsigned int i = 0; i < ranges; i++)
    {
        if (range_off[i] > cursor)
        {
            unsigned int span = range_off[i] - cursor;
            ccpy(out + cursor, g_dec.plain + taken, (size_t)span);
            taken += span;
        }
        cursor = range_off[i] + range_len[i];
    }
    if (cursor < media_len)
        ccpy(out + cursor, g_dec.plain + taken, (size_t)(media_len - cursor));

    g_h264_error = "";
    g_h264_detail[0] = 0;
    *out_len = media_len;
    return true;
}

bool decrypt_frame(const mls::group_state* g, unsigned long long sender_user_id,
                   const unsigned char* in, unsigned int in_len,
                   unsigned char* out, unsigned int* out_len)
{
    if (!g || !g->established) return false;
    if (!is_protected(in, in_len)) return false;

    unsigned int supplemental = in[in_len - 3];
    if (supplemental < TRUNCATED_TAG_BYTES + 1 + 2 + 1 || supplemental > in_len) return false;

    unsigned int trailer = in_len - supplemental;
    const unsigned char* tag = in + trailer;

    unsigned int sync_nonce = 0;
    unsigned int nonce_size = 0;
    if (!leb128_read(in + trailer + TRUNCATED_TAG_BYTES,
                     supplemental - TRUNCATED_TAG_BYTES - 3, &sync_nonce, &nonce_size))
        return false;

    unsigned int cipher_len = trailer;
    if (cipher_len == 0) return false;


    unsigned int generation = sync_nonce >> GENERATION_SHIFT;

    unsigned char key[KEY_BYTES];
    if (!ratchet_key(g, sender_user_id, generation, key)) return false;

    unsigned char nonce[NONCE_BYTES];
    build_nonce(sync_nonce, nonce);

    if (!crypto::aesgcm_decrypt(key, KEY_BYTES, nonce, NONCE_BYTES, 0, 0,
                                in, cipher_len, tag, TRUNCATED_TAG_BYTES, out))
        return false;

    *out_len = cipher_len;
    return true;
}

} // namespace dave
