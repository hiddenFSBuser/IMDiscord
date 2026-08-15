#include "pch.h"
#include "ubuffer.h"

void ubuffer::init()
{
    data = 0;
    size = 0;
    cap = 0;
}

void ubuffer::init(unsigned int reserve_size)
{
    data = 0;
    size = 0;
    cap = 0;
    reserve(reserve_size);
}

void ubuffer::free_buffer()
{
    if (data) memfree(data);
    data = 0;
    size = 0;
    cap = 0;
}

void ubuffer::clear()
{
    size = 0;
}

void ubuffer::reserve(unsigned int want)
{
    // +1 keeps room for the implicit terminator handed out by c_str().
    if (cap >= want + 1) return;

    unsigned int newcap = cap ? cap : 256;
    while (newcap < want + 1) newcap *= 2;

    unsigned char* nd = (unsigned char*)memalloc((int)newcap);
    if (!nd) return;
    if (size) ccpy(nd, data, size);
    if (data) memfree(data);
    data = nd;
    cap = newcap;
}

void ubuffer::append(const void* src, unsigned int len)
{
    if (!len) return;
    reserve(size + len);
    if (cap < size + len + 1) return;
    ccpy(data + size, src, len);
    size += len;
}

void ubuffer::append_str(const char* str)
{
    if (!str) return;
    append(str, (unsigned int)ccslenf(str));
}

void ubuffer::append_char(char c)
{
    reserve(size + 1);
    if (cap < size + 2) return;
    data[size++] = (unsigned char)c;
}

void ubuffer::append_fmt(const char* fmt, ...)
{
    char tmp[2048];
    va_list args;
    va_start(args, fmt);
    int n = cvnprint(tmp, sizeof(tmp), fmt, args);
    va_end(args);
    if (n < 0) return;
    if (n > (int)sizeof(tmp) - 1) n = (int)sizeof(tmp) - 1;
    append(tmp, (unsigned int)n);
}

void ubuffer::append_json_escaped(const char* str, int len)
{
    if (!str) return;
    if (len < 0) len = (int)ccslenf(str);

    for (int i = 0; i < len; i++)
    {
        unsigned char c = (unsigned char)str[i];
        switch (c)
        {
        case '\"': append("\\\"", 2); break;
        case '\\': append("\\\\", 2); break;
        case '\n': append("\\n", 2); break;
        case '\r': append("\\r", 2); break;
        case '\t': append("\\t", 2); break;
        case '\b': append("\\b", 2); break;
        case '\f': append("\\f", 2); break;
        default:
            if (c < 0x20)
            {
                const char* hex = "0123456789abcdef";
                char esc[6] = { '\\', 'u', '0', '0', hex[(c >> 4) & 0xF], hex[c & 0xF] };
                append(esc, 6);
            }
            else
            {
                append_char((char)c);
            }
            break;
        }
    }
}

void ubuffer::append_url_encoded(const char* str, int len)
{
    if (!str) return;
    if (len < 0) len = (int)ccslenf(str);

    const char* hex = "0123456789ABCDEF";
    for (int i = 0; i < len; i++)
    {
        unsigned char c = (unsigned char)str[i];
        bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                          (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
        if (unreserved)
        {
            append_char((char)c);
        }
        else
        {
            char esc[3] = { '%', hex[(c >> 4) & 0xF], hex[c & 0xF] };
            append(esc, 3);
        }
    }
}

const char* ubuffer::c_str()
{
    reserve(size + 1);
    if (!data) return "";
    data[size] = 0;
    return (const char*)data;
}

unsigned char* ubuffer::take(unsigned int* out_size)
{
    unsigned char* d = data;
    if (out_size) *out_size = size;
    data = 0;
    size = 0;
    cap = 0;
    return d;
}
