#include "pch.h"
#include <d3d11.h>
#include <dxgi.h>

#include "app.h"
#include "gfxload.h"
#include "oslib.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "system/io/ufile.h"
#include "ui/ui.h"
#include "discord/science.h"
#include "ui/theme.h"
#include "log.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "ole32.lib")

app_state g_app;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static const wchar_t* WINDOW_CLASS = L"IMDiscordWindow";

// ---------------------------------------------------------------------------
// device
// ---------------------------------------------------------------------------

static void create_rtv()
{
    ID3D11Texture2D* back = 0;
    g_app.swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back);
    if (!back) return;
    g_app.device->CreateRenderTargetView(back, 0, &g_app.rtv);
    back->Release();
}

static void release_rtv()
{
    if (g_app.rtv)
    {
        g_app.rtv->Release();
        g_app.rtv = 0;
    }
}

static bool create_device(HWND hwnd)
{
    DXGI_SWAP_CHAIN_DESC sd;
    ccfset(&sd, 0, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    oslib::init();
    if (!gfx::load()) return false;
    pfn_D3D11CreateDeviceAndSwapChain create = gfx::create_device();
    if (!create) return false;

    // Down to 9_1, which is what a card from the direct3d 9 era reports. The
    // runtime maps the whole api onto that hardware itself - the "10level9"
    // path - so a separate d3d9 renderer would buy nothing here: everything
    // this client draws is flat triangles and one texture at a time. What it
    // does buy is that the failure to start is a message, not a black window.
    D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
        D3D_FEATURE_LEVEL_9_3,
        D3D_FEATURE_LEVEL_9_2,
        D3D_FEATURE_LEVEL_9_1,
    };
    const UINT level_count = (UINT)(sizeof(levels) / sizeof(levels[0]));
    D3D_FEATURE_LEVEL got = D3D_FEATURE_LEVEL_9_1;

    HRESULT hr = create(0, D3D_DRIVER_TYPE_HARDWARE, 0, 0, levels, level_count,
                        D3D11_SDK_VERSION, &sd, &g_app.swapchain, &g_app.device,
                        &got, &g_app.context);

    if (FAILED(hr))
    {
        // No usable card, or a driver that will not do any of it. The software
        // renderer is slow but it draws, and a chat client that draws slowly
        // still beats one that does not open.
        log_line("gfx: аппаратное устройство не создалось (0x%08x), пробуем программное",
                 (unsigned int)hr);

        hr = create(0, D3D_DRIVER_TYPE_WARP, 0, 0, levels, level_count,
                    D3D11_SDK_VERSION, &sd, &g_app.swapchain, &g_app.device,
                    &got, &g_app.context);
    }

    if (FAILED(hr)) return false;

    log_line("gfx: уровень возможностей %x.%x", (unsigned int)(got >> 12) & 0xF,
             (unsigned int)(got >> 8) & 0xF);

    create_rtv();
    return true;
}

static void destroy_device()
{
    release_rtv();

    if (g_app.swapchain) { g_app.swapchain->Release(); g_app.swapchain = 0; }
    if (g_app.context) { g_app.context->Release(); g_app.context = 0; }
    if (g_app.device) { g_app.device->Release(); g_app.device = 0; }

    gfx::unload();
}

// ---------------------------------------------------------------------------
// window
// ---------------------------------------------------------------------------

static void take_dropped_files(HDROP drop)
{
    UINT n = DragQueryFileW(drop, 0xFFFFFFFF, 0, 0);
    for (UINT i = 0; i < n; i++)
    {
        UINT len = DragQueryFileW(drop, i, 0, 0);
        if (!len || len > 1000) continue;

        wchar_t* path = (wchar_t*)memalloc((int)((len + 2) * sizeof(wchar_t)));
        if (!path) continue;
        DragQueryFileW(drop, i, path, len + 1);
        g_app.dropped_files.push(path);
    }
    DragFinish(drop);
}

static LRESULT WINAPI wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam))
        return 1;

    switch (msg)
    {
    case WM_SIZE:
        if (wparam != SIZE_MINIMIZED)
        {
            g_app.resize_w = (UINT)LOWORD(lparam);
            g_app.resize_h = (UINT)HIWORD(lparam);
        }
        return 0;

    case WM_SYSCOMMAND:
        // Swallow the alt-menu so alt keybinds stay usable.
        if ((wparam & 0xFFF0) == SC_KEYMENU) return 0;
        break;

    case WM_DROPFILES:
        take_dropped_files((HDROP)wparam);
        return 0;

    case WM_DESTROY:
        g_app.running = false;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

// ---------------------------------------------------------------------------
// clipboard
// ---------------------------------------------------------------------------
// imconfig.h turns off imgui's built-in win32 clipboard, which otherwise leaves
// it talking to a private in-memory buffer that never reaches the OS. These
// handlers bridge CF_UNICODETEXT both ways so copy/paste works with any other
// application.

static ubuffer g_clipboard_utf8;

static const char* clipboard_get(ImGuiContext*)
{
    g_clipboard_utf8.clear();

    if (!OpenClipboard(g_app.hwnd)) return "";

    HANDLE handle = GetClipboardData(CF_UNICODETEXT);
    if (handle)
    {
        const wchar_t* wide = (const wchar_t*)GlobalLock(handle);
        if (wide)
        {
            int need = WideCharToMultiByte(CP_UTF8, 0, wide, -1, 0, 0, 0, 0);
            if (need > 1)
            {
                g_clipboard_utf8.reserve((unsigned int)need);
                if (g_clipboard_utf8.cap >= (unsigned int)need + 1)
                {
                    WideCharToMultiByte(CP_UTF8, 0, wide, -1, (char*)g_clipboard_utf8.data, need, 0, 0);
                    g_clipboard_utf8.size = (unsigned int)need - 1; // drop the terminator
                }
            }
            GlobalUnlock(handle);
        }
    }

    CloseClipboard();
    return g_clipboard_utf8.c_str();
}

static void clipboard_set(ImGuiContext*, const char* text)
{
    if (!text) return;
    if (!OpenClipboard(g_app.hwnd)) return;

    EmptyClipboard();

    int wide_len = MultiByteToWideChar(CP_UTF8, 0, text, -1, 0, 0);
    if (wide_len > 0)
    {
        HGLOBAL block = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)wide_len * sizeof(wchar_t));
        if (block)
        {
            wchar_t* wide = (wchar_t*)GlobalLock(block);
            if (wide)
            {
                MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, wide_len);
                GlobalUnlock(block);
                // On success the clipboard owns the block; do not free it.
                if (!SetClipboardData(CF_UNICODETEXT, block)) GlobalFree(block);
            }
            else
            {
                GlobalFree(block);
            }
        }
    }

    CloseClipboard();
}

// ---------------------------------------------------------------------------
// fonts
// ---------------------------------------------------------------------------

static ImFont* load_system_font(const wchar_t* file, float size, const ImWchar* ranges)
{
    wchar_t path[MAX_PATH];
    UINT n = GetWindowsDirectoryW(path, MAX_PATH);
    if (!n) return 0;

    const wchar_t* suffix = L"\\Fonts\\";
    int i = (int)n;
    for (int j = 0; suffix[j] && i < MAX_PATH - 1; j++) path[i++] = suffix[j];
    for (int j = 0; file[j] && i < MAX_PATH - 1; j++) path[i++] = file[j];
    path[i] = 0;

    ubuffer buf;
    buf.init();
    if (!ufile::read_all(path, &buf))
    {
        buf.free_buffer();
        return 0;
    }

    unsigned int size_bytes = 0;
    void* owned = buf.take(&size_bytes);

    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = true; // freed through IM_FREE == memfree
    cfg.OversampleH = 2;
    cfg.OversampleV = 1;
    cfg.PixelSnapH = true;

    return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(owned, (int)size_bytes, size, &cfg, ranges);
}

static void build_fonts()
{
    ImGuiIO& io = ImGui::GetIO();

    // Cyrillic + latin + punctuation; the app ships no font of its own, so the
    // system UI face is used and the built-in atlas font is the fallback.
    static const ImWchar ranges[] =
    {
        0x0020, 0x00FF,
        0x0100, 0x017F,
        0x0370, 0x03FF,
        0x0400, 0x04FF,
        0x2000, 0x206F,
        0x2190, 0x21FF,
        0x2500, 0x25FF,
        0x2600, 0x27BF,
        0,
    };

    g_app.font_text = load_system_font(L"segoeui.ttf", 16.0f, ranges);
    g_app.font_bold = load_system_font(L"seguisb.ttf", 16.0f, ranges);
    if (!g_app.font_bold) g_app.font_bold = load_system_font(L"segoeuib.ttf", 16.0f, ranges);
    g_app.font_big = load_system_font(L"seguisb.ttf", 22.0f, ranges);
    if (!g_app.font_big) g_app.font_big = load_system_font(L"segoeuib.ttf", 22.0f, ranges);

    if (!g_app.font_text) g_app.font_text = io.Fonts->AddFontDefault();
    if (!g_app.font_bold) g_app.font_bold = g_app.font_text;
    if (!g_app.font_big) g_app.font_big = g_app.font_bold;
    g_app.font_icon = g_app.font_text;

    io.FontDefault = g_app.font_text;
}

// ---------------------------------------------------------------------------
// entry
// ---------------------------------------------------------------------------

void app_request_close()
{
    g_app.running = false;
}

int app_main()
{
    ccfset(&g_app, 0, sizeof(g_app));
    g_app.dropped_files = ulist<wchar_t*>();

    CoInitializeEx(0, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    HINSTANCE inst = GetModuleHandleW(0);

    WNDCLASSEXW wc;
    ccfset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(0, IDC_ARROW);
    wc.lpszClassName = WINDOW_CLASS;
    RegisterClassExW(&wc);

    g_app.hwnd = CreateWindowExW(
        0, WINDOW_CLASS, L"IMDiscord", WS_OVERLAPPEDWINDOW,
        100, 100, 1280, 800, 0, 0, inst, 0);

    if (!g_app.hwnd)
    {
        UnregisterClassW(WINDOW_CLASS, inst);
        return 1;
    }

    if (!create_device(g_app.hwnd))
    {
        destroy_device();
        DestroyWindow(g_app.hwnd);
        UnregisterClassW(WINDOW_CLASS, inst);
        return 2;
    }

    DragAcceptFiles(g_app.hwnd, TRUE);
    ShowWindow(g_app.hwnd, SW_SHOWDEFAULT);
    UpdateWindow(g_app.hwnd);

    log_line("app: device and window ready");

    // IMGUI_DISABLE_DEFAULT_ALLOCATORS leaves imgui without an allocator until
    // this is called - CreateContext would hand back a null context.
    ImGui::SetAllocatorFunctions(
        [](size_t size, void*) -> void* { return memalloc((int)size); },
        [](void* ptr, void*) { memfree(ptr); });

    ImGui::CreateContext();

    g_clipboard_utf8.init();
    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    platform_io.Platform_GetClipboardTextFn = clipboard_get;
    platform_io.Platform_SetClipboardTextFn = clipboard_set;

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = 0;  // file functions are disabled in imconfig
    io.LogFilename = 0;

    build_fonts();
    log_line("app: fonts built");
    // What the person chose, before the first frame is drawn - otherwise the
    // client flashes discord's colours and then changes to theirs.
    theme::load();
    theme_apply();

    ImGui_ImplWin32_Init(g_app.hwnd);
    ImGui_ImplDX11_Init(g_app.device, g_app.context);
    log_line("app: imgui backends ready");

    // Before anything draws, so the first frame is already in the right
    // language rather than switching under the person a moment later.
    lang::init();

    science::init();
    ui_init();
    log_line("app: entering main loop");

    g_app.running = true;

    // When input was last seen. A window is only "at rest" once the cursor has
    // stopped for a moment - hover highlights and a blinking caret are still
    // animation, and cutting the rate the instant the mouse stops would show
    // as the interface going sticky under the hand.
    unsigned long long last_input = GetTickCount64();

    while (g_app.running)
    {
        MSG msg;
        bool had_input = false;

        while (PeekMessageW(&msg, 0, 0, 0, PM_REMOVE))
        {
            if (msg.message != WM_TIMER && msg.message != WM_PAINT) had_input = true;

            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) g_app.running = false;
        }
        if (!g_app.running) break;

        if (had_input) last_input = GetTickCount64();

        // Three speeds, and which one applies is decided here.
        //
        // Under the hand: whatever the monitor does, because that is the only
        // case where the refresh rate is the thing being judged.
        //
        // Watching a picture with nobody touching anything: sixty. A screen
        // share arrives at thirty frames a second and a camera at less, so a
        // hundred and forty four rebuilds of the whole interface produce the
        // same picture four times over. That is the bulk of what watching a
        // share costs on this side, and none of it is the video.
        //
        // Nothing at all: wait for an event. Any message ends the wait, so the
        // first thing the mouse does wakes it and nothing feels sticky.
        bool idle = GetTickCount64() - last_input > 400;

        if (idle)
        {
            DWORD wait = ui_wants_redraw() ? 16 : 60;
            MsgWaitForMultipleObjects(0, 0, FALSE, wait, QS_ALLINPUT);
        }

        if (g_app.occluded && g_app.swapchain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED)
        {
            Sleep(10);
            continue;
        }
        g_app.occluded = false;

        if (g_app.resize_w != 0 && g_app.resize_h != 0)
        {
            release_rtv();
            g_app.swapchain->ResizeBuffers(0, g_app.resize_w, g_app.resize_h, DXGI_FORMAT_UNKNOWN, 0);
            g_app.resize_w = 0;
            g_app.resize_h = 0;
            create_rtv();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ui_frame();

        ImGui::Render();

        const float clear[4] = { 0.129f, 0.133f, 0.145f, 1.0f };
        g_app.context->OMSetRenderTargets(1, &g_app.rtv, 0);
        g_app.context->ClearRenderTargetView(g_app.rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        HRESULT hr = g_app.swapchain->Present(1, 0);
        g_app.occluded = (hr == DXGI_STATUS_OCCLUDED);
    }

    ui_shutdown();

    // Before the network goes, so the last batch is not simply dropped.
    science::shutdown();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    g_clipboard_utf8.free_buffer();

    destroy_device();
    DestroyWindow(g_app.hwnd);
    UnregisterClassW(WINDOW_CLASS, inst);

    return 0;
}
