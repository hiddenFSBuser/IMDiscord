#pragma once
#include "imgui.h"

struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;
struct ID3D11RenderTargetView;

struct app_state
{
    HWND hwnd;
    ID3D11Device* device;
    ID3D11DeviceContext* context;
    IDXGISwapChain* swapchain;
    ID3D11RenderTargetView* rtv;

    bool running;
    bool occluded;
    unsigned int resize_w;
    unsigned int resize_h;

    ImFont* font_text;
    ImFont* font_bold;
    ImFont* font_big;
    ImFont* font_icon;

    // Files dropped onto the window since the last frame. Consumed by the chat
    // view, which turns them into pending attachments.
    ulist<wchar_t*> dropped_files;
};

extern app_state g_app;

int app_main();
void app_request_close();

// Builds the font atlas into the current imgui context. Called during
// start-up, and by --fonttest, which needs the atlas but no window.
void app_build_fonts();
