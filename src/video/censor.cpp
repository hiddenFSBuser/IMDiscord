#include "pch.h"
#include <dwmapi.h>
#include "censor.h"
#include "core/log.h"
#include "core/oslib.h"
#include "ufile.h"

// The implementation lives in textures.cpp; this only needs the declarations.
#include "stb/stb_image.h"

namespace
{
    censor_entry g_entries[CENSOR_MAX];
    int g_count = 0;

    CRITICAL_SECTION g_lock;
    bool g_ready = false;

    // The cover picture, decoded once and held as RGBA.
    unsigned char* g_cover = 0;
    int g_cover_w = 0, g_cover_h = 0;
    char g_cover_name[128] = { 0 };

    // A five by seven face for each letter of CENSORED, one bit per pixel with
    // the low bit on the left. A whole font would be a file; eight letters is
    // eight numbers, and the word never changes unless somebody edits it here.
    struct glyph
    {
        char ch;
        unsigned char rows[7];
    };

    const glyph GLYPHS[] = {
        { 'C', { 0x0E, 0x11, 0x01, 0x01, 0x01, 0x11, 0x0E } },
        { 'E', { 0x1F, 0x01, 0x01, 0x0F, 0x01, 0x01, 0x1F } },
        { 'N', { 0x11, 0x13, 0x15, 0x15, 0x19, 0x11, 0x11 } },
        { 'S', { 0x1E, 0x01, 0x01, 0x0E, 0x10, 0x10, 0x0F } },
        { 'O', { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
        { 'R', { 0x0F, 0x11, 0x11, 0x0F, 0x05, 0x09, 0x11 } },
        { 'D', { 0x07, 0x09, 0x11, 0x11, 0x11, 0x09, 0x07 } },
    };

    const glyph* find_glyph(char c)
    {
        for (unsigned int i = 0; i < sizeof(GLYPHS) / sizeof(GLYPHS[0]); i++)
            if (GLYPHS[i].ch == c) return &GLYPHS[i];
        return 0;
    }

    inline void put_pixel(unsigned char* bgra, int stride, int x, int y,
                          unsigned char b, unsigned char g, unsigned char r)
    {
        unsigned char* p = bgra + (size_t)y * stride + (size_t)x * 4;
        p[0] = b;
        p[1] = g;
        p[2] = r;
        p[3] = 255;
    }

    void fill_rect(unsigned char* bgra, int stride, int x0, int y0, int x1, int y1,
                   unsigned char b, unsigned char g, unsigned char r)
    {
        for (int y = y0; y < y1; y++)
        {
            unsigned char* row = bgra + (size_t)y * stride + (size_t)x0 * 4;
            for (int x = x0; x < x1; x++)
            {
                row[0] = b;
                row[1] = g;
                row[2] = r;
                row[3] = 255;
                row += 4;
            }
        }
    }

    // Draws the word centred in the box, at whatever size fits.
    void draw_word(unsigned char* bgra, int stride, int x0, int y0, int x1, int y1)
    {
        static const char* WORD = "CENSORED";
        const int letters = 8;
        const int gw = 5, gh = 7, gap = 1;

        int box_w = x1 - x0;
        int box_h = y1 - y0;
        if (box_w < 24 || box_h < 12) return;

        // Leave a third of the width as margin, then take the largest whole
        // scale that still fits.
        int want_w = letters * gw + (letters - 1) * gap;
        int scale = (box_w * 2 / 3) / want_w;
        int by_height = (box_h / 3) / gh;
        if (by_height < scale) scale = by_height;
        if (scale < 1) scale = 1;
        if (scale > 24) scale = 24;

        int text_w = want_w * scale;
        int text_h = gh * scale;
        int at_x = x0 + (box_w - text_w) / 2;
        int at_y = y0 + (box_h - text_h) / 2;

        for (int i = 0; i < letters; i++)
        {
            const glyph* g = find_glyph(WORD[i]);
            int left = at_x + i * (gw + gap) * scale;
            if (!g) continue;

            for (int row = 0; row < gh; row++)
            {
                unsigned char bits = g->rows[row];
                for (int col = 0; col < gw; col++)
                {
                    if (!(bits & (1 << col))) continue;

                    int px = left + col * scale;
                    int py = at_y + row * scale;

                    for (int sy = 0; sy < scale; sy++)
                        for (int sx = 0; sx < scale; sx++)
                        {
                            int x = px + sx, y = py + sy;
                            if (x < x0 || x >= x1 || y < y0 || y >= y1) continue;
                            put_pixel(bgra, stride, x, y, 235, 235, 235);
                        }
                }
            }
        }
    }

    // Where a window really is, or false when it is not on screen.
    //
    // Minimised is only one of several ways for a window to be gone, and the
    // obvious check catches the fewest of them:
    //
    //  - IsIconic is false for plenty of windows that are minimised anyway.
    //    Store apps and anything with its own title bar are usually cloaked
    //    by the compositor instead, and go on reporting themselves visible.
    //  - A window on another virtual desktop is cloaked too, and equally not
    //    on the screen being captured.
    //  - A minimised window is parked at -32000, which is a real rectangle as
    //    far as arithmetic is concerned. It has to be rejected by looking at
    //    where it actually is.
    //
    // GetWindowRect is also wrong by a few pixels on its own: since windows
    // ten it includes an invisible resize border, so a box drawn from it
    // spills past the window. The compositor knows the true frame.
    bool window_bounds(HWND hwnd, RECT* out)
    {
        if (!IsWindow(hwnd) || !IsWindowVisible(hwnd) || IsIconic(hwnd)) return false;

        if (oslib::window_cloaked(hwnd)) return false;

        RECT r;
        if (!oslib::window_frame_bounds(hwnd, &r))
        {
            if (!GetWindowRect(hwnd, &r)) return false;
        }

        if (r.right <= r.left || r.bottom <= r.top) return false;

        // Everything the desktop spans, including monitors left of and above
        // the primary one, which is why this is not simply zero based.
        int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
        int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
        int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);

        // A window parked off the desktop is not being shown to anybody. This
        // is what a minimised window looks like when nothing else caught it.
        if (r.right <= vx || r.bottom <= vy ||
            r.left >= vx + vw || r.top >= vy + vh)
            return false;

        *out = r;
        return true;
    }

    // Everything above a censored window can be hiding it, and something that
    // is hidden must not be painted over: the viewer is looking at whatever is
    // on top, and a black box there covers the wrong thing entirely.
    //
    // The z order is taken once per frame rather than per censored window,
    // because walking it is the same walk every time.
    struct stacked_window
    {
        HWND hwnd;
        RECT rect;
        bool solid;      // safe to treat as hiding what is under it
    };

    const int MAX_STACK = 96;
    stacked_window g_stack[MAX_STACK];
    int g_stack_count = 0;

    BOOL CALLBACK collect_stacked(HWND hwnd, LPARAM)
    {
        if (g_stack_count >= MAX_STACK) return FALSE;
        if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) return TRUE;

        RECT r;
        if (!GetWindowRect(hwnd, &r)) return TRUE;
        if (r.right <= r.left || r.bottom <= r.top) return TRUE;

        stacked_window* w = &g_stack[g_stack_count];
        w->hwnd = hwnd;
        w->rect = r;
        w->solid = true;

        // A window that is see-through, click-through, or not being drawn by
        // the compositor at all does not hide what is behind it. Where there
        // is any doubt the answer has to be "not solid": treating something
        // as an occluder wrongly means failing to censor, and that is the
        // mistake that matters here.
        LONG ex = GetWindowLongW(hwnd, GWL_EXSTYLE);
        if (ex & WS_EX_TRANSPARENT) w->solid = false;

        if (w->solid && (ex & WS_EX_LAYERED))
        {
            BYTE alpha = 255;
            DWORD flags = 0;
            COLORREF key = 0;
            if (GetLayeredWindowAttributes(hwnd, &key, &alpha, &flags))
            {
                if ((flags & LWA_ALPHA) && alpha < 250) w->solid = false;
                if (flags & LWA_COLORKEY) w->solid = false;
            }
            else
            {
                // Per pixel alpha, which is the usual shape of an overlay.
                w->solid = false;
            }
        }

        if (w->solid && oslib::window_cloaked(hwnd)) w->solid = false;

        g_stack_count++;
        return TRUE;
    }

    void snapshot_stack()
    {
        g_stack_count = 0;
        // EnumWindows walks the z order from the top down, so everything
        // before a window in this list is in front of it.
        EnumWindows(collect_stacked, 0);
    }

    // The part of `target` that nothing above it is covering, as a region in
    // desktop coordinates. Null when it is completely hidden.
    HRGN visible_region(HWND target, const RECT* bounds)
    {
        HRGN region = CreateRectRgn(bounds->left, bounds->top, bounds->right, bounds->bottom);
        if (!region) return 0;

        for (int i = 0; i < g_stack_count; i++)
        {
            if (g_stack[i].hwnd == target) break;      // everything after is behind
            if (!g_stack[i].solid) continue;

            const RECT* r = &g_stack[i].rect;
            if (r->right <= bounds->left || r->left >= bounds->right ||
                r->bottom <= bounds->top || r->top >= bounds->bottom)
                continue;

            HRGN over = CreateRectRgn(r->left, r->top, r->right, r->bottom);
            if (!over) continue;

            CombineRgn(region, region, over, RGN_DIFF);
            DeleteObject(over);
        }

        return region;
    }

    // Nearest neighbour, because the cover is usually a solid picture and a
    // smooth resize would cost more than it is worth on every frame.
    void blit_cover(unsigned char* bgra, int stride, int x0, int y0, int x1, int y1)
    {
        int box_w = x1 - x0;
        int box_h = y1 - y0;
        if (box_w <= 0 || box_h <= 0 || !g_cover) return;

        for (int y = 0; y < box_h; y++)
        {
            int sy = (int)((long long)y * g_cover_h / box_h);
            if (sy >= g_cover_h) sy = g_cover_h - 1;

            unsigned char* dst = bgra + (size_t)(y0 + y) * stride + (size_t)x0 * 4;
            const unsigned char* src_row = g_cover + (size_t)sy * g_cover_w * 4;

            for (int x = 0; x < box_w; x++)
            {
                int sx = (int)((long long)x * g_cover_w / box_w);
                if (sx >= g_cover_w) sx = g_cover_w - 1;

                const unsigned char* src = src_row + (size_t)sx * 4;

                // The cover may have transparency; anything see-through would
                // show what is being hidden, so it is composed over black.
                unsigned int alpha = src[3];
                dst[0] = (unsigned char)(src[2] * alpha / 255);
                dst[1] = (unsigned char)(src[1] * alpha / 255);
                dst[2] = (unsigned char)(src[0] * alpha / 255);
                dst[3] = 255;
                dst += 4;
            }
        }
    }
}

void censor::init()
{
    if (g_ready) return;
    InitializeCriticalSection(&g_lock);
    ccfset(g_entries, 0, sizeof(g_entries));
    g_count = 0;
    g_ready = true;
}

void censor::shutdown()
{
    if (!g_ready) return;
    clear_cover_image();
    DeleteCriticalSection(&g_lock);
    g_ready = false;
}

bool censor::is_censored(void* window)
{
    if (!g_ready || !window) return false;

    EnterCriticalSection(&g_lock);
    bool found = false;
    for (int i = 0; i < g_count && !found; i++)
        if (g_entries[i].window == window) found = true;
    LeaveCriticalSection(&g_lock);
    return found;
}

void censor::add(void* window, const char* title)
{
    if (!g_ready) init();
    if (!window || is_censored(window)) return;

    EnterCriticalSection(&g_lock);
    if (g_count < CENSOR_MAX)
    {
        g_entries[g_count].window = window;
        ccfset(g_entries[g_count].title, 0, sizeof(g_entries[g_count].title));
        if (title) ccstrncpy(g_entries[g_count].title, title,
                             sizeof(g_entries[g_count].title) - 1);
        g_count++;
    }
    LeaveCriticalSection(&g_lock);

    log_line("censor: закрыто окно \"%s\"", title ? title : "?");
}

void censor::remove(void* window)
{
    if (!g_ready) return;

    EnterCriticalSection(&g_lock);
    for (int i = 0; i < g_count; i++)
    {
        if (g_entries[i].window != window) continue;
        for (int k = i + 1; k < g_count; k++) g_entries[k - 1] = g_entries[k];
        g_count--;
        break;
    }
    LeaveCriticalSection(&g_lock);
}

void censor::clear()
{
    if (!g_ready) return;
    EnterCriticalSection(&g_lock);
    g_count = 0;
    LeaveCriticalSection(&g_lock);
}

int censor::count()
{
    return g_ready ? g_count : 0;
}

const censor_entry* censor::at(int index)
{
    if (!g_ready || index < 0 || index >= g_count) return 0;
    return &g_entries[index];
}

bool censor::set_cover_image(const wchar_t* path)
{
    if (!path || !path[0]) { clear_cover_image(); return true; }

    ubuffer file;
    file.init();
    if (!ufile::read_all(path, &file) || !file.size)
    {
        file.free_buffer();
        log_line("censor: картинка не читается");
        return false;
    }

    int w = 0, h = 0, channels = 0;
    unsigned char* pixels = stbi_load_from_memory(file.data, (int)file.size,
                                                  &w, &h, &channels, 4);
    file.free_buffer();

    if (!pixels || w <= 0 || h <= 0)
    {
        if (pixels) stbi_image_free(pixels);
        log_line("censor: картинка не разобралась");
        return false;
    }

    EnterCriticalSection(&g_lock);
    if (g_cover) stbi_image_free(g_cover);
    g_cover = pixels;
    g_cover_w = w;
    g_cover_h = h;

    // Just the file name, for the settings line.
    const char* name = 0;
    static char narrow[260];
    wcstochar(path, narrow, (int)sizeof(narrow));
    name = narrow;
    for (const char* p = narrow; *p; p++)
        if (*p == '\\' || *p == '/') name = p + 1;

    ccfset(g_cover_name, 0, sizeof(g_cover_name));
    ccstrncpy(g_cover_name, name, sizeof(g_cover_name) - 1);
    LeaveCriticalSection(&g_lock);

    log_line("censor: закрывающая картинка %dx%d (%s)", w, h, g_cover_name);
    return true;
}

void censor::clear_cover_image()
{
    if (!g_ready) return;

    EnterCriticalSection(&g_lock);
    if (g_cover) { stbi_image_free(g_cover); g_cover = 0; }
    g_cover_w = 0;
    g_cover_h = 0;
    g_cover_name[0] = 0;
    LeaveCriticalSection(&g_lock);
}

bool censor::has_cover_image() { return g_cover != 0; }
const char* censor::cover_name() { return g_cover_name; }

bool censor::active()
{
    return g_ready && g_count > 0;
}

int censor::apply(unsigned char* bgra, int width, int height, int stride,
                  const capture_mapping* map)
{
    if (!g_ready || !bgra || !map || g_count == 0) return 0;
    if (map->src_w <= 0 || map->src_h <= 0) return 0;

    int covered = 0;

    snapshot_stack();

    EnterCriticalSection(&g_lock);

    for (int i = 0; i < g_count; i++)
    {
        HWND hwnd = (HWND)g_entries[i].window;

        RECT r;
        if (!window_bounds(hwnd, &r)) continue;

        // Only the part nothing else is sitting on top of. A window entirely
        // behind another one gets no box at all, which is the whole point:
        // the viewer is looking at the window in front, and covering that up
        // hides the wrong thing.
        HRGN region = visible_region(hwnd, &r);
        if (!region) continue;

        DWORD bytes = GetRegionData(region, 0, 0);
        if (!bytes) { DeleteObject(region); continue; }

        RGNDATA* data = (RGNDATA*)memalloc((int)bytes);
        if (!data) { DeleteObject(region); continue; }

        if (GetRegionData(region, bytes, data) != bytes || data->rdh.nCount == 0)
        {
            memfree(data);
            DeleteObject(region);
            continue;
        }

        double sx = (double)map->dst_w / (double)map->src_w;
        double sy = (double)map->dst_h / (double)map->src_h;

        // The word goes in the middle of the window as a whole, not of each
        // piece, so a partly covered window still reads as one box.
        int word_x0 = width, word_y0 = height, word_x1 = 0, word_y1 = 0;
        bool drew = false;

        const RECT* pieces = (const RECT*)data->Buffer;
        for (DWORD k = 0; k < data->rdh.nCount; k++)
        {
            const RECT* piece = &pieces[k];

            // Desktop to frame: shift by what is being captured, scale by how
            // much it was shrunk, then shift again by the letterbox bars.
            int x0 = map->dst_x + (int)((double)(piece->left - map->src_x) * sx);
            int y0 = map->dst_y + (int)((double)(piece->top - map->src_y) * sy);
            int x1 = map->dst_x + (int)((double)(piece->right - map->src_x) * sx);
            int y1 = map->dst_y + (int)((double)(piece->bottom - map->src_y) * sy);

            if (x0 < 0) x0 = 0;
            if (y0 < 0) y0 = 0;
            if (x1 > width) x1 = width;
            if (y1 > height) y1 = height;
            if (x1 <= x0 || y1 <= y0) continue;

            if (g_cover) blit_cover(bgra, stride, x0, y0, x1, y1);
            else         fill_rect(bgra, stride, x0, y0, x1, y1, 0, 0, 0);

            if (x0 < word_x0) word_x0 = x0;
            if (y0 < word_y0) word_y0 = y0;
            if (x1 > word_x1) word_x1 = x1;
            if (y1 > word_y1) word_y1 = y1;
            drew = true;
        }

        memfree(data);
        DeleteObject(region);

        if (!drew) continue;

        if (!g_cover) draw_word(bgra, stride, word_x0, word_y0, word_x1, word_y1);
        covered++;
    }

    LeaveCriticalSection(&g_lock);
    return covered;
}
