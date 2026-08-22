#pragma once
#include "discord/types.h"

struct jval;

enum screenshare_state
{
    SHARE_IDLE = 0,
    SHARE_REQUESTING,     // waiting for the gateway to hand back a stream server
    SHARE_CONNECTING,     // voice handshake for the stream
    SHARE_LIVE,
    SHARE_FAILED,
};

namespace screenshare
{
    void init();
    void shutdown();

    // Starts a Go Live stream in the voice channel already joined. The monitor
    // index refers to capture::list_monitors.
    // `method` is a capture_method: how the screen is read.
    bool start(int monitor_index, int max_width, int max_height, int fps, int bitrate_kbps,
               bool with_audio = false, int method = 0);

    // Whether the share is carrying system sound, and why it is not when it
    // was asked for.
    bool audio_running();
    const char* audio_error();

    // Turns the machine's sound on or off while the share is running. Only the
    // audio track changes - the picture never stops, and the track itself goes
    // back to sending silence rather than disappearing, which is what keeps
    // the far side from reading the gap as a fault.
    void set_audio(bool on);
    void stop();

    screenshare_state state();
    const char* status_text();

    // Frames sent, packets sent, and kilobytes on the wire.
    unsigned int frames_sent();
    unsigned int packets_sent();
    unsigned int kb_sent();

    // Called by the gateway dispatcher.
    void on_stream_create(const jval* d);
    void on_stream_server_update(const jval* d);
    void on_gateway_disconnected();

    // Starts the share again after the gateway took it down, once the call is
    // back. Called every frame; does nothing unless something is waiting.
    void restore_if_pending();
}
