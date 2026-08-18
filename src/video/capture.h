#pragma once

// Screen capture for the outgoing video stream.
//
// The grabber is behind a small interface so the method can be swapped later:
// BitBlt is the one that always works, including on remote desktops and old
// drivers, but it costs a full GDI blit per frame and cannot see hardware
// overlays. DXGI desktop duplication and Windows.Graphics.Capture are the
// obvious additions, and both fit this shape.

// The most frames a second a share will send.
//
// Not a round number chosen for looking sensible: above this the H.264
// encoder refuses the output type outright with E_FAIL - "encoder:
// SetOutputType (0x80004005)" - and the share never starts at all. Better to
// stop at the last rate that works than to offer one that fails.
//
// Applied where the setting is read and again where the screen is grabbed,
// because the two used to disagree and a typed number was quietly undone in a
// place nobody could see.
const int SHARE_FPS_MAX = 172;

enum capture_method
{
    // The compositor hands over the frame it has already composed, on the
    // gpu, and says nothing at all when the screen has not changed. Windows 8
    // and later, monitors only - a single window cannot be duplicated.
    CAPTURE_DXGI = 0,
    // A full blit through GDI every frame, whether anything moved or not.
    // Slow, and blind to hardware overlays - but it works everywhere,
    // including on a single window and over remote desktop.
    CAPTURE_BITBLT,
    CAPTURE_METHOD_COUNT,
};

// A monitor or a top level window.
struct capture_target
{
    // Zero for a monitor, otherwise the window being captured.
    void* window;
    // Desktop coordinates of a monitor; ignored for a window.
    int x, y, width, height;
    bool primary;
    // Only meaningful for a window: it is a real window that can be picked,
    // it just is not on the screen at this moment.
    bool minimized;
    char name[128];
};

// One captured image. The pixels belong to the grabber and stay valid until the
// next grab or stop.
struct capture_frame
{
    const unsigned char* bgra;   // top-down, stride bytes per row
    int width;
    int height;
    int stride;
    // Microseconds from the performance counter, not the tick count: a 33 ms
    // frame period cannot be measured with a 15.6 ms clock, and the encoder and
    // the RTP timestamps both want better than that anyway.
    unsigned long long time_us;

    // Whether anything on screen actually moved since the last grab. Desktop
    // duplication says so for free - a still screen produces no frame at all,
    // and the pixels handed back are the previous ones. Encoding those again
    // costs a full scale and a full encode per layer for a picture nobody
    // changed, which on a quiet desktop is most of what a share spends.
    //
    // Always true on the blit path, which has no way to know.
    bool fresh;
};

// Where the picture ended up inside the frame, and what part of the desktop
// it came from. Anything that wants to draw over a particular window has to
// turn desktop coordinates into frame ones, and that needs both the crop and
// the scale.
struct capture_mapping
{
    int src_x, src_y;      // desktop position of what is being captured
    int src_w, src_h;

    int dst_x, dst_y;      // where the picture sits in the frame, for letterbox
    int dst_w, dst_h;

    int frame_w, frame_h;
};

namespace capture
{
    bool init();
    void shutdown();

    capture_method method();
    bool set_method(capture_method m);
    const char* method_name(capture_method m);

    // Fills out with the monitors present, most useful first, and returns how
    // many were written.
    int list_monitors(capture_target* out, int cap);

    // Describes a top level window by handle. False if it is gone or has no
    // usable size.
    bool describe_window(void* hwnd, capture_target* out);

    // Every top level window worth showing a person: visible, titled, not a
    // tool window and not one of the invisible shells windows keeps around.
    int list_windows(capture_target* out, int cap);

    // How the running capture maps the desktop onto its frames. False when
    // nothing is being captured.
    bool mapping(capture_mapping* out);

    // Begins capturing. The picture is scaled to fit inside max_width and
    // max_height with its shape intact, and both are rounded to even numbers
    // because the encoder needs subsampled chroma.
    //
    // With letterbox set, the frame comes out at exactly the requested size and
    // the leftover is filled with black. A screen that is not the same shape as
    // the stream would otherwise produce an unusual frame size, and a receiver
    // is happier with the ordinary one.
    bool start(const capture_target* target, int max_width, int max_height, int fps,
               bool letterbox = false);
    void stop();
    bool running();

    // The size frames actually come out at, which is the scaled size.
    int width();
    int height();

    // Returns false when the next frame is not due yet, so this is safe to call
    // as often as the caller likes.
    bool grab(capture_frame* out);

    // Whether the mouse pointer is drawn into the frame.
    void set_capture_cursor(bool on);
    bool capture_cursor();

    // Frames handed out and frames skipped because a grab ran long.
    unsigned int frames_captured();
    unsigned int frames_dropped();
    const char* last_error();

    // Grabs one frame with the given method and writes it out as a bitmap,
    // top row first, so the orientation can be looked at rather than argued
    // about. Run from --capturetest.
    bool self_test(capture_method method, const wchar_t* out_path);
}
