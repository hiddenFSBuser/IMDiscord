#pragma once
#include "mls_group.h"

// DAVE frame protection. Media is AES-128-GCM with an 8 byte truncated tag; the
// key comes from a per-sender hash ratchet seeded by the MLS exporter, and the
// generation is carried in the top byte of the 4 byte sync nonce.
//
// Opus frames have no unencrypted ranges, so the whole payload is encrypted and
// the associated data is empty.

namespace dave
{
    const unsigned int KEY_BYTES = 16;
    const unsigned int NONCE_BYTES = 12;
    const unsigned int TRUNCATED_TAG_BYTES = 8;
    const unsigned int SYNC_NONCE_BYTES = 4;
    const unsigned int SYNC_NONCE_OFFSET = NONCE_BYTES - SYNC_NONCE_BYTES;
    const unsigned int GENERATION_SHIFT = 24;
    const unsigned short MAGIC_MARKER = 0xFAFA;

    void reset_ratchets();

    // Wraps an opus packet for sending. out must have room for in_len + 32.
    bool encrypt_frame(const mls::group_state* g, unsigned long long self_user_id,
                       unsigned int* nonce_counter,
                       const unsigned char* in, unsigned int in_len,
                       unsigned char* out, unsigned int* out_len);

    // Wraps one H.264 access unit in Annex-B form.
    //
    // Video cannot be encrypted whole the way opus is: the server still has to
    // read the stream to route and repacketise it, so start codes, NAL headers
    // and enough of each slice header to reach the picture parameter set id
    // stay readable. Those spans are listed in the trailer as offset and length
    // pairs so the receiver knows what to skip, and they double as the cipher's
    // associated data. Everything else is encrypted in place.
    //
    // The output uses four byte start codes throughout, which is what a WebRTC
    // receiver normalises to anyway.
    //
    // Only one thread may be inside this at a time; the video pump is the only
    // caller. out_cap should be in_len + 256 to cover the trailer and any start
    // codes that grew from three bytes to four.
    bool encrypt_frame_h264(const mls::group_state* g, unsigned long long self_user_id,
                            unsigned int* nonce_counter,
                            const unsigned char* in, unsigned int in_len,
                            unsigned char* out, unsigned int out_cap,
                            unsigned int* out_len);

    // Unwraps a received frame. Returns false when the frame is not DAVE
    // protected or the tag does not verify.
    bool decrypt_frame(const mls::group_state* g, unsigned long long sender_user_id,
                       const unsigned char* in, unsigned int in_len,
                       unsigned char* out, unsigned int* out_len);

    // The counterpart to encrypt_frame_h264: reads the trailer, puts the clear
    // spans and the decrypted spans back in their places, and hands back the
    // original access unit. out needs room for in_len.
    bool decrypt_frame_h264(const mls::group_state* g, unsigned long long sender_user_id,
                            const unsigned char* in, unsigned int in_len,
                            unsigned char* out, unsigned int out_cap,
                            unsigned int* out_len);

    // True when the payload carries the DAVE trailer.
    bool is_protected(const unsigned char* data, unsigned int len);

    // Zero when the payload is not a protected frame; otherwise the length of
    // the protected frame, any traffic-analysis padding already discounted.
    // The padding rides AFTER the magic marker - a block of bytes all equal to
    // their own count - so a marker test at the very end of the buffer, the
    // one is_protected does, is fooled by it and the frame leaks onwards as
    // if it were plain media. Callers should feed the returned length to
    // decrypt_frame / decrypt_frame_h264, never the raw one.
    unsigned int protected_len(const unsigned char* data, unsigned int len);

    // Why the last decrypt_frame_h264 refused. Empty after a success. Every
    // refusal reaches the caller as a plain false, and a viewer decrypting
    // nothing needs to know which check it tripped.
    const char* last_h264_error();

    // Shape of the frame the last failure was looking at: how many clear
    // ranges it declared and how many of them still begin at a start code.
    // Ranges that no longer line up mean the frame changed on the way here,
    // which is a different problem from the key being wrong.
    const char* last_h264_detail();
}
