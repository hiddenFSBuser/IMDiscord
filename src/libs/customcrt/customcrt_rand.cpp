#include "pch.h"
#include "customcrt_rand.h"
#include <intrin.h> 

unsigned int g_rand_seed = 1;

void ccsrand(unsigned int seed) {
    g_rand_seed = seed;
}

int ccrand() {
    g_rand_seed = (g_rand_seed * 214013 + 2531011);
    return (g_rand_seed >> 16) & CCRAND_MAX;
}

void ccrand_init() {
    ccsrand((unsigned int)__rdtsc());
}

int ccrand_range(int min, int max) {
    if (min >= max) return min;
    return min + (ccrand() % (max - min + 1));
}

float ccrand_float() {
    return (float)ccrand() / (float)CCRAND_MAX;
}