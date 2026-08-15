#include "pch.h"

// stb_image is the only image decoder in the tree. Every hook is routed to the
// custom allocator so nothing reaches the CRT heap.
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_THREAD_LOCALS
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_ASSERT(x)        ((void)0)
#define STBI_MALLOC(sz)       memalloc((int)(sz))
#define STBI_REALLOC(p, sz)   memrealloc(p, (int)(sz))
#define STBI_FREE(p)          memfree(p)

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// Only the in-memory PNG writer is used, to turn a pasted screenshot into an
// attachment discord will accept.
#define STBI_WRITE_NO_STDIO
#define STBIW_ASSERT(x)       ((void)0)
#define STBIW_MALLOC(sz)      memalloc((int)(sz))
#define STBIW_REALLOC(p, sz)  memrealloc(p, (int)(sz))
#define STBIW_FREE(p)         memfree(p)
#define STBIW_MEMMOVE(a, b, s) ccmov(a, b, s)

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// ---------------------------------------------------------------------------
// streaming GIF playback
// ---------------------------------------------------------------------------
//
// stbi_load_gif_from_memory decodes every frame into one allocation, which for
// a twenty second animation means hundreds of megabytes and a long stall before
// anything appears. stb already decodes GIFs one frame at a time internally;
// these wrappers expose that so a player can pull frames as it needs them and
// keep only the current canvas in memory.

#include "stb_gif_stream.h"

struct imd_gif
{
    stbi__context ctx;
    stbi__gif state;

    unsigned char* data;        // owned copy of the compressed file
    int len;

    // Disposal method 3 restores the canvas from two frames back. The two
    // slots alternate, so a frame is copied once and is read again two frames
    // later; both stay null for the files that never ask for that disposal,
    // which is nearly all of them.
    unsigned char* history[2];

    int width;
    int height;
    int frame_bytes;
    int frames;                 // from the pre-scan, 0 when it gave up
    int index;                  // frames produced since the last rewind
    int finished;
};

int imd_gif_is_gif(const unsigned char* data, int len)
{
    if (!data || len < 6) return 0;
    if (data[0] != 'G' || data[1] != 'I' || data[2] != 'F') return 0;
    return (data[3] == '8' && (data[4] == '7' || data[4] == '9') && data[5] == 'a') ? 1 : 0;
}

// Skips a chain of length-prefixed sub-blocks. Returns the offset past the
// terminator, or -1 if the chain runs off the end of the file.
static int imd_gif_skip_blocks(const unsigned char* d, int len, int p)
{
    for (;;)
    {
        if (p >= len) return -1;
        int n = d[p++];
        if (n == 0) return p;
        p += n;
    }
}

// Walks the block structure without decoding a single pixel: it only needs to
// know how many frames there are and whether any of them restores the canvas
// from two frames back. Returns 0 if the file is malformed, in which case the
// caller assumes the expensive answer to both questions.
static int imd_gif_scan(const unsigned char* d, int len, int* frames, int* needs_history)
{
    *frames = 0;
    *needs_history = 0;

    if (len < 14) return 0;

    int p = 13;                                          // header + screen descriptor
    if (d[10] & 0x80) p += 3 * (2 << (d[10] & 7));       // global colour table

    for (;;)
    {
        if (p >= len) return 0;
        int tag = d[p++];

        if (tag == 0x3B) return 1;                       // trailer
        if (tag == 0x00) continue;                       // stray padding, seen in the wild

        if (tag == 0x21)                                 // extension
        {
            if (p >= len) return 0;
            int label = d[p++];
            // A graphic control extension carries the disposal method in the
            // top bits of its first payload byte.
            if (label == 0xF9 && p < len && d[p] == 4)
            {
                if (p + 5 > len) return 0;
                if (((d[p + 1] & 0x1C) >> 2) == 3) *needs_history = 1;
            }
            p = imd_gif_skip_blocks(d, len, p);
            if (p < 0) return 0;
            continue;
        }

        if (tag == 0x2C)                                 // image descriptor
        {
            if (p + 9 > len) return 0;
            int lflags = d[p + 8];
            p += 9;
            if (lflags & 0x80) p += 3 * (2 << (lflags & 7));
            if (p >= len) return 0;
            p++;                                         // LZW minimum code size
            p = imd_gif_skip_blocks(d, len, p);
            if (p < 0) return 0;
            (*frames)++;
            continue;
        }

        return 0;                                        // unknown block, give up
    }
}

imd_gif* imd_gif_open(const unsigned char* data, int len, int max_dimension,
                      int* w, int* h, int* frames)
{
    if (frames) *frames = 0;
    if (!imd_gif_is_gif(data, len)) return 0;
    if (len < 14 || max_dimension <= 0) return 0;

    // The logical screen size sits at offset 6, little endian.
    int width = data[6] | (data[7] << 8);
    int height = data[8] | (data[9] << 8);
    if (width <= 0 || height <= 0) return 0;
    if (width > max_dimension || height > max_dimension) return 0;
    // Four canvases have to fit in an int, and well inside memory besides.
    if (width > (1 << 28) / height / 4) return 0;

    int frame_count = 0;
    int needs_history = 0;
    if (!imd_gif_scan(data, len, &frame_count, &needs_history))
    {
        // Truncated or unusual, but stb may still get frames out of it. Assume
        // it animates and pay for the disposal-3 buffers.
        frame_count = 0;
        needs_history = 1;
    }

    imd_gif* g = (imd_gif*)STBI_MALLOC(sizeof(imd_gif));
    if (!g) return 0;
    memset(g, 0, sizeof(imd_gif));

    g->data = (unsigned char*)STBI_MALLOC(len);
    if (!g->data)
    {
        STBI_FREE(g);
        return 0;
    }
    memcpy(g->data, data, len);
    g->len = len;

    g->width = width;
    g->height = height;
    g->frame_bytes = width * height * 4;
    g->frames = frame_count;

    // Only worth carrying when a third frame can actually ask for it.
    if (needs_history && (frame_count == 0 || frame_count > 2))
    {
        g->history[0] = (unsigned char*)STBI_MALLOC(g->frame_bytes);
        g->history[1] = (unsigned char*)STBI_MALLOC(g->frame_bytes);
        if (!g->history[0] || !g->history[1])
        {
            imd_gif_close(g);
            return 0;
        }
    }

    stbi__start_mem(&g->ctx, g->data, g->len);

    if (w) *w = width;
    if (h) *h = height;
    if (frames) *frames = frame_count;
    return g;
}

const unsigned char* imd_gif_next(imd_gif* g, int* delay_ms)
{
    if (!g || g->finished) return 0;

    // Frame i needs frame i-2, which is the slot that is about to be reused.
    // Before the third frame there is nothing to restore and stb falls back to
    // the background, which is what it would show anyway.
    unsigned char* two_back = (g->history[0] && g->index >= 2) ? g->history[g->index & 1] : 0;

    int comp = 0;
    unsigned char* frame = (unsigned char*)stbi__gif_load_next(&g->ctx, &g->state, &comp, 4, two_back);

    // stb returns the context pointer itself when it reaches the trailer.
    if (!frame || frame == (unsigned char*)&g->ctx)
    {
        g->finished = 1;
        return 0;
    }

    // Safe to clobber the slot now: stb read it during disposal, at the top of
    // the call, before touching the canvas.
    if (g->history[0]) memcpy(g->history[g->index & 1], frame, g->frame_bytes);
    g->index++;

    // stb already converts the delay to milliseconds. Zero and 10 ms are both
    // encoder shorthand for "as fast as you can"; browsers substitute 100 ms
    // and animations are authored expecting that.
    int delay = g->state.delay;
    if (delay < 20) delay = 100;
    if (delay_ms) *delay_ms = delay;
    return frame;
}

void imd_gif_rewind(imd_gif* g)
{
    if (!g) return;

    if (g->state.out) STBI_FREE(g->state.out);
    if (g->state.history) STBI_FREE(g->state.history);
    if (g->state.background) STBI_FREE(g->state.background);

    // A zeroed state is what stb itself starts a decode from, and out being
    // null is what makes the next call re-read the header.
    memset(&g->state, 0, sizeof(g->state));

    // The history slots need no clearing: neither is read before both have
    // been written again.
    stbi__start_mem(&g->ctx, g->data, g->len);
    g->index = 0;
    g->finished = 0;
}

int imd_gif_bytes(const imd_gif* g)
{
    if (!g) return 0;

    int bytes = (int)sizeof(imd_gif) + g->len;
    if (g->history[0]) bytes += 2 * g->frame_bytes;
    // stb's canvas and background are allocated on the first frame.
    if (g->state.out) bytes += 2 * g->frame_bytes + g->width * g->height;
    return bytes;
}

void imd_gif_close(imd_gif* g)
{
    if (!g) return;

    if (g->state.out) STBI_FREE(g->state.out);
    if (g->state.history) STBI_FREE(g->state.history);
    if (g->state.background) STBI_FREE(g->state.background);
    if (g->history[0]) STBI_FREE(g->history[0]);
    if (g->history[1]) STBI_FREE(g->history[1]);
    if (g->data) STBI_FREE(g->data);

    STBI_FREE(g);
}
