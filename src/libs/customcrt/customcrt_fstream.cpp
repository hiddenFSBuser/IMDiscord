#include "pch.h"
#include <vadefs.h>

//void ccputchar(char c) {
//    putchar(c);
//}
//void ccputwchar(wchar_t c) {
//    putwchar(c);
//}

unsigned long long ccpow10(int n) {
    unsigned long long r = 1;
    for (int i = 0; i < n; i++) r *= 10;
    return r;
}

void reverse_str(char* start, char* end) {
    while (start < end) {
        char tmp = *start;
        *start = *end;
        *end = tmp;
        start++;
        end--;
    }
}

static double fast_pow10(int p) {
    static const double pows[] = {
        1.0, 10.0, 100.0, 1000.0, 10000.0, 100000.0, 1000000.0, 10000000.0, 100000000.0
    };
    if (p >= 0 && p < sizeof(pows) / sizeof(double)) return pows[p];
    double res = 1.0;
    while (p-- > 0) res *= 10.0;
    return res;
}

int cvnprint(char* buffer, size_t count, const char* format, va_list args) {
    if (!buffer || count == 0) return 0;

    char* dst = buffer;
    const char* end = buffer + count - 1;
    const char* ptr = format;

    char num_buf[64];

    while (*ptr && dst < end) {
        if (*ptr != '%') {
            *dst++ = *ptr++;
            continue;
        }

        ptr++;

        int pad_zero = 0;
        if (*ptr == '0') {
            pad_zero = 1;
            ptr++;
        }

        int width = 0;
        while (*ptr >= '0' && *ptr <= '9') {
            width = width * 10 + (*ptr - '0');
            ptr++;
        }

        int precision = 6;
        if (*ptr == '.') {
            ptr++;
            precision = 0;
            while (*ptr >= '0' && *ptr <= '9') {
                precision = precision * 10 + (*ptr - '0');
                ptr++;
            }
        }

        int is_long = 0;
        int is_long_long = 0;
        if (*ptr == 'l') {
            ptr++;
            if (*ptr == 'l') {
                is_long_long = 1;
                ptr++;
            }
            else {
                is_long = 1;
            }
        }
        // "h" and "hh" say the argument is narrower than an int. Both are
        // promoted to int on the way in, so nothing has to be read
        // differently - but skipping the letters matters: left in place they
        // were taken as the conversion itself, and "%04hu" printed the word
        // "hu" instead of a number. That is how a certificate date came out
        // as gibberish and every https connection was refused as not yet
        // valid.
        else if (*ptr == 'h') {
            ptr++;
            if (*ptr == 'h') ptr++;
        }
        else if (*ptr == 'z' || *ptr == 'j' || *ptr == 't') {
            // size_t and its relatives are pointer sized here.
            is_long_long = 1;
            ptr++;
        }

        switch (*ptr) {
        case 'c': {
            int ch = va_arg(args, int);
            if (dst < end) {
                *dst++ = (char)ch;
            }
            break;
        }
        case 's': {
            const char* s = va_arg(args, const char*);
            if (!s) s = xor_a("(null)");
            while (*s && dst < end) {
                *dst++ = *s++;
            }
            break;
        }
        case 'd':
        case 'i':
        case 'u': {
            unsigned long long uval;
            int is_negative = 0;
            int is_signed = (*ptr != 'u');

            if (is_signed) {
                long long val;
                if (is_long_long) {
                    val = va_arg(args, long long);
                }
                else if (is_long) {
                    val = va_arg(args, long);
                }
                else {
                    val = va_arg(args, int);
                }

                if (val < 0) {
                    is_negative = 1;
                    uval = (unsigned long long) - val;
                }
                else {
                    uval = (unsigned long long)val;
                }
            }
            else {
                if (is_long_long) {
                    uval = va_arg(args, unsigned long long);
                }
                else if (is_long) {
                    uval = va_arg(args, unsigned long);
                }
                else {
                    uval = va_arg(args, unsigned int);
                }
            }

            char* tmp_ptr = num_buf + sizeof(num_buf);
            *--tmp_ptr = '\0';

            if (uval == 0) {
                *--tmp_ptr = '0';
            }
            else {
                while (uval > 0) {
                    *--tmp_ptr = (uval % 10) + '0';
                    uval /= 10;
                }
            }

            int len = (int)((num_buf + sizeof(num_buf)) - tmp_ptr - 1);

            if (is_negative) {
                width--;
            }

            if (!pad_zero) {
                while (width > len && dst < end) {
                    *dst++ = ' ';
                    width--;
                }
            }

            if (is_negative && dst < end) {
                *dst++ = '-';
            }

            if (pad_zero) {
                while (width > len && dst < end) {
                    *dst++ = '0';
                    width--;
                }
            }

            while (*tmp_ptr && dst < end) {
                *dst++ = *tmp_ptr++;
            }
            break;
        }
        case 'p': {
            uintptr_t val = va_arg(args, uintptr_t);
            char* tmp_ptr = num_buf + sizeof(num_buf);

            const char* g_hex_lookup = xor_a("0123456789ABCDEF");

            int nibbles = sizeof(void*) * 2;
            for (int i = 0; i < nibbles; i++) {
                *--tmp_ptr = g_hex_lookup[val & 0xF];
                val >>= 4;
            }

            while (nibbles-- > 0 && dst < end) {
                *dst++ = *tmp_ptr++;
            }
            break;
        }
        case 'f': {
            double val = va_arg(args, double);
            if (val < 0) {
                if (dst < end) *dst++ = '-';
                val = -val;
            }

            double mult = fast_pow10(precision);
            val += 0.5 / mult;

            unsigned long long int_part = (unsigned long long)val;
            double frac_part = val - (double)int_part;

            char* tmp_ptr = num_buf + sizeof(num_buf);
            *--tmp_ptr = '\0';

            if (int_part == 0) {
                *--tmp_ptr = '0';
            }
            else {
                unsigned long long temp = int_part;
                while (temp > 0) {
                    *--tmp_ptr = (temp % 10) + '0';
                    temp /= 10;
                }
            }

            while (*tmp_ptr && dst < end) *dst++ = *tmp_ptr++;

            if (precision > 0 && dst < end) {
                *dst++ = '.';
                while (precision-- > 0 && dst < end) {
                    frac_part *= 10.0;
                    int digit = (int)frac_part;
                    *dst++ = digit + '0';
                    frac_part -= digit;
                }
            }
            break;
        }
        case 'X':
        case 'x': {
            unsigned long long val;
            if (is_long_long) {
                val = va_arg(args, unsigned long long);
            }
            else if (is_long) {
                val = va_arg(args, unsigned long);
            }
            else {
                val = va_arg(args, unsigned int);
            }

            const char* hex_chars = (*ptr == 'X')
                ? xor_a("0123456789ABCDEF")
                : xor_a("0123456789abcdef");

            char* tmp_ptr = num_buf + sizeof(num_buf);
            *--tmp_ptr = '\0';

            if (val == 0) {
                *--tmp_ptr = '0';
            }
            else {
                while (val > 0) {
                    *--tmp_ptr = hex_chars[val & 0xF];
                    val >>= 4;
                }
            }

            int len = (int)((num_buf + sizeof(num_buf)) - tmp_ptr - 1);

            if (!pad_zero) {
                while (width > len && dst < end) {
                    *dst++ = ' ';
                    width--;
                }
            }

            if (pad_zero) {
                while (width > len && dst < end) {
                    *dst++ = '0';
                    width--;
                }
            }

            while (*tmp_ptr && dst < end) {
                *dst++ = *tmp_ptr++;
            }
            break;
        }
        case '%':
            if (dst < end) *dst++ = '%';
            break;
        default:
            if (dst < end) *dst++ = '%';
            if (dst < end) *dst++ = *ptr;
            break;
        }
        ptr++;
    }

    *dst = '\0';
    return (int)(dst - buffer);
}
int cwvnprint(wchar_t* buffer, size_t count, const wchar_t* format, va_list args) {
    if (!buffer || count == 0) return 0;

    size_t written = 0;
    const wchar_t* ptr = format;

    size_t max_write = count - 1;

    auto out = [&](wchar_t c) {
        if (written < max_write) {
            buffer[written] = c;
        }
        written++;
        };

    while (*ptr) {
        if (*ptr == L'%') {
            ptr++;

            int precision = 6;
            bool precision_specified = false;

            if (*ptr == L'.') {
                ptr++;
                precision = 0;
                precision_specified = true;
                while (*ptr >= L'0' && *ptr <= L'9') {
                    precision = precision * 10 + (*ptr - L'0');
                    ptr++;
                }
            }

            switch (*ptr) {
            case L's': { // Wide String
                const wchar_t* s = va_arg(args, const wchar_t*);
                if (!s) s = xor_w(L"(null)");
                while (*s) out(*s++);
                break;
            }
            case L'd':
            case L'i': { // Signed Integer
                int val = va_arg(args, int);
                if (val < 0) {
                    out(L'-');
                    val = -val;
                }
                if (val == 0) {
                    out(L'0');
                }
                else {
                    wchar_t num_buf[32];
                    int i = 0;
                    unsigned int uval = (unsigned int)val;
                    while (uval > 0) {
                        num_buf[i++] = (uval % 10) + L'0';
                        uval /= 10;
                    }
                    while (i > 0) out(num_buf[--i]);
                }
                break;
            }
            case L'p': { // Pointer
                uintptr val = va_arg(args, uintptr);
                //out(L'0'); out(L'x');
                if (val == 0) {
                    out(L'0');
                }
                else {
                    wchar_t num_buf[20];
                    int i = 0;
                    while (val > 0) {
                        int digit = val % 16;
                        num_buf[i++] = (digit < 10) ? (digit + L'0') : (digit - 10 + L'A');
                        val /= 16;
                    }
                    while (i > 0) out(num_buf[--i]);
                }
                break;
            }
            case L'f': { // Double/Float
                double val = va_arg(args, double);

                if (val < 0) {
                    out(L'-');
                    val = -val;
                }

                double rounding = 0.5 / (double)ccpow10(precision);
                val += rounding;

                unsigned long long int_part = (unsigned long long)val;
                double frac_part = val - (double)int_part;

                if (int_part == 0) {
                    out(L'0');
                }
                else {
                    wchar_t num_buf[32];
                    int i = 0;
                    unsigned long long temp = int_part;
                    while (temp > 0) {
                        num_buf[i++] = (temp % 10) + L'0';
                        temp /= 10;
                    }
                    while (i > 0) out(num_buf[--i]);
                }

                if (precision > 0) {
                    out(L'.');
                    for (int i = 0; i < precision; i++) {
                        frac_part *= 10;
                        int digit = (int)frac_part;
                        out(digit + L'0');
                        frac_part -= digit;
                    }
                }
                break;
            }
            case L'%': {
                out(L'%');
                break;
            }
            default: {
                out(L'%');
                if (precision_specified) {
                    out(L'.');
                }
                out(*ptr);
                break;
            }
            }
        }
        else {
            out(*ptr);
        }
        ptr++;
    }

    if (written < count) {
        buffer[written] = L'\0';
    }
    else {
        buffer[count - 1] = L'\0';
    }

    return (int)written;
}

int cnprint(char* buffer, size_t count, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int ret = cvnprint(buffer, count, format, args);
    va_end(args);
    return ret;
}

int cprint(const char* format, ...) {
    char buffer[1024];

    va_list args;
    va_start(args, format);
    int ret = cvnprint(buffer, sizeof(buffer), format, args);
    va_end(args);

    WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE), buffer, ccmin(ret, sizeof(buffer)), 0, 0);
    //for (int i = 0; i < ret && i < sizeof(buffer); i++) {
    //    ccputchar(buffer[i]);
    //}
    return ret;
}

int wnprint(wchar_t* buffer, size_t count, const wchar_t* format, ...) {
    va_list args;
    va_start(args, format);
    int ret = cwvnprint(buffer, count, format, args);
    va_end(args);
    return ret;
}

int wprint(const wchar_t* format, ...) {
    wchar_t buffer[1024];

    va_list args;
    va_start(args, format);
    int ret = cwvnprint(buffer, sizeof(buffer) / sizeof(wchar_t), format, args);
    va_end(args);

    //for (int i = 0; i < ret && i < (sizeof(buffer) / sizeof(wchar_t)); i++) {
    //    ccputwchar(buffer[i]);
    //}
    return ret;
}

static int mis_digit(char c) {
    return c >= '0' && c <= '9';
}
static int mis_hexdigit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static int mis_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

int ccscan(const char* str, const char* format, ...) {
    va_list args;
    va_start(args, format);

    int assigned_count = 0;

    while (*format != '\0') {
        if (mis_space(*format)) {
            while (mis_space(*format)) format++;
            while (mis_space(*str)) str++;
            continue;
        }

        if (*format == '%') {
            format++;

            if (*format == 'd' || *format == 'i') {
                while (mis_space(*str)) str++;

                if (*str == '\0') break;

                int sign = 1;
                if (*str == '-') {
                    sign = -1;
                    str++;
                }
                else if (*str == '+') {
                    str++;
                }

                int base = 10;
                if (*str == '0' && (str[1] == 'x' || str[1] == 'X')) {
                    base = 16;
                    str += 2;
                }

                if ((base == 10 && !mis_digit(*str)) || (base == 16 && !mis_hexdigit(*str))) break;

                int value = 0;
                while (true) {
                    int c = *str;
                    int digit = -1;
                    if (c >= '0' && c <= '9') digit = c - '0';
                    else if (base == 16 && c >= 'a' && c <= 'f') digit = c - 'a' + 10;
                    else if (base == 16 && c >= 'A' && c <= 'F') digit = c - 'A' + 10;
                    if (digit < 0 || digit >= base) break;
                    value = value * base + digit;
                    str++;
                }

                int* ptr = va_arg(args, int*);
                if (ptr) {
                    *ptr = value * sign;
                }

                assigned_count++;
                format++;
            }
            else if (*format == 'X' || *format == 'x') {
                while (mis_space(*str)) str++;

                if (*str == '\0') break;

                unsigned int value = 0;
                while (true) {
                    int c = *str;
                    int digit = -1;
                    if (c >= '0' && c <= '9') digit = c - '0';
                    else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
                    else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
                    if (digit < 0) break;
                    value = (value << 4) | (unsigned int)digit;
                    str++;
                }

                unsigned int* ptr = va_arg(args, unsigned int*);
                if (ptr) {
                    *ptr = value;
                }

                assigned_count++;
                format++;
            }
            else if (*format == '%') {
                if (*str == '%') {
                    str++;
                    format++;
                }
                else {
                    break;
                }
            }
            else {
                break;
            }
        }
        else {
            if (*str == *format) {
                str++;
                format++;
            }
            else {
                break;
            }
        }
    }

    va_end(args);
    return assigned_count;
}