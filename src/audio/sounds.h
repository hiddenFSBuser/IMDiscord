#pragma once

// The short sounds the client makes for itself: somebody arriving in a call,
// a share starting, a direct message landing.
//
// The samples live in sounds_data.cpp, decoded at build time and carried in
// the binary - see the note there for why they are not mp3 any more.

enum ui_sound
{
    SOUND_VOICE_JOIN = 0,     // somebody arrived, us included
    SOUND_VOICE_LEAVE,
    SOUND_STREAM_START,
    SOUND_STREAM_STOP,
    SOUND_NOTIFY,             // a direct message
    SOUND_COUNT,
};

namespace sounds
{
    // Mixed into the same output the call uses, so it follows the chosen
    // device and the output volume rather than going somewhere of its own.
    // Returns at once; the sound is handed to the mixer, not waited on.
    void play(ui_sound which);

    // Off by default for nothing - all of them are on - but a person in a busy
    // channel wants the arrival chime gone and the message one kept.
    bool enabled(ui_sound which);
    void set_enabled(ui_sound which, bool on);

    // Kept apart from the call volume: a chime at the level of somebody's
    // voice is far too loud, and the two are adjusted for different reasons.
    float volume();
    void set_volume(float v);
}

// The files themselves, in sounds_data.cpp. Decoded on first use rather than
// at startup: a short mp3 takes about a millisecond, and paying for the ones
// somebody has switched off would be paying for nothing.
extern const unsigned char MP3_VOICE_JOIN[];
extern const unsigned int MP3_VOICE_JOIN_SIZE;
extern const unsigned char MP3_VOICE_LEAVE[];
extern const unsigned int MP3_VOICE_LEAVE_SIZE;
extern const unsigned char MP3_STREAM_START[];
extern const unsigned int MP3_STREAM_START_SIZE;
extern const unsigned char MP3_STREAM_STOP[];
extern const unsigned int MP3_STREAM_STOP_SIZE;
extern const unsigned char MP3_NOTIFY[];
extern const unsigned int MP3_NOTIFY_SIZE;
