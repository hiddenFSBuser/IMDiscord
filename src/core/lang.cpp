#include "pch.h"
#include "lang.h"
#include "storage.h"

// The table itself, generated from the source by tools/make_lang.py and sorted
// by hash there so nothing has to be sorted or hashed at startup.
extern const lang_entry LANG_EN_TABLE[];
extern const unsigned int LANG_EN_HASHES[];
extern const int LANG_EN_COUNT;

namespace
{
    int g_lang = -1;        // -1 until init, which is also "never chosen"

    unsigned int hash_of(const char* s)
    {
        // FNV-1a. Chosen for being four lines rather than for being clever:
        // the table is under a thousand entries and the search is a binary one
        // over the hashes, so the work is a handful of comparisons either way.
        unsigned int h = 2166136261u;
        while (*s)
        {
            h ^= (unsigned char)*s++;
            h *= 16777619u;
        }
        return h;
    }

    // What windows is set to. Only Russian keeps the original text - a client
    // in Russian is no use to somebody who does not read it, and English is
    // the one language the rest of the world is likelier to share.
    ui_lang system_language()
    {
        LANGID id = GetUserDefaultUILanguage();
        return (PRIMARYLANGID(id) == 0x19) ? LANG_RU : LANG_EN;   // 0x19 = LANG_RUSSIAN
    }
}

void lang::init()
{
    int stored = storage::settings_get_int("language", -1);

    if (stored != LANG_RU && stored != LANG_EN) stored = (int)system_language();
    g_lang = stored;
}

ui_lang lang::current()
{
    if (g_lang < 0) lang::init();
    return (ui_lang)g_lang;
}

void lang::set(ui_lang l)
{
    if (l != LANG_RU && l != LANG_EN) return;

    g_lang = (int)l;
    storage::settings_set_int("language", g_lang);
    storage::settings_save();
}

const char* lang::name(ui_lang l)
{
    return (l == LANG_RU) ? "Русский" : "English";
}

const char* tr(const char* ru)
{
    if (!ru) return "";
    if (lang::current() == LANG_RU) return ru;

    unsigned int h = hash_of(ru);

    int lo = 0;
    int hi = LANG_EN_COUNT - 1;

    while (lo <= hi)
    {
        int mid = lo + (hi - lo) / 2;

        if (LANG_EN_HASHES[mid] < h) lo = mid + 1;
        else if (LANG_EN_HASHES[mid] > h) hi = mid - 1;
        else
        {
            // Two different strings can land on the same hash, so the run of
            // entries sharing it is walked rather than trusted.
            int i = mid;
            while (i > 0 && LANG_EN_HASHES[i - 1] == h) i--;

            for (; i < LANG_EN_COUNT && LANG_EN_HASHES[i] == h; i++)
                if (ccscmp(LANG_EN_TABLE[i].ru, ru) == 0) return LANG_EN_TABLE[i].en;

            break;
        }
    }

    // Untranslated. The Russian shows through, which is worse than English and
    // much better than an empty label.
    return ru;
}
