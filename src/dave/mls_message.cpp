#include "pch.h"
#include "mls_message.h"
#include "core/crypto.h"
#include "core/log.h"

namespace mls
{

void proposal::write(tls_writer* w) const
{
    w->u16(type);
    switch (type)
    {
    case PROPOSAL_ADD:
        add.write(w);
        break;
    case PROPOSAL_REMOVE:
        w->u32(remove_index);
        break;
    case PROPOSAL_UPDATE:
        update_leaf.write(w);
        break;
    default:
        break;
    }
}

void proposal::compute_ref(unsigned char out[32]) const
{
    tls_writer w;
    w.init(768);
    write(&w);
    ref_hash("MLS 1.0 Proposal Reference", w.data(), w.size(), out);
    w.free_writer();
}

bool read_proposal(tls_reader* r, proposal* out)
{
    ccfset(out, 0, sizeof(*out));
    out->add.init();
    out->update_leaf.init();

    if (!r->u16(&out->type)) return false;

    switch (out->type)
    {
    case PROPOSAL_ADD:
        return out->add.read(r);

    case PROPOSAL_REMOVE:
        return r->u32(&out->remove_index);

    case PROPOSAL_UPDATE:
        return out->update_leaf.read(r);

    default:
        // Anything else is a proposal type this client never opts into, so it
        // cannot be skipped safely - the length is not self describing.
        return false;
    }
}

void proposal_message::compute_ref(unsigned char out[32]) const
{
    ref_hash("MLS 1.0 Proposal Reference", auth_content_bytes, auth_content_len, out);
}

bool read_proposal_message(tls_reader* r, proposal_message* out)
{
    ccfset(out, 0, sizeof(*out));

    unsigned int message_start = r->pos;

    unsigned short version = 0;
    unsigned short format = 0;
    if (!r->u16(&version) || !r->u16(&format)) return false;
    if (version != PROTOCOL_VERSION_MLS10) return false;
    if (format != WIRE_PUBLIC_MESSAGE) return false;

    // ---- FramedContent ----
    const unsigned char* gid = 0;
    unsigned int gid_len = 0;
    if (!r->opaque(&gid, &gid_len)) return false;
    if (gid_len > sizeof(out->group_id)) return false;
    ccpy(out->group_id, gid, gid_len);
    out->group_id_len = gid_len;

    if (!r->u64(&out->epoch)) return false;

    if (!r->u8(&out->snd.type)) return false;
    if (out->snd.type == SENDER_MEMBER || out->snd.type == SENDER_EXTERNAL)
    {
        if (!r->u32(&out->snd.index)) return false;
    }

    const unsigned char* aad = 0;
    unsigned int aad_len = 0;
    if (!r->opaque(&aad, &aad_len)) return false;

    unsigned char content = 0;
    if (!r->u8(&content)) return false;
    if (content != CONTENT_PROPOSAL) return false;

    unsigned int proposal_start = r->pos;
    if (!read_proposal(r, &out->prop)) return false;

    out->proposal_bytes = r->base + proposal_start;
    out->proposal_bytes_len = r->pos - proposal_start;

    // ---- FramedContentAuthData ----
    const unsigned char* sig = 0;
    unsigned int sig_len = 0;
    if (!r->opaque(&sig, &sig_len)) return false;

    // AuthenticatedContent starts after the two version bytes and ends here:
    // the membership tag that may follow belongs to PublicMessage, not to it.
    out->auth_content_bytes = r->base + message_start + 2;
    out->auth_content_len = r->pos - message_start - 2;

    // A membership tag only accompanies member senders; the external sender
    // that discord uses does not carry one.
    if (out->snd.type == SENDER_MEMBER)
    {
        const unsigned char* tag = 0;
        unsigned int tag_len = 0;
        if (!r->opaque(&tag, &tag_len)) return false;
    }

    return r->ok();
}

bool parse_proposals_payload(const void* data, unsigned int len,
                             bool* out_is_revoke,
                             proposal_message* out, unsigned int cap,
                             unsigned int* out_count)
{
    *out_count = 0;

    tls_reader r;
    r.init(data, len);

    unsigned char revoke = 0;
    if (!r.u8(&revoke)) return false;
    *out_is_revoke = revoke != 0;

    if (*out_is_revoke)
    {
        // A revoke carries proposal references rather than proposals.
        return true;
    }

    const unsigned char* body = 0;
    unsigned int body_len = 0;
    if (!r.opaque(&body, &body_len)) return false;

    tls_reader messages;
    messages.init(body, body_len);

    while (messages.remaining() > 0)
    {
        if (*out_count >= cap) return false;
        if (!read_proposal_message(&messages, &out[*out_count])) return false;
        (*out_count)++;
    }

    return messages.done();
}

} // namespace mls
