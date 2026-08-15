#include "pch.h"
#include "mls_types.h"
#include "core/crypto.h"
#include "core/log.h"

namespace mls
{

// ---------------------------------------------------------------------------
// labelled signatures and reference hashes
// ---------------------------------------------------------------------------

namespace
{
    // SignContent = { opaque label<V>; opaque content<V>; } with the label
    // prefixed by "MLS 1.0 ".
    void build_sign_content(const char* label,
                            const void* content, unsigned int content_len,
                            tls_writer* out)
    {
        char full[64];
        cnprint(full, sizeof(full), "MLS 1.0 %s", label);

        out->opaque(full, (unsigned int)ccslenf(full));
        out->opaque(content, content_len);
    }
}

bool sign_with_label(const unsigned char signature_private[96],
                     const char* label,
                     const void* content, unsigned int content_len,
                     unsigned char* signature, unsigned int* signature_len)
{
    tls_writer w;
    w.init(content_len + 128);
    build_sign_content(label, content, content_len, &w);

    bool ok = crypto::p256_sign(signature_private, w.data(), w.size(), signature, signature_len);
    w.free_writer();
    return ok;
}

bool verify_with_label(const unsigned char signature_public[65],
                       const char* label,
                       const void* content, unsigned int content_len,
                       const unsigned char* signature, unsigned int signature_len)
{
    tls_writer w;
    w.init(content_len + 128);
    build_sign_content(label, content, content_len, &w);

    bool ok = crypto::p256_verify(signature_public, w.data(), w.size(), signature, signature_len);
    w.free_writer();
    return ok;
}

void ref_hash(const char* label, const void* value, unsigned int value_len,
              unsigned char out[32])
{
    // struct { opaque label<V>; opaque value<V>; } RefHashInput
    tls_writer w;
    w.init(value_len + 64);
    w.opaque(label, (unsigned int)ccslenf(label));
    w.opaque(value, value_len);

    crypto::sha256(w.data(), w.size(), out);
    w.free_writer();
}

// ---------------------------------------------------------------------------
// credential
// ---------------------------------------------------------------------------

void credential::set_identity(unsigned long long user_id)
{
    ccfset(identity, 0, sizeof(identity));
    for (int i = 0; i < 8; i++) identity[i] = (unsigned char)(user_id >> (56 - i * 8));
    identity_len = 8;
}

unsigned long long credential::user_id() const
{
    if (identity_len != 8) return 0;

    unsigned long long v = 0;
    for (int i = 0; i < 8; i++) v = (v << 8) | identity[i];
    return v;
}

void credential::write(tls_writer* w) const
{
    w->u16(CREDENTIAL_TYPE_BASIC);
    w->opaque(identity, identity_len);
}

bool credential::read(tls_reader* r)
{
    unsigned short type = 0;
    if (!r->u16(&type)) return false;
    if (type != CREDENTIAL_TYPE_BASIC) return false;

    const unsigned char* id = 0;
    unsigned int len = 0;
    if (!r->opaque(&id, &len)) return false;
    if (len > sizeof(identity)) return false;

    ccfset(identity, 0, sizeof(identity));
    if (len) ccpy(identity, id, len);
    identity_len = len;
    return true;
}

// ---------------------------------------------------------------------------
// leaf node
// ---------------------------------------------------------------------------

namespace
{
    // Capabilities as discord's own clients send them, verified against a
    // captured leaf node. The extension and proposal lists name only
    // *non-default* types, so both stay empty: listing add/update/remove there
    // makes the leaf fail validation.
    void write_capabilities(tls_writer* w)
    {
        tls_writer versions;
        versions.init(8);
        versions.u16(PROTOCOL_VERSION_MLS10);
        w->opaque(versions);
        versions.free_writer();

        tls_writer suites;
        suites.init(8);
        suites.u16(CIPHERSUITE_P256);
        w->opaque(suites);
        suites.free_writer();

        w->opaque(0, 0);   // extensions: no non-default types
        w->opaque(0, 0);   // proposals: no non-default types

        tls_writer creds;
        creds.init(8);
        creds.u16(CREDENTIAL_TYPE_BASIC);
        w->opaque(creds);
        creds.free_writer();
    }

    // Capabilities is five consecutive vectors, not one blob, so the raw span
    // has to be measured rather than read as a single opaque field.
    bool read_capabilities(tls_reader* r)
    {
        const unsigned char* p = 0;
        unsigned int len = 0;
        for (int i = 0; i < 5; i++)
        {
            if (!r->opaque(&p, &len)) return false;
        }
        return true;
    }
}

void leaf_node::init()
{
    ccfset(this, 0, sizeof(*this));
    source = LEAF_SOURCE_KEY_PACKAGE;
}

void leaf_node::write_base(tls_writer* w) const
{
    w->opaque(encryption_key, hpke::NPK);
    w->opaque(signature_key, 65);
    cred.write(w);

    if (capabilities_len) w->raw(capabilities, capabilities_len);
    else write_capabilities(w);

    w->u8(source);

    if (source == LEAF_SOURCE_KEY_PACKAGE)
    {
        w->u64(not_before);
        w->u64(not_after);
    }
    else if (source == LEAF_SOURCE_COMMIT)
    {
        w->opaque(parent_hash, parent_hash_len);
    }

    if (extensions_len) w->raw(extensions, extensions_len);
    else w->opaque(0, 0);
}

void leaf_node::write(tls_writer* w) const
{
    write_base(w);
    w->opaque(signature, signature_len);
}

bool leaf_node::read(tls_reader* r)
{
    const unsigned char* p = 0;
    unsigned int len = 0;

    if (!r->opaque(&p, &len) || len != hpke::NPK) return false;
    ccpy(encryption_key, p, len);

    if (!r->opaque(&p, &len) || len != 65) return false;
    ccpy(signature_key, p, len);

    if (!cred.read(r)) return false;

    unsigned int caps_start = r->pos;
    if (!read_capabilities(r)) return false;
    capabilities_len = r->pos - caps_start;
    if (capabilities_len > sizeof(capabilities)) return false;
    ccpy(capabilities, r->base + caps_start, capabilities_len);

    if (!r->u8(&source)) return false;

    parent_hash_len = 0;
    if (source == LEAF_SOURCE_KEY_PACKAGE)
    {
        if (!r->u64(&not_before) || !r->u64(&not_after)) return false;
    }
    else if (source == LEAF_SOURCE_COMMIT)
    {
        if (!r->opaque(&p, &len) || len > sizeof(parent_hash)) return false;
        ccpy(parent_hash, p, len);
        parent_hash_len = len;
    }

    unsigned int ext_start = r->pos;
    if (!r->opaque(&p, &len)) return false;
    extensions_len = r->pos - ext_start;
    if (extensions_len > sizeof(extensions)) return false;
    ccpy(extensions, r->base + ext_start, extensions_len);

    if (!r->opaque(&p, &len) || len > sizeof(signature)) return false;
    ccpy(signature, p, len);
    signature_len = len;
    return true;
}

namespace
{
    // LeafNodeTBS adds the group context for update and commit sources.
    void build_leaf_tbs(const leaf_node* leaf,
                        const void* group_id, unsigned int group_id_len,
                        unsigned int leaf_index,
                        tls_writer* out)
    {
        leaf->write_base(out);

        if (leaf->source == LEAF_SOURCE_UPDATE || leaf->source == LEAF_SOURCE_COMMIT)
        {
            out->opaque(group_id, group_id_len);
            out->u32(leaf_index);
        }
    }
}

bool leaf_node::sign(const unsigned char signature_private[96],
                     const void* group_id, unsigned int group_id_len,
                     unsigned int leaf_index)
{
    tls_writer tbs;
    tbs.init(512);
    build_leaf_tbs(this, group_id, group_id_len, leaf_index, &tbs);

    bool ok = sign_with_label(signature_private, "LeafNodeTBS", tbs.data(), tbs.size(),
                              signature, &signature_len);
    tbs.free_writer();
    return ok;
}

bool leaf_node::verify(const void* group_id, unsigned int group_id_len,
                       unsigned int leaf_index) const
{
    tls_writer tbs;
    tbs.init(512);
    build_leaf_tbs(this, group_id, group_id_len, leaf_index, &tbs);

    bool ok = verify_with_label(signature_key, "LeafNodeTBS", tbs.data(), tbs.size(),
                                signature, signature_len);
    tbs.free_writer();
    return ok;
}

// ---------------------------------------------------------------------------
// key package
// ---------------------------------------------------------------------------

void key_package::init()
{
    ccfset(init_key, 0, sizeof(init_key));
    ccfset(signature, 0, sizeof(signature));
    signature_len = 0;
    extensions_len = 0;
    leaf.init();
}

namespace
{
    void write_key_package_base(const key_package* kp, tls_writer* w)
    {
        w->u16(PROTOCOL_VERSION_MLS10);
        w->u16(CIPHERSUITE_P256);
        w->opaque(kp->init_key, hpke::NPK);
        kp->leaf.write(w);

        if (kp->extensions_len) w->raw(kp->extensions, kp->extensions_len);
        else w->opaque(0, 0);
    }
}

void key_package::write(tls_writer* w) const
{
    write_key_package_base(this, w);
    w->opaque(signature, signature_len);
}

bool key_package::read(tls_reader* r)
{
    unsigned short version = 0, suite = 0;
    if (!r->u16(&version) || !r->u16(&suite)) return false;
    if (version != PROTOCOL_VERSION_MLS10 || suite != CIPHERSUITE_P256) return false;

    const unsigned char* p = 0;
    unsigned int len = 0;

    if (!r->opaque(&p, &len) || len != hpke::NPK) return false;
    ccpy(init_key, p, len);

    if (!leaf.read(r)) return false;

    unsigned int ext_start = r->pos;
    if (!r->opaque(&p, &len)) return false;
    extensions_len = r->pos - ext_start;
    if (extensions_len > sizeof(extensions)) return false;
    ccpy(extensions, r->base + ext_start, extensions_len);

    if (!r->opaque(&p, &len) || len > sizeof(signature)) return false;
    ccpy(signature, p, len);
    signature_len = len;
    return true;
}

bool key_package::sign(const unsigned char signature_private[96])
{
    tls_writer tbs;
    tbs.init(512);
    write_key_package_base(this, &tbs);

    bool ok = sign_with_label(signature_private, "KeyPackageTBS", tbs.data(), tbs.size(),
                              signature, &signature_len);
    tbs.free_writer();
    return ok;
}

bool key_package::verify() const
{
    // The leaf signature covers the identity; the key package signature is made
    // with the same key, which is what binds the two together.
    if (!leaf.verify(0, 0, 0)) return false;

    tls_writer tbs;
    tbs.init(512);
    write_key_package_base(this, &tbs);

    bool ok = verify_with_label(leaf.signature_key, "KeyPackageTBS", tbs.data(), tbs.size(),
                                signature, signature_len);
    tbs.free_writer();
    return ok;
}

void key_package::compute_ref(unsigned char out[32]) const
{
    tls_writer w;
    w.init(512);
    write(&w);
    ref_hash("MLS 1.0 KeyPackage Reference", w.data(), w.size(), out);
    w.free_writer();
}

bool create_key_package(unsigned long long user_id,
                        const unsigned char signature_private[96],
                        key_package* out,
                        key_package_private* out_private)
{
    out->init();

    unsigned char init_public[hpke::NPK];
    if (!crypto::p256_generate(init_public, out_private->init_private)) return false;
    ccpy(out->init_key, init_public, hpke::NPK);

    unsigned char encryption_public[hpke::NPK];
    if (!crypto::p256_generate(encryption_public, out_private->encryption_private)) return false;
    ccpy(out->leaf.encryption_key, encryption_public, hpke::NPK);

    crypto::p256_public_from_private(signature_private, out->leaf.signature_key);
    out->leaf.cred.set_identity(user_id);
    out->leaf.source = LEAF_SOURCE_KEY_PACKAGE;

    // Discord's own key packages carry an unbounded lifetime; matching that
    // removes clock skew as a reason for the server to reject the leaf.
    out->leaf.not_before = 0;
    out->leaf.not_after = 0xFFFFFFFFFFFFFFFFull;

    // A key_package source leaf is signed without group context.
    if (!out->leaf.sign(signature_private, 0, 0, 0)) return false;
    return out->sign(signature_private);
}

} // namespace mls
