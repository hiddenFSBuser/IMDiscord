/* zstd 1.5.7, built as one unit. The header next to this file holds the
 * whole amalgamation; this translation unit is the one that turns the
 * implementation on. The http layer uses the decoder half (ZSTD_decompress)
 * for content-encoding: zstd. */
#define ZSTD_IMPLEMENTATION
#include "zstd_single_sse2.h"
