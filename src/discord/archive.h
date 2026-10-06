#pragma once
#include "types.h"

struct jval;

// The client's own copy of every message it has ever seen.
//
// Three things need it. A channel opens instantly because its history is on
// disk before discord answers. A message somebody deletes stays visible,
// because deletion here is a flag rather than a removal. And when there is no
// connection at all - the network is down, or the token stopped working - the
// saved side of the client is still worth reading.
//
// One append-only file per channel, one JSON object per line, newest wins. A
// log rather than a database because the only writes are appends, the only
// reads are whole-channel, and a file that can be opened in a text editor is
// worth a great deal when something goes wrong.
//
// Alongside it, the ranges: which stretches of a channel were fetched in one
// contiguous run. Without them an export cannot tell a quiet afternoon from a
// hole where nothing was ever saved.

namespace archive
{
    // Everything lives under a folder named after the account, so two accounts
    // on one machine never overwrite each other.
    void init(snowflake self_id);
    void shutdown();
    bool ready();

    // Records a message as it stands. Cheap to call repeatedly: nothing is
    // written unless something actually changed.
    void put(const dmessage* m);
    void put_json(const jval* v);

    // Marks it deleted instead of dropping it.
    void mark_deleted(snowflake channel_id, snowflake message_id);

    // Notes that everything between these two ids arrived in one run.
    void note_range(snowflake channel_id, snowflake from_id, snowflake to_id);

    // Reads a channel back into the store, oldest first, without disturbing
    // anything already there. Returns how many messages it added.
    int load_channel(snowflake channel_id);

    // How many messages are on disk for a channel, and for everything.
    unsigned int channel_count(snowflake channel_id);
    unsigned int total_messages();
    unsigned int total_channels();

    // The ranges of a channel, oldest first, for the exporter.
    struct span
    {
        snowflake from_id;
        snowflake to_id;
    };
    int channel_spans(snowflake channel_id, span* out, int cap);

    // Every channel the archive holds, for the exporter and the statistics.
    int all_channels(snowflake* out, int cap);

    // ---- local search ----
    //
    // Searches message text across warmed history without touching the
    // network: live memory first, then the archive files on disk. Zero
    // scope means everywhere. Returns hits stored, total matches seen.
    struct search_hit
    {
        snowflake channel_id;
        snowflake id;
        snowflake author_id;
        char author_name[64];
        char timestamp[48];
        char snippet[256];

        // The attachment this hit is about. Empty for text hits.
        char file_url[700];
        char filename[128];
        unsigned int file_size;
        int media_kind;      // 0 none, 1 image, 2 video, 3 other file
        int attach_index;
    };

    enum search_flags
    {
        SEARCH_CASE = 1,     // case sensitive (default folds case)
        SEARCH_WHOLE = 2,    // whole words only
        SEARCH_PREFIX = 4,   // match at word starts instead of anywhere
        SEARCH_MEDIA = 8,    // only messages carrying attachments
        SEARCH_IMAGES = 16,  // per-attachment hits, images only
        SEARCH_VIDEOS = 32,  // per-attachment hits, videos only
    };

    int search_local(const char* query, int flags, snowflake scope_channel,
                     int year, int month,
                     search_hit* out, int cap, int* total);

    // ---- the snapshot ----
    //
    // Messages are only half of what a client needs to be worth opening with
    // no connection. The other half is who everybody is: the friend list, the
    // servers and their channels, the profiles of people who have said
    // something. None of that arrives again while the account is unreachable,
    // so it is written down on a timer and read back at startup.
    bool snapshot_save();
    bool snapshot_load();
    unsigned long long snapshot_age_seconds();
}
