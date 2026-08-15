#include "pch.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include "gfxload.h"
#include "log.h"

namespace
{
    HMODULE g_d3d11 = 0;
    HMODULE g_compiler = 0;

    pfn_D3D11CreateDeviceAndSwapChain g_create = 0;
    pfn_D3DCompile g_compile = 0;

    char g_why[192] = { 0 };
    char g_compiler_name[64] = { 0 };

    // Every name the shader compiler has shipped under that is still worth
    // asking for. 47 is what every current SDK redistributes and what windows
    // itself carries; the older ones are only found on a machine that once had
    // an older sdk or an older game on it, and cost nothing to try.
    const wchar_t* COMPILERS[] = {
        L"d3dcompiler_47.dll",
        L"d3dcompiler_46.dll",
        L"d3dcompiler_43.dll",
        L"d3dcompiler_42.dll",
    };
}

namespace
{
    // Loading a system library by bare name would take one sitting next to the
    // exe first, which is a way to be handed somebody else's code. The search
    // flag that prevents it is Windows 8, and Windows 7 only with an update
    // installed - so where it is refused, the full path is spelled out
    // instead. Same guarantee, older mechanism.
    HMODULE load_system(const wchar_t* name)
    {
        HMODULE lib = LoadLibraryExW(name, 0, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (lib) return lib;

        if (GetLastError() != ERROR_INVALID_PARAMETER) return 0;

        wchar_t path[MAX_PATH];
        UINT len = GetSystemDirectoryW(path, MAX_PATH);
        if (!len || len >= MAX_PATH - 2) return 0;

        path[len] = L'\\';
        path[len + 1] = 0;

        for (int i = 0; name[i] && len + 1 + i < MAX_PATH - 1; i++)
        {
            path[len + 1 + i] = name[i];
            path[len + 2 + i] = 0;
        }

        return LoadLibraryW(path);
    }
}

bool gfx::load()
{
    if (g_create) return true;

    g_why[0] = 0;

    // LOAD_LIBRARY_SEARCH_SYSTEM32 keeps this to the real system copy. Loading
    // a graphics dll by bare name would take one sitting next to the exe
    // first, which is a way to be handed somebody else's code.
    g_d3d11 = load_system(L"d3d11.dll");
    if (!g_d3d11)
    {
        ccstrncpy(g_why, "в системе нет d3d11.dll", sizeof(g_why) - 1);
        log_line("gfx: %s", g_why);
        return false;
    }

    g_create = (pfn_D3D11CreateDeviceAndSwapChain)
        GetProcAddress(g_d3d11, "D3D11CreateDeviceAndSwapChain");

    if (!g_create)
    {
        ccstrncpy(g_why, "d3d11.dll без нужной точки входа", sizeof(g_why) - 1);
        log_line("gfx: %s", g_why);
        FreeLibrary(g_d3d11);
        g_d3d11 = 0;
        return false;
    }

    for (unsigned int i = 0; i < sizeof(COMPILERS) / sizeof(COMPILERS[0]) && !g_compile; i++)
    {
        HMODULE lib = load_system(COMPILERS[i]);
        if (!lib) continue;

        pfn_D3DCompile fn = (pfn_D3DCompile)GetProcAddress(lib, "D3DCompile");
        if (!fn) { FreeLibrary(lib); continue; }

        g_compiler = lib;
        g_compile = fn;
        wcstochar(COMPILERS[i], g_compiler_name, (int)sizeof(g_compiler_name));
    }

    log_line("gfx: d3d11 загружен вручную, компилятор шейдеров %s",
             g_compiler_name[0] ? g_compiler_name : "не найден");
    return true;
}

void gfx::unload()
{
    g_create = 0;
    g_compile = 0;

    if (g_compiler) { FreeLibrary(g_compiler); g_compiler = 0; }
    if (g_d3d11) { FreeLibrary(g_d3d11); g_d3d11 = 0; }
}

pfn_D3D11CreateDeviceAndSwapChain gfx::create_device() { return g_create; }
pfn_D3DCompile gfx::compile() { return g_compile; }
const char* gfx::why() { return g_why; }
const char* gfx::compiler_name() { return g_compiler_name; }
