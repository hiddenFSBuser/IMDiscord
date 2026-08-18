#pragma once
#include "mls_message.h"

// MLS group state and commit generation (RFC 9420 sections 7, 8 and 12).
//
// Scope is deliberately narrow: this client creates a group, adds the other
// participants, and derives the epoch secrets DAVE needs. A commit carrying
// only Add proposals is allowed to omit the UpdatePath, which keeps TreeKEM
// path encryption out of the picture entirely - every parent node stays blank.

namespace mls
{
    const unsigned int MAX_MEMBERS = 64;
    // Leaves at even indices, parents at odd ones, so the flat array is twice
    // the leaf count less one.
    const unsigned int MAX_NODES = 2 * MAX_MEMBERS;
    const unsigned int MAX_UNMERGED = 32;

    // Extension type for the ratchet tree carried inside GroupInfo.
    const unsigned short EXTENSION_RATCHET_TREE = 0x0002;

    enum node_type
    {
        NODE_LEAF = 1,
        NODE_PARENT = 2,
    };

    // A non-blank parent. Until TreeKEM every one of these was blank, which is
    // the assumption a commit carrying an UpdatePath breaks: it hands out fresh
    // keys along the sender's path and the tree hash has to account for them.
    struct parent_node
    {
        unsigned char encryption_key[65];
        unsigned char parent_hash[32];
        unsigned int parent_hash_len;
        // Leaves added under this node since it was last rekeyed. They cannot
        // read what is encrypted to it, so a resolution has to list them too.
        unsigned int unmerged[MAX_UNMERGED];
        unsigned int unmerged_count;
        bool used;
    };

    struct group_state
    {
        unsigned char group_id[64];
        unsigned int group_id_len;
        unsigned long long epoch;

        // Leaves in tree order. Parents are blank throughout, so only the leaf
        // array is needed to describe the whole tree.
        leaf_node leaves[MAX_MEMBERS];
        bool leaf_used[MAX_MEMBERS];
        unsigned int leaf_count;      // allocated width, including blanks

        unsigned int my_leaf;
        unsigned char my_signature_private[96];
        unsigned char my_encryption_private[hpke::NSK];

        // Parents, indexed by node index rather than by parent number: the tree
        // math already speaks in node indices and translating twice invites the
        // off by one that only shows up as somebody else's media not opening.
        parent_node parents[MAX_NODES];

        // Private keys for the nodes above us that we have been given a path
        // secret for. Without them a later commit from the other side of the
        // tree cannot be opened.
        unsigned char node_private[MAX_NODES][hpke::NSK];
        bool node_private_set[MAX_NODES];

        // The GroupContext extensions, exactly as the group carries them.
        //
        // Kept verbatim rather than understood: this client has no use for any
        // of them, but the context is hashed into the key schedule, so writing
        // it back with an empty extensions field produces different keys from
        // everybody else's. A DAVE group always has at least one - the server
        // is an external sender and says so here - and the effect was that
        // every commit after joining failed its confirmation tag.
        unsigned char context_extensions[1024];
        unsigned int context_extensions_len;

        unsigned char tree_hash[32];
        unsigned char confirmed_transcript_hash[32];
        unsigned int confirmed_transcript_hash_len;
        unsigned char interim_transcript_hash[32];
        unsigned int interim_transcript_hash_len;

        unsigned char init_secret[32];
        unsigned char epoch_secret[32];
        unsigned char confirmation_key[32];
        unsigned char membership_key[32];
        unsigned char sender_data_secret[32];
        unsigned char encryption_secret[32];
        unsigned char exporter_secret[32];

        bool established;
    };

    // Starts a fresh group with this client alone at leaf 0 and runs the epoch
    // 0 key schedule off a random init secret.
    bool create_group(group_state* g,
                      const unsigned char group_id[], unsigned int group_id_len,
                      const leaf_node* my_leaf,
                      const unsigned char signature_private[96],
                      const unsigned char encryption_private[hpke::NSK]);

    // Tree hash over the current leaves with all parents blank.
    void compute_tree_hash(const group_state* g, unsigned char out[32]);

    // GroupContext for the state as it stands.
    void write_group_context(const group_state* g, tls_writer* w);

    // Applies Add proposals, advances to the next epoch, and produces the
    // commit and welcome bytes that op 28 expects concatenated.
    bool build_commit(group_state* g,
                      const proposal_message* proposals, unsigned int proposal_count,
                      ubuffer* out_commit, ubuffer* out_welcome);

    // Which leaf sent a commit, without applying anything. The server hands a
    // commit back to its own author looking like everybody else's, and the
    // author is the one member who cannot process it - the state it leads to
    // was computed when it was written, so it has to be swapped in rather than
    // calculated. Telling the two apart by comparing bytes does not hold: what
    // comes back has been through the server. This reads the sender out of the
    // FramedContent, which is what the sender field is for.
    //
    // False when the message cannot be parsed far enough to tell.
    bool commit_sender(const void* message, unsigned int len, unsigned int* out_leaf);

    // Joins an existing group from a Welcome: decrypts our GroupSecrets with
    // the key package's init key, unwraps the GroupInfo, rebuilds the tree and
    // runs the key schedule for the epoch we are joining.
    bool process_welcome(group_state* g,
                         const void* welcome, unsigned int welcome_len,
                         const key_package* my_key_package,
                         const key_package_private* my_private,
                         const unsigned char signature_private[96]);

    // A proposal kept from op 27. A commit names the proposals it carries by
    // reference rather than by value, so a member who did not author it can
    // only apply it if it held on to what it was told earlier.
    struct cached_proposal
    {
        unsigned char ref[32];
        proposal prop;
    };

    // Applies somebody else's commit, which arrives as op 29. Every member
    // except its author has to do this: the group moves to the next epoch and a
    // member that stays behind keeps deriving media keys nobody else is using,
    // so its frames stop opening and its own stop being readable.
    //
    // The state is left untouched when this returns false, and out_error is set
    // to a short reason.
    bool process_commit(group_state* g,
                        const void* message, unsigned int len,
                        const cached_proposal* known, unsigned int known_count,
                        const char** out_error);

    // Exports key material for DAVE's per-sender ratchets.
    bool export_secret(const group_state* g, const char* label,
                       const void* context, unsigned int context_len,
                       unsigned char* out, unsigned int out_len);
}
