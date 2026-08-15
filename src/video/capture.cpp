#include "pch.h"
#include <dwmapi.h>
#include "core/gfxload.h"
#include <dxgi1_2.h>
#include <d3d11.h>
#include "capture.h"
#include "core/log.h"
#include "ufile.h"
#include "core/oslib.h"

namespace
{
    // Discord tops out well below this, and it bounds the two DIBs the BitBlt
    // path keeps around.
    const int MAX_CAPTURE_DIMENSION = 4096;

    struct bitblt_state
    {
        HDC source;          // screen or window dc being read
        HWND window;         // zero when a monitor is being captured

        HDC full_dc;         // the grab at native size
        HBITMAP full_bmp;
        HBITMAP full_old;
        unsigned char* full_pixels;
        int full_w, full_h;

        // The shrunk copy is built by hand rather than by StretchBlt. GDI's
        // HALFTONE mode costs 18 ms on a 1280x1024 desktop, over half a frame at
        // 30 fps, and its only fast alternative drops pixels outright, which
        // turns shared text into mush. A box filter over the same data is a
        // single linear pass and comes out both quicker and cleaner.
        unsigned char* scaled_pixels;
        int scaled_w, scaled_h;      // the picture itself
        int box_w, box_h;            // the frame handed out, black around it
        int off_x, off_y;            // where the picture sits inside the frame

        unsigned int* accum;    // one destination row, four channels
        unsigned int* counts;   // source pixels folded into each destination one
        int* x_of_sx;           // destination column for every source column
    };

    // Desktop duplication, which is the fast path. Its own device rather than
    // the one the window draws with: a d3d11 context is not safe to touch from
    // two threads, and this runs on the capture thread while the ui is using
    // the other one.
    struct dxgi_state
    {
        ID3D11Device* device;
        ID3D11DeviceContext* context;
        IDXGIOutputDuplication* dupl;
        ID3D11Texture2D* staging;

        int width, height;
        bool have_frame;      // something has been copied at least once
    };

    dxgi_state g_dxgi;

    capture_method g_method = CAPTURE_DXGI;
    bool g_ready = false;
    bool g_running = false;
    bool g_cursor = true;

    capture_target g_target;

    // Filled by start(), so mapping() can answer without re-deriving it.
    capture_mapping g_map;
    int g_out_w = 0, g_out_h = 0;
    unsigned long long g_interval_us = 33333;
    unsigned long long g_next_due_us = 0;
    unsigned int g_captured = 0;
    unsigned int g_dropped = 0;
    char g_error[160];

    bitblt_state g_blt;

    // GetTickCount64 advances in steps of about 15.6 ms, which cannot express a
    // frame period of 33 ms: it would round every other frame up and settle at
    // 21 frames a second. Everything to do with pacing uses the performance
    // counter instead.
    unsigned long long now_us()
    {
        static LARGE_INTEGER freq = { 0 };
        if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);

        LARGE_INTEGER c;
        QueryPerformanceCounter(&c);
        return (unsigned long long)((c.QuadPart * 1000000LL) / freq.QuadPart);
    }

    void set_error(const char* what)
    {
        ccstrncpy(g_error, what, sizeof(g_error) - 1);
        g_error[sizeof(g_error) - 1] = 0;
    }

    // ---- the BitBlt grabber ------------------------------------------------

    void blt_free()
    {
        // Only a scaled capture owns this buffer; at native size it aliases the
        // grab itself and must not be freed.
        if (g_blt.scaled_pixels && g_blt.scaled_pixels != g_blt.full_pixels)
            memfree(g_blt.scaled_pixels);
        if (g_blt.accum) memfree(g_blt.accum);
        if (g_blt.counts) memfree(g_blt.counts);
        if (g_blt.x_of_sx) memfree(g_blt.x_of_sx);

        if (g_blt.full_dc)
        {
            if (g_blt.full_old) SelectObject(g_blt.full_dc, g_blt.full_old);
            DeleteDC(g_blt.full_dc);
        }
        if (g_blt.full_bmp) DeleteObject(g_blt.full_bmp);

        if (g_blt.source)
        {
            if (g_blt.window) ReleaseDC(g_blt.window, g_blt.source);
            else DeleteDC(g_blt.source);
        }

        ccfset(&g_blt, 0, sizeof(g_blt));
    }

    // A top-down 32 bit surface, so the rows come out in the order the encoder
    // and the RTP packetiser want them.
    bool make_dib(HDC reference, int w, int h, HDC* out_dc, HBITMAP* out_bmp,
                  HBITMAP* out_old, unsigned char** out_pixels)
    {
        BITMAPINFO bi;
        ccfset(&bi, 0, sizeof(bi));
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        void* bits = 0;
        HBITMAP bmp = CreateDIBSection(reference, &bi, DIB_RGB_COLORS, &bits, 0, 0);
        if (!bmp || !bits) return false;

        HDC dc = CreateCompatibleDC(reference);
        if (!dc) { DeleteObject(bmp); return false; }

        *out_old = (HBITMAP)SelectObject(dc, bmp);
        *out_dc = dc;
        *out_bmp = bmp;
        *out_pixels = (unsigned char*)bits;
        return true;
    }

    bool blt_start(const capture_target* t, int inner_w, int inner_h, int box_w, int box_h)
    {
        ccfset(&g_blt, 0, sizeof(g_blt));

        g_blt.window = (HWND)t->window;
        if (g_blt.window)
        {
            // The window dc, not the client dc: a stream that cuts off the
            // title bar looks broken.
            g_blt.source = GetWindowDC(g_blt.window);
            if (!g_blt.source) { set_error("окно не отдало контекст рисования"); return false; }
        }
        else
        {
            // A dc over the whole virtual desktop, so a monitor left of the
            // primary one (negative coordinates) still works.
            g_blt.source = CreateDCW(L"DISPLAY", 0, 0, 0);
            if (!g_blt.source) { set_error("не удалось открыть контекст экрана"); return false; }
        }

        g_blt.full_w = t->width;
        g_blt.full_h = t->height;

        if (!make_dib(g_blt.source, g_blt.full_w, g_blt.full_h,
                      &g_blt.full_dc, &g_blt.full_bmp, &g_blt.full_old, &g_blt.full_pixels))
        {
            set_error("не хватило памяти под кадр");
            blt_free();
            return false;
        }

        g_blt.scaled_w = inner_w;
        g_blt.scaled_h = inner_h;
        g_blt.box_w = box_w;
        g_blt.box_h = box_h;
        g_blt.off_x = ((box_w - inner_w) / 2) & ~1;   // even, so chroma lines up
        g_blt.off_y = ((box_h - inner_h) / 2) & ~1;

        if (inner_w == g_blt.full_w && inner_h == g_blt.full_h &&
            box_w == inner_w && box_h == inner_h)
        {
            // No scaling and no bars: streaming a monitor at its own size.
            g_blt.scaled_pixels = g_blt.full_pixels;
            return true;
        }

        g_blt.scaled_pixels = (unsigned char*)memalloc(box_w * box_h * 4);
        g_blt.accum = (unsigned int*)memalloc(inner_w * 4 * (int)sizeof(unsigned int));
        g_blt.counts = (unsigned int*)memalloc(inner_w * (int)sizeof(unsigned int));
        g_blt.x_of_sx = (int*)memalloc(g_blt.full_w * (int)sizeof(int));

        if (!g_blt.scaled_pixels || !g_blt.accum || !g_blt.counts || !g_blt.x_of_sx)
        {
            set_error("не хватило памяти под уменьшенный кадр");
            blt_free();
            return false;
        }

        // The bars are painted once; the filter only ever touches the inside.
        ccfset(g_blt.scaled_pixels, 0, (size_t)box_w * box_h * 4);
        for (int i = 3; i < box_w * box_h * 4; i += 4) g_blt.scaled_pixels[i] = 255;

        // A division per source pixel would cost more than the filter itself.
        for (int sx = 0; sx < g_blt.full_w; sx++)
            g_blt.x_of_sx[sx] = (int)((long long)sx * inner_w / g_blt.full_w);

        return true;
    }

    // Averages every source pixel into the destination pixel it lands on. The
    // source is walked once, in order, and only one destination row is live at a
    // time, so the whole thing stays in cache.
    void blt_downscale()
    {
        const int sw = g_blt.full_w, sh = g_blt.full_h;
        const int dw = g_blt.scaled_w, dh = g_blt.scaled_h;

        unsigned int* acc = g_blt.accum;
        unsigned int* cnt = g_blt.counts;
        const int* xmap = g_blt.x_of_sx;

        ccfset(acc, 0, dw * 4 * sizeof(unsigned int));
        ccfset(cnt, 0, dw * sizeof(unsigned int));

        int live_dy = 0;

        for (int sy = 0; sy < sh; sy++)
        {
            int dy = (int)((long long)sy * dh / sh);

            if (dy != live_dy)
            {
                unsigned char* dst = g_blt.scaled_pixels +
                    ((size_t)(g_blt.off_y + live_dy) * g_blt.box_w + g_blt.off_x) * 4;
                for (int dx = 0; dx < dw; dx++)
                {
                    unsigned int n = cnt[dx];
                    if (!n) n = 1;
                    dst[dx * 4 + 0] = (unsigned char)(acc[dx * 4 + 0] / n);
                    dst[dx * 4 + 1] = (unsigned char)(acc[dx * 4 + 1] / n);
                    dst[dx * 4 + 2] = (unsigned char)(acc[dx * 4 + 2] / n);
                    dst[dx * 4 + 3] = 255;
                }
                ccfset(acc, 0, dw * 4 * sizeof(unsigned int));
                ccfset(cnt, 0, dw * sizeof(unsigned int));
                live_dy = dy;
            }

            const unsigned char* src = g_blt.full_pixels + (size_t)sy * sw * 4;
            for (int sx = 0; sx < sw; sx++)
            {
                int dx = xmap[sx];
                acc[dx * 4 + 0] += src[sx * 4 + 0];
                acc[dx * 4 + 1] += src[sx * 4 + 1];
                acc[dx * 4 + 2] += src[sx * 4 + 2];
                cnt[dx]++;
            }
        }

        // The last band never sees a row change, so it is flushed here.
        unsigned char* dst = g_blt.scaled_pixels +
                    ((size_t)(g_blt.off_y + live_dy) * g_blt.box_w + g_blt.off_x) * 4;
        for (int dx = 0; dx < dw; dx++)
        {
            unsigned int n = cnt[dx];
            if (!n) n = 1;
            dst[dx * 4 + 0] = (unsigned char)(acc[dx * 4 + 0] / n);
            dst[dx * 4 + 1] = (unsigned char)(acc[dx * 4 + 1] / n);
            dst[dx * 4 + 2] = (unsigned char)(acc[dx * 4 + 2] / n);
            dst[dx * 4 + 3] = 255;
        }
    }

    void draw_cursor(HDC dc, int origin_x, int origin_y)
    {
        CURSORINFO ci;
        ccfset(&ci, 0, sizeof(ci));
        ci.cbSize = sizeof(ci);
        if (!GetCursorInfo(&ci) || !(ci.flags & CURSOR_SHOWING) || !ci.hCursor) return;

        // The hotspot is where the pointer actually points, and DrawIconEx wants
        // the top left corner instead.
        ICONINFO ii;
        ccfset(&ii, 0, sizeof(ii));
        if (!GetIconInfo(ci.hCursor, &ii)) return;

        int x = ci.ptScreenPos.x - origin_x - (int)ii.xHotspot;
        int y = ci.ptScreenPos.y - origin_y - (int)ii.yHotspot;

        DrawIconEx(dc, x, y, ci.hCursor, 0, 0, 0, 0, DI_NORMAL);

        if (ii.hbmColor) DeleteObject(ii.hbmColor);
        if (ii.hbmMask) DeleteObject(ii.hbmMask);
    }

    bool dxgi_grab_into_dib();      // defined below, next to the rest of it

    bool blt_grab(capture_frame* out, bool from_dxgi)
    {
        int src_x = g_blt.window ? 0 : g_target.x;
        int src_y = g_blt.window ? 0 : g_target.y;

        if (from_dxgi)
        {
            // The image is already in the dib. Everything below it - the
            // cursor, the scaler, the bars - is the same work either way,
            // which is why the fast path stops here instead of repeating it.
            if (!dxgi_grab_into_dib()) return false;
        }
        // CAPTUREBLT is what makes layered windows show up; without it a stream
        // has holes where translucent windows are.
        else if (!BitBlt(g_blt.full_dc, 0, 0, g_blt.full_w, g_blt.full_h,
                         g_blt.source, src_x, src_y, SRCCOPY | CAPTUREBLT))
        {
            set_error("BitBlt не смог прочитать экран");
            return false;
        }

        if (g_cursor) draw_cursor(g_blt.full_dc, src_x, src_y);

        // GDI writes through its own device, so the bits are only guaranteed to
        // be in the DIB after this, and the filter below reads them directly.
        GdiFlush();

        if (g_blt.scaled_pixels != g_blt.full_pixels) blt_downscale();

        // The frame handed out is the whole box, bars included.
        out->bgra = g_blt.scaled_pixels;
        out->width = g_blt.box_w;
        out->height = g_blt.box_h;
        out->stride = g_blt.box_w * 4;
        return true;
    }

    // ---- monitor enumeration ----------------------------------------------

    struct monitor_scan
    {
        capture_target* out;
        int cap;
        int count;
    };

    BOOL CALLBACK on_monitor(HMONITOR mon, HDC, LPRECT, LPARAM param)
    {
        monitor_scan* scan = (monitor_scan*)param;
        if (scan->count >= scan->cap) return FALSE;

        MONITORINFOEXW mi;
        ccfset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(mi);
        if (!GetMonitorInfoW(mon, &mi)) return TRUE;

        capture_target* t = &scan->out[scan->count];
        ccfset(t, 0, sizeof(*t));
        t->window = 0;
        t->x = mi.rcMonitor.left;
        t->y = mi.rcMonitor.top;
        t->width = mi.rcMonitor.right - mi.rcMonitor.left;
        t->height = mi.rcMonitor.bottom - mi.rcMonitor.top;
        t->primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;

        char label[64];
        cnprint(label, sizeof(label), "Экран %d (%dx%d)%s",
                scan->count + 1, t->width, t->height, t->primary ? ", основной" : "");
        ccstrncpy(t->name, label, sizeof(t->name) - 1);

        scan->count++;
        return TRUE;
    }
}

bool capture::init()
{
    if (g_ready) return true;
    ccfset(&g_blt, 0, sizeof(g_blt));
    ccfset(&g_target, 0, sizeof(g_target));
    g_error[0] = 0;
    g_ready = true;
    return true;
}

void capture::shutdown()
{
    stop();
    g_ready = false;
}

capture_method capture::method() { return g_method; }

namespace
{
    template <class T> void release(T** p) { if (*p) { (*p)->Release(); *p = 0; } }

    void dxgi_free()
    {
        release(&g_dxgi.staging);
        release(&g_dxgi.dupl);
        release(&g_dxgi.context);
        release(&g_dxgi.device);
        g_dxgi.have_frame = false;
    }

    // Finds the output whose desktop rectangle matches the monitor being
    // captured, and starts duplicating it.
    bool dxgi_open_output(const capture_target* t)
    {
        IDXGIDevice* dxgi_device = 0;
        if (FAILED(g_dxgi.device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgi_device)))
            return false;

        IDXGIAdapter* adapter = 0;
        HRESULT hr = dxgi_device->GetAdapter(&adapter);
        dxgi_device->Release();
        if (FAILED(hr) || !adapter) return false;

        bool started = false;

        for (UINT i = 0; !started; i++)
        {
            IDXGIOutput* output = 0;
            if (adapter->EnumOutputs(i, &output) != S_OK) break;

            DXGI_OUTPUT_DESC desc;
            if (SUCCEEDED(output->GetDesc(&desc)) &&
                desc.DesktopCoordinates.left == t->x &&
                desc.DesktopCoordinates.top == t->y)
            {
                IDXGIOutput1* output1 = 0;
                if (SUCCEEDED(output->QueryInterface(__uuidof(IDXGIOutput1), (void**)&output1)))
                {
                    hr = output1->DuplicateOutput(g_dxgi.device, &g_dxgi.dupl);
                    output1->Release();

                    if (SUCCEEDED(hr) && g_dxgi.dupl) started = true;
                    else set_error("рабочий стол занят другим приложением");
                }
            }

            output->Release();
        }

        adapter->Release();
        if (!started && !g_error[0]) set_error("не нашёлся такой экран");
        return started;
    }

    bool dxgi_start(const capture_target* t)
    {
        ccfset(&g_dxgi, 0, sizeof(g_dxgi));

        // A window cannot be duplicated - the api only knows about outputs.
        if (t->window) { set_error("окно так захватить нельзя"); return false; }

        if (!gfx::load()) { set_error("нет direct3d"); return false; }
        pfn_D3D11CreateDeviceAndSwapChain create = gfx::create_device();
        if (!create) { set_error("нет direct3d"); return false; }

        D3D_FEATURE_LEVEL levels[] = {
            D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0
        };
        D3D_FEATURE_LEVEL got;

        HRESULT hr = create(0, D3D_DRIVER_TYPE_HARDWARE, 0, 0, levels, 3, D3D11_SDK_VERSION,
                            0, 0, &g_dxgi.device, &got, &g_dxgi.context);
        if (FAILED(hr) || !g_dxgi.device)
        {
            set_error("устройство direct3d не создалось");
            dxgi_free();
            return false;
        }

        if (!dxgi_open_output(t)) { dxgi_free(); return false; }

        DXGI_OUTDUPL_DESC desc;
        g_dxgi.dupl->GetDesc(&desc);
        g_dxgi.width = (int)desc.ModeDesc.Width;
        g_dxgi.height = (int)desc.ModeDesc.Height;

        // The duplicated texture lives on the gpu and cannot be read directly.
        D3D11_TEXTURE2D_DESC staging;
        ccfset(&staging, 0, sizeof(staging));
        staging.Width = desc.ModeDesc.Width;
        staging.Height = desc.ModeDesc.Height;
        staging.MipLevels = 1;
        staging.ArraySize = 1;
        staging.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        staging.SampleDesc.Count = 1;
        staging.Usage = D3D11_USAGE_STAGING;
        staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        if (FAILED(g_dxgi.device->CreateTexture2D(&staging, 0, &g_dxgi.staging)))
        {
            set_error("не выделился буфер под кадр");
            dxgi_free();
            return false;
        }

        return true;
    }

    // Pulls the newest desktop image into the shared dib, so the cursor and
    // the scaler downstream work exactly as they do for the blit path.
    bool dxgi_grab_into_dib()
    {
        if (!g_dxgi.dupl) return false;

        DXGI_OUTDUPL_FRAME_INFO info;
        IDXGIResource* resource = 0;

        HRESULT hr = g_dxgi.dupl->AcquireNextFrame(0, &info, &resource);

        if (hr == DXGI_ERROR_WAIT_TIMEOUT)
        {
            // Nothing on screen changed. The previous image is still correct,
            // and saying so costs nothing - this is most of why duplication is
            // cheaper than blitting the desktop thirty times a second.
            return g_dxgi.have_frame;
        }

        if (FAILED(hr))
        {
            // Access is lost on a resolution change, a full screen game taking
            // over, or a secure desktop. Rebuilding is the documented answer.
            if (hr == DXGI_ERROR_ACCESS_LOST)
            {
                release(&g_dxgi.dupl);
                if (dxgi_open_output(&g_target)) return g_dxgi.have_frame;
            }
            set_error("рабочий стол перестал отдавать кадры");
            return false;
        }

        // A frame is handed over for a mouse move as much as for a redraw, and
        // one of those carries no picture at all - copying it paints the whole
        // screen black. LastPresentTime is what tells the two apart.
        if (info.LastPresentTime.QuadPart == 0)
        {
            resource->Release();
            g_dxgi.dupl->ReleaseFrame();
            return g_dxgi.have_frame;
        }

        ID3D11Texture2D* frame = 0;
        if (SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&frame)))
        {
            g_dxgi.context->CopyResource(g_dxgi.staging, frame);
            frame->Release();

            D3D11_MAPPED_SUBRESOURCE mapped;
            if (SUCCEEDED(g_dxgi.context->Map(g_dxgi.staging, 0, D3D11_MAP_READ, 0, &mapped)))
            {
                int rows = g_dxgi.height < g_blt.full_h ? g_dxgi.height : g_blt.full_h;
                int width = g_dxgi.width < g_blt.full_w ? g_dxgi.width : g_blt.full_w;

                // The dib is bottom up, which is what GDI made it and what the
                // scaler below already expects.
                for (int y = 0; y < rows; y++)
                {
                    const unsigned char* src =
                        (const unsigned char*)mapped.pData + (size_t)y * mapped.RowPitch;
                    // Row for row. The dib is created with a negative height,
                    // which makes it top down - the same order the duplicated
                    // texture arrives in - so nothing needs turning over.
                    // Flipping here, which is what this did at first, put the
                    // whole stream upside down for everybody watching.
                    unsigned char* dst =
                        g_blt.full_pixels + (size_t)y * g_blt.full_w * 4;

                    ccpy(dst, src, (size_t)width * 4);
                }

                g_dxgi.context->Unmap(g_dxgi.staging, 0);
                g_dxgi.have_frame = true;
            }
        }

        resource->Release();
        g_dxgi.dupl->ReleaseFrame();
        return g_dxgi.have_frame;
    }
}

bool capture::set_method(capture_method m)
{
    if (m < 0 || m >= CAPTURE_METHOD_COUNT) return false;
    if (g_running) return false;      // swapping mid-stream would drop frames
    g_method = m;
    return true;
}

const char* capture::method_name(capture_method m)
{
    switch (m)
    {
    case CAPTURE_DXGI:   return "DXGI (быстрый)";
    case CAPTURE_BITBLT: return "BitBlt (GDI)";
    default: return "неизвестный";
    }
}

int capture::list_monitors(capture_target* out, int cap)
{
    if (!out || cap <= 0) return 0;

    monitor_scan scan;
    scan.out = out;
    scan.cap = cap;
    scan.count = 0;
    EnumDisplayMonitors(0, 0, on_monitor, (LPARAM)&scan);

    // The primary monitor first: it is what nearly everyone means by "my
    // screen", and it should be the default selection.
    for (int i = 1; i < scan.count; i++)
    {
        if (!out[i].primary) continue;
        capture_target tmp = out[0];
        out[0] = out[i];
        out[i] = tmp;
        break;
    }
    return scan.count;
}

bool capture::describe_window(void* hwnd, capture_target* out)
{
    HWND w = (HWND)hwnd;
    if (!w || !IsWindow(w) || !out) return false;

    RECT r;
    if (!GetWindowRect(w, &r)) return false;

    int width = r.right - r.left;
    int height = r.bottom - r.top;
    if (width <= 0 || height <= 0) return false;

    ccfset(out, 0, sizeof(*out));
    out->window = w;
    out->x = r.left;
    out->y = r.top;
    out->width = width;
    out->height = height;

    wchar_t title[128];
    int n = GetWindowTextW(w, title, 128);
    if (n > 0) wcstochar(title, out->name, (int)sizeof(out->name));
    else ccstrncpy(out->name, "Окно", sizeof(out->name) - 1);

    return true;
}

bool capture::start(const capture_target* target, int max_width, int max_height, int fps,
                    bool letterbox)
{
    if (!target) { set_error("источник не задан"); return false; }
    if (!g_ready) { set_error("capture::init не вызывался"); return false; }
    stop();

    if (target->width <= 0 || target->height <= 0)
    {
        set_error("у источника нулевой размер");
        return false;
    }
    if (target->width > MAX_CAPTURE_DIMENSION || target->height > MAX_CAPTURE_DIMENSION)
    {
        set_error("источник слишком большой");
        return false;
    }

    if (max_width <= 0) max_width = target->width;
    if (max_height <= 0) max_height = target->height;
    if (fps <= 0) fps = 30;
    if (fps > 60) fps = 60;

    // Fit inside the limit without changing the shape of the picture, and never
    // upscale: sending more pixels than were captured buys nothing.
    int out_w = target->width;
    int out_h = target->height;
    if (out_w > max_width || out_h > max_height)
    {
        // Integer arithmetic throughout, so the two axes cannot disagree about
        // which of them is the binding one.
        long long by_width = (long long)max_width * out_h;
        long long by_height = (long long)max_height * out_w;
        if (by_width < by_height)
        {
            out_h = (int)(by_width / out_w);
            out_w = max_width;
        }
        else
        {
            out_w = (int)(by_height / out_h);
            out_h = max_height;
        }
    }

    // H.264 chroma is subsampled by two on both axes, so odd sizes are not
    // representable.
    out_w &= ~1;
    out_h &= ~1;
    if (out_w < 2) out_w = 2;
    if (out_h < 2) out_h = 2;

    // With bars the frame stays exactly the size that was asked for, and the
    // picture sits in the middle of it. A monitor whose shape differs from the
    // stream would otherwise yield an odd frame size.
    int box_w = out_w;
    int box_h = out_h;
    if (letterbox)
    {
        box_w = max_width & ~1;
        box_h = max_height & ~1;
        if (out_w > box_w) out_w = box_w;
        if (out_h > box_h) out_h = box_h;
    }

    g_target = *target;
    g_out_w = box_w;
    g_out_h = box_h;

    ccfset(&g_map, 0, sizeof(g_map));
    g_map.src_w = target->width;
    g_map.src_h = target->height;
    g_map.dst_w = out_w;
    g_map.dst_h = out_h;
    g_map.frame_w = box_w;
    g_map.frame_h = box_h;
    // Bars are split evenly, which is what the grabber does when it centres
    // the picture.
    g_map.dst_x = (box_w - out_w) / 2;
    g_map.dst_y = (box_h - out_h) / 2;

    if (target->window)
    {
        // A window is captured from its own top left, so desktop coordinates
        // have to be shifted by wherever it happens to be sitting.
        RECT r;
        if (GetWindowRect((HWND)target->window, &r))
        {
            g_map.src_x = r.left;
            g_map.src_y = r.top;
        }
    }
    else
    {
        g_map.src_x = target->x;
        g_map.src_y = target->y;
    }
    g_interval_us = 1000000ULL / (unsigned long long)fps;
    g_next_due_us = 0;
    g_captured = 0;
    g_dropped = 0;
    g_error[0] = 0;

    bool ok = false;
    switch (g_method)
    {
    case CAPTURE_DXGI:
        // The blit path owns the dib, the cursor and the scaler; duplication
        // only replaces where the desktop image comes from, so both are
        // started and the fast one fills the same buffer.
        ok = blt_start(&g_target, out_w, out_h, box_w, box_h) && dxgi_start(&g_target);
        if (!ok)
        {
            log_line("capture: DXGI не пошёл (%s), возвращаюсь на BitBlt", g_error);
            dxgi_free();
            g_error[0] = 0;
            g_method = CAPTURE_BITBLT;
            ok = g_blt.full_pixels != 0 || blt_start(&g_target, out_w, out_h, box_w, box_h);
        }
        break;

    case CAPTURE_BITBLT: ok = blt_start(&g_target, out_w, out_h, box_w, box_h); break;
    default: set_error("метод захвата не поддерживается"); break;
    }

    if (!ok)
    {
        log_line("capture: %s не запустился: %s", method_name(g_method), g_error);
        return false;
    }

    g_running = true;
    log_line("capture: %s, %s, %dx%d -> %dx%d в кадре %dx%d при %d к/с",
             method_name(g_method), g_target.name,
             g_target.width, g_target.height, out_w, out_h, box_w, box_h, fps);
    return true;
}

void capture::stop()
{
    if (!g_running) return;
    g_running = false;

    switch (g_method)
    {
    case CAPTURE_DXGI:   dxgi_free(); blt_free(); break;
    case CAPTURE_BITBLT: blt_free(); break;
    default: break;
    }

    log_line("capture: остановлен, кадров %u, пропущено %u", g_captured, g_dropped);
    g_out_w = 0;
    g_out_h = 0;
}

bool capture::running() { return g_running; }
int capture::width() { return g_out_w; }
int capture::height() { return g_out_h; }

bool capture::grab(capture_frame* out)
{
    if (!g_running || !out) return false;

    unsigned long long now = now_us();
    if (g_next_due_us == 0) g_next_due_us = now;    // the first frame is due at once
    if (now < g_next_due_us) return false;

    // The next slot is measured from this one rather than from when the grab
    // finishes, so the cost of a grab does not add itself to the frame period.
    g_next_due_us += g_interval_us;

    // A grab long enough to miss whole slots gives them up instead of building
    // a backlog that then gets rushed through.
    if (g_next_due_us <= now)
    {
        g_dropped += (unsigned int)((now - g_next_due_us) / g_interval_us) + 1;
        g_next_due_us = now + g_interval_us;
    }

    bool ok = false;
    switch (g_method)
    {
    case CAPTURE_DXGI:   ok = blt_grab(out, true); break;
    case CAPTURE_BITBLT: ok = blt_grab(out, false); break;
    default: break;
    }

    if (!ok) return false;

    out->time_us = now;
    g_captured++;
    return true;
}

void capture::set_capture_cursor(bool on) { g_cursor = on; }
bool capture::capture_cursor() { return g_cursor; }

unsigned int capture::frames_captured() { return g_captured; }
unsigned int capture::frames_dropped() { return g_dropped; }
const char* capture::last_error() { return g_error; }

bool capture::mapping(capture_mapping* out)
{
    if (!g_running || !out) return false;
    *out = g_map;
    return true;
}

namespace
{
    struct window_scan
    {
        capture_target* out;
        int cap;
        int count;
    };

    BOOL CALLBACK collect_window(HWND hwnd, LPARAM param)
    {
        window_scan* scan = (window_scan*)param;
        if (scan->count >= scan->cap) return FALSE;

        // Hidden outright is the only thing that disqualifies a window from
        // the list. Everything else here is about whether it is on screen
        // right now, which is a different question: somebody wants to mark a
        // window they are about to open just as much as one already up.
        if (!IsWindowVisible(hwnd)) return TRUE;

        LONG ex = GetWindowLongW(hwnd, GWL_EXSTYLE);
        // A tool window is a palette or a tooltip and belongs to something
        // else, unless it has asked to be treated as an application in its
        // own right.
        if ((ex & WS_EX_TOOLWINDOW) && !(ex & WS_EX_APPWINDOW)) return TRUE;

        wchar_t title[192];
        if (GetWindowTextW(hwnd, title, 192) <= 0) return TRUE;

        bool minimized = IsIconic(hwnd) != 0;

        // Cloaked means the compositor is not drawing it: another virtual
        // desktop, or a store app that has been suspended. A minimised one is
        // cloaked as well, and that is the case worth keeping - the window is
        // real and will come back.
        if (!minimized && oslib::window_cloaked(hwnd)) return TRUE;

        RECT r;
        if (!GetWindowRect(hwnd, &r)) return TRUE;

        if (!minimized)
        {
            if (r.right - r.left < 80 || r.bottom - r.top < 60) return TRUE;

            // Parked off the desktop entirely. Some windows are minimised
            // without ever setting the iconic flag, and this is what that
            // looks like from the outside.
            int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
            int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
            if (r.right <= vx || r.bottom <= vy ||
                r.left >= vx + GetSystemMetrics(SM_CXVIRTUALSCREEN) ||
                r.top >= vy + GetSystemMetrics(SM_CYVIRTUALSCREEN))
            {
                minimized = true;
            }
        }

        capture_target* t = &scan->out[scan->count];
        ccfset(t, 0, sizeof(*t));
        t->window = hwnd;
        t->minimized = minimized;
        if (!minimized)
        {
            t->x = r.left;
            t->y = r.top;
            t->width = r.right - r.left;
            t->height = r.bottom - r.top;
        }
        wcstochar(title, t->name, (int)sizeof(t->name));

        scan->count++;
        return TRUE;
    }
}

int capture::list_windows(capture_target* out, int cap)
{
    if (!out || cap <= 0) return 0;

    window_scan scan;
    scan.out = out;
    scan.cap = cap;
    scan.count = 0;

    EnumWindows(collect_window, (LPARAM)&scan);
    return scan.count;
}

bool capture::self_test(capture_method method, const wchar_t* out_path)
{
    capture_target monitors[8];
    int count = capture::list_monitors(monitors, 8);
    if (count <= 0) { log_line("capturetest: экранов не нашлось"); return false; }

    capture::init();
    capture::set_method(method);
    capture::set_capture_cursor(false);

    if (!capture::start(&monitors[0], monitors[0].width, monitors[0].height, 30, false))
    {
        log_line("capturetest: %s не запустился: %s", method_name(method), g_error);
        return false;
    }

    // Duplication says nothing until something on screen changes, so the very
    // first grab can legitimately be empty. Give it a few tries.
    capture_frame f;
    bool got = false;
    for (int i = 0; i < 60 && !got; i++)
    {
        got = capture::grab(&f);
        if (!got) Sleep(20);
    }

    if (!got)
    {
        log_line("capturetest: кадр не пришёл");
        capture::stop();
        return false;
    }

    log_line("capturetest: %s, кадр %dx%d, шаг %d", method_name(method),
             f.width, f.height, f.stride);

    // A top-down bitmap: negative height means row zero of the file is the top
    // of the picture. So whatever the buffer holds first ends up at the top,
    // and looking at the file answers which way up the buffer is.
    BITMAPFILEHEADER fh;
    BITMAPINFOHEADER ih;
    ccfset(&fh, 0, sizeof(fh));
    ccfset(&ih, 0, sizeof(ih));

    unsigned int pixels = (unsigned int)(f.width * f.height * 4);

    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(fh) + sizeof(ih);
    fh.bfSize = fh.bfOffBits + pixels;

    ih.biSize = sizeof(ih);
    ih.biWidth = f.width;
    ih.biHeight = -f.height;
    ih.biPlanes = 1;
    ih.biBitCount = 32;
    ih.biCompression = BI_RGB;

    ubuffer out;
    out.init(pixels + 128);
    out.append(&fh, sizeof(fh));
    out.append(&ih, sizeof(ih));
    for (int y = 0; y < f.height; y++)
        out.append(f.bgra + (size_t)y * f.stride, (unsigned int)(f.width * 4));

    bool written = ufile::write_all(out_path, out.data, out.size);
    out.free_buffer();

    capture::stop();

    log_line("capturetest: %s", written ? "снимок записан" : "снимок не записался");
    return written;
}
