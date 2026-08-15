#pragma once

struct ccdur {
    long long ns;

    long long g_seconds() const { return ns / 1000000000LL; }
    long long g_milliseconds() const { return ns / 1000000LL; }
    long long g_microseconds() const { return ns / 1000LL; }
    long long g_nanoseconds() const { return ns; }

    static ccdur milliseconds(long long ms) { return { ms * 1000000LL }; }
    static ccdur seconds(long long s) { return { s * 1000000000LL }; }
    static ccdur nanoseconds(long long ns) { return { ns }; }

    bool operator>(const ccdur& other) const { return ns > other.ns; }
    bool operator<(const ccdur& other) const { return ns < other.ns; }
    ccdur operator+(const ccdur& other) const { return { ns + other.ns }; }
    ccdur operator-(const ccdur& other) const { return { ns - other.ns }; }
};

struct cctp {
    long long ticks;

    bool operator>(const cctp& other) const { return ticks > other.ticks; }
    bool operator<(const cctp& other) const { return ticks < other.ticks; }
    bool operator>=(const cctp& other) const { return ticks >= other.ticks; }
    bool operator<=(const cctp& other) const { return ticks <= other.ticks; }

    cctp operator+(const ccdur& d) const { return { ticks + d.ns }; }
    cctp& operator+=(const ccdur& d) { ticks += d.ns; return *this; }

    ccdur operator-(const cctp& other) const { return { ticks - other.ticks }; }
};

struct cclock {
    static cctp now();
};

inline ccdur cc_ms(long long ms) { return ccdur::milliseconds(ms); }