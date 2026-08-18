#include "pch.h"
#include "dave_tests.h"
#include "tls_codec.h"
#include "hpke.h"
#include "mls_tree.h"
#include "mls_types.h"
#include "mls_message.h"
#include "mls_group.h"
#include "dave_frames.h"
#include "dave_fixture.h"
#include "core/crypto.h"
#include "core/log.h"
#include "system/io/ufile.h"

namespace
{
    int g_failures = 0;

    void check(const char* name, bool ok)
    {
        if (!ok)
        {
            g_failures++;
            log_line("selftest: FAIL %s", name);
        }
    }

    bool bytes_equal(const void* a, const void* b, unsigned int len)
    {
        const unsigned char* x = (const unsigned char*)a;
        const unsigned char* y = (const unsigned char*)b;
        for (unsigned int i = 0; i < len; i++) if (x[i] != y[i]) return false;
        return true;
    }

    void test_varint()
    {
        // The three encodings spelled out in RFC 9420 section 2.1.2.
        struct { unsigned int value; const unsigned char* expect; unsigned int len; } cases[] = {
            { 0x25,       (const unsigned char*)"\x25", 1 },
            { 0x3fff,     (const unsigned char*)"\x7f\xff", 2 },
            { 0x3fffffff, (const unsigned char*)"\xbf\xff\xff\xff", 4 },
        };

        for (int i = 0; i < 3; i++)
        {
            unsigned char out[4];
            unsigned int n = crypto::varint_write(out, cases[i].value);
            check("varint encoding", n == cases[i].len && bytes_equal(out, cases[i].expect, n));
        }

        // Boundary values must survive a round trip.
        unsigned int values[] = { 0, 1, 63, 64, 16383, 16384, 0x3fffffff };
        for (int i = 0; i < 7; i++)
        {
            tls_writer w;
            w.init();
            w.varint(values[i]);

            tls_reader r;
            r.init(w.data(), w.size());

            unsigned int got = 0;
            bool ok = r.varint(&got) && got == values[i] && r.done();
            check("varint round trip", ok);
            w.free_writer();
        }

        // A reserved prefix (0b11) has to be rejected rather than misread.
        unsigned char reserved[4] = { 0xC0, 0, 0, 0 };
        tls_reader r;
        r.init(reserved, 4);
        unsigned int dummy = 0;
        check("varint rejects the reserved prefix", !r.varint(&dummy));
    }

    void test_integers()
    {
        tls_writer w;
        w.init();
        w.u8(0x12);
        w.u16(0x3456);
        w.u32(0x789abcdeu);
        w.u64(0x0102030405060708ull);

        const unsigned char expect[] = {
            0x12,
            0x34, 0x56,
            0x78, 0x9a, 0xbc, 0xde,
            0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08
        };
        check("integers are big endian",
              w.size() == sizeof(expect) && bytes_equal(w.data(), expect, w.size()));

        tls_reader r;
        r.init(w.data(), w.size());

        unsigned char a = 0; unsigned short b = 0; unsigned int c = 0; unsigned long long d = 0;
        bool ok = r.u8(&a) && r.u16(&b) && r.u32(&c) && r.u64(&d) && r.done();
        check("integer round trip",
              ok && a == 0x12 && b == 0x3456 && c == 0x789abcdeu && d == 0x0102030405060708ull);

        w.free_writer();
    }

    void test_opaque()
    {
        // A vector long enough to need a two byte length prefix.
        unsigned char payload[200];
        for (int i = 0; i < 200; i++) payload[i] = (unsigned char)(i * 7);

        tls_writer inner;
        inner.init();
        inner.u16(0xbeef);
        inner.opaque(payload, sizeof(payload));

        tls_writer outer;
        outer.init();
        outer.opaque(inner);
        outer.u8(0xff);

        tls_reader r;
        r.init(outer.data(), outer.size());

        const unsigned char* nested = 0;
        unsigned int nested_len = 0;
        bool ok = r.opaque(&nested, &nested_len);

        tls_reader ri;
        ri.init(nested, nested_len);

        unsigned short marker = 0;
        const unsigned char* got = 0;
        unsigned int got_len = 0;
        ok = ok && ri.u16(&marker) && ri.opaque(&got, &got_len) && ri.done();
        ok = ok && marker == 0xbeef && got_len == sizeof(payload) &&
             bytes_equal(got, payload, got_len);

        unsigned char tail = 0;
        ok = ok && r.u8(&tail) && tail == 0xff && r.done();

        check("nested opaque round trip", ok);

        // Truncated input must fail rather than read past the end.
        tls_reader bad;
        bad.init(outer.data(), outer.size() - 3);
        const unsigned char* p = 0;
        unsigned int n = 0;
        check("reader rejects truncated input", !bad.opaque(&p, &n));

        inner.free_writer();
        outer.free_writer();
    }

    void test_hpke()
    {
        unsigned char pub[hpke::NPK];
        unsigned char priv[hpke::NSK];
        check("hpke keypair", crypto::p256_generate(pub, priv));

        const char* info = "mls hpke test info";
        const char* aad = "additional data";
        unsigned char plain[64];
        for (int i = 0; i < 64; i++) plain[i] = (unsigned char)(i ^ 0x5a);

        unsigned char enc[hpke::NENC];
        unsigned char cipher[64];
        unsigned char tag[hpke::NT];

        bool ok = hpke::seal_single(pub, info, (unsigned int)ccslenf(info),
                                    aad, (unsigned int)ccslenf(aad),
                                    plain, sizeof(plain), enc, cipher, tag);
        check("hpke seal", ok);

        unsigned char recovered[64];
        ok = ok && hpke::open_single(enc, priv, info, (unsigned int)ccslenf(info),
                                     aad, (unsigned int)ccslenf(aad),
                                     cipher, sizeof(cipher), tag, recovered);
        check("hpke open", ok && bytes_equal(recovered, plain, sizeof(plain)));

        // Wrong info must not decrypt: it feeds the key schedule.
        unsigned char junk[64];
        bool bad_info = hpke::open_single(enc, priv, "other info", 10,
                                          aad, (unsigned int)ccslenf(aad),
                                          cipher, sizeof(cipher), tag, junk);
        check("hpke rejects a different info", !bad_info);

        // Wrong aad must not authenticate.
        bool bad_aad = hpke::open_single(enc, priv, info, (unsigned int)ccslenf(info),
                                         "wrong aad", 9,
                                         cipher, sizeof(cipher), tag, junk);
        check("hpke rejects a different aad", !bad_aad);

        // A second recipient key must not open the message.
        unsigned char other_pub[hpke::NPK];
        unsigned char other_priv[hpke::NSK];
        crypto::p256_generate(other_pub, other_priv);
        bool bad_key = hpke::open_single(enc, other_priv, info, (unsigned int)ccslenf(info),
                                         aad, (unsigned int)ccslenf(aad),
                                         cipher, sizeof(cipher), tag, junk);
        check("hpke rejects the wrong recipient", !bad_key);
    }

    void test_hpke_sequence()
    {
        unsigned char pub[hpke::NPK];
        unsigned char priv[hpke::NSK];
        crypto::p256_generate(pub, priv);

        unsigned char enc[hpke::NENC];
        hpke::context sender, receiver;

        bool ok = hpke::setup_base_sender(pub, 0, 0, enc, &sender);
        ok = ok && hpke::setup_base_receiver(enc, priv, 0, 0, &receiver);
        check("hpke context setup", ok);

        // Both sides must derive the same key material.
        check("hpke contexts agree",
              ok && bytes_equal(sender.key, receiver.key, hpke::NK) &&
              bytes_equal(sender.base_nonce, receiver.base_nonce, hpke::NN));

        unsigned char c1[8], c2[8], t1[hpke::NT], t2[hpke::NT];
        ok = ok && hpke::seal(&sender, 0, 0, "message1", 8, c1, t1);
        ok = ok && hpke::seal(&sender, 0, 0, "message2", 8, c2, t2);

        unsigned char p1[8], p2[8];
        ok = ok && hpke::open(&receiver, 0, 0, c1, 8, t1, p1);
        ok = ok && hpke::open(&receiver, 0, 0, c2, 8, t2, p2);

        check("hpke sequence numbers advance in step",
              ok && bytes_equal(p1, "message1", 8) && bytes_equal(p2, "message2", 8));
    }
}

namespace
{
    void test_tree_math()
    {
        // Worked example from RFC 9420 appendix C for a tree of four leaves:
        //         3
        //     1       5
        //   0   2   4   6
        check("tree level", mls_tree::level(0) == 0 && mls_tree::level(1) == 1 &&
                            mls_tree::level(2) == 0 && mls_tree::level(3) == 2 &&
                            mls_tree::level(5) == 1);

        check("tree width", mls_tree::node_width(1) == 1 && mls_tree::node_width(4) == 7 &&
                            mls_tree::node_width(5) == 9);

        check("tree root", mls_tree::root(1) == 0 && mls_tree::root(4) == 3 &&
                           mls_tree::root(2) == 1);

        check("tree children", mls_tree::left(3) == 1 && mls_tree::right(3) == 5 &&
                               mls_tree::left(1) == 0 && mls_tree::right(1) == 2);

        check("tree parent", mls_tree::parent(0) == 1 && mls_tree::parent(2) == 1 &&
                             mls_tree::parent(1) == 3 && mls_tree::parent(5) == 3);

        check("tree sibling", mls_tree::sibling(0) == 2 && mls_tree::sibling(2) == 0 &&
                              mls_tree::sibling(1) == 5 && mls_tree::sibling(5) == 1);

        // Structural invariants across a range of tree sizes.
        for (unsigned int n = 1; n <= 64; n++)
        {
            unsigned int r = mls_tree::root(n);
            unsigned int w = mls_tree::node_width(n);

            bool ok = r < w;

            for (unsigned int x = 0; x < w && ok; x++)
            {
                if (mls_tree::level(x) > 0)
                {
                    // Both children must point back at their parent.
                    ok = mls_tree::parent(mls_tree::left(x)) == x &&
                         mls_tree::parent(mls_tree::right(x)) == x;
                }
                if (ok && x != r)
                {
                    // Sibling is an involution, and both share one parent.
                    unsigned int s = mls_tree::sibling(x);
                    ok = mls_tree::sibling(s) == x &&
                         mls_tree::parent(s) == mls_tree::parent(x);
                }
            }

            if (!ok)
            {
                check("tree invariants", false);
                break;
            }
        }

        // A direct path must climb to the root, and the copath must match it
        // in length.
        for (unsigned int n = 2; n <= 32; n++)
        {
            unsigned int path[64], co[64];
            unsigned int leaf = mls_tree::leaf_to_node(0);

            unsigned int pc = mls_tree::direct_path(leaf, n, path, 64);
            unsigned int cc = mls_tree::copath(leaf, n, co, 64);

            bool ok = pc == cc && pc > 0 && path[pc - 1] == mls_tree::root(n);
            for (unsigned int i = 0; i < pc && ok; i++) ok = mls_tree::in_subtree(path[i], leaf);

            if (!ok)
            {
                check("tree direct path", false);
                break;
            }
        }
    }

    // The one node of a sender's path whose subtree we sit in is where a
    // path secret can be decrypted, so getting this wrong means never finding
    // the ciphertext meant for us.
    void test_common_ancestor()
    {
        // Four leaves: nodes 0, 2, 4, 6 with parents 1, 3, 5 and root 3.
        check("ancestor of siblings", mls_tree::common_ancestor(0, 2) == 1);
        check("ancestor across the root", mls_tree::common_ancestor(0, 4) == 3);
        check("ancestor is symmetric",
              mls_tree::common_ancestor(4, 0) == mls_tree::common_ancestor(0, 4));
        check("ancestor of a node and itself", mls_tree::common_ancestor(2, 2) == 2);
        check("a parent contains its child", mls_tree::common_ancestor(1, 0) == 1);
        check("ancestor of the far pair", mls_tree::common_ancestor(0, 6) == 3);
        check("ancestor within the right half", mls_tree::common_ancestor(4, 6) == 5);

        // It has to agree with the direct paths: the answer is always the first
        // node the two paths share.
        for (unsigned int leaves = 2; leaves <= 8; leaves++)
        {
            for (unsigned int a = 0; a < leaves; a++)
            {
                for (unsigned int b = 0; b < leaves; b++)
                {
                    if (a == b) continue;

                    unsigned int na = mls_tree::leaf_to_node(a);
                    unsigned int nb = mls_tree::leaf_to_node(b);
                    unsigned int meet = mls_tree::common_ancestor(na, nb);

                    unsigned int path[32];
                    unsigned int n = mls_tree::direct_path(na, leaves, path, 32);

                    bool on_path = false;
                    for (unsigned int i = 0; i < n && !on_path; i++) on_path = path[i] == meet;

                    check("ancestor sits on the direct path", on_path);
                    check("ancestor covers both",
                          mls_tree::in_subtree(meet, na) && mls_tree::in_subtree(meet, nb));
                }
            }
        }
    }

    void test_key_package()
    {
        unsigned char sig_pub[65], sig_priv[96];
        check("signing keypair", crypto::p256_generate(sig_pub, sig_priv));

        mls::key_package kp;
        mls::key_package_private kp_priv;
        bool ok = mls::create_key_package(1310479198309056562ull, sig_priv, &kp, &kp_priv);
        check("key package creation", ok);
        check("key package self verifies", ok && kp.verify());

        // Serialize, parse back, and the result must still verify.
        tls_writer w;
        w.init(1024);
        kp.write(&w);

        mls::key_package parsed;
        parsed.init();

        tls_reader r;
        r.init(w.data(), w.size());

        bool parsed_ok = parsed.read(&r) && r.done();
        check("key package round trip", parsed_ok && parsed.verify());

        check("key package identity survives",
              parsed_ok && parsed.leaf.cred.identity_len == 8 &&
              parsed.leaf.cred.user_id() == 1310479198309056562ull);

        check("key package keys survive",
              parsed_ok && bytes_equal(parsed.init_key, kp.init_key, hpke::NPK) &&
              bytes_equal(parsed.leaf.encryption_key, kp.leaf.encryption_key, hpke::NPK));

        // A tampered signature must be caught.
        mls::key_package tampered = parsed;
        tampered.signature[0] ^= 0x01;
        check("key package rejects a bad signature", parsed_ok && !tampered.verify());

        // So must a tampered identity, since the leaf signature covers it.
        mls::key_package swapped = parsed;
        swapped.leaf.cred.identity[7] ^= 0x01;
        check("key package rejects a changed identity", parsed_ok && !swapped.verify());

        // Two key packages must produce different references.
        unsigned char ref_a[32], ref_b[32];
        kp.compute_ref(ref_a);
        tampered.compute_ref(ref_b);
        check("key package refs differ", !bytes_equal(ref_a, ref_b, 32));

        w.free_writer();
    }
}

namespace
{
    // Parses a real op 27 payload recorded from discord's voice gateway. This
    // is the only check here that proves agreement with the server rather than
    // with itself.
    void test_real_proposals()
    {
        bool is_revoke = true;
        mls::proposal_message messages[8];
        unsigned int count = 0;

        bool ok = mls::parse_proposals_payload(DAVE_PROPOSALS_FIXTURE,
                                               (unsigned int)sizeof(DAVE_PROPOSALS_FIXTURE),
                                               &is_revoke, messages, 8, &count);
        check("live proposals parse", ok);
        check("live proposals are not a revoke", ok && !is_revoke);
        check("live proposals hold one message", ok && count == 1);
        if (!ok || count != 1) return;

        const mls::proposal_message* m = &messages[0];

        check("live proposal group id is 8 bytes", m->group_id_len == 8);
        check("live proposal epoch is 0", m->epoch == 0);
        check("live proposal comes from the external sender",
              m->snd.type == mls::SENDER_EXTERNAL && m->snd.index == 0);
        check("live proposal is an add", m->prop.type == mls::PROPOSAL_ADD);

        // The captured add carries the other participant's key package.
        const mls::key_package* kp = &m->prop.add;
        check("live key package identity", kp->leaf.cred.identity_len == 8 &&
                                           kp->leaf.cred.user_id() == 1310479198309056562ull);
        check("live key package keys are uncompressed points",
              kp->init_key[0] == 0x04 && kp->leaf.encryption_key[0] == 0x04 &&
              kp->leaf.signature_key[0] == 0x04);
        check("live key package signatures are DER",
              kp->signature_len >= 8 && kp->signature[0] == 0x30 &&
              kp->leaf.signature_len >= 8 && kp->leaf.signature[0] == 0x30);

        // The strongest check available: discord's own signatures must verify
        // against our SignWithLabel implementation.
        check("live leaf node signature verifies", kp->leaf.verify(0, 0, 0));
        check("live key package signature verifies", kp->verify());

        // Re-serializing must reproduce the captured bytes exactly, which
        // proves the writer matches the reader field for field.
        tls_writer w;
        w.init(768);
        kp->write(&w);

        tls_writer proposal_bytes;
        proposal_bytes.init(768);
        m->prop.write(&proposal_bytes);

        check("live proposal re-serializes identically",
              proposal_bytes.size() == m->proposal_bytes_len &&
              bytes_equal(proposal_bytes.data(), m->proposal_bytes, m->proposal_bytes_len));

        w.free_writer();
        proposal_bytes.free_writer();
    }
}

// ---------------------------------------------------------------------------
// H.264 frame protection
// ---------------------------------------------------------------------------
//
// The trailer written by encrypt_frame_h264 is read back here by hand, the way
// a receiver would, rather than by calling our own reader. If the two ever
// disagree about the layout this is what notices.

namespace
{
    // An established group needs nothing more than an exporter secret for the
    // media ratchets to work, so a real handshake is not required to exercise
    // frame protection.
    void make_test_group(mls::group_state* g)
    {
        ccfset(g, 0, sizeof(*g));
        for (int i = 0; i < 32; i++) g->exporter_secret[i] = (unsigned char)(0xA0 + i);
        g->epoch = 3;
        g->established = true;
    }

    void put_nal(ubuffer* b, int code_len, unsigned char header,
                 unsigned int payload_len, unsigned char seed)
    {
        b->append_char(0); b->append_char(0);
        if (code_len == 4) b->append_char(0);
        b->append_char(1);
        b->append_char((char)header);
        for (unsigned int i = 0; i < payload_len; i++)
            b->append_char((char)(seed + (i % 251)));
    }

    unsigned int read_leb(const unsigned char* p, unsigned int len, unsigned int* value)
    {
        unsigned int v = 0, shift = 0, used = 0;
        while (used < len)
        {
            unsigned char byte = p[used++];
            v |= (unsigned int)(byte & 0x7F) << shift;
            if ((byte & 0x80) == 0) { *value = v; return used; }
            shift += 7;
            if (shift > 28) break;
        }
        *value = 0;
        return 0;
    }

    void test_h264_protection()
    {
        mls::group_state g;
        make_test_group(&g);

        const unsigned long long self_id = 1518472384972062778ull;

        // A believable access unit: delimiter, parameter sets, then a slice big
        // enough to be fragmented.
        ubuffer frame;
        frame.init(70000);
        put_nal(&frame, 4, 0x09, 1, 0x10);        // access unit delimiter
        put_nal(&frame, 4, 0x67, 24, 0x20);       // SPS
        put_nal(&frame, 3, 0x68, 6, 0x30);        // PPS, short start code on purpose
        put_nal(&frame, 4, 0x06, 40, 0x40);       // SEI
        put_nal(&frame, 4, 0x65, 40000, 0x50);    // IDR slice

        unsigned int cap = frame.size + 4096;
        unsigned char* out = (unsigned char*)memalloc((int)cap);
        unsigned int out_len = 0;
        unsigned int nonce = 7;

        bool encrypted = dave::encrypt_frame_h264(&g, self_id, &nonce,
                                                  frame.data, frame.size, out, cap, &out_len);
        check("h264 frame protects", encrypted);
        if (!encrypted) { memfree(out); frame.free_buffer(); return; }

        check("h264 nonce counter advances", nonce == 8);

        // ---- read the trailer the way a receiver does -----------------------
        check("h264 trailer ends with the magic marker",
              out_len > 3 && out[out_len - 2] == 0xFA && out[out_len - 1] == 0xFA);

        unsigned int supplemental = out[out_len - 3];
        check("h264 supplemental size fits the frame",
              supplemental >= 8 + 1 + 2 + 1 && supplemental <= out_len);

        unsigned int media_len = out_len - supplemental;
        unsigned int size_byte_at = out_len - 3;
        unsigned int at = media_len + 8;               // past the truncated tag

        unsigned int sync_nonce = 0;
        unsigned int used = read_leb(out + at, size_byte_at - at, &sync_nonce);
        check("h264 nonce reads back", used > 0 && sync_nonce == 7);
        at += used;

        // ---- the ranges ------------------------------------------------------
        unsigned int ranges = 0, cursor = 0, clear_total = 0;
        bool ordered = true, in_bounds = true, clear_untouched = true;

        while (at < size_byte_at)
        {
            unsigned int offset = 0, size = 0;
            unsigned int u1 = read_leb(out + at, size_byte_at - at, &offset);
            if (!u1) break;
            at += u1;
            unsigned int u2 = read_leb(out + at, size_byte_at - at, &size);
            if (!u2) break;
            at += u2;

            if (offset < cursor) ordered = false;
            if (offset > media_len || size > media_len - offset) in_bounds = false;
            cursor = offset + size;
            clear_total += size;
            ranges++;
        }

        check("h264 range list is consumed exactly", at == size_byte_at);
        check("h264 ranges are ordered and do not overlap", ordered);
        check("h264 ranges stay inside the media", in_bounds);
        check("h264 has ranges at all", ranges > 0);

        // Every clear span must still hold the original bytes, and the slice
        // payload must not. The frame was rebuilt with four byte start codes,
        // so it is compared against a copy normalised the same way.
        ubuffer expect;
        expect.init(frame.size + 16);
        put_nal(&expect, 4, 0x09, 1, 0x10);
        put_nal(&expect, 4, 0x67, 24, 0x20);
        put_nal(&expect, 4, 0x68, 6, 0x30);
        put_nal(&expect, 4, 0x06, 40, 0x40);
        put_nal(&expect, 4, 0x65, 40000, 0x50);

        check("h264 protected media is the frame with long start codes",
              media_len == expect.size);

        if (media_len == expect.size)
        {
            at = media_len + 8;
            read_leb(out + at, size_byte_at - at, &sync_nonce);
            at += used;
            cursor = 0;
            unsigned int changed = 0, total_secret = 0;

            while (at < size_byte_at)
            {
                unsigned int offset = 0, size = 0;
                at += read_leb(out + at, size_byte_at - at, &offset);
                at += read_leb(out + at, size_byte_at - at, &size);

                for (unsigned int i = cursor; i < offset; i++)
                {
                    total_secret++;
                    if (out[i] != expect.data[i]) changed++;
                }
                if (!bytes_equal(out + offset, expect.data + offset, size))
                    clear_untouched = false;
                cursor = offset + size;
            }
            for (unsigned int i = cursor; i < media_len; i++)
            {
                total_secret++;
                if (out[i] != expect.data[i]) changed++;
            }

            check("h264 clear spans are left as they were", clear_untouched);
            check("h264 slice payload really is encrypted",
                  total_secret > 1000 && changed > total_secret / 2);

            // The parameter sets have to survive: the server reads them.
            check("h264 leaves the whole SPS readable",
                  bytes_equal(out + 5, expect.data + 5, 29));
        }

        // ---- round trip -------------------------------------------------------
        unsigned char* back = (unsigned char*)memalloc((int)cap);
        unsigned int back_len = 0;

        bool decrypted = dave::decrypt_frame_h264(&g, self_id, out, out_len,
                                                  back, cap, &back_len);
        check("h264 frame unprotects", decrypted);
        check("h264 round trip recovers every byte",
              decrypted && back_len == expect.size &&
              bytes_equal(back, expect.data, expect.size));

        // A tampered slice must not verify.
        if (decrypted)
        {
            out[media_len / 2] ^= 0x40;
            unsigned int junk = 0;
            check("h264 tampering is caught",
                  !dave::decrypt_frame_h264(&g, self_id, out, out_len, back, cap, &junk));
            out[media_len / 2] ^= 0x40;
        }

        // A different sender derives a different key, so it must fail too.
        unsigned int junk_len = 0;
        check("h264 wrong sender cannot read it",
              !dave::decrypt_frame_h264(&g, self_id + 1, out, out_len, back, cap, &junk_len));

        memfree(back);
        memfree(out);
        expect.free_buffer();
        frame.free_buffer();
    }

    // A member who did not author a commit has to apply it to reach the next
    // epoch. Getting that wrong is invisible until media stops decrypting, so
    // it is checked here against the commit builder: two states that started
    // together must still be deriving the same secrets afterwards.
    // A derived secret has to name a usable key pair, and the same secret has
    // to name the same pair everywhere. The seal/open round trip is what proves
    // the private half really belongs to the public one.
    void test_derive_key_pair()
    {
        const unsigned char ikm[8] = { 9, 8, 7, 6, 5, 4, 3, 2 };

        unsigned char pub_a[hpke::NPK], priv_a[hpke::NSK];
        unsigned char pub_b[hpke::NPK], priv_b[hpke::NSK];

        bool ok = hpke::derive_key_pair(ikm, sizeof(ikm), pub_a, priv_a);
        check("derive_key_pair works", ok);
        ok = ok && hpke::derive_key_pair(ikm, sizeof(ikm), pub_b, priv_b);
        check("derive_key_pair is deterministic",
              ok && bytes_equal(pub_a, pub_b, hpke::NPK) &&
              bytes_equal(priv_a, priv_b, hpke::NSK));

        unsigned char other[hpke::NPK], other_priv[hpke::NSK];
        const unsigned char ikm2[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
        bool ok2 = hpke::derive_key_pair(ikm2, sizeof(ikm2), other, other_priv);
        check("derive_key_pair separates inputs",
              ok2 && !bytes_equal(pub_a, other, hpke::NPK));

        // The pair has to work for real, not merely exist.
        const unsigned char plain[5] = { 'h', 'e', 'l', 'l', 'o' };
        unsigned char enc[hpke::NENC], cipher[5], tag[hpke::NT];
        bool sealed = ok && hpke::seal_single(pub_a, "info", 4, 0, 0,
                                              plain, sizeof(plain), enc, cipher, tag);
        unsigned char recovered[5];
        bool opened = sealed && hpke::open_single(enc, priv_a, "info", 4, 0, 0,
                                                  cipher, sizeof(cipher), tag, recovered);
        check("derived pair seals and opens",
              opened && bytes_equal(recovered, plain, sizeof(plain)));
    }

    void test_commit_transition()
    {
        unsigned char alice_sig_pub[65], alice_sig_priv[96];
        unsigned char bob_sig_pub[65], bob_sig_priv[96];
        unsigned char carol_sig_pub[65], carol_sig_priv[96];

        if (!crypto::p256_generate(alice_sig_pub, alice_sig_priv) ||
            !crypto::p256_generate(bob_sig_pub, bob_sig_priv) ||
            !crypto::p256_generate(carol_sig_pub, carol_sig_priv))
        {
            check("commit: signing keys", false);
            return;
        }

        mls::key_package alice_kp, bob_kp, carol_kp;
        mls::key_package_private alice_priv, bob_priv, carol_priv;

        bool made = mls::create_key_package(1001ull, alice_sig_priv, &alice_kp, &alice_priv) &&
                    mls::create_key_package(1002ull, bob_sig_priv, &bob_kp, &bob_priv) &&
                    mls::create_key_package(1003ull, carol_sig_priv, &carol_kp, &carol_priv);
        check("commit: key packages", made);
        if (!made) return;

        const unsigned char group_id[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };

        mls::group_state alice;
        bool ok = mls::create_group(&alice, group_id, sizeof(group_id), &alice_kp.leaf,
                                    alice_sig_priv, alice_priv.encryption_private);
        check("commit: group created", ok);
        if (!ok) return;

        // ---- Alice adds Bob, Bob joins from the welcome ----
        //
        // The reference on a proposal covers the AuthenticatedContent it
        // arrived in. Nothing here is on the wire, so any stable bytes will do:
        // what matters is that the builder and the applier agree, which is the
        // same thing the real protocol relies on.
        const unsigned char bob_auth[4] = { 'b', 'o', 'b', '!' };
        mls::proposal_message add_bob;
        ccfset(&add_bob, 0, sizeof(add_bob));
        add_bob.prop.type = mls::PROPOSAL_ADD;
        add_bob.prop.add = bob_kp;
        add_bob.auth_content_bytes = bob_auth;
        add_bob.auth_content_len = sizeof(bob_auth);

        ubuffer commit1, welcome1;
        commit1.init();
        welcome1.init();
        ok = mls::build_commit(&alice, &add_bob, 1, &commit1, &welcome1);
        check("commit: alice adds bob", ok);

        // The builder wraps the welcome in an MLSMessage; op 30 hands over the
        // bare struct, which is what process_welcome is written against. Four
        // bytes of version and wire format come off here to match.
        mls::group_state bob;
        ok = ok && welcome1.size > 4 &&
             mls::process_welcome(&bob, welcome1.data + 4, welcome1.size - 4,
                                  &bob_kp, &bob_priv, bob_sig_priv);
        check("commit: bob joins from the welcome", ok);
        check("commit: same epoch after the welcome", ok && bob.epoch == alice.epoch);
        check("commit: same exporter after the welcome",
              ok && bytes_equal(bob.exporter_secret, alice.exporter_secret, 32));

        commit1.free_buffer();
        welcome1.free_buffer();
        if (!ok) return;

        // ---- Alice adds Carol; Bob has to follow by applying the commit ----
        const unsigned char carol_auth[6] = { 'c', 'a', 'r', 'o', 'l', '!' };
        mls::proposal_message add_carol;
        ccfset(&add_carol, 0, sizeof(add_carol));
        add_carol.prop.type = mls::PROPOSAL_ADD;
        add_carol.prop.add = carol_kp;
        add_carol.auth_content_bytes = carol_auth;
        add_carol.auth_content_len = sizeof(carol_auth);

        // What Bob keeps from op 27, so the reference in the commit resolves.
        mls::cached_proposal known;
        add_carol.compute_ref(known.ref);
        known.prop = add_carol.prop;

        ubuffer commit2, welcome2;
        commit2.init();
        welcome2.init();
        ok = mls::build_commit(&alice, &add_carol, 1, &commit2, &welcome2);
        check("commit: alice adds carol", ok);

        // Written out so the bytes can be read against RFC 9420 by something
        // other than the code that produced them. Every check in this file
        // holds this implementation against itself, which is exactly the kind
        // of agreement that survives a shared misreading of the spec - and a
        // commit discord silently ignores is what that looks like from here.
        if (ok)
        {
            wchar_t path[MAX_PATH];
            if (ufile::app_path(L"mls_commit.bin", path, MAX_PATH))
                ufile::write_all(path, commit2.data, commit2.size);
            if (ufile::app_path(L"mls_welcome.bin", path, MAX_PATH))
                ufile::write_all(path, welcome2.data, welcome2.size);
        }

        if (ok)
        {
            const char* why = "";
            bool applied = mls::process_commit(&bob, commit2.data, commit2.size, &known, 1, &why);
            if (!applied) log_line("selftest: process_commit refused: %s", why);

            check("commit: bob applies it", applied);
            check("commit: epochs match again", applied && bob.epoch == alice.epoch);
            check("commit: exporters match again",
                  applied && bytes_equal(bob.exporter_secret, alice.exporter_secret, 32));
            check("commit: bob sees three leaves",
                  applied && bob.leaf_used[0] && bob.leaf_used[1] && bob.leaf_used[2]);

            // Applying the same commit twice must not move the group again.
            bool twice = mls::process_commit(&bob, commit2.data, commit2.size, &known, 1, &why);
            check("commit: a repeat is refused", !twice && bob.epoch == alice.epoch);
        }

        commit2.free_buffer();
        welcome2.free_buffer();
        if (!ok) return;

        // ---- Alice removes Carol; Bob follows ----
        //
        // Somebody leaving is the half this used to be missing. A commit that
        // carried only the adds left the author on an epoch the group had
        // left the moment a viewer closed the stream, and from there nothing
        // it sent could be opened by anyone still watching.
        const unsigned char bye_auth[4] = { 'b', 'y', 'e', '!' };
        mls::proposal_message drop_carol;
        ccfset(&drop_carol, 0, sizeof(drop_carol));
        drop_carol.prop.type = mls::PROPOSAL_REMOVE;
        drop_carol.prop.remove_index = 2;
        drop_carol.auth_content_bytes = bye_auth;
        drop_carol.auth_content_len = sizeof(bye_auth);

        mls::cached_proposal known_remove;
        drop_carol.compute_ref(known_remove.ref);
        known_remove.prop = drop_carol.prop;

        ubuffer commit3, welcome3;
        commit3.init();
        welcome3.init();

        ok = mls::build_commit(&alice, &drop_carol, 1, &commit3, &welcome3);
        check("remove: alice commits it", ok);
        check("remove: no welcome for a commit that lets nobody in",
              ok && welcome3.size == 0);
        check("remove: carol's leaf is free again", ok && !alice.leaf_used[2]);

        if (ok)
        {
            const char* why = "";
            bool applied = mls::process_commit(&bob, commit3.data, commit3.size,
                                               &known_remove, 1, &why);
            if (!applied) log_line("selftest: process_commit refused the remove: %s", why);

            check("remove: bob applies it", applied);
            check("remove: epochs match", applied && bob.epoch == alice.epoch);
            check("remove: exporters match",
                  applied && bytes_equal(bob.exporter_secret, alice.exporter_secret, 32));
            check("remove: bob sees carol gone", applied && !bob.leaf_used[2]);
        }

        commit3.free_buffer();
        welcome3.free_buffer();

        // ---- a leaf added under a parent that is not blank ----
        //
        // Every parent is blank in a group this code builds on its own, so
        // nothing above ever exercises what happens when one is not. A real
        // group's tree arrives from a Welcome with rekeyed parents in it, and
        // an Add has to record the new leaf in each of them: they are hashed
        // into the tree hash, and a tree hash nobody else computes is a
        // confirmation tag that fails on the next commit.
        //
        // Two clients that both leave it out still agree with each other,
        // which is why this went unnoticed here and showed up only against
        // discord's own clients. The check is against the rule, not against
        // ourselves.
        {
            unsigned char before[32];
            compute_tree_hash(&alice, before);

            // The root of a two leaf tree, made to look rekeyed.
            unsigned int root = mls_tree::root(alice.leaf_count);
            mls::parent_node* p = &alice.parents[root];
            ccfset(p, 0, sizeof(*p));
            p->used = true;
            p->parent_hash_len = 0;

            unsigned char after_parent[32];
            compute_tree_hash(&alice, after_parent);
            check("unmerged: a parent that is not blank changes the tree hash",
                  !bytes_equal(before, after_parent, 32));

            const unsigned char dave_auth[5] = { 'd', 'a', 'v', 'e', '!' };
            mls::proposal_message add_dave;
            ccfset(&add_dave, 0, sizeof(add_dave));
            add_dave.prop.type = mls::PROPOSAL_ADD;
            add_dave.prop.add = carol_kp;
            add_dave.auth_content_bytes = dave_auth;
            add_dave.auth_content_len = sizeof(dave_auth);

            ubuffer commit4, welcome4;
            commit4.init();
            welcome4.init();

            bool built = mls::build_commit(&alice, &add_dave, 1, &commit4, &welcome4);
            check("unmerged: the add commits", built);

            const mls::parent_node* after = &alice.parents[root];
            check("unmerged: the new leaf is recorded in the parent",
                  built && after->unmerged_count == 1);
            check("unmerged: it is the leaf that was allocated",
                  built && after->unmerged_count == 1 && after->unmerged[0] == 2);

            commit4.free_buffer();
            welcome4.free_buffer();
        }
    }
}

bool dave_self_test()
{
    g_failures = 0;

    test_varint();
    test_integers();
    test_opaque();
    test_hpke();
    test_hpke_sequence();
    test_tree_math();
    test_common_ancestor();
    test_key_package();
    test_real_proposals();
    test_h264_protection();
    test_derive_key_pair();
    test_commit_transition();

    if (g_failures == 0) log_line("selftest: tls codec, hpke, tree math and key packages passed");
    else log_line("selftest: %d dave failure(s)", g_failures);

    return g_failures == 0;
}
