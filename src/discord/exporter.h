#pragma once
#include "types.h"

// Turning the archive back into something a person can read.
//
// One self contained HTML file per channel, built from what was saved rather
// than from what discord will hand over now: an account that no longer works
// still exports everything it ever saw.
//
// Attachments are either copied next to the page or left as links. Copying
// them makes the export outlive the message; linking keeps it small and is
// what most exports actually want. The choice is the caller's, because both
// are right for different reasons.
//
// Where a stretch of a conversation was never fetched, the page says so in
// place rather than running two distant messages together as if nothing was
// missing.

enum export_attachments
{
    EXPORT_LINKS_ONLY = 0,   // just the urls, as they were
    EXPORT_SAVE_FILES = 1,   // downloaded into a folder beside the page
};

namespace exporter
{
    // Writes one channel. Returns false only if nothing could be written at
    // all; a missing attachment is reported in the page, not fatal.
    bool channel_to_html(snowflake channel_id, const wchar_t* path,
                         export_attachments files);

    // Everything the archive holds, one file per channel, into a folder.
    // Returns how many channels were written.
    int everything_to_html(const wchar_t* folder, export_attachments files);

    // ---- warm-up ----
    //
    // Pulls whole histories into the archive. Which histories is a choice, and
    // the default is the careful one: direct messages only.
    //
    // Servers are left out unless asked for. A single busy server can be more
    // history than every private conversation put together, and dragging all
    // of it down is thousands of requests against an API that will start
    // refusing them - for channels the user very likely does not care about
    // keeping. Personal conversations are the ones worth having when an
    // account stops working.
    enum warm_scope
    {
        WARM_DIRECT_ONLY = 0,   // every direct and group chat, no servers
        WARM_EVERYTHING = 1,    // every readable channel there is
        WARM_SELECTED = 2,      // only what was ticked
    };

    void warm_set_scope(warm_scope scope);
    warm_scope warm_current_scope();

    // Used by WARM_SELECTED. A guild ticks or unticks all of its text
    // channels at once, which is what a person means by picking a server.
    void warm_select(snowflake channel_id, bool on);
    bool warm_is_selected(snowflake channel_id);
    void warm_select_guild(snowflake guild_id, bool on);
    bool warm_guild_fully_selected(snowflake guild_id);
    void warm_clear_selection();
    unsigned int warm_selection_count();

    // How many channels the current choice would cover, so the button can say
    // what it is about to do before it does it.
    unsigned int warm_planned_count();

    void warm_start();
    void warm_stop();
    bool warming();

    // What it is doing, for the settings view.
    const char* warm_status();
    unsigned int warm_channels_done();
    unsigned int warm_channels_total();
    unsigned int warm_messages();
}
