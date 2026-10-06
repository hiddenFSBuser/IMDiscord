#pragma once

// Build-wide miniz configuration for this client, force-included above
// miniz.h on every miniz translation unit (see build.bat).
//
// No CRT beyond what customcrt already provides: no stdio (only heap
// archives are used), no clock (fixed 1980 stamps), no assert (release
// builds define NDEBUG anyway, debug ones have no _wassert to link).
#define MINIZ_NO_STDIO 1
#define MINIZ_NO_TIME 1
#define MZ_ASSERT(x) ((void)0)
