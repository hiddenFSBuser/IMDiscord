#include "pch.h"
#include <d3d11.h>
#include "ui_state.h"
#include "video/player.h"
#include "theme.h"
#include "textures.h"

#include "core/app.h"
#include "core/log.h"
#include "discord/store.h"
#include "core/offline.h"
#include "discord/archive.h"
#include "discord/rest.h"
#include "discord/gateway.h"
#include "discord/voice.h"
#include "video/screenshare.h"
#include "net/http.h"
#include "system/io/ufile.h"

namespace
{
    const unsigned int MAX_RENDERED_MESSAGES = 300;
    const float MAX_IMAGE_WIDTH = 420.0f;
    const float MAX_IMAGE_HEIGHT = 340.0f;

    struct download_job
    {
        char url[600];
        wchar_t path[MAX_PATH];
    };

    void job_download(void* user)
    {
        download_job* j = (download_job*)user;

        http_response res;
        res.init();
        if (http::get(j->url, &res) && res.ok() && res.body.size)
        {
            if (ufile::write_all(j->path, res.body.data, res.body.size))
            {
                char name[MAX_PATH];
                wcstochar(j->path, name, sizeof(name));
                char msg[512];
                cnprint(msg, sizeof(msg), "Сохранено: %s", name);
                api::set_last_error(msg);
            }
            else
            {
                api::set_last_error("Не удалось записать файл на диск");
            }
        }
        else
        {
            api::set_last_error("Скачивание не удалось");
        }

        res.free_response();
        memfree(j);
    }

    void start_download(const char* url, const char* filename)
    {
        wchar_t suggested[MAX_PATH];
        chartowcs(filename, suggested, MAX_PATH);

        wchar_t chosen[MAX_PATH];
        if (!ufile::save_dialog(suggested, chosen, MAX_PATH)) return;

        download_job* j = (download_job*)memalloc(sizeof(download_job));
        if (!j) return;
        ccfset(j, 0, sizeof(download_job));
        ccstrncpy(j->url, url, sizeof(j->url) - 1);

        int i = 0;
        while (chosen[i] && i < MAX_PATH - 1) { j->path[i] = chosen[i]; i++; }
        j->path[i] = 0;

        jobs::post(job_download, j);
    }

    void human_size(unsigned int bytes, char* out, int cap)
    {
        if (bytes < 1024) cnprint(out, cap, "%u Б", bytes);
        else if (bytes < 1024 * 1024) cnprint(out, cap, "%u КБ", bytes / 1024);
        else cnprint(out, cap, "%u.%u МБ", bytes / (1024 * 1024), (bytes % (1024 * 1024)) * 10 / (1024 * 1024));
    }

    // Matches one extension against a url, starting at its dot. The extension
    // has to be the whole of it, so that a ".webpage" path never passes for a
    // picture.
    bool ext_is(const char* dot, const char* ext)
    {
        int i = 0;
        for (; ext[i]; i++)
        {
            char c = dot[i];
            if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            if (c != ext[i]) return false;
        }
        return dot[i] == 0 || dot[i] == '?';
    }

    // Whether the file behind a url can hold more than one frame.
    bool may_animate(const char* url)
    {
        const char* dot = 0;
        for (const char* p = url; *p && *p != '?'; p++)
            if (*p == '.') dot = p;

        if (!dot) return false;
        return ext_is(dot, ".gif") || ext_is(dot, ".webp");
    }

    // media.discordapp.net transcodes on the fly and answers with WebP by
    // default. Asking it for png explicitly costs them a transcode but comes
    // back lossless, and as a bonus it re-encodes progressive JPEGs, which the
    // stb decoder rejects. With the webp preference the proxy url is left as it
    // is and libwebp handles the result.
    void build_image_url(const char* proxy, const char* original, char* out, int cap)
    {
        const char* src = (proxy && proxy[0]) ? proxy : original;
        if (!src || !src[0]) { out[0] = 0; return; }

        // Anything that might animate has to come through untouched: asking the
        // proxy for a still format flattens it to its first frame. That means
        // GIFs, and it means WebP as well, which is what discord hands back for
        // links to the gif hosting sites. Either address is enough to tell,
        // since the proxy keeps the original name in its path.
        bool has_original = original && original[0];
        bool animated_src = may_animate(src) || (has_original && may_animate(original));

        if (animated_src && has_original) src = original;

        int len = (int)ccslenf(src);
        if (len >= cap - 16 || animated_src || !(proxy && proxy[0]) ||
            ui_image_format() == IMAGE_ALWAYS_WEBP)
        {
            ccstrncpy(out, src, cap - 1);
            out[cap - 1] = 0;
            return;
        }

        ccpy(out, src, (size_t)len);
        out[len] = 0;

        bool has_query = false;
        for (const char* p = src; *p; p++) if (*p == '?') { has_query = true; break; }

        const char* suffix;
        if (!has_query) suffix = "?format=png";
        else if (src[len - 1] == '&' || src[len - 1] == '?') suffix = "format=png";
        else suffix = "&format=png";

        ccstrncpy(out + len, suffix, cap - len - 1);
        out[cap - 1] = 0;
    }

    // Draws an image, scaled to fit, and returns true when it was clicked.
    bool draw_image(const char* url, int native_w, int native_h)
    {
        const texture* t = tex::get(url);
        if (!t->ready())
        {
            float w = native_w > 0 ? (float)native_w : 240.0f;
            float h = native_h > 0 ? (float)native_h : 160.0f;
            float scale = 1.0f;
            if (w > MAX_IMAGE_WIDTH) scale = MAX_IMAGE_WIDTH / w;
            if (h * scale > MAX_IMAGE_HEIGHT) scale = MAX_IMAGE_HEIGHT / h;

            ImVec2 p = ImGui::GetCursorScreenPos();
            ImVec2 size(w * scale, h * scale);
            ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), col::bg_hover, 6.0f);

            const char* label = (t->state == TEX_FAILED) ? "не удалось загрузить" : "загрузка...";
            ImVec2 ts = ImGui::CalcTextSize(label);
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(p.x + (size.x - ts.x) * 0.5f, p.y + (size.y - ts.y) * 0.5f), col::text_muted, label);

            ImGui::Dummy(size);
            return false;
        }

        float w = (float)t->width;
        float h = (float)t->height;
        float scale = 1.0f;
        if (w > MAX_IMAGE_WIDTH) scale = MAX_IMAGE_WIDTH / w;
        if (h * scale > MAX_IMAGE_HEIGHT) scale = MAX_IMAGE_HEIGHT / h;

        ImVec2 size(w * scale, h * scale);
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddImageRounded(t->id(), p, ImVec2(p.x + size.x, p.y + size.y),
                                                    ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 6.0f);
        return ImGui::InvisibleButton("##img", size);
    }

    // ---- video ----------------------------------------------------------

    ID3D11Texture2D* g_video_surface = 0;
    ID3D11ShaderResourceView* g_video_view = 0;
    int g_video_w = 0, g_video_h = 0;
    unsigned int g_video_serial = 0;

    void drop_video_texture()
    {
        if (g_video_view) { g_video_view->Release(); g_video_view = 0; }
        if (g_video_surface) { g_video_surface->Release(); g_video_surface = 0; }
        g_video_w = 0;
        g_video_h = 0;
    }

    // Dynamic, like the stream viewer's: the whole picture is replaced dozens
    // of times a second and a default-usage texture would mean a staging copy
    // for every frame.
    bool ensure_video_texture(int w, int h)
    {
        if (g_video_view && g_video_w == w && g_video_h == h) return true;
        drop_video_texture();

        if (!g_app.device || w <= 0 || h <= 0) return false;

        D3D11_TEXTURE2D_DESC desc;
        ccfset(&desc, 0, sizeof(desc));
        desc.Width = (UINT)w;
        desc.Height = (UINT)h;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        if (FAILED(g_app.device->CreateTexture2D(&desc, 0, &g_video_surface)) || !g_video_surface)
            return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC srv;
        ccfset(&srv, 0, sizeof(srv));
        srv.Format = desc.Format;
        srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = 1;

        if (FAILED(g_app.device->CreateShaderResourceView(g_video_surface, &srv, &g_video_view)))
        {
            drop_video_texture();
            return false;
        }

        g_video_w = w;
        g_video_h = h;
        return true;
    }

    void upload_video(const unsigned char* rgba, int w, int h)
    {
        if (!ensure_video_texture(w, h) || !g_app.context) return;

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (FAILED(g_app.context->Map(g_video_surface, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            return;

        const unsigned int row = (unsigned int)w * 4;
        for (int y = 0; y < h; y++)
            ccpy((unsigned char*)mapped.pData + (unsigned int)y * mapped.RowPitch,
                 rgba + (size_t)y * row, row);

        g_app.context->Unmap(g_video_surface, 0);
    }

    void format_clock(unsigned long long us, char* out, int cap)
    {
        unsigned int total = (unsigned int)(us / 1000000ULL);
        cnprint(out, cap, "%u:%02u", total / 60, total % 60);
    }

    // The still discord keeps for a video. Its proxy will hand back a frame
    // as an image if asked for one by format, which is where the preview used
    // to come from before videos stopped being drawn down the image path.
    void build_poster_url(const dattachment* a, char* out, int cap)
    {
        const char* src = (a->proxy_url && a->proxy_url[0]) ? a->proxy_url : a->url;
        if (!src || !src[0]) { out[0] = 0; return; }

        int len = (int)ccslenf(src);
        if (len >= cap - 20) { ccstrncpy(out, src, cap - 1); return; }

        ccpy(out, src, (size_t)len);
        out[len] = 0;

        bool has_query = false;
        for (const char* p = src; *p; p++) if (*p == '?') { has_query = true; break; }

        const char* suffix = has_query ? "&format=jpeg" : "?format=jpeg";
        ccstrncpy(out + len, suffix, cap - len - 1);
        out[cap - 1] = 0;
    }

    void draw_video_attachment(const dattachment* a)
    {
        bool mine = player::is_current(a->url);
        player_state st = mine ? player::state() : PLAYER_IDLE;

        // The card keeps its shape whether or not anything is playing, so the
        // messages around it do not jump when somebody presses play.
        float card_w = 480.0f;
        int pw = (mine && player::width() > 0) ? player::width() : (a->width > 0 ? a->width : 16);
        int ph = (mine && player::height() > 0) ? player::height() : (a->height > 0 ? a->height : 9);
        float card_h = card_w * (float)ph / (float)pw;
        if (card_h > 380.0f) card_h = 380.0f;
        if (card_h < 120.0f) card_h = 120.0f;

        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();

        dl->AddRectFilled(p, ImVec2(p.x + card_w, p.y + card_h), IM_COL32(12, 13, 16, 255), 8.0f);

        bool showing = false;

        // The still sits under everything, so there is something to look at
        // before anybody presses play and while the file is downloading.
        if (!mine || st == PLAYER_LOADING || st == PLAYER_IDLE)
        {
            char poster[720];
            build_poster_url(a, poster, sizeof(poster));

            const texture* t = poster[0] ? tex::get(poster) : 0;
            if (t && t->ready() && t->width > 0 && t->height > 0)
            {
                float scale = card_w / (float)t->width;
                float other = card_h / (float)t->height;
                if (other < scale) scale = other;

                float vw = (float)t->width * scale;
                float vh = (float)t->height * scale;
                ImVec2 at(p.x + (card_w - vw) * 0.5f, p.y + (card_h - vh) * 0.5f);

                dl->AddImageRounded(t->id(), at, ImVec2(at.x + vw, at.y + vh),
                                    ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 190), 4.0f);
            }
        }
        if (mine && (st == PLAYER_PLAYING || st == PLAYER_PAUSED || st == PLAYER_ENDED))
        {
            unsigned int serial = 0;
            const unsigned char* rgba = player::frame_rgba(&serial);
            if (rgba && serial != g_video_serial)
            {
                upload_video(rgba, player::width(), player::height());
                g_video_serial = serial;
            }

            if (g_video_view && g_video_w > 0)
            {
                // Letterboxed rather than stretched.
                float scale = card_w / (float)g_video_w;
                float other = card_h / (float)g_video_h;
                if (other < scale) scale = other;

                float vw = (float)g_video_w * scale;
                float vh = (float)g_video_h * scale;
                ImVec2 at(p.x + (card_w - vw) * 0.5f, p.y + (card_h - vh) * 0.5f);

                dl->AddImageRounded((ImTextureID)g_video_view, at,
                                    ImVec2(at.x + vw, at.y + vh),
                                    ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 4.0f);
                showing = true;
            }
        }

        // Clicking the picture is play and pause, the way it is everywhere.
        ImGui::InvisibleButton("##video", ImVec2(card_w, card_h));
        bool hovered = ImGui::IsItemHovered();

        if (ImGui::IsItemClicked())
        {
            if (!mine)                       player::open(a->url);
            else if (st == PLAYER_ENDED)     { player::open(a->url); }
            else                             player::toggle_pause();
        }

        if (!showing)
        {
            const char* text = "";
            if (mine && st == PLAYER_LOADING) text = "Загружается...";
            else if (mine && st == PLAYER_FAILED) text = player::last_error();

            if (text[0])
            {
                ImVec2 ts = ImGui::CalcTextSize(text);
                dl->AddRectFilled(ImVec2(p.x + (card_w - ts.x) * 0.5f - 8.0f,
                                         p.y + card_h * 0.5f + 22.0f),
                                  ImVec2(p.x + (card_w + ts.x) * 0.5f + 8.0f,
                                         p.y + card_h * 0.5f + 26.0f + ts.y + 4.0f),
                                  IM_COL32(0, 0, 0, 170), 4.0f);
                dl->AddText(ImVec2(p.x + (card_w - ts.x) * 0.5f, p.y + card_h * 0.5f + 26.0f),
                            (mine && st == PLAYER_FAILED) ? col::red : col::text_normal, text);
            }

            if (!mine || st != PLAYER_FAILED)
            {
                // A round play button in the middle, drawn rather than an icon
                // so it scales with the card.
                ImVec2 c(p.x + card_w * 0.5f, p.y + card_h * 0.5f - 6.0f);
                dl->AddCircleFilled(c, 26.0f, hovered ? col::accent : IM_COL32(40, 42, 50, 220));

                ImVec2 tri[3] = {
                    ImVec2(c.x - 7.0f, c.y - 11.0f),
                    ImVec2(c.x - 7.0f, c.y + 11.0f),
                    ImVec2(c.x + 12.0f, c.y),
                };
                dl->AddConvexPolyFilled(tri, 3, IM_COL32(255, 255, 255, 235));
            }
        }
        else if (st == PLAYER_PAUSED)
        {
            ImVec2 c(p.x + card_w * 0.5f, p.y + card_h * 0.5f);
            dl->AddCircleFilled(c, 26.0f, IM_COL32(20, 21, 26, 190));
            dl->AddRectFilled(ImVec2(c.x - 8.0f, c.y - 11.0f), ImVec2(c.x - 3.0f, c.y + 11.0f),
                              IM_COL32(255, 255, 255, 235), 1.5f);
            dl->AddRectFilled(ImVec2(c.x + 3.0f, c.y - 11.0f), ImVec2(c.x + 8.0f, c.y + 11.0f),
                              IM_COL32(255, 255, 255, 235), 1.5f);
        }

        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + card_h + 4.0f));

        // ---- the controls under it
        if (mine && (st == PLAYER_PLAYING || st == PLAYER_PAUSED || st == PLAYER_ENDED))
        {
            unsigned long long dur = player::duration_us();
            unsigned long long pos = player::position_us();
            if (dur && pos > dur) pos = dur;

            char elapsed[16], total[16];
            format_clock(pos, elapsed, sizeof(elapsed));
            format_clock(dur, total, sizeof(total));

            if (ImGui::SmallButton(st == PLAYER_PLAYING ? "Пауза" : "Играть"))
            {
                if (st == PLAYER_ENDED) player::open(a->url);
                else                    player::toggle_pause();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(player::muted() ? "Звук выкл" : "Звук вкл"))
                player::set_muted(!player::muted());
            ImGui::SameLine();
            if (ImGui::SmallButton("Стоп")) player::stop();

            ImGui::SameLine();
            char clock[40];
            cnprint(clock, sizeof(clock), "%s / %s", elapsed, total);
            ui_text_muted(clock);

            if (dur)
            {
                float where = (float)((double)pos / (double)dur);
                ImGui::SetNextItemWidth(card_w);
                if (ImGui::SliderFloat("##seek", &where, 0.0f, 1.0f, ""))
                    player::seek((unsigned long long)((double)where * (double)dur));
            }
        }
        else
        {
            char size_text[32];
            human_size(a->size, size_text, sizeof(size_text));

            ui_text_muted(a->filename ? a->filename : "видео");
            ImGui::SameLine();
            ui_text_muted(size_text);
            ImGui::SameLine();
            if (ImGui::SmallButton("Скачать")) start_download(a->url, a->filename);
        }
    }

    // ---- mentions in message text ---------------------------------------

    // On the wire a mention is <@id>, with an older <@!id> form that means the
    // same thing. <@&id> is a role and <#id> a channel; neither is a person, so
    // both are left as they are.
    snowflake mention_at(const char* p, int* length)
    {
        if (p[0] != '<' || p[1] != '@') return 0;

        int i = 2;
        if (p[i] == '!') i++;
        if (p[i] == '&') return 0;

        snowflake id = 0;
        int digits = 0;
        while (p[i] >= '0' && p[i] <= '9')
        {
            id = id * 10 + (snowflake)(p[i] - '0');
            i++;
            digits++;
        }

        if (!digits || p[i] != '>') return 0;
        *length = i + 1;
        return id;
    }

    void mention_label(snowflake id, char* out, int cap)
    {
        duser* u = store::find_user(id);
        if (u) cnprint(out, cap, "@%s", u->display_name());
        else   cnprint(out, cap, "@%llu", id);
    }

    // A tag rather than a run of text: it has a background, its own colour and
    // something to click, and none of that survives being part of one string
    // handed to a text call.
    void draw_mention_tag(snowflake id, snowflake guild_id, const char* label)
    {
        const float PAD = 4.0f;

        ImVec2 size = ImGui::CalcTextSize(label);
        ImVec2 at = ImGui::GetCursorScreenPos();

        ImGui::PushID((int)(id & 0x7FFFFFFF));
        ImGui::InvisibleButton("##mention", ImVec2(size.x + PAD * 2.0f, size.y));
        bool hovered = ImGui::IsItemHovered();
        bool clicked = ImGui::IsItemClicked();
        ImGui::PopID();

        // Being the one mentioned is worth seeing from across the room, so it
        // gets the accent colour rather than the muted one.
        bool at_me = id == store::self_id();

        ImU32 bg = at_me ? (hovered ? IM_COL32(88, 101, 242, 190) : IM_COL32(88, 101, 242, 110))
                         : (hovered ? IM_COL32(88, 101, 242, 120) : IM_COL32(88, 101, 242, 60));

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(at, ImVec2(at.x + size.x + PAD * 2.0f, at.y + size.y), bg, 3.0f);
        dl->AddText(ImVec2(at.x + PAD, at.y),
                    at_me ? col::text_normal : col::text_link, label);

        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (clicked) ui_open_profile(id, guild_id);
    }

    // Laid out word by word instead of handed over in one piece, because the
    // tags have to be separate items. Wrapping is done here for the same
    // reason: the wrapping a text call does is inside the string it was given,
    // and these are no longer one string.
    void draw_message_text(const char* text, snowflake guild_id, ImU32 colour)
    {
        float start_x = ImGui::GetCursorPosX();
        float wrap_x = start_x + ImGui::GetContentRegionAvail().x - 20.0f;
        float space = ImGui::CalcTextSize(" ").x;

        bool on_line = false;
        const char* p = text;

        while (*p)
        {
            if (*p == '\r') { p++; continue; }

            if (*p == '\n')
            {
                // The cursor is already below the last item, so a break is
                // only needed for a line that had nothing on it at all.
                if (!on_line) ImGui::NewLine();
                on_line = false;
                p++;
                continue;
            }

            if (*p == ' ' || *p == '\t') { p++; continue; }

            int taken = 0;
            snowflake who = mention_at(p, &taken);

            char label[128];
            float width = 0.0f;

            if (who)
            {
                mention_label(who, label, sizeof(label));
                width = ImGui::CalcTextSize(label).x + 8.0f;
            }
            else
            {
                const char* end = p;
                while (*end && *end != ' ' && *end != '\n' && *end != '\r' && *end != '\t')
                {
                    int skip = 0;
                    if (mention_at(end, &skip)) break;   // a tag starts here
                    end++;
                }
                if (end == p) end++;                     // never stall

                taken = (int)(end - p);
                int copy = taken < (int)sizeof(label) - 1 ? taken : (int)sizeof(label) - 1;
                ccpy(label, p, (size_t)copy);
                label[copy] = 0;
                width = ImGui::CalcTextSize(label).x;
            }

            if (on_line)
            {
                ImGui::SameLine(0.0f, space);
                if (ImGui::GetCursorPosX() + width > wrap_x)
                {
                    ImGui::NewLine();
                    on_line = false;
                }
            }
            if (!on_line) ImGui::SetCursorPosX(start_x);

            if (who)
            {
                draw_mention_tag(who, guild_id, label);
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Text, colour);
                ImGui::TextUnformatted(label);
                ImGui::PopStyleColor();
            }

            on_line = true;
            p += taken;
        }
    }

    void draw_attachment(const dattachment* a)
    {
        ImGui::PushID((int)(a->id & 0x7FFFFFFF));

        // Off by default, and when it is off a video is drawn the way it
        // always was: a still from the proxy with a download button on it.
        if (a->is_video() && ui_video_player())
        {
            draw_video_attachment(a);
            ImGui::PopID();
            return;
        }

        if (a->is_image())
        {
            char image_url[700];
            build_image_url(a->proxy_url, a->url, image_url, sizeof(image_url));

            if (draw_image(image_url, a->width, a->height))
            {
                ui_open_image_viewer(image_url, a->filename);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", a->filename);

            if (ImGui::BeginPopupContextItem("##imgctx"))
            {
                if (ImGui::MenuItem("Скачать")) start_download(a->url, a->filename);
                ImGui::EndPopup();
            }
        }
        else
        {
            char size_text[32];
            human_size(a->size, size_text, sizeof(size_text));

            ImVec2 p = ImGui::GetCursorScreenPos();
            float w = 340.0f, h = 52.0f;
            ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + w, p.y + h), col::bg_panel, 6.0f);
            ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + 12, p.y + 8), col::text_link, a->filename);
            ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + 12, p.y + 28), col::text_muted, size_text);

            ImGui::SetCursorScreenPos(ImVec2(p.x + w - 104, p.y + 12));
            if (ui_icon_button("Скачать##att", ImVec2(92, 28), col::accent, col::accent_hover))
                start_download(a->url, a->filename);

            ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + 4));
        }

        ImGui::PopID();
    }

    // Which address build_image_url is allowed to fall back to for an embed.
    // Handing it the original is what lets an animation be fetched from the
    // site it lives on; with the option off it only ever sees the proxy, and
    // nothing leaves discord.
    const char* embed_origin(const char* proxied, const char* source)
    {
        if (!ui_embed_direct_gifs()) return proxied;
        return (source && source[0]) ? source : proxied;
    }

    void draw_embed(const dembed* e)
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = 420.0f;

        ImGui::BeginGroup();
        ImGui::Indent(10.0f);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w - 20.0f);

        if (e->author_name) ui_text_muted(e->author_name);
        if (e->title)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, col::text_link);
            ImGui::TextWrapped("%s", e->title);
            ImGui::PopStyleColor();
        }
        if (e->description) ImGui::TextWrapped("%s", e->description);
        if (e->image_url)
        {
            char image_url[700];
            build_image_url(e->image_url, embed_origin(e->image_url, e->image_src),
                            image_url, sizeof(image_url));
            if (draw_image(image_url, e->image_w, e->image_h))
            {
                ccstrncpy(g_ui.viewer_url, image_url, sizeof(g_ui.viewer_url) - 1);
                g_ui.viewer_open = true;
            }
        }
        else if (e->thumbnail_url)
        {
            char image_url[700];
            build_image_url(e->thumbnail_url, embed_origin(e->thumbnail_url, e->thumbnail_src),
                            image_url, sizeof(image_url));
            draw_image(image_url, 0, 0);
        }
        else if (e->url)
        {
            // Nothing to draw: discord unfurled the link but sent neither an
            // image nor a thumbnail. Sites that host GIFs usually come through
            // as a "gifv" embed whose picture lives in a video field as mp4,
            // which this client has no decoder for. Logged once per draw is too
            // noisy, so only the first time the embed is seen.
            static const char* last_logged = 0;
            if (last_logged != e->url)
            {
                last_logged = e->url;
                log_line("embed: nothing drawable for %s", e->url);
            }
        }
        if (e->footer) ui_text_muted(e->footer);

        ImGui::PopTextWrapPos();
        ImGui::Unindent(10.0f);
        ImGui::EndGroup();

        ImVec2 end = ImGui::GetItemRectMax();

        // Embed colours arrive as 0xRRGGBB, while IM_COL32 packs to ABGR.
        ImU32 accent = col::separator;
        if (e->color)
            accent = IM_COL32((e->color >> 16) & 0xFF, (e->color >> 8) & 0xFF, e->color & 0xFF, 255);

        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x, p.y), ImVec2(p.x + 4, end.y), accent, 2.0f);
    }

    // The message types discord sends with no text of their own. The client
    // used to render these as an empty row from somebody, which is how a
    // channel full of calls looked like a channel full of nothing.
    const char* system_message_text(int type)
    {
        switch (type)
        {
        case 1:  return "добавил участника в беседу";
        case 2:  return "убрал участника из беседы";
        case 3:  return "начал звонок";
        case 4:  return "сменил название беседы";
        case 5:  return "сменил значок беседы";
        case 6:  return "закрепил сообщение";
        case 7:  return "зашёл на сервер";
        case 8:  case 9: case 10: case 11: return "забустил сервер";
        case 12: return "подписался на канал";
        case 18: return "создал ветку";
        case 21: return "ответил в ветке";
        case 46: return "опросу пришёл конец";
        default: return 0;
        }
    }

    void draw_system_message(dmessage* m, const char* text)
    {
        duser* author = store::find_user(m->author_id);

        ImGui::PushID((int)(m->id & 0x7FFFFFFF));
        ImGui::Dummy(ImVec2(0, 4.0f));
        ImGui::Indent(12.0f);

        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // A small marker rather than an avatar: these are events, not somebody
        // talking, and they should not read like a message.
        dl->AddCircleFilled(ImVec2(p.x + 6.0f, p.y + 8.0f), 3.5f,
                            m->type == 3 ? col::green : col::text_muted);

        ImGui::Indent(20.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, col::text_muted);

        char line[256];
        cnprint(line, sizeof(line), "%s %s",
                author ? author->display_name() : "кто-то", text);
        ImGui::TextUnformatted(line);

        char stamp[48];
        format_timestamp(m->timestamp, stamp, sizeof(stamp));
        if (stamp[0])
        {
            ImGui::SameLine();
            ImGui::TextUnformatted(stamp);
        }

        ImGui::PopStyleColor();
        ImGui::Unindent(20.0f);
        ImGui::Unindent(12.0f);
        ImGui::PopID();
    }

    void draw_message(dmessage* m, bool grouped)
    {
        // Anything with no body of its own is an event, and reads better as
        // one line than as an empty message from somebody.
        const char* system_text = (m->content && m->content[0]) ? 0
                                                                : system_message_text(m->type);
        if (system_text)
        {
            draw_system_message(m, system_text);
            return;
        }

        duser* author = store::find_user(m->author_id);

        ImGui::PushID((int)(m->id & 0x7FFFFFFF));
        ImGui::Dummy(ImVec2(0, grouped ? 1.0f : 6.0f));

        float avatar_size = 38.0f;
        float text_indent = avatar_size + 14.0f;
        ImVec2 row_start = ImGui::GetCursorScreenPos();

        // The row is a group rather than an InvisibleButton: a button spanning
        // the row would be added last and would steal hover from the avatar and
        // the inline images underneath it.
        ImGui::BeginGroup();

        if (!grouped)
        {
            ImGui::SetCursorScreenPos(ImVec2(row_start.x + 4.0f, row_start.y));
            ui_avatar(author, avatar_size, false);
            if (ImGui::IsItemClicked() && author) ui_open_profile(author->id, m->guild_id);

            ImGui::SetCursorScreenPos(ImVec2(row_start.x + text_indent, row_start.y));

            ImGui::PushFont(g_app.font_bold);
            ImGui::PushStyleColor(ImGuiCol_Text, col::text_normal);
            ImGui::TextUnformatted(author ? author->display_name() : "неизвестный");
            ImGui::PopStyleColor();
            ImGui::PopFont();

            if (ImGui::IsItemClicked() && author) ui_open_profile(author->id, m->guild_id);

            char stamp[48];
            format_timestamp(m->timestamp, stamp, sizeof(stamp));
            if (stamp[0])
            {
                ImGui::SameLine();
                ui_text_muted(stamp);
            }
            if (m->pending)
            {
                ImGui::SameLine();
                ui_text_muted("отправляется...");
            }
            if (m->deleted)
            {
                // Kept on screen rather than removed. Discord tells nobody what
                // was taken back, which is exactly why it is worth showing.
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, col::red);
                ImGui::TextUnformatted("удалено");
                ImGui::PopStyleColor();
            }
        }

        if (m->referenced_id)
        {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + text_indent);
            dchannel* ch = store::find_channel(m->channel_id);
            dmessage* ref = store::find_message(ch, m->referenced_id);
            if (ref)
            {
                duser* ref_author = store::find_user(ref->author_id);
                char preview[128];
                cnprint(preview, sizeof(preview), "> %s: %s",
                        ref_author ? ref_author->display_name() : "?",
                        ref->content ? ref->content : "");
                ui_text_muted(preview);
            }
            else
            {
                ui_text_muted("> ответ на сообщение");
            }
        }

        if (m->content && m->content[0])
        {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + text_indent);
            draw_message_text(m->content, m->guild_id,
                              m->failed ? col::red : col::text_normal);
        }

        for (unsigned int i = 0; i < m->attachments.count; i++)
        {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + text_indent);
            draw_attachment(&m->attachments[i]);
        }

        for (unsigned int i = 0; i < m->embeds.count; i++)
        {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + text_indent);
            draw_embed(&m->embeds[i]);
        }

        if (m->reactions.count)
        {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + text_indent);
            for (unsigned int i = 0; i < m->reactions.count; i++)
            {
                char label[96];
                cnprint(label, sizeof(label), "%s %d", m->reactions[i].emoji_name, m->reactions[i].count);
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(col::bg_panel));
                ImGui::SmallButton(label);
                ImGui::PopStyleColor();
                if (i + 1 < m->reactions.count) ImGui::SameLine();
            }
        }

        ImGui::EndGroup();

        float row_width = ImGui::GetContentRegionAvail().x;
        ImVec2 row_end = ImGui::GetCursorScreenPos();
        bool hovered = ImGui::IsMouseHoveringRect(row_start, ImVec2(row_start.x + row_width, row_end.y), false);

        if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
            ImGui::OpenPopup("##msgctx");

        if (ImGui::BeginPopup("##msgctx"))
        {
            if (ImGui::MenuItem("Ответить")) g_ui.reply_to = m->id;
            if (author && ImGui::MenuItem("Профиль автора")) ui_open_profile(author->id, m->guild_id);
            if (m->content && ImGui::MenuItem("Копировать текст")) ImGui::SetClipboardText(m->content);

            ImGui::Separator();
            if (author) ui_copy_id_item(author->id, "Скопировать ID автора");
            ui_copy_id_item(m->id, "Скопировать ID сообщения");
            ui_copy_id_item(m->channel_id, "Скопировать ID канала");
            if (m->guild_id) ui_copy_id_item(m->guild_id, "Скопировать ID сервера");

            if (author && author->id == store::self_id())
            {
                ImGui::Separator();
                if (ImGui::MenuItem("Удалить")) api::delete_message(m->channel_id, m->id);
            }
            ImGui::EndPopup();
        }

        if (hovered)
        {
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(row_start.x, row_start.y), ImVec2(row_start.x + row_width, row_end.y),
                IM_COL32(255, 255, 255, 6));
        }

        ImGui::PopID();
    }

    void draw_attachment_tray(float width)
    {
        if (!g_ui.pending_files.count) return;

        ImGui::Dummy(ImVec2(0, 2));
        for (unsigned int i = 0; i < g_ui.pending_files.count; i++)
        {
            ImGui::PushID((int)i);

            char size_text[32];
            human_size(g_ui.pending_files[i].size, size_text, sizeof(size_text));

            char label[320];
            cnprint(label, sizeof(label), "%s  (%s)", g_ui.pending_files[i].name, size_text);

            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(col::bg_panel));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4(col::bg_hover));
            ImGui::Button(label, ImVec2(0, 24));
            ImGui::PopStyleColor(2);

            ImGui::SameLine();
            if (ui_icon_button("x", ImVec2(24, 24), col::red, col::red))
            {
                if (g_ui.pending_files[i].data) memfree(g_ui.pending_files[i].data);
                g_ui.pending_files.delete_at(i);
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
    }

    void submit_message(dchannel* ch)
    {
        bool has_text = g_ui.message_input[0] != 0;
        bool has_files = g_ui.pending_files.count > 0;
        if (!has_text && !has_files) return;

        if (has_files)
        {
            api::send_message_with_files(ch->id, g_ui.message_input, &g_ui.pending_files);
            g_ui.pending_files = ulist<upload_file>();
        }
        else
        {
            api::send_message(ch->id, g_ui.message_input, g_ui.reply_to);
        }

        ccfset(g_ui.message_input, 0, sizeof(g_ui.message_input));
        g_ui.reply_to = 0;
        g_ui.scroll_to_bottom = true;
    }

    // ---- @ completion ---------------------------------------------------

    // Where the word being typed after an @ begins, or -1 when the caret is
    // not inside one.
    int mention_start(const char* text)
    {
        int len = (int)ccslenf(text);

        for (int i = len - 1; i >= 0; i--)
        {
            char c = text[i];
            if (c == '@')
            {
                // Only at a word boundary, so an email address does not open
                // the list halfway through.
                if (i == 0 || text[i - 1] == ' ' || text[i - 1] == '\n') return i;
                return -1;
            }
            if (c == ' ' || c == '\n') return -1;
        }
        return -1;
    }

    bool name_contains(const char* name, const char* needle)
    {
        if (!needle[0]) return true;

        for (const char* p = name; *p; p++)
        {
            const char* a = p;
            const char* b = needle;
            while (*a && *b && cctolower(*a) == cctolower(*b)) { a++; b++; }
            if (!*b) return true;
        }
        return false;
    }

    void replace_from(int at, const char* text)
    {
        g_ui.message_input[at] = 0;
        ccstrncpy(g_ui.message_input + at, text,
                  sizeof(g_ui.message_input) - at - 1);
    }

    // Discord wants the id on the wire, not the name - turning it back into
    // something readable is the receiving client's job.
    void insert_mention(int at, snowflake id)
    {
        char tail[64];
        cnprint(tail, sizeof(tail), "<@%llu> ", id);
        replace_from(at, tail);
    }

    void draw_mention_popup(dchannel* ch)
    {
        int at = mention_start(g_ui.message_input);
        if (at < 0) return;

        const char* needle = g_ui.message_input + at + 1;

        ImGui::BeginChild("##mentions", ImVec2(340, 150), true);

        int shown = 0;

        // A server offers everybody loaded; a conversation offers whoever is
        // in it.
        dguild* g = store::find_guild(ch->guild_id);
        if (g)
        {
            if (name_contains("everyone", needle) && ImGui::Selectable("@everyone"))
                replace_from(at, "@everyone ");
            if (name_contains("here", needle) && ImGui::Selectable("@here"))
                replace_from(at, "@here ");

            for (unsigned int i = 0; i < g->members.count && shown < 12; i++)
            {
                dmember* m = &g->members[i];
                duser* u = store::find_user(m->user_id);
                if (!u) continue;

                const char* name = m->nick ? m->nick : u->display_name();
                if (!name_contains(name, needle)) continue;

                ImGui::PushID((int)(u->id & 0x7FFFFFFF));
                if (ImGui::Selectable(name)) insert_mention(at, u->id);
                ImGui::PopID();
                shown++;
            }
        }
        else
        {
            for (unsigned int i = 0; i < ch->recipients.count && shown < 12; i++)
            {
                duser* u = store::find_user(ch->recipients[i]);
                if (!u) continue;

                const char* name = u->display_name();
                if (!name_contains(name, needle)) continue;

                ImGui::PushID((int)(u->id & 0x7FFFFFFF));
                if (ImGui::Selectable(name)) insert_mention(at, u->id);
                ImGui::PopID();
                shown++;
            }
        }

        if (!shown) ui_text_muted("никого не нашлось");
        ImGui::EndChild();
    }
}

void ui_view_chat(float width, float height)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetWindowPos();
    dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), col::bg_chat);

    dchannel* ch = store::find_channel(g_ui.active_channel);
    if (!ch)
    {
        ImGui::SetCursorPos(ImVec2(width * 0.5f - 90.0f, height * 0.5f));
        ui_text_muted("Выберите канал слева");
        return;
    }

    // ---- header ----
    const float header_h = 46.0f;
    char title[256];
    ui_channel_display_name(ch, title, sizeof(title));

    dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + header_h), col::bg_chat);
    dl->AddLine(ImVec2(origin.x, origin.y + header_h), ImVec2(origin.x + width, origin.y + header_h), col::separator);

    ImGui::SetCursorPos(ImVec2(16, 13));
    ImGui::PushFont(g_app.font_bold);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();

    if (ch->topic && ch->topic[0])
    {
        ImGui::SameLine(0, 14);
        ImGui::PushStyleColor(ImGuiCol_Text, col::text_muted);
        ImGui::TextUnformatted(ch->topic);
        ImGui::PopStyleColor();
    }

    // Direct messages get a call button: a DM call is an ordinary voice
    // connection to the channel itself, plus a ring so the other side notices.
    if (ch->is_dm())
    {
        bool in_this_call = voice::current_channel() == ch->id &&
                            voice::state() != VOICE_IDLE && voice::state() != VOICE_FAILED;

        ImGui::SetCursorPos(ImVec2(width - 48.0f, 8.0f));
        if (ui_glyph_button("##call", in_this_call ? ICON_HANGUP : ICON_PHONE, false,
                            ImVec2(32, 30),
                            in_this_call ? col::red : col::bg_panel,
                            in_this_call ? col::red : col::bg_hover,
                            in_this_call ? col::text_normal : col::green))
        {
            if (in_this_call)
            {
                // A stream belongs to the call it was opened in, so it cannot
                // outlive it.
                screenshare::stop();
                voice::leave();
            }
            else
            {
                voice::join(0, ch->id);
                api::ring_call(ch->id);
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(in_this_call ? "Завершить звонок" : "Позвонить");
    }

    // ---- input block height ----
    float tray_h = g_ui.pending_files.count ? (g_ui.pending_files.count * 28.0f + 6.0f) : 0.0f;
    float reply_h = g_ui.reply_to ? 24.0f : 0.0f;
    float input_h = 78.0f + tray_h + reply_h;
    float list_h = height - header_h - input_h;
    if (list_h < 80.0f) list_h = 80.0f;

    // ---- message list ----
    ImGui::SetCursorPos(ImVec2(0, header_h));
    ImGui::BeginChild("##messages", ImVec2(width, list_h), false, ImGuiWindowFlags_HorizontalScrollbar);

    // The archive is read the moment a channel is looked at, and on no other
    // condition.
    //
    // It used to sit inside the "nothing has been fetched yet" branch below,
    // which meant it never ran at all in the case it exists for: opening a
    // channel from the sidebar starts a fetch first, so by the time the view
    // drew, the channel was already marked loading, and then failed. Both of
    // those closed the branch, and the saved copy stayed on disk.
    //
    // The mark lives on the channel rather than in a list beside it, so it
    // dies with the store when accounts change.
    if (!ch->archive_loaded)
    {
        ch->archive_loaded = true;

        int restored = archive::load_channel(ch->id);
        ch->archive_messages = restored;
        if (restored) store::bump_revision();
    }

    if (!ch->history_loaded && !ch->history_loading && !ch->history_failed)
    {
        api::fetch_messages(ch->id, 0);
    }

    // Worth saying out loud: without it there is no way to tell an empty
    // channel from an archive that failed to open.
    if (ch->archive_messages > 0 && (offline::active() || !ch->history_loaded))
    {
        char note[96];
        cnprint(note, sizeof(note), "Из архива: %d сообщений", ch->archive_messages);

        ImGui::Dummy(ImVec2(0, 4));
        ImGui::Indent(16.0f);
        ui_text_muted(note);
        ImGui::Unindent(16.0f);
    }
    else if (offline::active() && ch->archive_messages == 0)
    {
        ImGui::Dummy(ImVec2(0, 8));
        ImGui::Indent(16.0f);
        ui_text_muted("Этот канал в архив не попал. Прогрей его, когда будет связь.");
        ImGui::Unindent(16.0f);
    }

    if (ch->history_loading)
    {
        ImGui::Dummy(ImVec2(0, 8));
        ImGui::Indent(16.0f);
        ui_text_muted("Загрузка сообщений...");
        ImGui::Unindent(16.0f);
    }
    else if (ch->history_failed && !ch->history_loaded)
    {
        ImGui::Dummy(ImVec2(0, 8));
        ImGui::Indent(16.0f);
        ui_text_muted(archive::channel_count(ch->id)
                      ? "Дальше только сохранённое."
                      : "История недоступна.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Повторить"))
        {
            ch->history_failed = false;
            api::fetch_messages(ch->id, 0);
        }
        ImGui::Unindent(16.0f);
    }
    else if (!ch->history_exhausted && ch->messages.count > 0)
    {
        ImGui::Dummy(ImVec2(0, 8));
        ImGui::Indent(16.0f);
        if (ImGui::SmallButton("Загрузить более старые сообщения"))
            api::fetch_messages(ch->id, ch->messages[0].id);
        ImGui::Unindent(16.0f);
    }

    ImGui::Indent(12.0f);

    unsigned int first = 0;
    if (ch->messages.count > MAX_RENDERED_MESSAGES) first = ch->messages.count - MAX_RENDERED_MESSAGES;

    snowflake prev_author = 0;
    unsigned long long prev_time = 0;

    for (unsigned int i = first; i < ch->messages.count; i++)
    {
        dmessage* m = &ch->messages[i];
        unsigned long long t = snowflake_time_ms(m->id);

        // Consecutive messages from one author within 7 minutes share a header.
        bool grouped = (m->author_id == prev_author) && (t - prev_time < 7 * 60 * 1000) && !m->referenced_id;
        draw_message(m, grouped);

        prev_author = m->author_id;
        prev_time = t;
    }

    ImGui::Unindent(12.0f);
    ImGui::Dummy(ImVec2(0, 8));

    if (g_ui.scroll_to_bottom || ch->messages.count != g_ui.seen_message_count)
    {
        // Only auto-scroll when the user is already near the end.
        if (g_ui.scroll_to_bottom || ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 80.0f)
            ImGui::SetScrollHereY(1.0f);
        g_ui.seen_message_count = ch->messages.count;
        g_ui.scroll_to_bottom = false;
    }

    ImGui::EndChild();

    // ---- composer ----
    ImGui::SetCursorPos(ImVec2(12, header_h + list_h + 4));
    ImGui::BeginGroup();

    if (g_ui.reply_to)
    {
        dmessage* ref = store::find_message(ch, g_ui.reply_to);
        duser* ref_author = ref ? store::find_user(ref->author_id) : 0;
        char label[160];
        cnprint(label, sizeof(label), "Ответ %s", ref_author ? ref_author->display_name() : "");
        ui_text_muted(label);
        ImGui::SameLine();
        if (ImGui::SmallButton("отменить")) g_ui.reply_to = 0;
    }

    draw_attachment_tray(width);

    if (ui_glyph_button("##attach", ICON_PLUS, false, ImVec2(32, 32), col::bg_panel, col::bg_hover, col::text_normal))
    {
        wchar_t path[1024];
        if (ufile::open_dialog(path, 1024)) ui_attach_path(path);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Прикрепить файл (можно перетащить или вставить Ctrl+V)");

    ImGui::SameLine();
    ImGui::SetNextItemWidth(width - 60.0f);

    ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CtrlEnterForNewLine;
    bool send = ImGui::InputTextMultiline("##input", g_ui.message_input, sizeof(g_ui.message_input),
                                          ImVec2(width - 60.0f, 32.0f), flags);

    bool input_active = ImGui::IsItemActive() || ImGui::IsItemFocused();

    if (send) submit_message(ch);

    ImGui::EndGroup();

    // Ctrl+V attaches a file or a bitmap; plain text is handled by imgui's own
    // paste. The text check keeps the two apart - a browser "copy image" puts
    // both a bitmap and the source url on the clipboard, and doing both at once
    // would attach the picture *and* type the url.
    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false))
    {
        bool has_text = IsClipboardFormatAvailable(CF_UNICODETEXT) != 0;
        bool has_payload = IsClipboardFormatAvailable(CF_HDROP) ||
                           IsClipboardFormatAvailable(CF_DIB) ||
                           IsClipboardFormatAvailable(CF_DIBV5);
        if (has_payload && !has_text) ui_paste_from_clipboard();
    }

    // ---- who is typing, on the line under the box
    {
        snowflake writers[8];
        int count = store::typing_in(ch->id, writers, 8);

        if (count > 0)
        {
            char line[256];
            duser* first = store::find_user(writers[0]);
            const char* a_name = first ? first->display_name() : "кто-то";

            if (count == 1)
            {
                cnprint(line, sizeof(line), "%s печатает...", a_name);
            }
            else if (count == 2)
            {
                duser* second = store::find_user(writers[1]);
                cnprint(line, sizeof(line), "%s и %s печатают...", a_name,
                        second ? second->display_name() : "кто-то");
            }
            else
            {
                cnprint(line, sizeof(line), "%s и ещё %d печатают...", a_name, count - 1);
            }

            ui_text_muted(line);
        }
    }

    // ---- @ completion
    draw_mention_popup(ch);

    // Typing indicator, at most once every 8 seconds.
    if (input_active && g_ui.message_input[0])
    {
        unsigned long long now = GetTickCount64();
        if (now - g_ui.last_typing_ms > 8000)
        {
            api::trigger_typing(ch->id);
            g_ui.last_typing_ms = now;
        }
    }

    if (api::last_error()[0])
    {
        ImGui::SetCursorPos(ImVec2(12, height - 20.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, col::yellow);
        ImGui::TextUnformatted(api::last_error());
        ImGui::PopStyleColor();
        if (ImGui::IsItemClicked()) api::clear_last_error();
    }
}

// ---------------------------------------------------------------------------
// full size image viewer
// ---------------------------------------------------------------------------

void ui_open_image_viewer(const char* url, const char* name)
{
    if (!url || !url[0]) return;

    ccfset(g_ui.viewer_url, 0, sizeof(g_ui.viewer_url));
    ccstrncpy(g_ui.viewer_url, url, sizeof(g_ui.viewer_url) - 1);

    ccfset(g_ui.viewer_name, 0, sizeof(g_ui.viewer_name));
    if (name && name[0]) ccstrncpy(g_ui.viewer_name, name, sizeof(g_ui.viewer_name) - 1);

    g_ui.viewer_zoom = 1.0f;
    g_ui.viewer_pan = ImVec2(0, 0);
    g_ui.viewer_open = true;
}

void ui_view_image_viewer()
{
    // OpenPopup must fire exactly once; calling it every frame would reset the
    // popup state and swallow the escape key.
    static bool opened = false;
    if (!g_ui.viewer_open)
    {
        opened = false;
        return;
    }
    if (!opened)
    {
        ImGui::OpenPopup("##viewer");
        opened = true;
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(200, 200), ImVec2(vp->WorkSize.x * 0.95f, vp->WorkSize.y * 0.95f));

    // A modal closes itself on escape without telling anybody, and this used
    // to leave `opened` true and viewer_open true at the same time: the popup
    // was gone, OpenPopup was never called again because it looked open, and
    // no picture could be opened for the rest of the session. Whatever imgui
    // decides, the flag here follows it.
    if (!ImGui::IsPopupOpen("##viewer"))
    {
        opened = false;
        g_ui.viewer_open = false;
        return;
    }

    if (ImGui::BeginPopupModal("##viewer", 0, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize))
    {
        const texture* t = tex::get(g_ui.viewer_url);
        if (t->ready())
        {
            // The size it gets at zoom one: as large as fits, never enlarged
            // past its own resolution. Zoom multiplies that.
            float max_w = vp->WorkSize.x * 0.88f;
            float max_h = vp->WorkSize.y * 0.78f;

            float fit = 1.0f;
            if ((float)t->width > max_w) fit = max_w / (float)t->width;
            if ((float)t->height * fit > max_h) fit = max_h / (float)t->height;

            if (g_ui.viewer_zoom < 1.0f) g_ui.viewer_zoom = 1.0f;
            if (g_ui.viewer_zoom > 8.0f) g_ui.viewer_zoom = 8.0f;

            float scale = fit * g_ui.viewer_zoom;
            float draw_w = (float)t->width * scale;
            float draw_h = (float)t->height * scale;

            // The window itself never grows past the fitted size; zooming in
            // moves the picture inside it instead of pushing the modal off
            // the screen.
            float pane_w = (float)t->width * fit;
            float pane_h = (float)t->height * fit;

            ImVec2 origin = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##pane", ImVec2(pane_w, pane_h));
            bool hovered = ImGui::IsItemHovered();

            // Zoom towards the cursor: the point under it stays put, which is
            // the only way scrolling into a corner of a picture feels right.
            if (hovered)
            {
                float wheel = ImGui::GetIO().MouseWheel;
                if (wheel != 0.0f)
                {
                    float before = g_ui.viewer_zoom;
                    float after = before * (wheel > 0.0f ? 1.25f : 0.8f);
                    if (after < 1.0f) after = 1.0f;
                    if (after > 8.0f) after = 8.0f;

                    ImVec2 mouse = ImGui::GetIO().MousePos;
                    ImVec2 centre(origin.x + pane_w * 0.5f, origin.y + pane_h * 0.5f);
                    ImVec2 from_centre(mouse.x - centre.x - g_ui.viewer_pan.x,
                                       mouse.y - centre.y - g_ui.viewer_pan.y);

                    float ratio = after / before;
                    g_ui.viewer_pan.x -= from_centre.x * (ratio - 1.0f);
                    g_ui.viewer_pan.y -= from_centre.y * (ratio - 1.0f);
                    g_ui.viewer_zoom = after;
                }
            }

            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            {
                ImVec2 delta = ImGui::GetIO().MouseDelta;
                g_ui.viewer_pan.x += delta.x;
                g_ui.viewer_pan.y += delta.y;
            }

            // Never let the picture be dragged away from the window entirely.
            float slack_x = (draw_w - pane_w) * 0.5f;
            float slack_y = (draw_h - pane_h) * 0.5f;
            if (slack_x < 0.0f) slack_x = 0.0f;
            if (slack_y < 0.0f) slack_y = 0.0f;

            if (g_ui.viewer_pan.x > slack_x) g_ui.viewer_pan.x = slack_x;
            if (g_ui.viewer_pan.x < -slack_x) g_ui.viewer_pan.x = -slack_x;
            if (g_ui.viewer_pan.y > slack_y) g_ui.viewer_pan.y = slack_y;
            if (g_ui.viewer_pan.y < -slack_y) g_ui.viewer_pan.y = -slack_y;

            ImVec2 at(origin.x + (pane_w - draw_w) * 0.5f + g_ui.viewer_pan.x,
                      origin.y + (pane_h - draw_h) * 0.5f + g_ui.viewer_pan.y);

            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->PushClipRect(origin, ImVec2(origin.x + pane_w, origin.y + pane_h), true);
            dl->AddImage(t->id(), at, ImVec2(at.x + draw_w, at.y + draw_h));
            dl->PopClipRect();

            char info[96];
            cnprint(info, sizeof(info), "%d x %d   %.0f%%", t->width, t->height,
                    (double)(g_ui.viewer_zoom * 100.0f));
            ui_text_muted(info);
        }
        else
        {
            ui_text_muted("Загрузка изображения...");
        }

        if (ImGui::Button("Закрыть", ImVec2(110, 30)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            g_ui.viewer_open = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("Крупнее", ImVec2(90, 30))) g_ui.viewer_zoom *= 1.25f;
        ImGui::SameLine();
        if (ImGui::Button("Мельче", ImVec2(90, 30))) g_ui.viewer_zoom *= 0.8f;
        ImGui::SameLine();
        if (ImGui::Button("Сбросить", ImVec2(100, 30)))
        {
            g_ui.viewer_zoom = 1.0f;
            g_ui.viewer_pan = ImVec2(0, 0);
        }

        ImGui::SameLine();
        if (ImGui::Button("Скачать", ImVec2(110, 30)))
        {
            char clean[260];

            if (g_ui.viewer_name[0])
            {
                ccstrncpy(clean, g_ui.viewer_name, sizeof(clean) - 1);
            }
            else
            {
                const char* name = g_ui.viewer_url;
                for (const char* p = g_ui.viewer_url; *p; p++)
                    if (*p == '/') name = p + 1;

                int i = 0;
                while (name[i] && name[i] != '?' && i < 255) { clean[i] = name[i]; i++; }
                clean[i] = 0;
            }

            start_download(g_ui.viewer_url, clean);
        }

        ImGui::SameLine();
        ui_text_muted("колесо - масштаб, перетаскивание - сдвиг");

        ImGui::EndPopup();
    }
}
