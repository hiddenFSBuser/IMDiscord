#pragma once

// The back channel of a call.
//
// Media goes out over udp and nobody says whether it arrived. Rtcp is the
// return path that says: here is what I received, here is what I lost, here
// is what I am missing, and here is how fast you may send. Without it a call
// runs blind - the encoder holds one bitrate whatever the link is doing, a
// gap in a picture waits for the sender's own keyframe timer, and the far
// side has no idea whether it is being heard at all.
//
// Three things live here:
//
//   receiver reports   what rfc 3550 calls RR: loss, jitter and how far the
//                      sequence has come, once a second per source. This is
//                      also what tells the far side the route is alive.
//
//   nack               "packets 1234 to 1236 never arrived, send them again".
//                      Asked for video only: an opus frame is twenty
//                      milliseconds and a retransmission of one arrives after
//                      the moment it belonged to, while one lost video packet
//                      spoils every frame until the next keyframe.
//
//   what comes back    sender reports, receiver reports about our own stream,
//                      and remb. The loss figure in a report about us, and
//                      the bitrate remb asks for, are what the audio encoder
//                      is steered by instead of the fixed number it used to
//                      hold.
//
// Nothing here encrypts or sends: the two transports protect packets
// differently and that belongs with them. This builds bytes and reads bytes.

namespace rtcp
{
    void reset();

    // One media packet arrived, after decryption.
    void on_media(unsigned int ssrc, unsigned short seq, unsigned int rtp_timestamp,
                  bool video);

    // One control packet arrived, after decryption. Compound packets are
    // walked through; self_ssrc says which reports are about us.
    void on_control(const unsigned char* p, int len, unsigned int self_ssrc);

    // What should go out now, or 0 when nothing is due yet. Writes a compound
    // packet: a receiver report, and a nack behind it when something is
    // missing. Call it often; it decides for itself when the moment has come.
    int build(unsigned int self_ssrc, unsigned char* out, int cap);

    // What the audio encoder should be using, in bits per second. Moves with
    // the loss reported about our own stream and with any remb that arrives.
    int audio_bitrate();

    // For the log: the last loss fraction reported about us, in percent, and
    // the last bitrate remb asked for. Both zero until something says
    // otherwise.
    int reported_loss_percent();
    int requested_bitrate();

    bool self_test();
}
