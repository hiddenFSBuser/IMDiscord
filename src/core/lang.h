#pragma once

// Interface language.
//
// The client was written in Russian and every string in it is still Russian in
// the source. Rather than replace them with keys - which would have meant
// touching every call site twice and leaving the code unreadable in the
// meantime - the Russian text is the key: tr() looks it up and hands back the
// English when English is chosen, and hands back exactly what it was given
// when it is not.
//
// That has two consequences worth knowing. A string with no translation shows
// through in Russian instead of showing a missing-key marker, which is the
// better failure. And editing a Russian string in the source silently detaches
// it from its translation, so the table is checked against the source by
// tools/check_lang.py rather than by hoping.

enum ui_lang
{
    LANG_RU = 0,
    LANG_EN,
    LANG_COUNT,
};

struct lang_entry
{
    const char* ru;
    const char* en;
};

namespace lang
{
    // Reads the stored choice, or asks windows when nothing was ever chosen.
    void init();

    ui_lang current();
    void set(ui_lang l);

    // What the language calls itself, for the dropdown. A language list that
    // names languages in the language you are trying to leave is no help.
    const char* name(ui_lang l);
}

// Russian in, the chosen language out. Never returns null, and never returns
// anything but the argument itself while Russian is chosen.
const char* tr(const char* ru);
