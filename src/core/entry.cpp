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
#include "imgui.h"
#include "ui/emoji.h"
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
    bool emojitest = false;
    bool fonttest = false;
    for (const wchar_t* p = cmdline; *p; p++)
    {
        if (p[0] == L'-' && p[1] == L'-' && p[2] == L's' && p[3] == L'e' && p[4] == L'l' &&
            p[5] == L'f' && p[6] == L't' && p[7] == L'e' && p[8] == L's' && p[9] == L't')
        {
            selftest = true;
            break;
        }
    }

    for (const wchar_t* p = cmdline; *p; p++)
    {
        if (p[0] == L'-' && p[1] == L'-' && p[2] == L'e' && p[3] == L'm' && p[4] == L'o' &&
            p[5] == L'j' && p[6] == L'i' && p[7] == L't' && p[8] == L'e' && p[9] == L's' &&
            p[10] == L't')
        {
            emojitest = true;
            break;
        }
    }

    for (const wchar_t* p = cmdline; *p; p++)
    {
        if (p[0] == L'-' && p[1] == L'-' && p[2] == L'f' && p[3] == L'o' && p[4] == L'n' &&
            p[5] == L't' && p[6] == L't' && p[7] == L'e' && p[8] == L's' && p[9] == L't')
        {
            fonttest = true;
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

    // --emojitest prints the address each emoji sequence resolves to. The
    // splitting is the whole risk in that path - a family is seven
    // characters and one picture, a flag is two and one - and it is the part
    // that cannot be checked by looking at a screen.
    // --fonttest builds the atlas with no window behind it and says which of
    // the characters servers decorate their channel names with the font can
    // draw. Every one it cannot is a question mark on screen, and guessing
    // which from a screenshot is how this was chased once already.
    if (fonttest)
    {
        struct probe { unsigned int cp; const char* what; };
        const probe probes[] = {
            { 0x00B7, "middle dot" },       { 0x2022, "bullet" },
            { 0x2219, "bullet operator" },  { 0x22C5, "dot operator" },
            { 0x2027, "hyphenation point" },{ 0x02D6, "modifier plus" },
            { 0x2502, "box bar" },          { 0x250A, "dashed bar" },
            { 0x256D, "box corner" },       { 0x2570, "box corner" },
            { 0x21B3, "arrow down right" }, { 0x2937, "arrow curving" },
            { 0x2726, "black star" },       { 0x2727, "white star" },
            { 0x273F, "blossom" },          { 0x2741, "florette" },
            { 0x279C, "heavy arrow" },      { 0x27A4, "black arrowhead" },
            { 0x2E31, "word separator" },   { 0x3000, "ideographic space" },
            { 0x300C, "corner bracket" },   { 0x3030, "wavy dash" },
            { 0x30FB, "katakana dot" },     { 0xFF5C, "fullwidth bar" },
            { 0xFF0D, "fullwidth minus" },  { 0x4E28, "cjk bar" },
            { 0x2500, "box line" },         { 0x00A0, "no-break space" },
        };

        // imconfig leaves imgui with no allocator until this is set, and
        // CreateContext hands back a null context without one - which is a
        // crash three lines later rather than an error here.
        ImGui::SetAllocatorFunctions(
            [](size_t size, void*) -> void* { return memalloc((int)size); },
            [](void* ptr, void*) { memfree(ptr); });

        ImGui::CreateContext();
        // No Build() here: app_build_fonts already did it and then took the
        // fallback glyph away. Building again would put it back, and this
        // check would be measuring its own second build.
        app_build_fonts();

        ImFont* f = ImGui::GetIO().Fonts->Fonts.empty() ? 0 : ImGui::GetIO().Fonts->Fonts[0];
        int missing = 0;
        int noisy = 0;

        for (int i = 0; i < (int)(sizeof(probes) / sizeof(probes[0])); i++)
        {
            bool have = f && f->FindGlyphNoFallback((ImWchar)probes[i].cp) != 0;
            if (!have) missing++;

            // What matters is not whether the glyph is there - most of these
            // are not - but that one that is not draws nothing and takes no
            // room, instead of a question mark.
            float advance = 0.0f;
            if (f && (int)probes[i].cp < f->IndexAdvanceX.Size)
                advance = f->IndexAdvanceX.Data[probes[i].cp];

            bool drawn = f && f->FindGlyph((ImWchar)probes[i].cp) != 0;
            if (!have && (drawn || advance != 0.0f)) noisy++;

            log_line("fonttest: U+%04X %s: глиф %s, рисуется %s, ширина %d",
                     probes[i].cp, probes[i].what, have ? "есть" : "нет",
                     drawn ? "да" : "нет", (int)(advance * 10.0f));
        }

        log_line("fonttest: без глифа %d из %d, из них шумят %d",
                 missing, (int)(sizeof(probes) / sizeof(probes[0])), noisy);

        WSACleanup();
        log_shutdown();
        ExitProcess(0);
    }

    if (emojitest)
    {
        // The fetch below goes through the real stack, so the real stack has to
        // be standing. Without this it fails in a millisecond with no error to
        // read, which looks exactly like a host refusing us.
        proxy::init();
        http::init("IMDiscord/1.0");
        const char* cases[] = {
            "\xF0\x9F\x99\x82",                                                  // slightly smiling
            "\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD",                                  // thumbs up, skin tone
            "\xE2\x9D\xA4\xEF\xB8\x8F",                                          // heart with selector
            "\xE2\x9C\x85",                                                  // check mark
            "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7",  // family
            "\xF0\x9F\x87\xB7\xF0\x9F\x87\xBA",                                  // flag
            "1\xEF\xB8\x8F\xE2\x83\xA3",                                        // keycap one
            "\xF0\x9F\x8F\xB3\xEF\xB8\x8F\xE2\x80\x8D\xF0\x9F\x8C\x88",              // rainbow flag
            "1",                                                        // a plain digit
            "\xE2\x86\x94",                                                  // a plain arrow
            "\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82",                        // ordinary text
        };

        // Which hosts this client's own tls can actually talk to. The address
        // being right is only half of it: the handshake is hand written here,
        // and a cdn it cannot get through is a cdn this feature cannot use.
        const char* hosts[] = {
            "https://cdn.jsdelivr.net/gh/jdecked/twemoji@latest/assets/72x72/1f642.png",
            "https://unpkg.com/twemoji@14.0.2/assets/72x72/1f642.png",
            "https://raw.githubusercontent.com/jdecked/twemoji/main/assets/72x72/1f642.png",
            "https://cdnjs.cloudflare.com/ajax/libs/twemoji/14.0.2/72x72/1f642.png",
            "https://abs.twimg.com/emoji/v2/72x72/1f642.png",
            "https://abs-0.twimg.com/emoji/v2/72x72/1f642.png",
            "https://twemoji.maxcdn.com/v/latest/72x72/1f642.png",
            "https://cdn.discordapp.com/emojis/1.png",
        };

        for (int i = 0; i < (int)(sizeof(hosts) / sizeof(hosts[0])); i++)
        {
            http_response res;
            res.init();
            bool ok = http::get(hosts[i], &res);
            log_line("emojitest: хост %s -> %s, http %d, %u байт", hosts[i],
                     ok ? "дошли" : "не дошли", res.status, res.body.size);
            res.free_response();
        }

        for (int i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++)
        {
            char url[256];
            int taken = uemoji::at(cases[i], url, sizeof(url));

            if (taken) log_line("emojitest: %s -> %d байт, %s", cases[i], taken, url);
            else       log_line("emojitest: %s -> не смайлик", cases[i]);

            // And then actually fetch it, through this client's own http and
            // its own tls. Working out the address is only half the path; the
            // other half is a host nothing else here has ever talked to.
            if (taken)
            {
                http_response res;
                res.init();
                bool ok = http::get(url, &res);
                log_line("emojitest:   загрузка: %s, http %d, %u байт, %s",
                         ok ? "запрос прошёл" : "запрос не прошёл", res.status,
                         res.body.size, res.content_type);
                res.free_response();
            }
        }

        WSACleanup();
        log_shutdown();
        ExitProcess(0);
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
