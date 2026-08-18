#include "pch.h"
#include <d3d11.h>

#include "ui_state.h"
#include "theme.h"

#include "core/app.h"
#include "core/gfxload.h"
#include "core/log.h"
#include "discord/store.h"
#include "video/streamview.h"
#include "discord/voice.h"

// The window somebody else's screen appears in.
//
// The viewer hands over finished pictures as RGBA and knows nothing about the
// renderer; everything here is the other half of that: one dynamic texture that
// is rewritten in place every frame, and enough chrome to tell a stream that is
// still connecting from one that has stopped.

namespace
{
    // ---- NV12 to RGBA, on the card ---------------------------------------
    //
    // The decoder hands over the planes it produced. Turning those into colour
    // used to be a pass over every pixel on the processor, and then four bytes
    // a pixel copied to the card; now the two planes go up as they are - one
    // and a half bytes a pixel - and the conversion happens where sampling
    // already happens, which costs nothing measurable.
    //
    // Rendered once per picture into an ordinary RGBA target, rather than
    // hooked into imgui's own drawing. Imgui would have to be persuaded to
    // swap shaders mid-list and put everything back afterwards; a target it
    // can sample like any other texture needs no such cooperation.
    struct nv12_pipeline
    {
        ID3D11VertexShader* vs;
        ID3D11PixelShader* ps;
        ID3D11SamplerState* sampler;
        bool tried;
    };

    nv12_pipeline g_nv12 = { 0, 0, 0, false };

    const char* NV12_SHADER =
        "Texture2D<float>  luma   : register(t0);\n"
        "Texture2D<float2> chroma : register(t1);\n"
        "SamplerState samp        : register(s0);\n"
        "\n"
        "// A single triangle covering the target, built from the vertex index.\n"
        "// A quad would need a vertex buffer and an index buffer to say the\n"
        "// same thing, and the seam down a quad's diagonal is a real artefact.\n"
        "void vsmain(uint id : SV_VertexID,\n"
        "            out float4 pos : SV_Position, out float2 uv : TEXCOORD0)\n"
        "{\n"
        "    uv  = float2((id << 1) & 2, id & 2);\n"
        "    pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);\n"
        "}\n"
        "\n"
        "float4 psmain(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target\n"
        "{\n"
        "    // BT.601 limited range, the same coefficients the processor side\n"
        "    // used: luma runs 16 to 235 and chroma is centred on 128.\n"
        "    float  y  = luma.Sample(samp, uv) - 0.0627451;\n"
        "    float2 cc = chroma.Sample(samp, uv) - 0.5019608;\n"
        "\n"
        "    float3 rgb;\n"
        "    rgb.r = 1.164383 * y + 1.596027 * cc.y;\n"
        "    rgb.g = 1.164383 * y - 0.391762 * cc.x - 0.812968 * cc.y;\n"
        "    rgb.b = 1.164383 * y + 2.017232 * cc.x;\n"
        "    return float4(saturate(rgb), 1);\n"
        "}\n";

    bool build_nv12_pipeline()
    {
        if (g_nv12.vs && g_nv12.ps) return true;
        if (g_nv12.tried) return false;
        g_nv12.tried = true;

        pfn_D3DCompile compile = gfx::compile();
        if (!compile || !g_app.device)
        {
            log_line("watch: нет компилятора шейдеров, цвет считается на процессоре");
            return false;
        }

        ID3DBlob* vs_code = 0;
        ID3DBlob* ps_code = 0;
        ID3DBlob* errors = 0;

        unsigned int len = (unsigned int)ccslenf(NV12_SHADER);
        bool ok = SUCCEEDED(compile(NV12_SHADER, len, 0, 0, 0, "vsmain", "vs_4_0",
                                    0, 0, &vs_code, &errors)) && vs_code;
        if (errors) { errors->Release(); errors = 0; }

        if (ok)
        {
            ok = SUCCEEDED(compile(NV12_SHADER, len, 0, 0, 0, "psmain", "ps_4_0",
                                   0, 0, &ps_code, &errors)) && ps_code;
            if (errors) { errors->Release(); errors = 0; }
        }

        if (ok)
            ok = SUCCEEDED(g_app.device->CreateVertexShader(
                     vs_code->GetBufferPointer(), vs_code->GetBufferSize(), 0, &g_nv12.vs));
        if (ok)
            ok = SUCCEEDED(g_app.device->CreatePixelShader(
                     ps_code->GetBufferPointer(), ps_code->GetBufferSize(), 0, &g_nv12.ps));

        if (vs_code) vs_code->Release();
        if (ps_code) ps_code->Release();

        if (ok)
        {
            // Linear, so the half resolution chroma is interpolated rather than
            // blocked up - which is what the processor version did by repeating
            // each pair across two pixels, only smoother.
            D3D11_SAMPLER_DESC sd;
            ccfset(&sd, 0, sizeof(sd));
            sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
            sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
            sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            ok = SUCCEEDED(g_app.device->CreateSamplerState(&sd, &g_nv12.sampler));
        }

        if (!ok)
        {
            if (g_nv12.vs) { g_nv12.vs->Release(); g_nv12.vs = 0; }
            if (g_nv12.ps) { g_nv12.ps->Release(); g_nv12.ps = 0; }
            if (g_nv12.sampler) { g_nv12.sampler->Release(); g_nv12.sampler = 0; }
            log_line("watch: шейдер NV12 не собрался");
            return false;
        }

        log_line("watch: цвет считается на видеокарте");
        return true;
    }

    ID3D11Texture2D* g_surface = 0;
    ID3D11ShaderResourceView* g_view = 0;
    int g_tex_w = 0;
    int g_tex_h = 0;

    // The two planes going up, and the target the shader writes.
    ID3D11Texture2D* g_luma = 0;
    ID3D11ShaderResourceView* g_luma_view = 0;
    ID3D11Texture2D* g_chroma = 0;
    ID3D11ShaderResourceView* g_chroma_view = 0;
    ID3D11RenderTargetView* g_rtv = 0;

    void drop_texture()
    {
        if (g_rtv) { g_rtv->Release(); g_rtv = 0; }
        if (g_luma_view) { g_luma_view->Release(); g_luma_view = 0; }
        if (g_luma) { g_luma->Release(); g_luma = 0; }
        if (g_chroma_view) { g_chroma_view->Release(); g_chroma_view = 0; }
        if (g_chroma) { g_chroma->Release(); g_chroma = 0; }
        if (g_view) { g_view->Release(); g_view = 0; }
        if (g_surface) { g_surface->Release(); g_surface = 0; }
        g_tex_w = 0;
        g_tex_h = 0;
    }

    // A dynamic single channel plane the processor writes and the shader reads.
    bool make_plane(int w, int h, DXGI_FORMAT format,
                    ID3D11Texture2D** tex, ID3D11ShaderResourceView** srv)
    {
        D3D11_TEXTURE2D_DESC desc;
        ccfset(&desc, 0, sizeof(desc));
        desc.Width = (UINT)w;
        desc.Height = (UINT)h;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        if (FAILED(g_app.device->CreateTexture2D(&desc, 0, tex)) || !*tex) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC sd;
        ccfset(&sd, 0, sizeof(sd));
        sd.Format = format;
        sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sd.Texture2D.MipLevels = 1;

        return SUCCEEDED(g_app.device->CreateShaderResourceView(*tex, &sd, srv)) && *srv;
    }

    // Copies one plane up. The card chooses its own row pitch, so this cannot
    // be one block copy however tempting it looks.
    void write_plane(ID3D11Texture2D* tex, const unsigned char* src, int w, int h, int bytes)
    {
        D3D11_MAPPED_SUBRESOURCE m;
        if (FAILED(g_app.context->Map(tex, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;

        const unsigned int row = (unsigned int)(w * bytes);
        for (int y = 0; y < h; y++)
            ccpy((unsigned char*)m.pData + (size_t)y * m.RowPitch,
                 src + (size_t)y * row, row);

        g_app.context->Unmap(tex, 0);
    }

    // The two source planes plus the colour target the shader writes and imgui
    // then samples like any other picture.
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
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

        if (FAILED(g_app.device->CreateTexture2D(&desc, 0, &g_surface)) || !g_surface) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC srv;
        ccfset(&srv, 0, sizeof(srv));
        srv.Format = desc.Format;
        srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = 1;

        bool ok = SUCCEEDED(g_app.device->CreateShaderResourceView(g_surface, &srv, &g_view)) && g_view;

        if (ok) ok = SUCCEEDED(g_app.device->CreateRenderTargetView(g_surface, 0, &g_rtv)) && g_rtv;

        // Chroma is stored at half resolution in both directions, two channels
        // interleaved - which is exactly one R8G8 texture of half the size.
        if (ok) ok = make_plane(w, h, DXGI_FORMAT_R8_UNORM, &g_luma, &g_luma_view);
        if (ok) ok = make_plane(w / 2, h / 2, DXGI_FORMAT_R8G8_UNORM, &g_chroma, &g_chroma_view);

        if (!ok)
        {
            drop_texture();
            return false;
        }

        g_tex_w = w;
        g_tex_h = h;
        log_line("watch: окно под картинку %dx%d", w, h);
        return true;
    }

    void upload(const unsigned char* nv12, int w, int h)
    {
        if (!build_nv12_pipeline()) return;
        if (!ensure_texture(w, h) || !g_app.context) return;

        write_plane(g_luma, nv12, w, h, 1);
        write_plane(g_chroma, nv12 + (size_t)w * h, w / 2, h / 2, 2);

        ID3D11DeviceContext* ctx = g_app.context;

        // Imgui rebuilds the whole pipeline for its own drawing every frame, so
        // what is set here does not have to be put back - only the render
        // target does, because the frame is still being drawn into it.
        ID3D11RenderTargetView* saved_rtv = 0;
        ID3D11DepthStencilView* saved_dsv = 0;
        ctx->OMGetRenderTargets(1, &saved_rtv, &saved_dsv);

        D3D11_VIEWPORT vp;
        ccfset(&vp, 0, sizeof(vp));
        vp.Width = (float)w;
        vp.Height = (float)h;
        vp.MaxDepth = 1.0f;

        ID3D11ShaderResourceView* views[2] = { g_luma_view, g_chroma_view };

        ctx->OMSetRenderTargets(1, &g_rtv, 0);
        ctx->RSSetViewports(1, &vp);
        ctx->IASetInputLayout(0);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(g_nv12.vs, 0, 0);
        ctx->PSSetShader(g_nv12.ps, 0, 0);
        ctx->PSSetShaderResources(0, 2, views);
        ctx->PSSetSamplers(0, 1, &g_nv12.sampler);
        ctx->OMSetBlendState(0, 0, 0xFFFFFFFF);
        ctx->Draw(3, 0);

        // Unbound before the target is restored: leaving a texture bound that
        // is about to be drawn into makes the runtime drop one of the two, and
        // which one it drops is not something to rely on.
        ID3D11ShaderResourceView* none[2] = { 0, 0 };
        ctx->PSSetShaderResources(0, 2, none);

        ctx->OMSetRenderTargets(1, &saved_rtv, saved_dsv);
        if (saved_rtv) saved_rtv->Release();
        if (saved_dsv) saved_dsv->Release();
    }

    // The camera gets a surface of its own rather than sharing the one above.
    // Both windows can be open at once as far as the renderer is concerned, and
    // a shared texture would have them overwrite each other's picture every
    // frame - the decoder is what limits this to one at a time, not this.
    ID3D11Texture2D* g_cam_surface = 0;
    ID3D11ShaderResourceView* g_cam_view = 0;
    int g_cam_w = 0;
    int g_cam_h = 0;

    void drop_camera_texture()
    {
        if (g_cam_view) { g_cam_view->Release(); g_cam_view = 0; }
        if (g_cam_surface) { g_cam_surface->Release(); g_cam_surface = 0; }
        g_cam_w = 0;
        g_cam_h = 0;
    }

    bool ensure_camera_texture(int w, int h)
    {
        if (g_cam_view && g_cam_w == w && g_cam_h == h) return true;
        drop_camera_texture();

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

        if (FAILED(g_app.device->CreateTexture2D(&desc, 0, &g_cam_surface)) || !g_cam_surface)
            return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC srv;
        ccfset(&srv, 0, sizeof(srv));
        srv.Format = desc.Format;
        srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = 1;

        if (FAILED(g_app.device->CreateShaderResourceView(g_cam_surface, &srv, &g_cam_view)) ||
            !g_cam_view)
        {
            drop_camera_texture();
            return false;
        }

        g_cam_w = w;
        g_cam_h = h;
        log_line("camera: окно под картинку %dx%d", w, h);
        return true;
    }

    void upload_camera(const unsigned char* rgba, int w, int h)
    {
        if (!ensure_camera_texture(w, h) || !g_app.context) return;

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (FAILED(g_app.context->Map(g_cam_surface, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            return;

        const unsigned int row = (unsigned int)w * 4;
        for (int y = 0; y < h; y++)
            ccpy((unsigned char*)mapped.pData + (unsigned int)y * mapped.RowPitch,
                 rgba + (size_t)y * row, row);

        g_app.context->Unmap(g_cam_surface, 0);
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

void ui_view_camera_window()
{
    snowflake who = voice::watched_camera();
    if (!who)
    {
        drop_camera_texture();
        return;
    }

    // Taken whether or not the window is collapsed: left in the decoder it
    // would appear as a stale picture the moment it is opened again.
    const unsigned char* rgba = 0;
    int fw = 0, fh = 0;
    if (voice::take_camera_frame(&rgba, &fw, &fh)) upload_camera(rgba, fw, fh);

    char title[192];
    {
        store::guard guard;
        duser* u = store::find_user(who);
        cnprint(title, sizeof(title), tr("Камера — %s###camera"),
                u ? u->display_name() : tr("участник"));
    }

    bool open = true;
    ImGui::SetNextWindowSize(ImVec2(640, 480), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(240, 160), ImVec2(FLT_MAX, FLT_MAX));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    bool shown = ImGui::Begin(title, &open, ImGuiWindowFlags_NoScrollbar |
                                            ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    if (shown)
    {
        ImVec2 avail = ImGui::GetContentRegionAvail();

        if (g_cam_view && g_cam_w > 0 && g_cam_h > 0)
        {
            ImVec2 origin = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddRectFilled(
                origin, ImVec2(origin.x + avail.x, origin.y + avail.y), IM_COL32(0, 0, 0, 255));

            ImVec2 size = fit((float)g_cam_w, (float)g_cam_h, avail);
            ImGui::SetCursorScreenPos(ImVec2(origin.x + (avail.x - size.x) * 0.5f,
                                             origin.y + (avail.y - size.y) * 0.5f));
            ImGui::Image((ImTextureID)g_cam_view, size);
        }
        else
        {
            // A camera sends nothing until it has a picture worth sending, and
            // the first self contained frame can be a second or two coming.
            ImGui::Dummy(ImVec2(0, avail.y * 0.45f));
            float text_w = ImGui::CalcTextSize(tr("Жду картинку...")).x;
            ImGui::SetCursorPosX((avail.x - text_w) * 0.5f);
            ui_text_muted(tr("Жду картинку..."));
        }
    }

    ImGui::End();

    if (!open) voice::watch_camera(0);
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
        cnprint(title, sizeof(title), tr("Демонстрация — %s###stream"),
                u ? u->display_name() : tr("участник"));
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
                ImGui::SetTooltip(tr("Правый клик — попросить свежий кадр"));

            // Sound, and a way to turn it off without leaving. Drawn over the
            // corner of the picture rather than in a bar of its own, so the
            // window stays all picture.
            {
                ImGui::SetCursorScreenPos(ImVec2(origin.x + 10.0f, origin.y + 10.0f));

                bool quiet = streamview::muted();
                ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 150));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(40, 42, 50, 220));
                ImGui::PushStyleColor(ImGuiCol_Text, quiet ? col::red : col::text_normal);

                if (ImGui::Button(quiet ? tr("Звук выкл") : tr("Звук вкл"), ImVec2(96, 26)))
                    streamview::set_muted(!quiet);

                ImGui::PopStyleColor(3);

                if (ImGui::IsItemHovered())
                {
                    if (streamview::audio_packets())
                        ImGui::SetTooltip(tr("Звук идёт со стрима"));
                    else
                        ImGui::SetTooltip(tr("Звука со стрима пока не было - "
                                          "возможно, стример его не передаёт"));
                }
            }

            // A picture that decoded to nothing but black is indistinguishable
            // from a working one until the brightness is put next to it, and by
            // then the counters have gone: the window is showing a frame.
            streamview::stats s;
            streamview::read_stats(&s);

            char overlay[320];
            cnprint(overlay, sizeof(overlay),
                    tr("%ux%u | шаг %d | яркость %u | кадров %u из %u | собрано %u | "
                    "не расшифровано %u | выброшено %u | пропущено %u%s%s%s"),
                    s.decoded_w, s.decoded_h, s.stride, s.luma,
                    s.frames, s.decoder_in, s.assembled, s.decrypt_fail, s.dropped, s.skipped,
                    s.waiting_for_idr ? tr(" | ждём опорный кадр") : "",
                    s.decoder_error && s.decoder_error[0] ? tr(" | декодер: ") : "",
                    s.decoder_error ? s.decoder_error : "");

            ImGui::GetWindowDrawList()->AddText(ImVec2(origin.x + 8.0f, origin.y + 6.0f),
                                                s.luma == 0 ? col::red : col::text_muted, overlay);

            // Once a single frame decodes the window stops showing the waiting
            // screen, and with it every reason the rest of them did not.
            if (s.dave_error && s.dave_error[0])
            {
                char why[224];
                cnprint(why, sizeof(why), tr("DAVE: %s | эпоха %u, коммитов %u применено %u%s%s"),
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
            if (!text || !text[0]) text = tr("Подключаемся...");

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
                                 tr("пакетов %u, из них видео %u\n"
                                 "собрано кадров %u, декодировано %u\n"
                                 "выброшено %u, не расшифровано %u\n"
                                 "видео ssrc %u, e2ee %s"),
                                 s.packets, s.video_packets, s.assembled, s.frames,
                                 s.dropped, s.decrypt_fail, s.video_ssrc,
                                 s.e2ee ? tr("да") : tr("нет"));

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
                                  tr("\nэпоха %u | op25 %u op27 %u op29 %u op30 %u"),
                                  s.epoch, s.external_sender_ops, s.proposal_ops,
                                  s.commit_ops, s.welcome_ops);

                if (at > 0 && s.frames)
                    at += cnprint(detail + at, sizeof(detail) - at,
                                  tr("\nкартинка %ux%u, шаг %d, яркость %u"),
                                  s.decoded_w, s.decoded_h, s.stride, s.luma);

                if (at > 0 && s.commit_error && s.commit_error[0])
                    at += cnprint(detail + at, sizeof(detail) - at,
                                  tr("\nкоммиты: применено %u, последний — %s"),
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
