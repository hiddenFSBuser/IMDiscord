#pragma once
#include "capture.h"

// Covering up chosen windows in an outgoing screen share.
//
// Whoever is watching sees the desktop as it is except where a window has been
// marked; there a black rectangle with the word CENSORED sits instead, or a
// picture of the person's choosing. The list can be changed while the stream
// is running, because the moment somebody needs this is usually the moment
// something has already appeared on screen.
//
// The cover follows the window: it is looked up by handle every frame, so
// moving, resizing or minimising the window all do the obvious thing. A window
// that closes drops out of the list on its own.
//
// This is a courtesy, not a security boundary. The picture is composed before
// encoding, so nothing censored reaches the wire - but a window that appears
// between two frames is on screen for those frames, and anything drawn over
// the top of a censored window by something else is not tracked.

const int CENSOR_MAX = 16;

struct censor_entry
{
    void* window;
    char title[128];
};

namespace censor
{
    void init();
    void shutdown();

    // Whether this window is being covered, and turning that on or off. The
    // title is remembered so the list stays readable after a window is gone.
    bool is_censored(void* window);
    void add(void* window, const char* title);
    void remove(void* window);
    void clear();

    int count();
    const censor_entry* at(int index);

    // The picture drawn instead of the black box. Passing null goes back to
    // the default. The file is decoded once and kept.
    bool set_cover_image(const wchar_t* path);
    void clear_cover_image();
    bool has_cover_image();
    const char* cover_name();

    // Draws over every censored window. `bgra` is the captured frame, which
    // the caller owns and which must be writable. Returns how many windows
    // were covered, which is zero when there is nothing to do - the caller
    // uses that to skip copying the frame at all.
    int apply(unsigned char* bgra, int width, int height, int stride,
              const capture_mapping* map);

    // Whether anything is set up at all, so a caller can avoid the copy.
    bool active();
}
