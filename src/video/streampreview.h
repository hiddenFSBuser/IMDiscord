#pragma once
#include "discord/types.h"

// Server-side stills of somebody else's Go Live stream, for the LIVE button.
//
// The official client uploads a frame of the stream every few minutes
// (POST /api/v9/streams/{key}/preview), and shows that still when a viewer
// hovers the stream. The same still is fetched here, with a plain authed GET
// on the same path, and drawn inside the button's tooltip.
//
// A still is a courtesy, not a promise: a streamer on an old client, on this
// client, or with previews switched off has none, and then the tooltip says
// so rather than sitting empty.

namespace streampreview
{
    void init();
    void shutdown();

    // Starts a background fetch for this stream, or does nothing when one is
    // already in flight or a fresh still is held. Safe to call every frame
    // while hovered; the request goes out at most once per refresh window.
    void request(snowflake guild_id, snowflake channel_id, snowflake user_id);

    // The tooltip itself: the still when one has arrived, a status line while
    // it is on its way or unavailable, and the action hint. Call it in place
    // of SetTooltip, while the LIVE button is hovered.
    void tooltip(snowflake guild_id, snowflake channel_id, snowflake user_id,
                 bool watching, const char* display_name);
}
