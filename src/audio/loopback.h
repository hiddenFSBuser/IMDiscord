#pragma once

// Capturing what the machine is playing, for a screen share with sound.
//
// The obvious way to do this is loopback on the output device, and it is the
// wrong way here: that captures everything, including this client's own
// output, so every voice in the call comes straight back out of the share and
// everybody hears themselves a moment late.
//
// Windows has a second kind of loopback that takes a process id and either
// records only that process tree or records everything except it. The second
// is what this uses, pointed at ourselves. The cost is that anything this
// client plays is excluded too - a video playing in a chat will not be heard
// by the viewers - and that is the right trade: an echo of the whole call is
// far worse than a clip nobody else can hear.
//
// It needs Windows 10 build 19041 or newer. On anything older activation
// fails, and the caller is told rather than quietly sending an echo.

namespace loopback
{
    // Begins capturing everything but this process. False when the system
    // cannot do it, with last_error() saying why.
    bool start();
    void stop();
    bool running();

    // One 20 ms frame of mono 48 kHz audio, which is what the share's opus
    // encoder takes. False when nothing has arrived yet, and the caller sends
    // silence for that frame.
    bool read_frame(short* mono, int samples);

    // Loudness of the last frame handed out, 0..1, for a level meter.
    float level();

    const char* last_error();

    // Tries the whole thing and writes what happened to the log, without a
    // window or a call. Run from --audiotest.
    bool self_test();
}
