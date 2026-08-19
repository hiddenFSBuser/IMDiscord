#include "pch.h"
#include <d3d11.h>

#include "textures.h"
#include "core/app.h"
#include "core/log.h"
#include "discord/store.h"
#include "core/offline.h"
#include "core/storage.h"
#include "net/http.h"
#include "system/io/ufile.h"
#include "stb_image.h"
#include "stb_gif_stream.h"
#include "libwebp/webp/decode.h"
#include "libwebp/webp/demux.h"

namespace
{
    // D3D11 feature level 11 guarantees 16384; 8192 keeps a single decoded
    // screenshot under 256 MB while still covering anything realistic.
    const int MAX_DIMENSION = 8192;

    // An animation keeps several canvases plus the compressed file resident,
    // so it gets a much tighter bound than a still. Anything above it still
    // shows up, just as its first frame through the one-shot decoder.
    const int MAX_ANIMATION_DIMENSION = 2048;

    // Animated entries keep the compressed file and one canvas resident rather
    // than every decoded frame, and are advanced from the UI thread.
    struct entry
    {
        unsigned __int64 key;
        texture tex;
        char url[512];

        ID3D11Texture2D* surface;      // kept for dynamic uploads

        // Exactly one of these is set on an animated entry. GIFs go through the
        // streaming wrapper over stb; discord's animated avatars and banners
        // are animated WebP, which libwebp's demuxer already plays frame by
        // frame against a persistent canvas.
        imd_gif* gif;
        WebPAnimDecoder* anim;
        unsigned char* anim_data;      // the bytes the WebP decoder reads from
        int anim_prev_ts;              // WebP timestamps are cumulative

        unsigned long long next_frame_ms;
        unsigned long long used_frame;
        unsigned long long used_ms;    // wall clock, for unloading

        // Who this picture belongs to, and since when nobody here is them.
        //
        // Pictures fetched under one account are of no use to another, and
        // there is no reason for somebody else's friends to sit in this
        // machine's memory for the twenty minutes the ordinary rule allows.
        // Signing back in makes owner match again, which is what stops and
        // resets the clock - it needs no announcement from the switcher and
        // cannot be left running by one.
        snowflake owner;
        unsigned long long foreign_since;
        unsigned int frames_shown;     // since the last rewind
        // What the decoder was counted as when it was added to the memory
        // total. Its own footprint moves across a rewind, so the number that
        // went in is the number that has to come back out.
        long decoder_bytes;
    };

    ulist<entry*> g_entries;
    CRITICAL_SECTION g_lock;
    bool g_ready = false;
    volatile long g_pending = 0;
    volatile long g_memory = 0;
    unsigned long long g_frame_counter = 0;
    unsigned int g_animation_cursor = 0;
    unsigned long long g_last_collect_ms = 0;

    // Read from settings on the UI thread and published here, because the
    // download jobs need it and the settings table is not thread safe.
    volatile long g_cache_hours = 24;
    volatile long g_cache_files = 0;
    volatile long g_cache_kb = 0;

    // An image nobody has looked at for this long gives up its GPU texture and
    // its decoder. The bytes on disk stay, so coming back to it is a local read
    // instead of a download.
    const unsigned long long UNLOAD_AFTER_MS = 20ULL * 60ULL * 1000ULL;

    // And the shorter one, for pictures whose account is no longer the one
    // signed in. Both clocks run at once and either can be the one that
    // expires: a picture the current account has not looked at in twenty
    // minutes goes, and so does one belonging to an account left five
    // minutes ago, however recently it was on screen before the switch.
    const unsigned long long FOREIGN_UNLOAD_AFTER_MS = 5ULL * 60ULL * 1000ULL;

    // The sweep walks every entry, so it runs on a timer rather than per frame.
    const unsigned long long COLLECT_EVERY_MS = 30ULL * 1000ULL;

    // How much a single UI frame may spend decoding GIFs, counted in pixels
    // because that is what the cost tracks. A budget in whole frames would
    // either stall the window on a few large animations or starve a member
    // list where every avatar is a 64 px animation: this covers roughly four
    // chat-sized GIFs, or two hundred avatars.
    const int MAX_ANIMATION_PIXELS = 1000000;

    // Appends to a wide string that is already in out, bounded by cap.
    void wappend(wchar_t* out, int cap, const wchar_t* tail)
    {
        int i = 0;
        while (out[i] && i < cap - 1) i++;
        int j = 0;
        while (tail[j] && i < cap - 1) out[i++] = tail[j++];
        out[i] = 0;
    }

    // One folder per account, named by the same machine bound hash the token
    // seal uses.
    //
    // Without this the sweep is shared: signing into a second account and
    // letting its cache age out takes the first account's pictures with it, and
    // for an account that has been removed those were the last copies in
    // existence. Keeping them apart means a sweep only ever touches the
    // pictures of whoever is signed in.
    bool cache_dir(wchar_t* out, int cap)
    {
        if (!ufile::app_path(L"cache", out, cap)) { out[0] = 0; return false; }
        CreateDirectoryW(out, 0);

        snowflake self = store::self_id();
        if (!self) return true;

        char tag[40];
        storage::account_tag(self, tag, sizeof(tag));

        wchar_t wtag[40];
        chartowcs(tag, wtag, 40);

        wappend(out, cap, L"\\");
        wappend(out, cap, wtag);
        CreateDirectoryW(out, 0);
        return true;
    }

    void cache_path(unsigned __int64 key, wchar_t* out, int cap)
    {
        if (!cache_dir(out, cap)) return;

        char name[32];
        cnprint(name, sizeof(name), "\\%016llx.img", key);

        wchar_t wname[32];
        chartowcs(name, wname, 32);
        wappend(out, cap, wname);
    }

    unsigned long long ft_ticks(const FILETIME& ft)
    {
        return ((unsigned long long)ft.dwHighDateTime << 32) | (unsigned long long)ft.dwLowDateTime;
    }

    // Windows file times count 100 ns intervals, so an hour is 3.6e10 of them.
    bool older_than(const FILETIME& written, int hours)
    {
        FILETIME now_ft;
        GetSystemTimeAsFileTime(&now_ft);

        unsigned long long now = ft_ticks(now_ft);
        unsigned long long then = ft_ticks(written);
        // A file stamped in the future means the clock moved; treat it as fresh
        // rather than deleting the whole cache.
        if (then >= now) return false;

        return (now - then) >= (unsigned long long)hours * 36000000000ULL;
    }

    bool cache_expired(const wchar_t* path, int hours)
    {
        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (!GetFileAttributesExW(path, GetFileExInfoStandard, &fad)) return false;
        return older_than(fad.ftLastWriteTime, hours);
    }

    // Deletes everything past its keep-for time and measures what is left. Runs
    // on the job pool at startup and whenever the setting changes, because a
    // large cache directory is slow to enumerate.
    void job_sweep_cache(void*)
    {
        int hours = (int)g_cache_hours;

        // Not while the client is running on what it saved. These files are the
        // only remaining copy of every avatar and every picture, and there is
        // nowhere to fetch them from again.
        if (offline::active())
        {
            log_line("tex: чистка кэша отложена, клиент работает по сохранённому");
            return;
        }

        wchar_t dir[MAX_PATH];
        if (!cache_dir(dir, MAX_PATH)) return;

        wchar_t pattern[MAX_PATH];
        pattern[0] = 0;
        wappend(pattern, MAX_PATH, dir);
        wappend(pattern, MAX_PATH, L"\\*.img");

        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(pattern, &fd);
        if (h == INVALID_HANDLE_VALUE) return;

        int removed = 0, kept = 0;
        unsigned long long freed = 0, live = 0;

        do
        {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

            unsigned long long size =
                ((unsigned long long)fd.nFileSizeHigh << 32) | (unsigned long long)fd.nFileSizeLow;

            // Zero hours means the on-disk cache is switched off, so everything
            // goes regardless of age.
            if (hours > 0 && !older_than(fd.ftLastWriteTime, hours))
            {
                kept++;
                live += size;
                continue;
            }

            wchar_t victim[MAX_PATH];
            victim[0] = 0;
            wappend(victim, MAX_PATH, dir);
            wappend(victim, MAX_PATH, L"\\");
            wappend(victim, MAX_PATH, fd.cFileName);

            if (DeleteFileW(victim)) { removed++; freed += size; }
            else { kept++; live += size; }
        }
        while (FindNextFileW(h, &fd));

        FindClose(h);

        InterlockedExchange(&g_cache_files, (long)kept);
        InterlockedExchange(&g_cache_kb, (long)(live / 1024));

        if (removed)
            log_line("tex: cache sweep dropped %d files (%llu KB), %d left (%llu KB)",
                     removed, freed / 1024, kept, live / 1024);
    }

    bool create_texture(const unsigned char* pixels, int w, int h, texture* out,
                        ID3D11Texture2D** keep_surface = 0)
    {
        if (!g_app.device) return false;

        D3D11_TEXTURE2D_DESC desc;
        ccfset(&desc, 0, sizeof(desc));
        desc.Width = (UINT)w;
        desc.Height = (UINT)h;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        // Animated images are rewritten every frame, so they need a dynamic
        // surface that the UI thread can map.
        desc.Usage = keep_surface ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
        desc.CPUAccessFlags = keep_surface ? D3D11_CPU_ACCESS_WRITE : 0;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA data;
        ccfset(&data, 0, sizeof(data));
        data.pSysMem = pixels;
        data.SysMemPitch = (UINT)(w * 4);

        ID3D11Texture2D* tex2d = 0;
        if (FAILED(g_app.device->CreateTexture2D(&desc, &data, &tex2d)) || !tex2d) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC srv;
        ccfset(&srv, 0, sizeof(srv));
        srv.Format = desc.Format;
        srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = 1;

        ID3D11ShaderResourceView* view = 0;
        HRESULT hr = g_app.device->CreateShaderResourceView(tex2d, &srv, &view);

        if (keep_surface && SUCCEEDED(hr)) *keep_surface = tex2d;
        else tex2d->Release();

        if (FAILED(hr) || !view) return false;

        out->srv = view;
        out->width = w;
        out->height = h;
        InterlockedAdd(&g_memory, (long)(w * h * 4));
        return true;
    }

    void anim_close(entry* e)
    {
        if (e->anim) { WebPAnimDecoderDelete(e->anim); e->anim = 0; }
        if (e->anim_data) { memfree(e->anim_data); e->anim_data = 0; }
        e->anim_prev_ts = 0;
    }

    // Sets up an animated WebP. The decoder reads straight out of the buffer it
    // is given and never copies it, so the bytes have to be ours and have to
    // outlive it.
    bool anim_open(entry* e, const unsigned char* data, unsigned int len,
                   int* w, int* h, int* frames)
    {
        e->anim_data = (unsigned char*)memalloc((int)len);
        if (!e->anim_data) return false;
        ccpy(e->anim_data, data, len);

        WebPData wd;
        wd.bytes = e->anim_data;
        wd.size = len;

        WebPAnimDecoderOptions opt;
        if (!WebPAnimDecoderOptionsInit(&opt)) { anim_close(e); return false; }
        opt.color_mode = MODE_RGBA;
        opt.use_threads = 0;

        e->anim = WebPAnimDecoderNew(&wd, &opt);
        if (!e->anim) { anim_close(e); return false; }

        WebPAnimInfo info;
        if (!WebPAnimDecoderGetInfo(e->anim, &info) ||
            info.canvas_width == 0 || info.canvas_height == 0 ||
            info.canvas_width > (unsigned int)MAX_ANIMATION_DIMENSION ||
            info.canvas_height > (unsigned int)MAX_ANIMATION_DIMENSION)
        {
            anim_close(e);
            return false;
        }

        *w = (int)info.canvas_width;
        *h = (int)info.canvas_height;
        *frames = (int)info.frame_count;
        e->anim_prev_ts = 0;
        return true;
    }

    const unsigned char* anim_next(entry* e, int* delay_ms)
    {
        if (!e->anim || !WebPAnimDecoderHasMoreFrames(e->anim)) return 0;

        unsigned char* buf = 0;
        int ts = 0;
        if (!WebPAnimDecoderGetNext(e->anim, &buf, &ts) || !buf) return 0;

        // Timestamps run from the start of the animation and mark the end of
        // each frame, so the gap between two of them is the frame's own time.
        int delay = ts - e->anim_prev_ts;
        e->anim_prev_ts = ts;
        if (delay < 20) delay = 100;
        if (delay_ms) *delay_ms = delay;
        return buf;
    }

    void anim_rewind(entry* e)
    {
        if (!e->anim) return;
        WebPAnimDecoderReset(e->anim);
        e->anim_prev_ts = 0;
    }

    // Stops an entry animating and hands back whatever the decoder held. The
    // texture keeps whichever frame was uploaded last.
    void drop_decoder(entry* e)
    {
        if (e->gif) { imd_gif_close(e->gif); e->gif = 0; }
        if (e->anim) anim_close(e);

        InterlockedAdd(&g_memory, -e->decoder_bytes);
        e->decoder_bytes = 0;
        e->tex.animated = false;
        e->frames_shown = 0;
    }

    // Gives up everything expensive an entry holds and leaves it empty, ready
    // to be loaded again. The entry itself stays in the table: callers hold the
    // texture pointer it returns, and freeing it would leave that dangling.
    void release_payload(entry* e)
    {
        drop_decoder(e);

        if (e->surface)
        {
            e->surface->Release();
            e->surface = 0;
        }
        if (e->tex.srv)
        {
            e->tex.srv->Release();
            e->tex.srv = 0;
            InterlockedAdd(&g_memory, -(long)(e->tex.width * e->tex.height * 4));
        }

        e->tex.animated = false;
        e->frames_shown = 0;
        e->next_frame_ms = 0;
        InterlockedExchange(&e->tex.state, TEX_EMPTY);
    }

    void job_load(void* user)
    {
        entry* e = (entry*)user;

        ubuffer blob;
        blob.init();

        wchar_t path[MAX_PATH];
        cache_path(e->key, path, MAX_PATH);

        // Zero hours turns the on-disk cache off; anything past its keep-for
        // time is dropped here rather than waiting for the next sweep.
        int hours = (int)g_cache_hours;
        bool cache_on = hours > 0 && path[0];
        if (path[0] && (!cache_on || cache_expired(path, hours))) DeleteFileW(path);

        bool from_disk = cache_on && ufile::read_all(path, &blob) && blob.size > 16;
        int status = 0;
        bool truncated = false;

        if (!from_disk)
        {
            http_response res;
            res.init();
            // Every format listed here has a decoder now, webp included.
            const char* accept = "Accept: image/png,image/jpeg,image/webp,image/gif,*/*;q=0.1\r\n";
            if (http::request("GET", e->url, accept, 0, 0, &res))
            {
                status = res.status;
                truncated = res.truncated;
                // A cut-off transfer still yields a plausible-looking buffer;
                // caching it would make the image fail forever.
                if (res.ok() && !res.truncated && res.body.size > 16)
                    blob.append(res.body.data, res.body.size);
            }
            res.free_response();
        }

        // An animated GIF is set up as a streaming decoder plus a dynamic
        // texture; only its first frame is decoded here. Whatever cannot be set
        // up that way drops through to the one-shot decoder below, which still
        // pulls a first frame out of a GIF.
        if (blob.size > 16 && imd_gif_is_gif(blob.data, (int)blob.size))
        {
            int gw = 0, gh = 0, frames = 0;
            imd_gif* gif = imd_gif_open(blob.data, (int)blob.size, MAX_ANIMATION_DIMENSION, &gw, &gh, &frames);

            int delay = 100;
            const unsigned char* first = gif ? imd_gif_next(gif, &delay) : 0;

            // Avatars and emoji are routinely single-frame GIFs. Those want a
            // plain immutable texture, not a dynamic surface stepped forever.
            bool stream = first && frames != 1;
            bool ok = first && create_texture(first, gw, gh, &e->tex, stream ? &e->surface : 0);

            if (ok && stream)
            {
                e->gif = gif;
                e->tex.animated = true;
                e->frames_shown = 1;
                e->next_frame_ms = GetTickCount64() + (unsigned long long)delay;
                e->decoder_bytes = (long)imd_gif_bytes(gif);
                InterlockedAdd(&g_memory, e->decoder_bytes);
            }
            else if (gif)
            {
                // create_texture has already copied the pixels to the GPU.
                imd_gif_close(gif);
            }

            if (ok)
            {
                if (!from_disk && cache_on) ufile::write_all(path, blob.data, blob.size);

                blob.free_buffer();
                InterlockedExchange(&e->tex.state, TEX_READY);
                InterlockedDecrement(&g_pending);
                return;
            }

            log_line("tex: gif stream unavailable (%u bytes), falling back %s", blob.size, e->url);
        }

        // The same treatment for animated WebP, which is what discord actually
        // serves for animated avatars and banners: it stopped offering a gif
        // for those and answers 415 if one is asked for.
        WebPBitstreamFeatures features;
        if (blob.size > 16 &&
            WebPGetFeatures(blob.data, blob.size, &features) == VP8_STATUS_OK &&
            features.has_animation)
        {
            int aw = 0, ah = 0, frames = 0;
            bool opened = anim_open(e, blob.data, blob.size, &aw, &ah, &frames);

            int delay = 100;
            const unsigned char* first = opened ? anim_next(e, &delay) : 0;

            bool stream = first && frames != 1;
            bool ok = first && create_texture(first, aw, ah, &e->tex, stream ? &e->surface : 0);

            if (ok && stream)
            {
                e->tex.animated = true;
                e->frames_shown = 1;
                e->next_frame_ms = GetTickCount64() + (unsigned long long)delay;
                // Two canvases inside the decoder, plus our copy of the file.
                e->decoder_bytes = (long)blob.size + 2 * aw * ah * 4;
                InterlockedAdd(&g_memory, e->decoder_bytes);
            }
            else
            {
                // create_texture has already copied the pixels to the GPU, and
                // a single-frame animation has nothing left to play.
                anim_close(e);
            }

            if (ok)
            {
                if (!from_disk && cache_on) ufile::write_all(path, blob.data, blob.size);

                blob.free_buffer();
                InterlockedExchange(&e->tex.state, TEX_READY);
                InterlockedDecrement(&g_pending);
                return;
            }

            log_line("tex: animated webp unavailable (%u bytes), falling back %s", blob.size, e->url);
        }

        int w = 0, h = 0, comp = 0;
        unsigned char* pixels = 0;
        bool webp = false;

        if (blob.size > 16)
        {
            pixels = stbi_load_from_memory(blob.data, (int)blob.size, &w, &h, &comp, 4);

            // stb has no WebP support, and discord's CDN hands it out for
            // plenty of attachments regardless of the file name.
            if (!pixels && WebPGetInfo(blob.data, blob.size, &w, &h))
            {
                pixels = WebPDecodeRGBA(blob.data, blob.size, &w, &h);
                webp = pixels != 0;
            }
        }

        bool ok = false;
        if (!pixels)
        {
            if (blob.size <= 16)
                log_line("tex: no data (http %d%s) %s", status, truncated ? ", truncated" : "", e->url);
            else
                log_line("tex: decode failed (%s, %u bytes) %s",
                         stbi_failure_reason() ? stbi_failure_reason() : "unknown", blob.size, e->url);

            // A cached file that no longer decodes is poison; drop it so the
            // next attempt refetches.
            if (from_disk && path[0]) DeleteFileW(path);
        }
        else if (w <= 0 || h <= 0 || w > MAX_DIMENSION || h > MAX_DIMENSION)
        {
            log_line("tex: %dx%d exceeds the %d px limit %s", w, h, MAX_DIMENSION, e->url);
        }
        else
        {
            ok = create_texture(pixels, w, h, &e->tex);
            if (!ok) log_line("tex: gpu upload failed for %dx%d %s", w, h, e->url);
            // Only cache bytes that actually decoded.
            else if (!from_disk && cache_on) ufile::write_all(path, blob.data, blob.size);
        }

        if (pixels)
        {
            if (webp) WebPFree(pixels);
            else stbi_image_free(pixels);
        }
        blob.free_buffer();

        InterlockedExchange(&e->tex.state, ok ? TEX_READY : TEX_FAILED);
        InterlockedDecrement(&g_pending);
    }
}

void tex::init()
{
    if (g_ready) return;
    InitializeCriticalSection(&g_lock);
    g_entries = ulist<entry*>();
    g_ready = true;
    g_last_collect_ms = GetTickCount64();

    // Settings are read here, on the UI thread, so the download jobs never
    // touch the settings table themselves.
    InterlockedExchange(&g_cache_hours, (long)storage::settings_get_int("cache_hours", 24));
    jobs::post(job_sweep_cache, 0);
}

void tex::shutdown()
{
    if (!g_ready) return;

    EnterCriticalSection(&g_lock);
    for (unsigned int i = 0; i < g_entries.count; i++)
    {
        release_payload(g_entries[i]);
        memfree(g_entries[i]);
    }
    g_entries.dispose();
    LeaveCriticalSection(&g_lock);

    DeleteCriticalSection(&g_lock);
    g_ready = false;
}

const texture* tex::get(const char* url)
{
    static texture empty = { 0, 0, 0, TEX_EMPTY };
    if (!g_ready || !url || !url[0]) return &empty;

    unsigned __int64 key = ccscrc64(url);

    EnterCriticalSection(&g_lock);
    for (unsigned int i = 0; i < g_entries.count; i++)
    {
        if (g_entries[i]->key == key)
        {
            entry* e = g_entries[i];

            // Remembering the frame lets advance_animations skip images that
            // scrolled out of view; the wall clock decides when collect drops
            // one altogether.
            e->used_frame = g_frame_counter;
            e->used_ms = GetTickCount64();

            // Whoever is looking at it now owns it. Two accounts sharing an
            // avatar is the ordinary case, and the one on screen is the one
            // whose clock should apply.
            e->owner = store::self_id();

            // Unloaded a while ago and wanted again: reload it. The bytes are
            // normally still on disk, so this does not go out to the network.
            if (e->tex.state == TEX_EMPTY)
            {
                InterlockedExchange(&e->tex.state, TEX_LOADING);
                InterlockedIncrement(&g_pending);
                jobs::post(job_load, e);
            }

            texture* t = &e->tex;
            LeaveCriticalSection(&g_lock);
            return t;
        }
    }

    entry* e = (entry*)memalloc(sizeof(entry));
    if (!e)
    {
        LeaveCriticalSection(&g_lock);
        return &empty;
    }
    ccfset(e, 0, sizeof(entry));
    e->key = key;
    e->tex.state = TEX_LOADING;
    e->used_frame = g_frame_counter;
    e->used_ms = GetTickCount64();
    e->owner = store::self_id();
    ccstrncpy(e->url, url, sizeof(e->url) - 1);
    g_entries.push(e);
    LeaveCriticalSection(&g_lock);

    InterlockedIncrement(&g_pending);
    jobs::post(job_load, e);
    return &e->tex;
}

void tex::advance_animations()
{
    if (!g_ready || !g_app.context) return;

    g_frame_counter++;
    unsigned long long now = GetTickCount64();
    int budget = MAX_ANIMATION_PIXELS;

    EnterCriticalSection(&g_lock);

    // Walking from a rotating cursor rather than from zero, so that a page full
    // of animations shares the budget instead of letting the first few starve
    // everything below them.
    for (unsigned int n = 0; n < g_entries.count && budget > 0; n++)
    {
        unsigned int i = (g_animation_cursor + n) % g_entries.count;
        entry* e = g_entries[i];
        if (!e->tex.animated || e->tex.state != TEX_READY) continue;
        if (!e->gif && !e->anim) continue;

        // Only animate what is actually on screen: everything else stays on
        // whatever frame it stopped at and costs nothing.
        if (g_frame_counter - e->used_frame > 2) continue;
        if (now < e->next_frame_ms) continue;

        int delay = 100;
        const unsigned char* frame = e->gif ? imd_gif_next(e->gif, &delay) : anim_next(e, &delay);
        if (!frame)
        {
            // The end of the animation, so loop. A file that turns out to hold
            // a single frame, or one that stopped decoding, is not worth
            // rewinding every hundred milliseconds forever: drop the decoder
            // and leave the last good frame on screen.
            if (e->frames_shown <= 1)
            {
                drop_decoder(e);
                continue;
            }

            if (e->gif) { imd_gif_rewind(e->gif); frame = imd_gif_next(e->gif, &delay); }
            else        { anim_rewind(e);         frame = anim_next(e, &delay); }

            e->frames_shown = 0;
            if (!frame) continue;
        }
        e->frames_shown++;

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(g_app.context->Map(e->surface, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            const unsigned int row = (unsigned int)e->tex.width * 4;
            for (int y = 0; y < e->tex.height; y++)
                ccpy((unsigned char*)mapped.pData + (unsigned int)y * mapped.RowPitch,
                     frame + (unsigned int)y * row, row);
            g_app.context->Unmap(e->surface, 0);
        }

        e->next_frame_ms = now + (unsigned long long)delay;
        budget -= e->tex.width * e->tex.height;
        g_animation_cursor = i + 1;
    }

    LeaveCriticalSection(&g_lock);
}

void tex::collect()
{
    if (!g_ready) return;

    unsigned long long now = GetTickCount64();
    if (now - g_last_collect_ms < COLLECT_EVERY_MS) return;
    g_last_collect_ms = now;

    int dropped = 0;
    int dropped_foreign = 0;
    long before = g_memory;

    // Zero while an account switch is in flight: the store has been thrown
    // away and the next one has not arrived. Everything would look foreign
    // for that moment, so the account rule sits out until somebody is
    // actually signed in.
    snowflake me = store::self_id();

    EnterCriticalSection(&g_lock);

    for (unsigned int i = 0; i < g_entries.count; i++)
    {
        entry* e = g_entries[i];

        // Only a finished entry can be let go. One still loading is being
        // written by a worker thread, and one that failed holds nothing.
        if (e->tex.state != TEX_READY) continue;

        // Started when the account behind this picture stopped being the
        // one signed in, cleared the moment it is again - which is the whole
        // of "reset and pause when they come back".
        if (me && e->owner && e->owner != me)
        {
            if (!e->foreign_since) e->foreign_since = now;
        }
        else
        {
            e->foreign_since = 0;
        }

        bool unseen = now - e->used_ms >= UNLOAD_AFTER_MS;
        bool orphaned = e->foreign_since &&
                        now - e->foreign_since >= FOREIGN_UNLOAD_AFTER_MS;

        if (!unseen && !orphaned) continue;

        release_payload(e);
        if (orphaned) dropped_foreign++;
        else          dropped++;
    }

    LeaveCriticalSection(&g_lock);

    if (dropped || dropped_foreign)
        log_line("tex: unloaded %d images not seen for %llu min and %d left by "
                 "another account %llu min ago, freeing %ld KB",
                 dropped, UNLOAD_AFTER_MS / 60000,
                 dropped_foreign, FOREIGN_UNLOAD_AFTER_MS / 60000,
                 (before - g_memory) / 1024);
}

bool tex::fetch_blob(const char* url, ubuffer* out)
{
    http_response res;
    res.init();
    bool ok = http::get(url, &res) && res.ok();
    if (ok) out->append(res.body.data, res.body.size);
    res.free_response();
    return ok;
}

unsigned int tex::memory_used() { return (unsigned int)g_memory; }
int tex::pending_downloads() { return (int)g_pending; }

int tex::cache_ttl_hours() { return (int)g_cache_hours; }
unsigned int tex::cache_disk_kb() { return (unsigned int)g_cache_kb; }
int tex::cache_file_count() { return (int)g_cache_files; }

void tex::set_cache_ttl_hours(int hours)
{
    if (hours < 0) hours = 0;
    if (hours > 24 * 30) hours = 24 * 30;
    if (hours == (int)g_cache_hours) return;

    InterlockedExchange(&g_cache_hours, (long)hours);
    storage::settings_set_int("cache_hours", hours);
    storage::settings_save();

    // Applying a shorter limit should take effect now, not at the next start.
    jobs::post(job_sweep_cache, 0);
}
