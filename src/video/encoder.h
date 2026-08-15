#pragma once

// H.264 encoding for the outgoing stream, on top of Media Foundation.
//
// Windows ships an H.264 encoder as a Media Foundation transform, so no codec
// source has to live in this tree and nothing links against a C runtime. Three
// of them are usually present: a software one, and hardware ones behind NVIDIA
// and Intel. Only the software transform is used for now because it is
// synchronous; the hardware ones are asynchronous and want the event driven
// interface, which is a later job.
//
// Everything Discord needs comes out of the box: constrained baseline profile,
// no B frames, in-band parameter sets, and Annex-B framing that feeds straight
// into the packetiser.

// Forward declared so this header stays clear of the Media Foundation ones.
struct IMFTransform;
struct IMFMediaBuffer;

// One encoder. Simulcast means running several at different resolutions at the
// same time, so none of this can live in file scope any more.
struct venc_stream
{
    IMFTransform* mft;
    IMFMediaBuffer* in_buffer;     // reused NV12 input
    IMFMediaBuffer* out_buffer;    // reused output, when the transform wants one
    bool running;
    bool mft_allocates;

    int w, h, fps;
    unsigned int in, out;
    char error[192];
    char name[128];

    unsigned char* nv12;
    int nv12_size;

    // The finished frame handed back by next().
    unsigned char* frame;
    int frame_cap;
    int frame_len;

    // The encoder emits the parameter sets once, at the head of the stream, and
    // every later keyframe arrives bare. A viewer who joins after that gets an
    // IDR it has no SPS or PPS for and cannot decode a thing, so they are kept
    // here and put back in front of every keyframe.
    unsigned char params[512];
    int params_len;
};

namespace venc
{
    // Writes every video encoder Media Foundation can offer, with the formats
    // it produces, to the log. Whether a codec other than H.264 is reachable
    // without dragging in a third party library decides whether clients that
    // lack H.264 can ever be served.
    void log_encoders();

    // Brings Media Foundation up. Safe to call more than once.
    bool init();
    void shutdown();

    // Both sides must be even. bitrate is what the encoder aims for on average.
    bool start(venc_stream* e, int width, int height, int fps, int bitrate_kbps);
    void stop(venc_stream* e);
    bool running(const venc_stream* e);

    int width(const venc_stream* e);
    int height(const venc_stream* e);

    // Converts a captured BGRA frame and hands it to the encoder. The pixels
    // are consumed before this returns.
    //
    // The frame size is passed in and checked rather than assumed: a capture
    // restarted at a different resolution would otherwise be read as if it were
    // still the old one, walking off the end of its buffer.
    bool submit(venc_stream* e, const unsigned char* bgra, int width, int height, int stride,
                unsigned long long time_us);

    // Takes the next finished frame, in Annex-B with start codes. The bytes
    // belong to the encoder and stay valid until the next call. False when
    // nothing is ready yet, which is normal: the encoder holds a frame or two.
    bool next(venc_stream* e, const unsigned char** data, int* len, bool* keyframe);

    // Makes the next frame an IDR, for a new viewer or after packet loss.
    void request_keyframe(venc_stream* e);

    // Turns one BGRA image into NV12. Exposed so it can be checked on its own;
    // submit calls it internally. dst_y is width*height, followed by the
    // interleaved chroma plane of width*height/2.
    void bgra_to_nv12(const unsigned char* bgra, int stride,
                      unsigned char* dst, int width, int height);

    // Box filtered halving, for the smaller simulcast layers. Both sides of the
    // result are rounded down to an even number because H.264 chroma is shared
    // across two by two blocks.
    void downscale_bgra(const unsigned char* src, int src_w, int src_h, int src_stride,
                        unsigned char* dst, int dst_w, int dst_h);

    const char* encoder_name(const venc_stream* e);
    const char* last_error(const venc_stream* e);
    unsigned int frames_in(const venc_stream* e);
    unsigned int frames_out(const venc_stream* e);
}
