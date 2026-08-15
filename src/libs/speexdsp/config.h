#pragma once

// speexdsp is normally configured by autotools, which this project does not
// use. Only the preprocessor (noise suppression + AGC) is built, so the
// settings below cover that path and nothing else.
//
// Note that arch.h in this tree has been patched to force FIXED_POINT, so the
// arithmetic mode is not chosen here. OUTSIDE_SPEEX is deliberately left
// undefined: it would suppress the speexdsp_types.h include that defines
// spx_int16_t and friends.

// No DLL export decoration: everything is linked statically.
#define EXPORT

// kiss_fft rather than smallft; it is the smaller of the two backends.
#define USE_KISS_FFT
