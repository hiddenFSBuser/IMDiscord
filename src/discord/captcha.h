#pragma once

struct jval;

// hCaptcha through the default browser, not a pasted token.
//
// Some requests come back 400 with a captcha demand instead of a result
// (adding a stranger, joining through an invite). The widget cannot run
// inside this client - it is obfuscated JavaScript that fingerprints a
// whole browser - so it runs where browsers live: a one-page harness on
// 127.0.0.1, opened in whatever browser the system defaults to, posting
// the token back to the loopback listener. Nothing listens anywhere else,
// the page lives for one solve, and the browser goes there directly,
// without any proxy this client itself uses.

namespace captcha
{
    struct challenge
    {
        char service[32];      // "hcaptcha"; anything else is refused
        char sitekey[128];
        char session[64];      // captcha_session_id, may be empty
        char rqdata[4096];
        char rqtoken[256];     // captcha_rqtoken, may be empty
    };

    // True when this 400 body is a captcha demand, parsed into out.
    bool parse_demand(const jval* root, challenge* out);

    // Blocking, call from a worker thread. Hosts the harness, opens the
    // default browser on it and waits for the token: up to five minutes,
    // less if the person cancels in the client. One solve at a time; a
    // second demand while one waits is refused at once. action names the
    // request for the waiting popup ("Заявка в друзья"...).
    bool solve_blocking(const challenge* ch, const char* action,
                        char* token_out, int token_cap);

    // For the waiting popup, read on the interface thread.
    bool waiting();
    const char* action_text();
    void cancel();
    void reopen_browser();

    // Whether a demand opens the browser at once. Off by default: the popup
    // offers to open it instead. Persisted in the settings.
    void set_auto_open(bool on);
    bool auto_open();
}
