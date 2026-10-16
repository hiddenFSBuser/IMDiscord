#include "pch.h"
#include <d3d11.h>
#include "ui_wallpaper.h"
#include "theme.h"
#include "core/app.h"
#include "core/log.h"
#include "core/storage.h"
#include "system/io/ufile.h"
#include "stb/stb_image.h"
#include "stb_image_write.h"

// Only in the implementation part of the header, so no prototype reaches
// this unit the normal way. Implemented once in stb_impl.obj, with C
// linkage like the rest of stb.
extern "C" unsigned char* stbi_write_png_to_mem(const unsigned char* pixels, int stride_bytes,
                                                int x, int y, int n, int* out_len);

namespace
{
    // Working pixels are capped at this long side: a blurred backdrop never
    // needs more, and the box blur below is linear in pixels times radius.
    const int MAX_SIDE = 1920;
    const int MAX_BLUR = 20;
    // Panels stay mostly solid so text keeps its contrast; the wallpaper
    // reads through them rather than replacing them.
    const unsigned int PANEL_ALPHA = 215;

    ID3D11ShaderResourceView* g_tex = 0;
    ID3D11Device* g_tex_device = 0;
    int g_tw = 0, g_th = 0;

    // Soft backdrop behind the sharp layer: a small copy, always blurred
    // well past the slider, drawn cover-fit so no pixel stays empty.
    ID3D11ShaderResourceView* g_back_tex = 0;
    int g_back_tw = 0, g_back_th = 0;
    const int BACK_SIDE = 480;
    const int BACK_BLUR = 14;

    // ---- crop state (same shape as the avatar cropper) ----
    unsigned char* g_pick = 0;
    int g_pick_w = 0, g_pick_h = 0;
    ID3D11ShaderResourceView* g_pick_tex = 0;
    bool g_cropping = false;
    bool g_crop_want_open = false;
    bool g_crop_init = false;
    ImVec2 g_crop_pos;
    ImVec2 g_crop_size;         // rect, not square: aspect is fixed
    int g_crop_handle = -1;     // 0..3 corners, 4 move
    ImVec2 g_img_pos;
    ImVec2 g_img_size;
    float g_img_scale = 1.0f;
    int g_ar_w = 16, g_ar_h = 9;
    // Portrait art on a landscape screen: the locked zone cannot isolate a
    // vertical picture, so the crop offers a flip to the mirrored aspect.
    bool g_ar_flip = false;

    float clampf(float v, float lo, float hi)
    {
        if (v < lo) return lo;
        if (v > hi) return hi;
        return v;
    }

    int clampi(int v, int lo, int hi)
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

    bool wallpaper_path(wchar_t* out, int out_chars)
    {
        return ufile::app_path(L"wallpaper.png", out, out_chars);
    }

    void free_working()
    {
        drop_texture(&g_tex);
        g_tw = g_th = 0;
        drop_texture(&g_back_tex);
        g_back_tw = g_back_th = 0;
        g_tex_device = 0;
    }

    void free_pick()
    {
        // Pick pixels are always memalloc'd (see decode_file): one free
        // path, no stbi-vs-heap guessing at the call sites.
        if (g_pick) { memfree(g_pick); g_pick = 0; }
        g_pick_w = g_pick_h = 0;
        drop_texture(&g_pick_tex);
    }

    // Box-average rect (sx, sy, sw, sh) of src (pitched sw_src) down to
    // tw x th, composited over bg_deep so the result is opaque.
    unsigned char* downscale_rect(const unsigned char* src, int src_w,
                                  int sx, int sy, int sw, int sh, int tw, int th)
    {
        if (!src || sw <= 0 || sh <= 0 || tw <= 0 || th <= 0) return 0;
        unsigned char* out = (unsigned char*)memalloc(tw * th * 4);
        if (!out) return 0;

        float fx = (float)sw / (float)tw;
        float fy = (float)sh / (float)th;
        for (int ty = 0; ty < th; ty++)
        {
            for (int tx = 0; tx < tw; tx++)
            {
                int x0 = (int)(tx * fx);
                int x1 = (int)((tx + 1) * fx);
                int y0 = (int)(ty * fy);
                int y1 = (int)((ty + 1) * fy);
                if (x1 <= x0) x1 = x0 + 1;
                if (y1 <= y0) y1 = y0 + 1;

                float r = 0, g = 0, b = 0, a = 0, n = 0;
                for (int y = y0; y < y1; y++)
                {
                    for (int x = x0; x <= x1; x++)
                    {
                        if (x < 0 || y < 0 || x >= sw || y >= sh) continue;
                        int idx = (((sy + y) * src_w) + (sx + x)) * 4;
                        r += src[idx];
                        g += src[idx + 1];
                        b += src[idx + 2];
                        a += src[idx + 3];
                        n += 1.0f;
                    }
                }
                if (n <= 0.0f) n = 1.0f;
                // Straight over bg_deep (30, 31, 34): translucent source
                // pixels land on the same base the panels use.
                float af = a / n / 255.0f;
                int o = (ty * tw + tx) * 4;
                out[o] = (unsigned char)((r / n) * af + 30.0f * (1.0f - af));
                out[o + 1] = (unsigned char)((g / n) * af + 31.0f * (1.0f - af));
                out[o + 2] = (unsigned char)((b / n) * af + 34.0f * (1.0f - af));
                out[o + 3] = 255;
            }
        }
        return out;
    }

    // Separable box blur, in place, RGB only (the working copy is opaque).
    void box_blur(unsigned char* px, int w, int h, int r)
    {
        if (!px || r <= 0 || w <= 0 || h <= 0) return;
        unsigned char* tmp = (unsigned char*)memalloc(w * h * 4);
        if (!tmp) return;

        int win = r * 2 + 1;
        for (int y = 0; y < h; y++)
        {
            int sr = 0, sg = 0, sb = 0;
            for (int x = -r; x <= r; x++)
            {
                int cx = x < 0 ? 0 : (x >= w ? w - 1 : x);
                int idx = (y * w + cx) * 4;
                sr += px[idx];
                sg += px[idx + 1];
                sb += px[idx + 2];
            }
            for (int x = 0; x < w; x++)
            {
                int o = (y * w + x) * 4;
                tmp[o] = (unsigned char)(sr / win);
                tmp[o + 1] = (unsigned char)(sg / win);
                tmp[o + 2] = (unsigned char)(sb / win);
                tmp[o + 3] = 255;
                int xout = x - r;
                int xin = x + r + 1;
                int io = (y * w + (xout < 0 ? 0 : (xout >= w ? w - 1 : xout))) * 4;
                int ii = (y * w + (xin < 0 ? 0 : (xin >= w ? w - 1 : xin))) * 4;
                sr += px[ii] - px[io];
                sg += px[ii + 1] - px[io + 1];
                sb += px[ii + 2] - px[io + 2];
            }
        }
        for (int x = 0; x < w; x++)
        {
            int sr = 0, sg = 0, sb = 0;
            for (int y = -r; y <= r; y++)
            {
                int cy = y < 0 ? 0 : (y >= h ? h - 1 : y);
                int idx = (cy * w + x) * 4;
                sr += tmp[idx];
                sg += tmp[idx + 1];
                sb += tmp[idx + 2];
            }
            for (int y = 0; y < h; y++)
            {
                int o = (y * w + x) * 4;
                px[o] = (unsigned char)(sr / win);
                px[o + 1] = (unsigned char)(sg / win);
                px[o + 2] = (unsigned char)(sb / win);
                int yout = y - r;
                int yin = y + r + 1;
                int io = ((yout < 0 ? 0 : (yout >= h ? h - 1 : yout)) * w + x) * 4;
                int ii = ((yin < 0 ? 0 : (yin >= h ? h - 1 : yin)) * w + x) * 4;
                sr += tmp[ii] - tmp[io];
                sg += tmp[ii + 1] - tmp[io + 1];
                sb += tmp[ii + 2] - tmp[io + 2];
            }
        }
        memfree(tmp);
    }

    // Decode any image file into RGBA. max_side caps the long side with a
    // box downscale (0 = keep native, for the crop source). The result is
    // always memalloc'd: the native stbi buffer is copied and freed inside,
    // so callers free with memfree and never care which path it took.
    bool decode_file(const wchar_t* path, int max_side,
                     unsigned char** px_out, int* w_out, int* h_out)
    {
        ubuffer blob;
        blob.init();
        if (!ufile::read_all(path, &blob) || !blob.size || blob.size > 30 * 1024 * 1024)
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
            log_line("wallpaper: файл не декодировался (%dx%d)", w, h);
            if (px) stbi_image_free(px);
            return false;
        }

        if (max_side > 0 && (w > max_side || h > max_side))
        {
            float s = (float)max_side / (float)(w > h ? w : h);
            int tw = (int)(w * s);
            int th = (int)(h * s);
            if (tw < 1) tw = 1;
            if (th < 1) th = 1;
            unsigned char* down = downscale_rect(px, w, 0, 0, w, h, tw, th);
            stbi_image_free(px);
            if (!down) return false;
            *px_out = down;
            *w_out = tw;
            *h_out = th;
            return true;
        }

        unsigned char* own = (unsigned char*)memalloc(w * h * 4);
        if (!own)
        {
            stbi_image_free(px);
            return false;
        }
        ccpy(own, px, (size_t)w * h * 4);
        stbi_image_free(px);
        *px_out = own;
        *w_out = w;
        *h_out = h;
        return true;
    }

    // Crop zone aspect from the system resolution: wide screens get 16:9,
    // portrait ones the mirrored 3:4, everything between stays 4:3.
    void screen_aspect(int* aw, int* ah)
    {
        int sw = GetSystemMetrics(SM_CXSCREEN);
        int sh = GetSystemMetrics(SM_CYSCREEN);
        if (sw <= 0 || sh <= 0) { *aw = 16; *ah = 9; return; }
        float r = (float)sw / (float)sh;
        if (r >= 1.5f) { *aw = 16; *ah = 9; }
        else if (r < 1.0f) { *aw = 3; *ah = 4; }
        else { *aw = 4; *ah = 3; }
    }

    bool begin_pick_from_file(const wchar_t* path)
    {
        unsigned char* px = 0;
        int w = 0, h = 0;
        if (!decode_file(path, 0, &px, &w, &h)) return false;

        free_pick();
        // decode_file hands out memalloc'd pixels (freed by free_pick).
        g_pick = px;
        g_pick_w = w;
        g_pick_h = h;
        g_pick_tex = make_rgba_texture(w, h, px);
        if (!g_pick_tex)
        {
            log_line("wallpaper: файл %dx%d не встал в текстуру", w, h);
            free_pick();
            return false;
        }

        screen_aspect(&g_ar_w, &g_ar_h);
        g_ar_flip = false;
        log_line("wallpaper: файл взят %dx%d, кроп %d:%d", w, h, g_ar_w, g_ar_h);
        g_cropping = true;
        g_crop_init = false;
        g_crop_handle = -1;
        g_crop_want_open = true;
        return true;
    }

    // Bounding box of everything that is not background. Background is the
    // average corner colour; a pixel belongs to the content when any
    // channel differs by more than TOL. One click snaps the zone onto the
    // art of letterboxed files (a small picture on a solid field), so the
    // saved wallpaper is art, not padding.
    bool content_box(int* x0, int* y0, int* x1, int* y1)
    {
        if (!g_pick || g_pick_w <= 0 || g_pick_h <= 0) return false;
        const int TOL = 32;
        long br = 0, bg = 0, bb = 0;
        const int corners[4][2] = { {0, 0}, {g_pick_w - 1, 0},
                                    {0, g_pick_h - 1}, {g_pick_w - 1, g_pick_h - 1} };
        for (int i = 0; i < 4; i++)
        {
            int idx = (corners[i][1] * g_pick_w + corners[i][0]) * 4;
            br += g_pick[idx];
            bg += g_pick[idx + 1];
            bb += g_pick[idx + 2];
        }
        br /= 4; bg /= 4; bb /= 4;

        int lx = g_pick_w, rx = -1, ty = g_pick_h, by = -1;
        for (int y = 0; y < g_pick_h; y++)
        {
            for (int x = 0; x < g_pick_w; x++)
            {
                int idx = (y * g_pick_w + x) * 4;
                int dr = (int)g_pick[idx] - (int)br; if (dr < 0) dr = -dr;
                int dg = (int)g_pick[idx + 1] - (int)bg; if (dg < 0) dg = -dg;
                int db = (int)g_pick[idx + 2] - (int)bb; if (db < 0) db = -db;
                if (dr > TOL || dg > TOL || db > TOL)
                {
                    if (x < lx) lx = x;
                    if (x > rx) rx = x;
                    if (y < ty) ty = y;
                    if (y > by) by = y;
                }
            }
        }
        if (rx < lx || by < ty) return false;   // solid field, nothing to snap to
        // Noise-scale findings would yank the zone onto specks.
        if (rx - lx < 100 || by - ty < 100) return false;
        *x0 = lx; *y0 = ty; *x1 = rx; *y1 = by;
        return true;
    }

    // Largest effective-aspect rect inside the content box, centred.
    void trim_borders()
    {
        int x0, y0, x1, y1;
        if (!content_box(&x0, &y0, &x1, &y1))
        {
            log_line("wallpaper: поля не нашлись, зона не тронута");
            return;
        }
        int aw = g_ar_flip ? g_ar_h : g_ar_w;
        int ah = g_ar_flip ? g_ar_w : g_ar_h;
        float ar = (float)aw / (float)ah;
        float bw = (float)(x1 - x0 + 1);
        float bh = (float)(y1 - y0 + 1);
        float w = bw, h = w / ar;
        if (h > bh) { h = bh; w = h * ar; }
        float sx = g_img_pos.x + x0 * g_img_scale;
        float sy = g_img_pos.y + y0 * g_img_scale;
        g_crop_size = ImVec2(w * g_img_scale, h * g_img_scale);
        g_crop_pos = ImVec2(sx + (bw * g_img_scale - g_crop_size.x) * 0.5f,
                            sy + (bh * g_img_scale - g_crop_size.y) * 0.5f);
        g_crop_handle = -1;
        log_line("wallpaper: поля убраны (%d, %d)-(%d, %d)", x0, y0, x1, y1);
    }

    // Crop (sx, sy, sw, sh) of the pick source, downscale the long side to
    // 1920, save as wallpaper.png and switch on. False on any failure, with
    // the crop left open so the zone can be adjusted and retried.
    bool accept_crop(int sx, int sy, int sw, int sh)
    {
        if (!g_pick || sx < 0 || sy < 0 || sw <= 0 || sh <= 0) return false;
        if (sx >= g_pick_w || sy >= g_pick_h) return false;
        if (sx + sw > g_pick_w) sw = g_pick_w - sx;
        if (sy + sh > g_pick_h) sh = g_pick_h - sy;
        if (sw <= 0 || sh <= 0) return false;

        int tw = sw, th = sh;
        int long_side = sw > sh ? sw : sh;
        if (long_side > MAX_SIDE)
        {
            float s = (float)MAX_SIDE / (float)long_side;
            tw = (int)(sw * s);
            th = (int)(sh * s);
            if (tw < 1) tw = 1;
            if (th < 1) th = 1;
        }

        unsigned char* out = downscale_rect(g_pick, g_pick_w, sx, sy, sw, sh, tw, th);
        if (!out) return false;

        int png_len = 0;
        unsigned char* png = stbi_write_png_to_mem(out, tw * 4, tw, th, 4, &png_len);
        memfree(out);
        if (!png || png_len <= 0)
        {
            if (png) memfree(png);
            return false;
        }

        wchar_t wpath[MAX_PATH];
        bool saved = wallpaper_path(wpath, MAX_PATH) &&
                     ufile::write_all(wpath, png, (unsigned int)png_len);
        memfree(png);
        if (!saved) return false;

        storage::settings_set_int("wallpaper_on", 1);
        storage::settings_save();
        wallpaper::reload();
        return true;
    }

}

namespace wallpaper
{
    bool enabled()
    {
        return storage::settings_get_int("wallpaper_on", 0) != 0;
    }

    bool active()
    {
        return enabled() && g_tex != 0;
    }

    int dim()
    {
        return clampi(storage::settings_get_int("wallpaper_dim", 45), 0, 100);
    }

    void set_dim(int v)
    {
        storage::settings_set_int("wallpaper_dim", clampi(v, 0, 100));
        storage::settings_save();
    }

    int blur()
    {
        return clampi(storage::settings_get_int("wallpaper_blur", 6), 0, MAX_BLUR);
    }

    void set_blur(int v)
    {
        storage::settings_set_int("wallpaper_blur", clampi(v, 0, MAX_BLUR));
        storage::settings_save();
    }

    void reload()
    {
        free_working();
        if (!enabled()) return;
        if (!g_app.device) return;

        wchar_t path[MAX_PATH];
        if (!wallpaper_path(path, MAX_PATH)) return;

        unsigned char* px = 0;
        int w = 0, h = 0;
        if (!decode_file(path, MAX_SIDE, &px, &w, &h))
        {
            log_line("wallpaper: нет файла, обои нечем рисовать");
            return;
        }

        // Sharp layer: user blur baked in, kept whole (contain-fit below
        // shows all of it, so nothing here may pre-crop).
        int r = blur();
        if (r > 0) box_blur(px, w, h, r);

        g_tex = make_rgba_texture(w, h, px);
        if (!g_tex)
        {
            log_line("wallpaper: текстура %dx%d не создалась", w, h);
            memfree(px);
            return;
        }
        g_tw = w;
        g_th = h;

        // Backdrop from the same pixels, small and blurred further: cheap,
        // and always softer than the sharp layer above it.
        {
            int bw = w, bh = h;
            int long_side = w > h ? w : h;
            if (long_side > BACK_SIDE)
            {
                float bs = (float)BACK_SIDE / (float)long_side;
                bw = (int)(w * bs);
                bh = (int)(h * bs);
                if (bw < 1) bw = 1;
                if (bh < 1) bh = 1;
            }
            unsigned char* tiny = downscale_rect(px, w, 0, 0, w, h, bw, bh);
            if (tiny)
            {
                box_blur(tiny, bw, bh, BACK_BLUR);
                g_back_tex = make_rgba_texture(bw, bh, tiny);
                memfree(tiny);
                if (g_back_tex) { g_back_tw = bw; g_back_th = bh; }
            }
        }
        memfree(px);

        g_tex_device = g_app.device;
        log_line("wallpaper: загружены %dx%d, блюр %d", w, h, r);
    }

    void ensure()
    {
        if (!enabled()) return;
        if ((g_tex && g_tex_device == g_app.device) || !g_app.device) return;
        // A missing file must not turn into disk IO on every frame: retry
        // at most once every two seconds.
        static unsigned long long last_try = 0;
        unsigned long long now = GetTickCount64();
        if (now - last_try < 2000) return;
        last_try = now;
        reload();
    }

    static void paint(ImDrawList* dl, ImVec2 origin, ImVec2 size)
    {
        if (!g_tex || g_tw <= 0 || g_th <= 0) return;
        if (size.x <= 0 || size.y <= 0) return;

        // Backdrop first: blurred cover over every pixel, so mismatched
        // aspects never leave bars. On a matched aspect it is the same
        // picture, only softer - visually pure cover.
        if (g_back_tex && g_back_tw > 0 && g_back_th > 0)
        {
            float s = size.x / (float)g_back_tw;
            {
                float s2 = size.y / (float)g_back_th;
                if (s2 > s) s = s2;
            }
            float dw = g_back_tw * s;
            float dh = g_back_th * s;
            ImVec2 at(origin.x + (size.x - dw) * 0.5f, origin.y + (size.y - dh) * 0.5f);
            dl->AddImage((ImTextureID)g_back_tex, at, ImVec2(at.x + dw, at.y + dh));
        }

        // Sharp layer contain-fit: the whole composition stays visible.
        // Portrait art is no longer zoom-cropped to a band.
        float s = size.x / (float)g_tw;
        {
            float s2 = size.y / (float)g_th;
            if (s2 < s) s = s2;
        }
        float dw = g_tw * s;
        float dh = g_th * s;
        ImVec2 at(origin.x + (size.x - dw) * 0.5f, origin.y + (size.y - dh) * 0.5f);
        dl->AddImage((ImTextureID)g_tex, at, ImVec2(at.x + dw, at.y + dh));

        // Geometry trace, once per texture: backdrop cover plus sharp
        // contain sizes, so a weird frame can be blamed precisely.
        static ID3D11ShaderResourceView* logged_for = 0;
        if (logged_for != g_tex)
        {
            logged_for = g_tex;
            log_line("wallpaper: рисую %dx%d в %.0fx%.0f -> %.0fx%.0f",
                     g_tw, g_th, size.x, size.y, dw, dh);
        }

        int a = dim() * 215 / 100;
        if (a > 0)
            dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                              IM_COL32(0, 0, 0, a));
    }

    void draw_under(ImVec2 origin, ImVec2 size)
    {
        ensure();
        if (!g_tex) return;
        paint(ImGui::GetWindowDrawList(), origin, size);
    }

    void draw_background(ImVec2 origin, ImVec2 size)
    {
        ensure();
        if (!g_tex) return;
        paint(ImGui::GetBackgroundDrawList(), origin, size);
    }

    ImU32 panel(ImU32 base)
    {
        if (!active()) return base;
        return (base & 0x00FFFFFFu) | ((ImU32)PANEL_ALPHA << 24);
    }

    ImVec4 tint(ImVec4 v)
    {
        if (active()) v.w *= (float)PANEL_ALPHA / 255.0f;
        return v;
    }

    void pick_file()
    {
        wchar_t path[MAX_PATH];
        path[0] = 0;
        if (!ufile::open_dialog(path, MAX_PATH)) return;
        begin_pick_from_file(path);
    }

    void crop_current()
    {
        wchar_t path[MAX_PATH];
        if (!wallpaper_path(path, MAX_PATH)) return;
        begin_pick_from_file(path);
    }

    void remove()
    {
        storage::settings_set_int("wallpaper_on", 0);
        storage::settings_save();
        free_working();
        wchar_t path[MAX_PATH];
        if (wallpaper_path(path, MAX_PATH)) DeleteFileW(path);
        log_line("wallpaper: убраны");
    }
}

void ui_view_wallpaper_crop()
{
    if (g_crop_want_open)
    {
        ImGui::OpenPopup("##wallcrop");
        g_crop_want_open = false;
    }
    if (!g_cropping) return;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    // Fullscreen modal, explicitly: auto-fit sizes to the buttons only,
    // while the picture is drawn at absolute coordinates - anything outside
    // the fitted rect gets clipped, which is exactly the "cut window".
    ImGui::SetNextWindowPos(vp->WorkPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(vp->WorkSize, ImGuiCond_Always);
    if (!ImGui::BeginPopupModal("##wallcrop", 0,
                                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
        return;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    // Effective aspect: flipped for portrait art on a landscape screen.
    int aw = g_ar_flip ? g_ar_h : g_ar_w;
    int ah = g_ar_flip ? g_ar_w : g_ar_h;
    float ar = (float)aw / (float)ah;

    float max_w = vp->WorkSize.x - 80.0f;
    float max_h = vp->WorkSize.y - 140.0f;
    if (max_w < 200.0f) max_w = 200.0f;
    if (max_h < 200.0f) max_h = 200.0f;
    float scale = max_w / (float)g_pick_w;
    {
        float s2 = max_h / (float)g_pick_h;
        if (s2 < scale) scale = s2;
    }
    g_img_scale = scale;
    g_img_size = ImVec2((float)g_pick_w * scale, (float)g_pick_h * scale);
    g_img_pos = ImVec2(vp->WorkPos.x + (vp->WorkSize.x - g_img_size.x) * 0.5f,
                       vp->WorkPos.y + (vp->WorkSize.y - g_img_size.y) * 0.5f - 20.0f);

    dl->AddImage((ImTextureID)g_pick_tex, g_img_pos,
                 ImVec2(g_img_pos.x + g_img_size.x, g_img_pos.y + g_img_size.y));

    if (!g_crop_init)
    {
        // Largest aspect rect that fits, centred - same idea as the avatar
        // square, generalised to the screen's 16:9 or 4:3.
        float w = g_img_size.x;
        float h = w / ar;
        if (h > g_img_size.y) { h = g_img_size.y; w = h * ar; }
        g_crop_size = ImVec2(w, h);
        g_crop_pos = ImVec2(g_img_pos.x + (g_img_size.x - w) * 0.5f,
                            g_img_pos.y + (g_img_size.y - h) * 0.5f);
        g_crop_init = true;
    }

    ImVec2 img_end(g_img_pos.x + g_img_size.x, g_img_pos.y + g_img_size.y);
    ImVec2 p[4] =
    {
        g_crop_pos,
        ImVec2(g_crop_pos.x + g_crop_size.x, g_crop_pos.y),
        ImVec2(g_crop_pos.x, g_crop_pos.y + g_crop_size.y),
        ImVec2(g_crop_pos.x + g_crop_size.x, g_crop_pos.y + g_crop_size.y),
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

        if (g_crop_handle == 4)
        {
            g_crop_pos.x = clampf(g_crop_pos.x + delta.x, g_img_pos.x,
                                  img_end.x - g_crop_size.x);
            g_crop_pos.y = clampf(g_crop_pos.y + delta.y, g_img_pos.y,
                                  img_end.y - g_crop_size.y);
        }
        else
        {
            // Fixed anchors per corner; width drives, height follows the
            // aspect, clamped by whatever side runs out first.
            float ax = g_crop_pos.x, ay = g_crop_pos.y;
            if (g_crop_handle == 0) { ax = p[3].x; ay = p[3].y; }
            else if (g_crop_handle == 1) { ax = p[0].x; ay = p[3].y; }
            else if (g_crop_handle == 2) { ax = p[3].x; ay = p[0].y; }

            float room;
            if (g_crop_handle == 3)
                room = img_end.x - g_crop_pos.x < (img_end.y - g_crop_pos.y) * ar
                       ? img_end.x - g_crop_pos.x : (img_end.y - g_crop_pos.y) * ar;
            else if (g_crop_handle == 0)
                room = ax - g_img_pos.x < (ay - g_img_pos.y) * ar
                       ? ax - g_img_pos.x : (ay - g_img_pos.y) * ar;
            else if (g_crop_handle == 1)
                room = img_end.x - ax < (ay - g_img_pos.y) * ar
                       ? img_end.x - ax : (ay - g_img_pos.y) * ar;
            else
                room = ax - g_img_pos.x < (img_end.y - ay) * ar
                       ? ax - g_img_pos.x : (img_end.y - ay) * ar;

            float ds = (g_crop_handle == 0 || g_crop_handle == 2) ? -delta.x : delta.x;
            float w = clampf(g_crop_size.x + ds, 80.0f, room);
            float h = w / ar;

            if (g_crop_handle == 3) { /* pos stays */ }
            else if (g_crop_handle == 0) { g_crop_pos.x = ax - w; g_crop_pos.y = ay - h; }
            else if (g_crop_handle == 1) { g_crop_pos.y = ay - h; }
            else { g_crop_pos.x = ax - w; }
            g_crop_size = ImVec2(w, h);
        }
    }

    // Dimmed outside, white frame, corner dots - the avatar cropper's look.
    dl->AddRectFilled(g_img_pos, ImVec2(g_img_pos.x + g_img_size.x, g_crop_pos.y),
                      IM_COL32(0, 0, 0, 160));
    dl->AddRectFilled(ImVec2(g_img_pos.x, g_crop_pos.y + g_crop_size.y),
                      ImVec2(g_img_pos.x + g_img_size.x, g_img_pos.y + g_img_size.y),
                      IM_COL32(0, 0, 0, 160));
    dl->AddRectFilled(ImVec2(g_img_pos.x, g_crop_pos.y),
                      ImVec2(g_crop_pos.x, g_crop_pos.y + g_crop_size.y),
                      IM_COL32(0, 0, 0, 160));
    dl->AddRectFilled(ImVec2(g_crop_pos.x + g_crop_size.x, g_crop_pos.y),
                      ImVec2(g_img_pos.x + g_img_size.x, g_crop_pos.y + g_crop_size.y),
                      IM_COL32(0, 0, 0, 160));

    dl->AddRect(g_crop_pos, ImVec2(g_crop_pos.x + g_crop_size.x,
                                   g_crop_pos.y + g_crop_size.y),
                IM_COL32_WHITE, 0.0f, 0, 2.0f);
    for (int i = 0; i < 4; i++)
        dl->AddCircleFilled(p[i], 5.0f, (g_crop_handle == i) ? IM_COL32(100, 255, 100, 255)
                                                             : IM_COL32_WHITE);

    // The zone the system resolution picked (flippable below).
    {
        char zone[64];
        cnprint(zone, sizeof(zone), "%d:%d", aw, ah);
        ImGui::SetCursorScreenPos(ImVec2(g_img_pos.x, g_img_pos.y + g_img_size.y + 8.0f));
        ui_text_muted(zone);
    }

    // Controls right under the picture when they fit there, pinned to the
    // viewport bottom otherwise: with a tall picture the row below the
    // image falls off-screen, which is how OK "disappears".
    // Enter confirms, Escape cancels.
    float want_y = g_img_pos.y + g_img_size.y + 28.0f;
    float pin_y = vp->WorkPos.y + vp->WorkSize.y - 52.0f;
    ImGui::SetCursorScreenPos(ImVec2(g_img_pos.x, want_y < pin_y ? want_y : pin_y));
    bool ok_pressed = ImGui::Button(tr("OK"), ImVec2(90, 35));
    ImGui::SameLine();
    bool cancel_pressed = ImGui::Button(tr("Отмена"), ImVec2(90, 35));
    ImGui::SameLine(0, 18);
    char flip_label[32];
    cnprint(flip_label, sizeof(flip_label), "%d:%d", aw, ah);
    if (ImGui::Button(flip_label, ImVec2(90, 35)))
    {
        g_ar_flip = !g_ar_flip;
        g_crop_init = false;
        g_crop_handle = -1;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(tr("Повернуть зону: для вертикальных картинок"));
    ImGui::SameLine(0, 18);
    if (ImGui::Button(tr("Убрать поля"), ImVec2(110, 35))) trim_borders();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(tr("Обвести содержимое: срезать одноцветные поля"));
    ImGui::SameLine(0, 18);
    ui_text_muted(tr("Светлая зона войдёт в обои"));
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false)) ok_pressed = true;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) cancel_pressed = true;

    if (ok_pressed)
    {
        int sx = (int)((g_crop_pos.x - g_img_pos.x) / g_img_scale);
        int sy = (int)((g_crop_pos.y - g_img_pos.y) / g_img_scale);
        int sw = (int)(g_crop_size.x / g_img_scale);
        int sh = (int)(g_crop_size.y / g_img_scale);

        if (accept_crop(sx, sy, sw, sh))
        {
            log_line("wallpaper: кроп принят (%d, %d, %dx%d)", sx, sy, sw, sh);
            free_pick();
            g_cropping = false;
            ImGui::CloseCurrentPopup();
        }
        else
        {
            log_line("wallpaper: кроп не вышел (%d, %d, %dx%d)", sx, sy, sw, sh);
        }
    }
    if (cancel_pressed)
    {
        log_line("wallpaper: кроп отменён");
        free_pick();
        g_cropping = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void ui_wallpaper_settings()
{
    ImGui::TextUnformatted(tr("Обои"));
    ImGui::Separator();

    if (!wallpaper::enabled())
    {
        if (ImGui::Button(tr("Выбрать обои"), ImVec2(-1, 0)))
            wallpaper::pick_file();
        ui_text_muted(tr("Картинка за чатом и панелями, с обрезкой под экран"));
    }
    else
    {
        if (ImGui::Button(tr("Заменить обои"), ImVec2(-1, 0)))
            wallpaper::pick_file();
        if (ImGui::Button(tr("Обрезать"), ImVec2(-1, 0)))
            wallpaper::crop_current();
        if (ImGui::Button(tr("Убрать обои"), ImVec2(-1, 0)))
            wallpaper::remove();

        int d = wallpaper::dim();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::SliderInt(tr("Затемнение"), &d, 0, 100, "%d%%"))
            wallpaper::set_dim(d);

        int b = wallpaper::blur();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::SliderInt(tr("Блюр"), &b, 0, 20))
            wallpaper::set_blur(b);
        // The blur bakes into the texture: rebuilding on every drag tick
        // would hitch, so it happens once the slider is released.
        if (ImGui::IsItemDeactivatedAfterEdit())
            wallpaper::reload();
    }
}
