#pragma once
#include "tls_codec.h"
#include "hpke.h"
#include "core/crypto.h"

// MLS object model, restricted to what DAVE v1 needs (RFC 9420).
//
// Only one ciphersuite exists here: MLS_128_DHKEMP256_AES128GCM_SHA256_P256,
// id 0x0002. Anything the protocol allows to vary is pinned to that.

namespace mls
{
    const unsigned short PROTOCOL_VERSION_MLS10 = 1;
    const unsigned short CIPHERSUITE_P256 = 0x0002;

    const unsigned short CREDENTIAL_TYPE_BASIC = 1;

    enum leaf_node_source
    {
        LEAF_SOURCE_RESERVED = 0,
        LEAF_SOURCE_KEY_PACKAGE = 1,
        LEAF_SOURCE_UPDATE = 2,
        LEAF_SOURCE_COMMIT = 3,
    };

    enum proposal_type
    {
        PROPOSAL_ADD = 1,
        PROPOSAL_UPDATE = 2,
        PROPOSAL_REMOVE = 3,
        PROPOSAL_PSK = 4,
        PROPOSAL_REINIT = 5,
        PROPOSAL_EXTERNAL_INIT = 6,
        PROPOSAL_GROUP_CONTEXT_EXTENSIONS = 7,
    };

    // SignWithLabel / VerifyWithLabel, RFC 9420 section 5.1.2. The signed
    // content is a SignContent struct, not the raw bytes.
    bool sign_with_label(const unsigned char signature_private[96],
                         const char* label,
                         const void* content, unsigned int content_len,
                         unsigned char* signature, unsigned int* signature_len);

    bool verify_with_label(const unsigned char signature_public[65],
                           const char* label,
                           const void* content, unsigned int content_len,
                           const unsigned char* signature, unsigned int signature_len);

    // RefHash(label, value) = Hash(RefHashInput), used for KeyPackageRef and
    // ProposalRef.
    void ref_hash(const char* label, const void* value, unsigned int value_len,
                  unsigned char out[32]);

    // ---- identity ----------------------------------------------------------

    struct credential
    {
        // BasicCredential. Discord puts the user id in as eight big-endian
        // bytes, not as a decimal string.
        unsigned char identity[32];
        unsigned int identity_len;

        void set_identity(unsigned long long user_id);
        unsigned long long user_id() const;
        void write(tls_writer* w) const;
        bool read(tls_reader* r);
    };

    // ---- leaf node ---------------------------------------------------------

    struct leaf_node
    {
        unsigned char encryption_key[hpke::NPK];
        unsigned char signature_key[65];
        credential cred;
        unsigned char source;
        unsigned long long not_before;      // source == key_package
        unsigned long long not_after;
        unsigned char parent_hash[32];      // source == commit
        unsigned int parent_hash_len;

        // Capabilities and extensions are signed over, so a parsed leaf has to
        // reproduce them byte for byte rather than substitute our own.
        unsigned char capabilities[256];
        unsigned int capabilities_len;
        unsigned char extensions[256];
        unsigned int extensions_len;

        unsigned char signature[crypto::P256_SIGNATURE_MAX_BYTES];
        unsigned int signature_len;

        void init();
        // Everything except the signature, i.e. the LeafNodeTBS prefix.
        void write_base(tls_writer* w) const;
        void write(tls_writer* w) const;
        bool read(tls_reader* r);

        // group_id and leaf_index are only mixed in for update/commit sources.
        bool sign(const unsigned char signature_private[96],
                  const void* group_id, unsigned int group_id_len,
                  unsigned int leaf_index);
        bool verify(const void* group_id, unsigned int group_id_len,
                    unsigned int leaf_index) const;
    };

    // ---- key package -------------------------------------------------------

    struct key_package
    {
        unsigned char init_key[hpke::NPK];
        leaf_node leaf;
        unsigned char extensions[256];
        unsigned int extensions_len;
        unsigned char signature[crypto::P256_SIGNATURE_MAX_BYTES];
        unsigned int signature_len;

        void init();
        void write(tls_writer* w) const;
        bool read(tls_reader* r);

        bool sign(const unsigned char signature_private[96]);
        bool verify() const;

        // KeyPackageRef = RefHash("MLS 1.0 KeyPackage Reference", KeyPackage)
        void compute_ref(unsigned char out[32]) const;
    };

    // Builds a fresh key package plus the private keys that go with it.
    struct key_package_private
    {
        unsigned char init_private[hpke::NSK];
        unsigned char encryption_private[hpke::NSK];
    };

    bool create_key_package(unsigned long long user_id,
                            const unsigned char signature_private[96],
                            key_package* out,
                            key_package_private* out_private);
}
