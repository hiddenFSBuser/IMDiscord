#include "pch.h"
#include <d3d11.h>
#include "imgui.h"

#include "streampreview.h"
#include "core/app.h"
#include "core/log.h"
#include "core/crypto.h"
#include "discord/rest.h"
#include "discord/store.h"
#include "ui/theme.h"
#include "net/json.h"
#include "net/http.h"
#include "stb_image.h"

// Server-side stills of somebody else's Go Live stream, for the LIVE button.
//
// The official client uploads a frame every few minutes to
// POST /api/v9/streams/{key}/preview and shows that still when a viewer
// hovers the stream. The same still is read back here with a plain authed
// GET on the same path: the key is built locally from the guild, the channel
// and the person, exactly the way the watch path builds it, so nothing has
// to arrive over the gateway first.
//
// What comes back is either the picture itself (content-type image/*) or a
// small json object carrying it - {"thumbnail":"data:image/jpeg;base64,..."}
// mirroring the upload, or a url to fetch it from. All three shapes are
// accepted, because the exact one is undocumented and was read off a capture
// rather than a specification.

namespace
{
    enum pv_state
    {
        PV_EMPTY = 0,
        PV_LOADING,
        PV_READY,
        PV_FAILED,
    };

    struct pv_entry
    {
        char key[192];
        volatile long state;
        ID3D11ShaderResourceView* srv;
        int w, h;
        unsigned long long last_try_ms;
        unsigned long long last_ok_ms;
        int fails;
    };

    // A handful, by design: only the streams under the cursor need one, and
    // the oldest entry is reused when they run out.
    const int PV_MAX = 8;

    // A still worth showing again without asking twice, and a failure worth
    // retrying rather than hammering: the official client uploads every few
    // minutes, so asking oftener buys nothing but traffic.
    const unsigned long long PV_FRESH_MS = 120ULL * 1000ULL;
    const unsigned long long PV_RETRY_MS = 15ULL * 1000ULL;
    const unsigned long long PV_RETRY_LATE_MS = 300ULL * 1000ULL;

    // Previews leave the streamer at a few hundred pixels across; anything
    // far above that is not a preview and is not worth the video memory.
    const int PV_MAX_DIMENSION = 1600;

    pv_entry g_entries[PV_MAX];
    CRITICAL_SECTION g_lock;
    bool g_ready = false;
    snowflake g_owner = 0;

    void build_key(snowflake guild_id, snowflake channel_id, snowflake user_id,
                   char* out, int cap)
    {
        if (guild_id)
            cnprint(out, cap, "guild:%llu:%llu:%llu", guild_id, channel_id, user_id);
        else
            cnprint(out, cap, "call:%llu:%llu", channel_id, user_id);
    }

    // The key travels in the path, so its colons go URL-encoded - which is
    // also the shape the official client is captured using.
    void build_path(const char* key, char* out, int cap)
    {
        int at = 0;
        at += cnprint(out + at, cap - at, "/streams/");

        for (const char* p = key; *p && at < cap - 1; p++)
        {
            if (*p == ':')
            {
                if (at + 3 >= cap) break;
                out[at++] = '%';
                out[at++] = '3';
                out[at++] = 'A';
            }
            else
            {
                out[at++] = *p;
            }
        }

        cnprint(out + at, cap - at, "/preview");
    }

    bool starts_with(const char* text, const char* prefix)
    {
        if (!text || !prefix) return false;
        while (*prefix)
        {
            if (*text != *prefix) return false;
            text++;
            prefix++;
        }
        return true;
    }

    bool contains(const char* haystack, const char* needle)
    {
        if (!haystack || !needle || !needle[0]) return false;
        for (const char* p = haystack; *p; p++)
        {
            const char* a = p;
            const char* b = needle;
            while (*a && *b && *a == *b) { a++; b++; }
            if (!*b) return true;
        }
        return false;
    }

    // Bytes of one picture out of a GET answer, whatever shape it took.
    bool extract_image(http_response* res, ubuffer* img)
    {
        if (res->content_type[0] && contains(res->content_type, "image/"))
        {
            if (res->body.size > 16)
            {
                img->append(res->body.data, res->body.size);
                return true;
            }
            return false;
        }

        jdoc doc;
        doc.init();
        if (!doc.parse(res->text(), (int)res->body.size))
        {
            doc.free_doc();
            return false;
        }

        const char* fields[4] = { "thumbnail", "preview_url", "thumbnail_url", "url" };
        const char* found = 0;
        for (int i = 0; i < 4 && !found; i++)
            found = doc.root->str(fields[i], 0);

        bool ok = false;
        if (found && found[0])
        {
            if (starts_with(found, "data:"))
            {
                // data:image/jpeg;base64,.... - the payload starts past the
                // first comma, which is also where the upload puts it.
                const char* comma = 0;
                for (const char* p = found; *p; p++)
                {
                    if (*p == ',') { comma = p + 1; break; }
                }

                if (comma && comma[0])
                    ok = crypto::base64_decode(comma, (int)ccslenf(comma), img);
            }
            else if (starts_with(found, "http"))
            {
                // A pointer rather than the picture: public CDN, no auth.
                http_response sub;
                sub.init();
                const char* accept = "Accept: image/png,image/jpeg,image/webp,*/*;q=0.1\r\n";
                if (http::request("GET", found, accept, 0, 0, &sub) &&
                    sub.ok() && !sub.truncated && sub.body.size > 16)
                    img->append(sub.body.data, sub.body.size);
                ok = img->size > 16;
                sub.free_response();
            }
        }

        doc.free_doc();
        return ok && img->size > 16;
    }

    bool upload_texture(const unsigned char* rgba, int w, int h,
                        ID3D11ShaderResourceView** out)
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
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA data;
        ccfset(&data, 0, sizeof(data));
        data.pSysMem = rgba;
        data.SysMemPitch = (UINT)(w * 4);

        ID3D11Texture2D* tex2d = 0;
        if (FAILED(g_app.device->CreateTexture2D(&desc, &data, &tex2d)) || !tex2d)
            return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC srv;
        ccfset(&srv, 0, sizeof(srv));
        srv.Format = desc.Format;
        srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = 1;

        ID3D11ShaderResourceView* view = 0;
        HRESULT hr = g_app.device->CreateShaderResourceView(tex2d, &srv, &view);
        tex2d->Release();

        if (FAILED(hr) || !view) return false;

        *out = view;
        return true;
    }

    struct fetch_job
    {
        char key[192];
    };

    void job_fetch(void* user)
    {
        fetch_job* j = (fetch_job*)user;

        http_response res;
        res.init();

        char path[1024];
        build_path(j->key, path, sizeof(path));

        bool asked = api::call("GET", path, 0, &res);

        ID3D11ShaderResourceView* view = 0;
        int w = 0, h = 0;

        if (asked && res.ok() && !res.truncated)
        {
            ubuffer img;
            img.init();

            if (extract_image(&res, &img))
            {
                int comp = 0;
                unsigned char* rgba = stbi_load_from_memory(
                    img.data, (int)img.size, &w, &h, &comp, 4);

                if (!rgba)
                {
                    log_line("streampreview: decode failed (%s, %u bytes)",
                             stbi_failure_reason() ? stbi_failure_reason() : "unknown",
                             img.size);
                }
                else if (w <= 0 || h <= 0 || w > PV_MAX_DIMENSION || h > PV_MAX_DIMENSION)
                {
                    log_line("streampreview: %dx%d rejected", w, h);
                    stbi_image_free(rgba);
                    rgba = 0;
                }
                else if (g_ready && !upload_texture(rgba, w, h, &view))
                {
                    log_line("streampreview: gpu upload failed for %dx%d", w, h);
                }

                if (rgba) stbi_image_free(rgba);
                if (!view) { w = 0; h = 0; }
            }

            img.free_buffer();
        }
        else if (asked)
        {
            // 404 is the ordinary answer for a streamer with nothing
            // uploaded - an old client, this client, or previews switched
            // off - so it is not worth a log line every time.
            if (res.status != 404)
                log_line("streampreview: GET %s -> http %d", j->key, res.status);
        }

        res.free_response();

        // Published under the lock, and only into the entry that is still
        // waiting for this key: while the fetch was in flight the slot may
        // have been reused for another stream, or the client may be on its
        // way out.
        EnterCriticalSection(&g_lock);
        if (g_ready)
        {
            for (int i = 0; i < PV_MAX; i++)
            {
                pv_entry* e = &g_entries[i];
                if (e->state != PV_LOADING) continue;
                if (ccscmp(e->key, j->key) != 0) continue;

                if (view)
                {
                    if (e->srv) e->srv->Release();
                    e->srv = view;
                    e->w = w;
                    e->h = h;
                    view = 0;
                    e->fails = 0;
                    e->last_ok_ms = GetTickCount64();
                    InterlockedExchange(&e->state, PV_READY);
                }
                else
                {
                    e->fails++;
                    e->last_try_ms = GetTickCount64();
                    InterlockedExchange(&e->state, PV_FAILED);
                }
                break;
            }
        }
        LeaveCriticalSection(&g_lock);

        if (view) view->Release();
        memfree(j);
    }

    pv_entry* find_entry(const char* key)
    {
        for (int i = 0; i < PV_MAX; i++)
        {
            if (g_entries[i].state != PV_EMPTY && ccscmp(g_entries[i].key, key) == 0)
                return &g_entries[i];
        }
        return 0;
    }

    void drop_entry(pv_entry* e)
    {
        if (e->srv) { e->srv->Release(); e->srv = 0; }
        e->w = 0;
        e->h = 0;
        e->fails = 0;
        e->last_try_ms = 0;
        e->last_ok_ms = 0;
        e->key[0] = 0;
        InterlockedExchange(&e->state, PV_EMPTY);
    }

    // Largest rectangle of the picture's shape that fits the space given.
    ImVec2 fit_image(float pic_w, float pic_h, ImVec2 avail)
    {
        if (pic_w <= 0.0f || pic_h <= 0.0f || avail.x <= 0.0f || avail.y <= 0.0f)
            return ImVec2(0, 0);

        float scale = avail.x / pic_w;
        float other = avail.y / pic_h;
        if (other < scale) scale = other;

        return ImVec2(pic_w * scale, pic_h * scale);
    }
}

void streampreview::init()
{
    InitializeCriticalSection(&g_lock);
    ccfset(g_entries, 0, sizeof(g_entries));
    g_owner = 0;
    g_ready = true;
}

void streampreview::shutdown()
{
    EnterCriticalSection(&g_lock);
    g_ready = false;
    for (int i = 0; i < PV_MAX; i++) drop_entry(&g_entries[i]);
    LeaveCriticalSection(&g_lock);
    DeleteCriticalSection(&g_lock);
}

void streampreview::request(snowflake guild_id, snowflake channel_id, snowflake user_id)
{
    if (!g_ready || !api::has_token() || api::is_bot()) return;
    if (!channel_id || !user_id) return;

    char key[192];
    build_key(guild_id, channel_id, user_id, key, sizeof(key));

    EnterCriticalSection(&g_lock);

    // Stills belong to whoever was signed in when they were fetched. A
    // switch of account drops them all rather than showing one account's
    // streams to another.
    snowflake self = store::self_id();
    if (self != g_owner)
    {
        for (int i = 0; i < PV_MAX; i++) drop_entry(&g_entries[i]);
        g_owner = self;
    }

    unsigned long long now = GetTickCount64();
    pv_entry* e = find_entry(key);

    bool fetch = false;
    if (!e)
    {
        // Reuse the stalest finished slot; a fetch in flight keeps its own.
        pv_entry* slot = 0;
        for (int i = 0; i < PV_MAX; i++)
        {
            if (g_entries[i].state == PV_EMPTY) { slot = &g_entries[i]; break; }
        }
        if (!slot)
        {
            for (int i = 0; i < PV_MAX; i++)
            {
                if (g_entries[i].state == PV_LOADING) continue;
                if (!slot || g_entries[i].last_ok_ms < slot->last_ok_ms)
                    slot = &g_entries[i];
            }
        }
        if (!slot)
        {
            LeaveCriticalSection(&g_lock);
            return;
        }

        drop_entry(slot);
        ccstrncpy(slot->key, key, sizeof(slot->key) - 1);
        slot->last_try_ms = now;
        InterlockedExchange(&slot->state, PV_LOADING);
        fetch = true;
    }
    else if (e->state == PV_READY)
    {
        fetch = (now - e->last_ok_ms) >= PV_FRESH_MS;
        if (fetch)
        {
            e->last_try_ms = now;
            InterlockedExchange(&e->state, PV_LOADING);
        }
    }
    else if (e->state == PV_FAILED)
    {
        unsigned long long wait = e->fails >= 3 ? PV_RETRY_LATE_MS : PV_RETRY_MS;
        fetch = (now - e->last_try_ms) >= wait;
        if (fetch)
        {
            e->last_try_ms = now;
            InterlockedExchange(&e->state, PV_LOADING);
        }
    }

    LeaveCriticalSection(&g_lock);

    if (!fetch) return;

    fetch_job* j = (fetch_job*)memalloc(sizeof(fetch_job));
    if (!j)
    {
        EnterCriticalSection(&g_lock);
        pv_entry* lost = find_entry(key);
        if (lost && lost->state == PV_LOADING)
        {
            lost->last_try_ms = GetTickCount64();
            InterlockedExchange(&lost->state, PV_FAILED);
        }
        LeaveCriticalSection(&g_lock);
        return;
    }

    ccstrncpy(j->key, key, sizeof(j->key) - 1);
    jobs::post(job_fetch, j);
}

void streampreview::tooltip(snowflake guild_id, snowflake channel_id, snowflake user_id,
                            bool watching, const char* display_name)
{
    const char* hint = watching ? tr("Закрыть демонстрацию")
                                : tr("Смотреть демонстрацию");

    // Bots never get a preview: the endpoint answers them with a refusal,
    // so the tooltip stays text rather than loading forever.
    if (!api::has_token() || api::is_bot())
    {
        ImGui::SetTooltip("%s", hint);
        return;
    }

    request(guild_id, channel_id, user_id);

    char key[192];
    build_key(guild_id, channel_id, user_id, key, sizeof(key));

    ID3D11ShaderResourceView* srv = 0;
    int w = 0, h = 0;
    long state = PV_EMPTY;

    EnterCriticalSection(&g_lock);
    pv_entry* e = find_entry(key);
    if (e)
    {
        state = e->state;
        if (e->state == PV_READY && e->srv)
        {
            // Held across the draw below: the worker replaces the texture
            // under this same lock, and a released picture must not be drawn.
            e->srv->AddRef();
            srv = e->srv;
            w = e->w;
            h = e->h;
        }
    }
    LeaveCriticalSection(&g_lock);

    if (!ImGui::BeginTooltip())
    {
        if (srv) srv->Release();
        return;
    }

    if (display_name && display_name[0])
    {
        ImGui::PushFont(g_app.font_bold);
        ImGui::TextUnformatted(display_name);
        ImGui::PopFont();
    }

    if (srv && w > 0 && h > 0)
    {
        ImVec2 size = fit_image((float)w, (float)h, ImVec2(320, 180));
        ImGui::Image((ImTextureID)srv, size);
    }
    else if (state == PV_LOADING || state == PV_EMPTY)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, col::text_muted);
        ImGui::TextUnformatted(tr("Загружаем превью..."));
        ImGui::PopStyleColor();
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_Text, col::text_muted);
        ImGui::TextUnformatted(tr("Превью недоступно"));
        ImGui::PopStyleColor();
    }

    ImGui::TextUnformatted(hint);
    ImGui::EndTooltip();

    if (srv) srv->Release();
}
