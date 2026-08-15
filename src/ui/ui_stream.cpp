#include "pch.h"
#include <d3d11.h>

#include "ui_state.h"
#include "theme.h"

#include "core/app.h"
#include "core/log.h"
#include "discord/store.h"
#include "video/streamview.h"

// The window somebody else's screen appears in.
//
// The viewer hands over finished pictures as RGBA and knows nothing about the
// renderer; everything here is the other half of that: one dynamic texture that
// is rewritten in place every frame, and enough chrome to tell a stream that is
// still connecting from one that has stopped.

namespace
{
    ID3D11Texture2D* g_surface = 0;
    ID3D11ShaderResourceView* g_view = 0;
    int g_tex_w = 0;
    int g_tex_h = 0;

    void drop_texture()
    {
        if (g_view) { g_view->Release(); g_view = 0; }
        if (g_surface) { g_surface->Release(); g_surface = 0; }
        g_tex_w = 0;
        g_tex_h = 0;
    }

    // A dynamic surface, because the whole picture is replaced several times a
    // second and a default-usage texture would mean a staging copy every time.
    bool ensure_texture(int w, int h)
    {
        if (g_view && g_tex_w == w && g_tex_h == h) return true;
        drop_texture();

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

        if (FAILED(g_app.device->CreateTexture2D(&desc, 0, &g_surface)) || !g_surface) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC srv;
        ccfset(&srv, 0, sizeof(srv));
        srv.Format = desc.Format;
        srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = 1;

        if (FAILED(g_app.device->CreateShaderResourceView(g_surface, &srv, &g_view)) || !g_view)
        {
            drop_texture();
            return false;
        }

        g_tex_w = w;
        g_tex_h = h;
        log_line("watch: окно под картинку %dx%d", w, h);
        return true;
    }

    void upload(const unsigned char* rgba, int w, int h)
    {
        if (!ensure_texture(w, h) || !g_app.context) return;

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (FAILED(g_app.context->Map(g_surface, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;

        const unsigned int row = (unsigned int)w * 4;
        for (int y = 0; y < h; y++)
            ccpy((unsigned char*)mapped.pData + (unsigned int)y * mapped.RowPitch,
                 rgba + (size_t)y * row, row);

        g_app.context->Unmap(g_surface, 0);
    }

    // Largest rectangle of the picture's shape that fits the space given.
    ImVec2 fit(float pic_w, float pic_h, ImVec2 avail)
    {
        if (pic_w <= 0.0f || pic_h <= 0.0f || avail.x <= 0.0f || avail.y <= 0.0f)
            return ImVec2(0, 0);

        float scale = avail.x / pic_w;
        float other = avail.y / pic_h;
        if (other < scale) scale = other;

        return ImVec2(pic_w * scale, pic_h * scale);
    }
}

void ui_view_stream_window()
{
    streamview_state st = streamview::state();
    if (st == WATCH_IDLE)
    {
        // Nothing to show, and the texture is worth several megabytes.
        drop_texture();
        return;
    }

    // Whatever arrived since the last frame is taken even when the window ends
    // up collapsed: leaving it in the viewer would show a stale picture the
    // moment it is opened again.
    const unsigned char* rgba = 0;
    int fw = 0, fh = 0;
    if (streamview::take_frame(&rgba, &fw, &fh)) upload(rgba, fw, fh);

    char title[192];
    {
        store::guard guard;
        duser* u = store::find_user(streamview::watching_user());
        cnprint(title, sizeof(title), "Демонстрация — %s###stream",
                u ? u->display_name() : "участник");
    }

    bool open = true;
    ImGui::SetNextWindowSize(ImVec2(960, 580), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(320, 200), ImVec2(FLT_MAX, FLT_MAX));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    bool shown = ImGui::Begin(title, &open, ImGuiWindowFlags_NoScrollbar |
                                            ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    if (shown)
    {
        ImVec2 avail = ImGui::GetContentRegionAvail();

        if (g_view && g_tex_w > 0 && g_tex_h > 0)
        {
            // Black behind the picture, so the bars either side of a shape that
            // does not match the window do not show the window colour.
            ImVec2 origin = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddRectFilled(
                origin, ImVec2(origin.x + avail.x, origin.y + avail.y), IM_COL32(0, 0, 0, 255));

            ImVec2 size = fit((float)g_tex_w, (float)g_tex_h, avail);
            ImGui::SetCursorScreenPos(ImVec2(origin.x + (avail.x - size.x) * 0.5f,
                                             origin.y + (avail.y - size.y) * 0.5f));
            ImGui::Image((ImTextureID)g_view, size);

            // A stuck picture is the one thing a viewer can fix from here: it
            // asks the sender for a frame that can be decoded on its own.
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) streamview::request_keyframe();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Правый клик — попросить свежий кадр");

            // Sound, and a way to turn it off without leaving. Drawn over the
            // corner of the picture rather than in a bar of its own, so the
            // window stays all picture.
            {
                ImGui::SetCursorScreenPos(ImVec2(origin.x + 10.0f, origin.y + 10.0f));

                bool quiet = streamview::muted();
                ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 150));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(40, 42, 50, 220));
                ImGui::PushStyleColor(ImGuiCol_Text, quiet ? col::red : col::text_normal);

                if (ImGui::Button(quiet ? "Звук выкл" : "Звук вкл", ImVec2(96, 26)))
                    streamview::set_muted(!quiet);

                ImGui::PopStyleColor(3);

                if (ImGui::IsItemHovered())
                {
                    if (streamview::audio_packets())
                        ImGui::SetTooltip("Звук идёт со стрима");
                    else
                        ImGui::SetTooltip("Звука со стрима пока не было - "
                                          "возможно, стример его не передаёт");
                }
            }

            // A picture that decoded to nothing but black is indistinguishable
            // from a working one until the brightness is put next to it, and by
            // then the counters have gone: the window is showing a frame.
            streamview::stats s;
            streamview::read_stats(&s);

            char overlay[320];
            cnprint(overlay, sizeof(overlay),
                    "%ux%u | шаг %d | яркость %u | кадров %u из %u | собрано %u | "
                    "не расшифровано %u | выброшено %u | пропущено %u%s%s%s",
                    s.decoded_w, s.decoded_h, s.stride, s.luma,
                    s.frames, s.decoder_in, s.assembled, s.decrypt_fail, s.dropped, s.skipped,
                    s.waiting_for_idr ? " | ждём опорный кадр" : "",
                    s.decoder_error && s.decoder_error[0] ? " | декодер: " : "",
                    s.decoder_error ? s.decoder_error : "");

            ImGui::GetWindowDrawList()->AddText(ImVec2(origin.x + 8.0f, origin.y + 6.0f),
                                                s.luma == 0 ? col::red : col::text_muted, overlay);

            // Once a single frame decodes the window stops showing the waiting
            // screen, and with it every reason the rest of them did not.
            if (s.dave_error && s.dave_error[0])
            {
                char why[224];
                cnprint(why, sizeof(why), "DAVE: %s | эпоха %u, коммитов %u применено %u%s%s",
                        s.dave_error, s.epoch, s.commit_ops, s.commits_applied,
                        s.commit_error && s.commit_error[0] ? " | " : "",
                        s.commit_error ? s.commit_error : "");

                ImGui::GetWindowDrawList()->AddText(ImVec2(origin.x + 8.0f, origin.y + 22.0f),
                                                    col::red, why);
            }
        }
        else
        {
            const char* text = streamview::status_text();
            if (!text || !text[0]) text = "Подключаемся...";

            // The counters go on screen next to the status rather than only in
            // the log, because a run that cannot write its log file leaves no
            // other way to see which stage the picture is being lost at.
            streamview::stats s;
            streamview::read_stats(&s);

            char detail[512];
            detail[0] = 0;
            if (st == WATCH_WAITING)
            {
                int at = cnprint(detail, sizeof(detail),
                                 "пакетов %u, из них видео %u\n"
                                 "собрано кадров %u, декодировано %u\n"
                                 "выброшено %u, не расшифровано %u\n"
                                 "видео ssrc %u, e2ee %s",
                                 s.packets, s.video_packets, s.assembled, s.frames,
                                 s.dropped, s.decrypt_fail, s.video_ssrc,
                                 s.e2ee ? "да" : "нет");

                if (at > 0 && s.dave_error && s.dave_error[0])
                    at += cnprint(detail + at, sizeof(detail) - at, "\nDAVE: %s", s.dave_error);

                // Whether the frame that failed still looks like the one that
                // was protected, which is a different fault from a bad key.
                if (at > 0 && s.dave_detail && s.dave_detail[0])
                    at += cnprint(detail + at, sizeof(detail) - at, "\n%s", s.dave_detail);

                // The group's own state. Whether the epoch moves at all, and
                // what stopped it if it does not, is the difference between a
                // key from the wrong epoch and something else entirely.
                if (at > 0)
                    at += cnprint(detail + at, sizeof(detail) - at,
                                  "\nэпоха %u | op25 %u op27 %u op29 %u op30 %u",
                                  s.epoch, s.external_sender_ops, s.proposal_ops,
                                  s.commit_ops, s.welcome_ops);

                if (at > 0 && s.frames)
                    at += cnprint(detail + at, sizeof(detail) - at,
                                  "\nкартинка %ux%u, шаг %d, яркость %u",
                                  s.decoded_w, s.decoded_h, s.stride, s.luma);

                if (at > 0 && s.commit_error && s.commit_error[0])
                    at += cnprint(detail + at, sizeof(detail) - at,
                                  "\nкоммиты: применено %u, последний — %s",
                                  s.commits_applied, s.commit_error);
            }

            ImVec2 text_size = ImGui::CalcTextSize(text);
            ImVec2 detail_size = detail[0] ? ImGui::CalcTextSize(detail) : ImVec2(0, 0);
            float block = text_size.y + (detail[0] ? detail_size.y + 10.0f : 0.0f);

            ImVec2 origin = ImGui::GetCursorScreenPos();
            float top = origin.y + (avail.y - block) * 0.5f;

            ImGui::SetCursorScreenPos(ImVec2(origin.x + (avail.x - text_size.x) * 0.5f, top));
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  st == WATCH_FAILED ? col::red : col::text_normal);
            ImGui::TextUnformatted(text);
            ImGui::PopStyleColor();

            if (detail[0])
            {
                ImGui::SetCursorScreenPos(ImVec2(origin.x + (avail.x - detail_size.x) * 0.5f,
                                                 top + text_size.y + 10.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, col::text_muted);
                ImGui::TextUnformatted(detail);
                ImGui::PopStyleColor();
            }
        }
    }

    ImGui::End();

    if (!open) streamview::stop();
}
