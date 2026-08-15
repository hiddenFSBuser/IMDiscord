#pragma once
#include "mls_types.h"

// MLSMessage framing (RFC 9420 section 6). Only what DAVE exchanges is
// modelled: public messages carrying proposals from the external sender, and
// the commit that answers them.

namespace mls
{
    enum wire_format
    {
        WIRE_RESERVED = 0,
        WIRE_PUBLIC_MESSAGE = 1,
        WIRE_PRIVATE_MESSAGE = 2,
        WIRE_WELCOME = 3,
        WIRE_GROUP_INFO = 4,
        WIRE_KEY_PACKAGE = 5,
    };

    enum sender_type
    {
        SENDER_RESERVED = 0,
        SENDER_MEMBER = 1,
        SENDER_EXTERNAL = 2,
        SENDER_NEW_MEMBER_PROPOSAL = 3,
        SENDER_NEW_MEMBER_COMMIT = 4,
    };

    enum content_type
    {
        CONTENT_RESERVED = 0,
        CONTENT_APPLICATION = 1,
        CONTENT_PROPOSAL = 2,
        CONTENT_COMMIT = 3,
    };

    struct sender
    {
        unsigned char type;
        unsigned int index;     // leaf index for member, sender index for external
    };

    struct proposal
    {
        unsigned short type;
        key_package add;            // PROPOSAL_ADD
        unsigned int remove_index;  // PROPOSAL_REMOVE
        leaf_node update_leaf;      // PROPOSAL_UPDATE

        // ProposalRef = RefHash("MLS 1.0 Proposal Reference", Proposal)
        void compute_ref(unsigned char out[32]) const;
        void write(tls_writer* w) const;
    };

    struct proposal_message
    {
        unsigned char group_id[64];
        unsigned int group_id_len;
        unsigned long long epoch;
        sender snd;
        proposal prop;

        // The bytes of the Proposal as they appeared on the wire.
        const unsigned char* proposal_bytes;
        unsigned int proposal_bytes_len;

        // AuthenticatedContent = wire_format || FramedContent || auth. This,
        // not the bare Proposal, is what a ProposalRef hashes over.
        const unsigned char* auth_content_bytes;
        unsigned int auth_content_len;

        // ProposalRef for this message.
        void compute_ref(unsigned char out[32]) const;
    };

    bool read_proposal(tls_reader* r, proposal* out);

    // One MLSMessage wrapping a PublicMessage that carries a proposal.
    bool read_proposal_message(tls_reader* r, proposal_message* out);

    // op 27 payload: uint8 is_revoke, then a vector of MLSMessages.
    bool parse_proposals_payload(const void* data, unsigned int len,
                                 bool* out_is_revoke,
                                 proposal_message* out, unsigned int cap,
                                 unsigned int* out_count);
}
