#pragma once
#include <d3d11.h>
#include <d3dcompiler.h>

// Direct3D loaded by hand instead of by the import table.
//
// Two reasons. The first is that a missing d3d11.dll or d3dcompiler is a
// message rather than a process that refuses to start with a system dialog
// naming a file the person has never heard of. The second is that the import
// table is the first thing anything inspecting a binary reads, and a client
// that only draws when there is something to draw has no business announcing
// a graphics stack before it has decided it needs one.
//
// The shader compiler is worse than the rest: its name carries a version
// number, and which numbers exist depends on what happens to be installed.
// Several are tried.

struct ID3D10Blob;

typedef HRESULT (WINAPI *pfn_D3D11CreateDeviceAndSwapChain)(
    IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
    const D3D_FEATURE_LEVEL*, UINT, UINT,
    const DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**,
    ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);

typedef HRESULT (WINAPI *pfn_D3DCompile)(
    LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*,
    LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);

namespace gfx
{
    // Brings in what is needed to open a window. False when direct3d is not on
    // this machine at all, with why() saying so.
    bool load();
    void unload();

    pfn_D3D11CreateDeviceAndSwapChain create_device();

    // Null when no shader compiler was found. The caller decides whether that
    // is fatal.
    pfn_D3DCompile compile();

    const char* why();

    // Which compiler was found, for the log.
    const char* compiler_name();
}
