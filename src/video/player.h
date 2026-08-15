#pragma once

// Playing an mp4 somebody posted in a chat.
//
// One player for the whole client. Two videos going at once would be two
// pictures nobody is watching and two soundtracks fighting each other, so
// starting one stops the other; the card in the chat that is not playing goes
// back to being a still with a play button on it.
//
// The work happens on a thread of its own. Decoding a frame takes long enough
// that doing it between two draw calls would show as a stutter in the whole
// window, not just in the video.

enum player_state
{
    PLAYER_IDLE = 0,
    PLAYER_LOADING,     // fetching the file
    PLAYER_READY,       // opened, waiting to be told to play
    PLAYER_PLAYING,
    PLAYER_PAUSED,
    PLAYER_ENDED,
    PLAYER_FAILED,
};

namespace player
{
    void init();
    void shutdown();

    // Starts fetching and plays as soon as it can. `url` identifies the video
    // and doubles as the key the chat uses to ask whether it is the one
    // playing.
    void open(const char* url);
    void stop();

    void toggle_pause();
    void seek(unsigned long long time_us);
    void set_muted(bool muted);
    bool muted();

    player_state state();
    const char* current_url();
    bool is_current(const char* url);
    const char* last_error();

    unsigned long long position_us();
    unsigned long long duration_us();

    int width();
    int height();

    // The frame to show, as RGBA. Null until the first one is decoded. The
    // pointer stays valid until the next call to this from the same thread,
    // which is the ui thread and the only caller.
    const unsigned char* frame_rgba(unsigned int* out_serial);

    // Called once per ui frame so the player can notice nobody has asked for
    // a picture in a while and let the file go.
    void tick();

    // Decodes a file from disk and reports what came out, without a window or
    // a network. Run from --mp4test.
    bool self_test(const wchar_t* path);
}
