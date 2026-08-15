#pragma once
#include "ubuffer.h"

// TLS presentation language codec as MLS uses it (RFC 9420 section 2).
//
// Integers are big endian. Variable length vectors carry the MLS varint length
// prefix rather than TLS 1.3's fixed-width one, so the length of a nested
// structure is only known after it has been written - nested content is built
// in a child writer and then folded in with write_opaque.

struct tls_writer
{
    ubuffer buf;

    void init();
    void init(unsigned int reserve);
    void free_writer();
    void clear() { buf.clear(); }

    void u8(unsigned char v);
    void u16(unsigned short v);
    void u32(unsigned int v);
    void u64(unsigned long long v);
    void raw(const void* data, unsigned int len);
    void varint(unsigned int v);

    // varint length prefix followed by the bytes.
    void opaque(const void* data, unsigned int len);
    void opaque(const tls_writer& child) { opaque(child.buf.data, child.buf.size); }

    const unsigned char* data() const { return buf.data; }
    unsigned int size() const { return buf.size; }
};

struct tls_reader
{
    const unsigned char* base;
    unsigned int size;
    unsigned int pos;
    bool failed;

    void init(const void* data, unsigned int len);

    bool u8(unsigned char* out);
    bool u16(unsigned short* out);
    bool u32(unsigned int* out);
    bool u64(unsigned long long* out);
    bool raw(void* out, unsigned int len);
    bool varint(unsigned int* out);

    // Returns a pointer into the source buffer; no copy is made.
    bool opaque(const unsigned char** out, unsigned int* out_len);
    bool skip(unsigned int len);

    unsigned int remaining() const { return failed ? 0 : size - pos; }
    bool ok() const { return !failed; }
    bool done() const { return !failed && pos == size; }
};
