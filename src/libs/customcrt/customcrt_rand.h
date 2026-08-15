#pragma once

#define CCRAND_MAX 0x7fff

void ccsrand(unsigned int seed);
int ccrand();
void ccrand_init();
int ccrand_range(int min, int max);
float ccrand_float();