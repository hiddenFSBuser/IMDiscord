#include "pch.h"
#include "customcrt_clock.h"

cctp cclock::now() {
    static long long frequency = 0;
    if (frequency == 0) {
        LARGE_INTEGER li;
        QueryPerformanceFrequency(&li);
        frequency = li.QuadPart;
    }

    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);

    long long seconds = li.QuadPart / frequency;
    long long fraction = li.QuadPart % frequency;
    long long ns = (seconds * 1000000000LL) + (fraction * 1000000000LL / frequency);

    return cctp{ ns };
}