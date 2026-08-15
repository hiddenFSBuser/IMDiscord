#include "pch.h"
#include "customcrt_encoding.h"
#include "customcrt_text.h"

// helpers
static const char g_base64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static int getbase64val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static bool isutf7safe(wchar_t c) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) return true;
    if (c == ' ' || c == '\'' || c == '(' || c == ')' || c == ',' || c == '-' || c == '.' || c == '/' || c == ':' || c == '?') return true;
    return false;
}

// main
int utf8to16(const char* src, int src_len, wchar_t* dst, int dst_len) {
    if (!src) return 0;
    if (src_len == -1) src_len = (int)ccslenf(src);

    int written = 0;
    const unsigned char* p = (const unsigned char*)src;
    const unsigned char* end = p + src_len;

    if (!dst || dst_len == 0) {
        while (p < end) {
            written++;
            if (*p < 0x80) p++;
            else if ((*p & 0xE0) == 0xC0) p += 2;
            else if ((*p & 0xF0) == 0xE0) p += 3;
            else if ((*p & 0xF8) == 0xF0) { p += 4; written++; }
            else p++;
        }
        return written + 1;
    }

    wchar_t* out = dst;
    wchar_t* out_end = dst + dst_len;

    while (p < end && out < out_end) {
        unsigned int code_point = 0;

        if (*p < 0x80) {
            code_point = *p++;
        }
        else if ((*p & 0xE0) == 0xC0) {
            code_point = (*p++ & 0x1F) << 6;
            if (p < end) code_point |= (*p++ & 0x3F);
        }
        else if ((*p & 0xF0) == 0xE0) {
            code_point = (*p++ & 0x0F) << 12;
            if (p < end) code_point |= (*p++ & 0x3F) << 6;
            if (p < end) code_point |= (*p++ & 0x3F);
        }
        else if ((*p & 0xF8) == 0xF0) {
            code_point = (*p++ & 0x07) << 18;
            if (p < end) code_point |= (*p++ & 0x3F) << 12;
            if (p < end) code_point |= (*p++ & 0x3F) << 6;
            if (p < end) code_point |= (*p++ & 0x3F);
        }
        else {
            p++; continue;
        }

        if (code_point <= 0xFFFF) {
            *out++ = (wchar_t)code_point;
            written++;
        }
        else {
            if (out + 1 < out_end) {
                code_point -= 0x10000;
                *out++ = (wchar_t)((code_point >> 10) + 0xD800);
                *out++ = (wchar_t)((code_point & 0x3FF) + 0xDC00);
                written += 2;
            }
            else {
                break;
            }
        }
    }

    if (out < out_end) *out = 0;

    return written;
}

int utf16to8(const wchar_t* src, int src_len, char* dst, int dst_len) {
    if (!src) return 0;
    if (src_len == -1) src_len = (int)ccwlen(src);

    int written = 0;
    const wchar_t* p = (const wchar_t*)src;
    const wchar_t* end = p + src_len;

    if (!dst || dst_len == 0) {
        while (p < end) {
            unsigned int c = *p++;
            if (c < 0x80) written += 1;
            else if (c < 0x800) written += 2;
            else if (c >= 0xD800 && c <= 0xDBFF && p < end) {
                written += 4;
                p++;
            }
            else written += 3;
        }
        return written + 1;
    }

    char* out = dst;
    char* out_end = dst + dst_len;

    while (p < end && out < out_end) {
        unsigned int c = *p++;

        if (c < 0x80) {
            *out++ = (char)c;
            written++;
        }
        else if (c < 0x800) {
            if (out + 1 >= out_end) break;
            *out++ = (char)(0xC0 | (c >> 6));
            *out++ = (char)(0x80 | (c & 0x3F));
            written += 2;
        }
        else if (c >= 0xD800 && c <= 0xDBFF && p < end) {
            unsigned int c2 = *p++;
            unsigned int code = 0x10000 + (((c & 0x3FF) << 10) | (c2 & 0x3FF));

            if (out + 3 >= out_end) break;
            *out++ = (char)(0xF0 | (code >> 18));
            *out++ = (char)(0x80 | ((code >> 12) & 0x3F));
            *out++ = (char)(0x80 | ((code >> 6) & 0x3F));
            *out++ = (char)(0x80 | (code & 0x3F));
            written += 4;
        }
        else {
            if (out + 2 >= out_end) break;
            *out++ = (char)(0xE0 | (c >> 12));
            *out++ = (char)(0x80 | ((c >> 6) & 0x3F));
            *out++ = (char)(0x80 | (c & 0x3F));
            written += 3;
        }
    }

    if (out < out_end) *out = 0;
    return written;
}

int utf16to7(const wchar_t* src, int src_len, char* dst, int dst_len) {
    if (!src) return 0;
    if (src_len == -1) src_len = (int)ccwlen(src);

    unsigned long long bit_buffer = 0;
    int bit_count = 0;
    bool in_shift = false;

    int written = 0;
    char* out = dst;
    char* out_end = dst ? dst + dst_len : (char*)-1;

    for (int i = 0; i < src_len; i++) {
        wchar_t c = src[i];

        if (isutf7safe(c)) {
            if (in_shift) {
                if (bit_count > 0) {
                    bit_buffer <<= (6 - bit_count);
                    if (dst) { if (out < out_end) *out++ = g_base64[bit_buffer & 0x3F]; else break; }
                    written++;
                }
                bit_buffer = 0; bit_count = 0;
                if (dst) { if (out < out_end) *out++ = '-'; else break; }
                written++;
                in_shift = false;
            }

            if (dst) { if (out < out_end) *out++ = (char)c; else break; }
            written++;

            if (c == '+') {
                if (dst) { if (out < out_end) *out++ = '-'; else break; }
                written++;
            }
        }
        else {
            if (!in_shift) {
                if (dst) { if (out < out_end) *out++ = '+'; else break; }
                written++;
                in_shift = true;
            }

            bit_buffer = (bit_buffer << 16) | c;
            bit_count += 16;

            while (bit_count >= 6) {
                bit_count -= 6;
                int idx = (bit_buffer >> bit_count) & 0x3F;
                if (dst) { if (out < out_end) *out++ = g_base64[idx]; else break; }
                written++;
            }
        }
    }

    if (in_shift) {
        if (bit_count > 0) {
            bit_buffer <<= (6 - bit_count);
            if (dst) { if (out < out_end) *out++ = g_base64[bit_buffer & 0x3F]; }
            written++;
        }
        if (dst) { if (out < out_end) *out++ = '-'; }
        written++;
    }

    if (dst && out < out_end) *out = 0;
    return written;
}

int utf7to16(const char* src, int src_len, wchar_t* dst, int dst_len) {
    if (!src) return 0;
    if (src_len == -1) src_len = (int)ccslenf(src);

    int written = 0;
    wchar_t* out = dst;
    wchar_t* out_end = dst ? dst + dst_len : (wchar_t*)-1;

    unsigned long long bit_buffer = 0;
    int bit_count = 0;
    bool in_shift = false;

    for (int i = 0; i < src_len; i++) {
        char c = src[i];

        if (!in_shift) {
            if (c == '+') {
                if (i + 1 < src_len && src[i + 1] == '-') {
                    if (dst) { if (out < out_end) *out++ = '+'; else break; }
                    written++;
                    i++;
                }
                else {
                    in_shift = true;
                    bit_buffer = 0;
                    bit_count = 0;
                }
            }
            else {
                if (dst) { if (out < out_end) *out++ = (wchar_t)c; else break; }
                written++;
            }
        }
        else {
            if (c == '-') {
                in_shift = false;
            }
            else {
                int val = getbase64val(c);
                if (val == -1) {
                    in_shift = false;
                    i--;
                    continue;
                }

                bit_buffer = (bit_buffer << 6) | val;
                bit_count += 6;

                if (bit_count >= 16) {
                    bit_count -= 16;
                    wchar_t wc = (wchar_t)((bit_buffer >> bit_count) & 0xFFFF);
                    if (dst) { if (out < out_end) *out++ = wc; else break; }
                    written++;
                }
            }
        }
    }

    if (dst && out < out_end) *out = 0;
    return written;
}