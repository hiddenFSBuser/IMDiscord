#pragma once

// H.264 decoding for a stream somebody else is sending, on top of Media
// Foundation. The counterpart to encoder.h, and it works the same way: Windows
// ships the transform, so no codec source lives in this tree and nothing links
// against a C runtime.
//
// The picture size is not known when the decoder starts. It comes out of the
// sequence parameter set in the first frames, and Media Foundation reports it
// by refusing to produce output until the type is renegotiated; that is handled
// inside and shows up here only as width() and height() changing.

namespace vdec
{
    // Brings Media Foundation up. Safe to call more than once, and safe to call
    // alongside the encoder: the two share the platform refcount.
    bool init();
    void shutdown();

    bool start();
    void stop();
    bool running();

    // Feeds one Annex-B access unit. The bytes are consumed before this returns.
    bool submit(const unsigned char* annexb, int len, unsigned long long time_us);

    // Takes the next decoded picture, converted to RGBA for the texture upload.
    // The bytes belong to the decoder and stay valid until the next call. False
    // when nothing is ready, which is normal while the decoder fills up.
    bool next(const unsigned char** rgba, int* width, int* height);

    // The visible size of the last picture. Zero until the first one arrives.
    int width();
    int height();

    // Throws away everything buffered, for a stream that restarted.
    void flush();

    const char* decoder_name();
    const char* last_error();
    unsigned int frames_in();
    unsigned int frames_out();

    // Average brightness of the last picture, and the stride the output type
    // declared. A decode that produces frames nobody can see is otherwise
    // indistinguishable from one that produces nothing.
    unsigned int last_luma();
    int stride();

    // NV12 to RGBA, BT.601 limited range, the inverse of venc::bgra_to_nv12.
    // nv12 points at the start of the luma plane, src_pitch is its stride, and
    // the chroma plane follows the whole of it at the same stride, which is how
    // every Media Foundation buffer is laid out. src_x and src_y cut the padding
    // a decoder adds to reach a whole macroblock; they are taken from the plane
    // start rather than folded into the pointer because the two planes are
    // cropped differently.
    void nv12_to_rgba(const unsigned char* nv12, int src_pitch, int plane_height,
                      int src_x, int src_y,
                      unsigned char* dst, int width, int height);
}
