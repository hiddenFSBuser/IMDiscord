#pragma once
#include "imgui.h"

struct ID3D11ShaderResourceView;

enum texture_state
{
    TEX_EMPTY = 0,
    TEX_LOADING,
    TEX_READY,
    TEX_FAILED,
};

struct texture
{
    ID3D11ShaderResourceView* srv;
    int width;
    int height;
    volatile long state;
    bool animated;

    bool ready() const { return state == TEX_READY && srv != 0; }
    ImTextureID id() const { return (ImTextureID)srv; }
};

// Image cache shared by avatars, guild icons and chat attachments.
//
// Downloads run on the job pool; D3D11 resource creation is thread safe when
// the device is not created with the single-threaded flag, so worker threads
// build the texture directly and the UI only ever reads.
namespace tex
{
    void init();
    void shutdown();

    // Never null. Returns an entry in TEX_LOADING state on the first call.
    const texture* get(const char* url);

    // Called once per UI frame. Advances animated images that were drawn
    // recently; GIF frames are decoded here rather than up front.
    void advance_animations();

    // Also once per UI frame, but it only does work every half minute: hands
    // back the texture and decoder of anything nobody has looked at in a while.
    // The entry survives, so the next tex::get reloads it, normally straight
    // off the disk cache.
    void collect();

    // Fetches raw bytes for a url through the same cache (used to save files).
    bool fetch_blob(const char* url, ubuffer* out);

    // Bytes currently held by decoded textures.
    unsigned int memory_used();
    int pending_downloads();

    // How long downloaded images stay on disk. Zero switches the disk cache
    // off and clears it. The size figures come from the last sweep.
    int cache_ttl_hours();
    void set_cache_ttl_hours(int hours);
    unsigned int cache_disk_kb();
    int cache_file_count();
}
