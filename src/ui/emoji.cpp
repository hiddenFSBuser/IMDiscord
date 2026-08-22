#include "pch.h"
#include "emoji.h"

// Where the pictures come from.
//
// Twemoji, the set discord itself draws, served by a public cdn. Nothing here
// is discord's: this is the one place in the client that fetches from a host
// that has nothing to do with the account, which is why the whole feature sits
// behind a setting that is off until somebody turns it on.
//
// The alternative was to draw them here. Windows ships a colour emoji face, but
// getting colour out of it needs DirectWrite to walk the COLR layers, or Direct2D
// to do the whole thing - a second and third library loaded for one glyph, and
// several hundred lines that could not be checked without looking at a screen.
// Fetching a png that is already correct costs forty lines and one host.
//
// Which host is not a matter of taste. The tls here is hand written, and it
// does not get through to every cdn: jsdelivr, raw.githubusercontent and
// twitter's own emoji host all end the handshake with a parse error, and the
// pictures simply never arrived. cdnjs answers, so cdnjs it is - the file is
// byte for byte the one the others serve.
namespace
{
    const char* CDN = "https://cdnjs.cloudflare.com/ajax/libs/twemoji/15.1.0/72x72";

    const unsigned int ZWJ = 0x200D;
    const unsigned int VS16 = 0xFE0F;      // "draw the one before me as a picture"
    const unsigned int KEYCAP = 0x20E3;

    // One codepoint out of utf-8, and how many bytes it took. Returns 0 at the
    // end of the string or on a byte that cannot start a character.
    unsigned int decode(const char* p, int* taken)
    {
        const unsigned char* u = (const unsigned char*)p;
        if (!u[0]) { *taken = 0; return 0; }

        if (u[0] < 0x80) { *taken = 1; return u[0]; }

        if ((u[0] & 0xE0) == 0xC0 && (u[1] & 0xC0) == 0x80)
        {
            *taken = 2;
            return ((unsigned int)(u[0] & 0x1F) << 6) | (unsigned int)(u[1] & 0x3F);
        }

        if ((u[0] & 0xF0) == 0xE0 && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80)
        {
            *taken = 3;
            return ((unsigned int)(u[0] & 0x0F) << 12) |
                   ((unsigned int)(u[1] & 0x3F) << 6) |
                   (unsigned int)(u[2] & 0x3F);
        }

        if ((u[0] & 0xF8) == 0xF0 && (u[1] & 0xC0) == 0x80 &&
            (u[2] & 0xC0) == 0x80 && (u[3] & 0xC0) == 0x80)
        {
            *taken = 4;
            return ((unsigned int)(u[0] & 0x07) << 18) |
                   ((unsigned int)(u[1] & 0x3F) << 12) |
                   ((unsigned int)(u[2] & 0x3F) << 6) |
                   (unsigned int)(u[3] & 0x3F);
        }

        *taken = 1;
        return 0;
    }

    bool in(unsigned int c, unsigned int lo, unsigned int hi) { return c >= lo && c <= hi; }

    // The characters that are a picture on their own, without being asked.
    //
    // Everything from U+1F000 up is emoji and nothing else, so that half is a
    // single test. Below it the blocks are shared with arrows, shapes and
    // dingbats that are ordinary text - "↔" and "▪" are not emoji - and only a
    // listed few default to being drawn as one. The rest of that range becomes
    // an emoji only when the text says so with a variation selector, which is
    // handled by the caller.
    bool default_picture(unsigned int c)
    {
        if (c >= 0x1F000 && c <= 0x1FAFF) return true;

        return in(c, 0x231A, 0x231B) || in(c, 0x23E9, 0x23EC) || c == 0x23F0 || c == 0x23F3 ||
               in(c, 0x25FD, 0x25FE) || in(c, 0x2614, 0x2615) || in(c, 0x2648, 0x2653) ||
               c == 0x267F || c == 0x2693 || c == 0x26A1 || in(c, 0x26AA, 0x26AB) ||
               in(c, 0x26BD, 0x26BE) || in(c, 0x26C4, 0x26C5) || c == 0x26CE || c == 0x26D4 ||
               c == 0x26EA || in(c, 0x26F2, 0x26F3) || c == 0x26F5 || c == 0x26FA ||
               c == 0x26FD || c == 0x2705 || in(c, 0x270A, 0x270B) || c == 0x2728 ||
               c == 0x274C || c == 0x274E || in(c, 0x2753, 0x2755) || c == 0x2757 ||
               in(c, 0x2795, 0x2797) || c == 0x27B0 || c == 0x27BF ||
               in(c, 0x2B1B, 0x2B1C) || c == 0x2B50 || c == 0x2B55;
    }

    // Characters that are only ever an emoji when the text asks for it, with a
    // variation selector right after. Without one they are the arrow, the
    // asterisk or the copyright sign they have always been.
    bool picture_if_asked(unsigned int c)
    {
        return c == 0x00A9 || c == 0x00AE || c == 0x203C || c == 0x2049 || c == 0x2122 ||
               c == 0x2139 || in(c, 0x2194, 0x21AA) || in(c, 0x231A, 0x23FA) || c == 0x24C2 ||
               in(c, 0x25AA, 0x25FE) || in(c, 0x2600, 0x27BF) || in(c, 0x2934, 0x2935) ||
               in(c, 0x2B00, 0x2BFF) || c == 0x3030 || c == 0x303D || c == 0x3297 ||
               c == 0x3299 || in(c, 0x0030, 0x0039) || c == 0x0023 || c == 0x002A;
    }

    bool skin_tone(unsigned int c) { return in(c, 0x1F3FB, 0x1F3FF); }
    bool regional(unsigned int c) { return in(c, 0x1F1E6, 0x1F1FF); }
    bool tag_char(unsigned int c) { return in(c, 0xE0020, 0xE007F); }

    void hex_append(char* out, int cap, int* at, unsigned int v)
    {
        char digits[8];
        int n = 0;
        if (!v) digits[n++] = '0';
        while (v) { digits[n++] = "0123456789abcdef"[v & 0xF]; v >>= 4; }

        while (n > 0 && *at < cap - 1) out[(*at)++] = digits[--n];
        out[*at] = 0;
    }
}

int uemoji::at(const char* p, char* url, int url_cap, bool (*lacks)(unsigned int))
{
    if (!p || !p[0] || !url || url_cap < 64) return 0;

    unsigned int seq[24];
    int count = 0;
    int used = 0;

    int taken = 0;
    unsigned int c = decode(p, &taken);
    if (!c || !taken) return 0;

    // The first character decides whether there is an emoji here at all. A
    // digit, a hash or an asterisk qualifies only as the start of a keycap,
    // which is checked below - otherwise every number in every message would
    // turn into a picture.
    bool asked = false;
    {
        int look = 0;
        unsigned int next = decode(p + taken, &look);
        asked = next == VS16;
    }

    bool keycap_start = (c == 0x0023 || c == 0x002A || in(c, 0x0030, 0x0039));

    // The shared blocks, decided by what the font can actually do.
    //
    // Unicode says these characters are text unless the message asks for a
    // picture with a variation selector, and that is the right rule for a
    // client whose font can draw them. This one cannot draw most of them: the
    // atlas is the system ui face, and a warning sign or a small blue diamond
    // comes out as a question mark. A question mark is not what the text says
    // either, so when the font has nothing the picture wins.
    //
    // Arrows and geometric shapes stay text, because the font does have
    // those and turning them into pictures would be the surprising thing.
    bool no_glyph = lacks && lacks(c);

    if (!default_picture(c) && !((asked || no_glyph) && picture_if_asked(c))) return 0;
    if (keycap_start && !asked) return 0;

    seq[count++] = c;
    used += taken;

    // Everything that can be glued to what came before. A flag is two regional
    // indicators, a keycap is a digit and a mark, a person can carry a skin
    // tone, and a chain joined by zero-width joiners is one picture however
    // long it runs - a family of four is seven characters and one image.
    for (;;)
    {
        if (count >= 20) break;

        int look = 0;
        unsigned int n = decode(p + used, &look);
        if (!n || !look) break;

        bool glue = n == VS16 || n == ZWJ || n == KEYCAP || skin_tone(n) || tag_char(n) ||
                    (regional(n) && count == 1 && regional(seq[0]));

        if (!glue)
        {
            // Only after a joiner, and then whatever follows belongs to the
            // sequence whatever it is - that is what the joiner said.
            if (count && seq[count - 1] == ZWJ)
            {
                seq[count++] = n;
                used += look;
                continue;
            }
            break;
        }

        seq[count++] = n;
        used += look;

        // A flag is exactly two, and a keycap ends at the mark. Reading on
        // would swallow the next flag's first half.
        if (n == KEYCAP) break;
        if (regional(n)) break;
    }

    // A joiner with nothing after it is not part of anything.
    while (count && seq[count - 1] == ZWJ) count--;
    if (!count) return 0;

    // A digit is only ever an emoji as part of a keycap. Without the mark
    // this is a number somebody wrote with a variation selector after it,
    // and asking for a picture of it would be asking for a file that does
    // not exist.
    bool has_keycap = false;
    bool has_zwj = false;
    for (int i = 0; i < count; i++)
    {
        if (seq[i] == KEYCAP) has_keycap = true;
        if (seq[i] == ZWJ) has_zwj = true;
    }

    if (keycap_start && !has_keycap) return 0;

    // The file is named by the codepoints, and the variation selector is
    // dropped from that name - unless the sequence is held together by a
    // joiner, where it is kept. That is twemoji's own rule and not a
    // guess at one: the rainbow flag is 1f3f3-fe0f-200d-1f308, while the
    // heart is plain 2764 and the keycap plain 31-20e3.
    bool keep_vs = has_zwj;

    int at = 0;
    for (int i = 0; CDN[i] && at < url_cap - 1; i++) url[at++] = CDN[i];
    if (at < url_cap - 1) url[at++] = '/';
    url[at] = 0;

    bool first = true;
    for (int i = 0; i < count; i++)
    {
        if (seq[i] == VS16 && !keep_vs) continue;

        if (!first && at < url_cap - 1) url[at++] = '-';
        first = false;
        hex_append(url, url_cap, &at, seq[i]);
    }

    const char* tail = ".png";
    for (int i = 0; tail[i] && at < url_cap - 1; i++) url[at++] = tail[i];
    url[at] = 0;

    return used;
}
