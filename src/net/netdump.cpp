#include "pch.h"
#include "netdump.h"
#include "core/log.h"
#include <mstcpip.h>
#include <iphlpapi.h>

#ifndef IF_TYPE_SOFTWARE_LOOPBACK
#define IF_TYPE_SOFTWARE_LOOPBACK 24
#endif

// ---------------------------------------------------------------------------
// Saying things
//
// Two destinations at once. The console is for the person watching the test
// happen; the log file is for afterwards, when the result has to be handed to
// somebody who was not sitting here.
// ---------------------------------------------------------------------------

static HANDLE g_console = 0;
static bool   g_console_owned = false;

static void say(const char* fmt, ...)
{
    char line[1024];

    va_list ap;
    va_start(ap, fmt);
    cvnprint(line, sizeof(line) - 3, fmt, ap);
    va_end(ap);

    log_line("netdump: %s", line);

    if (!g_console) return;

    size_t n = ccslenf(line);
    line[n]     = '\r';
    line[n + 1] = '\n';

    DWORD written = 0;
    WriteFile(g_console, line, (DWORD)(n + 2), &written, 0);
}

// A fixed width field. Column alignment is not worth leaning on printf flags
// for, so the padding happens here where it cannot surprise anybody.
static void pad(char* dst, int cap, const char* src, int width)
{
    int i = 0;
    while (src[i] && i < width && i < cap - 1) { dst[i] = src[i]; i++; }
    while (i < width && i < cap - 1) { dst[i] = ' '; i++; }
    dst[i] = 0;
}

static bool contains_ci(const char* hay, const char* needle)
{
    if (!hay || !needle || !needle[0]) return false;

    for (int i = 0; hay[i]; i++)
    {
        int j = 0;
        while (needle[j] && hay[i + j] &&
               cctolower(hay[i + j]) == cctolower(needle[j])) j++;

        if (!needle[j]) return true;
    }

    return false;
}

// ---------------------------------------------------------------------------
// Addresses
// ---------------------------------------------------------------------------

struct ipaddr
{
    unsigned char b[16];
    unsigned char family;      // 4 or 6

    bool same(const ipaddr& o) const
    {
        return family == o.family && ccmp(b, o.b, family == 4 ? 4 : 16) == 0;
    }

    bool loopback() const
    {
        if (family == 4) return b[0] == 127;

        for (int i = 0; i < 15; i++) if (b[i]) return false;
        return b[15] == 1;
    }
};

static void addr_text(const ipaddr& a, char* out, int cap)
{
    if (a.family == 4)
    {
        cnprint(out, cap, "%u.%u.%u.%u", a.b[0], a.b[1], a.b[2], a.b[3]);
        return;
    }

    // Eight groups, no "::" shortening. This is a diagnostic and the address
    // gets compared against a config file by eye; the literal form is the one
    // that matches.
    cnprint(out, cap, "%x:%x:%x:%x:%x:%x:%x:%x",
            (a.b[0]  << 8) | a.b[1],  (a.b[2]  << 8) | a.b[3],
            (a.b[4]  << 8) | a.b[5],  (a.b[6]  << 8) | a.b[7],
            (a.b[8]  << 8) | a.b[9],  (a.b[10] << 8) | a.b[11],
            (a.b[12] << 8) | a.b[13], (a.b[14] << 8) | a.b[15]);
}

static ipaddr g_local[64];
static int    g_local_count = 0;

static bool is_local(const ipaddr& a)
{
    for (int i = 0; i < g_local_count; i++) if (g_local[i].same(a)) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Which process owns a port
//
// The raw socket gives a port and no process. The tcp and udp tables give a
// port and a process id and no traffic. Together they give the thing actually
// wanted, which is a flow with a name on it.
// ---------------------------------------------------------------------------

struct port_owner
{
    unsigned char  proto;      // 6 or 17
    unsigned short port;
    unsigned int   pid;
};

static port_owner g_owners[4096];
static int        g_owner_count = 0;

struct proc_name
{
    unsigned int pid;
    char name[64];
};

static proc_name g_names[512];
static int       g_name_count = 0;

static const char* name_of(unsigned int pid)
{
    if (!pid) return "(неизвестно)";

    for (int i = 0; i < g_name_count; i++)
        if (g_names[i].pid == pid) return g_names[i].name;

    if (g_name_count >= 512) return "?";

    proc_name& slot = g_names[g_name_count++];
    slot.pid = pid;
    ccstrncpy(slot.name, "?", sizeof(slot.name) - 1);

    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
    if (!h) return slot.name;

    wchar_t wide[MAX_PATH];
    DWORD len = MAX_PATH;

    if (QueryFullProcessImageNameW(h, 0, wide, &len) && len)
    {
        // Only the file name. A full path is noise in a table that has to fit
        // on one line.
        int start = 0;
        for (int i = 0; i < (int)len; i++) if (wide[i] == L'\\') start = i + 1;

        char utf8[128];
        int n = utf16to8(wide + start, (int)len - start, utf8, sizeof(utf8) - 1);
        if (n < 0) n = 0;
        utf8[n] = 0;

        if (utf8[0]) ccstrncpy(slot.name, utf8, sizeof(slot.name) - 1);
    }

    CloseHandle(h);
    return slot.name;
}

// A connection an application opened to a proxy listening on this machine.
// Raw sockets do not see loopback traffic on windows, so this comes out of the
// table instead - and it is exactly the half of the picture that shows an
// application talking to a local proxy at all.
struct local_link
{
    unsigned int   pid;
    unsigned short port;
};

static local_link g_links[256];
static int        g_link_count = 0;

static void note_link(unsigned int pid, unsigned short port)
{
    for (int i = 0; i < g_link_count; i++)
        if (g_links[i].pid == pid && g_links[i].port == port) return;

    if (g_link_count >= 256) return;

    g_links[g_link_count].pid  = pid;
    g_links[g_link_count].port = port;
    g_link_count++;
}

static void add_owner(unsigned char proto, unsigned short port, unsigned int pid)
{
    for (int i = 0; i < g_owner_count; i++)
        if (g_owners[i].proto == proto && g_owners[i].port == port)
        {
            g_owners[i].pid = pid;
            return;
        }

    if (g_owner_count >= 4096) return;

    g_owners[g_owner_count].proto = proto;
    g_owners[g_owner_count].port  = port;
    g_owners[g_owner_count].pid   = pid;
    g_owner_count++;
}

static unsigned int owner_of(unsigned char proto, unsigned short port)
{
    for (int i = 0; i < g_owner_count; i++)
        if (g_owners[i].proto == proto && g_owners[i].port == port)
            return g_owners[i].pid;

    return 0;
}

static void refresh_tables()
{
    // ---- tcp, both families -----------------------------------------------
    for (int pass = 0; pass < 2; pass++)
    {
        ULONG family = pass == 0 ? AF_INET : AF_INET6;

        DWORD size = 0;
        GetExtendedTcpTable(0, &size, FALSE, family, TCP_TABLE_OWNER_PID_ALL, 0);
        if (!size) continue;

        void* buf = memalloc((int)size);
        if (!buf) continue;

        if (GetExtendedTcpTable(buf, &size, FALSE, family, TCP_TABLE_OWNER_PID_ALL, 0) == NO_ERROR)
        {
            if (family == AF_INET)
            {
                MIB_TCPTABLE_OWNER_PID* t = (MIB_TCPTABLE_OWNER_PID*)buf;

                for (DWORD i = 0; i < t->dwNumEntries; i++)
                {
                    MIB_TCPROW_OWNER_PID& r = t->table[i];

                    add_owner(6, ntohs((unsigned short)(r.dwLocalPort & 0xffff)), r.dwOwningPid);

                    const unsigned char* ra = (const unsigned char*)&r.dwRemoteAddr;
                    if (r.dwState == MIB_TCP_STATE_ESTAB && ra[0] == 127)
                        note_link(r.dwOwningPid, ntohs((unsigned short)(r.dwRemotePort & 0xffff)));
                }
            }
            else
            {
                MIB_TCP6TABLE_OWNER_PID* t = (MIB_TCP6TABLE_OWNER_PID*)buf;

                for (DWORD i = 0; i < t->dwNumEntries; i++)
                    add_owner(6, ntohs((unsigned short)(t->table[i].dwLocalPort & 0xffff)),
                              t->table[i].dwOwningPid);
            }
        }

        memfree(buf);
    }

    // ---- udp, both families -----------------------------------------------
    for (int pass = 0; pass < 2; pass++)
    {
        ULONG family = pass == 0 ? AF_INET : AF_INET6;

        DWORD size = 0;
        GetExtendedUdpTable(0, &size, FALSE, family, UDP_TABLE_OWNER_PID, 0);
        if (!size) continue;

        void* buf = memalloc((int)size);
        if (!buf) continue;

        if (GetExtendedUdpTable(buf, &size, FALSE, family, UDP_TABLE_OWNER_PID, 0) == NO_ERROR)
        {
            if (family == AF_INET)
            {
                MIB_UDPTABLE_OWNER_PID* t = (MIB_UDPTABLE_OWNER_PID*)buf;

                for (DWORD i = 0; i < t->dwNumEntries; i++)
                    add_owner(17, ntohs((unsigned short)(t->table[i].dwLocalPort & 0xffff)),
                              t->table[i].dwOwningPid);
            }
            else
            {
                MIB_UDP6TABLE_OWNER_PID* t = (MIB_UDP6TABLE_OWNER_PID*)buf;

                for (DWORD i = 0; i < t->dwNumEntries; i++)
                    add_owner(17, ntohs((unsigned short)(t->table[i].dwLocalPort & 0xffff)),
                              t->table[i].dwOwningPid);
            }
        }

        memfree(buf);
    }
}

// ---------------------------------------------------------------------------
// Flows
// ---------------------------------------------------------------------------

struct flow
{
    unsigned char  proto;
    ipaddr         remote;
    unsigned short local_port;
    unsigned short remote_port;

    unsigned long long out_pkts, out_bytes;
    unsigned long long in_pkts,  in_bytes;

    unsigned long long first_ms, last_ms;

    // Rate measured over the reporting window rather than over the whole run:
    // a call that starts thirty seconds in would otherwise average away to
    // nothing.
    unsigned long long window_pkts;
    unsigned int       peak_pps;

    unsigned int pid;
    bool         announced;
};

static flow g_flows[1024];
static int  g_flow_count = 0;

static flow* find_flow(unsigned char proto, const ipaddr& remote,
                       unsigned short lport, unsigned short rport)
{
    for (int i = 0; i < g_flow_count; i++)
    {
        flow& f = g_flows[i];
        if (f.proto == proto && f.local_port == lport && f.remote_port == rport &&
            f.remote.same(remote))
            return &f;
    }

    if (g_flow_count >= 1024) return 0;

    flow& f = g_flows[g_flow_count++];
    ccfset(&f, 0, sizeof(f));

    f.proto       = proto;
    f.remote      = remote;
    f.local_port  = lport;
    f.remote_port = rport;
    f.first_ms    = GetTickCount64();

    return &f;
}

static void on_packet(unsigned char family, const unsigned char* p, int n)
{
    ipaddr src, dst;
    unsigned char proto = 0;
    int hdr = 0;

    ccfset(&src, 0, sizeof(src));
    ccfset(&dst, 0, sizeof(dst));

    if (family == 4)
    {
        if (n < 20 || (p[0] >> 4) != 4) return;

        hdr = (p[0] & 0x0f) * 4;
        if (hdr < 20 || n < hdr + 4) return;

        proto = p[9];
        src.family = 4; ccpy(src.b, p + 12, 4);
        dst.family = 4; ccpy(dst.b, p + 16, 4);
    }
    else
    {
        if (n < 40 || (p[0] >> 4) != 6) return;

        hdr = 40;
        if (n < hdr + 4) return;

        // No extension header walking. Media does not use any, and a packet
        // that did would be counted under its extension number - visible as an
        // unknown protocol rather than silently mis-attributed.
        proto = p[6];
        src.family = 6; ccpy(src.b, p + 8,  16);
        dst.family = 6; ccpy(dst.b, p + 24, 16);
    }

    if (proto != 6 && proto != 17) return;

    bool going_out = is_local(src);
    bool coming_in = is_local(dst);

    // Loopback never reaches a raw socket on windows, and a packet that is
    // neither ours going out nor ours coming in belongs to somebody else.
    if (going_out == coming_in) return;

    unsigned short sport = (unsigned short)((p[hdr]     << 8) | p[hdr + 1]);
    unsigned short dport = (unsigned short)((p[hdr + 2] << 8) | p[hdr + 3]);

    flow* f = going_out ? find_flow(proto, dst, sport, dport)
                        : find_flow(proto, src, dport, sport);
    if (!f) return;

    if (going_out) { f->out_pkts++; f->out_bytes += (unsigned)n; }
    else           { f->in_pkts++;  f->in_bytes  += (unsigned)n; }

    f->window_pkts++;
    f->last_ms = GetTickCount64();
}

// ---------------------------------------------------------------------------
// Capture
// ---------------------------------------------------------------------------

struct capture
{
    SOCKET        s;
    unsigned char family;
};

static capture g_caps[32];
static int     g_cap_count = 0;

static volatile long g_stop = 0;

static BOOL WINAPI ctrl_handler(DWORD)
{
    InterlockedExchange(&g_stop, 1);
    return TRUE;
}

// Every address this machine answers on. Needed twice: to open a capture
// socket per interface, and to tell an outgoing packet from an incoming one.
static void collect_addresses(bool* saw_v6)
{
    ULONG size = 32768;
    IP_ADAPTER_ADDRESSES* aa = (IP_ADAPTER_ADDRESSES*)memalloc((int)size);
    if (!aa) return;

    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                  GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_FRIENDLY_NAME;

    ULONG rc = GetAdaptersAddresses(AF_UNSPEC, flags, 0, aa, &size);

    if (rc == ERROR_BUFFER_OVERFLOW)
    {
        memfree(aa);
        aa = (IP_ADAPTER_ADDRESSES*)memalloc((int)size);
        if (!aa) return;

        rc = GetAdaptersAddresses(AF_UNSPEC, flags, 0, aa, &size);
    }

    if (rc != NO_ERROR) { memfree(aa); return; }

    for (IP_ADAPTER_ADDRESSES* a = aa; a; a = a->Next)
    {
        if (a->OperStatus != IfOperStatusUp) continue;
        if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;

        for (IP_ADAPTER_UNICAST_ADDRESS* u = a->FirstUnicastAddress; u; u = u->Next)
        {
            sockaddr* sa = u->Address.lpSockaddr;
            if (!sa || g_local_count >= 64) continue;

            ipaddr got;
            ccfset(&got, 0, sizeof(got));

            if (sa->sa_family == AF_INET)
            {
                got.family = 4;
                ccpy(got.b, &((sockaddr_in*)sa)->sin_addr, 4);
            }
            else if (sa->sa_family == AF_INET6)
            {
                got.family = 6;
                ccpy(got.b, &((sockaddr_in6*)sa)->sin6_addr, 16);

                // A link local address cannot carry a capture socket and never
                // carries media either.
                if (got.b[0] == 0xfe && (got.b[1] & 0xc0) == 0x80) continue;

                *saw_v6 = true;
            }
            else continue;

            if (got.loopback()) continue;

            g_local[g_local_count++] = got;
        }
    }

    memfree(aa);
}

static bool open_capture(const ipaddr& a)
{
    if (g_cap_count >= 32) return false;

    SOCKET s = socket(a.family == 4 ? AF_INET : AF_INET6, SOCK_RAW,
                      a.family == 4 ? IPPROTO_IP : IPPROTO_IPV6);
    if (s == INVALID_SOCKET) return false;

    if (a.family == 4)
    {
        sockaddr_in sa;
        ccfset(&sa, 0, sizeof(sa));
        sa.sin_family = AF_INET;
        ccpy(&sa.sin_addr, a.b, 4);

        if (bind(s, (sockaddr*)&sa, sizeof(sa)) != 0) { closesocket(s); return false; }
    }
    else
    {
        sockaddr_in6 sa;
        ccfset(&sa, 0, sizeof(sa));
        sa.sin6_family = AF_INET6;
        ccpy(&sa.sin6_addr, a.b, 16);

        if (bind(s, (sockaddr*)&sa, sizeof(sa)) != 0) { closesocket(s); return false; }
    }

    int buf = 1 << 21;
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const char*)&buf, sizeof(buf));

    DWORD on = RCVALL_ON, returned = 0;
    if (WSAIoctl(s, SIO_RCVALL, &on, sizeof(on), 0, 0, &returned, 0, 0) != 0)
    {
        closesocket(s);
        return false;
    }

    g_caps[g_cap_count].s      = s;
    g_caps[g_cap_count].family = a.family;
    g_cap_count++;

    return true;
}

// ---------------------------------------------------------------------------
// Reporting
// ---------------------------------------------------------------------------

// What a real time media stream looks like from outside: udp, traffic in both
// directions, and a packet rate no signalling connection produces. Discord
// sends one opus frame every 20 ms, which is fifty packets a second.
static bool is_media(const flow& f)
{
    return f.proto == 17 && f.peak_pps >= 20 && f.out_pkts > 20 && f.in_pkts > 20;
}

static int __cdecl by_bytes(const void* a, const void* b)
{
    const flow* x = (const flow*)a;
    const flow* y = (const flow*)b;

    unsigned long long xt = x->out_bytes + x->in_bytes;
    unsigned long long yt = y->out_bytes + y->in_bytes;

    if (xt < yt) return 1;
    if (xt > yt) return -1;
    return 0;
}

static void flow_line(const flow& f, char* out, int cap)
{
    char remote[64];
    addr_text(f.remote, remote, sizeof(remote));

    char endpoint[96];
    cnprint(endpoint, sizeof(endpoint), "%s:%u", remote, f.remote_port);

    char who[32], where[48];
    pad(who,   sizeof(who),   name_of(f.pid), 24);
    pad(where, sizeof(where), endpoint,       40);

    cnprint(out, cap, "  %s %s %s %llu/%llu пак %llu/%llu КиБ пик %u п/с%s",
            f.proto == 17 ? "udp" : "tcp",
            who, where,
            f.out_pkts, f.in_pkts,
            f.out_bytes / 1024, f.in_bytes / 1024,
            f.peak_pps,
            is_media(f) ? "  <-- МЕДИА" : "");
}

// ---------------------------------------------------------------------------
// Checking the parser without a network
// ---------------------------------------------------------------------------

static void fake_v4(unsigned char* p, unsigned char proto,
                    const unsigned char* src, const unsigned char* dst,
                    unsigned short sport, unsigned short dport)
{
    ccfset(p, 0, 28);

    p[0] = 0x45;                 // version 4, five word header
    p[9] = proto;
    ccpy(p + 12, src, 4);
    ccpy(p + 16, dst, 4);

    p[20] = (unsigned char)(sport >> 8); p[21] = (unsigned char)(sport & 0xff);
    p[22] = (unsigned char)(dport >> 8); p[23] = (unsigned char)(dport & 0xff);
}

bool netdump::self_test()
{
    // A clean slate, in case anything ran before this.
    g_flow_count  = 0;
    g_local_count = 0;

    unsigned char me[4]     = { 192, 168, 1, 5 };
    unsigned char peer[4]   = { 66, 22, 241, 53 };
    unsigned char nobody[4] = { 8, 8, 8, 8 };

    ipaddr mine;
    ccfset(&mine, 0, sizeof(mine));
    mine.family = 4;
    ccpy(mine.b, me, 4);
    g_local[g_local_count++] = mine;

    unsigned char pkt[64];

    // A call: opus one way, opus the other, on one pair of ports. Both
    // directions have to land on a single flow, counted separately.
    for (int i = 0; i < 300; i++)
    {
        fake_v4(pkt, 17, me, peer, 50123, 50001);
        on_packet(4, pkt, 28);

        fake_v4(pkt, 17, peer, me, 50001, 50123);
        on_packet(4, pkt, 28);
    }

    // Something ordinary alongside it, so the media rule has to actually
    // discriminate rather than fire on whatever is busiest.
    for (int i = 0; i < 40; i++)
    {
        fake_v4(pkt, 6, me, nobody, 51000, 443);
        on_packet(4, pkt, 28);
    }

    // Not ours in either direction: somebody else's packet on the segment.
    fake_v4(pkt, 17, nobody, peer, 1000, 2000);
    on_packet(4, pkt, 28);

    if (g_flow_count != 2)
    {
        log_line("netdump: самопроверка: потоков %d, ждали 2", g_flow_count);
        return false;
    }

    flow* call = 0;
    flow* web  = 0;

    for (int i = 0; i < g_flow_count; i++)
        if (g_flows[i].proto == 17) call = &g_flows[i]; else web = &g_flows[i];

    if (!call || !web)
    {
        log_line("netdump: самопроверка: не нашлись оба потока");
        return false;
    }

    if (call->out_pkts != 300 || call->in_pkts != 300)
    {
        log_line("netdump: самопроверка: счёт %llu/%llu, ждали 300/300",
                 call->out_pkts, call->in_pkts);
        return false;
    }

    if (call->local_port != 50123 || call->remote_port != 50001)
    {
        log_line("netdump: самопроверка: порты %u/%u, ждали 50123/50001",
                 call->local_port, call->remote_port);
        return false;
    }

    if (call->remote.b[0] != 66 || call->remote.b[3] != 53)
    {
        log_line("netdump: самопроверка: не тот адрес у медиа-потока");
        return false;
    }

    // The rate the reporting window would have measured.
    call->peak_pps = 50;
    web->peak_pps  = 4;

    if (!is_media(*call))
    {
        log_line("netdump: самопроверка: звонок не признан медиа");
        return false;
    }

    if (is_media(*web))
    {
        log_line("netdump: самопроверка: tcp признан медиа");
        return false;
    }

    // Одностороннее udp - тоже не медиа: так выглядит dns или телеметрия,
    // а не разговор.
    flow lonely = *call;
    lonely.in_pkts = 0;
    if (is_media(lonely))
    {
        log_line("netdump: самопроверка: односторонний поток признан медиа");
        return false;
    }

    // ipv6, where the header is a different shape entirely.
    g_flow_count = 0;

    ipaddr mine6;
    ccfset(&mine6, 0, sizeof(mine6));
    mine6.family = 6;
    mine6.b[0] = 0x20; mine6.b[1] = 0x01; mine6.b[15] = 0x11;
    g_local[g_local_count++] = mine6;

    unsigned char six[64];
    ccfset(six, 0, sizeof(six));
    six[0] = 0x60;
    six[6] = 17;                       // next header: udp
    ccpy(six + 8, mine6.b, 16);
    six[24] = 0x20; six[25] = 0x01; six[39] = 0x99;
    six[40] = 0xc3; six[41] = 0x50;    // 50000
    six[42] = 0x1f; six[43] = 0x90;    // 8080

    on_packet(6, six, 48);

    if (g_flow_count != 1 || g_flows[0].out_pkts != 1 ||
        g_flows[0].local_port != 50000 || g_flows[0].remote_port != 8080)
    {
        log_line("netdump: самопроверка: ipv6 разобран неверно");
        return false;
    }

    g_flow_count  = 0;
    g_local_count = 0;

    log_line("netdump: самопроверка пройдена");
    return true;
}

int netdump::run(int seconds, const char* app_name)
{
    // A console of our own when there is none: the release build is a windows
    // subsystem image, so without this the output would reach only the log.
    if (AttachConsole(ATTACH_PARENT_PROCESS))
    {
        g_console = GetStdHandle(STD_OUTPUT_HANDLE);
    }
    else if (AllocConsole())
    {
        g_console = GetStdHandle(STD_OUTPUT_HANDLE);
        g_console_owned = true;
    }

    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCtrlHandler(ctrl_handler, TRUE);

    say("");
    say("---- netdump ----------------------------------------------------");
    say("Смотрит, какой процесс какие пакеты шлёт наружу. Ctrl+C - закончить раньше.");

    bool saw_v6 = false;
    collect_addresses(&saw_v6);

    if (!g_local_count)
    {
        say("Не нашлось ни одного сетевого адреса. Сеть выключена?");
        return 1;
    }

    for (int i = 0; i < g_local_count; i++)
    {
        char text[64];
        addr_text(g_local[i], text, sizeof(text));

        bool ok = open_capture(g_local[i]);
        say("  %s %s", text, ok ? "- слушаю" : "- не открылось");
    }

    if (!g_cap_count)
    {
        say("");
        say("Ни один захват не открылся. Почти всегда это значит одно: нет прав.");
        say("Запусти этот же файл от администратора.");

        if (g_console_owned)
        {
            say("");
            say("Enter - закрыть.");

            char c; DWORD got = 0;
            ReadFile(GetStdHandle(STD_INPUT_HANDLE), &c, 1, &got, 0);
        }

        return 1;
    }

    say("");
    say("Записываю %d секунд. Заходи в войс.", seconds);
    say("");

    unsigned long long started  = GetTickCount64();
    unsigned long long deadline = started + (unsigned long long)seconds * 1000;
    unsigned long long window   = started;
    unsigned long long last_tables = started;

    refresh_tables();

    unsigned char* packet = (unsigned char*)memalloc(65536);
    if (!packet) return 1;

    while (!g_stop && GetTickCount64() < deadline)
    {
        fd_set rd;
        FD_ZERO(&rd);
        for (int i = 0; i < g_cap_count; i++) FD_SET(g_caps[i].s, &rd);

        timeval tv;
        tv.tv_sec  = 0;
        tv.tv_usec = 200000;

        int ready = select(0, &rd, 0, 0, &tv);

        if (ready > 0)
        {
            for (int i = 0; i < g_cap_count; i++)
            {
                if (!FD_ISSET(g_caps[i].s, &rd)) continue;

                int n = recv(g_caps[i].s, (char*)packet, 65536, 0);
                if (n > 0) on_packet(g_caps[i].family, packet, n);
            }
        }

        unsigned long long now = GetTickCount64();

        // The tables move: a call opens a new udp socket, and a process that
        // exits takes its ports with it. Two seconds is often enough to catch
        // a port before its flow is reported, and slow enough to cost nothing.
        if (now - last_tables >= 2000)
        {
            refresh_tables();
            last_tables = now;
        }

        if (now - window >= 5000)
        {
            unsigned long long span = now - window;
            window = now;

            for (int i = 0; i < g_flow_count; i++)
            {
                flow& f = g_flows[i];

                unsigned int pps = (unsigned int)((f.window_pkts * 1000) / (span ? span : 1));
                f.window_pkts = 0;

                if (pps > f.peak_pps) f.peak_pps = pps;
                if (!f.pid) f.pid = owner_of(f.proto, f.local_port);

                // Announced once, when it first carries something worth
                // looking at. A whole table redrawn every five seconds would
                // bury the one line that matters.
                if (!f.announced && (f.out_pkts + f.in_pkts) > 20)
                {
                    f.announced = true;

                    char line[256];
                    flow_line(f, line, sizeof(line));
                    say("%s", line);
                }
            }
        }
    }

    memfree(packet);

    for (int i = 0; i < g_cap_count; i++)
    {
        DWORD off = RCVALL_OFF, returned = 0;
        WSAIoctl(g_caps[i].s, SIO_RCVALL, &off, sizeof(off), 0, 0, &returned, 0, 0);
        closesocket(g_caps[i].s);
    }

    refresh_tables();
    for (int i = 0; i < g_flow_count; i++)
        if (!g_flows[i].pid)
            g_flows[i].pid = owner_of(g_flows[i].proto, g_flows[i].local_port);

    custom_qsort(g_flows, g_flow_count, (int)sizeof(flow), by_bytes);

    // ---- what went over the wire ------------------------------------------
    say("");
    say("---- потоки ------------------------------------------------------");

    int shown = 0;
    for (int i = 0; i < g_flow_count && shown < 40; i++)
    {
        if (g_flows[i].out_bytes + g_flows[i].in_bytes < 2048) continue;

        char line[256];
        flow_line(g_flows[i], line, sizeof(line));
        say("%s", line);
        shown++;
    }

    if (!shown) say("  ничего заметного");

    // ---- what never reached the wire --------------------------------------
    say("");
    say("---- соединения на 127.0.0.1 -------------------------------------");
    say("(сырой сокет петлю не видит, это читается из таблицы соединений)");

    if (!g_link_count) say("  нет");

    for (int i = 0; i < g_link_count; i++)
    {
        char who[32];
        pad(who, sizeof(who), name_of(g_links[i].pid), 24);
        say("  %s -> 127.0.0.1:%u", who, g_links[i].port);
    }

    // ---- the answer -------------------------------------------------------
    say("");
    say("---- вердикт -----------------------------------------------------");

    int  media = 0;
    bool app_sent_media = false;
    bool other_sent_media = false;
    bool named = app_name && app_name[0];

    for (int i = 0; i < g_flow_count; i++)
    {
        if (!is_media(g_flows[i])) continue;

        media++;

        char remote[64];
        addr_text(g_flows[i].remote, remote, sizeof(remote));

        const char* who = name_of(g_flows[i].pid);
        say("  медиа: %s -> %s:%u  (%llu наружу, %llu внутрь, пик %u п/с)",
            who, remote, g_flows[i].remote_port,
            g_flows[i].out_pkts, g_flows[i].in_pkts, g_flows[i].peak_pps);

        if (named && contains_ci(who, app_name)) app_sent_media = true;
        else                                     other_sent_media = true;
    }

    say("");

    if (!media)
    {
        say("  Медиа-потока не видно вообще.");
        say("  Либо звонок за это время так и не начался, либо голос уходит");
        say("  с адреса, которого нет среди прослушанных выше.");
    }
    else if (named && app_sent_media)
    {
        say("  %s слал UDP наружу сам.", app_name);
        say("  Значит голос идёт мимо прокси, напрямую.");
    }
    else if (named && other_sent_media)
    {
        say("  UDP наружу слал не %s, а другой процесс - он в строках выше.", app_name);
        say("  Значит голос действительно уходит через него, а не напрямую.");
    }
    else
    {
        say("  Процесс в строках выше - это и есть тот, кто шлёт медиа наружу.");
    }

    if (saw_v6) say("  (ipv6 на машине есть и тоже прослушивался)");

    say("");
    say("Всё это записано в лог: %%LOCALAPPDATA%%\\IMDiscord\\imdiscord.log");

    if (g_console_owned)
    {
        say("");
        say("Enter - закрыть.");

        char c; DWORD got = 0;
        ReadFile(GetStdHandle(STD_INPUT_HANDLE), &c, 1, &got, 0);
    }

    return 0;
}
