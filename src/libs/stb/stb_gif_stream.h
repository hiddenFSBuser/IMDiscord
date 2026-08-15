#pragma once

// Frame-at-a-time GIF decoding, implemented in stb_impl.cpp on top of stb's
// internal incremental decoder.
//
// The public stbi_load_gif_from_memory expands the whole animation into one
// allocation, which is unusable for chat: a twenty second GIF costs hundreds of
// megabytes and a visible stall. Here the compressed file plus at most three
// canvases stay resident and frames are produced on demand.

struct imd_gif;

// Cheap magic check, safe on any buffer.
int imd_gif_is_gif(const unsigned char* data, int len);

// Copies the compressed data and prepares the decoder. Fails for anything
// larger than max_dimension on either axis, which bounds the canvases.
//
// frames is filled from a pass over the block structure that decodes nothing,
// so the caller can tell a real animation from the many single frame GIFs that
// arrive as avatars and emoji.
imd_gif* imd_gif_open(const unsigned char* data, int len, int max_dimension,
                      int* w, int* h, int* frames);

// Decodes the next frame. The returned RGBA canvas is owned by the decoder and
// stays valid until the next call. Returns null once the animation ends.
// delay_ms is already clamped the way browsers clamp it.
const unsigned char* imd_gif_next(imd_gif* g, int* delay_ms);

// Restarts from the first frame, for looping.
void imd_gif_rewind(imd_gif* g);

// Bytes the decoder holds on to, for the cache's memory counter.
int imd_gif_bytes(const imd_gif* g);

void imd_gif_close(imd_gif* g);
