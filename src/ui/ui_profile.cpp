#include "pch.h"
#include <d3d11.h>
#include "ui_state.h"
#include "theme.h"
#include "textures.h"
#include "core/app.h"
#include "core/log.h"
#include "core/crypto.h"
#include "core/offline.h"
#include "system/io/ufile.h"
#include "discord/store.h"
#include "discord/rest.h"
#include "discord/cdnfix.h"
#include "stb/stb_image.h"
#include "stb_image_write.h"

// Only in the implementation part of the header, so no prototype reaches
// this unit the normal way. Implemented once in stb_impl.obj, with C
// linkage like the rest of stb.
extern "C" unsigned char* stbi_write_png_to_mem(const unsigned char* pixels, int stride_bytes,
                                                int x, int y, int n, int* out_len);

namespace
{
    // ---- editor state ----
    // Everything is staged locally: the requests only go out on Apply,
    // display name + avatar together, username with its password apart.
    bool g_ed_open = false;
    bool g_ed_want_open = false;
    char g_ed_name[64];
    char g_ed_username[64];
    char g_ed_bio[512];
    char g_ed_pronouns[64];
    char g_ed_pass[256];
    char g_ed_manual_id[32];

    // Avatar staging: 0 current, 1 cropped file, 2 history id, 3 typed id.
    int g_av_mode = 0;
    char g_av_hist_id[32];

    // Banner staging: 0 unchanged, 1 cropped file, 2 remove.
    int g_bn_mode = 0;
    unsigned char* g_bn_final = 0;
    ID3D11ShaderResourceView* g_bn_preview = 0;
    char* g_bn_datauri = 0;

    unsigned char* g_av_src = 0;
    int g_av_w = 0, g_av_h = 0;
    ID3D11ShaderResourceView* g_av_tex = 0;

    unsigned char* g_av_final = 0;   // cropped result
    ID3D11ShaderResourceView* g_av_preview = 0;
    char* g_av_datauri = 0;

    bool g_hist_requested = false;
    api::avatar_item g_hist[24];
    int g_hist_n = 0;

    bool g_name_probe = false;       // pomelo answer waiting/known
    bool g_name_taken = false;
    char g_name_probe_err[128];

    bool g_busy_profile = false;
    bool g_busy_account = false;
    bool g_pend_username = false;
    bool g_sent_any = false;
    char g_err_profile[256];
    char g_err_account[256];

    // ---- crop overlay ----
    enum { CROP_AVATAR = 0, CROP_BANNER = 1 };
    const float BANNER_ASPECT = 2.5f;   // 600x240

    bool g_cropping = false;
    bool g_crop_want_open = false;
    int g_crop_kind = CROP_AVATAR;
    // The editor popup is deliberately closed while the crop runs: it is
    // the popup below, and anything closing it takes popups above it down
    // as well. It reopens when the crop ends, state intact.
    bool g_ed_suspended = false;
    bool g_crop_init = false;
    ImVec2 g_crop_pos;
    float g_crop_w = 0.0f;
    float g_crop_h = 0.0f;
    int g_crop_handle = -1;          // 0..3 corners, 4 move
    ImVec2 g_img_pos;
    ImVec2 g_img_size;
    float g_img_scale = 1.0f;

    float clampf(float v, float lo, float hi)
    {
        if (v < lo) return lo;
        if (v > hi) return hi;
        return v;
    }

    ID3D11ShaderResourceView* make_rgba_texture(int w, int h, const unsigned char* rgba)
    {
        if (!g_app.device || !rgba || w <= 0 || h <= 0) return 0;

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

        D3D11_SUBRESOURCE_DATA init;
        ccfset(&init, 0, sizeof(init));
        init.pSysMem = rgba;
        init.SysMemPitch = (UINT)(w * 4);

        ID3D11Texture2D* tex = 0;
        if (FAILED(g_app.device->CreateTexture2D(&desc, &init, &tex)) || !tex)
            return 0;

        ID3D11ShaderResourceView* view = 0;
        if (FAILED(g_app.device->CreateShaderResourceView(tex, 0, &view)))
        {
            tex->Release();
            return 0;
        }
        tex->Release();
        return view;
    }

    void drop_texture(ID3D11ShaderResourceView** view)
    {
        if (view && *view)
        {
            (*view)->Release();
            *view = 0;
        }
    }

    void free_avatar_source()
    {
        if (g_av_src) { stbi_image_free(g_av_src); g_av_src = 0; }
        g_av_w = g_av_h = 0;
        drop_texture(&g_av_tex);
    }

    void free_avatar_staged()
    {
        if (g_av_final) { memfree(g_av_final); g_av_final = 0; }
        drop_texture(&g_av_preview);
        if (g_av_datauri) { memfree(g_av_datauri); g_av_datauri = 0; }
        g_av_mode = 0;
        g_av_hist_id[0] = 0;
    }

    void free_banner_staged()
    {
        if (g_bn_final) { memfree(g_bn_final); g_bn_final = 0; }
        drop_texture(&g_bn_preview);
        if (g_bn_datauri) { memfree(g_bn_datauri); g_bn_datauri = 0; }
        g_bn_mode = 0;
    }

    bool pick_crop_file(int kind)
    {
        wchar_t path[MAX_PATH];
        path[0] = 0;
        if (!ufile::open_dialog(path, MAX_PATH)) return false;

        ubuffer blob;
        blob.init();
        if (!ufile::read_all(path, &blob) || !blob.size || blob.size > 15 * 1024 * 1024)
        {
            blob.free_buffer();
            return false;
        }

        int w = 0, h = 0, comp = 0;
        unsigned char* px = stbi_load_from_memory(blob.data, (int)blob.size,
                                                  &w, &h, &comp, 4);
        blob.free_buffer();
        if (!px || w <= 0 || h <= 0 || w > 8000 || h > 8000)
        {
            log_line("profile: файл не декодировался (%dx%d)", w, h);
            if (px) stbi_image_free(px);
            return false;
        }

        free_avatar_source();

        g_av_src = px;
        g_av_w = w;
        g_av_h = h;
        g_av_tex = make_rgba_texture(w, h, px);
        if (!g_av_tex)
        {
            log_line("profile: файл %dx%d не встал в текстуру", w, h);
            free_avatar_source();
            return false;
        }

        log_line("profile: файл взят %dx%d, кроп открыт", w, h);
        g_crop_kind = kind;
        g_cropping = true;
        g_crop_init = false;
        g_crop_handle = -1;
        g_ed_suspended = true;
        // The editor popup closes here, inside its own Begin: while the
        // crop runs there is no popup below it left to cascade from.
        ImGui::CloseCurrentPopup();
        // Opened in the frame scope, where BeginPopupModal below runs too:
        // a popup addressed from a foreign ID stack is never found.
        g_crop_want_open = true;
        return true;
    }

    bool pick_avatar_file()
    {
        bool picked = pick_crop_file(CROP_AVATAR);
        if (picked) free_avatar_staged();
        return picked;
    }

    bool pick_banner_file()
    {
        bool picked = pick_crop_file(CROP_BANNER);
        if (picked) free_banner_staged();
        return picked;
    }

    bool apply_crop(int sx, int sy, int sw, int sh)
    {
        if (!g_av_src || sw <= 0 || sh <= 0) return false;
        if (sx < 0) sx = 0;
        if (sy < 0) sy = 0;
        if (sx >= g_av_w || sy >= g_av_h) return false;
        if (sx + sw > g_av_w) sw = g_av_w - sx;
        if (sy + sh > g_av_h) sh = g_av_h - sy;
        if (sw <= 0 || sh <= 0) return false;

        int target_w, target_h;
        if (g_crop_kind == CROP_BANNER)
        {
            target_w = sw > 1200 ? 1200 : sw;
            target_h = target_w * 2 / 5;
            if (target_h < 1) target_h = 1;
            if (target_h > sh)
            {
                target_h = sh;
                target_w = target_h * 5 / 2;
            }
            if (target_w < 1) target_w = 1;
        }
        else
        {
            target_w = target_h = sw < sh ? sw : sh;
            if (target_w > 1024) target_w = target_h = 1024;
            if (target_w < 1) target_w = 1;
        }

        unsigned char* out = (unsigned char*)memalloc(target_w * target_h * 4);
        if (!out) return false;

        float scale_x = (float)sw / (float)target_w;
        float scale_y = (float)sh / (float)target_h;
        for (int ty = 0; ty < target_h; ty++)
        {
            for (int tx = 0; tx < target_w; tx++)
            {
                int sy0 = (int)(ty * scale_y);
                int sy1 = (int)((ty + 1) * scale_y);
                int sx0 = (int)(tx * scale_x);
                int sx1 = (int)((tx + 1) * scale_x);

                float r = 0, g = 0, b = 0, a = 0, n = 0;
                for (int y = sy0; y <= sy1; y++)
                {
                    for (int x = sx0; x <= sx1; x++)
                    {
                        if (x < 0 || y < 0 || x >= sw || y >= sh) continue;
                        int idx = (((sy + y) * g_av_w) + (sx + x)) * 4;
                        r += g_av_src[idx];
                        g += g_av_src[idx + 1];
                        b += g_av_src[idx + 2];
                        a += g_av_src[idx + 3];
                        n += 1.0f;
                    }
                }
                if (n <= 0.0f) n = 1.0f;

                int o = (ty * target_w + tx) * 4;
                out[o] = (unsigned char)(r / n);
                out[o + 1] = (unsigned char)(g / n);
                out[o + 2] = (unsigned char)(b / n);
                out[o + 3] = (unsigned char)(a / n);
            }
        }

        int png_len = 0;
        unsigned char* png = stbi_write_png_to_mem(out, target_w * 4, target_w, target_h,
                                                   4, &png_len);
        if (!png || png_len <= 0)
        {
            if (png) memfree(png);
            memfree(out);
            return false;
        }

        ubuffer b64;
        b64.init();
        crypto::base64_encode(png, (unsigned int)png_len, &b64);

        const char* prefix = "data:image/png;base64,";
        unsigned int total = (unsigned int)ccslenf(prefix) + b64.size;
        char* uri = (char*)memalloc((int)total + 1);
        if (!uri)
        {
            memfree(png);
            b64.free_buffer();
            memfree(out);
            return false;
        }
        ccpy(uri, prefix, ccslenf(prefix) + 1);
        ccpy(uri + ccslenf(prefix), b64.data, b64.size + 1);

        ID3D11ShaderResourceView* preview = make_rgba_texture(target_w, target_h, out);
        if (!preview)
        {
            memfree(uri);
            memfree(png);
            b64.free_buffer();
            memfree(out);
            return false;
        }

        if (g_crop_kind == CROP_BANNER)
        {
            free_banner_staged();
            g_bn_final = out;
            g_bn_preview = preview;
            g_bn_datauri = uri;
            g_bn_mode = 1;
        }
        else
        {
            free_avatar_staged();
            g_av_final = out;
            g_av_preview = preview;
            g_av_datauri = uri;
            g_av_mode = 1;
        }

        memfree(png);
        b64.free_buffer();
        return true;
    }

    void close_editor()
    {
        g_ed_open = false;
        g_cropping = false;
        g_crop_want_open = false;
        g_ed_suspended = false;
        ccfset(g_ed_pass, 0, sizeof(g_ed_pass));
        free_avatar_source();
        free_avatar_staged();
        free_banner_staged();
    }

    const char* nonempty_or_null(const char* s)
    {
        return (s && s[0]) ? s : 0;
    }

    bool same_text(const char* a, const char* b)
    {
        if (!a) a = "";
        if (!b) b = "";
        return ccscmp(a, b) == 0;
    }
}

void ui_open_profile_editor()
{
    store::guard g;
    duser* me = store::self();
    if (!me) return;

    ccfset(g_ed_name, 0, sizeof(g_ed_name));
    ccfset(g_ed_username, 0, sizeof(g_ed_username));
    ccfset(g_ed_bio, 0, sizeof(g_ed_bio));
    ccfset(g_ed_pronouns, 0, sizeof(g_ed_pronouns));
    ccfset(g_ed_pass, 0, sizeof(g_ed_pass));
    ccfset(g_ed_manual_id, 0, sizeof(g_ed_manual_id));

    if (me->global_name) ccstrncpy(g_ed_name, me->global_name, sizeof(g_ed_name) - 1);
    if (me->username) ccstrncpy(g_ed_username, me->username, sizeof(g_ed_username) - 1);
    if (me->bio) ccstrncpy(g_ed_bio, me->bio, sizeof(g_ed_bio) - 1);
    if (me->pronouns) ccstrncpy(g_ed_pronouns, me->pronouns, sizeof(g_ed_pronouns) - 1);

    free_avatar_source();
    free_avatar_staged();
    free_banner_staged();
    g_crop_kind = CROP_AVATAR;
    g_hist_n = 0;
    g_hist_requested = false;
    g_ed_suspended = false;
    g_name_probe = false;
    g_name_taken = false;
    g_name_probe_err[0] = 0;
    g_busy_profile = g_busy_account = false;
    g_pend_username = false;
    g_sent_any = false;
    g_err_profile[0] = 0;
    g_err_account[0] = 0;
    ccfset(g_ed_pass, 0, sizeof(g_ed_pass));

    // Drain stale outcomes from an abandoned run.
    {
        bool drop_ok = false;
        char drop[256];
        drop[0] = 0;
        api::take_profile_result(&drop_ok, drop, sizeof(drop));
        api::take_account_result(&drop_ok, drop, sizeof(drop));
        api::username_check drop_chk;
        ccfset(&drop_chk, 0, sizeof(drop_chk));
        api::take_username_check(&drop_chk);
    }

    g_ed_want_open = true;
    g_ed_open = true;
}

void ui_view_avatar_crop()
{
    // Frame diagnostics, throttled: if the overlay ever stops reaching
    // the draw below, the log shows the last state it had.
    static int diag_frames = 0;
    static bool diag_was = false;
    static bool shown_logged = false;

    if (!g_cropping)
    {
        diag_frames = 0;
        diag_was = false;
        shown_logged = false;
        g_crop_want_open = false;
        return;
    }

    if (g_crop_want_open)
    {
        ImGui::OpenPopup("##cropper");
        g_crop_want_open = false;
        log_line("profile: OpenPopup, сразу IsOpen=%d", ImGui::IsPopupOpen("##cropper") ? 1 : 0);
    }

    // A modal fullscreen popup: always on top with input captured, so the
    // main window can neither bury it nor be clicked through to. Dismissed
    // from the outside (Esc): same as Cancel, back to the editor.
    if (!ImGui::IsPopupOpen("##cropper"))
    {
        log_line("profile: кроп закрыт снаружи");
        free_avatar_source();
        g_cropping = false;
        g_ed_suspended = false;
        g_ed_want_open = true;
        g_ed_open = true;
        return;
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(vp->WorkSize, ImGuiCond_Always);
    if (!g_av_tex || !g_av_src || g_av_w <= 0 || g_av_h <= 0)
    {
        free_avatar_source();
        g_cropping = false;
        g_ed_suspended = false;
        g_ed_want_open = true;
        g_ed_open = true;
        return;
    }

    // Frame diagnostics, throttled: if the overlay ever stops reaching
    // the draw below, the log shows the last state it had.
    if (!diag_was)
    {
        log_line("profile: кроп рисуется");
        diag_frames = 0;
    }
    diag_was = true;
    if (++diag_frames % 60 == 0)
        log_line("profile: кроп жив, кадр %d", diag_frames);

    if (!ImGui::BeginPopupModal("##cropper", 0,
                                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
    {
        if (diag_frames % 60 == 0)
            log_line("profile: кроппер не открылся, кадр %d", diag_frames);
        return;
    }
    else
    {
        if (!shown_logged)
        {
            shown_logged = true;
            log_line("profile: кроппер показан");
        }
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();

    float max_area = 550.0f;
    float scale = max_area / (float)g_av_w;
    {
        float s2 = max_area / (float)g_av_h;
        if (s2 < scale) scale = s2;
    }
    g_img_scale = scale;
    g_img_size = ImVec2((float)g_av_w * scale, (float)g_av_h * scale);
    g_img_pos = ImVec2(vp->WorkPos.x + (vp->WorkSize.x - g_img_size.x) * 0.5f,
                       vp->WorkPos.y + (vp->WorkSize.y - g_img_size.y) * 0.5f - 20.0f);

    dl->AddImage((ImTextureID)g_av_tex, g_img_pos,
                 ImVec2(g_img_pos.x + g_img_size.x, g_img_pos.y + g_img_size.y));

    if (!g_crop_init)
    {
        float aspect = g_crop_kind == CROP_BANNER ? BANNER_ASPECT : 1.0f;
        float cw = g_img_size.x;
        float ch = cw / aspect;
        if (ch > g_img_size.y)
        {
            ch = g_img_size.y;
            cw = ch * aspect;
        }
        g_crop_w = cw;
        g_crop_h = ch;
        g_crop_pos = ImVec2(g_img_pos.x + (g_img_size.x - cw) * 0.5f,
                            g_img_pos.y + (g_img_size.y - ch) * 0.5f);
        g_crop_init = true;
    }

    ImVec2 p[4] =
    {
        g_crop_pos,
        ImVec2(g_crop_pos.x + g_crop_w, g_crop_pos.y),
        ImVec2(g_crop_pos.x, g_crop_pos.y + g_crop_h),
        ImVec2(g_crop_pos.x + g_crop_w, g_crop_pos.y + g_crop_h),
    };

    ImVec2 mouse = ImGui::GetMousePos();
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        g_crop_handle = -1;
        for (int i = 0; i < 4; i++)
        {
            float dx = mouse.x - p[i].x;
            float dy = mouse.y - p[i].y;
            if (dx * dx + dy * dy < 625.0f) { g_crop_handle = i; break; }
        }
        if (g_crop_handle < 0 &&
            mouse.x >= p[0].x && mouse.y >= p[0].y &&
            mouse.x <= p[3].x && mouse.y <= p[3].y)
            g_crop_handle = 4;
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) g_crop_handle = -1;

    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f) && g_crop_handle >= 0)
    {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        ImVec2 img_end(g_img_pos.x + g_img_size.x, g_img_pos.y + g_img_size.y);

        if (g_crop_handle == 4)
        {
            g_crop_pos.x = clampf(g_crop_pos.x + delta.x, g_img_pos.x,
                                  img_end.x - g_crop_w);
            g_crop_pos.y = clampf(g_crop_pos.y + delta.y, g_img_pos.y,
                                  img_end.y - g_crop_h);
        }
        else
        {
            float aspect = g_crop_w / g_crop_h;
            bool left = (g_crop_handle == 0 || g_crop_handle == 2);
            bool top = (g_crop_handle == 0 || g_crop_handle == 1);
            ImVec2 anchor(left ? g_crop_pos.x + g_crop_w : g_crop_pos.x,
                          top ? g_crop_pos.y + g_crop_h : g_crop_pos.y);

            float want_w = left ? (anchor.x - mouse.x) : (mouse.x - anchor.x);
            float want_h = top ? (anchor.y - mouse.y) : (mouse.y - anchor.y);
            if (want_h * aspect > want_w) want_w = want_h * aspect;

            float room_x = left ? anchor.x - g_img_pos.x : img_end.x - anchor.x;
            float room_y = (top ? anchor.y - g_img_pos.y : img_end.y - anchor.y) * aspect;
            float room = room_x < room_y ? room_x : room_y;

            float min_w = 40.0f < room ? 40.0f : room;
            want_w = clampf(want_w, min_w, room);

            g_crop_w = want_w;
            g_crop_h = want_w / aspect;
            g_crop_pos.x = left ? anchor.x - g_crop_w : anchor.x;
            g_crop_pos.y = top ? anchor.y - g_crop_h : anchor.y;
        }
    }

    // Crosshair, dimmed outside, white frame, corner dots.
    dl->AddLine(ImVec2(g_crop_pos.x + g_crop_w * 0.5f, g_crop_pos.y),
                ImVec2(g_crop_pos.x + g_crop_w * 0.5f, g_crop_pos.y + g_crop_h),
                IM_COL32(255, 255, 255, 30));
    dl->AddLine(ImVec2(g_crop_pos.x, g_crop_pos.y + g_crop_h * 0.5f),
                ImVec2(g_crop_pos.x + g_crop_w, g_crop_pos.y + g_crop_h * 0.5f),
                IM_COL32(255, 255, 255, 30));

    dl->AddRectFilled(g_img_pos, ImVec2(g_img_pos.x + g_img_size.x, g_crop_pos.y),
                      IM_COL32(0, 0, 0, 160));
    dl->AddRectFilled(ImVec2(g_img_pos.x, g_crop_pos.y + g_crop_h),
                      ImVec2(g_img_pos.x + g_img_size.x, g_img_pos.y + g_img_size.y),
                      IM_COL32(0, 0, 0, 160));
    dl->AddRectFilled(ImVec2(g_img_pos.x, g_crop_pos.y),
                      ImVec2(g_crop_pos.x, g_crop_pos.y + g_crop_h),
                      IM_COL32(0, 0, 0, 160));
    dl->AddRectFilled(ImVec2(g_crop_pos.x + g_crop_w, g_crop_pos.y),
                      ImVec2(g_img_pos.x + g_img_size.x, g_crop_pos.y + g_crop_h),
                      IM_COL32(0, 0, 0, 160));

    dl->AddRect(g_crop_pos, ImVec2(g_crop_pos.x + g_crop_w,
                                   g_crop_pos.y + g_crop_h),
                IM_COL32_WHITE, 0.0f, 0, 2.0f);
    for (int i = 0; i < 4; i++)
        dl->AddCircleFilled(p[i], 5.0f, (g_crop_handle == i) ? IM_COL32(100, 255, 100, 255)
                                                             : IM_COL32_WHITE);

    ImGui::SetCursorScreenPos(ImVec2(g_img_pos.x, g_img_pos.y + g_img_size.y + 20.0f));
    if (ImGui::Button(tr("OK"), ImVec2(90, 35)))
    {
        int sx = (int)((g_crop_pos.x - g_img_pos.x) / g_img_scale);
        int sy = (int)((g_crop_pos.y - g_img_pos.y) / g_img_scale);
        int sw = (int)(g_crop_w / g_img_scale);
        int sh = (int)(g_crop_h / g_img_scale);
        if (apply_crop(sx, sy, sw, sh))
        {
            log_line("profile: кроп принят (%d, %d, %d, %d)", sx, sy, sw, sh);
            free_avatar_source();
            g_cropping = false;
            ImGui::CloseCurrentPopup();
            // Back to the editor, with the staged preview waiting in it.
            g_ed_suspended = false;
            g_ed_want_open = true;
            g_ed_open = true;
        }
        else
        {
            log_line("profile: кроп не вышел (%d, %d, %d, %d)", sx, sy, sw, sh);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Отмена"), ImVec2(90, 35)))
    {
        log_line("profile: кроп отменён");
        free_avatar_source();
        g_cropping = false;
        ImGui::CloseCurrentPopup();
        g_ed_suspended = false;
        g_ed_want_open = true;
        g_ed_open = true;
    }

    ImGui::EndPopup();
}

void ui_view_profile_editor()
{
    if (g_ed_want_open)
    {
        ImGui::OpenPopup("##profiledit");
        g_ed_want_open = false;
    }
    if (!g_ed_open) return;

    // The editor popup is not modal, so anything outside it closes it.
    // While a crop runs it is closed deliberately (see pick_avatar_file),
    // so a missing popup then only means "not yet reopened", never a loss.
    if (!ImGui::IsPopupOpen("##profiledit"))
    {
        g_ed_open = false;
        if (!g_cropping && !g_ed_suspended)
        {
            log_line("profile: редактор закрыт снаружи");
            close_editor();
        }
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(600, 0), ImGuiCond_Always);
    if (!ImGui::BeginPopup("##profiledit", ImGuiWindowFlags_NoResize))
        return;

    if (!g_hist_requested)
    {
        g_hist_requested = true;
        api::fetch_avatars();
    }
    g_hist_n = api::avatar_history(g_hist, 24);

    // Outcomes first: they decide the busy flags the buttons below read.
    {
        bool ok = false;
        char err[256];
        err[0] = 0;
        if (api::take_profile_result(&ok, err, sizeof(err)))
        {
            g_busy_profile = false;
            if (ok) g_err_profile[0] = 0;
            else ccstrncpy(g_err_profile, err[0] ? err : "?", sizeof(g_err_profile) - 1);
        }
        if (api::take_account_result(&ok, err, sizeof(err)))
        {
            g_busy_account = false;
            if (ok)
            {
                g_err_account[0] = 0;
                if (g_pend_username)
                {
                    g_pend_username = false;
                    api::update_account_username(g_ed_username, g_ed_pass);
                    g_busy_account = true;
                    g_sent_any = true;
                }
            }
            else
            {
                g_pend_username = false;
                ccstrncpy(g_err_account, err[0] ? err : "?", sizeof(g_err_account) - 1);
            }
        }
        api::username_check chk;
        ccfset(&chk, 0, sizeof(chk));
        if (api::take_username_check(&chk))
        {
            g_name_probe = true;
            g_name_taken = chk.taken;
            ccstrncpy(g_name_probe_err, chk.error, sizeof(g_name_probe_err) - 1);
        }

        if (g_sent_any && !g_busy_profile && !g_busy_account && !g_pend_username &&
            !g_err_profile[0] && !g_err_account[0])
        {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            close_editor();
            return;
        }
    }

    duser* me = store::self();
    snowflake self_id = store::self_id();

    ImGui::TextUnformatted(tr("Изменить профиль"));
    ImGui::Separator();

    // ---- avatar ----
    {
        ImGui::PushID("avatar");
        ImVec2 at = ImGui::GetCursorScreenPos();
        if (g_av_mode == 1 && g_av_preview)
        {
            ImGui::GetWindowDrawList()->AddImageRounded(
                (ImTextureID)g_av_preview, at, ImVec2(at.x + 96.0f, at.y + 96.0f),
                ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 48.0f);
            ImGui::Dummy(ImVec2(96.0f, 96.0f));
        }
        else if (me)
        {
            ui_avatar(me, 96.0f, false);
        }
        else
        {
            ImGui::Dummy(ImVec2(96.0f, 96.0f));
        }

        ImGui::SameLine(0, 12.0f);
        ImGui::BeginGroup();
        if (ImGui::Button(tr("Выбрать файл"), ImVec2(220, 30)))
            pick_avatar_file();

        const char* src = 0;
        static char srcline[160];
        if (g_av_mode == 1) src = tr("новый: из файла");
        else if (g_av_mode == 2)
        {
            cnprint(srcline, sizeof(srcline), "история: %s", g_av_hist_id);
            src = srcline;
        }
        else if (g_av_mode == 3) src = tr("вписанный id");
        else src = tr("текущая");
        ui_text_muted(src);

        if (g_av_mode != 0 && ImGui::SmallButton(tr("Убрать выбор")))
            free_avatar_staged();
        ImGui::EndGroup();
        ImGui::PopID();
    }

    // ---- banner ----
    {
        ImGui::PushID("banner");
        ui_text_muted(tr("Баннер профиля"));
        float bw = 280.0f;
        float bh = bw / BANNER_ASPECT;
        ImVec2 bp = ImGui::GetCursorScreenPos();
        ImDrawList* bdl = ImGui::GetWindowDrawList();
        bool drew = false;

        if (g_bn_mode == 1 && g_bn_preview)
        {
            bdl->AddImageRounded((ImTextureID)g_bn_preview, bp,
                                 ImVec2(bp.x + bw, bp.y + bh),
                                 ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 8.0f);
            drew = true;
        }
        else if (g_bn_mode != 2 && me && me->banner && me->banner[0])
        {
            char url[320];
            cdn::user_banner(me, 512, url, sizeof(url));
            const texture* bt = url[0] ? tex::get(url) : 0;
            if (bt && bt->ready())
            {
                ImVec2 uv0(0, 0), uv1(1, 1);
                float src_aspect = bt->height > 0
                                 ? (float)bt->width / (float)bt->height : 1.0f;
                if (src_aspect > BANNER_ASPECT)
                {
                    float visible = BANNER_ASPECT / src_aspect;
                    uv0.x = (1.0f - visible) * 0.5f;
                    uv1.x = 1.0f - uv0.x;
                }
                else if (src_aspect < BANNER_ASPECT)
                {
                    float visible = src_aspect / BANNER_ASPECT;
                    uv0.y = (1.0f - visible) * 0.5f;
                    uv1.y = 1.0f - uv0.y;
                }
                bdl->AddImageRounded(bt->id(), bp, ImVec2(bp.x + bw, bp.y + bh),
                                     uv0, uv1, IM_COL32_WHITE, 8.0f);
                drew = true;
            }
        }

        if (!drew)
            bdl->AddRectFilled(bp, ImVec2(bp.x + bw, bp.y + bh), col::bg_hover, 8.0f);
        ImGui::Dummy(ImVec2(bw, bh));

        ImGui::SameLine(0, 12.0f);
        ImGui::BeginGroup();
        if (ImGui::Button(tr("Выбрать файл"), ImVec2(220, 30)))
            pick_banner_file();

        if (g_bn_mode == 1) ui_text_muted(tr("новый: из файла"));
        else if (g_bn_mode == 2) ui_text_muted(tr("будет убран"));
        else if (me && me->banner && me->banner[0]) ui_text_muted(tr("текущий"));
        else ui_text_muted(tr("нет баннера"));

        if (g_bn_mode == 1 && ImGui::SmallButton(tr("Убрать выбор")))
            free_banner_staged();
        else if (g_bn_mode == 0 && me && me->banner && me->banner[0] &&
                 ImGui::SmallButton(tr("Убрать баннер")))
        {
            free_banner_staged();
            g_bn_mode = 2;
        }
        else if (g_bn_mode == 2 && ImGui::SmallButton(tr("Оставить")))
            g_bn_mode = 0;
        ImGui::EndGroup();
        ImGui::PopID();
    }

    ImGui::Dummy(ImVec2(0, 4));
    ui_text_muted(tr("Прошлые аватарки"));
    if (ImGui::CollapsingHeader(tr("История"), ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (api::avatars_loading() && !g_hist_n)
        {
            ui_text_muted(tr("Загрузка..."));
        }
        else if (!g_hist_n)
        {
            ui_text_muted(api::avatars_failed() ? tr("Не удалось загрузить")
                                                : tr("Пока пусто"));
        }
        else
        {
            ImGui::BeginChild("##avhist", ImVec2(0, 132.0f), false);
            for (int i = 0; i < g_hist_n; i++)
            {
                ImGui::PushID(i);

                char thumb_url[320];
                cdn::archived_avatar(self_id, g_hist[i].id, g_hist[i].hash, 64,
                                     thumb_url, sizeof(thumb_url));
                const texture* th = thumb_url[0] ? tex::get(thumb_url) : 0;
                bool thumb_failed = th && th->state == TEX_FAILED;

                ImVec2 av_at = ImGui::GetCursorScreenPos();
                ImDrawList* tdl = ImGui::GetWindowDrawList();
                float av_round = 32.0f * theme::avatar_rounding();
                if (th && th->ready())
                    tdl->AddImageRounded(th->id(), av_at,
                                         ImVec2(av_at.x + 32.0f, av_at.y + 32.0f),
                                         ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, av_round);
                else
                    tdl->AddRectFilled(av_at, ImVec2(av_at.x + 32.0f, av_at.y + 32.0f),
                                       col::bg_hover, av_round);

                ImGui::SetCursorScreenPos(av_at);
                if (ImGui::InvisibleButton("##thumbview", ImVec2(32.0f, 32.0f)))
                {
                    if (thumb_failed)
                    {
                        // Dead link stays dead after the automatic attempts:
                        // tapping the square asks for one more go.
                        tex::retry(thumb_url);
                    }
                    else
                    {
                        char full[320];
                        cdn::archived_avatar(self_id, g_hist[i].id, g_hist[i].hash, 4096,
                                             full, sizeof(full));

                        char name[160];
                        cnprint(name, sizeof(name), "avatar-%s.png", g_hist[i].id);
                        ui_open_image_viewer(full, name);
                    }
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    ImGui::SetTooltip(thumb_failed ? tr("Не загрузилась, нажать чтобы повторить")
                                                   : tr("Открыть аватарку"));
                }
                if (thumb_failed)
                {
                    tdl->AddLine(av_at, ImVec2(av_at.x + 32.0f, av_at.y + 32.0f),
                                 col::red, 2.0f);
                    tdl->AddLine(ImVec2(av_at.x + 32.0f, av_at.y), ImVec2(av_at.x, av_at.y + 32.0f),
                                 col::red, 2.0f);
                }
                ImGui::SameLine(0, 8.0f);

                // The description can be long ("Screenshot_520, added ...");
                // without a cap it pushes the buttons past the edge, and the
                // row below the fold selects nothing.
                const char* base = g_hist[i].desc[0] ? g_hist[i].desc : g_hist[i].id;
                float max_w = ImGui::GetContentRegionAvail().x - 180.0f;
                if (max_w < 40.0f) max_w = 40.0f;

                char head[128];
                {
                    int best = 0;
                    int at = 0;
                    while (base[at])
                    {
                        int next = at + 1;
                        while (base[next] && ((unsigned char)base[next] & 0xC0) == 0x80)
                            next++;
                        if (ImGui::CalcTextSize(base, base + next).x > max_w) break;
                        best = at = next;
                    }
                    int k = 0;
                    for (; k < best && k < (int)sizeof(head) - 5; k++) head[k] = base[k];
                    head[k] = 0;
                    if (base[best]) ccstrncpy(head + k, "...", 4);
                }

                ImGui::BeginGroup();
                ImGui::TextUnformatted(head);
                ImGui::SameLine();
                if (ImGui::SmallButton(tr("Выбрать")))
                {
                    free_avatar_staged();
                    ccstrncpy(g_av_hist_id, g_hist[i].id, sizeof(g_av_hist_id) - 1);
                    g_av_mode = 2;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton(tr("ID")))
                    ImGui::SetClipboardText(g_hist[i].id);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(tr("Скопировать avatar_id"));

                ui_text_muted(g_hist[i].id);
                ImGui::EndGroup();
                ImGui::PopID();
            }
            ImGui::EndChild();
        }

        ImGui::SetNextItemWidth(220.0f);
        ImGui::InputTextWithHint("##avid", tr("любой avatar_id"),
                                 g_ed_manual_id, sizeof(g_ed_manual_id));
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Поставить")) && g_ed_manual_id[0])
        {
            free_avatar_staged();
            g_av_mode = 3;
        }
    }

    // ---- names ----
    ImGui::Dummy(ImVec2(0, 4));
    ui_text_muted(tr("Отображаемое имя"));
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##edname", g_ed_name, sizeof(g_ed_name));

    ui_text_muted(tr("Юзернейм"));
    ImGui::SetNextItemWidth(-140.0f);
    ImGui::InputText("##eduser", g_ed_username, sizeof(g_ed_username));
    ImGui::SameLine();
    if (ImGui::SmallButton(tr("Проверить")))
    {
        g_name_probe = false;
        g_name_probe_err[0] = 0;
        if (g_ed_username[0]) api::check_username(g_ed_username);
    }
    if (g_name_probe)
    {
        if (g_name_probe_err[0])
        {
            ImGui::PushStyleColor(ImGuiCol_Text, col::red);
            ImGui::TextUnformatted(g_name_probe_err);
            ImGui::PopStyleColor();
        }
        else if (g_name_taken)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, col::red);
            ImGui::TextUnformatted(tr("Занят"));
            ImGui::PopStyleColor();
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Text, col::green);
            ImGui::TextUnformatted(tr("Свободен"));
            ImGui::PopStyleColor();
        }
    }

    ui_text_muted(tr("Пароль (для смены юзернейма)"));
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##edpass", tr("текущий пароль"),
                             g_ed_pass, sizeof(g_ed_pass),
                             ImGuiInputTextFlags_Password);

    // ---- text ----
    ui_text_muted(tr("О себе"));
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextMultiline("##edbio", g_ed_bio, sizeof(g_ed_bio), ImVec2(-1, 80));

    ui_text_muted(tr("Местоимения"));
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##edpron", g_ed_pronouns, sizeof(g_ed_pronouns));

    if (g_err_profile[0])
    {
        ImGui::PushStyleColor(ImGuiCol_Text, col::red);
        ImGui::TextWrapped("%s", g_err_profile);
        ImGui::PopStyleColor();
    }
    if (g_err_account[0])
    {
        ImGui::PushStyleColor(ImGuiCol_Text, col::red);
        ImGui::TextWrapped("%s", g_err_account);
        ImGui::PopStyleColor();
    }

    ImGui::Dummy(ImVec2(0, 6));
    bool busy = g_busy_profile || g_busy_account || offline::active();
    if (busy)
    {
        ui_text_muted(offline::active() ? tr("Нет связи — сохранить нельзя")
                                        : tr("Сохранение..."));
    }
    else if (ImGui::Button(tr("Применить"), ImVec2(180, 32)))
    {
        bool name_ch = me && !same_text(g_ed_name, me->global_name);
        bool user_ch = me && !same_text(g_ed_username, me->username);
        bool bio_ch = me && !same_text(g_ed_bio, me->bio);
        bool pro_ch = me && !same_text(g_ed_pronouns, me->pronouns);
        bool av_ch = (g_av_mode == 1 && g_av_datauri) ||
                     (g_av_mode == 2 && g_av_hist_id[0]) ||
                     (g_av_mode == 3 && g_ed_manual_id[0]);
        bool bn_ch = (g_bn_mode == 1 && g_bn_datauri) || g_bn_mode == 2;

        if (!me)
        {
            ccstrncpy(g_err_account, tr("Нет данных аккаунта"),
                      sizeof(g_err_account) - 1);
        }
        else if (user_ch && !g_ed_pass[0])
        {
            ccstrncpy(g_err_account, tr("Для смены юзернейма введи пароль"),
                      sizeof(g_err_account) - 1);
        }
        else if (!name_ch && !user_ch && !bio_ch && !pro_ch && !av_ch && !bn_ch)
        {
            ImGui::CloseCurrentPopup();
            close_editor();
            ImGui::EndPopup();
            return;
        }
        else
        {
            g_sent_any = true;
            g_err_profile[0] = 0;
            g_err_account[0] = 0;

            if (bio_ch || pro_ch || bn_ch)
            {
                api::update_profile(bio_ch ? nonempty_or_null(g_ed_bio) : 0,
                                    pro_ch ? nonempty_or_null(g_ed_pronouns) : 0,
                                    (g_bn_mode == 1) ? g_bn_datauri : 0,
                                    g_bn_mode == 2);
                g_busy_profile = true;
            }
            const char* av_data = (g_av_mode == 1) ? g_av_datauri : 0;
            const char* av_id = (g_av_mode == 2) ? g_av_hist_id
                              : (g_av_mode == 3) ? g_ed_manual_id : 0;
            if (name_ch || av_ch)
            {
                api::update_account_display(name_ch ? g_ed_name : 0, av_data, av_id);
                g_busy_account = true;
            }
            if (user_ch)
            {
                if (!name_ch && !av_ch)
                {
                    api::update_account_username(g_ed_username, g_ed_pass);
                    g_busy_account = true;
                }
                else
                {
                    g_pend_username = true;
                }
            }
        }
    }

    if (!busy)
    {
        ImGui::SameLine();
        if (ImGui::Button(tr("Отмена"), ImVec2(140, 32)))
        {
            ImGui::CloseCurrentPopup();
            close_editor();
            ImGui::EndPopup();
            return;
        }
    }

    ImGui::EndPopup();
}
