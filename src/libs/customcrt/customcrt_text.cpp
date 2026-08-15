#include "pch.h"
#include "customcrt_text.h"

__forceinline unsigned char to_upper_a(unsigned char c) {
    return (c >= 'a' && c <= 'z') ? c - 32 : c;
}

__forceinline wchar_t to_upper_w(wchar_t c) {
    return (c >= L'a' && c <= L'z') ? c - 32 : c;
}


size_t ccslenf(const char* str) {
    const char* start = str;
    constexpr size_t word_size = sizeof(size_t);

    while ((reinterpret_cast<uintptr_t>(str) % word_size) != 0)
    {
        if (*str == '\0') {
            return str - start;
        }
        str++;
    }

    const size_t* word_ptr = reinterpret_cast<const size_t*>(str);

    const size_t HIMAGIC = 0x8080808080808080ULL;
    const size_t LOMAGIC = 0x0101010101010101ULL;

    while (true)
    {
        size_t word = *word_ptr++;
        if (((word - LOMAGIC) & ~word & HIMAGIC) != 0)
        {
            const char* char_ptr = reinterpret_cast<const char*>(word_ptr - 1);
            if (char_ptr[0] == '\0') return char_ptr - start;
            if (char_ptr[1] == '\0') return char_ptr + 1 - start;
            if (char_ptr[2] == '\0') return char_ptr + 2 - start;
            if (char_ptr[3] == '\0') return char_ptr + 3 - start;
            if (word_size > 4) {
                if (char_ptr[4] == '\0') return char_ptr + 4 - start;
                if (char_ptr[5] == '\0') return char_ptr + 5 - start;
                if (char_ptr[6] == '\0') return char_ptr + 6 - start;
                if (char_ptr[7] == '\0') return char_ptr + 7 - start;
            }
        }
    }
    return 0;
}
size_t ccslen(const char* str) {
    int len = 0;
    while (*str != 0) {
        str++;
        len++;
    }
    return len;
}
size_t ccwlen(const void* a1) {
    const WCHAR* str = (const WCHAR*)a1;
    if (str == NULL) {
        return 0;
    }

    const WCHAR* end = str;

    while (*end) {
        end++;
    }

    return (SIZE_T)(end - str);
}
int ccscmpf(const char* s1, const char* s2) {
    constexpr size_t word_size = sizeof(size_t);

    while (reinterpret_cast<uintptr_t>(s1) % word_size != 0 && *s1 && *s1 == *s2) {
        s1++;
        s2++;
    }

    if (*s1 != *s2) {
        return static_cast<unsigned char>(*s1) - static_cast<unsigned char>(*s2);
    }
    if (*s1 == '\0') {
        return 0;
    }

    const size_t* w1 = reinterpret_cast<const size_t*>(s1);
    const size_t* w2 = reinterpret_cast<const size_t*>(s2);

    const size_t HIMAGIC = 0x8080808080808080ULL;
    const size_t LOMAGIC = 0x0101010101010101ULL;

    while (*w1 == *w2)
    {
        if (((*w1 - LOMAGIC) & ~*w1 & HIMAGIC) != 0) {
            return 0;
        }
        w1++;
        w2++;
    }

    s1 = reinterpret_cast<const char*>(w1);
    s2 = reinterpret_cast<const char*>(w2);

    while (*s1 && *s1 == *s2) {
        s1++;
        s2++;
    }

    return static_cast<unsigned char>(*s1) - static_cast<unsigned char>(*s2);
}
int ccscmp(const char* a, const char* b) {
    while (*a && *a == *b) { ++a; ++b; }
    return (int)(unsigned char)(*a) - (int)(unsigned char)(*b);
}
int ccsncmp(const char* s1, const char* s2, size_t n) {
    if (n == 0) return 0;

    while (n > 0 && *s1 && *s1 == *s2) {
        s1++;
        s2++;
        n--;
    }

    if (n == 0) return 0;

    return (int)(unsigned char)(*s1) - (int)(unsigned char)(*s2);
}
int ccsncmpf(const char* s1, const char* s2, size_t n) {
    if (n == 0) return 0;

    constexpr size_t word_size = sizeof(size_t);

    while (reinterpret_cast<uintptr_t>(s1) % word_size != 0 && n > 0) {
        if (*s1 != *s2) {
            return static_cast<unsigned char>(*s1) - static_cast<unsigned char>(*s2);
        }
        if (*s1 == '\0') {
            return 0;
        }
        s1++;
        s2++;
        n--;
    }

    if (n == 0) return 0;

    const size_t* w1 = reinterpret_cast<const size_t*>(s1);
    const size_t* w2 = reinterpret_cast<const size_t*>(s2);

    const size_t HIMAGIC = 0x8080808080808080ULL;
    const size_t LOMAGIC = 0x0101010101010101ULL;

    while (n >= word_size && *w1 == *w2)
    {
        if (((*w1 - LOMAGIC) & ~*w1 & HIMAGIC) != 0) {
            return 0;
        }
        w1++;
        w2++;
        n -= word_size;
    }

    s1 = reinterpret_cast<const char*>(w1);
    s2 = reinterpret_cast<const char*>(w2);

    while (n > 0 && *s1 && *s1 == *s2) {
        s1++;
        s2++;
        n--;
    }

    if (n == 0) return 0;

    return static_cast<unsigned char>(*s1) - static_cast<unsigned char>(*s2);
}
int ccwcmp(const wchar_t* s1, const wchar_t* s2) {
    wchar_t c1, c2;
    do {
        c1 = *s1++;
        c2 = *s2++;
        if (c1 >= L'a' && c1 <= L'z') c1 -= 32; // to upper
        if (c2 >= L'a' && c2 <= L'z') c2 -= 32; // to upper
    } while (c1 && (c1 == c2));
    return c1 - c2;
}

int ccwcmpf(const wchar_t* s1, const wchar_t* s2) {
    constexpr size_t word_size = sizeof(size_t);

    while ((reinterpret_cast<uintptr_t>(s1) % word_size) != 0 && *s1 && *s1 == *s2) {
        s1++;
        s2++;
    }

    if (*s1 != *s2) {
        return static_cast<int>(*s1) - static_cast<int>(*s2);
    }
    if (*s1 == L'\0') {
        return 0;
    }

    const size_t* w1 = reinterpret_cast<const size_t*>(s1);
    const size_t* w2 = reinterpret_cast<const size_t*>(s2);

    const size_t HIMAGIC = 0x8000800080008000ULL;
    const size_t LOMAGIC = 0x0001000100010001ULL;

    while (*w1 == *w2) {
        size_t v = *w1;
        if (((v - LOMAGIC) & ~v & HIMAGIC) != 0) {
            return 0;
        }
        w1++;
        w2++;
    }

    s1 = reinterpret_cast<const wchar_t*>(w1);
    s2 = reinterpret_cast<const wchar_t*>(w2);

    while (*s1 && *s1 == *s2) {
        s1++;
        s2++;
    }

    return static_cast<int>(*s1) - static_cast<int>(*s2);
}

int ccscmpi(const char* s1, const char* s2) {
    int result;
    unsigned char c1, c2;
    do {
        c1 = to_upper_a((unsigned char)*s1++);
        c2 = to_upper_a((unsigned char)*s2++);
        result = (int)c1 - (int)c2;
    } while (result == 0 && c1 != 0);
    return result;
}

int ccwcmpi(const wchar_t* s1, const wchar_t* s2) {
    int result;
    wchar_t c1, c2;
    do {
        c1 = to_upper_w(*s1++);
        c2 = to_upper_w(*s2++);
        result = (int)c1 - (int)c2;
    } while (result == 0 && c1 != 0);
    return result;
}

int ccscmpif(const char* s1, const char* s2) {
    constexpr size_t word_size = sizeof(size_t);

    while ((reinterpret_cast<uintptr_t>(s1) % word_size) != 0) {
        unsigned char c1 = to_upper_a(*s1);
        unsigned char c2 = to_upper_a(*s2);
        if (c1 != c2) return c1 - c2;
        if (c1 == 0) return 0;
        s1++; s2++;
    }

    const size_t* w1 = reinterpret_cast<const size_t*>(s1);
    const size_t* w2 = reinterpret_cast<const size_t*>(s2);

    const size_t HIMAGIC = 0x8080808080808080ULL;
    const size_t LOMAGIC = 0x0101010101010101ULL;

    while (*w1 == *w2) {
        size_t v = *w1;
        if (((v - LOMAGIC) & ~v & HIMAGIC) != 0) {
            return 0;
        }
        w1++;
        w2++;
    }

    s1 = reinterpret_cast<const char*>(w1);
    s2 = reinterpret_cast<const char*>(w2);

    return ccscmpi(s1, s2);
}

int ccwcmpif(const wchar_t* s1, const wchar_t* s2) {
    constexpr size_t word_size = sizeof(size_t);

    while ((reinterpret_cast<uintptr_t>(s1) % word_size) != 0) {
        wchar_t c1 = to_upper_w(*s1);
        wchar_t c2 = to_upper_w(*s2);
        if (c1 != c2) return c1 - c2;
        if (c1 == 0) return 0;
        s1++; s2++;
    }

    const size_t* w1 = reinterpret_cast<const size_t*>(s1);
    const size_t* w2 = reinterpret_cast<const size_t*>(s2);

    const size_t HIMAGIC = 0x8000800080008000ULL;
    const size_t LOMAGIC = 0x0001000100010001ULL;

    while (*w1 == *w2) {
        size_t v = *w1;
        if (((v - LOMAGIC) & ~v & HIMAGIC) != 0) {
            return 0;
        }
        w1++;
        w2++;
    }

    s1 = reinterpret_cast<const wchar_t*>(w1);
    s2 = reinterpret_cast<const wchar_t*>(w2);

    return ccwcmpi(s1, s2);
}

unsigned int ccscrc32(const char* s) {
    if (!s) return 0;
    return ccrc32(s, ccslenf(s));
}
unsigned __int64 ccscrc64(const char* s) {
    if (!s) return 0;
    return ccrc64(s, ccslenf(s));
}

char* ccstrptr(unsigned long long value, char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) {
        return nullptr;
    }

    buffer[0] = '\0';

    if (value == 0) {
        if (buffer_size < 4) {
            return nullptr;
        }
        buffer[0] = '0';
        buffer[1] = 'x';
        buffer[2] = '0';
        buffer[3] = '\0';
        return buffer;
    }

    const char* hex_chars = "0123456789abcdef";

    char reversed_digits[16];
    int digit_count = 0;

    uintptr_t temp_value = value;

    while (temp_value > 0) {
        unsigned int remainder = temp_value % 16;

        reversed_digits[digit_count] = hex_chars[remainder];

        digit_count++;

        temp_value /= 16;
    }

    if (buffer_size < (size_t)digit_count + 3) {
        return nullptr;
    }

    buffer[0] = '0';
    buffer[1] = 'x';

    size_t buffer_pos = 2;

    for (int i = digit_count - 1; i >= 0; --i) {
        buffer[buffer_pos] = reversed_digits[i];
        buffer_pos++;
    }

    buffer[buffer_pos] = '\0';

    return buffer;
}
const char* ccstrstr(const char* haystack, const char* needle) {
    if (!*needle) return haystack;

    for (; *haystack; haystack++) {
        if (*haystack == *needle) {
            const char* h = haystack;
            const char* n = needle;

            while (*h && *n && *h == *n) {
                h++;
                n++;
            }
            if (!*n) return haystack;
        }
    }
    return nullptr;
}

char* ccstrtok(char* str, const char* delim) {
    static char* next_token = nullptr;
    if (str) next_token = str;

    if (!next_token || *next_token == '\0') return nullptr;

    char* start = next_token;
    while (*start) {
        bool is_delim = false;
        for (const char* d = delim; *d; d++) {
            if (*start == *d) { is_delim = true; break; }
        }
        if (!is_delim) break;
        start++;
    }

    if (*start == '\0') {
        next_token = nullptr;
        return nullptr;
    }

    char* end = start;
    while (*end) {
        bool is_delim = false;
        for (const char* d = delim; *d; d++) {
            if (*end == *d) { is_delim = true; break; }
        }
        if (is_delim) {
            *end = '\0';
            next_token = end + 1;
            return start;
        }
        end++;
    }

    next_token = nullptr;
    return start;
}

int ccstrti(const char* label) {
    if (!label || *label == '\0') return 0;

    int result = 0;
    bool isNegative = false;
    size_t i = 0;

    if (label[0] == '-') {
        isNegative = true;
        i = 1;
    }
    else if (label[0] == '+') {
        i = 1;
    }

    while (label[i]) {
        char c = label[i];
        if (c < '0' || c > '9') break;

        result = result * 10 + (c - '0');
        ++i;
    }

    return isNegative ? -result : result;
}
int ccstrthi(const char* label) {
    if (!label || *label == '\0') return 0;

    unsigned int result = 0;
    bool isNegative = false;
    size_t i = 0;

    if (label[0] == '-') {
        isNegative = true;
        i = 1;
    }
    else if (label[0] == '+') {
        i = 1;
    }

    if (label[i] == '0' && (label[i + 1] == 'x' || label[i + 1] == 'X')) {
        i += 2;
    }

    while (label[i]) {
        char c = label[i];
        int val = 0;

        if (c >= '0' && c <= '9') {
            val = c - '0';
        }
        else if (c >= 'a' && c <= 'f') {
            val = c - 'a' + 10;
        }
        else if (c >= 'A' && c <= 'F') {
            val = c - 'A' + 10;
        }
        else {
            break;
        }

        result = (result << 4) | (unsigned int)val;
        ++i;
    }

    return isNegative ? -(int)result : (int)result;
}
unsigned __int64 ccstrthill(const char* label) {
    if (!label || *label == '\0') return 0;

    unsigned __int64 result = 0;
    size_t i = 0;

    while (label[i] == ' ' || label[i] == '\t') i++;

    if (label[i] == '0' && (label[i + 1] == 'x' || label[i + 1] == 'X')) {
        i += 2;
    }

    while (label[i]) {
        char c = label[i];
        int val = 0;

        if (c >= '0' && c <= '9')      val = c - '0';
        else if (c >= 'a' && c <= 'f') val = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') val = c - 'A' + 10;
        else break;

        result = (result << 4) | (unsigned __int64)val;
        i++;
    }

    return result;
}

static inline int mis_space_char(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

long ccstrtol(const char* nptr, char** endptr, int base) {
    const char* s = nptr;
    unsigned long acc;
    int c;
    unsigned long cutoff;
    int neg = 0, any, cutlim;

    while (mis_space_char(*s)) {
        s++;
    }

    if (*s == '-') {
        neg = 1;
        s++;
    }
    else if (*s == '+') {
        s++;
    }

    if ((base == 0 || base == 16) && *s == '0' && (*(s + 1) == 'x' || *(s + 1) == 'X')) {
        int next = *(s + 2);
        if ((next >= '0' && next <= '9') || (next >= 'a' && next <= 'f') || (next >= 'A' && next <= 'F')) {
            base = 16;
            s += 2;
        }
        else if (base == 0) {
            base = 10;
        }
    }
    else if (base == 0) {
        base = (*s == '0') ? 8 : 10;
    }

    if (base < 2 || base > 36) {
        if (endptr) *endptr = (char*)nptr;
        return 0;
    }

    // LONG_MAX: 2147483647, LONG_MIN: -2147483648
    cutoff = neg ? 2147483648UL : 2147483647UL;
    cutlim = cutoff % (unsigned long)base;
    cutoff /= (unsigned long)base;

    for (acc = 0, any = 0;; s++) {
        c = *s;
        if (c >= '0' && c <= '9')
            c -= '0';
        else if (c >= 'A' && c <= 'Z')
            c -= 'A' - 10;
        else if (c >= 'a' && c <= 'z')
            c -= 'a' - 10;
        else
            break;

        if (c >= base)
            break;

        if (any < 0 || acc > cutoff || (acc == cutoff && c > cutlim)) {
            any = -1;
        }
        else {
            any = 1;
            acc *= base;
            acc += c;
        }
    }

    if (any < 0) {
        acc = neg ? 2147483648UL : 2147483647UL;
    }
    else if (neg) {
        acc = acc + 0x7FFFFFFF;
        //acc = -acc;
    }

    if (endptr != 0) {
        *endptr = (char*)(any ? s : nptr);
    }

    return (long)acc;
}

long long ccstrtoll(const char* nptr, char** endptr, int base) {
    const char* s = nptr;
    unsigned long long acc;
    int c;
    unsigned long long cutoff;
    int neg = 0, any, cutlim;

    while (mis_space_char(*s)) s++;

    if (*s == '-') {
        neg = 1;
        s++;
    }
    else if (*s == '+') {
        s++;
    }

    if ((base == 0 || base == 16) && *s == '0' && (*(s + 1) == 'x' || *(s + 1) == 'X')) {
        int next = *(s + 2);
        if ((next >= '0' && next <= '9') || (next >= 'a' && next <= 'f') || (next >= 'A' && next <= 'F')) {
            base = 16;
            s += 2;
        }
        else if (base == 0) {
            base = 10;
        }
    }
    else if (base == 0) {
        base = (*s == '0') ? 8 : 10;
    }

    if (base < 2 || base > 36) {
        if (endptr) *endptr = (char*)nptr;
        return 0;
    }

    // LLONG_MAX: 9223372036854775807, LLONG_MIN: -9223372036854775808
    cutoff = neg ? 9223372036854775808ULL : 9223372036854775807ULL;
    cutlim = cutoff % (unsigned long long)base;
    cutoff /= (unsigned long long)base;

    for (acc = 0, any = 0;; s++) {
        c = *s;
        if (c >= '0' && c <= '9') c -= '0';
        else if (c >= 'A' && c <= 'Z') c -= 'A' - 10;
        else if (c >= 'a' && c <= 'z') c -= 'a' - 10;
        else break;

        if (c >= base) break;

        if (any < 0 || acc > cutoff || (acc == cutoff && c > cutlim)) {
            any = -1;
        }
        else {
            any = 1;
            acc *= base;
            acc += c;
        }
    }

    if (any < 0) {
        acc = neg ? 9223372036854775808ULL : 9223372036854775807ULL;
    }
    else if (neg) {
        acc = acc + 0x7FFFFFFFFFFFFFFF;
        //acc = -acc;
    }

    if (endptr != 0) *endptr = (char*)(any ? s : nptr);

    return (long long)acc;
}

unsigned long long ccstrtoull(const char* nptr, char** endptr, int base) {
    const char* s = nptr;
    unsigned long long acc;
    int c;
    unsigned long long cutoff;
    int any, cutlim;

    while (mis_space_char(*s)) s++;
    if (*s == '+') s++;

    if ((base == 0 || base == 16) && *s == '0' && (*(s + 1) == 'x' || *(s + 1) == 'X')) {
        int next = *(s + 2);
        if ((next >= '0' && next <= '9') || (next >= 'a' && next <= 'f') || (next >= 'A' && next <= 'F')) {
            base = 16;
            s += 2;
        }
        else if (base == 0) {
            base = 10;
        }
    }
    else if (base == 0) {
        base = (*s == '0') ? 8 : 10;
    }

    if (base < 2 || base > 36) {
        if (endptr) *endptr = (char*)nptr;
        return 0;
    }

    // ULLONG_MAX: 18446744073709551615
    cutoff = 18446744073709551615ULL;
    cutlim = cutoff % (unsigned long long)base;
    cutoff /= (unsigned long long)base;

    for (acc = 0, any = 0;; s++) {
        c = *s;
        if (c >= '0' && c <= '9') c -= '0';
        else if (c >= 'A' && c <= 'Z') c -= 'A' - 10;
        else if (c >= 'a' && c <= 'z') c -= 'a' - 10;
        else break;

        if (c >= base) break;

        if (any < 0 || acc > cutoff || (acc == cutoff && c > cutlim)) {
            any = -1;
        }
        else {
            any = 1;
            acc *= base;
            acc += c;
        }
    }

    if (any < 0) {
        acc = 18446744073709551615ULL; // Max
    }

    if (endptr != 0) *endptr = (char*)(any ? s : nptr);

    return acc;
}
char* ccstrncpy(char* dst, const char* src, size_t count) {
    if (count == 0) return dst;
    char* d = dst;
    while (count > 1 && *src) {
        *d++ = *src++;
        count--;
    }
    *d = 0;
    return dst;
}

void custom_qsort(void* base, int count, int size, int(__cdecl* compare)(const void*, const void*)) {
    if (count <= 1) return;
    char* arr = (char*)base;
    char* temp = (char*)memalloc(size);
    for (int gap = count / 2; gap > 0; gap /= 2) {
        for (int i = gap; i < count; i++) {
            ccpy(temp, arr + i * size, size);
            int j;
            for (j = i; j >= gap && compare(arr + (j - gap) * size, temp) > 0; j -= gap)
                ccpy(arr + j * size, arr + (j - gap) * size, size);
            ccpy(arr + j * size, temp, size);
        }
    }
    memfree(temp);
}

float ccstrtf(const char* s) {
    if (!s || *s == '\0') return 0.0f;

    float res = 0.0f;
    float sign = 1.0f;

    if (*s == '-') {
        sign = -1.0f;
        s++;
    }
    else if (*s == '+') {
        s++;
    }

    while (*s >= '0' && *s <= '9') {
        res = res * 10.0f + (*s - '0');
        s++;
    }

    if (*s == '.' || *s == ',') {
        s++;
        float divisor = 10.0f;
        while (*s >= '0' && *s <= '9') {
            res += (*s - '0') / divisor;
            divisor *= 10.0f;
            s++;
        }
    }

    return res * sign;
}

wchar_t cctowlower(wchar_t wc) {
    if (wc >= L'A' && wc <= L'Z') {
        return wc + (L'a' - L'A');
    }

    if (wc >= 0x0410 && wc <= 0x042F) {
        return wc + 0x0020;
    }

    if (wc == 0x0401) {
        return 0x0451;
    }

    return wc;
}

char cctolower(char c) {
    if (c >= 'A' && c <= 'Z') {
        return c + ('a' - 'A');
    }
    return c;
}