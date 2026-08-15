#pragma once
#include "../../libs/customcrt/customcrt.h"

// Growable byte buffer with explicit lifetime (init/free) so it can be stored
// inside ulist without relying on copy constructors.
struct ubuffer
{
    unsigned char* data;
    unsigned int size;
    unsigned int cap;

    void init();
    void init(unsigned int reserve);
    void free_buffer();
    void clear();

    void reserve(unsigned int want);
    void append(const void* src, unsigned int len);
    void append_str(const char* str);
    void append_char(char c);
    void append_fmt(const char* fmt, ...);
    // Appends str with JSON string escaping, without the surrounding quotes.
    void append_json_escaped(const char* str, int len = -1);
    void append_url_encoded(const char* str, int len = -1);

    // Ensures a trailing zero without counting it in size, so data can be used
    // as a C string.
    const char* c_str();
    unsigned char* take(unsigned int* out_size);
};
