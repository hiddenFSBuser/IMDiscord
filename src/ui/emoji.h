#pragma once

// Unicode emoji in message text.
//
// Not the server ones - those are <:name:id> and come from discord's own cdn.
// These are the ordinary characters, 🙂 and 👍 and the rest, which arrive as
// plain utf-8 and which this client cannot draw: the font atlas is built from
// the system ui face, that face has no colour emoji, and half of them live
// outside the plane a 16-bit glyph index can reach anyway.
//
// So they are drawn as pictures, the same way the server ones are. What this
// file does is the part that has to be done here: work out where an emoji
// starts and ends in a byte stream, which is not one character but a sequence -
// a base, then any of a skin tone, a variation selector, a keycap, a flag's
// second half, or a chain of characters joined by zero-width joiners that
// together mean one picture.
namespace uemoji
{
    // Looks at the text starting at p. Returns how many bytes the emoji
    // sequence takes, or 0 if there is no emoji there. On success `url` is
    // filled with where its picture lives.
    //
    // `lacks` answers whether the font has no glyph for a character. Several
    // blocks hold both emoji and ordinary symbols, and what separates them
    // here is not what unicode says they are but whether this client could
    // draw them at all - a character it cannot draw is better as a picture
    // than as a question mark. Pass null to skip that and go by the text
    // alone, which is what a check with no font in front of it does.
    int at(const char* p, char* url, int url_cap, bool (*lacks)(unsigned int) = 0);
}
