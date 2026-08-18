#include "pch.h"
#include "log.h"
#include "system/io/ufile.h"

static HANDLE g_log_file = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION g_log_lock;
static bool g_log_ready = false;

namespace
{
    // Whether this run is one of the "--*test" modes rather than the client.
    //
    // Those write somewhere else and leave the ordinary log alone. A test run
    // that overwrites the record of the session somebody is trying to explain
    // is worse than no test at all, and it has already happened here more than
    // once - including to the very session that was being asked about.
    bool test_run()
    {
        const wchar_t* cmd = GetCommandLineW();

        for (const wchar_t* p = cmd; *p; p++)
        {
            if (p[0] != L'-' || p[1] != L'-') continue;

            // Every one of them ends in "test": selftest, mp4test, mp3test,
            // audiotest, tlstest, wstest, proxytest, capturetest.
            for (const wchar_t* q = p + 2; *q && *q != L' '; q++)
            {
                if (q[0] == L't' && q[1] == L'e' && q[2] == L's' && q[3] == L't' &&
                    (q[4] == 0 || q[4] == L' '))
                    return true;
            }
        }
        return false;
    }
}

void log_init()
{
    if (g_log_ready) return;

    InitializeCriticalSection(&g_log_lock);
    g_log_ready = true;

    bool testing = test_run();

    wchar_t path[MAX_PATH];
    wchar_t chosen[MAX_PATH];
    chosen[0] = 0;

    // Under the profile first, because that is where every other file this
    // client keeps lives. A second copy still running holds that one open, so a
    // numbered name is tried next, and if the profile is unreachable altogether
    // the log lands beside the executable instead.
    //
    // Something has to succeed here. A run that leaves no trace anywhere turns
    // every question about it into guesswork, which is exactly how a whole
    // debugging round was wasted.
    //
    // The previous run is moved aside first. Opening this one truncates it,
    // and what somebody wants to look at is almost always what happened just
    // before they closed the client and started it again to fetch the file -
    // which is precisely the run that used to be destroyed by fetching it.
    if (!testing)
    {
        wchar_t live[MAX_PATH];
        wchar_t kept[MAX_PATH];

        if (ufile::app_path(L"imdiscord.log", live, MAX_PATH) &&
            ufile::app_path(L"imdiscord.prev.log", kept, MAX_PATH))
        {
            DeleteFileW(kept);
            MoveFileW(live, kept);
        }
    }

    for (int attempt = 1; attempt <= 5 && g_log_file == INVALID_HANDLE_VALUE; attempt++)
    {
        char narrow[64];
        wchar_t name[64];

        const char* base = testing ? "imdiscord.test" : "imdiscord";

        if (attempt == 1) cnprint(narrow, sizeof(narrow), "%s.log", base);
        else              cnprint(narrow, sizeof(narrow), "%s.%d.log", base, attempt);
        chartowcs(narrow, name, 64);

        if (!ufile::app_path(name, path, MAX_PATH)) break;
        g_log_file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, 0,
                                 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
        if (g_log_file != INVALID_HANDLE_VALUE)
            for (int i = 0; i < MAX_PATH && (chosen[i] = path[i]) != 0; i++) {}
    }

    if (g_log_file == INVALID_HANDLE_VALUE)
    {
        wchar_t exe[MAX_PATH];
        DWORD n = GetModuleFileNameW(0, exe, MAX_PATH);
        if (n > 0 && n < MAX_PATH)
        {
            while (n > 0 && exe[n - 1] != L'\\' && exe[n - 1] != L'/') n--;
            exe[n] = 0;

            const wchar_t* leaf = L"imdiscord.log";
            for (int i = 0; leaf[i] && n < MAX_PATH - 1; i++) exe[n++] = leaf[i];
            exe[n] = 0;

            g_log_file = CreateFileW(exe, GENERIC_WRITE, FILE_SHARE_READ, 0,
                                     CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
            if (g_log_file != INVALID_HANDLE_VALUE)
                for (int i = 0; i < MAX_PATH && (chosen[i] = exe[i]) != 0; i++) {}
        }
    }

    // The build this log came out of, not just the run. Several rounds of
    // chasing one defect have turned on the question "was that the version
    // with the fix in it", and a log that cannot answer it costs a whole
    // exchange to find out.
    log_line("---- IMDiscord start (pid %u, сборка %s %s) ----",
             (unsigned int)GetCurrentProcessId(), __DATE__, __TIME__);
    if (chosen[0])
    {
        char where[MAX_PATH];
        wcstochar(chosen, where, (int)sizeof(where));
        log_line("log: пишу в %s", where);
    }
}

void log_shutdown()
{
    if (!g_log_ready) return;
    EnterCriticalSection(&g_log_lock);
    if (g_log_file != INVALID_HANDLE_VALUE)
    {
        CloseHandle(g_log_file);
        g_log_file = INVALID_HANDLE_VALUE;
    }
    LeaveCriticalSection(&g_log_lock);
}

static void log_raw(const char* text, int len)
{
    OutputDebugStringA(text);

    if (!g_log_ready || g_log_file == INVALID_HANDLE_VALUE) return;

    EnterCriticalSection(&g_log_lock);
    DWORD written = 0;
    WriteFile(g_log_file, text, (DWORD)len, &written, 0);
    FlushFileBuffers(g_log_file);
    LeaveCriticalSection(&g_log_lock);
}

void log_line(const char* fmt, ...)
{
    char line[4096];

    SYSTEMTIME st;
    GetLocalTime(&st);
    int head = cnprint(line, sizeof(line), "[%02d:%02d:%02d.%03d] ",
                       st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    if (head < 0) head = 0;

    va_list args;
    va_start(args, fmt);
    int n = cvnprint(line + head, sizeof(line) - head - 2, fmt, args);
    va_end(args);
    if (n < 0) n = 0;

    int total = head + n;
    if (total > (int)sizeof(line) - 3) total = (int)sizeof(line) - 3;
    line[total++] = '\r';
    line[total++] = '\n';
    line[total] = 0;

    log_raw(line, total);
}

void log_bytes(const char* tag, const void* data, unsigned int size)
{
    const unsigned char* p = (const unsigned char*)data;
    const char* hex = "0123456789abcdef";

    unsigned int shown = size > 256 ? 256 : size;
    char buf[3 * 256 + 1];
    for (unsigned int i = 0; i < shown; i++)
    {
        buf[i * 3 + 0] = hex[(p[i] >> 4) & 0xF];
        buf[i * 3 + 1] = hex[p[i] & 0xF];
        buf[i * 3 + 2] = ' ';
    }
    buf[shown * 3] = 0;
    log_line("%s (%u bytes): %s", tag, size, buf);
}

static LONG WINAPI crash_filter(EXCEPTION_POINTERS* info)
{
    unsigned __int64 base = (unsigned __int64)GetModuleHandleW(0);
    unsigned __int64 pc = (unsigned __int64)info->ExceptionRecord->ExceptionAddress;

    log_line("!!! unhandled exception 0x%08X at 0x%llX (image+0x%llX)",
             (unsigned int)info->ExceptionRecord->ExceptionCode, pc,
             pc >= base ? pc - base : 0);

    if (info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        info->ExceptionRecord->NumberParameters >= 2)
    {
        log_line("    %s at 0x%llX",
                 info->ExceptionRecord->ExceptionInformation[0] ? "write" : "read",
                 (unsigned __int64)info->ExceptionRecord->ExceptionInformation[1]);
    }

    log_shutdown();
    return EXCEPTION_EXECUTE_HANDLER;
}

void log_install_crash_handler()
{
    SetUnhandledExceptionFilter(crash_filter);
}
