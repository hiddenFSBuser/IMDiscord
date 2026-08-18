#include "pch.h"
#include "app.h"
#include "log.h"
#include "oslib.h"
#include "crypto.h"
#include "dave/dave_tests.h"
#include "audio/noise.h"
#include "audio/loopback.h"
#include "audio/music.h"
#include "video/encoder.h"
#include "net/proxy.h"
#include "net/tlsconn.h"
#include "net/websocket.h"
#include "video/player.h"
#include "video/capture.h"
#include "net/http.h"

// The image is linked with /NODEFAULTLIB, so nothing runs dynamic initializers
// for us. Walking the .CRT$XC* section by hand is the whole of "C++ runtime
// startup" that this project needs; teardown happens through ExitProcess.
typedef void(__cdecl* ctor_fn)(void);

#pragma section(".CRT$XCA", long, read)
#pragma section(".CRT$XCZ", long, read)
#pragma comment(linker, "/merge:.CRT=.rdata")

extern "C" __declspec(allocate(".CRT$XCA")) ctor_fn __xc_a[] = { 0 };
extern "C" __declspec(allocate(".CRT$XCZ")) ctor_fn __xc_z[] = { 0 };

static void run_static_ctors()
{
    for (ctor_fn* it = __xc_a; it < __xc_z; it++)
        if (*it) (*it)();
}

extern "C" void __stdcall im_entry()
{
    run_static_ctors();

    log_init();
    log_install_crash_handler();

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    oslib::init();

    // "--selftest" runs the crypto known-answer tests and exits, so the MLS
    // primitives can be checked without opening a window or touching discord.
    const wchar_t* cmdline = GetCommandLineW();
    bool selftest = false;
    for (const wchar_t* p = cmdline; *p; p++)
    {
        if (p[0] == L'-' && p[1] == L'-' && p[2] == L's' && p[3] == L'e' && p[4] == L'l' &&
            p[5] == L'f' && p[6] == L't' && p[7] == L'e' && p[8] == L's' && p[9] == L't')
        {
            selftest = true;
            break;
        }
    }

    // "--proxytest socks5://user:pass@host:port" walks a proxy through every
    // step this client will ask of it and says which one failed. Chasing a
    // proxy problem through the whole client is guesswork; this is not.
    {
        const wchar_t* found = 0;
        for (const wchar_t* p = cmdline; *p; p++)
        {
            if (p[0] == L'-' && p[1] == L'-' && p[2] == L'p' && p[3] == L'r' && p[4] == L'o' &&
                p[5] == L'x' && p[6] == L'y' && p[7] == L't' && p[8] == L'e' && p[9] == L's' &&
                p[10] == L't')
            {
                found = p + 11;
                break;
            }
        }

        if (found)
        {
            while (*found == L' ' || *found == L'"') found++;

            char text[512];
            int n = 0;
            while (found[n] && found[n] != L' ' && found[n] != L'"' && n < 511)
            {
                text[n] = (char)found[n];
                n++;
            }
            text[n] = 0;

            proxy::init();
            http::init("IMDiscord/1.0");

            proxy_config cfg;
            bool ok = proxy::parse_url(text, &cfg);
            if (!ok)
            {
                log_line("proxytest: не разобрал строку \"%s\"", text);
            }
            else
            {
                ok = proxy::self_test(&cfg);

                // And now a real request through the client's own transport,
                // which dials the proxy itself rather than being pointed at a
                // shim.
                http::set_proxy(&cfg);

                http_response res;
                res.init();
                bool got = http::get("https://discord.com/api/v9/gateway", &res);
                log_line("proxytest: запрос через WinHTTP - %s, код %d, тело %u байт",
                         got ? "прошёл" : "НЕ прошёл", res.status, res.body.size);
                if (got && res.status == 200)
                    log_line("proxytest: ответ %s", res.text());
                else
                    ok = false;
                res.free_response();
            }

            proxy::shutdown();
            WSACleanup();
            log_shutdown();
            ExitProcess(ok ? 0 : 1);
        }
    }

    // "--mp4test <path>" decodes a file and says whether a picture came out.
    {
        const wchar_t* found = 0;
        for (const wchar_t* p = cmdline; *p; p++)
        {
            if (p[0] == L'-' && p[1] == L'-' && p[2] == L'm' && p[3] == L'p' && p[4] == L'4' &&
                p[5] == L't' && p[6] == L'e' && p[7] == L's' && p[8] == L't')
            {
                found = p + 9;
                break;
            }
        }

        if (found)
        {
            while (*found == L' ' || *found == L'"') found++;

            wchar_t path[MAX_PATH];
            int n = 0;
            while (found[n] && found[n] != L'"' && n < MAX_PATH - 1) { path[n] = found[n]; n++; }
            while (n > 0 && path[n - 1] == L' ') n--;
            path[n] = 0;

            bool ok = player::self_test(path);
            WSACleanup();
            log_shutdown();
            ExitProcess(ok ? 0 : 1);
        }
    }

    // "--audiotest" reports whether this machine can capture system sound
    // without capturing our own, which is the one thing that decides if a
    // share can carry audio at all.
    {
        bool wanted = false;
        for (const wchar_t* p = cmdline; *p; p++)
        {
            if (p[0] == L'-' && p[1] == L'-' && p[2] == L'a' && p[3] == L'u' && p[4] == L'd' &&
                p[5] == L'i' && p[6] == L'o' && p[7] == L't' && p[8] == L'e' && p[9] == L's' &&
                p[10] == L't')
            {
                wanted = true;
                break;
            }
        }

        if (wanted)
        {
            bool ok = loopback::self_test();
            WSACleanup();
            log_shutdown();
            ExitProcess(ok ? 0 : 1);
        }
    }

    // "--tlstest" checks the hand written transport against the real thing:
    // socket, handshake, certificate, one request, one answer.
    {
        bool wanted = false;
        for (const wchar_t* p = cmdline; *p; p++)
        {
            if (p[0] == L'-' && p[1] == L'-' && p[2] == L't' && p[3] == L'l' && p[4] == L's' &&
                p[5] == L't' && p[6] == L'e' && p[7] == L's' && p[8] == L't')
            {
                wanted = true;
                break;
            }
        }

        if (wanted)
        {
            // The second half of the test goes through http::, which needs
            // its agent set the same way the client sets it.
            proxy::init();
            http::init("IMDiscord/1.0");

            bool ok = tlsnet::self_test("discord.com");
            WSACleanup();
            log_shutdown();
            ExitProcess(ok ? 0 : 1);
        }
    }

    // "--mp3test <path>" decodes a track the way the music player will and
    // writes the result beside the log as a wav, so what comes out of the
    // decoder and the resampler can be listened to rather than reasoned about.
    {
        const wchar_t* found = 0;
        for (const wchar_t* p = cmdline; *p; p++)
        {
            if (p[0] == L'-' && p[1] == L'-' && p[2] == L'm' && p[3] == L'p' && p[4] == L'3' &&
                p[5] == L't' && p[6] == L'e' && p[7] == L's' && p[8] == L't')
            {
                found = p + 9;
                break;
            }
        }

        if (found)
        {
            while (*found == L' ' || *found == L'"') found++;

            wchar_t path[MAX_PATH];
            int n = 0;
            while (found[n] && found[n] != L'"' && n < MAX_PATH - 1) { path[n] = found[n]; n++; }
            while (n > 0 && path[n - 1] == L' ') n--;
            path[n] = 0;

            bool ok = music::self_test(path);
            WSACleanup();
            log_shutdown();
            ExitProcess(ok ? 0 : 1);
        }
    }

    // "--wstest" opens the gateway for real and waits for its first message.
    {
        bool wanted = false;
        for (const wchar_t* p = cmdline; *p; p++)
        {
            if (p[0] == L'-' && p[1] == L'-' && p[2] == L'w' && p[3] == L's' &&
                p[4] == L't' && p[5] == L'e' && p[6] == L's' && p[7] == L't')
            {
                wanted = true;
                break;
            }
        }

        if (wanted)
        {
            bool ok = ws::self_test();
            WSACleanup();
            log_shutdown();
            ExitProcess(ok ? 0 : 1);
        }
    }

    // "--capturetest" grabs one frame each way and writes them out, so the
    // orientation can be looked at instead of reasoned about.
    {
        bool wanted = false;
        for (const wchar_t* p = cmdline; *p; p++)
            if (p[0] == L'-' && p[1] == L'-' && p[2] == L'c' && p[3] == L'a' && p[4] == L'p' &&
                p[5] == L't' && p[6] == L'u' && p[7] == L'r' && p[8] == L'e')
            { wanted = true; break; }

        if (wanted)
        {
            bool ok = capture::self_test(CAPTURE_BITBLT, L"capture_bitblt.bmp");
            ok = capture::self_test(CAPTURE_DXGI, L"capture_dxgi.bmp") && ok;
            WSACleanup();
            log_shutdown();
            ExitProcess(ok ? 0 : 1);
        }
    }

    if (selftest)
    {
        venc::log_encoders();

        bool ok = crypto::self_test();
        ok = dave_self_test() && ok;
        ok = noise::self_test() && ok;
        WSACleanup();
        log_shutdown();
        ExitProcess(ok ? 0 : 1);
    }

    int code = app_main();

    WSACleanup();
    log_line("---- IMDiscord exit (%d) ----", code);
    log_shutdown();
    ExitProcess((UINT)code);
}
