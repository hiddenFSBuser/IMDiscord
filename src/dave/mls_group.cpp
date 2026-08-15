#include "pch.h"
#include "mls_group.h"
#include "mls_tree.h"
#include "core/crypto.h"
#include "core/log.h"

namespace mls
{

namespace
{
    // optional<T> is a presence byte followed by the value when present.
    void write_optional_absent(tls_writer* w) { w->u8(0); }

    void write_group_context_body(const group_state* g, tls_writer* w)
    {
        w->u16(PROTOCOL_VERSION_MLS10);
        w->u16(CIPHERSUITE_P256);
        w->opaque(g->group_id, g->group_id_len);
        w->u64(g->epoch);
        w->opaque(g->tree_hash, 32);
        w->opaque(g->confirmed_transcript_hash, g->confirmed_transcript_hash_len);
        w->opaque(0, 0);   // extensions<V>
    }

    // ParentNode { HPKEPublicKey encryption_key; opaque parent_hash<V>;
    //              uint32 unmerged_leaves<V>; }
    void write_parent_node(const parent_node* p, tls_writer* w)
    {
        w->opaque(p->encryption_key, 65);
        w->opaque(p->parent_hash, p->parent_hash_len);

        tls_writer unmerged;
        unmerged.init(128);
        for (unsigned int i = 0; i < p->unmerged_count; i++) unmerged.u32(p->unmerged[i]);
        w->opaque(unmerged);
        unmerged.free_writer();
    }

    // TreeHashInput for a node, recursing over the implicit binary tree.
    void tree_hash_node(const group_state* g, unsigned int node, unsigned char out[32])
    {
        tls_writer w;
        w.init(512);

        if (mls_tree::is_leaf(node))
        {
            unsigned int leaf_index = mls_tree::node_to_leaf(node);

            w.u8(NODE_LEAF);
            w.u32(leaf_index);

            if (leaf_index < g->leaf_count && g->leaf_used[leaf_index])
            {
                w.u8(1);
                g->leaves[leaf_index].write(&w);
            }
            else
            {
                write_optional_absent(&w);
            }
        }
        else
        {
            unsigned char left_hash[32];
            unsigned char right_hash[32];
            tree_hash_node(g, mls_tree::left(node), left_hash);
            tree_hash_node(g, mls_tree::right(node), right_hash);

            w.u8(NODE_PARENT);
            if (node < MAX_NODES && g->parents[node].used)
            {
                w.u8(1);
                write_parent_node(&g->parents[node], &w);
            }
            else
            {
                write_optional_absent(&w);
            }
            w.opaque(left_hash, 32);
            w.opaque(right_hash, 32);
        }

        crypto::sha256(w.data(), w.size(), out);
        w.free_writer();
    }

    // The ratchet_tree extension, so a joiner can rebuild the tree from the
    // welcome alone.
    void write_ratchet_tree(const group_state* g, tls_writer* w)
    {
        tls_writer nodes;
        nodes.init(2048);

        unsigned int width = mls_tree::node_width(g->leaf_count);
        for (unsigned int node = 0; node < width; node++)
        {
            if (mls_tree::is_leaf(node))
            {
                unsigned int leaf_index = mls_tree::node_to_leaf(node);
                if (leaf_index < g->leaf_count && g->leaf_used[leaf_index])
                {
                    nodes.u8(1);            // optional present
                    nodes.u8(NODE_LEAF);
                    g->leaves[leaf_index].write(&nodes);
                    continue;
                }
            }
            else if (node < MAX_NODES && g->parents[node].used)
            {
                // A joiner rebuilds the tree from this, so a parent that has
                // been rekeyed has to travel with it or their tree hash will
                // not match ours.
                nodes.u8(1);
                nodes.u8(NODE_PARENT);
                write_parent_node(&g->parents[node], &nodes);
                continue;
            }
            write_optional_absent(&nodes);  // blank leaf or blank parent
        }

        w->opaque(nodes);
        nodes.free_writer();
    }

    void derive_epoch_secrets(group_state* g)
    {
        crypto::mls_derive_secret(g->epoch_secret, "sender data", g->sender_data_secret);
        crypto::mls_derive_secret(g->epoch_secret, "encryption", g->encryption_secret);
        crypto::mls_derive_secret(g->epoch_secret, "exporter", g->exporter_secret);
        crypto::mls_derive_secret(g->epoch_secret, "confirm", g->confirmation_key);
        crypto::mls_derive_secret(g->epoch_secret, "membership", g->membership_key);
        crypto::mls_derive_secret(g->epoch_secret, "init", g->init_secret);
    }

    // One turn of the key schedule, RFC 9420 section 8.1. commit_secret is all
    // zeroes when the commit carries no UpdatePath.
    void advance_key_schedule(group_state* g,
                              const unsigned char init_secret_prev[32],
                              const unsigned char commit_secret[32],
                              const unsigned char* group_context, unsigned int group_context_len,
                              unsigned char out_joiner_secret[32],
                              unsigned char out_welcome_secret[32])
    {
        unsigned char prk[32];
        crypto::hkdf_extract(init_secret_prev, 32, commit_secret, 32, prk);
        crypto::mls_expand_with_label(prk, "joiner", group_context, group_context_len,
                                      out_joiner_secret, 32);

        // No pre-shared keys are ever used, so psk_secret is a zero string.
        unsigned char psk_secret[32];
        ccfset(psk_secret, 0, sizeof(psk_secret));

        unsigned char member_prk[32];
        crypto::hkdf_extract(out_joiner_secret, 32, psk_secret, 32, member_prk);

        crypto::mls_expand_with_label(member_prk, "welcome", 0, 0, out_welcome_secret, 32);
        crypto::mls_expand_with_label(member_prk, "epoch", group_context, group_context_len,
                                      g->epoch_secret, 32);

        derive_epoch_secrets(g);

        ccfset(prk, 0, sizeof(prk));
        ccfset(member_prk, 0, sizeof(member_prk));
    }

    unsigned int allocate_leaf(group_state* g)
    {
        for (unsigned int i = 0; i < g->leaf_count; i++)
            if (!g->leaf_used[i]) return i;

        if (g->leaf_count >= MAX_MEMBERS) return 0xFFFFFFFF;
        return g->leaf_count++;
    }
}

// ---------------------------------------------------------------------------

bool create_group(group_state* g,
                  const unsigned char group_id[], unsigned int group_id_len,
                  const leaf_node* my_leaf,
                  const unsigned char signature_private[96],
                  const unsigned char encryption_private[hpke::NSK])
{
    if (group_id_len > sizeof(g->group_id)) return false;

    ccfset(g, 0, sizeof(*g));

    ccpy(g->group_id, group_id, group_id_len);
    g->group_id_len = group_id_len;
    g->epoch = 0;

    g->leaves[0] = *my_leaf;
    g->leaf_used[0] = true;
    g->leaf_count = 1;
    g->my_leaf = 0;

    ccpy(g->my_signature_private, signature_private, 96);
    ccpy(g->my_encryption_private, encryption_private, hpke::NSK);

    // A brand new group starts with empty transcript hashes.
    g->confirmed_transcript_hash_len = 0;
    g->interim_transcript_hash_len = 0;

    compute_tree_hash(g, g->tree_hash);

    unsigned char init_prev[32];
    crypto::random_bytes(init_prev, sizeof(init_prev));

    unsigned char commit_secret[32];
    ccfset(commit_secret, 0, sizeof(commit_secret));

    tls_writer ctx;
    ctx.init(512);
    write_group_context_body(g, &ctx);

    unsigned char joiner[32], welcome[32];
    advance_key_schedule(g, init_prev, commit_secret, ctx.data(), ctx.size(), joiner, welcome);
    ctx.free_writer();

    ccfset(init_prev, 0, sizeof(init_prev));
    ccfset(joiner, 0, sizeof(joiner));
    ccfset(welcome, 0, sizeof(welcome));

    g->established = true;
    return true;
}

void compute_tree_hash(const group_state* g, unsigned char out[32])
{
    tree_hash_node(g, mls_tree::root(g->leaf_count ? g->leaf_count : 1), out);
}

void write_group_context(const group_state* g, tls_writer* w)
{
    write_group_context_body(g, w);
}

bool export_secret(const group_state* g, const char* label,
                   const void* context, unsigned int context_len,
                   unsigned char* out, unsigned int out_len)
{
    // MLS-Exporter(label, context, length) =
    //   ExpandWithLabel(DeriveSecret(exporter_secret, label), "exported",
    //                   Hash(context), length)
    unsigned char base[32];
    if (!crypto::mls_derive_secret(g->exporter_secret, label, base)) return false;

    unsigned char context_hash[32];
    crypto::sha256(context, context_len, context_hash);

    return crypto::mls_expand_with_label(base, "exported", context_hash, 32, out, out_len);
}

// ---------------------------------------------------------------------------
// commit
// ---------------------------------------------------------------------------

namespace
{
    struct pending_add
    {
        key_package kp;
        unsigned int leaf_index;
        unsigned char ref[32];
    };

    void write_framed_content_commit(const group_state* g,
                                     const pending_add* adds, unsigned int add_count,
                                     tls_writer* w)
    {
        w->opaque(g->group_id, g->group_id_len);
        w->u64(g->epoch);                       // the epoch the commit is sent in
        w->u8(SENDER_MEMBER);
        w->u32(g->my_leaf);
        w->opaque(0, 0);                        // authenticated_data
        w->u8(CONTENT_COMMIT);

        // Commit { ProposalOrRef proposals<V>; optional<UpdatePath> path; }
        tls_writer proposals;
        proposals.init(1024);
        for (unsigned int i = 0; i < add_count; i++)
        {
            proposals.u8(2);                    // ProposalOrRefType: reference
            proposals.opaque(adds[i].ref, 32);
        }
        w->opaque(proposals);
        proposals.free_writer();

        // Adds alone do not require an UpdatePath.
        write_optional_absent(w);
    }

    // FramedContentTBS: version, wire format, content, then the group context
    // for member senders.
    void write_framed_content_tbs(const group_state* g,
                                  const unsigned char* content, unsigned int content_len,
                                  tls_writer* w)
    {
        w->u16(PROTOCOL_VERSION_MLS10);
        w->u16(WIRE_PUBLIC_MESSAGE);
        w->raw(content, content_len);
        write_group_context_body(g, w);
    }
}

bool build_commit(group_state* g,
                  const proposal_message* proposals, unsigned int proposal_count,
                  ubuffer* out_commit, ubuffer* out_welcome)
{
    if (!g->established) return false;

    // ---- collect the adds ----
    pending_add adds[MAX_MEMBERS];
    unsigned int add_count = 0;

    for (unsigned int i = 0; i < proposal_count; i++)
    {
        const proposal* p = &proposals[i].prop;
        if (p->type != PROPOSAL_ADD) continue;
        if (add_count >= MAX_MEMBERS) break;

        if (!p->add.verify())
        {
            log_line("mls: rejecting an add whose key package does not verify");
            continue;
        }

        adds[add_count].kp = p->add;
        // The reference covers the whole AuthenticatedContent, not the proposal.
        proposals[i].compute_ref(adds[add_count].ref);
        add_count++;
    }

    if (add_count == 0)
    {
        log_line("mls: nothing to commit");
        return false;
    }

    // ---- snapshot what the commit is signed against ----
    unsigned long long sending_epoch = g->epoch;
    unsigned char init_prev[32];
    ccpy(init_prev, g->init_secret, 32);

    unsigned char membership_key_prev[32];
    ccpy(membership_key_prev, g->membership_key, 32);

    tls_writer content;
    content.init(1024);

    // ---- apply the adds to the tree ----
    for (unsigned int i = 0; i < add_count; i++)
    {
        unsigned int leaf = allocate_leaf(g);
        if (leaf == 0xFFFFFFFF) return false;

        g->leaves[leaf] = adds[i].kp.leaf;
        g->leaf_used[leaf] = true;
        adds[i].leaf_index = leaf;
    }

    // The FramedContent is built against the epoch being left behind.
    unsigned long long saved_epoch = g->epoch;
    g->epoch = sending_epoch;
    write_framed_content_commit(g, adds, add_count, &content);
    g->epoch = saved_epoch;

    // ---- sign the commit against the old group context ----
    unsigned char old_tree_hash[32];
    ccpy(old_tree_hash, g->tree_hash, 32);

    tls_writer tbs;
    tbs.init(1024);
    write_framed_content_tbs(g, content.data(), content.size(), &tbs);

    unsigned char signature[crypto::P256_SIGNATURE_MAX_BYTES];
    unsigned int signature_len = 0;
    bool ok = sign_with_label(g->my_signature_private, "FramedContentTBS",
                              tbs.data(), tbs.size(), signature, &signature_len);
    if (!ok)
    {
        content.free_writer();
        tbs.free_writer();
        return false;
    }

    // ---- advance the epoch ----
    g->epoch = sending_epoch + 1;
    compute_tree_hash(g, g->tree_hash);

    // confirmed_transcript_hash = Hash(interim_prev || wire_format || content || signature)
    {
        tls_writer input;
        input.init(1024);
        input.raw(g->interim_transcript_hash, g->interim_transcript_hash_len);
        input.u16(WIRE_PUBLIC_MESSAGE);
        input.raw(content.data(), content.size());
        input.opaque(signature, signature_len);

        crypto::sha256(input.data(), input.size(), g->confirmed_transcript_hash);
        g->confirmed_transcript_hash_len = 32;
        input.free_writer();
    }

    tls_writer new_context;
    new_context.init(512);
    write_group_context_body(g, &new_context);

    unsigned char commit_secret[32];
    ccfset(commit_secret, 0, sizeof(commit_secret));   // no UpdatePath

    unsigned char joiner_secret[32];
    unsigned char welcome_secret[32];
    advance_key_schedule(g, init_prev, commit_secret,
                         new_context.data(), new_context.size(),
                         joiner_secret, welcome_secret);

    // confirmation_tag = MAC(confirmation_key, confirmed_transcript_hash)
    unsigned char confirmation_tag[32];
    crypto::hmac_sha256(g->confirmation_key, 32,
                        g->confirmed_transcript_hash, g->confirmed_transcript_hash_len,
                        confirmation_tag);

    // interim_transcript_hash = Hash(confirmed || confirmation_tag)
    {
        tls_writer input;
        input.init(128);
        input.raw(g->confirmed_transcript_hash, g->confirmed_transcript_hash_len);
        input.opaque(confirmation_tag, 32);
        crypto::sha256(input.data(), input.size(), g->interim_transcript_hash);
        g->interim_transcript_hash_len = 32;
        input.free_writer();
    }

    // ---- membership tag, computed with the previous epoch's key ----
    unsigned char membership_tag[32];
    {
        tls_writer tbm;
        tbm.init(1024);
        tbm.raw(tbs.data(), tbs.size());
        tbm.opaque(signature, signature_len);
        tbm.opaque(confirmation_tag, 32);

        crypto::hmac_sha256(membership_key_prev, 32, tbm.data(), tbm.size(), membership_tag);
        tbm.free_writer();
    }

    // ---- assemble the MLSMessage carrying the commit ----
    out_commit->clear();
    {
        tls_writer msg;
        msg.init(2048);
        msg.u16(PROTOCOL_VERSION_MLS10);
        msg.u16(WIRE_PUBLIC_MESSAGE);
        msg.raw(content.data(), content.size());
        msg.opaque(signature, signature_len);
        msg.opaque(confirmation_tag, 32);
        msg.opaque(membership_tag, 32);

        out_commit->append(msg.data(), msg.size());
        msg.free_writer();
    }

    // ---- welcome ----
    ok = true;
    out_welcome->clear();
    {
        // GroupInfo, signed by us and encrypted under the welcome secret.
        tls_writer group_info_tbs;
        group_info_tbs.init(4096);
        write_group_context_body(g, &group_info_tbs);

        tls_writer extensions;
        extensions.init(4096);
        extensions.u16(EXTENSION_RATCHET_TREE);
        {
            tls_writer tree;
            tree.init(4096);
            write_ratchet_tree(g, &tree);
            extensions.opaque(tree);
            tree.free_writer();
        }
        group_info_tbs.opaque(extensions);
        extensions.free_writer();

        group_info_tbs.opaque(confirmation_tag, 32);
        group_info_tbs.u32(g->my_leaf);

        unsigned char info_signature[crypto::P256_SIGNATURE_MAX_BYTES];
        unsigned int info_signature_len = 0;
        ok = sign_with_label(g->my_signature_private, "GroupInfoTBS",
                             group_info_tbs.data(), group_info_tbs.size(),
                             info_signature, &info_signature_len);

        tls_writer group_info;
        group_info.init(4096);
        group_info.raw(group_info_tbs.data(), group_info_tbs.size());
        group_info.opaque(info_signature, info_signature_len);

        // welcome_key / welcome_nonce protect the group info.
        unsigned char welcome_key[hpke::NK];
        unsigned char welcome_nonce[hpke::NN];
        crypto::mls_expand_with_label(welcome_secret, "key", 0, 0, welcome_key, hpke::NK);
        crypto::mls_expand_with_label(welcome_secret, "nonce", 0, 0, welcome_nonce, hpke::NN);

        // encrypted_group_info is ciphertext followed by tag, as one blob: it
        // is both the welcome field and the HPKE context for the secrets.
        unsigned int encrypted_info_len = group_info.size() + 16;
        unsigned char* encrypted_info = (unsigned char*)memalloc((int)encrypted_info_len);
        ok = ok && encrypted_info &&
             crypto::aesgcm_encrypt(welcome_key, hpke::NK, welcome_nonce, hpke::NN, 0, 0,
                                    group_info.data(), group_info.size(),
                                    encrypted_info, encrypted_info + group_info.size(), 16);

        tls_writer welcome;
        welcome.init(4096);
        welcome.u16(CIPHERSUITE_P256);

        // secrets<V>: one EncryptedGroupSecrets per added member.
        tls_writer secrets;
        secrets.init(2048);

        for (unsigned int i = 0; i < add_count && ok; i++)
        {
            unsigned char kp_ref[32];
            adds[i].kp.compute_ref(kp_ref);

            tls_writer group_secrets;
            group_secrets.init(256);
            group_secrets.opaque(joiner_secret, 32);
            write_optional_absent(&group_secrets);   // no path secret without a path
            group_secrets.opaque(0, 0);              // psks<V>

            // EncryptWithLabel(init_key, "Welcome", encrypted_group_info, GroupSecrets)
            tls_writer encrypt_context;
            encrypt_context.init(encrypted_info_len + 128);
            encrypt_context.opaque("MLS 1.0 Welcome", 15);
            encrypt_context.opaque(encrypted_info, encrypted_info_len);

            unsigned char enc[hpke::NENC];
            unsigned char* cipher = (unsigned char*)memalloc((int)group_secrets.size());
            unsigned char tag[hpke::NT];

            bool sealed = cipher && hpke::seal_single(adds[i].kp.init_key,
                                                      encrypt_context.data(), encrypt_context.size(),
                                                      0, 0,
                                                      group_secrets.data(), group_secrets.size(),
                                                      enc, cipher, tag);
            if (sealed)
            {
                secrets.opaque(kp_ref, 32);
                secrets.opaque(enc, hpke::NENC);

                tls_writer ct;
                ct.init(group_secrets.size() + 32);
                ct.raw(cipher, group_secrets.size());
                ct.raw(tag, hpke::NT);
                secrets.opaque(ct);
                ct.free_writer();
            }
            else
            {
                ok = false;
            }

            if (cipher) memfree(cipher);
            encrypt_context.free_writer();
            group_secrets.free_writer();
        }

        welcome.opaque(secrets);
        if (encrypted_info) welcome.opaque(encrypted_info, encrypted_info_len);

        if (ok)
        {
            tls_writer msg;
            msg.init(welcome.size() + 64);
            msg.u16(PROTOCOL_VERSION_MLS10);
            msg.u16(WIRE_WELCOME);
            msg.raw(welcome.data(), welcome.size());
            out_welcome->append(msg.data(), msg.size());
            msg.free_writer();
        }

        if (encrypted_info) memfree(encrypted_info);
        secrets.free_writer();
        welcome.free_writer();
        group_info.free_writer();
        group_info_tbs.free_writer();
        ccfset(welcome_key, 0, sizeof(welcome_key));
    }

    log_line("mls: commit for epoch %llu with %u add(s), commit %u bytes, welcome %u bytes",
             g->epoch, add_count, out_commit->size, out_welcome->size);

    content.free_writer();
    tbs.free_writer();
    new_context.free_writer();
    ccfset(init_prev, 0, sizeof(init_prev));
    ccfset(joiner_secret, 0, sizeof(joiner_secret));
    ccfset(welcome_secret, 0, sizeof(welcome_secret));
    ccfset(membership_key_prev, 0, sizeof(membership_key_prev));

    return ok;
}

// ---------------------------------------------------------------------------
// joining from a welcome
// ---------------------------------------------------------------------------

namespace
{
    // Rebuilds the leaf array from a ratchet_tree extension. Parent entries are
    // skipped: this client keeps no parent state.
    bool read_ratchet_tree(group_state* g, const unsigned char* data, unsigned int len)
    {
        tls_reader r;
        r.init(data, len);

        const unsigned char* nodes = 0;
        unsigned int nodes_len = 0;
        if (!r.opaque(&nodes, &nodes_len)) return false;

        tls_reader nr;
        nr.init(nodes, nodes_len);

        unsigned int node_index = 0;
        g->leaf_count = 0;
        for (unsigned int i = 0; i < MAX_NODES; i++) g->parents[i].used = false;

        while (nr.remaining() > 0)
        {
            unsigned char present = 0;
            if (!nr.u8(&present)) return false;

            if (present)
            {
                unsigned char type = 0;
                if (!nr.u8(&type)) return false;

                if (type == NODE_LEAF)
                {
                    unsigned int leaf_index = mls_tree::node_to_leaf(node_index);
                    if (leaf_index >= MAX_MEMBERS) return false;

                    if (!g->leaves[leaf_index].read(&nr)) return false;
                    g->leaf_used[leaf_index] = true;
                    if (leaf_index + 1 > g->leaf_count) g->leaf_count = leaf_index + 1;
                }
                else
                {
                    // ParentNode: encryption_key, parent_hash, unmerged_leaves.
                    // These used to be skipped, which was only ever right while
                    // no member sent an UpdatePath. A tree with real parents
                    // hashes differently, and dropping them here made the whole
                    // group look wrong from this side.
                    const unsigned char* key = 0;
                    unsigned int key_len = 0;
                    const unsigned char* hash = 0;
                    unsigned int hash_len = 0;
                    const unsigned char* unmerged = 0;
                    unsigned int unmerged_len = 0;

                    if (!nr.opaque(&key, &key_len)) return false;
                    if (!nr.opaque(&hash, &hash_len)) return false;
                    if (!nr.opaque(&unmerged, &unmerged_len)) return false;

                    if (node_index < MAX_NODES && key_len == 65 && hash_len <= 32 &&
                        (unmerged_len % 4) == 0)
                    {
                        parent_node* pn = &g->parents[node_index];
                        ccfset(pn, 0, sizeof(*pn));
                        ccpy(pn->encryption_key, key, 65);
                        ccpy(pn->parent_hash, hash, hash_len);
                        pn->parent_hash_len = hash_len;

                        unsigned int n = unmerged_len / 4;
                        if (n > MAX_UNMERGED) return false;
                        for (unsigned int k = 0; k < n; k++)
                        {
                            const unsigned char* e = unmerged + k * 4;
                            pn->unmerged[k] = ((unsigned int)e[0] << 24) | ((unsigned int)e[1] << 16) |
                                              ((unsigned int)e[2] << 8) | e[3];
                        }
                        pn->unmerged_count = n;
                        pn->used = true;
                    }
                }
            }
            else if (mls_tree::is_leaf(node_index))
            {
                unsigned int leaf_index = mls_tree::node_to_leaf(node_index);
                if (leaf_index < MAX_MEMBERS)
                {
                    g->leaf_used[leaf_index] = false;
                    if (leaf_index + 1 > g->leaf_count) g->leaf_count = leaf_index + 1;
                }
            }

            node_index++;
        }

        return g->leaf_count > 0;
    }
}

bool process_welcome(group_state* g,
                     const void* welcome, unsigned int welcome_len,
                     const key_package* my_key_package,
                     const key_package_private* my_private,
                     const unsigned char signature_private[96])
{
    tls_reader r;
    r.init(welcome, welcome_len);

    // Discord hands over the bare Welcome struct: the transition id has already
    // been stripped by the caller and there is no MLSMessage wrapper.
    unsigned short suite = 0;
    if (!r.u16(&suite) || suite != CIPHERSUITE_P256)
    {
        log_line("mls: welcome has an unexpected ciphersuite");
        return false;
    }

    const unsigned char* secrets = 0;
    unsigned int secrets_len = 0;
    if (!r.opaque(&secrets, &secrets_len)) return false;

    const unsigned char* encrypted_info = 0;
    unsigned int encrypted_info_len = 0;
    if (!r.opaque(&encrypted_info, &encrypted_info_len)) return false;
    if (encrypted_info_len < 16) return false;

    // ---- find the entry addressed to our key package ----
    unsigned char my_ref[32];
    my_key_package->compute_ref(my_ref);

    unsigned char joiner_secret[32];
    bool found = false;

    tls_reader sr;
    sr.init(secrets, secrets_len);

    while (sr.remaining() > 0 && !found)
    {
        const unsigned char* ref = 0;
        unsigned int ref_len = 0;
        if (!sr.opaque(&ref, &ref_len)) return false;

        const unsigned char* kem_output = 0;
        unsigned int kem_len = 0;
        if (!sr.opaque(&kem_output, &kem_len)) return false;

        const unsigned char* ciphertext = 0;
        unsigned int cipher_len = 0;
        if (!sr.opaque(&ciphertext, &cipher_len)) return false;

        if (ref_len != 32 || kem_len != hpke::NENC || cipher_len <= hpke::NT) continue;

        bool same = true;
        for (int i = 0; i < 32 && same; i++) same = ref[i] == my_ref[i];
        if (!same) continue;

        // EncryptWithLabel(init_key, "Welcome", encrypted_group_info, GroupSecrets)
        tls_writer context;
        context.init(encrypted_info_len + 128);
        context.opaque("MLS 1.0 Welcome", 15);
        context.opaque(encrypted_info, encrypted_info_len);

        unsigned int plain_len = cipher_len - hpke::NT;
        unsigned char* plain = (unsigned char*)memalloc((int)plain_len);

        bool opened = plain && hpke::open_single(kem_output, my_private->init_private,
                                                 context.data(), context.size(),
                                                 0, 0,
                                                 ciphertext, plain_len,
                                                 ciphertext + plain_len, plain);
        context.free_writer();

        if (opened)
        {
            tls_reader gs;
            gs.init(plain, plain_len);

            const unsigned char* js = 0;
            unsigned int js_len = 0;
            if (gs.opaque(&js, &js_len) && js_len == 32)
            {
                ccpy(joiner_secret, js, 32);
                found = true;
            }
        }

        if (plain) { ccfset(plain, 0, plain_len); memfree(plain); }
    }

    if (!found)
    {
        log_line("mls: welcome carries no secrets for this client");
        return false;
    }

    // ---- unwrap the group info ----
    unsigned char psk_secret[32];
    ccfset(psk_secret, 0, sizeof(psk_secret));

    unsigned char member_prk[32];
    crypto::hkdf_extract(joiner_secret, 32, psk_secret, 32, member_prk);

    unsigned char welcome_secret[32];
    crypto::mls_expand_with_label(member_prk, "welcome", 0, 0, welcome_secret, 32);

    unsigned char welcome_key[hpke::NK];
    unsigned char welcome_nonce[hpke::NN];
    crypto::mls_expand_with_label(welcome_secret, "key", 0, 0, welcome_key, hpke::NK);
    crypto::mls_expand_with_label(welcome_secret, "nonce", 0, 0, welcome_nonce, hpke::NN);

    unsigned int info_len = encrypted_info_len - 16;
    unsigned char* group_info = (unsigned char*)memalloc((int)info_len);
    if (!group_info) return false;

    bool ok = crypto::aesgcm_decrypt(welcome_key, hpke::NK, welcome_nonce, hpke::NN, 0, 0,
                                     encrypted_info, info_len,
                                     encrypted_info + info_len, 16, group_info);
    if (!ok)
    {
        log_line("mls: group info did not decrypt");
        memfree(group_info);
        return false;
    }

    // ---- parse the group info ----
    tls_reader gi;
    gi.init(group_info, info_len);

    unsigned int context_start = gi.pos;

    unsigned short ctx_version = 0, ctx_suite = 0;
    const unsigned char* gid = 0;
    unsigned int gid_len = 0;
    unsigned long long epoch = 0;
    const unsigned char* tree_hash = 0;
    unsigned int tree_hash_len = 0;
    const unsigned char* confirmed = 0;
    unsigned int confirmed_len = 0;
    const unsigned char* ctx_ext = 0;
    unsigned int ctx_ext_len = 0;

    ok = gi.u16(&ctx_version) && gi.u16(&ctx_suite) &&
         gi.opaque(&gid, &gid_len) && gi.u64(&epoch) &&
         gi.opaque(&tree_hash, &tree_hash_len) &&
         gi.opaque(&confirmed, &confirmed_len) &&
         gi.opaque(&ctx_ext, &ctx_ext_len);

    unsigned int context_len = gi.pos - context_start;

    if (!ok || gid_len > sizeof(g->group_id) || tree_hash_len != 32 || confirmed_len > 32)
    {
        memfree(group_info);
        return false;
    }

    // GroupInfo extensions carry the ratchet tree.
    const unsigned char* gi_ext = 0;
    unsigned int gi_ext_len = 0;
    const unsigned char* confirmation_tag = 0;
    unsigned int confirmation_tag_len = 0;
    unsigned int signer = 0;

    ok = gi.opaque(&gi_ext, &gi_ext_len) &&
         gi.opaque(&confirmation_tag, &confirmation_tag_len) &&
         gi.u32(&signer);

    if (!ok || confirmation_tag_len != 32)
    {
        memfree(group_info);
        return false;
    }

    // Sanity markers: if the group info decrypted correctly these describe the
    // real group, and if it did not they will be obvious nonsense.
    log_line("mls: group info epoch %llu, group id %u bytes, %u bytes of extensions",
             epoch, gid_len, gi_ext_len);

    // ---- rebuild the state ----
    ccfset(g->leaf_used, 0, sizeof(g->leaf_used));
    g->leaf_count = 0;

    bool have_tree = false;
    {
        tls_reader er;
        er.init(gi_ext, gi_ext_len);

        while (er.remaining() > 0)
        {
            unsigned short type = 0;
            const unsigned char* data = 0;
            unsigned int data_len = 0;
            if (!er.u16(&type) || !er.opaque(&data, &data_len)) break;

            log_line("mls:   group info extension 0x%04X, %u bytes", type, data_len);

            if (type == EXTENSION_RATCHET_TREE)
            {
                // extension_data already starts with the vector's own length
                // prefix, so it is passed through untouched.
                have_tree = read_ratchet_tree(g, data, data_len);
                break;
            }
        }
    }

    if (!have_tree)
    {
        log_line("mls: welcome has no ratchet tree extension");
        memfree(group_info);
        return false;
    }

    ccpy(g->group_id, gid, gid_len);
    g->group_id_len = gid_len;
    g->epoch = epoch;
    ccpy(g->tree_hash, tree_hash, 32);
    ccpy(g->confirmed_transcript_hash, confirmed, confirmed_len);
    g->confirmed_transcript_hash_len = confirmed_len;

    ccpy(g->my_signature_private, signature_private, 96);
    ccpy(g->my_encryption_private, my_private->encryption_private, hpke::NSK);

    // Locate our own leaf by matching the signature key.
    unsigned char my_signature_public[65];
    crypto::p256_public_from_private(signature_private, my_signature_public);

    g->my_leaf = 0xFFFFFFFF;
    for (unsigned int i = 0; i < g->leaf_count; i++)
    {
        if (!g->leaf_used[i]) continue;
        bool same = true;
        for (int k = 0; k < 65 && same; k++) same = g->leaves[i].signature_key[k] == my_signature_public[k];
        if (same) { g->my_leaf = i; break; }
    }

    if (g->my_leaf == 0xFFFFFFFF)
    {
        log_line("mls: our leaf is missing from the welcome tree");
        memfree(group_info);
        return false;
    }

    // ---- key schedule for the epoch we joined ----
    crypto::mls_expand_with_label(member_prk, "epoch",
                                  group_info + context_start, context_len,
                                  g->epoch_secret, 32);
    derive_epoch_secrets(g);

    // The confirmation tag proves we derived the same secrets as the group.
    unsigned char expected_tag[32];
    crypto::hmac_sha256(g->confirmation_key, 32,
                        g->confirmed_transcript_hash, g->confirmed_transcript_hash_len,
                        expected_tag);

    bool tag_ok = true;
    for (int i = 0; i < 32 && tag_ok; i++) tag_ok = expected_tag[i] == confirmation_tag[i];

    // interim_transcript_hash = Hash(confirmed || confirmation_tag)
    {
        tls_writer input;
        input.init(128);
        input.raw(g->confirmed_transcript_hash, g->confirmed_transcript_hash_len);
        input.opaque(confirmation_tag, 32);
        crypto::sha256(input.data(), input.size(), g->interim_transcript_hash);
        g->interim_transcript_hash_len = 32;
        input.free_writer();
    }

    g->established = true;

    log_line("mls: joined epoch %llu, leaf %u of %u, confirmation tag %s",
             g->epoch, g->my_leaf, g->leaf_count, tag_ok ? "ok" : "MISMATCH");

    ccfset(joiner_secret, 0, sizeof(joiner_secret));
    ccfset(member_prk, 0, sizeof(member_prk));
    ccfset(welcome_secret, 0, sizeof(welcome_secret));
    ccfset(welcome_key, 0, sizeof(welcome_key));
    ccfset(group_info, 0, info_len);
    memfree(group_info);

    return tag_ok;
}

} // namespace mls

// ---------------------------------------------------------------------------
// TreeKEM
// ---------------------------------------------------------------------------

namespace mls
{
namespace
{
    // The non-blank nodes that between them cover every non-blank descendant.
    //
    // A non-blank parent covers its whole subtree by itself, except for the
    // leaves added under it since it was last rekeyed: those were never given
    // its key, so they have to be listed separately or a sender would encrypt
    // to a node they cannot open.
    unsigned int resolution(const group_state* g, unsigned int node,
                            unsigned int* out, unsigned int cap)
    {
        if (cap == 0) return 0;
        if (node >= mls_tree::node_width(g->leaf_count)) return 0;

        if (mls_tree::is_leaf(node))
        {
            unsigned int leaf = mls_tree::node_to_leaf(node);
            if (leaf < g->leaf_count && g->leaf_used[leaf]) { out[0] = node; return 1; }
            return 0;
        }

        if (node < MAX_NODES && g->parents[node].used)
        {
            const parent_node* p = &g->parents[node];
            unsigned int n = 0;
            out[n++] = node;
            for (unsigned int i = 0; i < p->unmerged_count && n < cap; i++)
                out[n++] = mls_tree::leaf_to_node(p->unmerged[i]);
            return n;
        }

        unsigned int n = resolution(g, mls_tree::left(node), out, cap);
        n += resolution(g, mls_tree::right(node), out + n, cap - n);
        return n;
    }

    // The sender's direct path with the levels removed where the copath child
    // covers nobody. Those carry no ciphertext, so the path in the message is
    // shorter than the full direct path and the two only line up once the same
    // filtering is applied here.
    unsigned int filtered_direct_path(const group_state* g, unsigned int from_node,
                                      unsigned int* out_path, unsigned int* out_copath,
                                      unsigned int cap)
    {
        unsigned int path[MAX_NODES];
        unsigned int copath[MAX_NODES];

        unsigned int n = mls_tree::direct_path(from_node, g->leaf_count, path, MAX_NODES);
        unsigned int m = mls_tree::copath(from_node, g->leaf_count, copath, MAX_NODES);
        if (n != m) return 0;

        unsigned int kept = 0;
        unsigned int scratch[MAX_NODES];

        for (unsigned int i = 0; i < n && kept < cap; i++)
        {
            if (resolution(g, copath[i], scratch, MAX_NODES) == 0) continue;
            out_path[kept] = path[i];
            out_copath[kept] = copath[i];
            kept++;
        }
        return kept;
    }

    // ParentHashInput { HPKEPublicKey encryption_key; opaque parent_hash<V>;
    //                   opaque original_sibling_tree_hash<V>; }
    //
    // Walked from the root down the sender's path: every node on it has just
    // been rekeyed, so none of them has unmerged leaves and the sibling hash is
    // simply that subtree's tree hash.
    void recompute_parent_hashes(group_state* g, const unsigned int* path,
                                 unsigned int count)
    {
        for (unsigned int i = count; i > 0; i--)
        {
            unsigned int node = path[i - 1];
            if (node >= MAX_NODES || !g->parents[node].used) continue;

            if (i == count)
            {
                // The topmost node on the path is the root: nothing above it.
                g->parents[node].parent_hash_len = 0;
                continue;
            }

            unsigned int above = path[i];
            unsigned int sibling = mls_tree::sibling(node);

            unsigned char sibling_hash[32];
            tree_hash_node(g, sibling, sibling_hash);

            tls_writer w;
            w.init(256);
            w.opaque(g->parents[above].encryption_key, 65);
            w.opaque(g->parents[above].parent_hash, g->parents[above].parent_hash_len);
            w.opaque(sibling_hash, 32);

            crypto::sha256(w.data(), w.size(), g->parents[node].parent_hash);
            g->parents[node].parent_hash_len = 32;
            w.free_writer();
        }
    }

    // EncryptContext { opaque label<V>; opaque context<V>; }, which is what
    // HPKE takes as its info. The label is spelled out rather than built from
    // pieces because getting it wrong fails silently, as a frame that will not
    // open somewhere else entirely.
    void build_encrypt_info(const unsigned char* context, unsigned int context_len,
                            tls_writer* out)
    {
        const char* label = "MLS 1.0 UpdatePathNode";
        out->opaque(label, (unsigned int)ccslenf(label));
        out->opaque(context, context_len);
    }

    struct update_path_node
    {
        unsigned char encryption_key[65];
        // One per entry in the resolution of the copath child, in that order.
        const unsigned char* kem[MAX_UNMERGED + 2];
        const unsigned char* cipher[MAX_UNMERGED + 2];
        unsigned int cipher_len[MAX_UNMERGED + 2];
        unsigned int count;
    };

// Reads an UpdatePath, merges its public keys into the tree, and works out the
// commit secret from the one path secret this member is able to open.
bool apply_update_path(group_state* g, unsigned int sender_leaf,
                              tls_reader* r,
                              unsigned long long sending_epoch,
                              unsigned char commit_secret[32],
                              const char** out_error)
{
    // ---- the sender's new leaf ----
    leaf_node fresh_leaf;
    if (!fresh_leaf.read(r)) { *out_error = "лист в UpdatePath не разбирается"; return false; }

    const unsigned char* nodes_bytes = 0;
    unsigned int nodes_len = 0;
    if (!r->opaque(&nodes_bytes, &nodes_len))
    { *out_error = "узлы UpdatePath не читаются"; return false; }

    // ---- the path nodes ----
    update_path_node path_nodes[MAX_NODES];
    unsigned int path_node_count = 0;

    tls_reader nr;
    nr.init(nodes_bytes, nodes_len);

    while (nr.remaining() > 0)
    {
        if (path_node_count >= MAX_NODES) { *out_error = "слишком длинный путь"; return false; }

        update_path_node* node = &path_nodes[path_node_count];
        ccfset(node, 0, sizeof(*node));

        const unsigned char* key = 0;
        unsigned int key_len = 0;
        if (!nr.opaque(&key, &key_len) || key_len != 65)
        { *out_error = "ключ узла пути не 65 байт"; return false; }
        ccpy(node->encryption_key, key, 65);

        const unsigned char* cts = 0;
        unsigned int cts_len = 0;
        if (!nr.opaque(&cts, &cts_len))
        { *out_error = "шифротексты узла не читаются"; return false; }

        tls_reader cr;
        cr.init(cts, cts_len);
        while (cr.remaining() > 0)
        {
            if (node->count >= MAX_UNMERGED + 2)
            { *out_error = "слишком много шифротекстов"; return false; }

            const unsigned char* kem = 0;
            unsigned int kem_len = 0;
            const unsigned char* body = 0;
            unsigned int body_len = 0;
            if (!cr.opaque(&kem, &kem_len) || kem_len != 65 ||
                !cr.opaque(&body, &body_len) || body_len < hpke::NT + 1)
            { *out_error = "шифротекст узла битый"; return false; }

            node->kem[node->count] = kem;
            node->cipher[node->count] = body;
            node->cipher_len[node->count] = body_len;
            node->count++;
        }

        path_node_count++;
    }

    // ---- merge, so the tree is the one the path was built against ----
    if (sender_leaf >= MAX_MEMBERS) { *out_error = "отправитель вне дерева"; return false; }
    g->leaves[sender_leaf] = fresh_leaf;
    g->leaf_used[sender_leaf] = true;

    unsigned int sender_node = mls_tree::leaf_to_node(sender_leaf);

    unsigned int path[MAX_NODES];
    unsigned int copath[MAX_NODES];
    unsigned int steps = filtered_direct_path(g, sender_node, path, copath, MAX_NODES);

    if (steps != path_node_count)
    {
        *out_error = "путь не совпал с деревом";
        return false;
    }

    for (unsigned int i = 0; i < steps; i++)
    {
        unsigned int node = path[i];
        if (node >= MAX_NODES) { *out_error = "узел пути вне дерева"; return false; }

        parent_node* p = &g->parents[node];
        ccfset(p, 0, sizeof(*p));
        ccpy(p->encryption_key, path_nodes[i].encryption_key, 65);
        p->used = true;
        // Rekeyed, so everyone underneath can read it again.
        p->unmerged_count = 0;

        // Whatever we held for this node is stale the moment it is rekeyed.
        g->node_private_set[node] = false;
    }

    recompute_parent_hashes(g, path, steps);

    // ---- the provisional group context the sender encrypted against ----
    //
    // Next epoch, the tree hash as it stands now that the path is merged, and
    // the confirmed transcript hash from the epoch being left: the new one
    // cannot exist yet, because it covers this very message.
    unsigned char merged_tree_hash[32];
    compute_tree_hash(g, merged_tree_hash);

    tls_writer provisional;
    provisional.init(512);
    provisional.u16(PROTOCOL_VERSION_MLS10);
    provisional.u16(CIPHERSUITE_P256);
    provisional.opaque(g->group_id, g->group_id_len);
    provisional.u64(sending_epoch + 1);
    provisional.opaque(merged_tree_hash, 32);
    provisional.opaque(g->confirmed_transcript_hash, g->confirmed_transcript_hash_len);
    provisional.opaque(0, 0);   // extensions<V>

    tls_writer info;
    info.init(768);
    build_encrypt_info(provisional.data(), provisional.size(), &info);
    provisional.free_writer();

    // ---- find the one secret meant for us ----
    unsigned int my_node = mls_tree::leaf_to_node(g->my_leaf);
    unsigned int overlap = mls_tree::common_ancestor(sender_node, my_node);

    unsigned int step = steps;
    for (unsigned int i = 0; i < steps; i++)
        if (path[i] == overlap) { step = i; break; }

    if (step == steps)
    {
        info.free_writer();
        *out_error = "нас нет на пути отправителя";
        return false;
    }

    // Our position in the resolution of the copath child is the index of the
    // ciphertext addressed to us: the sender walked the same list in the same
    // order.
    unsigned int res[MAX_NODES];
    unsigned int res_count = resolution(g, copath[step], res, MAX_NODES);

    unsigned int slot = res_count;
    const unsigned char* my_private = 0;

    for (unsigned int i = 0; i < res_count; i++)
    {
        unsigned int node = res[i];
        if (node == my_node) { slot = i; my_private = g->my_encryption_private; break; }
        if (node < MAX_NODES && g->node_private_set[node])
        { slot = i; my_private = g->node_private[node]; break; }
    }

    if (slot >= res_count || slot >= path_nodes[step].count || !my_private)
    {
        info.free_writer();
        *out_error = "нет ключа ни для одного узла resolution";
        return false;
    }

    unsigned int body_len = path_nodes[step].cipher_len[slot];
    unsigned int secret_len = body_len - hpke::NT;
    if (secret_len != 32)
    {
        info.free_writer();
        *out_error = "path secret не 32 байта";
        return false;
    }

    unsigned char path_secret[32];
    bool opened = hpke::open_single(path_nodes[step].kem[slot], my_private,
                                    info.data(), info.size(), 0, 0,
                                    path_nodes[step].cipher[slot], secret_len,
                                    path_nodes[step].cipher[slot] + secret_len,
                                    path_secret);
    info.free_writer();

    if (!opened) { *out_error = "path secret не расшифровался"; return false; }

    // ---- ratchet the rest of the way to the root ----
    for (unsigned int i = step; i < steps; i++)
    {
        unsigned int node = path[i];

        unsigned char node_secret[32];
        if (!crypto::mls_derive_secret(path_secret, "node", node_secret))
        { *out_error = "не выводится секрет узла"; return false; }

        unsigned char pub[hpke::NPK];
        if (!hpke::derive_key_pair(node_secret, 32, pub, g->node_private[node]))
        { *out_error = "не выводится пара ключей узла"; return false; }

        // The key we derived has to be the one the sender announced, otherwise
        // the two of us are on different trees and everything after is noise.
        bool same = true;
        for (int k = 0; k < 65 && same; k++) same = pub[k] == path_nodes[i].encryption_key[k];
        if (!same)
        {
            g->node_private_set[node] = false;
            *out_error = "выведенный ключ узла не совпал с объявленным";
            return false;
        }
        g->node_private_set[node] = true;

        unsigned char next[32];
        if (!crypto::mls_derive_secret(path_secret, "path", next))
        { *out_error = "не выводится следующий path secret"; return false; }
        ccpy(path_secret, next, 32);
        ccfset(next, 0, sizeof(next));
    }

    // One more step past the root is what feeds the key schedule.
    ccpy(commit_secret, path_secret, 32);
    ccfset(path_secret, 0, sizeof(path_secret));
    return true;
}

}   // anonymous
}   // namespace mls

// ---------------------------------------------------------------------------
// applying somebody else's commit
// ---------------------------------------------------------------------------

namespace
{
    // ProposalOrRef.type
    const unsigned char PROPOSAL_OR_REF_PROPOSAL = 1;
    const unsigned char PROPOSAL_OR_REF_REFERENCE = 2;

    bool resolve_reference(const mls::cached_proposal* known, unsigned int known_count,
                           const unsigned char* ref, mls::proposal* out)
    {
        for (unsigned int i = 0; i < known_count; i++)
        {
            bool same = true;
            for (int k = 0; k < 32 && same; k++) same = known[i].ref[k] == ref[k];
            if (same) { *out = known[i].prop; return true; }
        }
        return false;
    }
}

bool mls::process_commit(group_state* g,
                         const void* message, unsigned int len,
                         const cached_proposal* known, unsigned int known_count,
                         const char** out_error)
{
    const char* ignored = 0;
    if (!out_error) out_error = &ignored;
    *out_error = "";

    if (!g || !g->established || !message) { *out_error = "нет группы"; return false; }

    const unsigned char* base = (const unsigned char*)message;

    tls_reader r;
    r.init(base, len);

    unsigned short version = 0, wire = 0;
    if (!r.u16(&version) || !r.u16(&wire)) { *out_error = "обрезанный MLSMessage"; return false; }
    if (version != PROTOCOL_VERSION_MLS10) { *out_error = "чужая версия MLS"; return false; }
    if (wire != WIRE_PUBLIC_MESSAGE) { *out_error = "коммит не PublicMessage"; return false; }

    // The transcript hashes cover the FramedContent exactly as it arrived, so
    // where it starts and ends has to be remembered rather than rebuilt.
    unsigned int content_start = r.pos;

    const unsigned char* gid = 0;
    unsigned int gid_len = 0;
    unsigned long long epoch = 0;
    if (!r.opaque(&gid, &gid_len) || !r.u64(&epoch)) { *out_error = "обрезанный заголовок"; return false; }

    if (gid_len != g->group_id_len) { *out_error = "другая группа"; return false; }
    for (unsigned int i = 0; i < gid_len; i++)
        if (gid[i] != g->group_id[i]) { *out_error = "другая группа"; return false; }

    // A commit is sent in the epoch it leaves behind. Anything else is either
    // one we have already applied or one from a future we cannot reach.
    if (epoch != g->epoch) { *out_error = "коммит для другой эпохи"; return false; }

    unsigned char sender_type = 0;
    if (!r.u8(&sender_type)) { *out_error = "обрезанный отправитель"; return false; }
    if (sender_type != SENDER_MEMBER) { *out_error = "коммит не от участника"; return false; }

    unsigned int sender_leaf = 0;
    if (!r.u32(&sender_leaf)) { *out_error = "обрезанный отправитель"; return false; }
    if (sender_leaf == g->my_leaf) { *out_error = "наш собственный коммит"; return false; }

    const unsigned char* auth_data = 0;
    unsigned int auth_data_len = 0;
    unsigned char content_kind = 0;
    if (!r.opaque(&auth_data, &auth_data_len) || !r.u8(&content_kind))
    { *out_error = "обрезанное содержимое"; return false; }
    if (content_kind != CONTENT_COMMIT) { *out_error = "это не коммит"; return false; }

    // ---- Commit { ProposalOrRef proposals<V>; optional<UpdatePath> path; } --
    const unsigned char* proposals_bytes = 0;
    unsigned int proposals_len = 0;
    if (!r.opaque(&proposals_bytes, &proposals_len))
    { *out_error = "обрезанный список предложений"; return false; }

    unsigned char has_path = 0;
    if (!r.u8(&has_path)) { *out_error = "обрезанный UpdatePath"; return false; }

    // The path has to be stepped over now, to find where the signed content
    // ends, but it cannot be applied until the proposals have been: it was
    // built against the tree they leave behind.
    unsigned int path_start = r.pos;
    unsigned int path_end = r.pos;

    if (has_path)
    {
        leaf_node skip_leaf;
        const unsigned char* skip_nodes = 0;
        unsigned int skip_len = 0;
        if (!skip_leaf.read(&r) || !r.opaque(&skip_nodes, &skip_len))
        { *out_error = "UpdatePath не разбирается"; return false; }
        path_end = r.pos;
    }

    unsigned int content_end = r.pos;

    // ---- FramedContentAuthData ---------------------------------------------
    const unsigned char* signature = 0;
    unsigned int signature_len = 0;
    const unsigned char* confirmation = 0;
    unsigned int confirmation_len = 0;
    if (!r.opaque(&signature, &signature_len) || !r.opaque(&confirmation, &confirmation_len))
    { *out_error = "обрезанная подпись"; return false; }
    if (confirmation_len != 32) { *out_error = "confirmation_tag не 32 байта"; return false; }

    // ---- gather the proposals ----------------------------------------------
    proposal applied[MAX_MEMBERS];
    unsigned int applied_count = 0;

    tls_reader pr;
    pr.init(proposals_bytes, proposals_len);

    while (pr.remaining() > 0)
    {
        unsigned char kind = 0;
        if (!pr.u8(&kind)) { *out_error = "предложение не читается"; return false; }
        if (applied_count >= MAX_MEMBERS) { *out_error = "слишком много предложений"; return false; }

        if (kind == PROPOSAL_OR_REF_PROPOSAL)
        {
            if (!read_proposal(&pr, &applied[applied_count]))
            { *out_error = "предложение не разбирается"; return false; }
        }
        else if (kind == PROPOSAL_OR_REF_REFERENCE)
        {
            const unsigned char* ref = 0;
            unsigned int ref_len = 0;
            if (!pr.opaque(&ref, &ref_len) || ref_len != 32)
            { *out_error = "ссылка на предложение битая"; return false; }

            if (!resolve_reference(known, known_count, ref, &applied[applied_count]))
            { *out_error = "предложение по ссылке неизвестно"; return false; }
        }
        else
        {
            *out_error = "неизвестный вид предложения";
            return false;
        }
        applied_count++;
    }

    // ---- everything is understood, so the state may now be changed ----------
    unsigned long long sending_epoch = g->epoch;

    unsigned char init_prev[32];
    ccpy(init_prev, g->init_secret, 32);

    for (unsigned int i = 0; i < applied_count; i++)
    {
        const proposal* p = &applied[i];

        if (p->type == PROPOSAL_ADD)
        {
            if (!p->add.verify())
            {
                log_line("mls: коммит добавляет участника с негодным key package");
                continue;
            }
            unsigned int leaf = allocate_leaf(g);
            if (leaf == 0xFFFFFFFF) { *out_error = "в дереве нет места"; return false; }
            g->leaves[leaf] = p->add.leaf;
            g->leaf_used[leaf] = true;
        }
        else if (p->type == PROPOSAL_REMOVE)
        {
            if (p->remove_index < MAX_MEMBERS)
            {
                g->leaf_used[p->remove_index] = false;
                if (p->remove_index == g->my_leaf)
                {
                    // Removed from the group. Nothing that follows is ours.
                    g->established = false;
                    *out_error = "нас удалили из группы";
                    return false;
                }
            }
        }
        else if (p->type == PROPOSAL_UPDATE)
        {
            if (sender_leaf < MAX_MEMBERS)
            {
                g->leaves[sender_leaf] = p->update_leaf;
                g->leaf_used[sender_leaf] = true;
            }
        }
    }

    // No UpdatePath means no path secret, so the commit secret is a zero
    // string; one that carries a path rekeys the tree and produces a real one.
    unsigned char commit_secret[32];
    ccfset(commit_secret, 0, sizeof(commit_secret));

    if (has_path)
    {
        tls_reader pr;
        pr.init(base + path_start, path_end - path_start);

        if (!apply_update_path(g, sender_leaf, &pr, sending_epoch, commit_secret, out_error))
        {
            // The tree is half rewritten by now and nothing downstream could
            // trust it, so the group is marked broken rather than left looking
            // healthy while deriving keys nobody else shares.
            g->established = false;
            return false;
        }
    }

    g->epoch = sending_epoch + 1;
    compute_tree_hash(g, g->tree_hash);

    // confirmed_transcript_hash = Hash(interim_prev || wire_format || content || signature)
    {
        tls_writer input;
        input.init(1024);
        input.raw(g->interim_transcript_hash, g->interim_transcript_hash_len);
        input.u16(WIRE_PUBLIC_MESSAGE);
        input.raw(base + content_start, content_end - content_start);
        input.opaque(signature, signature_len);

        crypto::sha256(input.data(), input.size(), g->confirmed_transcript_hash);
        g->confirmed_transcript_hash_len = 32;
        input.free_writer();
    }

    tls_writer new_context;
    new_context.init(512);
    write_group_context_body(g, &new_context);

    unsigned char joiner_secret[32];
    unsigned char welcome_secret[32];
    advance_key_schedule(g, init_prev, commit_secret,
                         new_context.data(), new_context.size(),
                         joiner_secret, welcome_secret);
    new_context.free_writer();

    // The confirmation tag is the proof that the whole schedule matched. It is
    // checked before anything is used, because a silent mismatch here surfaces
    // far away as media that will not decrypt.
    unsigned char expected[32];
    crypto::hmac_sha256(g->confirmation_key, 32,
                        g->confirmed_transcript_hash, g->confirmed_transcript_hash_len,
                        expected);

    bool tag_ok = true;
    for (int i = 0; i < 32 && tag_ok; i++) tag_ok = expected[i] == confirmation[i];
    if (!tag_ok)
    {
        *out_error = "confirmation_tag не сошёлся";
        g->established = false;
        return false;
    }

    // interim_transcript_hash = Hash(confirmed || confirmation_tag)
    {
        tls_writer input;
        input.init(128);
        input.raw(g->confirmed_transcript_hash, g->confirmed_transcript_hash_len);
        input.opaque(confirmation, 32);
        crypto::sha256(input.data(), input.size(), g->interim_transcript_hash);
        g->interim_transcript_hash_len = 32;
        input.free_writer();
    }

    log_line("mls: применён чужой коммит, эпоха %llu, предложений %u",
             g->epoch, applied_count);
    return true;
}
