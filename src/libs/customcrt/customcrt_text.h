#pragma once

size_t ccslenf(const char* str); // fast char len
size_t ccslen(const char* str);
size_t ccwlen(const void* str);

int ccscmpf(const char* s1, const char* s2); // fast char cmp
int ccscmp(const char* s1, const char* s2);
int ccwcmp(const wchar_t* s1, const wchar_t* s2);
int ccwcmpf(const wchar_t* s1, const wchar_t* s2); // fast wchar cmp

int ccsncmp(const char* s1, const char* s2, size_t n);
int ccsncmpf(const char* s1, const char* s2, size_t n); // fast char lengthed cmp

int ccscmpi(const char* s1, const char* s2);
int ccwcmpi(const wchar_t* s1, const wchar_t* s2);
int ccscmpif(const char* s1, const char* s2); // fast char i cmp
int ccwcmpif(const wchar_t* s1, const wchar_t* s2); // fast wchar i cmp

unsigned int ccscrc32(const char* s);
unsigned __int64 ccscrc64(const char* s);

char* ccstrptr(unsigned long long val, char* buffer, size_t buffer_size);
inline char* ccstrptr(void* val, char* buffer, size_t buffer_size) { return ccstrptr((void*)val, buffer, buffer_size); }
const char* ccstrstr(const char* haystack, const char* needle);
char* ccstrtok(char* str, const char* delim);
int ccstrti(const char* label); // string to int
int ccstrthi(const char* label); // hex string to int
unsigned __int64 ccstrthill(const char* label); // hex string to int64
float ccstrtf(const char* s); // string to float
char* ccstrncpy(char* dst, const char* src, size_t count);
void custom_qsort(void* base, int count, int size, int(__cdecl* compare)(const void*, const void*));

long ccstrtol(const char* nptr, char** endptr, int base);
long long ccstrtoll(const char* nptr, char** endptr, int base);
unsigned long long ccstrtoull(const char* nptr, char** endptr, int base);

wchar_t cctowlower(wchar_t wc);
char cctolower(char c);