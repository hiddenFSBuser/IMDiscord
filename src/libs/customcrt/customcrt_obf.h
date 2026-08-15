#pragma once
// String-obfuscation passthrough. In release builds these normally expand to a
// compile-time XOR that hides literals from the binary; here they are identity
// so the tree builds cleanly while keeping the call sites intact.

#define xor_a(str) ((const char*)(str))
#define xor_w(str) ((const wchar_t*)(str))

// temp_strc::dup - transient duplicate of a C string into a rotating scratch
// buffer. Only used on cold error paths (stb_image), so a small ring is enough.
struct temp_strc {
    static char* dup(const char* s) {
        static char rings[8][256];
        static unsigned int idx = 0;
        char* out = rings[idx & 7];
        idx++;
        int i = 0;
        while (s[i] && i < 255) { out[i] = s[i]; i++; }
        out[i] = 0;
        return out;
    }
};
