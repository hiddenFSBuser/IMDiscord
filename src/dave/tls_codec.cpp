#include "pch.h"
#include "tls_codec.h"
#include "core/crypto.h"

// ---------------------------------------------------------------------------
// writer
// ---------------------------------------------------------------------------

void tls_writer::init()
{
    buf.init(256);
}

void tls_writer::init(unsigned int reserve)
{
    buf.init(reserve);
}

void tls_writer::free_writer()
{
    buf.free_buffer();
}

void tls_writer::u8(unsigned char v)
{
    buf.append_char((char)v);
}

void tls_writer::u16(unsigned short v)
{
    unsigned char b[2] = { (unsigned char)(v >> 8), (unsigned char)v };
    buf.append(b, 2);
}

void tls_writer::u32(unsigned int v)
{
    unsigned char b[4] = {
        (unsigned char)(v >> 24), (unsigned char)(v >> 16),
        (unsigned char)(v >> 8), (unsigned char)v
    };
    buf.append(b, 4);
}

void tls_writer::u64(unsigned long long v)
{
    unsigned char b[8];
    for (int i = 0; i < 8; i++) b[i] = (unsigned char)(v >> (56 - i * 8));
    buf.append(b, 8);
}

void tls_writer::raw(const void* data, unsigned int len)
{
    buf.append(data, len);
}

void tls_writer::varint(unsigned int v)
{
    unsigned char b[4];
    unsigned int n = crypto::varint_write(b, v);
    buf.append(b, n);
}

void tls_writer::opaque(const void* data, unsigned int len)
{
    varint(len);
    if (len) buf.append(data, len);
}

// ---------------------------------------------------------------------------
// reader
// ---------------------------------------------------------------------------

void tls_reader::init(const void* data, unsigned int len)
{
    base = (const unsigned char*)data;
    size = len;
    pos = 0;
    failed = (data == 0 && len != 0);
}

bool tls_reader::u8(unsigned char* out)
{
    if (failed || pos + 1 > size) { failed = true; return false; }
    *out = base[pos++];
    return true;
}

bool tls_reader::u16(unsigned short* out)
{
    if (failed || pos + 2 > size) { failed = true; return false; }
    *out = (unsigned short)((base[pos] << 8) | base[pos + 1]);
    pos += 2;
    return true;
}

bool tls_reader::u32(unsigned int* out)
{
    if (failed || pos + 4 > size) { failed = true; return false; }
    *out = ((unsigned int)base[pos] << 24) | ((unsigned int)base[pos + 1] << 16) |
           ((unsigned int)base[pos + 2] << 8) | base[pos + 3];
    pos += 4;
    return true;
}

bool tls_reader::u64(unsigned long long* out)
{
    if (failed || pos + 8 > size) { failed = true; return false; }
    unsigned long long v = 0;
    for (int i = 0; i < 8; i++) v = (v << 8) | base[pos + i];
    pos += 8;
    *out = v;
    return true;
}

bool tls_reader::raw(void* out, unsigned int len)
{
    if (failed || pos + len > size) { failed = true; return false; }
    if (len) ccpy(out, base + pos, len);
    pos += len;
    return true;
}

bool tls_reader::varint(unsigned int* out)
{
    if (failed || pos + 1 > size) { failed = true; return false; }

    unsigned char first = base[pos];
    unsigned int prefix = (unsigned int)(first >> 6);

    if (prefix == 0)
    {
        *out = first & 0x3F;
        pos += 1;
        return true;
    }
    if (prefix == 1)
    {
        if (pos + 2 > size) { failed = true; return false; }
        *out = ((unsigned int)(first & 0x3F) << 8) | base[pos + 1];
        pos += 2;
        return true;
    }
    if (prefix == 2)
    {
        if (pos + 4 > size) { failed = true; return false; }
        *out = ((unsigned int)(first & 0x3F) << 24) | ((unsigned int)base[pos + 1] << 16) |
               ((unsigned int)base[pos + 2] << 8) | base[pos + 3];
        pos += 4;
        return true;
    }

    // Prefix 3 is reserved and must be rejected.
    failed = true;
    return false;
}

bool tls_reader::opaque(const unsigned char** out, unsigned int* out_len)
{
    unsigned int len = 0;
    if (!varint(&len)) return false;
    if (pos + len > size) { failed = true; return false; }

    *out = base + pos;
    *out_len = len;
    pos += len;
    return true;
}

bool tls_reader::skip(unsigned int len)
{
    if (failed || pos + len > size) { failed = true; return false; }
    pos += len;
    return true;
}
