#pragma once
#include "discord/types.h"

struct jval;

enum streamview_state
{
    WATCH_IDLE = 0,
    WATCH_REQUESTING,     // waiting for the gateway to hand back a stream server
    WATCH_CONNECTING,     // voice handshake for the stream
    WATCH_WAITING,        // connected, nothing decoded yet
    WATCH_LIVE,
    WATCH_FAILED,
};

namespace streamview
{
    void init();
    void shutdown();

    // Starts watching somebody else's Go Live stream. The channel is the voice
    // channel they are streaming in, which is normally the one we are sitting in
    // as well: watching from outside works the same way, but the stream key is
    // built from their channel, not ours.
    bool watch(snowflake guild_id, snowflake channel_id, snowflake user_id);
    void stop();

    streamview_state state();
    const char* status_text();
    snowflake watching_user();

    // Sound from the share. Muting stops it being decoded at all rather than
    // decoding it and throwing it away.
    void set_muted(bool muted);
    bool muted();
    unsigned int audio_packets();

    // The newest decoded picture, as RGBA. False when nothing has arrived since
    // the last call. Called from the UI thread; the bytes belong to the viewer
    // and stay valid until the next take_frame.
    bool take_frame(const unsigned char** rgba, int* width, int* height);

    // The size of the last picture, for a window that wants to size itself
    // before the first frame lands.
    int width();
    int height();

    unsigned int frames_decoded();
    unsigned int packets_in();

    // Counters for every stage of the path, so a stream that shows nothing can
    // be diagnosed from the window itself rather than from a log file that may
    // not have been written. Which of these is the first to stay at zero says
    // where the picture is being lost.
    struct stats
    {
        unsigned int packets;        // anything at all arrived on the socket
        unsigned int video_packets;  // of those, decrypted as the watched source
        unsigned int assembled;      // whole access units the depacketiser built
        unsigned int frames;         // pictures the decoder produced
        unsigned int dropped;        // access units given up on
        unsigned int decrypt_fail;
        unsigned int video_ssrc;
        bool e2ee;
        // Why the last frame would not unwrap. Empty when nothing has failed.
        const char* dave_error;
        // Shape of the frame that failed, which says whether it arrived intact.
        const char* dave_detail;

        // The group's side of it. A frame that will not open is almost always
        // a key from the wrong epoch, and these say whether the epoch is even
        // moving and what stopped it if it is not.
        unsigned int epoch;
        unsigned int external_sender_ops;   // op 25
        unsigned int proposal_ops;          // op 27
        unsigned int commit_ops;            // op 29
        unsigned int welcome_ops;           // op 30
        unsigned int commits_applied;
        const char* commit_error;

        // The picture side. A frame that decodes to nothing but black looks
        // exactly like one that never decoded, unless the brightness is shown.
        unsigned int decoded_w, decoded_h;
        unsigned int luma;
        int stride;
        // What the decoder was fed and what it said. A transform that swallows
        // frames without producing any looks the same from outside as one that
        // was never given anything.
        unsigned int decoder_in;
        const char* decoder_error;
        // Frames withheld from the decoder because the reference was broken and
        // no self contained frame had arrived to rebuild it yet.
        unsigned int skipped;
        bool waiting_for_idr;
    };
    void read_stats(stats* out);

    // Asks the sender for a fresh keyframe. Sent automatically on connect and
    // whenever the picture cannot be decoded, so the UI only needs this for a
    // "the picture is stuck" button.
    void request_keyframe();

    // Called by the gateway dispatcher. Each returns quietly when the dispatch
    // belongs to a different stream.
    void on_stream_create(const jval* d);
    void on_stream_server_update(const jval* d);
    void on_stream_delete(const jval* d);
    void on_gateway_disconnected();

    // The key this viewer is waiting on, so the sending side can tell the two
    // apart. Empty while idle.
    const char* stream_key();
}
