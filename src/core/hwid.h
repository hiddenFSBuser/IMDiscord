#pragma once

// Machine fingerprint. Only stable, machine-scoped sources are mixed in, so the
// value survives reboots and user changes but not a move to another PC.
namespace hwid
{
    // 32 raw bytes.
    void get(unsigned char out[32]);
    // Lowercase hex of the first 16 bytes, NUL terminated (needs 33 chars).
    void get_hex(char* out, int cap);
}
